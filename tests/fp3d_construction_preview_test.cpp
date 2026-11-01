#include "fp3d_construction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "construction.h"
#include "fp3d_snapshot.h"
#include "fp3d_targeting.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "skill.h"
#include "scoped_rng.h"

TEST_CASE( "fp3d_construction_preview_rechecks_native_requirements_without_side_effects",
           "[fp3d][construction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto &projects = get_constructions();
    const auto project = std::find_if( projects.begin(),
    projects.end(), []( const construction & entry ) {
        return entry.str_id == construction_str_id( "constr_wall_log" );
    } );
    REQUIRE( project != projects.end() );
    const auto position = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.ter_set( position, ter_str_id( "t_wall_log_half" ) );
    you.set_skill_level( skill_id( "fabrication" ), 3 );
    for( int n = 0; n < 2; ++n ) {
        here.add_item_or_charges( you.pos_bub( here ), item( itype_id( "log" ) ) );
    }
    for( int n = 0; n < 3; ++n ) {
        here.add_item_or_charges( you.pos_bub( here ), item( itype_id( "stick" ) ) );
    }
    here.add_item_or_charges( you.pos_bub( here ), item( itype_id( "shovel" ) ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here );
    auto target = fp3d_targeting::adjacent( scene, 0, 0 );
    REQUIRE( target );
    REQUIRE( target->position == here.get_abs( position ) );
    bool expected = false;
    SECTION( "real native materials tools skill and terrain" ) {
        expected = true;
    }
    SECTION( "knowledge changed before confirmation" ) {
        you.set_knowledge_level( skill_id( "fabrication" ), 0 );
    }
    SECTION( "native construction accepts knowledge despite low practical skill" ) {
        you.set_skill_level( skill_id( "fabrication" ), 0 );
        expected = true;
    }
    SECTION( "materials removed before confirmation" ) {
        here.i_clear( you.pos_bub( here ) );
    }
    SECTION( "terrain changed before confirmation" ) {
        here.ter_set( position, ter_str_id( "t_floor" ) );
    }
    SECTION( "target is now too far" ) {
        target->position += tripoint_rel_ms( 0, -3, 0 );
    }
    SECTION( "target is on another floor" ) {
        target->position += tripoint_rel_ms::above;
    }
    SECTION( "player origin no longer matches" ) {
        target->origin += tripoint_rel_ms::east;
    }
    you.invalidate_crafting_inventory();
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const int moves = you.get_moves();
    const auto terrain = here.ter( position );
    const auto items = here.i_at( you.pos_bub( here ) ).size();
    double expected_normal;
    {
        scoped_rng_restore preserve;
        expected_normal = normal_roll( 12, 3 );
    }
    CAPTURE( you.get_knowledge_level( skill_id( "fabrication" ) ), items, terrain );
    const bool allowed = fp3d_construction::preview_allowed( you, here, *project, *target );
    CHECK( allowed == expected );
    bool stable = true;
    for( int frame = 0; frame < 30; ++frame ) {
        const bool current = fp3d_construction::preview_allowed( you, here, *project, *target );
        stable = stable && current == expected;
    }
    CHECK( stable );
    if( items == 0 ) {
        CHECK_FALSE( fp3d_construction::begin( project->group ) );
    }
    CHECK( rng_get_engine() == engine );
    CHECK( normal_roll( 12, 3 ) == expected_normal );
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );
    CHECK( here.ter( position ) == terrain );
    CHECK( here.i_at( you.pos_bub( here ) ).size() == items );
    CHECK_FALSE( here.partial_con_at( position ) );
    CHECK( you.activity.is_null() );
}

TEST_CASE( "fp3d_construction_details_preserve_native_normal_samples", "[fp3d][construction]" )
{
    const auto &you = get_avatar();
    const auto clock = calendar::turn;
    const int moves = you.get_moves();
    const auto projects = fp3d_construction::catalogue();
    REQUIRE_FALSE( projects.empty() );
    for( const auto &project : projects ) {
        CAPTURE( project.id );
        const auto engine = rng_get_engine();
        double expected;
        {
            scoped_rng_restore preserve;
            expected = normal_roll( 12, 3 );
        }
        REQUIRE_FALSE( fp3d_construction::details( project.id ).empty() );
        CHECK( rng_get_engine() == engine );
        CHECK( normal_roll( 12, 3 ) == expected );
    }
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );
}
#endif
