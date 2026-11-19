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
#include "field_type.h"
#include "fp3d_vehicle_renderer.h"
#include "fp3d_construction.h"
#include "construction.h"
#include "sdl_geometry.h"
#include "sdltiles.h"
#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

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
              "f_stool", "f_glass_fridge", "f_chair", "f_coffee_table" } );
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
            if( pack == "UNDEAD_PEOPLE_BASE" && id == "f_coffee_table" ) {
                unsigned wood = 0;
                unsigned pale = 0;
                for( size_t i = 0; i < model.size(); ++i ) {
                    const int red = model[i] >> 24;
                    const int green = ( model[i] >> 16 ) & 0xff;
                    const int blue = ( model[i] >> 8 ) & 0xff;
                    if( model[i] != bare_floor[i] ) {
                        wood += red > blue + 20 && red > green;
                        pale += red > 100 && std::abs( red - green ) < 8 && std::abs( red - blue ) < 8;
                    }
                }
                CHECK( wood > 100 );
                CHECK( pale < 10 );
            }
            if( pack == "UNDEAD_PEOPLE_BASE" && id == "f_oven" ) {
                tile.appliance = "ap_oven";
                const auto installed = render();
                CHECK( installed == model );
                save( "connected-oven", installed );
                tile.appliance.clear();
            }
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
TEST_CASE( "fp3d_gl_model_proportions_and_glass_door_handles", "[.fp3d_gl]" )
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
        {
            // Review both sides of native furniture -> grid appliance conversion.
            const auto empty = render();
            auto &panel = scene.cells.at( 3 + 3 * 7 );
            panel.furniture = fp3d_furniture::describe( "f_solar_unit" );
            REQUIRE( panel.furniture );
            const auto pitched = render();
            CHECK( pitched != empty );
            save( "solar-before-grid", pitched );
            panel.furniture.reset();
            fp3d_vehicle::vehicle_view appliance;
            appliance.origin = panel.position;
            auto &solar = appliance.parts.emplace_back();
            solar.id = "ap_ground_solar_panel";
            scene.vehicles.push_back( appliance );
            const auto flat = render();
            CHECK( flat != empty );
            CHECK( flat != pitched );
            save( "solar-after-grid", flat );
            scene = floor_scene;
            scene.cells.at( 3 + 3 * 7 ).furniture = fp3d_furniture::describe( "f_locker" );
            save( "narrow-locker", render() );
            scene = floor_scene;
            fp3d_vehicle::vehicle_view bicycle;
            bicycle.origin = tripoint_abs_ms( 0, -3, 0 );
            bicycle.angle_degrees = 0.f;
            const auto add = [&]( int x, const std::string &id, fp3d_vehicle::role role ) {
                auto &p = bicycle.parts.emplace_back();
                p.mount_x = x;
                p.id = id;
                p.kind = role;
            };
            for( int x : { -1, 0, 1 } ) {
                add( x, "xlframe", fp3d_vehicle::role::frame );
            }
            add( -1, "wheel_bicycle", fp3d_vehicle::role::wheel );
            add( 1, "wheel_bicycle", fp3d_vehicle::role::wheel );
            add( 0, "saddle", fp3d_vehicle::role::seat );
            add( 0, "foot_pedals", fp3d_vehicle::role::engine );
            add( -1, "basketsm", fp3d_vehicle::role::cargo );
            scene.vehicles.push_back( bicycle );
            const auto cycle = render();
            CHECK( cycle != empty );
            save( "bicycle-side", cycle );
            scene.vehicles.front().angle_degrees = 20.f;
            const auto oblique = render();
            CHECK( oblique != cycle );
            save( "bicycle-oblique", oblique );
            scene = floor_scene;
            for( const std::string id : { "t_wall_glass", "t_door_glass_c", "t_door_glass_o" } ) {
                auto &door = scene.cells.at( 3 + 3 * 7 );
                door.terrain = id;
                door.kind = fp3d_snapshot::terrain_shape( id );
                save( id + "-white-pull", render() );
            }
            scene = floor_scene;
        }
    }
}
TEST_CASE( "fp3d_gl_ground_coverings_render_intensity_mixing_and_removal", "[.fp3d_gl]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "FP3D ground coverings",
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
    for( const std::string pack : { "UNDEAD_PEOPLE_BASE", "retrodays" } ) {
        CAPTURE( pack );
        REQUIRE( TILESETS.count( pack ) == 1 );
        auto assets = cache.load_tileset( pack, renderer, false, true, false, false );
        fp3d_materials::set_tileset( assets, renderer );
        fp3d_snapshot::scene_snapshot scene;
        scene.origin = tripoint_abs_ms::zero;
        scene.render_generation = 1;
        for( int y = -5; y <= 1; ++y ) {
            for( int x = -3; x <= 3; ++x ) {
                auto &tile = scene.cells.emplace_back();
                tile.position = tripoint_abs_ms( x, y, 0 );
                tile.terrain = "t_floor";
            }
        }
        auto &target = scene.cells.at( 3 + 3 * 7 );
        const auto render = [&]() {
            ++scene.render_generation;
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
        const auto save = [&]( const std::string &name, const auto &pixels ) {
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                std::filesystem::create_directories( directory );
                SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom(
                                             const_cast<std::uint32_t *>( pixels.data() ), width, height,
                                             32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                REQUIRE( picture );
                CHECK( IMG_SavePNG( picture.get(), ( std::filesystem::path( directory ) /
                                                     ( pack + "-" + name + ".png" ) ).u8string().c_str() ) == 0 );
            }
        };
        const auto clean = render();
        save( "clean", clean );
        for( const std::string id : { "fd_blood", "fd_blood_veggy", "fd_blood_insect",
                 "fd_blood_invertebrate", "fd_acid", "fd_bile", "fd_slime", "fd_sap",
                 "fd_sludge", "fd_gibs_flesh", "fd_gibs_veggy", "fd_gibs_insect",
                 "fd_gibs_invertebrate", "fd_web", "fd_web_nostick", "fd_void_web",
                 "fd_churned_earth", "fd_congealed_light", "fd_rubble", "fd_acid_vent" } ) {
            CAPTURE( id );
            target.ground_fields = { { id, 1 } };
            const auto thin = render();
            CHECK( thin != clean );
            const int maximum = field_type_str_id( id )->get_max_intensity();
            target.ground_fields.front().intensity = maximum;
            const auto thick = render();
            if( maximum > 1 ) {
                int rotation = 0;
                const auto *thin_art = fp3d_materials::resolve_ground_field( *assets, { id, 1 },
                                       scene.season, rotation );
                const auto *thick_art = fp3d_materials::resolve_ground_field( *assets, { id, maximum },
                                        scene.season, rotation );
                // Native intensity art may change; an unchanged sprite keeps
                // its full-tile size so neighbouring coverings still meet.
                if( thin_art && thin_art == thick_art ) {
                    CHECK( thick == thin );
                } else {
                    CHECK( thick != thin );
                }
            }
            save( id, thick );
            target.ground_fields.clear();
            CHECK( render() == clean );
        }
        if( pack == "UNDEAD_PEOPLE_BASE" ) {
            auto &neighbour = scene.cells.at( 4 + 3 * 7 );
            for( const std::string id : { "fd_blood", "fd_acid", "fd_slime", "fd_web", "fd_gibs_flesh" } ) {
                CAPTURE( id );
                target.ground_fields = { { id, 3 } };
                neighbour.ground_fields = { { id, 1 } };
                const auto isolated = render();
                fp3d_snapshot::connect_ground_fields( scene );
                CHECK( target.ground_fields.front().connections == 2 );
                CHECK( neighbour.ground_fields.front().connections == 4 );
                const auto connected = render();
                CHECK( connected != isolated );
                save( id + "-connected", connected );
            }
            neighbour.ground_fields.clear();
        }
        target.ground_fields = { { "fd_blood", 3 } };
        const auto blood = render();
        target.ground_fields.push_back( { "fd_acid", 2 } );
        const auto mixed = render();
        CHECK( mixed != blood );
        save( "mixed-blood-acid", mixed );
        target.ground_fields.erase( target.ground_fields.begin() );
        CHECK( render() != mixed );
        target.ground_fields.clear();
        CHECK( render() == clean );
        // The same covering must be hidden behind solid world geometry.
        auto &wall = scene.cells.at( 3 + 4 * 7 );
        wall.terrain = "t_wall";
        wall.kind = fp3d_snapshot::terrain_kind::wall;
        const auto blocked = render();
        target.ground_fields = { { "fd_acid", 3 }, { "fd_web", 3 } };
        CHECK( render() == blocked );
    }
}
TEST_CASE( "fp3d_gl_zombie_status_badges_keep_body_scale_and_show_detection", "[.fp3d_gl]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "FP3D zombie status badges",
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
    auto assets = cache.load_tileset( "UNDEAD_PEOPLE_BASE", renderer, false, true, false, false );
    fp3d_materials::set_tileset( assets, renderer );
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms::zero;
    for( int y = -5; y <= 1; ++y ) {
        for( int x = -3; x <= 3; ++x ) {
            auto &tile = scene.cells.emplace_back();
            tile.position = tripoint_abs_ms( x, y, 0 );
            tile.terrain = "t_floor";
        }
    }
    fp3d_snapshot::sprite zombie;
    zombie.id = "mon_zombie";
    zombie.height = 2.16f;
    zombie.position = tripoint_abs_ms( 0, -2, 0 );
    scene.sprites.push_back( zombie );
    const auto render = [&]( const std::string &name ) {
        SDL_SetRenderDrawColor( renderer.get(), 8, 12, 20, 255 );
        REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
        fp3d_compositor::publish( scene );
        fp3d_compositor::compose();
        std::vector<std::uint32_t> pixels( width * height );
        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                       pixels.data(), width * 4 ) == 0 );
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            std::filesystem::create_directories( directory );
            SDL_Surface_Ptr surface( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(),
                                     width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( surface );
            CHECK( IMG_SavePNG( surface.get(), ( std::filesystem::path( directory ) /
                                                 ( "zombie-" + name + ".png" ) ).u8string().c_str() ) == 0 );
        }
        return pixels;
    };
    const auto clean = render( "normal" );
    const auto key = fp3d_materials::sprite_key( scene.sprites.front() );
    for( const std::string icon : { "overlay_effect_stunned", "overlay_effect_downed",
             "overlay_effect_bleed", "overlay_effect_onfire" } ) {
        CAPTURE( icon );
        scene.sprites.front().status_icons = { icon };
        const auto affected = render( icon );
        CHECK( affected != clean );
        CHECK( fp3d_materials::sprite_key( scene.sprites.front() ) == key );
        // Status canvases must not alter the body or floor in the lower frame.
        CHECK( std::equal( clean.begin() + width * height / 2, clean.end(),
                           affected.begin() + width * height / 2 ) );
    }
    scene.sprites.front().status_icons.clear();
    scene.sprites.front().alerted = true;
    const auto detected = render( "sees-player" );
    CHECK( detected != clean );
    REQUIRE( fp3d_materials::bind() );
    CHECK( fp3d_materials::lookup( "$status:overlay_hostile_sees_player" ) );
    fp3d_materials::unbind();
    scene.sprites.front().alerted = false;
    CHECK( render( "cleared" ) == clean );
}

TEST_CASE( "fp3d_gl_natural_ground_matches_native_connected_art", "[.fp3d_gl][ground_terrain]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "FP3D natural terrain",
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

    const std::array<const char *, 16> subtiles{
        "unconnected", "end_piece", "end_piece", "corner",
        "end_piece", "corner", "edge", "t_connection",
        "end_piece", "edge", "corner", "t_connection",
        "corner", "t_connection", "t_connection", "center" };
    const std::array<int, 16> rotations{ 0, 0, 1, 0, 3, 3, 1, 0, 2, 0, 1, 1, 2, 3, 2, 0 };
    for( const std::string pack : { "UNDEAD_PEOPLE_BASE", "retrodays" } ) {
        DYNAMIC_SECTION( pack ) {
            CAPTURE( pack );
            REQUIRE( TILESETS.count( pack ) == 1 );
            auto assets = cache.load_tileset( pack, renderer, false, true, false, false );
            fp3d_materials::set_tileset( assets, renderer );
            fp3d_snapshot::scene_snapshot scene;
            for( const std::string id : { "t_grass", "t_grass_long", "t_grass_dead", "t_dirt", "t_sand", "t_moss" } ) {
                for( int mask = 0; mask < 16; ++mask ) {
                    auto &tile = scene.cells.emplace_back();
                    tile.terrain = id;
                    tile.ground_connections = mask;
                }
            }
            fp3d_materials::prepare( scene );
            SDL_RenderFlush( renderer.get() );
            REQUIRE( fp3d_materials::bind() );
            std::vector<unsigned char> atlas( 2048 * 2048 * 4 );
            glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data() );
            fp3d_materials::unbind();
            SDL_Texture_Ptr expected( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, 64, 64 ) );
            REQUIRE( expected );
            for( const auto &cell : scene.cells ) {
                const int mask = cell.ground_connections;
                const std::string key = fp3d_materials::terrain_key( cell );
                CAPTURE( key );
                const tile_type *base = fp3d_materials::resolve_terrain( *assets, cell.terrain, SPRING );
                REQUIRE( base );
                const tile_type *art = base;
                int rotation = 0;
                if( base->multitile ) {
                    auto child = assets->find_tile_type_by_season( cell.terrain + "_" + subtiles[mask], SPRING );
                    REQUIRE( child );
                    art = &child->tile();
                    rotation = rotations[mask];
                }
                REQUIRE( SDL_SetRenderTarget( renderer.get(), expected.get() ) == 0 );
                SDL_SetRenderDrawColor( renderer.get(), 180, 180, 180, 255 );
                SDL_RenderClear( renderer.get() );
                // One sprite per weighted choice in these packs; foreground arrays
                // encode orientations and the background must remain unrotated.
                for( bool foreground : { false, true } ) {
                    // Reproduce native rotation arrays using the same selected entry.
                    const auto &list = foreground ? art->fg : art->bg;
                    unsigned seed = 2166136261u;
                    for( unsigned char c : key ) { seed = ( seed ^ c ) * 16777619u; }
                    const auto *choices = list.pick( seed );
                    if( !choices || choices->empty() ) { continue; }
                    const int index = ( *choices )[rotation % choices->size()];
                    const texture *source = assets->get_tile( index );
                    REQUIRE( source );
                    const SDL_Rect dest{ 0, 0, 64, 64 };
                    REQUIRE( source->render_copy_ex( renderer, &dest,
                             foreground && choices->size() == 1 ? -90.0 * rotation : 0,
                             nullptr, SDL_FLIP_NONE ) == 0 );
                }
                std::vector<unsigned char> native( 64 * 64 * 4 );
                REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA32,
                                              native.data(), 64 * 4 ) == 0 );
                REQUIRE( SDL_SetRenderTarget( renderer.get(), nullptr ) == 0 );
                REQUIRE( fp3d_materials::bind() );
                const auto uv = fp3d_materials::lookup( key );
                REQUIRE( uv );
                const int ax = static_cast<int>( uv->left * 2048 );
                const int ay = static_cast<int>( uv->top * 2048 );
                CHECK( ( uv->right - uv->left ) * 2048 == Approx( 63.f ) );
                fp3d_materials::unbind();
                unsigned mismatches = 0;
                for( int y = 0; y < 64; ++y ) {
                    for( int x = 0; x < 64; ++x ) {
                        for( int c = 0; c < 4; ++c ) {
                            mismatches += atlas[( ( ay + y ) * 2048 + ax + x ) * 4 + c] !=
                                          native[( y * 64 + x ) * 4 + c];
                        }
                    }
                }
                CHECK( mismatches == 0 );
            }
            // Production renders of connected natural surfaces in both cameras.
            scene.cells.clear();
            scene.sky.daylight = 1.f;
            scene.weather.fog_end = 60.f;
            for( int y = -10; y <= 3; ++y ) {
                for( int x = -8; x <= 8; ++x ) {
                    auto &tile = scene.cells.emplace_back();
                    tile.position = tripoint_abs_ms( x, y, 0 );
                    tile.terrain = x >= -2 && x <= 2 && y >= -6 && y <= -2 ? "t_grass_long" :
                                   x >= -5 && x <= 4 && y >= -8 && y <= 0 ? "t_grass" : "t_dirt";
                }
            }
            fp3d_snapshot::connect_ground_terrain( scene );
            for( const std::string mode : { "first_person", "overhead" } ) {
                fp3d_compositor::restore_view_mode( mode );
                fp3d_compositor::publish( scene );
                fp3d_compositor::compose();
                if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                    std::vector<std::uint32_t> pixels( width * height );
                    REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                  pixels.data(), width * 4 ) == 0 );
                    SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(), width, height,
                                             32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                    REQUIRE( picture );
                    CHECK( IMG_SavePNG( picture.get(), ( std::filesystem::path( directory ) /
                                       ( pack + "-natural-ground-" + mode + ".png" ) ).u8string().c_str() ) == 0 );
                }
            }
        }
    }
}

TEST_CASE( "fp3d_gl_native_fire_and_smoke_render_in_both_cameras", "[.fp3d_gl][field_sprites]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    constexpr int width = 640;
    constexpr int height = 480;
    SDL_Window_Ptr window( SDL_CreateWindow( "FP3D field sprites", SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    REQUIRE( SDL_SetHint( SDL_HINT_RENDER_DRIVER, "opengl" ) );
    on_out_of_scope clear_hint( []() { SDL_ResetHint( SDL_HINT_RENDER_DRIVER ); } );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
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
    fp3d_compositor::set_viewport( 0, 0, width, height, width, height );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    REQUIRE( TILESETS.count( "UNDEAD_PEOPLE_BASE" ) == 1 );
    fp3d_materials::set_tileset( cache.load_tileset( "UNDEAD_PEOPLE_BASE", renderer,
                                 false, true, false, false ), renderer );
    fp3d_snapshot::scene_snapshot scene;
    scene.sky.daylight = 1.f;
    scene.weather.fog_end = 60.f;
    for( int y = -6; y <= 2; ++y ) {
        for( int x = -4; x <= 4; ++x ) {
            auto &tile = scene.cells.emplace_back();
            tile.position = tripoint_abs_ms( x, y, 0 );
            tile.terrain = "t_floor";
        }
    }
    fp3d_snapshot::sprite fire;
    fire.position = tripoint_abs_ms( -1, -4, 0 );
    fire.kind = fp3d_snapshot::sprite_kind::field;
    fire.id = "fd_fire";
    fire.field_intensity = 2;
    fire.height = 1.05f;
    auto smoke = fire;
    smoke.position = tripoint_abs_ms( 1, -4, 0 );
    smoke.id = "fd_smoke";
    smoke.height = 1.25f;
    smoke.elevation = .45f;
    const auto render = [&]() {
        SDL_SetRenderDrawColor( renderer.get(), 8, 12, 20, 255 );
        REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
        fp3d_compositor::publish( scene );
        fp3d_compositor::compose();
        std::vector<std::uint32_t> pixels( width * height );
        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                       pixels.data(), width * 4 ) == 0 );
        return pixels;
    };
    for( const std::string mode : { "first_person", "overhead" } ) {
        CAPTURE( mode );
        fp3d_compositor::restore_view_mode( mode );
        scene.sprites.clear();
        const auto bare = render();
        scene.sprites = { fire };
        CHECK( render() != bare );
        scene.sprites = { smoke };
        CHECK( render() != bare );
    }
    {
        REQUIRE( fp3d_materials::bind() );
        on_out_of_scope unbind( []() { fp3d_materials::unbind(); } );
        for( const auto &image : { fire, smoke } ) {
            const auto frames = fp3d_materials::field_animation( image );
            REQUIRE( frames.size() > 1 );
            CHECK( std::all_of( frames.begin(), frames.end(), []( const auto &frame ) {
                return frame.uv && frame.ticks > 0;
            } ) );
        }
    }
    fp3d_compositor::restore_view_mode( "first_person" );
    for( auto &tile : scene.cells ) {
        if( tile.position.y() == -2 ) {
            tile.terrain = "t_wall";
            tile.kind = fp3d_snapshot::terrain_kind::wall;
        }
    }
    scene.sprites.clear();
    const auto wall = render();
    scene.sprites = { fire, smoke };
    CHECK( render() == wall );
}

TEST_CASE( "fp3d_gl_crossed_flora_and_flat_lilypad_in_both_cameras", "[.fp3d_gl][plants_gl]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    constexpr int width = 640;
    constexpr int height = 480;
    SDL_Window_Ptr window( SDL_CreateWindow( "FP3D vegetation", SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    REQUIRE( SDL_SetHint( SDL_HINT_RENDER_DRIVER, "opengl" ) );
    on_out_of_scope clear_hint( []() { SDL_ResetHint( SDL_HINT_RENDER_DRIVER ); } );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
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
    fp3d_compositor::set_viewport( 0, 0, width, height, width, height );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    REQUIRE( TILESETS.count( "UNDEAD_PEOPLE_BASE" ) == 1 );
    fp3d_materials::set_tileset( cache.load_tileset( "UNDEAD_PEOPLE_BASE", renderer,
                                 false, true, false, false ), renderer );
    fp3d_snapshot::scene_snapshot scene;
    scene.sky.daylight = 1.f;
    scene.weather.fog_end = 60.f;
    for( int y = -7; y <= 2; ++y ) {
        for( int x = -4; x <= 4; ++x ) {
            auto &tile = scene.cells.emplace_back();
            tile.position = tripoint_abs_ms( x, y, 0 );
            tile.terrain = y < -2 ? "t_water_sh" : "t_grass";
        }
    }
    auto &lilypad = scene.cells[4 * 9 + 4];
    auto &cattails = scene.cells[4 * 9 + 3];
    const auto render = [&]() {
        SDL_SetRenderDrawColor( renderer.get(), 8, 12, 20, 255 );
        REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
        fp3d_compositor::publish( scene );
        fp3d_compositor::compose();
        std::vector<std::uint32_t> pixels( width * height );
        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                       pixels.data(), width * 4 ) == 0 );
        return pixels;
    };
    for( const std::string mode : { "first_person", "overhead" } ) {
        CAPTURE( mode );
        fp3d_compositor::restore_view_mode( mode );
        lilypad.vegetation.clear();
        cattails.vegetation.clear();
        cattails.vegetation_height = 0.f;
        const auto bare = render();
        lilypad.vegetation = "f_lilypad";
        const auto flat = render();
        CHECK( flat != bare );
        // A flat leaf must keep its pixels even if given an upright plant height.
        lilypad.vegetation_height = .85f;
        CHECK( render() == flat );
        lilypad.vegetation_height = 0.f;
        cattails.vegetation = "f_cattails";
        cattails.vegetation_height = .85f;
        auto planted = render();
        CHECK( planted != flat );
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom( planted.data(), width, height,
                                     32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( picture );
            CHECK( IMG_SavePNG( picture.get(), ( std::filesystem::path( directory ) /
                               ( "lilypad-" + mode + ".png" ) ).u8string().c_str() ) == 0 );
        }
    }
}

#endif
