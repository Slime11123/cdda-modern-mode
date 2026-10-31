#include "fp3d_inventory.h"
#include "fp3d_hud.h"
#include "fp3d_crafting.h"
#include "fp3d_construction.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <sstream>
#include "json.h"
#include "json_loader.h"
#include "item_pocket.h"
#include "avatar.h"
#include "cata_catch.h"
#include "calendar.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "type_id.h"
#include "vehicle.h"
#include "veh_type.h"
#include "vpart_position.h"
#include "units.h"

TEST_CASE( "fp3d_inventory_model_does_not_reveal_sealed_or_distant_items", "[fp3d][inventory]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto nearby = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.add_item_or_charges( nearby, item( itype_id( "rock" ) ) );
    here.furn_set( nearby, furn_str_id( "f_crate_c" ) );
    here.add_item_or_charges( you.pos_bub( here ) + tripoint_rel_ms( 0, 3, 0 ),
                              item( itype_id( "hammer" ) ) );
    here.build_map_cache( you.posz() );
    here.update_visibility_cache( you.posz() );
    const auto before_time = calendar::turn;
    const auto before_rng = rng_get_engine();
    const auto model = fp3d_inventory::capture( you, here );
    CHECK_FALSE( std::any_of( model.entries.begin(), model.entries.end(), []( const auto & row ) {
        return row.nearby;
    } ) );
    CHECK( std::any_of( model.groups.begin(), model.groups.end(), []( const auto & group ) {
        return group.capacity_l > 0;
    } ) );
    CHECK( calendar::turn == before_time );
    CHECK( rng_get_engine() == before_rng );
    here.furn_set( nearby, furn_str_id( "f_null" ) );
    const auto opened = fp3d_inventory::capture( you, here );
    const auto object = std::find_if( opened.entries.begin(),
    opened.entries.end(), []( const auto & row ) {
        return row.nearby;
    } );
    REQUIRE( object != opened.entries.end() );
    CHECK( object->location->typeId() == itype_id( "rock" ) );
    fp3d_inventory::operation pickup;
    pickup.items.emplace_back( object->location, 1 );
    REQUIRE( fp3d_inventory::execute( pickup, you, here ) );
    REQUIRE( you.has_activity( activity_id( "ACT_PICKUP" ) ) );
    process_activity( you );
    CHECK( here.i_at( nearby ).empty() );
    CHECK( you.has_amount( itype_id( "rock" ), 1 ) );
    CHECK_FALSE( fp3d_inventory::execute( pickup, you, here ) ); // Stale source must not duplicate.
}

TEST_CASE( "fp3d_hotbar_wields_through_native_cost_and_preserves_slot_handle", "[fp3d][inventory]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto knife = you.i_add( item( itype_id( "knife_combat" ) ) );
    fp3d_inventory::clear_slots();
    fp3d_inventory::assign_slot( 0, knife );
    const auto action = fp3d_inventory::select_slot( 0 );
    REQUIRE( action );
    const int before_moves = you.get_moves();
    REQUIRE( fp3d_inventory::execute( *action, you, here ) );
    REQUIRE( you.get_wielded_item() );
    CHECK( you.get_wielded_item()->typeId() == itype_id( "knife_combat" ) );
    CHECK( you.get_moves() < before_moves );
    const auto selected_again = fp3d_inventory::select_slot( 0 );
    REQUIRE( selected_again );
    CHECK( selected_again->items.front().first == you.get_wielded_item() );
    fp3d_inventory::clear_slots();
    CHECK_FALSE( fp3d_inventory::select_slot( 1 ) );
}

TEST_CASE( "fp3d_nearby_vehicle_cargo_uses_native_item_locations", "[fp3d][inventory]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    vehicle *cart = here.add_vehicle( vproto_id( "shopping_cart" ), p, 0_degrees, 100, 0 );
    REQUIRE( cart );
    const auto cargo = here.veh_at( p ).cargo();
    REQUIRE( cargo );
    REQUIRE( cart->add_item( here, cargo->part(), item( itype_id( "rock" ) ) ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    const auto model = fp3d_inventory::capture( you, here );
    const auto row = std::find_if( model.entries.begin(),
    model.entries.end(), []( const auto & entry ) {
        return entry.nearby && entry.location.where_recursive() == item_location::type::vehicle;
    } );
    REQUIRE( row != model.entries.end() );
    CHECK( row->location->typeId() == itype_id( "rock" ) );
    fp3d_inventory::operation action;
    action.items.emplace_back( row->location, 1 );
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    REQUIRE( you.has_activity( activity_id( "ACT_PICKUP" ) ) );
    process_activity( you );
    CHECK( cargo->items().empty() );
    CHECK( you.has_amount( itype_id( "rock" ), 1 ) );
}

TEST_CASE( "fp3d_container_transfer_can_cancel_and_rejects_cycles", "[fp3d][inventory]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto bag = you.top_items_loc().front();
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    auto objects = here.i_at( p );
    item_location rock( map_cursor( p ), &*objects.begin() );
    fp3d_inventory::operation action;
    action.kind = fp3d_inventory::operation_kind::insert;
    action.items.emplace_back( rock, 1 );
    action.destination = bag;
    const int moves = you.get_moves();
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    REQUIRE( you.has_activity( activity_id( "ACT_INSERT_ITEM" ) ) );
    you.cancel_activity();
    CHECK( here.i_at( p ).size() == 1 );
    CHECK( you.get_moves() == moves );
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    process_activity( you );
    CHECK( here.i_at( p ).empty() );
    CHECK( you.has_amount( itype_id( "rock" ), 1 ) );
    action.items.clear();
    action.items.emplace_back( bag, 1 );
    const int before = you.get_moves();
    CHECK_FALSE( fp3d_inventory::execute( action, you, here ) );
    CHECK( you.get_moves() == before );
    CHECK_FALSE( you.has_activity( activity_id( "ACT_INSERT_ITEM" ) ) );
}

TEST_CASE( "fp3d_navigation_hud_reads_native_route_without_advancing_simulation", "[fp3d][hud]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const auto before_time = calendar::turn;
    const auto before_moves = you.get_moves();
    const auto before_rng = rng_get_engine();
    you.omt_path.clear();
    CHECK_FALSE( fp3d_hud::capture( you ).navigation_heading );
    you.omt_path.push_back( you.pos_abs_omt() + tripoint_rel_omt( 4, 0, 0 ) );
    const auto hud = fp3d_hud::capture( you );
    REQUIRE( hud.navigation_heading );
    CHECK( *hud.navigation_heading == Approx( 90 ) );
    CHECK( hud.navigation_distance == 4 );
    CHECK( hud.navigation_z == 0 );
    you.omt_path.clear();
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    CHECK( rng_get_engine() == before_rng );
}

TEST_CASE( "fp3d_nearby_transfers_use_native_drop_and_move_activities", "[fp3d][inventory]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    const auto north = you.pos_bub( here ) + tripoint_rel_ms::north;
    const auto south = you.pos_bub( here ) + tripoint_rel_ms::south;
    auto *cart = here.add_vehicle( vproto_id( "shopping_cart" ), north, 0_degrees, 100, 0 );
    REQUIRE( cart );
    const auto cargo = here.veh_at( north ).cargo();
    REQUIRE( cargo );
    here.add_item_or_charges( south, item( itype_id( "rock" ) ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    auto objects = here.i_at( south );
    fp3d_inventory::operation move;
    move.kind = fp3d_inventory::operation_kind::place;
    move.items.emplace_back( item_location( map_cursor( south ), &*objects.begin() ), 1 );
    move.destination_tile = here.get_abs( north );
    move.to_vehicle = true;
    REQUIRE( fp3d_inventory::execute( move, you, here ) );
    REQUIRE( you.has_activity( activity_id( "ACT_MOVE_ITEMS" ) ) );
    process_activity( you );
    CHECK( here.i_at( south ).empty() );
    REQUIRE( cargo->items().size() == 1 );
    const auto model = fp3d_inventory::capture( you, here );
    const auto row = std::find_if( model.entries.begin(),
    model.entries.end(), []( const auto & entry ) {
        return entry.location.where_recursive() == item_location::type::vehicle;
    } );
    REQUIRE( row != model.entries.end() );
    move.items = { { row->location, 1 } };
    move.destination_tile = here.get_abs( south );
    move.to_vehicle = false;
    REQUIRE( fp3d_inventory::execute( move, you, here ) );
    process_activity( you );
    CHECK( cargo->items().empty() );
    CHECK( here.i_at( south ).size() == 1 );
    const auto knife = you.i_add( item( itype_id( "knife_combat" ) ) );
    move.items = { { knife, 1 } };
    REQUIRE( fp3d_inventory::execute( move, you, here ) );
    REQUIRE( you.has_activity( activity_id( "ACT_DROP" ) ) );
    process_activity( you );
    CHECK_FALSE( you.has_amount( itype_id( "knife_combat" ), 1 ) );
    CHECK( here.i_at( south ).size() == 2 );
    auto source = here.i_at( south );
    move.items = { { item_location( map_cursor( south ), &*source.begin() ), 1 } };
    item ammo( itype_id( "9mm" ), calendar::turn, 200 );
    item &stack = here.add_item_or_charges( south, ammo );
    const item_location stack_location( map_cursor( south ), &stack );
    move.items = { { stack_location, 2 } };
    move.destination_tile = here.get_abs( north );
    move.to_vehicle = true;
    REQUIRE( fp3d_inventory::execute( move, you, here ) );
    process_activity( you );
    REQUIRE( stack_location );
    CHECK( stack_location->charges == 198 );
    REQUIRE( cargo->items().size() == 1 );
    CHECK( cargo->items().begin()->charges == 2 );
    move.destination_tile = here.get_abs( north ) + tripoint_rel_ms::above;
    const int before = you.get_moves();
    CHECK_FALSE( fp3d_inventory::execute( move, you, here ) );
    CHECK( you.get_moves() == before );
    CHECK( here.i_at( south ).size() == 3 );
}

TEST_CASE( "fp3d_recipe_catalogue_and_native_preselection_are_read_only", "[fp3d][crafting]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    const auto before_rng = rng_get_engine();
    const auto before_time = calendar::turn;
    const auto before_moves = you.get_moves();
    const auto catalogue = fp3d_crafting::capture( you );
    REQUIRE_FALSE( catalogue.empty() );
    CHECK_FALSE( catalogue.front().name.empty() );
    CHECK_FALSE( fp3d_crafting::details( catalogue.front().id ).empty() );
    CHECK( rng_get_engine() == before_rng );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    fp3d_inventory::operation choice;
    choice.kind = fp3d_inventory::operation_kind::native;
    choice.native_action = ACTION_CRAFT;
    choice.crafting_recipe = catalogue.front().id;
    fp3d_inventory::stage( choice );
    CHECK( fp3d_inventory::take_craft_recipe() == choice.crafting_recipe );
    CHECK_FALSE( fp3d_inventory::pending() );
    CHECK( fp3d_inventory::take_craft_recipe() == recipe_id() );
    fp3d_inventory::stage( choice );
    fp3d_inventory::clear_pending();
    CHECK( fp3d_inventory::take_craft_recipe() == recipe_id() );
    CHECK( rng_get_engine() == before_rng );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
}

TEST_CASE( "fp3d_build_catalogue_is_read_only_and_missing_requirements_do_not_start_work",
           "[fp3d][construction]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto before_time = calendar::turn;
    const auto before_rng = rng_get_engine();
    const int before_moves = you.get_moves();
    const auto catalogue = fp3d_construction::catalogue();
    REQUIRE_FALSE( catalogue.empty() );
    const construction_group_str_id group( "build_log_wall" );
    REQUIRE( std::any_of( catalogue.begin(), catalogue.end(), [&]( const auto & row ) {
        return row.id == group;
    } ) );
    CHECK_FALSE( fp3d_construction::details( group ).empty() );
    CHECK( rng_get_engine() == before_rng );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    fp3d_inventory::operation choice;
    choice.kind = fp3d_inventory::operation_kind::native;
    choice.native_action = ACTION_CONSTRUCT;
    choice.construction_group = group;
    fp3d_inventory::stage( choice );
    CHECK( fp3d_inventory::take_construction_group() == group );
    CHECK_FALSE( fp3d_inventory::pending() );
    CHECK( fp3d_inventory::take_construction_group().is_null() );
    fp3d_inventory::stage( choice );
    fp3d_inventory::clear_pending();
    CHECK( fp3d_inventory::take_construction_group().is_null() );
    CHECK_FALSE( fp3d_construction::begin( construction_group_str_id( "fp3d_missing_project" ) ) );
    CHECK_FALSE( fp3d_construction::begin( group ) );
    CHECK( rng_get_engine() == before_rng );
    CHECK( you.activity.is_null() );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    CHECK( here.ter( you.pos_bub( here ) ) == ter_str_id( "t_grass" ) );
}

TEST_CASE( "fp3d_precise_pocket_survives_activity_reload_without_falling_back",
           "[fp3d][inventory][pockets]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.wear_item( item( itype_id( "backpack" ) ), false );
    auto bag = you.top_items_loc().front();
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.add_item_or_charges( p, item( itype_id( "bottle_plastic" ) ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    auto stack = here.i_at( p );
    item_location bottle( map_cursor( p ), &*stack.begin() );
    auto pockets = bag->get_all_contained_pockets();
    REQUIRE( pockets.size() >= 2 );
    REQUIRE( pockets[1]->can_contain( *bottle ).success() );
    fp3d_inventory::operation action;
    action.kind = fp3d_inventory::operation_kind::insert;
    action.destination = bag;
    action.destination_pocket = 1;
    action.items.emplace_back( bottle, 1 );
    const int before = you.get_moves();
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    const int selected_cost = you.activity.moves_total;
    you.cancel_activity();
    CHECK( you.get_moves() == before );
    CHECK( here.i_at( p ).size() == 1 );
    action.destination_pocket = -1;
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    CHECK( you.activity.moves_total == selected_cost );
    you.cancel_activity();
    action.destination_pocket = 1;
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    std::ostringstream saved;
    JsonOut out( saved );
    you.activity.serialize( out );
    player_activity restored;
    restored.deserialize( json_loader::from_string( saved.str() ).get_object() );
    you.activity = std::move( restored );
    REQUIRE( you.activity.values == std::vector<int> { 1 } );
    REQUIRE( you.activity.str_values.front() == "fp3d_target_pocket" );
    process_activity( you );
    CHECK( here.i_at( p ).empty() );
    CHECK( pockets[0]->empty() );
    REQUIRE( pockets[1]->size() == 1 );
    CHECK( pockets[1]->all_items_top().front()->typeId() == itype_id( "bottle_plastic" ) );
    here.add_item_or_charges( p, item( itype_id( "bottle_plastic" ) ) );
    stack = here.i_at( p );
    action.items = { { item_location( map_cursor( p ), &*stack.begin() ), 1 } };
    const int now = you.get_moves();
    CHECK_FALSE( fp3d_inventory::execute( action, you, here ) );
    CHECK( you.get_moves() == now );
    CHECK( here.i_at( p ).size() == 1 );
    CHECK( pockets[0]->empty() );
    action.destination_pocket = 999;
    CHECK_FALSE( fp3d_inventory::execute( action, you, here ) );
    CHECK( you.activity.is_null() );
    CHECK( here.i_at( p ).size() == 1 );

    action.destination_pocket = 0;
    action.items = { { item_location( bag, pockets[1]->all_items_top().front() ), 1 } };
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    process_activity( you );
    CHECK( pockets[1]->empty() );
    CHECK( pockets[0]->size() == 1 );
    CHECK( here.i_at( p ).size() == 1 );
    action.items = { { item_location( bag, pockets[0]->all_items_top().front() ), 1 } };
    const int after_transfer = you.get_moves();
    CHECK_FALSE( fp3d_inventory::execute( action, you, here ) );
    CHECK( you.get_moves() == after_transfer );
}

TEST_CASE( "fp3d_precise_pocket_keeps_partial_charges_and_revalidates_target",
           "[fp3d][inventory][pockets]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.add_item_or_charges( p, item(
                                  itype_id( "test_watertight_open_sealed_multipocket_container_2x250ml" ) ) );
    auto stack = here.i_at( p );
    item_location can( map_cursor( p ), &*stack.begin() );
    auto pockets = can->get_all_contained_pockets();
    REQUIRE( pockets.size() >= 2 );
    pockets[0]->unseal();
    pockets[1]->unseal();
    item ammo( itype_id( "9mm" ), calendar::turn, 200 );
    const auto q = you.pos_bub( here );
    here.add_item_or_charges( q, ammo );
    const auto large_pos = q + tripoint_rel_ms::east;
    here.add_item_or_charges( large_pos, item( itype_id( "backpack" ) ) );
    auto source_stack = here.i_at( q );
    item_location source( map_cursor( q ), &*source_stack.begin() );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    auto large_stack = here.i_at( large_pos );
    item_location oversized( map_cursor( large_pos ), &*large_stack.begin() );
    fp3d_inventory::operation action;
    action.kind = fp3d_inventory::operation_kind::insert;
    action.destination = can;
    action.destination_pocket = 1;
    action.items.emplace_back( source, 200 );
    fp3d_inventory::operation rejected = action;
    rejected.items = { { oversized, 1 } };
    std::string insert_error;
    CHECK_FALSE( fp3d_inventory::can_insert( rejected, you, here, insert_error ) );
    CHECK_FALSE( insert_error.empty() );
    const int moves_before_rejection = you.get_moves();
    CHECK_FALSE( fp3d_inventory::execute( rejected, you, here, &insert_error ) );
    CHECK( you.get_moves() == moves_before_rejection );
    CHECK( you.activity.is_null() );
    CHECK( oversized );
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    SECTION( "partial_transfer_only_uses_selected_pocket" ) {
        process_activity( you );
        CHECK( pockets[0]->empty() );
        REQUIRE( pockets[1]->size() == 1 );
        const int moved = pockets[1]->all_items_top().front()->charges;
        CHECK( moved > 0 );
        CHECK( moved < 200 );
        REQUIRE( source );
        CHECK( source->charges + moved == 200 );
    }
    SECTION( "sealed_after_scheduling_does_not_redirect_items" ) {
        ammo.charges = 1;
        REQUIRE( pockets[1]->insert_item( ammo ).success() );
        REQUIRE( pockets[1]->seal() );
        process_activity( you );
        CHECK( pockets[0]->empty() );
        REQUIRE( source );
        CHECK( source->charges == 200 );
        CHECK( pockets[1]->all_items_top().front()->charges == 1 );
    }
    SECTION( "removed_destination_leaves_source_intact" ) {
        can.remove_item();
        process_activity( you );
        REQUIRE( source );
        CHECK( source->charges == 200 );
        CHECK( you.activity.is_null() );
    }
}
#endif
