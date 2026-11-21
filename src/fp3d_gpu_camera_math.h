#pragma once
#ifndef CATA_SRC_FP3D_GPU_CAMERA_MATH_H
#define CATA_SRC_FP3D_GPU_CAMERA_MATH_H
#include <array>
#include <cmath>
#include <optional>

// Row-major matrix, M * column vector. SDL GPU clip depth is [0,1].
// The x/east, height/up, z/south basis matches fp3d_camera::project().
namespace fp3d_gpu_camera_math
{
struct matrix4 {
    std::array<float, 16> elements{};
    std::array<float, 4> transform( float east, float height, float south,
                                   float w = 1.0f ) const {
        const std::array<float, 4> p{ east, height, south, w };
        std::array<float, 4> result{};
        for( int row = 0; row < 4; ++row ) {
            for( int col = 0; col < 4; ++col ) {
                result[row] += elements[row * 4 + col] * p[col];
            }
        }
        return result;
    }
};
struct camera {
    float yaw_degrees = 0;
    float pitch_degrees = 0;
    float aspect = 1;
    float eye_east = 0;
    float eye_height = 1.6f;
    float eye_south = 0;
    float horizontal_fov_degrees = 120;
    float near_clip = .20f;
    float far_clip = 1536;
};
inline std::optional<matrix4> clip_from_world( const camera &view )
{
    constexpr float radians = 0.017453292519943295769f;
    if( !( std::isfinite( view.yaw_degrees ) && std::isfinite( view.pitch_degrees ) &&
           std::isfinite( view.aspect ) && std::isfinite( view.eye_east ) &&
           std::isfinite( view.eye_height ) && std::isfinite( view.eye_south ) &&
           std::isfinite( view.horizontal_fov_degrees ) &&
           std::isfinite( view.near_clip ) && std::isfinite( view.far_clip ) ) ||
        view.aspect <= 0 || view.horizontal_fov_degrees <= 0 ||
        view.horizontal_fov_degrees >= 180 || view.near_clip <= 0 ||
        view.far_clip <= view.near_clip ) {
        return std::nullopt;
    }
    const float cy = std::cos( view.yaw_degrees * radians );
    const float sy = std::sin( view.yaw_degrees * radians );
    const float cp = std::cos( view.pitch_degrees * radians );
    const float sp = std::sin( view.pitch_degrees * radians );
    const float h = 1 / std::tan( view.horizontal_fov_degrees * radians / 2 );
    const float v = view.aspect * h;
    const float ds = view.far_clip / ( view.far_clip - view.near_clip );
    const std::array<float, 4> right{ cy, 0, sy,
        -cy * view.eye_east - sy * view.eye_south };
    const std::array<float, 4> up{ sp * sy, cp, -sp * cy,
        -sp * sy * view.eye_east - cp * view.eye_height + sp * cy * view.eye_south };
    const std::array<float, 4> depth{ cp * sy, -sp, -cp * cy,
        -cp * sy * view.eye_east + sp * view.eye_height + cp * cy * view.eye_south };
    matrix4 result;
    for( int i = 0; i < 4; ++i ) {
        result.elements[i] = right[i] * h;
        result.elements[4+i] = up[i] * v;
        result.elements[8+i] = depth[i] * ds;
        result.elements[12+i] = depth[i];
    }
    result.elements[11] -= view.near_clip * ds;
    return result;
}
// Solar depth increases away from the sun. Snap the map in absolute light space
// while keeping world vertices relative to the snapshot origin.
inline matrix4 sun_shadow_clip_from_world( const std::array<float, 3> &sun,
        const std::array<double, 3> &anchor, float radius, unsigned resolution )
{
    const float horizontal = std::hypot( sun[0], sun[2] );
    const std::array<float, 3> right = horizontal > .005f ?
                                      std::array<float, 3>{ sun[2] / horizontal, 0, -sun[0] / horizontal } :
                                      std::array<float, 3>{ 1, 0, 0 };
    const std::array<float, 3> up{ sun[1] * right[2],
                                   sun[2] * right[0] - sun[0] * right[2], -sun[1] * right[0] };
    const double grid = 2.0 * radius / resolution;
    const auto snap = [&]( const std::array<float, 3> &axis ) {
        const double projected = axis[0] * anchor[0] + axis[1] * anchor[1] + axis[2] * anchor[2];
        return static_cast<float>( projected - std::round( projected / grid ) * grid );
    };
    constexpr float distance = 78.f;
    constexpr float near_plane = .1f;
    constexpr float depth_range = 2 * distance - near_plane;
    return { { right[0] / radius, right[1] / radius, right[2] / radius, snap( right ) / radius,
               up[0] / radius, up[1] / radius, up[2] / radius, snap( up ) / radius,
               -sun[0] / depth_range, -sun[1] / depth_range, -sun[2] / depth_range,
               ( distance - near_plane ) / depth_range, 0, 0, 0, 1 } };
}

// D3D cube face order and orientation. The point-shadow shader uses the same basis.
inline std::array<matrix4, 6> point_shadow_faces( const std::array<float, 4> &light )
{
    constexpr int axes[6][3][3] = {
        { { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0, 0 } },
        { { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 } },
        { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } },
        { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
        { { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } },
    };
    constexpr float near_plane = .05f;
    const float depth_scale = light[3] / ( light[3] - near_plane );
    std::array<matrix4, 6> faces{};
    for( size_t face = 0; face < faces.size(); ++face ) {
        auto &m = faces[face].elements;
        for( int axis = 0; axis < 3; ++axis ) {
            for( int i = 0; i < 3; ++i ) {
                const float component = static_cast<float>( axes[face][axis][i] );
                m[axis * 4 + i] = component;
                m[axis * 4 + 3] -= component * light[i];
            }
        }
        for( int i = 0; i < 4; ++i ) {
            m[12 + i] = m[8 + i];
            m[8 + i] *= depth_scale;
        }
        m[11] -= near_plane * depth_scale;
    }
    return faces;
}

struct ndc_point {
    float x, y, z, forward_depth;
};
inline std::optional<ndc_point> project( const camera &view, float east,
                                         float height, float south )
{
    const auto m = clip_from_world( view );
    if( !m ) {
        return std::nullopt;
    }
    const auto clip = m->transform( east, height, south );
    if( !std::isfinite( clip[3] ) || clip[3] < view.near_clip || clip[3] > view.far_clip ) {
        return std::nullopt;
    }
    return ndc_point{ clip[0]/clip[3], clip[1]/clip[3], clip[2]/clip[3], clip[3] };
}
} // namespace fp3d_gpu_camera_math
#endif
