#include "fp3d_inventory.h"
#include "fp3d_crafting.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <functional>
#include <array>
#include "output.h"
#include <utility>
#include "activity_actor_definitions.h"
#include "avatar.h"
#include "fp3d_journal.h"
#include "avatar_action.h"
#include "vehicle.h"
#include "veh_type.h"
#include "vehicle_selector.h"
#include "vpart_position.h"
#include "item.h"
#include "item_pocket.h"
#include "json.h"
#include "map.h"
#include "mapdata.h"
#include "messages.h"
#include "units.h"
#include "itype.h"
#include "iuse.h"
#include "scoped_rng.h"
namespace fp3d_inventory
{
namespace
{
std::optional<operation> queued;
std::array<item_location, 8> slots;
struct slot_identity {
    itype_id type;
    std::string variant;
    char invlet = 0;
    bool valid = false;
};
std::array<slot_identity, 8> slot_identities;
int chosen_slot = 0;

void remember_slot( int index, const item_location &location )
{
    if( index < 0 || index >= 8 ) {
        return;
    }
    slots[index] = location;
    auto &identity = slot_identities[index];
    identity = {};
    if( !location ) {
        return;
    }
    identity.type = location->typeId();
    identity.variant = location->has_itype_variant() ? location->itype_variant().id : std::string();
    identity.invlet = location->invlet;
    identity.valid = true;
}

void refresh_slots( avatar &you )
{
    for( int index = 0; index < 8; ++index ) {
        auto &slot = slots[index];
        const auto &identity = slot_identities[index];
        if( slot && you.has_item( *slot ) && slot->typeId() == identity.type &&
            ( slot->has_itype_variant() ? slot->itype_variant().id : std::string() ) == identity.variant &&
            ( identity.invlet == 0 || slot->invlet == identity.invlet ) ) {
            continue;
        }
        slot = item_location();
        if( !identity.valid ) {
            continue;
        }
        item_location fallback;
        int matches = 0;
        for( const item_location &candidate : you.all_items_loc() ) {
            if( !candidate || candidate->typeId() != identity.type ||
                ( candidate->has_itype_variant() ? candidate->itype_variant().id : std::string() ) != identity.variant ) {
                continue;
            }
            if( identity.invlet == 0 || candidate->invlet == identity.invlet ) {
                slot = candidate;
                break;
            }
            fallback = candidate;
            ++matches;
        }
        if( !slot && matches == 1 ) {
            slot = std::move( fallback );
        }
        if( !slot ) {
            slot_identities[index] = {};
        }
    }
}
}

bool accessible( const item_location &location, const avatar &you, const map &here )
{
    if( !location || you.is_dead_state() ) {
        return false;
    }
    auto ancestor = location;
    while( ancestor.has_parent() ) {
        if( const auto *pocket = ancestor.parent_pocket(); pocket && pocket->sealed() ) {
            return false;
        }
        ancestor = ancestor.parent_item();
    }
    if( location.where_recursive() == item_location::type::character ) {
        return location.held_by( you );
    }
    if( const auto *cursor = ancestor.veh_cursor(); cursor &&
        cursor->veh.part( cursor->part ).is_cleaner_on() ) {
        return false;
    }
    const auto p = location.pos_bub( here );
    return !you.is_blind() && p.z() == you.posz() && here.inbounds( p ) &&
           square_dist( p, you.pos_bub( here ) ) <= 1 && you.sees( here, p ) &&
           here.accessible_items( p ) && here.could_see_items( p, you );
}

std::vector<use_option> use_options( const item_location &location, const avatar &you, const map &here )
{
    scoped_rng_restore restore_rng;
    std::vector<use_option> result;
    if( !accessible( location, you, here ) ) { return result; }
    const item &object = *location;
    const bool default_action = !object.is_tool() && !object.type->can_use( "PETFOOD" ) &&
                                ( object.is_medication() || object.is_book() || object.has_flag( flag_id( "SPLINT" ) ) ||
                                  ( object.is_food() && !object.type->has_use() ) );
    if( default_action ) {
        result.push_back( { {}, object.is_book() ? "阅读" : object.is_medication() ? "使用药品" : "食用 / 饮用", {}, true, false } );
    } else {
        for( const auto &method : object.type->use_methods ) {
            const auto status = method.second.can_call( you, object, location.pos_bub( here ) );
            const bool powered = object.ammo_sufficient( &you, method.first );
            result.push_back( { method.first, method.second.get_name(), status.success() ?
                                powered ? method.second.get_description() : "所需能量或弹药不足" : status.str(),
                                status.success() && powered && !object.is_broken(), false } );
        }
        if( object.has_relic_activation() ) {
            const bool invokable = !object.is_tool() || object.type->has_use();
            result.push_back( { {}, "激活遗物", invokable ? "" : "此工具没有可用的主动用途", invokable, true } );
        }
    }
    return result;
}

model capture( avatar &you, map &here, std::optional<tripoint_abs_ms> nearby_target )
{
    model result;
    std::function<void( item_location, size_t, bool )> append;
    append = [&]( item_location loc, size_t group_index, bool equipped ) {
        if( !loc ) {
            return;
        }
        item &object = *loc;
        const bool near = result.groups[group_index].nearby;
        const int category = object.is_gun() || object.is_melee() ? 1 :
                             object.is_ammo() ? 2 : object.is_medication() ? 4 : object.is_food() ? 3 :
                             object.is_container() ? 6 : object.is_armor() ? 5 : object.is_tool() ? 7 : 8;
        result.groups[group_index].entries.push_back( result.entries.size() );
        result.entries.push_back( { loc, remove_color_tags( object.tname() ), result.groups[group_index].name, near, equipped, category,
                                    units::to_milliliter( object.volume() ) / 1000.0f,
                                    units::to_gram( object.weight() ) / 1000.0f } );
        int number = 0;
        int pocket_index = -1;
        for( item_pocket *pocket : object.get_all_contained_pockets() ) {
            ++pocket_index;
            if( !pocket->is_type( pocket_type::CONTAINER ) || ( near && ( pocket->sealed() ||
                    !pocket->transparent() ) ) ) {
                continue;
            }
            const size_t child_group = result.groups.size();
            result.groups.push_back( { remove_color_tags( object.tname() ) + " / 口袋 " + std::to_string( ++number ), loc,
                                       units::to_milliliter( pocket->volume_capacity() ) / 1000.0f, 0, near, {} } );
            auto &record = result.groups[child_group];
            const auto pocket_name = pocket->get_name().translated();
            if( !pocket_name.empty() ) {
                record.name += " / " + pocket_name;
            }
            record.used_l = units::to_milliliter( pocket->contains_volume() ) / 1000.0f;
            record.capacity_kg = units::to_gram( pocket->weight_capacity() ) / 1000.0f;
            record.used_kg = units::to_gram( pocket->contains_weight() ) / 1000.0f;
            record.max_length_cm = units::to_millimeter( pocket->get_pocket_data()->max_item_length ) / 10.0f;
            record.sealed = pocket->sealed();
            record.pocket_index = pocket_index;
            for( item *child : pocket->all_items_top() ) {
                // Location points to original storage; rendering never copies an item.
                append( item_location( loc, child ), child_group, false );
            }
        }
    };
    result.groups.push_back( { "手持与穿戴", {}, 0, 0, false, {} } );
    if( auto weapon = you.get_wielded_item() ) {
        append( weapon, 0, true );
    }
    for( auto loc : you.top_items_loc() ) {
        append( loc, 0, true );
    }
    if( !you.is_blind() ) {
        for( const auto &p : here.points_in_radius( you.pos_bub( here ), 1 ) ) {
            if( ( nearby_target && here.get_abs( p ) != *nearby_target ) ||
                p.z() != you.posz() || !you.sees( here, p ) || !here.accessible_items( p ) ||
                !here.could_see_items( p, you ) ) {
                continue;
            }
            auto objects = here.i_at( p );
            const auto offset = p - you.pos_bub( here );
            const size_t index = result.groups.size();
            const std::string source = here.has_furn( p ) ? here.furn( p ).obj().name() : "地面";
            result.groups.push_back( { source + " (" + std::to_string( offset.x() ) + "," + std::to_string( offset.y() ) + ") / z=" + std::to_string( p.z() ), {}, 0, 0, true, {}, here.get_abs( p ), false } );
            result.groups[index].capacity_l = units::to_milliliter( here.max_volume( p ) ) / 1000.0f;
            result.groups[index].used_l = units::to_milliliter( objects.stored_volume() ) / 1000.0f;
            for( item &object : objects ) {
                append( item_location( map_cursor( p ), &object ), index, false );
            }
            if( const auto cargo = here.veh_at( p ).cargo(); cargo && !cargo->part().is_cleaner_on() ) {
                auto storage = cargo->items();
                const size_t cargo_group = result.groups.size();
                result.groups.push_back( { "车辆 / " + cargo->vehicle().name + " / " + cargo->info().name(), {},
                                           units::to_milliliter( storage.max_volume() ) / 1000.0f,
                                           units::to_milliliter( storage.stored_volume() ) / 1000.0f, true, {}, here.get_abs( p ), true } );
                for( item &object : storage ) {
                    append( item_location( vehicle_cursor( cargo->vehicle(), cargo->part_index() ), &object ),
                            cargo_group, false );
                }
            }
        }
    }
    return result;
}

void assign_slot( int index, item_location item )
{
    if( index >= 0 && index < 8 ) {
        remember_slot( index, item );
    }
}
int active_slot()
{
    return chosen_slot;
}
void clear_slots()
{
    slots = {};
    slot_identities = {};
    chosen_slot = 0;
}
void save_slots( const avatar &you, JsonOut &json )
{
    json.member( "fp3d_hotbar" );
    json.start_object();
    json.member( "active", chosen_slot );
    json.member( "slots" );
    json.start_array();
    for( size_t i = 0; i < slots.size(); ++i ) {
        json.start_object();
        const auto &slot = slots[i];
        const auto &identity = slot_identities[i];
        if( identity.valid ) {
            json.member( "type", identity.type );
            json.member( "variant", identity.variant );
            json.member( "invlet", static_cast<int>( identity.invlet ) );
            if( slot && you.has_item( *slot ) ) {
                // Native item locations preserve the exact item, including nested pockets.
                json.member( "item", slot );
            }
        }
        json.end_object();
    }
    json.end_array();
    json.end_object();
}
void load_slots( avatar &you, const JsonObject &data )
{
    clear_slots();
    if( !data.has_object( "fp3d_hotbar" ) ) {
        return;
    }
    const JsonObject hotbar = data.get_object( "fp3d_hotbar" );
    chosen_slot = std::clamp( hotbar.get_int( "active", 0 ), 0, 7 );
    int index = 0;
    for( const JsonObject saved : hotbar.get_array( "slots" ) ) {
        if( index >= 8 ) {
            break;
        }
        auto &identity = slot_identities[index];
        if( saved.read( "type", identity.type ) && identity.type.is_valid() ) {
            identity.variant = saved.get_string( "variant", "" );
            identity.invlet = static_cast<char>( saved.get_int( "invlet", 0 ) );
            identity.valid = true;
            saved.read( "item", slots[index] );
        }
        ++index;
    }
    // Inventory and worn containers have already been loaded by Character::load.
    refresh_slots( you );
}
std::vector<std::string> slot_names( avatar &you )
{
    refresh_slots( you );
    std::vector<std::string> names;
    for( const auto &slot : slots ) {
        names.push_back( slot ? remove_color_tags( slot->tname() ) : "空" );
    }
    return names;
}
std::vector<slot_icon> slot_icons( avatar &you )
{
    refresh_slots( you );
    std::vector<slot_icon> icons;
    icons.reserve( slots.size() );
    for( auto &slot : slots ) {
        if( slot ) {
            icons.push_back( { slot->typeId().str(), slot->has_itype_variant() ? slot->itype_variant().id : std::string() } );
        } else {
            icons.emplace_back();
        }
    }
    return icons;
}

static void clear_slots_for( const drop_locations &items )
{
    for( size_t index = 0; index < slots.size(); ++index ) {
        auto &slot = slots[index];
        if( slot && std::any_of( items.begin(), items.end(), [&]( const drop_location &part ) {
        return slot == part.first;
        } ) ) {
            slot = item_location();
            slot_identities[index] = {};
        }
    }
}
std::optional<operation> select_slot( int index, avatar &you )
{
    refresh_slots( you );
    chosen_slot = ( index + 8 ) % 8;
    if( !slots[chosen_slot] ) {
        return std::nullopt;
    }
    operation action;
    action.kind = operation_kind::wield;
    action.items.emplace_back( slots[chosen_slot], 1 );
    action.hotbar_slot = chosen_slot;
    return action;
}

bool can_insert( const operation &action, avatar &you, const map &here, std::string &error )
{
    if( action.kind != operation_kind::insert || action.items.empty() || !action.destination ) {
        return true;
    }
    if( !accessible( action.destination, you, here ) ) {
        error = "目标容器无法触及。";
        return false;
    }
    const auto loc = action.items.front().first;
    if( action.destination == loc ) {
        error = "不能把物品放入自身。";
        return false;
    }
    for( const auto &part : action.items ) {
        if( !accessible( part.first, you, here ) || part.second <= 0 || part.second > part.first->count() ) {
            error = "物品已变化或无法触及，请重新打开面板。";
            return false;
        }
        auto ancestor = action.destination;
        while( ancestor ) {
            if( ancestor == part.first ) {
                error = "不能把容器放入自身或其内部。";
                return false;
            }
            if( !ancestor.has_parent() ) {
                break;
            }
            ancestor = ancestor.parent_item();
        }
    }
    if( action.destination_pocket >= 0 ) {
        const auto pockets = action.destination->get_all_contained_pockets();
        if( static_cast<size_t>( action.destination_pocket ) >= pockets.size() ) {
            error = "指定口袋不存在。";
            return false;
        }
        const item_pocket *pocket = pockets[action.destination_pocket];
        if( !pocket->is_type( pocket_type::CONTAINER ) || pocket->sealed() ) {
            error = "指定口袋已封闭或不可用。";
            return false;
        }
        for( const auto &part : action.items ) {
            if( part.first.has_parent() && part.first.parent_item() == action.destination &&
                pocket->has_item( *part.first ) ) {
                error = "物品已经在这个口袋中。";
                return false;
            }
            item candidate( *part.first );
            if( candidate.count_by_charges() ) {
                candidate.charges = 1;
            }
            if( !pocket->can_contain( candidate ).success() ) {
                error = "指定口袋无法容纳这件物品。";
                return false;
            }
        }
    } else {
        for( const auto &part : action.items ) {
            if( !action.destination->can_contain_directly( *part.first ).success() ) {
                error = "目标容器无法容纳这件物品。";
                return false;
            }
        }
    }
    return true;
}

bool execute( const operation &action, avatar &you, map &here, std::string *error )
{
    const auto fail = [&]( const std::string &message ) {
        if( error ) {
            *error = message;
        } else if( !message.empty() ) {
            add_msg( m_info, message );
        }
        return false;
    };
    if( action.kind == operation_kind::completed ) {
        return true;
    }
    if( action.kind == operation_kind::craft ) {
        std::string crafting_error;
        std::optional<tripoint_bub_ms> position;
        if( action.crafting_position ) {
            position = here.get_bub( *action.crafting_position );
            if( position->z() != you.pos_bub( here ).z() ||
                rl_dist( *position, you.pos_bub( here ) ) > 1 ) {
                return fail( "制作位置已无法触及，请重新打开工作台。" );
            }
        }
        if( !fp3d_crafting::start( you, action.crafting_recipe, action.crafting_batch, crafting_error,
                                 position ) ) {
            return fail( crafting_error );
        }
        return true;
    }
    if( action.kind == operation_kind::follower_order ) {
        if( !action.talking_npc_id || !action.destination_tile || !action.follower_command ||
            !fp3d_journal::issue_order( you, here, *action.talking_npc_id, *action.destination_tile,
                                        *action.follower_command ) ) {
            return fail( "此人物已变化、不可见或无法听从该命令，请刷新队友页。" );
        }
        return true;
    }
    std::string insert_error;
    if( action.kind == operation_kind::insert && !can_insert( action, you, here, insert_error ) ) {
        return fail( insert_error );
    }
    if( action.items.empty() ) {
        return fail( "没有选择物品。" );
    }
    for( const auto &part : action.items ) {
        if( !accessible( part.first, you, here ) || part.second <= 0 ||
            part.second > part.first->count() ) {
            return fail( "物品已变化或无法触及，请重新打开面板。" );
        }
    }
    // Reject overlapping selection before scheduling an activity: moving a bag
    // and its child in the same batch would otherwise invalidate a later target.
    for( auto it = action.items.begin(); it != action.items.end(); ++it ) {
        for( auto other = std::next( it ); other != action.items.end(); ++other ) {
            auto ancestor = it->first;
            while( ancestor ) {
                if( ancestor == other->first ) {
                    return fail( "不能同时移动相互包含的物品。" );
                }
                if( !ancestor.has_parent() ) {
                    break;
                }
                ancestor = ancestor.parent_item();
            }
            ancestor = other->first;
            while( ancestor.has_parent() ) {
                ancestor = ancestor.parent_item();
                if( ancestor == it->first ) {
                    return fail( "不能同时移动相互包含的物品。" );
                }
            }
        }
    }
    auto loc = action.items.front().first;
    switch( action.kind ) {
        case operation_kind::pickup:
            you.pick_up( action.items );
            return true;
        case operation_kind::drop:
            for( const auto &part : action.items ) {
                if( !part.first.held_by( you ) ) {
                    return fail( "物品已不再由角色持有。" );
                }
            }
            you.drop( action.items, you.pos_bub( here ) );
            clear_slots_for( action.items );
            return true;
        case operation_kind::insert:
            for( const auto &part : action.items ) {
                auto ancestor = action.destination;
                while( ancestor ) {
                    if( ancestor == part.first ) {
                        return fail( "不能把容器放入自身或其内部。" );
                    }
                    if( !ancestor.has_parent() ) {
                        break;
                    }
                    ancestor = ancestor.parent_item();
                }
            }
            for( const auto &part : action.items ) {
                auto source = part.first;
                if( !avatar_action::check_stealing( you, *source ) ) {
                    return fail( "没有权限移动该物品。" );
                }
            }
            you.assign_activity( insert_item_activity_actor( action.destination, action.items ) );
            if( action.destination_pocket >= 0 ) {
                you.activity.values = { action.destination_pocket };
                you.activity.str_values = { "fp3d_target_pocket", action.destination->typeId().str() };
            }
            return true;
        case operation_kind::place: {
            if( !action.destination_tile ) {
                return fail( "目的地无法触及，请重新选择。" );
            }
            const auto target = here.get_bub( *action.destination_tile );
            if( target.z() != you.posz() || !here.inbounds( target ) ||
                square_dist( target, you.pos_bub( here ) ) > 1 || !you.sees( here, target ) ||
                !here.accessible_items( target ) || !here.could_see_items( target, you ) ) {
                return fail( "目的地无法触及，请重新选择。" );
            }
            const auto cargo = here.veh_at( target ).cargo();
            if( ( action.to_vehicle && !cargo ) ||
                ( cargo && cargo->part().is_cleaner_on() ) || !here.can_put_items( target ) ) {
                return fail( "此处不能放置物品。" );
            }
            const bool own = loc.held_by( you );
            for( const auto &part : action.items ) {
                if( part.first.held_by( you ) != own ) {
                    return fail( "请分别移动随身物品和附近物品。" );
                }
            }
            if( own ) {
                // Native drop handles worn layers, capacity, spill prompts and cost.
                you.drop( action.items, target );
                clear_slots_for( action.items );
            } else {
                std::vector<item_location> items;
                std::vector<int> quantities;
                for( const auto &part : action.items ) {
                    if( !part.first->made_of_from_type( phase_id::SOLID ) || !part.first->is_owned_by( you, true ) ) {
                        return fail( "此搬运方式只能移动有权使用的固体物品。" );
                    }
                    if( !part.first.has_parent() && part.first.pos_abs() == *action.destination_tile &&
                        ( part.first.where_recursive() == item_location::type::vehicle ) == action.to_vehicle ) {
                        continue;
                    }
                    items.push_back( part.first );
                    quantities.push_back( part.second );
                }
                if( items.empty() ) {
                    return fail( "没有可移动的物品。" );
                }
                you.assign_activity( move_items_activity_actor( std::move( items ), std::move( quantities ),
                                     action.to_vehicle, target - you.pos_bub( here ) ) );
            }
            return true;
        }
        case operation_kind::wield: {
            if( !avatar_action::check_stealing( you, *loc ) ) {
                return fail( "没有权限持握该物品。" );
            }
            if( loc == you.get_wielded_item() ) {
                return true;
            }
            const bool success = you.wield( loc );
            if( success && action.hotbar_slot >= 0 && action.hotbar_slot < 8 ) {
                remember_slot( action.hotbar_slot, you.get_wielded_item() );
            }
            if( success ) {
                refresh_slots( you );
            }
            return success;
        }
        case operation_kind::wear:
            if( !you.wear( loc, true ) ) {
                return fail( "无法穿戴该物品。" );
            }
            return true;
        case operation_kind::takeoff:
            return loc.held_by( you ) && you.takeoff( loc );
        case operation_kind::use: {
            const auto options = use_options( loc, you, here );
            const auto choice = std::find_if( options.begin(), options.end(), [&]( const auto &option ) {
                return option.method == action.use_method && option.relic == action.activate_relic && option.enabled;
            } );
            if( choice == options.end() ) { return fail( "该用途目前无法使用。" ); }
            avatar_action::use_item( you, loc, action.use_method, action.activate_relic );
            return true;
        }
        case operation_kind::native:
        case operation_kind::craft:
        case operation_kind::follower_order:
            return false;
    }
    return false;
}
void stage( operation action )
{
    queued = std::move( action );
}
bool pending()
{
    return queued.has_value();
}
recipe_id take_craft_recipe()
{
    if( !queued || queued->kind != operation_kind::native || queued->native_action != ACTION_CRAFT ) {
        return {};
    }
    const auto id = queued->crafting_recipe;
    queued.reset();
    return id;
}
construction_group_str_id take_construction_group()
{
    if( !queued || queued->kind != operation_kind::native ||
        queued->native_action != ACTION_CONSTRUCT ||
        !queued->construction_group ) {
        return construction_group_str_id::NULL_ID();
    }
    const auto group = *queued->construction_group;
    queued.reset();
    return group;
}
std::optional<int> take_talking_npc_id()
{
    if( !queued || queued->kind != operation_kind::native || queued->native_action != ACTION_CHAT ) {
        return std::nullopt;
    }
    const auto id = queued->talking_npc_id;
    queued.reset();
    return id;
}
void clear_pending()
{
    queued.reset();
}
bool execute_pending( avatar &you, map &here, std::string *error )
{
    auto action = std::move( queued );
    queued.reset();
    if( action ) {
        return execute( *action, you, here, error );
    }
    return false;
}
}
#endif
