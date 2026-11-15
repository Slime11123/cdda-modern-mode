#pragma once
#ifndef CATA_SRC_FP3D_PBR_LIGHTING_H
#define CATA_SRC_FP3D_PBR_LIGHTING_H

#if defined(TILES) && defined(ENABLE_FP3D)
#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

namespace fp3d_pbr_lighting
{
struct result {
    GLuint texture = 0;
    GLuint framebuffer = 0;
    bool applied = false;
    bool failed = false;
    double submit_ms = 0;
};
// Light only the covered normal/ORM pixels. Returns scene_color unchanged on
// failure. Keep the existing Photon/MC post-pass and shadow maps downstream.
result apply( GLuint scene_color, GLuint scene_depth, GLuint surface,
              int width, int height, const float ( &sun )[3],
              float daylight, float clouds, float yaw, float pitch, float aspect,
              float intensity );
void release();
} // namespace fp3d_pbr_lighting
#endif
#endif // CATA_SRC_FP3D_PBR_LIGHTING_H
