#include "fp3d_furniture.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "fp3d_targeting.h"
#include "fp3d_snapshot.h"
#include "rng.h"

TEST_CASE( "fp3d_cabinet_front_faces_open_space_using_owned_walls", "[fp3d][furniture]" )
{
    for( const auto shape : {
             fp3d_furniture::kind::cupboard, fp3d_furniture::kind::locker,
             fp3d_furniture::kind::cabinet
         } ) {
        CHECK( fp3d_furniture::facing( shape, { false, false, false, false } ) == 0 );
        CHECK( fp3d_furniture::facing( shape, { true, false, false, false } ) == 0 );
        CHECK( fp3d_furniture::facing( shape, { false, true, false, false } ) == 1 );
        CHECK( fp3d_furniture::facing( shape, { false, false, true, false } ) == 2 );
        CHECK( fp3d_furniture::facing( shape, { false, false, false, true } ) == 3 );
        CHECK( fp3d_furniture::facing( shape, { true, true, false, false } ) == 0 );
        CHECK( fp3d_furniture::facing( shape, { true, false, true, false } ) == 1 );
        CHECK( fp3d_furniture::facing( shape, { true, true, true, true } ) == 0 );
    }
    CHECK( fp3d_furniture::facing( fp3d_furniture::kind::table, { false, true, false, false } ) == 0 );
    CHECK( fp3d_furniture::facing( fp3d_furniture::kind::bed, { false, true, false, false } ) == 0 );
}

TEST_CASE( "fp3d_chair_seat_faces_adjacent_table_with_backrest_outside", "[fp3d][furniture]" )
{
    using fp3d_furniture::kind;
    const auto &back = fp3d_furniture::parts( kind::chair )[1];
    for( unsigned direction = 0; direction < 4; ++direction ) {
        std::array<bool, 4> tables{};
        tables[direction] = true;
        const auto facing = fp3d_furniture::facing( kind::chair, {}, tables );
        CHECK( facing == ( direction + 2 ) % 4 );
        const auto oriented = fp3d_furniture::rotated( back, facing );
        const std::array<float, 4> back_offset{ oriented.z, -oriented.x, -oriented.z, oriented.x };
        CHECK( back_offset[direction] > 0 );
    }
}

TEST_CASE( "fp3d_rotated_bookcase_ray_tracks_its_open_front_and_side_panels",
           "[fp3d][furniture][ray]" )
{
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const auto moves = get_avatar().get_moves();
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms::zero;
    fp3d_snapshot::cell bookcase;
    bookcase.position = tripoint_abs_ms( 0, -3, 0 );
    bookcase.terrain = "t_floor";
    bookcase.furniture = fp3d_furniture::describe( "f_bookcase" );
    REQUIRE( bookcase.furniture );
    scene.cells.push_back( bookcase );
    fp3d_snapshot::sprite object;
    object.position = bookcase.position;
    object.kind = fp3d_snapshot::sprite_kind::item;
    object.id = "apple";
    object.elevation = 1.3f;
    object.height = .5f;
    scene.sprites.push_back( object );
    auto hit = fp3d_targeting::ray( scene, 0, 0 );
    REQUIRE( hit );
    CHECK( hit->type == fp3d_targeting::kind::item );
    fp3d_snapshot::cell backing;
    backing.position = bookcase.position + tripoint_rel_ms::east;
    backing.terrain = "t_wall";
    backing.kind = fp3d_snapshot::terrain_kind::wall;
    scene.cells.push_back( backing );
    hit = fp3d_targeting::ray( scene, 0, 0 );
    REQUIRE( hit );
    CHECK( hit->type == fp3d_targeting::kind::furniture );
    CHECK( hit->id == "f_bookcase" );
    scene.cells.back().position = bookcase.position + tripoint_rel_ms::north;
    hit = fp3d_targeting::ray( scene, 0, 0 );
    REQUIRE( hit );
    CHECK( hit->type == fp3d_targeting::kind::item );
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( get_avatar().get_moves() == moves );
}
#endif
