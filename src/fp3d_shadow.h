#pragma once
#ifndef CATA_SRC_FP3D_SHADOW_H
#define CATA_SRC_FP3D_SHADOW_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <cstdint>
#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif
namespace fp3d_snapshot { struct scene_snapshot; }
namespace fp3d_shadow
{
struct data {
    GLuint texture = 0;
    float radius = 30;
    float distance = 64;
    float strength = 0;
    std::array<float, 3> x = { 1, 0, 0 };
    std::array<float, 3> y = { 0, 1, 0 };
    std::array<float, 3> sun = { 0, 1, 0 };
    int resolution = 0;
    unsigned furniture_casters = 0;
    unsigned street_light_casters = 0;
    unsigned mailbox_casters = 0;
    unsigned streetlights_with_ceiling = 0;
    // Populated only for QA capture, never an extra gameplay readback.
    unsigned qa_depth_occupied_texels = 0;
    std::uint64_t qa_depth_hash = 0;
};
// Restricted nearby daylight shadow-map pass, only for known terrain geometry.
// Never queries game simulation or hidden world entities.
data render( const fp3d_snapshot::scene_snapshot &scene, int quality );
void release();
} // namespace fp3d_shadow
#endif
#endif
