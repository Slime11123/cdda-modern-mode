#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "avatar.h"
#include "activity_actor_definitions.h"
#include "fp3d_input.h"
#include "input.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "fp3d_snapshot.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "player_helpers.h"
#include "rng.h"
#include "vehicle.h"
#include "vpart_range.h"
#include "vpart_position.h"

// Run native and modern modes in separate fresh processes: normal_roll also
// caches a distribution value, so resetting just the engine is insufficient.
TEST_CASE( "fp3d_native_melee_comparison_export", "[.fp3d_combat_compare]" )
{
    const char *mode = std::getenv( "CDDA_FP3D_COMBAT_MODE" );
    const char *report = std::getenv( "CDDA_FP3D_COMBAT_REPORT" );
    REQUIRE( mode );
    REQUIRE( report );
    const bool modern = std::string( mode ) == "modern";
    REQUIRE( ( modern || std::string( mode ) == "native" ) );
    std::ofstream output( report );
    REQUIRE( output.good() );
    JsonOut json( output );
    json.start_array();
    for( const std::string weapon_id : {
             "", "2x4", "hammer"
         } ) {
        for( const unsigned seed : {
                 424242U, 737373U, 20261029U
             } ) {
            CAPTURE( weapon_id, seed, mode );
            clear_avatar();
            clear_map();
            set_time_to_day();
            g->set_safe_mode( SAFE_MODE_OFF );
            avatar &you = get_avatar();
            map &here = get_map();
            you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
            if( !weapon_id.empty() ) {
                you.set_wielded_item( item( itype_id( weapon_id ) ) );
            }
            const auto origin = you.pos_bub( here );
            monster &zed = spawn_test_monster( "mon_zombie", origin + tripoint_rel_ms::north );
            fp3d_snapshot::prepare_view( you, here );
            const auto target = fp3d_targeting::adjacent( fp3d_snapshot::capture( you, here ), 0, 0 );
            REQUIRE( target );
            REQUIRE( target->position == zed.pos_abs() );
            REQUIRE( you.sees( here, zed ) );
            you.set_moves( 1000 );
            zed.set_moves( 1000 );
            rng_set_engine_seed( seed );
            const auto time_before = calendar::turn;
            const auto rng_before = rng_get_engine();
            tripoint_rel_ms delta = tripoint_rel_ms::north;
            if( modern ) {
                const auto request = fp3d_interaction::resolve( "FP3D_ATTACK", target, you, here );
                REQUIRE( request );
                REQUIRE( request->position == zed.pos_bub( here ) );
                REQUIRE( request->targeted_melee );
                delta = *request->position - you.pos_bub( here );
            }
            CHECK( delta == tripoint_rel_ms::north );
            CHECK( you.get_moves() == 1000 );
            CHECK( rng_get_engine() == rng_before );
            const int target_hp = zed.get_hp();
            bool moved = false;
            if( modern ) {
                REQUIRE( fp3d_interaction::melee( you, here, zed.pos_bub( here ) ) );
            } else {
                moved = avatar_action::move( you, here, delta );
            }
            const int attack_moves = 1000 - you.get_moves();
            CHECK( attack_moves > 0 );
            CHECK( you.pos_bub( here ) == origin );
            REQUIRE( zed.get_hp() > 0 );
            const bool counterattack = zed.melee_attack( you );
            CHECK( zed.get_moves() < 1000 );
            CHECK( calendar::turn == time_before );
            std::ostringstream engine;
            engine << rng_get_engine();
            json.start_object();
            json.member( "weapon", weapon_id );
            json.member( "seed", seed );
            json.member( "move_result", moved );
            json.member( "attack_moves", attack_moves );
            json.member( "target_damage", target_hp - zed.get_hp() );
            json.member( "counterattack", counterattack );
            json.member( "counterattack_moves", 1000 - zed.get_moves() );
            json.member( "player_moves", you.get_moves() );
            json.member( "stamina", you.get_stamina() );
            json.member( "pain", you.get_pain() );
            json.member( "weapon_damage", you.get_wielded_item() ? you.get_wielded_item()->damage() : 0 );
            json.member( "body_hp" );
            json.start_object();
            for( const bodypart_id &bp : you.get_all_body_parts() ) {
                json.member( bp.id().str(), you.get_part_hp_cur( bp ) );
            }
            json.end_object();
            json.member( "rng", engine.str() );
            json.end_object();
        }
    }
    for( const unsigned seed : {
             424242U, 737373U, 20261029U
         } ) {
        CAPTURE( seed, mode );
        clear_avatar();
        clear_map();
        set_time_to_day();
        avatar &you = get_avatar();
        map &here = get_map();
        you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
        you.wear_item( item( itype_id( "backpack" ) ), false );
        item gun( itype_id( "glock_19" ) );
        gun.ammo_set( itype_id( "9mm" ), 10 );
        you.set_wielded_item( gun );
        auto weapon = you.get_wielded_item();
        monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub( here ) + tripoint_rel_ms( 0, -5,
                                           0 ) );
        fp3d_snapshot::prepare_view( you, here );
        REQUIRE( you.sees( here, zed ) );
        rng_set_engine_seed( seed );
        you.set_moves( 1000 );
        const auto engine_before = rng_get_engine();
        if( modern ) {
            const auto request = fp3d_interaction::resolve( "FP3D_AIM", std::nullopt, you, here );
            REQUIRE( request );
            REQUIRE( request->action == ACTION_FIRE );
            CHECK_FALSE( request->position );
        }
        CHECK( rng_get_engine() == engine_before );
        avatar_action::fire_wielded_weapon( you );
        REQUIRE( you.has_activity( activity_id( "ACT_AIM" ) ) );
        you.cancel_activity();
        you.recoil = 0;
        const int hp_before = zed.get_hp();
        const int shots = you.fire_gun( here, zed.pos_bub( here ), 1, *weapon );
        REQUIRE( shots == 1 );
        CHECK( weapon->ammo_remaining() == 9 );
        const int fire_moves = 1000 - you.get_moves();
        CHECK( fire_moves > 0 );
        auto ammo = you.i_add( item( itype_id( "9mm" ), calendar::turn, 20 ) );
        REQUIRE( ammo );
        item *magazine = weapon->magazine_current();
        REQUIRE( magazine );
        if( modern ) {
            CHECK( fp3d_input::resolve( fp3d_input::mode::first_person_3d,
                                        input_event( 'r', input_event_t::keyboard_code ), 0 ) ==
                   action_ident( ACTION_RELOAD_WIELDED ) );
        }
        item::reload_option option( &you, item_location( weapon, magazine ), ammo );
        REQUIRE( option );
        const int reload_moves = option.moves();
        const int quantity = option.qty();
        you.assign_activity( reload_activity_actor( std::move( option ) ) );
        REQUIRE( you.has_activity( activity_id( "ACT_RELOAD" ) ) );
        process_activity( you, true );
        CHECK( weapon->ammo_remaining() == 9 + quantity );
        CHECK( ammo->charges == 20 - quantity );
        std::ostringstream engine;
        engine << rng_get_engine();
        json.start_object();
        json.member( "weapon", "glock_19" );
        json.member( "seed", seed );
        json.member( "shots", shots );
        json.member( "attack_moves", fire_moves );
        json.member( "target_damage", hp_before - zed.get_hp() );
        json.member( "reload_moves", reload_moves );
        json.member( "reloaded", quantity );
        json.member( "ammo_remaining", weapon->ammo_remaining() );
        json.member( "reserve", ammo->charges );
        json.member( "rng", engine.str() );
        json.end_object();
    }
    for( const std::string prototype : {
             "bicycle", "car"
         } ) {
        for( const int yaw : {
                 0, 90, 225
             } ) {
            for( const char key : {
                     'w', 's', 'a', 'd'
                 } ) {
                CAPTURE( prototype, yaw, key, mode );
                clear_map();
                clear_vehicles();
                clear_avatar();
                avatar &you = get_avatar();
                map &here = get_map();
                vehicle *veh = here.add_vehicle( vproto_id( prototype ), tripoint_bub_ms( 65, 65, 0 ),
                                                 0_degrees, 100, 0 );
                REQUIRE( veh );
                bool boarded = false;
                for( const vpart_reference &part : veh->get_avail_parts( "CONTROLS" ) ) {
                    here.board_vehicle( part.pos_bub( here ), &you );
                    boarded = true;
                    break;
                }
                REQUIRE( boarded );
                you.controlling_vehicle = true;
                REQUIRE( fp3d_input::vehicle_controls() );
                veh->engine_on = true;
                you.set_moves( 100 );
                rng_set_engine_seed( 424242 );
                const auto clock = calendar::turn;
                const auto engine = rng_get_engine();
                point_rel_ms delta( key == 'a' ? -1 : key == 'd' ? 1 : 0,
                                    key == 'w' ? -1 : key == 's' ? 1 : 0 );
                if( modern ) {
                    const auto action = look_up_action( fp3d_input::resolve(
                                                            fp3d_input::mode::first_person_3d,
                                                            input_event( key, input_event_t::keyboard_code ), yaw,
                                                            fp3d_input::vehicle_controls() ) );
                    CHECK( get_delta_from_movement_action( action, iso_rotate::no ) == delta );
                    delta = get_delta_from_movement_action( action, iso_rotate::no );
                }
                CHECK( you.get_moves() == 100 );
                CHECK( calendar::turn == clock );
                CHECK( rng_get_engine() == engine );
                veh->pldrive( here, you, delta.x(), delta.y() );
                CHECK( calendar::turn == clock );
                std::ostringstream final_engine;
                final_engine << rng_get_engine();
                json.start_object();
                json.member( "vehicle", prototype );
                json.member( "yaw", yaw );
                json.member( "key", std::string( 1, key ) );
                json.member( "player_moves", you.get_moves() );
                json.member( "velocity", veh->velocity );
                json.member( "cruise_velocity", veh->cruise_velocity );
                json.member( "turn_degrees", units::to_degrees( veh->turn_dir ) );
                json.member( "gasoline", veh->fuel_left( here, itype_id( "gasoline" ) ) );
                json.member( "battery", veh->fuel_left( here, itype_id( "battery" ) ) );
                json.member( "rng", final_engine.str() );
                json.end_object();
                here.unboard_vehicle( you.pos_bub( here ) );
                you.controlling_vehicle = false;
            }
        }
    }
    json.end_array();
    output.flush();
    REQUIRE( output.good() );
}
TEST_CASE( "fp3d_gun_intents_defer_shots_to_native_aim_activity", "[fp3d][combat]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    map &here = get_map();
    item gun( itype_id( "glock_19" ) );
    gun.ammo_set( itype_id( "9mm" ), 10 );
    you.set_wielded_item( gun );
    REQUIRE( you.get_wielded_item()->ammo_remaining() == 10 );
    you.set_moves( 1000 );
    const auto clock = calendar::turn;
    CHECK_FALSE( fp3d_interaction::resolve( "FP3D_ATTACK", std::nullopt, you, here ) );
    for( const std::string command : { "FP3D_AIM" } ) {
        CAPTURE( command );
        const auto engine = rng_get_engine();
        const auto request = fp3d_interaction::resolve( command, std::nullopt, you, here );
        REQUIRE( request );
        CHECK( request->action == ACTION_FIRE );
        CHECK_FALSE( request->position );
        CHECK( rng_get_engine() == engine );
        avatar_action::fire_wielded_weapon( you );
        CHECK( you.has_activity( activity_id( "ACT_AIM" ) ) );
        CHECK( you.get_wielded_item()->ammo_remaining() == 10 );
        CHECK( you.get_moves() == 1000 );
        CHECK( calendar::turn == clock );
        you.cancel_activity();
        CHECK_FALSE( you.has_activity( activity_id( "ACT_AIM" ) ) );
        CHECK( you.get_wielded_item()->ammo_remaining() == 10 );
        CHECK( you.get_moves() == 1000 );
    }
}
#endif
