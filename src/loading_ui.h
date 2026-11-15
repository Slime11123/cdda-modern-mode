#pragma once
#ifndef CATA_SRC_LOADING_UI_H
#define CATA_SRC_LOADING_UI_H

#include <string>

namespace loading_ui
{
/**
 * Loading screen for world/mod data, shown while a world is active.
 */
void show( const std::string &context, const std::string &step );
void done();

/**
 * Startup (title screen) loading screen.
 *
 * Unlike show() this works before any world is active: it uses the vanilla
 * default image and never dereferences world_generator->active_world. It is a
 * fully opaque layer, so no menu or title button can leak through while the
 * game is still preparing itself.
 */
void begin_startup();
//! True while the startup layer is on screen.
bool startup_active();
//! Replaces the stage line ("Preparing the background scene", …).
void set_stage( const std::string &step );
//! Shows/hides the "Esc skips this wait" line under the tip.
void set_skip_hint( bool enabled );
//! Draws one frame, rotates the vanilla tip on a monotonic clock, discards input.
void update();
//! Like update(), but reports whether the player asked to skip the wait (Esc).
//! The key is consumed here, so it can never reach the title screen afterwards.
bool update_and_check_skip();
//! Removes the startup layer.
void end_startup();
} // namespace loading_ui

#endif // CATA_SRC_LOADING_UI_H
