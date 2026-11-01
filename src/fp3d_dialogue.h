#pragma once
#ifndef CATA_SRC_FP3D_DIALOGUE_H
#define CATA_SRC_FP3D_DIALOGUE_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>
#include <string>
#include <vector>
#include "cata_imgui.h"
#include "dialogue_win.h"

class npc;

namespace fp3d_dialogue
{
// Presentation only: actions return to dialogue::opt for native validation/effects.
class window : public cataimgui::window
{
    public:
        window( const npc &speaker, const std::string &history, int &selection,
                bool can_close, int scale_percent );
        ~window() override;
        void set_responses( const std::vector<talk_data> &lines, const std::vector<bool> &enabled );
        void set_history( const std::string &history );
        void handle_action( std::string &action );
        void request_confirmation( const std::string &warning );
        std::optional<bool> take_confirmation();
        void draw() override;

    protected:
        cataimgui::bounds get_bounds() override;
        void draw_controls() override;

    private:
        std::string name;
        std::string transcript;
        std::vector<talk_data> responses;
        std::vector<bool> available;
        int &selected;
        bool can_close;
        float scale;
        bool restore_no_mouse;
        bool reset_scroll = true;
        bool scroll_to_history = false;
        bool scroll_to_selection = false;
        std::string pending_action;
        std::string warning;
        bool open_warning = false;
        std::optional<bool> confirmation;
        SDL_Texture_Ptr portrait;
        SDL_Texture_Ptr background;
};
}
#endif
#endif
