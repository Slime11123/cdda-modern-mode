#include "fp3d_crafting.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <utility>
#include "avatar.h"
#include "calendar.h"
#include "crafting.h"
#include "crafting_gui.h"
#include "uistate.h"
#include "scoped_rng.h"
#include "rng.h"
#include "itype.h"
#include "inventory.h"
#include "output.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "skill.h"
#include "translations.h"
namespace fp3d_crafting
{
std::vector<entry> capture( const avatar &you )
{
    // Native crafting inventory builds temporary item variants. This is a
    // main-thread read-only preview; actual crafting runs outside this scope.
    scoped_rng_restore restore_rng;
    std::vector<entry> result;
    for( const recipe *rec : you.get_group_available_recipes() ) {
        if( rec->obsolete || rec->is_nested() ) {
            continue;
        }
        // result_name() constructs a random item variant. Read type metadata
        // instead so opening the catalogue does not consume gameplay RNG.
        const std::string name = !rec->name_.empty() ? rec->name_.translated() :
                                 rec->result()->nname( 1 );
        result.push_back( { rec->ident(), rec->result(), rec->variant(), remove_color_tags( name ),
                            rec->skill_used.is_null() ? "无主技能" : rec->skill_used->name(),
                            rec->get_difficulty( you ), rec->category,
                            rec->category.str().empty() || rec->category.str() == "CC_*" ? "未分类" :
                            _( rec->category.str().substr( 3 ) ) } );
    }
    std::sort( result.begin(), result.end(), []( const auto & a, const auto & b ) {
        return a.name < b.name || ( a.name == b.name && a.id.str() < b.id.str() );
    } );
    return result;
}
bool is_favorite( const recipe_id &id )
{
    return uistate.favorite_recipes.count( id ) != 0;
}
bool set_favorite( const avatar &you, const recipe_id &id, bool favorite )
{
    if( !id.is_valid() || id->obsolete || id->is_nested() ) {
        return false;
    }
    scoped_rng_restore restore_rng;
    if( !you.get_group_available_recipes().contains( &*id ) ) {
        return false;
    }
    if( favorite ) {
        uistate.favorite_recipes.insert( id );
        uistate.read_recipes.insert( id );
    } else {
        uistate.favorite_recipes.erase( id );
    }
    return true;
}
std::string details( const recipe_id &id )
{
    if( !id.is_valid() ) {
        return {};
    }
    // Native requirement formatting constructs temporary component items too.
    scoped_rng_restore restore_rng;
    return remove_color_tags( id->description.translated() + "\n" +
                              id->simple_requirements().list_all() );
}
std::string details( const recipe_id &id, const avatar &crafter, int batch_size )
{
    if( !id.is_valid() || batch_size < 1 || batch_size > 50 ) {
        return {};
    }
    scoped_rng_restore restore_rng;
    std::string result = ( batch_size == 1 ? "单批" : std::to_string( batch_size ) + "批" ) +
                         std::string( "预计耗时：" ) + to_string( time_duration::from_moves(
                                 crafter.expected_time_to_craft( *id, batch_size ) ) ) + "\n";
    for( const auto &skill : id->required_skills ) {
        result += "所需技能：" + skill.first->name() + " " + std::to_string( skill.second ) + "\n";
    }
    for( const auto &entry : {
             std::pair{ "必需熟练度：", id->required_proficiencies_string( &crafter ) },
             std::pair{ "可用熟练度：", id->used_proficiencies_string( &crafter ) },
             std::pair{ "缺少熟练度：", id->missing_proficiencies_string( &crafter ) }
         } ) {
        if( !entry.second.empty() ) {
            result += entry.first + entry.second + "\n";
        }
    }
    if( crafter.lighting_craft_speed_multiplier( *id ) <= 0 ) {
        result += "当前光线不足，无法制作此配方。\n";
    }
    if( crafter.morale_crafting_speed_multiplier( *id ) <= 0 ) {
        result += "当前士气不足，无法制作此配方。\n";
    }
    const inventory &available = crafter.crafting_inventory();
    const auto filter = id->get_component_filter( recipe_filter_flags::none );
    // Requirement checks cache availability inside their data; keep previews
    // local so they cannot change another native crafting menu's checks.
    const auto deduped = id->deduped_requirements();
    const bool materials = deduped.can_make_with_inventory( available, filter, batch_size,
                           craft_flags::start_only );
    result += materials ? "开工材料与工具：已满足。\n" :
              "开工材料与工具：尚未满足。\n";
    if( !materials ) {
        const auto needed = id->simple_requirements() * static_cast<unsigned>( batch_size );
        needed.can_make_with_inventory( available, filter, 1, craft_flags::start_only );
        const std::string missing = needed.list_missing();
        result += missing.empty() ? "现有物品无法同时满足所需材料组合。\n" :
                  "当前缺少：\n" + missing;
    }
    if( deduped.is_too_complex() ) {
        result += "复杂材料组合仍需原版确认。\n";
    }
    const std::string requirements = batch_size == 1 ? details( id ) :
                                     id->description.translated() + "\n" +
                                     ( id->simple_requirements() * static_cast<unsigned>( batch_size ) ).list_all();
    return remove_color_tags( result + "\n" + requirements );
}

bool start( avatar &you, const recipe_id &id, int batch_size, std::string &error )
{
    error.clear();
    if( you.is_dead_state() || you.activity || !id.is_valid() || batch_size < 1 || batch_size > 50 ||
        id->obsolete || id->is_nested() ) {
        error = "配方、批量或人物状态无效，请刷新制作页。";
        return false;
    }
    {
        // Selection/eligibility only. Product rolls belong to make_craft below.
        scoped_rng_restore restore_rng;
        you.invalidate_crafting_inventory();
        if( !you.get_group_available_recipes().contains( &*id ) ) {
            error = "此配方已不在原版当前可用配方中，请刷新制作页。";
            return false;
        }
        if( !confirm_crafting_recipe( you, *id, batch_size ) || !crafting_allowed( you, *id ) ) {
            error = "原版制作条件未通过，或确认已取消。";
            return false;
        }
    }
    uistate.read_recipes.insert( id );
    you.make_craft( id, batch_size );
    if( !you.activity ) {
        error = "制作未开始，材料选择或原版确认可能已取消。";
        return false;
    }
    return true;
}

}
#endif
