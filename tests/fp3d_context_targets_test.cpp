#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <sstream>
#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cata_catch.h"
#include "fp3d_compositor.h"
#include "fp3d_snapshot.h"
#include "game.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "cata_scope_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "item.h"

TEST_CASE( "fp3d_stair_wheel_uses_terrain_flags_and_requires_standing_on_stairs",
           "[fp3d][interaction][stairs]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto standing = you.pos_bub( here );
    for( const auto &id : { "t_stairs_up", "t_stairs_down", "t_floor" } ) {
        here.ter_set( standing, ter_str_id( id ) );
        const fp3d_targeting::target target{ you.pos_abs(), you.pos_abs(),
                                            fp3d_targeting::kind::terrain, id };
        const auto clock = calendar::turn;
        const auto engine = rng_get_engine();
        const int moves = you.get_moves();
        const auto choices = fp3d_interaction::context_entries( target, you, here );
        const auto up = fp3d_interaction::resolve( "FP3D_STAIRS_UP", target, you, here );
        const auto down = fp3d_interaction::resolve( "FP3D_STAIRS_DOWN", target, you, here );
        CHECK( static_cast<bool>( up ) == ( std::string( id ) == "t_stairs_up" ) );
        CHECK( static_cast<bool>( down ) == ( std::string( id ) == "t_stairs_down" ) );
        if( up ) { CHECK( up->action == ACTION_MOVE_UP ); }
        if( down ) { CHECK( down->action == ACTION_MOVE_DOWN ); }
        if( std::string( id ) != "t_floor" ) {
            CHECK( choices[0].visible );
            CHECK( choices[1].visible );
            CHECK( choices[0].enabled == static_cast<bool>( up ) );
            CHECK( choices[1].enabled == static_cast<bool>( down ) );
            CHECK_FALSE( choices[2].visible );
        }
        CHECK( calendar::turn == clock );
        CHECK( you.get_moves() == moves );
        CHECK( rng_get_engine() == engine );
    }
    const auto adjacent = standing + tripoint_rel_ms::north;
    here.ter_set( adjacent, ter_str_id( "t_stairs_up" ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    const fp3d_targeting::target target{ you.pos_abs(), here.get_abs( adjacent ),
                                        fp3d_targeting::kind::terrain, "t_stairs_up" };
    const auto choices = fp3d_interaction::context_entries( target, you, here );
    CHECK_FALSE( choices[0].enabled );
    CHECK_FALSE( choices[1].enabled );
    REQUIRE( choices[2].enabled );
    CHECK( choices[2].action == ACTION_MOVE_FORTH );
    CHECK_FALSE( fp3d_interaction::resolve( "FP3D_STAIRS_UP", target, you, here ) );
    const auto scene = fp3d_snapshot::capture( you, here );
    CHECK( std::find( scene.interactable.begin(), scene.interactable.end(),
                      target.position ) != scene.interactable.end() );
}

TEST_CASE( "fp3d_stair_requests_use_native_vertical_movement", "[fp3d][interaction][stairs]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto origin = you.pos_abs();
    here.ter_set( you.pos_bub( here ), ter_str_id( "t_stairs_up" ) );
    here.ter_set( you.pos_bub( here ) + tripoint_rel_ms::above, ter_str_id( "t_stairs_down" ) );
    here.build_map_cache( 0 );
    const fp3d_targeting::target target{ origin, origin, fp3d_targeting::kind::terrain, "t_stairs_up" };
    const auto up = fp3d_interaction::resolve( "FP3D_STAIRS_UP", target, you, here );
    REQUIRE( up );
    REQUIRE( up->action == ACTION_MOVE_UP );
    const int moves = you.get_moves();
    g->vertical_move( 1, false );
    CHECK( you.pos_abs() == origin + tripoint_rel_ms::above );
    CHECK( you.get_moves() < moves );
    CHECK_FALSE( fp3d_interaction::resolve( "FP3D_STAIRS_UP", target, you, here ) );
    const fp3d_targeting::target upper{ you.pos_abs(), you.pos_abs(),
                                       fp3d_targeting::kind::terrain, "t_stairs_down" };
    const auto down = fp3d_interaction::resolve( "FP3D_STAIRS_DOWN", upper, you, here );
    REQUIRE( down );
    REQUIRE( down->action == ACTION_MOVE_DOWN );
    g->vertical_move( -1, false );
    CHECK( you.pos_abs() == origin );
}

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
TEST_CASE( "modern_2d_context_options_follow_target_and_revalidate", "[fp3d][context_2d]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto options_for = [&]() {
        fp3d_snapshot::prepare_view( you, here );
        return fp3d_interaction::context_2d_entries( p, you, here );
    };
    const auto find_action = []( const auto &options, action_id action ) {
        return std::find_if( options.begin(), options.end(), [&]( const auto &row ) { return row.action == action; } );
    };
    here.ter_set( p, ter_str_id( "t_door_c" ) );
    auto options = options_for();
    REQUIRE_FALSE( options.empty() );
    CHECK( std::string( options.front().label ) == "前往此处" );
    const auto open = find_action( options, ACTION_OPEN );
    REQUIRE( open != options.end() );
    REQUIRE( open->enabled );
    const auto opening = *open;
    const auto clock = calendar::turn;
    const auto random = rng_get_engine();
    const auto moves = you.get_moves();
    const auto origin = you.pos_abs();
    const auto request = fp3d_interaction::resolve_2d_choice( opening, p, you, here );
    REQUIRE( request );
    CHECK( request->action == ACTION_OPEN );
    CHECK( request->position == p );
    CHECK( here.ter( p ) == ter_str_id( "t_door_c" ).id() );
    CHECK( calendar::turn == clock );
    CHECK( rng_get_engine() == random );
    CHECK( you.get_moves() == moves );
    CHECK( you.pos_abs() == origin );

    here.ter_set( p, ter_str_id( "t_door_o" ) );
    options = options_for();
    CHECK( find_action( options, ACTION_OPEN ) == options.end() );
    CHECK( find_action( options, ACTION_CLOSE ) != options.end() );
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( opening, p, you, here ) );

    here.ter_set( p, ter_str_id( "t_floor" ) );
    here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
    options = options_for();
    REQUIRE( find_action( options, ACTION_PICKUP ) != options.end() );
    const auto pickup = *find_action( options, ACTION_PICKUP );
    here.i_clear( p );
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( pickup, p, you, here ) );

    npc &person = spawn_npc( p.xy(), "test_talker" );
    on_out_of_scope cleanup( []() { clear_npcs(); } );
    options = options_for();
    CHECK( find_action( options, ACTION_CHAT ) != options.end() );
    person.toggle_trait( trait_id( "DEBUG_CLOAK" ) );
    options = options_for();
    CHECK( find_action( options, ACTION_CHAT ) == options.end() );

    you.add_effect( efftype_id( "blind" ), 1_hours );
    CHECK( options_for().empty() );
}

TEST_CASE( "modern_2d_context_travel_schedules_native_path_without_teleporting", "[fp3d][context_2d]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto destination = you.pos_bub( here ) + tripoint_rel_ms( 4, 0, 0 );
    fp3d_snapshot::prepare_view( you, here );
    const auto options = fp3d_interaction::context_2d_entries( destination, you, here );
    REQUIRE_FALSE( options.empty() );
    REQUIRE( options.front().enabled );
    const auto origin = you.pos_abs();
    const auto moves = you.get_moves();
    const auto time = calendar::turn;
    const auto request = fp3d_interaction::resolve_2d_choice( options.front(), destination, you, here );
    REQUIRE( request );
    CHECK( request->action != ACTION_NULL );
    CHECK( you.pos_abs() == origin );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == time );
    CHECK_FALSE( you.get_auto_move_route().empty() );
    you.clear_destination();
    CHECK_FALSE( fp3d_interaction::context_2d_entries( you.pos_bub( here ), you, here ).front().enabled );
    here.ter_set( destination, ter_str_id( "t_wall" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto wall = fp3d_interaction::context_2d_entries( destination, you, here );
    REQUIRE_FALSE( wall.empty() );
    CHECK_FALSE( wall.front().enabled );
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( wall.front(), destination, you, here ) );
}

TEST_CASE( "modern_2d_context_aim_passes_clicked_tile_to_native_aiming", "[fp3d][context_2d]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto target = you.pos_bub( here ) + tripoint_rel_ms( 4, 0, 0 );
    fp3d_snapshot::prepare_view( you, here );
    const auto is_aim = []( const auto &entry ) { return entry.action == ACTION_FIRE; };

    REQUIRE_FALSE( you.is_armed() );
    auto options = fp3d_interaction::context_2d_entries( target, you, here );
    REQUIRE_FALSE( options.empty() );
    CHECK( std::none_of( options.begin(), options.end(), is_aim ) );

    you.set_wielded_item( item( itype_id( "shortbow" ) ) );
    options = fp3d_interaction::context_2d_entries( target, you, here );
    CHECK( std::any_of( options.begin(), options.end(), is_aim ) );

    item gun( itype_id( "glock_19" ) );
    gun.ammo_set( itype_id( "9mm" ), 10 );
    you.set_wielded_item( gun );
    options = fp3d_interaction::context_2d_entries( target, you, here );
    const auto aim = std::find_if( options.begin(), options.end(), is_aim );
    REQUIRE( aim != options.end() );
    CHECK( std::string( options.front().label ) == "前往此处" );
    const auto intent = fp3d_interaction::resolve_2d_choice( *aim, target, you, here );
    REQUIRE( intent );
    REQUIRE( intent->position == target );
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( *aim, you.pos_bub( here ), you, here ) );
    const auto moves = you.get_moves();
    const auto clock = calendar::turn;
    const auto last_target = you.last_target_pos;
    const auto clicked = here.get_abs( *intent->position );
    avatar_action::fire_wielded_weapon( you, clicked );
    REQUIRE( you.has_activity( activity_id( "ACT_AIM" ) ) );
    std::ostringstream saved;
    JsonOut json( saved );
    you.activity.serialize( json );
    const auto data = json_loader::from_string( saved.str() ).get_object();
    data.allow_omitted_members();
    const auto wrapper = data.get_object( "actor" );
    wrapper.allow_omitted_members();
    const auto actor = wrapper.get_object( "actor_data" );
    actor.allow_omitted_members();
    tripoint_abs_ms initial_target;
    REQUIRE( actor.read( "initial_target", initial_target ) );
    CHECK( initial_target == clicked );
    CHECK( you.last_target_pos == last_target );
    CHECK( you.get_wielded_item()->ammo_remaining() == 10 );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );

    you.cancel_activity();
    you.remove_weapon();
    CHECK_FALSE( fp3d_interaction::resolve_2d_choice( *aim, target, you, here ) );
}
TEST_CASE( "modern_2d_distant_interaction_walks_then_resolves_the_selected_action",
           "[fp3d][context_2d][approach]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto controls = fp3d_compositor::controls();
    fp3d_compositor::apply_modes( false, true );
    on_out_of_scope cleanup( [&]() {
        you.clear_destination();
        fp3d_interaction::take_2d_arrival();
        fp3d_compositor::apply_modes( controls == fp3d_input::mode::first_person_3d,
                                     controls != fp3d_input::mode::original_2d );
        clear_npcs();
    } );
    REQUIRE( fp3d_compositor::controls() == fp3d_input::mode::modern_2d );
    // Cross a submap boundary so the saved absolute target must survive map shifting.
    const auto target = you.pos_bub( here ) + tripoint_rel_ms( 14, 0, 0 );
    const auto absolute_target = here.get_abs( target );
    action_id selected = ACTION_OPEN;
    std::string command;
    int reach = 1;
    SECTION( "open" ) {
        here.ter_set( target, ter_str_id( "t_door_c" ) );
    }
    SECTION( "close" ) {
        here.ter_set( target, ter_str_id( "t_door_o" ) );
        selected = ACTION_CLOSE;
    }
    SECTION( "search" ) {
        here.furn_set( target, furn_str_id( "f_locker" ) );
        selected = ACTION_EXAMINE_AND_PICKUP;
    }
    SECTION( "smash" ) {
        here.furn_set( target, furn_str_id( "f_locker" ) );
        selected = ACTION_SMASH;
    }
    SECTION( "pickup" ) {
        here.add_item_or_charges( target, item( itype_id( "rock" ) ) );
        selected = ACTION_PICKUP;
    }
    SECTION( "talk" ) {
        spawn_npc( target.xy(), "test_talker" );
        selected = ACTION_CHAT;
    }
    SECTION( "melee" ) {
        spawn_test_monster( "mon_zombie", target );
        selected = ACTION_NULL;
        command = "FP3D_ATTACK";
    }
    SECTION( "stairs" ) {
        here.ter_set( target, ter_str_id( "t_stairs_up" ) );
        selected = ACTION_MOVE_UP;
        reach = 0;
    }
    fp3d_snapshot::prepare_view( you, here );
    const auto options = fp3d_interaction::context_2d_entries( target, you, here );
    const auto choice = std::find_if( options.begin(), options.end(), [&]( const auto &entry ) {
        return entry.action == selected && std::string( entry.command ? entry.command : "" ) == command;
    } );
    REQUIRE( choice != options.end() );
    REQUIRE( choice->enabled );
    const auto origin = you.pos_abs();
    const auto moves = you.get_moves();
    const auto clock = calendar::turn;
    const auto first = fp3d_interaction::resolve_2d_choice( *choice, target, you, here );
    REQUIRE( first );
    REQUIRE( first->action != ACTION_NULL );
    CHECK( you.pos_abs() == origin );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );
    REQUIRE( you.has_destination() );
    CHECK( square_dist( you.get_auto_move_route().back(), target ) == reach );
    REQUIRE( you.get_destination_activity().id() == activity_id( "ACT_MODERN_2D_INTERACT" ) );

    // Reload the native destination activity while en route.
    std::ostringstream saved;
    JsonOut out( saved );
    you.get_destination_activity().serialize( out );
    player_activity restored;
    restored.deserialize( json_loader::from_string( saved.str() ).get_object() );
    you.set_destination_activity( restored );
    auto step = first->action;
    int steps = 0;
    do {
        const auto delta = get_delta_from_movement_action( step, iso_rotate::yes );
        REQUIRE( avatar_action::move( you, here, tripoint_rel_ms( delta, 0 ) ) );
        REQUIRE( ++steps <= 20 );
        CHECK_FALSE( fp3d_interaction::take_2d_arrival() );
        step = you.get_next_auto_move_direction();
    } while( step != ACTION_NULL );
    CHECK( you.get_moves() < moves );
    REQUIRE( you.has_destination_activity() );
    fp3d_snapshot::prepare_view( you, here );
    you.start_destination_activity();
    const auto arrived = fp3d_interaction::take_2d_arrival();
    REQUIRE( arrived );
    CHECK( arrived->position == here.get_bub( absolute_target ) );
    const auto expected_action = command == "FP3D_ATTACK" ?
                                 get_movement_action_from_delta( here.get_bub( absolute_target ) - you.pos_bub( here ), iso_rotate::yes ) : selected;
    CHECK( arrived->action == expected_action );
    CHECK( arrived->targeted_melee == ( command == "FP3D_ATTACK" ) );
    CHECK( arrived->targeted_pickup == ( selected == ACTION_PICKUP ) );
    CHECK_FALSE( you.has_destination() );
    CHECK_FALSE( you.has_destination_activity() );
    CHECK_FALSE( fp3d_interaction::take_2d_arrival() );
    if( selected == ACTION_OPEN ) {
        REQUIRE( here.open_door( you, *arrived->position, true ) );
        CHECK( here.ter( here.get_bub( absolute_target ) ) == ter_str_id( "t_door_o" ).id() );
    }
}

TEST_CASE( "modern_2d_approach_cancels_instead_of_acting_on_stale_targets",
           "[fp3d][context_2d][approach]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto controls = fp3d_compositor::controls();
    fp3d_compositor::apply_modes( false, true );
    on_out_of_scope cleanup( [&]() {
        you.clear_destination();
        fp3d_interaction::take_2d_arrival();
        fp3d_compositor::apply_modes( controls == fp3d_input::mode::first_person_3d,
                                     controls != fp3d_input::mode::original_2d );
    } );
    const auto target = you.pos_bub( here ) + tripoint_rel_ms( 5, 0, 0 );
    here.ter_set( target, ter_str_id( "t_door_c" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto options = fp3d_interaction::context_2d_entries( target, you, here );
    const auto open = std::find_if( options.begin(), options.end(), []( const auto &entry ) {
        return entry.action == ACTION_OPEN;
    } );
    REQUIRE( open != options.end() );
    REQUIRE( fp3d_interaction::resolve_2d_choice( *open, target, you, here ) );
    const auto destination = you.get_auto_move_route().back();
    SECTION( "manual_cancel_clears_followup" ) {
        you.abort_automove();
        you.setpos( here, destination );
        CHECK( you.get_destination_activity().is_null() );
        CHECK_FALSE( you.has_destination_activity() );
    }
    SECTION( "changed_terrain" ) {
        here.ter_set( target, ter_str_id( "t_door_o" ) );
        you.setpos( here, destination );
        you.start_destination_activity();
    }
    SECTION( "failed_last_step_is_not_arrival" ) {
        you.get_auto_move_route().clear();
        REQUIRE( you.has_destination_activity() );
        you.start_destination_activity();
    }
    SECTION( "changed_mode" ) {
        fp3d_compositor::apply_modes( false, false );
        you.setpos( here, destination );
        you.start_destination_activity();
    }
    CHECK_FALSE( fp3d_interaction::take_2d_arrival() );
}
TEST_CASE( "modern_2d_approach_does_not_attack_a_replacement_creature",
           "[fp3d][context_2d][approach]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto controls = fp3d_compositor::controls();
    fp3d_compositor::apply_modes( false, true );
    on_out_of_scope cleanup( [&]() {
        you.clear_destination();
        fp3d_interaction::take_2d_arrival();
        fp3d_compositor::apply_modes( controls == fp3d_input::mode::first_person_3d,
                                     controls != fp3d_input::mode::original_2d );
    } );
    const auto target = you.pos_bub( here ) + tripoint_rel_ms( 5, 0, 0 );
    monster &original = spawn_test_monster( "mon_zombie", target );
    fp3d_snapshot::prepare_view( you, here );
    const auto options = fp3d_interaction::context_2d_entries( target, you, here );
    const auto attack = std::find_if( options.begin(), options.end(), []( const auto &entry ) {
        return entry.command && std::string( entry.command ) == "FP3D_ATTACK";
    } );
    REQUIRE( attack != options.end() );
    REQUIRE( attack->enabled );
    REQUIRE( fp3d_interaction::resolve_2d_choice( *attack, target, you, here ) );
    const auto destination = you.get_auto_move_route().back();
    original.setpos( here, target + tripoint_rel_ms::east );
    SECTION( "original_moved" ) {}
    SECTION( "same_species_replaced_target" ) {
        spawn_test_monster( "mon_zombie", target );
    }
    you.setpos( here, destination );
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE( you.has_destination_activity() );
    you.start_destination_activity();
    CHECK_FALSE( fp3d_interaction::take_2d_arrival() );
}
#endif
