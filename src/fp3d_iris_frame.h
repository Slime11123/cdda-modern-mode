#pragma once
#ifndef CATA_SRC_FP3D_IRIS_FRAME_H
#define CATA_SRC_FP3D_IRIS_FRAME_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <cstdint>
#include <optional>

#include "fp3d_gpu_camera_math.h"

class time_point;
namespace fp3d_camera
{
struct pose;
}
namespace fp3d_snapshot
{
struct scene_snapshot;
}

namespace fp3d_iris
{
struct world_clock {
    int turn;
    int seconds_since_midnight;
};

world_clock read_world_clock( const time_point &time );

struct frame_view {
    // Native clip space uses [0, 1] depth. The draw backend applies any
    // pack-specific coordinate and matrix layout conversions.
    fp3d_gpu_camera_math::matrix4 clip_from_scene;
    std::array<int, 3> map_origin;
    std::array<float, 3> eye_in_scene;
    int width;
    int height;
    bool overhead;
    // Iris isEyeInWater uses 0 for air and 1 for water.
    int eye_in_water = 0;
};

struct frame_inputs {
    frame_view current;
    std::optional<frame_view> previous;
    world_clock clock;
    std::uint64_t history_epoch;
    std::array<float, 3> sun_direction;
    std::array<float, 2> wind;
    float daylight;
    float twilight;
    float moonlight;
    float rain_strength;
    float snow_strength;
    float cloud_cover;
    float fog_end;
    bool sky_visible;
    bool outdoors;
    bool lightning;
};

class frame_history
{
    public:
        std::optional<frame_inputs> prepare( const fp3d_snapshot::scene_snapshot &scene,
                const fp3d_camera::pose &camera, const fp3d_gpu_camera_math::matrix4 &clip_from_scene,
                int width, int height, bool overhead, world_clock clock, std::uint64_t history_epoch ) const;
        // Commit only after presentation. The caller changes epoch when
        // replacing the pack, reloading the world, or discarding GPU history.
        void presented( const frame_inputs &frame );
        void reset();

    private:
        std::optional<frame_view> last_view_;
        std::uint64_t last_epoch_ = 0;
};
} // namespace fp3d_iris
#endif
#endif
