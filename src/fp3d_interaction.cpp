#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "character_id.h"
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
#include "monster.h"
#include "npc.h"
#include "popup.h"
#include "translations.h"
namespace fp3d_interaction
{
bool melee( avatar &you, map &here, const tripoint_bub_ms &p )
{
    Creature *critter = get_creature_tracker().creature_at( p );
    if( you.is_dead_state() || you.is_blind() || fp3d_input::vehicle_controls() ||
        you.has_active_mutation( trait_id( "SHELL2" ) ) ||
        you.has_active_mutation( trait_id( "SHELL3" ) ) || you.has_effect( efftype_id( "incorporeal" ) ) ||
        !here.inbounds( p ) || p.z() != you.posz() ||
        square_dist( you.pos_bub( here ), p ) != 1 || !critter || critter->is_avatar() ||
        critter->is_dead_state() || !you.sees( here, *critter ) ) {
        fp3d_input::world_gestures().cancel();
        return false;
    }
    npc *person = critter->as_npc();
    const bool hostile = person ? person->is_enemy() :
                         critter->attitude_to( you ) == Creature::Attitude::HOSTILE;
    if( !hostile ) {
        // Disarm the held attack before opening a native, default-No dialog.
        // Closing it cannot turn the same physical press into another attack.
        fp3d_input::world_gestures().cancel();
        const auto origin = you.pos_abs();
        const auto identity = person ? person->getID() : character_id();
        const bool confirmed = query_popup().context( "YESNO" )
                               .message( "%s 并非敌对目标。攻击可能导致其反击或失去友好关系。确定攻击？",
                                         critter->disp_name() )
                               .option( "YES" ).option( "NO" ).cursor( 1 ).allow_cancel( true )
                               .default_color( c_light_red ).query().action == "YES";
        fp3d_input::world_gestures().cancel();
        if( !confirmed || you.pos_abs() != origin ||
            get_creature_tracker().creature_at( p ) != critter ||
            ( person && person->getID() != identity ) || critter->is_dead_state() ||
            !you.sees( here, *critter ) ) {
            return false;
        }
    }
    if( !g->check_safe_mode_allowed() || !you.move_effects( true ) ||
        !you.try_break_relax_gas( _( "Your willpower asserts itself, and so do you!" ),
                                 _( "You're too pacified to strike anything…" ) ) ) {
        return false;
    }
    const int moves = you.get_moves();
    // Native combat directly: a stunned movement command could redirect to
    // another tile, displace a pet or walk after the selected creature dies.
    you.melee_attack( *critter, true );
    if( you.get_moves() >= moves ) {
        return false;
    }
    if( person ) {
        person->on_attacked( you );
    } else if( monster *mon = critter->as_monster() ) {
        if( critter->is_hallucination() ) {
            critter->die( &here, &you );
        }
        g->draw_hit_mon( p, *mon, mon->is_dead_state() );
    }
    fp3d_compositor::melee_swing();
    return true;
}

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
    if( target.type == fp3d_targeting::kind::furniture ) {
        return here.furn( p ).id().str() == target.id;
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
    const bool up = object && here.has_flag( ter_furn_flag::TFLAG_GOES_UP, p );
    const bool down = object && here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, p );
    if( up || down ) {
        const bool standing = p == you.pos_bub( here );
        std::array<fp3d_quick_menu::entry, 8> choices{};
        for( auto &choice : choices ) {
            choice.visible = false;
        }
        choices[0] = { "上楼", standing ? "沿楼梯上楼。" : "请先走到楼梯上。",
                       ACTION_MOVE_UP, "FP3D_STAIRS_UP", standing && up, true };
        choices[1] = { "下楼", standing ? "沿楼梯下楼。" : "请先走到楼梯上。",
                       ACTION_MOVE_DOWN, "FP3D_STAIRS_DOWN", standing && down, true };
        choices[2] = { "走到楼梯上", "走到这格楼梯，再按 E 选择上下楼。",
                       standing ? ACTION_NULL : get_movement_action_from_delta( p - you.pos_bub( here ), iso_rotate::yes ),
                       nullptr, !standing && here.passable( p ), !standing };
        choices[7] = { "取消", "返回3D视角，不执行动作。", ACTION_NULL, nullptr, true, true };
        return choices;
    }
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
            { "拾取物品", "单件自动收纳，多件打开背包与目标容器两栏。", ACTION_PICKUP, nullptr, items, items },
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

bool quick_pickup( avatar &you, map &here, const tripoint_bub_ms &p, bool targeted )
{
    if( p.z() != you.posz() || !here.inbounds( p ) || square_dist( p, you.pos_bub( here ) ) > 1 ||
        !you.sees( here, p ) || !here.accessible_items( p ) || !here.could_see_items( p, you ) ) {
        return false;
    }
    auto objects = here.i_at( p );
    if( objects.empty() ) {
        return false;
    }
    if( targeted && objects.size() > 1 ) {
        auto action = fp3d_modern_ui::show( you, here, false, -1, here.get_abs( p ) );
        return action && fp3d_inventory::execute( *action, you, here );
    }
    // Native pick_up schedules the standard activity, including capacity,
    // theft, pocket selection, time and interruption rules. No item copying.
    const item *top = &here.maptile_at( p ).get_uppermost_item();
    for( item &object : objects ) {
        if( &object == top ) {
            you.pick_up( { { item_location( map_cursor( p ), &object ), targeted ? object.count() : 1 } } );
            return true;
        }
    }
    return false;
}

std::vector<fp3d_quick_menu::entry> context_2d_entries( const tripoint_bub_ms &p,
        const avatar &you, const map &here )
{
    if( you.is_dead_state() || you.is_blind() || !here.inbounds( p ) || p.z() != you.posz() || !you.sees( here, p ) ) {
        return {};
    }
    const bool near = square_dist( you.pos_bub( here ), p ) <= 1;
    const bool self = p == you.pos_bub( here );
    const auto *critter = get_creature_tracker().creature_at( p );
    const bool creature = critter && !critter->is_avatar() && !critter->is_dead_state() && you.sees( here, *critter );
    const auto *person = creature ? critter->as_npc() : nullptr;
    const bool opens = here.ter( p ).obj().open || here.furn( p ).obj().open;
    const bool closes = here.ter( p ).obj().close || here.furn( p ).obj().close;
    const bool items = here.could_see_items( p, you ) && here.maptile_at( p ).get_item_count() > 0;
    std::vector<fp3d_quick_menu::entry> result{
        { "前往此处", "沿原版安全路线前往此格，危险或障碍仍会中断移动。", ACTION_NULL,
          "MODERN_2D_TRAVEL", !self && !creature && ( here.passable( p ) || opens ) },
    };
    if( opens ) { result.push_back( { "打开", "打开这个门窗或容器。", ACTION_OPEN, nullptr, near } ); }
    if( closes ) { result.push_back( { "关闭", "关闭这个门窗或容器。", ACTION_CLOSE, nullptr, near } ); }
    if( items ) { result.push_back( { "拾取物品", "查看并拾取这格物品。", ACTION_PICKUP, nullptr,
                                     near && here.accessible_items( p ) } ); }
    if( person ) {
        result.push_back( { "交谈", "与点中的人物交谈。", ACTION_CHAT, nullptr, near } );
        if( npc_orders::can_issue( you, *person, npc_orders::kind::guard ) ) {
            result.push_back( { "原地守卫", "让这个队友守卫当前位置。", ACTION_NULL, "FP3D_GUARD", near } );
        }
        if( npc_orders::can_issue( you, *person, npc_orders::kind::follow ) ) {
            result.push_back( { "继续跟随", "让这个队友继续跟随。", ACTION_NULL, "FP3D_FOLLOW", near } );
        }
    }
    if( creature ) {
        result.push_back( { "近战攻击", "攻击点中的相邻目标；非敌对目标仍需确认。", ACTION_NULL, "FP3D_ATTACK", near } );
    } else {
        result.push_back( { "查看 / 搜索", "检查这个地块、家具或车辆。", ACTION_EXAMINE_AND_PICKUP, nullptr, near } );
        if( here.is_bashable( p ) ) {
            result.push_back( { "砸毁", "按原版规则砸击这个物体。", ACTION_SMASH, nullptr, near && !self } );
        }
    }
    if( here.has_flag( ter_furn_flag::TFLAG_GOES_UP, p ) ) {
        result.push_back( { "上楼", "先站到这格楼梯上，再上楼。", ACTION_MOVE_UP, nullptr, self } );
    }
    if( here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, p ) ) {
        result.push_back( { "下楼", "先站到这格楼梯上，再下楼。", ACTION_MOVE_DOWN, nullptr, self } );
    }
    return result;
}

std::optional<request> resolve_2d_choice( const fp3d_quick_menu::entry &choice,
        const tripoint_bub_ms &p, avatar &you, map &here )
{
    const auto current = context_2d_entries( p, you, here );
    const auto matching = std::find_if( current.begin(), current.end(), [&]( const auto &entry ) {
        return entry.enabled && entry.action == choice.action &&
               std::string( entry.command ? entry.command : "" ) == ( choice.command ? choice.command : "" );
    } );
    if( matching == current.end() ) { return std::nullopt; }
    const std::string command = choice.command ? choice.command : "";
    if( command == "MODERN_2D_TRAVEL" ) {
        if( !g || fp3d_input::vehicle_controls() ) { return std::nullopt; }
        const auto route = g->safe_route_to( you, p, 0, []( const std::string &message ) { add_msg( message ); } );
        if( !route || route->empty() ) { return std::nullopt; }
        you.set_destination( *route );
        const auto action = you.get_next_auto_move_direction();
        if( action == ACTION_NULL ) { you.clear_destination(); return std::nullopt; }
        return request{ action };
    }
    if( command == "FP3D_ATTACK" ) {
        return resolve( command, fp3d_targeting::target{ you.pos_abs(), here.get_abs( p ),
                        fp3d_targeting::kind::creature, "" }, you, here );
    }
    if( command == "FP3D_GUARD" || command == "FP3D_FOLLOW" ) {
        const auto *person = get_creature_tracker().creature_at<npc>( p );
        if( !person ) { return std::nullopt; }
        fp3d_inventory::operation operation;
        operation.kind = fp3d_inventory::operation_kind::follower_order;
        operation.talking_npc_id = person->getID().get_value();
        operation.destination_tile = here.get_abs( p );
        operation.follower_command = command == "FP3D_GUARD" ? npc_orders::kind::guard : npc_orders::kind::follow;
        fp3d_inventory::stage( std::move( operation ) );
        return request{ ACTION_ITEMACTION };
    }
    return request{ choice.action, p, choice.action == ACTION_PICKUP, choice.action == ACTION_PICKUP };
}

std::optional<request> resolve( const std::string &command,
                                const std::optional<fp3d_targeting::target> &selected_target, avatar &you, map &here )
{
    auto candidate = selected_target;
    if( command == "FP3D_INTERACT" || command == "FP3D_INTERACT_LONG" ) {
        const auto standing = you.pos_bub( here );
        if( here.has_flag( ter_furn_flag::TFLAG_GOES_UP, standing ) ||
            here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, standing ) ) {
            const auto choices = candidate ? context_entries( *candidate, you, here ) :
                                 std::array<fp3d_quick_menu::entry, 8>{};
            const bool actionable = std::any_of( choices.begin(), choices.end(), []( const auto &choice ) {
                return choice.visible && choice.enabled && ( choice.action != ACTION_NULL || choice.command );
            } );
            if( !actionable ) {
                candidate = fp3d_targeting::target{ you.pos_abs(), you.pos_abs(),
                            fp3d_targeting::kind::terrain, here.ter( standing ).id().str() };
            }
        }
    }
    if( command == "FP3D_ACTIONS" ) {
        const auto choice = fp3d_quick_menu::show();
        if( !choice || !fp3d_compositor::survival_menu_available() ) {
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
        auto operation = fp3d_inventory::select_slot( index, you );
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
    if( command == "FP3D_AIM" ) {
        const auto weapon = you.get_wielded_item();
        if( weapon && weapon->is_gun() && !weapon->gun_current_mode().melee() ) {
            return request{ ACTION_FIRE, std::nullopt, false };
        }
        return std::nullopt;
    }
    if( !candidate || !valid( *candidate, you, here ) ) {
        fp3d_input::world_gestures().cancel();
        add_msg( m_info, "准星目标当前不可见或无法触及，请靠近并保持同层。" );
        return std::nullopt;
    }
    const auto p = here.get_bub( candidate->position );
    if( command == "FP3D_STAIRS_UP" || command == "FP3D_STAIRS_DOWN" ) {
        const bool up = command == "FP3D_STAIRS_UP";
        if( p != you.pos_bub( here ) || fp3d_input::vehicle_controls() ||
            !here.has_flag( up ? ter_furn_flag::TFLAG_GOES_UP : ter_furn_flag::TFLAG_GOES_DOWN, p ) ) {
            return std::nullopt;
        }
        return request{ up ? ACTION_MOVE_UP : ACTION_MOVE_DOWN, std::nullopt, false };
    }
    if( command == "FP3D_ATTACK" ) {
        if( fp3d_input::vehicle_controls() ) {
            fp3d_input::world_gestures().cancel();
            add_msg( m_info, "驾驶时请先停止控制车辆再进行近战。" );
            return std::nullopt;
        }
        Creature *critter = get_creature_tracker().creature_at( p );
        if( critter && !critter->is_avatar() && !critter->is_dead_state() &&
            you.sees( here, *critter ) && p != you.pos_bub( here ) ) {
            return request{ get_movement_action_from_delta( p - you.pos_bub( here ), iso_rotate::yes ),
                            p, false, false, true };
        }
        if( candidate->type == fp3d_targeting::kind::creature ||
            ( critter && !critter->is_avatar() ) ) {
            fp3d_input::world_gestures().cancel();
            return std::nullopt;
        }
        return request{ ACTION_SMASH, p, false };
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
        const bool items = here.accessible_items( p ) && here.could_see_items( p, you ) &&
                           !here.i_at( p ).empty();
        const auto *npc = get_creature_tracker().creature_at<::npc>( p );
        const bool can_chat = npc && you.sees( here, *npc );
        const int npc_id = can_chat ? npc->getID().get_value() : -1;
        action_id action = can_chat ? ACTION_CHAT :
                           opens ? ACTION_OPEN : items ? ACTION_PICKUP : closes ? ACTION_CLOSE : ACTION_EXAMINE_AND_PICKUP;
        const bool stairs = !can_chat && ( here.has_flag( ter_furn_flag::TFLAG_GOES_UP, p ) ||
                                           here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, p ) );
        if( command == "FP3D_INTERACT_LONG" || stairs ) {
            const auto choice = fp3d_quick_menu::show( context_entries( *candidate, you, here ),
                                stairs ? "楼梯" : "目标操作" );
            if( !choice || !choice->enabled || !valid( *candidate, you, here ) ||
                !fp3d_compositor::turn_based() ) {
                return std::nullopt;
            }
            if( choice->command ) {
                if( stairs && ( std::string( choice->command ) == "FP3D_STAIRS_UP" ||
                                std::string( choice->command ) == "FP3D_STAIRS_DOWN" ) ) {
                    return resolve( choice->command, candidate, you, here );
                }
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
            const auto current_choices = context_entries( *candidate, you, here );
            if( stairs && std::none_of( current_choices.begin(), current_choices.end(), [&]( const auto &entry ) {
                return entry.visible && entry.enabled && entry.action == action && action != ACTION_NULL;
            } ) ) {
                return std::nullopt;
            }
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
        return request{ action, p, action == ACTION_PICKUP, action == ACTION_PICKUP };
    }
    return std::nullopt;
}
}
#endif
