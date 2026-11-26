#pragma once
#ifndef CATA_SRC_FP3D_SCENE_H
#define CATA_SRC_FP3D_SCENE_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include "fp3d_camera.h"
#include "fp3d_snapshot.h"
#include "fp3d_targeting.h"
#include "coordinates.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_set>
namespace fp3d_snapshot
{
struct scene_snapshot;
}
namespace fp3d_scene
{
struct blueprint {
    tripoint_abs_ms position;
    float height = 1;
    bool valid = false;
    std::string name;
    std::optional<fp3d_snapshot::cell> terrain;
    // A planning symbol only: -1 down, +1 up. Never opens a floor or shows another level.
    int vertical_hint = 0;
};
// Presentation height shared by opaque water and its depth-tested material mask.
inline float water_surface_offset( const std::string &terrain )
{
    if( terrain == "t_water_pump" || terrain == "t_water_dispenser" ) { return 0; }
    const bool water = terrain.compare( 0, 8, "t_water_" ) == 0 ||
                       terrain.compare( 0, 9, "t_swater_" ) == 0 ||
                       terrain == "t_sewage" ||
                       terrain == "t_pool" || terrain == "t_tidepool";
    return water ? -.18f : 0.0f;
}
enum class pavement_surface { none, asphalt, concrete };
inline pavement_surface classify_pavement( const std::string &id )
{
    if( id.compare( 0, 10, "t_concrete" ) == 0 || id.compare( 0, 10, "t_sidewalk" ) == 0 ) {
        return pavement_surface::concrete;
    }
    if( id.compare( 0, 6, "t_road" ) == 0 || id.compare( 0, 10, "t_pavement" ) == 0 ||
        id.compare( 0, 9, "t_asphalt" ) == 0 ) {
        return pavement_surface::asphalt;
    }
    return pavement_surface::none;
}
// A known upper floor owns the shared wall cap, including ramps and lowered water.
inline bool upper_floor_owns_cap( const fp3d_snapshot::cell *above )
{
    return above && ( above->floor_present || above->is_ramp ||
                      water_surface_offset( above->terrain ) < 0 );
}
inline constexpr float wall_width = .98f;
inline bool wall_neighbour( const fp3d_snapshot::cell *tile )
{
    using kind = fp3d_snapshot::terrain_kind;
    return tile && ( tile->kind == kind::wall || tile->kind == kind::glass ||
                     tile->kind == kind::window );
}
inline bool glass_wall( const fp3d_snapshot::cell &tile )
{
    return tile.kind == fp3d_snapshot::terrain_kind::glass &&
           tile.terrain.find( "door" ) == std::string::npos;
}
template<typename Lookup>
std::array<bool, 4> glass_barrier_links( const fp3d_snapshot::cell &tile, const Lookup &cell_at )
{
    std::array<bool, 4> links{};
    const std::array<tripoint_rel_ms, 4> directions{
        tripoint_rel_ms::north, tripoint_rel_ms::east, tripoint_rel_ms::south, tripoint_rel_ms::west
    };
    for( size_t i = 0; i < links.size(); ++i ) {
        const auto *adjacent = cell_at( tile.position + directions[i] );
        using kind = fp3d_snapshot::terrain_kind;
        links[i] = tile.terrain == "t_glass_railing" ?
                   adjacent && adjacent->terrain == tile.terrain :
                   wall_neighbour( adjacent ) ||
                   ( adjacent && ( adjacent->kind == kind::closed_door || adjacent->kind == kind::open_door ) );
    }
    return links;
}
inline constexpr float cutaway_wall_height = .45f;

template<typename Lookup>
std::unordered_set<tripoint_abs_ms> monster_cutaway_walls(
    const fp3d_snapshot::scene_snapshot &scene, const fp3d_camera::offset &eye,
    const Lookup &cell_at )
{
    std::unordered_set<tripoint_abs_ms> walls;
    for( const auto &monster : scene.sprites ) {
        if( monster.kind != fp3d_snapshot::sprite_kind::monster ) {
            continue;
        }
        const auto *support = cell_at( monster.position );
        if( !support || support->known != fp3d_snapshot::knowledge::visible || support->exterior_only ) {
            continue;
        }
        const auto rel = monster.position - scene.origin;
        const float level = rel.z() * fp3d_camera::floor_height;
        const float body = level + monster.elevation + monster.height * monster.display_scale * .5f;
        if( eye.height <= body || body >= level + fp3d_camera::floor_height ) {
            continue;
        }
        const float dx = eye.x - rel.x();
        const float dz = eye.y - rel.y();
        const float length = std::hypot( dx, dz );
        // Follow the view through the monster's own storey. Only wall tiles
        // in front of an already-visible monster are shortened.
        const float end = length * std::min( 1.f,
                          ( level + fp3d_camera::floor_height - body ) / ( eye.height - body ) );
        for( float distance = .25f; distance < std::min( end, 6.f ); distance += .25f ) {
            const auto position = monster.position + tripoint_rel_ms(
                                      static_cast<int>( std::lround( dx * distance / length ) ),
                                      static_cast<int>( std::lround( dz * distance / length ) ), 0 );
            const auto *tile = cell_at( position );
            if( position != monster.position && tile && tile->kind == fp3d_snapshot::terrain_kind::wall ) {
                walls.insert( position );
            }
        }
    }
    return walls;
}
template<typename Lookup>
unsigned furniture_facing( const fp3d_snapshot::cell &tile, const Lookup &known_cell )
{
    const auto table_at = [&]( const tripoint_abs_ms &p ) {
        const auto *other = known_cell( p );
        return other && fp3d_furniture::chair_table( other->furniture );
    };
    const auto known_wall = [&]( const tripoint_abs_ms &p ) {
        return wall_neighbour( known_cell( p ) );
    };
    if( !tile.furniture ) {
        return 0u;
    }
    if( tile.furniture->shape == fp3d_furniture::kind::chair ||
        tile.furniture->shape == fp3d_furniture::kind::camp_chair ) {
        return fp3d_furniture::facing( tile.furniture->shape, {},
        { table_at( tile.position + tripoint_rel_ms::north ), table_at( tile.position + tripoint_rel_ms::east ),
          table_at( tile.position + tripoint_rel_ms::south ), table_at( tile.position + tripoint_rel_ms::west ) } );
    }
    const auto shape = tile.furniture->shape;
    const auto grouped = []( fp3d_furniture::kind kind ) {
        return fp3d_furniture::shows_contents( kind ) ||
               kind == fp3d_furniture::kind::counter ||
               kind == fp3d_furniture::kind::cabinet ||
               kind == fp3d_furniture::kind::desk ||
               kind == fp3d_furniture::kind::rack;
    };
    const auto linked = [&]( tripoint_rel_ms direction ) {
        const auto *other = known_cell( tile.position + direction );
        return other && other->furniture &&
               ( other->furniture->shape == shape ||
                 ( grouped( shape ) && grouped( other->furniture->shape ) ) );
    };
    const bool row_ew = linked( tripoint_rel_ms::east ) || linked( tripoint_rel_ms::west );
    const bool row_ns = linked( tripoint_rel_ms::north ) || linked( tripoint_rel_ms::south );
    if( row_ew != row_ns ) {
        // Aligned cabinets, desks and benches share a row axis even if an
        // endpoint also touches a perpendicular wall.
        const unsigned a = row_ew ? 0u : 1u;
        const unsigned b = ( a + 2 ) % 4;
        const std::array<tripoint_rel_ms, 4> dirs{ tripoint_rel_ms::north,
            tripoint_rel_ms::east, tripoint_rel_ms::south, tripoint_rel_ms::west };
        const bool wall_a = known_wall( tile.position + dirs[a] );
        const bool wall_b = known_wall( tile.position + dirs[b] );
        return wall_a != wall_b ? ( wall_a ? a : b ) : a;
    }
    return fp3d_furniture::facing( tile.furniture->shape,
    { known_wall( tile.position + tripoint_rel_ms::north ),
      known_wall( tile.position + tripoint_rel_ms::east ),
      known_wall( tile.position + tripoint_rel_ms::south ),
      known_wall( tile.position + tripoint_rel_ms::west ) } );
}

template<typename Lookup>
std::string window_surround( const fp3d_snapshot::cell &tile, const Lookup &lookup )
{
    for( const auto &direction : { tripoint_rel_ms::east, tripoint_rel_ms::west,
                                  tripoint_rel_ms::north, tripoint_rel_ms::south } ) {
        const auto *neighbour = lookup( tile.position + direction );
        if( neighbour && neighbour->kind == fp3d_snapshot::terrain_kind::wall ) {
            return neighbour->terrain;
        }
    }
    return "t_wall";
}
inline bool stair_flight_visible( const fp3d_snapshot::cell &tile, const fp3d_snapshot::cell *above )
{
    return tile.kind != fp3d_snapshot::terrain_kind::ladder && tile.goes_up &&
           ( above ? above->goes_down || !above->floor_present :
             !tile.ceiling_present && !tile.exterior_only &&
             tile.known == fp3d_snapshot::knowledge::visible );
}
inline bool door_part_owns_cap( float top, bool upper_floor )
{
    return !upper_floor || top < fp3d_camera::floor_height - .001f;
}
} // namespace fp3d_scene
#endif
#endif // CATA_SRC_FP3D_SCENE_H
