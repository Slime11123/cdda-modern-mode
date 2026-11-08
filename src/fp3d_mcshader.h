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
    std::string final_fragment;
    std::string label;
    std::string note;
};
// Imports only metadata (plus a simple standalone legacy GLSL 1.20 final pass).
// Full Iris/OptiFine G-buffer/composite programs cannot execute in this backend.
profile inspect( const std::filesystem::path &path );
std::string bridge_fragment( const profile &pack, const std::string &builtin );
} // namespace fp3d_mcshader
#endif
#endif
