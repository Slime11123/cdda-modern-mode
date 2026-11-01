#include "fp3d_construction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "construction.h"
#include "game.h"
#include "map.h"
#include "rng.h"

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
             std::pair{ "t_window", kind::window }
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
    }
    for( const std::string id : {
             "t_open_air", "t_stairs_up", "t_stairs_down", "t_tree",
             "not_a_terrain", "t_null", ""
         } ) {
        CAPTURE( id );
        construction project{};
        project.post_terrain = id;
        CHECK_FALSE( fp3d_construction::terrain_preview( project, position ) );
    }
    construction furniture{};
    furniture.post_is_furniture = true;
    furniture.post_terrain = "f_crate_c";
    CHECK_FALSE( fp3d_construction::terrain_preview( furniture, position ) );
    CHECK( here.ter( here.get_bub( position ) ) == original );
    CHECK( you.pos_abs() == position );
    CHECK( calendar::turn == time );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == engine );
}
#endif
