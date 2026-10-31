#include "fp3d_input.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <filesystem>
#include <fstream>
#include <iterator>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "game.h"
#include "input.h"
#include "input_context.h"
#include "path_info.h"
#include "rng.h"
#include "sdl_wrappers.h"

TEST_CASE( "fp3d_mouse_rebinding_preserves_button_identity_and_single_dispatch",
           "[fp3d][input][mouse_bindings]" )
{
    restore_on_out_of_scope restore_manager( inp_mngr );
    const auto preferences = PATH_INFO::user_keybindings().generic_u8string();
    const bool existed = std::filesystem::exists( preferences );
    std::ifstream previous_file( preferences, std::ios::binary );
    const std::string previous( ( std::istreambuf_iterator<char>( previous_file ) ), {} );
    previous_file.close();
    on_out_of_scope restore_preferences( [&]() {
        if( existed ) {
            std::ofstream( preferences, std::ios::binary ) << previous;
        } else {
            std::filesystem::remove( preferences );
        }
    } );
    std::filesystem::create_directories( std::filesystem::path( preferences ).parent_path() );
    const auto &you = get_avatar();
    const auto time = calendar::turn;
    const auto moves = you.get_moves();
    const auto position = you.pos_abs();
    const auto engine = rng_get_engine();
    for( const std::string command : {
             "FP3D_PANEL", "FP3D_MAP", "FP3D_ACTIONS",
             "FP3D_RELOAD", "FP3D_WAIT", "FP3D_BINDINGS", "FP3D_SLOT_1", "FP3D_SLOT_NEXT"
         } ) {
        CAPTURE( command );
        std::ofstream( preferences ) << "[{\"id\":\"FP3D_INTERACT\",\"category\":\"FP3D\",\"version\":3,"
                                        "\"bindings\":[{\"input_method\":\"mouse\",\"key\":\"MOUSE_BACK_PRESSED\"}]},"
                                        "{\"id\":\"" << command << "\",\"category\":\"FP3D\",\"version\":3,"
                                        "\"bindings\":[{\"input_method\":\"mouse\",\"key\":\"MOUSE_FORWARD_PRESSED\"}]}]";
        inp_mngr = input_manager();
        inp_mngr.init();
        const auto world = get_default_mode_input_context();
        const input_event forward( MouseInput::X2ButtonPressed, input_event_t::mouse );
        const auto original = world.input_to_action( forward );
        const auto expected = fp3d_input::resolve( fp3d_input::mode::first_person_3d, forward, 0 );
        REQUIRE_FALSE( expected.empty() );
        REQUIRE( world.is_registered_action( expected ) );
        fp3d_input::gestures input;
        CHECK_FALSE( input.mouse_button( SDL_BUTTON_MIDDLE, true, 100 ) );
        CHECK_FALSE( input.mouse_button( 255, true, 100 ) );
        CHECK( input.poll( 100 ).empty() );
        REQUIRE( input.mouse_button( SDL_BUTTON_X1, true, 100 ) );
        CHECK( input.poll( 100 ).empty() );
        REQUIRE( input.mouse_button( SDL_BUTTON_X1, false, 150 ) );
        CHECK( input.poll( 150 ) == "FP3D_INTERACT" );
        REQUIRE( input.mouse_button( SDL_BUTTON_X2, true, 200 ) );
        CHECK( input.poll( 200 ) == expected );
        REQUIRE( input.mouse_button( SDL_BUTTON_X2, true, 210 ) );
        CHECK( input.poll( 1000 ).empty() );
        REQUIRE( input.mouse_button( SDL_BUTTON_X2, false, 1010 ) );
        CHECK( input.poll( 1010 ).empty() );
        REQUIRE( input.mouse_button( SDL_BUTTON_X2, true, 1100 ) );
        input.cancel(); // Menu, focus loss and mode changes discard pending work.
        CHECK( input.poll( 1200 ).empty() );
        CHECK_FALSE( input.mouse_button( SDL_BUTTON_X2, false, 1200 ) );
        CHECK( world.input_to_action( forward ) == original );
        CHECK( fp3d_input::resolve( fp3d_input::mode::original_2d, forward, 0 ).empty() );
        CHECK_FALSE( input.event( input_event( 'q', input_event_t::keyboard_code ), true, 1300 ) );
        CHECK( input.poll( 1400 ).empty() );
    }
    CHECK( calendar::turn == time );
    CHECK( you.get_moves() == moves );
    CHECK( you.pos_abs() == position );
    CHECK( rng_get_engine() == engine );
}
#endif
