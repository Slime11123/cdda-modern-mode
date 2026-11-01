#pragma once
#ifndef CATA_SRC_FP3D_INVENTORY_H
#define CATA_SRC_FP3D_INVENTORY_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>
#include <string>
#include <vector>
#include "action.h"
#include "coordinates.h"
#include "item_location.h"
#include "type_id.h"
#include "npc_orders.h"
class avatar;
class map;
namespace fp3d_inventory
{
struct entry {
    item_location location;
    std::string name;
    std::string group;
    bool nearby = false;
    bool equipped = false;
    int category = 0;
    float volume_l = 0;
    float weight_kg = 0;
};
struct group {
    std::string name;
    item_location container;
    float capacity_l = 0;
    float used_l = 0;
    bool nearby = false;
    std::vector<size_t> entries;
    std::optional<tripoint_abs_ms> tile;
    bool vehicle = false;
    float capacity_kg = 0;
    float used_kg = 0;
    float max_length_cm = 0;
    bool sealed = false;
    int pocket_index = -1;
};
struct model {
    std::vector<entry> entries;
    std::vector<group> groups;
};
model capture( avatar &you, map &here );
bool accessible( const item_location &location, const avatar &you, const map &here );
enum class operation_kind { pickup, drop, wield, wear, takeoff, use, insert, place, native, follower_order, completed };
struct operation {
    operation_kind kind = operation_kind::pickup;
    drop_locations items;
    item_location destination;
    action_id native_action = ACTION_NULL;
    recipe_id crafting_recipe;
    std::optional<int> tracking_mission;
    std::optional<int> talking_npc_id;
    std::optional<npc_orders::kind> follower_command;
    std::optional<construction_group_str_id> construction_group;
    int hotbar_slot = -1;
    std::optional<tripoint_abs_ms> destination_tile;
    bool to_vehicle = false;
    int destination_pocket = -1;
    std::string use_method;
    bool activate_relic = false;
};
struct use_option {
    std::string method;
    std::string name;
    std::string description;
    bool enabled = false;
    bool relic = false;
};
std::vector<use_option> use_options( const item_location &location, const avatar &you, const map &here );
void assign_slot( int index, item_location item );
std::optional<operation> select_slot( int index );
int active_slot();
void clear_slots();
std::vector<std::string> slot_names();
bool can_insert( const operation &action, avatar &you, const map &here, std::string &error );
bool execute( const operation &action, avatar &you, map &here, std::string *error = nullptr );
void stage( operation action );
bool pending();
recipe_id take_craft_recipe();
construction_group_str_id take_construction_group();
std::optional<int> take_talking_npc_id();
void clear_pending();
bool execute_pending( avatar &you, map &here, std::string *error = nullptr );
}
#endif
#endif
