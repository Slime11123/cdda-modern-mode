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

#include "debug.h"
#include "game.h"
#include "fp3d_camera.h"
#include "fp3d_input.h"
#include "imgui/imgui.h"
#include "fp3d_inventory.h"
#include "fp3d_materials.h"
#include "fp3d_scene.h"
#include "fp3d_snapshot.h"
#include "sdltiles.h"

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
bool enabled = false;
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
unsigned published_generation = 0;
unsigned prepared_generation = 0;
unsigned prepared_material_revision = 0;
fp3d_camera::follower camera;
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
    static const bool value = env_enabled( "CDDA_FP3D" );
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
    enabled = env_enabled( "CDDA_FP3D_START_ACTIVE" );
    selftest = env_enabled( "CDDA_FP3D_SELFTEST" );
    started = last_report = SDL_GetTicks();
    next_toggle = started + 100;
    DebugLog( D_INFO, D_MAIN ) << "fp3d: probe_revision=2, attached SDL " << static_cast<int>
                               ( version.major )
                               << "." << static_cast<int>( version.minor ) << "."
                               << static_cast<int>( version.patch ) << ", renderer=" << info.name
                               << ", GL=" << glGetString( GL_VERSION )
                               << ", GPU=" << glGetString( GL_RENDERER ) << "; F5 toggles probe";
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
        sync_mouse();
    }
    // Gameplay uses key codes. Discard delayed composition/text messages so
    // an IME prefix cannot become a second game command after capture starts.
    if( captures_game_input() ) {
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
    if( captures_game_input() && ( event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ) ) {
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
        fp3d_input::world_gestures().mouse_button( event.button.button,
                event.type == SDL_MOUSEBUTTONDOWN, SDL_GetTicks() );
        return true; // Never let a world click become native 2D auto-travel.
    }
    if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F5 ) {
        if( !event.key.repeat && available ) {
            enabled = !enabled;
            if( g ) {
                g->invalidate_main_ui_adaptor();
            }
            camera.reset();
            if( latest_scene ) {
                camera.confirm( latest_scene->origin, SDL_GetTicks() );
            }
            sync_mouse();
            ++toggles;
            DebugLog( D_INFO, D_MAIN ) << "fp3d: F5 enabled=" << enabled << ", toggles=" << toggles;
        }
        return true;
    }
    return false;
}

bool menu_text_input()
{
    return panel_input && turn_based() && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
}

bool captures_game_input()
{
    return requested() && available && enabled && draw_permitted && latest_scene && window &&
           SDL_GetKeyboardFocus() == window;
}

bool covers_map( const tripoint_abs_ms &center )
{
    return available && enabled && draw_permitted && latest_scene && latest_scene->origin == center;
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
}

std::vector<std::string> hud_lines()
{
    if( !turn_based() || !draw_permitted ) {
        return {};
    }
    if( construction_preview ) {
        return { construction_preview->name, construction_preview->valid ?
                 "可建造：E / 左键确认；Esc 取消" : "此格不可建造；转动视角选择相邻格；Esc 取消" };
    }
    std::vector<std::string> lines{ latest_scene->hud.status };
    if( fp3d_hud::navigation_enabled() && latest_scene->hud.navigation_heading ) {
        const float delta = std::remainder( *latest_scene->hud.navigation_heading - yaw, 360.0f );
        lines.push_back( "目的地 " + std::string( std::abs( delta ) < 15 ? "前方" : delta > 0 ?
                         "右转" : "左转" ) +
                         " / 大地图距离 " + std::to_string( latest_scene->hud.navigation_distance ) +
                         " / 楼层差 " + std::to_string( latest_scene->hud.navigation_z ) );
    }
    const auto &slots = latest_scene->hud.slots;
    lines.insert( lines.end(), slots.begin(), slots.end() );
    if( fp3d_input::world_gestures().expanded_hud() ) {
        const auto &expanded = latest_scene->hud.expanded;
        lines.insert( lines.end(), expanded.begin(), expanded.end() );
    }
    const auto target = interaction_target();
    const bool reachable = target && target->position.z() == latest_scene->origin.z() &&
                           square_dist( target->position, latest_scene->origin ) <= 1;
    lines.push_back( reachable ? "E 交互 / F 拾取 / Tab 面板 / Q 行动轮盘" : target ?
                     "目标超出触及范围 / 请靠近并保持同层" : "Tab 面板 / Q 行动轮盘 / Alt 状态 / F6 键位" );
    return lines;
}

SDL_Texture_Ptr copy_view()
{
    if( !available || !framebuffer || !latest_scene || SDL_GL_GetCurrentContext() != context ) {
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

std::optional<fp3d_targeting::target> interaction_target()
{
    return latest_scene ? fp3d_targeting::ray( *latest_scene, yaw, pitch,
           camera.relative_to( latest_scene->origin, SDL_GetTicks() ) ) : std::nullopt;
}

float heading()
{
    return yaw;
}

void set_viewport( int x, int y, int width, int height, int display_width, int display_height )
{
    if( display_width > 0 && display_height > 0 ) {
        viewport_x = static_cast<float>( x ) / display_width;
        viewport_y = static_cast<float>( y ) / display_height;
        viewport_width = static_cast<float>( width ) / display_width;
        viewport_height = static_cast<float>( height ) / display_height;
    }
}

void set_input_context( const std::string &category )
{
    panel_input = category == "FP3D_PANEL";
    if( category == "MAIN_MENU" ) { fp3d_modern_ui::clear_resume(); }
    if( !requested() ) {
        return;
    }
    // Menus own the screen and mouse while they collect input. The probe is
    // temporary presentation, so suppress it rather than cover original UI.
    const bool was_permitted = draw_permitted;
    draw_permitted = !fp3d_modern_ui::transfer_pending() &&
                     ( category == "DEFAULTMODE" || category == "FP3D_BUILD" || category == "MAIN_MENU" );
    if( was_permitted != draw_permitted && g ) {
        g->invalidate_main_ui_adaptor();
    }
    if( category == "MAIN_MENU" ) {
        fp3d_inventory::clear_slots();
        latest_scene.reset();
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
    camera.confirm( scene.origin, SDL_GetTicks(), accepted_walk && *accepted_walk == scene.origin );
    scene.render_generation = ++published_generation;
    latest_scene = std::move( scene );
}

void begin_action()
{
    fp3d_regression::action_boundary();
    accepted_walk.reset();
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
        std::string title = enabled ? "CDDA 0.I-1 - OpenGL 3D probe (F5: 2D)" :
                            "CDDA 0.I-1 - 2D 原版操作 (F5: 3D)";
        if( enabled && latest_scene ) {
            const auto &p = latest_scene->origin;
            title = "CDDA 0.I-1 - 3D map [" + std::to_string( p.x() ) + "," +
                    std::to_string( p.y() ) + "," + std::to_string( p.z() ) + "] V:" +
                    std::to_string( latest_scene->visible ) + " M:" +
                    std::to_string( latest_scene->remembered ) + " (3D 操作 | F5: 2D | F6: 按键设置)";
        }
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
        if( toggles < 100 ) {
            SDL_Event event{};
            event.type = SDL_KEYDOWN;
            event.key.windowID = SDL_GetWindowID( window );
            event.key.keysym.sym = SDLK_F5;
            if( SDL_PushEvent( &event ) != 1 ) {
                disable( "selftest event enqueue failed" );
            }
            next_toggle = SDL_GetTicks() + 100;
        } else if( test_step == 0 ) {
            DebugLog( D_INFO, D_MAIN ) << "fp3d: SELFTEST 50 F5 round trips, frames=" << frames
                                       << ", target_allocations=" << allocations;
            if( enabled || frames == 0 || allocations != 1 ) {
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
    return available && enabled && draw_permitted;
}

void compose()
{
    if( !available || !enabled || !draw_permitted ) {
        return;
    }
    if( latest_scene && ( prepared_generation != latest_scene->render_generation ||
                          prepared_material_revision != fp3d_materials::revision() ) ) {
        fp3d_materials::prepare( *latest_scene );
        prepared_generation = latest_scene->render_generation;
        prepared_material_revision = fp3d_materials::revision();
    }
    if( latest_scene && construction_preview && construction_preview->terrain ) {
        fp3d_snapshot::scene_snapshot materials;
        materials.season = latest_scene->season;
        materials.cells.push_back( *construction_preview->terrain );
        fp3d_materials::prepare( materials );
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
            fp3d_scene::draw( *latest_scene, yaw, pitch,
                              static_cast<float>( texture_width ) / texture_height,
                              camera.relative_to( latest_scene->origin, SDL_GetTicks() ), construction_preview );
            fp3d_materials::unbind();
        } else {
            draw_cube();
        }

        bind_framebuffer( GL_FRAMEBUFFER, saved.old_framebuffer );
        glViewport( 0, 0, width, height );
        glDisable( GL_DEPTH_TEST );
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
    fp3d_modern_ui::clear_resume();
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
    draw_permitted = true;
    latest_scene.reset();
    construction_preview.reset();
    camera.reset();
    accepted_walk.reset();
}
} // namespace fp3d_compositor
#endif
