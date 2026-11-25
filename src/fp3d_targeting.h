#pragma once
#ifndef CATA_SRC_FP3D_TARGETING_H
#define CATA_SRC_FP3D_TARGETING_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "coordinates.h"
#include "fp3d_camera.h"
namespace fp3d_snapshot
{
struct scene_snapshot;
}
namespace fp3d_targeting
{
enum class kind { terrain, creature, item, furniture, appliance, vehicle };
struct edge_outline {
    // Vertices in renderer order (east, height, south), relative to the scene origin.
    std::array<std::array<float, 3>, 4> corners;
    float height = 0;
    std::array<float, 4> color{ 1.f, 1.f, 1.f, 1.f };
    // Optional alpha silhouette instead of the four bounding edges.
    std::vector<std::array<std::array<float, 3>, 2>> segments;
};
struct target {
    tripoint_abs_ms origin;
    tripoint_abs_ms position;
    kind type = kind::terrain;
    std::string id;
    std::optional<edge_outline> outline = std::nullopt;
    // Vehicle-only identity. No pointers escape the game-thread snapshot.
    int vehicle_part = -1;
    std::optional<tripoint_abs_ms> vehicle_origin;
    int vehicle_mount_x = 0;
    int vehicle_mount_y = 0;
};
// Snapshot-only ray selection. Interactions default to six tiles; shooting
// may query further up to the native weapon's range.
// Remembered/enclosing surfaces block selection but cannot become targets.
// Native interaction reach and perception must still be checked before acting.
// screen_x/y are normalised device coordinates (-1..1, +y up) inside the world
// viewport; the first-person reticle uses the centre (0, 0). The overhead view
// passes the mouse position so picking and rendering share one camera. aspect
// is the world viewport's width/height, the same one the projection uses.
std::optional<target> ray( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch,
                           fp3d_camera::offset offset = {}, float max_reach = 6.0f,
                           float screen_x = 0.0f, float screen_y = 0.0f, float aspect = 1.0f,
                           bool camera_facing_sprites = false );

// First stage: one neighbouring tile in the camera direction, same storey.
// Looking down selects the player's feet. Memory and facades are excluded.
std::optional<target> adjacent( const fp3d_snapshot::scene_snapshot &scene, float yaw,
                                float pitch );
}
#endif
#endif
