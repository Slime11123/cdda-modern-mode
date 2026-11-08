#pragma once
#ifndef CATA_SRC_FP3D_VEHICLE_RENDERER_H
#define CATA_SRC_FP3D_VEHICLE_RENDERER_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdint>
#include <string>
#include <vector>

#include "coordinates.h"
#include "fp3d_camera.h"

namespace fp3d_snapshot
{
struct scene_snapshot;
}

namespace fp3d_vehicle
{
enum class role : unsigned char {
    frame, wheel, door, window, roof, seat, engine, cargo, armor, body, light, other
};

// Entirely owned game-thread snapshot. No map, vehicle or part pointers
// survive into the OpenGL render thread.
struct part_view {
    int index = -1;
    int mount_x = 0;
    int mount_y = 0;
    // Actual native tile of this particular mounted part; its rounded
    // occupancy is ONLY for native interaction (never for 3D geometry).
    tripoint_abs_ms native_position = tripoint_abs_ms::zero;
    std::string id;
    std::string variant;
    std::string looks_like;
    role kind = role::other;
    bool broken = false;
    bool open = false;
    bool glazed_door = false; // CDDA DOOR+WINDOW must not become a windshield
    bool hidden = false;
    float health = 1;
};
struct vehicle_view {
    tripoint_abs_ms origin;
    std::string name;
    float angle_degrees = 0;
    int pivot_x = 0;
    int pivot_y = 0;
    int real_parts = 0;
    std::vector<part_view> parts; // only native-visible, non-fake parts
};

// Pure data operations are testable without GL and independently of pose.
std::uint64_t topology_signature( const vehicle_view &vehicle );
void release(); // called before GL context or material atlas is destroyed
void draw( const fp3d_snapshot::scene_snapshot &scene,
           const fp3d_camera::frustum &view, fp3d_camera::offset camera );
// Draw exactly the opaque mesh used by the vehicle pass into a depth-only
// sunlight FBO. Shadow callers must set up their projection and color mask.
// Deliberately excludes separately alpha-blended window panes.
void draw_shadow( const fp3d_snapshot::scene_snapshot &scene, float radius );
// Real alpha-blended windows, drawn after opaque geometry and actors.
void draw_glass( const fp3d_snapshot::scene_snapshot &scene,
                 const fp3d_camera::frustum &view, fp3d_camera::offset camera );
} // namespace fp3d_vehicle
#endif
#endif
