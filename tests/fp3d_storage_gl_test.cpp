#include "fp3d_compositor.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "fp3d_materials.h"
#include "fp3d_construction.h"
#include "construction.h"
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
        if( pack == "UNDEAD_PEOPLE_BASE" ) {
            fp3d_snapshot::scene_snapshot crowded;
            for( int i = 0; i < 300; ++i ) {
                auto &cell = crowded.cells.emplace_back();
                cell.terrain = "fp3d_material_cache_probe_" + std::to_string( i );
            }
            fp3d_materials::prepare( crowded );
            REQUIRE( fp3d_materials::bind() );
            REQUIRE( fp3d_materials::lookup( "fp3d_material_cache_probe_255" ) );
            CHECK_FALSE( fp3d_materials::lookup( "fp3d_material_cache_probe_256" ) );
            fp3d_materials::unbind();
            auto &cell = scene.cells.front();
            cell.furniture = fp3d_furniture::describe( "f_bed_frame" );
            const auto before = fp3d_materials::revision();
            construction project{};
            project.post_is_furniture = true;
            project.post_terrain = "f_armchair";
            fp3d_scene::blueprint blueprint{ tripoint_abs_ms( 0, -2, 0 ), 1, true, "armchair" };
            blueprint.terrain = fp3d_construction::terrain_preview( project, blueprint.position );
            REQUIRE( blueprint.terrain );
            fp3d_compositor::set_blueprint( blueprint );
            const auto with_preview = render();
            CHECK( fp3d_materials::revision() == before + 1 );
            REQUIRE( fp3d_materials::bind() );
            CHECK( fp3d_materials::lookup_furniture( "f_bed_frame" ).side );
            CHECK( fp3d_materials::lookup_furniture( "f_armchair" ).side );
            CHECK( fp3d_materials::lookup( "t_floor" ) );
            fp3d_materials::unbind();
            CHECK( render() == with_preview );
            CHECK( fp3d_materials::revision() == before + 1 );
            fp3d_compositor::set_blueprint( std::nullopt );
            scene = floor_scene;
        }
        const auto bare_floor = render();
        std::vector<std::string> furniture{ "f_counter", "f_desk", "f_sofa", "f_sink",
                                            "f_oven", "f_fridge", "f_minifridge",
                                            "f_dresser", "f_wardrobe" };
        if( pack == "UNDEAD_PEOPLE_BASE" ) {
            furniture.insert( furniture.end(),
            { "f_bed_frame", "f_bench", "f_bench_park", "f_armchair", "f_fireplace",
              "f_washer", "f_dryer", "f_dishwasher", "f_woodstove",
              "f_stool", "f_glass_fridge" } );
        }
        for( const std::string &id : furniture ) {
            CAPTURE( id );
            scene = floor_scene;
            auto &tile = *std::find_if( scene.cells.begin(), scene.cells.end(), []( const auto &entry ) {
                return entry.position == tripoint_abs_ms( 0, -2, 0 );
            } );
            tile.furniture = fp3d_furniture::describe( id );
            REQUIRE( tile.furniture );
            const auto model = render();
            CHECK( model != bare_floor );
            if( pack == "UNDEAD_PEOPLE_BASE" && id == "f_bed_frame" ) {
                unsigned wood_pixels = 0;
                for( size_t i = 0; i < model.size(); ++i ) {
                    const int red = model[i] >> 24;
                    const int blue = ( model[i] >> 8 ) & 0xff;
                    wood_pixels += model[i] != bare_floor[i] && red > blue + 20;
                }
                CHECK( wood_pixels > 300 );
            }
            if( pack == "UNDEAD_PEOPLE_BASE" && id == "f_wardrobe" ) {
                REQUIRE( fp3d_materials::bind() );
                const auto faces = fp3d_materials::lookup_furniture( id );
                REQUIRE( faces.top );
                REQUIRE( faces.front );
                CHECK( faces.top->bottom < faces.front->top );
                fp3d_materials::unbind();
            }
            if( pack == "UNDEAD_PEOPLE_BASE" && id == "f_glass_fridge" ) {
                const float focal = width / ( 2 * std::tan(
                                        fp3d_camera::horizontal_fov_degrees * 3.14159265f / 360 ) );
                const auto pixel_at = [&]( float x, float y, float distance ) {
                    const int px = std::lround( width / 2.0f + focal * x / distance );
                    const int py = std::lround( height / 2.0f + focal *
                                               ( fp3d_camera::eye_height - y ) / distance );
                    return model[py * width + px];
                };
                const auto lid = pixel_at( .18f, 1.0f, 2 );
                const auto front = pixel_at( .18f, .65f, 1.58f );
                CHECK( int( ( lid >> 8 ) & 0xff ) > int( lid >> 24 ) + 10 );
                CHECK( std::abs( int( front >> 24 ) - int( ( front >> 8 ) & 0xff ) ) < 18 );
            }
            save( id + "-model", model );
        }
        {
            scene = floor_scene;
            for( auto &tile : scene.cells ) {
                if( tile.position == tripoint_abs_ms( 0, -1, 0 ) ||
                    tile.position == tripoint_abs_ms( 0, -3, 0 ) ) {
                    tile.furniture = fp3d_furniture::describe( "f_locker" );
                }
            }
            const auto cabinets = render();
            fp3d_snapshot::sprite apple;
            apple.position = tripoint_abs_ms( 0, -3, 0 );
            apple.kind = fp3d_snapshot::sprite_kind::item;
            apple.id = "apple";
            apple.height = .7f;
            apple.elevation = 1;
            scene.sprites.push_back( apple );
            CHECK( render() == cabinets );
            scene.sprites.front().position = tripoint_abs_ms( 0, -1, 0 );
            const auto visible = render();
            CHECK( visible != cabinets );
            REQUIRE( fp3d_compositor::interaction_target() );
            CHECK( fp3d_compositor::interaction_target()->type == fp3d_targeting::kind::item );
            save( "front-cabinet-contents-targeted", visible );
        }
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
        for( const std::string facing_id : {
                 "f_locker", "f_bookcase"
             } ) {
            CAPTURE( facing_id );
            // Neighbor-wall changes rotate the shared instance, not its cached
            // texture/model. The same geometry must be used by the placement ghost.
            scene = floor_scene;
            const auto cabinet_position = tripoint_abs_ms( 0, -2, 0 );
            auto &oriented = *std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & entry ) {
                return entry.position == cabinet_position;
            } );
            oriented.furniture = fp3d_furniture::describe( facing_id );
            const auto front = render();
            auto &backing = *std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & entry ) {
                return entry.position == cabinet_position + tripoint_rel_ms::east;
            } );
            backing.kind = fp3d_snapshot::terrain_kind::wall;
            backing.terrain = "t_wall";
            const auto turned = render();
            unsigned face_changes = 0;
            for( int py = height / 2; py < height / 2 + 48; ++py ) {
                for( int px = width / 2 - 16; px < width / 2 + 16; ++px ) {
                    face_changes += front[py * width + px] != turned[py * width + px];
                }
            }
            // Glyph packs can paint the central region black in both poses.
            // Check the actual separate-face artwork here and both packs' full
            // geometric footprint with an opaque diagnostic material below.
            if( pack == "UNDEAD_PEOPLE_BASE" ) {
                CHECK( face_changes > 20 );
            }
            save( facing_id + "-east-backing-front-faces-west", turned );
            scene.origin = tripoint_abs_ms( -1, 0, 0 );
            save( facing_id + "-east-backing-oblique", render() );
            scene.origin = tripoint_abs_ms::zero;
            oriented.furniture.reset();
            const auto only_wall = render();
            construction project{};
            project.post_is_furniture = true;
            project.post_terrain = facing_id;
            fp3d_scene::blueprint ghost{ cabinet_position, 1.85f, true, "oriented storage" };
            ghost.terrain = fp3d_construction::terrain_preview( project, cabinet_position );
            REQUIRE( ghost.terrain );
            fp3d_compositor::set_blueprint( ghost );
            const auto preview = render();
            save( facing_id + "-east-backing-blueprint", preview );
            ghost.valid = false;
            fp3d_compositor::set_blueprint( ghost );
            CHECK( render() != preview );
            fp3d_compositor::set_blueprint( std::nullopt );
            CHECK( render() == only_wall );
            // Material transparency must not redefine geometry. An unknown
            // image uses the renderer's opaque fallback while preserving the
            // real furniture shape and all normal orientation code paths.
            auto probe = *fp3d_furniture::describe( facing_id );
            probe.id = "fp3d_facing_geometry_probe_" + facing_id;
            oriented.furniture = probe;
            const auto built_geometry = render();
            oriented.furniture.reset();
            ghost.valid = true;
            ghost.terrain->furniture = probe;
            fp3d_compositor::set_blueprint( ghost );
            const auto ghost_geometry = render();
            const auto span = [&]( const std::vector<std::uint32_t> &pixels ) {
                int first = width;
                int last = -1;
                for( int px = width / 2 - 80; px < width / 2 + 80; ++px ) {
                    const size_t pixel = ( height / 2 + 30 ) * width + px;
                    if( pixels[pixel] != only_wall[pixel] ) {
                        first = std::min( first, px );
                        last = std::max( last, px );
                    }
                }
                return std::pair{ first, last };
            };
            const auto built_span = span( built_geometry );
            const auto ghost_span = span( ghost_geometry );
            CHECK( built_span.first < built_span.second );
            CHECK( std::abs( built_span.first - ghost_span.first ) <= 1 );
            CHECK( std::abs( built_span.second - ghost_span.second ) <= 1 );
            fp3d_compositor::set_blueprint( std::nullopt );
            CHECK( render() == only_wall );
            oriented.furniture = fp3d_furniture::describe( facing_id );
            backing.kind = fp3d_snapshot::terrain_kind::floor;
            backing.terrain = "t_floor";
            CHECK( render() == front ); // Removing the backing restores the free-space orientation.
        }
    }
}
#endif
