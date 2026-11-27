#pragma once
#ifndef CATA_SRC_FP3D_GPU_MESH_H
#define CATA_SRC_FP3D_GPU_MESH_H

#include "fp3d_geometry.h"
#include "fp3d_gpu_buffer.h"
#include <type_traits>

namespace fp3d_gpu
{
// Immutable indexed mesh upload. The caller owns the scene's material/pipeline
// and in-flight lifetime. No legacy graphics API is involved.
class indexed_mesh
{
    public:
        indexed_mesh() = default;
        indexed_mesh( const indexed_mesh & ) = delete;
        indexed_mesh &operator=( const indexed_mesh & ) = delete;

        bool initialize( SDL_GPUDevice *device, const fp3d_geometry::mesh &mesh ) {
            return initialize( device, mesh.vertices, mesh.indices );
        }

        // The pipeline declares the input layout for this vertex stream.
        template<typename Vertex>
        bool initialize( SDL_GPUDevice *device, const std::vector<Vertex> &vertices,
                         const std::vector<std::uint32_t> &indices ) {
            static_assert( std::is_trivially_copyable_v<Vertex> );
            if( count_ ) {
                return SDL_SetError( "FP3D indexed mesh already initialized" );
            }
            return stage_vertices( device, vertices.data(), vertices.size(), sizeof( Vertex ), indices, false );
        }

        // A slot must retire its previous GPU use before staging another mesh.
        bool stage( SDL_GPUDevice *device, const fp3d_geometry::mesh &mesh, bool retain = false ) {
            return stage_vertices( device, mesh.vertices.data(), mesh.vertices.size(),
                                   sizeof( fp3d_geometry::vertex ), mesh.indices, retain );
        }

    private:
        bool stage_vertices( SDL_GPUDevice *device, const void *vertices, size_t vertex_count,
                             size_t stride, const std::vector<std::uint32_t> &indices, bool retain ) {
            count_ = 0;
            uploaded_ = false;
            retained_ = retain;
            if( !vertex_count || indices.empty() || indices.size() % 3 != 0 ||
                vertex_count > SDL_MAX_UINT32 / stride ||
                indices.size() > SDL_MAX_UINT32 / sizeof( std::uint32_t ) ) {
                return SDL_SetError( "Invalid FP3D indexed mesh" );
            }
            for( const auto index : indices ) {
                if( index >= vertex_count ) {
                    return SDL_SetError( "FP3D vertex index out of range" );
                }
            }
            const Uint32 vb = static_cast<Uint32>( vertex_count * stride );
            const Uint32 ib = static_cast<Uint32>( indices.size() * sizeof( std::uint32_t ) );
            if( !vertices_.reserve( device, vb, SDL_GPU_BUFFERUSAGE_VERTEX ) ||
                !indices_.reserve( device, ib, SDL_GPU_BUFFERUSAGE_INDEX ) ||
                !( retain ? vertices_.stage_delta( vertices, vb ) : vertices_.stage( vertices, vb ) ) ||
                !( retain ? indices_.stage_delta( indices.data(), ib ) :
                   indices_.stage( indices.data(), ib ) ) ) {
                reset();
                return false;
            }
            count_ = static_cast<Uint32>( indices.size() );
            return true;
        }

    public:
        // The copy pass and later render pass must be on ordered GPU commands.
        bool record( SDL_GPUCopyPass *pass ) {
            if( !count_ || uploaded_ || !pass ) {
                return SDL_SetError( "Invalid FP3D mesh copy" );
            }
            if( !( retained_ ? vertices_.record_delta( pass ) && indices_.record_delta( pass ) :
                   vertices_.record( pass ) && indices_.record( pass ) ) ) {
                return false;
            }
            uploaded_ = true;
            return true;
        }
        void commit() {
            if( retained_ ) {
                vertices_.commit_delta();
                indices_.commit_delta();
            }
        }

        bool draw( SDL_GPURenderPass *pass, Uint32 instance_count = 1,
                   Uint32 first_instance = 0 ) const {
            return draw_range( pass, 0, count_, instance_count, first_instance );
        }

        bool ready() const {
            return uploaded_ && count_ != 0;
        }

        Uint32 index_count() const {
            return count_;
        }

        bool can_draw_range( Uint32 first, Uint32 count, Uint32 instances ) const {
            return ready() && count && instances && first <= count_ && count <= count_ - first;
        }

        bool draw_range( SDL_GPURenderPass *pass, Uint32 first_index, Uint32 index_count,
                         Uint32 instance_count, Uint32 first_instance ) const {
            if( !pass || !can_draw_range( first_index, index_count, instance_count ) ) {
                return SDL_SetError( "FP3D mesh not ready for indexed draw" );
            }
            bind_buffers( pass );
            SDL_DrawGPUIndexedPrimitives( pass, index_count, instance_count, first_index, 0,
                                          first_instance );
            return true;
        }

        bool draw_indirect( SDL_GPURenderPass *pass, SDL_GPUBuffer *commands,
                            Uint32 command_count, Uint32 byte_offset = 0 ) const {
            if( !pass || !uploaded_ || !commands || !command_count ) {
                return SDL_SetError( "FP3D mesh not ready for indirect draw" );
            }
            bind_buffers( pass );
            SDL_DrawGPUIndexedPrimitivesIndirect( pass, commands, byte_offset, command_count );
            return true;
        }

        std::uint64_t buffer_capacity() const {
            return std::uint64_t( vertices_.capacity() ) + indices_.capacity();
        }

        std::uint64_t delta_bytes() const {
            return std::uint64_t( vertices_.delta_bytes() ) + indices_.delta_bytes();
        }

        size_t delta_copy_count() const {
            return vertices_.delta_copy_count() + indices_.delta_copy_count();
        }

        // Retire GPU use before reset; destruction is subject to same lifetime.
        void reset() {
            vertices_.reset();
            indices_.reset();
            count_ = 0;
            uploaded_ = false;
        }

    private:
        void bind_buffers( SDL_GPURenderPass *pass ) const {
            SDL_GPUBufferBinding vertex_binding{};
            vertex_binding.buffer = vertices_.get();
            SDL_GPUBufferBinding index_binding{};
            index_binding.buffer = indices_.get();
            SDL_BindGPUVertexBuffers( pass, 0, &vertex_binding, 1 );
            SDL_BindGPUIndexBuffer( pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT );
        }

        upload_buffer vertices_;
        upload_buffer indices_;
        Uint32 count_ = 0;
        bool uploaded_ = false;
        bool retained_ = false;
};

// Borrow the uploaded GPU mesh and its optional instance stream for a draw.
struct indexed_draw {
    const indexed_mesh *mesh;
    Uint32 first_index;
    Uint32 index_count;
    Uint32 first_instance;
    Uint32 instance_count;
    SDL_GPUBuffer *instances;
    SDL_GPUBuffer *instance_light = nullptr;

    indexed_draw( const indexed_mesh *mesh, Uint32 first_index, Uint32 index_count,
                  Uint32 first_instance, Uint32 instance_count, SDL_GPUBuffer *instances ) :
        mesh( mesh ), first_index( first_index ), index_count( index_count ),
        first_instance( first_instance ), instance_count( instance_count ), instances( instances ) {}

    indexed_draw( const indexed_mesh *mesh ) : indexed_draw( mesh, 0, mesh ? mesh->index_count() : 0,
                0, 1, nullptr ) {}

    bool ready() const {
        return mesh && mesh->can_draw_range( first_index, index_count, instance_count );
    }

    bool record( SDL_GPURenderPass *pass ) const {
        if( !pass || !ready() ) {
            return SDL_SetError( "FP3D indexed draw is not ready" );
        }
        if( instances ) {
            const SDL_GPUBufferBinding binding{ instances, 0 };
            SDL_BindGPUVertexBuffers( pass, 1, &binding, 1 );
        }
        if( instance_light ) {
            const SDL_GPUBufferBinding binding{ instance_light, 0 };
            SDL_BindGPUVertexBuffers( pass, 2, &binding, 1 );
        }
        return mesh->draw_range( pass, first_index, index_count, instance_count, first_instance );
    }
};
} // namespace fp3d_gpu
#endif // CATA_SRC_FP3D_GPU_MESH_H
