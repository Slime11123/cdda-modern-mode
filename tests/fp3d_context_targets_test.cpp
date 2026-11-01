#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "fp3d_snapshot.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "cata_scope_helpers.h"
#include "player_helpers.h"
#include "rng.h"

TEST_CASE( "fp3d_object_context_contains_object_actions_and_never_npc_commands",
           "[fp3d][interaction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto cabinet = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.ter_set( cabinet, ter_str_id( "t_floor" ) );
    here.furn_set( cabinet, furn_str_id( "f_locker" ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    const auto engine = rng_get_engine();
    const int moves = you.get_moves();
    const auto clock = calendar::turn;
    const fp3d_targeting::target target{ you.pos_abs(), here.get_abs( cabinet ),
                                         fp3d_targeting::kind::terrain, here.ter( cabinet ).id().str() };
    const auto choices = fp3d_interaction::context_entries( target, you, here );
    REQUIRE( choices[3].visible );
    CHECK( choices[3].enabled );
    CHECK_FALSE( choices[2].visible );
    CHECK_FALSE( choices[5].visible );
    CHECK_FALSE( choices[6].visible );
    CHECK( rng_get_engine() == engine );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );
}

TEST_CASE( "fp3d_snapshot_interaction_affordances_only_describe_visible_nearby_targets",
           "[fp3d][interaction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    here.ter_set( you.pos_bub( here ), ter_str_id( "t_floor" ) );
    const auto nearby = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto distant = you.pos_bub( here ) + tripoint_rel_ms( 0, -5, 0 );
    here.ter_set( nearby, ter_str_id( "t_floor" ) );
    here.furn_set( nearby, furn_str_id( "f_locker" ) );
    here.furn_set( distant, furn_str_id( "f_locker" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const int moves = you.get_moves();
    const auto scene = fp3d_snapshot::capture( you, here );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      here.get_abs( nearby ) ) != scene.interactable.end() );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      here.get_abs( distant ) ) == scene.interactable.end() );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      you.pos_abs() ) == scene.interactable.end() );
    CHECK( scene.interactable.size() <= 9 );
    for( const auto &position : scene.interactable ) {
        CHECK( position.z() == you.posz() );
        CHECK( square_dist( position, you.pos_abs() ) <= 1 );
    }
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );
}

TEST_CASE( "fp3d_hidden_npc_does_not_leak_through_terrain_interaction_hints",
           "[fp3d][interaction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 64, 65 ), "test_talker" );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    here.ter_set( person.pos_bub( here ), ter_str_id( "t_floor" ) );
    here.furn_set( person.pos_bub( here ), furn_str_id( "f_null" ) );
    // Randomized NPC equipment may spill onto spawn. This case tests an
    // empty floor; visible ground items should still be interactable.
    here.i_clear( person.pos_bub( here ) );
    fp3d_snapshot::prepare_view( you, here );
    const fp3d_targeting::target terrain{ you.pos_abs(), person.pos_abs(),
                                          fp3d_targeting::kind::terrain, "t_floor" };
    REQUIRE( you.sees( here, person ) );
    REQUIRE( fp3d_interaction::context_entries( terrain, you, here )[2].visible );
    person.toggle_trait( trait_id( "DEBUG_CLOAK" ) );
    REQUIRE( person.is_invisible() );
    REQUIRE_FALSE( you.sees( here, person ) );
    REQUIRE( you.sees( here, person.pos_bub( here ) ) );
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const int moves = you.get_moves();
    const auto choices = fp3d_interaction::context_entries( terrain, you, here );
    for( int index = 0; index < 7; ++index ) {
        CHECK_FALSE( choices[index].visible );
    }
    const auto scene = fp3d_snapshot::capture( you, here );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      person.pos_abs() ) == scene.interactable.end() );
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );
}
#endif
