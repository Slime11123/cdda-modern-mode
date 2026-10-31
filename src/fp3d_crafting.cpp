#include "fp3d_crafting.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "cata_scope_helpers.h"
#include "rng.h"
#include "itype.h"
#include "output.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "skill.h"
namespace fp3d_crafting
{
std::vector<entry> capture( const avatar &you )
{
    // Native crafting inventory builds temporary item variants. This is a
    // main-thread read-only preview; actual crafting runs outside this scope.
    restore_on_out_of_scope<cata_default_random_engine> restore_rng( rng_get_engine() );
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
                            rec->get_difficulty( you ) } );
    }
    std::sort( result.begin(), result.end(), []( const auto & a, const auto & b ) {
        return a.name < b.name || ( a.name == b.name && a.id.str() < b.id.str() );
    } );
    return result;
}
std::string details( const recipe_id &id )
{
    if( !id.is_valid() ) {
        return {};
    }
    // Native requirement formatting constructs temporary component items too.
    restore_on_out_of_scope<cata_default_random_engine> restore_rng( rng_get_engine() );
    return remove_color_tags( id->description.translated() + "\n" +
                              id->simple_requirements().list_all() );
}
}
#endif
