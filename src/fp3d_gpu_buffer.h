#pragma once
#ifndef CATA_SRC_FP3D_GPU_BUFFER_H
#define CATA_SRC_FP3D_GPU_BUFFER_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

// Native GPU buffer and upload ownership. Each in-flight frame must retain
// its buffers until its fence retires. Record uploads before drawing.
namespace fp3d_gpu
{
struct delta_prefix {
    Uint32 size = 0;
    Uint32 block_size = 4096;
    // A shared identity is valid only for the exact immutable byte contents.
    std::shared_ptr<const void> revision{};
};

class upload_buffer
{
    public:
        upload_buffer() = default;
        upload_buffer( const upload_buffer & ) = delete;
        upload_buffer &operator=( const upload_buffer & ) = delete;
        ~upload_buffer() {
            reset();
        }

        bool initialize( SDL_GPUDevice *device, Uint32 capacity, SDL_GPUBufferUsageFlags usage ) {
            if( !device || !capacity || buffer_ || staging_ ) {
                return SDL_SetError( "Invalid or already allocated FP3D buffer" );
            }
            device_ = device;
            SDL_GPUBufferCreateInfo b{};
            b.size = capacity;
            b.usage = usage;
            buffer_ = SDL_CreateGPUBuffer( device_, &b );
            if( !buffer_ ) {
                reset();
                return false;
            }
            SDL_GPUTransferBufferCreateInfo t{};
            t.size = capacity;
            t.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            staging_ = SDL_CreateGPUTransferBuffer( device_, &t );
            if( !staging_ ) {
                reset();
                return false;
            }
            capacity_ = capacity;
            usage_ = usage;
            return true;
        }

        // Only grow an idle buffer. Allocate before releasing the old storage
        // so an allocation failure leaves the previous capacity intact.
        bool reserve( SDL_GPUDevice *device, Uint32 size, SDL_GPUBufferUsageFlags usage,
                      Uint32 capacity_limit = SDL_MAX_UINT32 ) {
            if( !size || size > capacity_limit ||
                ( device_ && ( device_ != device || usage_ != usage || capacity_ > capacity_limit ) ) ) {
                return SDL_SetError( "Invalid FP3D buffer reservation" );
            }
            if( size <= capacity_ ) {
                return true;
            }
            const Uint32 capacity = static_cast<Uint32>( SDL_min(
                                        capacity_limit, SDL_max( Uint64( size ), Uint64( capacity_ ) * 2 ) ) );
            upload_buffer replacement;
            if( !replacement.initialize( device, capacity, usage ) ) {
                return false;
            }
            reset();
            device_ = device;
            usage_ = usage;
            buffer_ = replacement.buffer_;
            staging_ = replacement.staging_;
            capacity_ = capacity;
            replacement.buffer_ = nullptr;
            replacement.staging_ = nullptr;
            return true;
        }

        // Map/write/unmap before opening the copy pass. Caller owns a frame
        // slot or otherwise ensures that the range is not reused in flight.
        bool stage( const void *data, Uint32 size ) {
            if( !staging_ || !data || !size || size > capacity_ ) {
                return SDL_SetError( "Invalid FP3D buffer upload" );
            }
            void *mapped = SDL_MapGPUTransferBuffer( device_, staging_, false );
            if( !mapped ) {
                return false;
            }
            SDL_memcpy( mapped, data, size );
            SDL_UnmapGPUTransferBuffer( device_, staging_ );
            staged_ = size;
            committed_.clear();
            committed_prefix_revision_.reset();
            pending_prefix_revision_.reset();
            base_ = nullptr;
            return true;
        }

        bool record( SDL_GPUCopyPass *pass, Uint32 destination_offset = 0 ) {
            if( !pass || !staged_ || destination_offset > capacity_ - staged_ ) {
                return SDL_SetError( "Invalid FP3D copy range" );
            }
            SDL_GPUTransferBufferLocation source{};
            source.transfer_buffer = staging_;
            SDL_GPUBufferRegion destination{};
            destination.buffer = buffer_;
            destination.offset = destination_offset;
            destination.size = staged_;
            SDL_UploadToGPUBuffer( pass, &source, &destination, false );
            staged_ = 0;
            return true;
        }
        // Copy only dirty runs in the aligned stream. A canceled command
        // buffer never changes the last committed CPU copy.
        bool stage_delta( const void *data, Uint32 size, const delta_prefix &prefix = {} ) {
            if( prefix.size > size ) {
                base_ = nullptr;
                return SDL_SetError( "Invalid FP3D retained upload prefix" );
            }
            const auto *source = static_cast<const Uint8 *>( data );
            return stage_delta_parts( source, prefix, source ? source + prefix.size : nullptr,
                                      size - prefix.size );
        }

        // Independent CPU spans share one GPU stream and the same copy budget.
        bool stage_delta_parts( const void *prefix_data, const delta_prefix &prefix, const void *suffix_data,
                                Uint32 suffix_size ) {
            base_ = nullptr;
            range_count_ = 0;
            staged_ = 0;
            if( !staging_ || prefix.size > capacity_ || suffix_size > capacity_ - prefix.size ||
                ( prefix.size == 0 && suffix_size == 0 ) || ( prefix.size && !prefix_data ) ||
                ( suffix_size && !suffix_data ) || suffix_size % 4 || prefix.block_size == 0 ||
                prefix.block_size % 4 || prefix.size % prefix.block_size ) {
                return SDL_SetError( "Invalid FP3D retained upload range" );
            }
            const auto *head = static_cast<const Uint8 *>( prefix_data );
            const auto *tail = static_cast<const Uint8 *>( suffix_data );
            const bool reuse_prefix = prefix.revision && prefix.revision == committed_prefix_revision_ &&
                                      prefix.size == committed_prefix_size_;
            if( !reuse_prefix ) {
                find_delta_ranges( head, 0, prefix.size, prefix.block_size );
            }
            find_delta_ranges( tail, prefix.size, suffix_size, 4096 );
            prefix_data_ = head;
            prefix_size_ = prefix.size;
            suffix_data_ = tail;
            if( range_count_ ) {
                auto *mapped = static_cast<Uint8 *>( SDL_MapGPUTransferBuffer( device_, staging_, false ) );
                if( !mapped ) {
                    return false;
                }
                copy_delta_ranges( mapped );
                SDL_UnmapGPUTransferBuffer( device_, staging_ );
            }
            pending_prefix_revision_ = prefix.revision;
            skipped_prefix_bytes_ = reuse_prefix ? prefix.size : 0;
            base_ = prefix.size ? head : tail;
            next_size_ = prefix.size + suffix_size;
            return true;
        }

        bool record_delta( SDL_GPUCopyPass *pass ) {
            if( !pass || !base_ ) {
                return SDL_SetError( "Invalid FP3D retained copy" );
            }
            for( size_t i = 0; i < range_count_; ++i ) {
                const auto &range = ranges_[i];
                SDL_GPUTransferBufferLocation source{};
                source.transfer_buffer = staging_;
                source.offset = range.offset;
                SDL_GPUBufferRegion destination{};
                destination.buffer = buffer_;
                destination.offset = range.offset;
                destination.size = range.size;
                SDL_UploadToGPUBuffer( pass, &source, &destination, false );
            }
            return true;
        }

        void commit_delta() {
            if( !base_ ) {
                return;
            }
            committed_.resize( next_size_ );
            copy_delta_ranges( committed_.data() );
            committed_prefix_revision_ = std::move( pending_prefix_revision_ );
            committed_prefix_size_ = prefix_size_;
            base_ = nullptr;
        }
        Uint32 capacity() const {
            return capacity_;
        }

        Uint32 delta_bytes() const {
            Uint32 bytes = 0;
            for( size_t i = 0; base_ && i < range_count_; ++i ) {
                bytes += ranges_[i].size;
            }
            return bytes;
        }

        size_t delta_copy_count() const {
            return base_ ? range_count_ : 0;
        }

        Uint32 delta_skipped_prefix_bytes() const {
            return base_ ? skipped_prefix_bytes_ : 0;
        }

        SDL_GPUBuffer *get() const {
            return buffer_;
        }
        // Caller must retire pending use before resetting and must keep the
        // GPU device alive. No wait or global synchronization here.
        void reset() {
            if( staging_ ) {
                SDL_ReleaseGPUTransferBuffer( device_, staging_ );
            }
            if( buffer_ ) {
                SDL_ReleaseGPUBuffer( device_, buffer_ );
            }
            staging_ = nullptr;
            buffer_ = nullptr;
            device_ = nullptr;
            capacity_ = staged_ = 0;
            usage_ = 0;
            committed_.clear();
            committed_prefix_revision_.reset();
            pending_prefix_revision_.reset();
            committed_prefix_size_ = 0;
            base_ = nullptr;
            next_size_ = 0;
            range_count_ = 0;
        }

    private:
        struct copy_range {
            Uint32 offset;
            Uint32 size;
        };
        void find_delta_ranges( const Uint8 *source, Uint32 first, Uint32 size, Uint32 block_size ) {
            const Uint32 end = first + size;
            const Uint32 common = static_cast<Uint32>( std::min<size_t>( end, committed_.size() ) );
            Uint32 offset = first;
            for( ; offset < common; ) {
                const Uint32 count = std::min( block_size, common - offset );
                if( SDL_memcmp( source + ( offset - first ), committed_.data() + offset, count ) ) {
                    add_range( offset, count );
                }
                offset += count;
            }
            if( offset < end ) {
                add_range( offset, end - offset );
            }
        }

        void copy_delta_ranges( Uint8 *destination ) const {
            for( size_t i = 0; i < range_count_; ++i ) {
                const auto &range = ranges_[i];
                Uint32 offset = range.offset;
                const Uint32 end = offset + range.size;
                if( offset < prefix_size_ ) {
                    const Uint32 split = std::min( end, prefix_size_ );
                    SDL_memcpy( destination + offset, prefix_data_ + offset, split - offset );
                    offset = split;
                }
                if( offset < end ) {
                    SDL_memcpy( destination + offset, suffix_data_ + ( offset - prefix_size_ ), end - offset );
                }
            }
        }


        void add_range( Uint32 offset, Uint32 size ) {
            if( range_count_ ) {
                auto &last = ranges_[range_count_ - 1];
                if( last.offset + last.size == offset || range_count_ == ranges_.size() ) {
                    // At the command budget, cover the remaining gaps with
                    // the final copy rather than queuing unbounded transfers.
                    last.size = offset + size - last.offset;
                    return;
                }
            }
            ranges_[range_count_++] = { offset, size };
        }

        SDL_GPUDevice *device_ = nullptr;
        SDL_GPUBuffer *buffer_ = nullptr;
        SDL_GPUTransferBuffer *staging_ = nullptr;
        SDL_GPUBufferUsageFlags usage_ = 0;
        Uint32 capacity_ = 0;
        Uint32 staged_ = 0;
        std::vector<Uint8> committed_;
        const Uint8 *base_ = nullptr;
        const Uint8 *prefix_data_ = nullptr;
        const Uint8 *suffix_data_ = nullptr;
        Uint32 prefix_size_ = 0;
        std::shared_ptr<const void> pending_prefix_revision_;
        std::shared_ptr<const void> committed_prefix_revision_;
        Uint32 committed_prefix_size_ = 0;
        Uint32 skipped_prefix_bytes_ = 0;
        Uint32 next_size_ = 0;
        std::array<copy_range, 16> ranges_{};
        size_t range_count_ = 0;
};
} // namespace fp3d_gpu
#endif // CATA_SRC_FP3D_GPU_BUFFER_H
