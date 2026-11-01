#include "fp3d_crafting.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "activity_type.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "fp3d_inventory.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "output.h"
#include "player_helpers.h"
#include "recipe.h"
#include "requirements.h"
#include "rng.h"
#include "scoped_rng.h"
#include "crafting_gui.h"
#include "crafting.h"
#include "skill.h"
#include "uistate.h"

static void prepare_batch_craft()
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.remove_weapon();
    you.learn_recipe( &recipe_id( "makeshift_crowbar_test_no_tools" ).obj() );
    for( int i = 0; i < 5; ++i ) {
        here.add_item_or_charges( you.pos_bub( here ), item( itype_id( "2x4" ), calendar::turn ) );
    }
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    you.set_moves( 100 );
}

static int ground_planks()
{
    int result = 0;
    for( const item &entry : get_map().i_at( get_avatar().pos_bub() ) ) {
        result += entry.typeId() == itype_id( "2x4" );
    }
    return result;
}

TEST_CASE( "fp3d_preview_restores_normal_distribution_spare_samples", "[fp3d][crafting]" )
{
    prepare_batch_craft();
    double expected;
    {
        scoped_rng_restore restore;
        expected = normal_roll( 12, 3 );
    }
    fp3d_crafting::capture( get_avatar() );
    fp3d_crafting::details( recipe_id( "makeshift_crowbar_test_no_tools" ), get_avatar(), 3 );
    CHECK( normal_roll( 12, 3 ) == expected );
}

TEST_CASE( "fp3d_recipe_favorites_share_native_ui_state_without_gameplay_cost", "[fp3d][crafting]" )
{
    prepare_batch_craft();
    restore_on_out_of_scope restore_favorites( uistate.favorite_recipes );
    restore_on_out_of_scope restore_read( uistate.read_recipes );
    const recipe_id id( "makeshift_crowbar_test_no_tools" );
    uistate.favorite_recipes.erase( id );
    uistate.read_recipes.erase( id );
    const int moves = get_avatar().get_moves();
    const auto clock = calendar::turn;
    const auto engine = rng_get_engine();
    REQUIRE( fp3d_crafting::set_favorite( get_avatar(), id, true ) );
    CHECK( fp3d_crafting::is_favorite( id ) );
    CHECK( uistate.favorite_recipes.count( id ) == 1 );
    CHECK( uistate.read_recipes.count( id ) == 1 );
    // Changes made in the original crafting menu are visible immediately too.
    uistate.favorite_recipes.erase( id );
    CHECK_FALSE( fp3d_crafting::is_favorite( id ) );
    uistate.favorite_recipes.insert( id );
    REQUIRE( fp3d_crafting::set_favorite( get_avatar(), id, false ) );
    CHECK_FALSE( fp3d_crafting::is_favorite( id ) );
    CHECK( uistate.read_recipes.count( id ) == 1 );
    const auto favorites = uistate.favorite_recipes;
    const auto read = uistate.read_recipes;
    CHECK_FALSE( fp3d_crafting::set_favorite( get_avatar(), recipe_id( "not_a_recipe" ), true ) );
    CHECK_FALSE( fp3d_crafting::set_favorite( get_avatar(), recipe_id( "cudgel_test_no_tools" ),
                 true ) );
    CHECK( uistate.favorite_recipes == favorites );
    CHECK( uistate.read_recipes == read );
    CHECK( get_avatar().get_moves() == moves );
    CHECK( calendar::turn == clock );
    CHECK( rng_get_engine() == engine );
}

TEST_CASE( "fp3d_batch_preview_uses_native_time_and_requirement_scaling", "[fp3d][crafting]" )
{
    prepare_batch_craft();
    avatar &you = get_avatar();
    const recipe_id id( "makeshift_crowbar_test_no_tools" );
    const int moves = you.get_moves();
    const auto clock = calendar::turn;
    const auto engine = rng_get_engine();
    for( int batch : {
             1, 3, 50
         } ) {
        std::string duration;
        std::string requirements;
        {
            restore_on_out_of_scope<cata_default_random_engine> restore_rng( rng_get_engine() );
            duration = to_string( time_duration::from_moves( you.expected_time_to_craft( *id, batch ) ) );
            requirements = remove_color_tags( ( id->simple_requirements() * static_cast<unsigned>
                                                ( batch ) ).list_all() );
        }
        const auto details = fp3d_crafting::details( id, you, batch );
        CHECK( details.find( duration ) != std::string::npos );
        CHECK( details.find( requirements ) != std::string::npos );
        for( int sample = 0; sample < 30; ++sample ) {
            CHECK( fp3d_crafting::details( id, you, batch ) == details );
        }
    }
    CHECK( fp3d_crafting::details( id, you, 0 ).empty() );
    CHECK( fp3d_crafting::details( id, you, 51 ).empty() );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );
    CHECK( rng_get_engine() == engine );
    CHECK( ground_planks() == 5 );
}

TEST_CASE( "fp3d_batch_request_starts_native_craft_and_consumes_only_selected_components",
           "[fp3d][crafting]" )
{
    prepare_batch_craft();
    avatar &you = get_avatar();
    map &here = get_map();
    int batch = 1;
    SECTION( "one batch" ) {}
    SECTION( "three batches" ) {
        batch = 3;
    }
    fp3d_inventory::operation request;
    request.kind = fp3d_inventory::operation_kind::craft;
    request.crafting_recipe = recipe_id( "makeshift_crowbar_test_no_tools" );
    request.crafting_batch = batch;
    const auto clock = calendar::turn;
    std::string error;
    REQUIRE( fp3d_inventory::execute( request, you, here, &error ) );
    CHECK( error.empty() );
    REQUIRE( you.activity.id() == activity_id( "ACT_CRAFT" ) );
    const auto craft = you.get_wielded_item();
    REQUIRE( craft );
    CHECK( craft->is_craft() );
    CHECK( craft->get_making().ident() == request.crafting_recipe );
    CHECK( craft->get_making_batch_size() == batch );
    CHECK( ground_planks() == 5 - batch );
    CHECK( calendar::turn == clock );
    const int moves = you.get_moves();
    const auto engine = rng_get_engine();
    CHECK_FALSE( fp3d_inventory::execute( request, you, here, &error ) );
    CHECK_FALSE( error.empty() );
    CHECK( ground_planks() == 5 - batch );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == engine );
}

TEST_CASE( "fp3d_batch_request_rechecks_invalid_and_changed_conditions_without_consumption",
           "[fp3d][crafting]" )
{
    prepare_batch_craft();
    avatar &you = get_avatar();
    map &here = get_map();
    fp3d_inventory::operation request;
    request.kind = fp3d_inventory::operation_kind::craft;
    request.crafting_recipe = recipe_id( "makeshift_crowbar_test_no_tools" );
    request.crafting_batch = 3;
    SECTION( "zero batch" ) {
        request.crafting_batch = 0;
    }
    SECTION( "oversized batch" ) {
        request.crafting_batch = 51;
    }
    SECTION( "unknown recipe identity" ) {
        request.crafting_recipe = recipe_id( "not_a_recipe" );
    }
    SECTION( "recipe is unavailable to the current group" ) {
        request.crafting_recipe = recipe_id( "cudgel_test_no_tools" );
    }
    SECTION( "known recipe but insufficient primary skill" ) {
        request.crafting_recipe = recipe_id( "cudgel_test_no_tools" );
        you.learn_recipe( &*request.crafting_recipe );
    }
    SECTION( "components removed after selection" ) {
        here.i_clear( you.pos_bub( here ) );
    }
    const int planks = ground_planks();
    const int moves = you.get_moves();
    const auto clock = calendar::turn;
    const auto engine = rng_get_engine();
    std::string error;
    CHECK_FALSE( fp3d_inventory::execute( request, you, here, &error ) );
    CHECK_FALSE( error.empty() );
    CHECK_FALSE( you.activity );
    CHECK( ground_planks() == planks );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );
    CHECK( rng_get_engine() == engine );
}

// Native selected-recipe action primitives versus the modern queued request.
// Optional completion feeds equal bounded test AP budgets to the native actor.
// Product code never refills AP; this is not a full-world-turn comparison.
TEST_CASE( "fp3d_native_batch_crafting_comparison", "[.fp3d_crafting_batch_compare]" )
{
    const char *mode = std::getenv( "CDDA_FP3D_COMBAT_MODE" );
    const char *report = std::getenv( "CDDA_FP3D_COMBAT_REPORT" );
    REQUIRE( mode );
    REQUIRE( report );
    const bool modern = std::string( mode ) == "modern";
    const char *finish_flag = std::getenv( "CDDA_FP3D_CRAFT_FINISH" );
    const bool finish = finish_flag && std::string( finish_flag ) == "1";
    REQUIRE( ( modern || std::string( mode ) == "native" ) );
    std::ofstream output( report );
    REQUIRE( output.good() );
    JsonOut json( output );
    json.start_array();
    for( int batch : {
             1, 3
         } ) {
        prepare_batch_craft();
        avatar &you = get_avatar();
        map &here = get_map();
        const recipe_id id( "makeshift_crowbar_test_no_tools" );
        you.set_skill_level( skill_id( "fabrication" ), 10 );
        rng_set_engine_seed( 424242 );
        {
            // The original menu also constructs this inventory and confirms
            // eligibility before entering the native make_craft primitive.
            scoped_rng_restore restore;
            you.invalidate_crafting_inventory();
            you.get_group_available_recipes();
            REQUIRE( confirm_crafting_recipe( you, *id, batch ) );
            REQUIRE( crafting_allowed( you, *id ) );
        }
        if( modern ) {
            fp3d_inventory::operation request;
            request.kind = fp3d_inventory::operation_kind::craft;
            request.crafting_recipe = id;
            request.crafting_batch = batch;
            REQUIRE( fp3d_inventory::execute( request, you, here ) );
        } else {
            you.make_craft( id, batch );
        }
        REQUIRE( you.activity.id() == activity_id( "ACT_CRAFT" ) );
        const auto craft = you.get_wielded_item();
        REQUIRE( craft );
        CHECK( craft->get_making_batch_size() == batch );
        CHECK( ground_planks() == 5 - batch );
        json.start_object();
        json.member( "scenario", "batch_craft_start" );
        json.member( "batch", batch );
        json.member( "player_moves", you.get_moves() );
        json.member( "stamina", you.get_stamina() );
        json.member( "time", to_turns<int>( calendar::turn - calendar::turn_zero ) );
        json.member( "planks", ground_planks() );
        json.member( "craft", *craft );
        if( finish ) {
            int ticks = 0;
            int work_moves = 0;
            while( you.activity.id() == activity_id( "ACT_CRAFT" ) && ticks < 4000 ) {
                you.set_moves( 100 );
                you.activity.do_turn( you );
                work_moves += 100 - you.get_moves();
                ++ticks;
            }
            REQUIRE_FALSE( you.activity );
            int result_count = you.amount_of( id->result() );
            for( const item &entry : here.i_at( you.pos_bub( here ) ) ) {
                result_count += entry.typeId() == id->result();
            }
            CHECK( result_count == batch );
            json.member( "activity_ticks", ticks );
            json.member( "work_moves", work_moves );
            json.member( "final_moves", you.get_moves() );
            json.member( "result_count", result_count );
            json.member( "final_planks", ground_planks() );
        }
        std::ostringstream engine;
        engine << rng_get_engine();
        json.member( "rng", engine.str() );
        json.end_object();
    }
    json.end_array();
    output.flush();
    REQUIRE( output.good() );
}
#endif
