#pragma once
#ifndef CATA_SRC_FP3D_GEOMETRY_VEHICLE_H
#define CATA_SRC_FP3D_GEOMETRY_VEHICLE_H

#include "fp3d_geometry_curved.h"

namespace fp3d_geometry
{
template<typename Sink, typename UV>
void vehicle_cuboid( Sink &sink, float x, float z, float w, float d, float bottom, float top,
                     const std::array<float, 3> &color, const std::optional<UV> &decal )
{
    const float l = x - w * .5f, r = x + w * .5f;
    const float n = z - d * .5f, s = z + d * .5f;
    sink.textured( false );
    sink.color( color[0], color[1], color[2] );
    sink.begin( primitive::quads );
    sink.normal( 0, 1, 0 );
    sink.point( l, top, n ); sink.point( l, top, s );
    sink.point( r, top, s ); sink.point( r, top, n );
    sink.normal( 0, 0, -1 );
    sink.point( r, bottom, n ); sink.point( l, bottom, n );
    sink.point( l, top, n ); sink.point( r, top, n );
    sink.normal( 0, 0, 1 );
    sink.point( l, bottom, s ); sink.point( r, bottom, s );
    sink.point( r, top, s ); sink.point( l, top, s );
    sink.normal( -1, 0, 0 );
    sink.point( l, bottom, n ); sink.point( l, bottom, s );
    sink.point( l, top, s ); sink.point( l, top, n );
    sink.normal( 1, 0, 0 );
    sink.point( r, bottom, s ); sink.point( r, bottom, n );
    sink.point( r, top, n ); sink.point( r, top, s );
    sink.end();

    // A top-down sprite belongs on the part's upper face, in its own
    // mount space. Its UVs never slide when the entire vehicle turns.
    if( decal ) {
        sink.textured( true );
        sink.alpha_cutoff( .25f );
        sink.color( 1, 1, 1 );
        sink.begin( primitive::quads );
        sink.normal( 0, 1, 0 );
        sink.texcoord( decal->left, decal->top );
        sink.point( l + .004f, top + .003f, n + .004f );
        sink.texcoord( decal->left, decal->bottom );
        sink.point( l + .004f, top + .003f, s - .004f );
        sink.texcoord( decal->right, decal->bottom );
        sink.point( r - .004f, top + .003f, s - .004f );
        sink.texcoord( decal->right, decal->top );
        sink.point( r - .004f, top + .003f, n + .004f );
        sink.end();
        sink.alpha_cutoff( std::nullopt );
    }
}

template<typename Sink, typename UV>
void vehicle_wheel( Sink &sink, float x, float z, bool damaged, bool narrow,
                    const std::optional<UV> &decal )
{
    const float radius = damaged ? .28f : narrow ? .34f : .45f;
    const float y = radius + .012f;
    const float half_thickness = narrow ? .105f : .20f;
    constexpr float pi = 3.14159265358979323846f;
    constexpr int sides = 16;
    sink.textured( false );
    sink.color( damaged ? .23f : .085f, damaged ? .19f : .095f, .09f );
    sink.begin( primitive::quad_strip );
    for( int i = 0; i <= sides; ++i ) {
        const float a = 2 * pi * i / sides;
        sink.normal( std::cos( a ), std::sin( a ), 0 );
        sink.point( x + radius * std::cos( a ), y + radius * std::sin( a ), z - half_thickness );
        sink.point( x + radius * std::cos( a ), y + radius * std::sin( a ), z + half_thickness );
    }
    sink.end();
    for( float side : { z - half_thickness, z + half_thickness } ) {
        sink.textured( false );
        sink.color( .14f, .15f, .15f );
        sink.begin( primitive::triangle_fan );
        sink.normal( 0, 0, side < z ? -1 : 1 );
        sink.point( x, y, side );
        for( int i = 0; i <= sides; ++i ) {
            const float a = 2 * pi * i / sides;
            sink.point( x + radius * std::cos( a ), y + radius * std::sin( a ), side );
        }
        sink.end();
        if( decal ) {
            sink.textured( true );
            sink.alpha_cutoff( .22f );
            sink.color( 1, 1, 1 );
            sink.begin( primitive::triangle_fan );
            sink.texcoord( ( decal->left + decal->right ) * .5f,
                          ( decal->top + decal->bottom ) * .5f );
            sink.point( x, y, side + ( side < z ? -.002f : .002f ) );
            for( int i = 0; i <= sides; ++i ) {
                const float a = 2 * pi * i / sides;
                sink.texcoord( ( decal->left + decal->right ) * .5f +
                              ( decal->right - decal->left ) * .5f * std::cos( a ),
                              ( decal->top + decal->bottom ) * .5f +
                              ( decal->bottom - decal->top ) * .5f * std::sin( a ) );
                sink.point( x + radius * .88f * std::cos( a ),
                            y + radius * .88f * std::sin( a ),
                            side + ( side < z ? -.002f : .002f ) );
            }
            sink.end();
            sink.alpha_cutoff( std::nullopt );
        }
        sink.textured( false );
        sink.color( damaged ? .30f : .62f, damaged ? .29f : .63f, .65f );
        sink.begin( primitive::triangle_fan );
        sink.point( x, y, side + ( side < z ? -.004f : .004f ) );
        for( int i = 0; i <= sides; ++i ) {
            const float a = 2 * pi * i / sides;
            sink.point( x + radius * .29f * std::cos( a ),
                        y + radius * .29f * std::sin( a ), side );
        }
        sink.end();
    }
}
} // namespace fp3d_geometry
#endif // CATA_SRC_FP3D_GEOMETRY_VEHICLE_H
