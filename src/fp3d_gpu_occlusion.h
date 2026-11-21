#pragma once
#ifndef CATA_SRC_FP3D_GPU_OCCLUSION_H
#define CATA_SRC_FP3D_GPU_OCCLUSION_H

#include <SDL3/SDL_gpu.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "fp3d_gpu_camera_math.h"

namespace fp3d_gpu::occlusion
{
inline constexpr Uint32 tile_size = 8;
inline constexpr Uint32 max_extent = 4096;
inline constexpr Uint32 max_draws = 8192;

struct candidate {
    Uint32 left = 0;
    Uint32 top = 0;
    Uint32 right = 0;
    Uint32 bottom = 0;
    float near_depth = 0;
    Uint32 eligible = 0;
    Uint32 padding[2]{};
};

struct viewport {
    Uint32 width;
    Uint32 height;
    Uint32 draw_count;
    Uint32 padding = 0;
};
static_assert( sizeof( candidate ) == 32 && sizeof( viewport ) == 16 &&
               sizeof( SDL_GPUIndexedIndirectDrawCommand ) == 20 );

inline Uint32 tile_count( Uint32 extent )
{
    return ( extent + tile_size - 1 ) / tile_size;
}

// Only opaque candidates with a complete, projectable bound use the result.
// Uncertain projections are kept on the original draw path.
inline candidate project( const fp3d_gpu_camera_math::matrix4 &matrix,
                          const std::array<float, 3> &center, const std::array<float, 3> &extent,
                          const viewport &view )
{
    if( !view.width || !view.height || view.width > max_extent || view.height > max_extent ||
        !std::all_of( extent.begin(), extent.end(), []( float value ) {
            return std::isfinite( value ) && value >= 0;
        } ) ) {
        return {};
    }
    float left = std::numeric_limits<float>::infinity(), top = left;
    float right = -left, bottom = right, near_depth = 1.f;
    for( unsigned corner = 0; corner < 8; ++corner ) {
        const auto coordinate = [&]( unsigned axis ) {
            return center[axis] + ( corner & ( 1u << axis ) ? extent[axis] : -extent[axis] );
        };
        const auto clip = matrix.transform( coordinate( 0 ), coordinate( 1 ), coordinate( 2 ) );
        if( !std::all_of( clip.begin(), clip.end(), []( float value ) { return std::isfinite( value ); } ) ||
            clip[3] <= .00001f || clip[2] <= 0 || clip[2] > clip[3] ) {
            return {};
        }
        const float x = ( .5f + .5f * clip[0] / clip[3] ) * view.width;
        const float y = ( .5f - .5f * clip[1] / clip[3] ) * view.height;
        left = std::min( left, x );
        top = std::min( top, y );
        right = std::max( right, x );
        bottom = std::max( bottom, y );
        near_depth = std::min( near_depth, clip[2] / clip[3] );
    }
    if( left < 1 || top < 1 || right >= view.width - 1 || bottom >= view.height - 1 ) {
        return {};
    }
    return { static_cast<Uint32>( std::floor( left - 1 ) ),
             static_cast<Uint32>( std::floor( top - 1 ) ),
             static_cast<Uint32>( std::ceil( right ) ),
             static_cast<Uint32>( std::ceil( bottom ) ), near_depth, 1 };
}

inline SDL_GPUComputePipeline *create_pipeline( SDL_GPUDevice *device, const Uint8 *code,
        size_t size, bool reduce )
{
    SDL_GPUComputePipelineCreateInfo info{};
    info.code = code;
    info.code_size = size;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXBC;
    info.num_samplers = reduce ? 1 : 0;
    info.num_readonly_storage_buffers = reduce ? 0 : 3;
    info.num_readwrite_storage_buffers = 1;
    info.num_uniform_buffers = 1;
    info.threadcount_x = reduce ? tile_size : 32;
    info.threadcount_y = reduce ? tile_size : 1;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline( device, &info );
}

struct resources {
    SDL_GPUComputePipeline *reduce;
    SDL_GPUComputePipeline *filter;
    SDL_GPUTexture *depth;
    SDL_GPUSampler *sampler;
    SDL_GPUBuffer *tiles;
    SDL_GPUBuffer *candidates;
    SDL_GPUBuffer *input_draws;
    SDL_GPUBuffer *output_draws;
};

// Depth must come from the current opaque pass, cleared to 1 for uncovered pixels.
// Input commands remain available for the caller's fallback.
inline bool record( SDL_GPUCommandBuffer *cmd, const viewport &view, const resources &gpu )
{
    if( !cmd || !view.width || !view.height || view.width > max_extent || view.height > max_extent ||
        !view.draw_count || view.draw_count > max_draws || !gpu.reduce || !gpu.filter || !gpu.depth ||
        !gpu.sampler || !gpu.tiles || !gpu.candidates || !gpu.input_draws || !gpu.output_draws ||
        gpu.input_draws == gpu.output_draws ) {
        return SDL_SetError( "Invalid FP3D occlusion dispatch" );
    }
    const SDL_GPUStorageBufferReadWriteBinding tile_output{ gpu.tiles, false, 0, 0, 0 };
    auto *reduce = SDL_BeginGPUComputePass( cmd, nullptr, 0, &tile_output, 1 );
    if( !reduce ) {
        return false;
    }
    SDL_BindGPUComputePipeline( reduce, gpu.reduce );
    const SDL_GPUTextureSamplerBinding depth_binding{ gpu.depth, gpu.sampler };
    SDL_BindGPUComputeSamplers( reduce, 0, &depth_binding, 1 );
    SDL_PushGPUComputeUniformData( cmd, 0, &view, sizeof( view ) );
    SDL_DispatchGPUCompute( reduce, tile_count( view.width ), tile_count( view.height ), 1 );
    SDL_EndGPUComputePass( reduce );

    const SDL_GPUStorageBufferReadWriteBinding draw_output{ gpu.output_draws, false, 0, 0, 0 };
    auto *filter = SDL_BeginGPUComputePass( cmd, nullptr, 0, &draw_output, 1 );
    if( !filter ) {
        return false;
    }
    SDL_BindGPUComputePipeline( filter, gpu.filter );
    const std::array<SDL_GPUBuffer *, 3> inputs{ gpu.tiles, gpu.candidates, gpu.input_draws };
    SDL_BindGPUComputeStorageBuffers( filter, 0, inputs.data(), inputs.size() );
    SDL_DispatchGPUCompute( filter, ( view.draw_count + 31 ) / 32, 1, 1 );
    SDL_EndGPUComputePass( filter );
    return true;
}
} // namespace fp3d_gpu::occlusion
#endif // CATA_SRC_FP3D_GPU_OCCLUSION_H
