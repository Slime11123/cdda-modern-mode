#pragma once
#ifndef CATA_SRC_FP3D_GEOMETRY_CURVED_H
#define CATA_SRC_FP3D_GEOMETRY_CURVED_H

#include "fp3d_geometry.h"
#include <utility>

// Source-preserving geometry generators for the existing curved furniture,
// foliage and window models. The sink records topology and texture state.
namespace fp3d_geometry
{
enum class primitive { quads, triangles, triangle_fan, quad_strip };

struct draw_range {
    std::uint32_t first_index;
    std::uint32_t index_count;
    bool textured;
    std::optional<float> alpha_cutoff;
    bool unlit = false;
    bool highlighted = false;
};

// Each draw range uses the same atlas but retains whether its original
// primitive used the texture. The render pass supplies blend/cull/depth state.
// All generated indices are triangle-list indices, including fan caps.
class primitive_mesh_sink
{
    public:
        primitive_mesh_sink( mesh &target, std::vector<draw_range> &ranges ) :
            target_( target ), ranges_( ranges ) {}
        void normal( float x, float y, float z ) {
            current_.normal = { x, y, z };
        }
        void highlight( bool highlighted ) {
            highlighted_ = highlighted;
        }
        void texcoord( float u, float v ) {
            current_.uv = { u, v };
        }
        void color( float r, float g, float b, float a = 1.f ) {
            current_.color = { r, g, b, a };
        }
        void textured( bool value ) {
            if( open_ ) {
                throw std::logic_error( "FP3D texture change inside primitive" );
            }
            textured_ = value;
        }
        void alpha_cutoff( std::optional<float> cutoff ) {
            if( open_ ) {
                throw std::logic_error( "FP3D alpha state change inside primitive" );
            }
            alpha_cutoff_ = cutoff;
        }
        void unlit( bool value ) {
            if( open_ ) {
                throw std::logic_error( "FP3D lighting state change inside primitive" );
            }
            unlit_ = value;
        }
        void begin( primitive type ) {
            if( open_ ) {
                throw std::logic_error( "Nested FP3D primitive" );
            }
            open_ = true;
            type_ = type;
            first_ = target_.vertices.size();
        }
        void point( float x, float y, float z ) {
            if( !open_ || target_.vertices.size() >= std::numeric_limits<std::uint32_t>::max() ) {
                throw std::logic_error( "Invalid FP3D primitive vertex" );
            }
            current_.position = { x, y, z };
            target_.vertices.push_back( current_ );
        }
        void end() {
            if( !open_ ) {
                throw std::logic_error( "No FP3D primitive" );
            }
            const auto count = target_.vertices.size() - first_;
            if( ( type_ == primitive::quads && count % 4 ) ||
                ( type_ == primitive::triangles && count % 3 ) ||
                ( type_ == primitive::triangle_fan && count < 3 ) ||
                ( type_ == primitive::quad_strip && ( count < 4 || count % 2 ) ) ) {
                throw std::logic_error( "Incomplete FP3D primitive" );
            }
            const auto added = type_ == primitive::quads ? count / 4 * 6 :
                               type_ == primitive::triangles ? count : ( count - 2 ) * 3;
            if( added > std::numeric_limits<std::uint32_t>::max() - target_.indices.size() ) {
                throw std::length_error( "FP3D index range overflow" );
            }
            const auto first_index = static_cast<std::uint32_t>( target_.indices.size() );
            const auto a = static_cast<std::uint32_t>( first_ );
            const auto tri = [&]( std::uint32_t i, std::uint32_t j, std::uint32_t k ) {
                target_.indices.push_back( a + i );
                target_.indices.push_back( a + j );
                target_.indices.push_back( a + k );
            };
            if( type_ == primitive::quads ) {
                for( std::uint32_t i = 0; i < count; i += 4 ) {
                    tri( i, i + 1, i + 2 );
                    tri( i, i + 2, i + 3 );
                }
            } else if( type_ == primitive::quad_strip ) {
                for( std::uint32_t i = 0; i + 3 < count; i += 2 ) {
                    tri( i, i + 1, i + 3 );
                    tri( i, i + 3, i + 2 );
                }
            } else if( type_ == primitive::triangles ) {
                for( std::uint32_t i = 0; i < count; i += 3 ) {
                    tri( i, i + 1, i + 2 );
                }
            } else {
                for( std::uint32_t i = 1; i + 1 < count; ++i ) {
                    tri( 0, i, i + 1 );
                }
            }
            if( added ) {
                const auto n = static_cast<std::uint32_t>( added );
                if( !ranges_.empty() && ranges_.back().textured == textured_ &&
                    ranges_.back().alpha_cutoff == alpha_cutoff_ && ranges_.back().unlit == unlit_ &&
                    ranges_.back().highlighted == highlighted_ &&
                    ranges_.back().first_index + ranges_.back().index_count == first_index ) {
                    ranges_.back().index_count += n;
                } else {
                    ranges_.push_back( { first_index, n, textured_, alpha_cutoff_, unlit_, highlighted_ } );
                }
            }
            open_ = false;
        }

    private:
        mesh &target_;
        std::vector<draw_range> &ranges_;
        vertex current_;
        primitive type_ = primitive::quads;
        std::size_t first_ = 0;
        bool textured_ = false;
        std::optional<float> alpha_cutoff_;
        bool open_ = false;
        bool unlit_ = false;
    bool highlighted_ = false;
};

inline constexpr std::array<std::array<float, 2>, 3> landscape_tree_positions{ {
    { -3, -2 }, { 3, 2 }, { -1, 4 }
} };
inline constexpr float landscape_tree_height = 5.f;
inline constexpr float first_ground_layer_offset = .006f;
inline constexpr float ground_layer_spacing = .001f;

inline constexpr float billboard_lift = .04f;
inline constexpr float bedding_lift = .045f;
inline constexpr float bedding_half_width = .34f;
inline constexpr float bedding_half_depth = .35f;

inline std::array<float, 3> billboard_position( const std::array<float, 3> &origin,
        const std::array<float, 3> &right, const std::array<float, 3> &up,
        float side, float rise, float lift = 0.f )
{
    const float adjusted_rise = rise - lift;
    return { origin[0] + right[0] * side + up[0] * adjusted_rise,
             origin[1] + billboard_lift + lift + up[1] * adjusted_rise,
             origin[2] + right[2] * side + up[2] * adjusted_rise };
}

inline float plant_width( float height )
{
    return std::max( .9f, height <= 1.2f ? height : height * .7f );
}

template<typename UV>
UV treetop_uv( const UV &source )
{
    const float height = source.bottom - source.top;
    return { source.left, source.top + height * .04f,
             source.right, source.top + height * .66f };
}

template<typename Sink, typename UV>
void crossed_plant( Sink &sink, float x, float z, float height, bool grass, const UV &uv, float surface_y = 0.f )
{
    // Two fixed, perpendicular diagonal planes; both sides remain visible.
    // Ground-cover textures are opaque, so cut blade silhouettes in geometry.
    for( const float diagonal : {
             -1.0f, 1.0f
         } ) {
        const float width = plant_width( height );
        const auto vertex = [&]( float u, float v ) {
            sink.texcoord( uv.left + ( uv.right - uv.left ) * u,
                          uv.bottom + ( uv.top - uv.bottom ) * v );
            sink.point( x + ( u - .5f ) * width, surface_y + .015f + v * height,
                        z + diagonal * ( u - .5f ) * width );
        };
        if( grass ) {
            sink.begin( primitive::triangles );
            for( int blade = 0; blade < 7; ++blade ) {
                const float left = blade / 7.0f;
                const float right = ( blade + 1 ) / 7.0f;
                vertex( left, 0 );
                vertex( right, 0 );
                vertex( left + ( right - left ) * .65f, blade % 2 ? .7f : 1.0f );
            }
            sink.end();
        } else {
            sink.begin( primitive::quads );
            vertex( 0, 0 );
            vertex( 1, 0 );
            vertex( 1, 1 );
            vertex( 0, 1 );
            sink.end();
        }
    }
}

template<typename Sink, typename UV>
void window_face( Sink &sink, const std::optional<UV> &material, float half_width, float bottom, float top )
{
    const auto uv = material.value_or( UV{ 0, 0, 1, 1 } );
    if( material ) {
        sink.textured( true );
    } else {
        sink.textured( false );
    }
    // Exactly one upright image in the aperture; never tile it over the sill,
    // lintel, jambs or ground. Culling is disabled, so both sides are visible.
    sink.begin( primitive::quads );
    sink.texcoord( uv.left, uv.bottom );
    sink.point( -half_width, bottom, 0 );
    sink.texcoord( uv.right, uv.bottom );
    sink.point( half_width, bottom, 0 );
    sink.texcoord( uv.right, uv.top );
    sink.point( half_width, top, 0 );
    sink.texcoord( uv.left, uv.top );
    sink.point( -half_width, top, 0 );
    sink.end();
}

// A horizontal cylinder puts the photographed front on both endcaps.
template<typename Part>
Part horizontal_furniture_cylinder( const Part &part )
{
    return Part{ 0, 0, part.width, part.top - part.bottom,
                 -part.depth * .5f, part.depth * .5f };
}

template<typename Faces>
Faces horizontal_furniture_surfaces( const Faces &surfaces )
{
    return { surfaces.front, surfaces.front, surfaces.side, surfaces.front };
}

template<typename Sink, typename Part, typename Faces>
void round_furniture_part( Sink &sink, const Part &part, const Faces &surfaces, float top_radius_scale )
{
    using UV = typename decltype(surfaces.top)::value_type;
    constexpr int sides = 16;
    for( const bool top : { true, false } ) {
        const auto material = top ? surfaces.top : surfaces.side;
        const auto uv = material.value_or( UV{ 0, 0, 1, 1 } );
        if( material ) {
            sink.textured( true );
        } else {
            sink.textured( false );
        }
        const float height = top ? part.top : part.bottom;
        sink.normal( 0, top ? 1 : -1, 0 );
        sink.begin( primitive::triangle_fan );
        sink.texcoord( ( uv.left + uv.right ) / 2, ( uv.top + uv.bottom ) / 2 );
        sink.point( part.x, height, part.z );
        for( int side = 0; side <= sides; ++side ) {
            const float angle = side * 6.2831853f / sides;
            const float x = std::cos( angle );
            const float z = std::sin( angle ) * ( top ? -1 : 1 );
            sink.texcoord( uv.left + ( x + 1 ) / 2 * ( uv.right - uv.left ),
                          uv.top + ( z + 1 ) / 2 * ( uv.bottom - uv.top ) );
            const float radius = top ? top_radius_scale : 1.0f;
            sink.point( part.x + x * part.width / 2 * radius, height,
                        part.z + z * part.depth / 2 * radius );
        }
        sink.end();
    }
    const auto uv = surfaces.side.value_or( UV{ 0, 0, 1, 1 } );
    if( surfaces.side ) {
        sink.textured( true );
    } else {
        sink.textured( false );
    }
    sink.begin( primitive::quads );
    for( int side = 0; side < sides; ++side ) {
        for( const auto corner : { std::pair{ side, false }, std::pair{ side, true },
                                   std::pair{ side + 1, true }, std::pair{ side + 1, false } } ) {
            const float angle = corner.first * 6.2831853f / sides;
            const float x = std::cos( angle );
            const float z = std::sin( angle );
            // Correctly sloped surface normals on a truncated cone.
            const float vertical = ( 1.0f - top_radius_scale ) * part.width * .5f /
                                   std::max( .01f, part.top - part.bottom );
            sink.normal( x, vertical, z );
            sink.texcoord( uv.left + ( uv.right - uv.left ) * corner.first / sides,
                          corner.second ? uv.top : uv.bottom );
            const float radius = corner.second ? top_radius_scale : 1.0f;
            sink.point( part.x + x * part.width / 2 * radius,
                        corner.second ? part.top : part.bottom,
                        part.z + z * part.depth / 2 * radius );
        }
    }
    sink.end();
}

template<typename Sink, typename Part, typename Faces>
void furniture_ellipsoid( Sink &sink, const Part &p, const Faces &surfaces )
{
    using UV = typename decltype(surfaces.top)::value_type;
    constexpr int sides = 20;
    constexpr int bands = 12;
    const auto material = surfaces.side ? surfaces.side : surfaces.front;
    const auto uv = material.value_or( UV{ 0, 0, 1, 1 } );
    if( material ) sink.textured( true );
    else sink.textured( false );
    const float mid = ( p.top + p.bottom ) * .5f;
    const float rh = ( p.top - p.bottom ) * .5f;
    const auto vertex = [&]( float latitude, float azimuth ) {
        const float cs = std::cos( latitude );
        const float nx = cs * std::cos( azimuth );
        const float nz = cs * std::sin( azimuth );
        const float ny = std::sin( latitude );
        sink.normal( nx, ny, nz );
        sink.texcoord( uv.left + ( azimuth / 6.2831853f ) * ( uv.right - uv.left ),
                      uv.top + ( latitude / 3.14159265f + .5f ) * ( uv.bottom - uv.top ) );
        sink.point( p.x + nx * p.width * .5f,
                    mid + ny * rh, p.z + nz * p.depth * .5f );
    };
    sink.begin( primitive::quads );
    for( int row = 0; row < bands; ++row ) {
        const float lat0 = -1.5707963f + 3.14159265f * row / bands;
        const float lat1 = -1.5707963f + 3.14159265f * ( row + 1 ) / bands;
        for( int col = 0; col < sides; ++col ) {
            const float lon0 = 6.2831853f * col / sides;
            const float lon1 = 6.2831853f * ( col + 1 ) / sides;
            vertex( lat0, lon0 );
            vertex( lat0, lon1 );
            vertex( lat1, lon1 );
            vertex( lat1, lon0 );
        }
    }
    sink.end();
}

template<typename Sink, typename Part, typename Faces>
void furniture_hollow_ring( Sink &sink, const Part &p, const Faces &material )
{
    using UV = typename decltype(material.front)::value_type;
    constexpr int sides = 32;
    const float cy = ( p.bottom + p.top ) * .5f;
    const float ry = ( p.top - p.bottom ) * .5f;
    const float rx = p.width * .5f;
    const float inner = std::max( .1f, std::min( p.inner, .92f ) );
    const auto front = material.front.value_or( UV{ 0, 0, 1, 1 } );
    const auto side = material.side.value_or( UV{ 0, 0, 1, 1 } );
    for( int i = 0; i < sides; ++i ) {
        const float a0 = i * 6.2831853f / sides;
        const float a1 = ( i + 1 ) * 6.2831853f / sides;
        const float c0 = std::cos( a0 ), s0 = std::sin( a0 );
        const float c1 = std::cos( a1 ), s1 = std::sin( a1 );
        const auto face_point = [&]( float c, float sn, float radius, float z ) {
            sink.texcoord( front.left + ( c * radius + 1.f ) * .5f * ( front.right - front.left ),
                          front.top + ( 1.f - sn * radius ) * .5f * ( front.bottom - front.top ) );
            sink.point( p.x + c * rx * radius, cy + sn * ry * radius, z );
        };
        if( material.front ) sink.textured( true );
        else sink.textured( false );
        for( const bool forward : { true, false } ) {
            sink.normal( 0, 0, forward ? 1 : -1 );
            const float z = p.z + ( forward ? p.depth : -p.depth ) * .5f;
            sink.begin( primitive::quads );
            if( forward ) {
                face_point( c0, s0, 1, z );
                face_point( c1, s1, 1, z );
                face_point( c1, s1, inner, z );
                face_point( c0, s0, inner, z );
            } else {
                face_point( c0, s0, inner, z );
                face_point( c1, s1, inner, z );
                face_point( c1, s1, 1, z );
                face_point( c0, s0, 1, z );
            }
            sink.end();
        }
        if( material.side ) sink.textured( true );
        else sink.textured( false );
        const auto edge_point = [&]( float c, float sn, float radius, float z, float u, float v ) {
            sink.texcoord( side.left + u * ( side.right - side.left ),
                          side.top + v * ( side.bottom - side.top ) );
            sink.point( p.x + c * rx * radius, cy + sn * ry * radius, z );
        };
        for( const bool inside : { false, true } ) {
            const float radius = inside ? inner : 1.f;
            sink.normal( inside ? -c0 : c0, inside ? -s0 : s0, 0 );
            sink.begin( primitive::quads );
            if( !inside ) {
                edge_point( c0, s0, radius, p.z - p.depth*.5f, float( i ) / sides, 1 );
                edge_point( c0, s0, radius, p.z + p.depth*.5f, float( i ) / sides, 0 );
                edge_point( c1, s1, radius, p.z + p.depth*.5f, float( i+1 ) / sides, 0 );
                edge_point( c1, s1, radius, p.z - p.depth*.5f, float( i+1 ) / sides, 1 );
            } else {
                edge_point( c1, s1, radius, p.z - p.depth*.5f, float( i+1 ) / sides, 1 );
                edge_point( c1, s1, radius, p.z + p.depth*.5f, float( i+1 ) / sides, 0 );
                edge_point( c0, s0, radius, p.z + p.depth*.5f, float( i ) / sides, 0 );
                edge_point( c0, s0, radius, p.z - p.depth*.5f, float( i ) / sides, 1 );
            }
            sink.end();
        }
    }
}
} // namespace fp3d_geometry
#endif // CATA_SRC_FP3D_GEOMETRY_CURVED_H
