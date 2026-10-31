#pragma once
#ifndef CATA_SRC_FP3D_MODERN_UI_H
#define CATA_SRC_FP3D_MODERN_UI_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include "fp3d_inventory.h"
#include <memory>
namespace cataimgui
{
class window;
}
namespace fp3d_modern_ui
{
std::unique_ptr<cataimgui::window> create( avatar &you, map &here, int scale_percent = 0,
        bool nearby = false, int initial_page = -1, item_location initial_use = {} );
int scale_percent();
bool transfer_pending();
bool take_resume();
void clear_resume();
// Returns an owned request. The drawing callback never executes gameplay.
std::optional<fp3d_inventory::operation> show( avatar &you, map &here, bool nearby = false,
        int initial_page = -1 );
}
#endif
#endif
