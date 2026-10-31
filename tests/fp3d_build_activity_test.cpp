#include "fp3d_compositor.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "construction.h"
#include "fp3d_hud.h"
#include "fp3d_snapshot.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "options_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "rng.h"
#include "sdl_geometry.h"
#include "sdltiles.h"
#include "skill.h"

// Resume an existing native partial construction, whose components were already
// committed. This measures the real activity handler, not initial placement or
// its component-selection UI. The fixture supplies 100 AP and one second/tick;
// it deliberately does not claim to exercise the complete world turn loop.
TEST_CASE( "fp3d_resumed_native_construction_work_comparison", "[.fp3d_build_activity_compare]" )
{
    const char *mode = std::getenv( "CDDA_FP3D_COMBAT_MODE" );
    const char *report = std::getenv( "CDDA_FP3D_COMBAT_REPORT" );
    REQUIRE( mode );
    REQUIRE( report );
    const bool modern = std::string( mode ) == "modern";
    REQUIRE( ( modern || std::string( mode ) == "native" ) );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
    } );
    SDL_Window_Ptr window( SDL_CreateWindow( "FP3D hidden construction regression",
                           SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    int driver = -1;
    for( int i = 0; i < SDL_GetNumRenderDrivers(); ++i ) {
        SDL_RendererInfo info{};
        if( SDL_GetRenderDriverInfo( i, &info ) == 0 && std::string( info.name ) == "opengl" ) {
            driver = i;
        }
    }
    REQUIRE( driver >= 0 );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), driver,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    GeometryRenderer_Ptr geometry = std::make_unique<DefaultGeometryRenderer>();
    tileset_cache cache;
    restore_on_out_of_scope old_tilecontext( tilecontext );
    tilecontext = std::make_shared<cata_tiles>( renderer, geometry, cache );
    REQUIRE( TILESETS.count( "retrodays" ) == 1 );
    tilecontext->load_tileset( "retrodays", false, true, false, false );
    on_out_of_scope release_renderer( []() {
        fp3d_compositor::release();
        tilecontext.reset();
    } );
    override_option scaling( "CONSTRUCTION_SCALING", "100" );
    const auto &projects = get_constructions();
    const auto project = std::find_if( projects.begin(),
    projects.end(), []( const construction & entry ) {
        return entry.str_id == construction_str_id( "constr_wall_log" );
    } );
    REQUIRE( project != projects.end() );
    std::ofstream output( report );
    REQUIRE( output.good() );
    JsonOut json( output );
    json.start_array();
    for( bool lose_knowledge : {
             false, true
         } ) {
        clear_avatar();
        clear_map();
        clear_npcs();
        set_time_to_day();
        avatar &you = get_avatar();
        map &here = get_map();
        you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
        you.set_skill_level( skill_id( "fabrication" ), 3 );
        const auto location = you.pos_bub( here ) + tripoint_rel_ms::north;
        here.ter_set( location, ter_str_id( "t_wall_log_half" ) );
        partial_con partial;
        partial.id = project->id;
        for( int n = 0; n < 2; ++n ) {
            partial.components.emplace_back( itype_id( "log" ) );
        }
        for( int n = 0; n < 3; ++n ) {
            partial.components.emplace_back( itype_id( "stick" ) );
        }
        here.partial_con_set( location, partial );
        you.assign_activity( activity_id( "ACT_BUILD" ) );
        you.activity.placement = here.get_abs( location );
        if( modern ) {
            REQUIRE( fp3d_compositor::requested() );
            fp3d_compositor::attach( renderer.get(), window.get() );
            fp3d_snapshot::scene_snapshot scene;
            scene.origin = you.pos_abs();
            fp3d_compositor::publish( std::move( scene ) );
            REQUIRE( fp3d_compositor::turn_based() );
        } else {
            REQUIRE_FALSE( fp3d_compositor::turn_based() );
        }
        rng_set_engine_seed( 424242 );
        const auto clock = calendar::turn;
        int ticks = 0;
        int spent = 0;
        bool read_only = true;
        while( !you.activity.is_null() && ticks < 10000 ) {
            you.set_moves( 100 );
            if( modern ) {
                const auto engine = rng_get_engine();
                const auto time = calendar::turn;
                fp3d_hud::capture( you );
                read_only = read_only && rng_get_engine() == engine &&
                            calendar::turn == time && you.get_moves() == 100;
            }
            you.activity.do_turn( you );
            spent += 100 - you.get_moves();
            calendar::turn += 1_seconds;
            ++ticks;
            if( ticks == 12 ) {
                const partial_con *pc = here.partial_con_at( location );
                REQUIRE( pc );
                REQUIRE( pc->counter > 0 );
                const int counter = pc->counter;
                you.cancel_activity();
                CHECK( you.activity.is_null() );
                REQUIRE( here.partial_con_at( location ) );
                CHECK( here.partial_con_at( location )->counter == counter );
                CHECK( here.partial_con_at( location )->components.size() == 5 );
                if( lose_knowledge ) {
                    you.set_knowledge_level( skill_id( "fabrication" ), 0 );
                }
                // The same ACT_BUILD assignment used after native resume confirmation.
                you.assign_activity( activity_id( "ACT_BUILD" ) );
                you.activity.placement = here.get_abs( location );
            }
        }
        CHECK( read_only );
        CHECK( ticks < 10000 );
        CHECK( you.activity.is_null() );
        CHECK( you.pos_bub( here ) == tripoint_bub_ms( 65, 65, 0 ) );
        CHECK( spent > 0 );
        CHECK( here.i_at( location ).empty() );
        CHECK( here.i_at( you.pos_bub( here ) ).empty() );
        if( lose_knowledge ) {
            REQUIRE( here.partial_con_at( location ) );
            CHECK( here.partial_con_at( location )->components.size() == 5 );
            CHECK( here.ter( location ) == ter_str_id( "t_wall_log_half" ) );
            CHECK( ticks == 13 );
        } else {
            CHECK_FALSE( here.partial_con_at( location ) );
            CHECK( here.ter( location ) == ter_str_id( "t_wall_log" ) );
            CHECK( ticks > 12 );
        }
        json.start_object();
        json.member( "scenario", lose_knowledge ? "resume-without-knowledge" : "complete-after-resume" );
        json.member( "ticks", ticks );
        json.member( "elapsed_seconds", to_seconds<int>( calendar::turn - clock ) );
        json.member( "spent_moves", spent );
        json.member( "player_moves", you.get_moves() );
        json.member( "terrain", here.ter( location ).id().str() );
        const partial_con *pc = here.partial_con_at( location );
        json.member( "counter", pc ? pc->counter : -1 );
        json.member( "components", pc ? pc->components.size() : 0 );
        json.member( "fabrication", you.get_skill_level_object( skill_id( "fabrication" ) ) );
        json.member( "stamina", you.get_stamina() );
        json.member( "hp", you.get_hp() );
        std::ostringstream engine;
        engine << rng_get_engine();
        json.member( "rng", engine.str() );
        json.end_object();
    }
    json.end_array();
    output.flush();
    CHECK( output.good() );
}
#endif
