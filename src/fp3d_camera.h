#pragma once
#ifndef CATA_SRC_FP3D_CAMERA_H
#define CATA_SRC_FP3D_CAMERA_H

#include "coordinates.h"
#include "map_scale_constants.h"

namespace fp3d_camera
{
constexpr float horizontal_fov_degrees = 120;
constexpr float floor_height = 2.2f;
constexpr float eye_height = 1.6f;
// Preserve enough depth precision for nearby floors after z-level changes.
// The 3D FBO uses a 24-bit depth buffer and draws distant landscape silhouettes.
constexpr float near_clip = .20f;
constexpr float far_clip = 1536.0f;
float horizontal_tangent();

// Camera-space bounds for rendering only; this never determines perception.
class frustum
{
    public:
        frustum( float yaw, float pitch, float aspect );
        bool contains( float x, float y, float z, float radius ) const;
        float right_x() const {
            return cos_yaw;
        }
        float right_z() const {
            return sin_yaw;
        }
    private:
        float cos_yaw;
        float sin_yaw;
        float cos_pitch;
        float sin_pitch;
        float tan_horizontal;
        float tan_vertical;
        float horizontal_margin;
        float vertical_margin;
};

// Forward/right components are camera-relative. The result is a native grid step.
tripoint_rel_ms movement( float yaw, int forward, int right );

struct offset {
    float x = 0;
    float y = 0;
};

// Presentation time only; attacks and damage are settled by native gameplay.
class swing
{
    public:
        void start( unsigned now );
        float amount( unsigned now ) const;
        void reset();
    private:
        bool active = false;
        unsigned started = 0;
};

// Presentation only: accepts confirmed positions, never movement intentions.
class follower
{
    public:
        void confirm( const tripoint_abs_ms &position, unsigned now, bool smooth = false );
        offset relative_to( const tripoint_abs_ms &origin, unsigned now ) const;
        void reset();
    private:
        bool initialized = false;
        tripoint_abs_ms target = tripoint_abs_ms::zero;
        double start_x = 0;
        double start_y = 0;
        unsigned started = 0;
        unsigned duration = 100;
};
} // namespace fp3d_camera
#endif
