#pragma once
#ifndef CATA_SRC_FP3D_GEOMETRY_H
#define CATA_SRC_FP3D_GEOMETRY_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

// Shared model generation. Emit the same ordered planar quads for the old
// renderer and SDL GPU. Geometry is independent of either graphics API.
// Coordinate system and UV convention remain those of the original models.
namespace fp3d_geometry
{
struct vertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{ 0, 1, 0 };
    std::array<float, 2> uv{};
    std::array<float, 4> color{ 1, 1, 1, 1 };
};

struct mesh {
    std::vector<vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Quad sink for model extraction: retain the original vertex order, UVs,
// normals and per-vertex attributes. GPU consumes indexed triangle lists.
// Only valid for quads (triangle fans/lines have separate emitters).
class quad_mesh_sink
{
    public:
        explicit quad_mesh_sink( mesh &target ) : target_( target ), first_( target.vertices.size() ) {}
        void normal( float x, float y, float z ) {
            current_.normal = { x, y, z };
        }
        void texcoord( float u, float v ) {
            current_.uv = { u, v };
        }
        void color( float r, float g, float b, float a = 1.f ) {
            current_.color = { r, g, b, a };
        }
        void point( float x, float y, float z ) {
            if( target_.vertices.size() >= std::numeric_limits<std::uint32_t>::max() ) {
                throw std::length_error( "FP3D mesh exceeds index range" );
            }
            current_.position = { x, y, z };
            target_.vertices.push_back( current_ );
            if( ( target_.vertices.size() - first_ ) % 4 == 0 ) {
                const auto a = static_cast<std::uint32_t>( target_.vertices.size() - 4 );
                const std::array<std::uint32_t, 6> triangles{ a, a + 1, a + 2, a, a + 2, a + 3 };
                target_.indices.insert( target_.indices.end(), triangles.begin(), triangles.end() );
            }
        }

    private:
        mesh &target_;
        std::size_t first_;
        vertex current_;
};

// Accept the active tileset's UV rectangle. The two windings intentionally
// reproduce the original floor and ceiling underside texture orientation.
template<typename Sink, typename UV>
void floor_face( Sink &sink, float x, float z, float y, const UV &uv,
                 float extent, bool underside = false )
{
    const float a = underside ? z + extent : z - extent;
    const float b = underside ? z - extent : z + extent;
    sink.normal( 0, underside ? -1 : 1, 0 );
    sink.texcoord( uv.left, uv.top );
    sink.point( x - extent, y, a );
    sink.texcoord( uv.left, uv.bottom );
    sink.point( x - extent, y, b );
    sink.texcoord( uv.right, uv.bottom );
    sink.point( x + extent, y, b );
    sink.texcoord( uv.right, uv.top );
    sink.point( x + extent, y, a );
}

// Keep the slope and UV winding shared with the flat floor and picking plane.
template<typename Sink, typename UV, typename Plane>
void ramp_face( Sink &sink, float x, float z, const UV &uv, const Plane &ramp )
{
    const float length = std::sqrt( 1 + ramp.dx * ramp.dx + ramp.dy * ramp.dy );
    sink.normal( -ramp.dx / length, 1 / length, -ramp.dy / length );
    sink.texcoord( uv.left, uv.top );
    sink.point( x - .5f, ramp.height( -.5f, -.5f ), z - .5f );
    sink.texcoord( uv.left, uv.bottom );
    sink.point( x - .5f, ramp.height( -.5f, .5f ), z + .5f );
    sink.texcoord( uv.right, uv.bottom );
    sink.point( x + .5f, ramp.height( .5f, .5f ), z + .5f );
    sink.texcoord( uv.right, uv.top );
    sink.point( x + .5f, ramp.height( .5f, -.5f ), z - .5f );
}

// Use the same cropped per-row wall faces and independent furniture face
// textures as the existing scene. No texture substitution or model LOD.
template<typename Sink, typename UV, typename Faces>
void box_faces( Sink &sink, float x, float z, float width, float depth,
                float bottom, float top, const std::optional<UV> &material,
                bool tile_sides, const Faces *faces, bool draw_top )
{
    const float l = x - width / 2;
    const float r = x + width / 2;
    const float n = z - depth / 2;
    const float s = z + depth / 2;
    const auto uv = ( faces ? faces->top : material ).value_or( UV{ 0, 0, 1, 1 } );
    if( draw_top ) {
        sink.normal( 0, 1, 0 );
        sink.texcoord( uv.left, uv.top );
        sink.point( l, top, n );
        sink.texcoord( uv.left, uv.bottom );
        sink.point( l, top, s );
        sink.texcoord( uv.right, uv.bottom );
        sink.point( r, top, s );
        sink.texcoord( uv.right, uv.top );
        sink.point( r, top, n );
    }
    const auto side = [&]( float x0, float z0, float x1, float z1, bool front = false,
                           bool back = false ) {
        const auto side_uv = faces ? ( front ? faces->front :
                                      back && faces->back ? faces->back : faces->side ).value_or( uv ) : uv;
        const float u1 = tile_sides ? side_uv.left + ( side_uv.right - side_uv.left ) *
                         std::min( 1.0f, std::hypot( x1 - x0, z1 - z0 ) ) : side_uv.right;
        const int first = tile_sides ? static_cast<int>( std::floor( bottom ) ) : 0;
        const int last = tile_sides ? static_cast<int>( std::ceil( top ) ) : 1;
        for( int row = first; row < last; ++row ) {
            const float y0 = tile_sides ? std::max( bottom, static_cast<float>( row ) ) : bottom;
            const float y1 = tile_sides ? std::min( top, static_cast<float>( row + 1 ) ) : top;
            const float v0 = tile_sides ? side_uv.bottom + ( side_uv.top - side_uv.bottom ) * ( y0 - row ) : side_uv.bottom;
            const float v1 = tile_sides ? side_uv.bottom + ( side_uv.top - side_uv.bottom ) * ( y1 - row ) : side_uv.top;
            sink.texcoord( side_uv.left, v0 );
            sink.point( x0, y0, z0 );
            sink.texcoord( u1, v0 );
            sink.point( x1, y0, z1 );
            sink.texcoord( u1, v1 );
            sink.point( x1, y1, z1 );
            sink.texcoord( side_uv.left, v1 );
            sink.point( x0, y1, z0 );
        }
    };
    sink.normal( 0, 0, -1 );
    side( faces ? r : l, n, faces ? l : r, n, false, true );
    sink.normal( 0, 0, 1 );
    side( faces ? l : r, s, faces ? r : l, s, true );
    sink.normal( -1, 0, 0 );
    side( l, faces ? n : s, l, faces ? s : n );
    sink.normal( 1, 0, 0 );
    side( r, faces ? s : n, r, faces ? n : s );
}
template<typename Faces, typename Sink, typename UV>
void window_frame( Sink &sink, const std::optional<UV> &material, float floor_height,
                   bool upper_floor )
{
    const Faces *faces = nullptr;
    box_faces( sink, 0, 0, 1, .18f, 0, .9f, material, true, faces, true );
    box_faces( sink, 0, 0, 1, .18f, 2.1f, floor_height, material, true, faces, !upper_floor );
    box_faces( sink, -.46f, 0, .08f, .22f, .9f, 2.1f, material, true, faces, true );
    box_faces( sink, .46f, 0, .08f, .22f, .9f, 2.1f, material, true, faces, true );

    // The box primitive has no bottom. Close the lintel with the same reversed UVs.
    const UV uv = material.value_or( UV{ 0, 0, 1, 1 } );
    sink.normal( 0, -1, 0 );
    sink.texcoord( uv.left, uv.top );
    sink.point( -.5f, 2.1f, .09f );
    sink.texcoord( uv.left, uv.bottom );
    sink.point( -.5f, 2.1f, -.09f );
    sink.texcoord( uv.right, uv.bottom );
    sink.point( .5f, 2.1f, -.09f );
    sink.texcoord( uv.right, uv.top );
    sink.point( .5f, 2.1f, .09f );
}

template<typename UV, typename Section>
UV stair_uv( UV material, const Section &section )
{
    const float width = material.right - material.left;
    material.left += width * ( section.width - 1 - section.east ) / section.width;
    material.right = material.left + width / section.width;
    return material;
}

// Crop each surviving piece to its original position on the whole door.
template<typename UV, typename Part>
UV damaged_door_uv( const UV &material, const Part &part, float floor_height )
{
    const float u = material.right - material.left;
    const float v = material.bottom - material.top;
    return {
        material.left + u * ( .5f + ( part.x - part.width / 2 ) / .98f ),
        material.bottom - v * part.top / floor_height,
        material.left + u * ( .5f + ( part.x + part.width / 2 ) / .98f ),
        material.bottom - v * part.bottom / floor_height
    };
}
} // namespace fp3d_geometry
#endif // CATA_SRC_FP3D_GEOMETRY_H
