#include "fp3d_quick_menu.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cmath>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "fp3d_input.h"
#include "game.h"
#include "input_context.h"
#include "input.h"
#include "rng.h"
#include "path_info.h"

TEST_CASE( "fp3d_wheel_hit_regions_match_clockwise_screen_directions", "[fp3d][wheel]" )
{
    const std::array<std::array<float, 2>, 8> points = {{
            {{ 0, -100 }}, {{ 70, -70 }}, {{ 100, 0 }}, {{ 70, 70 }},
            {{ 0, 100 }}, {{ -70, 70 }}, {{ -100, 0 }}, {{ -70, -70 }}
        }
    };
    for( int i = 0; i < 8; ++i ) {
        CAPTURE( i );
        CHECK( fp3d_quick_menu::sector_at( points[i][0], points[i][1], 40, 120 ) == i );
    }
    CHECK( fp3d_quick_menu::sector_at( 0, 0, 40, 120 ) == -1 );
    CHECK( fp3d_quick_menu::sector_at( 20, 10, 40, 120 ) == -1 );
    CHECK( fp3d_quick_menu::sector_at( 0, -121, 40, 120 ) == -1 );
    CHECK( fp3d_quick_menu::sector_at( 0, -120, 40, 120 ) == 0 );
    CHECK( fp3d_quick_menu::sector_at( 0, 0, 0, 120 ) == -1 );
    CHECK( fp3d_quick_menu::sector_at( 0, -100, 120, 40 ) == -1 );
    CHECK( fp3d_quick_menu::sector_at( std::numeric_limits<float>::quiet_NaN(), 0, 40, 120 ) == -1 );
}

TEST_CASE( "fp3d_wheel_choices_are_native_dispatchable_and_read_only", "[fp3d][wheel]" )
{
    const auto &you = get_avatar();
    const auto time = calendar::turn;
    const auto moves = you.get_moves();
    const auto position = you.pos_abs();
    const auto hp = you.get_hp();
    const auto stamina = you.get_stamina();
    const auto engine = rng_get_engine();
    const auto world = get_default_mode_input_context();
    for( const auto &choice : fp3d_quick_menu::entries() ) {
        REQUIRE( choice.label );
        REQUIRE( choice.description );
        CHECK( ( choice.action == ACTION_NULL ) == ( choice.command != nullptr ) );
        const auto command = choice.command ? choice.command : action_ident( choice.action );
        CHECK( world.is_registered_action( command ) );
        CHECK( choice.action != ACTION_PAUSE );
    }
    CHECK( calendar::turn == time );
    CHECK( you.get_moves() == moves );
    CHECK( you.pos_abs() == position );
    CHECK( you.get_hp() == hp );
    CHECK( you.get_stamina() == stamina );
    CHECK( rng_get_engine() == engine );
}

TEST_CASE( "fp3d_wheel_key_is_independent_rebindable_and_registered", "[fp3d][wheel][input]" )
{
    restore_on_out_of_scope restore_manager( inp_mngr );
    inp_mngr = input_manager();
    inp_mngr.init();
    const auto world = get_default_mode_input_context();
    const input_event q( 'q', input_event_t::keyboard_code );
    const input_event v( 'v', input_event_t::keyboard_code );
    const auto original_q = world.input_to_action( q );
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d, q, 0 ) == "FP3D_ACTIONS" );
    CHECK( world.is_registered_action( "FP3D_ACTIONS" ) );
    CHECK( fp3d_input::resolve( fp3d_input::mode::original_2d, q, 0 ).empty() );
    const auto preferences = PATH_INFO::user_keybindings().generic_u8string();
    const bool existed = std::filesystem::exists( preferences );
    std::ifstream input( preferences, std::ios::binary );
    const std::string previous( ( std::istreambuf_iterator<char>( input ) ), {} );
    input.close();
    on_out_of_scope restore_preferences( [&]() {
        if( existed ) {
            std::ofstream( preferences ) << previous;
        } else {
            std::filesystem::remove( preferences );
        }
    } );
    std::filesystem::create_directories( std::filesystem::path( preferences ).parent_path() );
    std::ofstream( preferences ) << R"([{"id":"FP3D_ACTIONS","category":"FP3D","version":3,
        "bindings":[{"input_method":"keyboard_code","key":"v"}]}])";
    inp_mngr = input_manager();
    inp_mngr.init();
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d, q, 0 ).empty() );
    CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d, v, 0 ) == "FP3D_ACTIONS" );
    CHECK( world.input_to_action( q ) == original_q );
}
#endif
