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
#include "fp3d_vehicle_renderer.h"

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
// Follow the complete native 3D sight range (10 levels), not an arbitrary
// four-up/two-down slice. CDDA's loaded map is capped by OVERMAP_HEIGHT.
// On an elevated floor we must still retain z=0, plus nearby basements.
// Limit unneeded underground levels when outdoors to keep capture cost low.
constexpr int render_above = 10;
constexpr int render_below = 10;
constexpr int below_for_storey( int player_z )
{
    return player_z >= 0 ?
           ( player_z + 2 < render_below ? player_z + 2 : render_below ) : 2;
}

enum class knowledge { visible, remembered };
enum class window_pane { glazed, opening, covered };

enum class terrain_kind { floor, wall, closed_door, open_door, window, obstacle, glass, damaged_door, fence, ladder, downspout };
// Read-only terrain-definition classification, also used by construction ghosts.
terrain_kind terrain_shape( const std::string &id );
// Door/frame parts in their east-west orientation, shared by drawing and picking.
const std::vector<fp3d_furniture::part> &door_parts( terrain_kind kind );
std::vector<fp3d_furniture::part> fence_parts( const std::array<bool, 4> &neighbours,
        bool posts_only = false );
// True when adjacent segments belong to one continuous real-world railing.
bool fence_connects( const std::string &left, const std::string &right );
// Chain-link fence gates use a dedicated open steel-frame model, including the
// locked, closed and swung-open native states. Orientation is derived only
// from already-visible neighbours, not hidden map data.
bool is_chain_gate( const std::string &id );
bool chain_gate_along_x( const std::array<bool, 4> &neighbours );
std::vector<fp3d_furniture::part> chain_gate_parts( const std::string &id, bool along_x );
// Custom solid models shared by scene drawing and pointer targeting. Empty
// for terrain that retains the existing world geometry path.
std::vector<fp3d_furniture::part> terrain_model_parts( const std::string &id,
        const std::array<bool, 4> &neighbours = {} );

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
    // A real exposed overhead metal walkway may not have its own visible
    // endpoint in native 3D LOS. Texture only its underside from this
    // presentation-only boundary, never copy upstairs contents.
    std::string ceiling_surface;
    bool floor_present = true;
    // Visual-only material override for a wooden railing built on the
    // exposed metal walkway of a radio tower. Determined from real adjacent
    // terrain while the cell is visible, not from partially visible sprites.
    bool metal_deck_railing = false;
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
    bool revivable = false;   // Native corpse.can_revive(); visual badge only.
    bool alerted = false;     // Hostile monster currently sees the avatar.
    bool bleeding = false;    // Native effect_bleed; no gameplay simulation.
    // Extra visible native effect badges, ordered by urgency; at most four total.
    std::vector<std::string> status_icons;
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
    // P0-V: one owned, native-perception-filtered copy per loaded vehicle.
    std::vector<fp3d_vehicle::vehicle_view> vehicles;
    // Native BOARDABLE mount position of the current avatar, if actually
    // onboard. The compositor must never derive this from rounded precalc.
    std::optional<fp3d_camera::rider_pose> rider;
    unsigned enumerated_vehicles = 0;
    unsigned visible_vehicle_parts = 0;
    std::optional<sprite> held_item;
    bool player_crouching = false;
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
