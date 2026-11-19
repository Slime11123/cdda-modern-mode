#include "fp3d_regression.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <vector>
#include <cstring>
#include <string>
#include <sstream>

#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

#include "avatar.h"
#include "calendar.h"
#include "debug.h"
#include "fp3d_compositor.h"
#include "fp3d_materials.h"
#include "game.h"
#include "monster.h"
#include "npc.h"
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
    std::vector<double> prepare_totals;
    std::vector<double> prepare_player;
    std::vector<double> prepare_extra;
    std::vector<double> prepare_visibility;
    std::vector<double> full_captures;
    std::vector<double> capture_tails;
    std::vector<double> material_costs;
    std::vector<double> scene_cpu;
    // Building geometry: the static index/regroup block and the chunk pass,
    // also split by whether a vehicle was in view, because the two halves of a
    // city route exercise different invalidation paths.
    std::vector<double> scene_index;
    std::vector<double> scene_chunk_pass;
    std::vector<double> scene_index_vehicles;
    std::vector<double> scene_chunk_pass_vehicles;
    // Whole-step accounting: how much synchronous work one presented frame pays.
    unsigned long presents = 0;
    unsigned long prepare_calls = 0;
    unsigned long prepare_extra_calls = 0;
    unsigned long capture_calls = 0;
    unsigned long publish_calls = 0;
    unsigned long material_calls = 0;
    unsigned long scene_draws = 0;
    unsigned long indexed_cells = 0;
    unsigned long scene_chunks = 0;
    unsigned long scene_rebuilt = 0;
    unsigned long landscape_chunks = 0;
    unsigned long landscape_rebuilt = 0;
    unsigned long scene_draws_vehicles = 0;
    unsigned long scene_forced_rebuilds = 0;
    unsigned long scene_changed_rebuilds = 0;
    unsigned long scene_index_rebuilds = 0;
    double scene_index_ms = 0;
    double scene_chunk_pass_ms = 0;
    std::map<std::string, std::vector<double>> stage_costs;
    std::map<std::string, std::vector<double>> gpu_costs;
    struct trace_entry {
        std::string stage;
        double milliseconds;
        Uint64 at;
        unsigned long action;
        unsigned long frame;
        unsigned long generation;
        tripoint_abs_ms position;
        int turn;
    };
    unsigned long action = 0;
    std::vector<trace_entry> trace;
    std::vector<std::string> native_states;
};
frame_sample_state frame_samples;
bool world_ready = false;

bool gpu_sampling_requested()
{
    static const bool enabled = []() {
        const char *value = std::getenv( "CDDA_FP3D_GPU_SELFTEST" );
        return value && std::strcmp( value, "1" ) == 0;
    }();
    return frame_sampling_requested() && enabled;
}
struct gpu_query_slot {
    const char *name = nullptr;
    bool finished = false;
};
struct gpu_query_state {
    SDL_GLContext context = nullptr;
    bool supported = false;
    std::array<GLuint, 512> queries = {};
    std::array<gpu_query_slot, 256> slots = {};
    unsigned dropped = 0;
    PFNGLGENQUERIESPROC gen = nullptr;
    PFNGLDELETEQUERIESPROC remove = nullptr;
    PFNGLQUERYCOUNTERPROC timestamp = nullptr;
    PFNGLGETQUERYIVPROC bits = nullptr;
    PFNGLGETQUERYOBJECTIVPROC available = nullptr;
    PFNGLGETQUERYOBJECTUI64VPROC result = nullptr;
} gpu_queries;

bool gpu_initialize()
{
    const SDL_GLContext current = SDL_GL_GetCurrentContext();
    if( !current ) {
        return false;
    }
    if( gpu_queries.context == current ) {
        return gpu_queries.supported;
    }
    // Query names belong to the old context; never delete them in a new one.
    gpu_queries = {};
    auto &q = gpu_queries;
    q.context = current;
    int major = 0;
    int minor = 0;
    const auto *version = reinterpret_cast<const char *>( glGetString( GL_VERSION ) );
    if( version ) {
        std::sscanf( version, "%d.%d", &major, &minor );
    }
    if( major > 3 || ( major == 3 && minor >= 3 ) ||
        SDL_GL_ExtensionSupported( "GL_ARB_timer_query" ) ) {
        q.gen = reinterpret_cast<PFNGLGENQUERIESPROC>( SDL_GL_GetProcAddress( "glGenQueries" ) );
        q.remove = reinterpret_cast<PFNGLDELETEQUERIESPROC>( SDL_GL_GetProcAddress( "glDeleteQueries" ) );
        q.timestamp = reinterpret_cast<PFNGLQUERYCOUNTERPROC>( SDL_GL_GetProcAddress( "glQueryCounter" ) );
        q.bits = reinterpret_cast<PFNGLGETQUERYIVPROC>( SDL_GL_GetProcAddress( "glGetQueryiv" ) );
        q.available = reinterpret_cast<PFNGLGETQUERYOBJECTIVPROC>( SDL_GL_GetProcAddress( "glGetQueryObjectiv" ) );
        q.result = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VPROC>( SDL_GL_GetProcAddress( "glGetQueryObjectui64v" ) );
        GLint bits = 0;
        if( q.gen && q.remove && q.timestamp && q.bits && q.available && q.result ) {
            q.bits( GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits );
            q.supported = bits > 0;
        }
        if( q.supported ) {
            q.gen( static_cast<GLsizei>( q.queries.size() ), q.queries.data() );
        }
    }
    DebugLog( D_INFO, D_MAIN ) << "fp3d: GPU_SELFTEST status="
                               << ( q.supported ? "available" : "unavailable" )
                               << ", renderer=" << reinterpret_cast<const char *>( glGetString( GL_RENDERER ) )
                               << ", version=" << ( version ? version : "unknown" );
    return q.supported;
}

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
turn_probe::turn_probe( const char *initial_stage ) : current( initial_stage )
{
    if( frame_sampling_requested() && world_ready && !frame_samples.done ) {
        started = SDL_GetPerformanceCounter();
    }
}
gpu_probe::gpu_probe( const char *name )
{
    if( !gpu_sampling_requested() || !frame_samples.started || frame_samples.done ||
        !gpu_initialize() ) {
        return;
    }
    auto &q = gpu_queries;
    for( size_t i = 0; i < q.slots.size(); ++i ) {
        if( !q.slots[i].name ) {
            slot = static_cast<int>( i );
            q.slots[i] = { name, false };
            q.timestamp( q.queries[2 * i], GL_TIMESTAMP );
            return;
        }
    }
    ++q.dropped;
}
gpu_probe::~gpu_probe()
{
    finish();
}
void gpu_probe::finish()
{
    if( slot >= 0 && gpu_queries.context == SDL_GL_GetCurrentContext() ) {
        gpu_queries.timestamp( gpu_queries.queries[2 * slot + 1], GL_TIMESTAMP );
        gpu_queries.slots[slot].finished = true;
    }
    slot = -1;
}
void gpu_collect()
{
    // Initialize during the existing warmup, before measured frames begin.
    if( !gpu_sampling_requested() || frame_samples.done || !gpu_initialize() ) {
        return;
    }
    auto &q = gpu_queries;
    for( size_t i = 0; i < q.slots.size(); ++i ) {
        auto &entry = q.slots[i];
        if( !entry.finished ) {
            continue;
        }
        GLint ready = GL_FALSE;
        q.available( q.queries[2 * i + 1], GL_QUERY_RESULT_AVAILABLE, &ready );
        if( ready ) {
            GLuint64 begin = 0;
            GLuint64 end = 0;
            q.result( q.queries[2 * i], GL_QUERY_RESULT, &begin );
            q.result( q.queries[2 * i + 1], GL_QUERY_RESULT, &end );
            if( end >= begin ) {
                frame_samples.gpu_costs[entry.name].push_back( ( end - begin ) / 1000000.0 );
            }
            entry = {};
        }
    }
}
void gpu_release()
{
    if( gpu_queries.supported && gpu_queries.context == SDL_GL_GetCurrentContext() ) {
        gpu_queries.remove( static_cast<GLsizei>( gpu_queries.queries.size() ),
                            gpu_queries.queries.data() );
    }
    gpu_queries = {};
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
    if( frame_samples.started && !frame_samples.done && started >= frame_samples.started ) {
        stage_sample( current, milliseconds );
    }
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
void action_started()
{
    if( frame_sampling_requested() && !frame_samples.done ) {
        ++frame_samples.action;
        stage_sample( "native_action_start", 0 );
    }
}
void native_turn_finished()
{
    if( !frame_sampling_requested() || !frame_samples.started || frame_samples.done ) {
        return;
    }
    stage_sample( "native_turn_complete", 0 );
    const avatar &you = get_avatar();
    // Copying/streaming the engine observes its state without consuming RNG.
    std::ostringstream state;
    const auto p = you.pos_abs();
    state << frame_samples.action << ',' << to_turn<int>( calendar::turn ) << ','
          << p.x() << ',' << p.y() << ',' << p.z() << ',' << you.get_moves() << ','
          << you.get_stamina() << ',' << you.get_hp() << ',' << rng_get_engine();
    std::vector<std::array<int, 5>> creatures;
    const auto add = [&]( const Creature &critter ) {
        const auto pos = critter.pos_abs();
        creatures.push_back( { pos.x(), pos.y(), pos.z(), critter.get_hp(), critter.get_moves() } );
    };
    for( const monster &mon : g->all_monsters() ) {
        add( mon );
    }
    for( const npc &who : g->all_npcs() ) {
        add( who );
    }
    std::sort( creatures.begin(), creatures.end() );
    uint64_t digest = 14695981039346656037ULL;
    for( const auto &critter : creatures ) {
        for( const int value : critter ) {
            digest = ( digest ^ static_cast<uint32_t>( value ) ) * 1099511628211ULL;
        }
    }
    state << ',' << creatures.size() << ',' << digest;
    frame_samples.native_states.push_back( state.str() );
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
        ++frame_samples.capture_calls;
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
void snapshot_full_capture( double full_ms, double tail_ms )
{
    if( frame_sampling_requested() && frame_samples.started && !frame_samples.done ) {
        frame_samples.full_captures.push_back( full_ms );
        frame_samples.capture_tails.push_back( tail_ms );
    }
}
void prepare_costs( double total_ms, double player_layer_ms, double extra_layers_ms,
                    double visibility_ms, int extra_layer_calls )
{
    if( frame_sampling_requested() && frame_samples.started && !frame_samples.done ) {
        frame_samples.prepare_totals.push_back( total_ms );
        frame_samples.prepare_player.push_back( player_layer_ms );
        frame_samples.prepare_extra.push_back( extra_layers_ms );
        frame_samples.prepare_visibility.push_back( visibility_ms );
        ++frame_samples.prepare_calls;
        frame_samples.prepare_extra_calls += static_cast<unsigned long>( extra_layer_calls );
    }
}
void publish_called()
{
    if( frame_sampling_requested() && frame_samples.started && !frame_samples.done ) {
        ++frame_samples.publish_calls;
    }
}
void material_prepare( double milliseconds )
{
    if( frame_sampling_requested() && frame_samples.started && !frame_samples.done ) {
        frame_samples.material_costs.push_back( milliseconds );
        ++frame_samples.material_calls;
    }
}
void scene_draw( double cpu_ms, int indexed_cells, int chunks, int rebuilt, int landscape_chunks,
                 int landscape_rebuilt )
{
    if( frame_sampling_requested() && frame_samples.started && !frame_samples.done ) {
        frame_samples.scene_cpu.push_back( cpu_ms );
        ++frame_samples.scene_draws;
        frame_samples.indexed_cells += static_cast<unsigned long>( indexed_cells );
        frame_samples.scene_chunks += static_cast<unsigned long>( chunks );
        frame_samples.scene_rebuilt += static_cast<unsigned long>( rebuilt );
        frame_samples.landscape_chunks += static_cast<unsigned long>( landscape_chunks );
        frame_samples.landscape_rebuilt += static_cast<unsigned long>( landscape_rebuilt );
    }
}
void scene_geometry( double index_ms, double chunk_pass_ms, int forced_rebuilds,
                     int changed_rebuilds, int visible_vehicles )
{
    if( frame_sampling_requested() && frame_samples.started && !frame_samples.done ) {
        auto &state = frame_samples;
        state.scene_index.push_back( index_ms );
        state.scene_chunk_pass.push_back( chunk_pass_ms );
        state.scene_index_ms += index_ms;
        state.scene_chunk_pass_ms += chunk_pass_ms;
        state.scene_forced_rebuilds += static_cast<unsigned long>( forced_rebuilds );
        state.scene_changed_rebuilds += static_cast<unsigned long>( changed_rebuilds );
        if( forced_rebuilds > 0 ) {
            ++state.scene_index_rebuilds;
        }
        if( visible_vehicles > 0 ) {
            ++state.scene_draws_vehicles;
            state.scene_index_vehicles.push_back( index_ms );
            state.scene_chunk_pass_vehicles.push_back( chunk_pass_ms );
        }
    }
}
void stage_sample( const char *name, double milliseconds )
{
    if( frame_sampling_requested() && frame_samples.started && !frame_samples.done ) {
        frame_samples.stage_costs[name].push_back( milliseconds );
        frame_samples.trace.push_back( { name, milliseconds, SDL_GetPerformanceCounter(),
                                        frame_samples.action, frame_samples.presents,
                                        frame_samples.publish_calls, get_avatar().pos_abs(),
                                        to_turn<int>( calendar::turn ) } );
    }
}
void presented( SDL_Renderer *renderer )
{
    if( !frame_sampling_requested() || frame_samples.done ) {
        return;
    }
    auto &state = frame_samples;
    if( !world_ready || !g || g->new_game ) {
        if( state.started ) {
            state.done = true;
            DebugLog( D_WARNING, D_MAIN ) << "fp3d: FRAME_SELFTEST aborted: left loaded game";
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
    // A fixed native action prefix makes route benchmarks independent of load
    // time and throughput. Ordinary interactive sampling retains the 2 s warmup.
    static const unsigned long start_action = []() -> unsigned long {
        const char *value = std::getenv( "CDDA_FP3D_FRAME_START_ACTION" );
        return value ? std::strtoul( value, nullptr, 10 ) : 0;
    }();
    if( start_action ? state.action < start_action :
        ( now - state.warmup ) * 1000.0 / frequency < 2000 ) {
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
                                   << ", action=" << state.action
                                   << ", queued_steps=" << get_avatar().get_auto_move_route().size();
        return;
    }
    const double interval = ( now - state.previous ) * 1000.0 / frequency;
    state.intervals.push_back( interval );
    ++state.presents;
    stage_sample( "present", interval );
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
    log_frame_distribution( "snapshot_full", state.full_captures );
    log_frame_distribution( "snapshot_tail", state.capture_tails );
    log_frame_distribution( "prepare_total", state.prepare_totals );
    log_frame_distribution( "prepare_player_layer", state.prepare_player );
    log_frame_distribution( "prepare_extra_layers", state.prepare_extra );
    log_frame_distribution( "prepare_visibility", state.prepare_visibility );
    log_frame_distribution( "materials_prepare", state.material_costs );
    log_frame_distribution( "scene_draw", state.scene_cpu );
    log_frame_distribution( "scene_index", state.scene_index );
    log_frame_distribution( "scene_chunk_pass", state.scene_chunk_pass );
    log_frame_distribution( "scene_index_vehicles", state.scene_index_vehicles );
    log_frame_distribution( "scene_chunk_pass_vehicles", state.scene_chunk_pass_vehicles );
    DebugLog( D_INFO, D_MAIN ) << "fp3d: FRAME_SELFTEST step_accounting, presents=" << state.presents
                               << ", prepare_calls=" << state.prepare_calls
                               << ", prepare_extra_layer_calls=" << state.prepare_extra_calls
                               << ", capture_calls=" << state.capture_calls
                               << ", publish_calls=" << state.publish_calls
                               << ", material_calls=" << state.material_calls
                               << ", scene_draws=" << state.scene_draws
                               << ", indexed_cells=" << state.indexed_cells
                               << ", scene_chunks=" << state.scene_chunks
                               << ", scene_rebuilt=" << state.scene_rebuilt
                               << ", landscape_chunks=" << state.landscape_chunks
                               << ", landscape_rebuilt=" << state.landscape_rebuilt
                               << ", draws_with_vehicles=" << state.scene_draws_vehicles
                               << ", index_rebuilds=" << state.scene_index_rebuilds
                               << ", forced_rebuilds=" << state.scene_forced_rebuilds
                               << ", changed_rebuilds=" << state.scene_changed_rebuilds
                               << ", index_ms=" << state.scene_index_ms
                               << ", chunk_pass_ms=" << state.scene_chunk_pass_ms;
    for( const auto &entry : state.stage_costs ) {
        log_frame_distribution( ( "stage_" + entry.first ).c_str(), entry.second );
    }
    for( const auto &entry : state.gpu_costs ) {
        log_frame_distribution( ( "gpu_" + entry.first ).c_str(), entry.second );
    }
    if( gpu_sampling_requested() ) {
        const auto pending = std::count_if( gpu_queries.slots.begin(), gpu_queries.slots.end(),
        []( const gpu_query_slot &entry ) { return entry.name != nullptr; } );
        DebugLog( D_INFO, D_MAIN ) << "fp3d: GPU_SELFTEST complete, supported=" << gpu_queries.supported
                                   << ", dropped=" << gpu_queries.dropped << ", pending=" << pending;
    }
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
        write_samples( "snapshot_full", state.full_captures );
        write_samples( "snapshot_tail", state.capture_tails );
        write_samples( "prepare_total", state.prepare_totals );
        write_samples( "prepare_player_layer", state.prepare_player );
        write_samples( "prepare_extra_layers", state.prepare_extra );
        write_samples( "prepare_visibility", state.prepare_visibility );
        write_samples( "materials_prepare", state.material_costs );
        write_samples( "scene_draw", state.scene_cpu );
        write_samples( "scene_index", state.scene_index );
        write_samples( "scene_chunk_pass", state.scene_chunk_pass );
        write_samples( "scene_index_vehicles", state.scene_index_vehicles );
        write_samples( "scene_chunk_pass_vehicles", state.scene_chunk_pass_vehicles );
        for( const auto &entry : state.stage_costs ) {
            write_samples( ( "stage_" + entry.first ).c_str(), entry.second );
        }
        for( const auto &entry : state.gpu_costs ) {
            write_samples( ( "gpu_" + entry.first ).c_str(), entry.second );
        }
        report.close();
        std::ofstream states( std::string( path ) + ".states.csv" );
        states << "action,turn,x,y,z,moves,stamina,hp,rng,creatures,creature_digest\n";
        for( const auto &entry : state.native_states ) {
            states << entry << '\n';
        }
        std::ofstream trace( std::string( path ) + ".trace.csv" );
        trace << std::setprecision( 15 ) << "stage,milliseconds,elapsed_ms,action,frame,publish,turn,x,y,z\n";
        for( const auto &entry : state.trace ) {
            trace << entry.stage << ',' << entry.milliseconds << ','
                  << ( entry.at - state.started ) * 1000.0 / frequency << ','
                  << entry.action << ',' << entry.frame << ',' << entry.generation << ','
                  << entry.turn << ',' << entry.position.x() << ',' << entry.position.y() << ','
                  << entry.position.z() << '\n';
        }
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
