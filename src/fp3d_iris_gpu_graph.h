#pragma once
#ifndef CATA_SRC_FP3D_IRIS_GPU_GRAPH_H
#define CATA_SRC_FP3D_IRIS_GPU_GRAPH_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "fp3d_iris_gpu_images.h"
#include "fp3d_gpu_mesh.h"

namespace fp3d_iris
{
struct graphics_stage {
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    // Same order as graph_pass::reads, matching the compiled shader slots.
    std::vector<SDL_GPUSampler *> samplers;
    std::vector<std::uint8_t> fragment_constants;
    // A geometry stage consumes resident native meshes instead of an implicit triangle.
    std::vector<fp3d_gpu::indexed_draw> draws{};
    std::vector<std::uint8_t> vertex_constants{};
    std::vector<SDL_GPUSampler *> vertex_samplers{};
    // Empty uses the stage-wide constants. Otherwise every indexed draw has
    // its own complete vertex constant block, in draw order.
    std::vector<std::vector<std::uint8_t>> draw_vertex_constants{};
    std::vector<std::vector<std::uint8_t>> draw_fragment_constants{};
    bool allow_empty_geometry = false;
    size_t readonly_textures = 0;
};
using fullscreen_stage = graphics_stage;

struct compute_stage {
    SDL_GPUComputePipeline *pipeline = nullptr;
    std::vector<SDL_GPUSampler *> samplers;
    std::vector<std::uint8_t> constants;
    std::array<Uint32, 3> groups{};
};

#if defined(TILES) && defined(ENABLE_FP3D)
inline bool has_pack_display_output( const pack_fullscreen_graph &plan )
{
    if( plan.error.empty() && !plan.passes.empty() ) {
        const auto &last = plan.passes.back();
        if( last.program == "final" && last.kind == pass_kind::fullscreen &&
            std::find( last.writes.begin(), last.writes.end(), plan.output ) != last.writes.end() ) {
            return true;
        }
    }
    return SDL_SetError( "Iris frame requires a final color output" );
}

inline bool record_pack_display( SDL_GPUCommandBuffer *command, const pack_fullscreen_graph &plan,
                                 const pack_graph_images &frame, const fp3d_gpu::scene_target &target )
{
    if( !has_pack_display_output( plan ) ) {
        return false;
    }
    const auto source = frame.textures.images().find( plan.output );
    const auto spec = frame.specs.find( plan.output );
    if( !command || !target.native_color() || source == frame.textures.images().end() ||
        spec == frame.specs.end() || !source->second || source->second == target.native_color() ||
        spec->second.format != target.color_format() || spec->second.width != target.width() ||
        spec->second.height != target.height() ) {
        return SDL_SetError( "Invalid Iris display images" );
    }
    SDL_GPUBlitInfo blit{};
    blit.source.texture = source->second;
    blit.source.w = spec->second.width;
    blit.source.h = spec->second.height;
    blit.destination.texture = target.native_color();
    blit.destination.w = target.width();
    blit.destination.h = target.height();
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture( command, &blit );
    return true;
}
#endif

// The caller owns pipelines, textures and command submission. On failure,
// discard the command buffer. This recorder never waits or reads back.
inline bool record_gpu_graph( SDL_GPUCommandBuffer *command,
        const std::vector<std::string> &external, const std::vector<graph_pass> &passes,
        const std::string &frame_output, const std::map<std::string, SDL_GPUTexture *> &images,
        const std::map<std::string, graphics_stage> &stages,
        const std::map<std::string, compute_stage> &compute = {},
        const std::map<std::string, graph_image_spec> &image_specs = {},
        const std::function<bool( const graph_pass & )> &after_graphics = {},
        const std::function<bool( const graph_pass & )> &before_graphics = {},
        const std::map<std::string, graph_texture> &readonly_images = {} )
{
    const auto graph = validate_graph( external, passes, frame_output );
    if( !graph.error.empty() ) {
        return SDL_SetError( "%s", graph.error.c_str() );
    }
    if( !passes.empty() && !command ) {
        return SDL_SetError( "Missing Iris command buffer" );
    }
    const auto lookup = [&]( const std::string &name ) -> SDL_GPUTexture * {
        const auto image = images.find( name );
        return image == images.end() ? nullptr : image->second;
    };
    const auto compatible_load = [&]( const std::string &source, const std::string &destination ) {
        const auto from = image_specs.find( source );
        const auto to = image_specs.find( destination );
        return from != image_specs.end() && to != image_specs.end() &&
               from->second.width && from->second.height && from->second == to->second;
    };
    for( const auto &[name, index] : graph.last_use ) {
        static_cast<void>( index );
        if( !lookup( name ) ) {
            return SDL_SetError( "Missing Iris image: %s", name.c_str() );
        }
    }
    std::map<SDL_GPUTexture *, size_t> live_until;
    for( const auto &name : external ) {
        if( const auto use = graph.last_use.find( name ); use != graph.last_use.end() ) {
            auto &last = live_until[lookup( name )];
            last = SDL_max( last, use->second );
        }
    }
    std::map<std::string, SDL_GPUViewport> viewports;
    // Validate all stages and physical aliases before recording any commands.
    for( size_t index = 0; index < passes.size(); ++index ) {
        const auto &pass = passes[index];
        const std::vector<SDL_GPUSampler *> *samplers = nullptr;
        if( pass.kind == pass_kind::compute ) {
            const auto stage = compute.find( pass.program );
            if( stage != compute.end() && stage->second.pipeline && !pass.writes.empty() &&
                pass.depth_write.empty() && stage->second.groups[0] && stage->second.groups[1] &&
                stage->second.groups[2] ) {
                samplers = &stage->second.samplers;
            }
        } else {
            const auto stage = stages.find( pass.program );
            if( stage != stages.end() && stage->second.pipeline &&
                ( !pass.writes.empty() || !pass.depth_write.empty() ) && pass.writes.size() <= 8 ) {
                samplers = &stage->second.samplers;
                const auto &draws = stage->second.draws;
                for( const auto *constants : { &stage->second.draw_vertex_constants,
                                               &stage->second.draw_fragment_constants } ) {
                    if( !constants->empty() &&
                        ( pass.kind != pass_kind::geometry || constants->size() != draws.size() ||
                          std::any_of( constants->begin(), constants->end(), []( const auto &bytes ) {
                              return bytes.empty() || bytes.size() % 16 != 0 || bytes.size() > 65536;
                          } ) ) ) {
                        return SDL_SetError( "Invalid Iris per-draw constants in %s", pass.program.c_str() );
                    }
                }
                const auto &vertex = stage->second.vertex_samplers;
                if( vertex.size() != pass.vertex_reads.size() ||
                    std::find( vertex.begin(), vertex.end(), nullptr ) != vertex.end() ) {
                    return SDL_SetError( "Invalid Iris vertex samplers in %s", pass.program.c_str() );
                }
                if( ( pass.kind == pass_kind::geometry && ( ( draws.empty() && !stage->second.allow_empty_geometry ) ||
                      std::any_of( draws.begin(), draws.end(), []( const auto &draw ) {
                          return !draw.ready();
                      } ) ) ) || ( pass.kind == pass_kind::fullscreen && !draws.empty() ) ) {
                    return SDL_SetError( "Invalid Iris geometry in %s", pass.program.c_str() );
                }
            }
        }
        if( !samplers || samplers->size() != pass.reads.size() ) {
            return SDL_SetError( "Unsupported or incomplete Iris stage: %s", pass.program.c_str() );
        }
        if( pass.viewport ) {
            const auto &viewport = *pass.viewport;
            if( pass.kind != pass_kind::fullscreen || pass.writes.empty() ||
                !( viewport.scale > 0 && viewport.scale <= 1 &&
                   viewport.x >= 0 && viewport.x <= 1 && viewport.y >= 0 && viewport.y <= 1 ) ) {
                return SDL_SetError( "Unsupported Iris viewport in %s", pass.program.c_str() );
            }
            const auto spec = image_specs.find( pass.writes.front() );
            if( spec == image_specs.end() ) {
                return SDL_SetError( "Missing Iris viewport attachment size in %s", pass.program.c_str() );
            }
            const float width = static_cast<float>( spec->second.width );
            const float height = static_cast<float>( spec->second.height );
            const float w = std::trunc( width * viewport.scale );
            const float h = std::trunc( height * viewport.scale );
            const float x = std::trunc( width * viewport.x );
            const float bottom = std::trunc( height * viewport.y );
            if( w < 1 || h < 1 || x + w > width || bottom + h > height ) {
                return SDL_SetError( "Iris viewport exceeds its attachment in %s", pass.program.c_str() );
            }
            viewports.emplace( pass.program, SDL_GPUViewport{ x, height - bottom - h, w, h, 0, 1 } );
        }
        const size_t readonly = pass.kind == pass_kind::compute ? 0 :
                                stages.at( pass.program ).readonly_textures;
        if( readonly > samplers->size() ||
            std::any_of( samplers->begin(), samplers->end() - readonly, []( const auto *sampler ) {
                return sampler == nullptr;
            } ) ) {
            return SDL_SetError( "Missing Iris sampler for %s", pass.program.c_str() );
        }
        for( auto read = pass.reads.end() - readonly; read != pass.reads.end(); ++read ) {
            const auto copy = readonly_images.find( *read );
            if( copy == readonly_images.end() || !copy->second || !image_specs.count( *read ) ) {
                return SDL_SetError( "Missing Iris storage copy for %s: %s", pass.program.c_str(), read->c_str() );
            }
        }
        for( const auto &[destination, source] : pass.color_loads ) {
            if( !compatible_load( source, destination ) ) {
                return SDL_SetError( "Incompatible Iris color load for %s: %s", pass.program.c_str(),
                                     destination.c_str() );
            }
        }
        if( !pass.depth_load.empty() &&
            ( !compatible_load( pass.depth_load, pass.depth_write ) ||
              image_specs.at( pass.depth_write ).format != SDL_GPU_TEXTUREFORMAT_D32_FLOAT ) ) {
            return SDL_SetError( "Incompatible Iris depth load in %s: %s", pass.program.c_str(),
                                 pass.depth_write.c_str() );
        }
        for( const auto &name : graph_outputs( pass ) ) {
            auto *texture = lookup( name );
            const auto prior = live_until.find( texture );
            if( prior != live_until.end() && prior->second >= index ) {
                return SDL_SetError( "Iris stage %s writes a live texture: %s", pass.program.c_str(), name.c_str() );
            }
            live_until[texture] = graph.last_use.at( name );
        }
    }

    for( const auto &pass : passes ) {
        std::vector<SDL_GPUTextureSamplerBinding> bindings;
        std::vector<SDL_GPUTexture *> readonly_bindings;
        const auto &samplers = pass.kind == pass_kind::compute ?
                               compute.at( pass.program ).samplers : stages.at( pass.program ).samplers;
        const size_t readonly = pass.kind == pass_kind::compute ? 0 :
                                stages.at( pass.program ).readonly_textures;
        const size_t sampled = pass.reads.size() - readonly;
        for( size_t index = 0; index < sampled; ++index ) {
            bindings.push_back( { lookup( pass.reads[index] ), samplers[index] } );
        }
        for( size_t index = sampled; index < pass.reads.size(); ++index ) {
            readonly_bindings.push_back( readonly_images.at( pass.reads[index] ).get() );
        }
        if( pass.kind == pass_kind::compute ) {
            const auto &stage = compute.at( pass.program );
            std::vector<SDL_GPUStorageTextureReadWriteBinding> outputs;
            for( const auto &name : pass.writes ) {
                SDL_GPUStorageTextureReadWriteBinding output{};
                output.texture = lookup( name );
                outputs.push_back( output );
            }
            auto *work = SDL_BeginGPUComputePass( command, outputs.data(), outputs.size(), nullptr, 0 );
            if( !work ) {
                return false;
            }
            SDL_BindGPUComputePipeline( work, stage.pipeline );
            if( !bindings.empty() ) {
                SDL_BindGPUComputeSamplers( work, 0, bindings.data(), bindings.size() );
            }
            if( !stage.constants.empty() ) {
                SDL_PushGPUComputeUniformData( command, 0, stage.constants.data(), stage.constants.size() );
            }
            SDL_DispatchGPUCompute( work, stage.groups[0], stage.groups[1], stage.groups[2] );
            SDL_EndGPUComputePass( work );
            continue;
        }
        if( before_graphics && !before_graphics( pass ) ) {
            return false;
        }
        const auto &stage = stages.at( pass.program );
        if( !pass.color_loads.empty() || !pass.depth_load.empty() || !readonly_bindings.empty() ) {
            auto *copy = SDL_BeginGPUCopyPass( command );
            if( !copy ) {
                return false;
            }
            const auto copy_image = [&]( const std::string &source, const std::string &destination ) {
                const auto &spec = image_specs.at( destination );
                SDL_GPUTextureLocation from{};
                from.texture = lookup( source );
                SDL_GPUTextureLocation to{};
                to.texture = lookup( destination );
                SDL_CopyGPUTextureToTexture( copy, &from, &to, spec.width, spec.height, 1, false );
            };
            for( const auto &[destination, source] : pass.color_loads ) {
                copy_image( source, destination );
            }
            if( !pass.depth_load.empty() ) {
                copy_image( pass.depth_load, pass.depth_write );
            }
            for( size_t index = sampled; index < pass.reads.size(); ++index ) {
                const auto &name = pass.reads[index];
                const auto &spec = image_specs.at( name );
                SDL_GPUTextureLocation from{};
                from.texture = lookup( name );
                SDL_GPUTextureLocation to{};
                to.texture = readonly_bindings[index - sampled];
                SDL_CopyGPUTextureToTexture( copy, &from, &to, spec.width, spec.height, 1, false );
            }
            SDL_EndGPUCopyPass( copy );
        }
        std::vector<SDL_GPUColorTargetInfo> targets;
        for( const auto &name : pass.writes ) {
            SDL_GPUColorTargetInfo target{};
            target.texture = lookup( name );
            const auto spec = image_specs.find( name );
            if( spec != image_specs.end() ) {
                const auto &clear = spec->second.clear_color;
                target.clear_color = { clear[0], clear[1], clear[2], spec->second.opaque_alpha ? 1.f : clear[3] };
            }
            target.load_op = pass.color_loads.count( name ) ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            targets.push_back( target );
        }
        SDL_GPUDepthStencilTargetInfo depth{};
        if( !pass.depth_write.empty() ) {
            depth.texture = lookup( pass.depth_write );
            depth.clear_depth = 1.f;
            depth.load_op = pass.depth_load.empty() ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        }
        auto *render = SDL_BeginGPURenderPass( command, targets.data(), targets.size(),
                                              depth.texture ? &depth : nullptr );
        if( !render ) {
            return false;
        }
        SDL_BindGPUGraphicsPipeline( render, stage.pipeline );
        if( !stage.vertex_constants.empty() ) {
            SDL_PushGPUVertexUniformData( command, 0, stage.vertex_constants.data(),
                                         stage.vertex_constants.size() );
        }
        if( !pass.vertex_reads.empty() ) {
            std::vector<SDL_GPUTextureSamplerBinding> vertex_bindings;
            for( size_t index = 0; index < pass.vertex_reads.size(); ++index ) {
                vertex_bindings.push_back( { lookup( pass.vertex_reads[index] ), stage.vertex_samplers[index] } );
            }
            SDL_BindGPUVertexSamplers( render, 0, vertex_bindings.data(), vertex_bindings.size() );
        }
        if( const auto viewport = viewports.find( pass.program ); viewport != viewports.end() ) {
            SDL_SetGPUViewport( render, &viewport->second );
        }
        if( !bindings.empty() ) {
            SDL_BindGPUFragmentSamplers( render, 0, bindings.data(), bindings.size() );
        }
        if( !readonly_bindings.empty() ) {
            SDL_BindGPUFragmentStorageTextures( render, 0, readonly_bindings.data(),
                                               readonly_bindings.size() );
        }
        if( !stage.fragment_constants.empty() ) {
            SDL_PushGPUFragmentUniformData( command, 0, stage.fragment_constants.data(),
                                           stage.fragment_constants.size() );
        }
        if( pass.kind == pass_kind::geometry ) {
            for( size_t index = 0; index < stage.draws.size(); ++index ) {
                if( !stage.draw_vertex_constants.empty() ) {
                    const auto &bytes = stage.draw_vertex_constants[index];
                    SDL_PushGPUVertexUniformData( command, 0, bytes.data(), bytes.size() );
                }
                if( !stage.draw_fragment_constants.empty() ) {
                    const auto &bytes = stage.draw_fragment_constants[index];
                    SDL_PushGPUFragmentUniformData( command, 0, bytes.data(), bytes.size() );
                }
                if( !stage.draws[index].record( render ) ) {
                    SDL_EndGPURenderPass( render );
                    return false;
                }
            }
        } else {
            SDL_DrawGPUPrimitives( render, 3, 1, 0, 0 );
        }
        SDL_EndGPURenderPass( render );
        if( after_graphics && !after_graphics( pass ) ) {
            return false;
        }
    }
    return true;
}
} // namespace fp3d_iris

#endif
