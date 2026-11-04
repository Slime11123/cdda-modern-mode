#pragma once
#ifndef CATA_SRC_FP3D_SIMPLE_UI_H
#define CATA_SRC_FP3D_SIMPLE_UI_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include "color.h"
#include "imgui/imgui.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>

class item;
struct iteminfo;

namespace cataimgui
{
class window;
}

namespace fp3d_simple_ui
{
// Native query/input loops keep ownership of actions and confirmations.
bool active();
ImVec4 text_color( const nc_color &color );
void accent_text( const std::string &text );
std::vector<iteminfo> item_information( const item &object );
// Adapt a native screen's drawing; its existing input loop still executes actions.
std::unique_ptr<cataimgui::window> create_panel( const std::string &title,
        const std::function<void( cataimgui::window & )> &draw,
        const std::function<bool()> &visible = {} );

class scoped_theme
{
    public:
        scoped_theme();
        ~scoped_theme();
        scoped_theme( const scoped_theme & ) = delete;
        scoped_theme &operator=( const scoped_theme & ) = delete;
    private:
        bool enabled;
};
}
#endif
#endif
