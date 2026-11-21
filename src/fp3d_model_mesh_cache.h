#pragma once
#ifndef CATA_SRC_FP3D_MODEL_MESH_CACHE_H
#define CATA_SRC_FP3D_MODEL_MESH_CACHE_H

#include <cstddef>
#include <map>
#include <memory>
#include <utility>
#include "fp3d_geometry.h"

namespace fp3d_geometry
{
// Local-space meshes are reusable even when their world position, visibility,
// tint or instance count changes. The caller owns those per-frame decisions.
template<typename Key>
class model_mesh_cache
{
    public:
        void set_atlas_layout( unsigned revision, std::size_t slots ) {
            const auto next = std::make_pair( revision, slots );
            if( layout_ != next ) {
                clear();
                layout_ = next;
            }
        }

        template<typename Build>
        void append( const Key &key, mesh &output, const Build &build ) {
            auto found = meshes_.find( key );
            if( found == meshes_.end() ) {
                mesh generated;
                build( generated );
                const std::size_t bytes = generated.vertices.capacity() * sizeof( vertex ) +
                                          generated.indices.capacity() * sizeof( std::uint32_t );
                if( bytes > byte_limit ) {
                    append_mesh( output, generated );
                    return;
                }
                if( bytes > byte_limit - bytes_ || meshes_.size() >= model_limit ) {
                    clear();
                }
                found = meshes_.emplace( key, std::move( generated ) ).first;
                bytes_ += bytes;
            }
            append_mesh( output, found->second );
        }

    private:
        static void append_mesh( mesh &output, const mesh &source ) {
            const auto base = static_cast<std::uint32_t>( output.vertices.size() );
            output.vertices.insert( output.vertices.end(), source.vertices.begin(), source.vertices.end() );
            for( const std::uint32_t index : source.indices ) {
                output.indices.push_back( base + index );
            }
        }

        void clear() {
            meshes_.clear();
            bytes_ = 0;
        }

        static constexpr std::size_t byte_limit = 8 * 1024 * 1024;
        static constexpr std::size_t model_limit = 1024;
        std::pair<unsigned, std::size_t> layout_{};
        std::map<Key, mesh> meshes_;
        std::size_t bytes_ = 0;
};
struct model_range {
    std::uint32_t first;
    std::uint32_t count;
};

// Append-only offsets keep an uploaded palette valid as visibility changes.
// A previous frame may still hold the old contents during atlas changes.
template<typename Key>
class resident_model_meshes
{
    public:
        void set_atlas_layout( unsigned revision, std::size_t slots ) {
            const auto next = std::make_pair( revision, slots );
            if( layout_ != next ) {
                ranges_.clear();
                geometry_.reset();
                layout_ = next;
            }
        }

        template<typename Build>
        std::optional<model_range> retain( const Key &key, const Build &build ) {
            const auto found = ranges_.find( key );
            if( found != ranges_.end() ) {
                return found->second;
            }
            if( ranges_.size() == model_limit ) {
                return std::nullopt;
            }
            mesh model;
            build( model );
            const std::size_t bytes = storage( model ) + ( geometry_ ? storage( *geometry_ ) : 0 );
            if( bytes > byte_limit ) {
                return std::nullopt;
            }
            if( !geometry_ ) {
                geometry_ = std::make_shared<mesh>();
            } else if( !geometry_.unique() ) {
                geometry_ = std::make_shared<mesh>( *geometry_ );
            }
            const auto base = static_cast<std::uint32_t>( geometry_->vertices.size() );
            const model_range range{ static_cast<std::uint32_t>( geometry_->indices.size() ),
                                     static_cast<std::uint32_t>( model.indices.size() ) };
            geometry_->vertices.insert( geometry_->vertices.end(), model.vertices.begin(), model.vertices.end() );
            for( const std::uint32_t index : model.indices ) {
                geometry_->indices.push_back( base + index );
            }
            ranges_.emplace( key, range );
            return range;
        }

        std::shared_ptr<const mesh> geometry() const {
            return geometry_;
        }

    private:
        static std::size_t storage( const mesh &m ) {
            return m.vertices.size() * sizeof( vertex ) + m.indices.size() * sizeof( std::uint32_t );
        }

        static constexpr std::size_t byte_limit = 4 * 1024 * 1024;
        static constexpr std::size_t model_limit = 512;
        std::pair<unsigned, std::size_t> layout_{};
        std::map<Key, model_range> ranges_;
        std::shared_ptr<mesh> geometry_;
};
} // namespace fp3d_geometry

#endif // CATA_SRC_FP3D_MODEL_MESH_CACHE_H
