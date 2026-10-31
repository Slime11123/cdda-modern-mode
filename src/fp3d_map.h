#pragma once
#ifndef CATA_SRC_FP3D_MAP_H
#define CATA_SRC_FP3D_MAP_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>
#include <string>
#include <vector>
#include "color.h"
#include "coordinates.h"
class avatar;
namespace fp3d_map
{
struct cell {
    tripoint_abs_omt position;
    std::string name = "未探索";
    std::string symbol;
    std::string note;
    std::string note_text;
    bool editable = false;
    nc_color color = c_dark_gray;
    bool known = false;
    bool route = false;
    bool objective = false;
};
struct model {
    tripoint_abs_omt player;
    tripoint_abs_omt center;
    int radius = 12;
    std::optional<tripoint_abs_omt> objective;
    std::vector<cell> cells;
};
// Existing overmaps and native vision levels only; never reveal or generate terrain.
model capture( const avatar &you, const tripoint_abs_omt &center, int radius );
// Empty text deletes a note. Never generate terrain just to write a marker.
bool write_note( const tripoint_abs_omt &position, const std::string &text, std::string &error );
}
#endif
#endif
