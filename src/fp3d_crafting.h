#pragma once
#ifndef CATA_SRC_FP3D_CRAFTING_H
#define CATA_SRC_FP3D_CRAFTING_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <string>
#include <vector>
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
};
std::vector<entry> capture( const avatar &you );
std::string details( const recipe_id &id );
std::string details( const recipe_id &id, const avatar &crafter );
}
#endif
#endif
