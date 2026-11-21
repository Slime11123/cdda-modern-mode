#pragma once
#ifndef CATA_SRC_FP3D_GPU_TERRAIN_COMPUTE_H
#define CATA_SRC_FP3D_GPU_TERRAIN_COMPUTE_H

#include <SDL3/SDL_gpu.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "fp3d_gpu_camera_math.h"
#include "fp3d_geometry.h"

namespace fp3d_gpu
{
// Experimental layout for opaque terrain only. The CPU chooses the candidates,
// supplies conservative bounds and retains ownership of game visibility.
namespace terrain_compute
{
inline constexpr Uint32 cells_per_block = 64;
inline constexpr Uint32 max_blocks = 512;
inline constexpr Uint32 threads_per_group = 32;

using vec4 = std::array<float, 4>;
struct cell {
    std::array<float, 3> position;
    float padding = 0;
    vec4 tint{ 1, 1, 1, 1 };
};
struct model {
    vec4 center;
    vec4 half_extent;
    vec4 uv_rect;
    float mode;
    Uint32 first_index;
    Uint32 index_count;
    Uint32 padding = 0;
};
struct block {
    vec4 center;
    vec4 half_extent;
    Uint32 first_cell;
    Uint32 cell_count;
    Uint32 model_index;
    Uint32 revision;
};
struct camera {
    std::array<vec4, 6> planes;
    Uint32 cell_count;
    Uint32 model_count;
    Uint32 block_count;
    Uint32 revision;
};
static_assert( sizeof( cell ) == 32 && sizeof( model ) == 64 && sizeof( block ) == 48 &&
               sizeof( camera ) == 112 && sizeof( SDL_GPUIndexedIndirectDrawCommand ) == 20 );

// Derive the bound from the indexed model rather than assuming every wall
// fits its tile. Ledges and overhanging faces must survive at frustum edges.
inline std::optional<model> make_model( const fp3d_geometry::mesh &mesh, Uint32 first_index,
                                        Uint32 index_count, const vec4 &uv, float mode )
{
    if( !index_count || index_count % 3 || first_index > mesh.indices.size() ||
        index_count > mesh.indices.size() - first_index ) {
        return std::nullopt;
    }
    model result{ {}, {}, uv, mode, first_index, index_count };
    for( size_t axis = 0; axis < 3; ++axis ) {
        float low = std::numeric_limits<float>::infinity();
        float high = -low;
        for( size_t i = 0; i < index_count; ++i ) {
            const auto index = mesh.indices[first_index + i];
            if( index >= mesh.vertices.size() || !std::isfinite( mesh.vertices[index].position[axis] ) ) {
                return std::nullopt;
            }
            low = std::min( low, mesh.vertices[index].position[axis] );
            high = std::max( high, mesh.vertices[index].position[axis] );
        }
        result.center[axis] = low * .5f + high * .5f;
        result.half_extent[axis] = high * .5f - low * .5f;
    }
    return result;
}

inline std::optional<block> make_block( const std::vector<cell> &cells, Uint32 first_cell,
                                       Uint32 cell_count, const model &shape, Uint32 model_index,
                                       Uint32 revision )
{
    if( !cell_count || cell_count > cells_per_block || first_cell > cells.size() ||
        cell_count > cells.size() - first_cell ) {
        return std::nullopt;
    }
    block result{ {}, {}, first_cell, cell_count, model_index, revision };
    auto low = cells[first_cell].position;
    auto high = low;
    for( size_t i = 1; i < cell_count; ++i ) {
        const auto &position = cells[first_cell + i].position;
        for( size_t axis = 0; axis < 3; ++axis ) {
            low[axis] = std::min( low[axis], position[axis] );
            high[axis] = std::max( high[axis], position[axis] );
        }
    }
    for( size_t axis = 0; axis < 3; ++axis ) {
        const float low_half = .5f * low[axis];
        const float high_half = .5f * high[axis];
        result.center[axis] = low_half + high_half + shape.center[axis];
        result.half_extent[axis] = high_half - low_half + shape.half_extent[axis];
    }
    return result;
}

inline std::array<vec4, 6> clip_planes( const fp3d_gpu_camera_math::matrix4 &m )
{
    std::array<vec4, 6> planes{};
    for( size_t i = 0; i < 4; ++i ) {
        const float w = m.elements[12 + i];
        planes[0][i] = w + m.elements[i];
        planes[1][i] = w - m.elements[i];
        planes[2][i] = w + m.elements[4 + i];
        planes[3][i] = w - m.elements[4 + i];
        planes[4][i] = m.elements[8 + i];
        planes[5][i] = w - m.elements[8 + i];
    }
    return planes;
}

inline bool intersects( const std::array<vec4, 6> &planes, const vec4 &center,
                        const vec4 &half_extent )
{
    for( const auto &plane : planes ) {
        float edge = plane[3];
        for( size_t i = 0; i < 3; ++i ) {
            edge += plane[i] * center[i] + std::abs( plane[i] ) * half_extent[i];
        }
        if( edge < -.001f ) {
            return false;
        }
    }
    return true;
}

inline SDL_GPUComputePipeline *create_pipeline( SDL_GPUDevice *device, const Uint8 *code,
        size_t size )
{
    SDL_GPUComputePipelineCreateInfo info{};
    info.code = code;
    info.code_size = size;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXBC;
    info.num_readonly_storage_buffers = 3;
    info.num_readwrite_storage_buffers = 2;
    info.num_uniform_buffers = 1;
    info.threadcount_x = threads_per_group;
    info.threadcount_y = info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline( device, &info );
}

inline bool record( SDL_GPUCommandBuffer *cmd, SDL_GPUComputePipeline *pipeline, const camera &view,
                    const std::array<SDL_GPUBuffer *, 3> &inputs, SDL_GPUBuffer *instances,
                    SDL_GPUBuffer *draws )
{
    if( !cmd || !pipeline || !inputs[0] || !inputs[1] || !inputs[2] || !instances || !draws ||
        !view.block_count || view.block_count > max_blocks ) {
        return SDL_SetError( "Invalid experimental terrain compute dispatch" );
    }
    const SDL_GPUStorageBufferReadWriteBinding outputs[]{ { instances, false, 0, 0, 0 },
        { draws, false, 0, 0, 0 }
    };
    auto *pass = SDL_BeginGPUComputePass( cmd, nullptr, 0, outputs, 2 );
    if( !pass ) {
        return false;
    }
    SDL_BindGPUComputePipeline( pass, pipeline );
    SDL_BindGPUComputeStorageBuffers( pass, 0, inputs.data(), inputs.size() );
    SDL_PushGPUComputeUniformData( cmd, 0, &view, sizeof( view ) );
    SDL_DispatchGPUCompute( pass, ( view.block_count + threads_per_group - 1 ) / threads_per_group,
                            1, 1 );
    SDL_EndGPUComputePass( pass );
    return true;
}
} // namespace terrain_compute
} // namespace fp3d_gpu
#endif // CATA_SRC_FP3D_GPU_TERRAIN_COMPUTE_H
