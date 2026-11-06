#include "fp3d_scene.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdint>
#include <vector>
#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "fp3d_compositor.h"
#include "fp3d_materials.h"
#include "sdl_geometry.h"
#include "sdltiles.h"

TEST_CASE( "fp3d_weather_camera_rotates_an_unchanged_scene", "[.fp3d_camera_weather_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden camera regression", 0, 0, 128, 128,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    int driver = -1;
    for( int i = 0; i < SDL_GetNumRenderDrivers(); ++i ) {
        SDL_RendererInfo info{};
        SDL_GetRenderDriverInfo( i, &info );
        if( std::string( info.name ) == "opengl" ) { driver = i; }
    }
    REQUIRE( driver >= 0 );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), driver,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    GeometryRenderer_Ptr geometry = std::make_unique<DefaultGeometryRenderer>();
    tileset_cache cache;
    restore_on_out_of_scope old_tilecontext( tilecontext );
    tilecontext = std::make_shared<cata_tiles>( renderer, geometry, cache );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() {
        fp3d_compositor::release();
        tilecontext.reset();
    } );
    REQUIRE( TILESETS.count( "UNDEAD_PEOPLE_BASE" ) == 1 );
    auto assets = cache.load_tileset( "UNDEAD_PEOPLE_BASE", renderer, false, true, false, false );
    fp3d_materials::set_tileset( assets, renderer );
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms::zero;
    scene.sky.visible = true;
    scene.sky.daylight = 1;
    scene.sky.sun_y = 1;
    fp3d_snapshot::cell wall;
    wall.position = tripoint_abs_ms( 0, -2, 0 );
    wall.kind = fp3d_snapshot::terrain_kind::wall;
    wall.terrain = "t_wall";
    scene.cells.push_back( wall );
    scene.weather.clouds = .9f;
    scene.weather.outdoors = true;
    scene.weather.fog_end = 40;
    scene.weather.particles = fp3d_weather::precipitation::rain;
    scene.weather.intensity = 1;
    scene.weather.columns.push_back( tripoint_abs_ms( 0, -3, 0 ) );
    fp3d_materials::prepare( scene );
    const auto render = [&]( float yaw, float pitch ) {
        SDL_SetRenderDrawColor( renderer.get(), 0, 0, 0, 255 );
        SDL_RenderClear( renderer.get() );
        SDL_RenderFlush( renderer.get() );
        REQUIRE( fp3d_materials::bind() );
        glViewport( 0, 0, 128, 128 );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_CULL_FACE );
        glDisable( GL_BLEND );
        glEnable( GL_DEPTH_TEST );
        glDepthFunc( GL_LESS );
        glDepthMask( GL_TRUE );
        glClearDepth( 1 );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
        fp3d_scene::draw( scene, yaw, pitch, 1, {} );
        std::vector<std::uint32_t> pixels( 16 * 16 );
        glReadPixels( 56, 56, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
        fp3d_materials::unbind();
        return pixels;
    };
    const auto north = render( 0, 0 );
    const auto east = render( 90, 0 );
    const auto above = render( 0, -60 );
    CHECK( static_cast<bool>( north != east ) );
    CHECK( static_cast<bool>( north != above ) );
    CHECK( glGetError() == GL_NO_ERROR );

    // Exercise the actual cached compositor, not just the world draw function.
    fp3d_compositor::set_viewport( 0, 0, 128, 128, 128, 128 );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    fp3d_compositor::publish( scene );
    SDL_ShowWindow( window.get() );
    SDL_SetWindowInputFocus( window.get() );
    SDL_PumpEvents();
    REQUIRE( fp3d_compositor::captures_game_input() );
    const auto frame = [&]() {
        SDL_SetRenderDrawColor( renderer.get(), 0, 0, 0, 255 );
        SDL_RenderClear( renderer.get() );
        fp3d_compositor::compose();
        std::vector<std::uint32_t> pixels( 128 * 128 );
        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                      pixels.data(), 128 * 4 ) == 0 );
        SDL_RenderPresent( renderer.get() );
        return pixels;
    };
    const auto first = frame();
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.xrel = 225;
    REQUIRE( fp3d_compositor::process_event( motion ) );
    CHECK( fp3d_compositor::heading() == Approx( 90 ) );
    const auto turned = frame();
    CHECK( static_cast<bool>( first != turned ) );
    SDL_HideWindow( window.get() );
}
#endif
