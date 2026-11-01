#include "fp3d_input.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <limits>
#include "cata_catch.h"
#include "input.h"

TEST_CASE( "fp3d_hold_delay_changes_interaction_boundaries_while_preserving_attack_timing",
           "[fp3d][input]" )
{
    fp3d_input::gestures input;
    const input_event interact( 'e', input_event_t::keyboard_code );
    const input_event pickup( 'f', input_event_t::keyboard_code );
    const input_event attack( MouseInput::LeftButtonPressed, input_event_t::mouse );
    CHECK( input.hold_delay() == 250 );
    input.set_hold_delay( 150 );
    REQUIRE( input.event( interact, true, 100 ) );
    CHECK( input.poll( 249 ).empty() );
    CHECK( input.poll( 250 ) == "FP3D_INTERACT_LONG" );
    CHECK( input.poll( 251 ).empty() );
    REQUIRE( input.event( interact, false, 260 ) );
    CHECK( input.poll( 260 ).empty() );

    input.set_hold_delay( 750 );
    REQUIRE( input.event( pickup, true, 1000 ) );
    CHECK( input.poll( 1749 ).empty() );
    REQUIRE( input.event( pickup, false, 1749 ) );
    CHECK( input.poll( 1749 ) == "FP3D_PICKUP" );
    CHECK( input.poll( 1750 ).empty() );
    REQUIRE( input.event( pickup, true, 2000 ) );
    CHECK( input.poll( 2749 ).empty() );
    CHECK( input.poll( 2750 ) == "FP3D_PICKUP_LONG" );
    REQUIRE( input.event( pickup, false, 2800 ) );
    CHECK( input.poll( 2800 ).empty() );

    REQUIRE( input.event( interact, true, 3000 ) );
    input.set_hold_delay( 1000 );
    CHECK( input.poll( 5000 ).empty() );
    CHECK_FALSE( input.event( interact, false, 5000 ) );
    REQUIRE( input.event( attack, true, 6000 ) );
    CHECK( input.poll( 6000 ) == "FP3D_ATTACK" );
    CHECK( input.poll( 6249 ).empty() );
    CHECK( input.poll( 6250 ) == "FP3D_ATTACK" );
    REQUIRE( input.event( attack, false, 6300 ) );
    CHECK( input.poll( 7000 ).empty() );

    input.set_hold_delay( -1 );
    CHECK( input.hold_delay() == 100 );
    input.set_hold_delay( 99999 );
    CHECK( input.hold_delay() == 1000 );
    input.cancel();
    CHECK( input.hold_delay() == 1000 );
    REQUIRE( input.event( interact, true, 8000 ) );
    input.set_hold_delay( 1000 );
    CHECK( input.poll( 8999 ).empty() );
    CHECK( input.poll( 9000 ) == "FP3D_INTERACT_LONG" );
    input.cancel();
    REQUIRE( input.event( interact, true, std::numeric_limits<unsigned>::max() - 499 ) );
    CHECK( input.poll( 499 ).empty() );
    CHECK( input.poll( 500 ) == "FP3D_INTERACT_LONG" );
}
#endif
