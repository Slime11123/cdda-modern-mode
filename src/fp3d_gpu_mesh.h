#pragma once
#ifndef CATA_SRC_FP3D_GPU_MESH_H
#define CATA_SRC_FP3D_GPU_MESH_H

#include "fp3d_geometry.h"
#include "fp3d_gpu_buffer.h"

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
            if( count_ ) {
                return SDL_SetError( "FP3D indexed mesh already initialized" );
            }
            return stage( device, mesh );
        }

        // A slot must retire its previous GPU use before staging another mesh.
        bool stage( SDL_GPUDevice *device, const fp3d_geometry::mesh &mesh, bool retain = false ) {
            count_ = 0;
            uploaded_ = false;
            retained_ = retain;
            if( mesh.vertices.empty() || mesh.indices.empty() ||
                mesh.indices.size() % 3 != 0 ||
                mesh.vertices.size() > SDL_MAX_UINT32 / sizeof( fp3d_geometry::vertex ) ||
                mesh.indices.size() > SDL_MAX_UINT32 / sizeof( std::uint32_t ) ) {
                return SDL_SetError( "Invalid FP3D indexed mesh" );
            }
            for( const auto index : mesh.indices ) {
                if( index >= mesh.vertices.size() ) {
                    return SDL_SetError( "FP3D vertex index out of range" );
                }
            }
            const Uint32 vb = static_cast<Uint32>( mesh.vertices.size() *
                                                  sizeof( fp3d_geometry::vertex ) );
            const Uint32 ib = static_cast<Uint32>( mesh.indices.size() * sizeof( std::uint32_t ) );
            if( !vertices_.reserve( device, vb, SDL_GPU_BUFFERUSAGE_VERTEX ) ||
                !indices_.reserve( device, ib, SDL_GPU_BUFFERUSAGE_INDEX ) ||
                !( retain ? vertices_.stage_delta( mesh.vertices.data(), vb ) :
                   vertices_.stage( mesh.vertices.data(), vb ) ) ||
                !( retain ? indices_.stage_delta( mesh.indices.data(), ib ) :
                   indices_.stage( mesh.indices.data(), ib ) ) ) {
                reset();
                return false;
            }
            count_ = static_cast<Uint32>( mesh.indices.size() );
            return true;
        }

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

        bool draw_range( SDL_GPURenderPass *pass, Uint32 first_index, Uint32 index_count,
                         Uint32 instance_count, Uint32 first_instance ) const {
            if( !pass || !uploaded_ || !instance_count || !index_count ||
                first_index > count_ || index_count > count_ - first_index ) {
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
} // namespace fp3d_gpu
#endif // CATA_SRC_FP3D_GPU_MESH_H
