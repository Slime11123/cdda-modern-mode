#include "fp3d_camera.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include "action.h"
#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cata_catch.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "type_id.h"

TEST_CASE( "fp3d_camera_maps_eight_directions_to_native_actions", "[fp3d][input]" )
{
    for( int sector = 0; sector < 8; ++sector ) {
        const auto forward = fp3d_camera::movement( sector * 45, 1, 0 );
        CHECK( forward != tripoint_rel_ms::zero );
        CHECK( fp3d_camera::movement( sector * 45 + 360, 1, 0 ) == forward );
        CHECK( fp3d_camera::movement( sector * 45 - 360, 1, 0 ) == forward );
        CHECK( fp3d_camera::movement( sector * 45, -1, 0 ) == -forward );
        CHECK( fp3d_camera::movement( sector * 45, 0, 1 ) ==
               fp3d_camera::movement( sector * 45 + 90, 1, 0 ) );
        CHECK( fp3d_camera::movement( sector * 45, 0, -1 ) ==
               fp3d_camera::movement( sector * 45 - 90, 1, 0 ) );
        for( const iso_rotate rotation : {
                 iso_rotate::no, iso_rotate::yes
             } ) {
            const auto action = get_movement_action_from_delta( forward, rotation );
            CHECK( get_delta_from_movement_action( action, rotation ) == forward.xy() );
        }
    }
    CHECK( fp3d_camera::movement( 0, 1, 0 ) == tripoint_rel_ms::north );
    CHECK( fp3d_camera::movement( 90, 1, 0 ) == tripoint_rel_ms::east );
    CHECK( fp3d_camera::movement( 22, 1, 0 ) == tripoint_rel_ms::north );
    CHECK( fp3d_camera::movement( 23, 1, 0 ) == tripoint_rel_ms( 1, -1, 0 ) );
    CHECK( fp3d_camera::movement( 0, 0, 0 ) == tripoint_rel_ms::zero );
}

TEST_CASE( "fp3d_camera_wide_view_culling_follows_pitch_and_yaw", "[fp3d][camera]" )
{
    const fp3d_camera::frustum north( 0, 0, 1.05f );
    CHECK( fp3d_camera::horizontal_fov_degrees == 120 );
    CHECK( north.contains( 0, -1.6f, -50, .75f ) );
    CHECK( north.contains( 10, 0, -10, .01f ) );
    CHECK( north.contains( 15, 0, -10, .1f ) );
    CHECK_FALSE( north.contains( 20, 0, -10, .1f ) );
    CHECK_FALSE( north.contains( 30, 0, -10, .1f ) );
    CHECK_FALSE( north.contains( 0, 0, 10, .5f ) );
    CHECK( north.contains( 0, 0, -240, .5f ) );
    CHECK( north.contains( 0, 0, -960, .5f ) );
    CHECK_FALSE( north.contains( 0, 0, -1600, .5f ) );
    const fp3d_camera::frustum east( 90, 0, 1.05f );
    CHECK( east.contains( 50, 0, 0, .75f ) );
    CHECK_FALSE( east.contains( 0, 0, -50, .75f ) );
    const fp3d_camera::frustum diagonal( 45, 0, 1.05f );
    CHECK( diagonal.contains( 60, -1.6f, -60, 1.4f ) );
    // Billboard right must be perpendicular to the camera's forward axis.
    CHECK( diagonal.right_x() == Approx( diagonal.right_z() ) );
    const fp3d_camera::frustum down( 0, 80, 1.05f );
    CHECK( down.contains( 0, -1.6f, 0, .1f ) );
    const fp3d_camera::frustum square( 0, 0, 1 );
    const fp3d_camera::frustum wide( 0, 0, 16.0f / 9 );
    CHECK( square.contains( 0, 10, -10, .01f ) );
    CHECK_FALSE( wide.contains( 0, 10, -10, .01f ) );
}

TEST_CASE( "fp3d_camera_follows_only_confirmed_positions", "[fp3d][camera]" )
{
    fp3d_camera::follower camera;
    const tripoint_abs_ms start( 1000000, -1000000, 0 );
    camera.confirm( start, 100 );
    CHECK( camera.relative_to( start, 100 ).x == 0 );
    CHECK( camera.relative_to( start, 100 ).y == 0 );
    const auto north = start + tripoint_rel_ms::north;
    camera.confirm( north, 200, true );
    CHECK( camera.relative_to( north, 200 ).y == Approx( 1 ) );
    CHECK( camera.relative_to( north, 250 ).y == Approx( .5 ) );
    // Refreshing the same confirmed position must not restart the animation.
    camera.confirm( north, 250 );
    CHECK( camera.relative_to( north, 300 ).y == 0 );
    // A one-cell forced displacement must snap too.
    const auto forced = north + tripoint_rel_ms::east;
    camera.confirm( forced, 305 );
    CHECK( camera.relative_to( forced, 305 ).x == 0 );
    CHECK( camera.relative_to( forced, 305 ).y == 0 );
    const auto teleported = north + tripoint_rel_ms( 6, 5, 0 );
    camera.confirm( teleported, 310 );
    CHECK( camera.relative_to( teleported, 310 ).x == 0 );
    CHECK( camera.relative_to( teleported, 310 ).y == 0 );
    const auto downstairs = teleported + tripoint_rel_ms( 0, 0, -1 );
    camera.confirm( downstairs, 320 );
    CHECK( camera.relative_to( downstairs, 320 ).x == 0 );
    CHECK( camera.relative_to( downstairs, 320 ).y == 0 );
    camera.reset();
    camera.confirm( start, 330 );
    CHECK( camera.relative_to( start, 330 ).x == 0 );
}

TEST_CASE( "fp3d_camera_does_not_advance_blocked_avatar_or_game_time", "[fp3d][camera]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto start = you.pos_abs();
    REQUIRE( here.ter_set( you.pos_bub( here ) + tripoint_rel_ms::north,
                           ter_str_id( "t_wall" ) ) );
    you.set_moves( 1000 );
    avatar_action::move( you, here, fp3d_camera::movement( 0, 1, 0 ) );
    CHECK( you.pos_abs() == start );
    const int moves = you.get_moves();
    const int stamina = you.get_stamina();
    const auto turn = calendar::turn;
    fp3d_camera::follower camera;
    camera.confirm( you.pos_abs(), 0 );
    for( unsigned frame = 1; frame <= 120; ++frame ) {
        camera.confirm( you.pos_abs(), frame * 16 );
        const auto offset = camera.relative_to( you.pos_abs(), frame * 16 );
        CHECK( offset.x == 0 );
        CHECK( offset.y == 0 );
    }
    CHECK( you.pos_abs() == start );
    CHECK( you.get_moves() == moves );
    CHECK( you.get_stamina() == stamina );
    CHECK( calendar::turn == turn );
    avatar_action::move( you, here, fp3d_camera::movement( 90, 1, 0 ) );
    CHECK( you.pos_abs() == start + tripoint_rel_ms::east );
    CHECK( you.get_moves() < moves );
}
TEST_CASE( "fp3d_held_swing_uses_only_presentation_time", "[fp3d][combat][camera]" )
{
    fp3d_camera::swing swing;
    CHECK( swing.amount( 0 ) == 0 );
    swing.start( 1000 );
    CHECK( swing.amount( 1000 ) == 0 );
    CHECK( swing.amount( 1045 ) == Approx( .5f ) );
    CHECK( swing.amount( 1090 ) == Approx( 1 ) );
    CHECK( swing.amount( 1195 ) == Approx( .5f ) );
    CHECK( swing.amount( 1300 ) == 0 );
    CHECK( swing.amount( 60000 ) == 0 );
    swing.start( 2000 );
    CHECK( swing.amount( 2090 ) == Approx( 1 ) );
    swing.reset();
    CHECK( swing.amount( 2090 ) == 0 );
    // Unsigned SDL ticks wrap without extending or breaking a strike.
    swing.start( 0xffffffd0U );
    CHECK( swing.amount( 42 ) == Approx( 1 ) );
    CHECK( swing.amount( 252 ) == 0 );
}
#endif
