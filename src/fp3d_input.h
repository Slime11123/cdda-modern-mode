#pragma once
#ifndef CATA_SRC_FP3D_INPUT_H
#define CATA_SRC_FP3D_INPUT_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <string>
#include <memory>
#include <deque>
#include <map>
#include "input_enums.h"
#include "action.h"

namespace cataimgui
{
class window;
}
class input_context;
struct input_event;
namespace fp3d_input
{
enum class mode { original_2d, modern_2d, first_person_3d };
mode next_mode( mode controls );
input_context modern_2d_context();
input_context context();
bool modifier_binding_editor();
void register_world_actions( input_context &world );
// One physical press, classified at the input boundary. No world mutations.
class gestures
{
        struct held {
            std::string command;
            unsigned started = 0;
            bool fired = false;
        };
        std::map<std::pair<input_event_t, int>, held> buttons;
        std::deque<std::string> pending;
        bool expanded = false;
        unsigned hold_delay_ms = 250;
    public:
        unsigned hold_delay() const { return hold_delay_ms; }
        // Session setting for interact/pickup only. Changing it cancels held input.
        void set_hold_delay( int milliseconds );
        bool event( const input_event &input, bool down, unsigned now );
        bool mouse_button( unsigned button, bool down, unsigned now );
        std::string poll( unsigned now );
        void cancel();
        bool expanded_hud() const {
            return expanded;
        }
};
gestures &world_gestures();
// Translate profile commands into native actions; never change world state.
std::string resolve( mode controls, const input_event &event, float yaw, bool driving = false );
bool vehicle_controls();
std::unique_ptr<cataimgui::window> create_mode_settings( int &view, int &keys, int &style );
action_id display_bindings( input_context &original );
}
#endif
#endif
