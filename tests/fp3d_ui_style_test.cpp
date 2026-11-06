#include "fp3d_compositor.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <filesystem>
#include <vector>
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "fp3d_snapshot.h"
#include "options_helpers.h"
#include "sdl_wrappers.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_imgui.h"
#include "color.h"
#include "fp3d_modern_ui.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "fp3d_quick_menu.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
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

TEST_CASE( "fp3d_ui_style_is_independent_of_view_and_controls", "[.fp3d_ui_style]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "UI style regression", SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
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
                REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
                const SDL_Rect sidebar_pixel{ 600, 240, 1, 1 };
                Uint32 before = 0;
                Uint32 after = 0;
                REQUIRE( SDL_RenderReadPixels( renderer.get(), &sidebar_pixel,
                                              SDL_PIXELFORMAT_RGBA8888, &before, 4 ) == 0 );
                fp3d_compositor::compose();
                REQUIRE( SDL_RenderReadPixels( renderer.get(), &sidebar_pixel,
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
                settings_key.type = SDL_KEYDOWN;
                settings_key.key.keysym.sym = SDLK_F5;
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
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "2D edge coordinates regression", SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_HIDDEN ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1, SDL_RENDERER_SOFTWARE ) );
    REQUIRE( renderer );
    for( const int scale : { 1, 2, 4 } ) {
        REQUIRE( SDL_RenderSetLogicalSize( renderer.get(), 640 * scale, 480 * scale ) == 0 );
        const point origin( 80 * scale, 24 * scale );
        const point size( 480 * scale, 432 * scale );
        for( const point mouse : { point( 80, 240 ), point( 559, 240 ), point( 320, 24 ),
                                  point( 320, 455 ), point( 80, 24 ), point( 559, 24 ),
                                  point( 80, 455 ), point( 559, 455 ) } ) {
            CAPTURE( scale, mouse );
            float x = 0;
            float y = 0;
            SDL_RenderWindowToLogical( renderer.get(), mouse.x, mouse.y, &x, &y );
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

TEST_CASE( "modern_2d_survival_panel_renders_without_a_3d_scene", "[.modern_2d_panel]" )
{
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_SetHint( SDL_HINT_RENDER_DRIVER, "opengl" );
    SDL_Window_Ptr window( SDL_CreateWindow( "2D survival panel regression", SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
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
    REQUIRE( ImGui_ImplSDL2_InitForSDLRenderer( window.get(), renderer.get() ) );
    REQUIRE( ImGui_ImplSDLRenderer2_Init( renderer.get() ) );
    on_out_of_scope gui_cleanup( []() {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
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
            REQUIRE( SDL_SetRenderDrawColor( renderer.get(), 18, 22, 18, 255 ) == 0 );
            REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            panel->draw();
            ImGui::Render();
            REQUIRE( ImGui::GetDrawData()->TotalVtxCount > 0 );
            ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
            if( frame == 2 ) {
                if( const char *directory = std::getenv( "CDDA_2D_PANEL_QA_DIR" ) ) {
                    std::filesystem::create_directories( directory );
                    std::vector<Uint32> pixels( 1280 * 720 );
                    REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                                  pixels.data(), 1280 * 4 ) == 0 );
                    SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(), 1280,
                                             720, 32, 1280 * 4, SDL_PIXELFORMAT_RGBA8888 ) );
                    REQUIRE( picture );
                    const auto filename = std::filesystem::path( directory ) /
                                          ( "page-" + std::to_string( page ) + ".png" );
                    CHECK( IMG_SavePNG( picture.get(), filename.u8string().c_str() ) == 0 );
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
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        menu.draw();
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData(), renderer.get() );
    };
    const auto save_menu = [&]( const char *filename ) {
        if( const char *directory = std::getenv( "CDDA_2D_PANEL_QA_DIR" ) ) {
            std::vector<Uint32> pixels( 1280 * 720 );
            REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
                                          pixels.data(), 1280 * 4 ) == 0 );
            SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormatFrom( pixels.data(), 1280,
                                     720, 32, 1280 * 4, SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( picture );
            const auto path = std::filesystem::path( directory ) / filename;
            CHECK( IMG_SavePNG( picture.get(), path.u8string().c_str() ) == 0 );
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
        if( candidate->WasActive && std::string( candidate->Name ).find( "模式设置##" ) == 0 ) {
            settings_window = candidate;
        }
    }
    REQUIRE( settings_window );
    const ImVec2 classic_choice( settings_window->Pos.x + 45,
                                settings_window->DC.CursorStartPos.y +
                                ( settings_window->Size.y - 43 ) * .28f + 8 );
    for( int frame = 0; frame < 3; ++frame ) {
        ImGui::GetIO().AddMousePosEvent( classic_choice.x, classic_choice.y );
        ImGui::GetIO().AddMouseButtonEvent( 0, frame == 1 );
        draw_menu( *settings );
    }
    CHECK( selected_view == 0 );
    CHECK( selected_keys == 1 );
    CHECK( selected_style == 1 );
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
    REQUIRE( SDL_UpdateTexture( map_image.get(), nullptr, map_pixels.data(), 1280 * 4 ) == 0 );
    auto retained = fp3d_quick_menu::create_dropdown( choices, point( 100, 100 ), 100,
                    std::move( map_image ) );
    for( int frame = 0; frame < 60; ++frame ) {
        ImGui::GetIO().AddMousePosEvent( 125 + frame * 3, 186 );
        draw_menu( *retained, false );
        if( frame == 5 ) {
            const SDL_Rect stale_tooltip{ 500, 210, 200, 30 };
            SDL_SetRenderDrawColor( renderer.get(), 255, 255, 255, 255 );
            SDL_RenderFillRect( renderer.get(), &stale_tooltip );
        }
        if( frame > 6 ) {
            for( const auto *window : GImGui->Windows ) {
                CHECK_FALSE( ( window->Active && ( window->Flags & ImGuiWindowFlags_Tooltip ) ) );
            }
        }
    }
    REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA8888,
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
    released.type = SDL_MOUSEBUTTONUP;
    released.button.windowID = SDL_GetWindowID( window.get() );
    released.button.button = SDL_BUTTON_LEFT;
    REQUIRE( ImGui_ImplSDL2_ProcessEvent( &released ) );

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
}
#endif
