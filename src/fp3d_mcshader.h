#pragma once
#ifndef CATA_SRC_FP3D_MCSHADER_H
#define CATA_SRC_FP3D_MCSHADER_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <filesystem>
#include <string>
namespace fp3d_mcshader
{
struct profile {
    bool recognized = false;
    bool direct_final = false;
    bool photon = false;
    bool shadows = true;
    bool bloom = true;
    bool volumetric = true;
    bool clouds = true;
    bool ao = true;
    bool reflections = false;
    float saturation = 1.15f;
    float warmness = 0.0f;
    float glow = 0.85f;
    bool has_water_waves = false;
    float water_texture = 0;
    float water_wave_strength = 1;
    struct wave_settings {
        int iterations = 3;
        float frequency = 1;
        float persistence = 1;
        float lacunarity = 1;
        float speed_still = 1;
        float speed_flowing = 1;
        bool height_variation = false;
    } water_waves;
    float water_absorption[3] = { .39f, .14f, .07f };
    std::string label;
};
// Reads shaderpack metadata and native GPU effect parameters.
profile inspect( const std::filesystem::path &path );
} // namespace fp3d_mcshader
#endif
#endif
