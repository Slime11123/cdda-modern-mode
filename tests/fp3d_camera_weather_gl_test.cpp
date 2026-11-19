#include "fp3d_scene.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdint>
#include <vector>
#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "calendar.h"
#include "fp3d_compositor.h"
#include "fp3d_materials.h"
#include "fp3d_shadow.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "sdl_geometry.h"
#include "sdltiles.h"
#include "type_id.h"

TEST_CASE( "fp3d_overhead_hidden_roof_preserves_sun_and_lamp_shadow_depth",
           "[.fp3d_overhead_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden roof shadow regression", 0, 0, 128, 128,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    on_out_of_scope cleanup( []() { fp3d_shadow::release(); } );
    fp3d_snapshot::scene_snapshot scene;
    scene.sky.daylight = 1;
    scene.sky.sun_x = .6f;
    scene.sky.sun_y = .8f;
    scene.sky.sun_z = 0;
    auto &room = scene.cells.emplace_back();
    room.terrain = "t_floor";
    const auto depth = [&]( bool lamp ) {
        ++scene.render_generation;
        GLuint texture;
        int size;
        if( lamp ) {
            texture = fp3d_shadow::render_point( scene, { 0, 1, 0, 10 } );
            size = 512;
        } else {
            const auto sun = fp3d_shadow::render( scene, 1 );
            texture = sun.texture;
            size = sun.resolution;
        }
        REQUIRE( texture != 0 );
        std::vector<float> values( size * size );
        glBindTexture( lamp ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, texture );
        glGetTexImage( lamp ? GL_TEXTURE_CUBE_MAP_POSITIVE_Y : GL_TEXTURE_2D, 0,
                       GL_DEPTH_COMPONENT, GL_FLOAT, values.data() );
        REQUIRE( glGetError() == GL_NO_ERROR );
        return values;
    };
    for( bool lamp : { false, true } ) {
        CAPTURE( lamp );
        room.ceiling_present = false;
        room.cutaway_ceiling = false;
        const auto open = depth( lamp );
        room.ceiling_present = true;
        const auto roof = depth( lamp );
        CHECK( roof != open );
        room.ceiling_present = false;
        room.cutaway_ceiling = true;
        CHECK( depth( lamp ) == roof );
        room.cutaway_ceiling = false;
        CHECK( depth( lamp ) == open );
    }
}

TEST_CASE( "fp3d_overhead_tower_structure_blends_without_hiding_the_lower_floor",
           "[.fp3d_overhead_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden tower structure regression", 0, 0, 128, 128,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );

    fp3d_snapshot::scene_snapshot scene;
    scene.sky.daylight = 1;
    for( int y = -6; y <= 6; ++y ) {
        for( int x = -6; x <= 6; ++x ) {
            auto &ground = scene.cells.emplace_back();
            ground.position = scene.origin + tripoint_rel_ms( x, y, 0 );
            ground.terrain = "t_floor";
        }
    }
    const auto render = [&]( const fp3d_camera::pose &camera ) {
        ++scene.render_generation;
        SDL_RenderFlush( renderer.get() );
        glViewport( 0, 0, 128, 128 );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_BLEND );
        glDisable( GL_CULL_FACE );
        glEnable( GL_DEPTH_TEST );
        glDepthFunc( GL_LESS );
        glDepthMask( GL_TRUE );
        glDepthRange( 0, 1 );
        glClearDepth( 1 );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        fp3d_scene::draw( scene, camera.yaw, camera.pitch, 1, camera.eye,
                          std::nullopt, false, 0, std::nullopt, true );
        std::vector<std::array<unsigned char, 4>> colour( 128 * 128 );
        std::vector<float> depth( 128 * 128 );
        glReadPixels( 0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, colour.data() );
        glReadPixels( 0, 0, 128, 128, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data() );
        CHECK( glGetError() == GL_NO_ERROR );
        return std::make_pair( std::move( colour ), std::move( depth ) );
    };

    for( float yaw : { 0.f, 95.f, 180.f } ) {
        const auto camera = fp3d_camera::orbit_pose( { yaw, 66, 8 }, 0, 0 );
        const auto [lower_colour, lower_depth] = render( camera );
        for( const std::string feature : { "t_metal_floor_no_roof", "t_radio_tower", "t_railing",
                                          "t_ladder_up_down", "f_table" } ) {
            CAPTURE( yaw, feature );
            auto &upper = scene.cells.emplace_back();
            upper.position = scene.origin + tripoint_rel_ms::above;
            if( feature == "f_table" ) {
                upper.terrain = "t_metal_floor_no_roof";
                upper.floor_present = false;
                upper.furniture = fp3d_furniture::describe( feature );
                REQUIRE( upper.furniture );
            } else {
                upper.terrain = feature;
                upper.kind = fp3d_snapshot::terrain_shape( feature );
                upper.goes_up = upper.goes_down = feature == "t_ladder_up_down";
                if( upper.goes_up ) {
                    // Compare the ladder itself, independent of its animated arrow.
                    upper.known = fp3d_snapshot::knowledge::remembered;
                }
            }
            const auto [opaque_colour, opaque_depth] = render( camera );
            upper.faded_tower = true;
            const auto [faded_colour, faded_depth] = render( camera );
            CHECK( faded_depth == lower_depth );
            int covered = 0;
            int retained_silhouette = 0;
            for( size_t i = 0; i < lower_depth.size(); ++i ) {
                if( lower_depth[i] < 1 && opaque_depth[i] < lower_depth[i] ) {
                    ++covered;
                    retained_silhouette += !std::equal( faded_colour[i].begin(), faded_colour[i].begin() + 3,
                                                       lower_colour[i].begin() ) &&
                                           !std::equal( faded_colour[i].begin(), faded_colour[i].begin() + 3,
                                                       opaque_colour[i].begin() );
                }
            }
            REQUIRE( covered > 5 );
            CHECK( retained_silhouette > covered / 2 );
            scene.cells.pop_back();
        }
    }
}

TEST_CASE( "fp3d_water_mask_excludes_navigation_cutouts_without_changing_depth",
           "[.fp3d_water_overlay_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden water overlay regression", 0, 0, 256, 256,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
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
    auto assets = cache.load_tileset( "UNDEAD_PEOPLE_BASE", renderer, false, true, false, false );
    fp3d_materials::set_tileset( assets, renderer );
    fp3d_snapshot::scene_snapshot scene;
    scene.sky.daylight = 1;
    for( int y = -10; y <= 10; ++y ) {
        for( int x = -10; x <= 10; ++x ) {
            auto &tile = scene.cells.emplace_back();
            tile.position = tripoint_abs_ms( x, y, 0 );
            tile.terrain = "t_water_dp";
        }
    }
    fp3d_snapshot::sprite actor;
    actor.kind = fp3d_snapshot::sprite_kind::character;
    actor.id = "player_male";
    actor.is_avatar = true;
    scene.sprites.push_back( actor );
    fp3d_snapshot::sprite held;
    held.kind = fp3d_snapshot::sprite_kind::held_item;
    held.id = "knife_combat";
    scene.held_item = held;
    fp3d_materials::prepare( scene );
    SDL_RenderFlush( renderer.get() );
    REQUIRE( fp3d_materials::bind() );
    REQUIRE( fp3d_materials::lookup( fp3d_materials::sprite_key( actor ) ) );
    REQUIRE( fp3d_materials::lookup( fp3d_materials::sprite_key( held ) ) );
    const auto read_color = []() {
        std::vector<unsigned char> pixels( 256 * 256 * 4 );
        glReadPixels( 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
        return pixels;
    };
    const auto read_depth = []() {
        std::vector<float> pixels( 256 * 256 );
        glReadPixels( 0, 0, 256, 256, GL_DEPTH_COMPONENT, GL_FLOAT, pixels.data() );
        return pixels;
    };
    for( bool overhead : { false, true } ) {
        for( float angle : { 0.f, 47.f } ) {
            for( float swing : { 0.f, 1.f } ) {
                CAPTURE( overhead, angle, swing );
                glViewport( 0, 0, 256, 256 );
                glDisable( GL_SCISSOR_TEST );
                glDisable( GL_BLEND );
                glEnable( GL_DEPTH_TEST );
                glDepthFunc( GL_LEQUAL );
                glDepthMask( GL_TRUE );
                glClearColor( 0, 0, 0, 0 );
                glClearDepth( 1 );
                glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
                REQUIRE( fp3d_materials::bind() );
                const auto camera = fp3d_camera::orbit_pose(
                                        { angle, overhead ? 65.f : 25.f, 5 }, 0, 0 );
                fp3d_scene::draw( scene, camera.yaw, camera.pitch, 1, camera.eye,
                                  std::nullopt, false, swing, std::nullopt, overhead );
                fp3d_materials::unbind();
                const auto depth = read_depth();
                // Supply a water mask under every overlay pixel, isolating
                // cutout coverage from terrain visibility and depth tests.
                glClearColor( .84f, .13f, .08f, .95f );
                glClear( GL_COLOR_BUFFER_BIT );
                const auto before = read_color();
                fp3d_scene::exclude_navigation_overlays_from_reflectivity();
                const auto after = read_color();
                CHECK( read_depth() == depth );
                unsigned cleared = 0, water = 0;
                int left = 256, right = -1, bottom = 256, top = -1;
                for( int y = 0; y < 256; ++y ) {
                    for( int x = 0; x < 256; ++x ) {
                        const int i = ( y * 256 + x ) * 4;
                        if( before[i] > 0 && after[i] == 0 ) {
                            ++cleared;
                            left = std::min( left, x ); right = std::max( right, x );
                            bottom = std::min( bottom, y ); top = std::max( top, y );
                        }
                        if( after[i] > 0 ) { ++water; }
                    }
                }
                CHECK( cleared > 20 );
                CHECK( water > 1000 );
                // Transparent space inside the cutout bounds keeps its water.
                unsigned transparent_water = 0;
                for( int y = bottom; y <= top; ++y ) {
                    for( int x = left; x <= right; ++x ) {
                        transparent_water += after[( y * 256 + x ) * 4] > 0;
                    }
                }
                CHECK( transparent_water > 10 );
                CHECK( glGetError() == GL_NO_ERROR );
            }
        }
    }
}

TEST_CASE( "fp3d_creatures_cast_contact_shadows_without_changing_depth",
           "[.fp3d_creature_shadow_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden creature contact shadows", 0, 0, 256, 256,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    fp3d_snapshot::scene_snapshot scene;
    scene.sky.daylight = 1;
    scene.sky.sun_y = 1;
    for( int y = -4; y <= 4; ++y ) {
        for( int x = -4; x <= 4; ++x ) {
            auto &tile = scene.cells.emplace_back();
            tile.position = tripoint_abs_ms( x, y, 0 );
            tile.terrain = "t_pavement";
        }
    }
    scene.sprites.emplace_back();
    // No atlas: both actor and item draw the identical fallback body. Any
    // colour difference therefore belongs to the contact shadow alone.
    std::vector<unsigned char> pixels( 256 * 256 * 4 );
    std::vector<float> depth( 256 * 256 );
    const auto render = [&]( bool overhead ) {
        SDL_RenderFlush( renderer.get() );
        glViewport( 0, 0, 256, 256 );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_BLEND );
        glDepthMask( GL_TRUE );
        glClearColor( .2f, .5f, .8f, 1 );
        glClearDepth( 1 );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        const auto camera = fp3d_camera::orbit_pose( { 0, overhead ? 65.f : 20.f, 5 }, 0, 0 );
        fp3d_scene::draw( scene, camera.yaw, camera.pitch, 1, camera.eye, std::nullopt,
                          false, 0, std::nullopt, overhead );
        glReadPixels( 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
        glReadPixels( 0, 0, 256, 256, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data() );
        REQUIRE( glGetError() == GL_NO_ERROR );
        return pixels;
    };
    for( bool overhead : { false, true } ) {
        CAPTURE( overhead );
        scene.sprites.front().kind = fp3d_snapshot::sprite_kind::item;
        const auto no_shadow = render( overhead );
        const auto item_depth = depth;
        scene.sprites.front().kind = fp3d_snapshot::sprite_kind::monster;
        const auto shadow = render( overhead );
        CHECK( shadow != no_shadow );
        CHECK( depth == item_depth );
        unsigned darker = 0;
        bool brightened = false;
        for( size_t i = 0; i < pixels.size(); i += 4 ) {
            brightened = brightened || shadow[i] > no_shadow[i];
            if( shadow[i] < no_shadow[i] ) { ++darker; }
        }
        CHECK_FALSE( brightened );
        CHECK( darker > 0 );
        scene.sprites.front().kind = fp3d_snapshot::sprite_kind::character;
        CHECK( render( overhead ) == shadow );
    }
}

TEST_CASE( "fp3d_creature_shadow_maps_follow_dynamic_silhouettes",
           "[.fp3d_creature_shadow_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden creature shadow maps", 0, 0, 640, 480,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
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
    auto assets = cache.load_tileset( "UNDEAD_PEOPLE_BASE", renderer, false, true, false, false );
    fp3d_materials::set_tileset( assets, renderer );
    fp3d_snapshot::scene_snapshot scene;
    scene.sky.daylight = 1;
    scene.sky.sun_x = .6f;
    scene.sky.sun_y = .8f;
    scene.sky.sun_z = 0;
    fp3d_snapshot::sprite actor;
    actor.id = "mon_zombie";
    actor.position = tripoint_abs_ms( 0, -3, 0 );
    scene.sprites.push_back( actor );
    fp3d_materials::prepare( scene );
    SDL_RenderFlush( renderer.get() );
    REQUIRE( fp3d_materials::bind() );
    REQUIRE( fp3d_materials::lookup( fp3d_materials::sprite_key( actor ) ) );
    scene.sprites.clear();
    const auto read_depth = []( GLuint texture, int size ) {
        std::vector<float> values( size * size );
        glBindTexture( GL_TEXTURE_2D, texture );
        glGetTexImage( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, values.data() );
        return values;
    };
    const auto sun_depth = [&]() {
        ++scene.render_generation;
        const auto map = fp3d_shadow::render( scene, 1 );
        REQUIRE( map.texture != 0 );
        return read_depth( map.texture, map.resolution );
    };
    const auto empty = sun_depth();
    scene.sprites.push_back( actor );
    const auto creature = sun_depth();
    CHECK( creature != empty );
    const auto occupied = std::count_if( creature.begin(), creature.end(),
    []( float value ) { return value < .99999f; } );
    CHECK( occupied > 20 );
    // At 1024 / 46 texels per metre, a solid 1.8m square would
    // occupy ~1280 texels at this sun angle. Alpha must cut it out.
    CHECK( occupied < 1100 );
    scene.sprites.front().position += tripoint_rel_ms::east;
    CHECK( sun_depth() != creature );
    scene.sprites.front().kind = fp3d_snapshot::sprite_kind::item;
    CHECK( sun_depth() == empty );
    scene.sprites.clear();
    CHECK( sun_depth() == empty );
    const auto beam_depth = [&]() {
        ++scene.render_generation;
        const auto map = fp3d_shadow::render_beam( scene, { 0, 1.6f, 0, 10 }, 0, 0 );
        REQUIRE( map != 0 );
        return read_depth( map, 1024 );
    };
    const auto empty_beam = beam_depth();
    scene.sprites.push_back( actor );
    CHECK( beam_depth() != empty_beam );
    scene.sprites.front().is_avatar = true;
    CHECK( beam_depth() == empty_beam );
    const auto point_depth = [&]() {
        ++scene.render_generation;
        const auto map = fp3d_shadow::render_point( scene, { 0, 2, 0, 10 } );
        REQUIRE( map != 0 );
        std::vector<float> values( 512 * 512 );
        glBindTexture( GL_TEXTURE_CUBE_MAP, map );
        glGetTexImage( GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_DEPTH_COMPONENT,
                       GL_FLOAT, values.data() );
        return values;
    };
    // A streetlamp still sees the avatar; only their held beam excludes it.
    const auto avatar_point = point_depth();
    scene.sprites.clear();
    CHECK( point_depth() != avatar_point );
    scene.sprites.push_back( actor );
    CHECK( point_depth() == avatar_point );
    CHECK( glGetError() == GL_NO_ERROR );
    if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
        std::filesystem::create_directories( directory );
        scene.sprites.front().is_avatar = false;
        for( int y = -7; y <= 2; ++y ) {
            for( int x = -4; x <= 4; ++x ) {
                auto &tile = scene.cells.emplace_back();
                tile.position = tripoint_abs_ms( x, y, 0 );
                tile.terrain = "t_pavement";
            }
        }
        fp3d_materials::prepare( scene );
        SDL_RenderFlush( renderer.get() );
        REQUIRE( fp3d_materials::bind() );
        for( bool overhead : { false, true } ) {
            glViewport( 0, 0, 640, 480 );
            glDepthMask( GL_TRUE );
            glClearColor( .2f, .3f, .4f, 1 );
            glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
            const auto camera = overhead ? fp3d_camera::orbit_pose( { 0, 65, 6 }, 0, -3 ) :
                                fp3d_camera::pose{ 0, 20, { 0, 0, 1.6f } };
            fp3d_scene::draw( scene, camera.yaw, camera.pitch, 640.f / 480, camera.eye,
                              std::nullopt, false, 0, std::nullopt, overhead );
            std::vector<unsigned char> pixels( 640 * 480 * 4 );
            glReadPixels( 0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
            for( int row = 0; row < 240; ++row ) {
                std::swap_ranges( pixels.begin() + row * 640 * 4,
                                  pixels.begin() + ( row + 1 ) * 640 * 4,
                                  pixels.begin() + ( 479 - row ) * 640 * 4 );
            }
            SDL_Surface_Ptr surface( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(),
                                     640, 480, 32, 640 * 4, SDL_PIXELFORMAT_RGBA32 ) );
            REQUIRE( surface );
            CHECK( IMG_SavePNG( surface.get(), ( std::filesystem::path( directory ) /
                                               ( overhead ? "contact-overhead.png" : "contact-first-person.png" ) ).u8string().c_str() ) == 0 );
        }
    }
}

TEST_CASE( "fp3d_overhead_sprites_keep_square_silhouettes_when_rotating",
           "[.fp3d_billboard_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden billboard regression", 0, 0, 256, 256,
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
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    // No atlas is bound: draw_sprite uses its solid magenta fallback, letting
    // this test measure the actual quad without tileset transparency margins.
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms::zero;
    scene.sky.daylight = 1;
    scene.sky.sun_y = 1;
    fp3d_snapshot::sprite image;
    image.position = scene.origin;
    scene.sprites.push_back( image );
    // Compare ordinary actors with the avatar under an actual opaque floor
    // slab. Only the avatar may remain visible through foreground models.
    for( int scenario : { 0, 1, 2, 3 } ) {
        CAPTURE( scenario );
        scene.sprites.front().is_avatar = scenario == 1 || scenario == 2;
        scene.cells.clear();
        if( scenario >= 2 ) {
            for( int y = -3; y <= 3; ++y ) {
                for( int x = -3; x <= 3; ++x ) {
                    fp3d_snapshot::cell slab;
                    slab.position = tripoint_abs_ms( x, y, 1 );
                    slab.kind = fp3d_snapshot::terrain_kind::floor;
                    slab.terrain = "t_pavement";
                    slab.floor_present = true;
                    scene.cells.push_back( slab );
                }
            }
        }
        for( float yaw : { 0.f, 90.f, 225.f } ) {
            for( float pitch : { 5.f, 60.f, 89.f } ) {
                if( scenario >= 2 && pitch == 5 ) {
                    continue; // The near-horizontal ray misses the finite slab.
                }
                CAPTURE( yaw, pitch );
                SDL_RenderFlush( renderer.get() );
                glViewport( 0, 0, 256, 256 );
                glDisable( GL_SCISSOR_TEST );
                glDisable( GL_BLEND );
                glDisable( GL_CULL_FACE );
                glEnable( GL_DEPTH_TEST );
                glDepthFunc( GL_LESS );
                glDepthMask( GL_TRUE );
                glClearColor( 0, 0, 0, 1 );
                glClearDepth( 1 );
                glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
                const auto camera = fp3d_camera::orbit_pose( { yaw, pitch, 6 }, 0, 0 );
                fp3d_scene::draw( scene, yaw, pitch, 1, camera.eye, std::nullopt,
                                  false, 0, std::nullopt, true );
                std::vector<unsigned char> pixels( 256 * 256 * 4 );
                glReadPixels( 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
                int left = 256, right = -1, bottom = 256, top = -1;
                for( int y = 0; y < 256; ++y ) {
                    for( int x = 0; x < 256; ++x ) {
                        const auto *pixel = &pixels[( y * 256 + x ) * 4];
                        if( pixel[0] > 40 && pixel[0] > pixel[1] * 2 && pixel[2] > pixel[1] * 2 ) {
                            left = std::min( left, x );
                            right = std::max( right, x );
                            bottom = std::min( bottom, y );
                            top = std::max( top, y );
                        }
                    }
                }
                if( scenario == 3 ) {
                    CHECK( right == -1 );
                    CHECK( glGetError() == GL_NO_ERROR );
                    continue;
                }
                REQUIRE( right - left > 10 );
                const float ratio = static_cast<float>( top - bottom + 1 ) / ( right - left + 1 );
                CHECK( ratio > .85f );
                CHECK( ratio < 1.15f );
                CHECK( glGetError() == GL_NO_ERROR );
            }
        }
    }
}

TEST_CASE( "fp3d_vehicle_ghost_ceiling_keeps_the_road", "[.fp3d_missing_surfaces_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden road surface regression", 0, 0, 256, 256,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    for( int coordinate : { 0, 7, -9 } ) {
        fp3d_snapshot::scene_snapshot scene;
        scene.origin = tripoint_abs_ms( coordinate, coordinate, 0 );
        scene.sky.daylight = 1;
        for( int y = -3; y <= 3; ++y ) {
            for( int x = -3; x <= 3; ++x ) {
                fp3d_snapshot::cell road;
                road.position = scene.origin + tripoint_rel_ms( x, y, 0 );
                road.terrain = "t_pavement";
                scene.cells.push_back( road );
            }
        }
        fp3d_vehicle::vehicle_view car;
        car.origin = scene.origin;
        // A short native frame with an intact roof admits the road-ceiling
        // suppression path. Geometry, vehicle and camera stay identical.
        for( int x = 0; x < 4; ++x ) {
            fp3d_vehicle::part_view frame;
            frame.index = x;
            frame.mount_x = x;
            frame.kind = fp3d_vehicle::role::frame;
            car.parts.push_back( frame );
        }
        fp3d_vehicle::part_view roof;
        roof.index = 4;
        roof.kind = fp3d_vehicle::role::roof;
        car.parts.push_back( roof );
        scene.vehicles.push_back( car );
        const auto render = [&]( const fp3d_camera::pose &camera ) {
            SDL_RenderFlush( renderer.get() );
            glViewport( 0, 0, 256, 256 );
            glDisable( GL_SCISSOR_TEST );
            glDisable( GL_BLEND );
            glDisable( GL_CULL_FACE );
            glEnable( GL_DEPTH_TEST );
            glDepthFunc( GL_LESS );
            glDepthMask( GL_TRUE );
            glClearColor( .2f, .5f, .8f, 1 );
            glClearDepth( 1 );
            glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
            fp3d_scene::draw( scene, camera.yaw, camera.pitch, 1, camera.eye,
                              std::nullopt, false, 0, std::nullopt, true );
            std::vector<float> depth( 256 * 256 );
            glReadPixels( 0, 0, 256, 256, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data() );
            CHECK( glGetError() == GL_NO_ERROR );
            return depth;
        };
        for( float yaw : { 0.f, 90.f, 180.f, 270.f } ) {
            for( float distance : { 6.f, 18.f, 48.f } ) {
                CAPTURE( coordinate, yaw, distance );
                const auto camera = fp3d_camera::orbit_pose( { yaw, 60, distance }, 0, 0 );
                scene.cells[24].ceiling_present = false;
                scene.render_generation++;
                const auto road = render( camera );
                scene.cells[24].ceiling_present = true;
                scene.render_generation++;
                CHECK( static_cast<bool>( render( camera ) == road ) );
                CHECK( fp3d_scene::vehicle_roof_sheet_suppressed( scene.origin,
                        scene.render_generation ) );
                render( camera );
                CHECK( fp3d_scene::statistics().rebuilt == 0 );
            }
        }
        // The underside also remains solid: a camera below the native floor
        // cannot see through the road into the vehicle above it.
        fp3d_camera::pose below;
        below.pitch = -30;
        below.eye = { 0, 3, -1 };
        scene.cells[24].ceiling_present = false;
        scene.render_generation++;
        const auto road = render( below );
        scene.cells[24].ceiling_present = true;
        scene.render_generation++;
        CHECK( static_cast<bool>( render( below ) == road ) );
    }
}

TEST_CASE( "fp3d_unknown_mask_changes_only_background_colour", "[.fp3d_missing_surfaces_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden unknown mask regression", 0, 0, 256, 256,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( -9, 7, 0 );
    scene.sky.daylight = 1;
    fp3d_snapshot::cell floor;
    floor.position = scene.origin + tripoint_rel_ms( 0, 0, -1 );
    floor.terrain = "t_pavement";
    scene.cells.push_back( floor );
    std::vector<unsigned char> colour( 256 * 256 * 4 );
    std::vector<float> depth( 256 * 256 );
    const auto render = [&]( const fp3d_camera::pose &camera ) {
        SDL_RenderFlush( renderer.get() );
        glViewport( 0, 0, 256, 256 );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_BLEND );
        glDisable( GL_CULL_FACE );
        glEnable( GL_DEPTH_TEST );
        glDepthFunc( GL_LESS );
        glDepthMask( GL_TRUE );
        glDepthRange( 0, 1 );
        glClearColor( .2f, .5f, .8f, 1 );
        glClearDepth( 1 );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        fp3d_scene::draw( scene, camera.yaw, camera.pitch, 1, camera.eye,
                          std::nullopt, false, 0, std::nullopt, true );
        GLdouble depth_range[2];
        glGetDoublev( GL_DEPTH_RANGE, depth_range );
        CHECK( depth_range[0] == 0 );
        CHECK( depth_range[1] == 1 );
        glReadPixels( 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, colour.data() );
        glReadPixels( 0, 0, 256, 256, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data() );
        CHECK( glGetError() == GL_NO_ERROR );
    };
    for( float yaw : { 0.f, 90.f, 180.f, 270.f } ) {
        for( float distance : { 6.f, 18.f, 48.f } ) {
            CAPTURE( yaw, distance );
            const auto camera = fp3d_camera::orbit_pose( { yaw, 60, distance }, 0, 0 );
            scene.unknown.clear();
            scene.render_generation++;
            render( camera );
            const auto known_colour = colour;
            const auto known_depth = depth;
            // An anonymous knowledge volume surrounding and above a known
            // lower floor. It must never paint over that already drawn floor.
            for( int y = -2; y <= 2; ++y ) {
                scene.unknown.push_back( { scene.origin + tripoint_rel_ms( -2, y, 0 ), 5 } );
            }
            scene.render_generation++;
            render( camera );
            CHECK( static_cast<bool>( depth == known_depth ) );
            int changed_background = 0;
            int changed_geometry = 0;
            int known_pixels = 0;
            for( size_t i = 0; i < depth.size(); ++i ) {
                const bool changed = !std::equal( colour.begin() + i * 4, colour.begin() + i * 4 + 4,
                                                 known_colour.begin() + i * 4 );
                known_pixels += depth[i] < 1;
                changed_geometry += depth[i] < 1 && changed;
                changed_background += depth[i] == 1 && changed;
            }
            CHECK( known_pixels > 0 );
            CHECK( changed_background > 20 );
            CHECK( changed_geometry == 0 );
            render( camera );
            CHECK( fp3d_scene::statistics().rebuilt == 0 );
        }
    }
}

TEST_CASE( "fp3d_daylight_invalidates_only_chunks_with_metal_undersides",
           "[.fp3d_camera_geometry_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Terrain daylight regression", 0, 0, 128, 128,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 5, 1, 0 );
    scene.render_generation = scene.geometry_generation = 1;
    scene.sky.daylight = .25f;
    fp3d_snapshot::cell tile;
    tile.position = tripoint_abs_ms( 1, 1, 0 );
    tile.terrain = "t_pavement";
    scene.cells.push_back( tile );
    tile.position = tripoint_abs_ms( 9, 1, 0 );
    tile.terrain = "t_metal_floor_no_roof";
    scene.cells.push_back( tile );
    const auto camera = fp3d_camera::orbit_pose( { 0.f, 60.f, 24.f }, 0, 0 );
    const auto render = [&]() {
        SDL_RenderFlush( renderer.get() );
        glViewport( 0, 0, 128, 128 );
        glDisable( GL_SCISSOR_TEST );
        glEnable( GL_DEPTH_TEST );
        glDepthMask( GL_TRUE );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        fp3d_scene::draw( scene, camera.yaw, camera.pitch, 1, camera.eye,
                          std::nullopt, false, 0, std::nullopt, true );
        std::vector<std::uint32_t> pixels( 128 * 128 );
        glReadPixels( 0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
        CHECK( glGetError() == GL_NO_ERROR );
        return pixels;
    };
    render();
    REQUIRE( fp3d_scene::statistics().rebuilt == 2 );
    scene.sky.daylight = .75f;
    const auto warm_cache = render();
    CHECK( fp3d_scene::statistics().indexed_cells == 0 );
    CHECK( fp3d_scene::statistics().rebuilt == 1 );
    render();
    CHECK( fp3d_scene::statistics().rebuilt == 0 );
    fp3d_scene::release();
    const auto cold_cache = render();
    CHECK( static_cast<bool>( cold_cache == warm_cache ) );
}

TEST_CASE( "fp3d_camera_geometry_uses_cached_depth_at_all_orbit_angles",
           "[.fp3d_camera_geometry_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden independent camera regression", 0, 0, 256, 256,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto player = you.pos_bub( here );
    const auto wall = player + tripoint_rel_ms( 8, 0, 1 );
    here.ter_set( wall, ter_str_id( "t_wall" ) );
    here.invalidate_visibility_cache();
    auto scene = fp3d_snapshot::capture_camera( you, here, 2, 0 );
    scene.render_generation = 1;
    fp3d_snapshot::reuse_geometry( scene, nullptr );
    scene.sprites.clear();
    scene.landscape.clear();
    std::vector<float> depth( 256 * 256 );
    const auto render = [&]( const fp3d_camera::pose &camera ) {
        SDL_RenderFlush( renderer.get() );
        glViewport( 0, 0, 256, 256 );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_BLEND );
        glDisable( GL_CULL_FACE );
        glEnable( GL_DEPTH_TEST );
        glDepthFunc( GL_LESS );
        glDepthMask( GL_TRUE );
        glDepthRange( 0, 1 );
        glClearDepth( 1 );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        fp3d_scene::draw( scene, camera.yaw, camera.pitch, 1, camera.eye,
                          std::nullopt, false, 0, std::nullopt, true );
        glReadPixels( 0, 0, 256, 256, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data() );
        CHECK( glGetError() == GL_NO_ERROR );
    };
    for( float yaw : { 0.f, 90.f, 180.f, 270.f } ) {
        for( float distance : { 6.f, 18.f, 48.f } ) {
            CAPTURE( yaw, distance );
            const auto camera = fp3d_camera::orbit_pose( { yaw, 60, distance }, 0, 0 );
            render( camera );
            const auto expected = depth;
            render( camera );
            CHECK( fp3d_scene::statistics().rebuilt == 0 );
            CHECK( static_cast<bool>( depth == expected ) );
            CHECK( std::any_of( depth.begin(), depth.end(), []( float d ) { return d < 1; } ) );
        }
    }
    // Keep the camera at the same absolute world position while the avatar
    // walks. Unchanged loaded terrain must reuse its complete chunk meshes;
    // cropping the capture around each new avatar cell rebuilt the edges.
    auto fixed_camera = fp3d_camera::orbit_pose( { 90, 60, 48 }, 0, 0 );
    render( fixed_camera );
    const auto stationary_depth = depth;
    const auto stationary_cells = scene.cells.size();
    for( int step = 1; step <= 12; ++step ) {
        you.setpos( here, player + tripoint_rel_ms( step, 0, 0 ) );
        const auto *allocation = scene.cells.data();
        auto next = fp3d_snapshot::capture_camera_reusing( you, here, &scene, 2, 0, false );
        next.render_generation = 1 + step;
        if( !next.geometry_generation ) { fp3d_snapshot::reuse_geometry( next, &scene ); }
        scene = std::move( next );
        CHECK( scene.geometry_generation == 1 );
        CHECK( scene.cells.data() == allocation );
        scene.sprites.clear();
        scene.landscape.clear();
        fixed_camera.eye.x -= 1;
        render( fixed_camera );
        CHECK( scene.cells.size() == stationary_cells );
        CHECK( fp3d_scene::statistics().rebuilt == 0 );
        CHECK( fp3d_scene::statistics().indexed_cells == 0 );
        if( step == 1 || step == 6 || step == 12 ) {
            const auto read_shadow = []( GLuint texture, int size ) {
                std::vector<float> values( static_cast<size_t>( size ) * size );
                glPushAttrib( GL_TEXTURE_BIT );
                glBindTexture( GL_TEXTURE_2D, texture );
                glGetTexImage( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, values.data() );
                glPopAttrib();
                return values;
            };
            const auto cached = fp3d_shadow::render( scene, 3 );
            REQUIRE( cached.texture );
            REQUIRE( cached.far_texture );
            const auto near_depth = read_shadow( cached.texture, cached.resolution );
            const auto far_depth = read_shadow( cached.far_texture, cached.far_resolution );
            fp3d_shadow::release();
            const auto rebuilt = fp3d_shadow::render( scene, 3 );
            REQUIRE( rebuilt.texture );
            const auto same_depth = []( const auto &a, const auto &b ) {
                return a.size() == b.size() && std::equal( a.begin(), a.end(), b.begin(),
                []( float x, float y ) { return std::abs( x - y ) <= .00001f; } );
            };
            CHECK( same_depth( near_depth, read_shadow( rebuilt.texture, rebuilt.resolution ) ) );
            CHECK( same_depth( far_depth, read_shadow( rebuilt.far_texture, rebuilt.far_resolution ) ) );
            CHECK( glGetError() == GL_NO_ERROR );
        }
        // Floating matrix translations may round slightly differently.
        for( size_t pixel = 0; pixel < depth.size(); ++pixel ) {
            if( std::abs( depth[pixel] - stationary_depth[pixel] ) > .00001f ) {
                FAIL( "Walking changed terrain depth at fixed world camera" );
                break;
            }
        }
    }
    // A real edit invalidates the affected meshes; its incremental output
    // agrees with a complete rebuild at the same camera.
    here.ter_set( wall, ter_str_id( "t_door_o" ) );
    auto edited = fp3d_snapshot::capture_camera_reusing( you, here, &scene, 2, 0, false );
    edited.render_generation = 14;
    fp3d_snapshot::reuse_geometry( edited, &scene );
    scene = std::move( edited );
    CHECK( scene.geometry_generation == 14 );
    scene.sprites.clear();
    scene.landscape.clear();
    const auto camera = fp3d_camera::orbit_pose( { 90, 60, 18 }, 0, 0 );
    render( camera );
    const auto incremental = depth;
    fp3d_scene::release();
    render( camera );
    CHECK( static_cast<bool>( depth == incremental ) );
}

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

// The 3D god view is verified against the real renderer here: the same capture
// the game publishes, the real compositor, and real screen pixels. A crash
// inside a third-party overlay DLL keeps the game executable from starting on
// this machine, so this test is the in-process substitute for the screenshots.
TEST_CASE( "fp3d_overhead_camera_cuts_the_room_roof_and_points_at_the_avatar",
           "[.fp3d_overhead_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden overhead regression", 0, 0, 128, 128,
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

    // One closed room around the avatar with a real floor above it, exactly the
    // case the overhead camera has to open up without revealing anything else.
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    for( int y = -2; y <= 2; ++y ) {
        for( int x = -2; x <= 2; ++x ) {
            const auto p = you.pos_bub( here ) + tripoint_rel_ms( x, y, 0 );
            const bool wall = std::abs( x ) == 2 || std::abs( y ) == 2;
            here.ter_set( p, ter_str_id( wall ? "t_wall" : "t_floor" ) );
            here.ter_set( p + tripoint_rel_ms::above, ter_str_id( "t_floor" ) );
        }
    }
    fp3d_snapshot::prepare_view( you, here );
    const auto eye_scene = fp3d_snapshot::capture( you, here, fp3d_snapshot::render_above,
                           fp3d_snapshot::render_below, false );
    const auto overhead_scene = fp3d_snapshot::capture( you, here, fp3d_snapshot::render_above,
                                fp3d_snapshot::render_below, true );
    const auto origin = overhead_scene.origin;
    const auto cell_at = []( const fp3d_snapshot::scene_snapshot & scene, const tripoint_abs_ms & p ) {
        const auto it = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & tile ) {
            return tile.position == p;
        } );
        return it == scene.cells.end() ? nullptr : &*it;
    };
    const auto *eye_room = cell_at( eye_scene, origin );
    const auto *overhead_room = cell_at( overhead_scene, origin );
    REQUIRE( eye_room );
    REQUIRE( overhead_room );
    REQUIRE( eye_room->ceiling_present );
    REQUIRE_FALSE( overhead_room->ceiling_present );

    fp3d_compositor::set_viewport( 0, 0, 128, 128, 128, 128 );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    fp3d_compositor::publish( overhead_scene );
    SDL_ShowWindow( window.get() );
    SDL_SetWindowInputFocus( window.get() );
    SDL_PumpEvents();
    REQUIRE( fp3d_compositor::captures_game_input() );
    CHECK_FALSE( fp3d_compositor::overhead() );

    const auto frame = [&]() {
        SDL_SetRenderDrawColor( renderer.get(), 0, 0, 0, 255 );
        SDL_RenderClear( renderer.get() );
        fp3d_compositor::compose();
        std::vector<std::uint32_t> pixels( 128 * 128 );
        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                      pixels.data(), 128 * 4 ) == 0 );
        return pixels;
    };
    const auto nontrivial = []( const std::vector<std::uint32_t> &pixels ) {
        return std::count_if( pixels.begin(), pixels.end(), []( const std::uint32_t pixel ) {
            return ( pixel & 0x00ffffffu ) != 0;
        } ) > 200;
    };
    const auto first_person = frame();
    REQUIRE( nontrivial( first_person ) );

    // One physical F6 press switches the camera; the auto-repeat must not, and
    // the release must not either. Nothing here opens the native settings menu.
    SDL_Event key{};
    key.type = SDL_KEYDOWN;
    key.key.keysym.sym = SDLK_F6;
    REQUIRE( fp3d_compositor::process_event( key ) );
    CHECK( fp3d_compositor::overhead() );
    SDL_Event repeat = key;
    repeat.key.repeat = 1;
    REQUIRE( fp3d_compositor::process_event( repeat ) );
    CHECK( fp3d_compositor::overhead() );
    SDL_Event release = key;
    release.type = SDL_KEYUP;
    REQUIRE( fp3d_compositor::process_event( release ) );
    CHECK( fp3d_compositor::overhead() );
    const auto overhead_frame = frame();
    REQUIRE( nontrivial( overhead_frame ) );
    CHECK( static_cast<bool>( overhead_frame != first_person ) );
    CHECK( glGetError() == GL_NO_ERROR );

    // A sky-only scene from the exact same pose is the reference for "this frame
    // really contains world geometry". Sky and fog are pure gradients, so only a
    // difference against that reference proves the room is inside the frustum.
    fp3d_snapshot::scene_snapshot sky_scene;
    sky_scene.origin = overhead_scene.origin;
    sky_scene.sky = overhead_scene.sky;
    sky_scene.weather = overhead_scene.weather;
    fp3d_compositor::publish( sky_scene );
    const auto sky_only = frame();
    fp3d_compositor::publish( overhead_scene );
    CHECK( static_cast<bool>( overhead_frame != sky_only ) );

    // The pointer ray uses the rendered camera: its centre is the avatar's tile,
    // and the wheel zooms without moving that anchor.
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = 64;
    motion.motion.y = 64;
    fp3d_compositor::process_event( motion );
    const auto pointed = fp3d_compositor::pointer_tile();
    REQUIRE( pointed );
    CHECK( *pointed == origin );
    SDL_Event wheel{};
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.y = 4;
    REQUIRE( fp3d_compositor::process_event( wheel ) );
    fp3d_compositor::process_event( motion );
    const auto zoomed = fp3d_compositor::pointer_tile();
    REQUIRE( zoomed );
    CHECK( *zoomed == origin );
    CHECK( static_cast<bool>( frame() != overhead_frame ) );

    // The cut is what makes the overhead view useful: the same camera over the
    // uncut capture of the same room must produce a different picture.
    fp3d_compositor::publish( eye_scene );
    const auto uncut = frame();
    CHECK( static_cast<bool>( uncut != overhead_frame ) );
    CHECK( glGetError() == GL_NO_ERROR );

    // F6 returns to the eye camera, and the overhead pointer stops existing.
    fp3d_compositor::publish( overhead_scene );
    REQUIRE( fp3d_compositor::process_event( key ) );
    CHECK_FALSE( fp3d_compositor::overhead() );
    CHECK_FALSE( fp3d_compositor::pointer_tile() );
    const auto back = frame();
    REQUIRE( nontrivial( back ) );
    CHECK( glGetError() == GL_NO_ERROR );

    // Leave the last composed frame in the overhead view, so running this test
    // with CDDA_FP3D_CAPTURE_FRAME=1 dumps fp3d-qa-frame.bmp of exactly what the
    // god view shows: the avatar's room opened from above, pointer on its tile.
    REQUIRE( fp3d_compositor::process_event( key ) );
    REQUIRE( fp3d_compositor::overhead() );
    SDL_Event centre{};
    centre.type = SDL_MOUSEMOTION;
    centre.motion.x = 64;
    centre.motion.y = 64;
    fp3d_compositor::process_event( centre );
    REQUIRE( fp3d_compositor::pointer_tile() );
    frame();
    CHECK( glGetError() == GL_NO_ERROR );
    SDL_HideWindow( window.get() );
}

// The remaining overhead acceptance items that can be checked without a human
// at the screen: the camera owns the wheel and the middle button, it never
// consumes the left/right clicks the native menus need, it never advances world
// time, and a menu blocks the toggle.
TEST_CASE( "fp3d_overhead_camera_owns_wheel_and_drag_but_not_world_clicks",
           "[.fp3d_overhead_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden overhead input regression", 0, 0, 128, 128,
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

    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    for( int y = -1; y <= 1; ++y ) {
        for( int x = -1; x <= 1; ++x ) {
            here.ter_set( you.pos_bub( here ) + tripoint_rel_ms( x, y, 0 ), ter_str_id( "t_floor" ) );
        }
    }
    you.set_moves( 1000 );
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here, fp3d_snapshot::render_above,
                       fp3d_snapshot::render_below, true );
    fp3d_compositor::set_viewport( 0, 0, 128, 128, 128, 128 );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    fp3d_compositor::publish( scene );
    SDL_ShowWindow( window.get() );
    SDL_SetWindowInputFocus( window.get() );
    // Activation is asynchronous on Windows; wait before injecting input.
    SDL_RaiseWindow( window.get() );
    for( int attempt = 0; attempt < 100 && SDL_GetKeyboardFocus() != window.get(); ++attempt ) {
        SDL_Delay( 10 );
        SDL_PumpEvents();
    }
    SDL_PumpEvents();
    INFO( "3D active: " << fp3d_compositor::view_3d() <<
          "; window focused: " << ( SDL_GetKeyboardFocus() == window.get() ) );
    REQUIRE( fp3d_compositor::captures_game_input() );
    const auto turn_before = calendar::turn;
    const auto moves_before = you.get_moves();
    SDL_Event f6{};
    f6.type = SDL_KEYDOWN;
    f6.key.keysym.sym = SDLK_F6;
    REQUIRE( fp3d_compositor::process_event( f6 ) );
    REQUIRE( fp3d_compositor::overhead() );
    // Toggling the camera is a presentation change only.
    CHECK( calendar::turn == turn_before );
    CHECK( you.get_moves() == moves_before );
    CHECK( fp3d_compositor::movement_heading() == fp3d_compositor::overhead_azimuth() );
    CHECK( fp3d_compositor::heading() == fp3d_compositor::overhead_azimuth() );

    // Wheel over the world viewport is the camera's; it never reaches inventory
    // scrolling, and both ends of the zoom range are hard limits.
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = 64;
    motion.motion.y = 64;
    fp3d_compositor::process_event( motion );
    const float distance_before = fp3d_compositor::overhead_distance();
    SDL_Event wheel{};
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.y = 2;
    REQUIRE( fp3d_compositor::process_event( wheel ) );
    CHECK( std::abs( fp3d_compositor::overhead_distance() - distance_before ) > .1f );
    SDL_Event zoom_limit{};
    zoom_limit.type = SDL_MOUSEWHEEL;
    zoom_limit.wheel.y = 1000;
    REQUIRE( fp3d_compositor::process_event( zoom_limit ) );
    CHECK( fp3d_compositor::overhead_distance() ==
           Approx( fp3d_camera::orbit_min_distance ) );
    zoom_limit.wheel.y = -2000;
    REQUIRE( fp3d_compositor::process_event( zoom_limit ) );
    CHECK( fp3d_compositor::overhead_distance() ==
           Approx( fp3d_camera::orbit_max_distance ) );

    // The right button never reaches the native dispatcher: the 3D profile binds
    // it to aiming, so an unclaimed right click would surface as an unknown
    // command. The camera consumes it and hands the world one plain secondary
    // select for the picked tile, which opens the ordinary 2D dropdown.
    SDL_Event right{};
    right.type = SDL_MOUSEBUTTONDOWN;
    right.button.button = SDL_BUTTON_RIGHT;
    right.button.x = 64;
    right.button.y = 64;
    right.button.clicks = 1;
    CHECK( fp3d_compositor::process_event( right ) );
    const auto pointed = fp3d_compositor::pointer_tile();
    REQUIRE( pointed );
    CHECK( *pointed == scene.origin );
    CHECK( fp3d_compositor::take_secondary_select() );
    CHECK_FALSE( fp3d_compositor::take_secondary_select() );
    SDL_Event right_up = right;
    right_up.type = SDL_MOUSEBUTTONUP;
    CHECK( fp3d_compositor::process_event( right_up ) );
    // The release alone must not queue anything.
    CHECK_FALSE( fp3d_compositor::take_secondary_select() );
    // Left clicks are handed to the native dispatcher, which resolves them
    // against the pointer pick instead of the undrawn 2D terrain window.
    SDL_Event left = right;
    left.button.button = SDL_BUTTON_LEFT;
    CHECK_FALSE( fp3d_compositor::process_event( left ) );
    SDL_Event left_up = left;
    left_up.type = SDL_MOUSEBUTTONUP;
    CHECK_FALSE( fp3d_compositor::process_event( left_up ) );
    // A right click outside the world viewport (over the HUD sidebar) is still
    // swallowed but must not claim a tile it never pointed at.
    SDL_Event outside = right;
    outside.button.x = 0;
    outside.button.y = 0;
    CHECK( fp3d_compositor::process_event( outside ) );
    CHECK_FALSE( fp3d_compositor::take_secondary_select() );

    // A native window owns the input: F6 must not switch the world camera behind
    // an open menu, and it must not be eaten either.
    fp3d_compositor::set_input_context( "FP3D_PANEL" );
    CHECK_FALSE( fp3d_compositor::process_event( f6 ) );
    CHECK( fp3d_compositor::overhead() );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );

    // The middle button turns the camera and never becomes a native action.
    SDL_Event middle{};
    middle.type = SDL_MOUSEBUTTONDOWN;
    middle.button.button = SDL_BUTTON_MIDDLE;
    middle.button.x = 64;
    middle.button.y = 64;
    const float azimuth_before = fp3d_compositor::overhead_azimuth();
    REQUIRE( fp3d_compositor::process_event( middle ) );
    SDL_Event drag{};
    drag.type = SDL_MOUSEMOTION;
    drag.motion.x = 84;
    drag.motion.y = 64;
    // A quarter-turn must rotate all four WASD actions into the camera frame.
    drag.motion.xrel = -257;
    drag.motion.state = SDL_BUTTON_MMASK;
    REQUIRE( fp3d_compositor::process_event( drag ) );
    CHECK( fp3d_compositor::overhead_azimuth() != Approx( azimuth_before ) );
    CHECK( fp3d_compositor::movement_heading() == fp3d_compositor::overhead_azimuth() );
    const auto heading = fp3d_compositor::movement_heading();
    CHECK( std::abs( std::remainder( heading - azimuth_before, 360.f ) ) ==
           Approx( 90.f ).margin( .1f ) );
    for( const auto &key : { std::pair{ std::pair{ 1, 0 }, tripoint_rel_ms::east },
                            std::pair{ std::pair{ -1, 0 }, tripoint_rel_ms::west },
                            std::pair{ std::pair{ 0, -1 }, tripoint_rel_ms::north },
                            std::pair{ std::pair{ 0, 1 }, tripoint_rel_ms::south } } ) {
        CHECK( fp3d_camera::movement( heading, key.first.first, key.first.second ) == key.second );
    }
    // Turning the camera keeps the avatar centred: put the cursor back on the
    // screen centre and it must still select the avatar's own tile, which is the
    // whole point of orbiting around the player instead of the screen.
    SDL_Event recentre = motion;
    recentre.motion.x = 64;
    recentre.motion.y = 64;
    recentre.motion.state = SDL_BUTTON_MMASK;
    fp3d_compositor::process_event( recentre );
    const auto still_center = fp3d_compositor::pointer_tile();
    REQUIRE( still_center );
    CHECK( *still_center == scene.origin );
    // Rotating and zooming are camera-only changes: no new capture and no chunk
    // rebuild. The scene statistics are the evidence, so a regression that
    // re-captures or re-tessellates the map per mouse frame shows up here.
    const auto frame = [&]() {
        SDL_SetRenderDrawColor( renderer.get(), 0, 0, 0, 255 );
        SDL_RenderClear( renderer.get() );
        fp3d_compositor::compose();
        std::vector<std::uint32_t> pixels( 128 * 128 );
        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                      pixels.data(), 128 * 4 ) == 0 );
        return pixels;
    };
    const auto before_turn = frame();
    const auto stats = fp3d_scene::statistics();
    CHECK( stats.chunks > 0 );
    SDL_Event turn_motion = drag;
    turn_motion.motion.x = 44;
    turn_motion.motion.xrel = -20;
    REQUIRE( fp3d_compositor::process_event( turn_motion ) );
    const auto after_turn = frame();
    CHECK( fp3d_scene::statistics().rebuilt == 0 );
    CHECK( static_cast<bool>( after_turn != before_turn ) );
    // Losing the window focus must end the drag, not leave the camera turning.
    SDL_Event lost{};
    lost.type = SDL_WINDOWEVENT;
    lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    fp3d_compositor::process_event( lost );
    const float azimuth_after = fp3d_compositor::overhead_azimuth();
    SDL_Event stray = drag;
    stray.motion.xrel = 40;
    fp3d_compositor::process_event( stray );
    CHECK( fp3d_compositor::overhead_azimuth() == Approx( azimuth_after ) );

    CHECK( calendar::turn == turn_before );
    CHECK( you.get_moves() == moves_before );
    SDL_HideWindow( window.get() );
}

// A minimal, first-composed-frame-is-overhead case. Running it alone with
// CDDA_FP3D_CAPTURE_FRAME=1 makes the compositor's QA dump save the real god
// view frame (fp3d-qa-frame.bmp) instead of a first-person frame, which is the
// screenshot evidence for the overhead view on a machine where the game binary
// cannot start.
TEST_CASE( "fp3d_overhead_qa_frame_dump", "[.fp3d_overhead_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden overhead frame", 0, 0, 128, 128,
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

    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    for( int y = -3; y <= 3; ++y ) {
        for( int x = -3; x <= 3; ++x ) {
            const auto p = you.pos_bub( here ) + tripoint_rel_ms( x, y, 0 );
            const bool wall = std::abs( x ) == 3 || std::abs( y ) == 3;
            const bool doorway = y == 3 && x == 0;
            here.ter_set( p, ter_str_id( wall && !doorway ? "t_wall" : "t_floor" ) );
            here.ter_set( p + tripoint_rel_ms::above, ter_str_id( "t_floor" ) );
        }
    }
    here.furn_set( you.pos_bub( here ) + tripoint_rel_ms( 1, -1, 0 ), furn_str_id( "f_table" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here, fp3d_snapshot::render_above,
                       fp3d_snapshot::render_below, true );
    // Diagnostics for the QA frame: what the dumped picture is actually made of.
    {
        std::string ids;
        for( const auto &tile : scene.cells ) {
            ids += tile.terrain + " ";
        }
        WARN( "qa frame: cells=" << scene.cells.size() << " visible=" << scene.visible
              << " remembered=" << scene.remembered << " fog_end=" << scene.weather.fog_end
              << " landscape=" << scene.landscape.size() << " sprites=" << scene.sprites.size()
              << " ids=" << ids );
    }
    fp3d_compositor::set_viewport( 0, 0, 128, 128, 128, 128 );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    fp3d_compositor::publish( scene );
    SDL_ShowWindow( window.get() );
    SDL_SetWindowInputFocus( window.get() );
    SDL_PumpEvents();
    REQUIRE( fp3d_compositor::captures_game_input() );
    SDL_Event f6{};
    f6.type = SDL_KEYDOWN;
    f6.key.keysym.sym = SDLK_F6;
    REQUIRE( fp3d_compositor::process_event( f6 ) );
    REQUIRE( fp3d_compositor::overhead() );
    SDL_Event centre{};
    centre.type = SDL_MOUSEMOTION;
    centre.motion.x = 64;
    centre.motion.y = 64;
    fp3d_compositor::process_event( centre );
    SDL_SetRenderDrawColor( renderer.get(), 0, 0, 0, 255 );
    SDL_RenderClear( renderer.get() );
    fp3d_compositor::compose();
    std::vector<std::uint32_t> pixels( 128 * 128 );
    REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                  pixels.data(), 128 * 4 ) == 0 );
    CHECK( std::count_if( pixels.begin(), pixels.end(), []( const std::uint32_t pixel ) {
        return ( pixel & 0x00ffffffu ) != 0;
    } ) > 200 );
    CHECK( glGetError() == GL_NO_ERROR );
    SDL_HideWindow( window.get() );
}

// The same dump, but outdoors: the whole visible field is known, so the picture
// shows the god view as the player will actually see it, with a real building
// keeping its own roof next to the avatar.
TEST_CASE( "fp3d_overhead_qa_frame_outdoor", "[.fp3d_overhead_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden overhead outdoor frame", 0, 0, 256, 192,
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

    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    // A small roofed building six tiles east, so the frame shows a real building
    // volume that keeps its own roof while the avatar stands outside it.
    for( int y = -2; y <= 2; ++y ) {
        for( int x = 4; x <= 8; ++x ) {
            const auto p = you.pos_bub( here ) + tripoint_rel_ms( x, y, 0 );
            const bool wall = x == 4 || x == 8 || std::abs( y ) == 2;
            here.ter_set( p, ter_str_id( wall ? "t_wall" : "t_floor" ) );
            here.ter_set( p + tripoint_rel_ms::above, ter_str_id( "t_floor" ) );
        }
    }
    here.furn_set( you.pos_bub( here ) + tripoint_rel_ms( 1, 1, 0 ), furn_str_id( "f_table" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here, fp3d_snapshot::render_above,
                       fp3d_snapshot::render_below, true );
    WARN( "outdoor qa frame: cells=" << scene.cells.size() << " visible=" << scene.visible
          << " fog_end=" << scene.weather.fog_end << " landscape=" << scene.landscape.size()
          << " sprites=" << scene.sprites.size() << " outdoors=" << scene.weather.outdoors );
    fp3d_compositor::set_viewport( 0, 0, 256, 192, 256, 192 );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    fp3d_compositor::publish( scene );
    SDL_ShowWindow( window.get() );
    SDL_SetWindowInputFocus( window.get() );
    SDL_PumpEvents();
    REQUIRE( fp3d_compositor::captures_game_input() );
    SDL_Event f6{};
    f6.type = SDL_KEYDOWN;
    f6.key.keysym.sym = SDLK_F6;
    REQUIRE( fp3d_compositor::process_event( f6 ) );
    REQUIRE( fp3d_compositor::overhead() );
    SDL_Event centre{};
    centre.type = SDL_MOUSEMOTION;
    centre.motion.x = 128;
    centre.motion.y = 96;
    fp3d_compositor::process_event( centre );
    SDL_SetRenderDrawColor( renderer.get(), 0, 0, 0, 255 );
    SDL_RenderClear( renderer.get() );
    fp3d_compositor::compose();
    std::vector<std::uint32_t> pixels( 256 * 192 );
    REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                  pixels.data(), 256 * 4 ) == 0 );
    CHECK( std::count_if( pixels.begin(), pixels.end(), []( const std::uint32_t pixel ) {
        return ( pixel & 0x00ffffffu ) != 0;
    } ) > 1000 );
    CHECK( glGetError() == GL_NO_ERROR );
    SDL_HideWindow( window.get() );
}
#endif
