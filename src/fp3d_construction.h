#pragma once
#ifndef CATA_SRC_FP3D_CONSTRUCTION_H
#define CATA_SRC_FP3D_CONSTRUCTION_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "type_id.h"
#include "fp3d_scene.h"
#include "coordinates.h"
struct construction;
class avatar;
class map;
namespace fp3d_targeting
{
struct target;
}
namespace fp3d_construction
{
struct project {
    construction_group_str_id id;
    std::string name;
    std::string category;
};
std::vector<project> catalogue();
// Optional crafter adds native material/tool/knowledge checks, before choosing a site.
std::string details( const construction_group_str_id &group, avatar *crafter = nullptr );
// Implemented beside the native menu so its private legality checks are reused.
bool begin( const construction_group_str_id &group );
// Read-only legality check shared by the preview and its final position confirmation.
bool preview_allowed( avatar &you, map &here, const construction &project,
                      const fp3d_targeting::target &target );
// Single-cell completed terrain, without changing the map or running post-effects.
std::optional<fp3d_snapshot::cell> terrain_preview( const construction &project,
        const tripoint_abs_ms &position );
// Owned placement display, including a direction marker for conditional excavation.
fp3d_scene::blueprint placement_preview( const construction &project,
        const tripoint_abs_ms &position, bool valid );
// Only selects a native validated position; never consumes resources or starts work.
std::optional<tripoint_bub_ms> choose( const std::map<tripoint_bub_ms, const construction *>
                                       &valid );
}
#endif
#endif
