#pragma once
#ifndef CATA_SRC_FP3D_RAMP_H
#define CATA_SRC_FP3D_RAMP_H

#include <array>
#include <string>

#include "fp3d_camera.h"
#include "fp3d_snapshot.h"

// Native bridge ramps occupy two squares, represented on both storeys.
// Keep drawing and ray picking on the same plane in native map coordinates.
namespace fp3d_ramp
{
inline bool paired( const std::string &id )
{
    return id.find( "_ramp_up_low" ) != std::string::npos ||
           id.find( "_ramp_up_high" ) != std::string::npos ||
           id.find( "_ramp_down_low" ) != std::string::npos ||
           id.find( "_ramp_down_high" ) != std::string::npos;
}

inline bool downward( const std::string &id )
{
    return id.find( "_ramp_down_" ) != std::string::npos;
}

struct surface {
    float center = 0;
    float dx = 0;
    float dy = 0;
    float height( float x, float y ) const {
        return center + dx * x + dy * y;
    }
};

template<typename Lookup>
surface plane( const fp3d_snapshot::cell &tile, const Lookup &lookup )
{
    const float height = fp3d_camera::floor_height;
    if( !paired( tile.terrain ) ) {
        return { tile.ramp_delta * height / 2, 0, -tile.ramp_delta * height };
    }
    const bool low = tile.terrain.compare( tile.terrain.size() - 4, 4, "_low" ) == 0;
    std::string partner = tile.terrain.substr( 0, tile.terrain.size() - ( low ? 4 : 5 ) );
    partner += low ? "_high" : "_low";
    // Default to the native bridgehead orientation when its partner is outside
    // the captured area. A visible neighbour also handles rotated mapgens.
    int dx = 0;
    int dy = 1;
    for( const auto &direction : std::array<tripoint_rel_ms, 4>{
             tripoint_rel_ms::north, tripoint_rel_ms::east,
             tripoint_rel_ms::south, tripoint_rel_ms::west
         } ) {
        const auto *other = lookup( tile.position + direction );
        if( other && other->terrain == partner ) {
            dx = direction.x() * ( low ? 1 : -1 );
            dy = direction.y() * ( low ? 1 : -1 );
            break;
        }
    }
    return { height * ( ( low ? .25f : .75f ) - ( downward( tile.terrain ) ? 1 : 0 ) ),
             dx * height / 2, dy * height / 2 };
}

template<typename Lookup>
const fp3d_snapshot::cell *support( const fp3d_snapshot::cell &tile, const Lookup &lookup )
{
    if( tile.is_ramp ) {
        return &tile;
    }
    if( tile.terrain.compare( 0, 11, "t_guardrail" ) == 0 ) {
        for( const auto &direction : std::array<tripoint_rel_ms, 4>{
                 tripoint_rel_ms::north, tripoint_rel_ms::east,
                 tripoint_rel_ms::south, tripoint_rel_ms::west
             } ) {
            const auto *other = lookup( tile.position + direction );
            if( other && paired( other->terrain ) ) {
                const auto grade = plane( *other, lookup );
                // End railings on the flat approaches must stay flat.
                if( grade.dx * direction.x() + grade.dy * direction.y() != 0 ) {
                    continue;
                }
                return other;
            }
        }
    }
    return nullptr;
}

inline bool covered( const fp3d_snapshot::cell &tile, const fp3d_snapshot::cell *above )
{
    if( !above || !paired( tile.terrain ) || downward( tile.terrain ) ) {
        return false;
    }
    std::string counterpart = tile.terrain;
    counterpart.replace( counterpart.find( "_ramp_up_" ), 9, "_ramp_down_" );
    return above->is_ramp && above->terrain == counterpart;
}
} // namespace fp3d_ramp

#endif // CATA_SRC_FP3D_RAMP_H
