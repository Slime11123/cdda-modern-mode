#pragma once
#ifndef CATA_SRC_FP3D_CRAFTING_H
#define CATA_SRC_FP3D_CRAFTING_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <string>
#include <vector>
#include <optional>
#include "coordinates.h"
#include "type_id.h"
class avatar;
namespace fp3d_crafting
{
struct entry {
    recipe_id id;
    itype_id result;
    std::string variant;
    std::string name;
    std::string skill;
    int difficulty = 0;
    crafting_category_id category;
    std::string category_name;
};
std::vector<entry> capture( const avatar &you );
bool is_favorite( const recipe_id &id );
bool set_favorite( const avatar &you, const recipe_id &id, bool favorite );
std::string details( const recipe_id &id );
std::string details( const recipe_id &id, const avatar &crafter, int batch_size = 1 );
bool start( avatar &you, const recipe_id &id, int batch_size, std::string &error,
            std::optional<tripoint_bub_ms> position = std::nullopt );
}
#endif
#endif
