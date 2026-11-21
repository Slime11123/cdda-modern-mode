#pragma once
#ifndef CATA_SRC_FP3D_GPU_INSTANCE_PAGES_H
#define CATA_SRC_FP3D_GPU_INSTANCE_PAGES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace fp3d_gpu
{
struct instance_page_revision {};

// Each slot covers an 8 by 8 map block for a key. Keeping recently unseen
// slots avoids reuploading them every time the camera crosses a block edge.
template<typename Key, typename Instance>
class instance_pages
{
        static_assert( std::is_trivially_copyable_v<Instance> );

    public:
        static constexpr std::size_t page_size = 64;
        static constexpr std::size_t page_count = 512;
        static constexpr std::size_t max_extent = page_size * page_count;

        void begin_frame() {
            ++frame_;
            for( auto it = pages_.begin(); it != pages_.end(); ) {
                const auto &page = it->second;
                if( frame_ - page.last_used > 32 ) {
                    occupied_[page.slot] = false;
                    it = pages_.erase( it );
                } else {
                    ++it;
                }
            }
        }

        template<typename Encode>
        std::optional<std::uint32_t> write( const Key &key, const std::vector<Instance> &instances,
                                            const Encode &encode ) {
            if( instances.empty() || instances.size() > page_size ) {
                return std::nullopt;
            }
            auto found = pages_.find( key );
            if( found == pages_.end() ) {
                if( pages_.size() == page_count ) {
                    return std::nullopt;
                }
                std::size_t slot = 0;
                while( occupied_[slot] ) {
                    ++slot;
                }
                found = pages_.emplace( key, entry{ slot, frame_ } ).first;
                occupied_[slot] = true;
                if( const auto end = ( slot + 1 ) * page_size; end > data_->size() ) {
                    writable_data().resize( end );
                }
            }
            auto &page = found->second;
            page.last_used = frame_;
            const auto first = static_cast<std::uint32_t>( page.slot * page_size );
            for( std::size_t i = 0; i < instances.size(); ++i ) {
                const Instance encoded = encode( instances[i] );
                if( std::memcmp( &(*data_)[first + i], &encoded, sizeof( Instance ) ) != 0 ) {
                    std::memcpy( &writable_data()[first + i], &encoded, sizeof( Instance ) );
                }
            }
            return first;
        }

        const std::vector<Instance> &data() const {
            return *data_;
        }

        std::shared_ptr<const std::vector<Instance>> snapshot() const {
            return data_;
        }

        std::shared_ptr<const instance_page_revision> snapshot_revision() {
            if( !revision_ ) {
                revision_ = std::make_shared<instance_page_revision>();
            }
            return revision_;
        }

        void clear() {
            pages_.clear();
            occupied_.fill( false );
            data_ = std::make_shared<std::vector<Instance>>();
            revision_.reset();
        }

    private:
        std::vector<Instance> &writable_data() {
            revision_.reset();
            if( !data_.unique() ) {
                data_ = std::make_shared<std::vector<Instance>>( *data_ );
            }
            return *data_;
        }

        struct entry {
            std::size_t slot;
            std::uint64_t last_used;
        };
        std::map<Key, entry> pages_;
        std::array<bool, page_count> occupied_{};
        std::shared_ptr<std::vector<Instance>> data_ = std::make_shared<std::vector<Instance>>();
        std::shared_ptr<const instance_page_revision> revision_;
        std::uint64_t frame_ = 0;
};
} // namespace fp3d_gpu

#endif // CATA_SRC_FP3D_GPU_INSTANCE_PAGES_H
