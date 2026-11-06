#pragma once
#ifndef CATA_SRC_FP3D_TARGETING_H
#define CATA_SRC_FP3D_TARGETING_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <optional>
#include <string>
#include "coordinates.h"
#include "fp3d_camera.h"
namespace fp3d_snapshot
{
struct scene_snapshot;
}
namespace fp3d_targeting
{
enum class kind { terrain, creature, item, furniture, appliance };
struct edge_outline {
    // Vertices in renderer order (east, height, south), relative to the scene origin.
    std::array<std::array<float, 3>, 4> corners;
    float height = 0;
};
struct target {
    tripoint_abs_ms origin;
    tripoint_abs_ms position;
    kind type = kind::terrain;
    std::string id;
    std::optional<edge_outline> outline = std::nullopt;
};
// Snapshot-only selection along the camera ray, at most six map tiles.
// Remembered/enclosing surfaces block selection but cannot become targets.
// Native interaction reach and perception must still be checked before acting.
std::optional<target> ray( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch,
                           fp3d_camera::offset offset = {} );

// First stage: one neighbouring tile in the camera direction, same storey.
// Looking down selects the player's feet. Memory and facades are excluded.
std::optional<target> adjacent( const fp3d_snapshot::scene_snapshot &scene, float yaw,
                                float pitch );
}
#endif
#endif
