#pragma once
#ifndef CATA_SRC_SDLTILES_H
#define CATA_SRC_SDLTILES_H

#include "point.h" // IWYU pragma: keep

namespace catacurses
{
class window;
} // namespace catacurses

#if defined(TILES)

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "color_loader.h"
#include "coords_fwd.h"
#include "fp3d_menu_startup.h"
#include "sdl_wrappers.h"
#include "string_id.h"

#if defined(__APPLE__)
// For TARGET_OS_IPHONE macro to test if is on iOS
#include <TargetConditionals.h>
#endif

class cata_tiles;
class Character;

// Character creation uses the active tileset for its appearance preview.
void set_character_creation_preview( const Character *ch, const catacurses::window &window,
                                     const std::vector<std::pair<std::string, std::string>> &gear );
void clear_character_creation_preview();

struct weather_type;

using weather_type_id = string_id<weather_type>;

namespace catacurses
{
class window;
} // namespace catacurses

extern std::shared_ptr<cata_tiles> tilecontext;
extern std::shared_ptr<cata_tiles> closetilecontext;
extern std::shared_ptr<cata_tiles> fartilecontext;
extern std::unique_ptr<cata_tiles> overmap_tilecontext;
extern std::array<SDL_Color, color_loader<SDL_Color>::COLOR_NAMES_COUNT> windowsPalette;
extern int fontheight;
extern int fontwidth;

// This function may refresh the screen, so it should not be used where tiles
// may be displayed. Actually, this is supposed to be called from init.cpp,
// and only from there.
struct input_event;
input_event sdl_keysym_to_keycode_evt( const SDL_KeyboardEvent &keysym );
void load_tileset();
void rescale_tileset( int size );
bool save_screenshot( const std::string &file_path );
#if defined(ENABLE_FP3D)
SDL_Texture_Ptr copy_2d_display_buffer();
// Scoped to the title screen; other dialogs and game modes keep normal rendering.
void set_main_menu_demo_active( bool active );
bool is_main_menu_demo_active();
// Suppress the sandbox spectator in original 2D creature rendering.
bool is_native_menu_demo_worker();
// Only used in the isolated title-demo child process; never captures a player's save.
void set_native_menu_worker_output( const std::string &output );
// Pixel canvas the title screen wants streamed at 1:1; 0 means "native size".
void set_native_menu_canvas( int width, int height );
// Sub-square camera offset (in map squares, -0.5..0.5) for smooth panning.
void set_native_menu_pan( float fraction_x, float fraction_y );
// Removes frame_*/tmp_* images left behind by a previous demonstration run.
void menu_clear_worker_frames( const std::string &directory );
// Starts (or reuses) the isolated scene process. True means a new frame session
// was opened, so a fresh first frame has to be waited for.
bool start_native_menu_demo();
// Receiving is independent of drawing: the title screen can collect the first
// frame while its menu is still hidden. Never advances twice for one frame.
void update_native_menu_demo();
// What the title screen is allowed to show right now.
struct native_menu_demo_report {
    //! ready is the only state that permits showing menu + live backdrop together.
    fp3d_menu::demo_state state = fp3d_menu::demo_state::idle;
    //! Frames validated in the current session.
    unsigned long long frames = 0;
    bool session_active = false;
    bool worker_running = false;
};
// Named differently from the report type on purpose: a function and a class with
// the same name would hide each other.
native_menu_demo_report native_menu_demo_status();
// Vanilla image drawn behind the menu when no live scene can be shown at all.
void set_main_menu_static_backdrop( const std::string &image_path );
void stop_native_menu_demo();
void set_native_menu_worker_output( const std::string &directory );
// Load the actual active tileset atlas, not merely its startup precheck metadata.
bool prepare_main_menu_demo_tileset();
// True only while the title sprite atlas loads ahead of game data.
bool main_menu_tileset_preload_in_progress();
// True inside the isolated demonstration process: the spectator sees its whole
// loaded map (no fog, no occlusion) so the title backdrop has no black squares.
bool menu_demo_spectator_visibility();
// Night scenes keep real lighting (lamps, shadows) instead of fully lit tiles.
bool menu_demo_night_scene();
void set_native_menu_night( bool night );
// Per-frame callback used by the isolated demo worker to advance its camera.
void set_native_menu_frame_hook( std::function<void()> hook );
// Sub-tile pan that belongs to the frame just drawn (used when cropping it).
void set_native_menu_capture_pan( float x, float y );
#endif
void toggle_fullscreen_window();

struct window_dimensions {
    point scaled_font_size;
    point window_pos_cell;
    point window_size_cell;
    point window_pos_pixel;
    point window_size_pixel;
};
window_dimensions get_window_dimensions( const catacurses::window &win );
// Get dimensional info of an imaginary normal catacurses::window with the given
// position and size. Unlike real catacurses::window, size can be zero.
window_dimensions get_window_dimensions( const point &pos, const point &size );

const SDL_Renderer_Ptr &get_sdl_renderer();

#endif // TILES

// Text level, valid only for a point relative to the window, not a point in overall space.
bool window_contains_point_relative( const catacurses::window &win, const point &p );
// Native font/palette for projected overhead combat text.
void draw_fp3d_overlay_text( const std::string &text, const point &position, int color );
#endif // CATA_SRC_SDLTILES_H
