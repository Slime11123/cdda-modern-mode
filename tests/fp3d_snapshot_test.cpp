#include "fp3d_snapshot.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cata_catch.h"
#include "enums.h"
#include "field.h"
#include "game.h"
#include "field_type.h"
#include "item.h"
#include "item_location.h"
#include "level_cache.h"
#include "map.h"
#include "map_helpers.h"
#include "map_memory.h"
#include "mapdata.h"
#include "monster.h"
#include "npc.h"
#include "omdata.h"
#include "overmapbuffer.h"
#include "cata_scope_helpers.h"
#include "player_helpers.h"
#include "rng.h"
#include "type_id.h"

namespace
{
void refresh_vision( map &here, int z )
{
    here.invalidate_map_cache( z );
    here.invalidate_visibility_cache();
    here.build_map_cache( z );
    here.update_visibility_cache( z );
}

void prepare_scene()
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const tripoint_bub_ms p = you.pos_bub( here );
    you.prepare_map_memory_region( here.get_abs( p - tripoint_rel_ms( fp3d_snapshot::radius,
                                   fp3d_snapshot::radius, 0 ) ),
                                   here.get_abs( p + tripoint_rel_ms( fp3d_snapshot::radius,
                                           fp3d_snapshot::radius, 0 ) ) );
}

const fp3d_snapshot::cell *find_cell( const fp3d_snapshot::scene_snapshot &scene,
                                      const tripoint_abs_ms &p )
{
    const auto it = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & tile ) {
        return tile.position == p;
    } );
    return it == scene.cells.end() ? nullptr : &*it;
}
} // namespace

TEST_CASE( "fp3d_radio_tower_has_open_steel_lattice_picking_geometry", "[fp3d][geometry]" )
{
    CHECK( fp3d_snapshot::terrain_shape( "t_radio_tower" ) ==
           fp3d_snapshot::terrain_kind::obstacle );
    const auto parts = fp3d_snapshot::terrain_model_parts( "t_radio_tower" );
    REQUIRE( parts.size() == 64 ); // 4 posts, 12 rails, 48 brace proxies.
    for( const auto &part : parts ) {
        CHECK( part.width > 0.f );
        CHECK( part.depth > 0.f );
        CHECK( part.bottom >= 0.f );
        CHECK( part.top <= fp3d_camera::floor_height );
        CHECK( part.top > part.bottom );
        // The lattice must never regain the old full-tile solid cube.
        CHECK_FALSE( part.width > .80f && part.depth > .80f );
    }
    CHECK( fp3d_snapshot::terrain_model_parts( "t_dirt" ).empty() );
}

TEST_CASE( "fp3d_chainlink_gates_have_hollow_open_and_closed_models", "[fp3d][geometry]" )
{
    using namespace fp3d_snapshot;
    CHECK( is_chain_gate( "t_chaingate_c" ) );
    CHECK( is_chain_gate( "t_chaingate_l" ) );
    CHECK( is_chain_gate( "t_chaingate_o" ) );
    CHECK( is_chain_gate( "t_chickenwire_gate_c" ) );
    CHECK( is_chain_gate( "t_chickenwire_gate_o" ) );
    CHECK_FALSE( is_chain_gate( "t_door_c" ) );
    CHECK( fence_connects( "t_chainfence", "t_chaingate_c" ) );
    CHECK( fence_connects( "t_chaingate_o", "t_chainfence_posts" ) );
    CHECK( fence_connects( "t_chickenwire_fence", "t_chickenwire_gate_c" ) );
    CHECK( fence_connects( "t_chickenwire_fence", "t_chaingate_o" ) );
    CHECK_FALSE( fence_connects( "t_chainfence", "t_wooden_fence" ) );
    CHECK( chain_gate_along_x( { false, true, false, true } ) );
    CHECK_FALSE( chain_gate_along_x( { true, false, true, false } ) );
    const auto closed = chain_gate_parts( "t_chaingate_c", true );
    const auto locked = chain_gate_parts( "t_chaingate_l", true );
    const auto opened = chain_gate_parts( "t_chaingate_o", true );
    REQUIRE( closed.size() == 10 );
    CHECK( locked.size() == closed.size() + 1 );
    REQUIRE( opened.size() == 9 );
    CHECK( chain_gate_parts( "t_door_c", true ).empty() );
    CHECK( chain_gate_parts( "t_chickenwire_gate_c", true ).size() == closed.size() );
    CHECK( chain_gate_parts( "t_chickenwire_gate_o", true ).size() == opened.size() );
    CHECK( terrain_shape( "t_chainfence" ) == terrain_kind::fence );
    CHECK( terrain_shape( "t_chickenwire_fence" ) == terrain_kind::fence );
    CHECK( terrain_shape( "t_chickenwire_fence_post" ) == terrain_kind::fence );
    CHECK( terrain_shape( "t_fence_post" ) == terrain_kind::fence );
    CHECK( terrain_shape( "t_screened_porch_wall" ) == terrain_kind::fence );
    CHECK( fence_parts( { true, true, true, true }, true ).size() == 1 );
    CHECK( fence_parts( { false, true, false, true }, false ).size() == 3 );
    for( const auto &part : closed ) {
        CHECK( part.width > 0.f );
        CHECK( part.depth > 0.f );
        CHECK_FALSE( part.width > .8f && part.depth > .8f );
    }
    // The open leaf folds toward the hinge, leaving the center passage free.
    CHECK( opened[4].x < -.3f );
    CHECK( opened[5].x < -.3f );
    CHECK( std::abs( chain_gate_parts( "t_chaingate_o", false )[4].z + .43f ) < .01f );
}

TEST_CASE( "fp3d_snapshot_keeps_hidden_door_memory", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms player = you.pos_bub( here );
    for( int dy = -12; dy <= 12; ++dy ) {
        REQUIRE( here.ter_set( player + tripoint_rel_ms( 1, dy, 0 ), ter_str_id( "t_wall" ) ) );
    }
    const tripoint_bub_ms target = player + tripoint_rel_ms( 2, 0, 0 );
    const tripoint_bub_ms unknown = player + tripoint_rel_ms( 3, 0, 0 );
    REQUIRE( here.ter_set( target, ter_str_id( "t_door_c" ) ) );
    you.memorize_terrain( here.get_abs( target ), "t_door_c", 0, 0 );
    you.memorize_terrain( here.get_abs( unknown ), "", 0, 0 );
    refresh_vision( here, player.z() );
    const auto &variables = here.get_visibility_variables_cache();
    REQUIRE( here.get_visibility( here.access_cache(
                                      player.z() ).visibility_cache[target.x()][target.y()],
                                  variables ) != visibility_type::CLEAR );
    const auto before = fp3d_snapshot::capture( you, here );
    const auto *remembered = find_cell( before, here.get_abs( target ) );
    REQUIRE( remembered );
    CHECK( remembered->known == fp3d_snapshot::knowledge::remembered );
    CHECK( remembered->terrain == "t_door_c" );

    // A live change behind the wall must not reach the presentation snapshot.
    REQUIRE( here.ter_set( target, ter_str_id( "t_door_o" ) ) );
    refresh_vision( here, player.z() );
    const auto after = fp3d_snapshot::capture( you, here );
    remembered = find_cell( after, here.get_abs( target ) );
    REQUIRE( remembered );
    CHECK( remembered->known == fp3d_snapshot::knowledge::remembered );
    CHECK( remembered->terrain == "t_door_c" );
    CHECK( remembered->kind == fp3d_snapshot::terrain_kind::closed_door );
    CHECK( find_cell( after, here.get_abs( unknown ) ) == nullptr );
}

TEST_CASE( "fp3d_snapshot_obeys_visibility_cache_freshness", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms target = you.pos_bub( here ) + tripoint_rel_ms::east;
    REQUIRE( here.ter_set( target, ter_str_id( "t_door_c" ) ) );
    you.memorize_terrain( here.get_abs( target ), "t_door_c", 0, 0 );
    refresh_vision( here, target.z() );
    const auto closed = fp3d_snapshot::capture( you, here );
    const auto *door = find_cell( closed, here.get_abs( target ) );
    REQUIRE( door );
    CHECK( door->known == fp3d_snapshot::knowledge::visible );
    CHECK( door->kind == fp3d_snapshot::terrain_kind::closed_door );

    REQUIRE( here.open_door( you, target, true, false ) );
    here.invalidate_visibility_cache();
    const auto stale = fp3d_snapshot::capture( you, here );
    door = find_cell( stale, here.get_abs( target ) );
    REQUIRE( door );
    CHECK( door->known == fp3d_snapshot::knowledge::remembered );
    CHECK( door->terrain == "t_door_c" );
    CHECK( here.get_visibility_variables_cache().visibility_cache_dirty );

    refresh_vision( here, target.z() );
    const auto opened = fp3d_snapshot::capture( you, here );
    door = find_cell( opened, here.get_abs( target ) );
    REQUIRE( door );
    CHECK( door->known == fp3d_snapshot::knowledge::visible );
    CHECK( door->terrain == "t_door_o" );
    CHECK( door->kind == fp3d_snapshot::terrain_kind::open_door );
    CHECK( you.get_memorized_tile( here.get_abs( target ) ).get_ter_id() == "t_door_c" );
}

TEST_CASE( "fp3d_snapshot_classifies_visible_wall", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms wall_pos = you.pos_bub( here ) + tripoint_rel_ms::west;
    const std::string wall_id = GENERATE( "t_wall", "t_brick_wall", "t_wall_wood" );
    REQUIRE( here.ter_set( wall_pos, ter_str_id( wall_id ) ) );
    refresh_vision( here, wall_pos.z() );
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto *wall = find_cell( scene, here.get_abs( wall_pos ) );
    REQUIRE( wall );
    CHECK( wall->terrain == wall_id );
    CHECK( wall->known == fp3d_snapshot::knowledge::visible );
    CHECK( wall->kind == fp3d_snapshot::terrain_kind::wall );
}

TEST_CASE( "fp3d_snapshot_preserves_state_and_absolute_coordinates", "[fp3d]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    refresh_vision( here, you.posz() );
    const auto position = you.pos_abs();
    const auto turn = calendar::turn;
    const int moves = you.get_moves();
    const int stamina = you.get_stamina();
    const int hp = you.get_hp();
    for( int i = 0; i < 10; ++i ) {
        const auto scene = fp3d_snapshot::capture( you, here );
        CHECK( scene.origin == position );
        const auto *east = find_cell( scene, position + tripoint_rel_ms::east );
        REQUIRE( east );
        CHECK( east->position - scene.origin == tripoint_rel_ms::east );
    }
    CHECK( you.pos_abs() == position );
    CHECK( calendar::turn == turn );
    CHECK( you.get_moves() == moves );
    CHECK( you.get_stamina() == stamina );
    CHECK( you.get_hp() == hp );

    you.set_moves( 1000 );
    avatar_action::move( you, here, tripoint_rel_ms::east );
    REQUIRE( you.pos_abs() == position + tripoint_rel_ms::east );
    refresh_vision( here, you.posz() );
    const auto moved = fp3d_snapshot::capture( you, here );
    CHECK( moved.origin == you.pos_abs() );
    const auto *old_ground = find_cell( moved, position );
    REQUIRE( old_ground );
    CHECK( old_ground->position - moved.origin == tripoint_rel_ms::west );
}

TEST_CASE( "fp3d_snapshot_filters_creatures_separately_from_terrain", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto target = you.pos_bub( here ) + tripoint_rel_ms( 2, 0, 0 );
    monster &zombie = spawn_test_monster( "mon_zombie", target, false );
    here.add_item_or_charges( target, item( itype_id( "rock" ) ) );
    refresh_vision( here, target.z() );
    REQUIRE( you.sees( here, zombie ) );
    const auto position = here.get_abs( target );
    const auto has = [&]( const fp3d_snapshot::scene_snapshot & scene, const std::string & id ) {
        return std::any_of( scene.sprites.begin(), scene.sprites.end(), [&]( const auto & image ) {
            return image.position == position && image.id == id;
        } );
    };
    const auto rng_before = rng_get_engine();
    const auto visible = fp3d_snapshot::capture( you, here );
    CHECK( has( visible, "mon_zombie" ) );
    CHECK( has( visible, "rock" ) );
    const auto rock = std::find_if( visible.sprites.begin(), visible.sprites.end(),
    []( const auto & image ) {
        return image.id == "rock";
    } );
    REQUIRE( rock != visible.sprites.end() );
    CHECK( rock->height == Approx( .7f ) );
    CHECK( rng_get_engine() == rng_before );
    zombie.add_effect( efftype_id( "invisibility" ), 1_hours );
    REQUIRE_FALSE( you.sees( here, zombie ) );
    const auto cloaked = fp3d_snapshot::capture( you, here );
    const auto *ground = find_cell( cloaked, position );
    REQUIRE( ground );
    CHECK( ground->known == fp3d_snapshot::knowledge::visible );
    CHECK_FALSE( has( cloaked, "mon_zombie" ) );
    CHECK( has( cloaked, "rock" ) );
    zombie.remove_effect( efftype_id( "invisibility" ) );
    REQUIRE( here.ter_set( target - tripoint_rel_ms::east, ter_str_id( "t_wall" ) ) );
    refresh_vision( here, target.z() );
    REQUIRE_FALSE( you.sees( here, zombie ) );
    const auto blocked = fp3d_snapshot::capture( you, here );
    CHECK_FALSE( has( blocked, "mon_zombie" ) );
    CHECK_FALSE( has( blocked, "rock" ) );
}

TEST_CASE( "fp3d_snapshot_does_not_reveal_sealed_items_or_stale_sprites", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto target = you.pos_bub( here ) + tripoint_rel_ms::south;
    here.add_item_or_charges( target, item( itype_id( "rock" ) ) );
    here.furn_set( target, furn_str_id( "f_crate_c" ) );
    refresh_vision( here, target.z() );
    REQUIRE_FALSE( here.sees_some_items( target, you ) );
    const auto closed = fp3d_snapshot::capture( you, here );
    const auto *ground = find_cell( closed, here.get_abs( target ) );
    REQUIRE( ground );
    CHECK( ground->known == fp3d_snapshot::knowledge::visible );
    CHECK( closed.sprites.empty() );
    here.furn_set( target, furn_str_id( "f_crate_o" ) );
    refresh_vision( here, target.z() );
    REQUIRE( here.sees_some_items( target, you ) );
    const auto opened = fp3d_snapshot::capture( you, here );
    REQUIRE( opened.sprites.size() == 1 );
    CHECK( opened.sprites.front().id == "rock" );
    here.invalidate_visibility_cache();
    const auto stale = fp3d_snapshot::capture( you, here );
    CHECK( stale.sprites.empty() );
}

TEST_CASE( "fp3d_snapshot_copies_visible_npc_appearance_by_value", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    npc &person = spawn_npc( ( you.pos_bub( here ) + tripoint_rel_ms( 0, -2, 0 ) ).xy(),
                             "test_talker" );
    refresh_vision( here, you.posz() );
    REQUIRE( you.sees( here, person ) );
    const auto rng_before = rng_get_engine();
    const auto first = fp3d_snapshot::capture( you, here );
    const auto find_person = [&]( const fp3d_snapshot::scene_snapshot & scene ) {
        return std::find_if( scene.sprites.begin(), scene.sprites.end(), [&]( const auto & image ) {
            return image.kind == fp3d_snapshot::sprite_kind::character &&
                   image.position == person.pos_abs();
        } );
    };
    const auto appearance = find_person( first );
    REQUIRE( appearance != first.sprites.end() );
    CHECK( appearance->overlays == person.get_overlay_ids() );
    CHECK( appearance->male == person.male );
    CHECK( appearance->height == Approx( 2.16f ) );
    CHECK( rng_get_engine() == rng_before );
    const bool old_male = person.male;
    person.male = !old_male;
    const auto second = fp3d_snapshot::capture( you, here );
    const auto changed = find_person( second );
    REQUIRE( changed != second.sprites.end() );
    CHECK( changed->male != old_male );
    CHECK( appearance->male == old_male );
    CHECK( appearance->id == ( old_male ? "npc_male" : "npc_female" ) );
}

TEST_CASE( "fp3d_snapshot_respects_native_field_item_suppression", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto target = you.pos_bub( here ) + tripoint_rel_ms( 0, 2, 0 );
    here.add_item_or_charges( target, item( itype_id( "rock" ) ) );
    here.add_field( target, fd_fire, 1, 10_minutes );
    refresh_vision( here, target.z() );
    REQUIRE_FALSE( here.field_at( target ).displayed_field_type()->display_items );
    REQUIRE( here.sees_some_items( target, you ) );
    const auto fire = fp3d_snapshot::capture( you, here );
    const auto *ground = find_cell( fire, here.get_abs( target ) );
    REQUIRE( ground );
    CHECK( ground->known == fp3d_snapshot::knowledge::visible );
    CHECK( fire.sprites.empty() );
    here.remove_field( target, fd_fire );
    refresh_vision( here, target.z() );
    // Native removal first sets intensity zero; its displayed field remains
    // until normal field cleanup. The snapshot must not perform that cleanup.
    CHECK_FALSE( here.field_at( target ).displayed_field_type()->display_items );
    CHECK( fp3d_snapshot::capture( you, here ).sprites.empty() );
    here.delete_field( target, fd_fire );
    refresh_vision( here, target.z() );
    REQUIRE( here.field_at( target ).displayed_field_type()->display_items );
    const auto cleared = fp3d_snapshot::capture( you, here );
    REQUIRE( cleared.sprites.size() == 1 );
    CHECK( cleared.sprites.front().id == "rock" );
}

TEST_CASE( "fp3d_blood_capture_tracks_visible_native_fields_without_memory_leaks", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here ) + tripoint_rel_ms( 0, 2, 0 );
    here.add_field( p, fd_blood, 3, 10_minutes );
    refresh_vision( here, p.z() );
    const auto before_rng = rng_get_engine();
    const auto before_time = calendar::turn;
    const auto before_moves = you.get_moves();
    const auto bloody = fp3d_snapshot::capture( you, here );
    const auto *tile = find_cell( bloody, here.get_abs( p ) );
    REQUIRE( tile );
    CHECK( tile->blood == "fd_blood" );
    CHECK( tile->blood_intensity == 3 );
    CHECK( rng_get_engine() == before_rng );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    here.invalidate_visibility_cache();
    const auto hidden = fp3d_snapshot::capture( you, here );
    CHECK( std::none_of( hidden.cells.begin(), hidden.cells.end(), []( const auto &cell ) {
        return !cell.blood.empty();
    } ) );
    here.delete_field( p, fd_blood );
    refresh_vision( here, p.z() );
    const auto clean = fp3d_snapshot::capture( you, here );
    REQUIRE( find_cell( clean, here.get_abs( p ) ) );
    CHECK( find_cell( clean, here.get_abs( p ) )->blood.empty() );
}

TEST_CASE( "fp3d_adjacent_stair_tiles_share_one_continuous_flight", "[fp3d][stairs]" )
{
    fp3d_snapshot::scene_snapshot scene;
    for( int y = 0; y < 3; ++y ) {
        for( int x = 0; x < 2; ++x ) {
            fp3d_snapshot::cell tile;
            tile.position = tripoint_abs_ms( x, y, 0 );
            tile.terrain = "t_stairs_up";
            tile.goes_up = true;
            scene.cells.push_back( tile );
        }
    }
    fp3d_snapshot::connect_stairs( scene );
    for( const auto &tile : scene.cells ) {
        REQUIRE( tile.stairs );
        CHECK( tile.stairs->rows == 3 );
        CHECK( tile.stairs->width == 2 );
        CHECK( tile.stairs->south == 2 - tile.position.y() );
    }
    scene.cells[2].goes_up = false;
    fp3d_snapshot::connect_stairs( scene );
    CHECK_FALSE( scene.cells[2].stairs );
    REQUIRE( scene.cells.front().stairs );
    CHECK( scene.cells.front().stairs->rows == 1 );
}

TEST_CASE( "fp3d_landscape_extends_beyond_reality_bubble_without_simulation", "[fp3d][landscape]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto distant = project_to<coords::omt>( you.pos_abs() + tripoint_rel_ms( 0, -168, 0 ) );
    const auto original = overmap_buffer.ter( distant );
    on_out_of_scope restore( [&]() {
        overmap_buffer.ter_set( distant, original );
    } );
    for( const std::string &id : {
             "field", "forest", "river_center", "road_ns"
         } ) {
        overmap_buffer.ter_set( distant, oter_id( id ) );
        fp3d_snapshot::prepare_view( you, here );
        const auto rng_before = rng_get_engine();
        const auto time_before = calendar::turn;
        const auto position_before = you.pos_abs();
        const auto explored = overmap_buffer.seen( distant );
        const auto scene = fp3d_snapshot::capture( you, here );
        REQUIRE_FALSE( scene.landscape.empty() );
        bool found = false;
        bool outside_details = true;
        for( const auto &patch : scene.landscape ) {
            const auto delta = patch.position - scene.origin;
            outside_details &= delta.x() > fp3d_snapshot::radius ||
                               delta.x() + patch.width <= -fp3d_snapshot::radius ||
                               delta.y() > fp3d_snapshot::radius ||
                               delta.y() + patch.depth <= -fp3d_snapshot::radius;
            if( project_to<coords::omt>( patch.position ) == distant ) {
                found = true;
                CHECK( patch.wooded == ( id == "forest" ) );
                CHECK( patch.terrain == ( id == "forest" ? "t_dirt" : id == "road_ns" ?
                                          "t_pavement" : id == "river_center" ? "t_water_dp" : "t_grass" ) );
            }
        }
        CHECK( found );
        CHECK( outside_details );
        CHECK( std::all_of( scene.landscape.begin(), scene.landscape.end(), []( const auto & patch ) {
            return patch.width != fp3d_snapshot::landscape_patch_size ||
                   patch.depth != fp3d_snapshot::landscape_patch_size ||
                   ( patch.position.x() % fp3d_snapshot::landscape_patch_size == 0 &&
                     patch.position.y() % fp3d_snapshot::landscape_patch_size == 0 );
        } ) );
        CHECK( rng_get_engine() == rng_before );
        CHECK( calendar::turn == time_before );
        CHECK( you.pos_abs() == position_before );
        CHECK( overmap_buffer.seen( distant ) == explored );
    }
    const auto city = project_to<coords::omt>( you.pos_abs() + tripoint_rel_ms( 0, -720, 0 ) );
    const auto old_city = overmap_buffer.ter( city );
    on_out_of_scope restore_city( [&]() {
        overmap_buffer.ter_set( city, old_city );
    } );
    const auto city_type = string_id<oter_type_t>( "s_gas_1" );
    REQUIRE( city_type.is_valid() );
    const oter_id city_terrain = city_type->get_first();
    REQUIRE( city_terrain != oter_id() );
    overmap_buffer.ter_set( city, city_terrain );
    fp3d_snapshot::prepare_view( you, here );
    const auto city_rng = rng_get_engine();
    const auto distant_city = fp3d_snapshot::capture( you, here );
    CHECK( std::any_of( distant_city.landscape.begin(),
    distant_city.landscape.end(), [&]( const auto & patch ) {
        return project_to<coords::omt>( patch.position ) == city && patch.building_height > 0 &&
               !patch.ground_present;
    } ) );
    CHECK( rng_get_engine() == city_rng );
    calendar::turn = calendar::turn_zero;
    fp3d_snapshot::prepare_view( you, here );
    CHECK( fp3d_snapshot::capture( you, here ).landscape.empty() );
    set_time_to_day();
    you.add_effect( efftype_id( "blind" ), 1_hours );
    fp3d_snapshot::prepare_view( you, here );
    CHECK( fp3d_snapshot::capture( you, here ).landscape.empty() );
}

TEST_CASE( "fp3d_snapshot_includes_native_visible_distance_beyond_old_2d_preview",
           "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto ground = you.pos_bub( here ) + tripoint_rel_ms( 0, -40, 0 );
    const auto target = you.pos_bub( here ) + tripoint_rel_ms( 30, 0, 0 );
    monster &zombie = spawn_test_monster( "mon_zombie", target, false );
    refresh_vision( here, you.posz() );
    REQUIRE( you.sees( here, zombie ) );
    const auto position = you.pos_abs();
    const auto turn = calendar::turn;
    const int moves = you.get_moves();
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto *distant = find_cell( scene, here.get_abs( ground ) );
    REQUIRE( distant );
    CHECK( distant->known == fp3d_snapshot::knowledge::visible );
    CHECK( distant->terrain == here.ter( ground ).id().str() );
    CHECK( scene.cells.size() > 25 * 25 );
    CHECK( std::any_of( scene.sprites.begin(), scene.sprites.end(), [&]( const auto & image ) {
        return image.position == zombie.pos_abs() && image.id == "mon_zombie";
    } ) );
    CHECK( you.pos_abs() == position );
    CHECK( calendar::turn == turn );
    CHECK( you.get_moves() == moves );
}
TEST_CASE( "fp3d_low_plants_use_visible_or_remembered_appearance", "[fp3d][vision][plants]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here );
    const auto grass = p + tripoint_rel_ms( 0, -2, 0 );
    const auto shrub = p + tripoint_rel_ms( 2, 0, 0 );
    const auto flower = p + tripoint_rel_ms( -2, 0, 0 );
    const auto tree = p + tripoint_rel_ms( 0, 2, 0 );
    const auto sapling = p + tripoint_rel_ms( -2, -2, 0 );
    const auto short_grass = p + tripoint_rel_ms( 2, -2, 0 );
    const auto dead_grass = p + tripoint_rel_ms( 2, 2, 0 );
    here.ter_set( short_grass, ter_str_id( "t_grass" ) );
    here.ter_set( dead_grass, ter_str_id( "t_grass_dead" ) );
    REQUIRE( here.ter_set( grass, ter_str_id( "t_grass_long" ) ) );
    REQUIRE( here.ter_set( shrub, ter_str_id( "t_shrub" ) ) );
    here.furn_set( flower, furn_str_id( "f_dandelion" ) );
    REQUIRE( here.ter_set( tree, ter_str_id( "t_tree" ) ) );
    REQUIRE( here.ter_set( sapling, ter_str_id( "t_tree_young" ) ) );
    refresh_vision( here, p.z() );
    const auto position = you.pos_abs();
    const auto turn = calendar::turn;
    const int moves = you.get_moves();
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto *g = find_cell( scene, here.get_abs( grass ) );
    const auto *s = find_cell( scene, here.get_abs( shrub ) );
    const auto *f = find_cell( scene, here.get_abs( flower ) );
    const auto *t = find_cell( scene, here.get_abs( tree ) );
    const auto *short_tile = find_cell( scene, here.get_abs( short_grass ) );
    const auto *dead_tile = find_cell( scene, here.get_abs( dead_grass ) );
    REQUIRE( short_tile );
    REQUIRE( dead_tile );
    CHECK( short_tile->vegetation.empty() );
    CHECK( dead_tile->vegetation.empty() );
    CHECK( short_tile->vegetation_height == 0 );
    REQUIRE( g );
    REQUIRE( s );
    REQUIRE( f );
    REQUIRE( t );
    const auto *young = find_cell( scene, here.get_abs( sapling ) );
    REQUIRE( young );
    CHECK( young->vegetation == "t_tree_young" );
    CHECK( young->vegetation_height < t->vegetation_height );
    CHECK( young->vegetation_height > s->vegetation_height );
    CHECK( g->vegetation == "t_grass_long" );
    CHECK( s->vegetation == "t_shrub" );
    CHECK( s->vegetation_height == Approx( 1.2f ) );
    CHECK( f->vegetation == "f_dandelion" );
    CHECK( t->vegetation == "t_tree" );
    CHECK( t->kind == fp3d_snapshot::terrain_kind::floor );
    CHECK( t->vegetation_height > s->vegetation_height );
    CHECK( g->vegetation_height < s->vegetation_height );
    CHECK( you.pos_abs() == position );
    CHECK( calendar::turn == turn );
    CHECK( you.get_moves() == moves );
    you.memorize_terrain( here.get_abs( shrub ), "t_shrub", 0, 0 );
    you.memorize_terrain( here.get_abs( flower ), "t_floor", 0, 0 );
    // New live furniture must not leak through a stale visibility cache.
    here.furn_set( flower, furn_str_id( "f_flower_tulip" ) );
    here.invalidate_visibility_cache();
    const auto hidden = fp3d_snapshot::capture( you, here );
    const auto *remembered = find_cell( hidden, here.get_abs( shrub ) );
    REQUIRE( remembered );
    CHECK( remembered->known == fp3d_snapshot::knowledge::remembered );
    CHECK( remembered->vegetation == "t_shrub" );
    const auto *hidden_flower = find_cell( hidden, here.get_abs( flower ) );
    REQUIRE( hidden_flower );
    CHECK( hidden_flower->vegetation.empty() );
}
TEST_CASE( "fp3d_held_item_follows_equipment_without_advancing_simulation", "[fp3d][held_item]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    you.remove_weapon();
    CHECK_FALSE( fp3d_snapshot::capture( you, here ).held_item );
    item knife( itype_id( "knife_combat" ) );
    REQUIRE( you.wield( knife ) );
    const auto position = you.pos_abs();
    const auto turn = calendar::turn;
    const int moves = you.get_moves();
    const int stamina = you.get_stamina();
    const auto rng_before = rng_get_engine();
    const auto first = fp3d_snapshot::capture( you, here );
    REQUIRE( first.held_item );
    CHECK( first.held_item->id == "knife_combat" );
    CHECK( first.held_item->kind == fp3d_snapshot::sprite_kind::held_item );
    CHECK( you.pos_abs() == position );
    CHECK( calendar::turn == turn );
    CHECK( you.get_moves() == moves );
    CHECK( you.get_stamina() == stamina );
    CHECK( rng_get_engine() == rng_before );
    CHECK( you.get_wielded_item()->typeId() == itype_id( "knife_combat" ) );
    you.remove_weapon();
    CHECK_FALSE( fp3d_snapshot::capture( you, here ).held_item );
    // The previous snapshot retains owned appearance after the item is removed.
    CHECK( first.held_item->id == "knife_combat" );
    item hammer( itype_id( "hammer" ) );
    REQUIRE( you.wield( hammer ) );
    const auto second = fp3d_snapshot::capture( you, here );
    REQUIRE( second.held_item );
    CHECK( second.held_item->id == "hammer" );
    CHECK( first.held_item->id == "knife_combat" );
    here.invalidate_visibility_cache();
    CHECK( fp3d_snapshot::capture( you, here ).held_item.has_value() );
    you.add_effect( efftype_id( "blind" ), 1_minutes );
    CHECK_FALSE( fp3d_snapshot::capture( you, here ).held_item );
    you.remove_effect( efftype_id( "blind" ) );
    REQUIRE_FALSE( you.is_blind() );
    CHECK( fp3d_snapshot::capture( you, here ).held_item.has_value() );
}
TEST_CASE( "fp3d_view_requires_neither_2d_exploration_nor_2d_visibility_mask", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto target = you.pos_bub( here ) + tripoint_rel_ms( 25, 0, 0 );
    const auto absolute = here.get_abs( target );
    you.memorize_terrain( absolute, "", 0, 0 );
    here.invalidate_map_cache( you.posz() );
    here.invalidate_visibility_cache();
    const auto position = you.pos_abs();
    const auto turn = calendar::turn;
    const int moves = you.get_moves();
    const auto rng_before = rng_get_engine();
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE( you.sees( here, target ) );
    REQUIRE( you.get_memorized_tile( absolute ).get_ter_id().empty() );
    // Simulate a hidden/undrawn 2D cell without changing the native LOS/light.
    here.access_cache( target.z() ).visibility_cache[target.x()][target.y()] = lit_level::DARK;
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto *tile = find_cell( scene, absolute );
    REQUIRE( tile );
    CHECK( tile->known == fp3d_snapshot::knowledge::visible );
    CHECK( tile->terrain == here.ter( target ).id().str() );
    CHECK( you.get_memorized_tile( absolute ).get_ter_id().empty() );
    CHECK( you.pos_abs() == position );
    CHECK( calendar::turn == turn );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == rng_before );
    set_time( calendar::turn_zero + 1_hours );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE_FALSE( you.sees( here, target ) );
    const auto night_scene = fp3d_snapshot::capture( you, here );
    const auto *night_tile = find_cell( night_scene, absolute );
    REQUIRE( night_tile );
    CHECK( night_tile->known == fp3d_snapshot::knowledge::visible );
    CHECK_FALSE( night_tile->exterior_only );
    CHECK_FALSE( you.sees( here, target ) );
}

TEST_CASE( "fp3d_dark_contents_use_los_without_changing_native_perception", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    const auto target = player + tripoint_rel_ms( 15, 0, 0 );
    monster &zombie = spawn_test_monster( "mon_zombie", target, false );
    here.furn_set( target, furn_str_id( "f_table" ) );
    here.add_item_or_charges( target, item( itype_id( "rock" ) ) );
    set_time( calendar::turn_zero + 1_hours );
    refresh_vision( here, player.z() );
    REQUIRE_FALSE( you.sees( here, target ) );
    REQUIRE_FALSE( you.sees( here, zombie ) );
    const auto position = here.get_abs( target );
    const auto has = [&]( const fp3d_snapshot::scene_snapshot & scene, const std::string & id ) {
        return std::any_of( scene.sprites.begin(), scene.sprites.end(), [&]( const auto & image ) {
            return image.position == position && image.id == id;
        } );
    };
    const auto rng = rng_get_engine();
    const auto time = calendar::turn;
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto *tile = find_cell( scene, position );
    REQUIRE( tile );
    CHECK( tile->furniture.has_value() );
    CHECK_FALSE( tile->exterior_only );
    CHECK( has( scene, "rock" ) );
    CHECK( has( scene, "mon_zombie" ) );
    CHECK_FALSE( you.sees( here, target ) );
    CHECK_FALSE( you.sees( here, zombie ) );
    CHECK( rng_get_engine() == rng );
    CHECK( calendar::turn == time );

    zombie.add_effect( efftype_id( "invisibility" ), 1_hours );
    CHECK_FALSE( has( fp3d_snapshot::capture( you, here ), "mon_zombie" ) );
    zombie.remove_effect( efftype_id( "invisibility" ) );

    for( int dy = -20; dy <= 20; ++dy ) {
        here.ter_set( player + tripoint_rel_ms( 7, dy, 0 ), ter_str_id( "t_wall" ) );
    }
    refresh_vision( here, player.z() );
    const auto blocked = fp3d_snapshot::capture( you, here );
    CHECK_FALSE( has( blocked, "rock" ) );
    CHECK_FALSE( has( blocked, "mon_zombie" ) );
    const auto *hidden = find_cell( blocked, position );
    CHECK( ( hidden == nullptr || !hidden->furniture.has_value() ) );
}

TEST_CASE( "fp3d_radio_tower_upper_walkway_is_metal_from_ground",
           "[fp3d][multilevel][radio_tower]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    REQUIRE( here.supports_zlevels() );
    const auto ground = you.pos_bub( here );
    const auto metal = ground + tripoint_rel_ms( 4, 0, 1 );
    const auto frame = ground + tripoint_rel_ms( 6, 0, 1 );
    const auto rail = ground + tripoint_rel_ms( 6, 1, 1 );
    // A connected 3x3 exterior deck: the middle metal panel has no
    // adjacent open-air cells, but the paved surface directly beneath it
    // is visible from the ground. It must not turn into neutral t_floor.
    for( int dx = 3; dx <= 5; ++dx ) {
        for( int dy = -1; dy <= 1; ++dy ) {
            REQUIRE( here.ter_set( ground + tripoint_rel_ms( dx, dy, 1 ),
                                   ter_str_id( "t_metal_floor_no_roof" ) ) );
        }
    }
    // Native military radio towers use an open steel frame surrounded by
    // elevated metal flooring, not an ordinary wooden upper-storey ceiling.
    REQUIRE( here.ter_set( frame, ter_str_id( "t_radio_tower" ) ) );
    REQUIRE( here.ter_set( rail, ter_str_id( "t_railing" ) ) );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here );
    CHECK( scene.max_z >= 1 );
    const auto *walkway = find_cell( scene, here.get_abs( metal ) );
    REQUIRE( walkway );
    CHECK( walkway->terrain == "t_metal_floor_no_roof" );
    CHECK( walkway->floor_present );
    // Open tower decks have no invented ceiling above an actual sky cell.
    CHECK_FALSE( walkway->ceiling_present );
    // From ground level, a missing upper LOS endpoint must not replace the
    // real metal deck underside with the generic wooden t_floor texture.
    const auto *under = find_cell( scene,
                                  here.get_abs( ground + tripoint_rel_ms( 4, 0, 0 ) ) );
    REQUIRE( under );
    CHECK( under->ceiling_present );
    CHECK( under->ceiling_surface == "t_metal_floor_no_roof" );
    const auto *steel = find_cell( scene, here.get_abs( frame ) );
    REQUIRE( steel );
    CHECK( steel->terrain == "t_radio_tower" );
    const auto *guardrail = find_cell( scene, here.get_abs( rail ) );
    REQUIRE( guardrail );
    CHECK( guardrail->kind == fp3d_snapshot::terrain_kind::fence );
}

TEST_CASE( "fp3d_upper_facades_enclose_a_multistorey_building", "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto origin = you.pos_bub( here );
    for( int z = 0; z < 4; ++z ) {
        for( int y = -3; y <= 3; ++y ) {
            for( int x = 4; x <= 9; ++x ) {
                const bool wall = x == 4 || x == 9 || std::abs( y ) == 3;
                const bool window = x == 4 && std::abs( y ) == 1;
                here.ter_set( origin + tripoint_rel_ms( x, y, z ),
                              ter_str_id( window ? "t_window" : wall ? "t_wall" : "t_floor" ) );
            }
        }
    }
    for( int y = -3; y <= 3; ++y ) {
        for( int x = 4; x <= 9; ++x ) {
            here.ter_set( origin + tripoint_rel_ms( x, y, 4 ), ter_str_id( "t_flat_roof" ) );
        }
    }
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here );
    for( int z = 1; z < 4; ++z ) {
        for( int y = -3; y <= 3; ++y ) {
            CAPTURE( z, y );
            const auto *tile = find_cell( scene, here.get_abs( origin + tripoint_rel_ms( 4, y, z ) ) );
            REQUIRE( tile );
            CHECK( tile->kind == ( std::abs( y ) == 1 ? fp3d_snapshot::terrain_kind::window :
                                   fp3d_snapshot::terrain_kind::wall ) );
        }
    }
}

TEST_CASE( "fp3d_multistorey_visibility_reaches_upper_floors_and_ground",
           "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    REQUIRE( here.supports_zlevels() );
    const auto ground = you.pos_bub( here );
    // A vertical facade taller than the old four-storey snapshot budget.
    for( int z = 0; z <= 8; ++z ) {
        REQUIRE( here.ter_set( ground + tripoint_rel_ms( 5, 0, z ),
                               ter_str_id( "t_wall" ) ) );
    }
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    const auto from_ground = fp3d_snapshot::capture( you, here );
    CHECK( from_ground.min_z <= 0 );
    CHECK( from_ground.max_z >= 8 );
    const auto *high_facade = find_cell( from_ground,
                                        here.get_abs( ground + tripoint_rel_ms( 5, 0, 8 ) ) );
    REQUIRE( high_facade );
    CHECK( high_facade->kind == fp3d_snapshot::terrain_kind::wall );

    // At fourth floor, z=0 must remain captured rather than turning into
    // empty sky. The native LOS/depth test still controls which cells draw.
    const auto upper = ground + tripoint_rel_ms( 0, 0, 4 );
    REQUIRE( here.ter_set( upper, ter_str_id( "t_floor" ) ) );
    for( int z = 1; z <= 4; ++z ) {
        REQUIRE( here.ter_set( ground + tripoint_rel_ms( 3, 0, z ),
                               ter_str_id( "t_open_air" ) ) );
    }
    const auto pavement = ground + tripoint_rel_ms( 3, 0, 0 );
    REQUIRE( here.ter_set( pavement, ter_str_id( "t_pavement" ) ) );
    you.setpos( here, upper );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    const auto from_fourth = fp3d_snapshot::capture( you, here );
    CHECK( from_fourth.origin.z() == upper.z() );
    CHECK( from_fourth.min_z <= ground.z() );
    CHECK( from_fourth.max_z >= upper.z() );
    CHECK( find_cell( from_fourth, here.get_abs( pavement ) ) != nullptr );
}

TEST_CASE( "fp3d_multilevel_native_visibility_and_memory", "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    REQUIRE( here.supports_zlevels() );
    const auto player = you.pos_bub( here );
    // An exposed facade has three real storeys, not three overlapping copies.
    for( int z = 0; z <= 2; ++z ) {
        REQUIRE( here.ter_set( player + tripoint_rel_ms( 4, 0, z ), ter_str_id( "t_wall" ) ) );
    }
    const auto roof = player + tripoint_rel_ms( 4, 0, 3 );
    REQUIRE( here.ter_set( roof, ter_str_id( "t_flat_roof" ) ) );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    const auto origin = you.pos_abs();
    const auto turn = calendar::turn;
    const int moves = you.get_moves();
    const auto rng = rng_get_engine();
    auto scene = fp3d_snapshot::capture( you, here );
    CHECK( scene.min_z == -2 );
    CHECK( scene.max_z == fp3d_snapshot::render_above );
    for( int z = 0; z <= 2; ++z ) {
        const auto p = player + tripoint_rel_ms( 4, 0, z );
        INFO( "facade z=" << z << " seen=" << here.access_cache( z ).seen_cache[p.x()][p.y()]
              << " light=" << here.ambient_light_at( p ) << " los=" << here.sees( player, p, 60 ) );
        REQUIRE( you.Creature::sees( here, p, false, you.unimpaired_range() ) );
        const auto *tile = find_cell( scene, here.get_abs( p ) );
        REQUIRE( tile );
        CHECK( tile->kind == fp3d_snapshot::terrain_kind::wall );
        CHECK( tile->position.z() == z );
    }
    const auto *roof_tile = find_cell( scene, here.get_abs( roof ) );
    REQUIRE( roof_tile );
    CHECK( roof_tile->terrain == "t_flat_roof" );
    CHECK( roof_tile->floor_present );
    CHECK( find_cell( scene, here.get_abs( roof + tripoint_rel_ms( 0, 0, 1 ) ) ) == nullptr );
    const auto air = player + tripoint_rel_ms( 2, 0, 1 );
    REQUIRE( you.sees( here, air ) );
    const auto *gap = find_cell( scene, here.get_abs( air ) );
    // Empty air is sparse: absence means no synthetic surface or ceiling.
    CHECK( gap == nullptr );
    // An unknown sealed room upstairs must not be disclosed or updated live.
    const auto hidden = player + tripoint_rel_ms( 8, 0, 1 );
    for( int dy = -2; dy <= 2; ++dy ) {
        for( int dx = -2; dx <= 2; ++dx ) {
            REQUIRE( here.ter_set( hidden + tripoint_rel_ms( dx, dy, 0 ), ter_str_id( "t_floor" ) ) );
            if( std::abs( dx ) == 2 || std::abs( dy ) == 2 ) {
                REQUIRE( here.ter_set( hidden + tripoint_rel_ms( dx, dy, 0 ), ter_str_id( "t_wall" ) ) );
            }
        }
    }
    you.memorize_terrain( here.get_abs( hidden ), "", 0, 0 );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE_FALSE( you.sees( here, hidden ) );
    scene = fp3d_snapshot::capture( you, here );
    CHECK( find_cell( scene, here.get_abs( hidden ) ) == nullptr );
    you.memorize_terrain( here.get_abs( hidden ), "t_stairs_down", 0, 0 );
    REQUIRE( here.ter_set( hidden, ter_str_id( "t_open_air" ) ) );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE_FALSE( you.sees( here, hidden ) );
    scene = fp3d_snapshot::capture( you, here );
    const auto *memory = find_cell( scene, here.get_abs( hidden ) );
    REQUIRE( memory );
    CHECK( memory->known == fp3d_snapshot::knowledge::remembered );
    CHECK( memory->terrain == "t_stairs_down" );
    CHECK( memory->goes_down );
    CHECK( memory->floor_present );
    CHECK_FALSE( memory->open_air );
    you.memorize_terrain( here.get_abs( hidden ), "t_floor", 0, 0 );
    scene = fp3d_snapshot::capture( you, here );
    memory = find_cell( scene, here.get_abs( hidden ) );
    REQUIRE( memory );
    CHECK( memory->known == fp3d_snapshot::knowledge::remembered );
    CHECK( memory->ceiling_present );

    CHECK( you.pos_abs() == origin );
    CHECK( calendar::turn == turn );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == rng );
    set_time( calendar::turn_zero + 1_hours );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    const auto dark = fp3d_snapshot::capture( you, here );
    const auto *night_wall = find_cell( dark, here.get_abs( player + tripoint_rel_ms( 4, 0, 1 ) ) );
    REQUIRE( night_wall );
    CHECK( night_wall->kind == fp3d_snapshot::terrain_kind::wall );
    set_time_to_day();
    player_wear_blindfold();
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    const auto blind = fp3d_snapshot::capture( you, here );
    CHECK( find_cell( blind, here.get_abs( player + tripoint_rel_ms( 4, 0, 1 ) ) ) == nullptr );
}

TEST_CASE( "fp3d_multilevel_origin_and_basement", "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto surface = you.pos_bub( here );
    const auto basement = surface + tripoint_rel_ms( 0, 0, -1 );
    REQUIRE( here.ter_set( basement, ter_str_id( "t_floor" ) ) );
    REQUIRE( here.ter_set( basement + tripoint_rel_ms::east, ter_str_id( "t_stairs_up" ) ) );
    you.setpos( here, basement );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    auto scene = fp3d_snapshot::capture( you, here );
    CHECK( scene.origin.z() == -1 );
    CHECK( scene.min_z == -3 );
    CHECK( scene.max_z == 9 );
    const auto *stairs = find_cell( scene, here.get_abs( basement + tripoint_rel_ms::east ) );
    REQUIRE( stairs );
    CHECK( stairs->goes_up );
    CHECK( stairs->floor_present );
    CHECK_FALSE( scene.sky.visible );
    // Rebasing the presentation never moves the native character.
    CHECK( you.pos_bub( here ) == basement );
    REQUIRE( here.ter_set( surface + tripoint_rel_ms( 0, 0, 1 ), ter_str_id( "t_floor" ) ) );
    you.setpos( here, surface + tripoint_rel_ms( 0, 0, 1 ) );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    scene = fp3d_snapshot::capture( you, here );
    CHECK( scene.origin.z() == 1 );
    CHECK( you.pos_bub( here ).z() == 1 );
}

TEST_CASE( "fp3d_visible_ceiling_boundary_keeps_upstairs_private", "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here );
    const auto overhead = p + tripoint_rel_ms( 0, 0, 1 );
    REQUIRE( here.ter_set( overhead, ter_str_id( "t_floor" ) ) );
    you.memorize_terrain( here.get_abs( overhead ), "", 0, 0 );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE_FALSE( you.sees( here, overhead ) );
    const auto enclosed = fp3d_snapshot::capture( you, here );
    const auto *room = find_cell( enclosed, here.get_abs( p ) );
    REQUIRE( room );
    CHECK( room->ceiling_present );
    CHECK( find_cell( enclosed, here.get_abs( overhead ) ) == nullptr );
    CHECK( you.get_memorized_tile( here.get_abs( overhead ) ).get_ter_id().empty() );
    REQUIRE( here.ter_set( overhead, ter_str_id( "t_open_air_rooved" ) ) );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    const auto opened = fp3d_snapshot::capture( you, here );
    room = find_cell( opened, here.get_abs( p ) );
    REQUIRE( room );
    CHECK_FALSE( room->ceiling_present );

    // Being indoors does not put a ceiling across a known open-air volume.
    REQUIRE( here.ter_set( p, ter_str_id( "t_floor" ) ) );
    REQUIRE( here.ter_set( overhead, ter_str_id( "t_open_air" ) ) );
    refresh_vision( here, p.z() );
    fp3d_snapshot::prepare_view( you, here );
    CHECK_FALSE( here.has_floor( overhead ) );
    CHECK_FALSE( here.is_outside( p ) );
    const auto indoor_open = fp3d_snapshot::capture( you, here );
    room = find_cell( indoor_open, here.get_abs( p ) );
    REQUIRE( room );
    CHECK_FALSE( room->ceiling_present );
}

TEST_CASE( "fp3d_upstairs_atrium_does_not_cover_the_visible_lower_room", "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto origin = you.pos_bub( here );
    for( int y = -5; y <= 1; ++y ) {
        for( int x = -3; x <= 3; ++x ) {
            const bool edge = std::abs( x ) == 3 || y == -5 || y == 1;
            const bool opening = std::abs( x ) <= 1 && y >= -4 && y <= -1;
            here.ter_set( origin + tripoint_rel_ms( x, y, 0 ), ter_str_id( edge ? "t_wall" : "t_floor" ) );
            here.ter_set( origin + tripoint_rel_ms( x, y, 1 ), ter_str_id(
                              edge ? "t_wall" : opening ? "t_open_air" : "t_floor" ) );
            here.ter_set( origin + tripoint_rel_ms( x, y, 2 ), ter_str_id( "t_flat_roof" ) );
        }
    }
    you.setpos( here, origin + tripoint_rel_ms::above );
    const auto lower = origin + tripoint_rel_ms( 0, -2, 0 );
    const auto opening = lower + tripoint_rel_ms::above;
    here.add_item_or_charges( lower, item( itype_id( "rock" ) ) );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE_FALSE( here.has_floor( opening ) );
    REQUIRE_FALSE( here.is_outside( lower ) );
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto *floor = find_cell( scene, here.get_abs( lower ) );
    REQUIRE( floor );
    CHECK( floor->floor_present );
    CHECK_FALSE( floor->ceiling_present );
    for( int y = -4; y <= -1; ++y ) {
        for( int x = -1; x <= 1; ++x ) {
            const auto *surface = find_cell( scene, here.get_abs( origin + tripoint_rel_ms( x, y, 0 ) ) );
            REQUIRE( surface );
            CHECK( surface->floor_present );
            CHECK_FALSE( surface->ceiling_present );
        }
    }
    CHECK( std::any_of( scene.sprites.begin(), scene.sprites.end(), [&]( const auto &image ) {
        return image.position == here.get_abs( lower ) && image.id == "rock";
    } ) );
    if( const auto *air = find_cell( scene, here.get_abs( opening ) ) ) {
        CHECK_FALSE( air->floor_present );
        CHECK( air->ceiling_present ); // The real roof remains one storey higher.
    }
    REQUIRE( here.ter_set( opening, ter_str_id( "t_floor" ) ) );
    here.invalidate_visibility_cache();
    fp3d_snapshot::prepare_view( you, here );
    const auto closed = fp3d_snapshot::capture( you, here );
    if( const auto *room = find_cell( closed, here.get_abs( lower ) ) ) {
        CHECK( room->ceiling_present );
    }
}

TEST_CASE( "fp3d_multilevel_capture_budget", "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    std::ofstream report;
    if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
        std::filesystem::create_directories( directory );
        report.open( std::filesystem::path( directory ) / "capture-budget.csv" );
        report << "storeys,above,below,prepare_ms,capture_ms,cells,estimated_owned_bytes\n";
    }
    const auto player = you.pos_bub( here );
    for( int storeys : {
             1, 3, 5
         } ) {
        for( int z = 0; z < storeys; ++z ) {
            for( int x = -8; x <= 8; ++x ) {
                here.ter_set( player + tripoint_rel_ms( x, -12, z ), ter_str_id( "t_wall" ) );
                REQUIRE( here.ter( player + tripoint_rel_ms( x, -12, z ) ) == ter_str_id( "t_wall" ) );
            }
        }
        here.invalidate_visibility_cache();
        const auto started = std::chrono::steady_clock::now();
        fp3d_snapshot::prepare_view( you, here );
        const double prepare_ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - started ).count();
        for( int above : {
                 0, fp3d_snapshot::render_above
             } ) {
            const auto scene = fp3d_snapshot::capture( you, here, above, above ? 2 : 0 );
            CHECK( scene.max_z == above );
            CHECK( scene.capture_ms >= 0 );
            if( report ) {
                report << storeys << ',' << above << ',' << ( above ? 2 : 0 ) << ','
                       << prepare_ms << ',' << scene.capture_ms << ',' << scene.cells.size()
                       << ',' << scene.cells.capacity() * sizeof( fp3d_snapshot::cell ) << '\n';
            }
        }
    }
    const auto bounded = fp3d_snapshot::capture( you, here, 100, 100 );
    CHECK( bounded.max_z == fp3d_snapshot::render_above );
    CHECK( bounded.min_z == -fp3d_snapshot::below_for_storey( player.z() ) );
}

TEST_CASE( "fp3d_native_stair_actions_keep_presentation_in_sync", "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto ground = you.pos_bub( here );
    REQUIRE( here.ter_set( ground, ter_str_id( "t_stairs_up" ) ) );
    REQUIRE( here.ter_set( ground + tripoint_rel_ms( 0, 0, 1 ), ter_str_id( "t_stairs_down" ) ) );
    you.set_moves( 1000 );
    g->vertical_move( 1, false );
    REQUIRE( you.posz() == 1 );
    CHECK( you.get_moves() < 1000 );
    const auto upstairs = you.pos_abs();
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here );
    CHECK( scene.origin == upstairs );
    CHECK( you.pos_abs() == upstairs );
    g->vertical_move( -1, false );
    REQUIRE( you.posz() == 0 );
    fp3d_snapshot::prepare_view( you, here );
    CHECK( fp3d_snapshot::capture( you, here ).origin == you.pos_abs() );
}

TEST_CASE( "fp3d_batched_perception_preserves_los_across_lighting", "[fp3d][vision][performance]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    for( int y = -15; y <= 15; ++y ) {
        here.ter_set( player + tripoint_rel_ms( 8, y, 0 ), ter_str_id( "t_wall" ) );
    }
    here.ter_set( player + tripoint_rel_ms( 4, 0, 1 ), ter_str_id( "t_wall" ) );
    std::unordered_map<tripoint_abs_ms, bool> daylight_visibility;
    for( int scenario = 0; scenario < 6; ++scenario ) {
        if( scenario == 1 ) {
            set_time( calendar::turn_zero + 1_hours );
        } else if( scenario == 2 ) {
            set_time_to_day();
            player_wear_blindfold();
        }
        if( scenario >= 3 ) {
            clear_avatar();
            you.setpos( here, player );
            set_time_to_day();
            clear_fields( 0 );
            if( scenario == 3 ) {
                for( int y = -4; y <= 4; ++y ) {
                    here.add_field( player + tripoint_rel_ms( 4, y, 0 ), field_type_str_id( "fd_smoke" ), 3 );
                }
            } else if( scenario == 4 ) {
                you.set_mutation( trait_id( "DEBUG_CLAIRVOYANCE" ) );
                you.recalc_sight_limits();
            }
        }
        here.invalidate_visibility_cache();
        fp3d_snapshot::prepare_view( you, here );
        if( scenario == 5 ) {
            const auto camera = player + tripoint_rel_ms( 10, 0, 0 );
            here.access_cache( 0 ).camera_cache[camera.x()][camera.y()] = 1;
            REQUIRE( you.sees( here, camera ) );
        }
        const auto scene = fp3d_snapshot::capture( you, here );
        std::unordered_map<tripoint_abs_ms, const fp3d_snapshot::cell *> cells;
        for( const auto &tile : scene.cells ) {
            cells.emplace( tile.position, &tile );
        }
        unsigned mismatches = 0;
        for( int z = 0; z <= 1; ++z ) {
            for( int y = -60; y <= 60; ++y ) {
                for( int x = -60; x <= 60; ++x ) {
                    const auto p = player + tripoint_rel_ms( x, y, z );
                    if( !here.inbounds( p ) || here.is_open_air( p ) ) {
                        continue;
                    }
                    const auto tile = cells.find( here.get_abs( p ) );
                    const bool captured = tile != cells.end() &&
                                          tile->second->known == fp3d_snapshot::knowledge::visible &&
                                          !tile->second->exterior_only;
                    const auto absolute = here.get_abs( p );
                    if( scenario == 0 ) {
                        daylight_visibility.emplace( absolute, captured );
                    }
                    const bool expected = scenario == 1 ? daylight_visibility.at( absolute ) :
                                          scenario == 2 ? false : you.sees( here, p );
                    mismatches += captured != expected;
                }
            }
        }
        INFO( "perception scenario=" << scenario );
        CHECK( mismatches == 0 );
    }
}

TEST_CASE( "fp3d_moving_capture_budget", "[fp3d][performance]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto start = you.pos_bub( here );
    for( int n = 0; n < 6; ++n ) {
        spawn_npc( ( start + tripoint_rel_ms( n * 2 - 5, -8, 0 ) ).xy(), "test_talker" );
    }
    std::ofstream report;
    if( const char *directory = std::getenv( "CDDA_FP3D_RENDER_REPORT_DIR" ) ) {
        report.open( std::filesystem::path( directory ) / "moving-capture.csv" );
        report << "step,prepare_ms,capture_ms,cells,sprites\n";
    }
    for( int step = 0; step < 12; ++step ) {
        const auto before = you.pos_abs();
        you.set_moves( 1000 );
        avatar_action::move( you, here, tripoint_rel_ms::east );
        REQUIRE( you.pos_abs() == before + tripoint_rel_ms::east );
        const auto started = std::chrono::steady_clock::now();
        fp3d_snapshot::prepare_view( you, here );
        const double prepare_ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - started ).count();
        const auto rng = rng_get_engine();
        const auto scene = fp3d_snapshot::capture( you, here );
        CHECK( scene.origin == you.pos_abs() );
        CHECK( rng_get_engine() == rng );
        if( report ) {
            report << step << ',' << prepare_ms << ',' << scene.capture_ms << ',' << scene.cells.size()
                   << ',' << scene.sprites.size() << '\n';
        }
    }
}

TEST_CASE( "fp3d_window_states_and_hidden_memory", "[fp3d][windows]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::east;
    using pane = fp3d_snapshot::window_pane;
    for( const auto &entry : std::vector<std::pair<std::string, pane>> {
    { "t_window", pane::glazed }, { "t_window_open", pane::opening },
    { "t_window_frame", pane::opening }, { "t_curtains", pane::covered },
    { "t_window_boarded", pane::covered }, { "t_window_empty_curtains_open", pane::opening }
} ) {
        REQUIRE( here.ter_set( p, ter_str_id( entry.first ) ) );
        refresh_vision( here, you.posz() );
        const auto scene = fp3d_snapshot::capture( you, here );
        const auto *window = find_cell( scene, here.get_abs( p ) );
        INFO( entry.first );
        REQUIRE( window );
        CHECK( window->kind == fp3d_snapshot::terrain_kind::window );
        CHECK( window->pane == entry.second );
    }
    for( int y = -12; y <= 12; ++y ) {
        here.ter_set( p + tripoint_rel_ms( 0, y, 0 ), ter_str_id( "t_wall" ) );
    }
    const auto hidden = p + tripoint_rel_ms::east;
    REQUIRE( here.ter_set( hidden, ter_str_id( "t_window_open" ) ) );
    you.memorize_terrain( here.get_abs( hidden ), "t_window", 0, 0 );
    refresh_vision( here, you.posz() );
    REQUIRE_FALSE( you.sees( here, hidden ) );
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto *window = find_cell( scene, here.get_abs( hidden ) );
    REQUIRE( window );
    CHECK( window->known == fp3d_snapshot::knowledge::remembered );
    CHECK( window->pane == pane::glazed );
}

TEST_CASE( "fp3d_visible_outdoors_supplies_window_sky", "[fp3d][windows]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here );
    for( int x = -2; x <= 2; ++x ) {
        for( int y = -2; y <= 2; ++y ) {
            here.ter_set( p + tripoint_rel_ms( x, y, 0 ), ter_str_id( "t_floor" ) );
            if( std::abs( x ) == 2 || std::abs( y ) == 2 ) {
                here.ter_set( p + tripoint_rel_ms( x, y, 0 ), ter_str_id( "t_wall" ) );
            }
        }
    }
    const auto window = p + tripoint_rel_ms( 0, -2, 0 );
    here.ter_set( window, ter_str_id( "t_window" ) );
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE_FALSE( here.is_outside( p ) );
    const auto exterior = p + tripoint_rel_ms( 0, -5, 0 );
    REQUIRE( you.sees( here, exterior ) );
    CHECK( fp3d_snapshot::capture( you, here ).sky.visible );
    here.ter_set( window, ter_str_id( "t_curtains" ) );
    refresh_vision( here, you.posz() );
    REQUIRE_FALSE( you.sees( here, exterior ) );
    CHECK_FALSE( fp3d_snapshot::capture( you, here ).sky.visible );
}
TEST_CASE( "fp3d_dark_indoor_surfaces_and_contents_remain_visible", "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto origin = you.pos_bub( here );
    for( int y = -8; y <= 8; ++y ) {
        for( int x = -8; x <= 8; ++x ) {
            const auto p = origin + tripoint_rel_ms( x, y, 0 );
            here.ter_set( p, ter_str_id( std::abs( x ) == 8 || std::abs( y ) == 8 ? "t_wall" : "t_floor" ) );
            here.ter_set( p + tripoint_rel_ms::above, ter_str_id( "t_floor" ) );
        }
    }
    const auto target = origin + tripoint_rel_ms( 0, -6, 0 );
    here.add_item_or_charges( target, item( itype_id( "rock" ) ) );
    refresh_vision( here, origin.z() + 1 );
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE_FALSE( here.is_outside( target ) );
    REQUIRE_FALSE( you.sees( here, target ) );
    const auto before = rng_get_engine();
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto *surface = find_cell( scene, here.get_abs( target ) );
    REQUIRE( surface );
    CHECK( surface->floor_present );
    CHECK( surface->ceiling_present );
    CHECK_FALSE( surface->exterior_only );
    const auto wall_position = origin + tripoint_rel_ms( 0, -8, 0 );
    const auto *wall = find_cell( scene, here.get_abs( wall_position ) );
    REQUIRE( wall );
    CHECK( wall->kind == fp3d_snapshot::terrain_kind::wall );

    CHECK( std::any_of( scene.sprites.begin(), scene.sprites.end(), [&]( const auto &sprite ) {
        return sprite.position == here.get_abs( target ) && sprite.id == "rock";
    } ) );
    CHECK( rng_get_engine() == before );
}
#endif
