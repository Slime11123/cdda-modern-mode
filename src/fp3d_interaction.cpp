#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "creature_tracker.h"
#include "fp3d_input.h"
#include "fp3d_inventory.h"
#include "fp3d_journal.h"
#include "fp3d_modern_ui.h"
#include "fp3d_compositor.h"
#include "fp3d_quick_menu.h"
#include "game.h"
#include "gun_mode.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "mapdata.h"
#include "submap.h"
#include "messages.h"
#include "npc.h"
#include "translations.h"
namespace fp3d_interaction
{
bool valid( const fp3d_targeting::target &target, const avatar &you, const map &here )
{
    const auto p = here.get_bub( target.position );
    if( you.is_dead_state() || you.is_blind() || target.origin != you.pos_abs() ||
        p.z() != you.posz() || !here.inbounds( p ) || square_dist( you.pos_bub( here ), p ) > 1 ||
        !you.sees( here, p ) ) {
        return false;
    }
    if( target.type == fp3d_targeting::kind::creature ) {
        const auto *critter = get_creature_tracker().creature_at( p );
        return critter && you.sees( here, *critter );
    }
    if( target.type == fp3d_targeting::kind::item ) {
        if( !here.could_see_items( p, you ) ) {
            return false;
        }
        const auto tile = here.maptile_at( p );
        return tile.get_item_count() > 0 && tile.get_uppermost_item().typeId().str() == target.id;
    }
    return here.ter( p ).id().str() == target.id;
}

std::array<fp3d_quick_menu::entry, 8> context_entries( const fp3d_targeting::target &target,
        const avatar &you, const map &here )
{
    const bool reachable = valid( target, you, here );
    const auto p = here.get_bub( target.position );
    const auto *critter = reachable ? get_creature_tracker().creature_at( p ) : nullptr;
    const bool creature = critter && !critter->is_dead_state() && p != you.pos_bub( here ) &&
                          you.sees( here, *critter );
    const auto *person = creature ? get_creature_tracker().creature_at<npc>( p ) : nullptr;
    const bool object = reachable && !creature;
    const bool opens = object && ( here.ter( p ).obj().open || here.furn( p ).obj().open );
    const bool closes = object && ( here.ter( p ).obj().close || here.furn( p ).obj().close );
    const bool items = object && here.could_see_items( p, you ) &&
                       here.maptile_at( p ).get_item_count() > 0;
    const bool examine = object && ( here.has_furn( p ) || opens || closes || items ||
                                     ( !here.ter( p ).obj().has_examine( iexamine::none ) &&
                                       !here.ter( p ).obj().has_examine( iexamine::invalid ) && here.ter( p ).obj().can_examine( p ) ) );
    return {{
            { "打开", "打开目标门窗或容器，沿用原版规则。", ACTION_OPEN, nullptr, opens, opens || closes },
            { "关闭", "关闭目标门窗或容器，沿用原版规则。", ACTION_CLOSE, nullptr, closes, opens || closes },
            { "交谈", "与此人物交谈；交易和招募仍由原版确认。", ACTION_CHAT, nullptr, person != nullptr, person != nullptr },
            { "查看 / 搜索", "查看目标或搜索容器，沿用原版检查。", ACTION_EXAMINE_AND_PICKUP, nullptr, examine, examine },
            { "拾取物品", "打开目标位置的原版物品选择。", ACTION_PICKUP, nullptr, items, items },
            {
                "原地守卫", "让队友守卫当前位置，按原版喊话和行动消耗结算。", ACTION_NULL, "FP3D_GUARD",
                person && npc_orders::can_issue( you, *person, npc_orders::kind::guard ), person != nullptr
            },
            {
                "继续跟随", "让可听见的守卫队友继续跟随，保留原版营地限制。", ACTION_NULL, "FP3D_FOLLOW",
                person && npc_orders::can_issue( you, *person, npc_orders::kind::follow ), person != nullptr
            },
            { "取消", "返回3D视角，不执行动作。", ACTION_NULL, nullptr, true, true }
        }};
}

bool quick_pickup( avatar &you, map &here, const tripoint_bub_ms &p )
{
    if( p.z() != you.posz() || !here.inbounds( p ) || square_dist( p, you.pos_bub( here ) ) > 1 ||
        !you.sees( here, p ) || !here.accessible_items( p ) || !here.could_see_items( p, you ) ) {
        return false;
    }
    auto objects = here.i_at( p );
    if( objects.empty() ) {
        return false;
    }
    // Native pick_up schedules the standard activity, including capacity,
    // theft, pocket selection, time and interruption rules. No item copying.
    const item *top = &here.maptile_at( p ).get_uppermost_item();
    for( item &object : objects ) {
        if( &object == top ) {
            you.pick_up( { { item_location( map_cursor( p ), &object ), 1 } } );
            return true;
        }
    }
    return false;
}

std::optional<request> resolve( const std::string &command,
                                const std::optional<fp3d_targeting::target> &candidate, avatar &you, map &here )
{
    if( command == "FP3D_ACTIONS" ) {
        const auto choice = fp3d_quick_menu::show();
        if( !choice || !fp3d_compositor::turn_based() ) {
            return std::nullopt;
        }
        if( choice->command ) {
            return resolve( choice->command, candidate, you, here );
        }
        return request{ choice->action, std::nullopt, false };
    }
    if( command.compare( 0, 10, "FP3D_SLOT_" ) == 0 ) {
        const int current = fp3d_inventory::active_slot();
        const int index = command == "FP3D_SLOT_NEXT" ? ( current + 1 ) % 8 : command == "FP3D_SLOT_PREV" ?
                          ( current + 7 ) % 8 : command.size() == 11 ? command.back() - '1' : -1;
        if( index < 0 || index >= 8 ) {
            return std::nullopt;
        }
        auto operation = fp3d_inventory::select_slot( index );
        if( g ) {
            g->invalidate_main_ui_adaptor();
        }
        if( !operation ) {
            return std::nullopt;
        }
        if( operation->kind == fp3d_inventory::operation_kind::completed ) {
            return std::nullopt;
        }
        fp3d_inventory::stage( std::move( *operation ) );
        return request{ ACTION_ITEMACTION, std::nullopt, false };
    }
    if( command == "FP3D_PANEL" || command == "FP3D_PICKUP_LONG" || command == "FP3D_MAP" ) {
        auto operation = fp3d_modern_ui::show( you, here, command == "FP3D_PICKUP_LONG",
                                               command == "FP3D_MAP" ? 4 : -1 );
        if( !operation ) {
            return std::nullopt;
        }
        if( operation->kind == fp3d_inventory::operation_kind::native ) {
            if( operation->tracking_mission ) {
                fp3d_journal::track( you, *operation->tracking_mission );
                return std::nullopt;
            }
            const auto native_action = operation->native_action;
            if( operation->talking_npc_id ) {
                const auto target = operation->destination_tile ?
                                    fp3d_journal::dialogue_target( you, here, *operation->talking_npc_id,
                                        *operation->destination_tile ) : std::nullopt;
                if( native_action != ACTION_CHAT || !target ) {
                    add_msg( m_info, "此人物已移动或当前无法交谈，请刷新队友页。" );
                    return std::nullopt;
                }
                fp3d_inventory::stage( std::move( *operation ) );
                return request{ ACTION_CHAT, *target, false };
            }
            if( ( native_action == ACTION_CRAFT && operation->crafting_recipe != recipe_id() ) ||
                ( native_action == ACTION_CONSTRUCT && operation->construction_group ) ) {
                fp3d_inventory::stage( std::move( *operation ) );
            }
            return request{ native_action, std::nullopt, false };
        }
        fp3d_inventory::stage( std::move( *operation ) );
        return request{ ACTION_ITEMACTION, std::nullopt, false };
    }
    if( command == "FP3D_AIM" || command == "FP3D_ATTACK" ) {
        const auto weapon = you.get_wielded_item();
        if( weapon && weapon->is_gun() && !weapon->gun_current_mode().melee() ) {
            return request{ ACTION_FIRE, std::nullopt, false };
        }
        if( command == "FP3D_AIM" ) {
            return std::nullopt;
        }
    }
    if( !candidate || !valid( *candidate, you, here ) ) {
        fp3d_input::world_gestures().cancel();
        add_msg( m_info, "准星目标当前不可见或无法触及，请靠近并保持同层。" );
        return std::nullopt;
    }
    const auto p = here.get_bub( candidate->position );
    if( command == "FP3D_ATTACK" ) {
        if( fp3d_input::vehicle_controls() ) {
            fp3d_input::world_gestures().cancel();
            add_msg( m_info, "驾驶时请先停止控制车辆再进行近战。" );
            return std::nullopt;
        }
        Creature *critter = get_creature_tracker().creature_at( p );
        if( !critter || critter->is_avatar() || !you.sees( here, *critter ) || p == you.pos_bub( here ) ) {
            fp3d_input::world_gestures().cancel();
            return std::nullopt;
        }
        return request{ get_movement_action_from_delta( p - you.pos_bub( here ), iso_rotate::yes ), p, false };
    }
    if( command == "FP3D_PICKUP" ) {
        if( !here.could_see_items( p, you ) || here.i_at( p ).empty() ) {
            add_msg( m_info, "目标位置没有可拾取的物品。" );
            return std::nullopt;
        }
        return request{ ACTION_PICKUP, p, true };
    }
    if( command == "FP3D_INTERACT" || command == "FP3D_INTERACT_LONG" ) {
        const bool opens = here.ter( p ).obj().open || here.furn( p ).obj().open;
        const bool closes = here.ter( p ).obj().close || here.furn( p ).obj().close;
        const auto *npc = get_creature_tracker().creature_at<::npc>( p );
        const bool can_chat = npc && you.sees( here, *npc );
        const int npc_id = can_chat ? npc->getID().get_value() : -1;
        action_id action = can_chat ? ACTION_CHAT :
                           opens ? ACTION_OPEN : closes ? ACTION_CLOSE : ACTION_EXAMINE_AND_PICKUP;
        if( command == "FP3D_INTERACT_LONG" ) {
            const auto choice = fp3d_quick_menu::show( context_entries( *candidate, you, here ),
                                "目标操作" );
            if( !choice || !choice->enabled || !valid( *candidate, you, here ) ||
                !fp3d_compositor::turn_based() ) {
                return std::nullopt;
            }
            if( choice->command ) {
                const std::string order_command = choice->command;
                if( order_command != "FP3D_GUARD" && order_command != "FP3D_FOLLOW" ) {
                    return std::nullopt;
                }
                fp3d_inventory::operation operation;
                operation.kind = fp3d_inventory::operation_kind::follower_order;
                operation.talking_npc_id = npc_id;
                operation.destination_tile = candidate->position;
                operation.follower_command = order_command == "FP3D_GUARD" ?
                                             npc_orders::kind::guard : npc_orders::kind::follow;
                fp3d_inventory::stage( std::move( operation ) );
                return request{ ACTION_ITEMACTION, std::nullopt, false };
            }
            action = choice->action;
            if( action == ACTION_NULL ) {
                return std::nullopt;
            }
        }
        if( action == ACTION_CHAT ) {
            if( !fp3d_journal::dialogue_target( you, here, npc_id, candidate->position ) ) {
                return std::nullopt;
            }
            fp3d_inventory::operation operation;
            operation.kind = fp3d_inventory::operation_kind::native;
            operation.native_action = ACTION_CHAT;
            operation.talking_npc_id = npc_id;
            operation.destination_tile = candidate->position;
            fp3d_inventory::stage( std::move( operation ) );
        }
        return request{ action, p, false };
    }
    return std::nullopt;
}
}
#endif
