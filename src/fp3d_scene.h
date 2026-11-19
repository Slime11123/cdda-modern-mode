#pragma once
#ifndef CATA_SRC_FP3D_SCENE_H
#define CATA_SRC_FP3D_SCENE_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include "fp3d_camera.h"
#include "fp3d_snapshot.h"
#include "fp3d_targeting.h"
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
    // A planning symbol only: -1 down, +1 up. Never opens a floor or shows another level.
    int vertical_hint = 0;
};
struct draw_statistics {
    unsigned chunks = 0;
    unsigned rebuilt = 0;
    double cpu_ms = 0;
    unsigned indexed_cells = 0;
    unsigned landscape_chunks = 0;
    unsigned landscape_rebuilt = 0;
    // P0-V coverage counters based on the current native-visible snapshot.
    unsigned native_loaded_vehicles = 0;
    unsigned visible_vehicles = 0;
    unsigned visible_vehicle_parts = 0;
};
draw_statistics statistics();
// Presentation height shared by opaque water and its depth-tested material mask.
float water_surface_offset( const std::string &terrain );
// Skip the Ultra reflection FBO and SSR sampling when the snapshot contains
// no reflective water or wet pavement. Cached per published generation.
bool has_reflective_surfaces( const fp3d_snapshot::scene_snapshot &scene, float rain );
// Draw the native visible, material-classified road/water reflectivity mask
// into a separate color FBO sharing the world depth attachment. World depth
// must be read-only; do not infer or reveal unseen map geometry.
void draw_reflectivity_mask( const fp3d_snapshot::scene_snapshot &scene,
                             float yaw, float pitch, float aspect,
                             fp3d_camera::offset camera, float rain );
// Reuse the *actual visible scene* ghost-roof mask for depth-only sunlight
// shadow rendering. False for a stale/different snapshot generation.
bool vehicle_roof_sheet_suppressed( const tripoint_abs_ms &position,
                                    unsigned render_generation );
// Call before destroying the current OpenGL context.
void release();
// Clear the current frame's avatar/hand/crosshair cutouts from a bound
// reflectivity FBO. Call after draw() and draw_reflectivity_mask().
void exclude_navigation_overlays_from_reflectivity();
// Draws only owned snapshot data within the compositor's saved GL state/FBO.
// overhead switches the hand overlay, the centre crosshair and the avatar's own
// tile sprite. Everything else (world geometry, lighting, shadows, reflections)
// is unchanged, so both cameras share one rendering path.
void draw( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch,
           float aspect, fp3d_camera::offset offset, const std::optional<blueprint> &preview = std::nullopt,
           bool interactable = false, float swing = 0,
           const std::optional<fp3d_targeting::target> &aim = std::nullopt,
           bool overhead = false );
} // namespace fp3d_scene
#endif
#endif // CATA_SRC_FP3D_SCENE_H
