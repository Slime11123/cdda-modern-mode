#pragma once
#ifndef CATA_SRC_FP3D_IRIS_GPU_GRAPH_H
#define CATA_SRC_FP3D_IRIS_GPU_GRAPH_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <array>
#include <cmath>
#include <cstdint>
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
};
using fullscreen_stage = graphics_stage;

struct compute_stage {
    SDL_GPUComputePipeline *pipeline = nullptr;
    std::vector<SDL_GPUSampler *> samplers;
    std::vector<std::uint8_t> constants;
    std::array<Uint32, 3> groups{};
};

// The caller owns pipelines, textures and command submission. On failure,
// discard the command buffer. This recorder never waits or reads back.
inline bool record_gpu_graph( SDL_GPUCommandBuffer *command,
        const std::vector<std::string> &external, const std::vector<graph_pass> &passes,
        const std::string &frame_output, const std::map<std::string, SDL_GPUTexture *> &images,
        const std::map<std::string, graphics_stage> &stages,
        const std::map<std::string, compute_stage> &compute = {},
        const std::map<std::string, graph_image_spec> &image_specs = {} )
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
                if( ( pass.kind == pass_kind::geometry && ( draws.empty() ||
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
        for( const auto *sampler : *samplers ) {
            if( !sampler ) {
                return SDL_SetError( "Missing Iris sampler for %s", pass.program.c_str() );
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
        const auto &samplers = pass.kind == pass_kind::compute ?
                               compute.at( pass.program ).samplers : stages.at( pass.program ).samplers;
        for( size_t index = 0; index < pass.reads.size(); ++index ) {
            bindings.push_back( { lookup( pass.reads[index] ), samplers[index] } );
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
        const auto &stage = stages.at( pass.program );
        if( !pass.color_loads.empty() || !pass.depth_load.empty() ) {
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
            SDL_EndGPUCopyPass( copy );
        }
        std::vector<SDL_GPUColorTargetInfo> targets;
        for( const auto &name : pass.writes ) {
            SDL_GPUColorTargetInfo target{};
            target.texture = lookup( name );
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
        if( const auto viewport = viewports.find( pass.program ); viewport != viewports.end() ) {
            SDL_SetGPUViewport( render, &viewport->second );
        }
        if( !bindings.empty() ) {
            SDL_BindGPUFragmentSamplers( render, 0, bindings.data(), bindings.size() );
        }
        if( !stage.fragment_constants.empty() ) {
            SDL_PushGPUFragmentUniformData( command, 0, stage.fragment_constants.data(),
                                           stage.fragment_constants.size() );
        }
        if( pass.kind == pass_kind::geometry ) {
            for( const auto &draw : stage.draws ) {
                if( !draw.record( render ) ) {
                    SDL_EndGPURenderPass( render );
                    return false;
                }
            }
        } else {
            SDL_DrawGPUPrimitives( render, 3, 1, 0, 0 );
        }
        SDL_EndGPURenderPass( render );
    }
    return true;
}
} // namespace fp3d_iris

#endif
