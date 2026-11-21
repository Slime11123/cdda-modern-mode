#include "fp3d_weather.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include "fp3d_camera.h"
#include "weather_type.h"

namespace fp3d_weather
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
float fraction( std::uint32_t seed )
{
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    return ( seed & 0xffffffu ) / 16777216.0f;
}
}

state sample( const weather_type &type, int windspeed, int winddirection,
              bool lightning, bool outdoors )
{
    state result;
    result.outdoors = outdoors;
    result.lightning = lightning;
    result.clouds = std::clamp( 1 - type.sun_multiplier, 0.0f, 1.0f );
    switch( type.precip ) {
        case precip_class::very_light: result.intensity = .15f; break;
        case precip_class::light: result.intensity = .4f; break;
        case precip_class::heavy: result.intensity = .75f; break;
        default: break;
    }
    if( type.sound_category == weather_sound_category::rainstorm ||
        type.sound_category == weather_sound_category::thunder ||
        type.sound_category == weather_sound_category::snowstorm ) {
        result.intensity = 1;
    }
    if( result.intensity > 0 ) {
        result.particles = type.rains ? precipitation::rain : precipitation::snow;
    }
    if( type.sound_category == weather_sound_category::portal_storm ) {
        result.particles = precipitation::anomaly;
        result.intensity = .45f;
    }
    const float penalty = std::max( 0.0f, type.sight_penalty - 1 );
    if( penalty > .001f ) { result.fog_end = std::max( 28.0f, 180.0f / ( 1 + penalty * 12 ) ); }
    const float angle = winddirection * pi / 180;
    const float speed = std::clamp( windspeed * .035f, 0.0f, 2.0f );
    // Weather directions name where the wind comes from (north blows south).
    result.wind_x = -std::sin( angle ) * speed;
    result.wind_z = std::cos( angle ) * speed;
    return result;
}

float flash( const state &weather, std::uint64_t milliseconds )
{
    static bool was_active = false;
    static std::uint64_t started = 0;
    if( weather.lightning && !was_active ) { started = milliseconds; }
    was_active = weather.lightning;
    if( !weather.lightning || milliseconds < started || milliseconds - started > 360 ) { return 0; }
    return 1 - ( milliseconds - started ) / 360.0f;
}

linear_fog fog_parameters( const state &weather, float daylight, float lightning, float far_scale )
{
    if( weather.fog_end <= 0 ) {
        return {};
    }
    const float light = .055f + daylight * .48f + lightning * .3f;
    const float scale = far_scale > .05f ? far_scale : 1.0f;
    return { { light * .91f, light * .97f, light, 1 }, 8 * scale, weather.fog_end * scale };
}

void visit_particles( const state &weather, const tripoint_abs_ms &origin,
                      std::uint64_t milliseconds, const std::function<void( particle )> &visit )
{
    if( weather.particles == precipitation::none || weather.columns.empty() ) { return; }
    const bool rain = weather.particles == precipitation::rain;
    const bool anomaly = weather.particles == precipitation::anomaly;
    const float seconds = ( milliseconds % 600000 ) / 1000.0f;
    const float speed = rain ? 9.0f : anomaly ? -.7f : 1.2f;
    unsigned count = 0;
    for( const auto &column : weather.columns ) {
        const auto p = column - origin;
        const std::uint32_t base = static_cast<std::uint32_t>( column.x() ) * 73856093u ^
                                   static_cast<std::uint32_t>( column.y() ) * 19349663u ^
                                   static_cast<std::uint32_t>( column.z() ) * 83492791u;
        for( unsigned layer = 0; layer < 3 && count < 1200; ++layer ) {
            const std::uint32_t seed = base + layer * 2654435761u;
            if( fraction( seed ) > weather.intensity ) { continue; }
            ++count;
            const float phase = fraction( seed + 1 ) * 7.5f + seconds * speed;
            const float height = 7.5f - ( phase - std::floor( phase / 7.5f ) * 7.5f );
            const float sway = rain ? 0 : std::sin( seconds * 1.3f + fraction( seed + 2 ) * 6.28f ) * .16f;
            const float x = p.x() + std::clamp( fraction( seed + 3 ) - .5f + weather.wind_x * ( 7.5f - height ) * .04f + sway, -.46f, .46f );
            const float z = p.y() + std::clamp( fraction( seed + 4 ) - .5f + weather.wind_z * ( 7.5f - height ) * .04f, -.46f, .46f );
            const float floor = p.z() * fp3d_camera::floor_height + .025f;
            const float y = floor + height;
            visit( { x, y, z, x + weather.wind_x * .06f, std::max( floor, y - .35f ),
                     z + weather.wind_z * .06f } );
        }
        if( count >= 1200 ) { break; }
    }
}


}
#endif
