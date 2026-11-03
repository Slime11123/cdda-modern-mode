#include "fp3d_compositor.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "fp3d_materials.h"
#include "sdl_geometry.h"
#include "sdltiles.h"

// Hidden production-renderer regression, using owned snapshots only.
TEST_CASE( "fp3d_gl_storage_doors_and_contents_keep_environment_occlusion", "[.fp3d_gl]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
    } );
    SDL_Window_Ptr window( SDL_CreateWindow( "FP3D hidden storage regression",
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
    on_out_of_scope cleanup( []() {
        fp3d_compositor::release();
        tilecontext.reset();
    } );
    fp3d_compositor::attach( renderer.get(), window.get() );
    int width = 0;
    int height = 0;
    REQUIRE( SDL_GetRendererOutputSize( renderer.get(), &width, &height ) == 0 );
    fp3d_compositor::set_viewport( 0, 0, width, height, width, height );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    for( const std::string pack : {
             "UNDEAD_PEOPLE_BASE", "retrodays"
         } ) {
        CAPTURE( pack );
        REQUIRE( TILESETS.count( pack ) == 1 );
        auto assets = cache.load_tileset( pack, renderer, false, true, false, false );
        fp3d_materials::set_tileset( assets, renderer );
        fp3d_snapshot::scene_snapshot scene;
        scene.origin = tripoint_abs_ms::zero;
        for( int y = -5; y <= 1; ++y ) {
            for( int x = -3; x <= 3; ++x ) {
                fp3d_snapshot::cell tile;
                tile.position = tripoint_abs_ms( x, y, 0 );
                tile.terrain = "t_floor";
                scene.cells.push_back( tile );
            }
        }
        const auto floor_scene = scene;
        const auto render = [&]() {
            SDL_SetRenderDrawColor( renderer.get(), 8, 12, 20, 255 );
            REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
            fp3d_compositor::publish( scene );
            fp3d_compositor::compose();
            REQUIRE( fp3d_materials::ready() );
            std::vector<std::uint32_t> pixels( width * height );
            REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                           pixels.data(), width * 4 ) == 0 );
            return pixels;
        };
        const auto save = [&]( const std::string & name, const std::vector<std::uint32_t> &pixels ) {
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                std::filesystem::create_directories( directory );
                SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom(
                                             const_cast<std::uint32_t *>( pixels.data() ), width, height,
                                             32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                REQUIRE( picture );
                CHECK( IMG_SavePNG( picture.get(), ( std::filesystem::path( directory ) /
                                                     ( pack + "-storage-" + name + ".png" ) ).u8string().c_str() ) == 0 );
            }
        };
        for( const std::string id : {
                 "f_locker", "f_cupboard"
             } ) {
            for( const auto &position : {
                     tripoint_abs_ms( 0, -1, 0 ), tripoint_abs_ms( 0, -3, 0 ),
                     tripoint_abs_ms( 1, -2, 0 )
                 } ) {
                CAPTURE( id, position );
                scene = floor_scene;
                const auto tile = std::find_if( scene.cells.begin(), scene.cells.end(),
                [&]( const auto & entry ) {
                    return entry.position == position;
                } );
                REQUIRE( tile != scene.cells.end() );
                tile->furniture = fp3d_furniture::describe( id );
                REQUIRE( tile->furniture );
                const auto cabinet = render();
                fp3d_snapshot::sprite object;
                object.position = position;
                object.kind = fp3d_snapshot::sprite_kind::item;
                object.id = "apple";
                object.height = .4f;
                object.elevation = id == "f_locker" ? .9f : .15f;
                scene.sprites.push_back( object );
                const auto with_item = render();
                unsigned differences = 0;
                for( size_t i = 0; i < with_item.size(); ++i ) {
                    differences += with_item[i] != cabinet[i];
                }
                CHECK( differences > 20 ); // Entire icon is inside the solid body.
                scene.sprites.front().multiple_items = true;
                CHECK( render() != with_item );
                scene.sprites.clear();
                CHECK( render() == cabinet );
                save( id + "-" + std::to_string( position.x() ) + "-" +
                      std::to_string( -position.y() ) + "-doors", cabinet );
                save( id + "-" + std::to_string( position.x() ) + "-" +
                      std::to_string( -position.y() ) + "-contents", with_item );
            }
        }
        scene = floor_scene;
        auto &locker = *std::find_if( scene.cells.begin(), scene.cells.end(), []( const auto & entry ) {
            return entry.position == tripoint_abs_ms( 0, -3, 0 );
        } );
        locker.furniture = fp3d_furniture::describe( "f_locker" );
        const auto cabinet = render();
        if( pack == "UNDEAD_PEOPLE_BASE" ) {
            REQUIRE( fp3d_materials::bind() );
            const auto body = fp3d_materials::lookup_furniture( "f_locker", 0 );
            REQUIRE( body.top );
            REQUIRE( body.side );
            REQUIRE( body.front );
            CHECK( body.top->bottom < body.front->top );
            CHECK( body.front->right - body.front->left >
                   3 * ( body.side->right - body.side->left ) );
            fp3d_materials::unbind();
        }
        fp3d_snapshot::sprite object;
        object.position = locker.position;
        object.kind = fp3d_snapshot::sprite_kind::item;
        object.id = "apple";
        object.height = .4f;
        object.elevation = .9f;
        // A creature is never included in the storage-content display pass.
        auto creature = object;
        creature.kind = fp3d_snapshot::sprite_kind::monster;
        creature.id = "mon_zombie";
        scene.sprites.push_back( creature );
        CHECK( render() == cabinet );
        scene.sprites.front() = object;
        scene.sprites.front().position += tripoint_rel_ms::north;
        CHECK( render() == cabinet ); // An ordinary item behind another tile.
        scene.sprites.clear();
        auto &wall = *std::find_if( scene.cells.begin(), scene.cells.end(), []( const auto & entry ) {
            return entry.position == tripoint_abs_ms( 0, -2, 0 );
        } );
        wall.terrain = "t_wall";
        wall.kind = fp3d_snapshot::terrain_kind::wall;
        const auto walled = render();
        scene.sprites.push_back( object );
        CHECK( render() == walled );
        save( "wall-occludes-contents", walled );
        // The same rule applies to a solid floor above the cabinet.
        scene = floor_scene;
        scene.origin = tripoint_abs_ms( 0, 0, 1 );
        for( int y = -5; y <= 1; ++y ) {
            for( int x = -3; x <= 3; ++x ) {
                fp3d_snapshot::cell floor;
                floor.position = tripoint_abs_ms( x, y, 1 );
                floor.terrain = "t_floor";
                scene.cells.push_back( floor );
            }
        }
        auto &below = *std::find_if( scene.cells.begin(), scene.cells.end(), []( const auto & entry ) {
            return entry.position == tripoint_abs_ms( 0, -3, 0 );
        } );
        below.furniture = fp3d_furniture::describe( "f_locker" );
        const auto upstairs = render();
        scene.sprites.push_back( object );
        CHECK( render() == upstairs );
        scene = floor_scene;
        auto &crate = *std::find_if( scene.cells.begin(), scene.cells.end(), []( const auto & entry ) {
            return entry.position == tripoint_abs_ms( 0, -3, 0 );
        } );
        crate.furniture = fp3d_furniture::describe( "f_crate_c" );
        const auto sealed = render();
        object.elevation = .1f;
        scene.sprites.push_back( object );
        CHECK( render() == sealed ); // Sealed native containers still hide contents.
    }
}
#endif
