#include "fp3d_compositor.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "cata_imgui.h"
#include "cached_options.h"
#include "cata_scope_helpers.h"
#include "color_loader.h"
#include "fp3d_simple_ui.h"
#include "fp3d_abilities.h"
#include "fp3d_modern_ui.h"
#include "bionics.h"
#include "character_martial_arts.h"
#include "fp3d_snapshot.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#include "input_popup.h"
#include "popup.h"
#include "rng.h"
#include "calendar.h"
#include "sdl_geometry.h"
#include "sdltiles.h"
#include "uilist.h"
#include "ui_manager.h"
#include "avatar.h"
#include "inventory_ui.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "ui_iteminfo.h"

namespace
{
class visible_picker : public inventory_pick_selector
{
    public:
        using inventory_pick_selector::inventory_pick_selector;
        using inventory_selector::simple_pick_entries;
};

class visible_multi : public inventory_multiselector
{
    public:
        using inventory_multiselector::inventory_multiselector;
        using inventory_selector::create_or_get_ui_adaptor;
        using inventory_selector::take_simple_input;
        void apply( const inventory_input &input ) {
            on_input( input );
        }
        const auto &selection() const {
            return to_use;
        }
};

class visible_ammo : public ammo_inventory_selector
{
    public:
        using ammo_inventory_selector::ammo_inventory_selector;
        using inventory_selector::create_or_get_ui_adaptor;
        using inventory_selector::take_simple_input;
};

class visible_unload : public unload_selector
{
    public:
        using unload_selector::unload_selector;
        using inventory_selector::create_or_get_ui_adaptor;
        using inventory_selector::take_simple_input;
};

class visible_examiner : public inventory_examiner
{
    public:
        using inventory_examiner::inventory_examiner;
        using inventory_selector::create_or_get_ui_adaptor;
};

class picker_preset : public inventory_selector_preset
{
    public:
        picker_preset() {
            append_cell( []( const item_location & ) {
                return "native value";
            }, "native field" );
        }
        bool is_shown( const item_location &location ) const override {
            return location->typeId() == itype_id( "apple" ) || location->typeId() == itype_id( "lighter" );
        }
        std::string get_denial( const item_location &location ) const override {
            return location->typeId() == itype_id( "lighter" ) ? "native denial" : "";
        }
};

class ammo_preset : public inventory_selector_preset
{
    public:
        bool is_shown( const item_location &location ) const override {
            return location->typeId() == itype_id( "9mm" );
        }
};

class visible_query : public query_popup
{
    public:
        using query_popup::create_or_get_impl;
};

ImGuiWindow *find_window( const std::string &name )
{
    for( ImGuiWindow *window : GImGui->Windows ) {
        if( window->Active && std::string( window->Name ).find( name ) != std::string::npos ) {
            return window;
        }
    }
    return nullptr;
}
}

TEST_CASE( "fp3d_simple_item_picker_preserves_native_filter_and_denials", "[fp3d][fp3d_picker]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    REQUIRE( you.wear_item( item( itype_id( "backpack" ), calendar::turn ) ) );
    const item_location apple = you.i_add( item( itype_id( "apple" ), calendar::turn ) );
    const item_location lighter = you.i_add( item( itype_id( "lighter" ), calendar::turn ) );
    const item_location rock = you.i_add( item( itype_id( "rock" ), calendar::turn ) );
    REQUIRE( apple );
    REQUIRE( lighter );
    REQUIRE( rock );
    const auto before_time = calendar::turn;
    const auto before_rng = rng_get_engine();
    const int before_moves = you.get_moves();
    picker_preset preset;
    visible_picker selector( you, preset );
    selector.add_character_items( you );
    std::vector<item_location> locations;
    const auto rows = selector.simple_pick_entries( locations );
    REQUIRE( rows.size() == 2 );
    REQUIRE( locations.size() == rows.size() );
    bool found_denial = false;
    bool found_apple = false;
    for( size_t i = 0; i < rows.size(); ++i ) {
        CHECK( rows[i].retval == static_cast<int>( i ) );
        CHECK( rows[i].desc.find( "native field: native value" ) != std::string::npos );
        if( locations[i] == apple ) {
            CHECK( rows[i].enabled );
            found_apple = true;
        } else if( locations[i] == lighter ) {
            CHECK_FALSE( rows[i].enabled );
            CHECK( rows[i].desc.find( "native denial" ) != std::string::npos );
            found_denial = true;
        } else {
            FAIL( "unexpected item escaped native preset" );
        }
    }
    CHECK( found_apple );
    CHECK( found_denial );
    selector.set_filter( "apple" );
    const auto filtered = selector.simple_pick_entries( locations );
    REQUIRE( filtered.size() == 1 );
    CHECK( locations.front() == apple );
    CHECK( apple );
    CHECK( lighter );
    CHECK( rock );
    CHECK( calendar::turn == before_time );
    CHECK( rng_get_engine() == before_rng );
    CHECK( you.get_moves() == before_moves );
}

TEST_CASE( "fp3d_ability_controls_use_character_state_and_reject_unavailable_requests",
           "[fp3d][fp3d_abilities]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    bool close = false;
    const trait_id web( "WEB_WEAVER" );
    CHECK_FALSE( fp3d_abilities::execute( you, web, close ) );
    you.set_mutation( web );
    you.set_thirst( 450 );
    CHECK_FALSE( fp3d_abilities::execute( you, web, close ) );
    CHECK_FALSE( you.has_active_mutation( web ) );
    you.set_thirst( 0 );
    REQUIRE( fp3d_abilities::execute( you, web, close ) );
    CHECK( you.has_active_mutation( web ) );
    REQUIRE( fp3d_abilities::execute( you, web, close ) );
    CHECK_FALSE( you.has_active_mutation( web ) );

    const bionic_id light( "bio_flashlight" );
    you.add_bionic( bionic_id( "bio_power_storage" ) );
    you.add_bionic( light );
    const auto installed = you.find_bionic_by_type( light );
    REQUIRE( installed );
    const fp3d_abilities::bionic_request toggle{ ( *installed )->get_uid(),
            fp3d_abilities::bionic_control::power };
    you.set_power_level( units::energy() );
    CHECK_FALSE( fp3d_abilities::execute( you, toggle, close ) );
    you.set_power_level( you.get_max_power_level() );
    const auto charged = you.get_power_level();
    REQUIRE( fp3d_abilities::execute( you, toggle, close ) );
    CHECK( you.has_active_bionic( light ) );
    CHECK( you.get_power_level() == charged - light->power_activate );
    REQUIRE( fp3d_abilities::execute( you, toggle, close ) );
    CHECK_FALSE( you.has_active_bionic( light ) );

    const matype_id karate( "style_karate" );
    CHECK_FALSE( fp3d_abilities::execute( you, karate, close ) );
    you.martial_arts_data->add_martialart( karate );
    REQUIRE( fp3d_abilities::execute( you, karate, close ) );
    CHECK( you.martial_arts_data->selected_style() == karate );
    REQUIRE( fp3d_abilities::execute( you, true, close ) );
    CHECK( you.martial_arts_data->keep_hands_free );
}

TEST_CASE( "fp3d_native_simple_windows_keep_input_and_style_isolated", "[.fp3d_simple_ui]" )
{
    const bool two_d = GENERATE( false, true );
    const bool modern = GENERATE( false, true );
    CAPTURE( two_d, modern );
    REQUIRE( fp3d_compositor::requested() );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
    } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Simple windows regression", SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
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
    auto previous_client = std::move( imclient );
    imclient = std::make_unique<cataimgui::client>( renderer, window, geometry );
    on_out_of_scope cleanup( [&]() {
        fp3d_compositor::release();
        ImGui_ImplSDLRenderer2_Shutdown();
        imclient.reset();
        ImGui::DestroyContext();
        imclient = std::move( previous_client );
    } );
    color_loader<SDL_Color>().load( windowsPalette );
    init_colors();
    REQUIRE( ImGui::GetIO().Fonts->AddFontFromFileTTF( "data/font/unifont.ttf", 18, nullptr,
             ImGui::GetIO().Fonts->GetGlyphRangesChineseFull() ) );
    fp3d_compositor::attach( renderer.get(), window.get() );
    fp3d_compositor::set_input_context( "MAIN_MENU" );
    fp3d_compositor::apply_modes( !two_d, modern );
    CHECK_FALSE( fp3d_simple_ui::active() );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    if( !two_d ) {
        fp3d_compositor::publish( fp3d_snapshot::scene_snapshot{} );
    }
    fp3d_compositor::set_input_context( "TEST_MODAL" );
    REQUIRE( fp3d_simple_ui::active() );
    CHECK( fp3d_compositor::turn_based() == !two_d );
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    REQUIRE( you.wear_item( item( itype_id( "backpack" ), calendar::turn ) ) );
    const item_location apple = you.i_add( item( itype_id( "apple" ), calendar::turn ) );
    REQUIRE( apple );
    item ammo_item( itype_id( "9mm" ), calendar::turn );
    ammo_item.charges = 30;
    const item_location ammo = you.i_add( ammo_item );
    const item_location magazine = you.i_add( item( itype_id( "glockmag" ), calendar::turn ) );
    REQUIRE( ammo );
    REQUIRE( magazine );
    you.set_mutation( trait_id( "WEB_WEAVER" ) );
    you.add_bionic( bionic_id( "bio_flashlight" ) );
    you.martial_arts_data->add_martialart( matype_id( "style_karate" ) );
    restore_on_out_of_scope restore_test( test_mode );
    test_mode = false;
    const auto before_time = calendar::turn;
    const auto before_rng = rng_get_engine();
    const int colors = GImGui->ColorStack.Size;
    const int before_moves = you.get_moves();
    const int variables = GImGui->StyleVarStack.Size;
    const ImVec4 background = ImGui::GetStyleColorVec4( ImGuiCol_WindowBg );
    const auto frame = [&]() {
        const auto draw_rng = rng_get_engine();
        SDL_SetRenderDrawColor( renderer.get(), 60, 80, 100, 255 );
        REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
        ui_manager::redraw();
        CHECK( GImGui->ColorStack.Size == colors );
        CHECK( GImGui->StyleVarStack.Size == variables );
        CHECK( ImGui::GetStyleColorVec4( ImGuiCol_WindowBg ).w == background.w );
        CHECK( rng_get_engine() == draw_rng );
    };
    const auto save = [&]( const std::string & name ) {
        if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
            std::filesystem::create_directories( directory );
            int width = 0;
            int height = 0;
            REQUIRE( SDL_GetRendererOutputSize( renderer.get(), &width, &height ) == 0 );
            SDL_Surface_Ptr picture( SDL_CreateRGBSurfaceWithFormat( 0, width, height, 32,
                                     SDL_PIXELFORMAT_RGBA8888 ) );
            REQUIRE( picture );
            REQUIRE( SDL_RenderReadPixels( renderer.get(), nullptr, picture->format->format,
                                           picture->pixels, picture->pitch ) == 0 );
            const std::string filename = ( two_d ? "2d-" : "" ) + name + ".png";
            CHECK( IMG_SavePNG( picture.get(), ( std::filesystem::path( directory ) /
                                                 filename ).u8string().c_str() ) == 0 );
        }
    };

    const auto click = [&]( float x, float y ) {
        ImGui::GetIO().AddMousePosEvent( x, y );
        frame();
        ImGui::GetIO().AddMouseButtonEvent( 0, true );
        frame();
        ImGui::GetIO().AddMouseButtonEvent( 0, false );
        frame();
    };

    {
        picker_preset preset;
        visible_multi selector( you, preset );
        selector.set_title( "简易多选回归" );
        selector.add_character_items( you );
        selector.set_filter( "apple" );
        auto ui = selector.create_or_get_ui_adaptor();
        frame();
        frame();
        ImGuiWindow *items = find_window( "/items" );
        REQUIRE( items );
        CHECK( selector.selection().empty() );
        const ImVec2 checkbox( items->DC.CursorStartPos.x + 8, items->DC.CursorStartPos.y + 8 );
        {
            auto information = fp3d_simple_ui::item_information( *apple );
            item_info_data data( "遮挡输入验证", apple->type_name(), information, {} );
            iteminfo_window details( data, point( -1, -1 ), 80, 0 );
            frame();
            frame();
            click( checkbox.x, checkbox.y );
            CHECK_FALSE( selector.take_simple_input() );
            CHECK( selector.selection().empty() );
        }
        frame();
        frame();
        items = find_window( "/items" );
        REQUIRE( items );
        click( items->DC.CursorStartPos.x + 8, items->DC.CursorStartPos.y + 8 );
        auto input = selector.take_simple_input();
        REQUIRE( input );
        CHECK( input->action == "SELECT" );
        REQUIRE( input->entry );
        CHECK( input->entry->any_item() == apple );
        selector.apply( *input );
        REQUIRE( selector.selection().size() == 1 );
        CHECK( selector.selection().front().first == apple );
        CHECK( selector.selection().front().second == 1 );
        CHECK_FALSE( selector.take_simple_input() );
        frame();
        save( "multiselect" );
        items = find_window( "/items" );
        REQUIRE( items );
        click( items->DC.CursorStartPos.x + 8, items->DC.CursorStartPos.y + 8 );
        input = selector.take_simple_input();
        REQUIRE( input );
        selector.apply( *input );
        CHECK( selector.selection().empty() );
        ImGuiWindow *root = find_window( "简易多选回归##" );
        REQUIRE( root );
        click( root->Pos.x + root->Size.x - 16, root->Pos.y + 13 );
        input = selector.take_simple_input();
        REQUIRE( input );
        CHECK( input->action == "QUIT" );
        ui.reset();
        CHECK_FALSE( selector.take_simple_input() );
    }
    {
        INFO( "before ammo chooser" );
        CHECK( rng_get_engine() == before_rng );
        ammo_preset preset;
        visible_ammo selector( you, magazine, preset );
        selector.set_title( "简易弹药回归" );
        selector.add_character_items( you );
        selector.set_filter( "9mm" );
        selector.set_all_entries_chosen_count();
        INFO( "after native ammo quantity preview" );
        CHECK( rng_get_engine() == before_rng );
        auto ui = selector.create_or_get_ui_adaptor();
        frame();
        frame();
        ImGuiWindow *items = find_window( "/items" );
        REQUIRE( items );
        click( items->DC.CursorStartPos.x + 60, items->DC.CursorStartPos.y + 8 );
        auto input = selector.take_simple_input();
        REQUIRE( input );
        CHECK( input->action == "MOUSE_MOVE" );
        REQUIRE( input->entry );
        CHECK( input->entry->any_item() == ammo );
        CHECK( input->entry->chosen_count == 15 );
        frame();
        save( "ammunition" );
        ImGuiWindow *root = find_window( "简易弹药回归##" );
        REQUIRE( root );
        click( root->DC.CursorStartPos.x + 10,
               root->DC.CursorPosPrevLine.y - root->CalcFontSize() - 16 + 8 );
        input = selector.take_simple_input();
        REQUIRE( input );
        CHECK( input->action == "DECREASE_COUNT" );
        CHECK_FALSE( input->entry );
        CHECK( ammo->charges == 30 );
        CHECK( magazine->ammo_remaining() == 0 );
    }
    {
        INFO( "before native item-info fixture" );
        CHECK( rng_get_engine() == before_rng );
        std::vector<iteminfo> information = fp3d_simple_ui::item_information( *apple );
        INFO( "after native item-info fixture" );
        CHECK( rng_get_engine() == before_rng );
        information.emplace_back( "DESCRIPTION", std::string( 6000, 'A' ) );
        item_info_data data( "苹果详情 / 简易窗口", apple->type_name(), information, {} );
        data.handle_scrolling = true;
        data.arrow_scrolling = true;
        iteminfo_window details( data, point( -1, -1 ), 80, 0 );
        frame();
        frame();
        ImGuiWindow *body = find_window( "/item details" );
        REQUIRE( body );
        CHECK( body->ScrollMax.y > 0 );
        save( "item-information" );
    }
    {
        visible_unload selector( you );
        selector.set_title( "卸载选择回归" );
        selector.add_character_items( you );
        selector.set_filter( "Glock" );
        auto ui = selector.create_or_get_ui_adaptor();
        frame();
        frame();
        ImGuiWindow *root = find_window( "卸载选择回归##" );
        REQUIRE( root );
        save( "unload-selection" );
        click( root->DC.CursorStartPos.x + 40,
               root->DC.CursorPosPrevLine.y - root->CalcFontSize() - 16 + 8 );
        const auto input = selector.take_simple_input();
        REQUIRE( input );
        CHECK( input->action == "CONTAIN_MODE" );
        CHECK_FALSE( input->entry );
    }
    {
        item_location backpack;
        for( const item_location &location : you.top_items_loc() ) {
            if( location->typeId() == itype_id( "backpack" ) ) {
                backpack = location;
            }
        }
        REQUIRE( backpack );
        visible_examiner examiner( you, backpack );
        examiner.set_title( "容器详情回归" );
        examiner.set_filter( "apple" );
        auto ui = examiner.create_or_get_ui_adaptor();
        frame();
        frame();
        REQUIRE( find_window( "容器详情回归##" ) );
        REQUIRE( find_window( "/details" ) );
        save( "container-information" );
        ui.reset();
        CHECK( examiner.cleanup() == EXAMINED_CONTENTS_UNCHANGED );
    }

    {
        uilist menu;
        menu.title = "物品操作 / 简易窗口";
        menu.text = "选择操作；无法使用的选项保留原因。长说明可以单独滚动。";
        menu.desc_enabled = true;
        menu.addentry_desc( 41, true, 'a', "查看详情", "保留原版效果、消耗与检查。" );
        menu.addentry_desc( 42, false, 'b', "无法使用", "缺少必要的工具。" );
        for( int i = 0; i < 120; ++i ) {
            menu.addentry_desc( i + 100, true, MENU_AUTOASSIGN, "物品操作 " + std::to_string( i ),
                                std::string( 2000, 'A' ) + "中文说明" );
        }
        auto ui = menu.create_or_get_ui();
        for( const point resolution : {
                 point( 1280, 720 ), point( 1920, 1080 ), point( 640, 480 )
             } ) {
            SDL_SetWindowSize( window.get(), resolution.x, resolution.y );
            ui_manager::screen_resized();
            frame();
            frame();
            ImGuiWindow *root = find_window( "物品操作 / 简易窗口##" );
            REQUIRE( root );
            CHECK( root->Pos.x >= 0 );
            CHECK( root->Pos.y >= 0 );
            CHECK( root->Pos.x + root->Size.x <= resolution.x + 1 );
            CHECK( root->Pos.y + root->Size.y <= resolution.y + 1 );
            ImGuiWindow *choices = find_window( "/choices" );
            REQUIRE( choices );
            CHECK( choices->ScrollMax.y > 0 );
            save( "choices-" + std::to_string( resolution.y ) );
        }
        SDL_SetWindowSize( window.get(), 1280, 720 );
        ui_manager::screen_resized();
        frame();
        frame();
        ImGuiWindow *choices = find_window( "/choices" );
        REQUIRE( choices );
        const auto click_row = [&]( int row ) {
            const float row_height = choices->CalcFontSize() + 12 + ImGui::GetStyle().ItemSpacing.y;
            ImGui::GetIO().AddMousePosEvent( choices->DC.CursorStartPos.x + 30,
                                             choices->DC.CursorStartPos.y + row * row_height + 8 );
            frame();
            ImGui::GetIO().AddMouseButtonEvent( 0, true );
            frame();
            ImGui::GetIO().AddMouseButtonEvent( 0, false );
            frame();
        };
        click_row( 1 );
        CHECK( menu.selected == 0 );
        click_row( 2 );
        CHECK( menu.selected == 2 );
        CHECK( menu.entries[menu.selected].retval == 100 );
        menu.entries.clear();
        menu.filterlist();
        frame();
        CHECK( menu.selected == -1 );
    }
    {
        number_input_popup<int> input( 40, 5, "选择数量" );
        input.set_description( "输入转移数量；取消会保留原来的数量。", c_white );
        input.set_label( "数量" );
        frame();
        frame();
        save( "quantity" );
        ImGuiWindow *root = find_window( "选择数量##" );
        REQUIRE( root );
        // Bottom row: real mouse release on the Confirm button.
        ImGui::GetIO().AddMousePosEvent( root->DC.CursorStartPos.x + 25,
                                         root->DC.CursorPosPrevLine.y + 8 );
        frame();
        ImGui::GetIO().AddMouseButtonEvent( 0, true );
        frame();
        ImGui::GetIO().AddMouseButtonEvent( 0, false );
        frame();
        CHECK( input.get_button_action() == "TEXT.CONFIRM" );
    }
    {
        visible_query query;
        query.context( "YESNO" ).message( "%s",
                                          "是否继续？实际结果由原版确认。" ).option( "YES" ).option( "NO" ).cursor( 1 );
        auto ui = query.create_or_get_impl();
        frame();
        frame();
        save( "confirmation" );
        REQUIRE( find_window( "QUERY_POPUP##" ) );
    }
    for( const bool anykey : { false, true } ) {
        visible_query query;
        query.context( "POPUP_WAIT" ).message( "%s", "你无法屠宰它；你缺少一些工具。" )
        .allow_cancel( !anykey ).allow_anykey( anykey );
        const auto ui = query.create_or_get_impl();
        frame();
        frame();
        const ImGuiWindow *root = find_window( "QUERY_POPUP##" );
        REQUIRE( root );
        ImGui::GetIO().AddMousePosEvent( root->DC.CursorStartPos.x + 12,
                                         root->DC.CursorPosPrevLine.y + 8 );
        frame();
        ImGui::GetIO().AddMouseButtonEvent( 0, true );
        frame();
        ImGui::GetIO().AddMouseButtonEvent( 0, false );
        if( anykey ) {
            // The other path releases the mouse during query's own first redraw.
            frame();
        }
        const query_popup::result result = query.query();
        CHECK_FALSE( result.wait_input );
        CHECK( result.action == ( anykey ? "ANY_INPUT" : "QUIT" ) );
        CHECK( result.evt.type == input_event_t::error );
    }
    {
        bool visible = true;
        int draws = 0;
        auto adapted = fp3d_simple_ui::create_panel( "适配窗口", [&]( cataimgui::window & panel ) {
            ++draws;
            CHECK( ImGui::GetStyleColorVec4( ImGuiCol_Text ).x == 1.f );
            CHECK( ImGui::GetStyleColorVec4( ImGuiCol_WindowBg ).w == Approx( two_d ? 1.f : .72f ) );
            panel.action_button( "CONFIRM", "执行原版操作" );
        }, [&]() {
            return visible;
        } );
        frame();
        frame();
        CHECK( draws >= 2 );
        visible = false;
        const int previous_draws = draws;
        frame();
        CHECK( draws == previous_draws );
        CHECK_FALSE( find_window( "适配窗口##" ) );
        visible = true;
        frame();
        CHECK( draws > previous_draws );
    }
    {
        SDL_SetWindowSize( window.get(), 640, 480 );
        std::vector<ImGuiID> page_ids;
        for( int page = 9; page <= 11; ++page ) {
            const auto panel = fp3d_modern_ui::create( you, get_map(), 150, false, page );
            frame();
            frame();
            const ImGuiWindow *body = find_window( "/能力操作_" );
            REQUIRE( body );
            CHECK( body->Size.x > 0 );
            CHECK( body->Size.y > 0 );
            CHECK( body->Pos.x + body->Size.x <= ImGui::GetIO().DisplaySize.x );
            CHECK( body->Pos.y + body->Size.y <= ImGui::GetIO().DisplaySize.y );
            for( const ImGuiID previous : page_ids ) {
                CHECK( body->ID != previous );
            }
            page_ids.push_back( body->ID );
            save( "survival-ability-" + std::to_string( page ) );
        }
        SDL_SetWindowSize( window.get(), 1280, 720 );
    }
    CHECK( fp3d_simple_ui::text_color( c_black ).x == Approx( .88f ) );
    nc_color white = c_white;
    nc_color red = c_red;
    CHECK( fp3d_simple_ui::text_color( c_white ).x == cataimgui::imvec4_from_color( white ).x );
    CHECK( fp3d_simple_ui::text_color( c_red ).y == cataimgui::imvec4_from_color( red ).y );
    fp3d_compositor::apply_modes( !two_d, false );
    CHECK( fp3d_simple_ui::active() );
    fp3d_compositor::apply_modes( false, false );
    CHECK( fp3d_simple_ui::active() );
    fp3d_compositor::set_input_context( "MAIN_MENU" );
    CHECK_FALSE( fp3d_simple_ui::active() );
    nc_color black = c_black;
    CHECK( fp3d_simple_ui::text_color( c_black ).x == cataimgui::imvec4_from_color( black ).x );
    {
        const fp3d_simple_ui::scoped_theme unchanged;
        CHECK( GImGui->ColorStack.Size == colors );
    }
    fp3d_compositor::apply_modes( false, true );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    CHECK( fp3d_simple_ui::active() );
    fp3d_compositor::set_input_context( "MAIN_MENU" );
    fp3d_compositor::set_input_context( "TEST_MODAL" );
    CHECK_FALSE( fp3d_simple_ui::active() );
    {
        fp3d_compositor::apply_modes( true, true );
        fp3d_compositor::publish( fp3d_snapshot::scene_snapshot{} );
        fp3d_compositor::set_input_context( "UILIST" );
        REQUIRE( fp3d_compositor::turn_based() );
        SDL_Texture_Ptr background_layer( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_TARGET, 1280, 720 ) );
        REQUIRE( background_layer );
        REQUIRE( SDL_SetRenderTarget( renderer.get(), background_layer.get() ) == 0 );
        REQUIRE( SDL_SetRenderDrawColor( renderer.get(), 40, 80, 120, 255 ) == 0 );
        REQUIRE( SDL_RenderClear( renderer.get() ) == 0 );
        int marker_x = 24;
        auto menu = fp3d_simple_ui::create_panel( "主菜单", [&]( cataimgui::window & ) {
            const float x = marker_x;
            ImGui::GetForegroundDrawList()->AddRectFilled( ImVec2( x, 12 ), ImVec2( x + 20, 32 ),
                    IM_COL32( 240, 20, 20, 128 ) );
            ImGui::TextUnformatted( "菜单项" );
        } );
        const auto present = [&]() {
            REQUIRE( SDL_SetRenderTarget( renderer.get(), nullptr ) == 0 );
            REQUIRE( SDL_RenderCopy( renderer.get(), background_layer.get(), nullptr, nullptr ) == 0 );
            imclient->present_overlay();
        };
        const auto pixel = [&]( int x ) {
            const SDL_Rect point{ x, 20, 1, 1 };
            std::uint32_t value = 0;
            REQUIRE( SDL_RenderReadPixels( renderer.get(), &point, SDL_PIXELFORMAT_RGBA8888,
                                          &value, sizeof( value ) ) == 0 );
            return value;
        };
        constexpr std::uint32_t backdrop = 0x285078ffu;
        ui_manager::redraw();
        ui_manager::redraw();
        REQUIRE( SDL_GetRenderTarget( renderer.get() ) == background_layer.get() );
        CHECK( pixel( 30 ) == backdrop );
        present();
        CHECK( ( pixel( 30 ) >> 24 ) == Approx( 140 ).margin( 2 ) );

        marker_x = 104;
        REQUIRE( SDL_SetRenderTarget( renderer.get(), background_layer.get() ) == 0 );
        ui_manager::redraw();
        present();
        CHECK( pixel( 30 ) == backdrop );
        CHECK( pixel( 110 ) != backdrop );

        menu.reset();
        present();
        CHECK( pixel( 110 ) == backdrop );
        fp3d_compositor::set_input_context( "DEFAULTMODE" );
    }
    CHECK( calendar::turn == before_time );
    CHECK( rng_get_engine() == before_rng );
    CHECK( you.get_moves() == before_moves );
}
#endif
