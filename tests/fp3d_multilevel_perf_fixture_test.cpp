#include "fp3d_snapshot.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "game.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "worldfactory.h"
#include "mapdata.h"

// A private native save for real-client performance sampling, not a synthetic
// scene draw. Movement and all subsequent world turns remain native.
TEST_CASE( "fp3d_export_three_storey_native_performance_world",
           "[.fp3d_multilevel_perf_fixture]" )
{
    const char *path = std::getenv( "CDDA_FP3D_MULTILEVEL_EXPORT" );
    REQUIRE( path );
    const auto destination = std::filesystem::u8path( path );
    REQUIRE( destination.is_absolute() );
    REQUIRE( destination.lexically_normal().generic_u8string().find( "/qa/0i1/" ) !=
             std::string::npos );
    REQUIRE_FALSE( std::filesystem::exists( destination ) );
    const char *requested = std::getenv( "CDDA_FP3D_MULTILEVEL_SCENARIO" );
    const std::string scenario = requested ? requested : "outdoor";
    REQUIRE( ( scenario == "outdoor" || scenario == "indoor" || scenario == "stairs" ) );
    clear_avatar();
    clear_map( -2, 4 );
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    REQUIRE( here.supports_zlevels() );
    const tripoint_bub_ms start = scenario == "outdoor" ? tripoint_bub_ms( 65, 65, 0 ) :
                                  scenario == "indoor" ? tripoint_bub_ms( 65, 49, 1 ) :
                                  tripoint_bub_ms( 60, 52, 0 );
    you.abort_automove();
    you.cancel_activity();
    you.omt_path.clear();
    for( const char *id : {
             "t_floor", "t_wall", "t_window", "t_door_c", "t_flat_roof",
             "t_stairs_up", "t_stairs_down", "t_pavement"
         } ) {
        REQUIRE( ter_str_id( id ).is_valid() );
    }
    REQUIRE( here.inbounds( tripoint_bub_ms( 57, 41, 0 ) ) );
    REQUIRE( here.inbounds( tripoint_bub_ms( 73, 57, 3 ) ) );
    for( int z = 0; z < 3; ++z ) {
        for( int y = 41; y <= 57; ++y ) {
            for( int x = 57; x <= 73; ++x ) {
                const bool wall = x == 57 || x == 73 || y == 41 || y == 57;
                here.ter_set( tripoint_bub_ms( x, y, z ), ter_str_id( wall ? "t_wall" : "t_floor" ) );
            }
        }
        for( int x : {
                 59, 62, 65, 68, 71
             } ) {
            here.ter_set( tripoint_bub_ms( x, 57, z ), ter_str_id( "t_window" ) );
        }
    }
    here.ter_set( tripoint_bub_ms( 65, 57, 0 ), ter_str_id( "t_door_c" ) );
    here.ter_set( tripoint_bub_ms( 60, 52, 0 ), ter_str_id( "t_stairs_up" ) );
    here.ter_set( tripoint_bub_ms( 60, 52, 1 ), ter_str_id( "t_stairs_down" ) );
    here.ter_set( tripoint_bub_ms( 62, 52, 1 ), ter_str_id( "t_stairs_up" ) );
    here.ter_set( tripoint_bub_ms( 62, 52, 2 ), ter_str_id( "t_stairs_down" ) );
    for( int y = 41; y <= 57; ++y ) {
        for( int x = 57; x <= 73; ++x ) {
            here.ter_set( tripoint_bub_ms( x, y, 3 ), ter_str_id( "t_flat_roof" ) );
        }
    }
    you.setpos( here, start );
    if( start.z() != here.get_abs_sub().z() ) {
        g->vertical_shift( start.z() );
    }
    REQUIRE( here.get_abs_sub().z() == start.z() );
    const auto first = start + ( scenario == "stairs" ? tripoint_rel_ms::above :
                                 tripoint_rel_ms::north );
    const auto second = scenario == "stairs" ? start : first + tripoint_rel_ms::east;
    if( scenario == "outdoor" ) {
        here.ter_set( start, ter_str_id( "t_pavement" ) );
        here.ter_set( first, ter_str_id( "t_pavement" ) );
        here.ter_set( second, ter_str_id( "t_pavement" ) );
    } else if( scenario == "stairs" ) {
        REQUIRE( here.has_flag( ter_furn_flag::TFLAG_GOES_UP, start ) );
        REQUIRE( here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, first ) );
    }
    std::vector<tripoint_bub_ms> route;
    route.reserve( 60001 );
    route.push_back( first );
    for( int step = 0; step < 60000; ++step ) {
        route.push_back( step % 2 == 0 ? second : first );
    }
    you.set_destination( route );
    you.set_moves( 100 );
    fp3d_snapshot::prepare_view( you, here );
    const auto time = calendar::turn;
    const auto engine = rng_get_engine();
    const auto scene = fp3d_snapshot::capture( you, here );
    std::array<int, 3> facade{};
    bool covered_origin = false;
    for( const auto &tile : scene.cells ) {
        const auto p = here.get_bub( tile.position );
        if( p == start ) {
            covered_origin = tile.ceiling_present && tile.floor_present &&
                             tile.known == fp3d_snapshot::knowledge::visible;
        }
        if( p.y() == 57 && p.x() >= 57 && p.x() <= 73 && p.z() >= 0 && p.z() < 3 &&
            tile.kind == fp3d_snapshot::terrain_kind::wall ) {
            ++facade[p.z()];
        }
    }
    CAPTURE( facade );
    if( scenario == "outdoor" ) {
        for( int count : facade ) {
            REQUIRE( count > 0 );
        }
    } else if( scenario == "indoor" ) {
        REQUIRE( covered_origin );
        REQUIRE_FALSE( here.is_outside( start ) );
    }
    CHECK( calendar::turn == time );
    CHECK( rng_get_engine() == engine );
    CHECK( you.pos_bub( here ) == start );
    CHECK( you.get_moves() == 100 );
    REQUIRE( you.get_auto_move_route().size() == 60001 );
    REQUIRE( g->save() );
    REQUIRE( world_generator->active_world );
    std::filesystem::copy( world_generator->active_world->folder_path().get_unrelative_path(),
                           destination, std::filesystem::copy_options::recursive );
    REQUIRE( std::filesystem::exists( destination / "mods.json" ) );
    REQUIRE( std::filesystem::exists( destination / "worldoptions.json" ) );
    std::ofstream output( destination.parent_path() / "fixture.json" );
    REQUIRE( output.good() );
    JsonOut json( output );
    json.start_object();
    json.member( "scenario", "native_three_storey_building_" + scenario + "_walk" );
    json.member( "covered_origin", covered_origin );
    json.member( "storeys", 3 );
    json.member( "saved_level", here.get_abs_sub().z() );
    json.member( "facade_cells", facade );
    json.member( "snapshot_cells", scene.cells.size() );
    json.member( "route_steps", route.size() );
    json.member( "start", you.pos_abs() );
    json.member( "first", here.get_abs( first ) );
    json.member( "second", here.get_abs( second ) );
    json.member( "turn", to_turn<int>( calendar::turn ) );
    json.end_object();
}
#endif
