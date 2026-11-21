#pragma once
#ifndef CATA_SRC_FP3D_SHORE_CACHE_H
#define CATA_SRC_FP3D_SHORE_CACHE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// Each bit represents one known water or shore cell in an 8 by 8 map block.
// Block coordinates are in units of blocks, with an independent key per level.
template<typename Point>
class fp3d_shore_cache
{
    public:
        using mask = std::uint64_t;
        using blocks = std::unordered_map<Point, mask>;
        static constexpr int width = 8;

        static Point block_of( const Point &p ) {
            return Point( floor_block( p.x() ), floor_block( p.y() ), p.z() );
        }

        static mask cell_bit( const Point &p ) {
            const int x = p.x() - floor_block( p.x() ) * width;
            const int y = p.y() - floor_block( p.y() ) * width;
            return mask{1} << ( y * width + x );
        }

        // This returns the number of affected blocks, including empty blocks
        // whose shore cells may be queried by a construction preview.
        std::size_t update( blocks next_water ) {
            std::unordered_set<Point> dirty;
            constexpr mask west = 0x0101010101010101ULL;
            constexpr mask east = 0x8080808080808080ULL;
            constexpr mask north = 0xffULL;
            constexpr mask south = north << ( 64 - width );
            const auto mark_changed = [&]( const Point &key, mask changed ) {
                if( !changed ) {
                    return;
                }
                dirty.insert( key );
                const auto edges = neighbours( key );
                const std::array<mask, 4> boundaries{ east, west, north, south };
                for( std::size_t i = 0; i < boundaries.size(); ++i ) {
                    if( changed & boundaries[i] ) {
                        dirty.insert( edges[i] );
                    }
                }
            };
            for( const auto &[key, contents] : water_ ) {
                mark_changed( key, contents ^ get( next_water, key ) );
            }
            for( const auto &[key, contents] : next_water ) {
                if( !water_.count( key ) ) {
                    mark_changed( key, contents );
                }
            }
            for( const auto &key : dirty ) {
                const auto edges = neighbours( key );
                const mask center = get( next_water, key );
                const mask shore = ( ( center >> 1 ) & ~east ) |
                                   ( ( center << 1 ) & ~west ) |
                                   ( center << width ) | ( center >> width ) |
                                   ( ( get( next_water, edges[0] ) & west ) << ( width - 1 ) ) |
                                   ( ( get( next_water, edges[1] ) & east ) >> ( width - 1 ) ) |
                                   ( get( next_water, edges[2] ) >> ( 64 - width ) ) |
                                   ( get( next_water, edges[3] ) << ( 64 - width ) );
                if( shore ) {
                    shore_[key] = shore;
                } else {
                    shore_.erase( key );
                }
            }
            water_ = std::move( next_water );
            return dirty.size();
        }

        mask shore_in( const Point &block ) const {
            return get( shore_, block );
        }

    private:
        static int floor_block( int coordinate ) {
            return coordinate / width - ( coordinate % width < 0 );
        }

        static std::array<Point, 4> neighbours( const Point &key ) {
            return { Point( key.x() + 1, key.y(), key.z() ), Point( key.x() - 1, key.y(), key.z() ),
                     Point( key.x(), key.y() - 1, key.z() ), Point( key.x(), key.y() + 1, key.z() ) };
        }

        static mask get( const blocks &source, const Point &key ) {
            const auto found = source.find( key );
            return found == source.end() ? 0 : found->second;
        }

        blocks water_;
        blocks shore_;
};

#endif // CATA_SRC_FP3D_SHORE_CACHE_H
