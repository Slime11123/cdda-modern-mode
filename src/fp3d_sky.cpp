#include "fp3d_sky.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>


#include "units.h"

namespace fp3d_sky
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
}

state sample( const time_point &time, bool visible )
{
    state result;
    result.visible = visible;
    const auto [azimuth, altitude] = sun_azimuth_altitude( time );
    const float alt = units::to_radians( altitude );
    const float az = units::to_radians( azimuth );
    const float degrees = units::to_degrees( altitude );
    float blend = std::clamp( ( degrees + 8 ) / 18, 0.0f, 1.0f );
    result.daylight = blend * blend * ( 3 - 2 * blend );
    result.twilight = std::clamp( 1 - std::abs( degrees + 1 ) / 9, 0.0f, 1.0f );
    result.sun_x = -std::sin( az ) * std::cos( alt );
    result.sun_y = std::sin( alt );
    result.sun_z = std::cos( az ) * std::cos( alt );
    result.moonlight = ( 1 - std::cos( static_cast<int>( get_moon_phase( time ) ) * pi / 4 ) ) / 2;
    return result;
}


}
#endif
