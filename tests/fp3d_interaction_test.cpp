#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include "avatar.h"
#include "cata_catch.h"
#include "calendar.h"
#include "fp3d_snapshot.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
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
    SECTION( "stair treads occupy only the rendered half of the tile" ) {
        add( 0, -1, 0 ).goes_up = true;
        add( 0, -1, 1 ).goes_down = true;
        add( 0, -3, 0, terrain_kind::wall );
        const auto steps = fp3d_targeting::ray( scene, 0, 0, { .25f, 0 } );
        REQUIRE( steps );
        CHECK( steps->position == scene.origin + tripoint_rel_ms::north );
        const auto side = fp3d_targeting::ray( scene, 0, 0, { -.25f, 0 } );
        REQUIRE( side );
        CHECK( side->position == scene.origin + tripoint_rel_ms( 0, -3, 0 ) );
    }
    SECTION( "a matched upper stair opening has no synthetic full ceiling" ) {
        auto &stairs = add( 0, 0, 0 );
        stairs.goes_up = true;
        stairs.ceiling_present = true;
        add( 0, 0, 1 ).goes_down = true;
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, -90, { .25f, .4f } ) );
        const auto upper_floor = fp3d_targeting::ray( scene, 0, -90, { -.25f, .4f } );
        REQUIRE( upper_floor );
        CHECK( upper_floor->position == scene.origin + tripoint_rel_ms::above );
    }
    SECTION( "six-tile range is finite" ) {
        add( 0, -8, 0, terrain_kind::wall );
        CHECK_FALSE( fp3d_targeting::ray( scene, 0, 0 ) );
        add( 0, -5, 0, terrain_kind::wall );
        REQUIRE( fp3d_targeting::ray( scene, 0, 0 ) );
    }
}
#endif
