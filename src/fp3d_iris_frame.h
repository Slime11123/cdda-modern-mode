#pragma once
#ifndef CATA_SRC_FP3D_IRIS_FRAME_H
#define CATA_SRC_FP3D_IRIS_FRAME_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "fp3d_gpu_camera_math.h"

class time_point;
namespace fp3d_camera
{
struct pose;
}
namespace fp3d_snapshot
{
struct scene_snapshot;
struct scene_biome;
}

namespace fp3d_iris
{
struct world_clock {
    int world_time;
    int world_day;
    float sun_angle = 0.f;
    int moon_phase = 4;
};

world_clock read_world_clock( const time_point &time );

struct biome_inputs {
    int id = 0;
    int category = 5;
    int precipitation = 1;
    float rainfall = 0.f;
    float temperature = 0.f;
};

biome_inputs map_scene_biome( const fp3d_snapshot::scene_biome &biome );
std::optional<int> biome_constant( std::string_view name );

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
    int frame_counter = 0;
    biome_inputs biome;
    std::array<int, 2> eye_brightness{};
    std::array<float, 3> sky_color{};
    std::array<float, 3> fog_color{};
    float fog_start = 0.f;
    float fog_distance = 0.f;
    bool blind = false;
    bool darkness = false;
    float night_vision = 0.f;
    int held_light = 0;
};

class frame_history
{
    public:
        std::optional<frame_inputs> prepare( const fp3d_snapshot::scene_snapshot &scene,
                const fp3d_camera::pose &camera, const fp3d_gpu_camera_math::matrix4 &clip_from_scene,
                int width, int height, bool overhead, world_clock clock, std::uint64_t history_epoch,
                std::array<int, 2> eye_brightness = {}, float lightning_flash = 0.f ) const;
        // Commit only after presentation. The caller changes epoch when
        // replacing the pack, reloading the world, or discarding GPU history.
        void presented( const frame_inputs &frame );
        void reset();

    private:
        std::optional<frame_view> last_view_;
        std::uint64_t last_epoch_ = 0;
        int next_frame_counter_ = 0;
};
} // namespace fp3d_iris
#endif
#endif
