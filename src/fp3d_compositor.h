#pragma once
#ifndef CATA_SRC_FP3D_COMPOSITOR_H
#define CATA_SRC_FP3D_COMPOSITOR_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <string>
#include <array>
#include <vector>
#include "fp3d_targeting.h"
#include "fp3d_scene.h"

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
std::array<int, 4> viewport_pixels();
SDL_Texture_Ptr copy_view();
std::optional<fp3d_targeting::target> interaction_target();
void set_viewport( int x, int y, int width, int height, int display_width, int display_height );
void set_input_context( const std::string &category );
void publish( fp3d_snapshot::scene_snapshot scene );
void begin_action();
void movement_finished( const tripoint_abs_ms &before, const tripoint_abs_ms &after,
                        const tripoint_rel_ms &expected );
bool tick();
void compose();
void release();
} // namespace fp3d_compositor
#endif
#endif // CATA_SRC_FP3D_COMPOSITOR_H
