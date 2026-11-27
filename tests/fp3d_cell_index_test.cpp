#include "fp3d_cell_index.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "calendar.h"
#include "cata_catch.h"
#include "fp3d_gpu_occlusion.h"
#include "fp3d_gpu_presentation.h"
#include "fp3d_gpu_terrain_compute.h"
#include "fp3d_gpu_world_qa.h"
#include "fp3d_iris_layout.h"
#include "fp3d_iris_gpu_program.h"
#include "fp3d_iris_frame.h"
#include "fp3d_iris_targets.h"
#include "fp3d_iris_pack.h"
#include "fp3d_shaderpacks.h"
#include "fp3d_targeting.h"
#include "options_helpers.h"

#ifdef _WIN32
TEST_CASE( "fp3d_iris_resolves_pack_custom_uniform_dependencies", "[iris_custom_uniforms][nogame]" )
{
    using namespace fp3d_iris;
    pack_text pack;
    pack.files["shaders.properties"] =
        "uniform.vec2.pixelSize = vec2(reciprocal, 1 / viewHeight)\n"
        "variable.float.reciprocal = gain / viewWidth\n"
        "variable.float.gain = 2 + 3 * 2\n"
        "uniform.bool.mapped = if(pixelSize.x >= 0.125, !false, false)\n"
        "uniform.int.count = intInput + 1\n"
        "uniform.float.broken = missingInput\n"
        "uniform.float.cycle = cycle + 1\n";
    const auto properties = inspect_properties( pack );
    REQUIRE( properties.error.empty() );
    uniform_values values{ { "viewWidth", std::vector<float>{ 64.f } },
        { "viewHeight", std::vector<float>{ 32.f } }, { "intInput", std::vector<std::int32_t>{ 3 } } };
    REQUIRE( apply_custom_uniforms( properties, { "mapped", "pixelSize", "count" }, values ).empty() );
    CHECK( std::get<std::vector<float>>( values.at( "pixelSize" ) ) == std::vector<float>{ .125f, .03125f } );
    CHECK( std::get<std::vector<std::uint32_t>>( values.at( "mapped" ) ) == std::vector<std::uint32_t>{ 1 } );
    CHECK( std::get<std::vector<std::int32_t>>( values.at( "count" ) ) == std::vector<std::int32_t>{ 4 } );
    CHECK( values.count( "reciprocal" ) == 0 );
    CHECK( apply_custom_uniforms( properties, { "broken" }, values ).find( "missing input missingInput" ) !=
           std::string::npos );
    CHECK( apply_custom_uniforms( properties, { "cycle" }, values ).find( "cyclic" ) != std::string::npos );
}

TEST_CASE( "fp3d_iris_custom_uniform_smoothing_uses_frame_time", "[iris_custom_uniforms][nogame]" )
{
    using namespace fp3d_iris;
    pack_text pack;
    pack.files["shaders.properties"] =
        "uniform.float.transition = smooth(rainStrength, 2, 4)\n"
        "uniform.float.snapped = smooth(12, rainStrength, 0, 0)\n"
        "uniform.float.zinvalid = unknownInput\n";
    const auto properties = inspect_properties( pack );
    REQUIRE( properties.error.empty() );
    uniform_smoothing history;
    const auto step = [&]( float rain, double elapsed ) {
        uniform_values values{ { "rainStrength", std::vector<float>{ rain } } };
        REQUIRE( apply_custom_uniforms( properties, { "transition", "snapped" }, values, &history,
                                        elapsed ).empty() );
        CHECK( std::get<std::vector<float>>( values.at( "snapped" ) ).front() == rain );
        return std::get<std::vector<float>>( values.at( "transition" ) ).front();
    };
    CHECK( step( 0.f, 0 ) == 0.f );
    CHECK( step( 1.f, 0.2 ) == Approx( .5f ) );
    CHECK( step( 0.f, 0.4 ) == Approx( .25f ) );
    const auto before_error = history;
    uniform_values values{ { "rainStrength", std::vector<float>{ 1.f } } };
    CHECK_FALSE( apply_custom_uniforms( properties, { "transition", "zinvalid" }, values, &history,
                                       0.2 ).empty() );
    CHECK( history == before_error );
    history.clear();
    CHECK( step( 1.f, 0 ) == 1.f );
}

TEST_CASE( "fp3d_iris_maps_scene_biome_to_pack_constants", "[iris_custom_uniforms][nogame]" )
{
    using namespace fp3d_iris;
    using climate = fp3d_snapshot::landscape_climate;
    const auto dry = map_scene_biome( { climate::plains, 11.f, 20.f } );
    CHECK( dry.id == biome_constant( "BIOME_DESERT" ).value() );
    CHECK( dry.category == biome_constant( "CAT_DESERT" ).value() );
    CHECK( dry.precipitation == biome_constant( "PPT_NONE" ).value() );
    CHECK( dry.rainfall == Approx( .20f ) );
    const auto river = map_scene_biome( { climate::river, 11.f, 20.f } );
    CHECK( river.category == biome_constant( "CAT_RIVER" ).value() );
    const auto taiga = map_scene_biome( { climate::forest, -5.f, 70.f } );
    CHECK( taiga.id == biome_constant( "BIOME_TAIGA" ).value() );
    CHECK( taiga.precipitation == biome_constant( "PPT_SNOW" ).value() );
    CHECK( map_scene_biome( { climate::underground, 15.f, 80.f } ).category ==
           biome_constant( "CAT_UNDERGROUND" ).value() );
    pack_text pack;
    pack.files["shaders.properties"] =
        "uniform.float.arid = smooth(if(biome_category == CAT_DESERT, 1.0, 0.0), 30, 30)\n";
    fp3d_snapshot::scene_snapshot scene;
    scene.biome = { climate::plains, 11.f, 20.f };
    const auto frame = frame_history{}.prepare( scene, {}, {}, 64, 64, false, {}, 1 );
    REQUIRE( frame );
    auto values = native_frame_uniform_values( *frame );
    uniform_smoothing smoothing;
    REQUIRE( apply_custom_uniforms( inspect_properties( pack ), { "arid" }, values, &smoothing ).empty() );
    CHECK( std::get<std::vector<float>>( values.at( "arid" ) ).front() == 1.f );
}

TEST_CASE( "fp3d_iris_eye_brightness_samples_avatar_storey", "[iris_custom_uniforms][nogame]" )
{
    using namespace fp3d_iris;
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 12, -2, -1 );
    const auto upstairs = scene.origin + tripoint_rel_ms::above;
    const block_light_levels block{ { scene.origin, 10 }, { upstairs, 3 } };
    const block_light_levels sky{ { upstairs, 18 } };
    CHECK( eye_brightness_at( scene.origin, block, sky ) == std::array<int, 2>{ 160, 0 } );
    CHECK( eye_brightness_at( upstairs, block, sky ) == std::array<int, 2>{ 48, 240 } );
    const auto frame = frame_history{}.prepare( scene, {}, {}, 64, 64, true, {}, 1,
                       eye_brightness_at( scene.origin, block, sky ) );
    REQUIRE( frame );
    auto values = native_frame_uniform_values( *frame );
    CHECK( std::get<std::vector<std::int32_t>>( values.at( "eyeBrightness" ) ) ==
           std::vector<std::int32_t>{ 160, 0 } );
    pack_text pack;
    pack.files["shaders.properties"] =
        "uniform.vec2.lightTest = vec2(eyeBrightness.x / 240.0, eyeBrightness.y / 240.0)\n";
    const auto properties = inspect_properties( pack );
    INFO( properties.error );
    REQUIRE( properties.uniform_expressions.count( "lightTest" ) == 1 );
    const auto error = apply_custom_uniforms( properties, { "lightTest" }, values );
    INFO( error );
    REQUIRE( error.empty() );
    REQUIRE( values.count( "lightTest" ) == 1 );
    const auto &light = std::get<std::vector<float>>( values.at( "lightTest" ) );
    CHECK( light[0] == Approx( 2.f / 3.f ) );
    CHECK( light[1] == 0.f );
}

TEST_CASE( "fp3d_iris_celestial_uniforms_follow_native_calendar", "[iris_custom_uniforms][nogame]" )
{
    using namespace fp3d_iris;
    const auto day = calendar::turn_zero + 3_days + 12_hours;
    const auto dawn = sunrise( day );
    const auto dusk = sunset( day );
    const auto next_dawn = sunrise( day + 1_days );
    REQUIRE( dawn < dusk );
    REQUIRE( dusk < next_dawn );
    CHECK( read_world_clock( dawn ).sun_angle == 0.f );
    CHECK( read_world_clock( dawn + ( dusk - dawn ) / 2 ).sun_angle == Approx( .25f ).margin( .001f ) );
    CHECK( read_world_clock( dusk ).sun_angle == .5f );
    const auto night = dusk + ( next_dawn - dusk ) / 2;
    const auto clock = read_world_clock( night );
    CHECK( clock.sun_angle == Approx( .75f ).margin( .001f ) );
    CHECK( clock.moon_phase == ( static_cast<int>( get_moon_phase( night ) ) + 4 ) % MOON_PHASE_MAX );
    fp3d_snapshot::scene_snapshot scene;
    scene.sky.sun_y = -1.f;
    const fp3d_gpu_camera_math::matrix4 clip{ { 1, 0, 0, 0, 0, 1, 0, 0,
                                               0, 0, 1, 0, 0, 0, 0, 1 } };
    const auto frame = frame_history{}.prepare( scene, {}, clip, 64, 64, false, clock, 1 );
    REQUIRE( frame );
    const auto values = fp3d_gpu_world_qa::iris_scene_uniforms( *frame, fp3d_camera::near_clip,
                        fp3d_camera::far_clip );
    CHECK( std::get<std::vector<float>>( values.at( "shadowAngle" ) )[0] ==
           Approx( .25f ).margin( .001f ) );
    REQUIRE( values.count( "upPosition" ) == 1 );
    const auto &up = std::get<std::vector<float>>( values.at( "upPosition" ) );
    CHECK( std::hypot( up[0], up[1], up[2] ) == Approx( 100.f ) );
}

TEST_CASE( "fp3d_iris_atmosphere_uniforms_follow_native_weather", "[iris_custom_uniforms][nogame]" )
{
    using namespace fp3d_iris;
    fp3d_snapshot::scene_snapshot scene;
    scene.sky.daylight = 1.f;
    scene.weather.fog_end = 60.f;
    const auto clear = frame_history{}.prepare( scene, {}, {}, 64, 64, false, {}, 1 );
    const auto flashed = frame_history{}.prepare( scene, {}, {}, 64, 64, false, {}, 1, {}, 1.f );
    REQUIRE( clear );
    REQUIRE( flashed );
    CHECK( clear->sky_color[0] < clear->sky_color[2] );
    CHECK( flashed->sky_color[0] > clear->sky_color[0] );
    CHECK( flashed->sky_color[0] == flashed->sky_color[1] );
    CHECK( flashed->sky_color[1] == flashed->sky_color[2] );
    CHECK( flashed->fog_distance == 60.f );
    CHECK( flashed->fog_start == 8.f );
    CHECK( flashed->fog_color[0] > clear->fog_color[0] );
    pack_text pack;
    pack.files["shaders.properties"] =
        "uniform.float.flashTest = if(equals(skyColor.r, skyColor.g, 0.01) && skyColor.r > 0.3, 1.0, 0.0)\n";
    const auto properties = inspect_properties( pack );
    auto values = native_frame_uniform_values( *flashed );
    REQUIRE( apply_custom_uniforms( properties, { "flashTest" }, values ).empty() );
    CHECK( std::get<std::vector<float>>( values.at( "flashTest" ) )[0] == 1.f );
}

TEST_CASE( "fp3d_iris_player_uniforms_preserve_vision_and_held_light", "[iris_custom_uniforms][nogame]" )
{
    using namespace fp3d_iris;
    fp3d_snapshot::scene_snapshot scene;
    scene.player_darkness = true;
    scene.player_night_vision = .45f;
    scene.held_light = 30;
    const auto frame = frame_history{}.prepare( scene, {}, {}, 64, 64, false, {}, 1 );
    REQUIRE( frame );
    const auto values = native_frame_uniform_values( *frame );
    CHECK( std::get<std::vector<float>>( values.at( "blindness" ) )[0] == 0.f );
    CHECK( std::get<std::vector<float>>( values.at( "darknessFactor" ) )[0] == 1.f );
    CHECK( std::get<std::vector<float>>( values.at( "nightVision" ) )[0] == Approx( .45f ) );
    CHECK( std::get<std::vector<std::int32_t>>( values.at( "heldBlockLightValue" ) )[0] == 15 );
    CHECK( std::get<std::vector<std::int32_t>>( values.at( "heldBlockLightValue2" ) )[0] == 0 );
    scene.player_blind = true;
    const auto blind = frame_history{}.prepare( scene, {}, {}, 64, 64, false, {}, 1 );
    REQUIRE( blind );
    const auto blind_values = native_frame_uniform_values( *blind );
    CHECK( std::get<std::vector<float>>( blind_values.at( "blindness" ) )[0] == 1.f );
    CHECK( std::get<std::vector<float>>( blind_values.at( "nightVision" ) )[0] == 0.f );
}

TEST_CASE( "fp3d_iris_reports_real_pack_compile_coverage", "[.][iris_pack_coverage]" )
{
    using namespace fp3d_shaderpacks;
    const char *pick = std::getenv( "CATA_IRIS_TEST_PACK_NAME" );
    const char *folder = std::getenv( "CATA_IRIS_TEST_PACK_ROOT" );
    REQUIRE( pick );
    REQUIRE( folder );
    REQUIRE( std::filesystem::u8path( directory() ) == std::filesystem::u8path( folder ) );
    struct compiler_scope {
        ~compiler_scope() {
            stop_iris_compilation();
        }
    } compiler;
    REQUIRE( choose( pick ) );
    REQUIRE( poll_iris_compilation().running );
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes( 4 );
    while( poll_iris_compilation().running && std::chrono::steady_clock::now() < deadline ) {
        SDL_Delay( 20 );
    }
    const auto &compiled = poll_iris_compilation();
    REQUIRE_FALSE( compiled.running );
    const auto diagnostic_count = compiled.diagnostics.size();
    WARN( "Iris pack coverage: " << pick << " compiled=" << compiled.stages.size() <<
          " diagnostics=" << diagnostic_count );
    for( const auto &stage : compiled.stages ) {
        WARN( "Iris compiled: " << stage.program << " " << stage.stage );
    }
    for( const auto &[name, texture] : compiled.textures ) {
        WARN( "Iris prepared texture: " << name << " " << texture.width << "x" << texture.height <<
              "x" << texture.depth << " " << texture.bytes.size() << " bytes" );
    }
    for( const auto &diagnostic : compiled.diagnostics ) {
        WARN( "Iris diagnostic: " << diagnostic );
    }
    for( const auto &program : compiled.unused_minecraft_programs ) {
        WARN( "Iris inactive Minecraft-only draw: " << program );
    }
    if( std::getenv( "CATA_IRIS_TEST_GRAPH" ) ) {
        for( const auto &diagnostic : compiled.diagnostics ) {
            REQUIRE( diagnostic.compare( 0, 26, "Unsupported Iris program: " ) == 0 );
        }
        REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
        struct video_scope {
            ~video_scope() {
                SDL_QuitSubSystem( SDL_INIT_VIDEO );
            }
        } graph_video;
        std::unique_ptr<SDL_GPUDevice, decltype( &SDL_DestroyGPUDevice )> device(
            SDL_CreateGPUDevice( SDL_GPU_SHADERFORMAT_DXIL, true, "direct3d12" ), SDL_DestroyGPUDevice );
        REQUIRE( device );
        auto subset = compiled;
        subset.diagnostics.clear();
        fp3d_iris::scene_post graph;
        SDL_ClearError();
        const bool ready = graph.update( device.get(), subset );
        WARN( "Iris compiled-stage subset GPU graph: " << ( ready ? "ready" : SDL_GetError() ) );
        if( std::getenv( "CATA_IRIS_TEST_FRAME" ) ) {
            INFO( SDL_GetError() );
            REQUIRE( ready );
            using namespace fp3d_gpu_world_qa;
            constexpr Uint32 extent = 64;
            const auto make_color = [&]( Uint32 size ) {
                return fp3d_iris::create_graph_texture( device.get(),
                       { fp3d_gpu::scene_target::color_format(), size, size },
                       SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER );
            };
            std::unique_ptr<SDL_Renderer, decltype( &SDL_DestroyRenderer )> renderer(
                SDL_CreateGPURenderer( device.get(), nullptr ), SDL_DestroyRenderer );
            REQUIRE( renderer );
            fp3d_gpu::scene_target target;
            REQUIRE( target.initialize( renderer.get(), extent, extent, true ) );
            auto atlas = make_color( 1 );
            REQUIRE( atlas );
            const SDL_GPUTransferBufferCreateInfo download{ SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                      extent * extent * 4, 0 };
            std::unique_ptr<SDL_GPUTransferBuffer, std::function<void( SDL_GPUTransferBuffer * )>> readback(
                SDL_CreateGPUTransferBuffer( device.get(), &download ), [&]( SDL_GPUTransferBuffer *buffer ) {
                    SDL_ReleaseGPUTransferBuffer( device.get(), buffer );
                } );
            REQUIRE( readback );
            fp3d_snapshot::scene_snapshot scene;
            scene.sky.sun_y = 1.f;
            scene.sky.daylight = 1.f;
            const auto brightness = fp3d_iris::eye_brightness_at( scene.origin,
                                    { { scene.origin, 8 } }, { { scene.origin, 15 } } );
            const fp3d_gpu_camera_math::matrix4 clip{ { 1, 0, 0, 0, 0, 1, 0, 0,
                                                       0, 0, 1, 0, 0, 0, 0, 1 } };
            fp3d_iris::frame_history history;
            const auto frame = history.prepare( scene, {}, clip, extent, extent, false,
                                               fp3d_iris::read_world_clock( calendar::turn_zero + 12_hours ),
                                               compiled.revision, brightness );
            REQUIRE( frame );
            auto uniforms = iris_scene_uniforms( *frame, fp3d_camera::near_clip, fp3d_camera::far_clip );
            if( graph.uses_shadow() ) {
                uniforms.merge( iris_shadow_uniforms( clip ) );
            }
            fp3d_iris::scene_post::frame_slot slot;
            auto *command = SDL_AcquireGPUCommandBuffer( device.get() );
            REQUIRE( command );
            constexpr SDL_FColor seed{ 1.f, 0.f, 1.f, 1.f };
            SDL_GPUColorTargetInfo color{};
            color.clear_color = seed;
            color.load_op = SDL_GPU_LOADOP_CLEAR;
            color.store_op = SDL_GPU_STOREOP_STORE;
            color.texture = atlas.get();
            auto *clear = SDL_BeginGPURenderPass( command, &color, 1, nullptr );
            REQUIRE( clear );
            SDL_EndGPURenderPass( clear );
            color.texture = target.native_color();
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture = target.depth();
            depth.clear_depth = 1.f;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            clear = SDL_BeginGPURenderPass( command, &color, 1, &depth );
            REQUIRE( clear );
            SDL_EndGPURenderPass( clear );
            // Exercise the original fullscreen stages and their GPU dependencies
            // with an empty scene. Geometry coverage requires real draw providers.
            const auto empty_geometry = []( auto &&... ) { return true; };
            const bool recorded = graph.record( command, target, slot, 1.f, std::move( uniforms ),
                                                { atlas.get(), { target.color_format(), 1, 1 } },
                                                empty_geometry, {}, {}, empty_geometry, empty_geometry );
            if( !recorded ) {
                const std::string error = SDL_GetError();
                SDL_CancelGPUCommandBuffer( command );
                FAIL( "Iris compiled-stage subset frame: " << error );
            }
            SDL_GPUTextureRegion source{};
            source.texture = target.native_color();
            source.w = source.h = extent;
            source.d = 1;
            const SDL_GPUTextureTransferInfo destination{ readback.get(), 0, extent, extent };
            auto *copy = SDL_BeginGPUCopyPass( command );
            REQUIRE( copy );
            SDL_DownloadFromGPUTexture( copy, &source, &destination );
            SDL_EndGPUCopyPass( copy );
            auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence( command );
            REQUIRE( fence );
            const bool finished = SDL_WaitForGPUFences( device.get(), true, &fence, 1 );
            SDL_ReleaseGPUFence( device.get(), fence );
            REQUIRE( finished );
            const auto *pixel = static_cast<const Uint8 *>( SDL_MapGPUTransferBuffer( device.get(),
                                readback.get(), false ) );
            REQUIRE( pixel );
            size_t changed = 0;
            for( size_t i = 0; i < extent * extent * 4; i += 4 ) {
                changed += pixel[i] != 255 || pixel[i + 1] != 0 || pixel[i + 2] != 255;
            }
            SDL_UnmapGPUTransferBuffer( device.get(), readback.get() );
            WARN( "Iris compiled-stage subset frame: " << changed << " pixels written by final" );
            CHECK( changed == extent * extent );
        }
    }
    REQUIRE( choose( "off" ) );
}

TEST_CASE( "fp3d_iris_compiles_current_selection_in_background", "[.][iris_background_compile]" )
{
    using namespace fp3d_shaderpacks;
    const char *folder = std::getenv( "CATA_IRIS_TEST_PACK_ROOT" );
    REQUIRE( folder );
    REQUIRE( std::filesystem::u8path( directory() ) == std::filesystem::u8path( folder ) );
    struct compiler_scope {
        ~compiler_scope() {
            choose( "off" );
            stop_iris_compilation();
        }
    } compiler;
    REQUIRE( choose( "composite-test" ) );
    const auto first = poll_iris_compilation().revision;
    REQUIRE( poll_iris_compilation().running );
    REQUIRE( choose( "off" ) );
    CHECK_FALSE( poll_iris_compilation().running );
    CHECK( poll_iris_compilation().stages.empty() );
    REQUIRE( choose( "composite-test" ) );
    const auto latest = poll_iris_compilation().revision;
    CHECK( latest != first );
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 20 );
    while( poll_iris_compilation().running && std::chrono::steady_clock::now() < deadline ) {
        SDL_Delay( 10 );
    }
    const auto &compiled = poll_iris_compilation();
    CHECK_FALSE( compiled.running );
    CHECK( compiled.revision == latest );
    INFO( ( compiled.diagnostics.empty() ? "No diagnostics" : compiled.diagnostics.front() ) );
    REQUIRE( compiled.diagnostics.empty() );
    REQUIRE( compiled.water_diagnostics.empty() );
    REQUIRE( compiled.stages.size() == 38 );
    for( const auto &stage : compiled.stages ) {
        CHECK( ( stage.program == "gbuffers_water" || stage.program == "composite2" || stage.program == "composite10" ||
                 stage.program == "deferred2" || stage.program == "deferred10" || fp3d_shaderpacks::is_iris_terrain_program( stage.program ) ||
                 fp3d_shaderpacks::is_iris_model_program( stage.program ) ||
                 fp3d_shaderpacks::is_iris_shadow_program( stage.program ) || stage.program == "shadowcomp" || stage.program == "final" ) );
        CHECK( fp3d_iris::load_compiled_shader( stage.bytecode, stage.cache_key ).error.empty() );
        CHECK( stage.layout.target_directive.has_value() == ( stage.stage == "frag" ) );
    }
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
    struct video_scope {
        ~video_scope() {
            SDL_QuitSubSystem( SDL_INIT_VIDEO );
        }
    } video;
    std::unique_ptr<SDL_GPUDevice, decltype( &SDL_DestroyGPUDevice )> device(
        SDL_CreateGPUDevice( SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_DXBC, true,
                             "direct3d12" ), SDL_DestroyGPUDevice );
    REQUIRE( device );
    std::unique_ptr<SDL_Renderer, decltype( &SDL_DestroyRenderer )> renderer(
        SDL_CreateGPURenderer( device.get(), nullptr ), SDL_DestroyRenderer );
    REQUIRE( renderer );
    fp3d_gpu::scene_target target;
    REQUIRE( target.initialize( renderer.get(), 64, 1, true ) );
    auto atlas = fp3d_iris::create_graph_texture( device.get(), { target.color_format(), 1, 1 },
                 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER );
    REQUIRE( atlas );
    fp3d_iris::scene_post post;
    fp3d_iris::scene_post::frame_slot slot;
    const bool loaded = post.update( device.get(), compiled );
    INFO( SDL_GetError() );
    REQUIRE( loaded );
    auto *command = SDL_AcquireGPUCommandBuffer( device.get() );
    REQUIRE( command );
    using namespace fp3d_gpu_world_qa;
    resources native_pipelines( device.get() );
    native_pipelines.vs = shader( device.get(), fp3d_gpu_shader_bytes::base_vertex,
                                  sizeof( fp3d_gpu_shader_bytes::base_vertex ), true, 1, 0 );
    native_pipelines.fs = shader( device.get(), fp3d_gpu_shader_bytes::base_fragment,
                                  sizeof( fp3d_gpu_shader_bytes::base_fragment ), false, 1, 6 );
    REQUIRE( initialize_scene_pipelines( native_pipelines.pipelines, device.get(),
                                         native_pipelines.vs, native_pipelines.fs ) );
    prepared_world world;
    world.camera.elements = { 1,0,0,0, 0,0,2,0, 0,1,0,.5f, 0,0,0,1 };
    fp3d_geometry::quad_mesh_sink sink( world.geometry );
    struct { float left, top, right, bottom; } uv{ 0, 0, 1, 1 };
    fp3d_geometry::floor_face( sink, 0, 0, 0, uv, .5f );
    fp3d_gpu::indexed_mesh mesh;
    REQUIRE( mesh.initialize( device.get(), world.geometry ) );
    world.instances.resize( 8 );
    world.instances[6].offset = { -.5f, .35f, 0.f, 2.f };
    world.instances[7].offset = { .5f, .3f, 0.f, 2.f };
    world.instances[5].offset = { -.5f, .45f, 0.f, 2.f };
    world.instances[4].offset = { .5f, .4f, 0.f, 2.f };
    world.instances[0].offset[0] = -.5f;
    world.instances[1].offset[0] = .5f;
    world.instances[2].offset = { -.5f, -.25f, 0, 2 };
    world.instances[3].offset = { .5f, .25f, 0, 2 };
    world.instances[2].tint = world.instances[3].tint = { 1, 0, 0, .5f };
    world.reflective_batches = {
        { { 0, 6, 0, 1 }, {}, reflective_surface::water_shallow, {}, 10002 },
        { { 0, 6, 1, 1 }, {}, reflective_surface::water_deep, {}, 10003 }
    };
    world.batches = { { 0, 6, 0, 2, false, false, true }, { 0, 6, 4, 1, false, false, false, 48, true },
        { 0, 6, 5, 1, false, false, false, 48 },
        { 0, 6, 6, 1, false, false, false, {}, false, iris_caster::block, 77 },
        { 0, 6, 7, 1, false, false, false, {}, false, iris_caster::entity, 33, 91 },
        { 0, 6, 6, 1, false, false, false, {}, false, iris_caster::block, {}, -1, true },
        { 0, 6, 2, 1, false, false, false, {}, false, iris_caster::entity }, { 0, 6, 3, 1 }
    };
    world.opaque_batches = world.badge_end = 6;
    world.blend_end = 8;
    const tripoint_abs_ms origin( 12, 18, 0 );
    const auto left = origin + tripoint_rel_ms( -1, 0, 0 );
    assign_iris_water_lights( world, origin, { { left, 4 }, { origin, 12 } },
    { { left, 2 }, { origin, 10 } } );
    CHECK( world.reflective_batches[0].iris_lights == std::vector<std::uint8_t>{ 0x24 } );
    CHECK( world.reflective_batches[1].iris_lights == std::vector<std::uint8_t>{ 0xac } );
    assign_iris_terrain_lights( world, origin, { { left, 4 }, { origin, 12 } },
    { { left, 2 }, { origin, 10 } } );
    REQUIRE( world.iris_terrain_lights[0].size() == 8 );
    CHECK( world.iris_terrain_lights[0][4] == 0xac );
    // Resident offsets are anchored and the GPU stream starts with fixed pages.
    prepared_world resident;
    resident.resident_to_scene = { -128.f, 0.f, -64.f };
    std::vector<fp3d_gpu::mesh_instance> pages( 2 );
    pages[1].offset = { 127.f, fp3d_camera::floor_height, 64.f, 0.f };
    resident.resident_pages = std::make_shared<const std::vector<fp3d_gpu::mesh_instance>>( pages );
    resident.resident_instances.resize( 1 );
    resident.resident_instances[0].offset = { 128.f, 0.f, 64.f, 0.f };
    resident.batches = { { 0, 6, 1, 1, true, true, false, 48 }, { 0, 6, 2, 1, true, false, false, 48 } };
    resident.opaque_batches = 2;
    const auto upper_left = left + tripoint_rel_ms::above;
    assign_iris_terrain_lights( resident, origin, { { upper_left, 2 }, { origin, 12 } },
    { { upper_left, 15 }, { origin, 10 } } );
    CHECK( resident.iris_terrain_lights[1] == std::vector<Uint32>{ 0, 0xf2, 0xac } );
    fp3d_gpu::upload_buffer instances;
    fp3d_gpu::upload_buffer terrain_light;
    REQUIRE( stage_iris_terrain_lights( device.get(), world.iris_terrain_lights[0], terrain_light ) );
    REQUIRE( stage_instances( device.get(), world.instances, instances, false ) );
    REQUIRE( stage_draws( device.get(), world, native_pipelines.draw_commands ) );
    auto *upload = SDL_BeginGPUCopyPass( command );
    REQUIRE( upload );
    REQUIRE( mesh.record( upload ) );
    REQUIRE( instances.record( upload ) );
    REQUIRE( terrain_light.record_delta( upload ) );
    REQUIRE( native_pipelines.draw_commands.record( upload ) );
    SDL_EndGPUCopyPass( upload );
    SDL_GPUColorTargetInfo attachment{};
    attachment.texture = atlas.get();
    attachment.clear_color = { 1.f, 1.f, 1.f, 1.f };
    attachment.load_op = SDL_GPU_LOADOP_CLEAR;
    attachment.store_op = SDL_GPU_STOREOP_STORE;
    auto *atlas_pass = SDL_BeginGPURenderPass( command, &attachment, 1, nullptr );
    REQUIRE( atlas_pass );
    SDL_EndGPURenderPass( atlas_pass );
    attachment.texture = target.native_color();
    attachment.clear_color = { .125f, .125f, .125f, 1.f };
    attachment.load_op = SDL_GPU_LOADOP_CLEAR;
    attachment.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = target.depth();
    depth.clear_depth = 1.f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    auto *native = SDL_BeginGPURenderPass( command, &attachment, 1, &depth );
    REQUIRE( native );
    SDL_EndGPURenderPass( native );
    const fp3d_iris::material_id_map materials( post.properties().materials.at( "block.properties" ) );
    const world_meshes meshes{ mesh, nullptr, {}, instances.get() };
    const auto bind = [&]( const fp3d_iris::shader_layout &layout,
                           const fp3d_iris::uniform_values &values, fp3d_iris::graphics_stage &stage ) {
        const auto result = bind_iris_water_draws( world, meshes, layout, materials, values, {}, stage );
        return result.error.empty() && result.missing.empty();
    };
    constexpr auto hdr = SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT;
    scene_pipelines hdr_pipelines;
    REQUIRE( initialize_scene_pipelines( hdr_pipelines, device.get(), native_pipelines.vs,
                                         native_pipelines.fs, hdr ) );
    const auto draw_glass = [&]( SDL_GPUTexture *color, SDL_GPUTexture *water_depth,
    SDL_GPUTextureFormat format ) {
        CHECK( format == hdr );
        return record_world( command, target, world, atlas.get(), hdr_pipelines, meshes,
                             instances.get(), native_pipelines.draw_commands.get(), {}, nullptr, false,
                             nullptr, nullptr, nullptr, nullptr, false, { color, water_depth } );
    };
    fp3d_gpu::textured_pipeline behind_water;
    REQUIRE( initialize_behind_water( device.get(), behind_water, hdr ) );
    const auto draw_refracted_glass = [&]( SDL_GPUTexture *color, SDL_GPUTexture *opaque_depth,
    SDL_GPUTexture *water_depth, SDL_GPUTextureFormat format ) {
        CHECK( format == hdr );
        CHECK( color == slot.images->textures.images().at( "colortex0:4" ) );
        CHECK( color != target.native_color() );
        CHECK( opaque_depth == slot.images->textures.images().at( "depthtex0:6" ) );
        CHECK( opaque_depth != target.depth() );
        CHECK( opaque_depth != water_depth );
        return record_world( command, target, world, atlas.get(), native_pipelines.pipelines, meshes,
                             instances.get(), native_pipelines.draw_commands.get(), {}, nullptr, false,
                             nullptr, nullptr, nullptr, nullptr, false,
                             { color, opaque_depth, water_depth, &behind_water } );
    };
    const auto bind_opaque = [&]( std::string_view name, const fp3d_iris::shader_layout &layout,
                                  const fp3d_iris::shader_layout &fragment, const fp3d_iris::uniform_values &values,
                                  fp3d_iris::graphics_stage &stage ) {
        const auto result = bind_iris_opaque_draws( world, meshes, layout, fragment, values, stage,
                            { terrain_light.get(), nullptr }, post, name );
        INFO( result.error );
        INFO( ( result.missing.empty() ? "" : result.missing.front() ) );
        REQUIRE( result.error.empty() );
        REQUIRE( result.missing.empty() );
        const std::map<std::string_view, Uint32> first_instances{
            { "gbuffers_terrain_solid", 4 }, { "gbuffers_terrain_cutout", 5 },
            { "gbuffers_block", 6 }, { "gbuffers_entities", 7 }, { "gbuffers_basic", 6 }
        };
        const auto first = first_instances.find( name );
        if( first == first_instances.end() ) {
            CHECK( stage.draws.empty() );
        } else {
            REQUIRE( stage.draws.size() == 1 );
            CHECK( stage.draws.front().first_instance == first->second );
        }
        return true;
    };
    const auto bind_shadow = [&]( std::string_view name, const fp3d_iris::shader_layout &layout,
                                  const fp3d_iris::uniform_values &values, fp3d_iris::graphics_stage &stage ) {
        const auto bound = bind_iris_shadow_draws( world, meshes, layout, values, stage, post, name, 10001 );
        const std::map<std::string_view, std::vector<Uint32>> expected{
            { "shadow_solid", { 4 } }, { "shadow_cutout", { 5 } },
            { "shadow_block", { 6, 6 } }, { "shadow_entities", { 7 } },
            { "shadow", {} }, { "shadow_water", { 0, 1 } },
            { "shadow_translucent_block", {} }, { "shadow_translucent_entities", { 2 } },
            { "shadow_translucent_geometry", { 3 } }
        };
        std::vector<Uint32> draws;
        for( const auto &draw : stage.draws ) {
            draws.push_back( draw.first_instance );
        }
        CHECK( draws == expected.at( name ) );
        return bound.error.empty() && bound.missing.empty();
    };
    REQUIRE( post.uses_water() );
    REQUIRE( post.uses_opaque_geometry() );
    REQUIRE( post.uses_shadow() );
    CHECK( post.shadow_resolution() == 32 );
    const fp3d_gpu_camera_math::matrix4 shadow_clip{ { 1,0,0,0, 0,0,2,0, 0,1,0,.5f, 0,0,0,1 } };
    fp3d_iris::frame_history history;
    fp3d_snapshot::scene_snapshot snapshot;
    snapshot.origin = tripoint_abs_ms( -18000000, 18, -2 );
    snapshot.weather.particles = fp3d_weather::precipitation::rain;
    snapshot.weather.intensity = .625f;
    snapshot.player_underwater = true;
    snapshot.sky.sun_x = .3f;
    snapshot.sky.sun_y = -.4f;
    snapshot.sky.sun_z = 0.f;
    auto frame_clip = world.camera;
    frame_clip.elements[13] = 1.f;
    auto prior_clip = frame_clip;
    prior_clip.elements[3] = .25f;
    fp3d_camera::pose camera{};
    camera.eye.x = -.25f;
    camera.eye.height = 1.5f;
    camera.eye.y = -.125f;
    const auto noon = fp3d_iris::read_world_clock( calendar::turn_zero + 3_days + 12_hours );
    const auto dawn = calendar::turn_zero + 3_days + 6_hours;
    const auto before_dawn = fp3d_iris::read_world_clock( dawn - 1_seconds );
    const auto at_dawn = fp3d_iris::read_world_clock( dawn );
    CHECK( before_dawn.world_time == 23999 );
    CHECK( before_dawn.world_day + 1 == at_dawn.world_day );
    CHECK( at_dawn.world_time == 0 );
    const auto prior = history.prepare( snapshot, camera, prior_clip, 64, 1, false, noon, latest );
    REQUIRE( prior );
    CHECK( prior->frame_counter == 0 );
    history.presented( *prior );
    camera.eye.x = .125f;
    const auto frame = history.prepare( snapshot, camera, frame_clip, 64, 1, false, noon, latest );
    REQUIRE( frame );
    REQUIRE( frame->previous );
    CHECK( frame->frame_counter == 1 );
    const auto restarted = history.prepare( snapshot, camera, world.camera, 64, 1, false, {}, latest + 1 );
    REQUIRE( restarted );
    CHECK_FALSE( restarted->previous );
    CHECK( restarted->frame_counter == 0 );
    auto shifted = snapshot;
    shifted.origin += tripoint_rel_ms::east;
    auto shifted_camera = camera;
    shifted_camera.eye.x -= 1.f;
    const auto rebased = history.prepare( shifted, shifted_camera, world.camera, 64, 1, false, {}, latest );
    REQUIRE( rebased );
    CHECK_FALSE( rebased->previous );
    const auto rebased_values = fp3d_iris::native_frame_uniform_values( *rebased );
    CHECK( rebased_values.at( "cameraPositionInt" ) == rebased_values.at( "previousCameraPositionInt" ) );
    CHECK( rebased_values.at( "cameraPositionFract" ) == rebased_values.at( "previousCameraPositionFract" ) );
    auto uniforms = iris_scene_uniforms( *frame, fp3d_camera::near_clip, fp3d_camera::far_clip );
    CHECK( uniforms.at( "cameraPositionInt" ) == rebased_values.at( "cameraPositionInt" ) );
    CHECK( uniforms.at( "cameraPositionFract" ) == rebased_values.at( "cameraPositionFract" ) );
    auto daylight = *frame;
    daylight.sun_direction[1] *= -1.f;
    const auto daylight_values = iris_scene_uniforms( daylight, fp3d_camera::near_clip,
                                 fp3d_camera::far_clip );
    CHECK( daylight_values.at( "shadowLightPosition" ) == daylight_values.at( "sunPosition" ) );
    CHECK( uniforms.at( "shadowLightPosition" ) == uniforms.at( "moonPosition" ) );
    uniforms.merge( iris_shadow_uniforms( shadow_clip ) );
    REQUIRE( post.record( command, target, slot, 0.f, std::move( uniforms ),
                          { atlas.get(), { target.color_format(), 1, 1 } }, bind, draw_glass,
                          draw_refracted_glass, bind_opaque, bind_shadow ) );
    REQUIRE( slot.images );
    CHECK( slot.images->specs.at( "colortex0:0" ).format == hdr );
    CHECK( slot.images->textures.images().at( "colortex0:0" ) != target.native_color() );
    CHECK( slot.images->specs.at( "shadowtex0:4" ).width == 32 );
    CHECK( slot.images->specs.at( "shadowcolor0:10" ).height == 32 );
    CHECK( slot.images->textures.images().at( "shadowtex0:4" ) !=
           slot.images->textures.images().at( "shadowtex1:5" ) );
    CHECK( slot.images->specs.at( "colortex1:1" ).format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT );
    CHECK( slot.images->specs.at( "colortex1:1" ).width == 32 );
    CHECK( slot.images->specs.at( "colortex5:1" ).format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT );
    CHECK( slot.images->specs.at( "colortex7:1" ).format == SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT );
    CHECK( slot.images->specs.at( "colortex9:1" ).opaque_alpha );
    CHECK( slot.images->specs.at( "colortex10:1" ).opaque_alpha );
    CHECK( slot.images->specs.at( "colortex15:0" ).clear_color == std::array<float, 4>{ 1.25f, -2.f, .5f, .25f } );
    CHECK( slot.images->textures.images().at( "depthtex1:0" ) == target.depth() );
    CHECK( slot.images->textures.images().at( "depthtex0:7" ) !=
           slot.images->textures.images().at( "depthtex0:6" ) );
    const SDL_GPUTransferBufferCreateInfo info{ SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, 256, 0 };
    auto *readback = SDL_CreateGPUTransferBuffer( device.get(), &info );
    REQUIRE( readback );
    auto *copy = SDL_BeginGPUCopyPass( command );
    REQUIRE( copy );
    SDL_GPUTextureRegion source{};
    source.texture = target.native_color();
    source.w = 64;
    source.h = source.d = 1;
    const SDL_GPUTextureTransferInfo destination{ readback, 0, 64, 1 };
    SDL_DownloadFromGPUTexture( copy, &source, &destination );
    SDL_EndGPUCopyPass( copy );
    auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence( command );
    REQUIRE( fence );
    REQUIRE( SDL_WaitForGPUFences( device.get(), true, &fence, 1 ) );
    const auto *pixel = static_cast<const Uint8 *>( SDL_MapGPUTransferBuffer( device.get(), readback, false ) );
    REQUIRE( pixel );
    for( int x = 0; x < 64; ++x ) {
        if( x < 32 ) {
            CHECK( std::abs( static_cast<int>( pixel[x * 4] ) - 159 ) <= 4 );
            CHECK( std::abs( static_cast<int>( pixel[x * 4 + 1] ) - 235 ) <= 4 );
            CHECK( std::abs( static_cast<int>( pixel[x * 4 + 2] ) - 91 ) <= 4 );
        } else if( x < 56 ) {
            CHECK( std::abs( static_cast<int>( pixel[x * 4] ) - 64 ) <= 4 );
            CHECK( std::abs( static_cast<int>( pixel[x * 4 + 1] ) - 88 ) <= 4 );
            CHECK( std::abs( static_cast<int>( pixel[x * 4 + 2] ) - 24 ) <= 4 );
        } else {
            CHECK( std::abs( static_cast<int>( pixel[x * 4] ) - 191 ) <= 4 );
            CHECK( std::abs( static_cast<int>( pixel[x * 4 + 1] ) - 223 ) <= 4 );
            CHECK( std::abs( static_cast<int>( pixel[x * 4 + 2] ) - 111 ) <= 4 );
        }
    }
    SDL_UnmapGPUTransferBuffer( device.get(), readback );
    SDL_ReleaseGPUTransferBuffer( device.get(), readback );
    SDL_ReleaseGPUFence( device.get(), fence );
    // With no viable water stage the native scene still draws the water and
    // the pack's composite/final programs transform that colour.
    world.batches.resize( 1 );
    world.opaque_batches = world.badge_end = world.blend_end = 1;
    world.width = 64;
    world.height = 1;
    for( size_t i = 0; i < 2; ++i ) {
        world.instances[i].offset[3] = 2.f;
        world.instances[i].tint = { .2f, .5f, .75f, 1.f };
    }
    REQUIRE( stage_instances( device.get(), world.instances, instances, false ) );
    REQUIRE( stage_draws( device.get(), world, native_pipelines.draw_commands ) );
    auto fallback = compiled;
    const auto keep = []( const auto &stage ) {
        return stage.program == "gbuffers_water" || stage.program == "composite2" ||
               stage.program == "composite10" || stage.program == "final";
    };
    fallback.stages.erase( std::remove_if( fallback.stages.begin(), fallback.stages.end(),
    [&]( const auto &stage ) { return !keep( stage ); } ), fallback.stages.end() );
    fallback.properties.materials.erase( "block.properties" );
    for( int variant = 0; variant < 4; ++variant ) {
        CAPTURE( variant );
        if( variant == 1 ) {
            fallback.stages.erase( std::remove_if( fallback.stages.begin(), fallback.stages.end(),
            []( const auto &stage ) { return stage.program == "gbuffers_water"; } ), fallback.stages.end() );
        }
        if( variant >= 2 ) {
            REQUIRE( choose( variant == 2 ? "broken-water" : "unsupported-water-resource" ) );
            const auto fallback_deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 20 );
            while( poll_iris_compilation().running && std::chrono::steady_clock::now() < fallback_deadline ) {
                SDL_Delay( 10 );
            }
            fallback = poll_iris_compilation();
            REQUIRE_FALSE( fallback.running );
            REQUIRE( fallback.diagnostics.empty() );
            if( variant == 2 ) {
                REQUIRE( fallback.water_diagnostics.size() == 1 );
                CHECK( fallback.water_diagnostics.front().find( "gbuffers_water.fsh" ) != std::string::npos );
                CHECK( fallback.water_diagnostics.front().find( "P7_WATER_COMPILE_FAILURE" ) != std::string::npos );
                CHECK( fallback.stages.size() == 6 );
                CHECK( status().find( "水面程序编译失败" ) != std::string::npos );
            } else {
                REQUIRE( fallback.water_diagnostics.empty() );
                REQUIRE( fallback.stages.size() == 8 );
            }
        }
        // The first two graphs are derived from the currently compiled fixture.
        // Give each a distinct revision solely for the GPU slot cache.
        fallback.revision = latest + 10 + variant;
        REQUIRE( post.update( device.get(), fallback ) );
        CHECK_FALSE( post.uses_water() );
        const std::array<std::string_view, 4> reasons{
            "pack does not map minecraft:water in block.properties",
            "pack does not provide gbuffers_water",
            "gbuffers_water compilation failed:",
            "gbuffers_water GPU setup failed: Missing Iris sampler resource: waterOnlyTexture"
        };
        CHECK( post.water_fallback_reason().compare( 0, reasons[variant].size(), reasons[variant] ) == 0 );
        command = SDL_AcquireGPUCommandBuffer( device.get() );
        REQUIRE( command );
        if( variant == 0 ) {
            auto *transfer = SDL_BeginGPUCopyPass( command );
            REQUIRE( transfer );
            REQUIRE( instances.record( transfer ) );
            REQUIRE( native_pipelines.draw_commands.record( transfer ) );
            SDL_EndGPUCopyPass( transfer );
        }
        REQUIRE( record_world( command, target, world, atlas.get(), native_pipelines.pipelines, meshes,
                               instances.get(), native_pipelines.draw_commands.get() ) );
        REQUIRE( post.record( command, target, slot, 0.f ) );
        CHECK( slot.images->specs.count( "depthtex0:1" ) == 0 );
        readback = SDL_CreateGPUTransferBuffer( device.get(), &info );
        REQUIRE( readback );
        copy = SDL_BeginGPUCopyPass( command );
        REQUIRE( copy );
        const SDL_GPUTextureTransferInfo fallback_destination{ readback, 0, 64, 1 };
        SDL_DownloadFromGPUTexture( copy, &source, &fallback_destination );
        SDL_EndGPUCopyPass( copy );
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence( command );
        REQUIRE( fence );
        REQUIRE( SDL_WaitForGPUFences( device.get(), true, &fence, 1 ) );
        pixel = static_cast<const Uint8 *>( SDL_MapGPUTransferBuffer( device.get(), readback, false ) );
        REQUIRE( pixel );
        for( int x : { 16, 48 } ) {
            CHECK( std::abs( static_cast<int>( pixel[x * 4] ) - 64 ) <= 1 );
            CHECK( std::abs( static_cast<int>( pixel[x * 4 + 1] ) - 127 ) <= 1 );
            CHECK( std::abs( static_cast<int>( pixel[x * 4 + 2] ) - 204 ) <= 1 );
        }
        SDL_UnmapGPUTransferBuffer( device.get(), readback );
        SDL_ReleaseGPUTransferBuffer( device.get(), readback );
        SDL_ReleaseGPUFence( device.get(), fence );
    }
    // Make the unavailable resource a non-water requirement as well. The
    // successful water-only fallback must not hide a shared dependency error.
    const auto final_fragment = std::find_if( fallback.stages.begin(), fallback.stages.end(),
    []( const auto &stage ) { return stage.program == "final" && stage.stage == "frag"; } );
    REQUIRE( final_fragment != fallback.stages.end() );
    final_fragment->layout.samplers.push_back( { "waterOnlyTexture", "sampler2D" } );
    ++fallback.revision;
    REQUIRE_FALSE( post.update( device.get(), fallback ) );
    CHECK( std::string_view( SDL_GetError() ).find( "without gbuffers_water" ) != std::string_view::npos );
    CHECK( post.water_fallback_reason().empty() );

    REQUIRE( choose( "history-test" ) );
    const auto history_deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 20 );
    while( poll_iris_compilation().running && std::chrono::steady_clock::now() < history_deadline ) {
        SDL_Delay( 10 );
    }
    const auto &history_pack = poll_iris_compilation();
    REQUIRE_FALSE( history_pack.running );
    REQUIRE( history_pack.diagnostics.empty() );
    REQUIRE( history_pack.stages.size() == 4 );
    REQUIRE( post.update( device.get(), history_pack ) );
    if( history_pack.properties.documents.count( "item.properties" ) ) {
        CHECK( post.held_item_id( "flashlight_on" ) == 42 );
        CHECK( post.held_item_id( "flashlight" ) == -1 );
        CHECK( post.held_item_id( "" ) == -1 );
    }
    fp3d_iris::scene_post::frame_slot history_slot;
    const bool tests_wetness = std::any_of( history_pack.stages.begin(), history_pack.stages.end(),
    []( const auto &stage ) {
        return stage.layout.wetness_half_life == 2.f && stage.layout.dryness_half_life == 4.f;
    } );
    const bool tests_brightness = std::any_of( history_pack.stages.begin(), history_pack.stages.end(),
    []( const auto &stage ) {
        return stage.layout.uniforms.count( "screenBrightness" ) != 0;
    } );
    override_option brightness( "FP3D_IRIS_BRIGHTNESS", "0.5" );
    const auto read_history = [&]( fp3d_iris::scene_post::frame_slot &draw_slot,
                                   const fp3d_iris::scene_post::frame_slot *prior_slot,
                                   float time, int expected_transition, bool retain_render_history = true ) {
        auto *cmd = SDL_AcquireGPUCommandBuffer( device.get() );
        REQUIRE( cmd );
        fp3d_iris::uniform_values weather{
            { "rainStrength", std::vector<float>{ time == .2f ? 1.f : 0.f } }
        };
        REQUIRE( post.record( cmd, target, draw_slot, time, std::move( weather ),
                              {}, {}, {}, {}, {}, {}, prior_slot, retain_render_history ) );
        CHECK( draw_slot.images->textures.images().count( "colortex5:0" ) == 1 );
        CHECK( draw_slot.images->textures.images().count( "colortex5:alt" ) == 1 );
        auto *transfer = SDL_CreateGPUTransferBuffer( device.get(), &info );
        REQUIRE( transfer );
        auto *download = SDL_BeginGPUCopyPass( cmd );
        REQUIRE( download );
        const SDL_GPUTextureTransferInfo location{ transfer, 0, 64, 1 };
        SDL_DownloadFromGPUTexture( download, &source, &location );
        SDL_EndGPUCopyPass( download );
        auto *complete = SDL_SubmitGPUCommandBufferAndAcquireFence( cmd );
        REQUIRE( complete );
        REQUIRE( SDL_WaitForGPUFences( device.get(), true, &complete, 1 ) );
        const auto *mapped = static_cast<const Uint8 *>( SDL_MapGPUTransferBuffer( device.get(), transfer, false ) );
        REQUIRE( mapped );
        const int red = mapped[0];
        if( history_pack.properties.uniform_expressions.count( "historyFade" ) ) {
            CHECK( std::abs( static_cast<int>( mapped[1] ) - expected_transition ) <= 1 );
        }
        if( tests_wetness ) {
            CHECK( std::abs( static_cast<int>( mapped[2] ) - expected_transition ) <= 1 );
        }
        SDL_UnmapGPUTransferBuffer( device.get(), transfer );
        SDL_ReleaseGPUTransferBuffer( device.get(), transfer );
        SDL_ReleaseGPUFence( device.get(), complete );
        return red;
    };
    CHECK( read_history( slot, nullptr, 0.f, 0 ) == 64 );
    CHECK( std::abs( read_history( history_slot, &slot, .2f, 128 ) - 128 ) <= 1 );
    CHECK( read_history( slot, &history_slot, .6f, 64 ) == 191 );
    CHECK( read_history( history_slot, &slot, .6f, 64, false ) == 64 );
    CHECK( read_history( history_slot, nullptr, .6f, 0 ) == 64 );
    if( tests_brightness ) {
        override_option dim( "FP3D_IRIS_BRIGHTNESS", "0.25" );
        CHECK( read_history( slot, nullptr, .6f, 0 ) == 32 );
    }
    REQUIRE( choose( "off" ) );
    CHECK( poll_iris_compilation().stages.empty() );
    CHECK_FALSE( post.update( device.get(), poll_iris_compilation() ) );
}

TEST_CASE( "fp3d_selected_iris_pack_tracks_selection_and_refresh", "[.][iris_selection]" )
{
    using namespace fp3d_shaderpacks;
    const char *folder = std::getenv( "CATA_IRIS_TEST_PACK_ROOT" );
    REQUIRE( folder );
    REQUIRE( std::filesystem::u8path( directory() ) == std::filesystem::u8path( folder ) );
    const auto off = selected_iris_pack();
    CHECK_FALSE( off.source );
    REQUIRE( choose( "photon_v1.4.zip" ) );
    const auto photon = selected_iris_pack();
    REQUIRE( photon.source );
    INFO( photon.source->error );
    REQUIRE( photon.source->error.empty() );
    CHECK( photon.source->program_directory == "world0" );
    CHECK( std::find( photon.source->programs.begin(), photon.source->programs.end(),
                      "world0/gbuffers_water" ) != photon.source->programs.end() );
    const auto without_bloom = fp3d_iris::prepare_pack_compilation( *photon.source, { { "BLOOM", "0" } } );
    const auto with_bloom = fp3d_iris::prepare_pack_compilation( *photon.source, { { "BLOOM", "1" } } );
    INFO( without_bloom.error );
    REQUIRE( without_bloom.error.empty() );
    INFO( with_bloom.error );
    REQUIRE( with_bloom.error.empty() );
    const auto contains = []( const fp3d_iris::pack_compilation &plan, const std::string &name ) {
        return std::any_of( plan.programs.begin(), plan.programs.end(), [&]( const auto &job ) {
            return job.name == name;
        } );
    };
    CHECK_FALSE( contains( without_bloom, "composite11" ) );
    CHECK( contains( with_bloom, "composite11" ) );
    CHECK( contains( without_bloom, "gbuffers_water" ) );
    CHECK( selected_iris_pack().source == photon.source );
    request_reload();
    CHECK( selected_iris_pack().revision != photon.revision );
    CHECK( selected_iris_pack().source != photon.source );
    REQUIRE( choose( "dimension-test" ) );
    const auto custom = selected_iris_pack();
    REQUIRE( custom.source );
    REQUIRE( custom.source->error.empty() );
    CHECK( custom.source->program_directory == "main" );
    CHECK( custom.source->programs == std::vector<std::string>{ "main/final" } );
    REQUIRE( choose( "off" ) );
    CHECK_FALSE( selected_iris_pack().source );
    CHECK( photon.source->program_directory == "world0" );
}

TEST_CASE( "fp3d_iris_loads_verified_d3d12_artifacts", "[.][iris_artifact_gpu]" )
{
    const char *folder = std::getenv( "CATA_IRIS_TEST_ARTIFACTS" );
    REQUIRE( folder );
    const auto path = std::filesystem::u8path( folder );
    const auto vertex = path / "vert.dxil";
    const auto fragment = path / "frag.dxil";
    const auto modified = path / "modified.dxil";
    const auto damaged = fp3d_iris::load_compiled_shader( modified );
    CHECK( damaged.error.find( "SHA-256 mismatch" ) != std::string::npos );
    CHECK( damaged.bytecode.empty() );
    const auto missing = fp3d_iris::load_compiled_shader( path / "absent.dxil" );
    CHECK( missing.error.find( "absent.dxil.json" ) != std::string::npos );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
    struct video_scope {
        ~video_scope() {
            SDL_QuitSubSystem( SDL_INIT_VIDEO );
        }
    } video;
    const auto destroy = []( SDL_GPUDevice *device ) { SDL_DestroyGPUDevice( device ); };
    std::unique_ptr<SDL_GPUDevice, decltype( destroy )> device(
        SDL_CreateGPUDevice( SDL_GPU_SHADERFORMAT_DXIL, true, "direct3d12" ), destroy );
    INFO( SDL_GetError() );
    REQUIRE( device );
    constexpr auto color = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    auto loaded = fp3d_iris::load_graphics_program( device.get(), { vertex, {} }, { fragment, {} }, {},
                  { { color, {} } } );
    INFO( SDL_GetError() );
    REQUIRE( loaded.program );
    CHECK( loaded.program->stage().pipeline );
    CHECK( loaded.vertex.input_locations.empty() );
    CHECK( loaded.fragment.output_locations == std::vector<unsigned>{ 0 } );
    REQUIRE( loaded.fragment.samplers.size() == 1 );
    CHECK( loaded.fragment.samplers.front().name == "colortex0" );
    REQUIRE( !loaded.fragment.cache_key.empty() );
    CHECK( fp3d_iris::load_compiled_shader( fragment, loaded.fragment.cache_key ).error.empty() );
    CHECK( fp3d_iris::load_compiled_shader( fragment, loaded.vertex.cache_key ).error.find(
               "cache key mismatch" ) != std::string::npos );
    CHECK_FALSE( fp3d_iris::load_graphics_program( device.get(), { fragment, {} }, { vertex, {} }, {},
                 { { color, {} } } ).program );

    using namespace fp3d_iris;
    const auto water_vertex_path = path / "water" / "vert.dxil";
    const auto water_fragment_path = path / "water" / "frag.dxil";
    const auto water_vertex = load_compiled_shader( water_vertex_path );
    const auto water_fragment = load_compiled_shader( water_fragment_path );
    REQUIRE( water_vertex.error.empty() );
    REQUIRE( water_fragment.error.empty() );
    graphics_pass_artifacts water{
        { water_vertex_path, water_vertex.layout.cache_key },
        { water_fragment_path, water_fragment.layout.cache_key },
        fp3d_gpu::native_mesh_vertex_input(), {}
    };
    for( const auto *layout : { &water_vertex.layout, &water_fragment.layout } ) {
        for( const auto &sampler : layout->samplers ) {
            water.sampler_images.emplace( sampler.name, "fixture" );
        }
    }
    pack_fullscreen_program water_pass{ "gbuffers_water", {}, {}, color_buffer_bank::scene,
                                        pass_kind::geometry, "depthtex0" };
    REQUIRE( bind_compiled_program( water_pass, water_vertex.layout, water_fragment.layout,
                                    water.sampler_images ).empty() );
    auto mismatched = water_fragment.layout;
    mismatched.target_directive = "/* RENDERTARGETS: 3 */";
    CHECK( bind_compiled_program( water_pass, water_vertex.layout, mismatched, water.sampler_images ).find(
               "disagree" ) != std::string::npos );
    pack_fullscreen_program final_pass{ "final", {}, {} };
    REQUIRE( bind_compiled_program( final_pass, loaded.vertex, loaded.fragment,
    { { "colortex0", "colortex13" } } ).empty() );
    const auto plan = build_pack_fullscreen_graph( { "fixture" }, { water_pass, final_pass }, "display" );
    REQUIRE( plan.error.empty() );
    CHECK( plan.passes.front().writes == std::vector<std::string>{ "colortex3:1", "colortex13:1" } );
    std::map<std::string, graphics_pass_artifacts> artifacts{
        { "gbuffers_water", water },
        { "final", { { vertex, loaded.vertex.cache_key }, { fragment, loaded.fragment.cache_key }, {},
            { { "colortex0", "colortex13" } } } }
    };
    const std::map<std::string, graph_image_spec> specs{
        { "colortex3:1", { color, 64, 64 } }, { "colortex13:1", { color, 64, 64 } },
        { "display:1", { color, 64, 64 } },
        { "depthtex0:1", { SDL_GPU_TEXTUREFORMAT_D32_FLOAT, 64, 64 } }
    };
    graphics_program_set active;
    REQUIRE( load_graphics_programs( device.get(), plan, artifacts, specs, active ) );
    REQUIRE( active.size() == 2 );
    const auto *final_pipeline = active.at( "final" ).program->stage().pipeline;
    REQUIRE( final_pipeline );
    artifacts.at( "final" ).fragment.cache_key = loaded.vertex.cache_key;
    CHECK_FALSE( load_graphics_programs( device.get(), plan, artifacts, specs, active ) );
    CHECK( std::string( SDL_GetError() ).find( "Iris pass final: Iris cache key mismatch" ) != std::string::npos );
    CHECK( active.at( "final" ).program->stage().pipeline == final_pipeline );
    artifacts.at( "final" ).fragment.cache_key = loaded.fragment.cache_key;
    artifacts.at( "final" ).sampler_images.at( "colortex0" ) = "fixture";
    CHECK_FALSE( load_graphics_programs( device.get(), plan, artifacts, specs, active ) );
    CHECK( std::string( SDL_GetError() ).find( "sampler bindings changed for final" ) != std::string::npos );
    CHECK( active.at( "final" ).program->stage().pipeline == final_pipeline );
}
#endif

TEST_CASE( "fp3d_iris_water_camera_matches_native_clip", "[fp3d][iris_water]" )
{
    fp3d_gpu_camera_math::camera camera;
    camera.yaw_degrees = 27.f;
    camera.pitch_degrees = -19.f;
    camera.eye_east = 12.f;
    camera.eye_height = 3.f;
    camera.eye_south = -7.f;
    camera.aspect = 1.6f;
    const auto native = fp3d_gpu_camera_math::clip_from_world( camera );
    REQUIRE( native );
    const auto values = fp3d_gpu_world_qa::iris_camera_uniforms( *native, camera.near_clip, camera.far_clip );
    const auto multiply = [&]( const char *name, const std::array<float, 4> &v ) {
        const auto &m = std::get<std::vector<float>>( values.at( name ) );
        std::array<float, 4> result{};
        for( int row = 0; row < 4; ++row ) {
            for( int col = 0; col < 4; ++col ) {
                result[row] += m[col * 4 + row] * v[col];
            }
        }
        return result;
    };
    const std::array<float, 4> world{ 15.f, 0.f, -14.f, 1.f };
    const auto view = multiply( "gbufferModelView", world );
    const auto clip = multiply( "gbufferProjection", view );
    const auto projected = native->transform( world[0], world[1], world[2] );
    CHECK( clip[0] == Approx( projected[0] ).margin( .00001f ) );
    CHECK( clip[1] == Approx( projected[1] ).margin( .00001f ) );
    CHECK( ( clip[2] + clip[3] ) * .5f == Approx( projected[2] ).margin( .00001f ) );
    CHECK( clip[3] == Approx( projected[3] ).margin( .00001f ) );
    const auto eye = multiply( "gbufferProjectionInverse", clip );
    const auto back = multiply( "gbufferModelViewInverse", view );
    for( int i = 0; i < 4; ++i ) {
        CHECK( eye[i] == Approx( view[i] ).margin( .00001f ) );
        CHECK( back[i] == Approx( world[i] ).margin( .00001f ) );
    }
}

TEST_CASE( "fp3d_iris_layout_binds_named_resources_and_std140_uniforms", "[fp3d][iris_water]" )
{
    using namespace fp3d_iris;
    const std::string source = R"({
        "version":1, "stage":"frag", "format":"dxil",
        "bytecode_sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "samplers":[{"name":"depthtex1","type":"sampler2D","slot":0},
                    {"name":"shadowtex1","type":"sampler2DShadow","slot":1}],
        "frame":{"size":80,"members":[{"name":"matrix","type":"mat3","offset":0,"matrix_stride":16},
                                     {"name":"isEyeInWater","type":"int","offset":48},
                                     {"name":"view_res","type":"vec2","offset":56},
                                     {"name":"spare","type":"uint","offset":64}]}
    })";
    const auto layout = parse_shader_layout( source );
    INFO( layout.error );
    REQUIRE( layout.error.empty() );
    REQUIRE( layout.samplers.size() == 2 );
    CHECK( layout.samplers[1].type == "sampler2DShadow" );
    const auto inputs = resolve_sampler_inputs( layout,
    { { "shadowtex1", "shadowtex1:1" }, { "depthtex1", "depthtex1:0" } } );
    CHECK( inputs.error.empty() );
    CHECK( inputs.images == std::vector<std::string>{ "depthtex1:0", "shadowtex1:1" } );
    CHECK( !resolve_sampler_inputs( layout, { { "depthtex1", "depthtex1:0" } } ).error.empty() );
    shader_layout vertex;
    vertex.stage = "vert";
    vertex.format = "dxil";
    vertex.samplers = { { "noise", "sampler2D" } };
    pack_fullscreen_program water{ "water", "/* RENDERTARGETS: 3 */", {},
                                  color_buffer_bank::scene, pass_kind::geometry };
    REQUIRE( bind_program_sampler_inputs( water, vertex, layout,
    { { "noise", "noise" }, { "shadowtex1", "shadowtex1" }, { "depthtex1", "depthtex1" } } ).empty() );
    const auto graph = build_pack_fullscreen_graph( { "noise", "depthtex1", "shadowtex1" },
                       { water }, "colortex3" );
    REQUIRE( graph.error.empty() );
    CHECK( graph.passes[0].vertex_reads == std::vector<std::string>{ "noise:0" } );
    CHECK( graph.passes[0].reads == std::vector<std::string>{ "depthtex1:0", "shadowtex1:0" } );
    CHECK_FALSE( bind_program_sampler_inputs( water, vertex, layout, { { "noise", "other" } } ).empty() );
    CHECK( water.vertex_samplers == std::vector<std::string>{ "noise" } );
    frame_inputs frame{};
    frame.current.width = 800;
    frame.current.height = 600;
    frame.current.eye_in_water = 1;
    auto values = native_frame_uniform_values( frame );
    values.emplace( "matrix", std::vector<float>{ 1, 2, 3, 4, 5, 6, 7, 8, 9 } );
    const auto packed = pack_frame_uniforms( layout, values );
    REQUIRE( packed.error.empty() );
    REQUIRE( packed.bytes.size() == 80 );
    std::array<float, 12> matrix;
    std::memcpy( matrix.data(), packed.bytes.data(), sizeof( matrix ) );
    CHECK( matrix == std::array<float, 12>{ 1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0 } );
    std::int32_t eye = 0;
    std::memcpy( &eye, packed.bytes.data() + 48, sizeof( eye ) );
    CHECK( eye == 1 );
    std::array<float, 2> resolution;
    std::memcpy( resolution.data(), packed.bytes.data() + 56, sizeof( resolution ) );
    CHECK( resolution == std::array<float, 2>{ 800, 600 } );
    CHECK( packed.missing == std::vector<std::string>{ "spare" } );
    CHECK( !pack_frame_uniforms( layout, { { "isEyeInWater", std::vector<float>{ 1 } } } ).error.empty() );
    auto overlapping = source;
    overlapping.replace( overlapping.find( "\"offset\":56" ), 11, "\"offset\":48" );
    CHECK( parse_shader_layout( overlapping ).error.find( "Overlapping" ) != std::string::npos );
}

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
    for( const char *terrain : { "modded_current", "t_water_dp", "t_pavement" } ) {
        const int x = static_cast<int>( scene.cells.size() ) - 1;
        auto &tile = scene.cells.emplace_back();
        tile.position = scene.origin + tripoint_rel_ms( x, -3, 0 );
        tile.terrain = terrain;
    }
    fp3d_camera::pose pose;
    pose.eye.height = 1.5f;
    scene.cells[0].water = fp3d_snapshot::water_traits{ fp3d_snapshot::water_depth::shallow, true, true };
    fp3d_snapshot::connect_ground_terrain( scene );
    CHECK( scene.cells[0].ground_connections == 2 );
    CHECK( scene.cells[1].ground_connections == 4 );
    terrain_index index;
    const fp3d_iris::material_id_map water_materials( {
        { 10001, { "minecraft:water" } },
        { 10002, { "cdda:modded_current:flowing=true:salt=true" } },
        { 10003, { "cdda:t_water_dp:depth=deep" } }
    } );
    const auto world = prepare( scene, pose, false, 512, 512, true, false, false, false, nullptr,
                               nullptr, &index, false, nullptr, nullptr, 1.f, nullptr, nullptr, nullptr,
                               &water_materials );
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
        CHECK( draw->iris_water_material == ( surface == reflective_surface::water_shallow ? 10002 : 10003 ) );
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

    resident_water.reflective_batches.push_back( { { 0, 6, 0, 1 }, {},
                                                 reflective_surface::water_shallow } );
    fp3d_iris::shader_layout vertex;
    vertex.stage = "vert";
    vertex.frame_size = 32;
    using scalar = fp3d_iris::uniform_scalar;
    vertex.uniforms = {
        { "cata_iris_anchor", { scalar::floating, 0, 3, 1, 0 } },
        { "cata_iris_lightmap", { scalar::floating, 16, 2, 1, 0 } },
        { "cata_iris_material", { scalar::floating, 24, 1, 1, 0 } }
    };
    fp3d_iris::graphics_stage stage;
    REQUIRE_FALSE( bind_iris_water_draws( resident_water, meshes, vertex,
                   fp3d_iris::material_id_map( {} ), {}, {}, stage ).error.empty() );
    CHECK( stage.draws.empty() );
    const fp3d_iris::material_id_map materials( { { 10001, { "water" } } } );
    const auto bound = bind_iris_water_draws( resident_water, meshes, vertex, materials, {},
                       { 16.f, 240.f }, stage );
    REQUIRE( bound.error.empty() );
    REQUIRE( stage.draw_vertex_constants.size() == 2 );
    std::array<float, 8> resident_constants;
    std::array<float, 8> dynamic_constants;
    std::memcpy( resident_constants.data(), stage.draw_vertex_constants[0].data(), 32 );
    std::memcpy( dynamic_constants.data(), stage.draw_vertex_constants[1].data(), 32 );
    CHECK( resident_constants == std::array<float, 8>{ 4, 2, -3, 0, 16, 240, 10001, 0 } );
    CHECK( dynamic_constants == std::array<float, 8>{ 0, 0, 0, 0, 16, 240, 10001, 0 } );
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

TEST_CASE( "fp3d_overhead_open_contents_fade_and_pointer_stays_on_avatar_storey",
           "[fp3d][overhead][open_structure]" )
{
    using namespace fp3d_gpu_world_qa;
    fp3d_snapshot::scene_snapshot scene;
    scene.origin = tripoint_abs_ms( 0, 0, 4 );
    for( int z = 4; z <= 5; ++z ) {
        auto &tile = scene.cells.emplace_back();
        tile.position = tripoint_abs_ms( 0, 0, z );
        tile.terrain = "t_floor";
        tile.furniture = fp3d_furniture::describe( "f_table" );
        REQUIRE( tile.furniture );
        tile.open_structure_above = z > 4;
        auto &item = scene.sprites.emplace_back();
        item.position = tile.position;
        item.kind = fp3d_snapshot::sprite_kind::item;
        item.id = "rock";
        item.height = .5f;
        item.elevation = 1.25f;
    }
    const fp3d_camera::offset eye{ 0.f, 0.f, 12.f };
    const auto unrestricted = fp3d_targeting::ray( scene, 0, 89, eye, 20, 0, 0, 1, true );
    const auto pointer = fp3d_targeting::ray( scene, 0, 89, eye, 20, 0, 0, 1, true, true );
    REQUIRE( unrestricted );
    REQUIRE( pointer );
    CHECK( unrestricted->position == scene.cells.back().position );
    CHECK( pointer->position == scene.origin );
    CHECK( pointer->type != fp3d_targeting::kind::terrain );

    const auto view = fp3d_camera::orbit_pose( {}, 0, 0 );
    const auto near = prepare( scene, view, true, 512, 512, true, false, false, false, nullptr,
                               nullptr, nullptr, false, nullptr, nullptr, .1f );
    const auto far = prepare( scene, view, true, 512, 512, true );
    REQUIRE( near );
    REQUIRE( far );
    const auto has_draw = []( const prepared_world &world, Uint32 begin, Uint32 end,
    float height, float opacity, bool furniture ) {
        for( Uint32 i = begin; i < end; ++i ) {
            const auto &draw = world.batches[i];
            for( Uint32 n = 0; n < draw.instance_count; ++n ) {
                const auto &instance = world.instances.at( draw.first_instance + n );
                if( instance.offset[1] == Approx( height ) && instance.tint[3] == Approx( opacity ) &&
                    ( draw.index_count > 6 ) == furniture ) {
                    return true;
                }
            }
        }
        return false;
    };
    const float upper = fp3d_camera::floor_height;
    CHECK( has_draw( *near, near->badge_end, near->blend_end, upper, .1f, true ) );
    CHECK( has_draw( *near, near->badge_end, near->blend_end, upper + 1.25f, .1f, false ) );
    CHECK( has_draw( *near, 0, near->opaque_batches, 0.f, 1.f, true ) );
    CHECK( has_draw( *near, 0, near->opaque_batches, 1.25f, 1.f, false ) );
    CHECK( has_draw( *far, 0, far->opaque_batches, upper, 1.f, true ) );
    CHECK( has_draw( *far, 0, far->opaque_batches, upper + 1.25f, 1.f, false ) );
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
