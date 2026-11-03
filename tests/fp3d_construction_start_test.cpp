#include "fp3d_construction.h"
#include "fp3d_targeting.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "construction.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "options_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "rng.h"
#include "skill.h"

namespace
{
void prepare_table_site()
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.set_skill_level( skill_id( "fabrication" ), 3 );
    const auto origin = you.pos_bub( here );
    here.ter_set( origin + tripoint_rel_ms::north, ter_str_id( "t_floor" ) );
    for( int n = 0; n < 4; ++n ) {
        here.add_item_or_charges( origin, item( itype_id( "2x4" ) ) );
    }
    here.add_item_or_charges( origin, item( itype_id( "wood_panel" ) ) );
    item nails( itype_id( "nail" ) );
    nails.charges = 8;
    here.add_item_or_charges( origin, nails );
    here.add_item_or_charges( origin, item( itype_id( "hammer" ) ) );
    here.add_item_or_charges( origin, item( itype_id( "saw" ) ) );
    you.invalidate_crafting_inventory();
    fp3d_snapshot::prepare_view( you, here );
}

template<typename Items>
int amount( const Items &items, const std::string &id )
{
    int result = 0;
    for( const item &entry : items ) {
        if( entry.typeId().str() == id ) {
            result += entry.count();
        }
    }
    return result;
}
} // namespace

// Exercises the production handoff after site selection, without input hooks,
// debug material grants or manually manufacturing a partial construction.
TEST_CASE( "fp3d_confirmed_table_site_consumes_native_materials_and_finishes_activity",
           "[fp3d][construction][start]" )
{
    override_option scaling( "CONSTRUCTION_SCALING", "100" );
    prepare_table_site();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto origin = you.pos_bub( here );
    const auto p = origin + tripoint_rel_ms::north;
    const construction_str_id project( "constr_table" );
    REQUIRE( project.is_valid() );
    const fp3d_targeting::target target{ you.pos_abs(), here.get_abs( p ),
                                         fp3d_targeting::kind::terrain, here.ter( p ).id().str() };
    REQUIRE( fp3d_construction::preview_allowed( you, here, project.obj(), target ) );
    const auto ghost = fp3d_construction::terrain_preview( project.obj(), target.position );
    REQUIRE( ghost );
    REQUIRE( ghost->furniture );
    const auto clock = calendar::turn;
    REQUIRE( fp3d_construction::start( project, target ) );
    REQUIRE( here.partial_con_at( p ) );
    const auto &components = here.partial_con_at( p )->components;
    CHECK( here.partial_con_at( p )->id == project.obj().id );
    CHECK( amount( components, "2x4" ) == 4 );
    CHECK( amount( components, "wood_panel" ) == 1 );
    CHECK( amount( components, "nail" ) == 8 );
    CHECK( amount( here.i_at( origin ), "2x4" ) == 0 );
    CHECK( amount( here.i_at( origin ), "wood_panel" ) == 0 );
    CHECK( amount( here.i_at( origin ), "nail" ) == 0 );
    CHECK( amount( here.i_at( origin ), "hammer" ) == 1 );
    CHECK( amount( here.i_at( origin ), "saw" ) == 1 );
    CHECK( you.activity.id() == activity_id( "ACT_BUILD" ) );
    CHECK( you.activity.placement == target.position );
    CHECK( calendar::turn == clock );
    CHECK( here.furn( p ) == furn_str_id::NULL_ID() );
    // Repeated confirmation cannot submit the same materials or another job.
    CHECK_FALSE( fp3d_construction::start( project, target ) );
    REQUIRE( here.partial_con_at( p ) );
    CHECK( amount( here.partial_con_at( p )->components, "nail" ) == 8 );
    int spent = 0;
    int ticks = 0;
    while( you.activity && ticks < 10000 ) {
        you.set_moves( 100 );
        you.activity.do_turn( you );
        spent += 100 - you.get_moves();
        calendar::turn += 1_seconds;
        ++ticks;
    }
    CHECK( spent > 0 );
    CHECK( ticks > 1 );
    CHECK( ticks < 10000 );
    CHECK( you.activity.is_null() );
    CHECK_FALSE( here.partial_con_at( p ) );
    CHECK( here.furn( p ) == furn_str_id( "f_table" ) );
    CHECK( you.pos_bub( here ) == origin );
    CHECK( amount( here.i_at( origin ), "hammer" ) == 1 );
    CHECK( amount( here.i_at( origin ), "saw" ) == 1 );
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto built = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto &entry ) {
        return entry.position == target.position;
    } );
    REQUIRE( built != scene.cells.end() );
    REQUIRE( built->furniture );
    CHECK( built->furniture->id == ghost->furniture->id );
    CHECK( built->furniture->shape == ghost->furniture->shape );
}

TEST_CASE( "fp3d_start_accepts_a_native_furniture_stage_and_consumes_its_mattress",
           "[fp3d][construction][start]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto origin = you.pos_bub( here );
    const auto p = origin + tripoint_rel_ms::north;
    here.furn_set( p, furn_str_id( "f_bed_frame" ) );
    here.add_item_or_charges( origin, item( itype_id( "mattress" ) ) );
    fp3d_snapshot::prepare_view( you, here );
    const fp3d_targeting::target target{ you.pos_abs(), here.get_abs( p ),
                                         fp3d_targeting::kind::furniture, "f_bed_frame" };
    REQUIRE( fp3d_construction::start( construction_str_id( "constr_finish_bed" ), target ) );
    REQUIRE( here.partial_con_at( p ) );
    CHECK( amount( here.partial_con_at( p )->components, "mattress" ) == 1 );
    CHECK( amount( here.i_at( origin ), "mattress" ) == 0 );
    int ticks = 0;
    while( you.activity && ticks < 10000 ) {
        you.set_moves( 100 );
        you.activity.do_turn( you );
        calendar::turn += 1_seconds;
        ++ticks;
    }
    CHECK( ticks > 1 );
    CHECK( ticks < 10000 );
    CHECK( you.activity.is_null() );
    CHECK_FALSE( here.partial_con_at( p ) );
    CHECK( here.furn( p ) == furn_str_id( "f_bed" ) );
}

TEST_CASE( "fp3d_start_rejects_changed_sites_and_stale_material_caches_before_consumption",
           "[fp3d][construction][start]" )
{
    prepare_table_site();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto origin = you.pos_bub( here );
    const auto p = origin + tripoint_rel_ms::north;
    construction_str_id project( "constr_table" );
    fp3d_targeting::target target{ you.pos_abs(), here.get_abs( p ),
                                   fp3d_targeting::kind::terrain, here.ter( p ).id().str() };
    REQUIRE( fp3d_construction::preview_allowed( you, here, project.obj(), target ) );
    SECTION( "required knowledge was lost" ) {
        you.set_knowledge_level( skill_id( "fabrication" ), 0 );
    }
    SECTION( "the hammer was removed after the preview cached inventory" ) {
        for( auto it = here.i_at( origin ).begin(); it != here.i_at( origin ).end(); ++it ) {
            if( it->typeId() == itype_id( "hammer" ) ) {
                here.i_rem( origin, it );
                break;
            }
        }
    }
    SECTION( "materials were removed without invalidating the preview cache" ) {
        here.i_clear( origin );
    }
    SECTION( "terrain identity changed" ) {
        here.ter_set( p, ter_str_id( "t_wall" ) );
    }
    SECTION( "the target is too far" ) {
        target.position += tripoint_rel_ms( 0, -3, 0 );
    }
    SECTION( "the target is on another level" ) {
        target.position += tripoint_rel_ms::above;
    }
    SECTION( "the player's captured origin changed" ) {
        target.origin += tripoint_rel_ms::east;
    }
    SECTION( "the player cannot build on their own tile" ) {
        target.position = you.pos_abs();
        target.id = here.ter( origin ).id().str();
    }
    SECTION( "the scene is now dark even if 3D still displays it" ) {
        set_time( calendar::turn_zero + 1_hours );
        fp3d_snapshot::prepare_view( you, here );
        REQUIRE( you.fine_detail_vision_mod() >= 4 );
    }
    SECTION( "the project no longer exists" ) {
        project = construction_str_id( "fp3d_missing_project" );
    }
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const auto moves = you.get_moves();
    const auto items = here.i_at( origin ).size();
    const auto terrain = here.ter( p );
    CAPTURE( project.str(), target.position, target.origin, target.id,
             you.get_knowledge_level( skill_id( "fabrication" ) ), you.fine_detail_vision_mod(), items );
    CHECK_FALSE( fp3d_construction::start( project, target ) );
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );
    CHECK( here.i_at( origin ).size() == items );
    CHECK( here.ter( p ) == terrain );
    CHECK_FALSE( here.partial_con_at( p ) );
    CHECK( you.activity.is_null() );
}
#endif
