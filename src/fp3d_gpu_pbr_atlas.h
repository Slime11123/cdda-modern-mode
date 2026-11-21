#pragma once
#ifndef CATA_SRC_FP3D_GPU_PBR_ATLAS_H
#define CATA_SRC_FP3D_GPU_PBR_ATLAS_H

#include <SDL3/SDL_gpu.h>
#include <array>

#include "fp3d_materials.h"

namespace fp3d_gpu
{
// The world submission fence also retires these small colour-atlas downloads.
class auxiliary_readback
{
    public:
        explicit auxiliary_readback( SDL_GPUDevice *device ) : device_( device ) {}
        auxiliary_readback( const auxiliary_readback & ) = delete;
        auxiliary_readback &operator=( const auxiliary_readback & ) = delete;
        ~auxiliary_readback() {
            if( transfer_ ) {
                SDL_ReleaseGPUTransferBuffer( device_, transfer_ );
            }
        }

        bool prepare( const std::vector<unsigned> &slots ) {
            if( slots.empty() || slots.size() > fp3d_materials::auxiliary_read_limit ||
                !pending_.empty() ) {
                return false;
            }
            if( !transfer_ ) {
                const SDL_GPUTransferBufferCreateInfo info{
                    SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                    fp3d_materials::auxiliary_read_limit * tile_bytes, 0
                };
                transfer_ = SDL_CreateGPUTransferBuffer( device_, &info );
            }
            return transfer_ != nullptr;
        }

        void record( SDL_GPUCopyPass *copy, SDL_GPUTexture *atlas,
                     const std::vector<unsigned> &slots ) const {
            for( size_t i = 0; i < slots.size(); ++i ) {
                const unsigned slot = slots[i];
                SDL_GPUTextureRegion from{};
                from.texture = atlas;
                from.x = ( slot % 32 ) * tile_size;
                from.y = ( slot / 32 ) * tile_size;
                from.w = from.h = tile_size;
                from.d = 1;
                const SDL_GPUTextureTransferInfo to{ transfer_, static_cast<Uint32>( i ) * tile_bytes,
                                                     tile_size, tile_size };
                SDL_DownloadFromGPUTexture( copy, &from, &to );
            }
        }

        void submitted( unsigned revision, const std::vector<unsigned> &slots ) {
            revision_ = revision;
            pending_ = slots;
        }

        template<typename Consume>
        void collect( SDL_GPUFence *fence, unsigned current_revision, const Consume &consume ) {
            if( pending_.empty() || !fence || !SDL_QueryGPUFence( device_, fence ) ) {
                return;
            }
            if( revision_ == current_revision ) {
                const auto *pixels = static_cast<const Uint8 *>(
                                         SDL_MapGPUTransferBuffer( device_, transfer_, false ) );
                if( pixels ) {
                    for( size_t i = 0; i < pending_.size(); ++i ) {
                        consume( revision_, pending_[i], pixels + i * tile_bytes );
                    }
                    SDL_UnmapGPUTransferBuffer( device_, transfer_ );
                }
            }
            pending_.clear();
        }

    private:
        static constexpr Uint32 tile_size = 64;
        static constexpr Uint32 tile_bytes = tile_size * tile_size * 4;
        SDL_GPUDevice *device_;
        SDL_GPUTransferBuffer *transfer_ = nullptr;
        unsigned revision_ = 0;
        std::vector<unsigned> pending_;
};

// An RGBA8 texture aligned with the colour atlas: tangent XY, roughness, metalness.
class pbr_atlas
{
    public:
        pbr_atlas() = default;
        pbr_atlas( const pbr_atlas & ) = delete;
        pbr_atlas &operator=( const pbr_atlas & ) = delete;
        ~pbr_atlas() { reset(); }

        bool stage( SDL_GPUDevice *device, unsigned revision,
                    const std::vector<fp3d_materials::auxiliary_tile> &tiles ) {
            if( revision_ != revision || ( device_ && device_ != device ) ) {
                reset();
                revision_ = revision;
            }
            pending_.clear();
            for( const auto &tile : tiles ) {
                if( tile.slot < uploaded_.size() && !uploaded_[tile.slot] && tile.data &&
                    tile.data->width == tile_size && tile.data->height == tile_size &&
                    tile.data->normal_rgba.size() == tile_bytes && tile.data->orm_rgba.size() == tile_bytes ) {
                    pending_.push_back( tile );
                    if( pending_.size() == upload_limit ) {
                        break;
                    }
                }
            }
            if( pending_.empty() ) {
                return initialized_;
            }
            if( !texture_ && !allocate( device ) ) {
                return false;
            }
            auto *pixels = static_cast<Uint8 *>( SDL_MapGPUTransferBuffer( device_, transfer_, false ) );
            if( !pixels ) {
                return false;
            }
            for( size_t i = 0; i < pending_.size(); ++i ) {
                auto *target = pixels + tile_offset( i );
                const auto &maps = *pending_[i].data;
                for( Uint32 j = 0; j < tile_bytes; j += 4 ) {
                    target[j] = maps.normal_rgba[j];
                    target[j + 1] = maps.normal_rgba[j + 1];
                    target[j + 2] = maps.orm_rgba[j + 1];
                    target[j + 3] = maps.orm_rgba[j + 2];
                }
            }
            SDL_UnmapGPUTransferBuffer( device_, transfer_ );
            return true;
        }

        // Initialize the neutral texels on the GPU before copying material tiles.
        bool record_clear( SDL_GPUCommandBuffer *cmd ) const {
            if( initialized_ ) {
                return true;
            }
            SDL_GPUColorTargetInfo color{};
            color.texture = texture_;
            color.clear_color = { 128.f / 255.f, 128.f / 255.f, 0.f, 0.f };
            color.load_op = SDL_GPU_LOADOP_CLEAR;
            color.store_op = SDL_GPU_STOREOP_STORE;
            auto *pass = SDL_BeginGPURenderPass( cmd, &color, 1, nullptr );
            if( !pass ) {
                return false;
            }
            SDL_EndGPURenderPass( pass );
            return true;
        }

        void record( SDL_GPUCopyPass *copy ) const {
            for( size_t i = 0; i < pending_.size(); ++i ) {
                const unsigned slot = pending_[i].slot;
                upload( copy, tile_offset( i ), tile_size,
                        ( slot % slots_per_row ) * tile_size, ( slot / slots_per_row ) * tile_size );
            }
        }

        void submitted() {
            initialized_ = true;
            for( const auto &tile : pending_ ) {
                uploaded_[tile.slot] = true;
            }
            pending_.clear();
        }

        SDL_GPUTexture *texture() const { return texture_; }
        Uint32 upload_bytes() const { return static_cast<Uint32>( pending_.size() ) * tile_bytes; }

    private:
        static constexpr Uint32 atlas_size = 2048;
        static constexpr Uint32 tile_size = 64;
        static constexpr Uint32 slots_per_row = atlas_size / tile_size;
        static constexpr Uint32 tile_bytes = tile_size * tile_size * 4;
        static constexpr Uint32 upload_limit = 8;

        static Uint32 tile_offset( size_t i ) {
            return static_cast<Uint32>( i ) * tile_bytes;
        }

        bool allocate( SDL_GPUDevice *device ) {
            device_ = device;
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            info.width = info.height = atlas_size;
            info.layer_count_or_depth = 1;
            info.num_levels = 1;
            texture_ = SDL_CreateGPUTexture( device_, &info );
            SDL_GPUTransferBufferCreateInfo transfer{};
            transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transfer.size = upload_limit * tile_bytes;
            transfer_ = texture_ ? SDL_CreateGPUTransferBuffer( device_, &transfer ) : nullptr;
            if( !transfer_ ) {
                reset();
                return false;
            }
            return true;
        }

        void upload( SDL_GPUCopyPass *copy, Uint32 offset, Uint32 size, Uint32 x, Uint32 y ) const {
            const SDL_GPUTextureTransferInfo source{ transfer_, offset, size, size };
            SDL_GPUTextureRegion dest{};
            dest.texture = texture_;
            dest.x = x;
            dest.y = y;
            dest.w = dest.h = size;
            dest.d = 1;
            SDL_UploadToGPUTexture( copy, &source, &dest, false );
        }

        void reset() {
            if( texture_ ) { SDL_ReleaseGPUTexture( device_, texture_ ); }
            if( transfer_ ) { SDL_ReleaseGPUTransferBuffer( device_, transfer_ ); }
            texture_ = nullptr;
            transfer_ = nullptr;
            device_ = nullptr;
            initialized_ = false;
            uploaded_.fill( false );
        }

        SDL_GPUDevice *device_ = nullptr;
        SDL_GPUTexture *texture_ = nullptr;
        SDL_GPUTransferBuffer *transfer_ = nullptr;
        unsigned revision_ = 0;
        bool initialized_ = false;
        std::array<bool, slots_per_row * slots_per_row> uploaded_{};
        std::vector<fp3d_materials::auxiliary_tile> pending_;
};
}
#endif
