#include "fp3d_input.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "input.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "vehicle.h"
#include "vpart_range.h"

// Same native physics in both modes; only the driver's command translation differs.
// This deliberately runs vehmove/idle, not a synthetic velocity calculation.
TEST_CASE( "fp3d_driving_physics_comparison_export", "[.fp3d_driving_compare]" )
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
    for( const std::string scenario : {
             "car-road", "car-wall", "bicycle-road"
         } ) {
        CAPTURE( scenario, mode );
        clear_avatar();
        clear_map();
        clear_vehicles();
        set_time_to_day();
        avatar &you = get_avatar();
        map &here = get_map();
        for( int y = 35; y <= 95; ++y ) {
            for( int x = 35; x <= 110; ++x ) {
                here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_str_id( "t_pavement" ) );
            }
        }
        const bool wall = scenario == "car-wall";
        if( wall ) {
            for( int y = 50; y <= 80; ++y ) {
                here.ter_set( tripoint_bub_ms( 75, y, 0 ), ter_str_id( "t_concrete_wall" ) );
            }
        }
        vehicle *veh = here.add_vehicle( vproto_id( scenario == "bicycle-road" ? "bicycle" : "car" ),
                                         tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 100, 0 );
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
        const auto start = veh->pos_abs();
        const auto clock = calendar::turn;
        const int initial_fuel = veh->fuel_left( here, itype_id( "gasoline" ) );
        const auto total_hp = [&]() {
            int result = 0;
            for( const vpart_reference &part : veh->get_all_parts() ) {
                result += part.part().hp();
            }
            return result;
        };
        const int initial_hp = total_hp();
        int distance = 0;
        int peak_velocity = 0;
        rng_set_engine_seed( 424242 );
        json.start_object();
        json.member( "scenario", scenario );
        json.member( "trace" );
        json.start_array();
        for( int turn = 0; turn < 12; ++turn ) {
            CAPTURE( turn );
            const char key = turn < 2 ? 'w' : turn == 4 && !wall ? 'd' : turn == 5 && !wall ? 'a' :
                             turn >= 8 && turn <= 9 && !wall ? 's' : '.';
            you.set_moves( 100 );
            const auto before_rng = rng_get_engine();
            if( key != '.' && you.controlling_vehicle && you.in_vehicle ) {
                point_rel_ms delta( key == 'a' ? -1 : key == 'd' ? 1 : 0,
                                    key == 'w' ? -1 : key == 's' ? 1 : 0 );
                if( modern ) {
                    const auto command = look_up_action( fp3d_input::resolve(
                            fp3d_input::mode::first_person_3d,
                            input_event( key, input_event_t::keyboard_code ), 135,
                            fp3d_input::vehicle_controls() ) );
                    delta = get_delta_from_movement_action( command, iso_rotate::no );
                }
                CHECK( rng_get_engine() == before_rng );
                CHECK( you.get_moves() == 100 );
                veh->pldrive( here, you, delta.x(), delta.y() );
            }
            const int command_moves = 100 - you.get_moves();
            // Explicit fixture clock: this is a physics integration check, not do_turn/UI acceptance.
            calendar::turn += 1_seconds;
            const auto before = veh->pos_abs();
            here.vehmove();
            veh->idle( here, true );
            distance += square_dist( before, veh->pos_abs() );
            peak_velocity = std::max( peak_velocity, std::abs( veh->velocity ) );
            json.start_object();
            json.member( "turn", turn );
            json.member( "key", std::string( 1, key ) );
            json.member( "command_moves", command_moves );
            json.member( "x", veh->pos_abs().x() );
            json.member( "y", veh->pos_abs().y() );
            json.member( "velocity", veh->velocity );
            json.member( "cruise_velocity", veh->cruise_velocity );
            json.member( "gasoline", veh->fuel_left( here, itype_id( "gasoline" ) ) );
            json.member( "battery", veh->fuel_left( here, itype_id( "battery" ) ) );
            json.member( "vehicle_hp", total_hp() );
            json.member( "player_hp", you.get_hp() );
            json.member( "stamina", you.get_stamina() );
            json.member( "in_vehicle", you.in_vehicle );
            json.end_object();
            REQUIRE( here.inbounds( veh->pos_bub( here ) ) );
        }
        json.end_array();
        CHECK( distance > 0 );
        CHECK( peak_velocity > 0 );
        if( scenario != "bicycle-road" ) {
            CHECK( veh->fuel_left( here, itype_id( "gasoline" ) ) < initial_fuel );
        }
        if( wall ) {
            CHECK( total_hp() < initial_hp );
        }
        std::ostringstream engine;
        engine << rng_get_engine();
        json.member( "distance", distance );
        json.member( "displacement", square_dist( start, veh->pos_abs() ) );
        json.member( "peak_velocity", peak_velocity );
        json.member( "fuel_used", initial_fuel - veh->fuel_left( here, itype_id( "gasoline" ) ) );
        json.member( "damage", initial_hp - total_hp() );
        json.member( "elapsed_seconds", to_seconds<int>( calendar::turn - clock ) );
        json.member( "rng", engine.str() );
        json.end_object();
        if( you.in_vehicle ) {
            here.unboard_vehicle( you.pos_bub( here ) );
        }
        you.controlling_vehicle = false;
    }
    json.end_array();
    output.flush();
    REQUIRE( output.good() );
}
#endif
