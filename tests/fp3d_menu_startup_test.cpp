// Behaviour tests for the title-screen startup gate.
//
// The rule under test is the product requirement, not the implementation: the
// menu may only be presented together with a backdrop that is actually usable
// for *this* startup. A created texture, an existing process, a mapped section
// or a frame file on disk is never enough on its own.
#include "fp3d_menu_startup.h"

#include <string>

#include "cata_catch.h"

namespace
{

fp3d_menu::readiness fresh_gate()
{
    fp3d_menu::readiness gate;
    gate.begin_session( 1000 );
    return gate;
}

fp3d_menu::frame_report good_frame( long seq = 1, int width = 1280, int height = 720 )
{
    fp3d_menu::frame_report report;
    report.seq = seq;
    report.width = width;
    report.height = height;
    report.seq_stable = true;
    report.uploaded = true;
    return report;
}

} // namespace

TEST_CASE( "menu demo gate stays closed before any session", "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate;
    CHECK( gate.evaluate( 0 ) == fp3d_menu::demo_state::idle );
    CHECK_FALSE( gate.ready() );
    // Nothing can be accepted while no session is running.
    CHECK_FALSE( gate.accept_frame( good_frame(), 0 ) );
    CHECK_FALSE( gate.ready() );
}

TEST_CASE( "menu demo gate requires uploaded pixels, not just a texture",
           "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate = fresh_gate();
    fp3d_menu::frame_report report = good_frame();
    // Texture created but the pixel upload failed: nothing to show.
    report.uploaded = false;
    CHECK_FALSE( gate.accept_frame( report, 1100 ) );
    CHECK( gate.evaluate( 1100 ) == fp3d_menu::demo_state::starting );
    CHECK_FALSE( gate.ready() );
}

TEST_CASE( "menu demo gate rejects an inconsistent sequence", "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate = fresh_gate();
    fp3d_menu::frame_report report = good_frame( 7 );
    // The producer overwrote the pixels while they were copied.
    report.seq_stable = false;
    CHECK_FALSE( gate.accept_frame( report, 1100 ) );
    CHECK_FALSE( gate.ready() );
    // A zero sequence means the producer never completed a frame.
    CHECK_FALSE( gate.accept_frame( good_frame( 0 ), 1100 ) );
    // An empty frame is unusable even when the copy was consistent.
    CHECK_FALSE( gate.accept_frame( good_frame( 8, 0, 720 ), 1100 ) );
    CHECK_FALSE( gate.ready() );
    // A later, valid frame of the same session does open the gate.
    CHECK( gate.accept_frame( good_frame( 9 ), 1100 ) );
    CHECK( gate.ready() );
    CHECK( gate.evaluate( 1100 ) == fp3d_menu::demo_state::ready );
}

TEST_CASE( "menu demo gate accepts a decoded fallback frame", "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate = fresh_gate();
    // The PNG fallback reports the same fields, so it opens the gate by the same rule.
    CHECK( gate.accept_frame( good_frame( 1, 1600, 900 ), 1200 ) );
    CHECK( gate.ready() );
    CHECK( gate.frames_accepted() == 1 );
}

TEST_CASE( "restarting the producer invalidates the previous session",
           "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate = fresh_gate();
    REQUIRE( gate.accept_frame( good_frame( 42 ), 1100 ) );
    REQUIRE( gate.ready() );

    // The worker is restarted: its old frame must not keep the title ready. The
    // stale bytes themselves are excluded by the channel (the frame files are
    // cleared and the shared section is re-opened), which is why the gate only
    // has to drop the ready flag.
    gate.begin_session( 1200 );
    CHECK_FALSE( gate.ready() );
    CHECK( gate.evaluate( 1200 ) == fp3d_menu::demo_state::starting );
    CHECK( gate.frames_accepted() == 0 );
    CHECK( gate.accept_frame( good_frame( 43 ), 1400 ) );
    CHECK( gate.ready() );
}

TEST_CASE( "an upload failure does not block the same frame number later",
           "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate = fresh_gate();
    fp3d_menu::frame_report report = good_frame( 5 );
    report.uploaded = false;
    CHECK_FALSE( gate.accept_frame( report, 1100 ) );
    // Same number, now with pixels: it was already consumed as a candidate.
    REQUIRE( gate.accept_frame( good_frame( 5 ), 1100 ) );
    CHECK( gate.ready() );
}

TEST_CASE( "frames of a finished session are ignored", "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate = fresh_gate();
    gate.end_session();
    CHECK_FALSE( gate.accept_frame( good_frame( 1 ), 1100 ) );
    CHECK_FALSE( gate.ready() );
}

TEST_CASE( "a session that published nothing counts as a failed attempt",
           "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate;
    gate.set_max_failed_attempts( 2 );
    gate.begin_session( 1000 );
    gate.end_session();
    CHECK( gate.failed_attempts() == 1 );
    // One restart is still allowed.
    CHECK( gate.evaluate( 2000 ) == fp3d_menu::demo_state::starting );
    gate.begin_session( 2000 );
    gate.end_session();
    CHECK( gate.failed_attempts() == 2 );
    // Two attempts without a single frame: give up instead of waiting forever.
    CHECK( gate.evaluate( 3000 ) == fp3d_menu::demo_state::failed );
}

TEST_CASE( "a session that published frames is not a failed attempt",
           "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate;
    gate.set_max_failed_attempts( 1 );
    gate.begin_session( 1000 );
    REQUIRE( gate.accept_frame( good_frame( 1 ), 1100 ) );
    gate.end_session();
    CHECK( gate.failed_attempts() == 0 );
}

TEST_CASE( "retries do not extend the total startup budget", "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate;
    gate.set_total_budget( 5000 );
    gate.set_max_failed_attempts( 100 );
    gate.begin_session( 1000 );
    // Repeated stalls/restarts keep the process "starting" but never reset the clock.
    for( unsigned long long t = 1000; t <= 5000; t += 1000 ) {
        gate.begin_session( t );
        CHECK( gate.evaluate( t ) == fp3d_menu::demo_state::starting );
    }
    CHECK( gate.evaluate( 6001 ) == fp3d_menu::demo_state::failed );
    // A late but valid frame cannot resurrect an expired budget for the menu gate.
    const bool late = gate.accept_frame( good_frame( 1 ), 6002 );
    CHECK_FALSE( late );
    CHECK_FALSE( gate.ready() );
}

TEST_CASE( "an unsupported platform never waits for a frame", "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate;
    gate.mark_unsupported();
    CHECK( gate.evaluate( 0 ) == fp3d_menu::demo_state::unsupported );
    CHECK_FALSE( gate.accept_frame( good_frame( 1 ), 0 ) );
}

TEST_CASE( "a producer that cannot be launched fails without waiting",
           "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate;
    gate.begin_session( 1000 );
    // The sandbox directory could not be created or the process was refused:
    // no frame can ever arrive, so the title must not keep waiting.
    gate.mark_failed();
    CHECK( gate.evaluate( 1100 ) == fp3d_menu::demo_state::failed );
    CHECK_FALSE( gate.accept_frame( good_frame( 1 ), 1200 ) );
    // Leaving and re-entering the title arms the gate again.
    gate.reset();
    CHECK( gate.evaluate( 1300 ) == fp3d_menu::demo_state::idle );
}

TEST_CASE( "leaving the title clears the gate for the next visit",
           "[fp3d_menu_startup]" )
{
    fp3d_menu::readiness gate;
    gate.set_total_budget( 5000 );
    gate.begin_session( 1000 );
    REQUIRE( gate.accept_frame( good_frame( 1 ), 1100 ) );
    gate.end_session();

    gate.reset();
    CHECK( gate.evaluate( 9000 ) == fp3d_menu::demo_state::idle );
    CHECK( gate.failed_attempts() == 0 );
    CHECK( gate.first_start_ms() == 0 );
    // A fresh visit gets a fresh budget after a previous failure.
    gate.begin_session( 9000 );
    CHECK( gate.evaluate( 9000 ) == fp3d_menu::demo_state::starting );
    CHECK( gate.accept_frame( good_frame( 1 ), 9100 ) );
    CHECK( gate.ready() );
}

TEST_CASE( "startup timeline reports monotonic milliseconds", "[fp3d_menu_startup]" )
{
    const unsigned long long first = fp3d_menu::now_ms();
    const unsigned long long second = fp3d_menu::now_ms();
    CHECK( second >= first );
    CHECK( std::string( fp3d_menu::state_name( fp3d_menu::demo_state::ready ) ) == "ready" );
}
