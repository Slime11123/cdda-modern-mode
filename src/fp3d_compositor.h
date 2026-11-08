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

// Windows/SDL2/OpenGL phase-1 probe, called only from the existing game thread.
namespace fp3d_snapshot
{
struct scene_snapshot;
}
namespace fp3d_compositor
{
bool requested();
bool view_3d();
void apply_modes( bool three_d, bool modern );
// Exercise the legacy three presets in renderer regression checks.
void cycle_mode();
fp3d_input::mode controls();
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
fp3d_hud::data hud_data();
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
void begin_action();
void melee_swing();
void movement_finished( const tripoint_abs_ms &before, const tripoint_abs_ms &after,
                        const tripoint_rel_ms &expected );
bool tick();
void compose();
void release();
} // namespace fp3d_compositor
#endif
#endif // CATA_SRC_FP3D_COMPOSITOR_H
