#pragma once
#ifndef CATA_SRC_FP3D_LOCAL_LIGHTS_H
#define CATA_SRC_FP3D_LOCAL_LIGHTS_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <string>
#include <vector>
#include "fp3d_snapshot.h"
#include "fp3d_camera.h"

namespace fp3d_local_lights
{
constexpr int max_active = 8;
struct selected {
    std::array<float, 4> position = {}; // snapshot-relative metres xyz + radius
    std::array<float, 4> color = {};    // RGB radiance + strength
    float beam = 0.0f;
    bool held_beam = false;
    bool fixture = false;
};
std::vector<selected> select( const fp3d_snapshot::scene_snapshot &scene,
                              fp3d_camera::offset camera );
// Extends only the built-in CDDA/Photon visual bridge, never an imported final.fsh.
std::string augment_fragment( const std::string &fragment );
} // namespace fp3d_local_lights
#endif
#endif
