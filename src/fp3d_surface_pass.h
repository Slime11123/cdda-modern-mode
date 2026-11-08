#pragma once
#ifndef CATA_SRC_FP3D_SURFACE_PASS_H
#define CATA_SRC_FP3D_SURFACE_PASS_H
#if defined(TILES) && defined(ENABLE_FP3D)
#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif
#include "fp3d_camera.h"
namespace fp3d_snapshot { struct scene_snapshot; }
namespace fp3d_surface_pass
{
// Isolated, optional P5-B prototype; this FBO never replaces game colour.
struct result {
    GLuint texture = 0;
    GLuint framebuffer = 0;
    unsigned floors = 0;
    unsigned wall_faces = 0;
    unsigned covered_pixels = 0;
    unsigned upward_pixels = 0;
    unsigned upright_wall_pixels = 0;
    // CPU + driver sync + GL readback, not an isolated GPU timer query.
    double cpu_readback_ms = 0.0;
    bool readback_performed = false;
    bool valid = false;
};
// Requires a borrowed current OpenGL context and previously rendered scene
// depth. Reprojects only known/visible wall and ground geometry.
result render( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch,
               float aspect, fp3d_camera::offset camera, int width, int height,
               GLuint existing_scene_depth, GLuint normal_atlas, GLuint orm_atlas,
               bool readback_stats = true );
bool draw_qa_preview();
void release();
} // namespace fp3d_surface_pass
#endif
#endif // CATA_SRC_FP3D_SURFACE_PASS_H
