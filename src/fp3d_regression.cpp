#include "fp3d_regression.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <vector>
#include <cstring>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "debug.h"
#include "fp3d_compositor.h"
#include "fp3d_materials.h"
#include "game.h"
#include "options.h"
#include "rng.h"
#include "sdl_wrappers.h"
#include "ui_manager.h"

namespace fp3d_regression
{
namespace
{
bool frame_sampling_requested()
{
    static const bool enabled = []() {
        const char *value = std::getenv( "CDDA_FP3D_FRAME_SELFTEST" );
        return value && std::strcmp( value, "1" ) == 0;
    }
    ();
    return enabled;
}
struct frame_sample_state {
    Uint64 warmup = 0;
    Uint64 started = 0;
    Uint64 previous = 0;
    bool done = false;
    int width = 0;
    int height = 0;
    bool resized = false;
    size_t unfocused = 0;
    size_t moving_captures = 0;
    tripoint_abs_ms position;
    time_point turn;
    double first_move_ms = -1;
    double last_move_ms = -1;
    std::vector<double> intervals;
    std::vector<double> captures;
    std::vector<double> detail_costs;
    std::vector<double> distant_costs;
    std::vector<double> perception_costs;
    std::vector<double> cell_costs;
};
frame_sample_state frame_samples;
bool world_ready = false;

void log_frame_distribution( const char *name, std::vector<double> values )
{
    if( values.empty() ) {
        DebugLog( D_INFO, D_MAIN ) << "fp3d: FRAME_SELFTEST " << name << ", samples=0";
        return;
    }
    std::sort( values.begin(), values.end() );
    const auto percentile = [&]( double p ) {
        return values[static_cast<size_t>( std::ceil( p * values.size() ) ) - 1];
    };
    DebugLog( D_INFO, D_MAIN ) << "fp3d: FRAME_SELFTEST " << name << ", samples=" << values.size()
                               << ", mean_ms=" << std::accumulate( values.begin(), values.end(), 0.0 ) / values.size()
                               << ", p50_ms=" << percentile( .50 ) << ", p95_ms=" << percentile( .95 )
                               << ", p99_ms=" << percentile( .99 ) << ", max_ms=" << values.back();
}

void check_idle_world()
{
    static const bool requested = []() {
        const char *value = std::getenv( "CDDA_FP3D_IDLE_SELFTEST" );
        return value && std::strcmp( value, "1" ) == 0;
    }
    ();
    static unsigned stage = 0;
    if( !requested || stage == 2 || !world_ready || !g || g->new_game ||
        !fp3d_compositor::turn_based() ) {
        return;
    }
    static Uint32 started = 0;
    static tripoint_abs_ms position;
    static time_point turn;
    static int moves = 0;
    static int stamina = 0;
    static int hp = 0;
    static int hunger = 0;
    static int thirst = 0;
    static cata_default_random_engine engine;
    const avatar &you = get_avatar();
    if( stage == 0 ) {
        started = SDL_GetTicks();
        position = you.pos_abs();
        turn = calendar::turn;
        moves = you.get_moves();
        stamina = you.get_stamina();
        hp = you.get_hp();
        hunger = you.get_hunger();
        thirst = you.get_thirst();
        engine = rng_get_engine();
        stage = 1;
        DebugLog( D_INFO, D_MAIN ) << "fp3d: IDLE_SELFTEST begin, duration_ms=60000, turn="
                                   << to_turn<int>( turn ) << ", moves=" << moves;
    } else if( SDL_GetTicks() - started >= 60000 ) {
        const bool unchanged = position == you.pos_abs() && turn == calendar::turn &&
                               moves == you.get_moves() && stamina == you.get_stamina() &&
                               hp == you.get_hp() && hunger == you.get_hunger() &&
                               thirst == you.get_thirst() && engine == rng_get_engine();
        stage = 2;
        DebugLog( D_INFO, D_MAIN ) << "fp3d: IDLE_SELFTEST complete, elapsed_ms="
                                   << SDL_GetTicks() - started << ", passed=" << unchanged
                                   << ", turn=" << to_turn<int>( calendar::turn )
                                   << ", moves=" << you.get_moves();
    }
}
}
turn_probe::turn_probe()
{
    if( frame_sampling_requested() && world_ready && !frame_samples.done ) {
        started = SDL_GetPerformanceCounter();
    }
}
turn_probe::~turn_probe()
{
    stage( "end" );
}
void turn_probe::stage( const char *name )
{
    if( !started ) {
        return;
    }
    const auto now = SDL_GetPerformanceCounter();
    const double milliseconds = ( now - started ) * 1000.0 / SDL_GetPerformanceFrequency();
    if( milliseconds >= 50 && g && !g->new_game ) {
        DebugLog( D_INFO, D_MAIN ) << "fp3d: FRAME_SELFTEST native_stage=" << current
                                   << ", ms=" << milliseconds << ", turn=" << to_turn<int>( calendar::turn )
                                   << ", queued_steps=" << get_avatar().get_auto_move_route().size();
    }
    current = name;
    started = now;
}

void action_boundary()
{
    if( !world_ready && g && !g->new_game ) {
        world_ready = true;
        if( frame_sampling_requested() ) {
            DebugLog( D_INFO, D_MAIN ) << "fp3d: FRAME_SELFTEST native action boundary, turn="
                                       << to_turn<int>( calendar::turn ) << ", moves=" << get_avatar().get_moves()
                                       << ", queued_steps=" << get_avatar().get_auto_move_route().size();
        }
    }
}
void snapshot_costs( double detail_ms, double distant_ms, double perception_ms, double cells_ms )
{
    if( frame_sampling_requested() && frame_samples.started && !frame_samples.done ) {
        frame_samples.detail_costs.push_back( detail_ms );
        frame_samples.distant_costs.push_back( distant_ms );
        frame_samples.perception_costs.push_back( perception_ms );
        frame_samples.cell_costs.push_back( cells_ms );
    }
}
void snapshot_captured( double milliseconds )
{
    if( frame_sampling_requested() && frame_samples.started && !frame_samples.done ) {
        frame_samples.captures.push_back( milliseconds );
        const auto position = get_avatar().pos_abs();
        if( position != frame_samples.position ) {
            frame_samples.position = position;
            ++frame_samples.moving_captures;
            const double elapsed = ( SDL_GetPerformanceCounter() - frame_samples.started ) *
                                   1000.0 / SDL_GetPerformanceFrequency();
            if( frame_samples.first_move_ms < 0 ) {
                frame_samples.first_move_ms = elapsed;
            }
            frame_samples.last_move_ms = elapsed;
        }
    }
}
void presented( SDL_Renderer *renderer )
{
    if( !frame_sampling_requested() || frame_samples.done ) {
        return;
    }
    auto &state = frame_samples;
    if( !world_ready || !g || g->new_game || !fp3d_compositor::turn_based() ) {
        if( state.started ) {
            state.done = true;
            DebugLog( D_WARNING, D_MAIN ) << "fp3d: FRAME_SELFTEST aborted: left loaded 3D game";
        }
        state.warmup = 0;
        return;
    }
    // Do not turn a background launch into an accepted foreground benchmark.
    if( !state.started && SDL_GetKeyboardFocus() != SDL_RenderGetWindow( renderer ) ) {
        state.warmup = 0;
        return;
    }
    const Uint64 now = SDL_GetPerformanceCounter();
    const double frequency = SDL_GetPerformanceFrequency();
    if( !state.warmup ) {
        state.warmup = now;
    }
    if( ( now - state.warmup ) * 1000.0 / frequency < 2000 ) {
        return;
    }
    int width = 0;
    int height = 0;
    if( SDL_GetRendererOutputSize( renderer, &width, &height ) != 0 ) {
        return;
    }
    if( !state.started ) {
        state.intervals.reserve( 10000 );
        state.captures.reserve( 1000 );
        state.detail_costs.reserve( 1000 );
        state.distant_costs.reserve( 1000 );
        state.perception_costs.reserve( 1000 );
        state.cell_costs.reserve( 1000 );
        state.started = state.previous = now;
        state.width = width;
        state.height = height;
        state.position = get_avatar().pos_abs();
        state.turn = calendar::turn;
        const auto viewport = fp3d_compositor::viewport_pixels();
        DebugLog( D_INFO, D_MAIN ) << "fp3d: FRAME_SELFTEST begin, duration_ms=60000, warmup_ms=2000"
                                   << ", width=" << width << ", height=" << height
                                   << ", viewport_width=" << viewport[2] << ", viewport_height=" << viewport[3]
                                   << ", queued_steps=" << get_avatar().get_auto_move_route().size();
        return;
    }
    const double interval = ( now - state.previous ) * 1000.0 / frequency;
    state.intervals.push_back( interval );
    if( interval >= 50 ) {
        DebugLog( D_INFO, D_MAIN ) << "fp3d: FRAME_SELFTEST slow_present, ms=" << interval
                                   << ", index=" << state.intervals.size() << ", turn=" << to_turn<int>( calendar::turn )
                                   << ", queued_steps=" << get_avatar().get_auto_move_route().size();
    }
    state.previous = now;
    state.resized = state.resized || width != state.width || height != state.height;
    state.unfocused += SDL_GetKeyboardFocus() != SDL_RenderGetWindow( renderer );
    const double elapsed = ( now - state.started ) * 1000.0 / frequency;
    if( elapsed < 60000 ) {
        return;
    }
    state.done = true;
    DebugLog( D_INFO, D_MAIN ) << "fp3d: FRAME_SELFTEST complete, elapsed_ms=" << elapsed
                               << ", intervals=" << state.intervals.size()
                               << ", average_fps=" << state.intervals.size() * 1000.0 / elapsed
                               << ", width=" << state.width << ", height=" << state.height
                               << ", resized=" << state.resized << ", unfocused_frames=" << state.unfocused
                               << ", moving_captures=" << state.moving_captures
                               << ", first_move_ms=" << state.first_move_ms << ", last_move_ms=" << state.last_move_ms
                               << ", world_elapsed_seconds=" << to_seconds<int>( calendar::turn - state.turn )
                               << ", queued_steps=" << get_avatar().get_auto_move_route().size();
    log_frame_distribution( "present_interval", state.intervals );
    log_frame_distribution( "snapshot", state.captures );
    log_frame_distribution( "snapshot_detail", state.detail_costs );
    log_frame_distribution( "snapshot_distant", state.distant_costs );
    log_frame_distribution( "snapshot_perception", state.perception_costs );
    log_frame_distribution( "snapshot_cells", state.cell_costs );
    if( const char *path = std::getenv( "CDDA_FP3D_FRAME_REPORT" ) ) {
        std::ofstream report( path );
        report << std::setprecision( 15 ) << "kind,index,milliseconds\n";
        const auto write_samples = [&]( const char *kind, const std::vector<double> &values ) {
            for( size_t i = 0; i < values.size(); ++i ) {
                report << kind << ',' << i << ',' << values[i] << '\n';
            }
        };
        write_samples( "present", state.intervals );
        write_samples( "snapshot", state.captures );
        write_samples( "snapshot_detail", state.detail_costs );
        write_samples( "snapshot_distant", state.distant_costs );
        write_samples( "snapshot_perception", state.perception_costs );
        write_samples( "snapshot_cells", state.cell_costs );
        report.close();
        DebugLog( D_INFO, D_MAIN ) << "fp3d: FRAME_SELFTEST raw_report=" << path
                                   << ", written=" << !report.fail();
    }
}
void tick()
{
    check_idle_world();
    static const bool requested = []() {
        const char *value = std::getenv( "CDDA_FP3D_TILESET_SELFTEST" );
        return value && std::strcmp( value, "1" ) == 0;
    }
    ();
    if( !requested ) {
        return;
    }
    static bool loading = false;
    static unsigned stage = 0;
    static Uint32 next = 0;
    if( loading || stage == 4 || !g || !fp3d_compositor::captures_game_input() ||
        SDL_GetTicks() < next ) {
        return;
    }
    static tripoint_abs_ms position;
    static time_point turn;
    static int moves = 0;
    static int stamina = 0;
    static int hp = 0;
    static std::string original;
    static bool passed = true;
    const avatar &you = get_avatar();
    if( stage == 0 ) {
        if( !fp3d_materials::ready() ) {
            return;
        }
        position = you.pos_abs();
        turn = calendar::turn;
        moves = you.get_moves();
        stamina = you.get_stamina();
        hp = you.get_hp();
        original = get_option<std::string>( "TILES" );
        stage = 1;
        next = SDL_GetTicks() + 2000;
        DebugLog( D_INFO, D_MAIN ) << "fp3d: TILESET_SELFTEST begin, source=" << original
                                   << ", position=" << position << ", moves=" << moves
                                   << ", turn=" << to_turn<int>( turn );
        return;
    }
    const bool unchanged = position == you.pos_abs() && turn == calendar::turn &&
                           moves == you.get_moves() && stamina == you.get_stamina() && hp == you.get_hp();
    const bool material_ready = fp3d_materials::ready();
    passed = passed && unchanged && material_ready;
    DebugLog( D_INFO, D_MAIN ) << "fp3d: TILESET_SELFTEST stage=" << stage
                               << ", source=" << get_option<std::string>( "TILES" )
                               << ", world_unchanged=" << unchanged << ", atlas_ready=" << material_ready;
    if( stage == 3 ) {
        stage = 4;
        DebugLog( D_INFO, D_MAIN ) << "fp3d: TILESET_SELFTEST complete, passed=" << passed;
        return;
    }
    const std::string target = stage == 1 ? ( original == "retrodays" ?
                               "UNDEAD_PEOPLE_BASE" : "retrodays" ) : original;
    ++stage;
    // Same option and native reload entry point as the graphics menu. The guard
    // covers nested event pumps during loading; no game action is submitted.
    loading = true;
    get_options().get_option( "TILES" ).setValue( target );
    g->reload_tileset();
    g->invalidate_main_ui_adaptor();
    ui_manager::redraw_invalidated();
    next = SDL_GetTicks() + 2000;
    loading = false;
}
} // namespace fp3d_regression
#endif
