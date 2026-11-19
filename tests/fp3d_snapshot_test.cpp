#include "fp3d_snapshot.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>

#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cached_options.h"
#include "cata_catch.h"
#include "enums.h"
#include "field.h"
#include "game.h"
#include "field_type.h"
#include "fp3d_camera.h"
#include "fp3d_materials.h"
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

// Perception assertions for world actors and items omit the overhead avatar
// and independently tested native map fields.
size_t world_sprite_count( const fp3d_snapshot::scene_snapshot &scene )
{
    return static_cast<size_t>( std::count_if( scene.sprites.begin(), scene.sprites.end(),
    []( const auto & sprite ) {
        return !sprite.is_avatar && sprite.kind != fp3d_snapshot::sprite_kind::field;
    } ) );
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
        CHECK_FALSE( ( part.width > .80f && part.depth > .80f ) );
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
        CHECK_FALSE( ( part.width > .8f && part.depth > .8f ) );
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

TEST_CASE( "fp3d_overhead_gait_badges_do_not_resize_the_avatar_body", "[fp3d][overhead][avatar]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    restore_on_out_of_scope restore_icons( show_creature_overlay_icons );
    show_creature_overlay_icons = true;
    const auto player_image = [&]() {
        fp3d_snapshot::prepare_view( you, here );
        const auto scene = fp3d_snapshot::capture( you, here );
        const auto player = std::find_if( scene.sprites.begin(), scene.sprites.end(),
        []( const auto &image ) { return image.is_avatar; } );
        REQUIRE( player != scene.sprites.end() );
        return *player;
    };
    you.set_movement_mode( move_mode_id( "walk" ) );
    const auto walking = player_image();
    const auto body_key = fp3d_materials::sprite_key( walking );
    for( const std::string gait : { "run", "crouch", "prone", "walk" } ) {
        CAPTURE( gait );
        you.set_movement_mode( move_mode_id( gait ) );
        REQUIRE( you.current_movement_mode() == move_mode_id( gait ) );
        const auto image = player_image();
        CHECK( fp3d_materials::sprite_key( image ) == body_key );
        CHECK( image.overlays == walking.overlays );
        CHECK( std::count( image.status_icons.begin(), image.status_icons.end(),
                          "overlay_" + gait ) == ( gait == "walk" ? 0 : 1 ) );
        if( gait == "run" ) {
            CHECK( image.height == walking.height );
        }
    }
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
    CHECK( world_sprite_count( closed ) == 0 );
    here.furn_set( target, furn_str_id( "f_crate_o" ) );
    refresh_vision( here, target.z() );
    REQUIRE( here.sees_some_items( target, you ) );
    const auto opened = fp3d_snapshot::capture( you, here );
    REQUIRE( world_sprite_count( opened ) == 1 );
    CHECK( opened.sprites.front().id == "rock" );
    here.invalidate_visibility_cache();
    const auto stale = fp3d_snapshot::capture( you, here );
    CHECK( world_sprite_count( stale ) == 0 );
}

TEST_CASE( "fp3d_zombie_effect_badges_preserve_body_and_native_detection", "[fp3d][vision]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here ) + tripoint_rel_ms( 0, -2, 0 );
    monster &zombie = spawn_test_monster( "mon_zombie", p, false );
    refresh_vision( here, p.z() );
    const auto capture = [&]() {
        const auto rng_before = rng_get_engine();
        const auto scene = fp3d_snapshot::capture( you, here );
        CHECK( rng_get_engine() == rng_before );
        const auto image = std::find_if( scene.sprites.begin(), scene.sprites.end(),
        []( const auto &sprite ) { return sprite.id == "mon_zombie"; } );
        REQUIRE( image != scene.sprites.end() );
        return *image;
    };
    REQUIRE( zombie.sees( here, you ) );
    REQUIRE( zombie.attitude_to( you ) == Creature::Attitude::HOSTILE );
    const auto normal = capture();
    CHECK( normal.alerted );
    CHECK( normal.overlays.empty() );
    const auto scene = fp3d_snapshot::capture( you, here );
    const auto player = std::find_if( scene.sprites.begin(), scene.sprites.end(),
    []( const auto &image ) { return image.is_avatar; } );
    REQUIRE( player != scene.sprites.end() );
    CHECK( normal.height == Approx( player->height ) );
    const auto before_time = calendar::turn;
    const auto before_moves = you.get_moves();
    for( const std::string effect : { "stunned", "downed", "webbed", "onfire" } ) {
        CAPTURE( effect );
        zombie.add_effect( efftype_id( effect ), 10_minutes );
        const auto affected = capture();
        CHECK( affected.height == normal.height );
        CHECK( affected.overlays.empty() );
        CHECK( affected.status_icons == std::vector<std::string>{ "overlay_effect_" + effect } );
        zombie.remove_effect( efftype_id( effect ) );
    }
    zombie.add_effect( efftype_id( "bleed" ), 10_minutes );
    CHECK( capture().bleeding );
    zombie.remove_effect( efftype_id( "bleed" ) );
    zombie.add_effect( efftype_id( "attention" ), 10_minutes );
    const auto attention = capture();
    CHECK( attention.status_icons == std::vector<std::string>{ "overlay_effect_attention" } );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    // Native perception, rather than the attention effect, owns the alert.
    zombie.add_effect( efftype_id( "blind" ), 10_minutes );
    REQUIRE_FALSE( zombie.sees( here, you ) );
    CHECK_FALSE( capture().alerted );
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
    const auto player = std::find_if( first.sprites.begin(), first.sprites.end(),
    []( const auto &image ) { return image.is_avatar; } );
    REQUIRE( player != first.sprites.end() );
    CHECK( appearance->height == Approx( player->height ) );
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
    const auto smoke_at = target + tripoint_rel_ms::east;
    here.add_field( smoke_at, field_type_str_id( "fd_smoke" ), 2, 10_minutes );
    refresh_vision( here, target.z() );
    REQUIRE_FALSE( here.field_at( target ).displayed_field_type()->display_items );
    REQUIRE( here.sees_some_items( target, you ) );
    const auto fire = fp3d_snapshot::capture( you, here );
    const auto *ground = find_cell( fire, here.get_abs( target ) );
    REQUIRE( ground );
    CHECK( ground->known == fp3d_snapshot::knowledge::visible );
    CHECK( world_sprite_count( fire ) == 0 );
    const auto has_field = []( const fp3d_snapshot::scene_snapshot &scene, const tripoint_abs_ms &p,
                               const std::string &id, int intensity ) {
        return std::any_of( scene.sprites.begin(), scene.sprites.end(), [&]( const auto &image ) {
            return image.kind == fp3d_snapshot::sprite_kind::field && image.position == p &&
                   image.id == id && image.field_intensity == intensity;
        } );
    };
    CHECK( has_field( fire, here.get_abs( target ), "fd_fire", 1 ) );
    CHECK( has_field( fire, here.get_abs( smoke_at ), "fd_smoke", 2 ) );
    here.remove_field( target, fd_fire );
    refresh_vision( here, target.z() );
    // Native removal first sets intensity zero; its displayed field remains
    // until normal field cleanup. The snapshot must not perform that cleanup.
    CHECK_FALSE( here.field_at( target ).displayed_field_type()->display_items );
    const auto dead_fire = fp3d_snapshot::capture( you, here );
    CHECK( world_sprite_count( dead_fire ) == 0 );
    CHECK_FALSE( has_field( dead_fire, here.get_abs( target ), "fd_fire", 1 ) );
    here.delete_field( target, fd_fire );
    refresh_vision( here, target.z() );
    REQUIRE( here.field_at( target ).displayed_field_type()->display_items );
    const auto cleared = fp3d_snapshot::capture( you, here );
    REQUIRE( world_sprite_count( cleared ) == 1 );
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
    REQUIRE( tile->ground_fields.size() == 1 );
    CHECK( tile->ground_fields.front().id == "fd_blood" );
    CHECK( tile->ground_fields.front().intensity == 3 );
    CHECK( rng_get_engine() == before_rng );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    here.invalidate_visibility_cache();
    const auto hidden = fp3d_snapshot::capture( you, here );
    CHECK( std::none_of( hidden.cells.begin(), hidden.cells.end(), []( const auto &cell ) {
        return !cell.ground_fields.empty();
    } ) );
    here.delete_field( p, fd_blood );
    refresh_vision( here, p.z() );
    const auto clean = fp3d_snapshot::capture( you, here );
    REQUIRE( find_cell( clean, here.get_abs( p ) ) );
    CHECK( find_cell( clean, here.get_abs( p ) )->ground_fields.empty() );
}

TEST_CASE( "fp3d_ground_fields_keep_mixed_coverings_and_native_intensity",
           "[fp3d][vision][ground_fields]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here ) + tripoint_rel_ms( 0, 2, 0 );
    const std::vector<std::string> ids{
        "fd_blood", "fd_blood_veggy", "fd_blood_insect", "fd_blood_invertebrate",
        "fd_acid", "fd_bile", "fd_slime", "fd_sap", "fd_sludge",
        "fd_gibs_flesh", "fd_gibs_veggy", "fd_gibs_insect", "fd_gibs_invertebrate",
        "fd_web", "fd_web_nostick", "fd_void_web", "fd_churned_earth",
        "fd_congealed_light", "fd_rubble", "fd_acid_vent"
    };
    for( const auto &id : ids ) {
        const field_type_str_id field( id );
        REQUIRE( field.is_valid() );
        REQUIRE( here.add_field( p, field, field->get_max_intensity(), 10_minutes ) );
    }
    // Invisible markers and volumetric gases must not become floor decals.
    here.add_field( p, field_type_str_id( "fd_clairvoyant" ), 1 );
    here.add_field( p, field_type_str_id( "fd_smoke" ), 1 );
    refresh_vision( here, p.z() );
    const auto before_rng = rng_get_engine();
    const auto before_time = calendar::turn;
    const auto before_moves = you.get_moves();
    const auto snapshot = fp3d_snapshot::capture( you, here );
    const auto *tile = find_cell( snapshot, here.get_abs( p ) );
    REQUIRE( tile );
    REQUIRE( tile->ground_fields.size() == ids.size() );
    for( const auto &id : ids ) {
        CAPTURE( id );
        const auto found = std::find_if( tile->ground_fields.begin(), tile->ground_fields.end(),
        [&]( const auto &field ) { return field.id == id; } );
        REQUIRE( found != tile->ground_fields.end() );
        CHECK( found->intensity == field_type_str_id( id )->get_max_intensity() );
    }
    CHECK( rng_get_engine() == before_rng );
    CHECK( calendar::turn == before_time );
    CHECK( you.get_moves() == before_moves );
    here.invalidate_visibility_cache();
    const auto hidden = fp3d_snapshot::capture( you, here );
    CHECK( std::none_of( hidden.cells.begin(), hidden.cells.end(), []( const auto &cell ) {
        return !cell.ground_fields.empty();
    } ) );
    // Native removal marks the entry dead, retaining intensity 1 until cleanup.
    here.remove_field( p, field_type_str_id( "fd_acid" ) );
    refresh_vision( here, p.z() );
    const auto changed = fp3d_snapshot::capture( you, here );
    const auto *updated = find_cell( changed, here.get_abs( p ) );
    REQUIRE( updated );
    CHECK( updated->ground_fields.size() == ids.size() - 1 );
    CHECK( std::none_of( updated->ground_fields.begin(), updated->ground_fields.end(),
    []( const auto &field ) { return field.id == "fd_acid"; } ) );
}

TEST_CASE( "fp3d_ground_coverings_connect_same_type_visible_neighbours", "[fp3d][ground_fields]" )
{
    const std::array<tripoint_rel_ms, 4> dirs{ tripoint_rel_ms::south,
        tripoint_rel_ms::east, tripoint_rel_ms::west, tripoint_rel_ms::north };
    for( const std::string id : { "fd_blood", "fd_acid", "fd_slime", "fd_web", "fd_gibs_flesh" } ) {
        for( int mask = 0; mask < 16; ++mask ) {
            CAPTURE( id, mask );
            fp3d_snapshot::scene_snapshot scene;
            fp3d_snapshot::cell center;
            center.position = tripoint_abs_ms::zero;
            center.ground_fields = { { id, 3 } };
            scene.cells.push_back( center );
            for( unsigned i = 0; i < dirs.size(); ++i ) {
                auto neighbour = center;
                neighbour.position += dirs[i];
                neighbour.ground_fields = { { mask & ( 1 << i ) ? id : "fd_bile", 1 } };
                scene.cells.push_back( neighbour );
            }
            fp3d_snapshot::connect_ground_fields( scene );
            CHECK( scene.cells.front().ground_fields.front().connections == mask );
            scene.cells[1].known = fp3d_snapshot::knowledge::remembered;
            fp3d_snapshot::connect_ground_fields( scene );
            CHECK( scene.cells.front().ground_fields.front().connections == ( mask & ~1 ) );
            scene.cells[2].ground_fields.clear();
            fp3d_snapshot::connect_ground_fields( scene );
            CHECK( scene.cells.front().ground_fields.front().connections == ( mask & ~3 ) );
        }
    }
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here ) + tripoint_rel_ms( 0, 2, 0 );
    here.add_field( p, fd_blood, 3 );
    here.add_field( p + tripoint_rel_ms::east, fd_blood, 1 );
    refresh_vision( here, p.z() );
    auto previous = fp3d_snapshot::capture_camera( you, here );
    REQUIRE( find_cell( previous, here.get_abs( p ) ) );
    CHECK( find_cell( previous, here.get_abs( p ) )->ground_fields.front().connections == 2 );
    here.delete_field( p + tripoint_rel_ms::east, fd_blood );
    refresh_vision( here, p.z() );
    const auto updated = fp3d_snapshot::capture_camera_reusing( you, here, &previous, 0, 0, false );
    REQUIRE( find_cell( updated, here.get_abs( p ) ) );
    CHECK( find_cell( updated, here.get_abs( p ) )->ground_fields.front().connections == 0 );
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
               !patch.ground_present && patch.wall_material == "t_brick_wall";
    } ) );
    CHECK( rng_get_engine() == city_rng );

    // Multi-level silhouettes use existing overmap storeys; a roof marker at
    // z=2 must not produce a third floor or change the wall family.
    const tripoint_abs_omt second = city + tripoint_rel_omt( 0, 0, 1 );
    const tripoint_abs_omt roof = city + tripoint_rel_omt( 0, 0, 2 );
    const oter_id old_second = overmap_buffer.ter( second );
    const oter_id old_roof = overmap_buffer.ter( roof );
    on_out_of_scope restore_upper( [&]() {
        overmap_buffer.ter_set( second, old_second );
        overmap_buffer.ter_set( roof, old_roof );
    } );
    const oter_str_id house_base( "house_2story_base_north" );
    const oter_str_id house_second( "house_2story_second_north" );
    const oter_str_id house_roof( "house_2story_roof_north" );
    REQUIRE( house_base.is_valid() );
    REQUIRE( house_second.is_valid() );
    REQUIRE( house_roof.is_valid() );
    overmap_buffer.ter_set( city, house_base.id() );
    overmap_buffer.ter_set( second, house_second.id() );
    overmap_buffer.ter_set( roof, house_roof.id() );
    fp3d_snapshot::prepare_view( you, here );
    const auto tall_city = fp3d_snapshot::capture( you, here );
    CHECK( std::any_of( tall_city.landscape.begin(), tall_city.landscape.end(),
    [&]( const auto & patch ) {
        return project_to<coords::omt>( patch.position ) == city &&
               patch.building_height == 2 * fp3d_camera::floor_height &&
               patch.wall_material == "t_wall_wood";
    } ) );
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
    // Steel-frame terrain must NEVER manufacture an opaque ceiling in
    // the open space underneath it merely because native has_floor()
    // considers the impassable lattice a supporting tile.
    const auto *under_steel = find_cell( scene,
                                        here.get_abs( frame + tripoint_rel_ms::below ) );
    if( under_steel ) {
        CHECK_FALSE( under_steel->ceiling_present );
        CHECK( under_steel->ceiling_surface.empty() );
    }
    const auto *guardrail = find_cell( scene, here.get_abs( rail ) );
    REQUIRE( guardrail );
    CHECK( guardrail->kind == fp3d_snapshot::terrain_kind::fence );
    // The tower's metal railing base must have the same appearance even if
    // its neighbouring metal panels are excluded from the 3D render cache.
    // The material decision is taken from the native map during capture.
    CHECK( guardrail->metal_deck_railing );
    const auto *under_rail = find_cell( scene,
                                       here.get_abs( rail + tripoint_rel_ms::below ) );
    if( under_rail && under_rail->ceiling_present ) {
        CHECK( under_rail->ceiling_surface == "t_metal_floor_no_roof" );
    }
}

TEST_CASE( "fp3d_overhead_fades_the_structure_above_the_current_radio_tower_floor",
           "[fp3d][camera_geometry][overhead][radio_tower]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto ground = you.pos_bub( here );
    const auto first_deck = ground + tripoint_rel_ms::above;
    const auto second_deck = first_deck + tripoint_rel_ms::above;
    for( const auto &p : { first_deck, second_deck } ) {
        REQUIRE( here.ter_set( p, ter_str_id( "t_metal_floor_no_roof" ) ) );
        REQUIRE( here.ter_set( p + tripoint_rel_ms::west, ter_str_id( "t_radio_tower" ) ) );
        REQUIRE( here.ter_set( p + tripoint_rel_ms::east, ter_str_id( "t_railing" ) ) );
        REQUIRE( here.ter_set( p + tripoint_rel_ms::south, ter_str_id( "t_ladder_up_down" ) ) );
    }
    const auto detached = first_deck + tripoint_rel_ms( 3, 0, 0 );
    REQUIRE( here.ter_set( detached, ter_str_id( "t_metal_floor_no_roof" ) ) );
    const auto faded = [&]( const fp3d_snapshot::scene_snapshot &scene, const tripoint_bub_ms &p ) {
        const auto *tile = find_cell( scene, here.get_abs( p ) );
        REQUIRE( tile );
        CHECK( tile->floor_present );
        return tile->faded_tower;
    };
    auto scene = fp3d_snapshot::capture_camera( you, here, 2, 0, true );
    CHECK_FALSE( faded( scene, ground ) );
    CHECK( faded( scene, first_deck ) );
    CHECK( faded( scene, second_deck ) );
    CHECK( faded( scene, first_deck + tripoint_rel_ms::east ) );
    CHECK( faded( scene, first_deck + tripoint_rel_ms::west ) );
    CHECK( faded( scene, first_deck + tripoint_rel_ms::south ) );
    CHECK_FALSE( faded( scene, detached ) );
    CHECK_FALSE( faded( fp3d_snapshot::capture_camera( you, here, 2, 0 ), first_deck ) );

    scene.render_generation = scene.geometry_generation = 1;
    auto reused = fp3d_snapshot::capture_camera_reusing( you, here, &scene, 2, 0, true );
    CHECK( reused.geometry_generation == 1 );
    CHECK( faded( reused, first_deck ) );

    you.setpos( here, first_deck );
    const auto climbed = fp3d_snapshot::capture_camera_reusing( you, here, &reused, 2, 1, true );
    CHECK_FALSE( faded( climbed, first_deck ) );
    CHECK_FALSE( faded( climbed, first_deck + tripoint_rel_ms::west ) );
    CHECK_FALSE( faded( climbed, first_deck + tripoint_rel_ms::east ) );
    CHECK_FALSE( faded( climbed, first_deck + tripoint_rel_ms::south ) );
    CHECK( faded( climbed, second_deck ) );

    you.setpos( here, detached + tripoint_rel_ms::below );
    const auto left = fp3d_snapshot::capture_camera( you, here, 2, 0, true );
    CHECK_FALSE( faded( left, first_deck ) );
    CHECK_FALSE( faded( left, detached ) );
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

TEST_CASE( "fp3d_balconies_do_not_drop_ray_visible_facade_surfaces", "[fp3d][multilevel]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto origin = you.pos_bub( here );
    for( int z = 0; z <= 4; ++z ) {
        for( int y = -4; y <= 4; ++y ) {
            for( int x = 7; x <= 14; ++x ) {
                const bool wall = x == 10 || x == 14 || std::abs( y ) == 4;
                const char *id = z == 4 ? "t_flat_roof" : x == 7 ? "t_metal_railing" :
                                 wall ? "t_concrete_wall_w" : x < 10 ? "t_thconc_floor" : "t_floor";
                REQUIRE( here.ter_set( origin + tripoint_rel_ms( x, y, z ), ter_str_id( id ) ) );
            }
        }
    }
    fp3d_snapshot::prepare_view( you, here );
    const auto scene = fp3d_snapshot::capture( you, here );
    int ray_visible = 0;
    for( int z = 1; z < 4; ++z ) {
        for( int y = -3; y <= 3; ++y ) {
            const auto p = origin + tripoint_rel_ms( 10, y, z );
            if( here.sees( origin, p, you.unimpaired_range() ) ) {
                ++ray_visible;
                CAPTURE( z, y );
                const auto *wall = find_cell( scene, here.get_abs( p ) );
                REQUIRE( wall );
                CHECK( wall->kind == fp3d_snapshot::terrain_kind::wall );
                CHECK_FALSE( wall->furniture );
            }
        }
    }
    CHECK( ray_visible > 0 );
}

TEST_CASE( "fp3d_unknown_mask_contains_no_terrain_and_preserves_known_openings", "[fp3d][snapshot]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto origin = you.pos_bub( here );
    const auto hidden = origin + tripoint_rel_ms( 6, 0, 0 );
    for( int y = -2; y <= 2; ++y ) {
        for( int x = -2; x <= 2; ++x ) {
            here.ter_set( hidden + tripoint_rel_ms( x, y, 0 ),
                          ter_str_id( std::abs( x ) == 2 || std::abs( y ) == 2 ? "t_wall" : "t_floor" ) );
        }
    }
    you.memorize_terrain( here.get_abs( hidden ), "", 0, 0 );
    const auto opening = origin + tripoint_rel_ms( 0, -2, 0 );
    REQUIRE( here.ter_set( opening, ter_str_id( "t_open_air" ) ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto before = fp3d_snapshot::capture( you, here );
    const auto masked = [&]( const auto &scene, const tripoint_abs_ms &p ) {
        return std::any_of( scene.unknown.begin(), scene.unknown.end(), [&]( const auto &span ) {
            return p.z() == span.position.z() && p.y() == span.position.y() &&
                   p.x() >= span.position.x() && p.x() < span.position.x() + span.width;
        } );
    };
    REQUIRE_FALSE( you.sees( here, hidden ) );
    CHECK( masked( before, here.get_abs( hidden ) ) );
    CHECK_FALSE( masked( before, here.get_abs( origin ) ) );
    CHECK_FALSE( masked( before, here.get_abs( opening ) ) );
    here.ter_set( hidden, ter_str_id( "t_lava" ) );
    here.furn_set( hidden, furn_str_id( "f_bookcase" ) );
    fp3d_snapshot::prepare_view( you, here );
    const auto after = fp3d_snapshot::capture( you, here );
    CHECK( masked( after, here.get_abs( hidden ) ) );
    CHECK_FALSE( find_cell( after, here.get_abs( hidden ) ) );
    REQUIRE( before.unknown.size() == after.unknown.size() );
    for( size_t i = 0; i < before.unknown.size(); ++i ) {
        CHECK( before.unknown[i].position == after.unknown[i].position );
        CHECK( before.unknown[i].width == after.unknown[i].width );
    }
}

TEST_CASE( "fp3d_flora_cards_cover_wildflowers_and_water_plants", "[fp3d][plants]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here ) + tripoint_rel_ms( 0, -2, 0 );
    for( const std::string id : {
             "f_black_eyed_susan", "f_bluebell", "f_burdock", "f_chamomile", "f_chicory",
             "f_dahlia", "f_dandelion", "f_datura", "f_flower_spurge", "f_flower_tulip",
             "f_lily", "f_mustard", "f_mutpoppy", "f_sunflower", "f_brambles", "f_cattails",
             "f_lotus", "f_algae_green", "f_algae_red", "f_bladderwrack", "f_kelp_sugar",
             "f_sea_lettuce", "f_lilypad", "f_region_flower", "f_region_flower_decorative",
             "f_region_water_plant", "f_region_weed"
         } ) {
        CAPTURE( id );
        here.ter_set( p, ter_str_id( "t_water_sh" ) );
        here.furn_set( p, furn_str_id( id ) );
        refresh_vision( here, p.z() );
        for( const bool camera : { false, true } ) {
            CAPTURE( camera );
            const auto scene = camera ? fp3d_snapshot::capture_camera( you, here, 1, 0 ) :
                               fp3d_snapshot::capture( you, here );
            const auto *tile = find_cell( scene, here.get_abs( p ) );
            REQUIRE( tile );
            CHECK_FALSE( tile->furniture );
            if( id.compare( 0, 9, "f_region_" ) == 0 ) {
                CHECK( tile->vegetation.empty() );
            } else {
                CHECK( tile->vegetation == id );
                if( id == "f_lilypad" ) {
                    CHECK( tile->vegetation_height == 0.f );
                } else {
                    CHECK( tile->vegetation_height > 0.f );
                }
            }
        }
    }
}

TEST_CASE( "fp3d_camera_geometry_reuse_tracks_live_map_changes", "[fp3d][camera_geometry]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here ) + tripoint_rel_ms( 4, 0, 0 );
    here.ter_set( p, ter_str_id( "t_wall" ) );
    auto previous = fp3d_snapshot::capture_camera( you, here, 1, 0 );
    previous.render_generation = 100;
    fp3d_snapshot::reuse_geometry( previous, nullptr );
    unsigned generation = 101;
    const auto changed = [&]() {
        auto scene = fp3d_snapshot::capture_camera_reusing( you, here, &previous, 1, 0, false );
        scene.render_generation = generation++;
        fp3d_snapshot::reuse_geometry( scene, &previous );
        CHECK( scene.geometry_generation == scene.render_generation );
        previous = std::move( scene );
    };
    here.ter_set( p, ter_str_id( "t_door_o" ) );
    changed();
    REQUIRE( find_cell( previous, here.get_abs( p ) ) );
    CHECK( find_cell( previous, here.get_abs( p ) )->kind == fp3d_snapshot::terrain_kind::open_door );
    here.furn_set( p, furn_str_id( "f_chair" ) );
    changed();
    REQUIRE( find_cell( previous, here.get_abs( p ) )->furniture );
    CHECK( find_cell( previous, here.get_abs( p ) )->furniture->id == "f_chair" );
    here.add_field( p, fd_blood, 3, 10_minutes );
    changed();
    REQUIRE_FALSE( find_cell( previous, here.get_abs( p ) )->ground_fields.empty() );
    CHECK( find_cell( previous, here.get_abs( p ) )->ground_fields.front().id == "fd_blood" );
    here.delete_field( p, fd_blood );
    changed();
    CHECK( find_cell( previous, here.get_abs( p ) )->ground_fields.empty() );
    here.furn_set( p, furn_str_id( "f_null" ) );
    here.ter_set( p, ter_str_id( "t_open_air" ) );
    changed();
    CHECK_FALSE( find_cell( previous, here.get_abs( p ) ) );
    const auto *allocation = previous.cells.data();
    auto same = fp3d_snapshot::capture_camera_reusing( you, here, &previous, 1, 0, false );
    same.render_generation = generation;
    CHECK( same.geometry_generation == generation - 1 );
    CHECK( same.cells.data() == allocation );
}

TEST_CASE( "fp3d_camera_geometry_is_independent_of_native_visibility",
           "[fp3d][camera_geometry]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    for( int z = 0; z <= 3; ++z ) {
        for( int y = -4; y <= 4; ++y ) {
            for( int x = 4; x <= 12; ++x ) {
                const bool wall = x == 4 || x == 12 || std::abs( y ) == 4;
                here.ter_set( player + tripoint_rel_ms( x, y, z ),
                              ter_str_id( z == 3 ? "t_flat_roof" : wall ? "t_wall" : "t_floor" ) );
            }
        }
    }
    const auto hidden = player + tripoint_rel_ms( 8, 0, 1 );
    here.furn_set( hidden, furn_str_id( "f_bookcase" ) );
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE_FALSE( here.sees( player, hidden, MAX_VIEW_DISTANCE ) );
    const auto absolute = here.get_abs( hidden );
    const auto native = fp3d_snapshot::capture( you, here );
    const auto *native_tile = find_cell( native, absolute );
    REQUIRE( ( !native_tile || !native_tile->furniture ) );
    const auto memory = you.get_memorized_tile( absolute ).get_ter_id();
    const float seen = here.access_cache( hidden.z() ).seen_cache[hidden.x()][hidden.y()];
    const auto random_before = rng_get_engine();
    const auto camera = fp3d_snapshot::capture_camera( you, here );
    const auto *tile = find_cell( camera, absolute );
    REQUIRE( tile );
    CHECK( tile->terrain == "t_floor" );
    CHECK( tile->floor_present );
    REQUIRE( tile->furniture );
    CHECK_FALSE( tile->exterior_only );
    CHECK( camera.unknown.empty() );
    CHECK( you.get_memorized_tile( absolute ).get_ter_id() == memory );
    CHECK( here.access_cache( hidden.z() ).seen_cache[hidden.x()][hidden.y()] == seen );
    CHECK( rng_get_engine() == random_before );
    const auto native_after = fp3d_snapshot::capture( you, here );
    CHECK( native_after.cells.size() == native.cells.size() );
    CHECK( native_after.visible == native.visible );
    CHECK( native_after.remembered == native.remembered );
    const auto *still_hidden = find_cell( native_after, absolute );
    CHECK( ( !still_hidden || !still_hidden->furniture ) );
    here.ter_set( hidden, ter_str_id( "t_wall" ) );
    here.invalidate_visibility_cache();
    const auto changed = fp3d_snapshot::capture_camera( you, here );
    REQUIRE( find_cell( changed, absolute ) );
    CHECK( find_cell( changed, absolute )->terrain == "t_wall" );
    CHECK( you.get_memorized_tile( absolute ).get_ter_id() == memory );
}

TEST_CASE( "fp3d_camera_geometry_preserves_real_open_air_and_upper_facades",
           "[fp3d][camera_geometry]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    const auto air = player + tripoint_rel_ms( 3, 0, 1 );
    const auto wall = player + tripoint_rel_ms( 12, 0, 4 );
    here.ter_set( air, ter_str_id( "t_open_air" ) );
    here.ter_set( wall, ter_str_id( "t_wall" ) );
    here.invalidate_visibility_cache();
    const auto camera = fp3d_snapshot::capture_camera( you, here );
    const auto *opening = find_cell( camera, here.get_abs( air ) );
    CHECK( ( !opening || ( opening->open_air && !opening->floor_present ) ) );
    const auto *facade = find_cell( camera, here.get_abs( wall ) );
    REQUIRE( facade );
    CHECK( facade->kind == fp3d_snapshot::terrain_kind::wall );
    CHECK_FALSE( facade->exterior_only );
}

TEST_CASE( "fp3d_camera_geometry_cuts_only_the_current_room_roof",
           "[fp3d][camera_geometry][overhead]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    for( int y = -3; y <= 3; ++y ) {
        for( int x = -3; x <= 7; ++x ) {
            const auto p = player + tripoint_rel_ms( x, y, 0 );
            const bool wall = x == -3 || x == 3 || x == 7 || std::abs( y ) == 3;
            here.ter_set( p, ter_str_id( wall ? "t_wall" : "t_floor" ) );
            here.ter_set( p + tripoint_rel_ms::above, ter_str_id( "t_flat_roof" ) );
        }
    }
    fp3d_snapshot::prepare_view( you, here );
    const auto eye = fp3d_snapshot::capture_camera( you, here );
    const auto cut = fp3d_snapshot::capture_camera( you, here,
                     fp3d_snapshot::render_above, fp3d_snapshot::render_below, true );
    REQUIRE( find_cell( eye, here.get_abs( player + tripoint_rel_ms::above ) ) );
    CHECK_FALSE( find_cell( cut, here.get_abs( player + tripoint_rel_ms::above ) ) );
    const auto *floor = find_cell( cut, here.get_abs( player ) );
    REQUIRE( floor );
    CHECK( floor->floor_present );
    CHECK_FALSE( floor->ceiling_present );
    CHECK( floor->cutaway_ceiling );
    const auto *wall = find_cell( cut, here.get_abs( player + tripoint_rel_ms( 3, 0, 0 ) ) );
    REQUIRE( wall );
    CHECK( wall->kind == fp3d_snapshot::terrain_kind::wall );
    CHECK( find_cell( cut, here.get_abs( player + tripoint_rel_ms( 5, 0, 1 ) ) ) );
}

TEST_CASE( "fp3d_overhead_hidden_roof_keeps_only_opaque_light_boundaries",
           "[fp3d][snapshot][overhead]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    for( int y = -2; y <= 2; ++y ) {
        for( int x = -2; x <= 2; ++x ) {
            const auto p = player + tripoint_rel_ms( x, y, 0 );
            here.ter_set( p, ter_str_id( std::abs( x ) == 2 || std::abs( y ) == 2 ?
                                       "t_wall" : "t_floor" ) );
            here.ter_set( p + tripoint_rel_ms::above, ter_str_id( "t_flat_roof" ) );
        }
    }
    // The adjacent solid roof keeps this an indoor region even with an opening.
    const auto aperture = player + tripoint_rel_ms::east;
    for( bool camera : { false, true } ) {
        auto previous = fp3d_snapshot::scene_snapshot{};
        for( const std::string roof : { "t_flat_roof", "t_glass_roof", "t_open_air", "t_flat_roof" } ) {
            CAPTURE( camera, roof );
            here.ter_set( aperture + tripoint_rel_ms::above, ter_str_id( roof ) );
            fp3d_snapshot::prepare_view( you, here );
            auto scene = camera ? fp3d_snapshot::capture_camera_reusing( you, here, &previous,
                         fp3d_snapshot::render_above, fp3d_snapshot::render_below, true ) :
                         fp3d_snapshot::capture( you, here, fp3d_snapshot::render_above,
                                 fp3d_snapshot::render_below, true );
            const auto *tile = find_cell( scene, here.get_abs( aperture ) );
            REQUIRE( tile );
            REQUIRE( tile->over_avatar_region );
            CHECK_FALSE( tile->ceiling_present );
            CHECK( tile->cutaway_ceiling == ( roof == "t_flat_roof" ) );
            if( camera ) {
                // Exercise the fast retained-cell path as well as roof edits.
                previous = std::move( scene );
                previous.render_generation = 1; // Simulate compositor publication.
                previous.geometry_generation = previous.render_generation;
                auto reused = fp3d_snapshot::capture_camera_reusing( you, here, &previous,
                              fp3d_snapshot::render_above, fp3d_snapshot::render_below, true );
                tile = find_cell( reused, here.get_abs( aperture ) );
                REQUIRE( tile );
                CHECK( tile->cutaway_ceiling == ( roof == "t_flat_roof" ) );
                CHECK( reused.geometry_generation == 1 );
                previous = std::move( reused );
            }
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

TEST_CASE( "fp3d_skylight_is_a_translucent_ceiling_not_an_opaque_roof", "[fp3d][multilevel]" )
{
    // The native skylight stays walkable roof glass; only its presentation
    // changes from an opaque t_floor sheet to a pane like a glass wall.
    CHECK( fp3d_snapshot::terrain_shape( "t_glass_roof" ) ==
           fp3d_snapshot::terrain_kind::floor );
    CHECK( fp3d_snapshot::is_glass_roof( "t_glass_roof" ) );
    CHECK_FALSE( fp3d_snapshot::is_glass_roof( "t_flat_roof" ) );
    CHECK_FALSE( fp3d_snapshot::is_glass_roof( "t_floor" ) );
    CHECK_FALSE( fp3d_snapshot::is_glass_roof( "t_open_air" ) );

    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto p = you.pos_bub( here );
    const auto overhead = p + tripoint_rel_ms( 0, 0, 1 );
    REQUIRE( here.ter_set( overhead, ter_str_id( "t_glass_roof" ) ) );
    refresh_vision( here, p.z() );
    fp3d_snapshot::prepare_view( you, here );
    const auto glazed = fp3d_snapshot::capture( you, here );
    const auto *room = find_cell( glazed, here.get_abs( p ) );
    REQUIRE( room );
    CHECK( room->ceiling_present );
    CHECK( room->glass_ceiling );
    // The roof glass must still be a normal floor cell in the snapshot, not a
    // synthetic wall/obstacle or an invented hole in the roof.
    const auto *roof = find_cell( glazed, here.get_abs( overhead ) );
    if( roof ) {
        CHECK( roof->kind == fp3d_snapshot::terrain_kind::floor );
        CHECK( fp3d_snapshot::is_glass_roof( roof->terrain ) );
    }

    // The same room under a solid roof keeps an opaque ceiling.
    REQUIRE( here.ter_set( overhead, ter_str_id( "t_flat_roof" ) ) );
    refresh_vision( here, p.z() );
    fp3d_snapshot::prepare_view( you, here );
    const auto solid = fp3d_snapshot::capture( you, here );
    room = find_cell( solid, here.get_abs( p ) );
    REQUIRE( room );
    CHECK( room->ceiling_present );
    CHECK_FALSE( room->glass_ceiling );
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

TEST_CASE( "fp3d_movement_step_profile", "[fp3d][performance]" )
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
        std::filesystem::create_directories( directory );
        report.open( std::filesystem::path( directory ) / "movement-step.csv" );
        report << "step,prepare_total_ms,player_layer_ms,extra_layers_ms,extra_layer_calls,"
                  "visibility_ms,memory_ms,capture_ms,capture_full_ms,tail_ms,cells,sprites\n";
    }
    constexpr int steps = 12;
    double prepare_sum = 0;
    double capture_full_sum = 0;
    for( int step = 0; step < steps; ++step ) {
        const auto before = you.pos_abs();
        you.set_moves( 1000 );
        avatar_action::move( you, here, tripoint_rel_ms::east );
        REQUIRE( you.pos_abs() == before + tripoint_rel_ms::east );
        // The whole synchronous cost of one step: prepare_view() then capture().
        const auto timing = fp3d_snapshot::prepare_view( you, here );
        const auto scene = fp3d_snapshot::capture( you, here );
        CHECK( scene.origin == you.pos_abs() );
        CHECK( scene.capture_full_ms >= scene.capture_ms );
        prepare_sum += timing.total_ms;
        capture_full_sum += scene.capture_full_ms;
        if( report ) {
            report << step << ',' << timing.total_ms << ',' << timing.player_layer_ms << ','
                   << timing.extra_layers_ms << ',' << timing.extra_layer_calls << ','
                   << timing.visibility_ms << ',' << timing.memory_ms << ',' << scene.capture_ms << ','
                   << scene.capture_full_ms << ',' << scene.capture_full_ms - scene.capture_ms << ','
                   << scene.cells.size() << ',' << scene.sprites.size() << '\n';
        }
    }
    WARN( "movement step: prepare_mean_ms=" << prepare_sum / steps
          << ", capture_full_mean_ms=" << capture_full_sum / steps );
}

namespace
{
std::uint64_t hash_floats( const cata::mdarray<float, point_bub_ms> &values )
{
    std::uint64_t hash = 1469598103934665603ull;
    for( int x = 0; x < MAPSIZE_X; ++x ) {
        for( int y = 0; y < MAPSIZE_Y; ++y ) {
            std::uint32_t bits = 0;
            const float value = values[x][y];
            std::memcpy( &bits, &value, sizeof( bits ) );
            hash = ( hash ^ bits ) * 1099511628211ull;
        }
    }
    return hash;
}
std::uint64_t hash_bools( const cata::mdarray<bool, point_bub_ms> &values )
{
    std::uint64_t hash = 1469598103934665603ull;
    for( int x = 0; x < MAPSIZE_X; ++x ) {
        for( int y = 0; y < MAPSIZE_Y; ++y ) {
            hash = ( hash ^ ( values[x][y] ? 1ull : 0ull ) ) * 1099511628211ull;
        }
    }
    return hash;
}
std::uint64_t hash_visibility( const cata::mdarray<lit_level, point_bub_ms> &values )
{
    std::uint64_t hash = 1469598103934665603ull;
    for( int x = 0; x < MAPSIZE_X; ++x ) {
        for( int y = 0; y < MAPSIZE_Y; ++y ) {
            hash = ( hash ^ static_cast<std::uint64_t>( static_cast<int>( values[x][y] ) ) ) *
                   1099511628211ull;
        }
    }
    return hash;
}
std::uint64_t hash_scene( const fp3d_snapshot::scene_snapshot &scene )
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&]( std::uint64_t value ) {
        hash = ( hash ^ value ) * 1099511628211ull;
    };
    for( const auto &tile : scene.cells ) {
        mix( static_cast<std::uint64_t>( std::hash<std::string> {}( tile.terrain ) ) );
        mix( static_cast<std::uint64_t>( tile.position.x() ) );
        mix( static_cast<std::uint64_t>( tile.position.y() ) );
        mix( static_cast<std::uint64_t>( tile.position.z() + 64 ) );
        mix( static_cast<std::uint64_t>( static_cast<int>( tile.kind ) ) );
        mix( static_cast<std::uint64_t>( static_cast<int>( tile.known ) ) );
        mix( ( tile.floor_present ? 1ull : 0ull ) | ( tile.ceiling_present ? 2ull : 0ull ) );
    }
    for( const auto &sprite : scene.sprites ) {
        mix( static_cast<std::uint64_t>( std::hash<std::string> {}( sprite.id ) ) );
    }
    return hash;
}
} // namespace

TEST_CASE( "fp3d_extra_storey_lightmap_only_costs_time", "[fp3d][multilevel][performance]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    for( int z = 1; z <= 3; ++z ) {
        for( int x = -4; x <= 4; ++x ) {
            here.ter_set( player + tripoint_rel_ms( x, -6, z ), ter_str_id( "t_wall" ) );
            here.ter_set( player + tripoint_rel_ms( x, 6, z ), ter_str_id( "t_floor" ) );
        }
        here.ter_set( player + tripoint_rel_ms( 0, 0, z ), ter_str_id( "t_floor" ) );
    }
    const auto invalidate_all = [&]() {
        for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
            here.invalidate_map_cache( z );
        }
        here.invalidate_visibility_cache();
    };
    // Same storey range prepare_view() walks. The upper lightmaps are only
    // generated when upper_lightmaps is true; the player layer and every level
    // below it always keep theirs.
    const auto prepare_like_view = [&]( bool upper_lightmaps ) {
        const auto p = you.pos_bub( here );
        here.build_map_cache( p.z() );
        for( int z = std::max( -OVERMAP_DEPTH, p.z() - fp3d_snapshot::below_for_storey( p.z() ) );
             z <= std::min( OVERMAP_HEIGHT, p.z() + fp3d_snapshot::render_above ); ++z ) {
            if( z == p.z() ) {
                continue;
            }
            here.build_map_cache( z, !upper_lightmaps && z > p.z() );
        }
        here.update_visibility_cache( p.z() );
    };
    const auto sweep = [&]() {
        std::vector<std::uint64_t> values;
        for( int z = std::max( -OVERMAP_DEPTH, player.z() - fp3d_snapshot::below_for_storey( player.z() ) );
             z <= std::min( OVERMAP_HEIGHT, player.z() + fp3d_snapshot::render_above ); ++z ) {
            const level_cache &cache = here.access_cache( z );
            values.push_back( hash_floats( cache.seen_cache ) );
            values.push_back( hash_floats( cache.camera_cache ) );
            values.push_back( hash_floats( cache.vision_transparency_cache ) );
            values.push_back( hash_floats( cache.transparency_cache ) );
            values.push_back( hash_bools( cache.floor_cache ) );
            values.push_back( hash_bools( cache.outside_cache ) );
            values.push_back( hash_visibility( cache.visibility_cache ) );
        }
        return values;
    };
    const auto measure = [&]( bool upper_lightmaps ) {
        invalidate_all();
        const auto started = std::chrono::steady_clock::now();
        prepare_like_view( upper_lightmaps );
        return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - started ).count();
    };
    // Cold passes first: definitions and caches are identical in both arms.
    const double cold_with = measure( true );
    const double cold_without = measure( false );
    // Steady state is the per-step case: caches are clean, only lightmaps are
    // regenerated if they are generated at all.
    const auto steady = [&]( bool upper_lightmaps ) {
        const auto started = std::chrono::steady_clock::now();
        prepare_like_view( upper_lightmaps );
        return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - started ).count();
    };
    double warm_with = 0;
    double warm_without = 0;
    for( int pass = 0; pass < 5; ++pass ) {
        warm_with += steady( true );
        warm_without += steady( false );
    }
    WARN( "extra storeys: cold " << cold_with << " ms with lightmaps / " << cold_without
          << " ms without; steady state " << warm_with / 5 << " ms / " << warm_without / 5 << " ms" );
    // The only thing the upper lightmaps may cost is time: every cache the 3D
    // scene and the native visibility rules read must stay byte-identical.
    invalidate_all();
    prepare_like_view( true );
    const auto with = sweep();
    const auto scene_with = fp3d_snapshot::capture( you, here );
    invalidate_all();
    prepare_like_view( false );
    const auto without = sweep();
    const auto scene_without = fp3d_snapshot::capture( you, here );
    REQUIRE( with.size() == without.size() );
    CHECK( with == without );
    CHECK( hash_scene( scene_with ) == hash_scene( scene_without ) );
    CHECK( scene_with.cells.size() == scene_without.cells.size() );
}

TEST_CASE( "fp3d_upper_storey_map_cache_calls_are_redundant", "[fp3d][multilevel][performance]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    for( int z = 1; z <= 3; ++z ) {
        for( int x = -4; x <= 4; ++x ) {
            here.ter_set( player + tripoint_rel_ms( x, -6, z ), ter_str_id( "t_wall" ) );
            here.ter_set( player + tripoint_rel_ms( x, 6, z ), ter_str_id( "t_floor" ) );
        }
        here.ter_set( player + tripoint_rel_ms( 0, 0, z ), ter_str_id( "t_floor" ) );
    }
    const auto invalidate_all = [&]() {
        for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
            here.invalidate_map_cache( z );
        }
        here.invalidate_visibility_cache();
    };
    // upper_calls == true reproduces the old per-storey build_map_cache() loop;
    // false is what prepare_view() does now: the player-layer call already
    // sweeps every z-level's caches and fills the LOS of all of them, so the
    // upper storeys need no call at all once their lightmap is skipped.
    const auto prepare_like_view = [&]( bool upper_calls ) {
        const auto p = you.pos_bub( here );
        here.build_map_cache( p.z() );
        const int lowest = std::max( -OVERMAP_DEPTH, p.z() - fp3d_snapshot::below_for_storey( p.z() ) );
        for( int z = lowest; z < p.z(); ++z ) {
            here.build_map_cache( z );
        }
        if( upper_calls ) {
            for( int z = p.z() + 1;
                 z <= std::min( OVERMAP_HEIGHT, p.z() + fp3d_snapshot::render_above ); ++z ) {
                here.build_map_cache( z, true );
            }
        }
        here.update_visibility_cache( p.z() );
    };
    const auto sweep = [&]() {
        std::vector<std::uint64_t> values;
        for( int z = std::max( -OVERMAP_DEPTH, player.z() - fp3d_snapshot::below_for_storey( player.z() ) );
             z <= std::min( OVERMAP_HEIGHT, player.z() + fp3d_snapshot::render_above ); ++z ) {
            const level_cache &cache = here.access_cache( z );
            values.push_back( hash_floats( cache.seen_cache ) );
            values.push_back( hash_floats( cache.camera_cache ) );
            values.push_back( hash_floats( cache.vision_transparency_cache ) );
            values.push_back( hash_floats( cache.transparency_cache ) );
            values.push_back( hash_bools( cache.floor_cache ) );
            values.push_back( hash_bools( cache.outside_cache ) );
            values.push_back( hash_visibility( cache.visibility_cache ) );
        }
        return values;
    };
    const auto measure = [&]( bool upper_calls ) {
        invalidate_all();
        const auto started = std::chrono::steady_clock::now();
        prepare_like_view( upper_calls );
        return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - started ).count();
    };
    const double cold_with = measure( true );
    const double cold_without = measure( false );
    double warm_with = 0;
    double warm_without = 0;
    for( int pass = 0; pass < 5; ++pass ) {
        const auto steady = [&]( bool upper_calls ) {
            const auto started = std::chrono::steady_clock::now();
            prepare_like_view( upper_calls );
            return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - started ).count();
        };
        warm_with += steady( true );
        warm_without += steady( false );
    }
    WARN( "upper storey calls: cold " << cold_with << " ms with / " << cold_without << " ms without; steady "
          << warm_with / 5 << " ms / " << warm_without / 5 << " ms" );
    invalidate_all();
    prepare_like_view( true );
    const auto with = sweep();
    const auto scene_with = fp3d_snapshot::capture( you, here );
    invalidate_all();
    prepare_like_view( false );
    const auto without = sweep();
    const auto scene_without = fp3d_snapshot::capture( you, here );
    REQUIRE( with.size() == without.size() );
    CHECK( with == without );
    CHECK( hash_scene( scene_with ) == hash_scene( scene_without ) );
    CHECK( scene_with.cells.size() == scene_without.cells.size() );
}

TEST_CASE( "fp3d_capture_cell_loop_costs", "[fp3d][performance]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    // Same domain the capture walks: (2*radius+1)^2 per storey, render_above+1
    // storeys above and below_for_storey below.
    const int lowest = std::max( -OVERMAP_DEPTH, player.z() - fp3d_snapshot::below_for_storey( player.z() ) );
    const int highest = std::min( OVERMAP_HEIGHT, player.z() + fp3d_snapshot::render_above );
    const int radius = fp3d_snapshot::radius;
    long long sink = 0;
    const auto walk = [&]( const char *name, const std::function<void( const tripoint_bub_ms & )> &body ) {
        const auto started = std::chrono::steady_clock::now();
        for( int z = lowest; z <= highest; ++z ) {
            for( int dy = -radius; dy <= radius; ++dy ) {
                for( int dx = -radius; dx <= radius; ++dx ) {
                    const tripoint_bub_ms p = player + tripoint_rel_ms( dx, dy, z - player.z() );
                    if( !here.inbounds( p ) ) {
                        continue;
                    }
                    body( p );
                }
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started ).count();
        WARN( "cell loop " << name << ": " << ms << " ms, sink=" << sink );
        return ms;
    };
    // warm the map caches so every variant sees the same prepared state
    here.build_map_cache( player.z() );
    for( int z = lowest; z < player.z(); ++z ) {
        here.build_map_cache( z );
    }
    here.update_visibility_cache( player.z(), false );
    const double bare = walk( "bare", [&]( const tripoint_bub_ms & ) {
        ++sink;
    } );
    const double memory = walk( "with get_memorized_tile", [&]( const tripoint_bub_ms &p ) {
        sink += static_cast<long long>(
                    you.get_memorized_tile( here.get_abs( p ) ).get_ter_id().size() );
    } );
    const double terrain = walk( "with ter().id()", [&]( const tripoint_bub_ms &p ) {
        sink += static_cast<long long>( here.ter( p ).id().str().size() );
    } );
    const double furniture = walk( "with furn().id()", [&]( const tripoint_bub_ms &p ) {
        sink += static_cast<long long>( here.furn( p ).id().str().size() );
    } );
    const double floor_flags = walk( "with has_floor/is_open_air/is_outside", [&]( const tripoint_bub_ms &p ) {
        sink += here.has_floor( p ) + here.is_open_air( p ) + here.is_outside( p );
    } );
    WARN( "cell loop deltas: memory=" << memory - bare << " ms, ter.id=" << terrain - bare
          << " ms, furn.id=" << furniture - bare << " ms, flags=" << floor_flags - bare << " ms" );
    CHECK( sink > 0 );
}

TEST_CASE( "fp3d_3d_prepare_only_needs_the_player_visibility_cache", "[fp3d][multilevel][performance]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    for( int z = 1; z <= 2; ++z ) {
        for( int x = -3; x <= 3; ++x ) {
            here.ter_set( player + tripoint_rel_ms( x, -5, z ), ter_str_id( "t_wall" ) );
            here.ter_set( player + tripoint_rel_ms( x, 5, z ), ter_str_id( "t_floor" ) );
        }
        here.ter_set( player + tripoint_rel_ms( 0, 0, z ), ter_str_id( "t_floor" ) );
    }
    const auto invalidate_all = [&]() {
        for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
            here.invalidate_map_cache( z );
        }
        here.invalidate_visibility_cache();
    };
    // The 3D path skips the native 2D map draw, so it only needs the player's
    // own visibility cache; the native recursive default additionally refreshes
    // every storey below within fov_3d_z_range (what the 2D isometric renderer
    // reads).
    const auto prepare = [&]( bool lower_levels ) {
        const auto p = you.pos_bub( here );
        here.build_map_cache( p.z() );
        const int lowest = std::max( -OVERMAP_DEPTH, p.z() - fp3d_snapshot::below_for_storey( p.z() ) );
        for( int z = lowest; z < p.z(); ++z ) {
            here.build_map_cache( z );
        }
        here.update_visibility_cache( p.z(), lower_levels );
    };
    const auto time_prepare = [&]( bool lower_levels ) {
        invalidate_all();
        const auto started = std::chrono::steady_clock::now();
        prepare( lower_levels );
        return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - started ).count();
    };
    double with_lower = 0;
    double without_lower = 0;
    for( int pass = 0; pass < 3; ++pass ) {
        with_lower += time_prepare( true );
        without_lower += time_prepare( false );
    }
    WARN( "player-only visibility cache: " << with_lower / 3 << " ms with lower storeys / "
          << without_lower / 3 << " ms without" );
    const auto other_cache_hashes = [&]() {
        std::vector<std::uint64_t> values;
        for( int z = std::max( -OVERMAP_DEPTH, player.z() - fp3d_snapshot::below_for_storey( player.z() ) );
             z <= std::min( OVERMAP_HEIGHT, player.z() + fp3d_snapshot::render_above ); ++z ) {
            const level_cache &cache = here.access_cache( z );
            values.push_back( hash_floats( cache.seen_cache ) );
            values.push_back( hash_floats( cache.camera_cache ) );
            values.push_back( hash_floats( cache.vision_transparency_cache ) );
            values.push_back( hash_floats( cache.transparency_cache ) );
            values.push_back( hash_bools( cache.floor_cache ) );
            values.push_back( hash_bools( cache.outside_cache ) );
        }
        return values;
    };
    invalidate_all();
    prepare( true );
    const std::uint64_t player_recursive = hash_visibility( here.access_cache( player.z() ).visibility_cache );
    const auto other_recursive = other_cache_hashes();
    const auto scene_recursive = fp3d_snapshot::capture( you, here );
    invalidate_all();
    prepare( false );
    const std::uint64_t player_only = hash_visibility( here.access_cache( player.z() ).visibility_cache );
    const auto other_player_only = other_cache_hashes();
    const auto scene_player_only = fp3d_snapshot::capture( you, here );
    CHECK( player_recursive == player_only );
    REQUIRE( other_recursive.size() == other_player_only.size() );
    CHECK( other_recursive == other_player_only );
    CHECK( hash_scene( scene_recursive ) == hash_scene( scene_player_only ) );
    CHECK( scene_recursive.cells.size() == scene_player_only.cells.size() );
}

TEST_CASE( "fp3d_fast_perception_retains_native_aiming_and_wall_obstruction",
           "[fp3d][vision][performance]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto player = you.pos_bub( here );
    for( int y = -12; y <= 12; ++y ) {
        here.ter_set( player + tripoint_rel_ms( 5, y, 0 ), ter_str_id( "t_wall" ) );
    }
    item weapon( itype_id( "glock_19" ) );
    REQUIRE( you.wield( weapon ) );
    you.last_target_pos = here.get_abs( player + tripoint_rel_ms( 0, -30, 0 ) );
    for( int recoil : { static_cast<int>( MAX_RECOIL ), 0 } ) {
        you.recoil = recoil;
        you.rebuild_aim_cache();
        fp3d_snapshot::prepare_view( you, here );
        const auto scene = fp3d_snapshot::capture( you, here, 0, 0 );
        std::unordered_map<tripoint_abs_ms, bool> visible;
        for( const auto &tile : scene.cells ) {
            visible.emplace( tile.position, tile.known == fp3d_snapshot::knowledge::visible &&
                             !tile.exterior_only );
        }
        const auto &cache = here.access_cache( player.z() );
        unsigned mismatches = 0;
        for( int y = -fp3d_snapshot::radius; y <= fp3d_snapshot::radius; ++y ) {
            for( int x = -fp3d_snapshot::radius; x <= fp3d_snapshot::radius; ++x ) {
                const auto p = player + tripoint_rel_ms( x, y, 0 );
                if( !here.inbounds( p ) ) { continue; }
                const auto found = visible.find( here.get_abs( p ) );
                const bool captured = found != visible.end() && found->second;
                const bool expected = !map::apparent_light_helper( cache, p ).obstructed;
                mismatches += captured != expected;
            }
        }
        INFO( "recoil=" << recoil );
        CHECK( mismatches == 0 );
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

TEST_CASE( "fp3d_overhead_capture_lifts_only_the_avatar_room_ceiling",
           "[fp3d][snapshot][overhead]" )
{
    prepare_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const auto origin = you.pos_bub( here );
    // The closed room has no outdoor LOS; supply a remembered neighbour so
    // the assertions below actually exercise geometry outside the cut region.
    you.memorize_terrain( here.get_abs( origin + tripoint_rel_ms( 5, 0, 0 ) ),
                          "t_pavement", 0, 0 );
    // One closed room around the avatar, with a real floor above it. The
    // overhead camera has to look into this room from above, so the capture is
    // asked to lift exactly this region's overhead boundary.
    for( int y = -2; y <= 2; ++y ) {
        for( int x = -2; x <= 2; ++x ) {
            const auto p = origin + tripoint_rel_ms( x, y, 0 );
            const bool wall = std::abs( x ) == 2 || std::abs( y ) == 2;
            here.ter_set( p, ter_str_id( wall ? "t_wall" : "t_floor" ) );
            here.ter_set( p + tripoint_rel_ms::above, ter_str_id( "t_floor" ) );
        }
    }
    fp3d_snapshot::prepare_view( you, here );
    REQUIRE_FALSE( here.is_outside( origin ) );
    const auto eye_view = fp3d_snapshot::capture( you, here, fp3d_snapshot::render_above,
                           fp3d_snapshot::render_below, false );
    const auto *room = find_cell( eye_view, here.get_abs( origin ) );
    REQUIRE( room );
    // The interior region is reported even for the eye camera; only the cut is
    // view dependent. Nothing about perception changed.
    CHECK( room->over_avatar_region );
    CHECK( room->known == fp3d_snapshot::knowledge::visible );
    CHECK( room->ceiling_present );

    const auto cut = fp3d_snapshot::capture( you, here, fp3d_snapshot::render_above,
                     fp3d_snapshot::render_below, true );
    const auto *cut_room = find_cell( cut, here.get_abs( origin ) );
    REQUIRE( cut_room );
    CHECK( cut_room->over_avatar_region );
    CHECK( cut_room->known == fp3d_snapshot::knowledge::visible );
    CHECK_FALSE( cut_room->ceiling_present );
    CHECK( cut_room->cutaway_ceiling );
    // Floor and walls of the floor the avatar stands on are never removed; the
    // cut is not "delete everything above z", it is this region's boundary.
    CHECK( cut_room->kind == fp3d_snapshot::terrain_kind::floor );
    CHECK( cut_room->floor_present );
    for( const auto &tile : cut.cells ) {
        if( tile.over_avatar_region && tile.position.z() > cut.origin.z() ) {
            FAIL( "the overhead cut kept an overhead cell of the avatar's region" );
        }
    }
    CHECK( cut.cells.size() <= eye_view.cells.size() );
    // The cut is surgical: every cell it removed belongs to the avatar's own
    // region above its own storey, and every cell it kept outside that region is
    // byte-for-byte the same knowledge the eye camera produced. No neighbouring
    // building, room or remembered tile is touched.
    size_t removed = 0;
    for( const auto &tile : eye_view.cells ) {
        const bool kept = std::any_of( cut.cells.begin(), cut.cells.end(), [&]( const auto & other ) {
            return other.position == tile.position;
        } );
        if( kept ) {
            continue;
        }
        ++removed;
        CHECK( tile.over_avatar_region );
        CHECK( tile.position.z() > eye_view.origin.z() );
    }
    size_t outside_region = 0;
    for( const auto &tile : cut.cells ) {
        if( tile.over_avatar_region ) {
            continue;
        }
        ++outside_region;
        const auto *before = find_cell( eye_view, tile.position );
        REQUIRE( before );
        CHECK( before->terrain == tile.terrain );
        CHECK( before->kind == tile.kind );
        CHECK( before->known == tile.known );
        CHECK( before->ceiling_present == tile.ceiling_present );
        CHECK( before->exterior_only == tile.exterior_only );
    }
    CHECK( outside_region > 0 );

    // The avatar's own tile sprite exists for the overhead drawer only.
    const auto avatar_sprite = std::find_if( cut.sprites.begin(), cut.sprites.end(),
    [&]( const auto & sprite ) {
        return sprite.is_avatar;
    } );
    REQUIRE( avatar_sprite != cut.sprites.end() );
    CHECK( avatar_sprite->position == cut.origin );
    CHECK( avatar_sprite->id == ( you.male ? "player_male" : "player_female" ) );
    // It is not a second creature: nothing in the snapshot can be attacked as
    // the avatar's own body.
    CHECK( std::count_if( cut.sprites.begin(), cut.sprites.end(), []( const auto & sprite ) {
        return sprite.is_avatar;
    } ) == 1 );
}

TEST_CASE( "fp3d_natural_ground_connections_follow_captured_neighbours", "[fp3d][ground_terrain]" )
{
    const std::array<tripoint_rel_ms, 4> dirs{ tripoint_rel_ms::south,
        tripoint_rel_ms::east, tripoint_rel_ms::west, tripoint_rel_ms::north };
    for( const std::string id : { "t_grass", "t_grass_long", "t_grass_dead", "t_dirt", "t_sand", "t_moss" } ) {
        for( int mask = 0; mask < 16; ++mask ) {
            CAPTURE( id, mask );
            fp3d_snapshot::scene_snapshot scene;
            fp3d_snapshot::cell tile;
            tile.terrain = id;
            tile.position = tripoint_abs_ms::zero;
            scene.cells.push_back( tile );
            for( int i = 0; i < 4; ++i ) {
                if( mask & ( 1 << i ) ) {
                    tile.position = tripoint_abs_ms::zero + dirs[i];
                    tile.known = fp3d_snapshot::knowledge::remembered;
                    scene.cells.push_back( tile );
                }
            }
            fp3d_snapshot::connect_ground_terrain( scene );
            CHECK( scene.cells.front().ground_connections == mask );
            auto previous = scene;
            const auto before = fp3d_materials::terrain_key( scene.cells.front() );
            scene.cells.front().ground_connections ^= 1;
            CHECK( fp3d_materials::terrain_key( scene.cells.front() ) != before );
            previous.geometry_generation = 1234;
            fp3d_snapshot::reuse_geometry( scene, &previous );
            CHECK( scene.geometry_generation != 1234 );
        }
    }
    fp3d_snapshot::scene_snapshot scene;
    fp3d_snapshot::cell tile;
    tile.terrain = "t_grass";
    tile.position = tripoint_abs_ms::zero;
    scene.cells.push_back( tile );
    tile.position += tripoint_rel_ms::east;
    tile.terrain = "t_grass_long";
    scene.cells.push_back( tile );
    tile.position = tripoint_abs_ms::zero + tripoint_rel_ms::above;
    tile.terrain = "t_grass";
    scene.cells.push_back( tile );
    fp3d_snapshot::connect_ground_terrain( scene );
    CHECK( scene.cells.front().ground_connections == 0 );
    scene.cells.back().terrain = "t_floor";
    fp3d_snapshot::connect_ground_terrain( scene );
    CHECK( scene.cells.back().ground_connections == -1 );
}

#endif
