#include "fp3d_hud.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <vector>
#include "action.h"
#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "fp3d_input.h"
#include "game.h"
#include "input.h"
#include "map.h"
#include "map_helpers.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "overmap_ui.h"
#include "monster.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "rng.h"

TEST_CASE( "fp3d_navigation_display_preserves_native_route_and_move_costs", "[fp3d][navigation]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    g->set_safe_mode( SAFE_MODE_OFF );
    on_out_of_scope restore_mode( []() {
        g->set_safe_mode( SAFE_MODE_ON );
    } );
    const auto start = you.pos_bub( here );
    const std::vector<tripoint_bub_ms> route = {
        start + tripoint_rel_ms::north,
        start + tripoint_rel_ms( 0, -2, 0 ),
        start + tripoint_rel_ms( 0, -3, 0 )
    };
    for( const auto &p : route ) {
        here.ter_set( p, ter_str_id( "t_pavement" ) );
    }
    you.set_destination( route );
    you.omt_path = { you.pos_abs_omt() + tripoint_rel_omt( 3, 0, 0 ) };
    const auto old_omt_path = you.omt_path;
    you.set_moves( 1000 );
    const auto clock = calendar::turn;
    const auto engine = rng_get_engine();
    const auto hunger = you.get_hunger();
    const auto thirst = you.get_thirst();
    fp3d_hud::data hud;
    for( int frame = 0; frame < 1000; ++frame ) {
        hud = fp3d_hud::capture( you );
    }
    REQUIRE( hud.navigation_heading );
    CHECK( *hud.navigation_heading == Approx( 90.0f ) );
    CHECK( hud.navigation_distance == 3 );
    CHECK( you.get_auto_move_route() == route );
    CHECK( you.omt_path == old_omt_path );
    CHECK( you.pos_bub( here ) == start );
    CHECK( you.get_moves() == 1000 );
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( you.get_hunger() == hunger );
    CHECK( you.get_thirst() == thirst );
    for( const auto &position : route ) {
        REQUIRE( you.has_destination() );
        const auto action = you.get_next_auto_move_direction();
        REQUIRE( action != ACTION_NULL );
        const int before_moves = you.get_moves();
        const auto delta = get_delta_from_movement_action( action, iso_rotate::yes );
        REQUIRE( avatar_action::move( you, here, tripoint_rel_ms( delta, 0 ) ) );
        CHECK( you.pos_bub( here ) == position );
        CHECK( you.get_moves() < before_moves );
    }
    CHECK_FALSE( you.has_destination() );
    you.clear_destination();
}

TEST_CASE( "fp3d_native_auto_route_stops_at_enemy_without_attacking", "[fp3d][navigation]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    g->set_safe_mode( SAFE_MODE_OFF );
    on_out_of_scope restore_mode( []() {
        g->set_safe_mode( SAFE_MODE_ON );
    } );
    const auto start = you.pos_bub( here );
    monster &enemy = spawn_test_monster( "mon_zombie", start + tripoint_rel_ms::north );
    const int hp = enemy.get_hp();
    you.set_destination( { start + tripoint_rel_ms::north, start + tripoint_rel_ms( 0, -2, 0 ) } );
    you.set_moves( 1000 );
    const auto clock = calendar::turn;
    const auto engine = rng_get_engine();
    const auto action = you.get_next_auto_move_direction();
    const auto delta = get_delta_from_movement_action( action, iso_rotate::yes );
    CHECK_FALSE( avatar_action::move( you, here, tripoint_rel_ms( delta, 0 ) ) );
    CHECK_FALSE( you.has_destination() );
    CHECK_FALSE( you.is_auto_moving() );
    CHECK( you.pos_bub( here ) == start );
    CHECK( enemy.get_hp() == hp );
    CHECK( you.get_moves() == 1000 );
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
}

TEST_CASE( "fp3d_walk_commands_obey_native_safemode_and_laser_lock", "[fp3d][navigation]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    on_out_of_scope restore_mode( []() {
        g->set_safe_mode( SAFE_MODE_ON );
    } );
    SECTION( "native safe mode stops movement" ) {
        g->set_safe_mode( SAFE_MODE_STOP );
    }
    SECTION( "laser lock blocks even with normal safe mode off" ) {
        g->set_safe_mode( SAFE_MODE_OFF );
        you.add_effect( efftype_id( "laserlocked" ), 1_minutes );
    }
    you.set_moves( 1000 );
    const auto start = you.pos_abs();
    const auto clock = calendar::turn;
    const auto engine = rng_get_engine();
    const auto action = look_up_action( fp3d_input::resolve( fp3d_input::mode::first_person_3d,
                                        input_event( 'w', input_event_t::keyboard_code ), 0 ) );
    const auto delta = get_delta_from_movement_action( action, iso_rotate::yes );
    REQUIRE( delta != point_rel_ms::zero );
    CHECK_FALSE( avatar_action::move( you, here, tripoint_rel_ms( delta, 0 ) ) );
    CHECK( you.pos_abs() == start );
    CHECK( you.get_moves() == 1000 );
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
}

TEST_CASE( "fp3d_map_uses_native_travel_confirmation_and_activity_routes", "[fp3d][navigation]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    // clear_avatar preserves travel metadata; this fixture starts without an older route.
    you.abort_automove();
    you.cancel_activity();
    you.omt_path.clear();
    const auto source = you.pos_abs_omt();
    const auto destination = source + tripoint_rel_omt( 1, 0, 0 );
    struct saved_cell {
        tripoint_abs_omt position;
        oter_id terrain;
        om_vision_level vision;
    };
    std::vector<saved_cell> saved;
    for( const auto &position : {
             source, destination
         } ) {
        saved.push_back( { position, overmap_buffer.ter( position ), overmap_buffer.seen( position ) } );
        overmap_buffer.ter_set( position, oter_str_id( "road_nesw" ).id() );
        const auto existing = overmap_buffer.get_existing_om_global( position );
        REQUIRE( existing );
        existing.om->set_seen( existing.local, om_vision_level::full, true );
    }
    on_out_of_scope restore( [&]() {
        you.abort_automove();
        you.cancel_activity();
        you.omt_path.clear();
        g->set_safe_mode( SAFE_MODE_ON );
        for( const auto &row : saved ) {
            overmap_buffer.ter_set( row.position, row.terrain );
            const auto existing = overmap_buffer.get_existing_om_global( row.position );
            existing.om->set_seen( existing.local, row.vision, true );
        }
    } );
    const auto path = overmap_buffer.get_travel_path( source, destination,
                      overmap_path_params::for_player() ).points;
    REQUIRE_FALSE( path.empty() );
    REQUIRE( path.front() == destination );
    REQUIRE( path.back() == source );
    you.set_moves( 20000 );
    const auto clock = calendar::turn;
    SECTION( "cancel native confirmation with no previous route" ) {
        // Native query_popup returns ERROR in test mode: no fake keyboard or accepted prompt.
        const auto engine = rng_get_engine();
        REQUIRE( you.omt_path.empty() );
        CHECK_FALSE( overmap_ui::try_travel_to_destination( you, destination, destination, false ) );
        CHECK( you.omt_path.empty() );
        CHECK_FALSE( you.activity );
        CHECK( you.get_moves() == 20000 );
        CHECK( calendar::turn == clock );
        CHECK( rng_get_engine() == engine );
    }
    SECTION( "cancel native confirmation preserves existing destination" ) {
        you.omt_path = { destination, source + tripoint_rel_omt( 0, 1, 0 ), source };
        const auto previous = you.omt_path;
        CHECK_FALSE( overmap_ui::try_travel_to_destination( you, destination, destination, false ) );
        CHECK( you.omt_path == previous );
        CHECK_FALSE( you.activity );
        CHECK( you.get_moves() == 20000 );
    }
    SECTION( "native travelling hands off to local route and resumes at arrival" ) {
        g->set_safe_mode( SAFE_MODE_OFF );
        you.omt_path = path;
        you.assign_activity( activity_id( "ACT_TRAVELLING" ) );
        you.activity.do_turn( you );
        REQUIRE_FALSE( you.activity );
        REQUIRE( you.has_destination() );
        const auto starting_position = you.pos_abs();
        const int starting_moves = you.get_moves();
        int steps = 0;
        while( you.has_destination() ) {
            REQUIRE( ++steps < 80 );
            const auto action = you.get_next_auto_move_direction();
            REQUIRE( action != ACTION_NULL );
            const auto delta = get_delta_from_movement_action( action, iso_rotate::yes );
            REQUIRE( avatar_action::move( you, here, tripoint_rel_ms( delta, 0 ) ) );
        }
        CHECK( steps > 0 );
        CHECK( you.pos_abs() != starting_position );
        CHECK( you.get_moves() < starting_moves );
        CHECK( you.pos_abs_omt() == destination );
        REQUIRE( you.has_destination_activity() );
        you.start_destination_activity();
        REQUIRE( you.has_activity( activity_id( "ACT_TRAVELLING" ) ) );
        you.activity.do_turn( you );
        CHECK_FALSE( you.activity );
        CHECK_FALSE( you.has_destination() );
        CHECK( you.omt_path.empty() );
    }
    SECTION( "unreachable local waypoint cancels the native travel activity" ) {
        const auto position = you.pos_bub( here );
        for( int y = -1; y <= 1; ++y ) {
            for( int x = -1; x <= 1; ++x ) {
                if( x || y ) {
                    here.ter_set( position + tripoint_rel_ms( x, y, 0 ), ter_str_id( "t_concrete_wall" ) );
                }
            }
        }
        you.omt_path = path;
        you.assign_activity( activity_id( "ACT_TRAVELLING" ) );
        you.activity.do_turn( you );
        CHECK_FALSE( you.activity );
        CHECK_FALSE( you.has_destination() );
        CHECK( you.pos_bub( here ) == position );
        CHECK( you.get_moves() == 20000 );
        CHECK( calendar::turn == clock );
    }
}
#endif
