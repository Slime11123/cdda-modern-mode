#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>

#include "avatar.h"
#include "cata_catch.h"
#include "calendar.h"
#include "fp3d_snapshot.h"
#include "fp3d_input.h"
#include "cata_scope_helpers.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "rng.h"
#include "type_id.h"

TEST_CASE( "fp3d_adjacent_target_and_native_revalidation_are_read_only", "[fp3d][interaction]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto north = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.ter_set( north, ter_str_id( "t_door_c" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto before_time = calendar::turn;
    const auto before_rng = rng_get_engine();
    const auto before_moves = you.get_moves();
    const auto target = fp3d_targeting::adjacent( scene, 0, 0 );
    REQUIRE( target );
    CHECK( target->position == here.get_abs( north ) );
    const auto ray_target = fp3d_targeting::ray( scene, 0, 0 );
    REQUIRE( ray_target );
    CHECK( ray_target->position == target->position );
    CHECK( fp3d_interaction::valid( *ray_target, you, here ) );
    CHECK( fp3d_interaction::valid( *target, you, here ) );
    const auto open = fp3d_interaction::resolve( "FP3D_INTERACT", ray_target, you, here );
    REQUIRE( open );
    CHECK( open->action == ACTION_OPEN );
    REQUIRE( open->position );
    CHECK( *open->position == north );
    CHECK_FALSE( fp3d_targeting::adjacent( scene, 0, -80 ) );
    auto remembered = scene;
    for( auto &tile : remembered.cells ) {
        tile.known = fp3d_snapshot::knowledge::remembered;
    }
    CHECK_FALSE( fp3d_targeting::adjacent( remembered, 0, 0 ) );
    auto stale = *target;
    stale.position += tripoint_rel_ms::above;
    CHECK_FALSE( fp3d_interaction::valid( stale, you, here ) );
    stale = *target;
    stale.origin += tripoint_rel_ms::east;
    CHECK_FALSE( fp3d_interaction::valid( stale, you, here ) );
    here.ter_set( north, ter_str_id( "t_door_o" ) );
    CHECK_FALSE( fp3d_interaction::valid( *target, you, here ) );
    CHECK( calendar::turn == before_time );
    CHECK( rng_get_engine() == before_rng );
    CHECK( you.get_moves() == before_moves );
}

TEST_CASE( "fp3d_quick_pickup_uses_native_activity_and_respects_closed_containers",
           "[fp3d][interaction]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
    here.furn_set( p, furn_str_id( "f_crate_c" ) );
    fp3d_snapshot::prepare_view( you, here );
    CHECK_FALSE( fp3d_interaction::quick_pickup( you, here, p ) );
    CHECK_FALSE( you.has_activity( activity_id( "ACT_PICKUP" ) ) );
    here.furn_set( p, furn_str_id( "f_null" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto before_time = calendar::turn;
    const auto target = fp3d_targeting::ray( fp3d_snapshot::capture( you, here ), 0, 55 );
    REQUIRE( target );
    const auto request = fp3d_interaction::resolve( "FP3D_PICKUP", target, you, here );
    REQUIRE( request );
    CHECK( request->quick_pickup );
    REQUIRE( fp3d_interaction::quick_pickup( you, here, p ) );
    REQUIRE( you.has_activity( activity_id( "ACT_PICKUP" ) ) );
    CHECK( calendar::turn == before_time );
    process_activity( you );
    CHECK( here.i_at( p ).empty() );
    CHECK( you.has_amount( itype_id( "rock" ), 1 ) );
}

TEST_CASE( "fp3d_hold_interact_lists_only_actions_for_the_aimed_target",
           "[fp3d][interaction][target_wheel]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    on_out_of_scope cleanup( []() { clear_npcs(); } );
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto choices_for = [&]( float pitch ) {
        fp3d_snapshot::prepare_view( you, here );
        const auto target = fp3d_targeting::ray( fp3d_snapshot::capture( you, here ), 0, pitch );
        REQUIRE( target );
        REQUIRE( target->position == here.get_abs( p ) );
        REQUIRE( fp3d_interaction::valid( *target, you, here ) );
        return fp3d_interaction::context_entries( *target, you, here );
    };
    const auto actions_of = []( const auto &choices ) {
        std::vector<action_id> actions;
        for( const auto &choice : choices ) {
            if( choice.visible && choice.enabled && choice.action != ACTION_NULL ) {
                actions.push_back( choice.action );
            }
        }
        return actions;
    };

    SECTION( "corpse exposes butchery and pickup, ordinary item exposes pickup" ) {
        here.add_item_or_charges( p, item::make_corpse( mtype_id( "mon_zombie" ), calendar::turn ) );
        const auto choices = choices_for( 55 );
        CHECK( actions_of( choices ) == std::vector<action_id>{ ACTION_PICKUP, ACTION_BUTCHER } );
        const auto corpse = fp3d_targeting::ray( fp3d_snapshot::capture( you, here ), 0, 55 );
        REQUIRE( corpse );
        CHECK( corpse->type == fp3d_targeting::kind::item );
        CHECK( corpse->id == "corpse_mon_zombie" );
        here.i_clear( p );
        here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
        CHECK_FALSE( fp3d_interaction::valid( *corpse, you, here ) );
        CHECK( actions_of( choices_for( 55 ) ) == std::vector<action_id>{ ACTION_PICKUP } );
    }
    SECTION( "a sealed crate keeps its own actions even with a corpse inside" ) {
        here.add_item_or_charges( p, item::make_corpse( mtype_id( "mon_zombie" ), calendar::turn ) );
        here.furn_set( p, furn_str_id( "f_crate_c" ) );
        const fp3d_targeting::target crate{ you.pos_abs(), here.get_abs( p ),
                                          fp3d_targeting::kind::furniture, "f_crate_c" };
        fp3d_snapshot::prepare_view( you, here );
        CHECK( actions_of( fp3d_interaction::context_entries( crate, you, here ) ) ==
               std::vector<action_id>{ ACTION_EXAMINE_AND_PICKUP } );
    }
    SECTION( "only the appropriate open or close action is shown" ) {
        here.ter_set( p, ter_str_id( "t_door_c" ) );
        const auto choices = choices_for( 0 );
        CHECK( actions_of( choices ) == std::vector<action_id>{ ACTION_OPEN, ACTION_EXAMINE_AND_PICKUP } );
        CHECK( std::none_of( choices.begin(), choices.end(), []( const auto &choice ) {
            return choice.visible && choice.action == ACTION_CLOSE;
        } ) );
        here.ter_set( p, ter_str_id( "t_door_o" ) );
        CHECK( actions_of( choices_for( 55 ) ) ==
               std::vector<action_id>{ ACTION_CLOSE, ACTION_EXAMINE_AND_PICKUP } );
    }
    SECTION( "a neutral NPC offers conversation, a monster offers melee" ) {
        npc &person = spawn_npc( p.xy(), "test_talker" );
        person.set_attitude( NPCATT_NULL );
        CHECK( actions_of( choices_for( 0 ) ) == std::vector<action_id>{ ACTION_CHAT } );
        clear_npcs();
        spawn_test_monster( "mon_zombie", p );
        const auto choices = choices_for( 0 );
        CHECK( actions_of( choices ).empty() );
        CHECK( std::any_of( choices.begin(), choices.end(), []( const auto &choice ) {
            return choice.visible && choice.enabled && choice.command &&
                   std::string( choice.command ) == "FP3D_ATTACK";
        } ) );
    }
}

TEST_CASE( "fp3d_camera_ray_selects_surfaces_and_respects_occlusion", "[fp3d][interaction][ray]" )
{
    using namespace fp3d_snapshot;
    scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 100000, -200000, 0 );
    const auto add = [&]( int x, int y, int z, terrain_kind kind = terrain_kind::floor ) -> cell & {
        cell tile;
        tile.position = scene.origin + tripoint_rel_ms( x, y, z );
        tile.terrain = kind == terrain_kind::wall ? "t_wall" : "t_floor";
        tile.kind = kind;
        scene.cells.push_back( tile );
        return scene.cells.back();
    };
    SECTION( "pitch selects a ground tile rather than the horizontal neighbour" ) {
        add( 0, 0, 0 );
        add( 0, -1, 0 );
        add( 0, -2, 0 );
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, 0 ) );
        const auto ground = fp3d_targeting::ray( scene, 0, 45 );
        REQUIRE( ground );
        CHECK( ground->position == scene.origin + tripoint_rel_ms( 0, -2, 0 ) );
        REQUIRE( ground->outline );
        CHECK( ground->outline->height == 0 );
        CHECK( ground->outline->corners.front() == std::array<float, 3>{ -.5f, 0, -2.5f } );
        const auto feet = fp3d_targeting::ray( scene, 0, 80 );
        REQUIRE( feet );
        CHECK( feet->position == scene.origin );
    }
    SECTION( "yaw and confirmed camera interpolation offset match rendered coordinates" ) {
        add( 2, 0, 0, terrain_kind::wall );
        const auto east = fp3d_targeting::ray( scene, 90, 0 );
        REQUIRE( east );
        CHECK( east->position == scene.origin + tripoint_rel_ms( 2, 0, 0 ) );
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, 0 ) );
        add( 0, -2, 0, terrain_kind::wall );
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, 0, { .8f, 0 } ) );
        REQUIRE( fp3d_targeting::ray( scene, 0, 0, { .2f, 0 } ) );
    }
    SECTION( "the nearest wall wins regardless of snapshot ordering" ) {
        add( 0, -4, 0, terrain_kind::wall );
        add( 0, -1, 0, terrain_kind::wall );
        const auto wall = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( wall );
        CHECK( wall->position == scene.origin + tripoint_rel_ms::north );
    }
    SECTION( "memory and enclosure geometry block hidden targets" ) {
        add( 0, -3, 0, terrain_kind::wall );
        auto &wall = add( 0, -1, 0, terrain_kind::wall );
        wall.known = knowledge::remembered;
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, 0 ) );
        wall.known = knowledge::visible;
        wall.exterior_only = true;
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, 0 ) );
    }
    SECTION( "a ceiling occludes the floor above" ) {
        add( 0, 0, 0 ).ceiling_present = true;
        add( 0, 0, 1 ).floor_present = false;
        const auto ceiling = fp3d_targeting::ray( scene, 0, -80 );
        REQUIRE( ceiling );
        CHECK( ceiling->position == scene.origin );
        scene.cells.front().ceiling_present = false;
        scene.cells.back().floor_present = true;
        const auto upstairs = fp3d_targeting::ray( scene, 0, -80 );
        REQUIRE( upstairs );
        CHECK( upstairs->position == scene.origin + tripoint_rel_ms::above );
    }
    SECTION( "open door aperture does not block the ray" ) {
        add( 0, -1, 0, terrain_kind::open_door );
        add( 0, -3, 0, terrain_kind::wall );
        const auto beyond = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( beyond );
        CHECK( beyond->position == scene.origin + tripoint_rel_ms( 0, -3, 0 ) );
        scene.cells.front().kind = terrain_kind::closed_door;
        const auto closed = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( closed );
        CHECK( closed->position == scene.origin + tripoint_rel_ms::north );
    }
    SECTION( "window glass is selectable but an empty opening is transparent" ) {
        add( 0, -1, 0, terrain_kind::window );
        add( 0, -3, 0, terrain_kind::wall );
        const auto glazed = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( glazed );
        CHECK( glazed->position == scene.origin + tripoint_rel_ms::north );
        scene.cells.front().pane = window_pane::opening;
        const auto opening = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( opening );
        CHECK( opening->position == scene.origin + tripoint_rel_ms( 0, -3, 0 ) );
    }
    SECTION( "glass doors remain selectable and damaged wood has an actual opening" ) {
        using shape = terrain_kind;
        auto &barrier = add( 0, -2, 0 );
        barrier.terrain = "t_door_b";
        barrier.kind = terrain_shape( barrier.terrain );
        REQUIRE( barrier.kind == shape::damaged_door );
        add( 0, -4, 0, shape::wall );
        const auto through = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( through );
        CHECK( through->position == scene.origin + tripoint_rel_ms( 0, -4, 0 ) );
        const auto lower_opening = fp3d_targeting::ray( scene, 0, 25 );
        CHECK( ( !lower_opening || lower_opening->position != scene.cells.front().position ) );
        const auto edge = fp3d_targeting::ray( scene, 0, 0, { .40f, 0 } );
        REQUIRE( edge );
        CHECK( edge->position == scene.cells.front().position );
        CHECK( terrain_shape( "t_door_locked" ) == shape::closed_door );
        CHECK( terrain_shape( "t_door_metal_locked" ) == shape::closed_door );
        CHECK( terrain_shape( "t_door_frame" ) == shape::open_door );
        CHECK( terrain_shape( "t_door_glass_o" ) == shape::open_door );
        for( const std::string id : { "t_door_glass_c", "t_wall_glass", "t_door_glass_frosted_c" } ) {
            scene.cells.front().terrain = id;
            scene.cells.front().kind = terrain_shape( id );
            REQUIRE( scene.cells.front().kind == shape::glass );
            const auto glass = fp3d_targeting::ray( scene, 0, 0 );
            REQUIRE( glass );
            CHECK( glass->position == scene.cells.front().position );
        }
    }
    SECTION( "visible sprites need a ray intersection and cannot bypass a wall" ) {
        sprite image;
        image.kind = sprite_kind::monster;
        image.position = scene.origin + tripoint_rel_ms( 0, -2, 0 );
        image.id = "mon_zombie";
        image.height = 1.8f;
        scene.sprites.push_back( image );
        auto target = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( target );
        CHECK( target->type == fp3d_targeting::kind::creature );
        CHECK_FALSE( fp3d_targeting::ray( scene, 40, 0 ) );
        add( 0, -1, 0, terrain_kind::wall );
        target = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( target );
        CHECK( target->type == fp3d_targeting::kind::terrain );
    }
    SECTION( "ground items use the visible quad and held items cannot be selected" ) {
        add( 0, -1, 0 );
        sprite image;
        image.kind = sprite_kind::item;
        image.position = scene.origin + tripoint_rel_ms::north;
        image.id = "rock";
        image.height = .3f;
        scene.sprites.push_back( image );
        const auto item = fp3d_targeting::ray( scene, 0, 55 );
        REQUIRE( item );
        CHECK( item->type == fp3d_targeting::kind::item );
        CHECK( item->id == "rock" );
        scene.sprites.front().kind = sprite_kind::held_item;
        const auto ground = fp3d_targeting::ray( scene, 0, 55 );
        REQUIRE( ground );
        CHECK( ground->type == fp3d_targeting::kind::terrain );
    }
    SECTION( "crossed vegetation is selectable above its ground" ) {
        auto &plant = add( 0, -1, 0 );
        plant.vegetation = "t_shrub";
        plant.vegetation_height = 1.2f;
        add( 0, -4, 0 );
        const auto shrub = fp3d_targeting::ray( scene, 0, 30 );
        REQUIRE( shrub );
        CHECK( shrub->position == scene.origin + tripoint_rel_ms::north );
        scene.cells.front().vegetation_height = 0;
        const auto ground = fp3d_targeting::ray( scene, 0, 30 );
        CHECK_FALSE( ground );
    }
    SECTION( "ramp slope intercepts the ray above its base" ) {
        auto &ramp = add( 0, -1, 0 );
        ramp.is_ramp = true;
        ramp.ramp_delta = 1;
        add( 0, -3, 0, terrain_kind::wall );
        const auto surface = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( surface );
        CHECK( surface->position == scene.origin + tripoint_rel_ms::north );
        REQUIRE( surface->outline );
        CHECK( surface->outline->corners.front()[1] == fp3d_camera::floor_height );
        CHECK( surface->outline->corners.back()[1] == 0 );
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, 0, { .8f, 0 } ) );
        scene.cells.front().is_ramp = false;
        const auto beyond = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( beyond );
        CHECK( beyond->position == scene.origin + tripoint_rel_ms( 0, -3, 0 ) );
    }
    SECTION( "a descending ramp uses its signed floor height" ) {
        auto &ramp = add( 0, -1, 1 );
        ramp.is_ramp = true;
        ramp.ramp_delta = -1;
        add( 0, -3, 0, terrain_kind::wall );
        const auto surface = fp3d_targeting::ray( scene, 0, 0 );
        REQUIRE( surface );
        CHECK( surface->position == scene.origin + tripoint_rel_ms( 0, -1, 1 ) );
    }
    SECTION( "stair treads span the full tile width" ) {
        add( 0, -1, 0 ).goes_up = true;
        add( 0, -1, 1 ).goes_down = true;
        add( 0, -3, 0, terrain_kind::wall );
        const auto steps = fp3d_targeting::ray( scene, 0, 0, { .25f, 0 } );
        REQUIRE( steps );
        CHECK( steps->position == scene.origin + tripoint_rel_ms::north );
        const auto side = fp3d_targeting::ray( scene, 0, 0, { -.25f, 0 } );
        REQUIRE( side );
        CHECK( side->position == scene.origin + tripoint_rel_ms::north );
    }
    SECTION( "a matched upper stair opening has no synthetic full ceiling" ) {
        auto &stairs = add( 0, 0, 0 );
        stairs.goes_up = true;
        stairs.ceiling_present = true;
        add( 0, 0, 1 ).goes_down = true;
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, -90, { .25f, .4f } ) );
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, -90, { -.25f, .4f } ) );
    }
    SECTION( "six-tile range is finite" ) {
        add( 0, -8, 0, terrain_kind::wall );
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, 0 ) );
        add( 0, -5, 0, terrain_kind::wall );
        REQUIRE( fp3d_targeting::ray( scene, 0, 0 ) );
    }
}
TEST_CASE( "fp3d_reticle_attack_selects_native_melee_or_targeted_smash",
           "[fp3d][interaction][combat]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.ter_set( p, ter_str_id( "t_window" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto target = fp3d_targeting::ray( fp3d_snapshot::capture( you, here ), 0, 0 );
    REQUIRE( target );
    REQUIRE( target->position == here.get_abs( p ) );
    const auto clock = calendar::turn;
    const auto engine = rng_get_engine();
    const int moves = you.get_moves();
    const auto smash = fp3d_interaction::resolve( "FP3D_ATTACK", target, you, here );
    REQUIRE( smash );
    CHECK( smash->action == ACTION_SMASH );
    CHECK( smash->position == p );
    CHECK_FALSE( smash->targeted_melee );
    auto stale = *target;
    stale.position += tripoint_rel_ms::north;
    CHECK_FALSE( fp3d_interaction::resolve( "FP3D_ATTACK", stale, you, here ) );
    stale = *target;
    stale.position += tripoint_rel_ms::above;
    CHECK_FALSE( fp3d_interaction::resolve( "FP3D_ATTACK", stale, you, here ) );
    stale = *target;
    stale.origin += tripoint_rel_ms::east;
    CHECK_FALSE( fp3d_interaction::resolve( "FP3D_ATTACK", stale, you, here ) );
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );

    here.ter_set( p, ter_str_id( "t_floor" ) );
    monster &zed = spawn_test_monster( "mon_zombie", p );
    fp3d_snapshot::prepare_view( you, here );
    const auto creature = fp3d_targeting::ray( fp3d_snapshot::capture( you, here ), 0, 0 );
    REQUIRE( creature );
    REQUIRE( creature->type == fp3d_targeting::kind::creature );
    const auto attack = fp3d_interaction::resolve( "FP3D_ATTACK", creature, you, here );
    REQUIRE( attack );
    CHECK( attack->targeted_melee );
    CHECK( attack->position == zed.pos_bub( here ) );
    item gun( itype_id( "glock_19" ) );
    gun.ammo_set( itype_id( "9mm" ), 10 );
    you.set_wielded_item( gun );
    const auto gun_melee = fp3d_interaction::resolve( "FP3D_ATTACK", creature, you, here );
    REQUIRE( gun_melee );
    CHECK( gun_melee->targeted_melee );
    CHECK( gun_melee->action != ACTION_FIRE );
    CHECK( you.get_wielded_item()->ammo_remaining() == 10 );
    const auto aim = fp3d_interaction::resolve( "FP3D_AIM", creature, you, here );
    REQUIRE( aim );
    CHECK( aim->action == ACTION_FIRE );
}

TEST_CASE( "fp3d_melee_cancel_protects_neutral_and_friendly_npcs_and_clears_held_attack",
           "[fp3d][interaction][combat][npc]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    on_out_of_scope cleanup( []() { clear_npcs(); } );
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 65, 64 ), "test_talker" );
    for( const auto attitude : { NPCATT_NULL, NPCATT_FOLLOW } ) {
        person.set_attitude( attitude );
        fp3d_snapshot::prepare_view( you, here );
        REQUIRE( you.sees( here, person ) );
        REQUIRE_FALSE( person.is_enemy() );
        auto &input = fp3d_input::world_gestures();
        REQUIRE( input.event( input_event( MouseInput::LeftButtonPressed, input_event_t::mouse ), true, 1000 ) );
        CHECK( input.poll( 1000 ) == "FP3D_ATTACK" );
        const int hp = person.get_hp();
        const int moves = you.get_moves();
        const int stamina = you.get_stamina();
        const auto origin = you.pos_abs();
        const auto clock = calendar::turn;
        const auto engine = rng_get_engine();
        // Native query_popup cancels in test mode; never auto-accept danger.
        CHECK_FALSE( fp3d_interaction::melee( you, here, person.pos_bub( here ) ) );
        CHECK( input.poll( 2000 ).empty() );
        CHECK( person.get_attitude() == attitude );
        CHECK( person.get_hp() == hp );
        CHECK( you.get_moves() == moves );
        CHECK( you.get_stamina() == stamina );
        CHECK( you.pos_abs() == origin );
        CHECK( calendar::turn == clock );
        CHECK( rng_get_engine() == engine );
    }
}

TEST_CASE( "fp3d_melee_attacks_hostile_npc_without_displacement_or_dialogue",
           "[fp3d][interaction][combat][npc]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    on_out_of_scope cleanup( []() { clear_npcs(); } );
    set_time_to_day();
    g->set_safe_mode( SAFE_MODE_OFF );
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    npc &person = spawn_npc( point_bub_ms( 65, 64 ), "test_talker" );
    person.set_attitude( NPCATT_KILL );
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE( you.sees( here, person ) );
    REQUIRE( person.is_enemy() );
    you.set_moves( 1000 );
    const auto origin = you.pos_abs();
    const auto position = person.pos_abs();
    const auto clock = calendar::turn;
    REQUIRE( fp3d_interaction::melee( you, here, person.pos_bub( here ) ) );
    CHECK( you.get_moves() < 1000 );
    CHECK( you.pos_abs() == origin );
    CHECK( person.pos_abs() == position );
    CHECK( calendar::turn == clock );
    CHECK_FALSE( fp3d_interaction::melee( you, here, you.pos_bub( here ) ) );
    CHECK_FALSE( fp3d_interaction::melee( you, here, tripoint_bub_ms( 65, 62, 0 ) ) );
}
TEST_CASE( "fp3d_smash_reticle_selects_furniture_and_ground_items_without_movement",
           "[fp3d][interaction][combat]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto origin = you.pos_abs();
    SECTION( "furniture" ) {
        here.furn_set( p, furn_str_id( "f_crate_c" ) );
    }
    SECTION( "ground item" ) {
        here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
    }
    fp3d_snapshot::prepare_view( you, here );
    const auto target = fp3d_targeting::ray( fp3d_snapshot::capture( you, here ), 0, 55 );
    REQUIRE( target );
    REQUIRE( target->position == here.get_abs( p ) );
    const auto engine = rng_get_engine();
    const auto attack = fp3d_interaction::resolve( "FP3D_ATTACK", target, you, here );
    REQUIRE( attack );
    CHECK( attack->action == ACTION_SMASH );
    CHECK( attack->position == p );
    CHECK_FALSE( attack->targeted_melee );
    CHECK( you.pos_abs() == origin );
    CHECK( rng_get_engine() == engine );
}

TEST_CASE( "fp3d_explicit_melee_never_redirects_stunned_input_or_attacks_an_empty_tile",
           "[fp3d][interaction][combat]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    g->set_safe_mode( SAFE_MODE_OFF );
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub( here ) + tripoint_rel_ms::north );
    monster &other = spawn_test_monster( "mon_zombie", you.pos_bub( here ) + tripoint_rel_ms::east );
    fp3d_snapshot::prepare_view( you, here );
    you.add_effect( efftype_id( "stunned" ), 1_minutes );
    you.set_moves( 1000 );
    const auto origin = you.pos_abs();
    const int other_hp = other.get_hp();
    REQUIRE( fp3d_interaction::melee( you, here, zed.pos_bub( here ) ) );
    CHECK( you.get_moves() < 1000 );
    CHECK( you.pos_abs() == origin );
    CHECK( other.get_hp() == other_hp );
    const int moves = you.get_moves();
    const auto engine = rng_get_engine();
    CHECK_FALSE( fp3d_interaction::melee( you, here, you.pos_bub( here ) + tripoint_rel_ms::south ) );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == engine );
    CHECK( you.pos_abs() == origin );
}

TEST_CASE( "fp3d_melee_cancel_also_protects_friendly_monsters", "[fp3d][interaction][combat]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    monster &pet = spawn_test_monster( "mon_zombie", you.pos_bub( here ) + tripoint_rel_ms::north );
    pet.friendly = -1;
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE( pet.attitude_to( you ) != Creature::Attitude::HOSTILE );
    const int hp = pet.get_hp();
    const int moves = you.get_moves();
    const auto origin = you.pos_abs();
    const auto position = pet.pos_abs();
    const auto engine = rng_get_engine();
    CHECK_FALSE( fp3d_interaction::melee( you, here, pet.pos_bub( here ) ) );
    CHECK( pet.get_hp() == hp );
    CHECK( pet.pos_abs() == position );
    CHECK( you.pos_abs() == origin );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == engine );
}
#endif
