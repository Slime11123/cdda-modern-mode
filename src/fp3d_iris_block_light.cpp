#include "fp3d_iris_block_light.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cmath>

#include "fp3d_snapshot.h"

namespace fp3d_iris
{
namespace
{
block_light_levels propagate( const block_light_topology &topology,
                              const std::vector<block_light_source> &sources, bool sky )
{
    struct step {
        tripoint_rel_ms delta;
        light_face from;
        light_face to;
    };
    const std::array<step, 6> steps{ {
        { tripoint_rel_ms( 1, 0, 0 ), light_face::east, light_face::west },
        { tripoint_rel_ms( -1, 0, 0 ), light_face::west, light_face::east },
        { tripoint_rel_ms( 0, 1, 0 ), light_face::south, light_face::north },
        { tripoint_rel_ms( 0, -1, 0 ), light_face::north, light_face::south },
        { tripoint_rel_ms( 0, 0, 1 ), light_face::up, light_face::down },
        { tripoint_rel_ms( 0, 0, -1 ), light_face::down, light_face::up },
    } };
    block_light_levels levels;
    std::array<std::vector<tripoint_abs_ms>, max_block_light + 1> pending;
    const auto raise_light = [&]( const tripoint_abs_ms &position, int level ) {
        auto [found, inserted] = levels.try_emplace( position, static_cast<std::uint8_t>( level ) );
        if( inserted || found->second < level ) {
            found->second = static_cast<std::uint8_t>( level );
            pending[level].push_back( position );
        }
    };
    for( const auto &source : sources ) {
        if( source.level > 0 && topology.count( source.position ) ) {
            raise_light( source.position, std::min( source.level, max_block_light ) );
        }
    }
    for( int level = max_block_light; level > 1; --level ) {
        for( size_t i = 0; i < pending[level].size(); ++i ) {
            const auto position = pending[level][i];
            if( levels.at( position ) != level ) {
                continue;
            }
            const auto &cell = topology.at( position );
            for( const auto &direction : steps ) {
                if( !cell.passes( direction.from ) ) {
                    continue;
                }
                const auto neighbour = position + direction.delta;
                const auto destination = topology.find( neighbour );
                if( destination == topology.end() || !destination->second.passes( direction.to ) ) {
                    continue;
                }
                const bool glass = ( direction.from == light_face::up && cell.glass_above ) ||
                                   ( direction.from == light_face::down && destination->second.glass_above );
                const int attenuation = destination->second.attenuation;
                const bool open_sky_column = sky && level == max_block_light &&
                                             direction.from == light_face::down && !glass && attenuation <= 1;
                const int remaining = open_sky_column ? level :
                                      level - std::max( glass ? 2 : 1, attenuation );
                if( remaining > 0 ) {
                    raise_light( neighbour, remaining );
                }
            }
        }
    }
    return levels;
}

block_light_topology scene_topology( const fp3d_snapshot::scene_snapshot &scene )
{
    block_light_topology topology;
    topology.reserve( scene.cells.size() );
    constexpr auto bit = []( light_face face ) { return static_cast<std::uint8_t>( face ); };
    constexpr auto horizontal = bit( light_face::east ) | bit( light_face::west ) |
                                bit( light_face::south ) | bit( light_face::north );
    for( const auto &tile : scene.cells ) {
        if( tile.known != fp3d_snapshot::knowledge::visible || tile.exterior_only ) {
            continue;
        }
        using kind = fp3d_snapshot::terrain_kind;
        const bool opaque = tile.kind == kind::wall || tile.kind == kind::closed_door ||
                            tile.kind == kind::damaged_door || tile.kind == kind::obstacle ||
                            ( tile.kind == kind::window && tile.pane == fp3d_snapshot::window_pane::covered );
        const bool glass = tile.kind == kind::glass || ( tile.kind == kind::window &&
                           tile.pane == fp3d_snapshot::window_pane::glazed );
        block_light_cell cell;
        if( !opaque ) {
            cell.open_faces = horizontal;
            cell.glass_above = tile.glass_ceiling && !tile.cutaway_ceiling;
            if( !tile.ceiling_present && !tile.cutaway_ceiling ) {
                cell.open_faces |= bit( light_face::up );
            }
            if( !tile.floor_present ) {
                cell.open_faces |= bit( light_face::down );
            }
            // A pane spans a snapshotted tile and attenuates entry into it.
            cell.attenuation = glass ? 2 : 1;
        }
        auto [existing, inserted] = topology.try_emplace( tile.position, cell );
        if( !inserted ) {
            existing->second.open_faces &= cell.open_faces;
            existing->second.attenuation = std::max( existing->second.attenuation, cell.attenuation );
            existing->second.glass_above &= cell.glass_above;
        }
    }
    for( auto &[position, cell] : topology ) {
        if( !cell.glass_above ) {
            continue;
        }
        const auto above = topology.find( position + tripoint_rel_ms( 0, 0, 1 ) );
        if( above != topology.end() && ( above->second.open_faces & horizontal ) ) {
            cell.open_faces |= bit( light_face::up );
            above->second.open_faces |= bit( light_face::down );
        }
    }
    return topology;
}
} // namespace

block_light_levels propagate_block_light( const block_light_topology &topology,
        const std::vector<block_light_source> &sources )
{
    return propagate( topology, sources, false );
}

block_light_levels scene_block_light( const fp3d_snapshot::scene_snapshot &scene,
                                      float full_level_emission )
{
    if( scene.lights.empty() || !std::isfinite( full_level_emission ) || full_level_emission <= 0.f ) {
        return {};
    }
    std::vector<block_light_source> sources;
    sources.reserve( scene.lights.size() );
    for( const auto &source : scene.lights ) {
        // Directional beams are consumed by the direct-light path.
        if( source.kind == fp3d_snapshot::light_kind::beam || !std::isfinite( source.intensity ) ||
            source.intensity <= 0.f ) {
            continue;
        }
        const float fraction = std::min( 1.f, source.intensity / full_level_emission );
        const int level = std::max( 1, static_cast<int>( std::lround( fraction * max_block_light ) ) );
        sources.push_back( { source.position, level } );
    }
    return sources.empty() ? block_light_levels{} : propagate_block_light( scene_topology( scene ), sources );
}

block_light_levels scene_sky_light( const fp3d_snapshot::scene_snapshot &scene,
                                    const std::vector<tripoint_abs_ms> &exposed )
{
    if( exposed.empty() ) {
        return {};
    }
    const auto topology = scene_topology( scene );
    std::vector<block_light_source> sources;
    sources.reserve( exposed.size() );
    for( const auto &position : exposed ) {
        const auto cell = topology.find( position );
        if( cell != topology.end() && cell->second.passes( light_face::up ) && !cell->second.glass_above ) {
            sources.push_back( { position, max_block_light } );
        }
    }
    return propagate( topology, sources, true );
}

std::optional<std::vector<std::uint32_t>> pack_light_volume( const light_volume_bounds &bounds,
        const block_light_levels &block, const block_light_levels &sky )
{
    constexpr size_t maximum_cells = 4 * 1024 * 1024;
    size_t count = 1;
    for( const auto extent : bounds.extent ) {
        if( extent == 0 || extent > maximum_cells / count ) {
            return std::nullopt;
        }
        count *= extent;
    }
    std::vector<std::uint32_t> volume( count, 0 );
    const auto add = [&]( const block_light_levels &levels, unsigned shift ) {
        const auto &[width, height, depth] = bounds.extent;
        for( const auto &[position, level] : levels ) {
            const std::int64_t x = std::int64_t( position.x() ) - bounds.origin.x();
            const std::int64_t y = std::int64_t( position.y() ) - bounds.origin.y();
            const std::int64_t z = std::int64_t( position.z() ) - bounds.origin.z();
            if( x >= 0 && x < width && y >= 0 && y < height && z >= 0 && z < depth ) {
                const size_t index = static_cast<size_t>( ( z * height + y ) * width + x );
                volume[index] |= static_cast<std::uint32_t>( std::min<int>( level, max_block_light ) ) << shift;
            }
        }
    };
    add( block, 0 );
    add( sky, 4 );
    return volume;
}
} // namespace fp3d_iris
#endif
