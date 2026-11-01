#pragma once
#ifndef CATA_SRC_FP3D_INTERACTION_H
#define CATA_SRC_FP3D_INTERACTION_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include "action.h"
#include "fp3d_targeting.h"
#include "fp3d_quick_menu.h"
class avatar;
class map;
namespace fp3d_interaction
{
struct request {
    action_id action = ACTION_NULL;
    std::optional<tripoint_bub_ms> position;
    bool quick_pickup = false;
};
bool valid( const fp3d_targeting::target &target, const avatar &you, const map &here );
std::array<fp3d_quick_menu::entry, 8> context_entries( const fp3d_targeting::target &target,
        const avatar &you, const map &here );
std::optional<request> resolve( const std::string &command,
                                const std::optional<fp3d_targeting::target> &target, avatar &you, map &here );
bool quick_pickup( avatar &you, map &here, const tripoint_bub_ms &position );
}
#endif
#endif
