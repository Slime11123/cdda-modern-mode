#include "fp3d_cell_index.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <limits>

#include "cata_catch.h"

TEST_CASE( "fp3d_cell_index_matches_snapshot_positions", "[fp3d][cell_index]" )
{
    fp3d_cell_index index;
    std::vector<fp3d_snapshot::cell> tiles( 4 );
    tiles[0].position = tripoint_abs_ms( -8, -1, -1 );
    tiles[1].position = tripoint_abs_ms( 0, 1, 1 );
    tiles[2].position = tripoint_abs_ms( -1, 0, 0 );
    tiles[3].position = tiles[0].position;
    index.rebuild( tiles );
    CHECK( index.get( tiles[0].position ) == &tiles[0] );
    CHECK( index.get( tiles[1].position ) == &tiles[1] );
    CHECK( index.get( tiles[2].position ) == &tiles[2] );
    CHECK( index.get( tripoint_abs_ms( -7, -1, -1 ) ) == nullptr );
    CHECK( index.get( tripoint_abs_ms( -9, -1, -1 ) ) == nullptr );
    CHECK( index.get( tripoint_abs_ms( 0, 2, 1 ) ) == nullptr );
    CHECK( index.get( tripoint_abs_ms( 0, 1, 2 ) ) == nullptr );

    SECTION( "sparse extreme coordinates use bounded storage" ) {
        tiles[1].position = tripoint_abs_ms( std::numeric_limits<int>::max(),
                                           std::numeric_limits<int>::min(), 0 );
        index.rebuild( tiles );
        CHECK( index.get( tiles[0].position ) == &tiles[0] );
        CHECK( index.get( tiles[1].position ) == &tiles[1] );
        CHECK( index.get( tripoint_abs_ms( 0, 1, 1 ) ) == nullptr );
    }
    SECTION( "replacement snapshot removes stale pointers" ) {
        std::vector<fp3d_snapshot::cell> replacement( 1 );
        replacement[0].position = tripoint_abs_ms( 99, 99, 0 );
        index.rebuild( replacement );
        CHECK( index.get( tiles[0].position ) == nullptr );
        CHECK( index.get( replacement[0].position ) == &replacement[0] );
    }
    index.clear();
    CHECK( index.get( tiles[0].position ) == nullptr );
    index.rebuild( {} );
    CHECK( index.get( tiles[1].position ) == nullptr );
}
#endif
