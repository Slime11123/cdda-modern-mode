#pragma once
#ifndef CATA_SRC_FP3D_SNAPSHOT_H
#define CATA_SRC_FP3D_SNAPSHOT_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <string>
#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "coordinates.h"
#include "calendar.h"
#include "map_scale_constants.h"
#include "fp3d_sky.h"
#include "fp3d_weather.h"
#include "fp3d_hud.h"
#include "fp3d_furniture.h"

class avatar;
class item;
class map;

namespace fp3d_snapshot
{
std::string item_sprite_id( const item &object );

// Cover the native loaded visibility range, independently of 2D tile zoom.
constexpr int radius = MAX_VIEW_DISTANCE;
// Coarse outdoor landscape beyond the detailed native reality bubble.
constexpr int landscape_radius = 240;
constexpr int landscape_patch_size = 12;
constexpr int city_radius = 960;
constexpr int render_above = 4;
constexpr int render_below = 2;

enum class knowledge { visible, remembered };
enum class window_pane { glazed, opening, covered };

enum class terrain_kind { floor, wall, closed_door, open_door, window, obstacle, glass, damaged_door, fence, ladder, downspout };
// Read-only terrain-definition classification, also used by construction ghosts.
terrain_kind terrain_shape( const std::string &id );
// Door/frame parts in their east-west orientation, shared by drawing and picking.
const std::vector<fp3d_furniture::part> &door_parts( terrain_kind kind );
std::vector<fp3d_furniture::part> fence_parts( const std::array<bool, 4> &neighbours,
        bool posts_only = false );

struct stair_section {
    int rows = 1;
    int width = 1;
    int south = 0;
    int east = 0;
};

struct cell {
    tripoint_abs_ms position;
    std::string terrain;
    terrain_kind kind = terrain_kind::floor;
    knowledge known = knowledge::visible;
    std::string vegetation;
    std::optional<fp3d_furniture::model> furniture;
    std::string appliance;
    std::optional<stair_section> stairs;
    std::string blood;
    int blood_intensity = 0;
    float vegetation_height = 0;
    window_pane pane = window_pane::glazed;
    bool exterior_only = false;
    // A visible room's immediate overhead boundary, without upstairs identity.
    bool ceiling_present = false;
    bool floor_present = true;
    bool open_air = false;
    bool goes_up = false;
    bool goes_down = false;
    bool is_ramp = false;
    int ramp_delta = 0;
};

enum class sprite_kind { monster, character, item, held_item };

struct sprite {
    tripoint_abs_ms position;
    sprite_kind kind = sprite_kind::monster;
    std::string id;
    std::string variant;
    std::vector<std::pair<std::string, std::string>> overlays;
    bool male = true;
    bool mirrored = false;
    bool multiple_items = false;
    float height = 1.8f;
    float elevation = 0;
};

struct landscape_patch {
    tripoint_abs_ms position; // Northwest tile centre of a square patch.
    std::string terrain;
    bool wooded = false;
    int width = landscape_patch_size;
    int depth = landscape_patch_size;
    float building_height = 0;
    bool ground_present = true;
};

// Owned presentation data. No map/entity pointers, simulation state or setters.
struct scene_snapshot {
    tripoint_abs_ms origin;
    unsigned render_generation = 0; // Assigned when publishing owned render data.
    season_type season = SPRING;
    fp3d_sky::state sky;
    fp3d_weather::state weather;
    fp3d_hud::data hud;
    std::vector<cell> cells;
    std::vector<landscape_patch> landscape;
    std::vector<sprite> sprites;
    std::optional<sprite> held_item;
    int min_z = 0;
    int max_z = 0;
    double capture_ms = 0;
    unsigned visible = 0;
    unsigned remembered = 0;
    std::vector<tripoint_abs_ms> interactable;
};

// Connect permitted adjacent stair tiles into rectangular flights.
void connect_stairs( scene_snapshot &scene );
std::vector<fp3d_furniture::part> stair_parts( const cell &tile );
// Vertical rails/rungs and round pipe sections, shared by rendering and picking.
std::vector<fp3d_furniture::part> climb_parts( const cell &tile );

// Prepare native view caches independently of the 2D Tiles draw or exploration.
void prepare_view( avatar &you, map &here );
// The game thread captures owned data after preparing the native view caches.
// A dirty or uninitialized visibility cache permits memory only.
// Optional ranges narrow the prepared budget; they cannot extend it.
scene_snapshot capture( avatar &you, const map &here,
                        int above = render_above, int below = render_below );
} // namespace fp3d_snapshot
#endif
#endif // CATA_SRC_FP3D_SNAPSHOT_H
