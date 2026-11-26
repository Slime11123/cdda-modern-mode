#include "fp3d_modern_ui.h"
#include "fp3d_aiming.h"
#include "fp3d_compositor.h"
#include "modern_2d_camera.h"
#include "fp3d_regression.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>


#include "avatar.h"
#include "creature_tracker.h"
#include "debug.h"
#include "game.h"
#include "fp3d_camera.h"
#include "fp3d_input.h"
#include "fp3d_interaction.h"
#include "fp3d_entry.h"
#include "imgui/imgui.h"
#include "fp3d_inventory.h"
#include "fp3d_materials.h"
#include "fp3d_gpu_world_qa.h"
#include "fp3d_legacy_shader_bridge.h"
#include "fp3d_shaderpacks.h"
#include "fp3d_snapshot.h"
#include "map.h"
#include "sdltiles.h"
#include "ui_manager.h"
#include "input.h"
#include "output.h"
#include "options.h"

namespace fp3d_compositor
{
namespace
{
int texture_width = 384;
int texture_height = 288;
SDL_Renderer *renderer = nullptr;
SDL_Window *window = nullptr;
SDL_Texture_Ptr menu_background;
bool enabled = false;
bool modern_2d = false;
bool in_world = false;
bool world_input = false;
bool overlay_input = false;
bool panel_input = false;
bool modern_aiming = false;
bool aiming_input = false;
std::optional<tripoint_abs_ms> aimed_creature;
// Tile the aim cursor actually rests on (creature, item or empty tile). Only
// used to place the modern HUD markers; selection and firing still come from
// the native targeting code.
std::optional<tripoint_abs_ms> aimed_tile;
bool available = false;
// Native GPU UI bring-up has no 3D renderer yet. Keep 3D unavailable
// independently of shared 2D presentation and input.
bool gpu_2d_available = false;
bool gpu_world_qa_attempted = false;
std::optional<fp3d_gpu_world_qa::prepared_world> gpu_world_qa_pending;
std::unique_ptr<fp3d_gpu::scene_target> gpu_world_qa_preview;
bool gpu_world_qa_composited = false;
std::unique_ptr<fp3d_gpu_world_qa::live_world> gpu_live_world;
struct gpu_view_state {
    fp3d_camera::pose camera;
    tripoint_abs_ms origin;
    unsigned geometry_generation;
    unsigned render_generation;
    unsigned material_revision;
    bool overhead;
    float aspect;
    float pointer_reach;
    unsigned pickup_revision = 0;
    unsigned contents_generation = 0;
};
std::optional<fp3d_compositor::pickup_highlight> pickup_source_highlight;
unsigned pickup_revision = 0;
std::optional<gpu_view_state> pending_gpu_view;
fp3d_gpu::frame_presentation<gpu_view_state> gpu_views;


const char *gpu_world_composite_path()
{
    const char *path = std::getenv( "CDDA_FP3D_GPU_WORLD_QA_COMPOSITE_PATH" );
    return path && *path ? path : nullptr;
}
bool draw_permitted = true;
bool selftest = false;
std::vector<map_overlay> native_map_overlays;
std::unordered_map<std::string, SDL_Texture_Ptr> overlay_icons;
unsigned overlay_icon_revision = 0;
bool selftest_initial_3d = false;
bool selftest_initial_modern = false;
unsigned toggles = 0;
unsigned frames = 0;
unsigned test_step = 0;
int original_width = 0;
int original_height = 0;
Uint32 next_toggle = 0;
float yaw = 0.0f;
float pitch = 0.0f;
// Overhead ("god view") camera. This is a camera type, not a rendering mode:
// 3D stays enabled and the same snapshot, materials and HUD pipeline are used.
// Its angles are kept separately from the first-person yaw/pitch so switching
// back restores the original facing and elevation exactly.
bool overhead_view = false;
fp3d_camera::orbit orbit_camera;
bool orbit_dragging = false;
right_camera_drag camera_drag;
modern_2d_camera overhead_pan;
std::optional<tripoint_abs_ms> pointer_room;
std::optional<tripoint_abs_ms> curtain_peek;
point curtain_direction;

bool first_person_curtain_peek()
{
    return curtain_peek && enabled && !overhead_view;
}
// A right-click the overhead view consumed, waiting for the world dispatcher to
// turn it into an ordinary secondary select.
bool secondary_select_pending = false;
std::optional<tripoint_abs_ms> secondary_select_tile;
std::optional<tripoint_abs_ms> dispatched_secondary_tile;
point secondary_select_mouse;
// Last pointer position inside the world viewport, in normalised device
// coordinates. The overhead camera has a free cursor, so picking needs it.
struct pointer_state {
    bool inside = false;
    float ndc_x = 0;
    float ndc_y = 0;
} pointer;
std::optional<fp3d_snapshot::scene_snapshot> latest_scene;
std::optional<fp3d_hud::data> latest_2d_hud;
unsigned published_generation = 0;
unsigned contents_generation = 0; // Native publications only; camera masks do not change atlas contents.
unsigned prepared_generation = 0;
fp3d_camera::follower camera;
fp3d_camera::rider_follower boarded_camera;
fp3d_camera::swing held_swing;
bool swing_pending = false;
struct recent_hit_record {
    tripoint_abs_ms position;
    int damage = 0;
    int hp = 0;
    int maximum = 0;
    unsigned at = 0;
};
std::vector<recent_hit_record> recent_hits;
float crouch_eye_offset = 0.0f;
unsigned crouch_eye_tick = 0;
std::optional<tripoint_abs_ms> accepted_walk;
float viewport_x = 0;
float viewport_y = 0;
float viewport_width = 0;
float viewport_height = 0;

void reset_pointer_room()
{
    pointer_room.reset();
    if( latest_scene && latest_scene->pointer_room ) {
        latest_scene->pointer_room.reset();
        latest_scene->render_generation = ++published_generation;
        pending_gpu_view.reset();
        gpu_world_qa_pending.reset();
    }
}

// One camera pose for the world FBO, reticle ray and target details.
fp3d_camera::offset visual_camera( unsigned now )
{
    if( !latest_scene ) {
        return {};
    }
    // Native stance controls posture. Smooth only the camera's presentation
    // offset; physics, aiming and interaction share this exact eye position.
    const float desired = latest_scene->player_crouching && !latest_scene->rider ? -.43f : 0.0f;
    const float elapsed = crouch_eye_tick ? std::min( 1.0f, ( now - crouch_eye_tick ) / 135.0f ) : 1.0f;
    crouch_eye_tick = now;
    crouch_eye_offset += ( desired - crouch_eye_offset ) * elapsed;
    fp3d_camera::offset result = latest_scene->rider && boarded_camera.active() ?
                                 boarded_camera.relative_to( latest_scene->origin, now ) :
                                 camera.relative_to( latest_scene->origin, now );
    result.height += crouch_eye_offset;
    return result;
}

// The camera pose the frame is actually drawn with. Fullscreen post effects,
// shadow passes, local lights, the reticle/pointer ray and the HUD all read this
// one pose, so an overhead frame can never be composited with first-person
// parameters.
fp3d_camera::pose camera_pose( unsigned now )
{
    if( overhead_view && curtain_peek && latest_scene ) {
        const auto rel = *curtain_peek - latest_scene->origin;
        auto peek = orbit_camera;
        const float length = std::hypot( curtain_direction.x, curtain_direction.y );
        if( length > 0 ) {
            peek.azimuth = std::atan2( curtain_direction.x, -curtain_direction.y ) *
                           ( 180.f / 3.14159265f );
            return fp3d_camera::orbit_pose( peek, rel.x() + 1.5f * curtain_direction.x / length,
                                            rel.y() + 1.5f * curtain_direction.y / length );
        }
        return fp3d_camera::orbit_pose( peek, rel.x(), rel.y() );
    }
    if( first_person_curtain_peek() ) {
        const float length = std::hypot( curtain_direction.x, curtain_direction.y );
        if( length > 0 ) {
            return { static_cast<float>( std::atan2( curtain_direction.x, -curtain_direction.y ) *
                                         180.0 / 3.141592653589793 ), 0.f,
                     { -.44f * curtain_direction.x / length, -.44f * curtain_direction.y / length, 1.65f } };
        }
    }
    if( latest_scene && panel_input && pickup_source_highlight ) {
        const auto rel = pickup_source_highlight->tile - latest_scene->origin;
        if( overhead_view ) {
            auto preview = orbit_camera;
            preview.distance = pickup_source_highlight->type == pickup_highlight::kind::vehicle ? 12.f : 8.f;
            return fp3d_camera::orbit_pose( preview, rel.x(), rel.y() );
        }
        const auto eye = visual_camera( now );
        const float x = rel.x() - eye.x;
        const float y = rel.y() - eye.y;
        const float distance = std::hypot( x, y );
        if( distance > .1f ) {
            constexpr float degrees = 180.f / 3.14159265f;
            return { std::atan2( x, -y ) * degrees,
                     std::atan2( eye.height - .75f, distance ) * degrees, eye };
        }
    }
    if( !overhead_view || !latest_scene ) {
        return { yaw, pitch, visual_camera( now ) };
    }
    // Orbit only around already-confirmed avatar positions: the same smoothing
    // the first-person follower uses. Input intent never moves the pivot.
    const fp3d_camera::offset follow = latest_scene->rider && boarded_camera.active() ?
                                       boarded_camera.relative_to( latest_scene->origin, now ) :
                                       camera.relative_to( latest_scene->origin, now );
    const point pan = overhead_pan.pixels();
    const float angle = orbit_camera.azimuth * 3.14159265f / 180.f;
    const float scale = orbit_camera.distance / 850.f;
    return fp3d_camera::orbit_pose( orbit_camera,
                                  follow.x + ( pan.x * std::cos( angle ) - pan.y * std::sin( angle ) ) * scale,
                                  follow.y + ( pan.x * std::sin( angle ) + pan.y * std::cos( angle ) ) * scale );
}

float overhead_pointer_reach()
{
    return std::clamp( orbit_camera.distance * 3.0f + 12.0f, 12.0f, 150.0f );
}

std::optional<gpu_view_state> targeting_view()
{
    if( !latest_scene ) {
        return std::nullopt;
    }
    if( !gpu_live_world ) {
        const float aspect = texture_height > 0 ? static_cast<float>( texture_width ) / texture_height : 1.f;
        return gpu_view_state{ camera_pose( SDL_GetTicks() ), latest_scene->origin,
                               latest_scene->geometry_generation, latest_scene->render_generation,
                               fp3d_materials::revision(), overhead_view, aspect, overhead_pointer_reach(),
                               0, contents_generation };
    }
    const auto &view = gpu_views.target_view();
    if( !view || view->origin != latest_scene->origin || view->overhead != overhead_view ||
        view->geometry_generation != latest_scene->geometry_generation ) {
        return std::nullopt;
    }
    return view;
}

// Map the window mouse position onto the world viewport in normalised device
// coordinates. SDL reports the cursor in the renderer's logical space whenever
// a logical size is set, so both sides are measured in that same space here.
void update_pointer( int x, int y )
{
    pointer.inside = false;
    if( !renderer ) {
        return;
    }
    int logical_width = 0;
    int logical_height = 0;
    int output_width = 0;
    int output_height = 0;
    GetLogicalRenderSize( renderer, &logical_width, &logical_height );
    SDL_GetCurrentRenderOutputSize( renderer, &output_width, &output_height );
    if( logical_width <= 0 || logical_height <= 0 ) {
        logical_width = output_width;
        logical_height = output_height;
    }
    if( logical_width <= 0 || logical_height <= 0 ) {
        return;
    }
    const float left = viewport_x * logical_width;
    const float top = viewport_y * logical_height;
    const float width = viewport_width * logical_width;
    const float height = viewport_height * logical_height;
    if( width <= 0 || height <= 0 || x < left || y < top || x >= left + width || y >= top + height ) {
        return;
    }
    pointer.ndc_x = 2.0f * ( x - left ) / width - 1.0f;
    pointer.ndc_y = 1.0f - 2.0f * ( y - top ) / height;
    pointer.inside = true;
}

// Snapshot-only pointer query for the overhead camera: one ray through the
// mouse position, using the exact pose the frame is drawn with. Cached because
// the HUD, the prompt and the outline all ask for it every frame.
std::optional<fp3d_targeting::target> pointing_target( const fp3d_camera::pose &pose, float aspect,
        float reach, bool fresh = false )
{
    if( !latest_scene || !pointer.inside || right_camera_dragging() ) {
        return std::nullopt;
    }
    static unsigned generation = 0;
    static fp3d_camera::pose previous_pose;
    static float previous_x = 0;
    static float previous_y = 0;
    static float previous_aspect = 0;
    static float previous_reach = 0;
    static std::optional<fp3d_targeting::target> cached;
    // Pixel-exact sprite alpha picking must not reuse a neighbour's hit after a
    // small mouse movement. HUD text has its own presentation hysteresis.
    const bool pointer_moved = pointer.ndc_x != previous_x || pointer.ndc_y != previous_y;
    if( fresh || generation != latest_scene->render_generation ||
        previous_pose.yaw != pose.yaw || previous_pose.pitch != pose.pitch ||
        previous_pose.eye.x != pose.eye.x || previous_pose.eye.y != pose.eye.y ||
        previous_pose.eye.height != pose.eye.height ||
        pointer_moved || previous_aspect != aspect || previous_reach != reach ) {
        // Reach covers the whole orbit distance plus the pickable depth behind
        // the pivot, so a zoomed-out camera can still select what it shows.
        cached = fp3d_targeting::ray( *latest_scene, pose.yaw, pose.pitch, pose.eye, reach,
                                      pointer.ndc_x, pointer.ndc_y, aspect, true );
        generation = latest_scene->render_generation;
        previous_pose = pose;
        previous_x = pointer.ndc_x;
        previous_y = pointer.ndc_y;
        previous_aspect = aspect;
        previous_reach = reach;
    }
    return cached;
}

void sync_mouse()
{
    // The overhead camera needs a free, visible cursor; only the first-person
    // camera owns relative mouse capture.
    const bool capture = !curtain_peek && available && enabled && draw_permitted && latest_scene && window &&
                         !overhead_view && SDL_GetKeyboardFocus() == window;
    const bool keyboard_world = overhead_view && enabled && draw_permitted && !aiming_input && window &&
                                SDL_GetKeyboardFocus() == window;
    if( !capture && !keyboard_world ) {
        fp3d_input::world_gestures().cancel();
    }
    if( capture != ( SDL_GetWindowRelativeMouseMode( window ) == true ) &&
        !SDL_SetWindowRelativeMouseMode( window, capture ) ) {
        enabled = false;
        SDL_SetWindowRelativeMouseMode( window, false );
        DebugLog( D_WARNING, D_MAIN ) << "fp3d: mouse capture failed; keeping original 2D";
    }
    if( overhead_view && enabled && !SDL_CursorVisible() ) {
        SDL_ShowCursor();
    }
}


bool env_enabled( const char *name )
{
    const char *value = std::getenv( name );
    return value && std::strcmp( value, "1" ) == 0;
}

fp3d_gpu_world_qa::terrain_index &live_terrain_index()
{
    static fp3d_gpu_world_qa::terrain_index index( true, true,
            env_enabled( "CDDA_FP3D_GPU_TERRAIN_PAGES" ) );
    return index;
}

void disable( const char *reason )
{
    enabled = false;
    available = false;
    selftest = false;
    sync_mouse();
    DebugLog( D_WARNING, D_MAIN ) << "fp3d: disabled: " << reason << "; keeping original 2D";
}

} // namespace

void clear_map_overlays()
{
    native_map_overlays.clear();
}

void add_map_overlay( map_overlay overlay )
{
    // The modern aiming HUD draws its own thin line of fire and target marker,
    // so the native cursor/line tiles stay out of the way. Targeting, occlusion
    // and hit resolution are untouched: only the sprite is skipped.
    if( aiming_input && ( overlay.id == "cursor" || overlay.id == "line_target" ) ) {
        return;
    }
    native_map_overlays.push_back( std::move( overlay ) );
}

const std::vector<map_overlay> &map_overlays()
{
    return native_map_overlays;
}

bool requested()
{
    // In the Windows Modern Mode build, start the real game executable directly.
    // An explicit CDDA_FP3D=0 remains a developer opt-out; other platforms keep
    // the original opt-in behavior.
    static const bool value = []() {
        const char *setting = std::getenv( "CDDA_FP3D" );
#if defined(_WIN32)
        return setting == nullptr || std::strcmp( setting, "1" ) == 0;
#else
        return setting && std::strcmp( setting, "1" ) == 0;
#endif
    }();
    return value;
}

void attach( SDL_Renderer *new_renderer, SDL_Window *new_window )
{
    if( !requested() ) {
        return;
    }
    renderer = new_renderer;
    window = new_window;
    const char *renderer_name = SDL_GetRendererName( renderer );
    const int version = SDL_GetVersion();
    SDL_GPUDevice *device = SDL_GetGPURendererDevice( renderer );
    if( !device ) {
        disable( "GPU 2D probe requires SDL GPU device" );
        return;
    }
    gpu_2d_available = true;
    const char *gpu_driver = SDL_GetGPUDeviceDriver( device );
    if( gpu_driver &&
        std::strcmp( gpu_driver, "direct3d12" ) == 0 &&
        ( SDL_GetGPUShaderFormats( device ) & SDL_GPU_SHADERFORMAT_DXBC ) ) {
        gpu_live_world = std::make_unique<fp3d_gpu_world_qa::live_world>( renderer,
                         env_enabled( "CDDA_FP3D_GPU_TERRAIN_COMPUTE" ),
                         env_enabled( "CDDA_FP3D_GPU_OCCLUSION" ) );
    }
    const auto view = get_option<std::string>( "FP3D_VIEW" );
    const auto keys = get_option<std::string>( "FP3D_CONTROLS" );
    available = gpu_live_world != nullptr;
    enabled = available && ( view == "3d" || ( view == "auto" && default_view_3d() ) );
    modern_2d = keys == "modern" || ( keys == "auto" && default_modern_controls( enabled ) );
    selftest = env_enabled( "CDDA_FP3D_SELFTEST" );
    selftest_initial_3d = enabled;
    selftest_initial_modern = modern_2d;
    toggles = frames = test_step = 0;
    next_toggle = SDL_GetTicks() + 100;
    if( env_enabled( "CDDA_FP3D_CAPTURE_FRAME" ) && env_enabled( "CDDA_FP3D_QA_OVERHEAD" ) ) {
        overhead_view = true;
        if( const char *azimuth = std::getenv( "CDDA_FP3D_QA_OVERHEAD_AZIMUTH" ) ) {
            orbit_camera.azimuth = static_cast<float>( std::atof( azimuth ) );
        }
        if( const char *distance = std::getenv( "CDDA_FP3D_QA_OVERHEAD_DISTANCE" ) ) {
            orbit_camera.distance = std::clamp( static_cast<float>( std::atof( distance ) ),
                                                fp3d_camera::orbit_min_distance,
                                                fp3d_camera::orbit_max_distance );
        }
    }
    DebugLog( D_INFO, D_MAIN ) << "fp3d: attached SDL "
                              << SDL_VERSIONNUM_MAJOR( version ) << "."
                              << SDL_VERSIONNUM_MINOR( version ) << "."
                              << SDL_VERSIONNUM_MICRO( version ) << ", renderer=" << renderer_name
                              << ", driver=" << SDL_GetGPUDeviceDriver( device )
                              << "; SDL GPU 3D available=" << available;
}

static bool process_overhead_camera_event( const SDL_Event &event )
{
    if( !overhead_view || !enabled || !draw_permitted || !window ) {
        return false;
    }
    const SDL_WindowID id = SDL_GetWindowID( window );
    const bool aim_panel = aiming_input && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
    if( event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID == id ) {
        update_pointer( event.motion.x, event.motion.y );
        if( orbit_dragging ) {
            if( event.motion.state & SDL_BUTTON_MMASK ) {
                orbit_camera.azimuth = std::remainder(
                                           orbit_camera.azimuth - event.motion.xrel * .35f, 360.f );
            } else {
                orbit_dragging = false;
            }
            return true;
        }
    } else if( event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.windowID == id ) {
        update_pointer( event.wheel.mouse_x, event.wheel.mouse_y );
        if( pointer.inside && !aim_panel ) {
            orbit_camera.distance = std::clamp( orbit_camera.distance - event.wheel.y * 1.5f,
                                                fp3d_camera::orbit_min_distance,
                                                fp3d_camera::orbit_max_distance );
            return true;
        }
    } else if( ( event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ) &&
               event.button.windowID == id && event.button.button == SDL_BUTTON_MIDDLE ) {
        update_pointer( event.button.x, event.button.y );
        if( event.type == SDL_EVENT_MOUSE_BUTTON_UP ) {
            const bool was_dragging = orbit_dragging;
            orbit_dragging = false;
            return was_dragging;
        }
        if( pointer.inside && !aim_panel ) {
            orbit_dragging = true;
            return true;
        }
    }
    return false;
}

bool process_event( const SDL_Event &event )
{
    if( !requested() ) {
        return false;
    }
    if( event.type == SDL_EVENT_WINDOW_FOCUS_LOST ) {
        reset_pointer_room();
        camera_drag.cancel();
        held_swing.reset();
        swing_pending = false;
        orbit_dragging = false;
        secondary_select_pending = false;
        secondary_select_tile.reset();
        dispatched_secondary_tile.reset();
    }
    if( !draw_permitted || curtain_peek ) {
        return false;
    }
    // Camera gestures are consumed before the targeting input handler. All
    // other aiming input stays with the targeting panel.
    if( process_overhead_camera_event( event ) ) {
        return true;
    }
    if( aiming_input ) {
        return false;
    }
    sync_mouse();
    // F6 inside the 3D world switches between the eye camera and the overhead
    // camera. It is consumed here so the native settings window never opens
    // from the world; only the first press counts, so holding the key cannot
    // toggle repeatedly, and the toggle never advances world time.
    if( enabled && in_world && draw_permitted && window && SDL_GetKeyboardFocus() == window &&
        ( event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ) ) {
        if( event.key.key == SDLK_F6 ) {
            if( event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat ) {
                toggle_camera();
            }
            return true;
        }
    }
    // Overhead view: a free cursor replaces mouse look. The wheel and the middle
    // button belong to the camera; left/right clicks are handed to the native
    // dispatcher, which resolves them against the pointer pick instead of the
    // (undrawn) 2D terrain window. Wheel and drag never reach inventory, attack
    // or auto-travel.
    const bool overhead_world = overhead_view && enabled && world_input && draw_permitted && window &&
                                SDL_GetKeyboardFocus() == window;
    if( overhead_world || modern_2d_input() ) {
        if( event.type == SDL_EVENT_MOUSE_MOTION ) {
            camera_drag.move( point( event.motion.x, event.motion.y ) );
            if( overhead_world && event.motion.windowID == SDL_GetWindowID( window ) &&
                ( event.motion.state & SDL_BUTTON_RMASK ) ) {
                // Native MOUSE_MOVE opens liveview and redraws/captures the map.
                // Right-drag is a camera gesture, including its threshold phase.
                return true;
            }
        }
        if( ( event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ) &&
            event.button.windowID == SDL_GetWindowID( window ) &&
            event.button.button == SDL_BUTTON_RIGHT ) {
            const point cursor( event.button.x, event.button.y );
            update_pointer( cursor.x, cursor.y );
            if( event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ) {
                if( pointer.inside ) {
                    camera_drag.begin( cursor, SDL_GetTicks() );
                }
                return true;
            }
            camera_drag.move( cursor );
            const bool click = camera_drag.release();
            if( overhead_world ) {
                reset_pointer_room();
                if( click && pointer.inside ) {
                    secondary_select_tile = pointer_tile();
                    secondary_select_pending = secondary_select_tile.has_value();
                    secondary_select_mouse = cursor;
                }
                return true;
            }
            return !click;
        }
    }
    // A lone modifier press never reaches the native dispatcher (the SDL bridge
    // drops it so it cannot act as both a modifier and a command). A 3D profile
    // may still bind one to a command - run/walk on Shift, the status panel on
    // Alt - so hand those to the gesture queue ourselves. Keys the profile does
    // not bind stay untouched, and the key-up edge only closes the hold state.
    if( modern_2d && captures_game_input() &&
        ( event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ) &&
        ( event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT ||
          event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL ||
          event.key.key == SDLK_LALT || event.key.key == SDLK_RALT ) ) {
        const input_event modifier_key( event.key.key, input_event_t::keyboard_code );
        const auto command = fp3d_input::context().input_to_action( modifier_key );
        if( command != "ERROR" && ( !overhead_world || command == "FP3D_RUN" ) ) {
            if( command == "FP3D_RUN" && event.type == SDL_EVENT_KEY_DOWN ) {
                const bool shift = event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT;
                if( event.key.repeat || ( shift && ( event.key.mod & ( SDL_KMOD_CTRL | SDL_KMOD_ALT |
                                                     SDL_KMOD_GUI ) ) ) ) {
                    return true;
                }
            }
            if( fp3d_input::world_gestures().event( modifier_key,
                    event.type == SDL_EVENT_KEY_DOWN, SDL_GetTicks() ) ) {
                return true;
            }
        }
    }
    if( overhead_world ) {
        if( event.type == SDL_EVENT_TEXT_INPUT && event.text.text &&
            event.text.text[0] && !event.text.text[1] &&
            fp3d_input::context().input_to_action( input_event(
                    static_cast<unsigned char>( event.text.text[0] ), input_event_t::keyboard_code ) ) ==
            "FP3D_INTERACT" ) {
            return true; // Do not dispatch the consumed physical key again as native text.
        }
        // Use the existing short/long interact gesture with the free cursor,
        // before ordinary keyboard actions fall through to native examine.
        if( event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ) {
            const auto key = sdl_keysym_to_keycode_evt( event.key );
            if( fp3d_input::context().input_to_action( key ) == "FP3D_INTERACT" ) {
                if( event.type == SDL_EVENT_KEY_DOWN && event.key.repeat ) { return true; }
                return fp3d_input::world_gestures().event( key,
                        event.type == SDL_EVENT_KEY_DOWN, SDL_GetTicks() );
            }
        }
        if( event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ) {
            update_pointer( event.button.x, event.button.y );
        }
        return false;
    }
    // Gameplay uses key codes. Discard delayed composition/text messages so
    // an IME prefix cannot become a second game command after capture starts.
    if( captures_game_input() || modern_2d_input() ) {
        if( event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING ) {
            return true;
        }
#if defined(SDL_HINT_IME_SUPPORT_EXTENDED_TEXT)
        if( event.type == SDL_EVENT_TEXT_EDITING_EXT ) {
            SDL_free( event.editExt.text );
            return true;
        }
#endif
    }
    if( event.type == SDL_EVENT_MOUSE_MOTION && captures_game_input() ) {
        yaw += event.motion.xrel * 0.4f;
        yaw = std::remainder( yaw, 360.0f );
        pitch = std::clamp( pitch + event.motion.yrel * 0.4f, -80.0f, 80.0f );
        return true;
    }
    if( captures_game_input() && modern_2d && ( event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ) ) {
        auto key = sdl_keysym_to_keycode_evt( event.key );
        if( event.key.key == SDLK_LSHIFT || event.key.key == SDLK_LCTRL ||
            event.key.key == SDLK_LALT || event.key.key == SDLK_RSHIFT ||
            event.key.key == SDLK_RCTRL || event.key.key == SDLK_RALT ) {
            key = input_event( event.key.key, input_event_t::keyboard_code );
        }
        if( event.type == SDL_EVENT_KEY_DOWN && event.key.repeat ) {
            const auto command = fp3d_input::context().input_to_action( key );
            if( command != "ERROR" && command != "FP3D_FORWARD" && command != "FP3D_BACKWARD" &&
                command != "FP3D_LEFT" && command != "FP3D_RIGHT" && command != "FP3D_WAIT" ) {
                return true;
            }
        }
        if( fp3d_input::world_gestures().event( key, event.type == SDL_EVENT_KEY_DOWN, SDL_GetTicks() ) ) {
            return true;
        }

    }
    if( captures_game_input() && ( event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                   event.type == SDL_EVENT_MOUSE_BUTTON_UP ) ) {
        if( modern_2d ) {
            fp3d_input::world_gestures().mouse_button( event.button.button,
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_GetTicks() );
        }
        return true; // Never let a world click become native 2D auto-travel.
    }
    return false;
}

bool menu_text_input()
{
    return panel_input && survival_menu_available() && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
}

bool captures_game_input()
{
    return requested() && available && enabled && draw_permitted && !aiming_input && !curtain_peek &&
           latest_scene && window &&
           SDL_GetKeyboardFocus() == window;
}

bool view_3d()
{
    return requested() && enabled;
}

bool overhead()
{
    return requested() && enabled && overhead_view;
}

void toggle_camera()
{
    if( !requested() || !enabled ) {
        return;
    }
    overhead_view = !overhead_view;
    camera_drag.cancel();
    overhead_pan = {};
    reset_pointer_room();
    // One physical press must leave no half-finished input behind in either
    // direction, and the old pointer pick may never be reused as a target.
    orbit_dragging = false;
    pointer.inside = false;
    secondary_select_pending = false;
    secondary_select_tile.reset();
    dispatched_secondary_tile.reset();
    fp3d_input::world_gestures().cancel();
    fp3d_interaction::cancel_direction_selection();
    held_swing.reset();
    swing_pending = false;
    accepted_walk.reset();
    sync_mouse();
    // The overhead camera needs the cut snapshot (and vice versa), so ask for a
    // fresh native frame instead of flashing the previous one.
    ui_manager::invalidate_all_ui_adaptors();
    if( g ) {
        g->mark_main_ui_adaptor_resize();
    }
}

bool default_view_3d()
{
    const char *start_active = std::getenv( "CDDA_FP3D_START_ACTIVE" );
    return available && ( start_active ? env_enabled( "CDDA_FP3D_START_ACTIVE" ) : requested() );
}

bool default_modern_controls( bool three_d )
{
    return three_d || !available;
}

bool preferred_overhead()
{
    return overhead_view;
}

std::string saved_view_mode()
{
    return enabled ? ( overhead_view ? "overhead" : "first_person" ) : "2d";
}

void restore_view_mode( const std::string &mode )
{
    if( !requested() ) {
        return;
    }
    // Missing/unknown values retain the configured rendering preference, but
    // must never inherit another character's overhead camera in this session.
    const bool known = mode == "2d" || mode == "first_person" || mode == "overhead";
    overhead_view = mode == "overhead";
    apply_modes( known ? mode != "2d" : enabled, modern_2d );
}

float movement_heading()
{
    // WASD stays relative to the screen after orbiting with the middle button.
    return heading();
}

float overhead_azimuth()
{
    return orbit_camera.azimuth;
}

float overhead_distance()
{
    return orbit_camera.distance;
}

static bool visible_pointer_monster( const fp3d_targeting::target &target )
{
    if( !g || target.type != fp3d_targeting::kind::creature ) { return false; }
    const auto p = get_map().get_bub( target.position );
    if( !get_map().inbounds( p ) ) { return false; }
    const Creature *critter = get_creature_tracker().creature_at( p, true );
    return critter && critter->is_monster() && !critter->is_dead_state() &&
           get_avatar().sees( get_map(), *critter );
}

bool world_pointer_input()
{
    return world_input && window && SDL_GetKeyboardFocus() == window;
}

std::optional<tripoint_abs_ms> pointer_monster( bool fresh )
{
    if( !overhead() || !world_pointer_input() || !latest_scene || !pointer.inside || !draw_permitted ||
        latest_scene->origin != get_avatar().pos_abs() ) { return std::nullopt; }
    const auto frame = targeting_view();
    if( !frame ) { return std::nullopt; }
    const auto target = pointing_target( frame->camera, frame->aspect, frame->pointer_reach, fresh );
    return target && visible_pointer_monster( *target ) ?
           std::optional<tripoint_abs_ms>( target->position ) : std::nullopt;
}

std::optional<tripoint_abs_ms> pointer_tile()
{
    if( !overhead_view || !latest_scene || !pointer.inside || !draw_permitted ) {
        return std::nullopt;
    }
    // Same camera, same snapshot, same storey as the frame the player is
    // looking at. Only natively visible, non-facade cells can be produced.
    const auto frame = targeting_view();
    if( !frame ) {
        return std::nullopt;
    }
    const auto &pose = frame->camera;
    const auto target = pointing_target( pose, frame->aspect, frame->pointer_reach );
    if( target && target->position.z() == latest_scene->origin.z() && overhead_tile_visible( target->position ) ) {
        return target->position;
    }
    // A facade or another storey's roof may be the first mesh hit. The tile
    // menu still needs a map square on the player's floor, rather than silently
    // dropping the right-click. Unproject onto that floor and keep the same
    // snapshot visibility gate; this never invents an unseen destination.
    const auto floor = fp3d_camera::pointer_floor( pose, frame->aspect, pointer.ndc_x, pointer.ndc_y );
    if( !floor ) { return std::nullopt; }
    const tripoint_abs_ms position = latest_scene->origin + tripoint_rel_ms(
                                        static_cast<int>( std::floor( ( *floor )[0] + .5f ) ),
                                        static_cast<int>( std::floor( ( *floor )[1] + .5f ) ), 0 );
    return overhead_tile_visible( position ) ? std::optional<tripoint_abs_ms>( position ) : std::nullopt;
}

bool overhead_tile_visible( const tripoint_abs_ms &position )
{
    if( !overhead() || !latest_scene || position.z() != latest_scene->origin.z() ||
        !g || latest_scene->origin != get_avatar().pos_abs() ) {
        return false;
    }
    // This also works while the menu owns input and draw_permitted is false.
    return std::any_of( latest_scene->cells.begin(), latest_scene->cells.end(),
    [&]( const auto &tile ) {
        return tile.position == position &&
               tile.known == fp3d_snapshot::knowledge::visible && !tile.exterior_only;
    } );
}

bool take_secondary_select( point *mouse )
{
    if( !secondary_select_pending ) {
        return false;
    }
    secondary_select_pending = false;
    if( !overhead_view || !draw_permitted || !secondary_select_tile ) {
        secondary_select_tile.reset();
        return false;
    }
    // Later motion, releases and camera frames must not change this click.
    dispatched_secondary_tile = secondary_select_tile;
    secondary_select_tile.reset();
    if( mouse ) { *mouse = secondary_select_mouse; }
    return true;
}

std::optional<tripoint_abs_ms> take_secondary_select_target()
{
    const auto target = dispatched_secondary_tile;
    dispatched_secondary_tile.reset();
    return target;
}

void apply_modes( bool three_d, bool modern )
{
    camera_drag.cancel();
    overhead_pan = {};
    reset_pointer_room();
    enabled = three_d && available;
    if( gpu_live_world && !enabled ) {
        gpu_live_world->hide();
        pending_gpu_view.reset();
        gpu_views.clear();
        gpu_world_qa_pending.reset();
    }
    modern_2d = modern;
    fp3d_input::world_gestures().cancel();
    fp3d_interaction::cancel_direction_selection();
    ui_manager::invalidate_all_ui_adaptors();
    if( g ) {
        g->mark_main_ui_adaptor_resize();
    }
    camera.reset();
    boarded_camera.reset();
    held_swing.reset();
    swing_pending = false;
    orbit_dragging = false;
    pointer.inside = false;
    secondary_select_pending = false;
    secondary_select_tile.reset();
    dispatched_secondary_tile.reset();
    if( latest_scene ) {
        const unsigned now = SDL_GetTicks();
        camera.confirm( latest_scene->origin, now );
        if( latest_scene->rider ) {
            boarded_camera.confirm( *latest_scene->rider, now );
        }
    }
    sync_mouse();
}

void cycle_mode()
{
    const auto next = fp3d_input::next_mode( controls() );
    apply_modes( next == fp3d_input::mode::first_person_3d,
                 next != fp3d_input::mode::original_2d );
    ++toggles;
}

fp3d_input::mode controls()
{
    return !requested() || !modern_2d ? fp3d_input::mode::original_2d :
           enabled ? fp3d_input::mode::first_person_3d : fp3d_input::mode::modern_2d;
}

bool modern_2d_input()
{
    return controls() == fp3d_input::mode::modern_2d && world_input && window &&
           SDL_GetKeyboardFocus() == window;
}

bool right_camera_dragging()
{
    return camera_drag.dragging( SDL_GetTicks() );
}

void set_curtain_peek( std::optional<tripoint_abs_ms> position, const point &direction )
{
    curtain_peek = position;
    curtain_direction = direction;
    if( !position ) {
        latest_scene.reset();
        pending_gpu_view.reset();
        gpu_world_qa_pending.reset();
        gpu_views.clear();
        // Keep displaying the last 3D texture until the restored camera submits a frame.
    }
    sync_mouse();
    if( g ) {
        g->invalidate_main_ui_adaptor();
    }
}

bool survival_menu_available()
{
    // Shared menus follow the game session, not the camera or movement bindings.
    return requested() && ( available || gpu_2d_available ) && ( in_world || turn_based() );
}

bool modern_2d_world()
{
    return in_world && controls() == fp3d_input::mode::modern_2d && ( available || gpu_2d_available );
}

bool modern_2d_overlay_open()
{
    return controls() == fp3d_input::mode::modern_2d && overlay_input;
}

bool uses_hud()
{
    if( !requested() ) {
        return false;
    }
    const auto style = get_option<std::string>( "FP3D_UI_STYLE" );
    return style == "hud" || ( style == "auto" &&
                              view_3d() );
}

void publish_hud( fp3d_hud::data hud )
{
    latest_2d_hud = std::move( hud );
}

bool covers_map( const tripoint_abs_ms &center )
{
    return !curtain_peek && active() && latest_scene && latest_scene->origin == center &&
           ( !gpu_live_world || gpu_live_world->texture() );
}

bool active()
{
    return available && enabled && ( draw_permitted || curtain_peek.has_value() );
}

bool turn_based()
{
    return available && enabled && latest_scene.has_value();
}
std::array<int, 4> viewport_pixels()
{
    int width = 0, height = 0;
    if( renderer ) {
        SDL_GetCurrentRenderOutputSize( renderer, &width, &height );
    }
    return { static_cast<int>( viewport_x * width ), static_cast<int>( viewport_y * height ),
             static_cast<int>( viewport_width * width ), static_cast<int>( viewport_height * height ) };
}
namespace
{
std::optional<fp3d_scene::blueprint> construction_preview;
}
void set_blueprint( std::optional<fp3d_scene::blueprint> preview )
{
    construction_preview = std::move( preview );
    prepared_generation = 0;
}

void set_pickup_highlight( std::optional<pickup_highlight> source )
{
    if( !( pickup_source_highlight == source ) ) {
        pickup_source_highlight = std::move( source );
        ++pickup_revision;
        if( g && turn_based() ) {
            g->invalidate_main_ui_adaptor();
        }
    }
}

std::vector<std::string> hud_lines()
{
    if( !uses_hud() || !draw_permitted || aiming_input ||
        ( enabled ? !latest_scene.has_value() : !latest_2d_hud.has_value() ) ) {
        return {};
    }
    if( enabled && construction_preview ) {
        return { construction_preview->name, construction_preview->valid ?
                 "可建造：E / 左键确认；Esc 取消" : "此格不可建造；转动视角选择相邻格；Esc 取消" };
    }
    const auto &hud = enabled ? latest_scene->hud : *latest_2d_hud;
    std::vector<std::string> lines{ hud.status };
    if( enabled && latest_scene->rider ) {
        lines.push_back( latest_scene->rider->driver ?
                         "车内：驾驶视角（CDDA 原版车辆控制）" :
                         "车内：乘坐视角（操作遵循 CDDA 原版）" );
    }
    if( fp3d_hud::navigation_enabled() && hud.navigation_heading ) {
        const float delta = std::remainder( *hud.navigation_heading - heading(), 360.0f );
        lines.push_back( "目的地" + std::string( std::abs( delta ) < 15 ? "前方" : delta > 0 ?
                         "右转" : "左转" ) +
                         " / 大地图距离 " + std::to_string( hud.navigation_distance ) +
                         " / 楼层差 " + std::to_string( hud.navigation_z ) );
    }
    const auto &slots = hud.slots;
    lines.insert( lines.end(), slots.begin(), slots.end() );
    if( fp3d_input::world_gestures().expanded_hud() ) {
        const auto &expanded = hud.expanded;
        lines.insert( lines.end(), expanded.begin(), expanded.end() );
    }
    if( !overhead_view ) {
        if( const std::string selection = fp3d_interaction::direction_selection_hint(); !selection.empty() ) {
            lines.push_back( selection );
        }
    }
    if( !enabled || !modern_2d ) {
        return lines;
    }
    const auto target = interaction_target();
    const bool reachable = target && target->position.z() == latest_scene->origin.z() &&
                           square_dist( target->position, latest_scene->origin ) <= 1;
    if( overhead_view ) {
        lines.push_back( target && !reachable ?
                         "目标超出触及范围 / 请靠近并保持同层" :
                         "F6 第一人称 / 滚轮缩放 / 中键拖动旋转 / 右键地块菜单 / F5 模式设置" );
        return lines;
    }
    lines.push_back( target && !reachable ? "目标超出触及范围 / 请靠近并保持同层" :
                     "Tab 面板 / Q 行动轮盘 / Alt 状态 / F6 上帝视角 / F5 模式设置" );
    return lines;
}

const fp3d_hud::data &hud_data()
{
    static const fp3d_hud::data empty;
    return enabled ? ( latest_scene ? latest_scene->hud : empty ) :
           ( latest_2d_hud ? *latest_2d_hud : empty );
}

static bool interaction_available( const std::optional<fp3d_targeting::target> &target )
{
    if( !turn_based() || !draw_permitted || construction_preview ) { return false; }
    if( !target || target->position.z() != latest_scene->origin.z() ||
        square_dist( target->position, latest_scene->origin ) > 1 ) { return false; }
    // Native vehicle components use a separate mounted-part snapshot, not
    // terrain/context_entries' interactable cell list. This affects only
    // the E prompt: the real action still calls game-thread valid() and
    // rechecks native vehicle identity and interaction reach.
    if( target->type == fp3d_targeting::kind::vehicle ) {
        return std::any_of( latest_scene->vehicles.begin(), latest_scene->vehicles.end(),
        [&]( const fp3d_vehicle::vehicle_view &car ) {
            return target->vehicle_origin && car.origin == *target->vehicle_origin &&
                   std::any_of( car.parts.begin(), car.parts.end(), [&]( const auto &part ) {
                return part.index == target->vehicle_part &&
                       part.native_position == target->position && part.id == target->id;
            } );
        } );
    }
    return std::find( latest_scene->interactable.begin(), latest_scene->interactable.end(),
                      target->position ) != latest_scene->interactable.end();
}
const std::vector<fp3d_hud::data::message_entry> &target_details()
{
    static const std::vector<fp3d_hud::data::message_entry> empty;
    if( !g || !turn_based() || !draw_permitted || construction_preview ) {
        return empty;
    }
    static unsigned generation = 0;
    static std::optional<fp3d_targeting::target> previous;
    static std::vector<fp3d_hud::data::message_entry> lines;
    static unsigned retain_until = 0;
    const auto forget = [&]() {
        previous.reset();
        lines.clear();
        retain_until = 0;
        return &empty;
    };
    const auto target = interaction_target();
    if( !target ) {
        // The pointer query can miss for a frame or two while the GPU view and
        // the captured snapshot hand over. Hold the last answer briefly instead
        // of blinking, then drop it so no stale text stays on screen.
        if( previous && !lines.empty() && SDL_GetTicks() < retain_until ) {
            return lines;
        }
        return *forget();
    }
    if( target->origin != get_avatar().pos_abs() ) {
        return *forget();
    }
    if( generation != latest_scene->render_generation || !previous ||
        target->position != previous->position || target->type != previous->type ||
        target->id != previous->id || target->vehicle_part != previous->vehicle_part ||
        target->vehicle_origin != previous->vehicle_origin ) {
        lines = fp3d_hud::inspect( get_avatar(), get_map(), *target );
        // QA only (CDDA_FP3D_HINT_DEBUG=1): show which target won the pick and
        // why a creature line may be missing. Never enabled in normal play.
        if( std::getenv( "CDDA_FP3D_HINT_DEBUG" ) ) {
            const map &here = get_map();
            const Creature *who = here.inbounds( here.get_bub( target->position ) ) ?
                                  get_creature_tracker().creature_at( here.get_bub( target->position ) ) : nullptr;
            const std::string tag = string_format(
                                        "hint t=%d id=%s lines=%d who=%s sees=%d",
                                        static_cast<int>( target->type ), target->id, static_cast<int>( lines.size() ),
                                        who ? who->disp_name() : "-",
                                        who ? static_cast<int>( get_avatar().sees( here, *who ) ) : -1 );
            lines.insert( lines.begin(), { tag, c_light_cyan } );
        }
        previous = target;
        generation = latest_scene->render_generation;
    }
    retain_until = SDL_GetTicks() + 200;
    return lines;
}

bool interaction_available()
{
    return turn_based() && draw_permitted && !construction_preview &&
           interaction_available( interaction_target() );
}

std::string interaction_prompt()
{
    if( turn_based() && draw_permitted ) {
        const std::string selection = fp3d_interaction::direction_selection_hint();
        if( !selection.empty() ) { return selection; }
    }
    if( !interaction_available() ) { return {}; }
    const auto &bindings = inp_mngr.get_input_for_action( "FP3D_INTERACT", "FP3D" );
    // 3D gestures resolve raw SDL key codes even when the original menus use
    // character input. get_desc would incorrectly hide those physical bindings.
    return "按[" + ( bindings.empty() ? std::string( "未绑定" ) :
                    remove_color_tags( bindings.front().long_description() ) ) +
           ( overhead_view ? "]与鼠标指向目标互动" : "]互动" );
}
bool draw_menu_background( int x, int y, int width, int height )
{
    SDL_Texture *native_view = gpu_live_world && enabled && in_world ? gpu_live_world->texture() :
                               nullptr;
    if( curtain_peek ) {
        // Keep the previous 3D frame until the peek snapshot completes.
        if( !native_view ) {
            return false;
        }
    } else if( draw_permitted || ( !native_view && !turn_based() ) ) {
        return false;
    }
    if( !native_view && !menu_background ) { menu_background = copy_view(); }
    const bool clipped = SDL_RenderClipEnabled( renderer );
    SDL_Rect previous;
    SDL_GetRenderClipRect( renderer, &previous );
    const SDL_Rect destination{ x, y, width, height };
    SDL_SetRenderClipRect( renderer, &destination );
    if( native_view || menu_background ) {
        int output_width = 0, output_height = 0, logical_width = 0, logical_height = 0;
        SDL_GetCurrentRenderOutputSize( renderer, &output_width, &output_height );
        GetLogicalRenderSize( renderer, &logical_width, &logical_height );
        if( logical_width == 0 || logical_height == 0 ) {
            logical_width = output_width;
            logical_height = output_height;
        }
        // Preserve the 3D viewport's aspect and position even when the native
        // terrain window covers only part of it or SDL uses logical scaling.
        const SDL_Rect view{ static_cast<int>( viewport_x * logical_width ),
                             static_cast<int>( viewport_y * logical_height ),
                             static_cast<int>( viewport_width * logical_width ),
                             static_cast<int>( viewport_height * logical_height ) };
        RenderTextureRotatedPixels( renderer, native_view ? native_view : menu_background.get(),
                                    nullptr, &view, 0, nullptr,
                                    native_view ? SDL_FLIP_NONE : SDL_FLIP_VERTICAL );
    } else {
        Uint8 r, g, b, a;
        SDL_GetRenderDrawColor( renderer, &r, &g, &b, &a );
        SDL_SetRenderDrawColor( renderer, 0, 0, 0, 255 );
        RenderFillRectPixels( renderer, &destination );
        SDL_SetRenderDrawColor( renderer, r, g, b, a );
    }
    SDL_SetRenderClipRect( renderer, clipped ? &previous : nullptr );
    // Suppress the terrain draw even if the snapshot could not be copied.
    return true;
}

SDL_Texture_Ptr copy_view()
{
    if( !enabled && modern_2d ) { return copy_2d_display_buffer(); }
    if( gpu_live_world ) {
        auto *source = gpu_live_world->texture();
        float width = 0;
        float height = 0;
        if( !enabled || !source || !SDL_GetTextureSize( source, &width, &height ) ) {
            return {};
        }
        SDL_Texture_Ptr image( SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA32,
                               SDL_TEXTUREACCESS_TARGET, static_cast<int>( width ), static_cast<int>( height ) ) );
        if( !image ) { return {}; }
        auto *previous_target = SDL_GetRenderTarget( renderer );
        if( !SDL_SetRenderTarget( renderer, image.get() ) ) { return {}; }
        // Existing copy_view consumers flip 3D texture snapshots when displaying them.
        const bool copied = SDL_RenderTextureRotated( renderer, source, nullptr, nullptr, 0,
                            nullptr, SDL_FLIP_VERTICAL );
        SDL_SetRenderTarget( renderer, previous_target );
        if( !copied || !SDL_SetTextureBlendMode( image.get(), SDL_BLENDMODE_NONE ) ) {
            return {};
        }
        return image;
    }
    return {};

}

static std::optional<fp3d_targeting::target> interaction_target( const fp3d_camera::pose &pose,
        float aspect, float reach )
{
    if( overhead_view ) {
        // In the overhead view the pointer is the only target source: a leftover
        // first-person reticle target must never win over the mouse.
        return pointing_target( pose, aspect, reach );
    }
    // The reticle, prompt and HUD share the same pure snapshot query. Avoid
    // scanning all scene cells again while the camera and snapshot are unchanged.
    static unsigned generation = 0;
    static float previous_yaw = 0;
    static float previous_pitch = 0;
    static fp3d_camera::offset previous_offset;
    static std::optional<fp3d_targeting::target> cached;
    if( generation != latest_scene->render_generation || previous_yaw != pose.yaw ||
        previous_pitch != pose.pitch || previous_offset.x != pose.eye.x ||
        previous_offset.y != pose.eye.y || previous_offset.height != pose.eye.height ) {
        cached = fp3d_targeting::ray( *latest_scene, pose.yaw, pose.pitch, pose.eye );
        generation = latest_scene->render_generation;
        previous_yaw = pose.yaw;
        previous_pitch = pose.pitch;
        previous_offset = pose.eye;
    }
    return cached;
}

std::optional<fp3d_targeting::target> interaction_target()
{
    const auto frame = targeting_view();
    return frame ? interaction_target( frame->camera, frame->aspect, frame->pointer_reach ) : std::nullopt;
}

std::optional<tripoint_abs_ms> firing_target( int range )
{
    if( !enabled || !latest_scene || range <= 0 ) {
        return std::nullopt;
    }
    const auto frame = targeting_view();
    if( !frame ) {
        return std::nullopt;
    }
    const float distance_limit = std::clamp( static_cast<float>( range ), 1.0f, 64.0f );
    const auto &pose = frame->camera;
    // The weapon and the HUD share this exact camera ray. Geometry in front
    // of the crosshair (including sprites and doors) wins over empty space.
    if( const auto hit = fp3d_targeting::ray( *latest_scene, pose.yaw, pose.pitch, pose.eye,
            distance_limit ) ) {
        if( hit->position != latest_scene->origin ) {
            return hit->position;
        }
    }

    // Nothing visible at the reticle: shoot along the camera direction, not
    // at the nearest NPC or the native 2D cursor. Bound the end inside the map.
    constexpr float radians = 3.14159265358979323846f / 180.0f;
    const float sine = std::sin( pose.yaw * radians );
    const float cosine = std::cos( pose.yaw * radians );
    const float cp = std::cos( pose.pitch * radians );
    const float dz = -std::sin( pose.pitch * radians );
    map &here = get_map();
    for( float distance = distance_limit; distance >= 1.0f; distance -= 0.5f ) {
        const tripoint_rel_ms delta(
            static_cast<int>( std::lround( pose.eye.x + distance * sine * cp ) ),
            static_cast<int>( std::lround( pose.eye.y - distance * cosine * cp ) ),
            static_cast<int>( std::lround( ( pose.eye.height + distance * dz ) /
                                          fp3d_camera::floor_height ) ) );
        const tripoint_abs_ms position = latest_scene->origin + delta;
        if( position != latest_scene->origin && here.inbounds( here.get_bub( position ) ) ) {
            return position;
        }
    }
    return std::nullopt;
}

float heading()
{
    if( !enabled ) {
        return 0.0f;
    }
    // The overhead view has no first-person facing; the compass follows the
    // camera azimuth so turning the camera is still legible.
    return overhead_view ? orbit_camera.azimuth : yaw;
}

void set_viewport( int x, int y, int width, int height, int display_width, int display_height )
{
    if( display_width > 0 && display_height > 0 ) {
        const int effective_width = enabled && uses_hud() ? display_width - x : width;
        viewport_x = static_cast<float>( x ) / display_width;
        viewport_y = static_cast<float>( y ) / display_height;
        viewport_width = static_cast<float>( effective_width ) / display_width;
        viewport_height = static_cast<float>( height ) / display_height;
    }
}

void set_input_context( const std::string &category )
{
    panel_input = category == "FP3D_PANEL";
    world_input = category == "DEFAULTMODE";
    if( !world_input ) {
        reset_pointer_room();
        camera_drag.cancel();
    }
    aiming_input = modern_aiming && category == "TARGET";
    if( world_input || category == "MAIN_MENU" ) {
        in_world = world_input;
    }
    overlay_input = category == "MODERN_2D_CONTEXT" || category == "FP3D_WHEEL" || aiming_input;
    if( category == "MAIN_MENU" ) {
        fp3d_modern_ui::clear_resume();
        if( gpu_live_world ) {
            gpu_live_world->hide();
            pending_gpu_view.reset();
            gpu_views.clear();
            gpu_world_qa_pending.reset();
        }
    }
    if( !requested() ) {
        return;
    }
    // Native windows are drawn over a frozen 3D background in the terrain
    // draw stage; final composition must not cover their controls.
    const bool was_permitted = draw_permitted;
    draw_permitted = !fp3d_modern_ui::transfer_pending() &&
                     ( category == "DEFAULTMODE" || category == "FP3D_BUILD" || category == "MAIN_MENU" ||
                       aiming_input );
    if( was_permitted && !draw_permitted ) {
        menu_background = copy_view();
        held_swing.reset();
        swing_pending = false;
        // A native window takes the mouse away: never leave a camera drag open.
        orbit_dragging = false;
        pointer.inside = false;
        secondary_select_pending = false;
        secondary_select_tile.reset();
        dispatched_secondary_tile.reset();
    }
    if( draw_permitted ) { menu_background.reset(); }
    if( was_permitted != draw_permitted && g ) {
        g->invalidate_main_ui_adaptor();
    }
    if( category == "MAIN_MENU" ) {
        fp3d_interaction::cancel_direction_selection();
        fp3d_entry::reset();
        fp3d_inventory::clear_slots();
        latest_scene.reset();
        latest_2d_hud.reset();
        camera.reset();
        boarded_camera.reset();
        accepted_walk.reset();
        orbit_dragging = false;
        pointer.inside = false;
        secondary_select_pending = false;
        secondary_select_tile.reset();
        dispatched_secondary_tile.reset();
    }
    sync_mouse();
}

void publish( fp3d_snapshot::scene_snapshot scene )
{
    fp3d_regression::snapshot_captured( scene.capture_ms );
    fp3d_regression::publish_called();
    if( !latest_scene || latest_scene->origin != scene.origin ||
        latest_scene->visible != scene.visible || latest_scene->remembered != scene.remembered ||
        latest_scene->sprites.size() != scene.sprites.size() ) {
        const auto count_kind = [&]( fp3d_snapshot::terrain_kind kind ) {
            return std::count_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & tile ) {
                return tile.kind == kind;
            } );
        };
        DebugLog( D_INFO, D_MAIN ) << "fp3d: snapshot origin=" << scene.origin
                                   << ", visible=" << scene.visible << ", remembered=" << scene.remembered
                                   << ", walls=" << count_kind( fp3d_snapshot::terrain_kind::wall )
                                   << ", closed_doors=" << count_kind( fp3d_snapshot::terrain_kind::closed_door )
                                   << ", open_doors=" << count_kind( fp3d_snapshot::terrain_kind::open_door )
                                   << ", sprites=" << scene.sprites.size()
                                   << ", landscape_patches=" << scene.landscape.size()
                                   << ", z_range=" << scene.min_z << ".." << scene.max_z
                                   << ", capture_ms=" << scene.capture_ms;
    }
    // capture() already connected these stairs; avoid a second full-scene
    // scan and sort when publishing each turn. Synthetic scenes still work.
    if( !scene.stairs_connected ) {
        fp3d_snapshot::connect_stairs( scene );
    }
    const unsigned now = SDL_GetTicks();
    camera.confirm( scene.origin, now, accepted_walk && *accepted_walk == scene.origin );
    if( scene.rider ) {
        // Native turn authority stays in CDDA. Rotate only the *display*
        // heading by the confirmed difference so free mouse-look remains.
        // The overhead camera has its own azimuth, so the delta is tracked but
        // not applied there.
        const float delta = boarded_camera.confirm( *scene.rider, now );
        if( !overhead_view ) {
            yaw = std::remainder( yaw + delta, 360.0f );
        }
    } else {
        boarded_camera.reset();
    }
    scene.render_generation = ++published_generation;
    contents_generation = scene.render_generation;
    if( !scene.geometry_generation ) {
        fp3d_snapshot::reuse_geometry( scene, latest_scene ? &*latest_scene : nullptr );
    }
    live_terrain_index().invalidate_vehicle_roofs();
    latest_scene = std::move( scene );
}

bool gpu_world_qa_requested()
{
    const char *path = std::getenv( "CDDA_FP3D_GPU_WORLD_QA_PATH" );
    return gpu_2d_available && !gpu_live_world && !gpu_world_qa_attempted && path && *path;
}

static std::optional<fp3d_targeting::edge_outline> pointer_sprite_outline(
    const fp3d_snapshot::scene_snapshot &scene, const fp3d_targeting::target &target,
    const fp3d_camera::pose &pose, float aspect )
{
    using fp3d_snapshot::sprite_kind;
    const auto sprite = std::find_if( scene.sprites.begin(), scene.sprites.end(),
    [&]( const fp3d_snapshot::sprite & image ) {
        return !image.is_avatar && image.position == target.position && image.id == target.id &&
               ( target.type == fp3d_targeting::kind::item ? image.kind == sprite_kind::item :
                 image.kind == sprite_kind::character || image.kind == sprite_kind::monster );
    } );
    if( sprite == scene.sprites.end() ) {
        return std::nullopt;
    }
    const auto p = sprite->position - scene.origin;
    const std::array<float, 3> base{ static_cast<float>( p.x() ),
                                    p.z() * fp3d_camera::floor_height + sprite->elevation,
                                    static_cast<float>( p.y() ) };
    const fp3d_camera::frustum camera( pose.yaw, pose.pitch, aspect );
    const std::array<float, 3> right{ camera.right_x(), 0.f, camera.right_z() };
    const std::array<float, 3> up{ camera.up_x(), camera.up_y(), camera.up_z() };
    const float height = sprite->height * sprite->display_scale;
    const float half = height * .5f;
    const float top = height - fp3d_geometry::billboard_lift;
    using fp3d_geometry::billboard_position;
    fp3d_targeting::edge_outline outline{ { billboard_position( base, right, up, -half, 0.f ),
            billboard_position( base, right, up, half, 0.f ),
            billboard_position( base, right, up, half, top ),
            billboard_position( base, right, up, -half, top ) } };
    if( const auto *alpha = fp3d_materials::sprite_alpha( *sprite, SDL_GetTicks() ) ) {
        const auto vertex = [&]( float x, float y ) {
            float u = x / alpha->width;
            if( sprite->mirrored ) { u = 1.f - u; }
            return billboard_position( base, right, up, ( u - .5f ) * height,
                                       ( 1.f - y / alpha->height ) * top );
        };
        alpha->edges( 0, 0, alpha->width, alpha->height, [&]( int x1, int y1, int x2, int y2 ) {
            outline.segments.push_back( { vertex( x1, y1 ), vertex( x2, y2 ) } );
        } );
    }
    return outline;
}

static void prepare_gpu_world( const fp3d_snapshot::scene_snapshot &scene,
                               const fp3d_camera::pose &view, bool overhead, bool reuse_material_slots = false )
{
    Uint32 width = 512;
    Uint32 height = 512;
    if( gpu_live_world || gpu_world_composite_path() ) {
        auto area = viewport_pixels();
        if( area[2] <= 0 || area[3] <= 0 ) {
            SDL_GetRenderOutputSize( renderer, &area[2], &area[3] );
        }
        if( area[2] > 0 && area[3] > 0 ) {
            const float longest_edge = std::max( area[2], area[3] );
            const float scale = gpu_live_world ? std::min( 1.f, 4096.f / longest_edge ) :
                                512.f / longest_edge;
            width = std::max( 1l, std::lround( area[2] * scale ) );
            height = std::max( 1l, std::lround( area[3] * scale ) );
        }
    }
    const float aspect = static_cast<float>( width ) / height;
    const auto aim = gpu_live_world && !aiming_input ?
                     interaction_target( view, aspect, overhead_pointer_reach() ) : std::nullopt;
    const bool selecting_with_pointer = overhead && fp3d_interaction::direction_selection_pending();
    std::optional<fp3d_targeting::edge_outline> outline;
    if( aim && ( !selecting_with_pointer ||
                 ( world_input && g && fp3d_interaction::direction_selection_accepts(
                       *aim, get_avatar(), get_map() ) ) ) ) {
        outline = aim->outline;
        const bool hover_monster = overhead && world_pointer_input() && !selecting_with_pointer &&
                                   visible_pointer_monster( *aim );
        const bool hover_item = overhead && world_pointer_input() &&
                                aim->type == fp3d_targeting::kind::item;
        if( ( selecting_with_pointer || hover_monster || hover_item ) && !outline &&
            ( aim->type == fp3d_targeting::kind::item || aim->type == fp3d_targeting::kind::creature ) ) {
            outline = pointer_sprite_outline( scene, *aim, view, aspect );
        }
        if( outline && hover_monster && fp3d_interaction::can_quick_melee( get_avatar(), get_map(),
                get_map().get_bub( aim->position ) ) ) {
            outline->color = { 1.f, .12f, .12f, 1.f };
        }
    }
    const auto legacy_shaders = fp3d_legacy_shader_bridge::current( gpu_live_world != nullptr );
    const bool daylight_shadows = legacy_shaders.active();
    const bool local_lights = daylight_shadows && fp3d_shaderpacks::local_lights_enabled();
    gpu_world_qa_pending = fp3d_gpu_world_qa::prepare( scene, view, overhead, width, height,
                           gpu_live_world != nullptr, local_lights,
                           gpu_live_world && fp3d_shaderpacks::local_shadows_enabled(),
                           daylight_shadows &&
                           fp3d_shaderpacks::shadows_enabled() && fp3d_shaderpacks::quality() >= 3,
                           gpu_live_world && construction_preview ? &*construction_preview : nullptr,
                           outline ? &*outline : nullptr,
                           gpu_live_world ? &live_terrain_index() : nullptr, reuse_material_slots,
                           panel_input && pickup_source_highlight ? &*pickup_source_highlight : nullptr,
                           curtain_peek ? &*curtain_peek : nullptr,
                           overhead ? fp3d_camera::open_structure_opacity( orbit_camera.distance ) : 1.f );
    if( gpu_world_qa_pending && legacy_shaders.active() ) {
        fp3d_legacy_shader_bridge::configure_frame( legacy_shaders, *gpu_world_qa_pending,
                scene, view, overhead, width, height );
    }
    if( !gpu_world_qa_pending ) {
        DebugLog( D_WARNING, D_MAIN ) << "fp3d: GPU world has no eligible terrain geometry";
    }
}

void capture_gpu_world_qa( avatar &you, const map &here )
{
    if( !gpu_world_qa_requested() ) { return; }
    gpu_world_qa_attempted = true;
    const bool overhead = !env_enabled( "CDDA_FP3D_GPU_WORLD_QA_FIRST_PERSON" );
    const auto scene = fp3d_snapshot::capture_camera_reusing( you, here, nullptr,
                       fp3d_snapshot::render_above, fp3d_snapshot::render_below, overhead );
    fp3d_camera::offset confirmed_eye;
    if( scene.rider ) {
        fp3d_camera::rider_follower pose;
        pose.confirm( *scene.rider, SDL_GetTicks() );
        confirmed_eye = pose.relative_to( scene.origin, SDL_GetTicks() );
    } else if( scene.player_crouching ) {
        confirmed_eye.height -= .43f;
    }
    const fp3d_camera::pose view = overhead ?
                                   fp3d_camera::orbit_pose( orbit_camera, confirmed_eye.x, confirmed_eye.y ) :
                                   fp3d_camera::pose{ yaw, pitch, confirmed_eye };
    prepare_gpu_world( scene, view, overhead );
}

void gpu_world_qa_after_present( bool presented )
{
    gpu_views.finish_present( presented );
    if( !presented ) {
        if( gpu_live_world ) {
            pending_gpu_view.reset();
            gpu_world_qa_pending.reset();
        }
        return;
    }
    if( gpu_world_qa_composited && gpu_world_qa_preview ) {
        SDL_WaitForGPUIdle( gpu_world_qa_preview->device() );
        gpu_world_qa_preview.reset();
    }
    if( !gpu_world_qa_pending ) return;
    if( gpu_live_world ) {
        if( ( active() || ( panel_input && turn_based() ) ) && in_world && latest_scene && pending_gpu_view &&
            pending_gpu_view->render_generation == latest_scene->render_generation &&
            pending_gpu_view->overhead == overhead_view && gpu_live_world->ready() ) {
            gpu_live_world->collect_auxiliary();
            if( gpu_world_qa_pending->atmosphere.pbr_eye_strength[3] > 0 ) {
                gpu_world_qa_pending->auxiliary_slots = fp3d_materials::visible_auxiliary_slots( *latest_scene );
            }
            const auto result = gpu_live_world->submit( *gpu_world_qa_pending );
            if( result == fp3d_gpu::submission_result::submitted ) {
                gpu_views.submit( *pending_gpu_view );
                if( ( panel_input || curtain_peek ) && g ) {
                    g->invalidate_main_ui_adaptor();
                    if( panel_input ) {
                        ui_manager::invalidate_all_ui_adaptors();
                    }
                }
                if( selftest ) {
                    ++frames;
                }
            } else if( result == fp3d_gpu::submission_result::failed ) {
                DebugLog( D_WARNING, D_MAIN ) << "fp3d: native GPU world submission failed: " << SDL_GetError();
            }
        }
        pending_gpu_view.reset();
        gpu_world_qa_pending.reset();
        return;
    }
    const char *path = std::getenv( "CDDA_FP3D_GPU_WORLD_QA_PATH" );
    if( !path || !*path || !fp3d_gpu_world_qa::render_after_present( renderer,
            *gpu_world_qa_pending, path, gpu_world_composite_path() ? &gpu_world_qa_preview : nullptr ) ) {
        DebugLog( D_WARNING, D_MAIN ) << "fp3d: GPU_WORLD_QA failed: " << SDL_GetError();
    }
    gpu_world_qa_pending.reset();
}

void publish_camera( avatar &you, const map &here, bool cut_overhead )
{
    const bool native_peek = cut_overhead && curtain_peek.has_value();
    const bool reveal = native_peek;
    publish( fp3d_snapshot::capture_camera_reusing( you, here,
             latest_scene ? &*latest_scene : nullptr,
             fp3d_snapshot::render_above, fp3d_snapshot::render_below, cut_overhead, reveal, native_peek, pointer_room ) );
}

void begin_action()
{
    fp3d_regression::action_boundary();
    accepted_walk.reset();
}

void record_hit( const tripoint_abs_ms &position, int damage, int hp, int maximum )
{
    if( !view_3d() || damage <= 0 ) {
        return;
    }
    const unsigned now = SDL_GetTicks();
    recent_hits.erase( std::remove_if( recent_hits.begin(), recent_hits.end(), [&]( const auto & hit ) {
        return now - hit.at > 1250;
    } ), recent_hits.end() );
    if( recent_hits.size() >= 8 ) {
        recent_hits.erase( recent_hits.begin() );
    }
    recent_hits.push_back( { position, damage, std::max( 0, hp ), std::max( 1, maximum ), now } );
}

float hit_flash( const tripoint_abs_ms &position )
{
    const unsigned now = SDL_GetTicks();
    for( auto it = recent_hits.rbegin(); it != recent_hits.rend(); ++it ) {
        if( it->position == position && now - it->at < 260 ) {
            return 1.0f - ( now - it->at ) / 260.0f;
        }
    }
    return 0.0f;
}

std::optional<hit_feedback> latest_hit()
{
    if( recent_hits.empty() ) {
        return std::nullopt;
    }
    const auto &last = recent_hits.back();
    const unsigned elapsed = SDL_GetTicks() - last.at;
    if( elapsed >= 1250 ) {
        return std::nullopt;
    }
    return hit_feedback{ last.damage, last.hp, last.maximum, elapsed / 1250.0f };
}

void melee_swing()
{
    // Start on the first displayed frame, after native combat/world processing.
    swing_pending = true;
}

void movement_finished( const tripoint_abs_ms &before, const tripoint_abs_ms &after,
                        const tripoint_rel_ms &expected )
{
    if( !requested() || expected == tripoint_rel_ms::zero ) {
        return;
    }
    if( after - before == expected ) {
        accepted_walk = after;
    } else {
        // Blocked or redirected motion must immediately agree with the avatar.
        camera.reset();
        camera.confirm( after, SDL_GetTicks() );
    }
}

bool tick()
{
    sync_mouse();
    static uint64_t last_pan = 0;
    const uint64_t now = SDL_GetTicks();
    if( now - last_pan >= 16 ) {
        const bool panning = overhead() && world_input && active() && right_camera_dragging();
        const auto area = viewport_pixels();
        const point size( std::max( 2, area[2] ), std::max( 2, area[3] ) );
        const point center = size / 2;
        const point cursor = panning && pointer.inside ?
                             point( ( pointer.ndc_x + 1 ) * .5f * size.x,
                                    ( 1 - pointer.ndc_y ) * .5f * size.y ) : center;
        overhead_pan.update( overhead(), cursor, size, ( now - last_pan ) / 1000.f );
        last_pan = now;
        std::optional<tripoint_abs_ms> target;
        if( panning && pointer.inside && latest_scene && window && !aiming_input &&
            SDL_GetKeyboardFocus() == window ) {
            // Recompute after pan/rotation/zoom even when the mouse stands still.
            const auto floor = fp3d_camera::pointer_floor( camera_pose( now ),
                               static_cast<float>( size.x ) / size.y, pointer.ndc_x, pointer.ndc_y );
            if( floor ) {
                target = latest_scene->origin + tripoint_rel_ms(
                             static_cast<int>( std::floor( ( *floor )[0] + .5f ) ),
                             static_cast<int>( std::floor( ( *floor )[1] + .5f ) ), 0 );
            }
        }
        if( target != pointer_room ) {
            pointer_room = target;
            if( latest_scene && g && fp3d_snapshot::update_pointer_room(
                    *latest_scene, get_avatar(), get_map(), target ) ) {
                // Camera-only changes must not invalidate the native terrain UI:
                // that calls prepare_view/capture on the entire loaded map.
                latest_scene->render_generation = ++published_generation;
                pending_gpu_view.reset();
                gpu_world_qa_pending.reset();
            }
        }
    }
    if( requested() && window ) {
        std::string title = std::string( "CDDA 0.I-1 - " ) + ( enabled ? "3D" : "2D" ) +
                            ( modern_2d ? " / 现代键位" : " / 经典键位" ) +
                            " (F5 / F6: 模式设置)";
        static const bool performance_qa = []() {
            const char *value = std::getenv( "CDDA_FP3D_FRAME_SELFTEST" );
            return value && std::strcmp( value, "1" ) == 0;
        }();
        if( performance_qa ) {
            title = "性能采样（独立副本） - " + title;
        }
        if( std::strcmp( SDL_GetWindowTitle( window ), title.c_str() ) != 0 ) {
            SDL_SetWindowTitle( window, title.c_str() );
        }
    }
    if( selftest && available && in_world && SDL_GetTicks() >= next_toggle ) {
        if( toggles < 150 ) {
            cycle_mode();
            next_toggle = SDL_GetTicks() + 100;
        } else if( test_step == 0 ) {
            DebugLog( D_INFO, D_MAIN ) << "fp3d: SELFTEST 50 three-mode preset round trips, frames=" << frames;
            // Starting in 3D is the default in the current build; exactly 150
            // transitions return to the initial mode, not necessarily to 2D.
            if( enabled != selftest_initial_3d || modern_2d != selftest_initial_modern ||
                frames == 0 ) {
                disable( "selftest mode or frame invariant failed" );
                return false;
            }
            enabled = true;
            SDL_GetWindowSize( window, &original_width, &original_height );
            SDL_SetWindowSize( window, original_width + 80, original_height + 48 );
            test_step = 1;
            next_toggle = SDL_GetTicks() + 500;
        } else if( test_step == 1 ) {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize( window, &width, &height );
            if( width != original_width + 80 || height != original_height + 48 ) {
                disable( "selftest window resize failed" );
                return false;
            }
            toggle_fullscreen_window(); // The verified 0.I-1 entry point.
            test_step = 2;
            next_toggle = SDL_GetTicks() + 500;
        } else if( test_step == 2 ) {
            if( !( SDL_GetWindowFlags( window ) & SDL_WINDOW_FULLSCREEN ) ) {
                disable( "selftest enter fullscreen failed" );
                return false;
            }
            toggle_fullscreen_window();
            test_step = 3;
            next_toggle = SDL_GetTicks() + 500;
        } else {
            if( SDL_GetWindowFlags( window ) & SDL_WINDOW_FULLSCREEN ) {
                disable( "selftest leave fullscreen failed" );
                return false;
            }
            SDL_SetWindowSize( window, original_width, original_height );
            selftest = false;
            DebugLog( D_INFO, D_MAIN ) << "fp3d: SELFTEST resize/fullscreen round trip complete";
        }
    }
    const auto &submitted = gpu_views.submitted_view();
    const unsigned desired = pickup_source_highlight ? pickup_revision : 0;
    return active() || ( panel_input && turn_based() &&
                         ( submitted ? submitted->pickup_revision : 0 ) != desired );
}

void gpu_world_qa_capture_composite()
{
    if( !gpu_world_qa_composited || !gpu_world_qa_preview ) {
        return;
    }
    const char *path = gpu_world_composite_path();
    if( !path ) {
        return;
    }
    std::unique_ptr<SDL_Surface, decltype( &SDL_DestroySurface )> pixels(
        SDL_RenderReadPixels( renderer, nullptr ), SDL_DestroySurface );
    const bool saved = pixels && SDL_SaveBMP( pixels.get(), path );
    DebugLog( D_INFO, D_MAIN ) << "fp3d: GPU_WORLD_QA composite_saved=" << saved
                              << ", width=" << ( pixels ? pixels->w : 0 )
                              << ", height=" << ( pixels ? pixels->h : 0 );
}

// Native cues compose after both world renderers.
static void draw_map_overlays( const fp3d_camera::pose &overlay_camera,
                               const tripoint_abs_ms &origin, const SDL_Rect &clip, float aspect )
{
    if( native_map_overlays.empty() || clip.w <= 0 || clip.h <= 0 ) { return; }
    if( overlay_icon_revision != fp3d_materials::revision() ) {
        overlay_icons.clear();
        overlay_icon_revision = fp3d_materials::revision();
    }
    SDL_Rect old_clip;
    SDL_GetRenderClipRect( renderer, &old_clip );
    const bool clipped = SDL_RenderClipEnabled( renderer );
    SDL_SetRenderClipRect( renderer, &clip );
    for( const auto &overlay : native_map_overlays ) {
        const auto offset = overlay.position - origin;
        // Above/below sound art carries its native storey cue; anchor it
        // to the current map plane like 2D, not inside an unseen room.
        const bool sound = overlay.id.compare( 0, 8, "footstep" ) == 0;
        const auto projected = fp3d_camera::project( overlay_camera, aspect, offset.x(),
                               ( sound ? 0 : offset.z() ) * fp3d_camera::floor_height + .08f, offset.y() );
        if( !projected ) { continue; }
        const auto &p = *projected;
        const int x = clip.x + static_cast<int>( ( p[0] + 1 ) * .5f * clip.w );
        const int y = clip.y + static_cast<int>( ( 1 - p[1] ) * .5f * clip.h );
        if( !overlay.text.empty() ) {
            draw_fp3d_overlay_text( overlay.text, point( x, y ), overlay.color );
            continue;
        }
        auto icon = overlay_icons.find( overlay.id );
        if( icon == overlay_icons.end() ) {
            icon = overlay_icons.emplace( overlay.id, fp3d_materials::tileset_icon( overlay.id ) ).first;
        }
        if( !icon->second ) { continue; }
        const int size = static_cast<int>( std::clamp( clip.w /
                         ( 2.f * fp3d_camera::horizontal_tangent() * p[2] ), 12.f, 96.f ) );
        const SDL_Rect dest{ x - size / 2, y - size / 2, size, size };
        RenderTextureRotatedPixels( renderer, icon->second.get(), nullptr, &dest,
                          overlay.rotation < 0 ? 0 : -90.0 * overlay.rotation, nullptr,
                          overlay.rotation < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE );
    }
    SDL_SetRenderClipRect( renderer, clipped ? &old_clip : nullptr );
}

// ---------------------------------------------------------------- aiming HUD
// World-space aiming markers for the overhead view. Everything here is closed
// except for one point and one stroke: the current aim tile gets four corner
// brackets and a thin ground ring, and a dim dashed line of fire connects the
// avatar to it. Positions come from the same camera projection the world render
// uses, so the markers follow camera movement, orbit and zoom exactly, and the
// hit resolution of the native targeting code is never touched.
namespace
{
inline SDL_Color aim_accent()
{
    // #E5B940
    return SDL_Color{ 0xE5, 0xB9, 0x40, 0xFF };
}
inline SDL_Color aim_muted()
{
    // #9FA6AD
    return SDL_Color{ 0x9F, 0xA6, 0xAD, 0xFF };
}

void aim_stroke( const std::vector<SDL_Point> &points, const SDL_Color &color, Uint8 alpha )
{
    if( points.size() < 2 ) {
        return;
    }
    SDL_SetRenderDrawColor( renderer, color.r, color.g, color.b, alpha );
    RenderLinesPixels( renderer, points.data(), static_cast<int>( points.size() ) );
}

// One corner bracket: two short strokes meeting at the corner.
void aim_bracket( float x, float y, float horizontal, float vertical, float arm,
                  const SDL_Color &color, Uint8 alpha )
{
    aim_stroke( { { static_cast<int>( std::lround( x ) ), static_cast<int>( std::lround( y ) ) },
        { static_cast<int>( std::lround( x + horizontal * arm ) ), static_cast<int>( std::lround( y ) ) } },
    color, alpha );
    aim_stroke( { { static_cast<int>( std::lround( x ) ), static_cast<int>( std::lround( y ) ) },
        { static_cast<int>( std::lround( x ) ), static_cast<int>( std::lround( y + vertical * arm ) ) } },
    color, alpha );
}

// Thin ground ring: a flattened ellipse so it reads as lying on the floor
// instead of as a second reticle.
void aim_ground_ring( float cx, float cy, float radius, float squash,
                      const SDL_Color &color, Uint8 alpha )
{
    constexpr int segments = 28;
    std::vector<SDL_Point> points;
    points.reserve( segments + 1 );
    for( int i = 0; i <= segments; ++i ) {
        const float angle = 2.f * 3.14159265358979323846f * static_cast<float>( i ) / segments;
        points.push_back( { static_cast<int>( std::lround( cx + std::cos( angle ) * radius ) ),
                            static_cast<int>( std::lround( cy + std::sin( angle ) * radius * squash ) ) } );
    }
    aim_stroke( points, color, alpha );
}
}

static void draw_aiming_markers( const fp3d_camera::pose &camera, const tripoint_abs_ms &origin,
                                 const SDL_Rect &clip, float aspect )
{
    if( !aimed_tile || clip.w <= 0 || clip.h <= 0 ) {
        return;
    }
    struct screen_point {
        float x = 0.f;
        float y = 0.f;
        float tile = 0.f;
    };
    const auto project = [&]( const tripoint_abs_ms &position, float height,
    screen_point & out ) {
        const auto offset = position - origin;
        const auto projected = fp3d_camera::project( camera, aspect, offset.x(), height, offset.y() );
        if( !projected ) {
            return false;
        }
        const auto &p = *projected;
        out.x = clip.x + ( p[0] + 1.f ) * .5f * clip.w;
        out.y = clip.y + ( 1.f - p[1] ) * .5f * clip.h;
        out.tile = clip.w / ( 2.f * fp3d_camera::horizontal_tangent() * std::max( p[2], .05f ) );
        // Reject markers whose anchor sits well outside the 3D viewport; the
        // clip rect alone would still let a long stroke smear along an edge.
        const float margin = std::max( out.tile, 4.f ) * 3.f;
        return out.x > clip.x - margin && out.x < clip.x + clip.w + margin &&
               out.y > clip.y - margin && out.y < clip.y + clip.h + margin;
    };

    screen_point target;
    if( !project( *aimed_tile, .05f, target ) ) {
        return;
    }
    screen_point source;
    const bool has_source = project( origin, .05f, source );

    SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode( renderer, &previous_blend );
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 0;
    SDL_GetRenderDrawColor( renderer, &r, &g, &b, &a );
    SDL_Rect clip_before{};
    SDL_GetRenderClipRect( renderer, &clip_before );    const bool was_clipped = SDL_RenderClipEnabled( renderer );
    SDL_SetRenderClipRect( renderer, &clip );
    SDL_SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );

    const float tile = std::max( target.tile, 6.f );

    // Line of fire: dim, dashed and thin, so it never competes with the world.
    if( has_source && ( std::fabs( source.x - target.x ) > 1.f ||
                        std::fabs( source.y - target.y ) > 1.f ) ) {
        const float dx = target.x - source.x;
        const float dy = target.y - source.y;
        const float length = std::hypot( dx, dy );
        const float dash = std::max( 4.f, tile * .22f );
        const float nx = dx / length;
        const float ny = dy / length;
        for( float run = 0.f; run < length; run += dash * 2.f ) {
            const float end = std::min( run + dash, length );
            aim_stroke( { { static_cast<int>( std::lround( source.x + nx * run ) ),
                    static_cast<int>( std::lround( source.y + ny * run ) ) },
                { static_cast<int>( std::lround( source.x + nx * end ) ),
                    static_cast<int>( std::lround( source.y + ny * end ) ) } },
            aim_accent(), 105 );
        }
    }

    // Weak highlight for the tile under the mouse when it differs from the aim
    // tile (keyboard aiming), never a second full lock frame.
    if( const auto hovered = pointer_tile(); hovered && *hovered != *aimed_tile ) {
        screen_point hover;
        if( project( *hovered, .05f, hover ) ) {
            const float half = std::max( hover.tile, 6.f ) * .42f;
            aim_bracket( hover.x - half, hover.y - half, 1.f, 1.f, half * .45f, aim_muted(), 70 );
            aim_bracket( hover.x + half, hover.y - half, -1.f, 1.f, half * .45f, aim_muted(), 70 );
            aim_bracket( hover.x - half, hover.y + half, 1.f, -1.f, half * .45f, aim_muted(), 70 );
            aim_bracket( hover.x + half, hover.y + half, -1.f, -1.f, half * .45f, aim_muted(), 70 );
        }
    }

    // Current target: four corner brackets plus a thin ring at its feet. Only
    // the aimed tile is marked, so no field of large frames appears.
    const bool creature = aimed_creature && *aimed_creature == *aimed_tile;
    const float half = tile * ( creature ? .62f : .5f );
    const float arm = half * ( creature ? .55f : .4f );
    const Uint8 alpha = creature ? 235 : 150;
    aim_bracket( target.x - half, target.y - half, 1.f, 1.f, arm, aim_accent(), alpha );
    aim_bracket( target.x + half, target.y - half, -1.f, 1.f, arm, aim_accent(), alpha );
    aim_bracket( target.x - half, target.y + half, 1.f, -1.f, arm, aim_accent(), alpha );
    aim_bracket( target.x + half, target.y + half, -1.f, -1.f, arm, aim_accent(), alpha );
    if( creature ) {
        aim_ground_ring( target.x, target.y + tile * .1f, tile * .52f, .34f, aim_accent(), 150 );
    } else {
        aim_ground_ring( target.x, target.y + tile * .1f, tile * .38f, .32f, aim_muted(), 90 );
    }

    SDL_SetRenderClipRect( renderer, was_clipped ? &clip_before : nullptr );
    SDL_SetRenderDrawBlendMode( renderer, previous_blend );
    SDL_SetRenderDrawColor( renderer, r, g, b, a );
}

static void draw_gpu_hand_and_reticle( const SDL_FRect &view, const gpu_view_state &frame )
{
    // The SDL compositor displays the last submitted world frame, which can
    // trail the live avatar snapshot by one movement update. The hand is a
    // screen-space avatar overlay and must not disappear during that handoff.
    if( frame.overhead || !latest_scene ) {
        return;
    }
    const unsigned now = SDL_GetTicks();
    if( swing_pending ) {
        held_swing.start( now );
        swing_pending = false;
    }
    if( latest_scene->held_item ) {
        const auto uv = fp3d_materials::lookup_gpu(
                            fp3d_materials::sprite_key( *latest_scene->held_item ) );
        auto *atlas = fp3d_materials::gpu_colour_atlas( renderer );
        float w = 0;
        float h = 0;
        if( uv && atlas && SDL_GetTextureSize( atlas, &w, &h ) ) {
            const float strike = std::clamp( held_swing.amount( now ), 0.f, 1.f );
            const float size = view.h * .525f * ( 1.f - .22f * strike );
            const SDL_FRect source{ uv->left * w, uv->top * h,
                                   ( uv->right - uv->left ) * w, ( uv->bottom - uv->top ) * h };
            const SDL_FRect dest{ view.x + view.w * ( .81f - .16f * strike ) - size / 2,
                                  view.y + view.h * ( .86f - .25f * strike ) - size / 2,
                                  size, size };
            SDL_BlendMode previous = SDL_BLENDMODE_NONE;
            SDL_GetTextureBlendMode( atlas, &previous );
            SDL_SetTextureBlendMode( atlas, SDL_BLENDMODE_BLEND );
            const SDL_Rect clip{ static_cast<int>( view.x ), static_cast<int>( view.y ),
                                 static_cast<int>( view.w ), static_cast<int>( view.h ) };
            SDL_Rect previous_clip{};
            SDL_GetRenderClipRect( renderer, &previous_clip );
            const bool clipped = SDL_RenderClipEnabled( renderer );
            SDL_SetRenderClipRect( renderer, &clip );
            SDL_RenderTextureRotated( renderer, atlas, &source, &dest, 20 + 65 * strike,
                                      nullptr, SDL_FLIP_HORIZONTAL );
            SDL_SetRenderClipRect( renderer, clipped ? &previous_clip : nullptr );
            SDL_SetTextureBlendMode( atlas, previous );
        }
    }
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor( renderer, &r, &g, &b, &a );
    const float x = view.x + view.w / 2;
    const float y = view.y + view.h / 2;
    if( !aiming_input && !overhead_view &&
        frame.geometry_generation == latest_scene->geometry_generation &&
        interaction_available( interaction_target( frame.camera, frame.aspect, frame.pointer_reach ) ) ) {
        SDL_SetRenderDrawColor( renderer, 255, 191, 77, 255 );
        const float size = view.h * .009f;
        const SDL_FPoint lines[] = { { x, y + size }, { x + size, y }, { x, y - size },
                                    { x - size, y }, { x, y + size } };
        SDL_RenderLines( renderer, lines, 5 );
    } else {
        SDL_SetRenderDrawColor( renderer, 204, 204, 204, 255 );
        const float size = view.h * .006f;
        SDL_RenderLine( renderer, x - size, y, x + size, y );
        SDL_RenderLine( renderer, x, y - size, x, y + size );
    }
    SDL_SetRenderDrawColor( renderer, r, g, b, a );
}

void compose()
{
    gpu_views.begin_composition();
    auto preparation = fp3d_gpu::preparation_mode::defer;
    const bool panel_highlight = panel_input && turn_based();
    const unsigned desired_pickup_revision = panel_highlight && pickup_source_highlight ? pickup_revision : 0;
    if( gpu_live_world && ( active() || panel_highlight ) && in_world && latest_scene &&
        !gpu_world_qa_pending ) {
        const auto &submitted = gpu_views.submitted_view();
        const bool pickup_changed = ( submitted ? submitted->pickup_revision : 0 ) != desired_pickup_revision;
        const bool materials_current = submitted && !construction_preview &&
                                       submitted->contents_generation == contents_generation &&
                                       submitted->material_revision == fp3d_materials::revision() &&
                                       submitted->overhead == overhead_view;
        if( !panel_highlight || pickup_changed ) {
            preparation = gpu_live_world->preparation( materials_current && !pickup_changed );
        }
    }
    if( preparation != fp3d_gpu::preparation_mode::defer ) {
        const auto area = viewport_pixels();
        if( area[2] > 0 && area[3] > 0 ) {
            texture_width = area[2];
            texture_height = area[3];
        }
        const auto pose = camera_pose( SDL_GetTicks() );
        prepare_gpu_world( *latest_scene, pose, overhead_view,
                           preparation == fp3d_gpu::preparation_mode::reuse_materials );
        if( gpu_world_qa_pending ) {
            pending_gpu_view = gpu_view_state{ pose, latest_scene->origin,
                latest_scene->geometry_generation, latest_scene->render_generation,
                gpu_world_qa_pending->material_revision, overhead_view,
                static_cast<float>( gpu_world_qa_pending->width ) / gpu_world_qa_pending->height,
                overhead_pointer_reach(), desired_pickup_revision, contents_generation
            };
        }
    }
    SDL_Texture *native_view = gpu_live_world ? gpu_live_world->texture() :
                               gpu_world_qa_preview && !gpu_world_qa_composited ?
                               gpu_world_qa_preview->color() : nullptr;
    if( native_view && ( gpu_live_world ? active() && in_world && !curtain_peek : modern_2d_world() ) ) {
        int width = 0;
        int height = 0;
        GetLogicalRenderSize( renderer, &width, &height );
        if( width <= 0 || height <= 0 ) {
            SDL_GetCurrentRenderOutputSize( renderer, &width, &height );
        }
        const SDL_FRect view = viewport_width > 0 && viewport_height > 0 ?
                               SDL_FRect{ viewport_x * width, viewport_y * height,
                                          viewport_width * width, viewport_height * height } :
                               SDL_FRect{ 0, 0, static_cast<float>( width ), static_cast<float>( height ) };
        const bool composed = SDL_RenderTexture( renderer, native_view, nullptr, &view );
        if( !gpu_live_world ) {
            gpu_world_qa_composited = composed;
        } else if( composed ) {
            gpu_views.world_composed();
            const auto &frame = gpu_views.target_view();
            if( frame && frame->overhead ) {
                const SDL_Rect view_clip{ static_cast<int>( view.x ), static_cast<int>( view.y ),
                                          static_cast<int>( view.w ), static_cast<int>( view.h ) };
                draw_map_overlays( frame->camera, frame->origin, view_clip, frame->aspect );
                if( aiming_input ) {
                    draw_aiming_markers( frame->camera, frame->origin, view_clip, frame->aspect );
                }
            } else if( frame ) {
                draw_gpu_hand_and_reticle( view, *frame );
            }
        }
    }
}

void release()
{
    camera_drag.cancel();
    overhead_pan = {};
    reset_pointer_room();
    curtain_peek.reset();
    native_map_overlays.clear();
    overlay_icons.clear();
    overlay_icon_revision = 0;
    fp3d_entry::reset();
    fp3d_modern_ui::clear_resume();
    menu_background.reset();
    if( gpu_world_qa_preview ) {
        SDL_FlushRenderer( renderer );
        SDL_WaitForGPUIdle( gpu_world_qa_preview->device() );
        gpu_world_qa_preview.reset();
    }
    gpu_world_qa_composited = false;
    gpu_live_world.reset();
    pending_gpu_view.reset();
    gpu_views.clear();
    fp3d_materials::release();
    if( requested() ) {
        SDL_SetWindowRelativeMouseMode( window, false );
    }
    renderer = nullptr;
    window = nullptr;
    gpu_world_qa_pending.reset();
    gpu_world_qa_attempted = false;
    available = enabled = selftest = gpu_2d_available = false;
    panel_input = modern_aiming = aiming_input = false;
    aimed_creature.reset();
    aimed_tile.reset();
    modern_2d = in_world = world_input = overlay_input = false;
    overhead_view = false;
    orbit_dragging = false;
    pointer.inside = false;
    secondary_select_pending = false;
    secondary_select_tile.reset();
    dispatched_secondary_tile.reset();
    draw_permitted = true;
    latest_scene.reset();
    construction_preview.reset();
    latest_2d_hud.reset();
    camera.reset();
    boarded_camera.reset();
    held_swing.reset();
    swing_pending = false;
    accepted_walk.reset();
}
namespace aiming
{
void set_active( bool active )
{
    modern_aiming = active;
    orbit_dragging = false;
    if( !active ) {
        aimed_creature.reset();
        aimed_tile.reset();
    }
    set_input_context( active ? "TARGET" : "DEFAULTMODE" );
}

bool active()
{
    return aiming_input;
}

void set_target( std::optional<tripoint_abs_ms> position )
{
    aimed_creature = position;
}

std::optional<tripoint_abs_ms> target()
{
    return aiming_input ? aimed_creature : std::nullopt;
}

void set_aim_tile( std::optional<tripoint_abs_ms> position )
{
    aimed_tile = position;
}

std::optional<tripoint_abs_ms> aim_tile()
{
    return aiming_input ? aimed_tile : std::nullopt;
}

std::optional<tripoint_abs_ms> pick( const point &mouse )
{
    update_pointer( mouse.x, mouse.y );
    return pointer_tile();
}
}
} // namespace fp3d_compositor
#endif
