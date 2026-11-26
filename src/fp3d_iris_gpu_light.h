#pragma once
#ifndef CATA_SRC_FP3D_IRIS_GPU_LIGHT_H
#define CATA_SRC_FP3D_IRIS_GPU_LIGHT_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdint>
#include <vector>

#include "fp3d_gpu_buffer.h"

namespace fp3d_iris
{
// Retain the volume through commit_delta(), and the upload buffer until the
// submitted frame retires. A native frame does not allocate this buffer.
inline bool stage_light_volume( SDL_GPUDevice *device, fp3d_gpu::upload_buffer &buffer,
                                const std::vector<std::uint32_t> &volume )
{
    constexpr Uint32 limit = 16 * 1024 * 1024;
    if( volume.empty() || volume.size() > limit / sizeof( std::uint32_t ) ) {
        return SDL_SetError( "Invalid Iris light volume size" );
    }
    const auto bytes = static_cast<Uint32>( volume.size() * sizeof( std::uint32_t ) );
    return buffer.reserve( device, bytes,
                           SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
                           limit ) && buffer.stage_delta( volume.data(), bytes );
}
} // namespace fp3d_iris
#endif
#endif
