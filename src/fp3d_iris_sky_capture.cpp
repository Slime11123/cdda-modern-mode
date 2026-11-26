#include "fp3d_iris_block_light.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <unordered_map>

#include "fp3d_snapshot.h"
#include "level_cache.h"
#include "map.h"
#include "map_scale_constants.h"

namespace fp3d_iris
{
std::vector<tripoint_abs_ms> capture_sky_exposure( const fp3d_snapshot::scene_snapshot &scene,
        const map &here )
{
    std::unordered_map<point_bub_ms, int> lowest_exposed;
    std::vector<tripoint_abs_ms> exposed;
    for( const auto &tile : scene.cells ) {
        if( tile.known != fp3d_snapshot::knowledge::visible || tile.exterior_only ||
            tile.cutaway_ceiling || tile.ceiling_present || tile.glass_ceiling ) {
            continue;
        }
        const auto at = here.get_bub( tile.position );
        if( at.z() < 0 || !here.inbounds( at ) ) {
            continue;
        }
        const auto &level = here.get_cache_ref( at.z() );
        const auto point = at.xy();
        if( level.outside_cache_dirty || level.transparency_cache_dirty.any() ||
            !level.outside_cache[point] || level.transparency_cache[point] <= LIGHT_TRANSPARENCY_SOLID ) {
            continue;
        }
        if( !here.supports_zlevels() ) {
            exposed.push_back( tile.position );
            continue;
        }
        auto [column, inserted] = lowest_exposed.try_emplace( point, 0 );
        if( inserted ) {
            // A floor on level z stops light on its way to z-1. The top of
            // that floor still opens to the sky. Retain only the cutoff.
            for( int z = OVERMAP_HEIGHT; z > 0; --z ) {
                const auto &above = here.get_cache_ref( z );
                if( above.floor_cache_dirty || above.transparency_cache_dirty.any() ||
                    above.transparency_cache[point] <= LIGHT_TRANSPARENCY_SOLID ) {
                    column->second = z + 1;
                    break;
                }
                if( above.floor_cache[point] ) {
                    column->second = z;
                    break;
                }
            }
        }
        if( at.z() >= column->second ) {
            exposed.push_back( tile.position );
        }
    }
    return exposed;
}

block_light_levels scene_sky_light( const fp3d_snapshot::scene_snapshot &scene, const map &here )
{
    return scene_sky_light( scene, capture_sky_exposure( scene, here ) );
}
} // namespace fp3d_iris
#endif
