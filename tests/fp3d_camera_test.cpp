#include "fp3d_camera.h"
#include "modern_2d_camera.h"

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

TEST_CASE( "right_camera_hold_and_drag_are_distinct_from_context_clicks",
           "[fp3d][camera][edge_pan]" )
{
    right_camera_drag drag;
    drag.begin( point( 30, 40 ), 1000 );
    drag.move( point( 34, 43 ) );
    CHECK_FALSE( drag.dragging( 1200 ) );
    CHECK( drag.release() );
    drag.begin( point( 30, 40 ), 2000 );
    drag.move( point( 55, 43 ) );
    CHECK_FALSE( drag.dragging( 2179 ) );
    CHECK( drag.dragging( 2180 ) );
    drag.move( point( 30, 40 ) );
    CHECK( drag.dragging( 2300 ) );
    CHECK_FALSE( drag.release() );
    CHECK_FALSE( drag.dragging( 2400 ) );
}

TEST_CASE( "modern_2d_edge_camera_peeks_and_returns_without_grid_steps", "[fp3d][camera][edge_pan]" )
{
    modern_2d_camera camera;
    const point size( 1000, 800 );
    CHECK_FALSE( camera.update( true, size / 2, size, .016f ) );
    camera.update( true, point( 999, 400 ), size, .016f );
    const point first = camera.pixels();
    CHECK( first.x > 0 );
    CHECK( first.x < 100 );
    CHECK( first.y == 0 );
    for( int frame = 0; frame < 180; ++frame ) {
        camera.update( true, point( 999, 400 ), size, .016f );
    }
    CHECK( camera.pixels().x > first.x );
    CHECK( camera.pixels().x <= 450 );
    for( int frame = 0; frame < 180; ++frame ) {
        camera.update( true, size / 2, size, .016f );
    }
    CHECK( camera.pixels() == point::zero );

    camera.update( true, point::zero, size, .016f );
    CHECK( camera.pixels().x < 0 );
    CHECK( camera.pixels().y < 0 );
    // Mode/menu/focus changes and the widest zoom disable and clear the offset.
    CHECK( camera.update( false, point::zero, size, .016f ) );
    CHECK( camera.pixels() == point::zero );
    CHECK_FALSE( camera.update( false, point::zero, size, .016f ) );
    CHECK_FALSE( camera.update( true, point( 1100, 400 ), size, .016f ) );
    CHECK_FALSE( camera.update( true, point::zero, point::zero, .016f ) );
}

TEST_CASE( "modern_2d_edge_camera_changes_direction_along_each_edge", "[fp3d][camera][edge_pan]" )
{
    const point size( 1000, 800 );
    modern_2d_camera camera;
    // Visit all sides/corners with the SAME camera, including direction reversals.
    for( const point mouse : { point( 999, 400 ), point( 0, 400 ), point( 500, 0 ),
                              point( 500, 799 ), point( 0, 0 ), point( 999, 799 ),
                              point( 999, 0 ), point( 0, 799 ),
                              // These diagonals used to lock to a single axis.
                              point( 999, 200 ), point( 999, 600 ), point( 0, 200 ), point( 0, 600 ),
                              point( 250, 0 ), point( 750, 0 ), point( 250, 799 ), point( 750, 799 ) } ) {
        CAPTURE( mouse );
        for( int frame = 0; frame < 180; ++frame ) {
            camera.update( true, mouse, size, .016f );
        }
        const point pan = camera.pixels();
        if( mouse.x == 500 ) {
            CHECK( pan.x == 0 );
        } else {
            CHECK( ( pan.x > 0 ) == ( mouse.x > 500 ) );
            CHECK( pan.x != 0 );
        }
        if( mouse.y == 400 ) {
            CHECK( pan.y == 0 );
        } else {
            CHECK( ( pan.y > 0 ) == ( mouse.y > 400 ) );
            CHECK( pan.y != 0 );
        }
        CHECK( std::abs( pan.x ) <= 450 );
        CHECK( std::abs( pan.y ) <= 360 );
    }
    for( int frame = 0; frame < 180; ++frame ) {
        camera.update( true, size / 2, size, .016f );
    }
    CHECK( camera.pixels() == point::zero );
}

TEST_CASE( "modern_2d_look_around_has_a_circular_limit_at_every_angle", "[fp3d][camera][edge_pan]" )
{
    for( const point size : { point( 1000, 800 ), point( 1600, 600 ), point( 600, 1000 ) } ) {
        const float radius = ( std::min( size.x, size.y ) - 1 ) * .5f;
        const float reach = std::min( size.x, size.y ) * .45f;
        modern_2d_camera camera;
        for( int degrees = 0; degrees < 360; ++degrees ) {
            CAPTURE( size, degrees );
            const float angle = degrees * 3.14159265f / 180;
            const float vx = std::cos( angle );
            const float vy = std::sin( angle );
            // Sweep a circle inside the rectangular viewport, including points
            // far from its edges. A rectangular trigger fails at these angles.
            const point mouse( std::lround( ( size.x - 1 ) * .5f + radius * vx ),
                               std::lround( ( size.y - 1 ) * .5f + radius * vy ) );
            for( int frame = 0; frame < 160; ++frame ) {
                camera.update( true, mouse, size, .016f );
            }
            const point pan = camera.pixels();
            CHECK( std::hypot( pan.x, pan.y ) == Approx( reach ).margin( 2 ) );
            CHECK( std::abs( pan.x * vy - pan.y * vx ) < 1.5f );
            CHECK( pan.x * vx + pan.y * vy > 0 );
        }
        for( int frame = 0; frame < 180; ++frame ) {
            camera.update( true, size / 2, size, .016f );
        }
        CHECK( camera.pixels() == point::zero );
    }
    // Distance increases reach gradually in the circular transition band.
    modern_2d_camera near;
    modern_2d_camera far;
    const point size( 1000, 800 );
    for( int frame = 0; frame < 180; ++frame ) {
        near.update( true, point( 800, 400 ), size, .016f );
        far.update( true, point( 890, 400 ), size, .016f );
    }
    CHECK( near.pixels().x > 0 );
    CHECK( far.pixels().x > near.pixels().x );
}

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

TEST_CASE( "fp3d_overhead_camera_orbits_the_pivot_and_looks_down", "[fp3d][camera][overhead]" )
{
    // Azimuth 0 looks north, so the eye must stand south of, and above, the pivot.
    fp3d_camera::orbit view;
    view.azimuth = 0;
    view.distance = 18;
    const fp3d_camera::pose north = fp3d_camera::orbit_pose( view, 0, 0 );
    CHECK( north.yaw == 0 );
    CHECK( north.pitch == fp3d_camera::orbit_elevation_degrees );
    CHECK( north.eye.x == Approx( 0 ).margin( .001 ) );
    CHECK( north.eye.y == Approx( 18 * std::cos( 60.f * 3.14159265f / 180 ) ).margin( .01 ) );
    CHECK( north.eye.height ==
           Approx( fp3d_camera::orbit_pivot_height + 18 * std::sin( 60.f * 3.14159265f / 180 ) ).margin( .01 ) );
    CHECK( north.eye.height > fp3d_camera::eye_height );
    // The eye keeps exactly the requested distance from the pivot, at every
    // azimuth and zoom, so the player can never drift out of frame.
    for( const float azimuth : {
             0.f, 45.f, 90.f, 179.f, 270.f, 359.f, 720.f
         } ) {
        for( const float distance : {
                 6.f, 12.f, 18.f, 48.f, 2.f, 400.f
             } ) {
            CAPTURE( azimuth, distance );
            view.azimuth = azimuth;
            view.distance = distance;
            const fp3d_camera::pose pose = fp3d_camera::orbit_pose( view, 3.5f, -2.25f );
            const float clamped = std::clamp( distance, fp3d_camera::orbit_min_distance,
                                              fp3d_camera::orbit_max_distance );
            const float dx = pose.eye.x - 3.5f;
            const float dy = pose.eye.y - ( -2.25f );
            const float dz = pose.eye.height - fp3d_camera::orbit_pivot_height;
            CHECK( std::sqrt( dx * dx + dy * dy + dz * dz ) == Approx( clamped ).margin( .01 ) );
            // Looking down means a negative height component in the shared
            // forward convention: ( cos(p)*sin(y), -cos(p)*cos(y), -sin(p) ).
            CHECK( dz > 0 );
            CHECK( pose.pitch > 0 );
            CHECK( std::remainder( pose.yaw, 360.f ) == Approx( std::remainder( azimuth, 360.f ) ) );
        }
    }
    // Turning the azimuth moves the eye around the pivot in the horizontal plane
    // and never changes its height.
    view.distance = 18;
    view.azimuth = 90;
    const fp3d_camera::pose east = fp3d_camera::orbit_pose( view, 0, 0 );
    CHECK( east.eye.x == Approx( -18 * std::cos( 60.f * 3.14159265f / 180 ) ).margin( .01 ) );
    CHECK( east.eye.y == Approx( 0 ).margin( .01 ) );
    CHECK( east.eye.height == Approx( north.eye.height ).margin( .01 ) );
    // A fixed downward angle: the first version never lets the player rotate it.
    view.elevation = 2;
    CHECK( fp3d_camera::orbit_pose( view, 0, 0 ).pitch >= 5 );
    view.elevation = 120;
    CHECK( fp3d_camera::orbit_pose( view, 0, 0 ).pitch <= 89 );
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
TEST_CASE( "fp3d_overhead_billboard_axes_preserve_projected_size", "[fp3d][overhead][camera]" )
{
    constexpr float radians = 3.14159265358979323846f / 180;
    for( float yaw : { 0.f, 45.f, 90.f, 180.f, 270.f } ) {
        for( float pitch : { 0.f, 60.f, 89.f } ) {
            CAPTURE( yaw, pitch );
            const fp3d_camera::frustum view( yaw, pitch, 1 );
            const float sy = std::sin( yaw * radians );
            const float cy = std::cos( yaw * radians );
            const float sp = std::sin( pitch * radians );
            const float cp = std::cos( pitch * radians );
            // A full sprite-height step must project entirely onto screen up,
            // with no sideways drift or depth change, even near top-down.
            const float forward = sy * view.up_x() - cy * view.up_z();
            CHECK( cp * view.up_y() + sp * forward == Approx( 1 ).margin( .00001 ) );
            CHECK( cp * forward - sp * view.up_y() == Approx( 0 ).margin( .00001 ) );
            CHECK( view.right_x() * view.up_x() + view.right_z() * view.up_z() ==
                   Approx( 0 ).margin( .00001 ) );
        }
    }
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

TEST_CASE( "fp3d_pointer_floor_tracks_orbit_projection_and_storey_planes", "[fp3d][camera][cutaway]" )
{
    for( const float yaw : { 0.f, 90.f, 180.f, 270.f } ) {
        for( const float distance : { 6.f, 18.f, 48.f } ) {
            for( const float aspect : { .8f, 1.7777778f } ) {
                const auto base = fp3d_camera::orbit_pose( { yaw, 60.f, distance }, 3.25f, -2.5f );
                for( const float height : { 0.f, 2.2f, 8.8f } ) {
                    auto pose = base;
                    pose.eye.height += height;
                    const auto screen = fp3d_camera::project( pose, aspect, 4.f, height, -1.f );
                    REQUIRE( screen );
                    const auto floor = fp3d_camera::pointer_floor( pose, aspect,
                                       ( *screen )[0], ( *screen )[1], height );
                    REQUIRE( floor );
                    CHECK( ( *floor )[0] == Approx( 4.f ).margin( .0001f ) );
                    CHECK( ( *floor )[1] == Approx( -1.f ).margin( .0001f ) );
                }
            }
        }
    }
    CHECK_FALSE( fp3d_camera::pointer_floor( { 0, 0, { 0, 0, 2 } }, 1, 0, 0 ) );
    CHECK_FALSE( fp3d_camera::pointer_floor( { 0, 60, { 0, 0, 2 } }, 0, 0, 0 ) );
}
