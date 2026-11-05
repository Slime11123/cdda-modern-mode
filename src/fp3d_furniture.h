#pragma once
#ifndef CATA_SRC_FP3D_FURNITURE_H
#define CATA_SRC_FP3D_FURNITURE_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace fp3d_furniture
{
enum class kind {
    table, chair, bed, bed_frame, crate, open_crate, cupboard, cabinet, rack, locker,
    counter, desk, sofa, sink, oven, fridge, mini_fridge, dresser, bathtub, toilet,
    bench, park_bench, armchair, fireplace, washer, dishwasher, wood_stove, stool, chest_freezer
};
struct model {
    std::string id;
    kind shape;
};
struct part {
    float x, z, width, depth, bottom, top;
    bool round = false;
};
// Owned, read-only presentation shared by built furniture, ghosts and picking.
std::optional<model> describe( const std::string &id );
const std::vector<part> &parts( kind shape );
float item_surface( kind shape );
// Only already-captured accessible contents receive the storage display pass.
bool shows_contents( kind shape );
// Backing walls in native north/east/south/west order; no live-map queries.
unsigned facing( kind shape, const std::array<bool, 4> &walls,
                 const std::array<bool, 4> &tables = {} );
bool chair_table( const std::optional<model> &furniture );
// Same quarter-turn transform as the rendered model, for part-level picking.
part rotated( part value, unsigned quarter_turns );
}
#endif
#endif
