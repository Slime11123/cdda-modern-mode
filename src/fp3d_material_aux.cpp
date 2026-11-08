#include "fp3d_material_aux.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace fp3d_material_aux
{
namespace
{
bool has( const std::string &value, const char *fragment )
{
    return value.find( fragment ) != std::string::npos;
}

std::uint8_t byte( float value )
{
    return static_cast<std::uint8_t>( std::lround( std::clamp( value, 0.0f, 1.0f ) * 255.0f ) );
}

float luminance( const std::uint8_t *p )
{
    return ( 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2] ) / 255.0f;
}
} // namespace

surface classify( const std::string &key )
{
    // UI sprites and billboard art must not be treated as physical relief.
    if( key.empty() || has( key, "$status:" ) || has( key, "$sprite:" ) ||
        has( key, "$blood:" ) || has( key, "$item:" ) ) {
        return {};
    }
    if( has( key, "$plant:" ) || has( key, "tree" ) || has( key, "grass" ) ||
        has( key, "bush" ) || has( key, "leaf" ) || has( key, "flower" ) ) {
        return { category::vegetation, 0.92f, 0.0f, 0.0f };
    }
    if( has( key, "water" ) || has( key, "sewage" ) ) {
        return { category::water, 0.12f, 0.0f, 0.0f };
    }
    if( has( key, "glass" ) || has( key, "window" ) ) {
        return { category::glass, 0.14f, 0.0f, 0.0f };
    }
    if( has( key, "road" ) || has( key, "asphalt" ) || has( key, "sidewalk" ) ||
        has( key, "pavement" ) ) {
        return { category::road, 0.87f, 0.0f, 0.12f };
    }
    if( has( key, "metal" ) || has( key, "steel" ) || has( key, "fence" ) ||
        has( key, "pipe" ) ) {
        return { category::metal, 0.43f, 0.70f, 0.08f };
    }
    if( has( key, "$vehicle:" ) ) {
        // All-vehicle metallic=1 would incorrectly treat seats and tyres as chrome.
        if( has( key, "wheel" ) || has( key, "tire" ) || has( key, "seat" ) ) {
            return { category::fabric, 0.87f, 0.0f, 0.0f };
        }
        return { category::metal, 0.55f, 0.35f, 0.07f };
    }
    if( has( key, "wood" ) || has( key, "plank" ) || has( key, "board" ) ||
        has( key, "f_table" ) || has( key, "f_door" ) ) {
        return { category::wood, 0.80f, 0.0f, 0.11f };
    }
    if( has( key, "stone" ) || has( key, "brick" ) || has( key, "wall" ) ||
        has( key, "rock" ) || has( key, "concrete" ) || has( key, "cement" ) ) {
        return { category::stone, 0.89f, 0.0f, 0.15f };
    }
    if( has( key, "dirt" ) || has( key, "sand" ) || has( key, "soil" ) ||
        has( key, "mud" ) || has( key, "gravel" ) ) {
        return { category::soil, 0.94f, 0.0f, 0.10f };
    }
    return {};
}

maps generate( const std::string &key, const std::uint8_t *rgba,
               int width, int height, int pitch )
{
    maps result;
    result.material = classify( key );
    // Bound allocations and reject nonsensical/overflowing image dimensions.
    if( !rgba || width <= 0 || height <= 0 || width > 2048 || height > 2048 ||
        pitch < width * 4 || pitch > 2048 * 8 ) {
        return result;
    }
    result.width = width;
    result.height = height;
    result.normal_rgba.resize( static_cast<std::size_t>( width ) * height * 4 );
    result.orm_rgba.resize( result.normal_rgba.size() );
    const float strength = result.material.normal_strength;
    const auto at = [&]( int x, int y ) -> const std::uint8_t * {
        x = std::clamp( x, 0, width - 1 );
        y = std::clamp( y, 0, height - 1 );
        return rgba + static_cast<std::size_t>( y ) * pitch + x * 4;
    };
    for( int y = 0; y < height; ++y ) {
        for( int x = 0; x < width; ++x ) {
            const std::uint8_t *c = at( x, y );
            const std::size_t out = ( static_cast<std::size_t>( y ) * width + x ) * 4;
            float nx = 0.0f;
            float ny = 0.0f;
            float nz = 1.0f;
            if( strength > 0.0f && c[3] >= 200 ) {
                // Do not interpret a sprite's transparent outline as a cliff.
                const auto sample = [&]( int sx, int sy ) {
                    const std::uint8_t *p = at( sx, sy );
                    return p[3] >= 200 ? luminance( p ) : luminance( c );
                };
                // Weak central differences preserve the pixel-art character.
                const float dx = ( sample( x + 1, y ) - sample( x - 1, y ) ) * strength;
                const float dy = ( sample( x, y + 1 ) - sample( x, y - 1 ) ) * strength;
                const float length = std::sqrt( dx * dx + dy * dy + 1.0f );
                nx = -dx / length;
                ny = -dy / length;
                nz = 1.0f / length;
            }
            result.normal_rgba[out] = byte( nx * 0.5f + 0.5f );
            result.normal_rgba[out + 1] = byte( ny * 0.5f + 0.5f );
            result.normal_rgba[out + 2] = byte( nz * 0.5f + 0.5f );
            result.normal_rgba[out + 3] = c[3];
            result.orm_rgba[out] = 255;
            result.orm_rgba[out + 1] = byte( result.material.roughness );
            result.orm_rgba[out + 2] = byte( result.material.metallic );
            result.orm_rgba[out + 3] = c[3];
        }
    }
    return result;
}
} // namespace fp3d_material_aux
#endif
