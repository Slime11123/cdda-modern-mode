#include "fp3d_sky.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>

#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

#include "units.h"

namespace fp3d_sky
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float radius = 40;

void color( const state &sky, float elevation )
{
    const float horizon = 1 - elevation;
    const float day = sky.daylight;
    const float glow = sky.twilight * horizon * horizon;
    glColor3f( .008f + day * ( .20f + .35f * horizon ) + .35f * glow,
               .012f + day * ( .43f + .26f * horizon ) + .12f * glow,
               .035f + day * ( .76f + .10f * horizon ) - .015f * glow );
}

void disc( float x, float y, float z, float size )
{
    // Build a tangent basis on the sphere, independent of camera orientation.
    const float length = std::sqrt( x * x + z * z );
    const float rx = length > .001f ? z / length : 1;
    const float rz = length > .001f ? -x / length : 0;
    const float ux = y * rz;
    const float uy = z * rx - x * rz;
    const float uz = -y * rx;
    glBegin( GL_TRIANGLE_FAN );
    glVertex3f( x * radius, y * radius, z * radius );
    for( int i = 0; i <= 24; ++i ) {
        const float angle = i * 2 * pi / 24;
        const float u = std::cos( angle ) * size;
        const float v = std::sin( angle ) * size;
        glVertex3f( x * radius + rx * u + ux * v, y * radius + uy * v,
                    z * radius + rz * u + uz * v );
    }
    glEnd();
}
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

void draw( const state &sky )
{
    if( !sky.visible ) {
        return;
    }
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_DEPTH_TEST );
    glDepthMask( GL_FALSE );
    glShadeModel( GL_SMOOTH );
    // Cover the entire background, including gaps beyond the terrain boundary.
    // The lower hemisphere keeps the horizon tint; terrain still draws in front.
    for( int ring = -12; ring < 12; ++ring ) {
        glBegin( GL_QUAD_STRIP );
        for( int segment = 0; segment <= 48; ++segment ) {
            const float az = segment * 2 * pi / 48;
            for( int edge = 0; edge < 2; ++edge ) {
                const float elevation = ( ring + edge ) / 12.0f;
                const float alt = elevation * pi / 2;
                color( sky, std::max( 0.0f, elevation ) );
                glVertex3f( radius * std::cos( alt ) * std::sin( az ),
                            radius * std::sin( alt ), radius * std::cos( alt ) * std::cos( az ) );
            }
        }
        glEnd();
    }
    const float night = std::clamp( 1 - sky.daylight * 4, 0.0f, 1.0f );
    if( night > 0 ) {
        glPointSize( 1 );
        glBegin( GL_POINTS );
        // Fixed spherical distribution, with no gameplay RNG or wall-clock time.
        for( int i = 0; i < 240; ++i ) {
            const float y = ( i + .5f ) / 240;
            const float angle = i * 2.39996323f;
            const float horizontal = std::sqrt( 1 - y * y );
            const float brightness = night * ( .35f + ( i % 7 ) * .08f );
            glColor3f( brightness, brightness, brightness );
            glVertex3f( radius * horizontal * std::cos( angle ), radius * y,
                        radius * horizontal * std::sin( angle ) );
        }
        glEnd();
    }
    if( sky.sun_y > 0 ) {
        glColor3f( 1, .85f + .13f * sky.daylight, .55f + .4f * sky.daylight );
        disc( sky.sun_x, sky.sun_y, sky.sun_z, .65f );
    } else if( night > 0 && sky.moonlight > .01f ) {
        const float brightness = night * ( .25f + .65f * sky.moonlight );
        glColor3f( brightness, brightness, brightness * .95f );
        // Decorative moon opposite the sun; brightness follows native phase.
        disc( -sky.sun_x, -sky.sun_y, -sky.sun_z, .5f );
    }
    glDepthMask( GL_TRUE );
    glEnable( GL_DEPTH_TEST );
}
}
#endif
