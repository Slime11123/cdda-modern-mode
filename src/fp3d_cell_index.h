#pragma once
#ifndef CATA_SRC_FP3D_CELL_INDEX_H
#define CATA_SRC_FP3D_CELL_INDEX_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "fp3d_snapshot.h"

// Snapshot-owned pointers, rebuilt at the same geometry boundary as the meshes.
// Compact scenes avoid hashing every neighbor; distant sparse scenes retain the
// original map semantics without allocating their entire bounding volume.
class fp3d_cell_index
{
    private:
        std::vector<const fp3d_snapshot::cell *> dense;
        std::unordered_map<tripoint_abs_ms, const fp3d_snapshot::cell *> sparse;
        tripoint_abs_ms origin;
        size_t width = 0;
        size_t height = 0;
        size_t depth = 0;

        size_t offset( size_t x, size_t y, size_t z ) const {
            return ( z * height + y ) * width + x;
        }

    public:
        void clear() {
            dense.clear();
            sparse.clear();
            width = height = depth = 0;
        }

        void rebuild( const std::vector<fp3d_snapshot::cell> &tiles ) {
            clear();
            if( tiles.empty() ) {
                return;
            }
            int xmin = tiles.front().position.x(), xmax = xmin;
            int ymin = tiles.front().position.y(), ymax = ymin;
            int zmin = tiles.front().position.z(), zmax = zmin;
            for( const auto &tile : tiles ) {
                xmin = std::min( xmin, tile.position.x() );
                xmax = std::max( xmax, tile.position.x() );
                ymin = std::min( ymin, tile.position.y() );
                ymax = std::max( ymax, tile.position.y() );
                zmin = std::min( zmin, tile.position.z() );
                zmax = std::max( zmax, tile.position.z() );
            }
            origin = tripoint_abs_ms( xmin, ymin, zmin );
            const uint64_t w = int64_t( xmax ) - xmin + 1;
            const uint64_t h = int64_t( ymax ) - ymin + 1;
            const uint64_t d = int64_t( zmax ) - zmin + 1;
            constexpr size_t capacity = 2 * 1024 * 1024;
            const size_t limit = std::min( capacity, std::max( size_t( 1024 ),
                                          std::min( tiles.size(), capacity / 8 ) * 8 ) );
            // Divide before multiplying so even extreme coordinates are safe.
            if( w <= limit && h <= limit / w && d <= limit / ( w * h ) ) {
                width = w;
                height = h;
                depth = d;
                dense.assign( width * height * depth, nullptr );
                for( const auto &tile : tiles ) {
                    auto &entry = dense[offset( int64_t( tile.position.x() ) - xmin,
                                               int64_t( tile.position.y() ) - ymin,
                                               int64_t( tile.position.z() ) - zmin )];
                    if( !entry ) {
                        entry = &tile;
                    }
                }
            } else {
                sparse.reserve( tiles.size() );
                for( const auto &tile : tiles ) {
                    sparse.emplace( tile.position, &tile );
                }
            }
        }

        const fp3d_snapshot::cell *get( const tripoint_abs_ms &p ) const {
            if( !dense.empty() ) {
                const int64_t x = int64_t( p.x() ) - origin.x();
                const int64_t y = int64_t( p.y() ) - origin.y();
                const int64_t z = int64_t( p.z() ) - origin.z();
                if( x < 0 || y < 0 || z < 0 || uint64_t( x ) >= width ||
                    uint64_t( y ) >= height || uint64_t( z ) >= depth ) {
                    return nullptr;
                }
                return dense[offset( x, y, z )];
            }
            const auto it = sparse.find( p );
            return it == sparse.end() ? nullptr : it->second;
        }
};

#endif // TILES && ENABLE_FP3D
#endif // CATA_SRC_FP3D_CELL_INDEX_H
