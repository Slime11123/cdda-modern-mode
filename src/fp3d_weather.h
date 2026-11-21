#pragma once
#ifndef CATA_SRC_FP3D_WEATHER_H
#define CATA_SRC_FP3D_WEATHER_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include "coordinates.h"

struct weather_type;
namespace fp3d_weather
{
enum class precipitation { none, rain, snow, anomaly };
struct state {
    precipitation particles = precipitation::none;
    float intensity = 0;
    float clouds = 0;
    float fog_end = 0;
    float wind_x = 0;
    float wind_z = 0;
    bool outdoors = false;
    bool lightning = false;
    // Exposed floor positions only, copied on the game thread. No map reads in draw().
    std::vector<tripoint_abs_ms> columns;
};
struct linear_fog {
    std::array<float, 4> color{};
    float start = 0;
    float end = 0;
};
linear_fog fog_parameters( const state &weather, float daylight, float flash, float far_scale );
state sample( const weather_type &type, int windspeed, int winddirection,
              bool lightning, bool outdoors );
// A native lightning event gets one brief presentation pulse even while paused.
float flash( const state &weather, std::uint64_t milliseconds );
struct particle {
    float x, y, z;
    float end_x, end_y, end_z;
};
void visit_particles( const state &weather, const tripoint_abs_ms &origin,
                      std::uint64_t milliseconds, const std::function<void( particle )> &visit );

}
#endif
#endif
