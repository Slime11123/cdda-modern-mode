#include "fp3d_interaction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "fp3d_inventory.h"
#include "fp3d_snapshot.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "rng.h"

TEST_CASE( "fp3d_E_single_item_uses_native_automatic_pockets_and_keeps_charges",
           "[fp3d][interaction][pickup]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    item object( itype_id( "aspirin" ) );
    object.charges = 7;
    here.add_item_or_charges( p, object );
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here );
    REQUIRE( scene.sprites.size() == 1 );
    CHECK_FALSE( scene.sprites.front().multiple_items );
    const auto target = fp3d_targeting::ray( scene, 0, 55 );
    REQUIRE( target );
    const auto before_time = calendar::turn;
    const auto before_moves = you.get_moves();
    const auto before_rng = rng_get_engine();
    const auto request = fp3d_interaction::resolve( "FP3D_INTERACT", target, you, here );
    REQUIRE( request );
    CHECK( request->action == ACTION_PICKUP );
    CHECK( request->quick_pickup );
    CHECK( request->targeted_pickup );
    CHECK_FALSE( you.activity );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    CHECK( rng_get_engine() == before_rng );
    REQUIRE( fp3d_interaction::quick_pickup( you, here, p, true ) );
    process_activity( you );
    CHECK( here.i_at( p ).empty() );
    const auto held = you.all_items_loc();
    const auto found = std::find_if( held.begin(), held.end(), []( const item_location & loc ) {
        return loc && loc->typeId() == itype_id( "aspirin" );
    } );
    REQUIRE( found != held.end() );
    CHECK( ( *found )->charges == 7 );
    REQUIRE( found->has_parent() );
    CHECK( found->parent_item()->typeId() == itype_id( "backpack" ) );
}

TEST_CASE( "fp3d_pickup_view_limits_sources_and_tracks_native_multiple_item_visibility",
           "[fp3d][interaction][pickup]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto other = you.pos_bub( here ) + tripoint_rel_ms::east;
    here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
    here.add_item_or_charges( p, item( itype_id( "bandages" ) ) );
    here.add_item_or_charges( other, item( itype_id( "crowbar" ) ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto before_time = calendar::turn;
    const auto before_moves = you.get_moves();
    const auto before_rng = rng_get_engine();
    const auto view = fp3d_inventory::capture( you, here, here.get_abs( p ) );
    int nearby_items = 0;
    bool backpack = false;
    for( const auto &entry : view.entries ) {
        if( entry.nearby ) {
            CHECK( entry.location.pos_bub( here ) == p );
            CHECK( entry.location->typeId() != itype_id( "crowbar" ) );
            ++nearby_items;
        } else {
            backpack = backpack || entry.location->typeId() == itype_id( "backpack" );
        }
    }
    CHECK( nearby_items == 2 );
    CHECK( backpack );
    const auto image_at = [&]( const fp3d_snapshot::scene_snapshot & scene ) {
        return std::find_if( scene.sprites.begin(), scene.sprites.end(), [&]( const auto & image ) {
            return image.kind == fp3d_snapshot::sprite_kind::item && image.position == here.get_abs( p );
        } );
    };
    auto scene = fp3d_snapshot::capture( you, here );
    REQUIRE( image_at( scene ) != scene.sprites.end() );
    CHECK( image_at( scene )->multiple_items );
    const auto target = fp3d_targeting::ray( scene, 0, 55 );
    REQUIRE( target );
    const auto request = fp3d_interaction::resolve( "FP3D_INTERACT", target, you, here );
    REQUIRE( request );
    CHECK( request->action == ACTION_PICKUP );
    CHECK( request->quick_pickup );
    CHECK( request->targeted_pickup );
    const auto quick_request = fp3d_interaction::resolve( "FP3D_PICKUP", target, you, here );
    REQUIRE( quick_request );
    CHECK_FALSE( quick_request->targeted_pickup );
    here.i_at( p ).erase( here.i_at( p ).begin() );
    scene = fp3d_snapshot::capture( you, here );
    REQUIRE( image_at( scene ) != scene.sprites.end() );
    CHECK_FALSE( image_at( scene )->multiple_items );
    here.furn_set( p, furn_str_id( "f_crate_c" ) );
    fp3d_snapshot::prepare_view( you, here );
    scene = fp3d_snapshot::capture( you, here );
    CHECK( image_at( scene ) == scene.sprites.end() );
    const auto closed = fp3d_inventory::capture( you, here, here.get_abs( p ) );
    CHECK( std::none_of( closed.entries.begin(), closed.entries.end(), []( const auto & entry ) {
        return entry.nearby;
    } ) );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    CHECK( rng_get_engine() == before_rng );
}
#endif
