#pragma once
#ifndef CATA_SRC_FP3D_FURNITURE_H
#define CATA_SRC_FP3D_FURNITURE_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>
#include <string>
#include <vector>

namespace fp3d_furniture
{
enum class kind { table, chair, bed, bed_frame, crate, open_crate, cupboard, cabinet, rack, locker };
struct model {
    std::string id;
    kind shape;
};
struct part {
    float x, z, width, depth, bottom, top;
};
// Owned, read-only presentation shared by built furniture, ghosts and picking.
std::optional<model> describe( const std::string &id );
const std::vector<part> &parts( kind shape );
float item_surface( kind shape );
// Only already-captured accessible contents receive the storage display pass.
bool shows_contents( kind shape );
}
#endif
#endif
