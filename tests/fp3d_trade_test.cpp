#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "item.h"
#include "item_pocket.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "rng.h"
#include "trade_ui.h"

TEST_CASE( "fp3d_trade_mouse_selection_uses_native_stacks_and_denials", "[fp3d][npc_trade]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    npc trader;
    trader.set_fac( faction_id( "no_faction" ) );
    trader.setpos( here, you.pos_bub( here ) + tripoint_rel_ms::north );
    trader.wear_item( item( itype_id( "backpack" ) ), false );
    item stock( itype_id( "9mm" ), calendar::turn, 12 );
    stock.set_owner( trader );
    const auto medicine = trader.i_add( stock );
    REQUIRE( medicine );
    trade_ui parent( you, trader );
    trade_preset preset( trader, you );
    trade_selector selector( &parent, trader, preset );
    selector.add_character_items( trader );
    const auto before_rng = rng_get_engine();
    const int before_moves = you.get_moves();
    const auto before_time = calendar::turn;
    const auto entries = selector.trade_entries();
    const auto found = std::find_if( entries.begin(), entries.end(), [&]( const inventory_entry * entry ) {
        return entry->any_item() == medicine;
    } );
    REQUIRE( found != entries.end() );
    REQUIRE( ( *found )->is_selectable() );
    REQUIRE( selector.set_trade_count( medicine, 99 ) );
    REQUIRE( selector.to_trade().size() == 1 );
    CHECK( selector.to_trade().front().first == medicine );
    CHECK( selector.to_trade().front().second == 12 );
    CHECK( medicine->charges == 12 );
    CHECK_FALSE( selector.set_trade_count( item_location(), 1 ) );
    const auto equipment = std::find_if( entries.begin(), entries.end(), [&]( const inventory_entry * entry ) {
        return trader.is_worn( *entry->any_item() );
    } );
    REQUIRE( equipment != entries.end() );
    CHECK_FALSE( ( *equipment )->is_selectable() );
    CHECK_FALSE( selector.set_trade_count( ( *equipment )->any_item(), 1 ) );
    REQUIRE( selector.set_trade_count( medicine, 0 ) );
    CHECK( selector.to_trade().empty() );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    CHECK( rng_get_engine() == before_rng );
}

TEST_CASE( "fp3d_trade_never_counts_container_and_hidden_contents_twice", "[fp3d][npc_trade]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    you.wear_item( item( itype_id( "backpack" ) ), false );
    item bag( itype_id( "backpack" ) );
    bag.put_in( item( itype_id( "aspirin" ) ), pocket_type::CONTAINER );
    for( item_pocket *pocket : bag.get_all_standard_pockets() ) {
        pocket->settings.set_collapse( true );
    }
    const auto container = you.i_add( bag );
    REQUIRE( container );
    npc trader;
    trader.set_fac( faction_id( "no_faction" ) );
    trade_ui parent( you, trader );
    trade_preset preset( you, trader );
    trade_selector selector( &parent, you, preset );
    selector.add_character_items( you );
    item_location child;
    for( const auto *entry : selector.trade_entries() ) {
        if( entry->any_item().has_parent() && entry->any_item().parent_item() == container ) {
            child = entry->any_item();
        }
    }
    REQUIRE( child );
    REQUIRE( selector.set_trade_count( container, 1 ) );
    REQUIRE( selector.set_trade_count( child, 1 ) );
    REQUIRE( selector.to_trade().size() == 1 );
    CHECK( selector.to_trade().front().first == child );
    REQUIRE( selector.set_trade_count( container, 1 ) );
    REQUIRE( selector.to_trade().size() == 1 );
    CHECK( selector.to_trade().front().first == container );
    CHECK( container->has_item( *child ) );
}
#endif
