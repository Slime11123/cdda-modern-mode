#pragma once
#ifndef CATA_SRC_FP3D_QUICK_MENU_H
#define CATA_SRC_FP3D_QUICK_MENU_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <memory>
#include <optional>
#include <string>
#include "action.h"
namespace cataimgui
{
class window;
}
namespace fp3d_quick_menu
{
struct entry {
    const char *label;
    const char *description;
    action_id action;
    const char *command;
    bool enabled = true;
};
const std::array<entry, 8> &entries();
// Clockwise sectors start at twelve o'clock. The centre and exterior cancel.
int sector_at( float x, float y, float inner_radius, float outer_radius );
std::unique_ptr<cataimgui::window> create( int scale_percent = 100 );
std::unique_ptr<cataimgui::window> create( const std::array<entry, 8> &choices,
        const std::string &heading, int scale_percent = 100 );
// Drawing/selection only; the caller dispatches the returned native request.
std::optional<entry> show();
std::optional<entry> show( const std::array<entry, 8> &choices, const std::string &heading );
}
#endif
#endif
