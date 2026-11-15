#include "fp3d_menu_startup.h"

#include <chrono>
#include <cstdio>
#include <fstream>

namespace fp3d_menu
{

const char *state_name( demo_state state )
{
    switch( state ) {
        case demo_state::idle:
            return "idle";
        case demo_state::starting:
            return "starting";
        case demo_state::ready:
            return "ready";
        case demo_state::failed:
            return "failed";
        case demo_state::unsupported:
            return "unsupported";
    }
    return "unknown";
}

void readiness::set_total_budget( unsigned long long ms )
{
    budget_ = ms;
}

void readiness::set_max_failed_attempts( int count )
{
    max_failed_ = count > 0 ? count : 1;
}

void readiness::begin_session( unsigned long long now_ms_value )
{
    if( first_start_ == 0 ) {
        first_start_ = now_ms_value;
    }
    // A new session invalidates every byte the previous one published: the
    // title screen must never treat last run's frame as this run's first frame.
    active_ = true;
    ready_ = false;
    failed_ = false;
    had_progress_ = false;
    last_seq_ = 0;
    accepted_ = 0;
}

void readiness::end_session()
{
    // Only a session that never published anything counts as a failed attempt:
    // a worker that correctly stopped after publishing thousands of frames is
    // not a startup failure.
    if( active_ && accepted_ == 0 ) {
        ++failed_attempts_;
    }
    active_ = false;
    ready_ = false;
    had_progress_ = false;
    last_seq_ = 0;
    accepted_ = 0;
}

void readiness::reset()
{
    first_start_ = 0;
    active_ = false;
    ready_ = false;
    unsupported_ = false;
    failed_ = false;
    had_progress_ = false;
    ever_ready_ = false;
    accepted_ = 0;
    failed_attempts_ = 0;
    last_seq_ = 0;
}

void readiness::mark_unsupported()
{
    unsupported_ = true;
    active_ = false;
    ready_ = false;
}

void readiness::mark_failed()
{
    failed_ = true;
    active_ = false;
    ready_ = false;
}

bool readiness::accept_frame( const frame_report &report, unsigned long long now_ms_value )
{
    if( unsupported_ || !active_ ) {
        // Frames of an earlier session, or of a session that already failed,
        // are never good enough to gate the first presentation on.
        return false;
    }
    if( ready_ ) {
        return false;
    }
    // The startup budget is spent: a late frame must not make the menu wait
    // again. Once a backdrop has been shown, later sessions are not limited.
    if( !ever_ready_ && first_start_ != 0 && now_ms_value > first_start_ + budget_ ) {
        return false;
    }
    // A sequence number of zero means the producer never completed a frame; a
    // non increasing one is a replayed file or a stale mapping.
    if( report.seq <= 0 || report.seq <= last_seq_ ) {
        return false;
    }
    // Written-while-copying: the pixels may be a mix of two frames.
    if( !report.seq_stable ) {
        return false;
    }
    // Texture creation or pixel upload failed: there is nothing to draw.
    if( !report.uploaded ) {
        return false;
    }
    if( report.width <= 0 || report.height <= 0 ) {
        return false;
    }
    last_seq_ = report.seq;
    had_progress_ = true;
    ++accepted_;
    ready_ = true;
    ever_ready_ = true;
    return true;
}

demo_state readiness::evaluate( unsigned long long now_ms_value ) const
{
    if( unsupported_ ) {
        return demo_state::unsupported;
    }
    if( ready_ ) {
        return demo_state::ready;
    }
    // Once a scene has been shown, a restart of the producer is normal
    // housekeeping; nothing may turn that into a permanent failure.
    if( ever_ready_ ) {
        return demo_state::starting;
    }
    // The producer could not be launched at all.
    if( failed_ ) {
        return demo_state::failed;
    }
    // A worker that keeps dying without a frame must not keep the player on the
    // loading screen, but a single restart is still allowed.
    if( failed_attempts_ >= max_failed_ ) {
        return demo_state::failed;
    }
    // The budget runs from the first attempt, so restarts cannot extend it.
    if( first_start_ != 0 && now_ms_value > first_start_ + budget_ ) {
        return demo_state::failed;
    }
    if( first_start_ == 0 ) {
        return demo_state::idle;
    }
    return demo_state::starting;
}

namespace
{
std::chrono::steady_clock::time_point &timeline_epoch()
{
    static std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    return epoch;
}
std::string &timeline_path()
{
    static std::string path;
    return path;
}
} // namespace

unsigned long long now_ms()
{
    const std::chrono::steady_clock::duration elapsed =
        std::chrono::steady_clock::now() - timeline_epoch();
    return static_cast<unsigned long long>(
               std::chrono::duration_cast<std::chrono::milliseconds>( elapsed ).count() );
}

void timeline_set_path( const std::string &path )
{
    timeline_path() = path;
}

void timeline_mark( const std::string &label )
{
    const unsigned long long ms = now_ms();
    std::fprintf( stderr, "[startup] +%llums %s\n", ms, label.c_str() );
    if( timeline_path().empty() ) {
        return;
    }
    std::ofstream out( timeline_path(), std::ios::app );
    if( out ) {
        out << ms << '\t' << label << '\n';
    }
}

} // namespace fp3d_menu
