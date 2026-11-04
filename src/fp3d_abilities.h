#pragma once
#ifndef CATA_SRC_FP3D_ABILITIES_H
#define CATA_SRC_FP3D_ABILITIES_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <variant>
#include <vector>

#include "type_id.h"

class avatar;

namespace fp3d_abilities
{
enum class bionic_control { power, weapon, fuel };
struct bionic_request {
    unsigned int uid;
    bionic_control control;
};
using ability_request = std::variant<trait_id, bionic_request, matype_id, bool>;

std::vector<matype_id> available_styles( avatar &you );
bool mutation_ready( const avatar &you, const trait_id &id );
bool execute( avatar &you, const ability_request &request, bool &close );
}
#endif
#endif
