#include "loading_ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "cached_options.h"
#include "catacharset.h"
#include "cata_path.h"
#include "cata_utility.h"
#include "debug.h"
#include "input.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "rng.h"
#include "text_snippets.h"
#include "translations.h"
#include "ui_manager.h"

#if defined(TILES)
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/imgui.h"
#undef IMGUI_DEFINE_MATH_OPERATORS
#include "flexbuffer_json.h"
#include "mod_manager.h"
#include "sdltiles.h"
#include "sdl_wrappers.h"
#include "worldfactory.h"
#else
#include "cursesdef.h"
#endif // TILES

namespace
{

// A vanilla tip is rotated on a monotonic clock, never per rendered frame, so a
// slow (or fast) machine does not change how long a tip stays readable.
constexpr std::chrono::seconds tip_interval( 8 );

struct ui_state {
    ui_adaptor *ui;
    background_pane *bg;
#ifdef TILES
    ImVec2 window_size;
    ImVec2 splash_size;
    // Source pixel size of the splash image, kept so a window resize can
    // recompute the layout without decoding the image again.
    ImVec2 splash_pixels;
    SDL_Texture_Ptr splash;
    cata_path chosen_load_img;
#else
    size_t splash_width = 0;
    std::vector<std::string> splash;
    std::string blanks;
#endif
    std::string context;
    std::string step;
    //! Startup (title screen) mode: opaque full screen layer, vanilla image.
    bool startup = false;
    //! Currently displayed vanilla tip; empty when no tip data is available.
    std::string tip;
    std::chrono::steady_clock::time_point tip_since = std::chrono::steady_clock::now();
    //! Set once the tip source has been read (successfully or not).
    bool tip_source_checked = false;
};

ui_state *gLUI = nullptr;
//! The startup layer is *requested* from begin_startup() until end_startup().
//! Kept separately from the widget pointer because a debug popup during loading
//! destroys the widget (`debugmsg` calls `loading_ui::done()`) without ending the
//! startup phase.
bool startup_lifetime_open = false;
//! Only shown while the caller can actually honour a skip request (i.e. while
//! waiting for the background scene), so the hint never promises nothing.
bool skip_hint_enabled = false;

std::vector<std::string> &tip_cache()
{
    static std::vector<std::string> tips;
    return tips;
}

void add_tip( const std::string &text )
{
    if( remove_color_tags( text ).find_first_not_of( " \t\r\n" ) == std::string::npos ) {
        return;
    }
    tip_cache().push_back( text );
}

/**
 * Reads the vanilla tips into a private display cache.
 *
 * The global snippet library is not available this early (core data is still
 * being loaded and calling it would disturb the data lifecycle), so the same
 * JSON the game uses later is read directly. Nothing is registered globally and
 * no tip text is duplicated by hand.
 */
void load_tips_from_json()
{
#if defined(TILES)
    const cata_path path = PATH_INFO::jsondir() / "tips.json";
    try {
        read_from_file_optional_json( path, []( const JsonValue & root ) {
            if( !root.test_array() ) {
                return;
            }
            for( const JsonValue entry : root.get_array() ) {
                if( !entry.test_object() ) {
                    continue;
                }
                const JsonObject jo = entry.get_object();
                // tips.json carries members the loading screen does not need
                // ("type" and more). This is a display-only read, not a full
                // validation pass, so unread members must not be reported.
                jo.allow_omitted_members();
                if( !jo.has_string( "category" ) || jo.get_string( "category" ) != "tip" ) {
                    continue;
                }
                if( jo.has_array( "text" ) ) {
                    for( const JsonValue text_value : jo.get_array( "text" ) ) {
                        translation text;
                        if( text_value.test_string() && text_value.read( text ) ) {
                            add_tip( text.translated() );
                        }
                    }
                } else if( jo.has_member( "text" ) ) {
                    translation text;
                    if( jo.read( "text", text ) ) {
                        add_tip( text.translated() );
                    }
                }
            }
        } );
    } catch( const std::exception &err ) {
        DebugLog( D_WARNING, D_MAIN ) << "Could not read vanilla tips for the loading screen: " << err.what();
    }
    DebugLog( D_INFO, D_MAIN ) << "loading screen tip cache: " << tip_cache().size() << " entries";
#endif
}

std::string random_tip()
{
    // Once core data is loaded the original category is authoritative, so the
    // displayed text stays identical to the one the title screen uses.
    if( SNIPPET.has_category( "tip" ) ) {
        const std::optional<translation> snippet = SNIPPET.random_from_category( "tip" );
        if( snippet.has_value() ) {
            std::string text = snippet->translated();
            if( remove_color_tags( text ).find_first_not_of( " \t\r\n" ) != std::string::npos ) {
                return text;
            }
        }
    }
    std::vector<std::string> &tips = tip_cache();
    if( tips.empty() ) {
        return std::string();
    }
    return random_entry( tips );
}

void pick_tip( ui_state &state, bool force )
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if( !force ) {
        if( state.tip.empty() ) {
            // Nothing to read yet: keep retrying, but not on every frame.
            if( state.tip_since != std::chrono::steady_clock::time_point() &&
                now - state.tip_since < std::chrono::seconds( 1 ) ) {
                return;
            }
        } else if( now - state.tip_since < tip_interval ) {
            return;
        }
    }
    if( !state.tip_source_checked ) {
        state.tip_source_checked = true;
        load_tips_from_json();
    }
    if( tip_cache().empty() && !SNIPPET.has_category( "tip" ) ) {
        state.tip_since = now;
        return;
    }
    // Avoid showing the same tip twice in a row when there is a choice.
    const std::string previous = state.tip;
    std::string next = random_tip();
    for( int attempt = 0; attempt < 8 && !next.empty() && next == previous; ++attempt ) {
        next = random_tip();
    }
    if( !next.empty() ) {
        state.tip = next;
    }
    state.tip_since = now;
}

#ifdef TILES
/**
 * Scales the splash image to fit 90% of the viewport, preserving its aspect
 * ratio. Recomputed on every layout change, so resizing the window never
 * stretches the artwork.
 */
ImVec2 fitted_splash_size( const ui_state &state )
{
    const ImVec2 max_size = ImGui::GetMainViewport()->Size * 0.9f;
    if( state.splash_pixels.x <= 0.0f || state.splash_pixels.y <= 0.0f ) {
        return ImVec2( 0.0f, 0.0f );
    }
    const float width_ratio = state.splash_pixels.x / max_size.x;
    const float height_ratio = state.splash_pixels.y / max_size.y;
    const float longest_side_ratio = width_ratio > height_ratio ? width_ratio : height_ratio;
    if( longest_side_ratio <= 0.0f ) {
        return state.splash_pixels;
    }
    return ImVec2( state.splash_pixels.x / longest_side_ratio,
                   state.splash_pixels.y / longest_side_ratio );
}

void prepare_splash( ui_state &state )
{
    if( state.chosen_load_img == cata_path() ) {
        state.chosen_load_img = PATH_INFO::gfxdir() / "cdda.png"; // vanilla default load screen
    }
    try {
        SDL_Surface_Ptr surf = load_image( state.chosen_load_img.get_unrelative_path().u8string().c_str() );
        if( !surf || surf->w <= 0 || surf->h <= 0 ) {
            return;
        }
        state.splash_pixels = ImVec2( static_cast<float>( surf->w ), static_cast<float>( surf->h ) );
        // leave some space for the loading text...
        state.splash_size = fitted_splash_size( state );
        state.splash = CreateTextureFromSurface( get_sdl_renderer(), surf );
    } catch( const std::exception &err ) {
        // A missing or broken image must not stop the game from starting: the
        // screen degrades to a solid background plus the stage text.
        DebugLog( D_WARNING, D_MAIN ) << "Loading screen image unavailable: " << err.what();
        state.splash.reset();
    }
}
#endif

void resize()
{
    if( gLUI == nullptr ) {
        return;
    }
#ifdef TILES
    if( gLUI->splash ) {
        gLUI->splash_size = fitted_splash_size( *gLUI );
        gLUI->window_size = gLUI->splash_size +
                            ImVec2( 0.0f, 2.0f * ImGui::GetTextLineHeightWithSpacing() );
    }
#endif
}

void redraw_startup();
void redraw();

void redraw()
{
    if( gLUI->startup ) {
        redraw_startup();
        return;
    }
#ifdef TILES
    ImVec2 pos = { 0.5f, 0.5f };
    ImGui::SetNextWindowPos( ImGui::GetMainViewport()->Size * pos, ImGuiCond_Always, { 0.5f, 0.5f } );
    ImGui::SetNextWindowSize( gLUI->window_size );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
    ImGui::PushStyleColor( ImGuiCol_WindowBg, { 0.0f, 0.0f, 0.0f, 1.0f } );
    if( ImGui::Begin( "Loading…", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings ) ) {
        if( gLUI->splash ) {
            ImGui::Image( reinterpret_cast<ImTextureID>( gLUI->splash.get() ), gLUI->splash_size );
        }
        float w = ImGui::CalcTextSize( gLUI->context.c_str() ).x + ImGui::CalcTextSize( " " ).x +
                  ImGui::CalcTextSize( gLUI->step.c_str() ).x;
        ImGui::SetCursorPosX( ( ( ImGui::GetWindowWidth() - w ) * 0.5f ) );
        ImGui::Text( "%s %s", gLUI->context.c_str(), gLUI->step.c_str() );
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
#else
    int x = ( TERMX - static_cast<int>( gLUI->splash_width ) ) / 2;
    int y = 0;
    nc_color white = c_white;
    for( const std::string &line : gLUI->splash ) {
        if( !line.empty() && line[0] == '#' ) {
            continue;
        }
        print_colored_text( catacurses::stdscr, point( x, y++ ), white,
                            white, line );
    }
    mvwprintz( catacurses::stdscr, point( 0, TERMY - 1 ), c_black, gLUI->blanks );
    center_print( catacurses::stdscr, TERMY - 1, c_white, string_format( "%s %s",
                  gLUI->context.c_str(), gLUI->step.c_str() ) );
#endif
}

/**
 * The startup layer: an opaque full screen base, the vanilla image scaled to
 * fit, the real stage text and a vanilla tip. Nothing of the title screen may
 * show through here.
 */
void redraw_startup()
{
#ifdef TILES
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const ImVec2 vp_size = vp->Size;
    ImGui::SetNextWindowPos( vp->Pos );
    ImGui::SetNextWindowSize( vp_size );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
    ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.0f, 0.0f, 0.0f, 1.0f ) );
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoFocusOnAppearing;
    if( ImGui::Begin( "##startup_loading", nullptr, flags ) ) {
        const float line = ImGui::GetTextLineHeightWithSpacing();
        // Reserve the bottom band for the stage line, a wrapped tip and the
        // optional skip hint, so a long Chinese tip can never overlap them.
        const float text_block = line * 5.0f;
        const ImVec2 avail( vp_size.x * 0.92f,
                            std::max( line, vp_size.y * 0.92f - text_block ) );
        if( gLUI->splash && gLUI->splash_pixels.x > 0.0f && gLUI->splash_pixels.y > 0.0f ) {
            const float scale = std::min( avail.x / gLUI->splash_pixels.x,
                                          avail.y / gLUI->splash_pixels.y );
            if( scale > 0.0f ) {
                const ImVec2 size( gLUI->splash_pixels.x * scale, gLUI->splash_pixels.y * scale );
                ImGui::SetCursorPos( ImVec2( ( vp_size.x - size.x ) * 0.5f,
                                            std::max( 0.0f, ( vp_size.y - text_block - size.y ) * 0.5f ) ) );
                ImGui::Image( reinterpret_cast<ImTextureID>( gLUI->splash.get() ), size );
            }
        }
        const float stage_width = ImGui::CalcTextSize( gLUI->step.c_str() ).x;
        ImGui::SetCursorPos( ImVec2( std::max( 0.0f, ( vp_size.x - stage_width ) * 0.5f ),
                                    std::max( 0.0f, vp_size.y - text_block + line * 0.5f ) ) );
        ImGui::TextUnformatted( gLUI->step.c_str() );
        if( !gLUI->tip.empty() ) {
            ImGui::SetCursorPosX( vp_size.x * 0.1f );
            ImGui::PushTextWrapPos( vp_size.x * 0.9f );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.78f, 0.78f, 0.72f, 1.0f ) );
            ImGui::TextWrapped( "%s", gLUI->tip.c_str() );
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
        if( skip_hint_enabled ) {
            const std::string hint = _( "Press Esc to skip the background wait and open the menu now." );
            const float hint_width = ImGui::CalcTextSize( hint.c_str() ).x;
            ImGui::SetCursorPos( ImVec2( std::max( 0.0f, ( vp_size.x - hint_width ) * 0.5f ),
                                        std::max( 0.0f, vp_size.y - line * 1.5f ) ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.55f, 0.55f, 0.55f, 1.0f ) );
            ImGui::TextUnformatted( hint.c_str() );
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar( 2 );
#else
    const int splash_top = std::max( 0, ( TERMY - static_cast<int>( gLUI->splash.size() ) - 4 ) / 2 );
    int y = splash_top;
    for( const std::string &line : gLUI->splash ) {
        if( !line.empty() && line[0] == '#' ) {
            continue;
        }
        center_print( catacurses::stdscr, y++, c_white, remove_color_tags( line ) );
    }
    if( !gLUI->tip.empty() ) {
        const int width = std::max( 20, TERMX - 4 );
        const std::vector<std::string> folded = foldstring( gLUI->tip, width );
        int tip_y = std::max( y + 1, TERMY - 3 - static_cast<int>( folded.size() ) );
        for( const std::string &line : folded ) {
            center_print( catacurses::stdscr, tip_y++, c_dark_gray, line );
        }
    }
    center_print( catacurses::stdscr, TERMY - 2, c_white, gLUI->step );
    if( skip_hint_enabled ) {
        center_print( catacurses::stdscr, TERMY - 3, c_dark_gray,
                      _( "Press Esc to skip the background wait and open the menu now." ) );
    }
#endif
}

void ensure_state()
{
    if( gLUI != nullptr ) {
        return;
    }
    gLUI = new struct ui_state;
    gLUI->bg = new background_pane;
    gLUI->ui = new ui_adaptor;
    gLUI->ui->is_imgui = true;
    gLUI->ui->on_redraw( []( ui_adaptor & ) {
        redraw();
    } );
    gLUI->ui->on_screen_resize( []( ui_adaptor & ) {
        resize();
    } );
}

void update_state( const std::string &context, const std::string &step )
{
    ensure_state();
#ifdef TILES
    if( gLUI->chosen_load_img == cata_path() ) {
        std::vector<cata_path> imgs;
        // The mod list only exists once a world is active; during startup there
        // is none, and the vanilla default image is the right one.
        if( world_generator && world_generator->active_world != nullptr ) {
            std::vector<mod_id> &active_mod_list = world_generator->active_world->active_mod_order;
            for( mod_id &some_mod : active_mod_list ) {
                // We haven't migrated mods yet
                // TODO: Move mod migration before this function?
                if( !some_mod.is_valid() ) {
                    continue;
                }
                const MOD_INFORMATION &mod = *some_mod;
                for( const std::string &img_name : mod.loading_images ) {
                    // There may be more than one file matching the name, so we need to get all of them
                    for( cata_path &img_path : get_files_from_path( img_name, mod.path, true ) ) {
                        imgs.emplace_back( img_path );
                    }
                }
            }
        }
        if( !imgs.empty() ) {
            gLUI->chosen_load_img = random_entry( imgs );
        }
        prepare_splash( *gLUI );
        gLUI->window_size = gLUI->splash_size + ImVec2{ 0.0f, 2.0f * ImGui::GetTextLineHeightWithSpacing() };
    }
#else
    if( gLUI->splash.empty() ) {
        std::string splash = PATH_INFO::title( get_holiday_from_time() );
        if( get_option<bool>( "ENABLE_ASCII_TITLE" ) ) {
            splash = read_whole_file( splash ).value_or( _( "Cataclysm: Dark Days Ahead" ) );
        }
        gLUI->splash = string_split( splash, '\n' );
        gLUI->blanks = std::string( TERMX, ' ' );
        for( const std::string &line : gLUI->splash ) {
            if( !line.empty() && line[0] == '#' ) {
                continue;
            }
            gLUI->splash_width = std::max( gLUI->splash_width, remove_color_tags( line ).length() );
        }
    }
#endif
    gLUI->context = std::string( context );
    gLUI->step = std::string( step );
}

void present( bool startup_mode )
{
    // A debug popup during loading tears the layer down (see debugmsg), so the
    // state has to exist again before the next frame is drawn.
    ensure_state();
    gLUI->startup = startup_mode;
    ui_manager::redraw();
    refresh_display();
    // Consumes and discards key presses and clicks: nothing typed while the game
    // is still loading may activate a title screen button afterwards.
    inp_mngr.pump_events();
}

/**
 * Same as @ref present, but hands back the key that was pending (already
 * consumed). Used for the "Esc skips this wait" affordance.
 */
input_event present_capturing_key( bool startup_mode )
{
    ensure_state();
    gLUI->startup = startup_mode;
    ui_manager::redraw();
    refresh_display();
    return inp_mngr.pump_events_capture_key();
}

} // namespace

void loading_ui::show( const std::string &context, const std::string &step )
{
    if( test_mode ) {
        return;
    }
    // World/mod loading takes over from the startup layer, including its image.
    const bool leaving_startup = startup_lifetime_open;
    startup_lifetime_open = false;
    if( leaving_startup && gLUI != nullptr ) {
#ifdef TILES
        gLUI->chosen_load_img = cata_path();
        gLUI->splash.reset();
        gLUI->splash_pixels = ImVec2( 0.0f, 0.0f );
        gLUI->splash_size = ImVec2( 0.0f, 0.0f );
#endif
    }
    update_state( context, step );
    present( false );
}

void loading_ui::done()
{
    if( gLUI != nullptr ) {
#ifdef TILES
        gLUI->chosen_load_img = cata_path();
#endif
        delete gLUI->ui;
        delete gLUI->bg;
        delete gLUI;
        gLUI = nullptr;
    }
}

void loading_ui::begin_startup()
{
    if( test_mode ) {
        return;
    }
    startup_lifetime_open = true;
    ensure_state();
    gLUI->context.clear();
    gLUI->step = _( "Initializing the game" );
#ifdef TILES
    prepare_splash( *gLUI );
#endif
    pick_tip( *gLUI, true );
    present( true );
}

bool loading_ui::startup_active()
{
    return startup_lifetime_open && gLUI != nullptr;
}

void loading_ui::set_stage( const std::string &step )
{
    if( test_mode || !startup_lifetime_open ) {
        return;
    }
    if( gLUI != nullptr && gLUI->step == step ) {
        // Same stage: the caller is polling, no extra frame is needed.
        return;
    }
    ensure_state();
    gLUI->step = step;
    present( true );
}

void loading_ui::update()
{
    if( test_mode || !startup_lifetime_open ) {
        return;
    }
    ensure_state();
    pick_tip( *gLUI, false );
    present( true );
}

bool loading_ui::update_and_check_skip()
{
    if( test_mode || !startup_lifetime_open ) {
        return false;
    }
    ensure_state();
    pick_tip( *gLUI, false );
    // The key is consumed by this call either way, so a skip request can never
    // leak into the title screen and activate an entry there.
    const input_event pending = present_capturing_key( true );
    if( pending.type != input_event_t::keyboard_char &&
        pending.type != input_event_t::keyboard_code ) {
        return false;
    }
    return pending.get_first_input() == KEY_ESCAPE;
}

void loading_ui::set_skip_hint( bool enabled )
{
    if( skip_hint_enabled == enabled ) {
        return;
    }
    skip_hint_enabled = enabled;
}

void loading_ui::end_startup()
{
    skip_hint_enabled = false;
    startup_lifetime_open = false;
    done();
}
