#pragma once
#ifndef CATA_SRC_FP3D_GPU_TARGET_H
#define CATA_SRC_FP3D_GPU_TARGET_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

namespace fp3d_gpu
{
// SDL owns the color texture and its native GPU handle. The depth texture
// belongs to this object. A common GPU device allows 3D/2D composition without
// downloading pixels or sharing an OpenGL context.
//
// Lifetime: keep the renderer/device alive until reset()/destruction. Before
// resetting or replacing an in-flight target, flush the renderer and retire
// the frame fences. This class never stalls the GPU itself.
class scene_target
{
    public:
        scene_target() = default;
        scene_target( const scene_target & ) = delete;
        scene_target &operator=( const scene_target & ) = delete;
        ~scene_target() {
            reset();
        }

        bool initialize( SDL_Renderer *renderer, int width, int height, bool sample_depth = false ) {
            if( color_ || depth_ || !renderer || width <= 0 || height <= 0 ) {
                return SDL_SetError( "Invalid or already initialized FP3D GPU target" );
            }
            device_ = SDL_GetGPURendererDevice( renderer );
            if( !device_ ) {
                return SDL_SetError( "FP3D requires an SDL GPU renderer" );
            }
            color_ = SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_TARGET, width, height );
            if( !color_ ) {
                reset();
                return false;
            }
            native_ = static_cast<SDL_GPUTexture *>( SDL_GetPointerProperty(
                          SDL_GetTextureProperties( color_ ),
                          SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr ) );
            if( !native_ ) {
                reset();
                return SDL_SetError( "FP3D GPU texture interop unavailable" );
            }
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = depth_format();
            info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            info.width = static_cast<Uint32>( width );
            info.height = static_cast<Uint32>( height );
            info.layer_count_or_depth = 1;
            info.num_levels = 1;
            if( sample_depth ) {
                const auto usage = info.usage | SDL_GPU_TEXTUREUSAGE_SAMPLER;
                if( SDL_GPUTextureSupportsFormat( device_, info.format, info.type, usage ) ) {
                    info.usage = usage;
                    depth_ = SDL_CreateGPUTexture( device_, &info );
                    sampleable_depth_ = depth_ != nullptr;
                }
            }
            if( !depth_ ) {
                info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
                depth_ = SDL_CreateGPUTexture( device_, &info );
            }
            if( !depth_ || !SDL_SetTextureBlendMode( color_, SDL_BLENDMODE_NONE ) ) {
                reset();
                return false;
            }
            return true;
        }

        void reset() {
            if( depth_ ) {
                SDL_ReleaseGPUTexture( device_, depth_ );
            }
            if( color_ ) {
                SDL_DestroyTexture( color_ );
            }
            depth_ = nullptr;
            sampleable_depth_ = false;
            color_ = nullptr;
            native_ = nullptr;
            device_ = nullptr;
        }

        static constexpr SDL_GPUTextureFormat color_format() {
            return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        }
        static constexpr SDL_GPUTextureFormat depth_format() {
            return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        }
        SDL_Texture *color() const {
            return color_;
        }
        SDL_GPUTexture *native_color() const {
            return native_;
        }
        SDL_GPUTexture *depth() const {
            return depth_;
        }
        bool sampleable_depth() const {
            return sampleable_depth_;
        }
        SDL_GPUDevice *device() const {
            return device_;
        }

    private:
        SDL_GPUDevice *device_ = nullptr; // borrowed from the SDL renderer
        SDL_Texture *color_ = nullptr; // owned SDL wrapper
        SDL_GPUTexture *native_ = nullptr; // borrowed from color_
        SDL_GPUTexture *depth_ = nullptr; // owned GPU texture
        bool sampleable_depth_ = false;
};
} // namespace fp3d_gpu
#endif // CATA_SRC_FP3D_GPU_TARGET_H
