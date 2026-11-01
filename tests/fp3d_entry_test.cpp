#include "fp3d_entry.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <limits>

#include "avatar.h"
#include "calendar.h"
#include "display.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "game.h"
#include "kill_tracker.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "profession.h"
#include "rng.h"

TEST_CASE( "fp3d_entry_reveals_holds_and_fades_once", "[fp3d][entry]" )
{
    fp3d_entry::timeline intro;
    CHECK_FALSE( intro.active() );
    intro.restart();
    // The loading screen can take arbitrarily long; the first world frame is transparent.
    intro.update( 100000, false );
    intro.update( 200000, true );
    CHECK( intro.opacity() == 0.0f );
    intro.update( 200450, true );
    CHECK( intro.opacity() == Approx( 0.5f ) );
    CHECK( intro.opacity( 6 ) == 0.0f );
    intro.update( 201500, true );
    for( int element = 0; element <= 6; ++element ) {
        CHECK( intro.opacity( element ) == 1.0f );
    }
    intro.update( 205000, true );
    CHECK( intro.opacity( 6 ) == 1.0f );
    intro.update( 205750, true );
    CHECK( intro.opacity() == Approx( 0.5f ) );
    CHECK( intro.opacity( 6 ) == Approx( 0.5f ) );
    intro.update( 206500, true );
    CHECK_FALSE( intro.active() );
    CHECK( intro.opacity() == 0.0f );
    intro.update( 300000, true );
    CHECK_FALSE( intro.active() );
}

TEST_CASE( "fp3d_entry_pauses_when_hidden_and_restarts_for_the_next_load", "[fp3d][entry]" )
{
    fp3d_entry::timeline intro;
    intro.restart();
    intro.update( 10, true );
    intro.update( 460, true );
    const float before_menu = intro.opacity();
    intro.update( 470, false );
    intro.update( 200000, false );
    intro.update( 300000, true );
    CHECK( intro.opacity() == before_menu );
    intro.update( 300450, true );
    CHECK( intro.opacity() == 1.0f );
    intro.reset();
    CHECK_FALSE( intro.active() );
    intro.restart();
    intro.update( 400000, true );
    CHECK( intro.opacity() == 0.0f );
    // SDL's 32-bit tick rollover does not skip or extend the animation.
    intro.restart();
    intro.update( std::numeric_limits<std::uint32_t>::max() - 449, true );
    intro.update( 0, true );
    CHECK( intro.opacity() == Approx( 0.5f ) );
}

TEST_CASE( "fp3d_entry_uses_the_loaded_character_and_native_calendar", "[fp3d][entry]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    restore_on_out_of_scope restore_turn( calendar::turn );
    restore_on_out_of_scope restore_start( calendar::start_of_game );
    calendar::start_of_game = calendar::turn_zero + 91_days + 8_hours;
    calendar::turn = calendar::start_of_game + 3_days;
    you.name = "Entry overlay survivor";
    you.custom_profession = "测试职业";
    const auto position = you.pos_abs();
    const auto turn = calendar::turn;
    const auto rng_before = rng_get_engine();
    const auto result = fp3d_entry::capture( you );
    CHECK( result.day == "第 4 天" );
    CHECK( result.date == display::date_string() + " " + display::time_string( you ) );
    CHECK( result.rows[0].second == you.name );
    CHECK( result.rows[1].second == you.custom_profession );
    CHECK_FALSE( result.rows[2].second.empty() );
    CHECK( result.rows[2].second.find( '<' ) == std::string::npos );
    CHECK( result.rows[2].second.find( '\n' ) == std::string::npos );
    CHECK_FALSE( result.rows[3].second.empty() );
    CHECK( result.rows[4].second == std::to_string( g->get_kill_tracker().total_kill_count() ) );
    CHECK( you.pos_abs() == position );
    CHECK( calendar::turn == turn );
    CHECK( rng_get_engine() == rng_before );
    you.custom_profession.clear();
    you.prof = profession::generic();
    CHECK( fp3d_entry::capture( you ).rows[1].second == you.prof->gender_appropriate_name( you.male ) );
}
#endif
