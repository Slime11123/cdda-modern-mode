#include "fp3d_compositor.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "calendar.h"
#include "rng.h"
#include "input_context.h"
#include "input.h"
#include "avatar.h"
#include "cata_imgui.h"
#include "fp3d_modern_ui.h"
#include "fp3d_quick_menu.h"
#include "fp3d_interaction.h"
#include "mission.h"
#include "character_id.h"
#include "npc.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "map_memory.h"
#include "player_helpers.h"
#include "cata_scope_helpers.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "cata_tiles.h"
#include "sdltiles.h"
#include "color.h"
#include "fp3d_materials.h"
#include "fp3d_snapshot.h"
#include "fp3d_scene.h"
#include "fp3d_construction.h"
#include "construction.h"
#include "options.h"
#include "options_helpers.h"
#include "messages.h"
#include "sdl_geometry.h"
#include "sdltiles.h"

// Optional graphics integration check, explicitly selected on a GL-capable host.
// It creates only a hidden test window, never inspects or controls desktop apps.
TEST_CASE( "fp3d_gl_materials_and_billboard_depth", "[.fp3d_gl]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
    } );
    SDL_Window_Ptr window( SDL_CreateWindow( "FP3D hidden graphics regression",
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

    for( const std::string &pack : {
             "UNDEAD_PEOPLE_BASE", "retrodays"
         } ) {
        INFO( "tileset: " << pack );
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
                if( std::abs( x ) <= 1 && y == -2 ) {
                    tile.terrain = "t_wall";
                    tile.kind = fp3d_snapshot::terrain_kind::wall;
                }
                scene.cells.push_back( tile );
            }
        }
        SDL_Color underlay{ 8, 12, 20, 255 };
        const auto render = [&]( bool publish = true ) {
            SDL_SetRenderDrawColor( renderer.get(), underlay.r, underlay.g, underlay.b, 255 );
            REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
            if( publish ) {
                fp3d_compositor::publish( scene );
            }
            fp3d_compositor::compose();
            REQUIRE( fp3d_materials::ready() );
            std::vector<std::uint32_t> pixels( width * height );
            // Bounded diagnostics in this optional test only. The runtime
            // composition path has no readbacks or CPU texture copies.
            REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                           pixels.data(), width * 4 ) == 0 );
            return pixels;
        };
        const auto baseline = render();
        {
            const auto saved_scene = scene;
            fp3d_snapshot::cell near_door;
            near_door.position = tripoint_abs_ms( 0, -1, 0 );
            near_door.terrain = "t_door_c";
            near_door.kind = fp3d_snapshot::terrain_kind::closed_door;
            const auto near_tile = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto &tile ) {
                return tile.position == near_door.position;
            } );
            REQUIRE( near_tile != scene.cells.end() );
            *near_tile = near_door;
            const auto ordinary = render();
            CHECK_FALSE( fp3d_compositor::interaction_available() );
            scene.interactable.push_back( near_door.position );
            const auto highlighted = render();
            CHECK( highlighted != ordinary );
            REQUIRE( fp3d_compositor::interaction_available() );
            CHECK( fp3d_compositor::interaction_prompt().find( "按[" ) == 0 );
            CHECK( fp3d_compositor::interaction_prompt().find( "]互动" ) != std::string::npos );
            {
                restore_on_out_of_scope restore_bindings( inp_mngr );
                auto &bindings = const_cast<std::vector<input_event> &>(
                                     inp_mngr.get_input_for_action( "FP3D_INTERACT", "FP3D" ) );
                bindings = { input_event( 'v', input_event_t::keyboard_code ) };
                CHECK( ( fp3d_compositor::interaction_prompt().find( "v" ) != std::string::npos ||
                         fp3d_compositor::interaction_prompt().find( "V" ) != std::string::npos ) );
            }
            // Native terrain buffer first, original UI controls second, final
            // composition last. The old 2D map sentinel must never show through.
            const auto old_target = SDL_GetRenderTarget( renderer.get() );
            SDL_Texture_Ptr legacy_buffer( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, width, height ) );
            REQUIRE( legacy_buffer );
            REQUIRE( SDL_SetRenderTarget( renderer.get(), legacy_buffer.get() ) == 0 );
            REQUIRE( SDL_SetRenderDrawColor( renderer.get(), 255, 0, 255, 255 ) == 0 );
            REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
            fp3d_compositor::set_input_context( "NATIVE_LEGACY_WINDOW" );
            CHECK( fp3d_compositor::interaction_prompt().empty() );
            REQUIRE( fp3d_compositor::draw_menu_background( 0, 0, width, height ) );
            // Native SDL scaling must keep the frozen view in the same place.
            REQUIRE( SDL_RenderSetLogicalSize( renderer.get(), width / 2, height / 2 ) == 0 );
            REQUIRE( fp3d_compositor::draw_menu_background( 0, 0, width / 2, height / 2 ) );
            REQUIRE( SDL_RenderSetLogicalSize( renderer.get(), 0, 0 ) == 0 );
            SDL_Rect control{ width / 4, height / 4, 40, 40 };
            REQUIRE( SDL_SetRenderDrawColor( renderer.get(), 17, 211, 49, 255 ) == 0 );
            REQUIRE( SDL_RenderFillRect( renderer.get(), &control ) == 0 );
            fp3d_compositor::compose();
            std::vector<std::uint32_t> legacy_pixels( width * height );
            REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                          legacy_pixels.data(), width * 4 ) == 0 );
            const auto pixel_format = SDL_AllocFormat( SDL_PIXELFORMAT_RGBA8888 );
            REQUIRE( pixel_format );
            CHECK( legacy_pixels[7 * width + 7] != SDL_MapRGBA( pixel_format, 255, 0, 255, 255 ) );
            CHECK( legacy_pixels[( control.y + 5 ) * width + control.x + 5] == SDL_MapRGBA( pixel_format, 17, 211, 49, 255 ) );
            SDL_FreeFormat( pixel_format );
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                for( const auto &sample : { std::pair{ "-interaction-reticle.png", &highlighted },
                                           std::pair{ "-native-window-background.png", static_cast<const std::vector<std::uint32_t> *>( &legacy_pixels ) } } ) {
                    SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom(
                                                 const_cast<std::uint32_t *>( sample.second->data() ), width, height, 32, width * 4,
                                                 SDL_PIXELFORMAT_RGBA8888 ) );
                    REQUIRE( picture );
                    CHECK( IMG_SavePNG( picture.get(), ( std::filesystem::path( directory ) /
                                                       ( pack + sample.first ) ).u8string().c_str() ) == 0 );
                }
            }
            REQUIRE( SDL_SetRenderTarget( renderer.get(), old_target ) == 0 );
            fp3d_compositor::set_input_context( "DEFAULTMODE" );
            scene = saved_scene;
            CHECK( render() == baseline );
        }
        fp3d_scene::blueprint ghost{ tripoint_abs_ms( 0, -3, 0 ), 1, true, "test" };
        fp3d_compositor::set_blueprint( ghost );
        CHECK( render() == baseline ); // A preview behind the wall must stay hidden.
        ghost.position = tripoint_abs_ms( 0, -1, 0 );
        fp3d_compositor::set_blueprint( ghost );
        const auto allowed_preview = render();
        CHECK( allowed_preview != baseline );
        ghost.valid = false;
        fp3d_compositor::set_blueprint( ghost );
        CHECK( render() != allowed_preview );
        fp3d_compositor::set_blueprint( std::nullopt );
        CHECK( render() == baseline );
        {
            const auto original_scene = scene;
            for( auto &tile : scene.cells ) {
                tile.kind = fp3d_snapshot::terrain_kind::floor;
                tile.terrain = "t_floor";
            }
            const auto empty_room = render();
            const auto empty_scene = scene;
            for( const std::string id : { "t_floor", "t_wall_wood", "t_door_c", "t_door_o", "t_window" } ) {
                CAPTURE( id );
                construction project{};
                project.post_terrain = id;
                ghost.position = tripoint_abs_ms( 0, -3, 0 );
                ghost.valid = true;
                ghost.terrain = fp3d_construction::terrain_preview( project, ghost.position );
                REQUIRE( ghost.terrain );
                fp3d_compositor::set_blueprint( ghost );
                scene = original_scene;
                CHECK( render() == baseline ); // The completed shape must also stay behind walls.
                scene = empty_scene;
                auto allowed = render();
                CHECK( allowed != empty_room );
                REQUIRE( fp3d_materials::bind() );
                CHECK( fp3d_materials::lookup( id == "t_window" ? "$window:" + id : id ) );
                fp3d_materials::unbind();
                // Compare the aperture area: a glyph-based pack can leave an
                // individual face pixel transparent even on a closed door.
                unsigned aperture_changes = 0;
                for( int py = height / 2 - 16; py < height / 2 + 16; ++py ) {
                    for( int px = width / 2 - 12; px < width / 2 + 12; ++px ) {
                        const size_t pixel = py * width + px;
                        aperture_changes += allowed[pixel] != empty_room[pixel];
                    }
                }
                if( id == "t_door_o" || id == "t_floor" ) {
                    CHECK( aperture_changes == 0 );
                } else if( id != "t_window" ) {
                    CHECK( aperture_changes > 0 );
                }
                ghost.valid = false;
                fp3d_compositor::set_blueprint( ghost );
                CHECK( render() != allowed );
                if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                    std::filesystem::create_directories( directory );
                    SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom(
                                                 allowed.data(), width, height,
                                                 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                    REQUIRE( picture );
                    const auto output = std::filesystem::path( directory ) / ( pack + "-blueprint-" + id + ".png" );
                    CHECK( IMG_SavePNG( picture.get(), output.u8string().c_str() ) == 0 );
                }
                fp3d_compositor::set_blueprint( std::nullopt );
                CHECK( render() == empty_room );
            }
            scene = original_scene;
            CHECK( render() == baseline );
        }
        fp3d_snapshot::sprite zombie;
        zombie.height = 2.16f;
        zombie.id = "mon_zombie";
        zombie.position = tripoint_abs_ms( 0, -3, 0 );
        scene.sprites.push_back( zombie );
        const auto occluded = render();
        unsigned occluded_changes = 0;
        for( size_t i = 0; i < baseline.size(); ++i ) {
            occluded_changes += baseline[i] != occluded[i];
        }
        CHECK( occluded_changes == 0 );
        scene.sprites.front().position = tripoint_abs_ms( 0, -1, 0 );
        auto visible = render();
        unsigned changed = 0;
        for( size_t i = 0; i < visible.size(); ++i ) {
            changed += baseline[i] != visible[i];
        }
        CHECK( changed > 100 );
        // This pixel lies inside the quad but outside the creature silhouette.
        CHECK( visible[height * 3 / 4 * width + 10] == baseline[height * 3 / 4 * width + 10] );
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            std::filesystem::create_directories( directory );
            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                       visible.data(), width, height, 32, width * 4,
                                       SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( image );
            const auto output = std::filesystem::path( directory ) / ( pack + "-billboard.png" );
            CHECK( IMG_SavePNG( image.get(), output.u8string().c_str() ) == 0 );
        }
        if( pack == "UNDEAD_PEOPLE_BASE" ) {
            for( auto &tile : scene.cells ) {
                tile.kind = fp3d_snapshot::terrain_kind::floor;
                tile.terrain = "t_floor";
            }
            fp3d_snapshot::sprite person;
            person.height = 2.16f;
            person.position = tripoint_abs_ms( 0, -3, 0 );
            person.kind = fp3d_snapshot::sprite_kind::character;
            person.id = "npc_male";
            REQUIRE( fp3d_materials::resolve_overlay( *assets, true, "worn_tshirt", "" ) );
            scene.sprites.front() = person;
            const auto bare = render();
            scene.sprites.front().overlays.emplace_back( "worn_tshirt", "" );
            auto dressed = render();
            unsigned outfit_changes = 0;
            for( size_t i = 0; i < bare.size(); ++i ) {
                outfit_changes += bare[i] != dressed[i];
            }
            CHECK( outfit_changes > 25 );
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom( dressed.data(), width,
                                       height, 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                REQUIRE( image );
                const auto output = std::filesystem::path( directory ) / "UNDEAD_PEOPLE_BASE-npc-shirt.png";
                CHECK( IMG_SavePNG( image.get(), output.u8string().c_str() ) == 0 );
            }
        }
        // Exercise the real projection and culling beyond the former 12-cell
        // snapshot, both straight ahead and 45 degrees off the viewing axis.
        scene.sprites.clear();
        for( auto &tile : scene.cells ) {
            tile.kind = fp3d_snapshot::terrain_kind::floor;
            tile.terrain = "t_floor";
        }
        const auto empty_actor = render();
        fp3d_snapshot::sprite ratio_probe;
        ratio_probe.id = "fp3d_missing_square_ratio_probe";
        ratio_probe.position = tripoint_abs_ms( 0, -3, 0 );
        scene.sprites.push_back( ratio_probe );
        const auto square_actor = render();
        int min_x = width;
        int min_y = height;
        int max_x = -1;
        int max_y = -1;
        for( int y = 0; y < height; ++y ) {
            for( int x = 0; x < width; ++x ) {
                if( square_actor[y * width + x] != empty_actor[y * width + x] ) {
                    min_x = std::min( min_x, x );
                    max_x = std::max( max_x, x );
                    min_y = std::min( min_y, y );
                    max_y = std::max( max_y, y );
                }
            }
        }
        // A square diagnostic atlas image must stay square on screen. The
        // former half-width billboard produced an aspect of approximately .5.
        REQUIRE( max_y > min_y );
        const float actor_aspect = static_cast<float>( max_x - min_x + 1 ) / ( max_y - min_y + 1 );
        CHECK( actor_aspect > .9f );
        CHECK( actor_aspect < 1.1f );
        scene.sprites.clear();
        scene.cells.clear();
        for( int y = -60; y <= 1; ++y ) {
            for( int x = -60; x <= 60; ++x ) {
                fp3d_snapshot::cell tile;
                tile.position = tripoint_abs_ms( x, y, 0 );
                tile.terrain = "t_floor";
                scene.cells.push_back( tile );
            }
        }
        // A daylight backdrop keeps dark RetroDays walls distinguishable
        // from empty space even when their texture covers only a few pixels.
        scene.sky = fp3d_sky::sample( calendar::turn_zero + 12_hours, true );
        const auto empty_distance = render();
        for( const tripoint_abs_ms position : {
                 tripoint_abs_ms( 0, -40, 0 ), tripoint_abs_ms( 20, -20, 0 ),
                 tripoint_abs_ms( 15, -10, 0 ), tripoint_abs_ms( 30, -180, 0 )
             } ) {
            INFO( "distant wall: " << position );
            fp3d_snapshot::cell wall;
            wall.position = position;
            wall.terrain = "t_wall";
            wall.kind = fp3d_snapshot::terrain_kind::wall;
            scene.cells.push_back( wall );
            // At 180 units a single tile can be smaller than one screen pixel.
            // A five-tile facade gives a reliable far-plane visibility target.
            const bool far_facade = position.y() < -96;
            if( far_facade ) {
                for( const int dx : {
                         -2, -1, 1, 2
                     } ) {
                    wall.position = position + tripoint_rel_ms( dx, 0, 0 );
                    scene.cells.push_back( wall );
                }
            }
            const auto distant = render();
            unsigned distant_changes = 0;
            for( size_t i = 0; i < distant.size(); ++i ) {
                distant_changes += empty_distance[i] != distant[i];
            }
            CHECK( distant_changes > 5 );
            scene.cells.resize( scene.cells.size() - ( far_facade ? 5 : 1 ) );
        }
        // Actual far landscape beyond both the old 60-tile sampling budget
        // and the old 96-unit clipping plane, using both native tilesets.
        for( int y = -240; y < -60; y += 12 ) {
            for( int x = -240; x < 240; x += 12 ) {
                scene.landscape.push_back( { tripoint_abs_ms( x, y, 0 ), "t_grass", x % 36 == 0 } );
            }
        }
        scene.sky = fp3d_sky::sample( calendar::turn_zero + 12_hours, true );
        auto landscape = render();
        const auto patches = scene.landscape;
        scene.landscape.clear();
        const auto no_landscape = render();
        unsigned landscape_changes = 0;
        for( size_t i = 0; i < landscape.size(); ++i ) {
            landscape_changes += landscape[i] != no_landscape[i];
        }
        CHECK( landscape_changes > 50 );
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                       landscape.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( image );
            const auto output = std::filesystem::path( directory ) / ( pack + "-landscape-240.png" );
            CHECK( IMG_SavePNG( image.get(), output.u8string().c_str() ) == 0 );
        }
        fp3d_snapshot::cell near_wall;
        near_wall.position = tripoint_abs_ms( 0, -1, 0 );
        near_wall.terrain = "t_wall";
        near_wall.kind = fp3d_snapshot::terrain_kind::wall;
        scene.cells.push_back( near_wall );
        const auto occluded_landscape = render();
        scene.landscape = patches;
        const auto behind_wall = render();
        CHECK( behind_wall[height / 2 * width + width / 2] ==
               occluded_landscape[height / 2 * width + width / 2] );
        CHECK( behind_wall[( height / 2 + 10 ) * width + width / 2] ==
               occluded_landscape[( height / 2 + 10 ) * width + width / 2] );
        scene.landscape.clear();
        scene.cells.pop_back(); // Remove the near wall used by the occlusion check.
        const auto city_background = render();
        scene.landscape.push_back( { tripoint_abs_ms( 50, -720, 0 ), "t_pavement", false, 24, 24, 12, false } );
        auto city_pixels = render();
        unsigned city_changes = 0;
        for( size_t i = 0; i < city_pixels.size(); ++i ) {
            city_changes += city_pixels[i] != city_background[i];
        }
        CHECK( city_changes > 3 );
        CHECK( fp3d_scene::statistics().landscape_chunks > 0 );
        CHECK( render( false ) == city_pixels );
        CHECK( fp3d_scene::statistics().indexed_cells == 0 );
        CHECK( fp3d_scene::statistics().landscape_rebuilt == 0 );
        render();
        CHECK( fp3d_scene::statistics().landscape_rebuilt == 0 );
        scene.origin += tripoint_rel_ms::east;
        render();
        CHECK( fp3d_scene::statistics().landscape_rebuilt == 0 );
        scene.landscape.back().building_height += 3;
        city_pixels = render();
        CHECK( fp3d_scene::statistics().landscape_rebuilt == 1 );
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                       city_pixels.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( image );
            CHECK( IMG_SavePNG( image.get(), ( std::filesystem::path( directory ) /
                                               ( pack + "-city-720.png" ) ).u8string().c_str() ) == 0 );
        }
        scene.origin = tripoint_abs_ms::zero;
        scene.landscape.clear();
        scene.sky.visible = false;
        scene.cells.clear();
        for( int y = -12; y <= 1; ++y ) {
            for( int x = -6; x <= 6; ++x ) {
                fp3d_snapshot::cell tile;
                tile.position = tripoint_abs_ms( x, y, 0 );
                tile.terrain = "t_grass";
                scene.cells.push_back( tile );
            }
        }
        const auto bare_ground = render();
        fp3d_snapshot::cell plant;
        plant.position = tripoint_abs_ms( 0, -3, 0 );
        plant.terrain = "t_grass";
        plant.vegetation = "t_shrub";
        plant.vegetation_height = 1.2f;
        scene.cells.push_back( plant );
        const auto planted = render();
        unsigned plant_changes = 0;
        for( size_t i = 0; i < planted.size(); ++i ) {
            plant_changes += planted[i] != bare_ground[i];
        }
        CHECK( plant_changes > 100 );
        fp3d_snapshot::cell wall;
        wall.position = tripoint_abs_ms( 0, -2, 0 );
        wall.kind = fp3d_snapshot::terrain_kind::wall;
        wall.terrain = "t_wall";
        scene.cells.push_back( wall );
        const auto hidden_plant = render();
        scene.cells.erase( scene.cells.end() - 2 );
        const auto wall_only = render();
        CHECK( hidden_plant == wall_only );
        scene.cells.pop_back();
        const auto add_plant = [&]( int x, int y, const std::string & id, float height ) {
            fp3d_snapshot::cell tile;
            tile.position = tripoint_abs_ms( x, y, 0 );
            tile.terrain = "t_grass";
            tile.vegetation = id;
            tile.vegetation_height = height;
            scene.cells.push_back( tile );
        };
        add_plant( -1, -2, "t_grass_long", .65f );
        add_plant( 1, -2, "f_dandelion", .5f );
        add_plant( 0, -4, "t_shrub", 1.2f );
        add_plant( -3, -7, "t_tree", 5 );
        auto vegetation = render();
        // The hidden native 2D map must not bleed through partial-alpha leaves.
        underlay = SDL_Color{ 255, 0, 0, 255 };
        const auto red_underlay = render();
        underlay = SDL_Color{ 0, 0, 255, 255 };
        CHECK( render() == red_underlay );
        underlay = SDL_Color{ 8, 12, 20, 255 };
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                       vegetation.data(), width, height, 32,
                                       width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( image );
            const auto output = std::filesystem::path( directory ) / ( pack + "-crossed-plants.png" );
            CHECK( IMG_SavePNG( image.get(), output.u8string().c_str() ) == 0 );
        }
        // An empty terrain boundary must leave sky, including below the horizon.
        const auto terrain_scene = scene;
        scene.cells.clear();
        scene.sprites.clear();
        for( const auto time : {
                 calendar::turn_zero, calendar::turn_zero + 12_hours
             } ) {
            scene.sky = fp3d_sky::sample( time, true );
            auto background = render();
            CHECK( std::none_of( background.begin(), background.end(), []( std::uint32_t pixel ) {
                return ( pixel & 0xffffff00U ) == 0;
            } ) );
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                           background.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                REQUIRE( image );
                const auto output = std::filesystem::path( directory ) /
                                    ( pack + ( time == calendar::turn_zero ? "-full-sky-night.png" : "-full-sky-day.png" ) );
                CHECK( IMG_SavePNG( image.get(), output.u8string().c_str() ) == 0 );
            }
        }
        scene.sky.visible = false;
        const auto empty_items = render();
        fp3d_snapshot::sprite ground_item;
        ground_item.kind = fp3d_snapshot::sprite_kind::item;
        ground_item.id = "rock";
        ground_item.position = tripoint_abs_ms( 0, -3, 0 );
        unsigned small_area = 0;
        for( const float size : {
                 .3f, .7f
             } ) {
            ground_item.height = size;
            scene.sprites = { ground_item };
            const auto pixels = render();
            unsigned area = 0;
            for( size_t i = 0; i < pixels.size(); ++i ) {
                area += pixels[i] != empty_items[i];
            }
            if( size == .3f ) {
                small_area = area;
                REQUIRE( small_area > 0 );
            } else {
                CHECK( area > small_area * 2 );
            }
        }
        scene = terrain_scene;
        // The sky stays behind terrain and changes only with native game time.
        const auto dark_sky = vegetation;
        for( const std::string &period : {
                 "day", "dusk", "night"
             } ) {
            const auto time = period == "day" ? calendar::turn_zero + 12_hours :
                              period == "dusk" ? sunset( calendar::turn_zero ) : calendar::turn_zero;
            scene.sky = fp3d_sky::sample( time, true );
            auto sky_pixels = render();
            const auto sky_pixel = static_cast<size_t>( width * height / 4 + width / 2 );
            CHECK( sky_pixels[sky_pixel] != dark_sky[sky_pixel] );
            // Sky must not overwrite the existing ground under the camera.
            CHECK( sky_pixels[height * width - width / 2] == dark_sky[height * width - width / 2] );
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                           sky_pixels.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                REQUIRE( image );
                const auto output = std::filesystem::path( directory ) / ( pack + "-sky-" + period + ".png" );
                CHECK( IMG_SavePNG( image.get(), output.u8string().c_str() ) == 0 );
            }
        }
        scene.sky.visible = false;
        CHECK( render() == dark_sky );
        scene.sky = fp3d_sky::sample( calendar::turn_zero + 12_hours, true );
        const auto empty_hands = render();
        fp3d_snapshot::sprite held;
        held.kind = fp3d_snapshot::sprite_kind::held_item;
        for( const std::string &id : {
                 "knife_combat", "glock_19"
             } ) {
            held.id = id;
            REQUIRE( fp3d_materials::resolve_sprite( *assets, held, scene.season ) );
            scene.held_item = held;
            auto equipped = render();
            unsigned changed_pixels = 0;
            unsigned outside_hand_area = 0;
            for( int y = 0; y < height; ++y ) {
                for( int x = 0; x < width; ++x ) {
                    if( equipped[y * width + x] != empty_hands[y * width + x] ) {
                        ++changed_pixels;
                        outside_hand_area += y < height / 2 || x < width / 3;
                    }
                }
            }
            CHECK( changed_pixels > 50 );
            CHECK( outside_hand_area == 0 );
            CHECK( equipped[height / 2 * width + width / 2] ==
                   empty_hands[height / 2 * width + width / 2] );
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                           equipped.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                REQUIRE( image );
                const auto output = std::filesystem::path( directory ) / ( pack + "-held-" + id + ".png" );
                CHECK( IMG_SavePNG( image.get(), output.u8string().c_str() ) == 0 );
            }
        }
        scene.held_item.reset();
        CHECK( render() == empty_hands );
        // Three storeys must extend upwards, with no duplicated floor at Y=0.
        scene.cells.clear();
        scene.sprites.clear();
        scene.sky.visible = false;
        auto previous_storey = render();
        std::vector<double> draw_ms;
        for( int level = 0; level < 3; ++level ) {
            for( int x = -2; x <= 2; ++x ) {
                fp3d_snapshot::cell wall;
                wall.position = tripoint_abs_ms( x, -8, level );
                wall.terrain = "t_wall";
                wall.kind = fp3d_snapshot::terrain_kind::wall;
                scene.cells.push_back( wall );
            }
            const auto started = std::chrono::steady_clock::now();
            auto storeys = render();
            draw_ms.push_back( std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - started ).count() );
            unsigned upper_changes = 0;
            unsigned lower_changes = 0;
            for( int y = 0; y < height; ++y ) {
                for( int x = 0; x < width; ++x ) {
                    if( storeys[y * width + x] != previous_storey[y * width + x] ) {
                        ++upper_changes;
                        if( level > 0 ) {
                            lower_changes += y >= height / 2;
                        }
                    }
                }
            }
            CHECK( upper_changes > 50 );
            CHECK( lower_changes == 0 );
            previous_storey = storeys;
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                std::ofstream timing( std::filesystem::path( directory ) /
                                      ( pack + "-draw-cpu.csv" ), std::ios::app );
                render();
                const auto stats = fp3d_scene::statistics();
                timing << level + 1 << ',' << stats.cpu_ms << ',' << stats.chunks << ',' << stats.rebuilt << '\n';
            }
        }
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                       previous_storey.data(), width, height, 32, width * 4,
                                       SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( image );
            CHECK( IMG_SavePNG( image.get(), ( std::filesystem::path( directory ) /
                                               ( pack + "-three-storeys.png" ) ).u8string().c_str() ) == 0 );
            std::ofstream report( std::filesystem::path( directory ) / ( pack + "-layers-timing.txt" ) );
            for( size_t level = 0; level < draw_ms.size(); ++level ) {
                report << level + 1 << " storeys: render+readback ms=" << draw_ms[level] << '\n';
            }
        }
        // Reusing unchanged geometry must avoid rebuilding display lists.
        CHECK( render() == previous_storey );
        CHECK( fp3d_scene::statistics().rebuilt == 0 );
        scene.cells.front().terrain = "t_door_o";
        scene.cells.front().kind = fp3d_snapshot::terrain_kind::open_door;
        CHECK( render() != previous_storey );
        CHECK( fp3d_scene::statistics().rebuilt > 0 );
        // A known upper floor is a ceiling from below and occludes upper actors.
        scene.cells.clear();
        for( int y = -12; y <= 2; ++y ) {
            for( int x = -8; x <= 8; ++x ) {
                fp3d_snapshot::cell ceiling;
                ceiling.position = tripoint_abs_ms( x, y, 1 );
                ceiling.terrain = "t_floor";
                scene.cells.push_back( ceiling );
            }
        }
        const auto ceiling_only = render();
        zombie.position = tripoint_abs_ms( 0, -4, 1 );
        scene.sprites.push_back( zombie );
        CHECK( render() == ceiling_only );
        for( auto &tile : scene.cells ) {
            tile.floor_present = false;
            tile.open_air = true;
        }
        const auto open_shaft_actor = render();
        scene.sprites.clear();
        const auto open_shaft_empty = render();
        CHECK( open_shaft_actor != open_shaft_empty );
        CHECK( open_shaft_empty != ceiling_only );
        // The visible-room proxy provides the same underside without any
        // upstairs cell or upstairs texture identity in the snapshot.
        scene.cells.clear();
        for( int y = -12; y <= 2; ++y ) {
            for( int x = -8; x <= 8; ++x ) {
                fp3d_snapshot::cell room;
                room.position = tripoint_abs_ms( x, y, 0 );
                room.terrain = "t_floor";
                room.floor_present = false;
                room.ceiling_present = true;
                scene.cells.push_back( room );
            }
        }
        const auto proxy_ceiling = render();
        CHECK( proxy_ceiling != open_shaft_empty );
        scene.sprites.push_back( zombie );
        CHECK( render() == proxy_ceiling );
        scene.sprites.clear();
        // A ramp has no flat upper-floor surface, so keep the neutral room
        // ceiling under that cell rather than leaving a hole in its coverage.
        fp3d_snapshot::cell ramp_above;
        ramp_above.position = tripoint_abs_ms( 0, -3, 1 );
        ramp_above.terrain = "t_floor";
        ramp_above.floor_present = true;
        ramp_above.is_ramp = true;
        scene.cells.push_back( ramp_above );
        const auto ramp_with_ceiling = render();
        scene.cells.front().ceiling_present = false;
        const auto ramp_without_ceiling = render();
        CHECK( ramp_with_ceiling != ramp_without_ceiling );
        scene.cells.front().ceiling_present = true;
        scene.cells.pop_back();
        const auto before_toggle = render();
        SDL_Event toggle{};
        toggle.type = SDL_KEYDOWN;
        toggle.key.keysym.sym = SDLK_F5;
        REQUIRE( fp3d_compositor::process_event( toggle ) );
        const auto original_2d = render();
        CHECK( original_2d != before_toggle );
        REQUIRE( fp3d_compositor::process_event( toggle ) );
        CHECK( render() == before_toggle );
        CHECK( fp3d_compositor::covers_map( scene.origin ) );
        CHECK_FALSE( fp3d_compositor::covers_map( scene.origin + tripoint_rel_ms::east ) );
        fp3d_compositor::set_input_context( "INVENTORY" );
        CHECK_FALSE( fp3d_compositor::covers_map( scene.origin ) );
        CHECK( render() == original_2d );
        fp3d_compositor::set_input_context( "DEFAULTMODE" );
        CHECK( render() == before_toggle );

        // A lower stair never punches through an unrelated solid upper floor.
        scene.cells.clear();
        fp3d_snapshot::cell lower_stair;
        lower_stair.position = tripoint_abs_ms( 0, -3, 0 );
        lower_stair.terrain = "t_floor";
        scene.cells.push_back( lower_stair );
        fp3d_snapshot::cell upper_stair = lower_stair;
        upper_stair.position = tripoint_abs_ms( 0, -3, 1 );
        scene.cells.push_back( upper_stair );
        const auto intact_floor = render();
        scene.cells.front().goes_up = true;
        CHECK( render() == intact_floor );
        scene.cells.back().goes_down = true;
        const auto matched_stairs = render();
        CHECK( matched_stairs != intact_floor );
        CHECK( fp3d_scene::statistics().rebuilt > 0 );
        scene.cells.back().floor_present = false;
        scene.cells.back().open_air = true;
        CHECK( render() != matched_stairs );

        scene.cells.clear();
        fp3d_snapshot::cell ramp;
        ramp.position = tripoint_abs_ms( 0, -3, 0 );
        ramp.terrain = "t_floor";
        ramp.is_ramp = true;
        ramp.ramp_delta = 1;
        scene.cells.push_back( ramp );
        const auto ramp_up = render();
        scene.cells.front().ramp_delta = -1;
        CHECK( render() != ramp_up );

        // Negative layers and a changed player z use the same world mapping.
        scene.cells.clear();
        zombie.position = tripoint_abs_ms( 0, -5, -1 );
        scene.sprites.push_back( zombie );
        const auto basement_actor = render();
        scene.origin = tripoint_abs_ms( 0, 0, -1 );
        const auto downstairs = render();
        CHECK( basement_actor != downstairs );
        scene.origin = tripoint_abs_ms::zero;
        scene.sprites.clear();
        // One window image belongs only inside the aperture, never on the
        // ground, sill or lintel. Use identical terrain IDs to isolate geometry.
        scene.cells.clear();
        scene.sprites.clear();
        scene.sky = fp3d_sky::sample( calendar::turn_zero + 12_hours, true );
        for( int x : {
                 -1, 1
             } ) {
            fp3d_snapshot::cell side;
            side.position = tripoint_abs_ms( x, -2, 0 );
            side.terrain = "t_wall";
            side.kind = fp3d_snapshot::terrain_kind::wall;
            scene.cells.push_back( side );
        }
        fp3d_snapshot::cell window_tile;
        window_tile.position = tripoint_abs_ms( 0, -2, 0 );
        window_tile.terrain = "t_window_domestic";
        window_tile.kind = fp3d_snapshot::terrain_kind::window;
        window_tile.pane = fp3d_snapshot::window_pane::opening;
        scene.cells.push_back( window_tile );
        const auto open_window = render();
        scene.cells.back().pane = fp3d_snapshot::window_pane::glazed;
        auto closed_window = render();
        const float scale = width / ( 4 * fp3d_camera::horizontal_tangent() );
        unsigned window_changes = 0;
        unsigned outside_aperture = 0;
        for( int y = 0; y < height; ++y ) {
            for( int x = 0; x < width; ++x ) {
                if( closed_window[y * width + x] != open_window[y * width + x] ) {
                    ++window_changes;
                    outside_aperture += std::abs( x - width / 2 ) > .42f * scale + 2 ||
                                        y < height / 2 - .5f * scale - 2 ||
                                        y > height / 2 + .7f * scale + 2;
                }
            }
        }
        CHECK( window_changes > 50 );
        CHECK( outside_aperture == 0 );
        zombie.position = tripoint_abs_ms( 0, -3, 0 );
        scene.sprites.push_back( zombie );
        auto through_glass = render();
        CHECK( through_glass != closed_window );
        scene.cells.back().pane = fp3d_snapshot::window_pane::covered;
        const auto covered_actor = render();
        scene.sprites.clear();
        CHECK( render() == covered_actor );
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                       closed_window.data(), width, height, 32, width * 4,
                                       SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( image );
            CHECK( IMG_SavePNG( image.get(), ( std::filesystem::path( directory ) /
                                               ( pack + "-window.png" ) ).u8string().c_str() ) == 0 );
        }
        scene.sky.visible = false;

        // A new atlas slot must not invalidate every previously cached chunk.
        scene.cells.clear();
        for( int x : {
                 -8, 0, 8
             } ) {
            fp3d_snapshot::cell tile;
            tile.position = tripoint_abs_ms( x, -10, 0 );
            tile.terrain = "t_floor";
            scene.cells.push_back( tile );
        }
        render();
        render();
        CHECK( fp3d_scene::statistics().rebuilt == 0 );
        REQUIRE( fp3d_scene::statistics().chunks >= 3 );
        const unsigned previous_revision = fp3d_materials::revision();
        fp3d_snapshot::cell new_material;
        new_material.position = tripoint_abs_ms( 24, -24, 0 );
        new_material.terrain = "t_pavement";
        scene.cells.push_back( new_material );
        render();
        CHECK( fp3d_materials::revision() == previous_revision );
        CHECK( fp3d_scene::statistics().rebuilt == 1 );
        scene.season = WINTER;
        render();
        CHECK( fp3d_materials::revision() != previous_revision );
        CHECK( fp3d_scene::statistics().rebuilt == fp3d_scene::statistics().chunks );
        scene.season = SPRING;

        // A bounded hidden-window benchmark, not a claim about live game FPS.
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            std::ofstream report( std::filesystem::path( directory ) / ( pack + "-benchmark.csv" ) );
            report << "storeys,cells,cold_draw_cpu_ms,warm_draw_cpu_ms,render_readback_present_fps\n";
            for( int storeys : {
                     1, 3, 5
                 } ) {
                scene.cells.clear();
                for( int y = -60; y <= 60; ++y ) {
                    for( int x = -60; x <= 60; ++x ) {
                        fp3d_snapshot::cell ground;
                        ground.position = tripoint_abs_ms( x, y, 0 );
                        ground.terrain = "t_grass";
                        scene.cells.push_back( ground );
                    }
                }
                for( int z = 0; z < storeys; ++z ) {
                    for( int x = -8; x <= 8; ++x ) {
                        fp3d_snapshot::cell facade;
                        facade.position = tripoint_abs_ms( x, -12, z );
                        facade.kind = fp3d_snapshot::terrain_kind::wall;
                        facade.terrain = "t_wall";
                        scene.cells.push_back( facade );
                    }
                }
                fp3d_scene::release();
                render();
                const double cold_ms = fp3d_scene::statistics().cpu_ms;
                render();
                REQUIRE( fp3d_scene::statistics().rebuilt == 0 );
                double draw_total = 0;
                const auto started = std::chrono::steady_clock::now();
                for( int frame = 0; frame < 30; ++frame ) {
                    render( false );
                    CHECK( fp3d_scene::statistics().indexed_cells == 0 );
                    draw_total += fp3d_scene::statistics().cpu_ms;
                    SDL_RenderPresent( renderer.get() );
                }
                const double seconds = std::chrono::duration<double>(
                                           std::chrono::steady_clock::now() - started ).count();
                report << storeys << ',' << scene.cells.size() << ',' << cold_ms << ','
                       << draw_total / 30 << ',' << 30 / seconds << '\n';
            }
        }

        // Exercise the native draw/memory pass with the opaque-map shortcut,
        // rather than only testing a synthetic compositor underlay.
        clear_avatar();
        clear_map();
        set_time_to_day();
        avatar &you = get_avatar();
        map &here = get_map();
        you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
        const auto center = you.pos_bub( here );
        const auto wall_pos = center + tripoint_rel_ms( 3, 0, 0 );
        const auto furniture_pos = center + tripoint_rel_ms( -3, 0, 0 );
        here.ter_set( wall_pos, ter_str_id( "t_wall" ) );
        here.furn_set( furniture_pos, furn_str_id( "f_table" ) );
        fp3d_snapshot::prepare_view( you, here );
        cata_tiles native_tiles( renderer, geometry, cache );
        native_tiles.load_tileset( pack );
        fp3d_compositor::publish( fp3d_snapshot::capture( you, here ) );
        std::multimap<point, formatted_text> text;
        color_block_overlay_container blocks;
        fp3d_compositor::set_input_context( "INVENTORY" );
        native_tiles.draw( point::zero, center, width, height, text, blocks );
        const auto wall_memory = you.get_memorized_tile( here.get_abs( wall_pos ) ).get_ter_id();
        const auto furniture_memory = you.get_memorized_tile( here.get_abs( furniture_pos ) ).get_dec_id();
        REQUIRE( wall_memory == "t_wall" );
        REQUIRE( furniture_memory == "f_table" );
        you.memorize_terrain( here.get_abs( wall_pos ), "", 0, 0 );
        you.memorize_clear_decoration( here.get_abs( furniture_pos ), "" );
        here.memory_cache_ter_set_dirty( wall_pos, true );
        here.memory_cache_dec_set_dirty( furniture_pos, true );
        fp3d_compositor::set_input_context( "DEFAULTMODE" );
        REQUIRE( fp3d_compositor::covers_map( you.pos_abs() ) );
        native_tiles.draw( point::zero, center, width, height, text, blocks );
        CHECK( you.get_memorized_tile( here.get_abs( wall_pos ) ).get_ter_id() == wall_memory );
        CHECK( you.get_memorized_tile( here.get_abs( furniture_pos ) ).get_dec_id() == furniture_memory );

        // Draw the same production panel in its native ImGui/SDL backend.
        // This hidden test window owns the entire context, with no desktop input.
        item pack_item( itype_id( "backpack" ) );
        pack_item.set_var( "name",
                           "急救与生存装备·大容量背包（中文长名称与嵌套口袋验证）" );
        you.wear_item( pack_item, false );
        item knife( itype_id( "knife_combat" ) );
        knife.set_var( "name", "外出探险常备的多用途战斗匕首（中文长名称验证）" );
        you.i_add( knife );
        for( int i = 0; i < 256; ++i ) {
            item bandage( itype_id( "bandages" ), calendar::turn, 1 );
            bandage.set_var( "name", "急救备用绷带·独立物品 " + std::to_string( i ) );
            you.i_add( bandage );
        }
        you.add_effect( efftype_id( "hunger_very_hungry" ), 1_hours );
        you.add_effect( efftype_id( "bleed" ), 1_hours, bodypart_str_id( "arm_l" ).id() );
        you.set_part_hp_cur( bodypart_str_id( "arm_l" ).id(), 20 );
        you.set_skill_level( skill_id( "firstaid" ), 3 );
        here.add_item_or_charges( center + tripoint_rel_ms::north, item( itype_id( "rock" ) ) );
        you.reset_all_missions();
        mission::clear_all();
        mission *task = mission::reserve_new( mission_type_id( "TEST_MISSION_GOAL_CONDITION1" ),
                                              character_id() );
        REQUIRE( task );
        task->set_assigned_player_id( character_id( -2 ) );
        task->assign( you );
        task->set_target( you.pos_abs_omt() + tripoint( 5, 2, 0 ) );
        spawn_npc( ( center + tripoint_rel_ms::west ).xy(), "test_talker" );
        here.build_map_cache( 0 );
        here.update_visibility_cache( 0 );
        on_out_of_scope journal_cleanup( [&]() {
            clear_npcs();
            you.reset_all_missions();
            mission::clear_all();
        } );
        struct saved_map_cell {
            tripoint_abs_omt position;
            oter_id terrain;
            om_vision_level vision;
        };
        std::vector<saved_map_cell> original_map;
        const auto note_position = you.pos_abs_omt() + tripoint_rel_omt( 2, -2, 0 );
        const bool had_note = overmap_buffer.has_note( note_position );
        const std::string old_note = overmap_buffer.note( note_position );
        const auto original_route = you.omt_path;
        on_out_of_scope map_cleanup( [&]() {
            for( const auto &row : original_map ) {
                overmap_buffer.ter_set( row.position, row.terrain );
                if( const auto om = overmap_buffer.get_existing_om_global( row.position ) ) {
                    om.om->set_seen( om.local, row.vision, true );
                }
            }
            if( had_note ) {
                overmap_buffer.add_note( note_position, old_note );
            } else {
                overmap_buffer.delete_note( note_position );
            }
            you.omt_path = original_route;
        } );
        for( int y = -8; y <= 8; ++y ) {
            for( int x = -8; x <= 8; ++x ) {
                const auto p = you.pos_abs_omt() + tripoint_rel_omt( x, y, 0 );
                original_map.push_back( { p, overmap_buffer.ter( p ), overmap_buffer.seen( p ) } );
                overmap_buffer.ter_set( p, oter_str_id( y == 0 ? "road_ew" : x == 0 ? "road_ns" :
                                                        y < -3 ? "forest" : "field" ).id() );
                overmap_buffer.set_seen( p, om_vision_level::full );
            }
        }
        overmap_buffer.add_note( note_position,
                                 "临时营地：饮水与物资储备（中文长标记）" );
        you.omt_path.clear();
        for( int x = 5; x >= 1; --x ) {
            you.omt_path.push_back( you.pos_abs_omt() + tripoint_rel_omt( x, 0, 0 ) );
        }
        override_option message_limit( "MESSAGE_LIMIT", "1000" );
        Messages::clear_messages();
        on_out_of_scope messages_cleanup( []() {
            Messages::clear_messages();
        } );
        for( int n = 0; n < 256; ++n ) {
            Messages::add_msg( "长日志验证：探索时发现的物资与附近环境，需要在完整详情中阅读。\n第二行包含中文、## 和保留的原版消息内容。记录 "
                               +
                               std::to_string( n ) );
        }
        REQUIRE( Messages::size() == 256 );
        color_loader<SDL_Color>().load( windowsPalette );
        init_colors();
        ImGui::CreateContext();
        REQUIRE( ImGui_ImplSDL2_InitForSDLRenderer( window.get(), renderer.get() ) );
        REQUIRE( ImGui_ImplSDLRenderer2_Init( renderer.get() ) );
        on_out_of_scope gui_cleanup( [&]() {
            ImGui_ImplSDLRenderer2_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            SDL_SetWindowSize( window.get(), width, height );
        } );
        ImGui::GetIO().IniFilename = nullptr;
        REQUIRE( ImGui::GetIO().Fonts->AddFontFromFileTTF( "data/font/unifont.ttf", 18, nullptr,
                 ImGui::GetIO().Fonts->GetGlyphRangesChineseFull() ) );
        // Exercise a real focused ImGui text field and the native SDL backend,
        // including committed UTF-8. No desktop keyboard or IME is controlled.
        {
            const auto text_time = calendar::turn;
            const auto text_moves = you.get_moves();
            const auto text_rng = rng_get_engine();
            char query[64] = {};
            fp3d_compositor::set_input_context( "FP3D_PANEL" );
            REQUIRE( fp3d_compositor::turn_based() );
            for( int frame = 0; frame < 10; ++frame ) {
                ImGui_ImplSDLRenderer2_NewFrame();
                ImGui_ImplSDL2_NewFrame();
                if( frame == 4 ) {
                    SDL_Event commit{};
                    commit.type = SDL_TEXTINPUT;
                    commit.text.windowID = SDL_GetWindowID( window.get() );
                    SDL_strlcpy( commit.text.text, "口袋", sizeof( commit.text.text ) );
                    REQUIRE( ImGui_ImplSDL2_ProcessEvent( &commit ) );
                }
                ImGui::NewFrame();
                ImGui::SetNextWindowPos( ImVec2( 0, 0 ) );
                ImGui::SetNextWindowSize( ImVec2( 320, 100 ) );
                ImGui::Begin( "文字输入回归", nullptr, ImGuiWindowFlags_NoSavedSettings );
                if( frame == 0 ) { ImGui::SetKeyboardFocusHere(); }
                if( frame == 7 ) { ImGui::ClearActiveID(); }
                ImGui::InputText( "##query", query, sizeof( query ) );
                ImGui::End();
                if( frame == 6 ) {
                    REQUIRE( ImGui::GetIO().WantTextInput );
                    CHECK( fp3d_compositor::menu_text_input() );
                    CHECK( std::string( query ) == "口袋" );
                    fp3d_compositor::set_input_context( "DEFAULTMODE" );
                    CHECK_FALSE( fp3d_compositor::menu_text_input() ); // Ignore stale field focus in the world.
                    fp3d_compositor::set_input_context( "FP3D_WHEEL" );
                    CHECK_FALSE( fp3d_compositor::menu_text_input() );
                    fp3d_compositor::set_input_context( "FP3D_PANEL" );
                    CHECK( fp3d_compositor::menu_text_input() );
                }
                if( frame == 9 ) {
                    CHECK_FALSE( ImGui::GetIO().WantTextInput );
                    CHECK_FALSE( fp3d_compositor::menu_text_input() );
                }
                ImGui::Render();
                ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
            }
            fp3d_compositor::set_input_context( "DEFAULTMODE" );
            CHECK( calendar::turn == text_time );
            CHECK( you.get_moves() == text_moves );
            CHECK( rng_get_engine() == text_rng );
        }
        std::ofstream ui_report;
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            ui_report.open( std::filesystem::path( directory ) / ( pack + "-panel-performance.csv" ) );
            ui_report << "width,height,scale,page,create_ms,mean_frame_ms,vertices\n";
        }
        for( const auto &layout : {
                 std::array{ 1920, 1080, 100 }, std::array{ 1280, 720, 100 },
                 std::array{ 1920, 1080, 150 }, std::array{ 1280, 720, 150 }
             } ) {
            SDL_SetWindowSize( window.get(), layout[0], layout[1] );
            SDL_RenderSetViewport( renderer.get(), nullptr );
            const auto before_time = calendar::turn;
            const auto before_moves = you.get_moves();
            you.set_wielded_item( item( itype_id( "atomic_lamp" ) ) );
            const auto before_rng = rng_get_engine();
            const auto before_messages = Messages::size();
            const bool before_unread = Messages::has_undisplayed_messages();
            for( int page = 0; page < 10; ++page ) {
                const auto create_started = std::chrono::steady_clock::now();
                auto menu = fp3d_modern_ui::create( you, here, layout[2], page == 1,
                                                    page == 9 ? 0 : page == 8 ? 7 : page == 7 ? 4 : page == 6 ? 3 : page >= 4 ? page + 1 : page >= 2 ? page - 1 : 0,
                                                    page == 9 ? you.get_wielded_item() : item_location() );
                const double create_ms = std::chrono::duration<double, std::milli>(
                                             std::chrono::steady_clock::now() - create_started ).count();
                const auto frame_started = std::chrono::steady_clock::now();
                for( int frame = 0; frame < 30; ++frame ) {
                    ImGui_ImplSDLRenderer2_NewFrame();
                    ImGui_ImplSDL2_NewFrame();
                    // Drop into blank space to the right of a nearby source's item grid.
                    // This exercises production hit testing, rather than a dedicated drop button.
                    const bool source_drop = page == 1 && layout[2] == 100 && frame >= 10 && frame <= 11;
                    if( source_drop ) {
                        ImGuiWindow *near_window = nullptr;
                        for( ImGuiWindow *candidate : GImGui->Windows ) {
                            if( candidate->WasActive && std::string( candidate->Name ).find( "/附近物品_" ) != std::string::npos ) {
                                near_window = candidate;
                            }
                        }
                        if( !near_window ) {
                            // Narrow layouts show nearby sources inside the selected middle tab.
                            for( ImGuiWindow *candidate : GImGui->Windows ) {
                                if( candidate->WasActive && std::string( candidate->Name ).find( "/我的物品_" ) != std::string::npos ) {
                                    near_window = candidate;
                                }
                            }
                        }
                        REQUIRE( near_window );
                        const float target_y = near_window->DC.CursorStartPos.y +
                                               2 * ImGui::GetTextLineHeightWithSpacing() +
                                               5 * ImGui::GetFrameHeightWithSpacing() + 20;
                        ImGui::GetIO().AddMousePosEvent( near_window->Pos.x + near_window->Size.x - 30, target_y );
                        ImGui::GetIO().AddMouseButtonEvent( 0, false );
                    }
                    ImGui::NewFrame();
                    if( source_drop && frame == 10 && ImGui::BeginDragDropSource( ImGuiDragDropFlags_SourceExtern ) ) {
                        const size_t item_index = 0; // The native, wielded atomic lamp.
                        ImGui::SetDragDropPayload( "FP3D_ITEM", &item_index, sizeof( item_index ) );
                        ImGui::EndDragDropSource();
                    }
                    const std::string panel_name = "生存面板##" + std::to_string( reinterpret_cast<uint64_t>( menu.get() ) );
                    if( page == 0 && frame == 20 ) {
                        ImGui::Begin( panel_name.c_str() );
                        ImGui::OpenPopup( "层级验证" );
                        if( ImGui::BeginPopup( "层级验证" ) ) {
                            ImGui::TextUnformatted( "操作菜单" );
                            ImGui::EndPopup();
                        }
                        ImGui::End();
                    }
                    if( source_drop ) {
                        REQUIRE( GImGui->DragDropActive );
                    }
                    if( page == 7 && layout[0] == 1280 && frame == 24 ) {
                        for( ImGuiWindow *candidate : GImGui->Windows ) {
                            if( candidate->WasActive && std::string( candidate->Name ).find( "/地图标记_" ) != std::string::npos ) {
                                ImGui::SetScrollY( candidate, candidate->ScrollMax.y );
                            }
                        }
                    }
                    menu->draw();
                    if( source_drop ) {
                        CHECK( menu->get_is_open() );
                        if( frame == 10 ) {
                            CHECK( GImGui->DragDropAcceptIdCurr != 0 );
                        } else {
                            // EndDragDropTarget clears the payload only after delivery.
                            CHECK_FALSE( GImGui->DragDropActive );
                        }
                    }
                    if( page == 0 && frame == 20 ) {
                        REQUIRE( !GImGui->OpenPopupStack.empty() );
                        ImGuiWindow *popup = GImGui->OpenPopupStack.back().Window;
                        REQUIRE( popup );
                        const auto root_order = std::find( GImGui->Windows.begin(), GImGui->Windows.end(),
                                                         ImGui::FindWindowByName( panel_name.c_str() ) );
                        const auto popup_order = std::find( GImGui->Windows.begin(), GImGui->Windows.end(), popup );
                        CHECK( popup_order > root_order );
                    }
                    ImGui::Render();
                    SDL_SetRenderDrawColor( renderer.get(), 12, 16, 20, 255 );
                    SDL_RenderClear( renderer.get() );
                    ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
                }
                const double mean_frame_ms = std::chrono::duration<double, std::milli>(
                                                 std::chrono::steady_clock::now() - frame_started ).count() / 30;
                ui_report << layout[0] << ',' << layout[1] << ',' << layout[2] << ',' << page << ',' << create_ms <<
                ','
                          << mean_frame_ms << ',' << ImGui::GetDrawData()->TotalVtxCount << '\n';
                CHECK( calendar::turn == before_time );
                CHECK( you.get_moves() == before_moves );
                CHECK( rng_get_engine() == before_rng );
                if( page == 8 ) {
                    CHECK( Messages::size() == before_messages );
                    CHECK( Messages::has_undisplayed_messages() == before_unread );
                }
                REQUIRE( ImGui::GetDrawData()->TotalVtxCount > 100 );
                CHECK( ImGui::GetDrawData()->TotalVtxCount < 20000 );
                if( page == 3 ) {
                    ImGuiWindow *categories = nullptr;
                    ImGuiWindow *grid = nullptr;
                    ImGuiWindow *details = nullptr;
                    for( ImGuiWindow *child : GImGui->Windows ) {
                        if( !child->Active ) {
                            continue;
                        }
                        const std::string name = child->Name;
                        if( name.find( "/配方分类_" ) != std::string::npos ) {
                            categories = child;
                        } else if( name.find( "/配方_" ) != std::string::npos ) {
                            grid = child;
                        } else if( name.find( "/配方详情_" ) != std::string::npos ) {
                            details = child;
                        }
                    }
                    REQUIRE( grid );
                    REQUIRE( details );
                    CHECK( grid->Pos.x + grid->Size.x <= details->Pos.x );
                    CHECK( details->Pos.x + details->Size.x <= layout[0] );
                    CHECK( grid->Pos.y + grid->Size.y <= layout[1] );
                    if( layout[0] == 1280 && layout[2] == 150 ) {
                        CHECK_FALSE( categories ); // Compact category selector stays in the grid column.
                    } else {
                        REQUIRE( categories );
                        CHECK( categories->Pos.x + categories->Size.x <= grid->Pos.x );
                        CHECK( categories->Size.x >= 130 * layout[2] / 100.0f );
                    }
                }
                if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                    std::vector<std::uint32_t> pixels( layout[0] * layout[1] );
                    REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                   pixels.data(), layout[0] * 4 ) == 0 );
                    if( page == 7 ) {
                        // The known forest must be visible in the actual SDL output,
                        // rather than turning black through the text backend's palette.
                        size_t forest_pixels = 0;
                        for( int y = layout[1] / 4; y < layout[1] / 2; ++y ) {
                            for( int x = layout[0] * 2 / 5; x < layout[0] * 7 / 10; ++x ) {
                                const auto pixel = pixels[y * layout[0] + x];
                                const int red = pixel >> 24;
                                const int green = ( pixel >> 16 ) & 255;
                                const int blue = ( pixel >> 8 ) & 255;
                                forest_pixels += green > 20 && green > red * 3 / 2 && green > blue * 3 / 2;
                            }
                        }
                        CHECK( forest_pixels > 50 );
                    }
                    SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(), layout[0],
                                             layout[1],
                                             32, layout[0] * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                    REQUIRE( picture );
                    const auto output = std::filesystem::path( directory ) /
                                        ( pack + ( page == 0 ? "-panel-" : page == 1 ? "-near-" : page == 2 ? "-health-" : page == 3 ?
                                                   "-craft-" : page == 4 ? "-missions-" : page == 5 ? "-people-" : page == 6 ? "-building-" : page == 7
                                                   ? "-map-" : page == 8 ? "-more-" : "-use-" )
                                          +
                                          std::to_string(
                                              layout[0] ) + "-" + std::to_string( layout[2] ) + ".png" );
                    CHECK( IMG_SavePNG( picture.get(), output.u8string().c_str() ) == 0 );
                }
            }
            // The radial menu uses the same native ImGui backend and preserves
            // the map viewport instead of stretching the frozen 3D scene.
            {
                const auto old_viewport = fp3d_compositor::viewport_pixels();
                fp3d_compositor::set_viewport( 0, 0, layout[0] * 3 / 4, layout[1], layout[0], layout[1] );
                on_out_of_scope restore_viewport( [&]() {
                    fp3d_compositor::set_viewport( old_viewport[0], old_viewport[1], old_viewport[2],
                                                  old_viewport[3], layout[0], layout[1] );
                } );
                const auto wheel_position = you.pos_abs();
                const int wheel_hp = you.get_hp();
                const int wheel_stamina = you.get_stamina();
                const int wheel_hunger = you.get_hunger();
                const int wheel_thirst = you.get_thirst();
                auto radial = fp3d_quick_menu::create( layout[2] );
                const auto wheel_area = fp3d_compositor::viewport_pixels();
                const ImVec2 center( wheel_area[0] + wheel_area[2] * .5f,
                                     wheel_area[1] + wheel_area[3] * .5f );
                for( int frame = 0; frame < 30; ++frame ) {
                    ImGui_ImplSDLRenderer2_NewFrame();
                    ImGui_ImplSDL2_NewFrame();
                    ImGui::GetIO().AddMousePosEvent( center.x - 160, center.y );
                    ImGui::GetIO().AddMouseButtonEvent( 0, frame == 28 );
                    ImGui::NewFrame();
                    radial->draw();
                    if( frame == 27 ) { CHECK( radial->get_is_open() ); }
                    if( frame == 28 ) {
                        CHECK_FALSE( radial->get_is_open() );
                        CHECK( radial->get_button_action() == "FP3D_PANEL" );
                    }
                    ImGui::Render();
                    SDL_SetRenderDrawColor( renderer.get(), 12, 16, 20, 255 );
                    SDL_RenderClear( renderer.get() );
                    ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
                    if( frame == 8 ) {
                        REQUIRE( ImGui::GetDrawData()->TotalVtxCount > 100 );
                        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                            std::vector<std::uint32_t> pixels( layout[0] * layout[1] );
                            REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                          pixels.data(), layout[0] * 4 ) == 0 );
                            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(), layout[0],
                                                   layout[1], 32, layout[0] * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                            REQUIRE( image );
                            const auto filename = std::filesystem::path( directory ) /
                                                  ( pack + "-wheel-" + std::to_string( layout[0] ) + "-" +
                                                    std::to_string( layout[2] ) + ".png" );
                            CHECK( IMG_SavePNG( image.get(), filename.u8string().c_str() ) == 0 );
                        }
                    }
                }
                const fp3d_targeting::target context_target{ you.pos_abs(),
                    here.get_abs( you.pos_bub( here ) + tripoint_rel_ms::west ),
                    fp3d_targeting::kind::creature, {} };
                const auto context_choices = fp3d_interaction::context_entries( context_target, you, here );
                REQUIRE( context_choices[2].enabled );
                REQUIRE_FALSE( context_choices[5].enabled );
                auto contextual = fp3d_quick_menu::create( context_choices, "目标操作", layout[2] );
                for( int frame = 0; frame < 9; ++frame ) {
                    ImGui_ImplSDLRenderer2_NewFrame();
                    ImGui_ImplSDL2_NewFrame();
                    ImGui::GetIO().AddMousePosEvent( frame < 7 ? center.x + 160 : center.x,
                                                   frame < 7 ? center.y : center.y - 160 );
                    ImGui::GetIO().AddMouseButtonEvent( 0, frame == 4 || frame == 7 );
                    ImGui::NewFrame();
                    contextual->draw();
                    if( frame == 4 ) {
                        CHECK( contextual->get_is_open() );
                        CHECK_FALSE( contextual->has_button_action() );
                    }
                    if( frame == 7 ) {
                        CHECK_FALSE( contextual->get_is_open() );
                        CHECK( contextual->get_button_action() == action_ident( ACTION_CHAT ) );
                    }
                    ImGui::Render();
                    SDL_SetRenderDrawColor( renderer.get(), 12, 16, 20, 255 );
                    SDL_RenderClear( renderer.get() );
                    ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
                    if( frame == 2 ) {
                        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                            std::vector<std::uint32_t> pixels( layout[0] * layout[1] );
                            REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                          pixels.data(), layout[0] * 4 ) == 0 );
                            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(), layout[0],
                                                   layout[1], 32, layout[0] * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                            REQUIRE( image );
                            const auto filename = std::filesystem::path( directory ) /
                                                  ( pack + "-context-" + std::to_string( layout[0] ) + "-" +
                                                    std::to_string( layout[2] ) + ".png" );
                            CHECK( IMG_SavePNG( image.get(), filename.u8string().c_str() ) == 0 );
                        }
                    }
                }
                auto context_cancel = fp3d_quick_menu::create( context_choices, "目标操作", layout[2] );
                for( int frame = 0; frame < 3; ++frame ) {
                    ImGui_ImplSDLRenderer2_NewFrame();
                    ImGui_ImplSDL2_NewFrame();
                    ImGui::GetIO().AddMousePosEvent( center.x - 160, center.y );
                    ImGui::GetIO().AddMouseButtonEvent( 0, frame == 1 );
                    ImGui::NewFrame();
                    context_cancel->draw();
                    ImGui::Render();
                }
                CHECK_FALSE( context_cancel->get_is_open() );
                CHECK_FALSE( context_cancel->has_button_action() );
                auto cancelled = fp3d_quick_menu::create( layout[2] );
                for( int frame = 0; frame < 3; ++frame ) {
                    ImGui_ImplSDLRenderer2_NewFrame();
                    ImGui_ImplSDL2_NewFrame();
                    ImGui::GetIO().AddMousePosEvent( center.x, center.y );
                    ImGui::GetIO().AddMouseButtonEvent( 1, frame == 1 );
                    ImGui::NewFrame();
                    cancelled->draw();
                    ImGui::Render();
                }
                CHECK_FALSE( cancelled->get_is_open() );
                CHECK_FALSE( cancelled->has_button_action() );
                CHECK( calendar::turn == before_time );
                CHECK( you.get_moves() == before_moves );
                CHECK( you.pos_abs() == wheel_position );
                CHECK( you.get_hp() == wheel_hp );
                CHECK( you.get_stamina() == wheel_stamina );
                CHECK( you.get_hunger() == wheel_hunger );
                CHECK( you.get_thirst() == wheel_thirst );
                CHECK( rng_get_engine() == before_rng );
            }
        }


    }
}
#endif
