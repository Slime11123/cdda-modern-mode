#pragma once
#ifndef CATA_SRC_FP3D_SCENE_H
#define CATA_SRC_FP3D_SCENE_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include "fp3d_camera.h"
#include "fp3d_snapshot.h"
#include "coordinates.h"
#include <optional>
#include <string>
namespace fp3d_snapshot
{
struct scene_snapshot;
}
namespace fp3d_scene
{
struct blueprint {
    tripoint_abs_ms position;
    float height = 1;
    bool valid = false;
    std::string name;
    std::optional<fp3d_snapshot::cell> terrain;
};
struct draw_statistics {
    unsigned chunks = 0;
    unsigned rebuilt = 0;
    double cpu_ms = 0;
    unsigned indexed_cells = 0;
    unsigned landscape_chunks = 0;
    unsigned landscape_rebuilt = 0;
};
draw_statistics statistics();
// Call before destroying the current OpenGL context.
void release();
// Draws only owned snapshot data within the compositor's saved GL state/FBO.
void draw( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch,
           float aspect, fp3d_camera::offset offset, const std::optional<blueprint> &preview = std::nullopt, bool interactable = false );
} // namespace fp3d_scene
#endif
#endif // CATA_SRC_FP3D_SCENE_H
