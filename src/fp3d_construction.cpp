#include "fp3d_construction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <set>
#include "avatar.h"
#include "cata_scope_helpers.h"
#include "construction.h"
#include "construction_group.h"
#include "construction_category.h"
#include "output.h"
#include "requirements.h"
#include "scoped_rng.h"
#include "skill.h"
#include "fp3d_compositor.h"
#include "fp3d_input.h"
#include "fp3d_interaction.h"
#include "game.h"
#include "input_context.h"
#include "inventory.h"
#include "map.h"
#include "mapdata.h"
#include "ui_manager.h"
namespace fp3d_construction
{
std::vector<project> catalogue()
{
    std::vector<project> result;
    std::set<construction_group_str_id> seen;
    for( const auto &entry : get_constructions() ) {
        if( entry.on_display && entry.group.is_valid() && seen.insert( entry.group ).second ) {
            result.push_back( { entry.group, entry.group->name(), entry.category->name() } );
        }
    }
    std::sort( result.begin(), result.end(), []( const auto & a, const auto & b ) {
        return a.name < b.name;
    } );
    return result;
}
std::string details( const construction_group_str_id &group, avatar *crafter )
{
    scoped_rng_restore restore_rng;
    const inventory *available = crafter ? &crafter->crafting_inventory() : nullptr;
    std::string result;
    int stage = 0;
    for( const auto &entry : get_constructions() ) {
        if( entry.group != group || !entry.on_display ) {
            continue;
        }
        result += "阶段 " + std::to_string( ++stage ) + "\n";
        if( crafter ) {
            result += player_can_build( *crafter, *available, entry, true ) ?
                      "材料、工具与知识：已满足该阶段需求。\n" :
                      "材料、工具与知识：尚未满足该阶段需求。\n";
        }
        for( const auto &line : entry.get_folded_time_string( 60 ) ) {
            result += line + "\n";
        }
        for( const auto &skill : entry.required_skills ) {
            result += skill.first->name() + " " + std::to_string( skill.second ) + "\n";
        }
        result += entry.pre_note.translated() + "\n" + entry.requirements->list_all() + "\n";
    }
    if( crafter && !result.empty() ) {
        result += "位置与光照在选择施工位置时检查。\n";
    }
    return remove_color_tags( result );
}
bool preview_allowed( avatar &you, map &here, const construction &project,
                      const fp3d_targeting::target &target )
{
    scoped_rng_restore restore_rng;
    return fp3d_interaction::valid( target, you, here ) &&
           can_construct( project, here.get_bub( target.position ) ) &&
           player_can_build( you, you.crafting_inventory(), project, true );
}
std::optional<fp3d_snapshot::cell> terrain_preview( const construction &project,
        const tripoint_abs_ms &position )
{
    const ter_str_id terrain( project.post_terrain );
    if( project.post_is_furniture || terrain.is_null() || !terrain.is_valid() ) {
        return std::nullopt;
    }
    const auto &definition = terrain.obj();
    // These need multi-cell post-effects or a dedicated plant/furniture model.
    for( const auto flag : {
             ter_furn_flag::TFLAG_NO_FLOOR, ter_furn_flag::TFLAG_NO_FLOOR_WATER,
             ter_furn_flag::TFLAG_GOES_UP, ter_furn_flag::TFLAG_GOES_DOWN,
             ter_furn_flag::TFLAG_RAMP, ter_furn_flag::TFLAG_RAMP_UP, ter_furn_flag::TFLAG_RAMP_DOWN,
             ter_furn_flag::TFLAG_TREE, ter_furn_flag::TFLAG_YOUNG, ter_furn_flag::TFLAG_SHRUB,
             ter_furn_flag::TFLAG_FLOWER
         } ) {
        if( definition.has_flag( flag ) ) {
            return std::nullopt;
        }
    }
    fp3d_snapshot::cell result;
    result.position = position;
    result.terrain = terrain.str();
    result.kind = fp3d_snapshot::terrain_shape( result.terrain );
    if( result.kind == fp3d_snapshot::terrain_kind::obstacle ) {
        return std::nullopt;
    }
    if( result.kind == fp3d_snapshot::terrain_kind::window ) {
        result.pane = !definition.has_flag( ter_furn_flag::TFLAG_TRANSPARENT ) ?
                      fp3d_snapshot::window_pane::covered : definition.movecost > 0 ?
                      fp3d_snapshot::window_pane::opening : fp3d_snapshot::window_pane::glazed;
    }
    return result;
}

std::optional<tripoint_bub_ms> choose( const std::map<tripoint_bub_ms, const construction *>
                                       &valid )
{
    avatar &you = get_avatar();
    map &here = get_map();
    const auto origin = you.pos_abs();
    fp3d_input::world_gestures().cancel();
    fp3d_compositor::set_input_context( "FP3D_BUILD" );
    on_out_of_scope reset( []() {
        fp3d_compositor::set_blueprint( std::nullopt );
        fp3d_input::world_gestures().cancel();
        fp3d_compositor::set_input_context( "DEFAULTMODE" );
        g->invalidate_main_ui_adaptor();
    } );
    input_context context( "FP3D_BUILD", keyboard_mode::keycode );
    context.register_action( "QUIT" );
    context.register_action( "FP3D_INTERACT" );
    context.register_action( "FP3D_ATTACK" );
    context.register_action( "ANY_INPUT" );
    while( fp3d_compositor::turn_based() && !you.is_dead_state() && you.pos_abs() == origin ) {
        const auto candidate = fp3d_compositor::interaction_target();
        std::optional<tripoint_bub_ms> position;
        bool allowed = false;
        fp3d_scene::blueprint preview;
        preview.position = origin;
        preview.name = "建造预览：瞄准相邻目标格";
        if( candidate ) {
            position = here.get_bub( candidate->position );
            const auto match = valid.find( *position );
            allowed = match != valid.end() && preview_allowed( you, here, *match->second, *candidate );
            preview.position = candidate->position;
            preview.valid = allowed;
            if( match != valid.end() ) {
                const construction &con = *match->second;
                preview.name = "建造预览：" + con.group->name();
                preview.terrain = terrain_preview( con, candidate->position );
                preview.height = con.post_is_furniture ? 1.0f :
                                 ter_str_id( con.post_terrain ).is_valid() &&
                                 ter_str_id( con.post_terrain )->movecost == 0 ? fp3d_camera::floor_height : .12f;
            }
        }
        fp3d_compositor::set_blueprint( std::move( preview ) );
        g->invalidate_main_ui_adaptor();
        ui_manager::redraw();
        const auto &action = context.handle_input( 16 );
        if( action == "QUIT" ) {
            return std::nullopt;
        }
        if( allowed && ( action == "FP3D_INTERACT" || action == "FP3D_ATTACK" ) ) {
            const auto current = fp3d_compositor::interaction_target();
            if( current && current->position == candidate->position &&
                preview_allowed( you, here, *valid.at( *position ), *current ) ) {
                return position;
            }
        }
    }
    return std::nullopt;
}
}
#endif
