#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "fp3d_inventory.h"
#include "fp3d_snapshot.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "rng.h"

TEST_CASE( "fp3d_direction_selection_keeps_waiting_until_a_reachable_object_is_selected",
           "[fp3d][interaction]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto near = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto far = near + tripoint_rel_ms::north;
    const furn_str_id rack( "f_rack" );
    here.furn_set( near, rack );
    here.furn_set( far, rack );
    fp3d_snapshot::prepare_view( you, here );
    const fp3d_targeting::target furniture{
        you.pos_abs(), here.get_abs( near ), fp3d_targeting::kind::furniture, rack.str()
    };
    const fp3d_targeting::target too_far{
        you.pos_abs(), here.get_abs( far ), fp3d_targeting::kind::furniture, rack.str()
    };
    REQUIRE( fp3d_interaction::begin_direction_selection( ACTION_GRAB ) );
    CHECK( fp3d_interaction::direction_selection_accepts( furniture, you, here ) );
    CHECK_FALSE( fp3d_interaction::direction_selection_accepts( too_far, you, here ) );
    CHECK_FALSE( fp3d_interaction::confirm_direction_selection( std::nullopt, you, here, true ) );
    CHECK_FALSE( fp3d_interaction::confirm_direction_selection( too_far, you, here, true ) );
    CHECK( fp3d_interaction::direction_selection_pending() );
    const auto selection = fp3d_interaction::confirm_direction_selection( furniture, you, here, true );
    REQUIRE( selection );
    CHECK( selection->action == ACTION_GRAB );
    CHECK( selection->position == near );
    CHECK_FALSE( fp3d_interaction::direction_selection_pending() );
    CHECK_FALSE( fp3d_interaction::confirm_direction_selection( furniture, you, here, true ) );

    REQUIRE( fp3d_interaction::begin_direction_selection( ACTION_GRAB ) );
    const auto reticle = fp3d_interaction::resolve( "FP3D_INTERACT", furniture, you, here );
    REQUIRE( reticle );
    CHECK( reticle->position == near );
}

TEST_CASE( "fp3d_pickup_sources_include_empty_and_stocked_display_furniture",
           "[fp3d][inventory][pickup]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto pos = you.pos_bub( here );
    const auto rack = pos + tripoint_rel_ms::north;
    const auto case_open = pos + tripoint_rel_ms::east;
    const auto case_sealed = pos + tripoint_rel_ms::west;
    here.furn_set( rack, furn_str_id( "f_rack" ) );
    here.furn_set( case_open, furn_str_id( "f_displaycase_o" ) );
    here.furn_set( case_sealed, furn_str_id( "f_displaycase" ) );
    here.add_item_or_charges( case_open, item( itype_id( "bandages" ) ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto view = fp3d_inventory::capture( you, here );
    const auto source = [&]( const tripoint_bub_ms &p ) {
        return std::find_if( view.groups.begin(), view.groups.end(), [&]( const auto &group ) {
            return group.tile == here.get_abs( p ) && !group.source_sprite.empty();
        } );
    };
    REQUIRE( source( rack ) != view.groups.end() );
    CHECK( source( rack )->source_sprite == "f_rack" );
    CHECK( source( rack )->entries.empty() );
    REQUIRE( source( case_open ) != view.groups.end() );
    CHECK( source( case_open )->source_sprite == "f_displaycase_o" );
    CHECK( source( case_open )->entries.size() == 1 );
    CHECK( source( case_sealed ) == view.groups.end() );
}

TEST_CASE( "right_click_closed_curtains_requests_a_targeted_peek",
           "[fp3d][interaction][pickup]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms room( 65, 65, 0 );
    const auto window = room + tripoint_rel_ms::north;
    you.setpos( here, room );
    here.ter_set( room, ter_str_id( "t_floor" ) );
    here.ter_set( window, ter_str_id( "t_curtains" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto entries = fp3d_interaction::context_2d_entries( window, you, here );
    const auto choice = std::find_if( entries.begin(), entries.end(), []( const auto &entry ) {
        return entry.action == ACTION_PEEK;
    } );
    REQUIRE( choice != entries.end() );
    const auto request = fp3d_interaction::resolve_2d_choice( *choice, window, you, here );
    REQUIRE( request );
    CHECK( request->action == ACTION_PEEK );
    CHECK( request->position == window );
}

TEST_CASE( "fp3d_E_single_item_uses_native_automatic_pockets_and_keeps_charges",
           "[fp3d][interaction][pickup]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    item object( itype_id( "aspirin" ) );
    object.charges = 7;
    here.add_item_or_charges( p, object );
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here );
    // The avatar's own tile sprite is published for the overhead camera only;
    // world content here is exactly the one ground item.
    REQUIRE( std::count_if( scene.sprites.begin(), scene.sprites.end(),
    []( const auto & sprite ) {
        return !sprite.is_avatar;
    } ) == 1 );
    CHECK_FALSE( scene.sprites.front().multiple_items );
    const auto target = fp3d_targeting::ray( scene, 0, 55 );
    REQUIRE( target );
    const auto before_time = calendar::turn;
    const auto before_moves = you.get_moves();
    const auto before_rng = rng_get_engine();
    const auto request = fp3d_interaction::resolve( "FP3D_INTERACT", target, you, here );
    REQUIRE( request );
    CHECK( request->action == ACTION_PICKUP );
    CHECK( request->quick_pickup );
    CHECK( request->targeted_pickup );
    CHECK_FALSE( you.activity );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    CHECK( rng_get_engine() == before_rng );
    REQUIRE( fp3d_interaction::quick_pickup( you, here, p, true ) );
    process_activity( you );
    CHECK( here.i_at( p ).empty() );
    const auto held = you.all_items_loc();
    const auto found = std::find_if( held.begin(), held.end(), []( const item_location & loc ) {
        return loc && loc->typeId() == itype_id( "aspirin" );
    } );
    REQUIRE( found != held.end() );
    CHECK( ( *found )->charges == 7 );
    REQUIRE( found->has_parent() );
    CHECK( found->parent_item()->typeId() == itype_id( "backpack" ) );
}

TEST_CASE( "fp3d_pickup_view_limits_sources_and_tracks_native_multiple_item_visibility",
           "[fp3d][interaction][pickup]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto other = you.pos_bub( here ) + tripoint_rel_ms::east;
    here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
    here.add_item_or_charges( p, item( itype_id( "bandages" ) ) );
    here.add_item_or_charges( other, item( itype_id( "crowbar" ) ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto before_time = calendar::turn;
    const auto before_moves = you.get_moves();
    const auto before_rng = rng_get_engine();
    const auto view = fp3d_inventory::capture( you, here, here.get_abs( p ) );
    int nearby_items = 0;
    bool backpack = false;
    for( const auto &entry : view.entries ) {
        if( entry.nearby ) {
            CHECK( entry.location.pos_bub( here ) == p );
            CHECK( entry.location->typeId() != itype_id( "crowbar" ) );
            ++nearby_items;
        } else {
            backpack = backpack || entry.location->typeId() == itype_id( "backpack" );
        }
    }
    CHECK( nearby_items == 2 );
    CHECK( backpack );
    const auto image_at = [&]( const fp3d_snapshot::scene_snapshot & scene ) {
        return std::find_if( scene.sprites.begin(), scene.sprites.end(), [&]( const auto & image ) {
            return image.kind == fp3d_snapshot::sprite_kind::item && image.position == here.get_abs( p );
        } );
    };
    auto scene = fp3d_snapshot::capture( you, here );
    REQUIRE( image_at( scene ) != scene.sprites.end() );
    CHECK( image_at( scene )->multiple_items );
    const auto target = fp3d_targeting::ray( scene, 0, 55 );
    REQUIRE( target );
    const auto request = fp3d_interaction::resolve( "FP3D_INTERACT", target, you, here );
    REQUIRE( request );
    CHECK( request->action == ACTION_PICKUP );
    CHECK( request->quick_pickup );
    CHECK( request->targeted_pickup );
    const auto quick_request = fp3d_interaction::resolve( "FP3D_PICKUP", target, you, here );
    REQUIRE( quick_request );
    CHECK( quick_request->targeted_pickup );
    here.i_at( p ).erase( here.i_at( p ).begin() );
    scene = fp3d_snapshot::capture( you, here );
    REQUIRE( image_at( scene ) != scene.sprites.end() );
    CHECK_FALSE( image_at( scene )->multiple_items );
    here.furn_set( p, furn_str_id( "f_crate_c" ) );
    fp3d_snapshot::prepare_view( you, here );
    scene = fp3d_snapshot::capture( you, here );
    CHECK( image_at( scene ) == scene.sprites.end() );
    const auto closed = fp3d_inventory::capture( you, here, here.get_abs( p ) );
    CHECK( std::none_of( closed.entries.begin(), closed.entries.end(), []( const auto & entry ) {
        return entry.nearby;
    } ) );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    CHECK( rng_get_engine() == before_rng );
}
#endif
