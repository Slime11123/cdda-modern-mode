#include "fp3d_furniture.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "construction.h"
#include "fp3d_construction.h"
#include "fp3d_interaction.h"
#include "fp3d_snapshot.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "options_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "rng.h"

TEST_CASE( "fp3d_furniture_blueprints_use_native_projects_without_changing_the_world",
           "[fp3d][furniture][blueprint]" )
{
    const auto &you = get_avatar();
    const auto &here = get_map();
    const auto origin = you.pos_abs();
    const auto before = here.furn( you.pos_bub( here ) );
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const auto moves = you.get_moves();
    for( const std::string id : {
             "f_table", "f_chair", "f_bed", "f_bed_frame", "f_crate_c",
             "f_crate_o", "f_cupboard", "f_locker", "f_bookcase", "f_rack"
         } ) {
        CAPTURE( id );
        const auto project = std::find_if( get_constructions().begin(), get_constructions().end(),
        [&]( const construction & entry ) {
            return entry.post_is_furniture && entry.post_terrain == id;
        } );
        REQUIRE( project != get_constructions().end() );
        const auto ghost = fp3d_construction::terrain_preview( *project, origin );
        REQUIRE( ghost );
        REQUIRE( ghost->furniture );
        CHECK( ghost->furniture->id == id );
        CHECK( ghost->position == origin );
        CHECK_FALSE( ghost->floor_present );
        CHECK_FALSE( ghost->goes_up );
        CHECK_FALSE( ghost->ceiling_present );
    }
    const auto variant = fp3d_furniture::describe( "f_table_fancy" );
    REQUIRE( variant );
    CHECK( variant->shape == fp3d_furniture::kind::table );
    CHECK( variant->id == "f_table_fancy" );
    const auto locker = fp3d_furniture::describe( "f_locker" );
    const auto bookcase = fp3d_furniture::describe( "f_bookcase" );
    REQUIRE( locker );
    REQUIRE( bookcase );
    CHECK( locker->shape == fp3d_furniture::kind::locker );
    CHECK( locker->shape != bookcase->shape );
    CHECK_FALSE( fp3d_furniture::describe( "f_woodstove" ) );
    CHECK_FALSE( fp3d_furniture::describe( "not_a_furniture" ) );
    CHECK( here.furn( you.pos_bub( here ) ) == before );
    CHECK( you.pos_abs() == origin );
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == clock );
    CHECK( rng_get_engine() == engine );
}

TEST_CASE( "fp3d_furniture_capture_items_and_revalidation_follow_native_visibility",
           "[fp3d][furniture][vision][interaction]" )
{
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    for( const std::string id : {
             "f_table", "f_chair", "f_bed"
         } ) {
        CAPTURE( id );
        here.furn_set( p, furn_str_id( id ) );
        here.i_clear( p );
        fp3d_snapshot::prepare_view( you, here );
        const auto engine = rng_get_engine();
        const auto clock = calendar::turn;
        const auto moves = you.get_moves();
        const auto scene = fp3d_snapshot::capture( you, here );
        const auto tile = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & entry ) {
            return entry.position == here.get_abs( p );
        } );
        REQUIRE( tile != scene.cells.end() );
        REQUIRE( tile->furniture );
        CHECK( tile->furniture->id == id );
        const auto target = fp3d_targeting::ray( scene, 0, 40 );
        REQUIRE( target );
        CHECK( target->type == fp3d_targeting::kind::furniture );
        CHECK( target->id == id );
        CHECK( fp3d_interaction::valid( *target, you, here ) );
        CHECK( rng_get_engine() == engine );
        CHECK( calendar::turn == clock );
        CHECK( you.get_moves() == moves );
        here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
        fp3d_snapshot::prepare_view( you, here );
        const auto with_item = fp3d_snapshot::capture( you, here );
        const auto image = std::find_if( with_item.sprites.begin(), with_item.sprites.end(),
        [&]( const auto & entry ) {
            return entry.position == here.get_abs( p ) && entry.kind == fp3d_snapshot::sprite_kind::item;
        } );
        REQUIRE( image != with_item.sprites.end() );
        CHECK( image->elevation ==
               Approx( fp3d_furniture::item_surface( tile->furniture->shape ) ) );
        const auto object = fp3d_targeting::ray( with_item, 0, 30 );
        REQUIRE( object );
        CHECK( object->type == fp3d_targeting::kind::item );
        CHECK( object->id == "rock" );
        here.furn_set( p, furn_str_id( "f_null" ) );
        CHECK_FALSE( fp3d_interaction::valid( *target, you, here ) );
        you.memorize_terrain( here.get_abs( p ), "t_floor", 0, 0 );
        here.invalidate_visibility_cache();
        const auto hidden = fp3d_snapshot::capture( you, here );
        CHECK( hidden.sprites.empty() );
        CHECK( std::none_of( hidden.cells.begin(), hidden.cells.end(), []( const auto & entry ) {
            return entry.furniture.has_value();
        } ) );
    }
}

TEST_CASE( "fp3d_furniture_parts_fit_their_tiles_and_display_surfaces", "[fp3d][furniture]" )
{
    for( const std::string id : {
             "f_table", "f_chair", "f_bed", "f_bed_frame", "f_crate_c", "f_crate_o",
             "f_cupboard", "f_locker", "f_bookcase", "f_rack",
             "f_counter", "f_desk", "f_sofa", "f_sink", "f_oven", "f_fridge",
             "f_minifridge", "f_dresser", "f_wardrobe", "f_bathtub", "f_toilet",
             "f_bench", "f_bench_wooden", "f_bench_park", "f_armchair", "f_fireplace",
             "f_fireplace_stone"
         } ) {
        CAPTURE( id );
        const auto model = fp3d_furniture::describe( id );
        REQUIRE( model );
        CHECK( model->id == id );
        const auto &parts = fp3d_furniture::parts( model->shape );
        REQUIRE_FALSE( parts.empty() );
        CHECK( std::any_of( parts.begin(), parts.end(), []( const auto &part ) { return part.bottom == 0; } ) );
        for( const auto &part : parts ) {
            CHECK( part.width > 0 );
            CHECK( part.depth > 0 );
            CHECK( part.bottom >= 0 );
            CHECK( part.top > part.bottom );
            CHECK( part.top <= fp3d_camera::floor_height );
            CHECK( std::abs( part.x ) + part.width / 2 <= .501f );
            CHECK( std::abs( part.z ) + part.depth / 2 <= .501f );
        }
        const float elevation = fp3d_furniture::item_surface( model->shape );
        CHECK( std::any_of( parts.begin(), parts.end(), [&]( const auto &part ) {
            return std::abs( part.x ) <= part.width / 2 && std::abs( part.z ) <= part.depth / 2 &&
                   ( fp3d_furniture::shows_contents( model->shape ) ?
                     elevation > part.bottom && elevation < part.top : elevation == part.top );
        } ) );
    }
    CHECK_FALSE( fp3d_furniture::describe( "f_counter_gate_c" ) );
    CHECK_FALSE( fp3d_furniture::describe( "f_counter_gate_o" ) );
    CHECK( fp3d_furniture::parts( fp3d_furniture::kind::mini_fridge ).front().top <
           fp3d_furniture::parts( fp3d_furniture::kind::fridge ).front().top );
}

TEST_CASE( "fp3d_plumbing_rays_distinguish_basin_floors_and_rims",
           "[fp3d][furniture][ray]" )
{
    fp3d_snapshot::scene_snapshot scene;
    fp3d_snapshot::cell fixture;
    fixture.position = scene.origin;
    fixture.terrain = "t_floor";
    scene.cells.push_back( fixture );
    for( const std::string id : { "f_sink", "f_bathtub", "f_toilet" } ) {
        CAPTURE( id );
        auto &model = scene.cells.front().furniture;
        model = fp3d_furniture::describe( id );
        REQUIRE( model );
        const auto basin = fp3d_targeting::ray( scene, 0, 90 );
        REQUIRE( basin );
        CHECK( basin->id == id );
        REQUIRE( basin->outline );
        const auto top = []( const fp3d_targeting::target &hit ) {
            return hit.outline->corners.front()[1] + hit.outline->height;
        };
        CHECK( top( *basin ) == Approx( fp3d_furniture::item_surface( model->shape ) ) );
        const float edge_x = id == "f_toilet" ? .19f : .37f;
        const auto rim = fp3d_targeting::ray( scene, 0, 90, { edge_x, 0 } );
        REQUIRE( rim );
        CHECK( rim->id == id );
        REQUIRE( rim->outline );
        CHECK( top( *rim ) > top( *basin ) );
    }
}

TEST_CASE( "fp3d_storage_ray_sees_items_only_through_their_own_cabinet", "[fp3d][furniture][ray]" )
{
    fp3d_snapshot::scene_snapshot scene;
    fp3d_snapshot::cell rear;
    rear.position = tripoint_abs_ms( 0, -3, 0 );
    rear.terrain = "t_floor";
    rear.furniture = fp3d_furniture::describe( "f_locker" );
    REQUIRE( rear.furniture );
    scene.cells.push_back( rear );
    fp3d_snapshot::sprite item;
    item.position = rear.position;
    item.kind = fp3d_snapshot::sprite_kind::item;
    item.id = "apple";
    item.elevation = 1;
    item.height = .7f;
    scene.sprites.push_back( item );
    auto hit = fp3d_targeting::ray( scene, 0, 0 );
    REQUIRE( hit );
    CHECK( hit->type == fp3d_targeting::kind::item );
    CHECK_FALSE( hit->outline );

    scene.sprites.front().kind = fp3d_snapshot::sprite_kind::monster;
    hit = fp3d_targeting::ray( scene, 0, 0 );
    REQUIRE( hit );
    CHECK( hit->type == fp3d_targeting::kind::furniture );
    CHECK( hit->outline );

    scene.sprites.front() = item;
    auto front = rear;
    front.position = tripoint_abs_ms( 0, -1, 0 );
    scene.cells.push_back( front );
    hit = fp3d_targeting::ray( scene, 0, 0 );
    REQUIRE( hit );
    CHECK( hit->type == fp3d_targeting::kind::furniture );
    CHECK( hit->position == front.position );
    scene.cells.back().kind = fp3d_snapshot::terrain_kind::wall;
    scene.cells.back().furniture.reset();
    hit = fp3d_targeting::ray( scene, 0, 0 );
    REQUIRE( hit );
    CHECK( hit->type == fp3d_targeting::kind::terrain );
    CHECK( hit->position == front.position );
}

TEST_CASE( "fp3d_furniture_ray_uses_model_parts_and_respects_wall_occlusion",
           "[fp3d][furniture][ray]" )
{
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms::zero;
    fp3d_snapshot::cell table;
    table.position = tripoint_abs_ms( 0, -2, 0 );
    table.terrain = "t_floor";
    table.floor_present = false;
    table.furniture = fp3d_furniture::describe( "f_table" );
    scene.cells.push_back( table );
    fp3d_snapshot::cell wall;
    wall.position = tripoint_abs_ms( 0, -3, 0 );
    wall.terrain = "t_wall";
    wall.kind = fp3d_snapshot::terrain_kind::wall;
    scene.cells.push_back( wall );
    const auto top = fp3d_targeting::ray( scene, 0, 22 );
    REQUIRE( top );
    CHECK( top->type == fp3d_targeting::kind::furniture );
    const auto gap = fp3d_targeting::ray( scene, 0, 31 );
    REQUIRE( gap );
    CHECK( gap->position == wall.position ); // Open space between the legs is not a solid cube.
    scene.cells.back().position = tripoint_abs_ms( 0, -1, 0 );
    const auto occluded = fp3d_targeting::ray( scene, 0, 22 );
    REQUIRE( occluded );
    CHECK( occluded->position == scene.cells.back().position );
}

TEST_CASE( "fp3d_storage_models_keep_native_sealed_contents_hidden",
           "[fp3d][furniture][vision][interaction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.add_item_or_charges( p, item( itype_id( "rock" ) ) );
    for( const std::string id : {
             "f_crate_o", "f_crate_c", "f_cupboard", "f_locker",
             "f_bookcase", "f_rack", "f_bed_frame"
         } ) {
        CAPTURE( id );
        here.furn_set( p, furn_str_id( id ) );
        fp3d_snapshot::prepare_view( you, here );
        const auto engine = rng_get_engine();
        const auto clock = calendar::turn;
        const auto moves = you.get_moves();
        const auto scene = fp3d_snapshot::capture( you, here );
        const auto tile = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & entry ) {
            return entry.position == here.get_abs( p );
        } );
        REQUIRE( tile != scene.cells.end() );
        REQUIRE( tile->furniture );
        CHECK( tile->furniture->id == id );
        const auto image = std::find_if( scene.sprites.begin(),
        scene.sprites.end(), [&]( const auto & entry ) {
            return entry.position == here.get_abs( p ) && entry.kind == fp3d_snapshot::sprite_kind::item;
        } );
        CHECK( ( image != scene.sprites.end() ) == ( id != "f_crate_c" ) );
        const fp3d_targeting::target target{ you.pos_abs(), here.get_abs( p ),
                                             fp3d_targeting::kind::furniture, id };
        CHECK( fp3d_interaction::valid( target, you, here ) );
        if( id == "f_crate_c" ) {
            const auto choices = fp3d_interaction::context_entries( target, you, here );
            CHECK( std::none_of( choices.begin(), choices.end(), []( const auto & entry ) {
                return entry.action == ACTION_PICKUP && entry.enabled;
            } ) );
            CHECK_FALSE( fp3d_interaction::quick_pickup( you, here, p, true ) );
        } else {
            REQUIRE( image != scene.sprites.end() );
            CHECK( image->elevation == Approx( fp3d_furniture::item_surface( tile->furniture->shape ) ) );
        }
        CHECK( here.i_at( p ).size() == 1 );
        CHECK( calendar::turn == clock );
        CHECK( you.get_moves() == moves );
        CHECK( rng_get_engine() == engine );
    }
}

// Starts from committed components, as when resuming native ACT_BUILD. Initial
// placement/component-choice UI and the full world turn loop are not exercised.
TEST_CASE( "fp3d_bed_frame_changes_to_completed_model_after_native_build_activity",
           "[fp3d][furniture][construction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    set_time_to_day();
    override_option scaling( "CONSTRUCTION_SCALING", "100" );
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto p = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.furn_set( p, furn_str_id( "f_bed_frame" ) );
    const auto &projects = get_constructions();
    const auto project = std::find_if( projects.begin(),
    projects.end(), []( const construction & entry ) {
        return entry.str_id == construction_str_id( "constr_finish_bed" );
    } );
    REQUIRE( project != projects.end() );
    partial_con partial;
    partial.id = project->id;
    partial.components.emplace_back( itype_id( "mattress" ) );
    here.partial_con_set( p, partial );
    you.assign_activity( activity_id( "ACT_BUILD" ) );
    you.activity.placement = here.get_abs( p );
    fp3d_snapshot::prepare_view( you, here );
    const auto before = fp3d_snapshot::capture( you, here );
    const auto model_at = [&]( const fp3d_snapshot::scene_snapshot & scene ) {
        const auto tile = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & entry ) {
            return entry.position == here.get_abs( p );
        } );
        REQUIRE( tile != scene.cells.end() );
        REQUIRE( tile->furniture );
        return *tile->furniture;
    };
    CHECK( model_at( before ).shape == fp3d_furniture::kind::bed_frame );
    const auto ghost = fp3d_construction::terrain_preview( *project, here.get_abs( p ) );
    REQUIRE( ghost );
    REQUIRE( ghost->furniture );
    CHECK( ghost->furniture->shape == fp3d_furniture::kind::bed );
    you.set_moves( 100 );
    you.activity.do_turn( you );
    REQUIRE( here.partial_con_at( p ) );
    CHECK( here.partial_con_at( p )->counter > 0 );
    const int counter = here.partial_con_at( p )->counter;
    you.cancel_activity();
    CHECK( you.activity.is_null() );
    REQUIRE( here.partial_con_at( p ) );
    CHECK( here.partial_con_at( p )->counter == counter );
    CHECK( here.partial_con_at( p )->components.size() == 1 );
    CHECK( here.furn( p ) == furn_str_id( "f_bed_frame" ) );
    you.assign_activity( activity_id( "ACT_BUILD" ) );
    you.activity.placement = here.get_abs( p );
    int ticks = 0;
    while( you.activity && ticks < 10000 ) {
        you.set_moves( 100 );
        you.activity.do_turn( you );
        calendar::turn += 1_seconds;
        ++ticks;
    }
    CHECK( ticks > 1 );
    CHECK( ticks < 10000 );
    CHECK( you.activity.is_null() );
    CHECK_FALSE( here.partial_con_at( p ) );
    CHECK( here.furn( p ) == furn_str_id( "f_bed" ) );
    CHECK( here.i_at( p ).empty() );
    fp3d_snapshot::prepare_view( you, here );
    const auto after = fp3d_snapshot::capture( you, here );
    CHECK( model_at( after ).shape == ghost->furniture->shape );
    CHECK( model_at( after ).id == ghost->furniture->id );
}
#endif
