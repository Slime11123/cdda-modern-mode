#include "fp3d_journal.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character_id.h"
#include "bodypart.h"
#include "fp3d_inventory.h"
#include "map.h"
#include "map_helpers.h"
#include "mission.h"
#include "npc.h"
#include "player_helpers.h"
#include "rng.h"
#include "type_id.h"

TEST_CASE( "fp3d_journal_tracks_only_current_native_missions_without_a_turn", "[fp3d][mission]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    avatar &you = get_avatar();
    you.reset_all_missions();
    mission::clear_all();
    on_out_of_scope cleanup( [&]() {
        you.reset_all_missions();
        mission::clear_all();
    } );
    mission *first = mission::reserve_new( mission_type_id( "TEST_MISSION_GOAL_CONDITION1" ),
                                           character_id() );
    mission *second = mission::reserve_new( mission_type_id( "TEST_MISSION_GOAL_CONDITION2" ),
                                            character_id() );
    REQUIRE( first );
    REQUIRE( second );
    first->set_assigned_player_id( character_id( -2 ) );
    second->set_assigned_player_id( character_id( -2 ) );
    first->assign( you );
    second->assign( you );
    first->set_target( you.pos_abs_omt() + tripoint( 5, 2, 0 ) );
    you.set_active_mission( *first );
    const auto before_time = calendar::turn;
    const auto before_rng = rng_get_engine();
    const int before_moves = you.get_moves();
    const auto model = fp3d_journal::capture( you, get_map() );
    REQUIRE( model.missions.size() == 2 );
    CHECK( model.missions.front().tracked );
    CHECK_FALSE( model.missions.front().destination.empty() );
    REQUIRE( fp3d_journal::track( you, second->get_id() ) );
    CHECK( you.get_active_mission() == second );
    CHECK_FALSE( fp3d_journal::track( you, -999 ) );
    you.reset_all_missions();
    CHECK_FALSE( fp3d_journal::track( you, second->get_id() ) );
    CHECK( calendar::turn == before_time );
    CHECK( rng_get_engine() == before_rng );
    CHECK( you.get_moves() == before_moves );
}

TEST_CASE( "fp3d_people_page_does_not_reveal_hidden_or_other_floor_npcs", "[fp3d][npc]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &visible = spawn_npc( point_bub_ms( 64, 65 ), "test_talker" );
    npc &hidden = spawn_npc( point_bub_ms( 70, 65 ), "test_talker" );
    npc &above = spawn_npc( point_bub_ms( 66, 66 ), "test_talker" );
    here.ter_set( tripoint_bub_ms( 66, 66, 1 ), ter_str_id( "t_floor" ) );
    above.setpos( here, tripoint_bub_ms( 66, 66, 1 ) );
    REQUIRE( above.posz() == 1 );
    here.ter_set( tripoint_bub_ms( 68, 65, 0 ), ter_str_id( "t_wall" ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    const auto before_rng = rng_get_engine();
    const auto before_time = calendar::turn;
    const int before_moves = you.get_moves();
    const auto model = fp3d_journal::capture( you, here );
    REQUIRE( model.people.size() == 1 );
    CHECK( model.people.front().position == visible.pos_abs() );
    CHECK( model.people.front().id == visible.getID().get_value() );
    CHECK( model.people.front().position != hidden.pos_abs() );
    CHECK( model.people.front().position != above.pos_abs() );
    CHECK( rng_get_engine() == before_rng );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
}

TEST_CASE( "fp3d_people_dialogue_revalidates_identity_position_visibility_and_range", "[fp3d][npc]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 64, 65 ), "test_talker" );
    npc &replacement = spawn_npc( point_bub_ms( 65, 66 ), "test_talker" );
    const int id = person.getID().get_value();
    REQUIRE( id > 0 );
    const auto original_position = person.pos_abs();
    auto expected_position = original_position;
    bool allowed = false;
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    on_out_of_scope cleanup( []() { clear_npcs(); } );
    SECTION( "visible adjacent native NPC" ) { allowed = true; }
    SECTION( "another NPC moved into the saved position" ) {
        person.setpos( here, tripoint_bub_ms( 70, 65, 0 ) );
        replacement.setpos( here, here.get_bub( original_position ) );
        CHECK( fp3d_journal::dialogue_target( you, here, replacement.getID().get_value(), original_position ) );
    }
    SECTION( "same NPC moved away" ) {
        person.setpos( here, tripoint_bub_ms( 70, 65, 0 ) );
        expected_position = person.pos_abs();
    }
    SECTION( "same NPC changed floor" ) {
        const auto above = person.pos_bub( here ) + tripoint_rel_ms::above;
        here.ter_set( above, ter_str_id( "t_floor" ) );
        person.setpos( here, above );
        expected_position = person.pos_abs();
    }
    SECTION( "player cannot see" ) {
        you.add_effect( efftype_id( "blind" ), 1_minutes );
    }
    SECTION( "NPC no longer alive" ) {
        person.set_part_hp_cur( bodypart_str_id( "torso" ).id(), 0 );
        REQUIRE( person.is_dead_state() );
    }
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const int moves = you.get_moves();
    const auto result = fp3d_journal::dialogue_target( you, here, id, expected_position );
    CHECK( static_cast<bool>( result ) == allowed );
    if( result ) { CHECK( *result == here.get_bub( original_position ) ); }
    CHECK_FALSE( fp3d_journal::dialogue_target( you, here, -1, expected_position ) );
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );
}

TEST_CASE( "fp3d_selected_dialogue_identity_is_consumed_once_without_stealing_other_requests", "[fp3d][npc]" )
{
    fp3d_inventory::operation request;
    request.kind = fp3d_inventory::operation_kind::native;
    request.native_action = ACTION_CHAT;
    request.talking_npc_id = 42;
    fp3d_inventory::stage( request );
    REQUIRE( fp3d_inventory::take_talking_npc_id() == std::optional<int>( 42 ) );
    CHECK_FALSE( fp3d_inventory::pending() );
    CHECK_FALSE( fp3d_inventory::take_talking_npc_id() );
    request.native_action = ACTION_CRAFT;
    fp3d_inventory::stage( request );
    CHECK_FALSE( fp3d_inventory::take_talking_npc_id() );
    CHECK( fp3d_inventory::pending() );
    fp3d_inventory::clear_pending();
    CHECK_FALSE( fp3d_inventory::pending() );
}
#endif
