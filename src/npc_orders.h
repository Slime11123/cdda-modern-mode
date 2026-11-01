#pragma once
#ifndef CATA_SRC_NPC_ORDERS_H
#define CATA_SRC_NPC_ORDERS_H
class avatar;
class npc;
namespace npc_orders
{
enum class kind { follow, guard };
// Native shouted follower commands; queries do not change actor state.
bool can_issue( const avatar &you, const npc &person, kind order );
bool issue( avatar &you, npc &person, kind order );
}
#endif
