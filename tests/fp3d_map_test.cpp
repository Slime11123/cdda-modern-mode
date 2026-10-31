#include "fp3d_map.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <limits>
#include <memory>
#include <sstream>
#include "game.h"
#include "json.h"
#include "json_loader.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "rng.h"
TEST_CASE( "fp3d_map_obeys_native_knowledge_notes_and_route_without_simulation", "[fp3d][map_ui]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    you.setpos( get_map(), tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_abs_omt();
    const auto q = p + tripoint_rel_omt( 1, 0, 0 );
    const auto old_p = overmap_buffer.ter( p );
    const auto old_q = overmap_buffer.ter( q );
    const auto seen_p = overmap_buffer.seen( p );
    const auto seen_q = overmap_buffer.seen( q );
    const bool had_note = overmap_buffer.has_note( q );
    const std::string old_note = overmap_buffer.note( q );
    const auto old_route = you.omt_path;
    const auto map_p = overmap_buffer.get_existing_om_global( p );
    const auto map_q = overmap_buffer.get_existing_om_global( q );
    REQUIRE( static_cast<bool>( map_p ) );
    REQUIRE( static_cast<bool>( map_q ) );
    on_out_of_scope restore( [&]() {
        overmap_buffer.ter_set( p, old_p );
        overmap_buffer.ter_set( q, old_q );
        map_p.om->set_seen( map_p.local, seen_p, true );
        map_q.om->set_seen( map_q.local, seen_q, true );
        if( had_note ) {
            overmap_buffer.add_note( q, old_note );
        } else {
            overmap_buffer.delete_note( q );
        }
        you.omt_path = old_route;
    } );
    overmap_buffer.ter_set( p, oter_str_id( "forest" ).id() );
    overmap_buffer.ter_set( q, oter_str_id( "forest_thick" ).id() );
    map_p.om->set_seen( map_p.local, om_vision_level::vague, true );
    map_q.om->set_seen( map_q.local, om_vision_level::unseen, true );
    REQUIRE( overmap_buffer.seen( p ) == om_vision_level::vague );
    REQUIRE( overmap_buffer.seen( q ) == om_vision_level::unseen );
    overmap_buffer.add_note( q, "玩家手写的未知地区标记" );
    you.omt_path = { q, p };
    const auto before_rng = rng_get_engine();
    const auto before_time = calendar::turn;
    const int before_moves = you.get_moves();
    const auto view = fp3d_map::capture( you, p, 4 );
    REQUIRE( view.cells.size() == 81 );
    CHECK( view.player == p );
    REQUIRE( view.objective );
    CHECK( *view.objective == q );
    const auto known = std::find_if( view.cells.begin(), view.cells.end(),
    [&]( const auto & row ) {
        return row.position == p;
    } );
    REQUIRE( known != view.cells.end() );
    CHECK( known->known );
    CHECK( known->name == oter_str_id( "forest" )->get_name( om_vision_level::vague ) );
    CHECK( known->symbol == oter_str_id( "forest" )->get_symbol( om_vision_level::vague ) );
    CHECK( known->route );
    const auto unknown = std::find_if( view.cells.begin(), view.cells.end(),
    [&]( const auto & row ) {
        return row.position == q;
    } );
    REQUIRE( unknown != view.cells.end() );
    CHECK_FALSE( unknown->known );
    CHECK( unknown->name == "未探索" );
    CHECK( unknown->symbol.empty() );
    CHECK( unknown->note == "玩家手写的未知地区标记" );
    CHECK( unknown->route );
    CHECK( unknown->objective );
    CHECK( overmap_buffer.seen( q ) == om_vision_level::unseen );
    CHECK( you.omt_path == std::vector<tripoint_abs_omt> { q, p } );
    CHECK( rng_get_engine() == before_rng );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
}

TEST_CASE( "fp3d_map_does_not_generate_distant_overmaps_and_bounds_view_size", "[fp3d][map_ui]" )
{
    clear_avatar();
    const tripoint_abs_omt remote( 2000000, 2000000, 0 );
    REQUIRE_FALSE( static_cast<bool>( overmap_buffer.get_existing_om_global( remote ) ) );
    const auto before_rng = rng_get_engine();
    const auto view = fp3d_map::capture( get_avatar(),
                                         tripoint_abs_omt( remote.xy(), 100 ), std::numeric_limits<int>::max() );
    CHECK( view.radius == 18 );
    CHECK( view.center.z() == OVERMAP_HEIGHT );
    CHECK( view.cells.size() == 1369 );
    CHECK( std::all_of( view.cells.begin(), view.cells.end(), []( const auto & row ) {
        return !row.known && row.symbol.empty() && row.note.empty();
    } ) );
    CHECK_FALSE( static_cast<bool>( overmap_buffer.get_existing_om_global( remote ) ) );
    CHECK( rng_get_engine() == before_rng );
}
TEST_CASE( "fp3d_map_edits_native_notes_without_exploration_or_simulation", "[fp3d][map_ui]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    const auto position = you.pos_abs_omt() + tripoint_rel_omt( 2, 3, 0 );
    const auto existing = overmap_buffer.get_existing_om_global( position );
    REQUIRE( existing );
    const auto old_vision = overmap_buffer.seen( position );
    const auto old_note = overmap_buffer.note( position );
    const auto old_danger = overmap_buffer.note_danger_radius( position );
    const bool had_note = overmap_buffer.has_note( position );
    on_out_of_scope restore( [&]() {
        existing.om->set_seen( existing.local, old_vision, true );
        if( had_note ) {
            overmap_buffer.add_note( position, old_note );
            overmap_buffer.mark_note_dangerous( position, old_danger, old_danger >= 0 );
        } else {
            overmap_buffer.delete_note( position );
        }
    } );
    existing.om->set_seen( existing.local, om_vision_level::unseen, true );
    const auto clock = calendar::turn;
    const auto moves = you.get_moves();
    const auto engine = rng_get_engine();
    const auto route = you.omt_path;
    std::string error;
    const std::string first = "!:R;物资点 ## 中文标记\n待搜索";
    REQUIRE( fp3d_map::write_note( position, first, error ) );
    CHECK( error.empty() );
    CHECK( overmap_buffer.note( position ) == first );
    overmap_buffer.mark_note_dangerous( position, 3, true );
    const std::string revised = "!:G;物资已取走 <color_white>留待返回</color>";
    REQUIRE( fp3d_map::write_note( position, revised, error ) );
    CHECK( overmap_buffer.note( position ) == revised );
    CHECK( overmap_buffer.note_danger_radius( position ) == 3 );
    const auto view = fp3d_map::capture( you, position, 4 );
    const auto row = std::find_if( view.cells.begin(), view.cells.end(), [&]( const auto &entry ) {
        return entry.position == position;
    } );
    REQUIRE( row != view.cells.end() );
    CHECK( row->editable );
    CHECK_FALSE( row->known );
    CHECK( row->note_text == revised );
    CHECK( row->note == "!:G;物资已取走 留待返回" );
    CHECK( overmap_buffer.seen( position ) == om_vision_level::unseen );

    // Native overmap view serialization, with no FP3D fields or alternate note storage.
    std::ostringstream saved;
    existing.om->serialize_view( saved );
    const auto text = saved.str();
    auto restored = std::make_unique<overmap>( point_abs_om::zero );
    restore_on_out_of_scope restore_version( savegame_loading_version );
    savegame_loading_version = savegame_version;
    const JsonValue json = json_loader::from_string( text.substr( text.find( '\n' ) + 1 ) );
    restored->unserialize_view( json.get_object() );
    CHECK( restored->note( existing.local ) == revised );
    CHECK( restored->note_danger_radius( existing.local ) == 3 );
    CHECK( restored->seen( existing.local ) == om_vision_level::unseen );

    REQUIRE( fp3d_map::write_note( position, "", error ) );
    CHECK_FALSE( overmap_buffer.has_note( position ) );
    CHECK( overmap_buffer.note_danger_radius( position ) == -1 );
    REQUIRE( fp3d_map::write_note( position, "", error ) );
    CHECK_FALSE( overmap_buffer.has_note( position ) );
    CHECK( calendar::turn == clock );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == engine );
    CHECK( you.omt_path == route );
}

TEST_CASE( "fp3d_map_note_writes_reject_unloaded_regions_and_invalid_layers", "[fp3d][map_ui]" )
{
    const tripoint_abs_omt remote( 2000000, 2000000, 0 );
    REQUIRE_FALSE( static_cast<bool>( overmap_buffer.get_existing_om_global( remote ) ) );
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const auto moves = get_avatar().get_moves();
    std::string error;
    CHECK_FALSE( fp3d_map::write_note( remote, "不生成远方地形", error ) );
    CHECK_FALSE( error.empty() );
    CHECK_FALSE( fp3d_map::write_note( remote, "", error ) );
    CHECK_FALSE( static_cast<bool>( overmap_buffer.get_existing_om_global( remote ) ) );
    for( int layer : { -OVERMAP_DEPTH - 1, OVERMAP_HEIGHT + 1 } ) {
        CHECK_FALSE( fp3d_map::write_note( tripoint_abs_omt( get_avatar().pos_abs_omt().xy(), layer ),
                                         "无效楼层", error ) );
        CHECK_FALSE( error.empty() );
    }
    CHECK( rng_get_engine() == engine );
    CHECK( calendar::turn == clock );
    CHECK( get_avatar().get_moves() == moves );
}
#endif
