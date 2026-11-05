#include "fp3d_construction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "construction.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "item.h"
#include "options_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "rng.h"
#include "skill.h"

TEST_CASE( "fp3d_construction_ghost_uses_completed_terrain_without_building_it",
           "[fp3d][construction][blueprint]" )
{
    const auto &you = get_avatar();
    const auto &here = get_map();
    const auto position = you.pos_abs();
    const auto original = here.ter( here.get_bub( position ) );
    const auto time = calendar::turn;
    const auto moves = you.get_moves();
    const auto engine = rng_get_engine();
    using kind = fp3d_snapshot::terrain_kind;
    for( const auto &entry : {
             std::pair{ "t_floor", kind::floor },
             std::pair{ "t_wall_wood", kind::wall },
             std::pair{ "t_door_c", kind::closed_door },
             std::pair{ "t_door_o", kind::open_door },
             std::pair{ "t_window", kind::window },
             std::pair{ "t_stairs_up", kind::floor },
             std::pair{ "t_wood_stairs_up", kind::floor },
             std::pair{ "t_wood_stairs_up_half", kind::floor }
         } ) {
        CAPTURE( entry.first );
        construction project{};
        project.post_terrain = entry.first;
        const auto ghost = fp3d_construction::terrain_preview( project, position );
        REQUIRE( ghost );
        CHECK( ghost->terrain == entry.first );
        CHECK( ghost->position == position );
        CHECK( ghost->kind == entry.second );
        CHECK( ghost->known == fp3d_snapshot::knowledge::visible );
        CHECK_FALSE( ghost->exterior_only );
        CHECK_FALSE( ghost->ceiling_present );
        CHECK( ghost->floor_present );
        CHECK( ghost->goes_up == ( std::string( entry.first ) == "t_stairs_up" ||
                                   std::string( entry.first ) == "t_wood_stairs_up" ) );
    }
    for( const std::string id : {
             "t_open_air", "t_stairs_down", "t_tree",
             "not_a_terrain", "t_null", ""
         } ) {
        CAPTURE( id );
        construction project{};
        project.post_terrain = id;
        CHECK_FALSE( fp3d_construction::terrain_preview( project, position ) );
    }
    construction furniture{};
    furniture.post_is_furniture = true;
    for( const std::string id : { "f_washer", "f_dryer", "f_dishwasher", "f_woodstove" } ) {
        CAPTURE( id );
        furniture.post_terrain = id;
        const auto ghost = fp3d_construction::terrain_preview( furniture, position );
        REQUIRE( ghost );
        REQUIRE( ghost->furniture );
        CHECK( ghost->furniture->id == id );
        CHECK( ghost->position == position );
    }
    furniture.post_terrain = "f_counter_gate_c";
    CHECK_FALSE( fp3d_construction::terrain_preview( furniture, position ) );
    bool frame = false;
    bool completed = false;
    for( const construction &project : get_constructions() ) {
        if( project.group != construction_group_str_id( "build_wooden_staircase" ) ) {
            continue;
        }
        if( project.post_terrain == "t_wood_stairs_up_half" ||
            project.post_terrain == "t_wood_stairs_up" ) {
            const auto ghost = fp3d_construction::terrain_preview( project, position );
            REQUIRE( ghost );
            CHECK( ghost->goes_up == ( project.post_terrain == "t_wood_stairs_up" ) );
            frame = frame || project.post_terrain == "t_wood_stairs_up_half";
            completed = completed || project.post_terrain == "t_wood_stairs_up";
        }
    }
    CHECK( frame );
    CHECK( completed );
    CHECK( here.ter( here.get_bub( position ) ) == original );
    CHECK( you.pos_abs() == position );
    CHECK( calendar::turn == time );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == engine );
}

TEST_CASE( "fp3d_excavation_planning_symbol_does_not_predict_hidden_levels",
           "[fp3d][construction][blueprint]" )
{
    const auto &you = get_avatar();
    const auto &here = get_map();
    const auto p = you.pos_bub( here );
    const auto current = here.ter( p );
    const auto lower = here.ter( p + tripoint_rel_ms::below );
    const auto upper = here.ter( p + tripoint_rel_ms::above );
    const auto time = calendar::turn;
    const auto moves = you.get_moves();
    const auto engine = rng_get_engine();
    for( const auto &entry : {
             std::pair{ "constr_dig_downstair", -1 },
             std::pair{ "constr_mine_downstair", -1 },
             std::pair{ "constr_mine_upstair", 1 }
         } ) {
        const auto project = std::find_if( get_constructions().begin(), get_constructions().end(),
        [&]( const construction & value ) {
            return value.str_id.str() == entry.first;
        } );
        REQUIRE( project != get_constructions().end() );
        for( const bool allowed : {
                 false, true
             } ) {
            const auto preview = fp3d_construction::placement_preview( *project, you.pos_abs(), allowed );
            CHECK( preview.position == you.pos_abs() );
            CHECK( preview.valid == allowed );
            CHECK( preview.vertical_hint == entry.second );
            CHECK_FALSE( preview.terrain );
            CHECK( preview.name.find( "完工结果由原版决定" ) != std::string::npos );
        }
    }
    const auto regular = std::find_if( get_constructions().begin(), get_constructions().end(),
    []( const construction & value ) {
        return value.post_terrain == "f_cupboard";
    } );
    REQUIRE( regular != get_constructions().end() );
    const auto preview = fp3d_construction::placement_preview( *regular, you.pos_abs(), true );
    CHECK( preview.vertical_hint == 0 );
    CHECK( preview.terrain );
    CHECK( here.ter( p ) == current );
    CHECK( here.ter( p + tripoint_rel_ms::below ) == lower );
    CHECK( here.ter( p + tripoint_rel_ms::above ) == upper );
    CHECK( calendar::turn == time );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == engine );
}

// Committed native activity fixture, not initial placement or full do_turn.
TEST_CASE( "fp3d_concrete_ramp_ghost_and_native_completion_keep_upper_links_separate",
           "[fp3d][construction][blueprint]" )
{
    override_option scaling( "CONSTRUCTION_SCALING", "100" );
    for( const bool high : {
             false, true
         } ) {
        CAPTURE( high );
        clear_avatar();
        clear_map();
        clear_npcs();
        set_time_to_day();
        avatar &you = get_avatar();
        map &here = get_map();
        you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
        you.set_skill_level( skill_id( "fabrication" ), 3 );
        const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
        const auto above = p + tripoint_rel_ms::above;
        here.ter_set( above, ter_str_id( "t_open_air" ) );
        const auto id = construction_str_id( high ? "constr_concrete_ramp_high" :
                                             "constr_concrete_ramp_low" );
        const auto &projects = get_constructions();
        const auto project = std::find_if( projects.begin(),
        projects.end(), [&]( const construction & entry ) {
            return entry.str_id == id;
        } );
        REQUIRE( project != projects.end() );
        const auto engine = rng_get_engine();
        const auto clock = calendar::turn;
        const auto moves = you.get_moves();
        const auto original = here.ter( p );
        const auto ghost = fp3d_construction::terrain_preview( *project, here.get_abs( p ) );
        REQUIRE( ghost );
        CHECK( ghost->terrain == ( high ? "t_ramp_up_high" : "t_ramp_up_low" ) );
        CHECK( ghost->is_ramp == high );
        CHECK( ghost->ramp_delta == ( high ? 1 : 0 ) );
        CHECK_FALSE( ghost->goes_up ); // RAMP_UP is distinct from native GOES_UP.
        CHECK( here.ter( p ) == original );
        CHECK( here.ter( above ) == ter_str_id( "t_open_air" ) );
        CHECK( rng_get_engine() == engine );
        CHECK( calendar::turn == clock );
        CHECK( you.get_moves() == moves );
        partial_con partial;
        partial.id = project->id;
        for( int i = 0; i < 5; ++i ) {
            partial.components.emplace_back( itype_id( "concrete" ) );
        }
        partial.components.emplace_back( itype_id( "water" ), calendar::turn, 5 );
        here.partial_con_set( p, partial );
        you.assign_activity( activity_id( "ACT_BUILD" ) );
        you.activity.placement = here.get_abs( p );
        int ticks = 0;
        while( you.activity && ticks < 5000 ) {
            you.set_moves( 1000 );
            you.activity.do_turn( you );
            calendar::turn += 10_seconds;
            ++ticks;
        }
        CHECK( ticks > 1 );
        CHECK( ticks < 5000 );
        CHECK( you.activity.is_null() );
        CHECK_FALSE( here.partial_con_at( p ) );
        CHECK( here.ter( p ).id().str() == ghost->terrain );
        CHECK( here.ter( above ) == ter_str_id( high ? "t_ramp_down_high" : "t_ramp_down_low" ) );
        fp3d_snapshot::prepare_view( you, here );
        const auto scene = fp3d_snapshot::capture( you, here );
        const auto tile = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & entry ) {
            return entry.position == here.get_abs( p );
        } );
        REQUIRE( tile != scene.cells.end() );
        CHECK( tile->terrain == ghost->terrain );
        CHECK( tile->is_ramp == ghost->is_ramp );
        CHECK( tile->ramp_delta == ghost->ramp_delta );
    }
}
#endif
