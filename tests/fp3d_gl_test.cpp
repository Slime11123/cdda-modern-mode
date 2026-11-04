#include "fp3d_compositor.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
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
#include "fp3d_dialogue.h"
#include "fp3d_trade.h"
#include "fp3d_quick_menu.h"
#include "fp3d_interaction.h"
#include "mission.h"
#include "character_id.h"
#include "npc.h"
#include "npc_opinion.h"
#include "npctrade.h"
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
#include "fp3d_entry.h"
#include "sdl_font.h"
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
            const auto white_pixels = []( const auto &pixels ) {
                return std::count( pixels.begin(), pixels.end(), 0xffffffffu );
            };
            const auto wall = fp3d_compositor::interaction_target();
            REQUIRE( wall );
            CHECK( wall->id == "t_wall" );
            CHECK( white_pixels( baseline ) > 20 );

            fp3d_snapshot::sprite item;
            item.kind = fp3d_snapshot::sprite_kind::item;
            item.id = "rock";
            item.height = 1.8f;
            item.position = tripoint_abs_ms( 0, -3, 0 );
            scene.sprites.push_back( item );
            CHECK( render() == baseline );

            scene.sprites.back().position = tripoint_abs_ms( 0, -1, 0 );
            CHECK( white_pixels( render() ) > 20 );
            const auto sprite = fp3d_compositor::interaction_target();
            REQUIRE( sprite );
            CHECK( sprite->type == fp3d_targeting::kind::item );
            CHECK( sprite->id == "rock" );
            scene.sprites.clear();
        }
        if( pack == "UNDEAD_PEOPLE_BASE" ) {
            // Exercise the actual SDL text/opacity path and renderer state,
            // including expiry, against a rendered world in a hidden window.
            clear_map();
            clear_avatar();
            avatar &you = get_avatar();
            you.name = "JOSHUA MCCORKLE";
            you.custom_profession = "伐木工";
            REQUIRE( TTF_Init() == 0 );
            {
                restore_on_out_of_scope restore_palette( windowsPalette );
                color_loader<SDL_Color>().load( windowsPalette );
                const std::string typeface = std::filesystem::exists( "C:/Windows/Fonts/msyh.ttc" ) ?
                                             "C:/Windows/Fonts/msyh.ttc" : "data/font/unifont.ttf";
                CachedTTFFont entry_font( 12, 24, windowsPalette, typeface, 20, true );
                for( const auto &size : { point( 1280, 720 ), point( 1920, 1080 ) } ) {
                    SDL_SetWindowSize( window.get(), size.x, size.y );
                    REQUIRE( SDL_GetRendererOutputSize( renderer.get(), &width, &height ) == 0 );
                    fp3d_compositor::set_viewport( 0, 0, width, height, width, height );
                    const auto empty = render();
                    fp3d_entry::begin( you );
                    const Uint32 started = SDL_GetTicks();
                    for( const int elapsed : { 0, 450, 1500, 5000, 5750, 6550 } ) {
                        const Uint32 now = SDL_GetTicks() - started;
                        if( now < static_cast<Uint32>( elapsed ) ) {
                            SDL_Delay( elapsed - now );
                        }
                        render( false );
                        const SDL_Rect saved_clip{ 3, 4, width - 10, height - 12 };
                        REQUIRE( SDL_RenderSetClipRect( renderer.get(), &saved_clip ) == 0 );
                        REQUIRE( SDL_SetRenderDrawColor( renderer.get(), 17, 31, 47, 101 ) == 0 );
                        fp3d_entry::draw( renderer, geometry, entry_font, { 0, 0, width, height }, true );
                        SDL_Rect restored_clip;
                        SDL_RenderGetClipRect( renderer.get(), &restored_clip );
                        CHECK( restored_clip.x == saved_clip.x );
                        CHECK( restored_clip.y == saved_clip.y );
                        CHECK( restored_clip.w == saved_clip.w );
                        CHECK( restored_clip.h == saved_clip.h );
                        float scale_x, scale_y;
                        SDL_RenderGetScale( renderer.get(), &scale_x, &scale_y );
                        CHECK( scale_x == 1.0f );
                        CHECK( scale_y == 1.0f );
                        Uint8 r, green, b, a;
                        SDL_GetRenderDrawColor( renderer.get(), &r, &green, &b, &a );
                        CHECK( r == 17 );
                        CHECK( green == 31 );
                        CHECK( b == 47 );
                        CHECK( a == 101 );
                        SDL_RenderSetClipRect( renderer.get(), nullptr );
                        std::vector<Uint32> pixels( width * height );
                        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                      pixels.data(), width * 4 ) == 0 );
                        if( elapsed == 6550 ) {
                            CHECK( pixels == empty );
                        } else if( elapsed >= 450 ) {
                            CHECK( pixels != empty );
                        }
                        if( elapsed == 1500 ) {
                            CHECK( std::count_if( pixels.begin(), pixels.end(), []( Uint32 pixel ) {
                                return ( pixel >> 24 ) > 220 && ( ( pixel >> 16 ) & 255 ) > 220 &&
                                       ( ( pixel >> 8 ) & 255 ) > 220;
                            } ) > 100 );
                        }
                        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                                       pixels.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                            REQUIRE( image );
                            const auto output = std::filesystem::path( directory ) /
                                                ( "entry-" + std::to_string( height ) + "-" + std::to_string( elapsed ) + ".png" );
                            CHECK( IMG_SavePNG( image.get(), output.u8string().c_str() ) == 0 );
                        }
                    }
                }
            }
            TTF_Quit();
            fp3d_entry::reset();
            SDL_SetWindowSize( window.get(), 640, 480 );
            REQUIRE( SDL_GetRendererOutputSize( renderer.get(), &width, &height ) == 0 );
            fp3d_compositor::set_viewport( 0, 0, width, height, width, height );
        }
        {
            const auto saved_scene = scene;
            scene.cells.clear();
            scene.sprites.clear();
            fp3d_snapshot::sprite image;
            image.kind = fp3d_snapshot::sprite_kind::item;
            image.id = "rock";
            image.position = tripoint_abs_ms( 0, -3, 0 );
            image.height = 1;
            scene.sprites.push_back( image );
            const auto single = render();
            scene.sprites.front().multiple_items = true;
            REQUIRE( assets->find_tile_type( "highlight_item" ) );
            auto multiple = render();
            REQUIRE( fp3d_materials::bind() );
            CHECK( fp3d_materials::lookup( fp3d_materials::sprite_key( scene.sprites.front() ) ) );
            fp3d_materials::unbind();
            size_t changed = 0;
            size_t sum_x = 0;
            for( size_t i = 0; i < single.size(); ++i ) {
                if( single[i] != multiple[i] ) {
                    ++changed;
                    sum_x += i % width;
                }
            }
            REQUIRE( changed > 0 );
            if( pack == "UNDEAD_PEOPLE_BASE" ) {
                CHECK( static_cast<float>( sum_x ) / changed > width * .5f );
            }
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom(
                                            multiple.data(), width, height,
                                            32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                REQUIRE( picture );
                CHECK( IMG_SavePNG( picture.get(), ( std::filesystem::path( directory ) /
                                                    ( pack + "-multiple-items.png" ) ).u8string().c_str() ) == 0 );
            }
            scene.sprites.front().multiple_items = false;
            CHECK( render() == single );
            fp3d_snapshot::cell wall;
            wall.position = tripoint_abs_ms( 0, -1, 0 );
            wall.terrain = "t_wall";
            wall.kind = fp3d_snapshot::terrain_kind::wall;
            scene.cells.push_back( wall );
            const auto hidden_single = render();
            scene.sprites.front().multiple_items = true;
            CHECK( render() == hidden_single );
            scene = saved_scene;
        }
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
            for( const std::string project_id : { "constr_dig_downstair", "constr_mine_upstair" } ) {
                const auto project = std::find_if( get_constructions().begin(), get_constructions().end(),
                [&]( const construction & value ) { return value.str_id.str() == project_id; } );
                REQUIRE( project != get_constructions().end() );
                auto marker = fp3d_construction::placement_preview( *project, tripoint_abs_ms( 0, -3, 0 ), true );
                REQUIRE( marker.vertical_hint != 0 );
                fp3d_compositor::set_blueprint( marker );
                scene = original_scene;
                CHECK( render() == baseline );
                scene = empty_scene;
                const auto allowed = render();
                CHECK( allowed != empty_room );
                marker.valid = false;
                fp3d_compositor::set_blueprint( marker );
                CHECK( render() != allowed );
                if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                    SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom(
                                                 const_cast<std::uint32_t *>( allowed.data() ), width, height,
                                                 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                    REQUIRE( picture );
                    CHECK( IMG_SavePNG( picture.get(), ( std::filesystem::path( directory ) /
                                                         ( pack + "-planning-" + project_id + ".png" ) ).u8string().c_str() ) == 0 );
                }
                fp3d_compositor::set_blueprint( std::nullopt );
                CHECK( render() == empty_room );
            }
            for( const std::string id : {
                     "t_floor", "t_wall_wood", "t_door_c", "t_door_o", "t_window",
                     "t_stairs_up", "t_wood_stairs_up", "t_wood_stairs_up_half",
                     "t_ramp_up_high", "t_ramp_up_low",
                     "f_table", "f_chair", "f_bed", "f_bed_frame", "f_crate_c", "f_crate_o",
                     "f_cupboard", "f_locker", "f_bookcase", "f_rack"
                 } ) {
                CAPTURE( id );
                construction project{};
                project.post_terrain = id;
                project.post_is_furniture = id.compare( 0, 2, "f_" ) == 0;
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
                CHECK( fp3d_materials::lookup( project.post_is_furniture ? "$furniture:" + id :
                                               id == "t_window" ? "$window:" + id : id ) );
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
                if( id == "f_locker" || id == "f_bookcase" || id == "f_rack" ) {
                    CHECK( aperture_changes > 0 );
                } else if( project.post_is_furniture || id == "t_door_o" || id == "t_floor" ||
                           id == "t_ramp_up_low" ) {
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
                if( project.post_is_furniture ) {
                    const auto cell = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & tile ) {
                        return tile.position == ghost.position;
                    } );
                    REQUIRE( cell != scene.cells.end() );
                    cell->furniture = ghost.terrain->furniture;
                    const auto built = render();
                    CHECK( built != empty_room );
                    if( id == "f_crate_c" || id == "f_crate_o" || id == "f_locker" ||
                        id == "f_bookcase" || id == "f_rack" || id == "f_chair" ) {
                        REQUIRE( fp3d_materials::bind() );
                        const auto faces = fp3d_materials::lookup_furniture( id );
                        REQUIRE( faces.top );
                        REQUIRE( faces.front );
                        if( pack == "UNDEAD_PEOPLE_BASE" ) {
                            CHECK( faces.top->bottom < faces.front->top );
                            if( id == "f_chair" ) {
                                const auto leg = fp3d_materials::lookup_furniture( id, 2 );
                                REQUIRE( leg.top );
                                CHECK( leg.top->top > faces.front->bottom );
                            }
                        } else {
                            CHECK( faces.top->top == faces.front->top );
                            CHECK( faces.top->bottom == faces.front->bottom );
                        }
                        fp3d_materials::unbind();
                    }
                    if( id == "f_cupboard" ) {
                        REQUIRE( fp3d_materials::bind() );
                        const auto faces = fp3d_materials::lookup_furniture( id );
                        REQUIRE( faces.top );
                        REQUIRE( faces.front );
                        REQUIRE( faces.side );
                        if( pack == "UNDEAD_PEOPLE_BASE" ) {
                            CHECK( faces.top->bottom < faces.front->top );
                            CHECK( faces.side->right - faces.side->left <
                                   faces.front->right - faces.front->left );
                        } else {
                            CHECK( faces.top->top == faces.front->top );
                            CHECK( faces.top->bottom == faces.front->bottom );
                        }
                        fp3d_materials::unbind();
                        if( pack == "UNDEAD_PEOPLE_BASE" ) {
                            // Sample actual projected surfaces. The old whole-sprite
                            // mapping put a white countertop stripe on the front.
                            const float focal = width / ( 2 * std::tan(
                                                    fp3d_camera::horizontal_fov_degrees * 3.14159265f / 360 ) );
                            const auto format = SDL_AllocFormat( SDL_PIXELFORMAT_RGBA8888 );
                            REQUIRE( format );
                            const auto sample = [&]( float x, float y, float distance ) {
                                const int px = std::lround( width / 2.0f + focal * x / distance );
                                const int py = std::lround( height / 2.0f + focal *
                                                           ( fp3d_camera::eye_height - y ) / distance );
                                std::array<Uint8, 4> rgba{};
                                SDL_GetRGBA( built[py * width + px], format, &rgba[0], &rgba[1], &rgba[2], &rgba[3] );
                                return rgba;
                            };
                            const auto top = sample( 0, .86f, 3 );
                            const auto front = sample( .2f, .75f, 2.6f );
                            CAPTURE( top, front );
                            CHECK( top[0] > 30 );
                            CHECK( std::abs( int( top[0] ) - top[2] ) < 12 );
                            CHECK( front[0] > front[2] * 1.3f );
                            CHECK( front[1] > front[2] * 1.3f );
                            SDL_FreeFormat( format );
                        }
                    }
                    fp3d_snapshot::sprite object;
                    object.kind = fp3d_snapshot::sprite_kind::item;
                    object.position = ghost.position;
                    object.id = "rock";
                    object.height = .7f;
                    object.elevation = fp3d_furniture::item_surface( cell->furniture->shape );
                    if( id != "f_crate_c" ) {
                        scene.sprites.push_back( object );
                        CHECK( render() != built );
                        scene.sprites.clear();
                        CHECK( render() == built );
                    }
                    if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                        SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom(
                                                     const_cast<std::uint32_t *>( built.data() ), width, height,
                                                     32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                        REQUIRE( picture );
                        CHECK( IMG_SavePNG( picture.get(), ( std::filesystem::path( directory ) /
                                                             ( pack + "-built-" + id + ".png" ) ).u8string().c_str() ) == 0 );
                    }
                    cell->furniture.reset();
                    CHECK( render() == empty_room ); // Removing furniture must invalidate its cached mesh.
                }
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
        // Missing-material checker squares expose vertical wall stretching
        // independently of the selected tileset's artwork.
        fp3d_snapshot::cell ratio_wall;
        ratio_wall.position = tripoint_abs_ms( 0, -4, 0 );
        ratio_wall.kind = fp3d_snapshot::terrain_kind::wall;
        ratio_wall.terrain = "fp3d_missing_wall_ratio_probe";
        scene.cells.push_back( ratio_wall );
        auto square_wall = render();
        const auto longest_complete_run = [&]( int x, int y, int dx, int dy, int count ) {
            int longest = 0;
            int start = 0;
            for( int i = 1; i < count; ++i ) {
                if( square_wall[( y + i * dy ) * width + x + i * dx] !=
                    square_wall[( y + ( i - 1 ) * dy ) * width + x + ( i - 1 ) * dx] ) {
                    if( start > 0 ) {
                        longest = std::max( longest, i - start );
                    }
                    start = i;
                }
            }
            return longest;
        };
        const int checker_width = longest_complete_run( width / 2 - 20, height / 2 + 12, 1, 0, 40 );
        const int checker_height = longest_complete_run( width / 2 + 8, height / 2 - 30, 0, 1, 70 );
        REQUIRE( checker_width > 4 );
        CHECK( checker_height > checker_width * .8f );
        CHECK( checker_height < checker_width * 1.25f );
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                       square_wall.data(), width, height,
                                       32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( image );
            CHECK( IMG_SavePNG( image.get(), ( std::filesystem::path( directory ) /
                                               ( pack + "-wall-ratio.png" ) ).u8string().c_str() ) == 0 );
        }
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
            const auto clock = calendar::turn;
            const auto engine = rng_get_engine();
            const int moves = get_avatar().get_moves();
            fp3d_compositor::melee_swing();
            render( false ); // First frame starts the presentation clock.
            SDL_Delay( 90 );
            const auto striking = render( false );
            unsigned animated_pixels = 0;
            double rest_x = 0, rest_y = 0, strike_x = 0, strike_y = 0;
            unsigned rest_count = 0, strike_count = 0;
            for( int y = 0; y < height; ++y ) {
                for( int x = 0; x < width; ++x ) {
                    const auto index = y * width + x;
                    animated_pixels += striking[index] != equipped[index];
                    if( equipped[index] != empty_hands[index] ) {
                        rest_x += x; rest_y += y; ++rest_count;
                    }
                    if( striking[index] != empty_hands[index] ) {
                        strike_x += x; strike_y += y; ++strike_count;
                    }
                }
            }
            CHECK( animated_pixels > 50 );
            REQUIRE( rest_count > 0 );
            REQUIRE( strike_count > 0 );
            CHECK( strike_x / strike_count < rest_x / rest_count );
            CHECK( strike_y / strike_count < rest_y / rest_count );
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                           const_cast<std::uint32_t *>( striking.data() ), width, height, 32, width * 4,
                                           SDL_PIXELFORMAT_RGBA8888 ) );
                REQUIRE( image );
                const auto output = std::filesystem::path( directory ) / ( pack + "-swing-" + id + ".png" );
                CHECK( IMG_SavePNG( image.get(), output.u8string().c_str() ) == 0 );
            }
            SDL_Delay( 310 );
            CHECK( render( false ) == equipped );
            CHECK( calendar::turn == clock );
            CHECK( rng_get_engine() == engine );
            CHECK( get_avatar().get_moves() == moves );
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
        // Toggle the ceiling directly below the ramp, inside the camera view.
        // The first room cell can fall outside the view with a lower ceiling.
        auto room_below_ramp = std::find_if( scene.cells.begin(), scene.cells.end(),
        [&]( const fp3d_snapshot::cell &cell ) {
            return cell.position == ramp_above.position + tripoint_rel_ms( 0, 0, -1 );
        } );
        REQUIRE( room_below_ramp != scene.cells.end() );
        room_below_ramp->ceiling_present = false;
        const auto ramp_without_ceiling = render();
        CHECK( ramp_with_ceiling != ramp_without_ceiling );
        room_below_ramp->ceiling_present = true;
        scene.cells.pop_back();
        const auto before_toggle = render();
        fp3d_compositor::cycle_mode();
        CHECK( fp3d_compositor::controls() == fp3d_input::mode::original_2d );
        CHECK_FALSE( fp3d_compositor::active() );
        const auto original_2d = render();
        CHECK( original_2d != before_toggle );
        fp3d_compositor::cycle_mode();
        CHECK( fp3d_compositor::controls() == fp3d_input::mode::modern_2d );
        CHECK_FALSE( fp3d_compositor::active() );
        CHECK_FALSE( fp3d_compositor::covers_map( scene.origin ) );
        CHECK( render() == original_2d );
        fp3d_compositor::cycle_mode();
        CHECK( fp3d_compositor::controls() == fp3d_input::mode::first_person_3d );
        CHECK( fp3d_compositor::active() );
        CHECK( render() == before_toggle );
        CHECK( fp3d_compositor::covers_map( scene.origin ) );
        CHECK_FALSE( fp3d_compositor::covers_map( scene.origin + tripoint_rel_ms::east ) );
        fp3d_compositor::set_input_context( "INVENTORY" );
        CHECK_FALSE( fp3d_compositor::active() );
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
        for( const std::string id : { "t_door_glass_c", "t_wall_glass", "t_door_b", "t_door_locked" } ) {
            CAPTURE( id );
            scene.cells.back().terrain = id;
            scene.cells.back().kind = fp3d_snapshot::terrain_shape( id );
            const auto empty = render();
            scene.sprites.push_back( zombie );
            auto occupied = render();
            CHECK( ( occupied != empty ) == ( id != "t_door_locked" ) );
            scene.sprites.clear();
            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                SDL_Surface_Ptr image( SDL_CreateRGBSurfaceWithFormatFrom(
                                           occupied.data(), width, height, 32, width * 4,
                                           SDL_PIXELFORMAT_RGBA8888 ) );
                REQUIRE( image );
                CHECK( IMG_SavePNG( image.get(), ( std::filesystem::path( directory ) /
                                                   ( pack + "-" + id + ".png" ) ).u8string().c_str() ) == 0 );
            }
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
        // Covered 2D drawing must leave every pixel untouched, including native
        // animation/cursor layers, and defer the draw cache until 2D resumes.
        REQUIRE( SDL_SetRenderDrawColor( renderer.get(), 213, 37, 91, 255 ) == 0 );
        REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
        std::vector<std::uint32_t> covered_before( width * height );
        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                      covered_before.data(), width * 4 ) == 0 );
        here.draw_points_cache_dirty = true;
        native_tiles.init_draw_cursor( center );
        native_tiles.init_draw_hit( wall_pos, "t_wall" );
        native_tiles.init_draw_highlight( furniture_pos );
        native_tiles.draw( point::zero, center, width, height, text, blocks );
        std::vector<std::uint32_t> covered_after( width * height );
        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                      covered_after.data(), width * 4 ) == 0 );
        CHECK( covered_after == covered_before );
        CHECK( here.draw_points_cache_dirty );
        CHECK( text.empty() );
        CHECK( blocks.second.empty() );
        CHECK( you.get_memorized_tile( here.get_abs( wall_pos ) ).get_ter_id() == wall_memory );
        CHECK( you.get_memorized_tile( here.get_abs( furniture_pos ) ).get_dec_id() == furniture_memory );
        fp3d_compositor::cycle_mode();
        CHECK_FALSE( fp3d_compositor::active() );
        native_tiles.draw( point::zero, center, width, height, text, blocks );
        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                      covered_after.data(), width * 4 ) == 0 );
        CHECK( covered_after != covered_before );
        CHECK_FALSE( here.draw_points_cache_dirty );
        fp3d_compositor::cycle_mode();
        fp3d_compositor::cycle_mode();
        CHECK( fp3d_compositor::active() );

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
        here.add_item_or_charges( center + tripoint_rel_ms::north, item( itype_id( "bandages" ) ) );
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
            for( int page = 0; page < 12; ++page ) {
                const auto create_started = std::chrono::steady_clock::now();
                auto menu = fp3d_modern_ui::create( you, here, layout[2], page == 1,
                                                    page >= 9 ? 0 : page == 8 ? 7 : page == 7 ? 4 : page == 6 ? 3 : page >= 4 ? page + 1 : page >= 2 ? page - 1 : 0,
                                                    page == 9 ? you.get_wielded_item() : item_location(),
                                                    page == 10 ? std::make_optional( here.get_abs( center + tripoint_rel_ms::north ) ) : std::nullopt,
                                                    page == 11 ? std::make_optional( here.get_abs( center + tripoint_rel_ms::north ) ) : std::nullopt );
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
                    if( page == 8 && ( frame == 6 || frame == 7 ) ) {
                        ImGuiWindow *categories = nullptr;
                        for( ImGuiWindow *candidate : GImGui->Windows ) {
                            if( candidate->WasActive && std::string( candidate->Name ).find( "/更多分类_" ) != std::string::npos ) {
                                categories = candidate;
                            }
                        }
                        REQUIRE( categories );
                        const float font = categories->CalcFontSize();
                        const float line = font + ImGui::GetStyle().ItemSpacing.y;
                        ImGui::GetIO().AddMousePosEvent( categories->DC.CursorStartPos.x + 30,
                                                       categories->DC.CursorStartPos.y + 3 * line + font / 2 );
                        ImGui::GetIO().AddMouseButtonEvent( 0, frame == 6 );
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
                if( page == 10 ) {
                    ImGuiWindow *mine = nullptr;
                    ImGuiWindow *source = nullptr;
                    for( ImGuiWindow *child : GImGui->Windows ) {
                        if( !child->Active ) { continue; }
                        const std::string name = child->Name;
                        if( name.find( "/拾取背包_" ) != std::string::npos ) { mine = child; }
                        if( name.find( "/拾取目标_" ) != std::string::npos ) { source = child; }
                        CHECK( name.find( "/装备_" ) == std::string::npos );
                    }
                    REQUIRE( mine );
                    REQUIRE( source );
                    CHECK( mine->Pos.x + mine->Size.x <= source->Pos.x );
                    CHECK( source->Pos.x + source->Size.x <= layout[0] );
                    CHECK( source->Pos.y + source->Size.y <= layout[1] );
                }
                if( page == 4 ) {
                    CAPTURE( layout[0], layout[1], layout[2] );
                    ImGuiWindow *list = nullptr;
                    ImGuiWindow *details = nullptr;
                    ImGuiWindow *actions = nullptr;
                    for( ImGuiWindow *child : GImGui->Windows ) {
                        if( !child->Active ) {
                            continue;
                        }
                        const std::string name = child->Name;
                        if( name.find( "/任务列表_" ) != std::string::npos ) {
                            list = child;
                        } else if( name.find( "/任务详情_" ) != std::string::npos ) {
                            details = child;
                        } else if( name.find( "/任务操作_" ) != std::string::npos ) {
                            actions = child;
                        }
                    }
                    REQUIRE( list );
                    REQUIRE( details );
                    CHECK( list->Pos.x + list->Size.x <= details->Pos.x );
                    CHECK( details->Pos.x + details->Size.x <= layout[0] );
                    CHECK( details->Pos.y + details->Size.y <= layout[1] );
                    if( layout[0] == 1920 && layout[2] == 100 ) {
                        REQUIRE( actions );
                        CHECK( details->Pos.x + details->Size.x <= actions->Pos.x );
                        CHECK( actions->Pos.x + actions->Size.x <= layout[0] );
                    } else {
                        CHECK_FALSE( actions );
                    }
                }
                if( page == 3 || page == 6 || page == 11 ) {
                    CAPTURE( page, layout[0], layout[1], layout[2] );
                    const std::string category_name = page != 6 ? "/配方分类_" : "/建造分类_";
                    const std::string grid_name = page != 6 ? "/配方_" : "/建造项目_";
                    const std::string details_name = page != 6 ? "/配方详情_" : "/建造说明_";
                    ImGuiWindow *categories = nullptr;
                    ImGuiWindow *grid = nullptr;
                    ImGuiWindow *details = nullptr;
                    for( ImGuiWindow *child : GImGui->Windows ) {
                        if( !child->Active ) {
                            continue;
                        }
                        const std::string name = child->Name;
                        if( name.find( category_name ) != std::string::npos ) {
                            categories = child;
                        } else if( name.find( grid_name ) != std::string::npos ) {
                            grid = child;
                        } else if( name.find( details_name ) != std::string::npos ) {
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
                                                   ? "-map-" : page == 8 ? "-more-" : page == 10 ? "-pickup-" : page == 11 ? "-workbench-" : "-use-" )
                                          +
                                          std::to_string(
                                              layout[0] ) + "-" + std::to_string( layout[2] ) + ".png" );
                    CHECK( IMG_SavePNG( picture.get(), output.u8string().c_str() ) == 0 );
                }
            }
            // Drive the production dialogue controls through real ImGui mouse
            // events. Reading/clicking only queues native actions, never effects.
            {
                restore_on_out_of_scope restore_dialogue_rng( rng_get_engine() );
                on_out_of_scope restore_dialogue_context( []() {
                    fp3d_compositor::set_input_context( "DEFAULTMODE" );
                } );
                npc speaker;
                speaker.name = "特里安娜";
                speaker.male = false;
                speaker.wear_item( item( itype_id( "jacket_jean" ) ), false );
                const auto dialogue_rng = rng_get_engine();
                const auto dialogue_time = calendar::turn;
                const int dialogue_moves = you.get_moves();
                dialogue_window history;
                history.clear_history_highlights();
                history.add_to_history( "有什么发现吗？找个冥想圣物给我吧，哪种都行。", speaker.name, c_white );
                history.add_history_separator();
                history.add_to_history( "还没有。", "你", c_white );
                history.add_history_separator();
                history.add_to_history( "好歹我们还有个避难所可以呆。", speaker.name, c_white );
                std::vector<talk_data> replies = {
                    { c_white, "a", "关于那个任务……" },
                    { c_white, "b", "[询问] 我们现在该做什么？" },
                    { c_dark_gray, "c", "[条件不足] 这个回复不可选择" },
                    { c_white, "d", "[任务] 我能为你做些什么？" },
                    { c_white, "e", "[组队] 与我同行吧，如何？" },
                    { c_white, "f", "[交易] 让我们做笔交易吧。" },
                    { c_white, "g", "能和我分享一些有用的东西吗？" },
                    { c_white, "h", "再会！" }
                };
                std::vector<bool> enabled( replies.size(), true );
                enabled[2] = false;
                int selection = 0;
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                {
                    fp3d_dialogue::window dialogue( speaker, history.history_text(), selection, true, layout[2] );
                    dialogue.set_responses( replies, enabled );
                    ImGuiWindow *root = nullptr;
                    const auto frame = [&]( ImVec2 mouse = ImVec2( -100, -100 ), bool pressed = false ) {
                        ImGui_ImplSDLRenderer2_NewFrame();
                        ImGui_ImplSDL2_NewFrame();
                        ImGui::GetIO().AddMousePosEvent( mouse.x, mouse.y );
                        ImGui::GetIO().AddMouseButtonEvent( 0, pressed );
                        ImGui::NewFrame();
                        dialogue.draw();
                        for( ImGuiWindow *candidate : GImGui->Windows ) {
                            if( candidate->Active && std::string( candidate->Name ).find( "NPC dialogue##" ) == 0 &&
                                !( candidate->Flags & ImGuiWindowFlags_ChildWindow ) ) {
                                root = candidate;
                            }
                        }
                        ImGui::Render();
                        SDL_SetRenderDrawColor( renderer.get(), 25, 30, 38, 255 );
                        SDL_RenderClear( renderer.get() );
                        ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
                    };
                    const auto click = [&]( ImVec2 position ) {
                        frame( position );
                        frame( position, true );
                        frame( position, false );
                    };
                    const auto action = [&]() {
                        std::string result = "TIMEOUT";
                        dialogue.handle_action( result );
                        return result;
                    };
                    for( int n = 0; n < 5; ++n ) { frame(); }
                    REQUIRE( root );
                    CHECK( root->Pos.x >= 0 );
                    CHECK( root->Pos.y >= 0 );
                    CHECK( root->Pos.x + root->Size.x <= layout[0] );
                    CHECK( root->Pos.y + root->Size.y <= layout[1] );
                    CHECK_FALSE( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse );
                    const float scale = layout[2] / 100.0f;
                    ImGuiWindow *choices = nullptr;
                    for( ImGuiWindow *candidate : root->DC.ChildWindows ) {
                        if( std::string( candidate->Name ).find( "/回复选项_" ) != std::string::npos ) {
                            choices = candidate;
                        }
                    }
                    REQUIRE( choices );
                    const ImVec2 second( choices->DC.CursorStartPos.x + 80 * scale,
                                         choices->DC.CursorStartPos.y + 46 * scale + 19 * scale );
                    click( second );
                    CHECK( action() == "CONFIRM" );
                    CHECK( selection == 1 );
                    click( ImVec2( second.x, second.y + 46 * scale ) );
                    CHECK( action() == "TIMEOUT" );
                    CHECK( selection == 1 );
                    if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                        std::vector<std::uint32_t> pixels( layout[0] * layout[1] );
                        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                       pixels.data(), layout[0] * 4 ) == 0 );
                        SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(), layout[0], layout[1],
                                                 32, layout[0] * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                        REQUIRE( picture );
                        const auto output = std::filesystem::path( directory ) / ( pack + "-dialogue-" +
                                            std::to_string( layout[0] ) + "-" + std::to_string( layout[2] ) + ".png" );
                        CHECK( IMG_SavePNG( picture.get(), output.u8string().c_str() ) == 0 );
                    }
                    const std::array<std::string, 5> actions = { "LOOK_AT", "SIZE_UP_STATS", "ASSESS_PERSONALITY", "YELL", "CHECK_OPINION" };
                    for( size_t i = 0; i < actions.size(); ++i ) {
                        frame();
                        const bool narrow = root->Size.x - 36 * scale < 850 * scale;
                        const int columns = narrow ? 3 : 5;
                        const float width = ( root->Size.x - 36 * scale - ( columns - 1 ) * 10 * scale ) / columns;
                        const float last_y = root->DC.CursorPos.y - 8 * scale - 15 * scale;
                        click( ImVec2( root->Pos.x + 18 * scale + ( i % columns ) * ( width + 10 * scale ) + width / 2,
                                       last_y - ( narrow && i < 3 ? 38 * scale : 0 ) ) );
                        CHECK( action() == actions[i] );
                        history.add_history_separator();
                        history.add_to_history( "操作结果：仍可继续选择原来的对话。" );
                        dialogue.set_history( history.history_text() );
                        for( int n = 0; n < 4; ++n ) { frame(); }
                        CHECK( selection == 1 );
                        CHECK( action() == "TIMEOUT" );
                        click( second );
                        CHECK( action() == "CONFIRM" );
                        CHECK( selection == 1 );
                    }
                    ImGuiWindow *transcript = nullptr;
                    for( ImGuiWindow *candidate : root->DC.ChildWindows ) {
                        if( std::string( candidate->Name ).find( "/交谈记录_" ) != std::string::npos ) {
                            transcript = candidate;
                        }
                    }
                    REQUIRE( transcript );
                    CHECK( transcript->Scroll.y == Approx( transcript->ScrollMax.y ) );
                    click( ImVec2( root->Pos.x + root->Size.x - 68 * scale, root->Pos.y + 34 * scale ) );
                    CHECK( action() == "QUIT" );

                    for( bool proceed : { false, true } ) {
                        dialogue.request_confirmation( "你可能会被攻击！是否继续？" );
                        for( int n = 0; n < 4; ++n ) { frame(); }
                        ImGuiWindow *modal = nullptr;
                        for( ImGuiWindow *candidate : GImGui->Windows ) {
                            if( candidate->Active && ( candidate->Flags & ImGuiWindowFlags_Modal ) ) { modal = candidate; }
                        }
                        REQUIRE( modal );
                        const float x = modal->Pos.x + modal->WindowPadding.x +
                                        ( proceed ? 60 : 190 ) * scale;
                        const float y = modal->DC.CursorPos.y - 8 * scale - 18 * scale;
                        click( ImVec2( x, y ) );
                        const auto result = dialogue.take_confirmation();
                        REQUIRE( result );
                        CHECK( *result == proceed );
                        CHECK( action() == "TIMEOUT" );
                    }
                    for( int i = 0; i < 35; ++i ) {
                        replies.push_back( { c_white, "", "这是一条需要自动换行的长回复：如果我们能找到足够的食物、干净的水和一个安全的落脚点，就可以继续探索附近的建筑。" } );
                        enabled.push_back( true );
                    }
                    dialogue.set_responses( replies, enabled );
                    frame();
                    const ImVec2 scroll_at( choices->Pos.x + 80 * scale, choices->Pos.y + 40 * scale );
                    for( int n = 0; n < 12; ++n ) {
                        ImGui::GetIO().AddMouseWheelEvent( 0, -3 );
                        frame( scroll_at );
                    }
                    CHECK( choices->Scroll.y > 0 );
                    std::string end = "END";
                    dialogue.handle_action( end );
                    for( int n = 0; n < 4; ++n ) { frame(); }
                    CHECK( selection == static_cast<int>( replies.size() ) - 1 );
                    CHECK( choices->Scroll.y == Approx( choices->ScrollMax.y ) );
                }
                CHECK( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse );
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                CHECK( calendar::turn == dialogue_time );
                CHECK( you.get_moves() == dialogue_moves );
                CHECK( rng_get_engine() == dialogue_rng );
            }
            // The trade grid and quantity/confirmation buttons are driven by
            // mouse events against the production native trade model.
            {
                restore_on_out_of_scope restore_trade_rng( rng_get_engine() );
                on_out_of_scope restore_trade_context( []() {
                    fp3d_compositor::set_input_context( "DEFAULTMODE" );
                } );
                npc trader;
                trader.name = "特里安娜";
                trader.set_fac( faction_id( "no_faction" ) );
                trader.setpos( here, you.pos_bub( here ) + tripoint_rel_ms::north );
                trader.wear_item( item( itype_id( "backpack" ) ), false );
                item stock( itype_id( "9mm" ), calendar::turn, 12 );
                stock.set_owner( trader );
                const auto medicine = trader.i_add( stock );
                REQUIRE( medicine );
                const bool large_trade = layout[0] == 1920 && layout[2] == 100;
                if( large_trade ) {
                    for( int i = 0; i < 1000; ++i ) {
                        item extra( itype_id( "bandages" ), calendar::turn );
                        extra.set_var( "name", "交易性能测试绷带 " + std::to_string( i ) );
                        extra.set_owner( trader );
                        trader.i_add( extra );
                    }
                }
                trader.op_of_u.owed = 100000;
                trade_ui model( you, trader );
                const auto before_rng = rng_get_engine();
                const auto before_time = calendar::turn;
                const auto before_moves = you.get_moves();
                const auto before_cash = you.cash;
                const float scale = layout[2] / 100.0f;
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                {
                    fp3d_trade::window panel( model, layout[2] );
                    ImGuiWindow *root = nullptr;
                    const auto frame = [&]( ImVec2 mouse = ImVec2( -100, -100 ), bool pressed = false ) {
                        ImGui_ImplSDLRenderer2_NewFrame();
                        ImGui_ImplSDL2_NewFrame();
                        ImGui::GetIO().AddMousePosEvent( mouse.x, mouse.y );
                        ImGui::GetIO().AddMouseButtonEvent( 0, pressed );
                        ImGui::NewFrame();
                        panel.draw();
                        for( ImGuiWindow *candidate : GImGui->Windows ) {
                            if( candidate->Active && std::string( candidate->Name ).find( "NPC trade##" ) == 0 &&
                                !( candidate->Flags & ImGuiWindowFlags_ChildWindow ) ) { root = candidate; }
                        }
                        ImGui::Render();
                        SDL_SetRenderDrawColor( renderer.get(), 25, 30, 38, 255 );
                        SDL_RenderClear( renderer.get() );
                        ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
                    };
                    const auto click = [&]( ImVec2 pos ) {
                        frame( pos ); frame( pos, true ); frame( pos );
                    };
                    const auto child = []( ImGuiWindow *parent, const std::string &label ) {
                        ImGuiWindow *result = nullptr;
                        for( ImGuiWindow *candidate : parent->DC.ChildWindows ) {
                            if( std::string( candidate->Name ).find( "/" + label + "_" ) != std::string::npos ) {
                                result = candidate;
                            }
                        }
                        return result;
                    };
                    for( int n = 0; n < 5; ++n ) { frame(); }
                    REQUIRE( root );
                    if( large_trade ) {
                        const auto started = std::chrono::steady_clock::now();
                        for( int n = 0; n < 30; ++n ) { frame( ImVec2( 600 + n, 300 ) ); }
                        const double elapsed = std::chrono::duration<double, std::milli>(
                                                   std::chrono::steady_clock::now() - started ).count() / 30;
                        // Rendering work must stay proportional to the viewport,
                        // not the thousand items below it (previously >70k vertices).
                        CHECK( ImGui::GetDrawData()->TotalVtxCount < 12000 );
                        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                            std::ofstream report( std::filesystem::path( directory ) / ( pack + "-trade-performance.csv" ) );
                            report << "npc_extra_items,mean_frame_ms,vertices\n1000," << elapsed << ","
                                   << ImGui::GetDrawData()->TotalVtxCount << "\n";
                        }
                    }
                    CHECK( root->Pos.x >= 0 );
                    CHECK( root->Pos.y >= 0 );
                    CHECK( root->Pos.x + root->Size.x <= layout[0] );
                    CHECK( root->Pos.y + root->Size.y <= layout[1] );
                    CHECK_FALSE( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse );
                    REQUIRE( root->DC.ChildWindows.size() == 2 );
                    ImGuiWindow *left = root->DC.ChildWindows[0];
                    ImGuiWindow *right = root->DC.ChildWindows[1];
                    const int player_side = 1;
                    const int npc_side = 0;
                    CHECK( left->ChildId == ImHashStr( "交易背包", 0, ImHashData( &player_side, sizeof( player_side ), root->ID ) ) );
                    CHECK( right->ChildId == ImHashStr( "交易背包", 0, ImHashData( &npc_side, sizeof( npc_side ), root->ID ) ) );
                    auto *grid = child( right, "物品网格" );
                    auto *quantity = child( right, "数量操作" );
                    REQUIRE( grid ); REQUIRE( quantity );
                    CHECK( grid->CalcFontSize() == Approx( 18 * scale ) );
                    CHECK( quantity->CalcFontSize() == Approx( 18 * scale ) );
                    CHECK( left->Pos.x + left->Size.x <= right->Pos.x );
                    CHECK( quantity->Pos.y + quantity->Size.y <= right->Pos.y + right->Size.y );
                    if( large_trade ) {
                        const float full_scroll = grid->ScrollMax.y;
                        CHECK( full_scroll > 5000 );
                        const ImVec2 scroll_at( grid->Pos.x + 50 * scale, grid->Pos.y + 40 * scale );
                        frame( scroll_at );
                        frame( scroll_at );
                        ImGui::GetIO().AddMouseWheelEvent( 0, -10000 );
                        for( int n = 0; n < 4; ++n ) { frame( scroll_at ); }
                        CHECK( grid->Scroll.y == Approx( full_scroll ) );
                        CHECK( grid->ScrollMax.y == Approx( full_scroll ) );
                        CHECK( ImGui::GetDrawData()->TotalVtxCount < 12000 );
                        ImGui::GetIO().AddMouseWheelEvent( 0, 10000 );
                        for( int n = 0; n < 4; ++n ) { frame( scroll_at ); }
                        CHECK( grid->Scroll.y == Approx( 0 ) );
                    }
                    // Category dropdown: ammunition leaves one deterministic card.
                    click( ImVec2( right->DC.CursorStartPos.x + 100 * scale,
                                   grid->Pos.y - 6 * scale - ( 18 * scale + 6 ) / 2 ) );
                    ImGuiWindow *combo = nullptr;
                    for( ImGuiWindow *candidate : GImGui->Windows ) {
                        if( candidate->Active && std::string( candidate->Name ).find( "##Combo_" ) == 0 ) { combo = candidate; }
                    }
                    REQUIRE( combo );
                    click( ImVec2( combo->DC.CursorStartPos.x + 30 * scale,
                                   combo->DC.CursorStartPos.y + 2.5f * ( 18 * scale + 6 * scale ) ) );
                    frame();
                    const ImVec2 card( grid->DC.CursorStartPos.x + 48 * scale,
                                       grid->DC.CursorStartPos.y + 85 * scale + 1 );
                    click( card );
                    CHECK_FALSE( panel.process_action() );
                    frame();
                    const float control_width = ( quantity->WorkRect.GetWidth() - 32 * scale ) / 5;
                    const auto count_button = [&]( int column ) {
                        click( ImVec2( quantity->DC.CursorStartPos.x + column * ( control_width + 8 * scale ) + control_width / 2,
                                       quantity->DC.CursorPos.y - 6 * scale - 15 * scale ) );
                        CHECK_FALSE( panel.process_action() );
                        frame();
                    };
                    count_button( 2 ); // All 12.
                    count_button( 0 ); // 11.
                    count_button( 4 ); // Remove.
                    count_button( 1 ); // 1.
                    count_button( 1 ); // 2.
                    if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                        std::vector<std::uint32_t> pixels( layout[0] * layout[1] );
                        REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                       pixels.data(), layout[0] * 4 ) == 0 );
                        SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(), layout[0], layout[1],
                                                 32, layout[0] * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                        REQUIRE( picture );
                        const auto output = std::filesystem::path( directory ) / ( pack + "-trade-" +
                                            std::to_string( layout[0] ) + "-" + std::to_string( layout[2] ) + ".png" );
                        CHECK( IMG_SavePNG( picture.get(), output.u8string().c_str() ) == 0 );
                    }
                    const auto confirm = [&]() {
                        click( ImVec2( root->Pos.x + root->Size.x - 86 * scale,
                                       root->DC.CursorPos.y - 6 * scale - 17 * scale ) );
                        CHECK_FALSE( panel.process_action() );
                        for( int n = 0; n < 4; ++n ) { frame(); }
                        ImGuiWindow *modal = nullptr;
                        for( ImGuiWindow *candidate : GImGui->Windows ) {
                            if( candidate->Active && ( candidate->Flags & ImGuiWindowFlags_Modal ) ) { modal = candidate; }
                        }
                        return modal;
                    };
                    auto *modal = confirm();
                    REQUIRE( modal );
                    // Returning to the grid must leave the offer pending.
                    click( ImVec2( modal->DC.CursorStartPos.x + 203 * scale,
                                   modal->DC.CursorPos.y - 6 * scale - 18 * scale ) );
                    CHECK_FALSE( panel.process_action() );
                    frame();
                    modal = confirm();
                    REQUIRE( modal );
                    click( ImVec2( modal->DC.CursorStartPos.x + 65 * scale,
                                   modal->DC.CursorPos.y - 6 * scale - 18 * scale ) );
                    const auto result = panel.process_action();
                    REQUIRE( result );
                    CHECK( result->traded );
                    REQUIRE( result->items_trader.size() == 1 );
                    CHECK( result->items_trader.front().first == medicine );
                    CHECK( result->items_trader.front().second == 2 );
                    CHECK( result->value_trader == npc_trading::trading_price( you, trader, { medicine, 2 } ) );
                    CHECK( result->balance == 100000 - result->value_trader );
                    CHECK( result->items_you.empty() );
                    auto *your_grid = child( left, "物品网格" );
                    REQUIRE( your_grid );
                    for( int n = 0; n < 8; ++n ) {
                        ImGui::GetIO().AddMouseWheelEvent( 0, -3 );
                        frame( ImVec2( your_grid->Pos.x + 50 * scale, your_grid->Pos.y + 40 * scale ) );
                    }
                    CHECK( your_grid->Scroll.y > 0 );
                    click( ImVec2( root->Pos.x + root->Size.x - 66 * scale, root->Pos.y + 27 * scale ) );
                    const auto cancelled = panel.process_action();
                    REQUIRE( cancelled );
                    CHECK_FALSE( cancelled->traded );
                    CHECK( cancelled->items_trader.empty() );
                }
                CHECK( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse );
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                CHECK( medicine->charges == 12 );
                CHECK( calendar::turn == before_time );
                CHECK( you.get_moves() == before_moves );
                CHECK( you.cash == before_cash );
                CHECK( rng_get_engine() == before_rng );
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
                REQUIRE_FALSE( context_choices[5].visible );
                auto contextual = fp3d_quick_menu::create( context_choices, "目标操作", layout[2] );
                for( int frame = 0; frame < 9; ++frame ) {
                    ImGui_ImplSDLRenderer2_NewFrame();
                    ImGui_ImplSDL2_NewFrame();
                    ImGui::GetIO().AddMousePosEvent( center.x, center.y - 160 );
                    ImGui::GetIO().AddMouseButtonEvent( 0, frame == 7 );
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
                    ImGui::GetIO().AddMousePosEvent( center.x, center.y + 160 );
                    ImGui::GetIO().AddMouseButtonEvent( 0, frame == 1 );
                    ImGui::NewFrame();
                    context_cancel->draw();
                    ImGui::Render();
                }
                CHECK_FALSE( context_cancel->get_is_open() );
                CHECK_FALSE( context_cancel->has_button_action() );
                {
                    const auto position = you.pos_bub( here );
                    item_location corpse;
                    {
                        restore_on_out_of_scope restore_fixture_rng( rng_get_engine() );
                        corpse = here.add_item_or_charges_ret_loc( position,
                                 item::make_corpse( mtype_id( "mon_deer" ), calendar::turn ) );
                    }
                    REQUIRE( corpse );
                    on_out_of_scope remove_corpse( [&]() { corpse.remove_item(); } );
                    const fp3d_targeting::target target{ you.pos_abs(), here.get_abs( position ),
                            fp3d_targeting::kind::item, fp3d_snapshot::item_sprite_id( *corpse ) };
                    const auto choices = fp3d_interaction::context_entries( target, you, here );
                    auto corpse_wheel = fp3d_quick_menu::create( choices, "尸体", layout[2] );
                    for( int frame = 0; frame < 5; ++frame ) {
                        ImGui_ImplSDLRenderer2_NewFrame();
                        ImGui_ImplSDL2_NewFrame();
                        ImGui::GetIO().AddMousePosEvent( center.x + 139, center.y + 80 );
                        ImGui::GetIO().AddMouseButtonEvent( 0, frame == 3 );
                        ImGui::NewFrame();
                        corpse_wheel->draw();
                        ImGui::Render();
                    }
                    CHECK_FALSE( corpse_wheel->get_is_open() );
                    CHECK( corpse_wheel->get_button_action() == action_ident( ACTION_BUTCHER ) );
                    CHECK( corpse );
                }
                {
                    const auto stair_position = you.pos_bub( here );
                    const ter_id previous_terrain = here.ter( stair_position );
                    on_out_of_scope restore_stair( [&]() { here.ter_set( stair_position, previous_terrain ); } );
                    here.ter_set( stair_position, ter_str_id( "t_stairs_up" ) );
                    const fp3d_targeting::target stair_target{ you.pos_abs(), you.pos_abs(),
                            fp3d_targeting::kind::terrain, "t_stairs_up" };
                    const auto stair_choices = fp3d_interaction::context_entries( stair_target, you, here );
                    REQUIRE( stair_choices[0].enabled );
                    REQUIRE_FALSE( stair_choices[1].enabled );
                    auto stair_wheel = fp3d_quick_menu::create( stair_choices, "楼梯", layout[2] );
                    for( int frame = 0; frame < 5; ++frame ) {
                        ImGui_ImplSDLRenderer2_NewFrame();
                        ImGui_ImplSDL2_NewFrame();
                        ImGui::GetIO().AddMousePosEvent( center.x, center.y - 160 );
                        ImGui::GetIO().AddMouseButtonEvent( 0, frame == 3 );
                        ImGui::NewFrame();
                        stair_wheel->draw();
                        ImGui::Render();
                        SDL_SetRenderDrawColor( renderer.get(), 12, 16, 20, 255 );
                        SDL_RenderClear( renderer.get() );
                        ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
                        if( frame == 2 ) {
                            REQUIRE( stair_wheel->get_is_open() );
                            if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
                                std::vector<std::uint32_t> pixels( layout[0] * layout[1] );
                                REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                              pixels.data(), layout[0] * 4 ) == 0 );
                                SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(), layout[0],
                                                         layout[1], 32, layout[0] * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                                const auto filename = std::filesystem::path( directory ) /
                                                      ( pack + "-stairs-" + std::to_string( layout[0] ) + "-" +
                                                        std::to_string( layout[2] ) + ".png" );
                                REQUIRE( picture );
                                CHECK( IMG_SavePNG( picture.get(), filename.u8string().c_str() ) == 0 );
                            }
                        }
                    }
                    CHECK_FALSE( stair_wheel->get_is_open() );
                    CHECK( stair_wheel->get_button_action() == "FP3D_STAIRS_UP" );
                }
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
