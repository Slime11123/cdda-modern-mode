#pragma once
#ifndef CATA_SRC_FP3D_MAP_H
#define CATA_SRC_FP3D_MAP_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    // Native overmap tileset ID at this exact vision level.
    std::string tileset_id;
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
struct marker_tint {
    std::uint8_t red = 255;
    std::uint8_t green = 220;
    std::uint8_t blue = 80;
};
struct compass_marker {
    tripoint_abs_omt position;
    marker_tint tint;
};
// Use native overmap note codes so X marker colors persist in ordinary saves.
constexpr int marker_palette_size = 8;
const char *marker_color_name( int index );
marker_tint marker_color( std::string_view note );
int marker_color_index( std::string_view note );
std::string marker_label( std::string_view note );
std::string format_marker( const std::string &label, int color_index );
std::vector<compass_marker> compass_markers( const tripoint_abs_omt &player );
}
#endif
#endif
