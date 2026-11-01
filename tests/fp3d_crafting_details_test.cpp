#include "fp3d_crafting.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "output.h"
#include "player_helpers.h"
#include "recipe.h"
#include "rng.h"
#include "skill.h"

TEST_CASE( "fp3d_recipe_details_match_native_time_skills_and_proficiencies_without_a_turn",
           "[fp3d][crafting]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    const auto time = calendar::turn;
    const auto moves = you.get_moves();
    const auto position = you.pos_abs();
    const auto terrain = get_map().ter( you.pos_bub() );
    const auto engine = rng_get_engine();
    for( const recipe_id id : {
             recipe_id( "cudgel_test_no_tools" ), recipe_id( "bp_40fmj_test_no_tools" )
         } ) {
        CAPTURE( id );
        REQUIRE( id.is_valid() );
        std::string duration;
        {
            restore_on_out_of_scope<cata_default_random_engine> restore_rng( rng_get_engine() );
            duration = to_string( time_duration::from_moves( you.expected_time_to_craft( *id ) ) );
        }
        const auto view = fp3d_crafting::details( id, you );
        CHECK( view.find( duration ) != std::string::npos );
        CHECK( view.find( fp3d_crafting::details( id ) ) != std::string::npos );
        CHECK( view.find( "<color_" ) == std::string::npos );
        for( const auto &skill : id->required_skills ) {
            CHECK( view.find( skill.first->name() + " " + std::to_string( skill.second ) ) !=
                   std::string::npos );
        }
        const auto missing = remove_color_tags( id->missing_proficiencies_string( &you ) );
        if( !missing.empty() ) {
            CHECK( view.find( missing ) != std::string::npos );
        }
        for( int n = 0; n < 100; ++n ) {
            REQUIRE( fp3d_crafting::details( id, you ) == view );
        }
    }
    CHECK( fp3d_crafting::details( recipe_id( "not_a_recipe" ), you ).empty() );
    CHECK( you.pos_abs() == position );
    CHECK( get_map().ter( you.pos_bub() ) == terrain );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == time );
    CHECK( rng_get_engine() == engine );
}
#endif
