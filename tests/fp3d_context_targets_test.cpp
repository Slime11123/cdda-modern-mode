#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <sstream>
#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cata_catch.h"
#include "fp3d_snapshot.h"
#include "game.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "cata_scope_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "item.h"

TEST_CASE( "fp3d_stair_wheel_uses_terrain_flags_and_requires_standing_on_stairs",
           "[fp3d][interaction][stairs]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto standing = you.pos_bub( here );
    for( const auto &id : { "t_stairs_up", "t_stairs_down", "t_floor" } ) {
        here.ter_set( standing, ter_str_id( id ) );
        const fp3d_targeting::target target{ you.pos_abs(), you.pos_abs(),
                                            fp3d_targeting::kind::terrain, id };
        const auto clock = calendar::turn;
        const auto engine = rng_get_engine();
        const int moves = you.get_moves();
        const auto choices = fp3d_interaction::context_entries( target, you, here );
        const auto up = fp3d_interaction::resolve( "FP3D_STAIRS_UP", target, you, here );
        const auto down = fp3d_interaction::resolve( "FP3D_STAIRS_DOWN", target, you, here );
        CHECK( static_cast<bool>( up ) == ( std::string( id ) == "t_stairs_up" ) );
        CHECK( static_cast<bool>( down ) == ( std::string( id ) == "t_stairs_down" ) );
        if( up ) { CHECK( up->action == ACTION_MOVE_UP ); }
        if( down ) { CHECK( down->action == ACTION_MOVE_DOWN ); }
        if( std::string( id ) != "t_floor" ) {
            CHECK( choices[0].visible );
            CHECK( choices[1].visible );
            CHECK( choices[0].enabled == static_cast<bool>( up ) );
            CHECK( choices[1].enabled == static_cast<bool>( down ) );
            CHECK_FALSE( choices[2].visible );
        }
        CHECK( calendar::turn == clock );
        CHECK( you.get_moves() == moves );
        CHECK( rng_get_engine() == engine );
    }
    const auto adjacent = standing + tripoint_rel_ms::north;
    here.ter_set( adjacent, ter_str_id( "t_stairs_up" ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    const fp3d_targeting::target target{ you.pos_abs(), here.get_abs( adjacent ),
                                        fp3d_targeting::kind::terrain, "t_stairs_up" };
    const auto choices = fp3d_interaction::context_entries( target, you, here );
    CHECK_FALSE( choices[0].enabled );
    CHECK_FALSE( choices[1].enabled );
    REQUIRE( choices[2].enabled );
    CHECK( choices[2].action == ACTION_MOVE_FORTH );
    CHECK_FALSE( fp3d_interaction::resolve( "FP3D_STAIRS_UP", target, you, here ) );
    const auto scene = fp3d_snapshot::capture( you, here );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      target.position ) != scene.interactable.end() );
}

TEST_CASE( "fp3d_stair_requests_use_native_vertical_movement", "[fp3d][interaction][stairs]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto origin = you.pos_abs();
    here.ter_set( you.pos_bub( here ), ter_str_id( "t_stairs_up" ) );
    here.ter_set( you.pos_bub( here ) + tripoint_rel_ms::above, ter_str_id( "t_stairs_down" ) );
    here.build_map_cache( 0 );
    const fp3d_targeting::target target{ origin, origin, fp3d_targeting::kind::terrain, "t_stairs_up" };
    const auto up = fp3d_interaction::resolve( "FP3D_STAIRS_UP", target, you, here );
    REQUIRE( up );
    REQUIRE( up->action == ACTION_MOVE_UP );
    const int moves = you.get_moves();
    g->vertical_move( 1, false );
    CHECK( you.pos_abs() == origin + tripoint_rel_ms::above );
    CHECK( you.get_moves() < moves );
    CHECK_FALSE( fp3d_interaction::resolve( "FP3D_STAIRS_UP", target, you, here ) );
    const fp3d_targeting::target upper{ you.pos_abs(), you.pos_abs(),
                                       fp3d_targeting::kind::terrain, "t_stairs_down" };
    const auto down = fp3d_interaction::resolve( "FP3D_STAIRS_DOWN", upper, you, here );
    REQUIRE( down );
    REQUIRE( down->action == ACTION_MOVE_DOWN );
    g->vertical_move( -1, false );
    CHECK( you.pos_abs() == origin );
}

TEST_CASE( "fp3d_object_context_contains_object_actions_and_never_npc_commands",
           "[fp3d][interaction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto cabinet = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.ter_set( cabinet, ter_str_id( "t_floor" ) );
    here.furn_set( cabinet, furn_str_id( "f_locker" ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    const auto engine = rng_get_engine();
    const int moves = you.get_moves();
    const auto clock = calendar::turn;
    const fp3d_targeting::target target{ you.pos_abs(), here.get_abs( cabinet ),
                                         fp3d_targeting::kind::terrain, here.ter( cabinet ).id().str() };
    const auto choices = fp3d_interaction::context_entries( target, you, here );
    REQUIRE( choices[3].visible );
    CHECK( choices[3].enabled );
    CHECK_FALSE( choices[2].visible );
    CHECK_FALSE( choices[5].visible );
    CHECK_FALSE( choices[6].visible );
    CHECK( rng_get_engine() == engine );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );
}

TEST_CASE( "fp3d_snapshot_interaction_affordances_only_describe_visible_nearby_targets",
           "[fp3d][interaction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    here.ter_set( you.pos_bub( here ), ter_str_id( "t_floor" ) );
    const auto nearby = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto distant = you.pos_bub( here ) + tripoint_rel_ms( 0, -5, 0 );
    here.ter_set( nearby, ter_str_id( "t_floor" ) );
    here.furn_set( nearby, furn_str_id( "f_locker" ) );
    here.furn_set( distant, furn_str_id( "f_locker" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const int moves = you.get_moves();
    const auto scene = fp3d_snapshot::capture( you, here );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      here.get_abs( nearby ) ) != scene.interactable.end() );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      here.get_abs( distant ) ) == scene.interactable.end() );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      you.pos_abs() ) == scene.interactable.end() );
    CHECK( scene.interactable.size() <= 9 );
    for( const auto &position : scene.interactable ) {
        CHECK( position.z() == you.posz() );
        CHECK( square_dist( position, you.pos_abs() ) <= 1 );
    }
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );
}

TEST_CASE( "fp3d_hidden_npc_does_not_leak_through_terrain_interaction_hints",
           "[fp3d][interaction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 64, 65 ), "test_talker" );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    here.ter_set( person.pos_bub( here ), ter_str_id( "t_floor" ) );
    here.furn_set( person.pos_bub( here ), furn_str_id( "f_null" ) );
    // Randomized NPC equipment may spill onto spawn. This case tests an
    // empty floor; visible ground items should still be interactable.
    here.i_clear( person.pos_bub( here ) );
    fp3d_snapshot::prepare_view( you, here );
    const fp3d_targeting::target terrain{ you.pos_abs(), person.pos_abs(),
                                          fp3d_targeting::kind::terrain, "t_floor" };
    REQUIRE( you.sees( here, person ) );
    REQUIRE( fp3d_interaction::context_entries( terrain, you, here )[2].visible );
    person.toggle_trait( trait_id( "DEBUG_CLOAK" ) );
    REQUIRE( person.is_invisible() );
    REQUIRE_FALSE( you.sees( here, person ) );
    REQUIRE( you.sees( here, person.pos_bub( here ) ) );
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const int moves = you.get_moves();
    const auto choices = fp3d_interaction::context_entries( terrain, you, here );
    for( int index = 0; index < 7; ++index ) {
        CHECK_FALSE( choices[index].visible );
    }
    const auto scene = fp3d_snapshot::capture( you, here );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      person.pos_abs() ) == scene.interactable.end() );
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );
}
TEST_CASE( "modern_2d_context_options_follow_target_and_revalidate", "[fp3d][context_2d]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto options_for = [&]() {
        fp3d_snapshot::prepare_view( you, here );
        return fp3d_interaction::context_2d_entries( p, you, here );
    };
    const auto find_action = []( const auto &options, action_id action ) {
        return std::find_if( options.begin(), options.end(), [&]( const auto &row ) { return row.action == action; } );
    };
    here.ter_set( p, ter_str_id( "t_door_c" ) );
    auto options = options_for();
    REQUIRE_FALSE( options.empty() );
    CHECK( std::string( options.front().label ) == "前往此处" );
    const auto open = find_action( options, ACTION_OPEN );
    REQUIRE( open != options.end() );
    REQUIRE( open->enabled );
    const auto opening = *open;
    const auto clock = calendar::turn;
    const auto random = rng_get_engine();
    const auto moves = you.get_moves();
    const auto origin = you.pos_abs();
    const auto request = fp3d_interaction::resolve_2d_choice( opening, p, you, here );
    REQUIRE( request );
    CHECK( request->action == ACTION_OPEN );
    CHECK( request->position == p );
    CHECK( here.ter( p ) == ter_str_id( "t_door_c" ).id() );
    CHECK( calendar::turn == clock );
    CHECK( rng_get_engine() == random );
    CHECK( you.get_moves() == moves );
    CHECK( you.pos_abs() == origin );

    here.ter_set( p, ter_str_id( "t_door_o" ) );
    options = options_for();
    CHECK( find_action( options, ACTION_OPEN ) == options.end() );
    CHECK( find_action( options, ACTION_CLOSE ) != options.end() );
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( opening, p, you, here ) );

    here.ter_set( p, ter_str_id( "t_floor" ) );
    here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
    options = options_for();
    REQUIRE( find_action( options, ACTION_PICKUP ) != options.end() );
    const auto pickup = *find_action( options, ACTION_PICKUP );
    here.i_clear( p );
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( pickup, p, you, here ) );

    npc &person = spawn_npc( p.xy(), "test_talker" );
    on_out_of_scope cleanup( []() { clear_npcs(); } );
    options = options_for();
    CHECK( find_action( options, ACTION_CHAT ) != options.end() );
    person.toggle_trait( trait_id( "DEBUG_CLOAK" ) );
    options = options_for();
    CHECK( find_action( options, ACTION_CHAT ) == options.end() );

    you.add_effect( efftype_id( "blind" ), 1_hours );
    CHECK( options_for().empty() );
}

TEST_CASE( "modern_2d_context_travel_schedules_native_path_without_teleporting", "[fp3d][context_2d]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto destination = you.pos_bub( here ) + tripoint_rel_ms( 4, 0, 0 );
    fp3d_snapshot::prepare_view( you, here );
    const auto options = fp3d_interaction::context_2d_entries( destination, you, here );
    REQUIRE_FALSE( options.empty() );
    REQUIRE( options.front().enabled );
    const auto origin = you.pos_abs();
    const auto moves = you.get_moves();
    const auto time = calendar::turn;
    const auto request = fp3d_interaction::resolve_2d_choice( options.front(), destination, you, here );
    REQUIRE( request );
    CHECK( request->action != ACTION_NULL );
    CHECK( you.pos_abs() == origin );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == time );
    CHECK_FALSE( you.get_auto_move_route().empty() );
    you.clear_destination();
    CHECK_FALSE( fp3d_interaction::context_2d_entries( you.pos_bub( here ), you, here ).front().enabled );
    here.ter_set( destination, ter_str_id( "t_wall" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto wall = fp3d_interaction::context_2d_entries( destination, you, here );
    REQUIRE_FALSE( wall.empty() );
    CHECK_FALSE( wall.front().enabled );
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( wall.front(), destination, you, here ) );
}

TEST_CASE( "modern_2d_context_aim_passes_clicked_tile_to_native_aiming", "[fp3d][context_2d]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto target = you.pos_bub( here ) + tripoint_rel_ms( 4, 0, 0 );
    fp3d_snapshot::prepare_view( you, here );
    const auto is_aim = []( const auto &entry ) { return entry.action == ACTION_FIRE; };

    REQUIRE_FALSE( you.is_armed() );
    auto options = fp3d_interaction::context_2d_entries( target, you, here );
    REQUIRE_FALSE( options.empty() );
    CHECK( std::none_of( options.begin(), options.end(), is_aim ) );

    you.set_wielded_item( item( itype_id( "shortbow" ) ) );
    options = fp3d_interaction::context_2d_entries( target, you, here );
    CHECK( std::any_of( options.begin(), options.end(), is_aim ) );

    item gun( itype_id( "glock_19" ) );
    gun.ammo_set( itype_id( "9mm" ), 10 );
    you.set_wielded_item( gun );
    options = fp3d_interaction::context_2d_entries( target, you, here );
    const auto aim = std::find_if( options.begin(), options.end(), is_aim );
    REQUIRE( aim != options.end() );
    CHECK( std::string( options.front().label ) == "前往此处" );
    const auto intent = fp3d_interaction::resolve_2d_choice( *aim, target, you, here );
    REQUIRE( intent );
    REQUIRE( intent->position == target );
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( *aim, you.pos_bub( here ), you, here ) );
    const auto moves = you.get_moves();
    const auto clock = calendar::turn;
    const auto last_target = you.last_target_pos;
    const auto clicked = here.get_abs( *intent->position );
    avatar_action::fire_wielded_weapon( you, clicked );
    REQUIRE( you.has_activity( activity_id( "ACT_AIM" ) ) );
    std::ostringstream saved;
    JsonOut json( saved );
    you.activity.serialize( json );
    const auto data = json_loader::from_string( saved.str() ).get_object();
    data.allow_omitted_members();
    const auto wrapper = data.get_object( "actor" );
    wrapper.allow_omitted_members();
    const auto actor = wrapper.get_object( "actor_data" );
    actor.allow_omitted_members();
    tripoint_abs_ms initial_target;
    REQUIRE( actor.read( "initial_target", initial_target ) );
    CHECK( initial_target == clicked );
    CHECK( you.last_target_pos == last_target );
    CHECK( you.get_wielded_item()->ammo_remaining() == 10 );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );

    you.cancel_activity();
    you.remove_weapon();
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( *aim, target, you, here ) );
}
#endif
