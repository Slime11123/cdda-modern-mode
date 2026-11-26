#pragma once
#ifndef CATA_SRC_FP3D_IRIS_BLOCK_LIGHT_H
#define CATA_SRC_FP3D_IRIS_BLOCK_LIGHT_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "coordinates.h"

class map;

namespace fp3d_snapshot
{
struct scene_snapshot;
}

namespace fp3d_iris
{
inline constexpr int max_block_light = 15;

enum class light_face : std::uint8_t {
    east = 1, west = 2, south = 4, north = 8, up = 16, down = 32
};

struct block_light_cell {
    std::uint8_t open_faces = 0;
    // Cost to enter through an open face, including the one-level distance loss.
    std::uint8_t attenuation = 1;
    bool glass_above = false;

    bool passes( light_face face ) const {
        return ( open_faces & static_cast<std::uint8_t>( face ) ) != 0;
    }
};

struct block_light_source {
    tripoint_abs_ms position;
    int level = 0;
};

using block_light_topology = std::unordered_map<tripoint_abs_ms, block_light_cell>;
using block_light_levels = std::unordered_map<tripoint_abs_ms, std::uint8_t>;

// The input contains only cells with known render topology. Unlisted cells do
// not propagate or expose light. Sky light and native brightness are separate.
block_light_levels propagate_block_light( const block_light_topology &topology,
        const std::vector<block_light_source> &sources );

// Project already captured native emitters onto the compatibility scale.
// The caller supplies the native intensity corresponding to level 15.
block_light_levels scene_block_light( const fp3d_snapshot::scene_snapshot &scene,
                                      float full_level_emission );
// Exposures must come from native visibility/topology. They are not inferred
// from the absence of a ceiling in a partial snapshot.
block_light_levels scene_sky_light( const fp3d_snapshot::scene_snapshot &scene,
                                    const std::vector<tripoint_abs_ms> &exposed );
// Call on the game thread after capture and after the native caches are ready.
std::vector<tripoint_abs_ms> capture_sky_exposure( const fp3d_snapshot::scene_snapshot &scene,
        const map &here );
block_light_levels scene_sky_light( const fp3d_snapshot::scene_snapshot &scene, const map &here );

struct light_volume_bounds {
    tripoint_abs_ms origin;
    std::array<std::uint32_t, 3> extent{};
};

// Words are ordered east, south, then storey. Block light uses bits 0–3 and
// sky light uses bits 4–7. Values outside the bounds are omitted.
std::optional<std::vector<std::uint32_t>> pack_light_volume( const light_volume_bounds &bounds,
        const block_light_levels &block, const block_light_levels &sky );
} // namespace fp3d_iris
#endif
#endif
