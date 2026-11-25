#pragma once
#ifndef CATA_SRC_FP3D_CAMERA_H
#define CATA_SRC_FP3D_CAMERA_H

#include "coordinates.h"
#include <array>
#include <optional>
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
        float up_x() const {
            return sin_pitch * sin_yaw;
        }
        float up_y() const {
            return cos_pitch;
        }
        float up_z() const {
            return -sin_pitch * cos_yaw;
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
    float height = eye_height; // camera and reticle must have the same eye
};

// The 3D overhead ("god view") camera. Presentation only: the raised eye never
// becomes a perception source, so this code never reads or changes the native
// visibility/lighting caches. Rotation and zoom must not reveal anything the
// avatar's own native 2D perception does not already know.
// The downward angle is fixed in the first version; middle-drag only turns the
// azimuth and the wheel only changes the distance.
constexpr float orbit_elevation_degrees = 60;
constexpr float orbit_min_distance = 6;
constexpr float orbit_max_distance = 48;
constexpr float orbit_default_distance = 18;
// Pivot height above the avatar's own floor. The look-at point is the centre of
// the tile the avatar stands on, so the screen centre is always that tile and
// rotating/zooming can never slide the player out of frame.
constexpr float orbit_pivot_height = 0;

struct orbit {
    float azimuth = 0;                          // degrees; 0 looks north
    float elevation = orbit_elevation_degrees;  // degrees below the horizon
    float distance = orbit_default_distance;    // tiles from eye to pivot
};

// One camera pose for the world FBO, the pointer ray and the reticle.
// pitch follows the engine convention: positive looks down.
struct pose {
    float yaw = 0;
    float pitch = 0;
    offset eye;
};

// Eye = pivot - forward * distance with the same forward convention as the
// first-person camera and fp3d_targeting::ray:
// ( cos(pitch) * sin(yaw), -sin(pitch), -cos(pitch) * cos(yaw) ).
// Horizontal coordinates use one renderer unit per tile; the vertical axis uses
// the same unit (floor_height per storey). Pivot is scene-relative, so the
// caller keeps using confirmed avatar positions.
pose orbit_pose( const orbit &view, float pivot_x, float pivot_y );
// NDC x/y and forward depth, using the world's exact perspective convention.
std::optional<std::array<float, 3>> project( const pose &camera, float aspect,
        float x, float height, float z );

// Intersect the pointer ray with an explicit scene-relative storey plane.
std::optional<std::array<float, 2>> pointer_floor( const pose &camera, float aspect,
        float ndc_x, float ndc_y, float height = 0 );

// Game-thread-owned native vehicle pose; never retains vehicle/part pointers.
struct rider_pose {
    double world_x = 0;
    double world_y = 0;
    float eye = 1.42f;
    float vehicle_angle = 0; // authoritative confirmed native facing
    int mount_x = 0;
    int mount_y = 0;
    int storey = 0;
    bool driver = false;
};

// Only interpolates already-confirmed native position. Does not accelerate,
// steer, change collision, perform a turn or update the avatar grid location.
class rider_follower
{
    public:
        // Returns native vehicle heading delta, for a free-look camera
        // which follows car steering without replacing mouse look input.
        float confirm( const rider_pose &seat, unsigned now );
        offset relative_to( const tripoint_abs_ms &origin, unsigned now ) const;
        void reset();
        bool active() const { return initialized; }
    private:
        bool initialized = false;
        rider_pose target;
        double begin_x = 0, begin_y = 0;
        float begin_eye = eye_height;
        unsigned started = 0;
        unsigned duration = 90;
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
