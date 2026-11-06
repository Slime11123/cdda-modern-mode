#include "fp3d_modern_ui.h"
#include "fp3d_compositor.h"
#include "fp3d_regression.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

#include "avatar.h"
#include "debug.h"
#include "game.h"
#include "fp3d_camera.h"
#include "fp3d_input.h"
#include "fp3d_entry.h"
#include "imgui/imgui.h"
#include "fp3d_inventory.h"
#include "fp3d_materials.h"
#include "fp3d_scene.h"
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
SDL_GLContext context = nullptr; // Borrowed; never create or swap a context.
GLuint framebuffer = 0;
GLuint color_texture = 0;
GLuint depth_buffer = 0;
SDL_Texture_Ptr menu_background;
bool enabled = false;
bool modern_2d = false;
bool in_world = false;
bool world_input = false;
bool overlay_input = false;
bool panel_input = false;
bool available = false;
bool draw_permitted = true;
bool selftest = false;
unsigned toggles = 0;
unsigned allocations = 0;
unsigned frames = 0;
unsigned test_step = 0;
int original_width = 0;
int original_height = 0;
Uint32 next_toggle = 0;
Uint32 started = 0;
Uint32 last_report = 0;
float yaw = 0.0f;
float pitch = 0.0f;
std::optional<fp3d_snapshot::scene_snapshot> latest_scene;
std::optional<fp3d_hud::data> latest_2d_hud;
unsigned published_generation = 0;
unsigned prepared_generation = 0;
unsigned prepared_material_revision = 0;
fp3d_camera::follower camera;
fp3d_camera::swing held_swing;
bool swing_pending = false;
std::optional<tripoint_abs_ms> accepted_walk;
float viewport_x = 0;
float viewport_y = 0;
float viewport_width = 0;
float viewport_height = 0;

void sync_mouse()
{
    const bool capture = available && enabled && draw_permitted && latest_scene && window &&
                         SDL_GetKeyboardFocus() == window;
    if( !capture ) {
        fp3d_input::world_gestures().cancel();
    }
    if( capture != ( SDL_GetRelativeMouseMode() == SDL_TRUE ) &&
        SDL_SetRelativeMouseMode( capture ? SDL_TRUE : SDL_FALSE ) != 0 ) {
        enabled = false;
        SDL_SetRelativeMouseMode( SDL_FALSE );
        DebugLog( D_WARNING, D_MAIN ) << "fp3d: mouse capture failed; keeping original 2D";
    }
}

PFNGLGENFRAMEBUFFERSPROC gen_framebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC bind_framebuffer = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC delete_framebuffers = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC framebuffer_texture = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC check_framebuffer = nullptr;
PFNGLGENRENDERBUFFERSPROC gen_renderbuffers = nullptr;
PFNGLBINDRENDERBUFFERPROC bind_renderbuffer = nullptr;
PFNGLDELETERENDERBUFFERSPROC delete_renderbuffers = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC renderbuffer_storage = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC framebuffer_renderbuffer = nullptr;
PFNGLUSEPROGRAMPROC use_program = nullptr;
PFNGLACTIVETEXTUREPROC active_texture = nullptr;

bool env_enabled( const char *name )
{
    const char *value = std::getenv( name );
    return value && std::strcmp( value, "1" ) == 0;
}

void disable( const char *reason )
{
    enabled = false;
    available = false;
    selftest = false;
    sync_mouse();
    DebugLog( D_WARNING, D_MAIN ) << "fp3d: disabled: " << reason << "; keeping original 2D";
}

template<typename T>
bool load_proc( T &proc, const char *name, const char *extension = nullptr )
{
    proc = reinterpret_cast<T>( SDL_GL_GetProcAddress( name ) );
    if( !proc && extension ) {
        proc = reinterpret_cast<T>( SDL_GL_GetProcAddress( extension ) );
    }
    return proc != nullptr;
}

// SDL's GL2 renderer uses a compatibility context and caches GL state. Restore
// everything touched, including state not covered by glPushAttrib (FBO,
// program, active texture and matrices). Only SDL's documented GL texture
// bind/unbind interop is allowed inside; no SDL renderer drawing occurs.
struct saved_gl_state {
    GLint old_framebuffer = 0;
    GLint old_renderbuffer = 0;
    GLint old_program = 0;
    GLint old_active_texture = 0;
    GLint old_matrix_mode = 0;

    saved_gl_state() {
        glGetIntegerv( GL_FRAMEBUFFER_BINDING, &old_framebuffer );
        glGetIntegerv( GL_RENDERBUFFER_BINDING, &old_renderbuffer );
        glGetIntegerv( GL_CURRENT_PROGRAM, &old_program );
        glGetIntegerv( GL_ACTIVE_TEXTURE, &old_active_texture );
        glGetIntegerv( GL_MATRIX_MODE, &old_matrix_mode );
        glPushAttrib( GL_ALL_ATTRIB_BITS );
        active_texture( GL_TEXTURE0 );
        use_program( 0 );
        for( GLenum matrix : {
                 GL_PROJECTION, GL_MODELVIEW, GL_TEXTURE
             } ) {
            glMatrixMode( matrix );
            glPushMatrix();
            glLoadIdentity();
        }
    }

    ~saved_gl_state() {
        for( GLenum matrix : {
                 GL_TEXTURE, GL_MODELVIEW, GL_PROJECTION
             } ) {
            glMatrixMode( matrix );
            glPopMatrix();
        }
        bind_framebuffer( GL_FRAMEBUFFER, old_framebuffer );
        bind_renderbuffer( GL_RENDERBUFFER, old_renderbuffer );
        use_program( old_program );
        glPopAttrib();
        active_texture( old_active_texture );
        glMatrixMode( old_matrix_mode );
    }
};

bool create_targets()
{
    gen_framebuffers( 1, &framebuffer );
    bind_framebuffer( GL_FRAMEBUFFER, framebuffer );
    glGenTextures( 1, &color_texture );
    glBindTexture( GL_TEXTURE_2D, color_texture );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, texture_width, texture_height, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    framebuffer_texture( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture, 0 );
    gen_renderbuffers( 1, &depth_buffer );
    bind_renderbuffer( GL_RENDERBUFFER, depth_buffer );
    renderbuffer_storage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, texture_width, texture_height );
    framebuffer_renderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buffer );
    if( check_framebuffer( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
        disable( "incomplete color/depth framebuffer" );
        return false;
    }
    ++allocations;
    DebugLog( D_INFO, D_MAIN ) << "fp3d: targets ready, RGBA8+depth24=" << texture_width << "x"
                               << texture_height << ", allocations="
                               << allocations;
    return true;
}

void draw_cube()
{
    // Unsorted faces, culling disabled: correct occlusion requires depth tests.
    static const GLfloat vertices[8][3] = {
        { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
        { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 }
    };
    static const int faces[6][4] = {
        { 0, 1, 2, 3 }, { 4, 7, 6, 5 }, { 0, 4, 5, 1 },
        { 3, 2, 6, 7 }, { 1, 5, 6, 2 }, { 0, 3, 7, 4 }
    };
    static const GLfloat colors[6][3] = {
        { .95f, .3f, .2f }, { .2f, .7f, .95f }, { .3f, .85f, .45f },
        { .95f, .75f, .2f }, { .65f, .35f, .9f }, { .2f, .85f, .8f }
    };
    glBegin( GL_QUADS );
    for( int face = 0; face < 6; ++face ) {
        glColor3fv( colors[face] );
        for( int index : faces[face] ) {
            glVertex3fv( vertices[index] );
        }
    }
    glEnd();
}
} // namespace

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
    SDL_RendererInfo info{};
    SDL_version version{};
    SDL_GetVersion( &version );
    if( SDL_GetRendererInfo( renderer, &info ) != 0 || !info.name ||
        std::strcmp( info.name, "opengl" ) != 0 ) {
        disable( "actual SDL renderer is not opengl" );
        return;
    }
    if( SDL_RenderFlush( renderer ) != 0 || !( context = SDL_GL_GetCurrentContext() ) ) {
        disable( "SDL renderer has no current GL context" );
        return;
    }
    const bool loaded =
        load_proc( gen_framebuffers, "glGenFramebuffers", "glGenFramebuffersEXT" ) &&
        load_proc( bind_framebuffer, "glBindFramebuffer", "glBindFramebufferEXT" ) &&
        load_proc( delete_framebuffers, "glDeleteFramebuffers", "glDeleteFramebuffersEXT" ) &&
        load_proc( framebuffer_texture, "glFramebufferTexture2D", "glFramebufferTexture2DEXT" ) &&
        load_proc( check_framebuffer, "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT" ) &&
        load_proc( gen_renderbuffers, "glGenRenderbuffers", "glGenRenderbuffersEXT" ) &&
        load_proc( bind_renderbuffer, "glBindRenderbuffer", "glBindRenderbufferEXT" ) &&
        load_proc( delete_renderbuffers, "glDeleteRenderbuffers", "glDeleteRenderbuffersEXT" ) &&
        load_proc( renderbuffer_storage, "glRenderbufferStorage", "glRenderbufferStorageEXT" ) &&
        load_proc( framebuffer_renderbuffer, "glFramebufferRenderbuffer",
                   "glFramebufferRenderbufferEXT" ) &&
        load_proc( use_program, "glUseProgram" ) && load_proc( active_texture, "glActiveTexture" );
    if( !loaded ) {
        disable( "required GL compatibility/FBO functions unavailable" );
        return;
    }
    available = true;
    const auto view = get_option<std::string>( "FP3D_VIEW" );
    // Without a launcher, an untouched profile defaults to 3D; players can
    // still explicitly select 2D in the F5/F6 mode settings.
    const char *start_active = std::getenv( "CDDA_FP3D_START_ACTIVE" );
    const bool default_3d = start_active ? env_enabled( "CDDA_FP3D_START_ACTIVE" ) : requested();
    enabled = view == "3d" || ( view == "auto" && default_3d );
    const auto keys = get_option<std::string>( "FP3D_CONTROLS" );
    modern_2d = keys == "modern" || ( keys == "auto" && enabled );
    selftest = env_enabled( "CDDA_FP3D_SELFTEST" );
    started = last_report = SDL_GetTicks();
    next_toggle = started + 100;
    DebugLog( D_INFO, D_MAIN ) << "fp3d: probe_revision=2, attached SDL " << static_cast<int>
                               ( version.major )
                               << "." << static_cast<int>( version.minor ) << "."
                               << static_cast<int>( version.patch ) << ", renderer=" << info.name
                               << ", GL=" << glGetString( GL_VERSION )
                               << ", GPU=" << glGetString( GL_RENDERER ) << "; F5/F6 opens mode settings";
}

bool process_event( const SDL_Event &event )
{
    if( !requested() ) {
        return false;
    }
    if( !draw_permitted ) {
        return false;
    }
    if( event.type == SDL_WINDOWEVENT ) {
        if( event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ) {
            held_swing.reset();
            swing_pending = false;
        }
        sync_mouse();
    }
    // Gameplay uses key codes. Discard delayed composition/text messages so
    // an IME prefix cannot become a second game command after capture starts.
    if( captures_game_input() || modern_2d_input() ) {
        if( event.type == SDL_TEXTINPUT || event.type == SDL_TEXTEDITING ) {
            return true;
        }
#if defined(SDL_HINT_IME_SUPPORT_EXTENDED_TEXT)
        if( event.type == SDL_TEXTEDITING_EXT ) {
            SDL_free( event.editExt.text );
            return true;
        }
#endif
    }
    if( event.type == SDL_MOUSEMOTION && captures_game_input() ) {
        yaw += event.motion.xrel * 0.4f;
        yaw = std::remainder( yaw, 360.0f );
        pitch = std::clamp( pitch + event.motion.yrel * 0.4f, -80.0f, 80.0f );
        return true;
    }
    if( captures_game_input() && modern_2d && ( event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ) ) {
        auto key = sdl_keysym_to_keycode_evt( event.key.keysym );
        if( event.key.keysym.sym == SDLK_LSHIFT || event.key.keysym.sym == SDLK_LCTRL ||
            event.key.keysym.sym == SDLK_LALT || event.key.keysym.sym == SDLK_RSHIFT ||
            event.key.keysym.sym == SDLK_RCTRL || event.key.keysym.sym == SDLK_RALT ) {
            key = input_event( event.key.keysym.sym, input_event_t::keyboard_code );
        }
        if( event.type == SDL_KEYDOWN && event.key.repeat ) {
            const auto command = fp3d_input::context().input_to_action( key );
            if( command != "ERROR" && command != "FP3D_FORWARD" && command != "FP3D_BACKWARD" &&
                command != "FP3D_LEFT" && command != "FP3D_RIGHT" && command != "FP3D_WAIT" ) {
                return true;
            }
        }
        if( fp3d_input::world_gestures().event( key, event.type == SDL_KEYDOWN, SDL_GetTicks() ) ) {
            return true;
        }

    }
    if( captures_game_input() && ( event.type == SDL_MOUSEBUTTONDOWN ||
                                   event.type == SDL_MOUSEBUTTONUP ) ) {
        if( modern_2d ) {
            fp3d_input::world_gestures().mouse_button( event.button.button,
                    event.type == SDL_MOUSEBUTTONDOWN, SDL_GetTicks() );
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
    return requested() && available && enabled && draw_permitted && latest_scene && window &&
           SDL_GetKeyboardFocus() == window;
}

bool view_3d()
{
    return requested() && enabled;
}

void apply_modes( bool three_d, bool modern )
{
    enabled = three_d && available;
    modern_2d = modern;
    fp3d_input::world_gestures().cancel();
    ui_manager::invalidate_all_ui_adaptors();
    if( g ) {
        g->mark_main_ui_adaptor_resize();
    }
    camera.reset();
    held_swing.reset();
    swing_pending = false;
    if( latest_scene ) {
        camera.confirm( latest_scene->origin, SDL_GetTicks() );
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

bool survival_menu_available()
{
    // Shared menus follow the game session, not the camera or movement bindings.
    return requested() && available && ( in_world || turn_based() );
}

bool modern_2d_world()
{
    return in_world && controls() == fp3d_input::mode::modern_2d && available;
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
    return active() && latest_scene && latest_scene->origin == center;
}

bool active()
{
    return available && enabled && draw_permitted;
}

bool turn_based()
{
    return available && enabled && latest_scene.has_value();
}
std::array<int, 4> viewport_pixels()
{
    int width = 0, height = 0;
    if( renderer ) {
        SDL_GetRendererOutputSize( renderer, &width, &height );
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

std::vector<std::string> hud_lines()
{
    if( !uses_hud() || !draw_permitted ||
        ( enabled ? !latest_scene.has_value() : !latest_2d_hud.has_value() ) ) {
        return {};
    }
    if( enabled && construction_preview ) {
        return { construction_preview->name, construction_preview->valid ?
                 "可建造：E / 左键确认；Esc 取消" : "此格不可建造；转动视角选择相邻格；Esc 取消" };
    }
    const auto &hud = enabled ? latest_scene->hud : *latest_2d_hud;
    std::vector<std::string> lines{ hud.status };
    if( fp3d_hud::navigation_enabled() && hud.navigation_heading ) {
        const float delta = std::remainder( *hud.navigation_heading - heading(), 360.0f );
        lines.push_back( "目的地 " + std::string( std::abs( delta ) < 15 ? "前方" : delta > 0 ?
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
    if( !enabled || !modern_2d ) {
        return lines;
    }
    const auto target = interaction_target();
    const bool reachable = target && target->position.z() == latest_scene->origin.z() &&
                           square_dist( target->position, latest_scene->origin ) <= 1;
    lines.push_back( target && !reachable ? "目标超出触及范围 / 请靠近并保持同层" :
                     "Tab 面板 / Q 行动轮盘 / Alt 状态 / F5/F6 模式设置" );
    return lines;
}

fp3d_hud::data hud_data()
{
    return enabled ? ( latest_scene ? latest_scene->hud : fp3d_hud::data{} ) :
           latest_2d_hud.value_or( fp3d_hud::data{} );
}

static bool interaction_available( const std::optional<fp3d_targeting::target> &target )
{
    if( !turn_based() || !draw_permitted || construction_preview ) { return false; }
    if( !target || target->position.z() != latest_scene->origin.z() ||
        square_dist( target->position, latest_scene->origin ) > 1 ) { return false; }
    return std::find( latest_scene->interactable.begin(), latest_scene->interactable.end(),
                      target->position ) != latest_scene->interactable.end();
}
const std::vector<fp3d_hud::data::message_entry> &target_details()
{
    static const std::vector<fp3d_hud::data::message_entry> empty;
    if( !g || !turn_based() || !draw_permitted || construction_preview ) {
        return empty;
    }
    const auto target = interaction_target();
    if( !target || target->origin != get_avatar().pos_abs() ) {
        return empty;
    }
    static unsigned generation = 0;
    static std::optional<fp3d_targeting::target> previous;
    static std::vector<fp3d_hud::data::message_entry> lines;
    if( generation != latest_scene->render_generation || !previous ||
        target->position != previous->position || target->type != previous->type ||
        target->id != previous->id ) {
        lines = fp3d_hud::inspect( get_avatar(), get_map(), *target );
        previous = target;
        generation = latest_scene->render_generation;
    }
    return lines;
}

bool interaction_available()
{
    return turn_based() && draw_permitted && !construction_preview &&
           interaction_available( interaction_target() );
}

std::string interaction_prompt()
{
    if( !interaction_available() ) { return {}; }
    const auto &bindings = inp_mngr.get_input_for_action( "FP3D_INTERACT", "FP3D" );
    // 3D gestures resolve raw SDL key codes even when the original menus use
    // character input. get_desc would incorrectly hide those physical bindings.
    return "按[" + ( bindings.empty() ? std::string( "未绑定" ) :
                    remove_color_tags( bindings.front().long_description() ) ) + "]互动";
}
bool draw_menu_background( int x, int y, int width, int height )
{
    if( !turn_based() || draw_permitted ) { return false; }
    if( !menu_background ) { menu_background = copy_view(); }
    const SDL_bool clipped = SDL_RenderIsClipEnabled( renderer );
    SDL_Rect previous;
    SDL_RenderGetClipRect( renderer, &previous );
    const SDL_Rect destination{ x, y, width, height };
    SDL_RenderSetClipRect( renderer, &destination );
    if( menu_background ) {
        int output_width = 0, output_height = 0, logical_width = 0, logical_height = 0;
        SDL_GetRendererOutputSize( renderer, &output_width, &output_height );
        SDL_RenderGetLogicalSize( renderer, &logical_width, &logical_height );
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
        SDL_RenderCopyEx( renderer, menu_background.get(), nullptr, &view, 0, nullptr, SDL_FLIP_VERTICAL );
    } else {
        Uint8 r, g, b, a;
        SDL_GetRenderDrawColor( renderer, &r, &g, &b, &a );
        SDL_SetRenderDrawColor( renderer, 0, 0, 0, 255 );
        SDL_RenderFillRect( renderer, &destination );
        SDL_SetRenderDrawColor( renderer, r, g, b, a );
    }
    SDL_RenderSetClipRect( renderer, clipped ? &previous : nullptr );
    // Suppress the terrain draw even if the snapshot could not be copied.
    return true;
}

SDL_Texture_Ptr copy_view()
{
    if( !enabled && modern_2d ) { return copy_2d_display_buffer(); }
    if( !enabled || !available || !framebuffer || !latest_scene || SDL_GL_GetCurrentContext() != context ) {
        return {};
    }
    SDL_RenderFlush( renderer );
    SDL_Texture_Ptr image( SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA8888,
                           SDL_TEXTUREACCESS_STATIC, texture_width, texture_height ) );
    if( !image ) {
        return {};
    }
    saved_gl_state saved;
    if( SDL_GL_BindTexture( image.get(), nullptr, nullptr ) != 0 ) {
        return {};
    }
    bind_framebuffer( GL_FRAMEBUFFER, framebuffer );
    glCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, texture_width, texture_height );
    SDL_GL_UnbindTexture( image.get() );
    SDL_SetTextureBlendMode( image.get(), SDL_BLENDMODE_NONE );
    return image; // GPU-to-GPU snapshot; never reads pixels back to the CPU.
}

static std::optional<fp3d_targeting::target> interaction_target( fp3d_camera::offset offset )
{
    // The reticle, prompt and HUD share the same pure snapshot query. Avoid
    // scanning all scene cells again while the camera and snapshot are unchanged.
    static unsigned generation = 0;
    static float previous_yaw = 0;
    static float previous_pitch = 0;
    static fp3d_camera::offset previous_offset;
    static std::optional<fp3d_targeting::target> cached;
    if( generation != latest_scene->render_generation || previous_yaw != yaw ||
        previous_pitch != pitch || previous_offset.x != offset.x || previous_offset.y != offset.y ) {
        cached = fp3d_targeting::ray( *latest_scene, yaw, pitch, offset );
        generation = latest_scene->render_generation;
        previous_yaw = yaw;
        previous_pitch = pitch;
        previous_offset = offset;
    }
    return cached;
}

std::optional<fp3d_targeting::target> interaction_target()
{
    return latest_scene ? interaction_target( camera.relative_to( latest_scene->origin,
                          SDL_GetTicks() ) ) : std::nullopt;
}

float heading()
{
    return enabled ? yaw : 0.0f;
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
    if( world_input || category == "MAIN_MENU" ) {
        in_world = world_input;
    }
    overlay_input = category == "MODERN_2D_CONTEXT" || category == "FP3D_WHEEL";
    if( category == "MAIN_MENU" ) { fp3d_modern_ui::clear_resume(); }
    if( !requested() ) {
        return;
    }
    // Native windows are drawn over a frozen 3D background in the terrain
    // draw stage; final composition must not cover their controls.
    const bool was_permitted = draw_permitted;
    draw_permitted = !fp3d_modern_ui::transfer_pending() &&
                     ( category == "DEFAULTMODE" || category == "FP3D_BUILD" || category == "MAIN_MENU" );
    if( was_permitted && !draw_permitted ) {
        menu_background = copy_view();
        held_swing.reset();
        swing_pending = false;
    }
    if( draw_permitted ) { menu_background.reset(); }
    if( was_permitted != draw_permitted && g ) {
        g->invalidate_main_ui_adaptor();
    }
    if( category == "MAIN_MENU" ) {
        fp3d_entry::reset();
        fp3d_inventory::clear_slots();
        latest_scene.reset();
        latest_2d_hud.reset();
        camera.reset();
        accepted_walk.reset();
    }
    sync_mouse();
}

void publish( fp3d_snapshot::scene_snapshot scene )
{
    fp3d_regression::snapshot_captured( scene.capture_ms );
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
    fp3d_snapshot::connect_stairs( scene );
    camera.confirm( scene.origin, SDL_GetTicks(), accepted_walk && *accepted_walk == scene.origin );
    scene.render_generation = ++published_generation;
    latest_scene = std::move( scene );
}

void begin_action()
{
    fp3d_regression::action_boundary();
    accepted_walk.reset();
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
    if( selftest && available && SDL_GetTicks() >= next_toggle ) {
        if( toggles < 150 ) {
            cycle_mode();
            next_toggle = SDL_GetTicks() + 100;
        } else if( test_step == 0 ) {
            DebugLog( D_INFO, D_MAIN ) << "fp3d: SELFTEST 50 three-mode preset round trips, frames=" << frames
                                       << ", target_allocations=" << allocations;
            if( enabled || modern_2d || frames == 0 || allocations != 1 ) {
                disable( "selftest toggle/resource invariant failed" );
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
            DebugLog( D_INFO, D_MAIN ) << "fp3d: SELFTEST resize/fullscreen round trip complete; "
                                          "starting continuous cube render";
        }
    }
    return active();
}

void compose()
{
    if( !active() ) {
        return;
    }
    if( latest_scene && ( prepared_generation != latest_scene->render_generation ||
                          prepared_material_revision != fp3d_materials::revision() ) ) {
        const auto *preview = construction_preview && construction_preview->terrain ?
                              &*construction_preview->terrain : nullptr;
        fp3d_materials::prepare( *latest_scene, preview );
        prepared_generation = latest_scene->render_generation;
        prepared_material_revision = fp3d_materials::revision();
    }
    if( SDL_RenderFlush( renderer ) != 0 || SDL_GL_GetCurrentContext() != context ) {
        disable( "flush failed or renderer context changed" );
        return;
    }
    if( glGetError() != GL_NO_ERROR ) {
        disable( "GL error already present before probe" );
        return;
    }
    int width = 0;
    int height = 0;
    if( SDL_GetRendererOutputSize( renderer, &width, &height ) != 0 || width <= 0 || height <= 0 ) {
        return;
    }
    {
        saved_gl_state saved;
        const int desired_width = latest_scene ? std::max( 1,
                                  static_cast<int>( width * viewport_width ) ) : 384;
        const int desired_height = latest_scene ? std::max( 1,
                                   static_cast<int>( height * viewport_height ) ) : 288;
        if( desired_width != texture_width || desired_height != texture_height ) {
            if( framebuffer ) {
                delete_framebuffers( 1, &framebuffer );
                delete_renderbuffers( 1, &depth_buffer );
                glDeleteTextures( 1, &color_texture );
                framebuffer = color_texture = depth_buffer = 0;
            }
            texture_width = desired_width;
            texture_height = desired_height;
        }
        if( !framebuffer && !create_targets() ) {
            return;
        }
        bind_framebuffer( GL_FRAMEBUFFER, framebuffer );
        glViewport( 0, 0, texture_width, texture_height );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_BLEND );
        glDisable( GL_CULL_FACE );
        glDisable( GL_TEXTURE_2D );
        glEnable( GL_DEPTH_TEST );
        glDepthFunc( GL_LESS );
        glDepthMask( GL_TRUE );
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        glClearColor( 0, 0, 0, latest_scene ? 1 : 0 );
        glClearDepth( 1.0 );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        glMatrixMode( GL_PROJECTION );
        glLoadIdentity();
        glFrustum( -.1333, .1333, -.1, .1, .2, 30.0 );
        glMatrixMode( GL_MODELVIEW );
        glLoadIdentity();
        glTranslatef( 0, 0, -4.5f );
        glRotatef( pitch, 1, 0, 0 );
        glRotatef( yaw + ( SDL_GetTicks() - started ) * .025f, 0, 1, 0 );
        if( latest_scene ) {
            fp3d_materials::bind();
            glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
            if( swing_pending ) {
                held_swing.start( SDL_GetTicks() );
                swing_pending = false;
            }
            const auto offset = camera.relative_to( latest_scene->origin, SDL_GetTicks() );
            const auto aim = draw_permitted ? interaction_target( offset ) : std::nullopt;
            fp3d_scene::draw( *latest_scene, yaw, pitch,
                              static_cast<float>( texture_width ) / texture_height,
                              offset, construction_preview, interaction_available( aim ),
                              held_swing.amount( SDL_GetTicks() ), aim );
            fp3d_materials::unbind();
        } else {
            draw_cube();
        }

        bind_framebuffer( GL_FRAMEBUFFER, saved.old_framebuffer );
        glViewport( 0, 0, width, height );
        glDisable( GL_DEPTH_TEST );
        // World passes may use backface culling, cutout alpha, lighting or fog.
        // The complete frame is an unlit screen quad with reversed Y winding.
        glDisable( GL_CULL_FACE );
        glDisable( GL_ALPHA_TEST );
        glDisable( GL_LIGHTING );
        glDisable( GL_FOG );
        glEnable( GL_TEXTURE_2D );
        glBindTexture( GL_TEXTURE_2D, color_texture );
        glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
        if( latest_scene ) {
            // Cutout atlas pixels can retain partial alpha. A complete world
            // frame replaces the 2D map, rather than blending that map through.
            glDisable( GL_BLEND );
        } else {
            glEnable( GL_BLEND );
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        }
        glMatrixMode( GL_PROJECTION );
        glLoadIdentity();
        glOrtho( 0, width, height, 0, -1, 1 );
        glMatrixMode( GL_MODELVIEW );
        glLoadIdentity();
        glColor4f( 1, 1, 1, 1 );
        const float scale = std::min( 1.0f, std::min( width / 768.0f, height / 576.0f ) );
        // Keep the probe over the map, clear of the original right sidebar.
        const float left = latest_scene ? width * viewport_x : 24.0f;
        const float top = latest_scene ? height * viewport_y : 24.0f;
        const float right = left + texture_width * ( latest_scene ? 1.0f : scale );
        const float bottom = top + texture_height * ( latest_scene ? 1.0f : scale );
        glBegin( GL_QUADS );
        glTexCoord2f( 0, 1 );
        glVertex2f( left, top );
        glTexCoord2f( 1, 1 );
        glVertex2f( right, top );
        glTexCoord2f( 1, 0 );
        glVertex2f( right, bottom );
        glTexCoord2f( 0, 0 );
        glVertex2f( left, bottom );
        glEnd();
    }
    const GLenum error = glGetError();
    if( error != GL_NO_ERROR || SDL_RenderFlush( renderer ) != 0 ) {
        disable( "GL pass/state restore failed" );
        return;
    }
    ++frames;
    if( SDL_GetTicks() - last_report >= 60000 ) {
        DebugLog( D_INFO, D_MAIN ) << "fp3d: health elapsed_ms=" << SDL_GetTicks() - started
                                   << ", frames=" << frames << ", toggles=" << toggles
                                   << ", target_allocations=" << allocations << ", gl_error=0";
        last_report = SDL_GetTicks();
    }
}

void release()
{
    fp3d_entry::reset();
    fp3d_modern_ui::clear_resume();
    menu_background.reset();
    fp3d_materials::release();
    if( requested() ) {
        SDL_SetRelativeMouseMode( SDL_FALSE );
    }
    if( context && SDL_GL_GetCurrentContext() == context ) {
        if( renderer ) {
            SDL_RenderFlush( renderer );
        }
        fp3d_scene::release();
        if( framebuffer ) {
            delete_framebuffers( 1, &framebuffer );
        }
        if( depth_buffer ) {
            delete_renderbuffers( 1, &depth_buffer );
        }
        if( color_texture ) {
            glDeleteTextures( 1, &color_texture );
        }
    }
    framebuffer = color_texture = depth_buffer = 0;
    context = nullptr;
    renderer = nullptr;
    window = nullptr;
    available = enabled = selftest = false;
    panel_input = false;
    modern_2d = in_world = world_input = overlay_input = false;
    draw_permitted = true;
    latest_scene.reset();
    construction_preview.reset();
    latest_2d_hud.reset();
    camera.reset();
    held_swing.reset();
    swing_pending = false;
    accepted_walk.reset();
}
} // namespace fp3d_compositor
#endif
