#include "fp3d_compositor.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "avatar.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "fp3d_interaction.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "do_turn.h"
#include "fp3d_hud.h"
#include "fp3d_snapshot.h"
#include "game.h"
#include "gamemode.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "sdl_geometry.h"
#include "sdltiles.h"
#include "weather.h"
#include "weather_type.h"

// Bounded full world turns. Navigation uses a native destination. Optional
// combat drains AP with native attack primitives before each world turn, so
// neither scenario needs desktop input. Combat covers AI, not the input loop
// or private autosave bookkeeping.
TEST_CASE( "fp3d_full_world_turn_navigation_comparison", "[.fp3d_world_turn_compare]" )
{
    const char *mode = std::getenv( "CDDA_FP3D_COMBAT_MODE" );
    const char *report = std::getenv( "CDDA_FP3D_COMBAT_REPORT" );
    REQUIRE( mode );
    REQUIRE( report );
    const bool modern = std::string( mode ) == "modern";
    const char *combat_flag = std::getenv( "CDDA_FP3D_WORLD_COMBAT" );
    const bool combat = combat_flag && std::string( combat_flag ) == "1";
    REQUIRE( ( modern || std::string( mode ) == "native" ) );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
    } );
    SDL_Window_Ptr window( SDL_CreateWindow( "FP3D hidden world turn regression",
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
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    std::vector<tripoint_bub_ms> route;
    for( int step = 1; step <= 10; ++step ) {
        const auto p = you.pos_bub( here ) + tripoint_rel_ms( 0, -step, 0 );
        here.ter_set( p, ter_str_id( "t_pavement" ) );
        route.push_back( p );
    }
    here.ter_set( you.pos_bub( here ), ter_str_id( "t_pavement" ) );
    if( !combat ) {
        you.set_destination( route );
    }
    you.set_moves( 100 );
    monster &enemy = spawn_test_monster( "mon_zombie", you.pos_bub( here ) +
                                         ( combat ? tripoint_rel_ms::north : tripoint_rel_ms( 8, 0, 0 ) ) );
    if( combat ) {
        // Fixture health keeps a short comparison from ending at an input prompt.
        enemy.set_hp( 500 );
    }
    const int enemy_initial_hp = enemy.get_hp();
    const auto enemy_start = enemy.pos_abs();
    const auto player_start = you.pos_abs();
    const int player_initial_hp = you.get_hp();
    g->set_safe_mode( SAFE_MODE_OFF );
    override_option real_time( "TURN_DURATION", "0" );
    override_option autosave( "AUTOSAVE", "false" );
    override_option npc_spawns( "NPC_SPAWNTIME", "0" );
    scoped_weather_override weather_clear( WEATHER_CLEAR );
    restore_on_out_of_scope old_new_game( g->new_game );
    restore_on_out_of_scope old_quit( g->uquit );
    REQUIRE( g->gametype() == special_game_type::NONE );
    g->new_game = false;
    g->uquit = QUIT_NO;
    on_out_of_scope restore_game( [&]() {
        you.abort_automove();
        g->set_safe_mode( SAFE_MODE_ON );
    } );
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
    std::ofstream output( report );
    REQUIRE( output.good() );
    JsonOut json( output );
    json.start_array();
    for( int turn = 0; turn < 3; ++turn ) {
        CAPTURE( mode, turn );
        if( !combat ) {
            REQUIRE( you.has_destination() );
        }
        if( modern ) {
            const auto engine = rng_get_engine();
            const int moves = you.get_moves();
            fp3d_hud::capture( you );
            CHECK( rng_get_engine() == engine );
            CHECK( you.get_moves() == moves );
        }
        if( combat ) {
            // Native action primitives exhaust the current action budget. The
            // complete native world turn then runs AI, attacks and AP recovery.
            // No test-side AP refill, clock increment or manual monster attack.
            int attacks = 0;
            while( you.get_moves() > 0 ) {
                REQUIRE( ++attacks <= 8 );
                REQUIRE_FALSE( enemy.is_dead() );
                REQUIRE( enemy.pos_abs() == enemy_start );
                tripoint_rel_ms delta = tripoint_rel_ms::north;
                if( modern ) {
                    const auto engine = rng_get_engine();
                    const int moves = you.get_moves();
                    fp3d_snapshot::prepare_view( you, here );
                    const auto target = fp3d_targeting::ray( fp3d_snapshot::capture( you, here ), 0, 0 );
                    REQUIRE( target );
                    REQUIRE( target->position == enemy.pos_abs() );
                    const auto request = fp3d_interaction::resolve( "FP3D_ATTACK", target, you, here );
                    REQUIRE( request );
                    REQUIRE( request->position == enemy.pos_bub( here ) );
                    delta = tripoint_rel_ms( get_delta_from_movement_action( request->action, iso_rotate::yes ), 0 );
                    CHECK( rng_get_engine() == engine );
                    CHECK( you.get_moves() == moves );
                }
                const int before = you.get_moves();
                avatar_action::move( you, here, delta );
                REQUIRE( you.get_moves() < before );
                you.movecounter = before - you.get_moves();
                you.action_taken();
            }
            REQUIRE( you.get_moves() <= 0 );
        }
        REQUIRE_FALSE( do_turn() );
        CHECK( calendar::turn == clock + ( turn + 1 ) * 1_seconds );
        if( !combat ) {
            REQUIRE( you.has_destination() );
        }
        json.start_object();
        json.member( "turn", turn );
        json.member( "elapsed_seconds", to_seconds<int>( calendar::turn - clock ) );
        json.member( "x", you.pos_abs().x() );
        json.member( "y", you.pos_abs().y() );
        json.member( "player_moves", you.get_moves() );
        json.member( "command_moves", you.movecounter );
        json.member( "hp", you.get_hp() );
        json.member( "stamina", you.get_stamina() );
        json.member( "hunger", you.get_hunger() );
        json.member( "thirst", you.get_thirst() );
        json.member( "remaining_steps", you.get_auto_move_route().size() );
        json.member( "enemy_x", enemy.pos_abs().x() );
        json.member( "enemy_y", enemy.pos_abs().y() );
        json.member( "enemy_hp", enemy.get_hp() );
        if( combat ) {
            json.member( "scenario", "native_melee_then_full_world_turn" );
            json.member( "enemy_initial_hp", enemy_initial_hp );
            json.member( "enemy_moves", enemy.get_moves() );
            json.member( "pain", you.get_pain() );
            json.member( "head_hp", you.get_part_hp_cur( bodypart_str_id( "head" ).id() ) );
            json.member( "torso_hp", you.get_part_hp_cur( bodypart_str_id( "torso" ).id() ) );
        }
        std::ostringstream engine;
        engine << rng_get_engine();
        json.member( "rng", engine.str() );
        json.end_object();
    }
    if( combat ) {
        CHECK( you.pos_abs() == player_start );
        CHECK( enemy.get_hp() < enemy_initial_hp );
        CHECK( you.get_hp() < player_initial_hp );
    } else {
        CHECK( you.pos_abs() != player_start );
        CHECK( enemy.pos_abs() != enemy_start );
    }
    json.end_array();
    output.flush();
    REQUIRE( output.good() );
}
#endif
