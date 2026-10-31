#include "fp3d_inventory.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "fp3d_map.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "worldfactory.h"

// Export an actual native world save before the test runner removes its scratch world.
// The caller supplies a fresh QA destination; never overwrite an existing directory.
TEST_CASE( "fp3d_export_pending_activity_for_official_readback", "[.fp3d_save_compat_export]" )
{
    const char *export_dir = std::getenv( "CDDA_FP3D_COMPAT_EXPORT_DIR" );
    REQUIRE( export_dir );
    const auto destination = std::filesystem::u8path( export_dir );
    REQUIRE_FALSE( std::filesystem::exists( destination ) );
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.abort_automove();
    you.cancel_activity();
    you.omt_path.clear();
    you.wear_item( item( itype_id( "backpack" ) ), false );
    REQUIRE_FALSE( you.top_items_loc().empty() );
    auto bag = you.top_items_loc().front();
    REQUIRE( bag->typeId() == itype_id( "backpack" ) );
    auto pockets = bag->get_all_contained_pockets();
    REQUIRE( pockets.size() >= 2 );
    const auto tile = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.add_item_or_charges( tile, item( itype_id( "bottle_plastic" ) ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    auto objects = here.i_at( tile );
    REQUIRE( objects.size() == 1 );
    item_location bottle( map_cursor( tile ), &*objects.begin() );
    REQUIRE( pockets[1]->can_contain( *bottle ).success() );
    fp3d_inventory::operation transfer;
    transfer.kind = fp3d_inventory::operation_kind::insert;
    transfer.destination = bag;
    transfer.destination_pocket = 1;
    transfer.items.emplace_back( bottle, 1 );
    REQUIRE( fp3d_inventory::execute( transfer, you, here ) );
    REQUIRE( you.has_activity( activity_id( "ACT_INSERT_ITEM" ) ) );
    CHECK( you.activity.values == std::vector<int> { 1 } );
    CHECK( you.activity.str_values.front() == "fp3d_target_pocket" );
    const auto note_position = you.pos_abs_omt();
    const std::string note = "!:Y;新版 3D 存档兼容核验";
    std::string error;
    REQUIRE( fp3d_map::write_note( note_position, note, error ) );
    CHECK( overmap_buffer.note( note_position ) == note );
    you.set_moves( 0 );
    REQUIRE( g->save() );
    CHECK( you.has_activity( activity_id( "ACT_INSERT_ITEM" ) ) );
    REQUIRE( world_generator->active_world );
    const auto source = world_generator->active_world->folder_path().get_unrelative_path();
    std::filesystem::copy( source, destination, std::filesystem::copy_options::recursive );
    REQUIRE( std::filesystem::exists( destination / "mods.json" ) );
    REQUIRE( std::filesystem::exists( destination / "worldoptions.json" ) );
    std::ofstream report( destination.parent_path() / "fixture.json" );
    REQUIRE( report.good() );
    JsonOut json( report );
    json.start_object();
    json.member( "world", world_generator->active_world->world_name );
    json.member( "activity", you.activity.id().str() );
    json.member( "preferred_pocket", 1 );
    json.member( "note", note );
    json.member( "note_x", note_position.x() );
    json.member( "note_y", note_position.y() );
    json.member( "turn", to_turn<int>( calendar::turn ) );
    json.member( "ground_items", objects.size() );
    json.end_object();
}
#endif
