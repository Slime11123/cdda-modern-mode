#include "fp3d_journal.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "json.h"
#include "messages.h"
#include "npctalk.h"
#include "string_formatter.h"
#include "translations.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "fp3d_inventory.h"
#include "fp3d_interaction.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_helpers.h"
#include "rng.h"

TEST_CASE( "fp3d_follower_orders_keep_native_costs_and_refresh_read_only_availability",
           "[fp3d][npc][orders]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 64, 65 ), "test_talker" );
    person.set_fac( faction_id( "your_followers" ) );
    person.set_attitude( NPCATT_FOLLOW );
    SECTION( "adjacent ally" ) {}
    SECTION( "visible ally beyond conversation range" ) {
        person.setpos( here, tripoint_bub_ms( 70, 65, 0 ) );
    }
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    REQUIRE( person.is_player_ally() );
    REQUIRE( person.is_following() );
    fp3d_inventory::operation request;
    request.kind = fp3d_inventory::operation_kind::follower_order;
    request.talking_npc_id = person.getID().get_value();
    request.destination_tile = person.pos_abs();
    const auto position = you.pos_abs();
    const auto time = calendar::turn;
    for( const auto order : {
             npc_orders::kind::guard, npc_orders::kind::follow
         } ) {
        const auto engine = rng_get_engine();
        const int moves = you.get_moves();
        REQUIRE( npc_orders::can_issue( you, person, order ) );
        const auto view = fp3d_journal::capture( you, here );
        const auto row = std::find_if( view.people.begin(), view.people.end(), [&]( const auto & entry ) {
            return entry.id == person.getID().get_value();
        } );
        REQUIRE( row != view.people.end() );
        CHECK( row->can_guard == ( order == npc_orders::kind::guard ) );
        CHECK( row->can_follow == ( order == npc_orders::kind::follow ) );
        CHECK( you.get_moves() == moves );
        CHECK( rng_get_engine() == engine );
        request.follower_command = order;
        const int speed = you.get_speed();
        REQUIRE( fp3d_inventory::execute( request, you, here ) );
        CHECK( you.get_moves() == moves - speed );
        CHECK( person.is_following() == ( order == npc_orders::kind::follow ) );
        CHECK( person.mission == ( order == npc_orders::kind::guard ? NPC_MISSION_GUARD_ALLY :
                                   NPC_MISSION_NULL ) );
        CHECK( you.pos_abs() == position );
        CHECK( calendar::turn == time );
    }
}

TEST_CASE( "fp3d_follower_order_rejects_stale_invisible_or_illegal_targets_without_cost",
           "[fp3d][npc][orders]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 64, 65 ), "test_talker" );
    person.set_fac( faction_id( "your_followers" ) );
    person.set_attitude( NPCATT_FOLLOW );
    fp3d_inventory::operation request;
    request.kind = fp3d_inventory::operation_kind::follower_order;
    request.talking_npc_id = person.getID().get_value();
    request.destination_tile = person.pos_abs();
    request.follower_command = npc_orders::kind::guard;
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    SECTION( "NPC moved after selection" ) {
        person.setpos( here, tripoint_bub_ms( 64, 64, 0 ) );
    }
    SECTION( "another NPC occupies the selected tile" ) {
        person.setpos( here, tripoint_bub_ms( 64, 64, 0 ) );
        spawn_npc( point_bub_ms( 64, 65 ), "test_talker" );
    }
    SECTION( "player is blind" ) {
        you.add_effect( efftype_id( "blind" ), 1_minutes );
    }
    SECTION( "NPC died" ) {
        person.set_part_hp_cur( bodypart_str_id( "torso" ).id(), 0 );
        REQUIRE( person.is_dead_state() );
    }
    SECTION( "NPC is not an ally" ) {
        person.set_fac( faction_id( "no_faction" ) );
    }
    SECTION( "NPC cannot hear" ) {
        person.add_effect( efftype_id( "deaf" ), 1_hours, false, 3 );
        REQUIRE_FALSE( person.can_hear( you.pos_bub(), you.get_shout_volume() ) );
    }
    SECTION( "not a following ally" ) {
        person.set_attitude( NPCATT_NULL );
    }
    SECTION( "guard assigned to a faction camp" ) {
        person.set_mission( NPC_MISSION_GUARD_ALLY );
        person.set_attitude( NPCATT_NULL );
        person.companion_mission_role_id = "FACTION_CAMP";
        request.follower_command = npc_orders::kind::follow;
    }
    SECTION( "solid wall hides NPC" ) {
        person.setpos( here, tripoint_bub_ms( 63, 65, 0 ) );
        request.destination_tile = person.pos_abs();
        here.ter_set( tripoint_bub_ms( 64, 65, 0 ), ter_str_id( "t_wall" ) );
    }
    SECTION( "NPC on another floor" ) {
        here.ter_set( tripoint_bub_ms( 64, 65, 1 ), ter_str_id( "t_floor" ) );
        person.setpos( here, tripoint_bub_ms( 64, 65, 1 ) );
        request.destination_tile = person.pos_abs();
    }
    SECTION( "unknown command" ) {
        request.follower_command = static_cast<npc_orders::kind>( 99 );
    }
    SECTION( "missing identity" ) {
        request.talking_npc_id.reset();
    }
    SECTION( "invalid identity" ) {
        request.talking_npc_id = -1;
    }
    SECTION( "missing position" ) {
        request.destination_tile.reset();
    }
    SECTION( "missing command" ) {
        request.follower_command.reset();
    }
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    const auto engine = rng_get_engine();
    const auto time = calendar::turn;
    const int moves = you.get_moves();
    const auto position = you.pos_abs();
    const auto mission = person.mission;
    const auto attitude = person.get_attitude();
    std::string error;
    CHECK_FALSE( fp3d_inventory::execute( request, you, here, &error ) );
    CHECK_FALSE( error.empty() );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == time );
    CHECK( you.pos_abs() == position );
    CHECK( rng_get_engine() == engine );
    CHECK( person.mission == mission );
    CHECK( person.get_attitude() == attitude );
}


TEST_CASE( "fp3d_context_wheel_reads_native_permissions_without_mutating_the_world",
           "[fp3d][npc][wheel]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 64, 65 ), "test_talker" );
    person.set_fac( faction_id( "your_followers" ) );
    person.set_attitude( NPCATT_FOLLOW );
    bool guard = true;
    bool follow = false;
    bool chat = true;
    SECTION( "following ally" ) {}
    SECTION( "guarding ally" ) {
        person.set_attitude( NPCATT_NULL );
        person.set_mission( NPC_MISSION_GUARD_ALLY );
        guard = false;
        follow = true;
    }
    SECTION( "camp guard" ) {
        person.set_attitude( NPCATT_NULL );
        person.set_mission( NPC_MISSION_GUARD_ALLY );
        person.companion_mission_role_id = "FACTION_CAMP";
        guard = false;
    }
    SECTION( "neutral person" ) {
        person.set_fac( faction_id( "no_faction" ) );
        guard = false;
    }
    SECTION( "deaf ally" ) {
        person.add_effect( efftype_id( "deaf" ), 1_hours, false, 3 );
        REQUIRE_FALSE( person.can_hear( you.pos_bub(), you.get_shout_volume() ) );
        guard = false;
    }
    SECTION( "distant person is outside targeted interaction reach" ) {
        person.setpos( here, tripoint_bub_ms( 70, 65, 0 ) );
        guard = chat = false;
    }
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    const fp3d_targeting::target target{ you.pos_abs(), person.pos_abs(), fp3d_targeting::kind::creature, {} };
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const int moves = you.get_moves();
    const auto mission = person.mission;
    for( int sample = 0; sample < 30; ++sample ) {
        const auto choices = fp3d_interaction::context_entries( target, you, here );
        CHECK( choices[2].visible == chat );
        CHECK_FALSE( choices[0].visible );
        CHECK_FALSE( choices[1].visible );
        CHECK_FALSE( choices[3].visible );
        CHECK_FALSE( choices[4].visible );
        CHECK( choices[2].enabled == chat );
        CHECK( choices[5].enabled == guard );
        CHECK( choices[6].enabled == follow );
        CHECK( choices[7].enabled );
    }
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );
    CHECK( rng_get_engine() == engine );
    CHECK( person.mission == mission );
}

TEST_CASE( "fp3d_short_interaction_stages_the_selected_npc_identity_once",
           "[fp3d][npc][interaction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 64, 65 ), "test_talker" );
    npc &other = spawn_npc( point_bub_ms( 65, 64 ), "test_talker" );
    REQUIRE( person.getID() != other.getID() );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    fp3d_inventory::clear_pending();
    on_out_of_scope cleanup( []() {
        clear_npcs();
        fp3d_inventory::clear_pending();
    } );
    const fp3d_targeting::target target{ you.pos_abs(), person.pos_abs(), fp3d_targeting::kind::creature, {} };
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const int moves = you.get_moves();
    const auto result = fp3d_interaction::resolve( "FP3D_INTERACT", target, you, here );
    REQUIRE( result );
    CHECK( result->action == ACTION_CHAT );
    CHECK( result->position == std::optional<tripoint_bub_ms>( person.pos_bub( here ) ) );
    CHECK( fp3d_inventory::take_talking_npc_id() == std::optional<int>( person.getID().get_value() ) );
    CHECK_FALSE( fp3d_inventory::take_talking_npc_id() );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );
    CHECK( rng_get_engine() == engine );
}

// Cold-process native case primitives versus the actual modern operation path.
TEST_CASE( "fp3d_native_follower_order_comparison", "[.fp3d_orders_compare]" )
{
    const char *mode = std::getenv( "CDDA_FP3D_COMBAT_MODE" );
    const char *report = std::getenv( "CDDA_FP3D_COMBAT_REPORT" );
    REQUIRE( mode );
    REQUIRE( report );
    const bool modern = std::string( mode ) == "modern";
    REQUIRE( ( modern || std::string( mode ) == "native" ) );
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 70, 65 ), "test_talker" );
    person.set_fac( faction_id( "your_followers" ) );
    person.set_attitude( NPCATT_FOLLOW );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    REQUIRE( you.sees( here, person ) );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    rng_set_engine_seed( 424242 );
    std::ofstream output( report );
    REQUIRE( output.good() );
    JsonOut json( output );
    json.start_array();
    for( const auto order : {
             npc_orders::kind::guard, npc_orders::kind::follow
         } ) {
        const int moves = you.get_moves();
        const int cost = you.get_speed();
        if( modern ) {
            fp3d_inventory::operation request;
            request.kind = fp3d_inventory::operation_kind::follower_order;
            request.talking_npc_id = person.getID().get_value();
            request.destination_tile = person.pos_abs();
            request.follower_command = order;
            REQUIRE( fp3d_inventory::execute( request, you, here ) );
        } else {
            // Exact original single-NPC guard/follow case and chat tail.
            std::string yell;
            if( order == npc_orders::kind::guard ) {
                talk_function::assign_guard( person );
                yell = string_format( _( "Guard here, %s!" ), person.get_name() );
            } else {
                talk_function::stop_guard( person );
                yell = string_format( _( "Follow me, %s!" ), person.get_name() );
            }
            const std::string message = string_format( _( "\"%s\"" ), yell );
            add_msg( _( "You yell %s" ), message );
            you.shout( string_format( _( "%s yelling %s" ), you.disp_name(), message ), true );
            you.mod_moves( -you.get_speed() );
        }
        CHECK( you.get_moves() == moves - cost );
        CHECK( person.is_following() == ( order == npc_orders::kind::follow ) );
        json.start_object();
        json.member( "scenario", order == npc_orders::kind::guard ? "guard" : "follow" );
        json.member( "player_moves", you.get_moves() );
        json.member( "stamina", you.get_stamina() );
        json.member( "time", to_turns<int>( calendar::turn - calendar::turn_zero ) );
        json.member( "x", you.pos_abs().x() );
        json.member( "y", you.pos_abs().y() );
        json.member( "z", you.pos_abs().z() );
        json.member( "npc_x", person.pos_abs().x() );
        json.member( "npc_y", person.pos_abs().y() );
        json.member( "npc_z", person.pos_abs().z() );
        json.member( "mission", static_cast<int>( person.mission ) );
        json.member( "attitude", static_cast<int>( person.get_attitude() ) );
        json.member( "faction", person.get_fac_id().str() );
        json.member( "topic", person.chatbin.first_topic );
        json.member( "goal_x", person.goal.x() );
        json.member( "goal_y", person.goal.y() );
        json.member( "goal_z", person.goal.z() );
        if( person.guard_pos ) {
            json.member( "guard_x", person.guard_pos->x() );
            json.member( "guard_y", person.guard_pos->y() );
            json.member( "guard_z", person.guard_pos->z() );
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
