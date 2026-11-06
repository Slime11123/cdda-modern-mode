#include "fp3d_weather.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "fp3d_snapshot.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "weather.h"

TEST_CASE( "fp3d_weather_maps_native_weather_without_changing_gameplay", "[fp3d_weather]" )
{
    const auto engine = rng_get_engine();
    const auto time = calendar::turn;
    const auto rain = fp3d_weather::sample( weather_type_id( "rain" ).obj(), 20, 0, false, true );
    const auto drizzle = fp3d_weather::sample( weather_type_id( "light_drizzle" ).obj(), 0, 0, false, true );
    const auto snow = fp3d_weather::sample( weather_type_id( "snowing" ).obj(), 20, 90, false, true );
    const auto blizzard = fp3d_weather::sample( weather_type_id( "snowstorm" ).obj(), 50, 0, false, true );
    CHECK( rain.particles == fp3d_weather::precipitation::rain );
    CHECK( rain.intensity > drizzle.intensity );
    CHECK( rain.wind_x == Approx( 0 ).margin( .001 ) );
    CHECK( rain.wind_z > 0 );
    CHECK( snow.particles == fp3d_weather::precipitation::snow );
    CHECK( snow.wind_x < 0 );
    CHECK( blizzard.intensity > snow.intensity );
    const auto mist = fp3d_weather::sample( weather_type_id( "mist" ).obj(), 0, 0, false, true );
    const auto fog = fp3d_weather::sample( weather_type_id( "fog" ).obj(), 0, 0, false, true );
    CHECK( fog.fog_end < mist.fog_end );
    CHECK( fog.particles == fp3d_weather::precipitation::none );
    const auto sunny = fp3d_weather::sample( weather_type_id( "sunny" ).obj(), 0, 0, false, true );
    CHECK( sunny.clouds == 0 );
    CHECK( sunny.fog_end == 0 );
    const auto portal = fp3d_weather::sample( weather_type_id( "portal_storm" ).obj(), 0, 0, false, true );
    CHECK( portal.particles == fp3d_weather::precipitation::anomaly );
    CHECK( calendar::turn == time );
    CHECK( rng_get_engine() == engine );
}

TEST_CASE( "fp3d_weather_keeps_precipitation_out_of_sheltered_tiles", "[fp3d_weather]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    auto &you = get_avatar();
    auto &here = get_map();
    auto &weather = get_weather();
    restore_on_out_of_scope restore_weather( weather.weather_id );
    weather.weather_id = weather_type_id( "rain" );
    const tripoint_bub_ms p( 65, 65, 0 );
    you.setpos( here, p );
    here.ter_set( p, ter_str_id( "t_grass" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto open = fp3d_snapshot::capture( you, here );
    CHECK( open.weather.outdoors );
    CHECK( std::find( open.weather.columns.begin(), open.weather.columns.end(), here.get_abs( p ) ) != open.weather.columns.end() );
    here.ter_set( p, ter_str_id( "t_floor" ) );
    here.ter_set( p + tripoint_rel_ms::above, ter_str_id( "t_flat_roof" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto sheltered = fp3d_snapshot::capture( you, here );
    CHECK_FALSE( sheltered.weather.outdoors );
    CHECK( std::find( sheltered.weather.columns.begin(), sheltered.weather.columns.end(), here.get_abs( p ) ) == sheltered.weather.columns.end() );
}

TEST_CASE( "fp3d_native_lightning_is_a_brief_pulse_in_a_paused_world", "[fp3d_weather]" )
{
    fp3d_weather::state state;
    fp3d_weather::flash( state, 0 );
    state.lightning = true;
    CHECK( fp3d_weather::flash( state, 1000 ) == 1 );
    CHECK( fp3d_weather::flash( state, 1180 ) == Approx( .5f ) );
    CHECK( fp3d_weather::flash( state, 2000 ) == 0 );
    state.lightning = false;
    CHECK( fp3d_weather::flash( state, 2100 ) == 0 );
}
#endif
