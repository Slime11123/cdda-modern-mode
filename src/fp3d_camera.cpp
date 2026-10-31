#include "fp3d_camera.h"

#include <algorithm>
#include <cmath>

namespace fp3d_camera
{
namespace
{
constexpr float radians = 3.14159265358979323846f / 180;
}

float horizontal_tangent()
{
    return std::tan( horizontal_fov_degrees * radians / 2 );
}

frustum::frustum( float yaw, float pitch, float aspect ) :
    cos_yaw( std::cos( yaw * radians ) ), sin_yaw( std::sin( yaw * radians ) ),
    cos_pitch( std::cos( pitch * radians ) ), sin_pitch( std::sin( pitch * radians ) ),
    tan_horizontal( horizontal_tangent() ), tan_vertical( tan_horizontal / aspect ),
    horizontal_margin( std::sqrt( 1 + tan_horizontal * tan_horizontal ) ),
    vertical_margin( std::sqrt( 1 + tan_vertical * tan_vertical ) )
{
}

bool frustum::contains( float x, float y, float z, float radius ) const
{
    const float side = cos_yaw * x + sin_yaw * z;
    const float forward = sin_yaw * x - cos_yaw * z;
    const float up = cos_pitch * y + sin_pitch * forward;
    const float depth = cos_pitch * forward - sin_pitch * y;
    return depth + radius >= near_clip && depth - radius <= far_clip &&
           std::abs( side ) <= depth * tan_horizontal + radius * horizontal_margin &&
           std::abs( up ) <= depth * tan_vertical + radius * vertical_margin;
}

tripoint_rel_ms movement( float yaw, int forward, int right )
{
    if( forward == 0 && right == 0 ) {
        return tripoint_rel_ms::zero;
    }
    constexpr double pi = 3.14159265358979323846;
    const double angle = yaw * pi / 180 + std::atan2( right, forward );
    int sector = static_cast<int>( std::lround( angle / ( pi / 4 ) ) ) % 8;
    sector = ( sector + 8 ) % 8;
    static const tripoint_rel_ms directions[] = {
        tripoint_rel_ms( 0, -1, 0 ), tripoint_rel_ms( 1, -1, 0 ),
        tripoint_rel_ms( 1, 0, 0 ), tripoint_rel_ms( 1, 1, 0 ),
        tripoint_rel_ms( 0, 1, 0 ), tripoint_rel_ms( -1, 1, 0 ),
        tripoint_rel_ms( -1, 0, 0 ), tripoint_rel_ms( -1, -1, 0 )
    };
    return directions[sector];
}

offset follower::relative_to( const tripoint_abs_ms &origin, unsigned now ) const
{
    if( !initialized ) {
        return {};
    }
    const double t = std::min( 1.0, static_cast<double>( now - started ) / duration );
    return { static_cast<float>( start_x + ( target.x() - start_x ) * t - origin.x() ),
             static_cast<float>( start_y + ( target.y() - start_y ) * t - origin.y() ) };
}

void follower::confirm( const tripoint_abs_ms &position, unsigned now, bool smooth )
{
    if( initialized && position == target ) {
        return;
    }
    const tripoint_rel_ms delta = position - target;
    if( !initialized || !smooth || delta.z() != 0 || std::abs( delta.x() ) > 1 ||
        std::abs( delta.y() ) > 1 ) {
        start_x = position.x();
        start_y = position.y();
    } else {
        const offset current = relative_to( target, now );
        start_x = target.x() + current.x;
        start_y = target.y() + current.y;
    }
    duration = initialized ? std::clamp( now - started, 25u, 100u ) : 100;
    target = position;
    started = now;
    initialized = true;
}

void follower::reset()
{
    initialized = false;
}
} // namespace fp3d_camera
