#include "fp3d_sky.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <cmath>

#include "cata_catch.h"
#include "avatar.h"
#include "fp3d_snapshot.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "type_id.h"

TEST_CASE( "fp3d_sky_follows_native_day_twilight_and_night", "[fp3d][sky]" )
{
    const auto time = calendar::turn;
    const auto day = fp3d_sky::sample( calendar::turn_zero + 12_hours, true );
    const auto night = fp3d_sky::sample( calendar::turn_zero, true );
    const auto dusk = fp3d_sky::sample( sunset( calendar::turn_zero ), true );
    CHECK( day.visible );
    CHECK( day.daylight > .9f );
    CHECK( day.sun_y > 0 );
    CHECK( night.daylight < .01f );
    CHECK( night.sun_y < 0 );
    CHECK( dusk.twilight > .9f );
    CHECK( dusk.daylight > night.daylight );
    CHECK( dusk.daylight < day.daylight );
    CHECK( day.sun_x * day.sun_x + day.sun_y * day.sun_y + day.sun_z * day.sun_z == Approx( 1 ) );
    CHECK( calendar::turn == time );
    CHECK_FALSE( fp3d_sky::sample( calendar::turn_zero + 12_hours, false ).visible );
    // Repainting does not animate a second clock while the game is paused.
    const auto repeated = fp3d_sky::sample( calendar::turn_zero + 12_hours, true );
    CHECK( repeated.sun_x == day.sun_x );
    CHECK( repeated.sun_y == day.sun_y );
    CHECK( repeated.sun_z == day.sun_z );
}
TEST_CASE( "fp3d_sky_is_hidden_indoors_and_while_blind", "[fp3d][sky][vision]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms p( 65, 65, 0 );
    you.setpos( here, p );
    here.ter_set( p, ter_str_id( "t_grass" ) );
    REQUIRE( here.ter( p ) == ter_str_id( "t_grass" ) );
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0 );
    REQUIRE( here.is_outside( p ) );
    CHECK( fp3d_snapshot::capture( you, here ).sky.visible );
    you.add_effect( efftype_id( "blind" ), 1_minutes );
    REQUIRE( you.is_blind() );
    CHECK_FALSE( fp3d_snapshot::capture( you, here ).sky.visible );
    you.remove_effect( efftype_id( "blind" ) );
    REQUIRE( here.ter_set( p, ter_str_id( "t_floor" ) ) );
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0 );
    REQUIRE_FALSE( here.is_outside( p ) );
    CHECK_FALSE( fp3d_snapshot::capture( you, here ).sky.visible );
}
#endif
