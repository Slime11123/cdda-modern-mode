#include "fp3d_cell_index.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <chrono>
#include <cstring>
#include <limits>

#include "cata_catch.h"
#include "fp3d_gpu_occlusion.h"
#include "fp3d_gpu_presentation.h"
#include "fp3d_gpu_terrain_compute.h"
#include "fp3d_gpu_world_qa.h"
#include "fp3d_targeting.h"

TEST_CASE( "fp3d_input_frame_advances_only_with_sdl_presentation",
           "[fp3d][gpu_presentation]" )
{
    fp3d_gpu::frame_presentation<unsigned> view;
    CHECK_FALSE( view.target_view() );
    view.submit( 1 );
    CHECK_FALSE( view.target_view() );
    view.begin_composition();
    view.world_composed();
    CHECK( view.target_view() == 1 );
    view.finish_present( true );
    view.submit( 2 );
    CHECK( view.target_view() == 1 );
    CHECK( view.submitted_view() == 2 );

    view.begin_composition();
    view.world_composed();
    CHECK( view.target_view() == 2 );
    view.finish_present( false );
    CHECK( view.target_view() == 1 );

    // A skipped native submit leaves the same texture and metadata available.
    view.begin_composition();
    view.world_composed();
    view.finish_present( true );
    CHECK( view.target_view() == 2 );

    view.begin_composition();
    CHECK_FALSE( view.target_view() );
    view.finish_present( true );
    CHECK_FALSE( view.target_view() );

    view.clear();
    view.begin_composition();
    view.world_composed();
    CHECK_FALSE( view.target_view() );
    view.finish_present( true );
    CHECK_FALSE( view.target_view() );
}

TEST_CASE( "fp3d_world_prepares_at_most_once_while_a_submission_is_busy",
           "[fp3d][gpu_presentation]" )
{
    using fp3d_gpu::preparation_mode;
    fp3d_gpu::frame_preparation preparation;
    CHECK( preparation.choose( false, false ) == preparation_mode::defer );
    CHECK( preparation.choose( true, false ) == preparation_mode::refresh_materials );
    preparation.submitted();
    CHECK( preparation.choose( false, true ) == preparation_mode::reuse_materials );
    CHECK( preparation.choose( false, true ) == preparation_mode::defer );
    CHECK( preparation.choose( false, false ) == preparation_mode::defer );
    CHECK( preparation.choose( true, true ) == preparation_mode::refresh_materials );
    CHECK( preparation.choose( false, true ) == preparation_mode::defer );
    preparation.submitted();
    CHECK( preparation.choose( false, true ) == preparation_mode::reuse_materials );
}

TEST_CASE( "fp3d_iris_collects_prepared_water_without_wet_pavement", "[fp3d][iris_water]" )
{
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 64, 64, 0 );
    scene.weather.particles = fp3d_weather::precipitation::rain;
    scene.weather.intensity = 1.f;
    for( const char *terrain : { "t_water_sh", "t_water_dp", "t_pavement" } ) {
        const int x = static_cast<int>( scene.cells.size() ) - 1;
        auto &tile = scene.cells.emplace_back();
        tile.position = scene.origin + tripoint_rel_ms( x, -3, 0 );
        tile.terrain = terrain;
    }
    fp3d_camera::pose pose;
    pose.eye.height = 1.5f;
    const auto world = prepare( scene, pose, false, 512, 512, true );
    REQUIRE( world );
    REQUIRE( std::any_of( world->reflective_batches.begin(), world->reflective_batches.end(),
    []( const reflective_batch &entry ) { return entry.surface == reflective_surface::asphalt; } ) );
    const fp3d_gpu::indexed_mesh dynamic;
    const fp3d_gpu::indexed_mesh resident;
    const world_meshes meshes{ dynamic, &resident, { 4, 2, -3 } };
    const auto selected = collect_water_geometry( *world, meshes );
    REQUIRE( selected.size() == 2 );
    for( const auto surface : { reflective_surface::water_shallow, reflective_surface::water_deep } ) {
        const auto source = std::find_if( world->reflective_batches.begin(), world->reflective_batches.end(),
        [surface]( const reflective_batch &entry ) { return entry.surface == surface; } );
        const auto draw = std::find_if( selected.begin(), selected.end(),
        [surface]( const water_geometry_draw &entry ) { return entry.surface == surface; } );
        REQUIRE( source != world->reflective_batches.end() );
        REQUIRE( draw != selected.end() );
        CHECK( draw->draw.first_index == source->draws.first_index );
        CHECK( draw->draw.index_count == source->draws.index_count );
        CHECK( draw->draw.first_instance == source->draws.first_instance );
        CHECK( draw->draw.instance_count == source->draws.instance_count );
        CHECK( draw->draw.mesh == ( source->draws.resident ? &resident : &dynamic ) );
    }
    prepared_world resident_water;
    resident_water.reflective_batches.push_back( { { 7, 6, 21, 2, true, true }, {},
                                                 reflective_surface::water_deep } );
    const auto anchored = collect_water_geometry( resident_water, meshes );
    REQUIRE( anchored.size() == 1 );
    CHECK( anchored[0].draw.mesh == &resident );
    CHECK( anchored[0].draw.first_instance == 21 );
    CHECK( anchored[0].anchor_to_scene == meshes.resident_to_scene );
}

TEST_CASE( "fp3d_pickup_highlight_is_scoped_to_the_selected_container",
           "[fp3d][gpu_presentation]" )
{
    using highlight = fp3d_compositor::pickup_highlight;
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 64, 64, 0 );
    const tripoint_rel_ms offset( 0, -3, 0 );
    for( int x = 0; x < 2; ++x ) {
        auto &cell = scene.cells.emplace_back();
        cell.position = scene.origin + offset + tripoint_rel_ms( x, 0, 0 );
        cell.furniture = fp3d_furniture::describe( "f_locker" );
        REQUIRE( cell.furniture );
    }
    const highlight selected{ highlight::kind::furniture, scene.origin + offset, "f_locker", {} };
    fp3d_camera::pose view;
    view.eye.height = 1.5f;
    const auto render = [&]( const highlight *source ) {
        return prepare( scene, view, false, 512, 512, false, false, false, false,
                        nullptr, nullptr, nullptr, false, source );
    };
    const auto normal = render( nullptr );
    const auto picking = render( &selected );
    REQUIRE( normal );
    REQUIRE( picking );
    const auto white_models = []( const prepared_world &world ) {
        return std::count_if( world.instances.begin(), world.instances.end(), []( const auto &instance ) {
            return instance.offset[3] == fp3d_gpu::white_model_material;
        } );
    };
    CHECK( white_models( *normal ) == 0 );
    CHECK( white_models( *picking ) == 1 );
}

TEST_CASE( "fp3d_curtain_peek_blends_only_the_window_being_used",
           "[fp3d][gpu_presentation][windows]" )
{
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 64, 64, 0 );
    for( int x = 0; x < 2; ++x ) {
        auto &cell = scene.cells.emplace_back();
        cell.position = scene.origin + tripoint_rel_ms( x, -2, 0 );
        cell.terrain = "t_curtains";
        cell.kind = fp3d_snapshot::terrain_kind::window;
        cell.pane = fp3d_snapshot::window_pane::covered;
    }
    fp3d_camera::pose view;
    view.eye.height = 1.65f;
    const auto render = [&]( const tripoint_abs_ms *peek ) {
        return prepare( scene, view, false, 512, 512, false, false, false, false,
                        nullptr, nullptr, nullptr, false, nullptr, peek );
    };
    const auto normal = render( nullptr );
    const auto peeking = render( &scene.cells.front().position );
    REQUIRE( normal );
    REQUIRE( peeking );
    CHECK( normal->pane_count == 0 );
    CHECK( peeking->pane_count == 1 );
}

TEST_CASE( "fp3d_occlusion_keeps_bounds_touching_clip_planes",
           "[fp3d][gpu_occlusion]" )
{
    using namespace fp3d_gpu::occlusion;
    const fp3d_gpu_camera_math::matrix4 clip{
        { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
    };
    const viewport view{ 128, 96, 1 };
    const auto middle = project( clip, { 0, 0, .5f }, { .25f, .25f, .125f }, view );
    REQUIRE( middle.eligible == 1 );
    CHECK( middle.left == 47 );
    CHECK( middle.right == 80 );
    CHECK( middle.top == 35 );
    CHECK( middle.bottom == 60 );
    CHECK( middle.near_depth == .375f );
    CHECK( project( clip, { 0, 0, .05f }, { .25f, .25f, .125f }, view ).eligible == 0 );
    CHECK( project( clip, { 0, 0, .99f }, { .25f, .25f, .125f }, view ).eligible == 0 );
    CHECK( project( clip, { .9f, 0, .5f }, { .2f, .25f, .125f }, view ).eligible == 0 );
    CHECK( project( clip, { 0, 0, .5f }, { -1, .25f, .125f }, view ).eligible == 0 );
    CHECK( project( clip, { 0, 0, .5f }, { .25f, .25f, .125f }, { 4097, 96, 1 } ).eligible == 0 );
    CHECK( tile_count( 128 ) == 16 );
    CHECK( tile_count( 129 ) == 17 );
}

TEST_CASE( "fp3d_occlusion_leaves_prepass_and_unselected_batches_in_original_order",
           "[fp3d][gpu_occlusion]" )
{
    using namespace fp3d_gpu_world_qa;
    prepared_world world;
    world.width = world.height = 128;
    world.camera.elements = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    auto geometry = std::make_shared<fp3d_geometry::mesh>();
    geometry->vertices.resize( 4 );
    geometry->vertices[0].position = { -.25f, -.25f, .25f };
    geometry->vertices[1].position = { .25f, -.25f, .25f };
    geometry->vertices[2].position = { .25f, .25f, .25f };
    geometry->vertices[3].position = { -.25f, .25f, .25f };
    geometry->indices = { 0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3 };
    world.resident_geometry = geometry;
    auto instances = std::make_shared<std::vector<fp3d_gpu::mesh_instance>>( 128 );
    ( *instances )[64].offset[2] = .25f;
    world.resident_pages = instances;
    world.batches = { { 0, 12, 0, 1, true, true },
        { 0, 6, 64, 1, true, true }, { 0, 6, 0, 1 }
    };
    world.opaque_batches = 2;
    const auto inputs = prepare_occlusion( world );
    REQUIRE( inputs.candidates.size() == 3 );
    CHECK( inputs.occluders == std::vector<Uint32>{ 0 } );
    CHECK( inputs.candidate_count == 1 );
    CHECK( inputs.candidates[0].eligible == 0 );
    CHECK( inputs.candidates[1].eligible == 1 );
    CHECK( inputs.candidates[1].near_depth == .5f );
    CHECK( inputs.candidates[2].eligible == 0 );
}

TEST_CASE( "fp3d_compute_frustum_preserves_terrain_crossing_clip_planes",
           "[fp3d][terrain_compute]" )
{
    using namespace fp3d_gpu::terrain_compute;
    fp3d_gpu_camera_math::camera camera;
    camera.eye_height = 0;
    camera.horizontal_fov_degrees = 90;
    camera.far_clip = 64;
    const auto projection = fp3d_gpu_camera_math::clip_from_world( camera );
    REQUIRE( projection );
    const auto planes = clip_planes( *projection );
    const vec4 extent{ .5f, .5f, .5f, 0.f };
    CHECK( intersects( planes, { 0.f, 0.f, -3.f, 0.f }, extent ) );
    CHECK( intersects( planes, { 3.5f, 0.f, -3.f, 0.f }, extent ) );
    CHECK( intersects( planes, { 0.f, 0.f, 0.f, 0.f }, extent ) );
    CHECK_FALSE( intersects( planes, { 0.f, 0.f, 1.f, 0.f }, extent ) );
    CHECK_FALSE( intersects( planes, { 8.f, 0.f, -3.f, 0.f }, extent ) );
    CHECK_FALSE( intersects( planes, { 0.f, 0.f, -70.f, 0.f }, extent ) );

    fp3d_geometry::mesh mesh;
    mesh.vertices.resize( 4 );
    mesh.vertices[0].position = { -1.25f, 0.f, 0.f };
    mesh.vertices[1].position = { 1.25f, 0.f, 0.f };
    mesh.vertices[2].position = { 0.f, 3.f, 0.f };
    mesh.vertices[3].position = { 80.f, 80.f, 80.f }; // Not referenced by this model.
    mesh.indices = { 0, 1, 2, 1, 2, 6 };
    const auto bound = make_model( mesh, 0, 3, { 0, 0, 1, 1 }, 0 );
    REQUIRE( bound );
    CHECK( bound->center == vec4{ 0.f, 1.5f, 0.f, 0.f } );
    CHECK( bound->half_extent == vec4{ 1.25f, 1.5f, 0.f, 0.f } );
    CHECK_FALSE( make_model( mesh, 3, 3, {}, 0 ) );
    CHECK_FALSE( make_model( mesh, 6, 3, {}, 0 ) );
    const std::vector<cell> cells{ { { 4, 0, -3 } }, { { 7, 4, -3 } } };
    const auto group = make_block( cells, 0, 2, *bound, 0, 12 );
    REQUIRE( group );
    CHECK( group->center == vec4{ 5.5f, 3.5f, -3.f, 0.f } );
    CHECK( group->half_extent == vec4{ 2.75f, 3.5f, 0.f, 0.f } );
    CHECK( intersects( planes, group->center, group->half_extent ) );
    CHECK_FALSE( make_block( cells, 1, 2, *bound, 0, 12 ) );
}

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

TEST_CASE( "fp3d_cached_terrain_matches_full_scene_gather", "[fp3d][static_floor_cache]" )
{
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    scene.geometry_generation = 1;
    terrain_index cache;
    terrain_index resident( true, true );
    const auto tile = [&]( int x, int y, int z, const std::string &id ) {
        fp3d_snapshot::cell c;
        c.position = tripoint_abs_ms( x, y, z );
        c.terrain = id;
        scene.cells.push_back( c );
    };
    tile( -9, -6, 0, "t_pavement" );
    tile( -8, -6, 0, "t_water_dp" );
    tile( -7, -6, 0, "t_concrete_floor" );
    tile( -7, -6, 1, "t_concrete_floor" );
    tile( -6, -6, 0, "t_grass" );
    tile( -5, -6, 0, "t_floor_metal_gangway" );
    tile( -4, -6, 0, "t_wood_stairs_up_half" );
    tile( -3, -6, 0, "" );
    tile( -2, -6, 0, "t_dirt" );
    scene.cells.back().known = fp3d_snapshot::knowledge::remembered;
    scene.cells.back().ground_connections = 3;
    tile( -8, -8, 0, "t_wall" );
    scene.cells.back().kind = fp3d_snapshot::terrain_kind::wall;
    tile( -8, -8, 1, "t_water_dp" );
    tile( -5, -8, 0, "t_wall" );
    scene.cells.back().kind = fp3d_snapshot::terrain_kind::wall;
    scene.cells.back().known = fp3d_snapshot::knowledge::remembered;
    scene.cells.back().floor_present = false;
    tile( -6, -7, 0, "t_door_glass_c" );
    scene.cells.back().kind = fp3d_snapshot::terrain_kind::glass;
    fp3d_camera::pose view;
    view.eye.x = -5.f;
    view.eye.height = 4.f;
    view.pitch = 27.f;

    const auto compare = [&] {
        const auto direct = prepare( scene, view, false, 512, 512, true );
        const auto cached = prepare( scene, view, false, 512, 512, true,
                                     false, false, false, nullptr, nullptr, &cache );
        const auto split = prepare( scene, view, false, 512, 512, true,
                                    false, false, false, nullptr, nullptr, &resident );
        REQUIRE( direct );
        REQUIRE( cached );
        REQUIRE( split );
        REQUIRE( split->resident_geometry );
        REQUIRE_FALSE( split->instances.empty() );
        REQUIRE( split->resident_instance_count > 0 );
        REQUIRE( split->batches.size() == direct->batches.size() );
        REQUIRE( split->instances.size() + split->resident_instance_count == direct->instances.size() );
        for( size_t i = 0; i < split->batches.size(); ++i ) {
            const auto &draw = split->batches[i];
            const auto &expected = direct->batches[i];
            const auto &mesh = draw.resident ? *split->resident_geometry : split->geometry;
            REQUIRE( draw.index_count == expected.index_count );
            REQUIRE( draw.instance_count == expected.instance_count );
            const auto &stream = draw.paged_instances ? *split->resident_pages :
                                 draw.resident ? split->resident_instances : split->instances;
            const size_t prefix = draw.resident && !draw.paged_instances && split->resident_pages ?
                                  split->resident_pages->size() : 0;
            REQUIRE( draw.first_instance >= prefix );
            const size_t first_instance = draw.first_instance - prefix;
            REQUIRE( first_instance + draw.instance_count <= stream.size() );
            for( Uint32 n = 0; n < draw.instance_count; ++n ) {
                const auto &instance = stream[first_instance + n];
                const auto &expected_instance = direct->instances[expected.first_instance + n];
                auto offset = instance.offset;
                if( draw.resident ) {
                    offset[0] += split->resident_to_scene[0];
                    offset[2] += split->resident_to_scene[2];
                }
                REQUIRE( offset == expected_instance.offset );
                REQUIRE( instance.uv_rect == expected_instance.uv_rect );
                REQUIRE( instance.tint == expected_instance.tint );
            }
            REQUIRE( draw.first_index + draw.index_count <= mesh.indices.size() );
            for( Uint32 n = 0; n < draw.index_count; ++n ) {
                const auto &v = mesh.vertices.at( mesh.indices[draw.first_index + n] );
                const auto &e = direct->geometry.vertices.at( direct->geometry.indices[expected.first_index + n] );
                REQUIRE( v.position == e.position );
                REQUIRE( v.normal == e.normal );
                REQUIRE( v.uv == e.uv );
                REQUIRE( v.color == e.color );
            }
        }
        REQUIRE( direct->floor_count > 0 );
        CHECK( cached->floor_count == direct->floor_count );
        CHECK( cached->wall_count == direct->wall_count );
        CHECK( cached->uncapped_walls == direct->uncapped_walls );
        CHECK( cached->missing == direct->missing );
        CHECK( cached->geometry.indices == direct->geometry.indices );
        REQUIRE( cached->geometry.vertices.size() == direct->geometry.vertices.size() );
        for( size_t i = 0; i < cached->geometry.vertices.size(); ++i ) {
            const auto &v = cached->geometry.vertices[i];
            const auto &expected = direct->geometry.vertices[i];
            REQUIRE( v.position == expected.position );
            REQUIRE( v.normal == expected.normal );
            REQUIRE( v.uv == expected.uv );
            REQUIRE( v.color == expected.color );
        }
        REQUIRE( cached->instances.size() == direct->instances.size() );
        for( size_t i = 0; i < cached->instances.size(); ++i ) {
            REQUIRE( cached->instances[i].offset == direct->instances[i].offset );
            REQUIRE( cached->instances[i].uv_rect == direct->instances[i].uv_rect );
            REQUIRE( cached->instances[i].tint == direct->instances[i].tint );
        }
        REQUIRE( cached->batches.size() == direct->batches.size() );
        for( size_t i = 0; i < cached->batches.size(); ++i ) {
            const auto &a = cached->batches[i];
            const auto &b = direct->batches[i];
            REQUIRE( a.first_index == b.first_index );
            REQUIRE( a.index_count == b.index_count );
            REQUIRE( a.first_instance == b.first_instance );
            REQUIRE( a.instance_count == b.instance_count );
        }
        REQUIRE( cached->reflective_batches.size() == direct->reflective_batches.size() );
        for( size_t i = 0; i < cached->reflective_batches.size(); ++i ) {
            REQUIRE( cached->reflective_batches[i].material == direct->reflective_batches[i].material );
        }
    };
    compare();
    REQUIRE( cache.plain_cells[0] );
    REQUIRE( cache.plain_cells[9] );
    REQUIRE( cache.plain_cells[11] );
    CHECK_FALSE( cache.plain_cells[5] );
    CHECK_FALSE( cache.plain_cells[6] );
    CHECK_FALSE( cache.plain_cells[7] );
    scene.weather.particles = fp3d_weather::precipitation::rain;
    scene.weather.intensity = .75f;
    view.yaw = -5.f;
    compare();
    scene.cells[1].terrain = "t_dirt";
    scene.cells[0].terrain = "t_grass";
    scene.cells[0].roof_connections = 4;
    scene.cells[3].floor_present = false;
    scene.cells[10].terrain = "t_open_air";
    scene.cells[10].floor_present = false;
    ++scene.geometry_generation;
    cache.invalidate_vehicle_roofs();
    resident.invalidate_vehicle_roofs();
    compare();
    scene.origin = tripoint_abs_ms( -1, -1, 0 );
    view.eye.x += 1.f;
    view.eye.y += 1.f;
    compare();
}
// Opt-in CPU fixture. Both arms reuse the same spatial and model mesh caches.
TEST_CASE( "fp3d_ordinary_terrain_recipe_cpu_profile", "[.][static_terrain_profile]" )
{
    using namespace fp3d_gpu_world_qa;
    using clock = std::chrono::steady_clock;
    fp3d_snapshot::scene_snapshot scene;
    scene.geometry_generation = 1;
    scene.sky.sun_y = .7f;
    scene.sky.sun_z = .7f;
    scene.sky.daylight = .8f;
    scene.cells.reserve( 56000 );
    for( int z = 0; z < 2; ++z ) {
        for( int y = -125; y < 0; ++y ) {
            for( int x = -112; x < 112; ++x ) {
                auto &tile = scene.cells.emplace_back();
                tile.position = tripoint_abs_ms( x, y, z );
                if( ( x + y ) % 3 == 0 ) {
                    tile.kind = kind::wall;
                    tile.terrain = "t_wall";
                } else if( x % 2 == 0 ) {
                    tile.terrain = "t_pavement";
                } else {
                    tile.terrain = "t_concrete_floor";
                    tile.ground_connections = ( x + 112 ) % 8;
                }
            }
        }
    }
    terrain_index cached;
    terrain_index uncached( false );
    terrain_index gpu_resident( true, true );
    fp3d_camera::pose view;
    view.eye.height = 12.f;
    view.pitch = 28.f;
    double cached_ms = 0;
    double uncached_ms = 0;
    double resident_ms = 0;
    unsigned prepared_tiles = 0;
    const auto sample = [&]( terrain_index &index, double &accumulated, bool warmup ) {
        const auto start = clock::now();
        const auto frame = prepare( scene, view, true, 2424, 1440, true, false, false, true,
                                    nullptr, nullptr, &index );
        const auto finish = clock::now();
        REQUIRE( frame );
        REQUIRE( frame->floor_count > 0 );
        REQUIRE( frame->wall_count > 0 );
        prepared_tiles = frame->floor_count + frame->wall_count;
        if( !warmup ) {
            accumulated += std::chrono::duration<double, std::milli>( finish - start ).count();
        }
    };
    for( const bool edited : { false, true } ) {
        cached_ms = 0;
        uncached_ms = 0;
        resident_ms = 0;
        constexpr int samples = 24;
        for( int i = -2; i < samples; ++i ) {
            view.eye.x = i * .25f;
            view.yaw = ( i % 7 ) - 3.f;
            if( edited ) {
                scene.cells[8000].ground_connections = i & 7;
                ++scene.geometry_generation;
            }
            // Rotate the three paths to reduce ordering and warm-cache bias.
            const std::array<terrain_index *, 3> paths{ &cached, &uncached, &gpu_resident };
            const std::array<double *, 3> totals{ &cached_ms, &uncached_ms, &resident_ms };
            for( int offset = 0; offset < 3; ++offset ) {
                const size_t which = ( i + 3 + offset ) % 3;
                sample( *paths[which], *totals[which], i < 0 );
            }
        }
        WARN( "terrain_recipe_cpu_profile edited=" << edited << ", samples=" << samples
              << ", snapshot_cells=" << scene.cells.size() << ", prepared_tiles=" << prepared_tiles
              << ", cached_ms=" << cached_ms / samples << ", uncached_ms=" << uncached_ms / samples
              << ", resident_ms=" << resident_ms / samples );
    }
}
TEST_CASE( "fp3d_packed_terrain_dirty_blocks", "[.][static_terrain_profile]" )
{
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    scene.geometry_generation = 1;
    scene.sky.sun_y = .7f;
    scene.sky.sun_z = .7f;
    scene.sky.daylight = .8f;
    for( int y = -112; y < 0; ++y ) {
        for( int x = -96; x < 96; ++x ) {
            auto &cell = scene.cells.emplace_back();
            cell.position = tripoint_abs_ms( x, y, 0 );
            cell.terrain = "t_pavement";
        }
    }

    terrain_index resident( true, true );
    fp3d_camera::pose view;
    view.eye.height = 12.f;
    view.pitch = 28.f;
    const auto prepare_frame = [&] {
        auto frame = prepare( scene, view, true, 2424, 1440, true, false, false, true,
                              nullptr, nullptr, &resident );
        REQUIRE( frame );
        REQUIRE( frame->resident_instances.size() > 1000 );
        return std::move( *frame );
    };
    const auto dirty_packed_bytes = []( const prepared_world &before, const prepared_world &after ) {
        const auto *old_bytes = reinterpret_cast<const unsigned char *>(
                                    before.resident_instances.data() );
        const auto *new_bytes = reinterpret_cast<const unsigned char *>(
                                    after.resident_instances.data() );
        const size_t element_size = sizeof( fp3d_gpu::mesh_instance );
        const size_t old_size = before.resident_instances.size() * element_size;
        const size_t new_size = after.resident_instances.size() * element_size;
        const size_t common = std::min( old_size, new_size );
        size_t dirty = new_size - common;
        for( size_t offset = 0; offset < common; offset += 4096 ) {
            const size_t length = std::min<size_t>( 4096, common - offset );
            if( std::memcmp( old_bytes + offset, new_bytes + offset, length ) != 0 ) {
                dirty += length;
            }
        }
        return dirty;
    };

    const auto baseline = prepare_frame();
    CHECK( dirty_packed_bytes( baseline, prepare_frame() ) == 0 );
    // Moving the scene origin alone must preserve encoded terrain at the same world camera.
    scene.origin = tripoint_abs_ms( 1, 0, 0 );
    view.eye.x = -1.f;
    const auto recentered = prepare_frame();
    CHECK( dirty_packed_bytes( baseline, recentered ) == 0 );
    REQUIRE( baseline.resident_instances.size() == recentered.resident_instances.size() );

    // One legitimately changed visible cell edits a single packed instance.
    auto &cell = scene.cells[scene.cells.size() / 2 + 96];
    cell.known = fp3d_snapshot::knowledge::remembered;
    ++scene.geometry_generation;
    const auto visibility = prepare_frame();
    const size_t visibility_dirty = dirty_packed_bytes( recentered, visibility );
    CHECK( visibility_dirty > 0 );
    CHECK( visibility_dirty <= 8192 );

    // Removing that cell shifts all later entries in its tightly packed group.
    cell.floor_present = false;
    ++scene.geometry_generation;
    const auto removed = prepare_frame();
    REQUIRE( removed.resident_instances.size() + 1 == visibility.resident_instances.size() );
    const size_t removed_dirty = dirty_packed_bytes( visibility, removed );
    CHECK( removed_dirty > visibility_dirty );

    view.eye.x += 1.f;
    const auto moved = prepare_frame();
    WARN( "packed_terrain_dirty_blocks packed_bytes=" <<
          baseline.resident_instances.size() * sizeof( fp3d_gpu::mesh_instance ) <<
          ", visibility_dirty=" << visibility_dirty << ", removed_dirty=" << removed_dirty <<
          ", moved_dirty=" << dirty_packed_bytes( removed, moved ) );

    terrain_index split( true, true, true );
    scene.weather.particles = fp3d_weather::precipitation::rain;
    scene.weather.intensity = .75f;
    cell.floor_present = true;
    ++scene.geometry_generation;
    const auto gather = [&]( terrain_index &index ) {
        auto frame = prepare( scene, view, true, 2424, 1440, true, false, false, true,
                              nullptr, nullptr, &index );
        REQUIRE( frame );
        return std::move( *frame );
    };
    const auto instances = []( const prepared_world &frame, const auto &batches ) {
        std::vector<std::array<float, 12>> values;
        for( const auto &draw : batches ) {
            REQUIRE( draw.resident );
            const auto &stream = draw.paged_instances ? *frame.resident_pages : frame.resident_instances;
            const size_t prefix = !draw.paged_instances && frame.resident_pages ?
                                  frame.resident_pages->size() : 0;
            REQUIRE( draw.first_instance >= prefix );
            const size_t first = draw.first_instance - prefix;
            REQUIRE( first + draw.instance_count <= stream.size() );
            for( size_t i = first; i < first + draw.instance_count; ++i ) {
                const auto &item = stream[i];
                auto &value = values.emplace_back();
                std::copy( item.offset.begin(), item.offset.end(), value.begin() );
                std::copy( item.uv_rect.begin(), item.uv_rect.end(), value.begin() + 4 );
                std::copy( item.tint.begin(), item.tint.end(), value.begin() + 8 );
            }
        }
        std::sort( values.begin(), values.end() );
        return values;
    };
    const auto wet_packed = gather( resident );
    const auto wet_split = gather( split );
    REQUIRE( wet_split.resident_pages );
    REQUIRE_FALSE( wet_split.resident_instances.empty() );
    CHECK( wet_split.batches.size() == wet_packed.batches.size() + terrain_index::split_page_budget );
    CHECK( instances( wet_split, wet_split.batches ) == instances( wet_packed, wet_packed.batches ) );
    REQUIRE_FALSE( wet_packed.reflective_batches.empty() );
    size_t reflected = 0;
    for( const auto &reflection : wet_split.reflective_batches ) {
        CHECK( reflection.material == wet_packed.reflective_batches.front().material );
        reflected += reflection.draws.instance_count;
    }
    CHECK( reflected == wet_packed.reflective_batches.front().draws.instance_count );
    const auto repeated = gather( split );
    CHECK( repeated.resident_page_revision == wet_split.resident_page_revision );
    const auto selected = std::find_if( wet_split.batches.begin(), wet_split.batches.end(),
    []( const batch &draw ) {
        return draw.paged_instances && draw.instance_count == terrain_index::instance_pages::page_size;
    } );
    REQUIRE( selected != wet_split.batches.end() );
    const auto &offset = ( *wet_split.resident_pages )[selected->first_instance].offset;
    const auto coordinate = scene.origin + tripoint_rel_ms(
                                static_cast<int>( offset[0] + wet_split.resident_to_scene[0] ),
                                static_cast<int>( offset[2] + wet_split.resident_to_scene[2] ), 0 );
    const auto edited_cell = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto &entry ) {
        return entry.position == coordinate;
    } );
    REQUIRE( edited_cell != scene.cells.end() );
    edited_cell->floor_present = false;
    ++scene.geometry_generation;
    const auto edited_packed = gather( resident );
    const auto edited_split = gather( split );
    CHECK( instances( edited_split, edited_split.batches ) == instances( edited_packed,
            edited_packed.batches ) );
    REQUIRE( edited_split.resident_pages );
    REQUIRE( edited_split.resident_pages->size() == wet_split.resident_pages->size() );
    constexpr size_t page_size = terrain_index::instance_pages::page_size;
    constexpr size_t page_bytes = sizeof( fp3d_gpu::mesh_instance ) * page_size;
    size_t dirty_pages = 0;
    for( size_t i = 0; i < wet_split.resident_pages->size(); i += page_size ) {
        if( std::memcmp( wet_split.resident_pages->data() + i,
                         edited_split.resident_pages->data() + i, page_bytes ) != 0 ) {
            ++dirty_pages;
        }
    }
    CHECK( dirty_pages == 1 );
    CHECK( dirty_packed_bytes( wet_split, edited_split ) == 0 );
    const size_t packed_edit = dirty_packed_bytes( wet_packed, edited_packed );
    CHECK( packed_edit > page_bytes );
    WARN( "terrain_page_split packed_edit=" << packed_edit << ", split_edit=" <<
          dirty_pages * page_bytes << ", extra_batches=" <<
          wet_split.batches.size() - wet_packed.batches.size() );
}

TEST_CASE( "fp3d_static_instance_upload_positions_survive_movement_within_anchor",
           "[fp3d][resident_instance_anchor]" )
{
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    terrain_index storage( true, true );
    scene.geometry_generation = 1;
    scene.origin = tripoint_abs_ms( -64, 64, 0 );
    auto &cell = scene.cells.emplace_back();
    cell.position = tripoint_abs_ms( -63, 58, 0 );
    cell.terrain = "t_pavement";
    fp3d_camera::pose view;
    view.eye.height = 3.f;
    view.pitch = 25.f;
    const auto prepare_static = [&]() {
        return prepare( scene, view, false, 512, 512, true, false, false, false,
                        nullptr, nullptr, &storage );
    };
    const auto first = prepare_static();
    REQUIRE( first );
    REQUIRE( first->batches.size() == 1 );
    REQUIRE( first->batches.front().resident );
    REQUIRE( first->resident_instance_count == 1 );
    REQUIRE( first->resident_pages );
    CHECK( first->resident_pages->size() == 64 );
    CHECK( first->resident_instances.empty() );
    REQUIRE( first->batches.front().paged_instances );
    const auto first_slot = first->batches.front().first_instance;
    REQUIRE( first->instances.empty() );
    const auto compute = prepare_terrain_compute( *first );
    REQUIRE( compute.draw_indices == std::vector<Uint32>{ 0 } );
    REQUIRE( compute.cells.size() == 1 );
    REQUIRE( compute.models.size() == 1 );
    REQUIRE( compute.blocks.size() == 1 );
    const auto &cached = ( *first->resident_pages )[first_slot];
    CHECK( compute.cells.front().position == std::array<float, 3>{ cached.offset[0], cached.offset[1],
                                                                   cached.offset[2] } );
    CHECK( compute.cells.front().tint == cached.tint );
    CHECK( compute.models.front().uv_rect == cached.uv_rect );
    CHECK( compute.blocks.front().revision == first->material_revision );
    auto translated = *first;
    translated.camera.elements = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const auto translated_planes = terrain_compute_planes( translated );
    CHECK( translated_planes[0][3] == 1.f + translated.resident_to_scene[0] );
    auto invalid = *first;
    invalid.batches.front().instance_count = 2;
    const auto altered = std::make_shared<std::vector<fp3d_gpu::mesh_instance>>( *first->resident_pages );
    ( *altered )[first_slot + 1] = cached;
    ( *altered )[first_slot + 1].uv_rect[0] += .125f;
    invalid.resident_pages = altered;
    CHECK( prepare_terrain_compute( invalid ).blocks.empty() );
    terrain_compute_input_cache compute_cache;
    CHECK_FALSE( compute_cache.reuse_or_rebuild( *first ) );
    CHECK( compute_cache.reuse_or_rebuild( translated ) );
    const auto *cells_data = compute_cache.inputs().cells.data();
    scene.origin = tripoint_abs_ms( -62, 65, 0 );
    view.eye.x -= 2.f;
    view.eye.y -= 1.f;
    const auto second = prepare_static();
    REQUIRE( second );
    REQUIRE( second->resident_instance_count == 1 );
    REQUIRE( second->batches.front().first_instance == first_slot );
    CHECK( first->resident_pages == second->resident_pages );
    REQUIRE( first->resident_page_revision );
    CHECK( first->resident_page_revision == second->resident_page_revision );
    CHECK( compute_cache.reuse_or_rebuild( *second ) );
    CHECK( compute_cache.inputs().cells.data() == cells_data );
    CHECK( first->resident_geometry == second->resident_geometry );
    CHECK( second->resident_to_scene[0] == first->resident_to_scene[0] - 2.f );
    CHECK( second->resident_to_scene[2] == first->resident_to_scene[2] - 1.f );

    scene.origin = tripoint_abs_ms( 0, 65, 0 );
    view.eye.x -= 62.f;
    const auto crossed = prepare_static();
    REQUIRE( crossed );
    REQUIRE( crossed->resident_instance_count == 1 );
    REQUIRE( crossed->batches.front().first_instance == first_slot );
    CHECK( crossed->resident_to_scene[0] == 0.f );
    REQUIRE( crossed->resident_pages );
    CHECK( crossed->resident_pages != first->resident_pages );
    CHECK( crossed->resident_page_revision != first->resident_page_revision );
    CHECK_FALSE( compute_cache.reuse_or_rebuild( *crossed ) );
    REQUIRE( compute_cache.inputs().cells.size() == 1 );
    CHECK( compute_cache.inputs().cells.front().position[0] == -63.f );
    CHECK( ( *crossed->resident_pages )[first_slot].offset[0] == -63.f );
    CHECK( ( *first->resident_pages )[first_slot].offset[0] == 65.f );
    CHECK( crossed->resident_geometry == first->resident_geometry );

    auto rerouted = *crossed;
    rerouted.batches.front().paged_instances = false;
    CHECK_FALSE( compute_cache.reuse_or_rebuild( rerouted ) );
    CHECK( compute_cache.inputs().blocks.empty() );
    CHECK( compute_cache.reuse_or_rebuild( rerouted ) );
    ++rerouted.material_revision;
    CHECK_FALSE( compute_cache.reuse_or_rebuild( rerouted ) );
}
TEST_CASE( "fp3d_resident_mesh_palette_keeps_offsets_and_caps_storage", "[fp3d][resident_model_cache]" )
{
    fp3d_geometry::resident_model_meshes<int> palette;
    const auto triangle = []( fp3d_geometry::mesh &mesh ) {
        mesh.vertices.resize( 3 );
        mesh.indices = { 0, 1, 2 };
    };
    const auto first = palette.retain( 0, triangle );
    REQUIRE( first );
    const auto original = palette.geometry();
    REQUIRE( palette.retain( 0, triangle )->first == first->first );
    REQUIRE( original == palette.geometry() );
    for( int i = 1; i < 512; ++i ) {
        const auto next = palette.retain( i, triangle );
        REQUIRE( next );
        REQUIRE( next->first == static_cast<unsigned>( i * 3 ) );
    }
    CHECK_FALSE( palette.retain( 512, triangle ) );
    REQUIRE( palette.geometry()->indices.size() == 512 * 3 );
    REQUIRE( original->indices.size() == 3 );
    palette.set_atlas_layout( 1, 4 );
    CHECK_FALSE( palette.geometry() );
    auto too_big = []( fp3d_geometry::mesh &mesh ) {
        mesh.vertices.resize( ( 4 * 1024 * 1024 ) / sizeof( fp3d_geometry::vertex ) + 1 );
        mesh.indices = { 0, 1, 2 };
    };
    CHECK_FALSE( palette.retain( 0, too_big ) );
    CHECK_FALSE( palette.geometry() );
    CHECK( palette.retain( 0, triangle )->first == 0 );
}
TEST_CASE( "fp3d_block_instance_pages_retain_offsets_and_evict_inactive_blocks",
           "[fp3d][resident_instance_pages]" )
{
    using pages_type = fp3d_gpu::instance_pages<int, int>;
    pages_type pages;
    const auto encode = []( int x ) { return x + 64; };
    pages.begin_frame();
    const auto first = pages.write( 1, { 3, 5 }, encode );
    const auto neighbour = pages.write( 2, { 7 }, encode );
    REQUIRE( first == 0 );
    REQUIRE( neighbour == pages_type::page_size );
    REQUIRE( pages.data()[*first + 1] == 69 );
    REQUIRE( pages.data()[*neighbour] == 71 );
    const auto revision = pages.snapshot_revision();
    const auto *storage = pages.data().data();
    CHECK( pages.write( 1, { 3, 4 }, encode ) == first );
    CHECK( pages.data().data() == storage );
    CHECK( pages.snapshot_revision() != revision );
    CHECK( pages.write( 1, { 3, 5 }, encode ) == first );
    const auto previous = pages.snapshot();
    const auto previous_revision = pages.snapshot_revision();

    pages.begin_frame();
    CHECK( pages.write( 2, { 7 }, encode ) == neighbour );
    CHECK( pages.snapshot() == previous );
    CHECK( pages.snapshot_revision() == previous_revision );
    CHECK( pages.write( 1, { 3, 6 }, encode ) == first );
    CHECK( pages.data()[*first + 1] == 70 );
    CHECK( pages.snapshot_revision() != previous_revision );
    CHECK( ( *previous )[*first + 1] == 69 );
    CHECK( pages.data()[*neighbour] == 71 );
    for( int frame = 0; frame < 33; ++frame ) {
        pages.begin_frame();
        REQUIRE( pages.write( 2, { 7 }, encode ) == neighbour );
    }
    CHECK( pages.write( 3, { 13 }, encode ) == first );
    CHECK( pages.data()[*first] == 77 );
    CHECK( pages.data()[*neighbour] == 71 );
    for( size_t i = 2; i < pages_type::page_count; ++i ) {
        REQUIRE( pages.write( static_cast<int>( i + 100 ), { 9 }, encode ) );
    }
    CHECK( pages.data().size() == pages_type::max_extent );
    CHECK_FALSE( pages.write( 10000, { 9 }, encode ) );
    CHECK_FALSE( pages.write( 2, std::vector<int>( pages_type::page_size + 1 ), encode ) );
    CHECK( pages.write( 2, { 7 }, encode ) == neighbour );
    pages.clear();
    CHECK( pages.data().empty() );
    CHECK( pages.write( 2, { 7 }, encode ) == first );
}

TEST_CASE( "fp3d_open_structure_reuses_resident_mesh_with_sorted_alpha_and_shadows",
           "[fp3d][overhead][open_structure]" )
{
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 0, 0, 0 );
    scene.geometry_generation = 1;
    for( int z = 0; z <= 2; ++z ) {
        auto &tile = scene.cells.emplace_back();
        tile.position = tripoint_abs_ms( 0, 0, z );
        tile.terrain = z ? "t_flat_roof" : "t_pavement";
        tile.open_structure_above = z > 0;
    }
    terrain_index cache( true, true, true );
    const auto view = fp3d_camera::orbit_pose( {}, 0, 0 );
    const auto frame = [&]( float opacity ) {
        return prepare( scene, view, true, 512, 512, true, false, false, true, nullptr,
                        nullptr, &cache, true, nullptr, nullptr, opacity );
    };
    const auto far = frame( 1.f );
    const auto middle = frame( .55f );
    const auto near = frame( .1f );
    REQUIRE( far );
    REQUIRE( middle );
    REQUIRE( near );
    REQUIRE( far->resident_geometry );
    CHECK( far->resident_geometry == middle->resident_geometry );
    CHECK( far->resident_geometry == near->resident_geometry );
    CHECK( far->opaque_batches > near->opaque_batches );
    CHECK( far->open_structure_shadows.empty() );
    REQUIRE( near->badge_end + 2 == near->blend_end );
    const auto alpha = []( const prepared_world &world, const batch &draw ) {
        REQUIRE( draw.resident );
        const size_t prefix = world.resident_pages ? world.resident_pages->size() : 0;
        return world.resident_instances.at( draw.first_instance - prefix ).tint[3];
    };
    CHECK( alpha( *middle, middle->batches.at( middle->badge_end ) ) == Approx( .55f ) );
    CHECK( alpha( *near, near->batches.at( near->badge_end ) ) == Approx( .1f ) );
    CHECK( alpha( *near, near->batches.at( near->badge_end + 1 ) ) == Approx( .1f ) );
    CHECK( near->open_structure_shadows.size() == 1 );
    CHECK( near->batches.at( near->badge_end ).first_instance <
           near->batches.at( near->badge_end + 1 ).first_instance );
}

TEST_CASE( "fp3d_pointer_cutaway_lowers_only_near_walls_in_cached_and_direct_geometry",
           "[fp3d][cutaway][static_floor_cache]" )
{
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 0, 0, 4 );
    scene.geometry_generation = 1;
    scene.pointer_room.emplace();
    scene.pointer_room->anchor = scene.origin;
    scene.pointer_room->interior.insert( scene.origin );
    for( const int x : { -1, 1 } ) {
        auto &tile = scene.cells.emplace_back();
        tile.position = tripoint_abs_ms( x, 0, 4 );
        tile.terrain = "t_wall";
        tile.kind = kind::wall;
        tile.over_avatar_region = true;
        scene.pointer_room->columns.insert( tile.position );
    }
    terrain_index cache;
    const auto wall_height = [&]( const prepared_world &frame, int x ) {
        float height = 0;
        for( const auto &draw : frame.batches ) {
            for( unsigned n = 0; n < draw.instance_count; ++n ) {
                const auto &instance = frame.instances[draw.first_instance + n];
                if( instance.offset[0] != x ) { continue; }
                for( unsigned i = 0; i < draw.index_count; ++i ) {
                    const auto vertex = frame.geometry.indices[draw.first_index + i];
                    height = std::max( height, frame.geometry.vertices[vertex].position[1] +
                                       instance.offset[1] );
                }
            }
        }
        return height;
    };
    for( const int camera_side : { -1, 1 } ) {
        const auto view = fp3d_camera::orbit_pose(
                          { camera_side < 0 ? 90.f : 270.f, 60.f, 18.f }, 0, 0 );
        for( const bool cached : { false, true } ) {
            const auto frame = prepare( scene, view, true, 800, 600, true,
                                        false, false, false, nullptr, nullptr, cached ? &cache : nullptr );
            REQUIRE( frame );
            CHECK( wall_height( *frame, camera_side ) == Approx( .45f ) );
            CHECK( wall_height( *frame, -camera_side ) == Approx( fp3d_camera::floor_height ) );
        }
    }
    scene.pointer_room.reset();
    const auto view = fp3d_camera::orbit_pose( {}, 0, 0 );
    const auto restored = prepare( scene, view, true, 800, 600, true );
    REQUIRE( restored );
    CHECK( wall_height( *restored, -1 ) == Approx( fp3d_camera::floor_height ) );
    CHECK( wall_height( *restored, 1 ) == Approx( fp3d_camera::floor_height ) );
}

TEST_CASE( "fp3d_live_cutaway_masks_restore_cached_roofs_and_keep_high_storeys",
           "[fp3d][cutaway][static_floor_cache]" )
{
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 0, 0, 4 );
    scene.camera_geometry = scene.camera_cutaway = true;
    scene.geometry_generation = 1;
    for( const int x : { 0, 4 } ) {
        for( const int z : { 0, 4, 5 } ) {
            auto &tile = scene.cells.emplace_back();
            tile.position = tripoint_abs_ms( x, 0, z );
            tile.terrain = "t_floor";
            tile.floor_present = true;
            tile.over_avatar_region = x == 0 && z >= 4;
        }
    }
    auto &facade = scene.cells.emplace_back();
    facade.position = tripoint_abs_ms( 8, 0, 7 );
    facade.terrain = "t_floor";
    facade.floor_present = true;
    const auto view = fp3d_camera::orbit_pose( { 0, 60, 25 }, 2, 0 );
    terrain_index cache;
    const auto take = [&]() {
        return prepare( scene, view, true, 800, 600, true,
                        false, false, false, nullptr, nullptr, &cache );
    };
    const auto contains = [&]( const prepared_world &frame, int x, int z ) {
        return std::any_of( frame.instances.begin(), frame.instances.end(), [&]( const auto &instance ) {
            return instance.offset[0] == x && instance.offset[2] == 0 &&
                   instance.offset[1] == ( z - scene.origin.z() ) * fp3d_camera::floor_height;
        } );
    };
    const auto normal = take();
    REQUIRE( normal );
    CHECK_FALSE( contains( *normal, 0, 5 ) );
    CHECK( contains( *normal, 4, 5 ) );
    const auto *chunks = cache.chunks.data();
    scene.pointer_room.emplace();
    scene.pointer_room->anchor = tripoint_abs_ms( 4, 0, 4 );
    scene.pointer_room->columns.insert( scene.pointer_room->anchor );
    const auto selected = take();
    REQUIRE( selected );
    CHECK( contains( *selected, 0, 5 ) );
    CHECK_FALSE( contains( *selected, 4, 5 ) );
    CHECK( contains( *selected, 0, 4 ) );
    CHECK( contains( *selected, 4, 4 ) );
    CHECK( contains( *selected, 0, 0 ) );
    CHECK( contains( *selected, 4, 0 ) );
    CHECK( contains( *selected, 8, 7 ) );
    CHECK( cache.chunks.data() == chunks );
    // The same mask must also remove the roof from the picking ray.
    const auto floor_hit = fp3d_targeting::ray( scene, 0, 90, { 4, 0, 10 }, 30 );
    REQUIRE( floor_hit );
    CHECK( floor_hit->position == tripoint_abs_ms( 4, 0, 4 ) );
    scene.pointer_room.reset();
    const auto restored = take();
    REQUIRE( restored );
    CHECK_FALSE( contains( *restored, 0, 5 ) );
    CHECK( contains( *restored, 4, 5 ) );
    CHECK( cache.chunks.data() == chunks );
    CHECK( scene.cells.size() == 7 );
    const auto roof_hit = fp3d_targeting::ray( scene, 0, 90, { 4, 0, 10 }, 30 );
    REQUIRE( roof_hit );
    CHECK( roof_hit->position == tripoint_abs_ms( 4, 0, 5 ) );
}
#endif
