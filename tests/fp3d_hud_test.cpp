#include "fp3d_hud.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <algorithm>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "debug.h"
#include "display.h"
#include "item.h"
#include "effect.h"
#include "output.h"
#include "fp3d_entry.h"
#include "fp3d_snapshot.h"
#include "fp3d_targeting.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "translations.h"

TEST_CASE( "fp3d_hud_inspects_only_the_currently_visible_target", "[fp3d][hud]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto near = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto far = near + tripoint_rel_ms::north;
    here.ter_set( near, ter_str_id( "t_pavement" ) );
    here.ter_set( far, ter_str_id( "t_pavement" ) );
    fp3d_snapshot::prepare_view( you, here );
    const fp3d_targeting::target road{ you.pos_abs(), here.get_abs( near ),
                                     fp3d_targeting::kind::terrain, "t_pavement" };
    const auto lines = fp3d_hud::inspect( you, here, road );
    REQUIRE( lines.size() >= 6 );
    CHECK_FALSE( lines.front().text.empty() );
    const auto has_text = []( const auto &details, const std::string &text ) {
        return std::any_of( details.begin(), details.end(), [&]( const auto &line ) {
            return line.text == text;
        } );
    };
    CHECK( has_text( lines, here.ter( near )->description.translated() ) );
    CHECK( has_text( lines, string_format( _( "Concealment: %d%%" ), here.coverage( near ) ) ) );
    CHECK( has_text( lines, string_format( _( "Move cost: %d" ), here.move_cost( near ) * 50 ) ) );
    CHECK( lines.back().text.find( _( "Lighting: " ) ) == 0 );

    const item rock( itype_id( "rock" ) );
    here.add_item_or_charges( near, rock );
    const fp3d_targeting::target object{ you.pos_abs(), here.get_abs( near ),
                                        fp3d_targeting::kind::item, "rock" };
    CHECK( has_text( fp3d_hud::inspect( you, here, object ), rock.tname() ) );
    here.furn_set( near, furn_str_id( "f_crate_c" ) );
    fp3d_snapshot::prepare_view( you, here );
    CHECK_FALSE( has_text( fp3d_hud::inspect( you, here, object ), rock.tname() ) );

    here.furn_set( near, furn_str_id( "f_null" ) );
    here.ter_set( near, ter_str_id( "t_wall" ) );
    fp3d_snapshot::prepare_view( you, here );
    const fp3d_targeting::target hidden{ you.pos_abs(), here.get_abs( far ),
                                        fp3d_targeting::kind::terrain, "t_pavement" };
    CHECK( fp3d_hud::inspect( you, here, hidden ).empty() );
    you.setpos( here, you.pos_bub( here ) + tripoint_rel_ms::east );
    CHECK( fp3d_hud::inspect( you, here, road ).empty() );
}

TEST_CASE( "fp3d_hud_body_health_keeps_limbs_independent_and_native_colors", "[fp3d][hud]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    you.setpos( get_map(), tripoint_bub_ms( 65, 65, 0 ) );
    const std::array<bodypart_id, 6> parts = { body_part_head, body_part_torso,
                                             body_part_arm_l, body_part_arm_r, body_part_leg_l, body_part_leg_r };
    const std::array<int, 6> hp = { 100, 87, 62, 37, 12, 0 };
    const std::array<nc_color, 6> colors = { c_green, c_light_green, c_yellow, c_light_red, c_red, c_red };
    for( size_t i = 0; i < parts.size(); ++i ) {
        you.set_part_hp_max( parts[i], 100 );
        you.set_part_hp_cur( parts[i], hp[i] );
    }
    const auto before_turn = calendar::turn;
    const auto before_moves = you.get_moves();
    const auto before_rng = rng_get_engine();
    const auto hud = fp3d_hud::capture( you );
    REQUIRE( hud.body_regions.size() == parts.size() );
    for( size_t i = 0; i < parts.size(); ++i ) {
        CAPTURE( i );
        const auto &region = hud.body_regions[i];
        CHECK( region.label == body_part_name( parts[i] ) );
        CHECK( region.current == hp[i] );
        CHECK( region.maximum == 100 );
        CHECK( region.color == colors[i] );
        CHECK( region.color == display::get_bodygraph_bp_color( you, parts[i], bodygraph_var::hp ) );
        CHECK( you.get_part_hp_cur( parts[i] ) == hp[i] );
    }
    CHECK( calendar::turn == before_turn );
    CHECK( you.get_moves() == before_moves );
    CHECK( rng_get_engine() == before_rng );

    // Healing one leg must neither change nor mask the other leg's damage.
    you.set_part_hp_cur( body_part_leg_r, 100 );
    const auto healed = fp3d_hud::capture( you );
    REQUIRE( healed.body_regions.size() == parts.size() );
    CHECK( healed.body_regions[4].current == 12 );
    CHECK( healed.body_regions[4].color == c_red );
    CHECK( healed.body_regions[5].current == 100 );
    CHECK( healed.body_regions[5].color == c_green );

    // Native bodygraph threshold boundaries at exactly 12.5/37.5/62.5/87.5 percent.
    you.set_part_hp_max( body_part_head, 80 );
    const std::array<int, 4> boundaries = { 10, 30, 50, 70 };
    const std::array<nc_color, 4> boundary_colors = { c_light_red, c_yellow, c_light_green, c_green };
    for( size_t i = 0; i < boundaries.size(); ++i ) {
        CAPTURE( i );
        you.set_part_hp_cur( body_part_head, boundaries[i] );
        const auto boundary = fp3d_hud::capture( you );
        REQUIRE_FALSE( boundary.body_regions.empty() );
        CHECK( boundary.body_regions.front().color == boundary_colors[i] );
    }
}
TEST_CASE( "fp3d_displays_follow_native_character_information", "[fp3d][hud]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    restore_on_out_of_scope restore_debug( debug_mode );
    debug_mode = false;
    you.set_part_hp_max( body_part_arm_l, 100 );
    you.set_part_hp_cur( body_part_arm_l, 37 );
    you.set_pain( 35 );
    REQUIRE_FALSE( you.has_watch() );
    const auto ordinary = fp3d_hud::capture( you );
    CHECK( ordinary.temperature == "-" );
    CHECK( ordinary.day_time.find( to_string_time_of_day( calendar::turn ) ) == std::string::npos );
    CHECK( ordinary.status.find( std::to_string( you.get_stamina_max() ) ) == std::string::npos );
    CHECK( fp3d_hud::health_text( you, body_part_arm_l ).find( "37" ) == std::string::npos );
    CHECK( fp3d_hud::health_text( you, body_part_arm_l ).find( get_hp_bar( 37, 100 ).first ) == 0 );
    CHECK( ordinary.expanded.back().find( '/' ) == std::string::npos );
    CHECK( fp3d_entry::capture( you ).date.find( to_string_time_of_day( calendar::turn ) ) == std::string::npos );
    REQUIRE( you.wear_item( item( itype_id( "diving_watch" ) ), false ) );
    REQUIRE( you.has_watch() );
    const auto equipped = fp3d_hud::capture( you );
    CHECK( equipped.day_time.find( to_string_time_of_day( calendar::turn ) ) != std::string::npos );
    CHECK( equipped.temperature == display::get_temp( you ) );
    CHECK( equipped.temperature != "-" );
    CHECK( fp3d_entry::capture( you ).rows[3].second.find( equipped.temperature ) != std::string::npos );
    you.add_effect( efftype_id( "got_checked" ), 1_hours );
    const auto checked = fp3d_hud::capture( you );
    const auto pain = std::find_if( checked.status_entries.begin(), checked.status_entries.end(),
    []( const auto &entry ) { return entry.label == "疼痛"; } );
    REQUIRE( pain != checked.status_entries.end() );
    CHECK( pain->value == display::pain_text_color( you ).first );
    CHECK( pain->value.find( "35" ) != std::string::npos );
    debug_mode = true;
    CHECK( fp3d_hud::health_text( you, body_part_arm_l ) == "37/100" );
    you.toggle_trait( trait_id( "NOPAIN" ) );
    CHECK( fp3d_hud::health_text( you, body_part_arm_l ).find( "37" ) == std::string::npos );
}
#endif
