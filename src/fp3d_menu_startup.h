#pragma once
#ifndef CATA_SRC_FP3D_MENU_STARTUP_H
#define CATA_SRC_FP3D_MENU_STARTUP_H

#include <string>

/**
 * Startup gate for the native title-screen backdrop.
 *
 * The title screen may only be shown together with its background in one single
 * presentation: "the demonstration process was started", "a frame file exists"
 * or "a texture pointer is not null" are not enough. This file owns the
 * decision, free of SDL, so it can be unit tested: the caller reports what the
 * frame channel actually did (sequence stability, texture upload result) and
 * receives the state the title screen is allowed to show.
 */
namespace fp3d_menu
{

enum class demo_state {
    idle,        //!< No session has been started yet.
    starting,    //!< Session running, no usable frame yet.
    ready,       //!< A validated frame of the current session can be drawn.
    failed,      //!< No usable frame will arrive (budget spent / worker keeps dying).
    unsupported, //!< This platform cannot provide the native backdrop.
};

//! Human readable name, used in logs.
const char *state_name( demo_state state );

/**
 * One report from the frame channel.
 *
 * @c uploaded must only be true when the texture was created *and* the pixels
 * were uploaded successfully. @c seq_stable must only be true when the
 * consistency check around the copy passed (shared memory written by the worker
 * while copying means the candidate must be thrown away).
 */
struct frame_report {
    long seq = 0;
    int width = 0;
    int height = 0;
    bool seq_stable = false;
    bool uploaded = false;
};

class readiness
{
    public:
        //! Total budget counted from the first session start of this process.
        //! Retries do not extend it.
        static constexpr unsigned long long default_total_budget_ms = 180000ULL;
        //! Attempts that die without ever publishing a frame before giving up.
        static constexpr int default_max_failed_attempts = 2;

        void set_total_budget( unsigned long long ms );
        void set_max_failed_attempts( int count );

        //! Starts (or restarts) the frame session; every per-session flag resets.
        void begin_session( unsigned long long now_ms );
        //! The worker is gone. Ends the session without granting readiness.
        void end_session();
        //! Forgets everything, including the total budget (leaving the title).
        void reset();
        void mark_unsupported();
        //! The producer cannot run at all (launch failed); no frame will arrive.
        void mark_failed();

        bool session_active() const {
            return active_;
        }
        bool ready() const {
            return ready_;
        }
        bool unsupported() const {
            return unsupported_;
        }
        unsigned long long frames_accepted() const {
            return accepted_;
        }
        int failed_attempts() const {
            return failed_attempts_;
        }
        //! First start of this process, 0 when never started.
        unsigned long long first_start_ms() const {
            return first_start_;
        }

        /**
         * Validates one frame report.
         * @return true when this report is what made the backdrop ready.
         */
        bool accept_frame( const frame_report &report, unsigned long long now_ms );

        demo_state evaluate( unsigned long long now_ms ) const;

    private:
        unsigned long long first_start_ = 0;
        unsigned long long budget_ = default_total_budget_ms;
        int max_failed_ = default_max_failed_attempts;
        bool active_ = false;
        bool ready_ = false;
        bool unsupported_ = false;
        //! Hard failure: the producer could not even be launched.
        bool failed_ = false;
        bool had_progress_ = false;
        //! Set once any session produced a usable frame; afterwards the startup
        //! budget and the retry limits no longer apply (a restart is routine).
        bool ever_ready_ = false;
        unsigned long long accepted_ = 0;
        int failed_attempts_ = 0;
        long last_seq_ = 0;
};

/**
 * Monotonic milliseconds since the first call, for deadlines and for the
 * startup timeline. Independent of the render frame counter.
 */
unsigned long long now_ms();

//! Redirects the startup timeline into a file; empty path disables it.
void timeline_set_path( const std::string &path );
//! Appends one labelled point in time (also mirrored to stderr).
void timeline_mark( const std::string &label );

} // namespace fp3d_menu

#endif // CATA_SRC_FP3D_MENU_STARTUP_H
