#pragma once
#ifndef CATA_SRC_FP3D_STRUCTURE_GEOMETRY_H
#define CATA_SRC_FP3D_STRUCTURE_GEOMETRY_H

#include <array>
#include <cmath>
#include <cstddef>

namespace fp3d_geometry
{
using structure_point = std::array<float, 3>;

template<typename Emit>
void kiln_fire_vertices( const Emit &emit )
{
    struct flame { float x, z, width, height; };
    constexpr std::array<flame, 5> flames{{
        { -.115f, .105f, .085f, .305f }, { -.038f, .046f, .105f, .420f },
        { .071f, .130f, .083f, .354f }, { .012f, -.040f, .064f, .283f },
        { .148f, .000f, .048f, .246f }
    }};
    for( const auto &f : flames ) {
        constexpr float floor = .111f;
        for( const bool across : { true, false } ) {
            const float x = across ? f.width : 0.f;
            const float z = across ? 0.f : f.width;
            emit( f.x - x, floor, f.z - z, 1.f, .27f, .015f, .86f );
            emit( f.x + .018f, floor + f.height, f.z - .018f, 1.f, .83f, .12f, .94f );
            emit( f.x + x, floor, f.z + z, 1.f, .32f, .025f, .84f );
        }
    }
}

template<typename Sink>
void stop_sign_letters( Sink &sink )
{
    constexpr std::array<std::array<unsigned, 5>, 4> letters{{
        { 7, 4, 7, 1, 7 }, { 7, 2, 2, 2, 2 }, { 7, 5, 5, 5, 7 }, { 7, 5, 7, 4, 4 }
    }};
    constexpr float pixel = .031f;
    for( const float side : { -1.f, 1.f } ) {
        sink.normal( 0, 0, side );
        const float z = .05f + side * .022f;
        for( unsigned letter = 0; letter < letters.size(); ++letter ) {
            for( unsigned row = 0; row < 5; ++row ) {
                for( unsigned col = 0; col < 3; ++col ) {
                    if( !( letters[letter][row] & ( 4u >> col ) ) ) { continue; }
                    const float x = ( letter * 4 + col - 7.5f ) * pixel;
                    const float y = 1.54f + ( 2.5f - row ) * pixel;
                    sink.point( side * x, y - pixel, z );
                    sink.point( side * ( x + pixel ), y - pixel, z );
                    sink.point( side * ( x + pixel ), y, z );
                    sink.point( side * x, y, z );
                }
            }
        }
    }
}

template<typename Beam>
void radio_tower_beams( float height, const Beam &beam )
{
    constexpr float half = .34f;
    for( const float sx : { -half, half } ) {
        for( const float sz : { -half, half } ) {
            beam( sx, 0, sz, sx, height, sz, .075f, false );
        }
    }
    for( int level = 0; level <= 2; ++level ) {
        const float y = height * level / 2.f;
        for( const float side : { -half, half } ) {
            beam( -half, y, side, half, y, side, .052f, false );
            beam( side, y, -half, side, y, half, .052f, false );
        }
    }
    for( int level = 0; level < 2; ++level ) {
        const float bottom = height * level / 2.f;
        const float top = height * ( level + 1 ) / 2.f;
        for( const float side : { -half, half } ) {
            beam( -half, bottom, side, half, top, side, .041f, true );
            beam( half, bottom, side, -half, top, side, .041f, true );
            beam( side, bottom, -half, side, top, half, .041f, true );
            beam( side, bottom, half, side, top, -half, .041f, true );
        }
    }
}

template<typename Sink>
void structural_beam( Sink &sink, const structure_point &a, const structure_point &b, float thickness )
{
    const float dx = b[0] - a[0];
    const float dy = b[1] - a[1];
    const float dz = b[2] - a[2];
    const float horizontal = std::hypot( dx, dz );
    const float length = std::hypot( horizontal, dy );
    if( length < .0001f ) {
        return;
    }
    const structure_point along{ dx / length, dy / length, dz / length };
    const structure_point right = horizontal > .0001f ?
                                  structure_point{ dz / horizontal, 0, -dx / horizontal } :
                                  structure_point{ 1, 0, 0 };
    const structure_point across{ along[1] * right[2], along[2] * right[0] - along[0] * right[2],
                                  -along[1] * right[0] };
    const float half = thickness * .5f;
    const auto vertex = [&]( const structure_point &p, float x, float z ) {
        sink.point( p[0] + half * ( x * right[0] + z * across[0] ),
                    p[1] + half * ( x * right[1] + z * across[1] ),
                    p[2] + half * ( x * right[2] + z * across[2] ) );
    };
    constexpr std::array<std::array<float, 2>, 4> corners{{
        { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 }
    }};
    for( const float sign : { -1.f, 1.f } ) {
        sink.normal( along[0] * sign, along[1] * sign, along[2] * sign );
        for( size_t i = 0; i < corners.size(); ++i ) {
            const auto &c = corners[sign > 0 ? corners.size() - 1 - i : i];
            vertex( sign > 0 ? b : a, c[0], c[1] );
        }
    }
    for( size_t i = 0; i < corners.size(); ++i ) {
        const auto &u = corners[i];
        const auto &v = corners[( i + 1 ) % corners.size()];
        const float nx = ( u[0] + v[0] ) * .5f;
        const float nz = ( u[1] + v[1] ) * .5f;
        sink.normal( nx * right[0] + nz * across[0], nx * right[1] + nz * across[1],
                     nx * right[2] + nz * across[2] );
        vertex( a, u[0], u[1] );
        vertex( b, u[0], u[1] );
        vertex( b, v[0], v[1] );
        vertex( a, v[0], v[1] );
    }
}

template<typename Sink>
void wire_lattice( Sink &sink, float ax, float az, float bx, float bz, float bottom, float top )
{
    const float width = std::hypot( bx - ax, bz - az );
    if( width <= .001f || top <= bottom ) {
        return;
    }
    const float dx = ( bx - ax ) / width;
    const float dz = ( bz - az ) / width;
    constexpr int columns = 8;
    constexpr int rows = 13;
    constexpr float thickness = .006f;
    const auto point = [&]( float u, float y ) {
        sink.point( ax + dx * u, y, az + dz * u );
    };
    sink.normal( -dz, 0, dx );
    const float du = width / columns;
    const float dy = ( top - bottom ) / rows;
    const float length = std::hypot( du, dy );
    for( int col = 0; col < columns; ++col ) {
        for( int row = 0; row < rows; ++row ) {
            const float u = du * col;
            for( const float sign : { -1.f, 1.f } ) {
                const float y = bottom + dy * ( row + ( sign < 0 ? 1 : 0 ) );
                const float side_u = sign * thickness * dy / length;
                const float side_y = -thickness * du / length;
                point( u + side_u, y + side_y );
                point( u + du + side_u, y + sign * dy + side_y );
                point( u + du - side_u, y + sign * dy - side_y );
                point( u - side_u, y - side_y );
            }
        }
    }
}
}
#endif
