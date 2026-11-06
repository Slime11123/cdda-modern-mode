#pragma once
#ifndef CATA_SRC_FP3D_QUICK_MENU_H
#define CATA_SRC_FP3D_QUICK_MENU_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "point.h"
#include "sdl_wrappers.h"
#include "action.h"
namespace cataimgui
{
class window;
}
struct uilist_entry;
namespace fp3d_quick_menu
{
struct entry {
    const char *label;
    const char *description;
    action_id action;
    const char *command;
    bool enabled = true;
    bool visible = true;
};
const std::array<entry, 8> &entries();
// Clockwise sectors start at twelve o'clock. The centre and exterior have no sector.
int sector_at( float x, float y, float inner_radius, float outer_radius, int count = 8 );
std::unique_ptr<cataimgui::window> create( int scale_percent = 100 );
std::unique_ptr<cataimgui::window> create( const std::array<entry, 8> &choices,
        const std::string &heading, int scale_percent = 100 );
// Drawing/selection only; the caller dispatches the returned native request.
std::optional<entry> show();
std::optional<entry> show( const std::array<entry, 8> &choices, const std::string &heading );
// Preserve native menu return values; cancellation returns to the caller's parent.
std::unique_ptr<cataimgui::window> create_paged( const std::vector<uilist_entry> &choices,
        const std::string &heading, int scale_percent = 100, int page = 0 );
std::optional<int> show_paged( const std::vector<uilist_entry> &choices,
                             const std::string &heading, int &page );
// Cursor-anchored list; all actions are returned to the native dispatcher.
std::unique_ptr<cataimgui::window> create_dropdown( const std::vector<entry> &choices,
        const point &anchor, int scale_percent = 100, SDL_Texture_Ptr background = {} );
std::optional<entry> show_dropdown( const std::vector<entry> &choices, const point &anchor );
}
#endif
#endif
