#pragma once
#ifndef CATA_SRC_FP3D_VEHICLE_ROOF_VISIBILITY_H
#define CATA_SRC_FP3D_VEHICLE_ROOF_VISIBILITY_H

#include <cmath>
#include <unordered_set>
#include <vector>

#include "fp3d_snapshot.h"

namespace fp3d_scene
{
struct vehicle_roof_visibility {
    std::unordered_set<tripoint_abs_ms> ceilings;
    std::unordered_set<tripoint_abs_ms> floors;
};

// Isolated wooden sheets over an intact car roof are presentation artifacts.
// Building floors, stairs, connected roofs and concrete bridges remain solid.
template<typename CellLookup>
vehicle_roof_visibility find_vehicle_roof_ghosts( const fp3d_snapshot::scene_snapshot &scene,
        const CellLookup &known_cell )
{
    vehicle_roof_visibility result;
    std::unordered_set<tripoint_abs_ms> candidate_upper;
    const auto road_base = []( const fp3d_snapshot::cell *tile ) {
        if( !tile || !tile->floor_present || tile->open_air ) {
            return false;
        }
        const std::string &id = tile->terrain;
        return id.find( "t_pavement" ) == 0 ||
               id.find( "t_road" ) == 0 ||
               id.find( "t_sidewalk" ) == 0 ||
               id.find( "t_asphalt" ) == 0;
    };
    for( const fp3d_vehicle::vehicle_view &car : scene.vehicles ) {
        int frames = 0;
        int roofs = 0;
        for( const fp3d_vehicle::part_view &part : car.parts ) {
            frames += part.kind == fp3d_vehicle::role::frame;
            roofs += part.kind == fp3d_vehicle::role::roof && !part.broken;
        }
        if( frames < 4 || roofs < 1 ) {
            continue; // no suppression above bicycles or bare frames
        }
        const float angle = car.angle_degrees * 3.14159265358979323846f / 180.f;
        const float cs = std::cos( angle ), sn = std::sin( angle );
        for( const fp3d_vehicle::part_view &part : car.parts ) {
            if( part.kind != fp3d_vehicle::role::roof || part.broken ) {
                continue;
            }
            const float lx = float( part.mount_x - car.pivot_x );
            const float lz = float( part.mount_y - car.pivot_y );
            const tripoint_abs_ms ground(
                car.origin.x() + int( std::round( cs * lx - sn * lz ) ),
                car.origin.y() + int( std::round( sn * lx + cs * lz ) ),
                car.origin.z() );
            const auto *base = known_cell( ground );
            if( !road_base( base ) ) {
                continue;
            }
            const auto *above = known_cell( ground + tripoint_rel_ms::above );
            if( base->ceiling_present && ( !above || !above->floor_present ) ) {
                result.ceilings.insert( ground );
            }
            if( above && above->floor_present &&
                above->kind == fp3d_snapshot::terrain_kind::floor &&
                !above->goes_up && !above->goes_down && !above->is_ramp ) {
                // Only the suspect floating wooden roof, not a normal
                // paved upper road or concrete bridge above a car.
                const std::string &id = above->terrain;
                const bool suspect_wood_sheet = id == "t_floor" ||
                    id.find( "wood" ) != std::string::npos ||
                    id.find( "board" ) != std::string::npos ||
                    id.find( "roof" ) != std::string::npos;
                if( suspect_wood_sheet ) {
                    candidate_upper.insert( ground + tripoint_rel_ms::above );
                }
            }
        }
    }
    constexpr size_t max_isolated_tiles = 16;
    std::unordered_set<tripoint_abs_ms> visited;
    for( const auto &seed : candidate_upper ) {
        if( !visited.insert( seed ).second ) {
            continue;
        }
        std::vector<tripoint_abs_ms> pending{seed};
        bool structural = false;
        // Scan the entire connected component even when a structural
        // neighbor or large area is detected. Otherwise another roof
        // seed from the same component could be misclassified as a
        // new, small isolated patch after the first early exit.
        for( size_t i = 0; i < pending.size(); ++i ) {
            const auto *tile = known_cell( pending[i] );
            if( !tile || !tile->floor_present ||
                tile->kind != fp3d_snapshot::terrain_kind::floor ||
                tile->goes_up || tile->goes_down || tile->is_ramp ) {
                structural = true;
                continue;
            }
            for( const auto &step : { tripoint_rel_ms::north, tripoint_rel_ms::south,
                                     tripoint_rel_ms::east, tripoint_rel_ms::west } ) {
                const auto neighbor = pending[i] + step;
                const auto *next = known_cell( neighbor );
                if( !next ) {
                    continue;
                }
                if( next->kind == fp3d_snapshot::terrain_kind::wall ||
                    next->kind == fp3d_snapshot::terrain_kind::window ||
                    next->kind == fp3d_snapshot::terrain_kind::closed_door ||
                    next->kind == fp3d_snapshot::terrain_kind::damaged_door ) {
                    structural = true;
                    continue;
                }
                if( next->floor_present &&
                    next->kind == fp3d_snapshot::terrain_kind::floor &&
                    visited.insert( neighbor ).second ) {
                    pending.push_back( neighbor );
                }
            }
            if( pending.size() > max_isolated_tiles ) {
                structural = true;
            }
        }
        if( !structural && pending.size() <= max_isolated_tiles ) {
            for( const auto &tile : pending ) {
                if( candidate_upper.count( tile ) ) {
                    result.ceilings.insert( tile );
                    result.floors.insert( tile );
                }
            }
        }
    }
    return result;
}
} // namespace fp3d_scene
#endif
