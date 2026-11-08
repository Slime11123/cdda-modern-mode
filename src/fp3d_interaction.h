#pragma once
#ifndef CATA_SRC_FP3D_INTERACTION_H
#define CATA_SRC_FP3D_INTERACTION_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include "action.h"
#include "fp3d_targeting.h"
#include "fp3d_quick_menu.h"
class avatar;
class map;
class activity_actor;
class JsonValue;
namespace fp3d_interaction
{
struct request {
    action_id action = ACTION_NULL;
    std::optional<tripoint_bub_ms> position;
    bool quick_pickup = false;
    bool targeted_pickup = false;
    bool targeted_melee = false;
};
bool valid( const fp3d_targeting::target &target, const avatar &you, const map &here );
// 3D directional actions are armed first, then resolved by E on the reticle.
bool begin_direction_selection( action_id action );
void cancel_direction_selection();
std::string direction_selection_hint();

// Runs at the native action boundary, never from the renderer or input resolver.
bool melee( avatar &you, map &here, const tripoint_bub_ms &position );
// Resolve and fire on the native game thread, preserving native ballistics.
bool fire( avatar &you, map &here );
std::array<fp3d_quick_menu::entry, 8> context_entries( const fp3d_targeting::target &target,
        const avatar &you, const map &here );
std::optional<request> resolve( const std::string &command,
                                const std::optional<fp3d_targeting::target> &target, avatar &you, map &here );
bool quick_pickup( avatar &you, map &here, const tripoint_bub_ms &position,
                   bool targeted = false );
std::vector<fp3d_quick_menu::entry> context_2d_entries( const tripoint_bub_ms &position,
        const avatar &you, const map &here );
std::optional<request> resolve_2d_choice( const fp3d_quick_menu::entry &choice,
        const tripoint_bub_ms &position, avatar &you, map &here );
// The native destination activity submits its revalidated action at arrival.
std::optional<request> take_2d_arrival();
std::unique_ptr<activity_actor> deserialize_2d_interaction( JsonValue &data );
}
#endif
#endif
