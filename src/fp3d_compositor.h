#pragma once
#ifndef CATA_SRC_FP3D_COMPOSITOR_H
#define CATA_SRC_FP3D_COMPOSITOR_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <string>
#include <array>
#include <vector>
#include "fp3d_targeting.h"
#include "fp3d_scene.h"
#include "fp3d_input.h"
#include "fp3d_hud.h"

#include "sdl_wrappers.h"
#include "coordinates.h"
#include "point.h"

// SDL3 GPU compositor, called only from the existing game thread.
namespace fp3d_snapshot
{
struct scene_snapshot;
}
class avatar;
class map;
namespace fp3d_compositor
{
// Native transient map feedback, copied before the covered 2D pass clears it.
struct map_overlay {
    tripoint_abs_ms position;
    std::string id;
    int rotation = 0;
    std::string text;
    int color = 0;
};
void clear_map_overlays();
void add_map_overlay( map_overlay overlay );
const std::vector<map_overlay> &map_overlays();
bool requested();
bool view_3d();
bool default_view_3d();
bool default_modern_controls( bool three_d );
void apply_modes( bool three_d, bool modern );
// Exercise the legacy three presets in renderer regression checks.
void cycle_mode();
fp3d_input::mode controls();
// The 3D camera type is its own axis, independent of "3D rendering is on".
// F6 flips it inside the 3D world; nothing here is a 2D mode.
bool overhead();
bool preferred_overhead();
void toggle_camera();
// Per-character save preference, independent of global rendering defaults.
std::string saved_view_mode();
void restore_view_mode( const std::string &mode );
// Modern movement follows the active camera heading, including overhead orbit.
float movement_heading();
// Read-only presentation state of the overhead camera (HUD and regression
// tests). Never a perception input.
float overhead_azimuth();
float overhead_distance();
// The native tile under the mouse cursor inside the 3D world viewport. Only
// natively visible, non-facade cells on the avatar's own storey qualify.
std::optional<tripoint_abs_ms> pointer_tile();
bool overhead_tile_visible( const tripoint_abs_ms &position );
// Overhead right-click: the 3D profile binds the right button to aiming, so the
// native secondary-select binding can never arrive. The compositor owns the
// click and hands the world dispatcher one plain "SEC_SELECT" request instead,
// which then opens the ordinary modern 2D dropdown on the picked tile.
bool take_secondary_select( point *mouse = nullptr );
std::optional<tripoint_abs_ms> take_secondary_select_target();
bool modern_2d_input();
bool modern_2d_world();
bool modern_2d_overlay_open();
// Shared modern interfaces, independent of 3D scene capture and movement controls.
bool survival_menu_available();
// UI style is independent of the selected control/view mode.
bool uses_hud();
void publish_hud( fp3d_hud::data hud );
// True only while the 3D view is allowed to render, including its first frame.
bool active();
void attach( SDL_Renderer *renderer, SDL_Window *window );
bool process_event( const SDL_Event &event );
bool captures_game_input();
// Text composition belongs only to an active field in the modern panel.
bool menu_text_input();
// The opaque 3D scene replaces this exact native map viewport.
bool covers_map( const tripoint_abs_ms &center );
float heading();
void set_blueprint( std::optional<fp3d_scene::blueprint> preview );
bool turn_based();
std::vector<std::string> hud_lines();
// Borrowed until the next HUD/scene publication or compositor reset.
const fp3d_hud::data &hud_data();
const std::vector<fp3d_hud::data::message_entry> &target_details();
bool interaction_available();
std::string interaction_prompt();
bool draw_menu_background( int x, int y, int width, int height );
std::array<int, 4> viewport_pixels();
SDL_Texture_Ptr copy_view();
std::optional<fp3d_targeting::target> interaction_target();
// The center-reticle world target for immediate 3D gunfire (not an interaction target).
std::optional<tripoint_abs_ms> firing_target( int range );
void set_viewport( int x, int y, int width, int height, int display_width, int display_height );
void set_input_context( const std::string &category );
void publish( fp3d_snapshot::scene_snapshot scene );
void publish_camera( avatar &you, const map &here, bool cut_overhead );
// Explicit native GPU migration QA; never changes the normal render mode.
bool gpu_world_qa_requested();
void capture_gpu_world_qa( avatar &you, const map &here );
void gpu_world_qa_after_present( bool presented );
void gpu_world_qa_capture_composite();
void begin_action();
void melee_swing();
struct hit_feedback {
    int damage = 0;
    int hp = 0;
    int maximum = 0;
    float progress = 0;
};
// Read-only rendering feedback from damage actually resolved by native combat.
void record_hit( const tripoint_abs_ms &position, int damage, int hp, int maximum );
float hit_flash( const tripoint_abs_ms &position );
std::optional<hit_feedback> latest_hit();
void movement_finished( const tripoint_abs_ms &before, const tripoint_abs_ms &after,
                        const tripoint_rel_ms &expected );
bool tick();
void compose();
void release();
} // namespace fp3d_compositor
#endif
#endif // CATA_SRC_FP3D_COMPOSITOR_H
