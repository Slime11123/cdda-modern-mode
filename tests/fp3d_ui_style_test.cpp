#include "fp3d_compositor.h"
#include "fp3d_aiming.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <filesystem>
#include <vector>
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "fp3d_snapshot.h"
#include "fp3d_shaderpacks.h"
#include "fp3d_materials.h"
#include "fp3d_survival_style.h"
#include "options_helpers.h"
#include "sdl_wrappers.h"
#include "sdl_geometry.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_imgui.h"
#include "color.h"
#include "fp3d_modern_ui.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "fp3d_quick_menu.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_sdlrenderer3.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "sdltiles.h"
#include "modern_2d_camera.h"
#include "fp3d_dialogue.h"
#include "fp3d_trade.h"
#include "npc.h"
#include "item.h"
#include "trade_ui.h"
#include <cmath>

TEST_CASE( "survival_map_uses_native_overmap_tiles_without_changing_renderer_or_world",
           "[.survival_map_render]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Overmap regression", 640, 480, SDL_WINDOW_HIDDEN ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), "software" ) );
    REQUIRE( renderer );
    GeometryRenderer_Ptr geometry = std::make_unique<DefaultGeometryRenderer>();
    tileset_cache cache;
    restore_on_out_of_scope restore_tiles( tilecontext );
    tilecontext = std::make_shared<cata_tiles>( renderer, geometry, cache );
    cata_tiles tiles( renderer, geometry, cache );
    const char *pack = std::getenv( "CDDA_OVERMAP_QA_TILESET" );
    tiles.load_tileset( pack ? pack : "Larwick Overmap", false, false, false, true );
    clear_avatar();
    const auto center = get_avatar().pos_abs_omt();
    struct saved_cell {
        tripoint_abs_omt position;
        oter_id terrain;
        om_vision_level vision;
    };
    std::vector<saved_cell> original;
    on_out_of_scope restore_world( [&]() {
        for( const auto &cell : original ) {
            overmap_buffer.ter_set( cell.position, cell.terrain );
            const auto om = overmap_buffer.get_existing_om_global( cell.position );
            om.om->set_seen( om.local, cell.vision, true );
        }
    } );
    for( int y = -9; y <= 9; ++y ) {
        for( int x = -9; x <= 9; ++x ) {
            const auto pos = center + point( x, y );
            original.push_back( { pos, overmap_buffer.ter( pos ), overmap_buffer.seen( pos ) } );
            overmap_buffer.ter_set( pos, oter_str_id( y == 0 ? "road_ew" : x == 0 ? "road_ns" :
                                    y < -3 ? "forest_thick" : "field" ).id() );
            overmap_buffer.set_seen( pos, om_vision_level::full );
        }
    }
    const auto before_rng = rng_get_engine();
    const auto before_time = calendar::turn;
    const auto before_moves = get_avatar().get_moves();
    SDL_Texture_Ptr target( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_RGBA8888,
                            SDL_TEXTUREACCESS_TARGET, 640, 480 ) );
    REQUIRE( target );
    REQUIRE( SDL_SetRenderTarget( renderer.get(), target.get() ) );
    SDL_Rect viewport{ 7, 11, 300, 240 };
    const SDL_Rect clip{ 2, 3, 170, 130 };
    SDL_SetRenderViewport( renderer.get(), &viewport );
    SDL_SetRenderScale( renderer.get(), 1.5f, 1.5f );
    SDL_SetRenderClipRect( renderer.get(), &clip );
    SDL_GetRenderViewport( renderer.get(), &viewport );
    SDL_SetRenderDrawColor( renderer.get(), 12, 34, 56, 78 );
    auto image = tiles.overmap_texture( center, 8 );
    REQUIRE( image );
    CHECK( SDL_GetRenderTarget( renderer.get() ) == target.get() );
    SDL_Rect actual;
    SDL_GetRenderViewport( renderer.get(), &actual );
    CHECK( SDL_RectsEqual( &actual, &viewport ) );
    SDL_GetRenderClipRect( renderer.get(), &actual );
    CHECK( SDL_RectsEqual( &actual, &clip ) );
    float sx, sy;
    SDL_GetRenderScale( renderer.get(), &sx, &sy );
    CHECK( sx == 1.5f );
    CHECK( sy == 1.5f );
    Uint8 r, green, b, a;
    SDL_GetRenderDrawColor( renderer.get(), &r, &green, &b, &a );
    CHECK( r == 12 );
    CHECK( green == 34 );
    CHECK( b == 56 );
    CHECK( a == 78 );
    int width, height;
    REQUIRE( QueryTexturePixels( image.get(), nullptr, nullptr, &width, &height ) == 0 );
    CHECK( width == 17 * tiles.get_tile_width() );
    CHECK( height == 17 * tiles.get_tile_height() );
    SDL_SetRenderTarget( renderer.get(), image.get() );
    SDL_SetRenderViewport( renderer.get(), nullptr );
    SDL_SetRenderScale( renderer.get(), 1, 1 );
    SDL_SetRenderClipRect( renderer.get(), nullptr );
    std::vector<Uint32> pixels( width * height );
    REQUIRE( ReadRenderPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                  pixels.data(), width * 4 ) == 0 );
    CHECK( std::count_if( pixels.begin(), pixels.end(), []( Uint32 pixel ) {
        return ( pixel >> 8 ) != 0;
    } ) > width * height * .1 );
    if( const char *directory = std::getenv( "CDDA_2D_PANEL_QA_DIR" ) ) {
        SDL_Surface_Ptr surface( CreateSurfaceFromPixels( pixels.data(), width, height,
                                 32, width * 4, SDL_PIXELFORMAT_RGBA8888 ) );
        REQUIRE( surface );
        REQUIRE( SDL_SaveBMP( surface.get(), ( std::filesystem::path( directory ) /
                             "native-overmap.bmp" ).string().c_str() ) );
    }
    const tripoint_abs_omt remote( 2000000, 2000000, 0 );
    REQUIRE_FALSE( static_cast<bool>( overmap_buffer.get_existing_om_global( remote ) ) );
    REQUIRE( tiles.overmap_texture( remote, 32 ) );
    CHECK_FALSE( static_cast<bool>( overmap_buffer.get_existing_om_global( remote ) ) );
    CHECK( rng_get_engine() == before_rng );
    CHECK( calendar::turn == before_time );
    CHECK( get_avatar().get_moves() == before_moves );
    SDL_SetRenderTarget( renderer.get(), nullptr );
}

TEST_CASE( "fp3d_ui_style_is_independent_of_view_and_controls", "[.fp3d_ui_style]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "UI style regression", 640, 480, SDL_WINDOW_HIDDEN ) );
    REQUIRE( window );
    SDL_SetHintWithPriority( SDL_HINT_GPU_DRIVER, "direct3d12", SDL_HINT_OVERRIDE );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), "gpu" ) );
    REQUIRE( renderer );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    fp3d_compositor::attach( renderer.get(), window.get() );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    fp3d_snapshot::scene_snapshot scene;
    scene.hud.status = "3D status";
    fp3d_compositor::publish( scene );
    fp3d_hud::data hud;
    hud.status = "fresh 2D status";
    fp3d_compositor::publish_hud( hud );
    for( int mode = 0; mode < 3; ++mode ) {
        const auto controls = fp3d_compositor::controls();
        const bool is_3d = controls == fp3d_input::mode::first_person_3d;
        for( const std::string style : { "auto", "classic", "hud" } ) {
            CAPTURE( mode, style );
            override_option option( "FP3D_UI_STYLE", style );
            const bool hud_visible = style == "hud" || ( style == "auto" && is_3d );
            CHECK( fp3d_compositor::uses_hud() == hud_visible );
            CHECK( fp3d_compositor::controls() == controls );
            CHECK( fp3d_compositor::active() == is_3d );
            fp3d_compositor::set_viewport( 0, 0, 480, 480, 640, 480 );
            CHECK( fp3d_compositor::viewport_pixels()[2] == ( is_3d && hud_visible ? 640 : 480 ) );
            // Left-side native panels must also keep their space.
            fp3d_compositor::set_viewport( 160, 0, 480, 480, 640, 480 );
            CHECK( fp3d_compositor::viewport_pixels()[0] == 160 );
            CHECK( fp3d_compositor::viewport_pixels()[2] == 480 );
            if( is_3d && style == "classic" ) {
                fp3d_compositor::set_viewport( 0, 0, 480, 480, 640, 480 );
                SDL_SetRenderDrawColor( renderer.get(), 71, 93, 117, 255 );
                REQUIRE( SDL_RenderClear( renderer.get() ) );
                const SDL_Rect sidebar_pixel{ 600, 240, 1, 1 };
                Uint32 before = 0;
                Uint32 after = 0;
                REQUIRE( ReadRenderPixels( renderer.get(), &sidebar_pixel,
                                              SDL_PIXELFORMAT_RGBA8888, &before, 4 ) == 0 );
                fp3d_compositor::compose();
                REQUIRE( ReadRenderPixels( renderer.get(), &sidebar_pixel,
                                              SDL_PIXELFORMAT_RGBA8888, &after, 4 ) == 0 );
                CHECK( after == before );
            }
            const auto lines = fp3d_compositor::hud_lines();
            CHECK( lines.empty() == !hud_visible );
            if( hud_visible ) {
                REQUIRE_FALSE( lines.empty() );
                CHECK( lines.front() == ( is_3d ? "3D status" : "fresh 2D status" ) );
            }
            fp3d_compositor::set_input_context( "INVENTORY" );
            CHECK( fp3d_compositor::hud_lines().empty() );
            fp3d_compositor::set_input_context( "DEFAULTMODE" );
        }
        fp3d_compositor::cycle_mode();
    }
    for( const bool three_d : { false, true } ) {
        for( const bool modern : { false, true } ) {
            for( const std::string style : { "classic", "hud" } ) {
                CAPTURE( three_d, modern, style );
                override_option option( "FP3D_UI_STYLE", style );
                fp3d_compositor::apply_modes( three_d, modern );
                const auto expected = !modern ? fp3d_input::mode::original_2d :
                                      three_d ? fp3d_input::mode::first_person_3d : fp3d_input::mode::modern_2d;
                CHECK( fp3d_compositor::view_3d() == three_d );
                CHECK( fp3d_compositor::active() == three_d );
                CHECK( fp3d_compositor::controls() == expected );
                CHECK( fp3d_compositor::uses_hud() == ( style == "hud" ) );
                SDL_Event settings_key{};
                settings_key.type = SDL_EVENT_KEY_DOWN;
                settings_key.key.key = SDLK_F5;
                // F5 reaches the input dispatcher; it no longer changes modes directly.
                CHECK_FALSE( fp3d_compositor::process_event( settings_key ) );
                CHECK( fp3d_compositor::view_3d() == three_d );
                CHECK( fp3d_compositor::controls() == expected );
                CHECK( fp3d_input::resolve( expected,
                       input_event( 'w', input_event_t::keyboard_code ), 90 ).empty() == !modern );
            }
        }
    }
    fp3d_compositor::set_input_context( "MAIN_MENU" );
    CHECK( fp3d_compositor::hud_lines().empty() );
}
TEST_CASE( "modern_2d_edge_pan_uses_scaled_renderer_coordinates", "[.modern_2d_edge_coordinates]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "2D edge coordinates regression", 640, 480, SDL_WINDOW_HIDDEN ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), "software" ) );
    REQUIRE( renderer );
    for( const int scale : { 1, 2, 4 } ) {
        REQUIRE( SetLogicalRenderSize( renderer.get(), 640 * scale, 480 * scale ) == 0 );
        const point origin( 80 * scale, 24 * scale );
        const point size( 480 * scale, 432 * scale );
        for( const point mouse : { point( 80, 240 ), point( 559, 240 ), point( 320, 24 ),
                                  point( 320, 455 ), point( 80, 24 ), point( 559, 24 ),
                                  point( 80, 455 ), point( 559, 455 ) } ) {
            CAPTURE( scale, mouse );
            float x = 0;
            float y = 0;
            SDL_RenderCoordinatesFromWindow( renderer.get(), mouse.x, mouse.y, &x, &y );
            const point local = point( std::floor( x ), std::floor( y ) ) - origin;
            modern_2d_camera camera;
            for( int frame = 0; frame < 180; ++frame ) {
                camera.update( true, local, size, .016f );
            }
            const point pan = camera.pixels();
            if( mouse.x != 320 ) {
                CHECK( pan.x != 0 );
                CHECK( ( pan.x > 0 ) == ( mouse.x > 320 ) );
            }
            if( mouse.y != 240 ) {
                CHECK( pan.y != 0 );
                CHECK( ( pan.y > 0 ) == ( mouse.y > 240 ) );
            }
        }
    }
}

TEST_CASE( "fp3d_shader_list_rescan_finds_added_pack_without_selecting_it",
           "[fp3d][shader_list]" )
{
    const std::string selected = fp3d_shaderpacks::selected();
    std::string error;
    const auto initial = fp3d_shaderpacks::scan( &error );
    REQUIRE( error.empty() );
    const std::string fixture_id = "fp3d-ui-rescan-test-" + std::to_string( SDL_GetTicksNS() );
    const auto matches = [&]( const fp3d_shaderpacks::entry &pack ) {
        return pack.id == fixture_id;
    };
    CHECK( std::none_of( initial.begin(), initial.end(), matches ) );
    const auto path = std::filesystem::u8path( fp3d_shaderpacks::directory() ) / fixture_id;
    REQUIRE( std::filesystem::create_directory( path ) );
    on_out_of_scope cleanup( [&]() {
        std::error_code remove_error;
        std::filesystem::remove( path, remove_error );
    } );
    const auto refreshed = fp3d_shaderpacks::scan( &error );
    CHECK( error.empty() );
    CHECK( std::any_of( refreshed.begin(), refreshed.end(), matches ) );
    CHECK( fp3d_shaderpacks::selected() == selected );
}

TEST_CASE( "modern_2d_survival_panel_renders_without_a_3d_scene", "[.modern_2d_panel]" )
{
    override_option view_mode( "FP3D_VIEW", "2d" );
    override_option control_mode( "FP3D_CONTROLS", "classic" );
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "2D survival panel regression", 1280, 720, SDL_WINDOW_HIDDEN ) );
    REQUIRE( window );
    SDL_SetHintWithPriority( SDL_HINT_GPU_DRIVER, "direct3d12", SDL_HINT_OVERRIDE );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), "gpu" ) );
    REQUIRE( renderer );
    GeometryRenderer_Ptr geometry = std::make_unique<DefaultGeometryRenderer>();
    tileset_cache cache;
    restore_on_out_of_scope restore_tiles( tilecontext );
    tilecontext = std::make_shared<cata_tiles>( renderer, geometry, cache );
    restore_on_out_of_scope restore_overmap( std::move( overmap_tilecontext ) );
    overmap_tilecontext = std::make_unique<cata_tiles>( renderer, geometry, cache );
    const char *pack = std::getenv( "CDDA_OVERMAP_QA_TILESET" );
    overmap_tilecontext->load_tileset( pack ? pack : "Larwick Overmap", false, false, false, true );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    fp3d_compositor::attach( renderer.get(), window.get() );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    REQUIRE( fp3d_compositor::controls() == fp3d_input::mode::original_2d );
    CHECK( fp3d_compositor::survival_menu_available() );
    fp3d_compositor::cycle_mode();
    REQUIRE( fp3d_compositor::controls() == fp3d_input::mode::modern_2d );
    CHECK( fp3d_compositor::survival_menu_available() );
    CHECK_FALSE( fp3d_compositor::turn_based() );
    CHECK_FALSE( fp3d_compositor::active() );
    CHECK_FALSE( fp3d_compositor::copy_view() );

    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    map &here = get_map();
    color_loader<SDL_Color>().load( windowsPalette );
    init_colors();
    ImGui::CreateContext();
    REQUIRE( ImGui_ImplSDL3_InitForSDLRenderer( window.get(), renderer.get() ) );
    REQUIRE( ImGui_ImplSDLRenderer3_Init( renderer.get() ) );
    on_out_of_scope gui_cleanup( []() {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    } );
    ImGui::GetIO().IniFilename = nullptr;
    REQUIRE( ImGui::GetIO().Fonts->AddFontFromFileTTF( "data/font/unifont.ttf", 18, nullptr,
             ImGui::GetIO().Fonts->GetGlyphRangesChineseFull() ) );
    fp3d_compositor::set_input_context( "FP3D_PANEL" );
    const auto time = calendar::turn;
    const int moves = you.get_moves();
    const auto position = you.pos_abs();
    const auto random = rng_get_engine();
    for( int page = 0; page < 9; ++page ) {
        CAPTURE( page );
        auto panel = fp3d_modern_ui::create( you, here, 100, false, page );
        for( int frame = 0; frame < 3; ++frame ) {
            REQUIRE( SDL_SetRenderDrawColor( renderer.get(), 18, 22, 18, 255 ) );
            REQUIRE( SDL_RenderClear( renderer.get() ) );
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            panel->draw();
            ImGui::Render();
            REQUIRE( ImGui::GetDrawData()->TotalVtxCount > 0 );
            ImGui_ImplSDLRenderer3_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
            if( frame == 2 ) {
                if( const char *directory = std::getenv( "CDDA_2D_PANEL_QA_DIR" ) ) {
                    std::filesystem::create_directories( directory );
                    std::vector<Uint32> pixels( 1280 * 720 );
                    REQUIRE( ReadRenderPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                  pixels.data(), 1280 * 4 ) == 0 );
                    SDL_Surface_Ptr picture( CreateSurfaceFromPixels( pixels.data(), 1280,
                                             720, 32, 1280 * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                    REQUIRE( picture );
                    const auto filename = std::filesystem::path( directory ) /
                                          ( "page-" + std::to_string( page ) + ".png" );
                    CHECK( IMG_SavePNG( picture.get(), filename.u8string().c_str() ) );
                }
            }
            SDL_RenderPresent( renderer.get() );
        }
        CHECK( calendar::turn == time );
        CHECK( you.get_moves() == moves );
        CHECK( you.pos_abs() == position );
        CHECK( rng_get_engine() == random );
    }
    // Exercise the production cursor menu and Q wheel without any 3D snapshot.
    fp3d_compositor::set_viewport( 0, 0, 1280, 720, 1280, 720 );
    fp3d_compositor::set_input_context( "MODERN_2D_CONTEXT" );
    CHECK( fp3d_compositor::modern_2d_overlay_open() );
    const std::vector<fp3d_quick_menu::entry> choices = {
        { "前往此处", "沿原版路径前往此处。", ACTION_NULL, "MODERN_2D_TRAVEL" },
        { "打开", "打开点中的门。", ACTION_OPEN, nullptr },
        { "拾取物品", "此项暂不可用。", ACTION_PICKUP, nullptr, false }
    };
    const auto draw_menu = [&]( cataimgui::window &menu, bool clear = true ) {
        if( clear ) {
            SDL_SetRenderDrawColor( renderer.get(), 18, 22, 18, 255 );
            SDL_RenderClear( renderer.get() );
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        menu.draw();
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
    };
    const auto save_menu = [&]( const char *filename ) {
        if( const char *directory = std::getenv( "CDDA_2D_PANEL_QA_DIR" ) ) {
            SDL_Surface_Ptr picture( SDL_RenderReadPixels( renderer.get(), nullptr ) );
            REQUIRE( picture );
            const auto path = std::filesystem::path( directory ) / filename;
            CHECK( IMG_SavePNG( picture.get(), path.u8string().c_str() ) );
        }
    };
    int selected_view = 1;
    int selected_keys = 1;
    int selected_style = 1;
    auto settings = fp3d_input::create_mode_settings( selected_view, selected_keys, selected_style );
    for( int frame = 0; frame < 3; ++frame ) { draw_menu( *settings ); }
    save_menu( "mode-settings.png" );
    ImGuiWindow *settings_window = nullptr;
    for( auto *candidate : GImGui->Windows ) {
        if( candidate->WasActive && !( candidate->Flags & ImGuiWindowFlags_ChildWindow ) &&
            std::string( candidate->Name ).find( "模式设置##" ) == 0 ) {
            settings_window = candidate;
        }
    }
    REQUIRE( settings_window );
    CHECK( settings_window->Size.x == Approx( 1280 * .85f ).margin( 1.f ) );
    CHECK( settings_window->Size.y == Approx( 720 * .85f ).margin( 1.f ) );
    CHECK( settings_window->Pos.x == Approx( ( 1280 - settings_window->Size.x ) / 2 ).margin( 1.f ) );
    CHECK( settings_window->Pos.y == Approx( ( 720 - settings_window->Size.y ) / 2 ).margin( 1.f ) );
    const auto find_child = []( const char *name ) -> ImGuiWindow * {
        for( auto *candidate : GImGui->Windows ) {
            if( candidate->WasActive && std::string( candidate->Name ).find( name ) != std::string::npos ) {
                return candidate;
            }
        }
        return nullptr;
    };
    const auto click_settings = [&]( const ImVec2 &position ) {
        for( int frame = 0; frame < 4; ++frame ) {
            ImGui::GetIO().AddMousePosEvent( position.x, position.y );
            ImGui::GetIO().AddMouseButtonEvent( 0, frame == 1 );
            draw_menu( *settings );
        }
    };
    auto *body = find_child( "/##mode_content" );
    auto *navigation = find_child( "/##mode_navigation" );
    REQUIRE( body );
    REQUIRE( navigation );
    CHECK( navigation->Size.x == Approx( settings_window->InnerRect.GetWidth() * .24f ).margin( 9.f ) );
    const ImVec2 classic_choice( body->DC.CursorStartPos.x + 25.f,
                                body->DC.CursorStartPos.y + 3 * ( 18.f + 6.f ) - 6.f );
    click_settings( classic_choice );
    CHECK( selected_view == 0 );
    CHECK( selected_keys == 1 );
    CHECK( selected_style == 1 );
    const ImVec2 modern_choice( body->DC.CursorStartPos.x + body->WorkRect.GetWidth() * .75f,
                                classic_choice.y );
    click_settings( modern_choice );
    CHECK( selected_view == 1 );

    const auto open_combo = [&]( const ImVec2 &position ) {
        click_settings( position );
        REQUIRE( ImGui::IsPopupOpen( nullptr, ImGuiPopupFlags_AnyPopupId ) );
        auto *popup = GImGui->OpenPopupStack.back().Window;
        REQUIRE( popup );
        return popup;
    };
    // The combo uses its own ImGui window. Opening it is insufficient if the
    // settings window is then reordered above it and intercepts the click.
    auto *camera_popup = open_combo( ImVec2( body->Pos.x + 100.f, body->Pos.y + 317.f ) );
    const ImVec2 overhead_option( camera_popup->DC.CursorStartPos.x + 25.f,
                                 camera_popup->DC.CursorStartPos.y + 18.f + 6.f + 8.f );
    ImGui::GetIO().AddMousePosEvent( overhead_option.x, overhead_option.y );
    draw_menu( *settings );
    CHECK( GImGui->HoveredWindow == camera_popup );
    click_settings( overhead_option );
    CHECK_FALSE( ImGui::IsPopupOpen( nullptr, ImGuiPopupFlags_AnyPopupId ) );

    const char *settings_captures[] = {
        "mode-display.png", "mode-controls.png", "mode-interface.png", "mode-shaders.png",
        "mode-performance.png"
    };
    const float button_height = ( 18.f + 2 * 5.f ) * 1.35f;
    for( int page = 0; page < 5; ++page ) {
        const ImVec2 item( navigation->DC.CursorStartPos.x + 35.f,
                           navigation->DC.CursorStartPos.y + page * ( button_height + 6.f ) +
                           button_height * .5f );
        click_settings( item );
        auto *active_body = find_child( "/##mode_content" );
        REQUIRE( active_body );
        if( page != 0 ) {
            CHECK( active_body != body );
        }
        body = active_body;
        CHECK( active_body->OuterRectClipped.Max.y < settings_window->InnerRect.Max.y );
        save_menu( settings_captures[page] );
        if( page == 3 ) {
            for( float y : { 110.f, 198.f } ) {
                auto *popup = open_combo( ImVec2( body->Pos.x + 100.f, body->Pos.y + y ) );
                ImGui::GetIO().AddMousePosEvent( popup->DC.CursorStartPos.x + 20.f,
                                                 popup->DC.CursorStartPos.y + 8.f );
                draw_menu( *settings );
                CHECK( GImGui->HoveredWindow == popup );
                click_settings( ImVec2( settings_window->Pos.x - 10.f, settings_window->Pos.y + 20.f ) );
                CHECK_FALSE( ImGui::IsPopupOpen( nullptr, ImGuiPopupFlags_AnyPopupId ) );
            }
        }
    }
    REQUIRE( SDL_SetWindowSize( window.get(), 1920, 1080 ) );
    REQUIRE( SDL_SyncWindow( window.get() ) );
    settings->mark_resized();
    draw_menu( *settings );
    CHECK( settings_window->Size.x == Approx( 1920 * .85f ).margin( 1.f ) );
    CHECK( settings_window->Size.y == Approx( 1080 * .85f ).margin( 1.f ) );
    REQUIRE( SDL_SetWindowSize( window.get(), 1280, 720 ) );
    REQUIRE( SDL_SyncWindow( window.get() ) );
    CHECK( calendar::turn == time );
    CHECK( you.get_moves() == moves );
    CHECK( you.pos_abs() == position );
    settings.reset();
    auto popup = fp3d_quick_menu::create_dropdown( choices, point( 1250, 710 ) );
    for( int frame = 0; frame < 3; ++frame ) { draw_menu( *popup ); }
    ImGuiWindow *list = nullptr;
    for( auto *candidate : GImGui->Windows ) {
        if( candidate->WasActive && std::string( candidate->Name ).find( "地块操作##" ) == 0 ) {
            list = candidate;
        }
    }
    REQUIRE( list );
    CHECK( list->Pos.x >= 0 );
    CHECK( list->Pos.y >= 0 );
    CHECK( list->Pos.x + list->Size.x <= 1280 );
    CHECK( list->Pos.y + list->Size.y <= 720 );
    save_menu( "context-menu.png" );
    const ImVec2 first_row( list->Pos.x + 30, list->DC.CursorStartPos.y + 14 );
    for( int frame = 0; frame < 3; ++frame ) {
        ImGui::GetIO().AddMousePosEvent( first_row.x, first_row.y );
        ImGui::GetIO().AddMouseButtonEvent( 0, frame == 1 );
        draw_menu( *popup );
    }
    CHECK_FALSE( popup->get_is_open() );
    CHECK( popup->get_button_action() == "MODERN_2D_TRAVEL" );
    popup.reset();

    // Reproduce the retained native display buffer: do NOT clear between frames.
    // Old moving tooltips and arbitrary stale pixels must be erased by the menu.
    constexpr Uint32 map_pixel = 0x122216ff;
    std::vector<Uint32> map_pixels( 1280 * 720, map_pixel );
    SDL_Texture_Ptr map_image( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_RGBA8888,
                              SDL_TEXTUREACCESS_STATIC, 1280, 720 ) );
    REQUIRE( map_image );
    REQUIRE( SDL_UpdateTexture( map_image.get(), nullptr, map_pixels.data(), 1280 * 4 ) );
    auto retained = fp3d_quick_menu::create_dropdown( choices, point( 100, 100 ), 100,
                    std::move( map_image ) );
    for( int frame = 0; frame < 60; ++frame ) {
        ImGui::GetIO().AddMousePosEvent( 125 + frame * 3, 186 );
        draw_menu( *retained, false );
        if( frame == 5 ) {
            const SDL_Rect stale_tooltip{ 500, 210, 200, 30 };
            SDL_SetRenderDrawColor( renderer.get(), 255, 255, 255, 255 );
            RenderFillRectPixels( renderer.get(), &stale_tooltip );
        }
        if( frame > 6 ) {
            for( const auto *window : GImGui->Windows ) {
                CHECK_FALSE( ( window->Active && ( window->Flags & ImGuiWindowFlags_Tooltip ) ) );
            }
        }
    }
    REQUIRE( ReadRenderPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                  map_pixels.data(), 1280 * 4 ) == 0 );
    int stale_pixels = 0;
    for( int y = 0; y < 720; ++y ) {
        for( int x = 420; x < 1280; ++x ) {
            stale_pixels += map_pixels[y * 1280 + x] != map_pixel;
        }
    }
    CHECK( stale_pixels == 0 );
    CHECK( retained->get_is_open() );
    save_menu( "context-retained-clean.png" );
    retained.reset();

    auto responsive = fp3d_quick_menu::create_dropdown( choices, point( 100, 100 ) );
    for( int frame = 0; frame < 3; ++frame ) {
        ImGui::GetIO().AddMousePosEvent( 130, 122 );
        draw_menu( *responsive );
    }
    ImGui::GetIO().AddMouseButtonEvent( 0, true );
    draw_menu( *responsive );
    CHECK_FALSE( responsive->get_is_open() ); // Respond on press, before release.
    CHECK( responsive->get_button_action() == "MODERN_2D_TRAVEL" );
    ImGui::GetIO().AddMouseButtonEvent( 0, false );
    responsive.reset();
    // The SDL release arrives after the menu has already gone away.
    SDL_Event released{};
    released.type = SDL_EVENT_MOUSE_BUTTON_UP;
    released.button.windowID = SDL_GetWindowID( window.get() );
    released.button.button = SDL_BUTTON_LEFT;
    REQUIRE( ImGui_ImplSDL3_ProcessEvent( &released ) );

    {
        auto aiming = fp3d_quick_menu::create_dropdown( {
            { "瞄准", "进入原版瞄准。", ACTION_FIRE, nullptr, true }
        }, point( 100, 100 ) );
        ImGui::GetIO().AddMousePosEvent( 130, 122 );
        draw_menu( *aiming );
        draw_menu( *aiming );
        ImGui::GetIO().AddMouseButtonEvent( 0, true );
        draw_menu( *aiming );
        CHECK( aiming->get_is_open() );
        CHECK_FALSE( aiming->has_button_action() );
        ImGui::GetIO().AddMouseButtonEvent( 0, false );
        draw_menu( *aiming );
        CHECK_FALSE( aiming->get_is_open() );
        CHECK( aiming->get_button_action() == action_ident( ACTION_FIRE ) );
    }

    fp3d_compositor::set_input_context( "FP3D_WHEEL" );
    CHECK( fp3d_compositor::modern_2d_overlay_open() );
    auto wheel = fp3d_quick_menu::create();
    for( int frame = 0; frame < 3; ++frame ) {
        ImGui::GetIO().AddMousePosEvent( 640, 220 );
        draw_menu( *wheel );
    }
    save_menu( "q-wheel.png" );
    ImGui::GetIO().AddMouseButtonEvent( 0, true );
    draw_menu( *wheel );
    CHECK_FALSE( wheel->get_is_open() );
    CHECK( wheel->get_button_action() == action_ident( ACTION_THROW ) );
    ImGui::GetIO().AddMouseButtonEvent( 0, false );
    wheel.reset();

    auto disabled = fp3d_quick_menu::create_dropdown( choices, point( 100, 100 ) );
    for( int frame = 0; frame < 3; ++frame ) {
        ImGui::GetIO().AddMousePosEvent( 130, 186 );
        ImGui::GetIO().AddMouseButtonEvent( 0, frame == 1 );
        draw_menu( *disabled );
    }
    CHECK( disabled->get_is_open() );
    CHECK_FALSE( disabled->has_button_action() );
    ImGui::GetIO().AddMouseButtonEvent( 1, true );
    draw_menu( *disabled );
    CHECK_FALSE( disabled->get_is_open() );
    CHECK_FALSE( disabled->has_button_action() );
    disabled.reset();

    auto cancelled = fp3d_quick_menu::create_dropdown( choices, point( 100, 100 ) );
    ImGui::GetIO().AddMouseButtonEvent( 1, false );
    draw_menu( *cancelled );
    ImGui::GetIO().AddMousePosEvent( 600, 400 );
    ImGui::GetIO().AddMouseButtonEvent( 0, true );
    draw_menu( *cancelled );
    CHECK_FALSE( cancelled->get_is_open() );
    CHECK_FALSE( cancelled->has_button_action() );
    ImGui::GetIO().AddMouseButtonEvent( 0, false );
    cancelled.reset();

    // All shared interaction interfaces also work before any 3D scene exists.
    {
        restore_on_out_of_scope restore_rng( rng_get_engine() );
        npc speaker;
        speaker.name = "互动界面测试员";
        speaker.set_fac( faction_id( "no_faction" ) );
        speaker.setpos( here, you.pos_bub( here ) + tripoint_rel_ms::north );
        item stock( itype_id( "9mm" ), calendar::turn, 12 );
        stock.set_owner( speaker );
        const auto ammunition = speaker.i_add( stock );
        REQUIRE( ammunition );
        const auto bench = you.pos_bub( here ) + tripoint_rel_ms::east;
        here.furn_set( bench, furn_str_id( "f_counter" ) );
        for( int percent : { 100, 150 } ) {
            CAPTURE( percent );
            ImGui::GetIO().ClearInputMouse();
            fp3d_compositor::set_input_context( "FP3D_PANEL" );
            CHECK( fp3d_compositor::survival_menu_available() );
            auto crafting = fp3d_modern_ui::create( you, here, percent, false, 2, {},
                            std::nullopt, here.get_abs( bench ) );
            for( int frame = 0; frame < 3; ++frame ) { draw_menu( *crafting ); }
            save_menu( percent == 100 ? "workbench-100.png" : "workbench-150.png" );
            crafting.reset();

            int selected_reply = 0;
            {
                fp3d_dialogue::window dialogue( speaker, "你好，需要什么帮助？", selected_reply, true, percent );
                dialogue.set_responses( { { c_white, "a", "我想看看你的物品。" },
                                         { c_white, "b", "再见。" } }, { true, true } );
                CHECK( fp3d_compositor::survival_menu_available() );
                CHECK_FALSE( fp3d_compositor::turn_based() );
                for( int frame = 0; frame < 3; ++frame ) { draw_menu( dialogue ); }
                ImGuiWindow *responses = nullptr;
                for( auto *candidate : GImGui->Windows ) {
                    if( candidate->Active && std::string( candidate->Name ).find( "/回复选项_" ) != std::string::npos ) {
                        responses = candidate;
                    }
                }
                REQUIRE( responses );
                save_menu( percent == 100 ? "dialogue-100.png" : "dialogue-150.png" );
                const ImVec2 reply( responses->DC.CursorStartPos.x + 30,
                                    responses->DC.CursorStartPos.y + 20 * percent / 100.0f );
                for( int frame = 0; frame < 3; ++frame ) {
                    ImGui::GetIO().AddMousePosEvent( reply.x, reply.y );
                    ImGui::GetIO().AddMouseButtonEvent( 0, frame == 1 );
                    draw_menu( dialogue );
                }
                std::string action = "NONE";
                dialogue.handle_action( action );
                CHECK( action == "CONFIRM" );
                CHECK( selected_reply == 0 );
            }
            ImGui::GetIO().ClearInputMouse();
            {
                trade_ui model( you, speaker );
                fp3d_trade::window trading( model, percent );
                CHECK( fp3d_compositor::survival_menu_available() );
                CHECK_FALSE( fp3d_compositor::turn_based() );
                for( int frame = 0; frame < 3; ++frame ) { draw_menu( trading ); }
                save_menu( percent == 100 ? "trade-100.png" : "trade-150.png" );
                ImGui::GetIO().WantTextInput = false;
                trading.handle_action( "QUIT" );
                REQUIRE( trading.process_action() );
                CHECK( ammunition->charges == 12 );
            }
        }
        here.furn_set( bench, furn_str_id( "f_null" ) );
        fp3d_compositor::set_input_context( "FP3D_PANEL" );
    }
    CHECK( calendar::turn == time );
    CHECK( you.get_moves() == moves );
    CHECK( you.pos_abs() == position );
    CHECK( rng_get_engine() == random );
    // Focused text fields accept IME input in 2D too, but world input does not.
    fp3d_compositor::set_input_context( "FP3D_PANEL" );
    CHECK_FALSE( fp3d_compositor::modern_2d_overlay_open() );
    ImGui::GetIO().WantTextInput = true;
    CHECK( fp3d_compositor::menu_text_input() );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    CHECK_FALSE( fp3d_compositor::menu_text_input() );
    ImGui::GetIO().WantTextInput = false;

    // Use real character art and an equipable overlay in the SDL renderer.
    you.name = "超长中文姓名用于检查人物档案在小窗口内是否正确换行";
    you.male = true;
    const char *external_tiles = std::getenv( "CDDA_SURVIVAL_QA_TILESET" );
    const std::string primary_tiles = external_tiles ? external_tiles : "UltimateCataclysm";
    override_option tiles( "TILES", primary_tiles );
    tilecontext->load_tileset( primary_tiles, false, false, false );
    const auto texture_pixels = [&]( SDL_Texture *texture ) {
        SDL_Texture *previous = SDL_GetRenderTarget( renderer.get() );
        REQUIRE( SDL_SetRenderTarget( renderer.get(), texture ) );
        on_out_of_scope restore_target( [&]() {
            SDL_SetRenderTarget( renderer.get(), previous );
        } );
        float w = 0;
        float h = 0;
        REQUIRE( SDL_GetTextureSize( texture, &w, &h ) );
        std::vector<Uint32> pixels( static_cast<size_t>( w * h ) );
        REQUIRE( ReadRenderPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                  pixels.data(), static_cast<int>( w ) * 4 ) == 0 );
        return pixels;
    };
    auto bare_portrait = fp3d_materials::character_icon( you );
    REQUIRE( bare_portrait );
    const auto bare_pixels = texture_pixels( bare_portrait.get() );
    const auto backpack = you.wear_item( item( itype_id( "backpack" ) ), false );
    REQUIRE( backpack );
    auto worn_portrait = fp3d_materials::character_icon( you );
    REQUIRE( worn_portrait );
    CHECK( texture_pixels( worn_portrait.get() ) != bare_pixels );
    you.toggle_trait( trait_id( "QUICK" ) );
    {
        override_option alternative( "TILES", "Chibi_Ultica" );
        tilecontext->load_tileset( "Chibi_Ultica", false, false, false );
        auto alternate_portrait = fp3d_materials::character_icon( you );
        CHECK( alternate_portrait );
    }
    tilecontext->load_tileset( primary_tiles, false, false, false );

    const auto before_turn = calendar::turn;
    const int before_moves = you.get_moves();
    const auto before_rng = rng_get_engine();
    const auto original_background = ImGui::GetColorU32( ImGuiCol_WindowBg );
    const std::array<point, 3> resolutions = { point( 640, 480 ), point( 1280, 720 ),
                                              point( 1920, 1080 ) };
    for( const point &resolution : resolutions ) {
        REQUIRE( SDL_SetWindowSize( window.get(), resolution.x, resolution.y ) );
        REQUIRE( SDL_SyncWindow( window.get() ) );
        // Hidden SDL windows may keep the original swapchain dimensions after
        // resizing. Render to a matching target so the screenshot is exact.
        SDL_Texture_Ptr frame_target( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, resolution.x, resolution.y ) );
        REQUIRE( frame_target );
        SDL_Texture *previous_target = SDL_GetRenderTarget( renderer.get() );
        REQUIRE( SDL_SetRenderTarget( renderer.get(), frame_target.get() ) );
        on_out_of_scope restore_target( [&]() {
            SDL_SetRenderTarget( renderer.get(), previous_target );
        } );
        for( const int percent : { 100, 125, 150 } ) {
            for( const int camera_mode : { 0, 1, 2 } ) {
                CAPTURE( resolution, percent, camera_mode );
                fp3d_compositor::apply_modes( camera_mode != 0, true );
                if( camera_mode != 0 &&
                    fp3d_compositor::preferred_overhead() != ( camera_mode == 2 ) ) {
                    fp3d_compositor::toggle_camera();
                }
                fp3d_compositor::set_input_context( "FP3D_PANEL" );
                auto profile = fp3d_modern_ui::create( you, here, percent, false, 1 );
                for( int frame = 0; frame < 3; ++frame ) {
                    draw_menu( *profile );
                }
                auto *profile_child = find_child( "/人物档案" );
                auto *center_child = find_child( "/人物分类" );
                auto *detail_child = find_child( "/人物详情" );
                REQUIRE( profile_child );
                REQUIRE( center_child );
                REQUIRE( detail_child );
                CHECK( profile_child->Pos.x < center_child->Pos.x );
                CHECK( center_child->Pos.x < detail_child->Pos.x );
                CHECK( detail_child->Pos.x + detail_child->Size.x <= resolution.x );
                CHECK( detail_child->Pos.y + detail_child->Size.y <= resolution.y );
                CHECK( ImGui::GetColorU32( ImGuiCol_WindowBg ) == original_background );
                if( resolution == point( 1280, 720 ) && percent == 100 && camera_mode == 0 ) {
                    const char *shots[] = { "character-body.png", "character-equipment.png",
                                           "character-attributes.png", "character-traits.png" };
                    auto *tabs = GImGui->TabBars.GetByKey( center_child->GetID( "人物分类" ) );
                    REQUIRE( tabs );
                    REQUIRE( tabs->Tabs.Size == 4 );
                    for( int i = 0; i < tabs->Tabs.Size; ++i ) {
                        const auto &tab = tabs->Tabs[i];
                        const ImGuiID tab_id = tab.ID;
                        const float x = tabs->BarRect.Min.x + tab.Offset + tab.Width * .5f;
                        const float y = ( tabs->BarRect.Min.y + tabs->BarRect.Max.y ) * .5f;
                        for( int frame = 0; frame < 4; ++frame ) {
                            ImGui::GetIO().AddMousePosEvent( x, y );
                            ImGui::GetIO().AddMouseButtonEvent( 0, frame == 1 );
                            draw_menu( *profile );
                        }
                        CHECK( tabs->SelectedTabId == tab_id );
                        if( i == 1 ) {
                            const float before_height = detail_child->ContentSize.y;
                            const ImVec2 first_gear( center_child->DC.CursorStartPos.x + 30.f,
                                                     center_child->DC.CursorStartPos.y + 135.f );
                            for( int frame = 0; frame < 4; ++frame ) {
                                ImGui::GetIO().AddMousePosEvent( first_gear.x, first_gear.y );
                                ImGui::GetIO().AddMouseButtonEvent( 0, frame == 1 );
                                draw_menu( *profile );
                            }
                            CHECK( detail_child->ContentSize.y > before_height );
                        }
                        save_menu( shots[i] );
                    }
                } else if( resolution == point( 640, 480 ) && percent == 150 && camera_mode == 0 ) {
                    save_menu( "character-480p-150.png" );
                } else if( resolution == point( 1920, 1080 ) && percent == 125 && camera_mode == 2 ) {
                    save_menu( "character-1080p-125.png" );
                }
            }
        }
    }
    CHECK( calendar::turn == before_turn );
    CHECK( you.get_moves() == before_moves );
    CHECK( rng_get_engine() == before_rng );

    fp3d_compositor::apply_modes( false, true );
    fp3d_compositor::set_input_context( "FP3D_PANEL" );
    const tripoint_bub_ms ground = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.ter_set( ground, ter_str_id( "t_floor" ) );
    here.furn_set( ground, furn_str_id( "f_null" ) );
    here.add_item( ground, item( itype_id( "rock" ) ), 144 );
    here.build_map_cache( ground.z() );
    here.update_visibility_cache( ground.z() );
    REQUIRE( here.i_at( ground ).size() >= 128 );
    REQUIRE( SDL_SetWindowSize( window.get(), 1280, 720 ) );
    REQUIRE( SDL_SyncWindow( window.get() ) );
    auto inventory = fp3d_modern_ui::create( you, here, 100, false, 0 );
    for( int frame = 0; frame < 3; ++frame ) {
        draw_menu( *inventory );
    }
    auto *nearby = find_child( "/##拾取列表" );
    REQUIRE( nearby );
    CHECK( nearby->ScrollMax.y > 1000 );
    save_menu( "inventory-long-list.png" );
    inventory.reset();

    item kit( itype_id( "bottle_plastic" ) );
    REQUIRE( kit.put_in( item( itype_id( "bandages" ) ), pocket_type::CONTAINER ).success() );
    kit.set_var( "fp3d_inventory_tree_open", "1" );
    REQUIRE( ( **backpack ).put_in( kit, pocket_type::CONTAINER ).success() );
    REQUIRE( ( **backpack ).put_in( item( itype_id( "hammer" ) ), pocket_type::CONTAINER ).success() );
    inventory = fp3d_modern_ui::create( you, here, 100, false, 0 );
    for( int frame = 0; frame < 3; ++frame ) {
        draw_menu( *inventory );
    }
    auto *carried = find_child( "/我的物品" );
    REQUIRE( carried );
    const float folded_height = carried->ContentSize.y;
    ( **backpack ).set_var( "fp3d_inventory_tree_open", "1" );
    for( int frame = 0; frame < 3; ++frame ) {
        draw_menu( *inventory );
    }
    CHECK( carried->ContentSize.y > folded_height );
    const float folded_pocket_height = carried->ContentSize.y;
    ( **backpack ).set_var( "fp3d_inventory_pocket_open_0", "1" );
    for( int frame = 0; frame < 3; ++frame ) {
        draw_menu( *inventory );
    }
    CHECK( carried->ContentSize.y > folded_pocket_height );
    save_menu( "inventory-compact-tree.png" );

    const auto inventory_mouse = [&]( const ImVec2 &position, bool down ) {
        ImGui::GetIO().AddMousePosEvent( position.x, position.y );
        ImGui::GetIO().AddMouseButtonEvent( ImGuiMouseButton_Left, down );
        draw_menu( *inventory );
    };
    const ImVec2 carried_first( carried->DC.CursorStartPos.x + 65, carried->DC.CursorStartPos.y + 221 );
    const ImVec2 carried_second( carried_first.x + 95, carried_first.y + 38 );
    inventory_mouse( carried_first, false );
    inventory_mouse( carried_first, true );
    inventory_mouse( carried_second, true );
    inventory_mouse( carried_second, false );
    CHECK( std::count_if( carried->DrawList->VtxBuffer.begin(), carried->DrawList->VtxBuffer.end(),
    []( const ImDrawVert &vertex ) {
        return vertex.col == fp3d_survival_style::selected;
    } ) >= 24 );
    save_menu( "inventory-box-selection.png" );
    inventory_mouse( carried_first, false );
    inventory_mouse( carried_first, true );
    inventory_mouse( ImVec2( carried_first.x + 80, carried_first.y ), true );
    REQUIRE( ImGui::GetDragDropPayload() );
    CHECK( ImGui::GetDragDropPayload()->IsDataType( "FP3D_ITEM" ) );
    inventory_mouse( carried_first, false );
    inventory.reset();

    here.i_clear( ground );
    item pickup_bottle( itype_id( "bottle_plastic" ) );
    item bandage( itype_id( "bandages" ) );
    bandage.set_damage( bandage.max_damage() / 2 );
    REQUIRE( pickup_bottle.put_in( bandage, pocket_type::CONTAINER ).success() );
    REQUIRE( pickup_bottle.put_in( item( itype_id( "needle_bone" ) ), pocket_type::CONTAINER ).success() );
    pickup_bottle.set_var( "fp3d_inventory_tree_open", "0" );
    here.add_item_or_charges( ground, pickup_bottle );
    here.add_item_or_charges( ground, bandage );
    here.add_item_or_charges( ground, item( itype_id( "needle_bone" ) ) );
    here.furn_set( ground, furn_str_id( "f_locker" ) );
    here.build_map_cache( ground.z() );
    here.update_visibility_cache( ground.z() );
    auto pickup = fp3d_modern_ui::create( you, here, 100, false, 0, {}, here.get_abs( ground ) );
    const auto mouse_frame = [&]( const ImVec2 &position, bool down ) {
        ImGui::GetIO().AddMousePosEvent( position.x, position.y );
        ImGui::GetIO().AddMouseButtonEvent( 0, down );
        draw_menu( *pickup );
    };
    for( int frame = 0; frame < 3; ++frame ) {
        draw_menu( *pickup );
    }
    auto *sources = find_child( "/##拾取来源" );
    auto *pickup_list = find_child( "/##拾取列表" );
    REQUIRE( sources );
    REQUIRE( pickup_list );
    const float ground_list_height = pickup_list->ContentSize.y;
    const float icon_stride = 44.f + 8.f;
    CHECK( sources->ContentSize.x == Approx( 44.f + icon_stride ).margin( 1.f ) );
    const ImVec2 locker_source( sources->DC.CursorStartPos.x + icon_stride + 22,
                               sources->DC.CursorStartPos.y + 22 );
    mouse_frame( locker_source, false );
    mouse_frame( locker_source, true );
    mouse_frame( locker_source, false );
    draw_menu( *pickup );
    CHECK( pickup_list->ContentSize.y > ground_list_height );
    const float start_y = pickup_list->DC.CursorStartPos.y;
    const ImVec2 first_item( pickup_list->DC.CursorStartPos.x + 30, start_y + 67 );
    const ImVec2 second_item( first_item.x + 100, start_y + 108 );
    mouse_frame( first_item, false );
    mouse_frame( first_item, true );
    mouse_frame( second_item, true );
    mouse_frame( second_item, false );
    size_t highlights = 0;
    for( const auto &vertex : pickup_list->DrawList->VtxBuffer ) {
        highlights += vertex.col == fp3d_survival_style::selected;
    }
    CHECK( highlights >= 24 );
    save_menu( "pickup-box-selection.png" );
    mouse_frame( first_item, false );
    mouse_frame( first_item, true );
    mouse_frame( ImVec2( first_item.x - 80, first_item.y ), true );
    REQUIRE( ImGui::GetDragDropPayload() );
    CHECK( ImGui::GetDragDropPayload()->IsDataType( "FP3D_ITEM" ) );
    mouse_frame( first_item, false );

    for( const int percent : { 125, 150 } ) {
        pickup.reset();
        pickup = fp3d_modern_ui::create( you, here, percent, false, 0, {}, here.get_abs( ground ) );
        for( int frame = 0; frame < 3; ++frame ) {
            draw_menu( *pickup );
        }
        sources = find_child( "/##拾取来源" );
        pickup_list = find_child( "/##拾取列表" );
        REQUIRE( sources );
        REQUIRE( pickup_list );
        CHECK( sources->CalcFontSize() == Approx( GImGui->FontBaseSize * percent / 100.f ) );
        CHECK( pickup_list->CalcFontSize() == Approx( GImGui->FontBaseSize * percent / 100.f ) );
        const float empty = pickup_list->ContentSize.y;
        const float factor = percent / 100.f;
        const ImVec2 scaled_locker( sources->DC.CursorStartPos.x + ( icon_stride + 22 ) * factor,
                                    sources->DC.CursorStartPos.y + 22 * factor );
        mouse_frame( scaled_locker, false );
        mouse_frame( scaled_locker, true );
        mouse_frame( scaled_locker, false );
        draw_menu( *pickup );
        CHECK( pickup_list->ContentSize.y > empty );
        save_menu( percent == 125 ? "pickup-125.png" : "pickup-150.png" );
    }

    pickup.reset();
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    fp3d_compositor::apply_modes( true, true );
    if( fp3d_compositor::view_3d() ) {
        fp3d_compositor::restore_view_mode( "overhead" );
        REQUIRE( fp3d_compositor::overhead() );
        fp3d_compositor::aiming::set_active( true );
        const auto aim_tile = here.get_abs( ground );
        fp3d_compositor::aiming::set_target( aim_tile );
        const int moves_before_orbit = you.get_moves();
        const auto time_before_orbit = calendar::turn;
        const float yaw_before = fp3d_compositor::overhead_azimuth();
        const float distance_before = fp3d_compositor::overhead_distance();
        const bool captured_mouse = ImGui::GetIO().WantCaptureMouse;
        ImGui::GetIO().WantCaptureMouse = false;
        SDL_Event middle{};
        middle.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        middle.button.windowID = SDL_GetWindowID( window.get() );
        middle.button.button = SDL_BUTTON_MIDDLE;
        middle.button.x = 320;
        middle.button.y = 240;
        REQUIRE( fp3d_compositor::process_event( middle ) );
        SDL_Event drag{};
        drag.type = SDL_EVENT_MOUSE_MOTION;
        drag.motion.windowID = middle.button.windowID;
        drag.motion.state = SDL_BUTTON_MMASK;
        drag.motion.x = 350;
        drag.motion.y = 240;
        drag.motion.xrel = 30;
        CHECK( fp3d_compositor::process_event( drag ) );
        CHECK( fp3d_compositor::overhead_azimuth() == Approx( yaw_before - 10.5f ) );
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.windowID = middle.button.windowID;
        wheel.wheel.mouse_x = 350;
        wheel.wheel.mouse_y = 240;
        wheel.wheel.y = 1;
        CHECK( fp3d_compositor::process_event( wheel ) );
        CHECK( fp3d_compositor::overhead_distance() == Approx( distance_before - 1.5f ) );
        ImGui::GetIO().WantCaptureMouse = true;
        CHECK_FALSE( fp3d_compositor::process_event( wheel ) );
        ImGui::GetIO().WantCaptureMouse = false;
        wheel.wheel.y = -1;
        CHECK( fp3d_compositor::process_event( wheel ) );
        drag.motion.xrel = -30;
        CHECK( fp3d_compositor::process_event( drag ) );
        middle.type = SDL_EVENT_MOUSE_BUTTON_UP;
        CHECK( fp3d_compositor::process_event( middle ) );
        CHECK_FALSE( fp3d_compositor::process_event( drag ) );
        middle.button.button = SDL_BUTTON_LEFT;
        CHECK_FALSE( fp3d_compositor::process_event( middle ) );
        middle.button.button = SDL_BUTTON_RIGHT;
        CHECK_FALSE( fp3d_compositor::process_event( middle ) );
        CHECK( fp3d_compositor::aiming::target() == aim_tile );
        CHECK( you.get_moves() == moves_before_orbit );
        CHECK( calendar::turn == time_before_orbit );
        ImGui::GetIO().WantCaptureMouse = captured_mouse;
        fp3d_compositor::aiming::set_active( false );
        set_time_to_day();
        here.furn_set( ground, furn_str_id( "f_locker" ) );
        here.build_map_cache( ground.z() );
        here.update_visibility_cache( ground.z() );
        fp3d_compositor::publish_camera( you, here, true );
        const auto submit_world = [&]() {
            fp3d_compositor::compose();
            fp3d_compositor::gpu_world_qa_after_present( SDL_RenderPresent( renderer.get() ) );
            REQUIRE( SDL_WaitForGPUIdle( SDL_GetGPURendererDevice( renderer.get() ) ) );
        };
        submit_world();
        REQUIRE( fp3d_compositor::copy_view() );
        fp3d_compositor::set_input_context( "FP3D_PANEL" );
        pickup = fp3d_modern_ui::create( you, here, 100, false, 0, {}, here.get_abs( ground ) );
        for( int frame = 0; frame < 3; ++frame ) {
            draw_menu( *pickup );
        }
        sources = find_child( "/##拾取来源" );
        REQUIRE( sources );
        const auto white_pixels = [&]() {
            REQUIRE( fp3d_compositor::draw_menu_background( 0, 0, 1280, 720 ) );
            SDL_Surface_Ptr frame( SDL_RenderReadPixels( renderer.get(), nullptr ) );
            REQUIRE( frame );
            SDL_Surface_Ptr pixels( SDL_ConvertSurface( frame.get(), SDL_PIXELFORMAT_RGBA32 ) );
            REQUIRE( pixels );
            size_t count = 0;
            for( int y = 0; y < pixels->h; ++y ) {
                const auto *row = static_cast<const Uint8 *>( pixels->pixels ) + y * pixels->pitch;
                for( int x = 0; x < pixels->w; ++x ) {
                    const Uint8 *p = row + x * 4;
                    count += p[0] >= 250 && p[1] >= 250 && p[2] >= 250;
                }
            }
            return count;
        };
        const size_t normal_pixels = white_pixels();
        const ImVec2 locker_button( sources->DC.CursorStartPos.x + icon_stride + 22,
                                   sources->DC.CursorStartPos.y + 22 );
        mouse_frame( locker_button, false );
        mouse_frame( locker_button, true );
        mouse_frame( locker_button, false );
        submit_world();
        const size_t highlighted_pixels = white_pixels();
        CHECK( highlighted_pixels > normal_pixels + 100 );
        draw_menu( *pickup, false );
        const auto *pickup_window = sources->RootWindow;
        REQUIRE( pickup_window );
        CHECK( pickup_window->Size.y == Approx( ImGui::GetIO().DisplaySize.y * .93f ).margin( 1.f ) );
        CHECK( pickup_window->Pos.y == Approx( ImGui::GetIO().DisplaySize.y * .035f ).margin( 1.f ) );
        save_menu( "pickup-highlight-3d.png" );
        pickup.reset();
        submit_world();
        CHECK( white_pixels() + 100 < highlighted_pixels );

        const auto player = you.pos_bub( here );
        here.furn_set( ground, furn_str_id( "f_null" ) );
        for( int x : { -2, -1, 1, 2 } ) {
            here.ter_set( ground + tripoint_rel_ms( x, 0, 0 ), ter_str_id( "t_wall" ) );
        }
        for( int x = -3; x <= 3; ++x ) {
            here.ter_set( ground + tripoint_rel_ms( x, -3, 0 ), ter_str_id( "t_pavement" ) );
        }
        here.ter_set( ground, ter_str_id( "t_curtains" ) );
        you.setpos( here, ground, false );
        fp3d_snapshot::prepare_view( you, here );
        fp3d_compositor::set_curtain_peek( here.get_abs( ground ), point( 0, -1 ) );
        fp3d_compositor::set_input_context( "LOOK" );
        REQUIRE( fp3d_compositor::active() );
        fp3d_compositor::publish_camera( you, here, true );
        submit_world();
        REQUIRE( fp3d_compositor::draw_menu_background( 0, 0, 1280, 720 ) );
        save_menu( "curtain-peek-overhead.png" );
        you.setpos( here, player, false );
        fp3d_compositor::set_curtain_peek( std::nullopt );
        CHECK_FALSE( fp3d_compositor::active() );
        CHECK( fp3d_compositor::draw_menu_background( 0, 0, 1280, 720 ) );
        fp3d_compositor::set_input_context( "DEFAULTMODE" );
        CHECK( fp3d_compositor::copy_view() );
        fp3d_snapshot::prepare_view( you, here );
        fp3d_compositor::publish_camera( you, here, true );
        submit_world();

        you.setpos( here, ground, false );
        fp3d_snapshot::prepare_view( you, here );
        fp3d_compositor::set_curtain_peek( here.get_abs( ground ), point( 0, -1 ) );
        fp3d_compositor::set_input_context( "LOOK" );
        fp3d_compositor::restore_view_mode( "first_person" );
        REQUIRE( fp3d_compositor::active() );
        fp3d_compositor::publish_camera( you, here, false );
        submit_world();
        REQUIRE( fp3d_compositor::draw_menu_background( 0, 0, 1280, 720 ) );
        save_menu( "curtain-peek-first-person.png" );
        you.setpos( here, player, false );
        fp3d_compositor::set_curtain_peek( std::nullopt );
        CHECK_FALSE( fp3d_compositor::active() );
        CHECK( fp3d_compositor::draw_menu_background( 0, 0, 1280, 720 ) );
    }
}
#endif
