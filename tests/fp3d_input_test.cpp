#include "fp3d_input.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <filesystem>
#include <fstream>
#include <iterator>
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "game.h"
#include "input.h"
#include "input_context.h"
#include "input_enums.h"
#include "path_info.h"
#include "sdl_wrappers.h"

TEST_CASE( "fp3d_three_modes_keep_2d_movement_independent", "[fp3d][input]" )
{
    restore_on_out_of_scope restore_manager( inp_mngr );
    inp_mngr = input_manager();
    inp_mngr.init();
    using fp3d_input::mode;
    CHECK( fp3d_input::next_mode( mode::original_2d ) == mode::modern_2d );
    CHECK( fp3d_input::next_mode( mode::modern_2d ) == mode::first_person_3d );
    CHECK( fp3d_input::next_mode( mode::first_person_3d ) == mode::original_2d );
    auto original = get_default_mode_input_context();
    const auto original_w = original.input_to_action( input_event( 'w', input_event_t::keyboard_code ) );
    const input_event tab( SDLK_TAB, input_event_t::keyboard_code );
    CHECK( fp3d_input::resolve( mode::modern_2d, tab, 0 ) == "FP3D_PANEL" );
    CHECK( fp3d_input::resolve( mode::first_person_3d, tab, 0 ) == "FP3D_PANEL" );
    CHECK( fp3d_input::resolve( mode::original_2d, tab, 0 ).empty() );
    for( const auto &entry : {
             std::pair{ 'w', tripoint_rel_ms::north },
             std::pair{ 's', tripoint_rel_ms::south },
             std::pair{ 'a', tripoint_rel_ms::west },
             std::pair{ 'd', tripoint_rel_ms::east }
         } ) {
        const input_event key( entry.first, input_event_t::keyboard_code );
        for( int yaw : { 0, 45, 90, 180, 270 } ) {
            CHECK( fp3d_input::resolve( mode::modern_2d, key, yaw ) ==
                   action_ident( get_movement_action_from_delta( entry.second, iso_rotate::yes ) ) );
            CHECK( fp3d_input::resolve( mode::original_2d, key, yaw ).empty() );
        }
    }
    CHECK( fp3d_input::resolve( mode::modern_2d,
                                input_event( 'e', input_event_t::keyboard_code ), 0 ).empty() );
    CHECK( fp3d_input::resolve( mode::modern_2d,
                                input_event( MouseInput::LeftButtonPressed, input_event_t::mouse ), 0 ).empty() );
    input_context menu( "PICKUP" );
    menu.register_action( "CONFIRM" );
    CHECK( menu.input_to_action( input_event( 'w', input_event_t::keyboard_code ) ) == "ERROR" );
    CHECK( !fp3d_input::resolve( mode::first_person_3d,
                                 input_event( 'w', input_event_t::keyboard_code ), 0 ).empty() );
    CHECK( original.input_to_action( input_event( 'w', input_event_t::keyboard_code ) ) == original_w );
}

TEST_CASE( "fp3d_control_profiles_are_independent_and_rebindable", "[fp3d][input]" )
{
    restore_on_out_of_scope restore_manager( inp_mngr );
    inp_mngr = input_manager();
    inp_mngr.init();
    auto original = get_default_mode_input_context();
    const auto wield_keys = inp_mngr.get_input_for_action( "wield", "DEFAULTMODE" );
    const input_event w( 'w', input_event_t::keyboard_code );
    const input_event r( 'r', input_event_t::keyboard_code );
    const auto original_w = original.input_to_action( w );
    REQUIRE( original_w != "ERROR" );
    REQUIRE_FALSE( original.is_registered_action( "FP3D_FORWARD" ) );
    auto profile = fp3d_input::context();
    CHECK( profile.is_registered_action( "FP3D_FORWARD" ) );
    const input_event map_key( 'm', input_event_t::keyboard_code );
    const auto original_map = original.input_to_action( map_key );
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d, map_key, 0 ) == "FP3D_MAP" );
    CHECK( original.input_to_action( map_key ) == original_map );
    CHECK( fp3d_input::resolve( fp3d_input::mode::original_2d, map_key, 0 ).empty() );
    CHECK( fp3d_input::resolve( fp3d_input::mode::original_2d, w, 0 ).empty() );
    for( const int yaw : {
             0, 45, 90, 135, 180, 225, 270, 315
         } ) {
        const auto action = fp3d_input::resolve( fp3d_input::mode::first_person_3d, w, yaw );
        CHECK( !action.empty() );
        CHECK( original.is_registered_action( action ) );
        const auto opposite = fp3d_input::resolve( fp3d_input::mode::first_person_3d,
                              input_event( 's', input_event_t::keyboard_code ), yaw );
        CHECK( action != opposite );
    }
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d,
                                input_event( static_cast<int>( keycode::f6 ), input_event_t::keyboard_code ), 0 ) ==
           action_ident( ACTION_KEYBINDINGS ) );
    // A native menu still owns its own input context, even with matching keys.
    input_context menu( "PICKUP" );
    menu.register_action( "CONFIRM" );
    CHECK( menu.input_to_action( w ) == "ERROR" );
    // Load a saved profile through the native preferences loader. Keep this
    // isolated test user's previous file intact, including on failed checks.
    const auto preferences = PATH_INFO::user_keybindings().generic_u8string();
    const bool existed = std::filesystem::exists( preferences );
    std::ifstream previous( preferences, std::ios::binary );
    const std::string previous_text( ( std::istreambuf_iterator<char>( previous ) ), {} );
    previous.close();
    on_out_of_scope restore_preferences( [&]() {
        if( existed ) {
            std::ofstream( preferences, std::ios::binary ) << previous_text;
        } else {
            std::filesystem::remove( preferences );
        }
    } );
    std::filesystem::create_directories( std::filesystem::path( preferences ).parent_path() );
    std::ofstream( preferences ) << R"([{"id":"FP3D_FORWARD","category":"FP3D","version":3,
        "bindings":[{"input_method":"keyboard_code","key":"r"}]},
        {"id":"MODERN_2D_NORTH","category":"MODERN_2D","version":3,
        "bindings":[{"input_method":"keyboard_code","key":"z"}]},
        {"id":"FP3D_PANEL","category":"MODERN_2D","version":3,
        "bindings":[{"input_method":"keyboard_code","key":"p"}]}])";
    inp_mngr = input_manager();
    inp_mngr.init();
    CHECK( fp3d_input::resolve( fp3d_input::mode::modern_2d, w, 0 ).empty() );
    CHECK( fp3d_input::resolve( fp3d_input::mode::modern_2d,
                                input_event( 'z', input_event_t::keyboard_code ), 0 ) ==
           action_ident( get_movement_action_from_delta( tripoint_rel_ms::north, iso_rotate::yes ) ) );
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d, w, 0 ).empty() );
    CHECK( !fp3d_input::resolve( fp3d_input::mode::first_person_3d, r, 0 ).empty() );
    CHECK( original.input_to_action( w ) == original_w );
    CHECK( inp_mngr.get_input_for_action( "wield", "DEFAULTMODE" ) == wield_keys );
    CHECK( fp3d_input::resolve( fp3d_input::mode::original_2d, r, 0 ).empty() );
    const input_event tab( SDLK_TAB, input_event_t::keyboard_code );
    const input_event panel_key( 'p', input_event_t::keyboard_code );
    CHECK( fp3d_input::resolve( fp3d_input::mode::modern_2d, panel_key, 0 ) == "FP3D_PANEL" );
    CHECK( fp3d_input::resolve( fp3d_input::mode::modern_2d, tab, 0 ).empty() );
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d, tab, 0 ) == "FP3D_PANEL" );
    CHECK( fp3d_input::resolve( fp3d_input::mode::original_2d, panel_key, 0 ).empty() );
}

TEST_CASE( "fp3d_gestures_classify_once_and_cancel_without_world_ticks", "[fp3d][input]" )
{
    fp3d_input::gestures input;
    const input_event e( 'e', input_event_t::keyboard_code );
    const input_event f( 'f', input_event_t::keyboard_code );
    const input_event attack( MouseInput::LeftButtonPressed, input_event_t::mouse );
    REQUIRE( input.event( e, true, 100 ) );
    CHECK( input.poll( 200 ).empty() );
    REQUIRE( input.event( e, false, 220 ) );
    CHECK( input.poll( 220 ) == "FP3D_INTERACT" );
    CHECK( input.poll( 800 ).empty() );
    REQUIRE( input.event( f, true, 1000 ) );
    REQUIRE( input.event( f, true, 1200 ) ); // OS repeat must not reset the hold.
    CHECK( input.poll( 1250 ) == "FP3D_PICKUP_LONG" );
    REQUIRE( input.event( f, false, 1300 ) );
    CHECK( input.poll( 1300 ).empty() );
    REQUIRE( input.event( attack, true, 2000 ) );
    CHECK( input.poll( 2000 ) == "FP3D_ATTACK" );
    REQUIRE( input.event( attack, true, 2050 ) );
    CHECK( input.poll( 2050 ).empty() );
    CHECK( input.poll( 2250 ) == "FP3D_ATTACK" );
    CHECK( input.poll( 2251 ).empty() );
    CHECK( input.poll( 2499 ).empty() );
    CHECK( input.poll( 2500 ) == "FP3D_ATTACK" );
    input.cancel();
    CHECK( input.poll( 3000 ).empty() );
    CHECK_FALSE( input.event( attack, false, 3000 ) );
    for( const auto &entry : {
             std::pair{ 'r', ACTION_RELOAD_WIELDED },
             std::pair{ ' ', ACTION_PAUSE }
         } ) {
        CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d,
                                    input_event( entry.first, input_event_t::keyboard_code ), 0 ) == action_ident( entry.second ) );
    }
    for( const int yaw : {
             0, 45, 90, 180, 270
         } ) {
        CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d,
                                    input_event( 'w', input_event_t::keyboard_code ), yaw, true ) ==
               action_ident( get_movement_action_from_delta( tripoint_rel_ms::north, iso_rotate::no ) ) );
    }
}

TEST_CASE( "fp3d_hotbar_and_modifier_commands_are_independent", "[fp3d][input]" )
{
    INFO( "modifier name code=" << inp_mngr.get_keycode( input_event_t::keyboard_code,
            "LEFT_SHIFT" ) << " SDL=" << SDLK_LSHIFT );
    for( const auto &binding : inp_mngr.get_input_for_action( "FP3D_RUN", "FP3D" ) ) {
        INFO( "run binding=" << binding.get_first_input() );
        CHECK( binding.get_first_input() == SDLK_LSHIFT );
    }
    fp3d_input::gestures input;
    CHECK( input.event( input_event( SDLK_LSHIFT, input_event_t::keyboard_code ), true, 0 ) );
    CHECK( input.poll( 0 ) == action_ident( ACTION_TOGGLE_RUN ) );
    input.cancel();
    CHECK( input.event( input_event( SDLK_LCTRL, input_event_t::keyboard_code ), true, 0 ) );
    CHECK( input.poll( 0 ) == action_ident( ACTION_TOGGLE_CROUCH ) );
    input.cancel();
    CHECK( input.event( input_event( SDLK_LALT, input_event_t::keyboard_code ), true, 0 ) );
    CHECK( input.expanded_hud() );
    CHECK( input.poll( 1000 ).empty() );
    input.cancel();
    CHECK_FALSE( input.expanded_hud() );
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d,
                                input_event( '1', input_event_t::keyboard_code ), 0 ) == "FP3D_SLOT_1" );
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d,
                                input_event( MouseInput::ScrollWheelUp, input_event_t::mouse ), 0 ) == "FP3D_SLOT_PREV" );
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d,
                                input_event( MouseInput::ScrollWheelDown, input_event_t::mouse ), 0 ) == "FP3D_SLOT_NEXT" );
    CHECK( fp3d_input::resolve( fp3d_input::mode::original_2d,
                                input_event( '1', input_event_t::keyboard_code ), 0 ).empty() );
}

TEST_CASE( "fp3d_map_intent_reaches_the_native_world_dispatcher", "[fp3d][input][map_dispatch]" )
{
    restore_on_out_of_scope restore_manager( inp_mngr );
    inp_mngr = input_manager();
    inp_mngr.init();
    const auto world = get_default_mode_input_context();
    const input_event key( 'm', input_event_t::keyboard_code );
    const auto native = world.input_to_action( key );
    const auto mapped = fp3d_input::resolve( fp3d_input::mode::first_person_3d, key, 0 );
    REQUIRE( mapped == "FP3D_MAP" );
    CHECK( world.is_registered_action( mapped ) );
    CHECK( fp3d_input::resolve( fp3d_input::mode::original_2d, key, 0 ).empty() );
    CHECK( world.input_to_action( key ) == native );
}
#endif
