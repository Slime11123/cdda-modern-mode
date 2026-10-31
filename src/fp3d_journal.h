#pragma once
#ifndef CATA_SRC_FP3D_JOURNAL_H
#define CATA_SRC_FP3D_JOURNAL_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>
#include <string>
#include <vector>
#include "coordinates.h"
class avatar;
class map;
namespace fp3d_journal
{
struct mission_entry {
    int id = -1;
    int state = 0; // Active, completed, failed.
    bool tracked = false;
    std::string name;
    std::string description;
    std::string destination;
    std::string deadline;
};
struct person_entry {
    tripoint_abs_ms position;
    std::string name;
    std::string attitude;
    int distance = 0;
    bool ally = false;
    bool following = false;
    int id = -1;
};
struct model {
    std::vector<mission_entry> missions;
    std::vector<person_entry> people;
};
model capture( const avatar &you, map &here );
bool track( avatar &you, int mission_id );
// Revalidate identity, unchanged position and native perception immediately before dialogue.
std::optional<tripoint_bub_ms> dialogue_target( const avatar &you, const map &here, int npc_id,
        const tripoint_abs_ms &expected_position );
}
#endif
#endif
