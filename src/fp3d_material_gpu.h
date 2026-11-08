#pragma once
#ifndef CATA_SRC_FP3D_MATERIAL_GPU_H
#define CATA_SRC_FP3D_MATERIAL_GPU_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <vector>
#include "fp3d_materials.h"
#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

namespace fp3d_material_gpu
{
// All GL functions require the live SDL compositor context.
// CPU-side auxiliary() and prefetch_auxiliary() must run before SDL_RenderFlush.
struct status {
    GLuint normal = 0;
    GLuint orm = 0;
    unsigned revision = 0;
    unsigned uploaded = 0;
    unsigned attempted = 0;
    bool failed = false;
};
status current();
// Creates two additional, UV-aligned 2048x2048 RGBA8 atlases when explicitly
// requested. Existing color atlas, lighting, and shader paths stay untouched.
bool sync( unsigned revision, const std::vector<fp3d_materials::auxiliary_tile> &tiles );
// QA-only readback of actual GPU texels, not a simulated CPU unit test.
bool verify_texels( const std::vector<fp3d_materials::auxiliary_tile> &tiles );
// QA-only side-by-side actual GPU sampling: never part of the shipped world view.
bool draw_qa_preview( const std::vector<fp3d_materials::auxiliary_tile> &tiles );
void release();
} // namespace fp3d_material_gpu
#endif
#endif // CATA_SRC_FP3D_MATERIAL_GPU_H
