#include "fp3d_iris_materials.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <string_view>

#include "fp3d_iris_properties.h"
#include "fp3d_scene.h"

namespace fp3d_iris
{
virtual_material terrain_material( const fp3d_snapshot::cell &tile )
{
    virtual_material material;
    if( tile.open_air || tile.terrain.empty() ) {
        return material;
    }
    material.native_id = "cdda:" + tile.terrain;
    if( tile.water || fp3d_scene::water_surface_offset( tile.terrain ) < 0.f ) {
        material.shader_id = "minecraft:water";
        material.surface = material_surface::water;
        if( tile.water ) {
            material.deep_water = tile.water->depth == fp3d_snapshot::water_depth::deep;
            material.flowing_water = tile.water->current;
            material.salt_water = tile.water->salt;
            material.sewage = tile.water->sewage;
            material.submerged_water = tile.water->submerged;
            return material;
        }
        material.deep_water = tile.terrain.find( "_dp" ) != std::string::npos;
        material.flowing_water = tile.terrain.find( "_moving_" ) != std::string::npos;
        material.salt_water = tile.terrain.compare( 0, 9, "t_swater_" ) == 0;
        material.sewage = tile.terrain == "t_sewage";
        return material;
    }
    using kind = fp3d_snapshot::terrain_kind;
    if( tile.kind == kind::glass || ( tile.kind == kind::window &&
                                     tile.pane == fp3d_snapshot::window_pane::glazed ) ) {
        material.surface = material_surface::transparent;
        material.shader_id = "minecraft:glass";
    } else if( const auto pavement = fp3d_scene::classify_pavement( tile.terrain );
               pavement != fp3d_scene::pavement_surface::none ) {
        material.shader_id = pavement == fp3d_scene::pavement_surface::asphalt ?
                             "minecraft:black_concrete" : "minecraft:gray_concrete";
    } else if( tile.terrain == "t_grass" || tile.terrain == "t_dirt" || tile.terrain == "t_sand" ) {
        material.shader_id = tile.terrain == "t_grass" ? "minecraft:grass_block" :
                             tile.terrain == "t_dirt" ? "minecraft:dirt" : "minecraft:sand";
    }
    return material;
}

material_id_map::material_id_map( const std::vector<material_rule> &rules )
{
    for( const auto &rule : rules ) {
        if( rule.conditional ) {
            continue;
        }
        for( const std::string_view text : rule.selectors ) {
            const auto first = text.find( ':' );
            const auto second = first == text.npos ? first : text.find( ':', first + 1 );
            const auto equals = text.find( '=' );
            const bool namespaced = first != text.npos && ( equals == text.npos || second < equals );
            const auto end_of_id = namespaced ? second : first;
            const auto id = text.substr( 0, end_of_id );
            if( id.empty() || id.front() == '%' || id.front() == ':' || id.find( '=' ) != id.npos ||
                id.back() == ':' ) {
                ++unsupported_;
                continue;
            }
            selector candidate{ rule.id, {} };
            bool valid = true;
            if( end_of_id != text.npos ) {
                auto constraints = text.substr( end_of_id + 1 );
                valid = !constraints.empty();
                while( !constraints.empty() ) {
                    const auto next = constraints.find( ':' );
                    const auto constraint = constraints.substr( 0, next );
                    const auto split = constraint.find( '=' );
                    if( split == constraint.npos || split == 0 || split + 1 == constraint.size() ) {
                        valid = false;
                        break;
                    }
                    auto [state, inserted] = candidate.states.try_emplace(
                            std::string( constraint.substr( 0, split ) ) );
                    if( !inserted ) {
                        valid = false;
                        break;
                    }
                    auto values = constraint.substr( split + 1 );
                    for( ;; ) {
                        const auto comma = values.find( ',' );
                        const auto value = values.substr( 0, comma );
                        if( value.empty() ) {
                            valid = false;
                            break;
                        }
                        state->second.emplace_back( value );
                        if( comma == values.npos ) {
                            break;
                        }
                        values.remove_prefix( comma + 1 );
                    }
                    if( !valid || next == constraints.npos ) {
                        break;
                    }
                    constraints.remove_prefix( next + 1 );
                    valid = !constraints.empty();
                }
            }
            if( valid ) {
                const auto name = namespaced ? std::string( id ) : "minecraft:" + std::string( id );
                selectors_[name].push_back( std::move( candidate ) );
            } else {
                ++unsupported_;
            }
        }
    }
}

std::optional<int> material_id_map::resolve( const virtual_material &material ) const
{
    for( const auto *id : { &material.native_id, &material.shader_id } ) {
        const auto bucket = selectors_.find( *id );
        if( bucket == selectors_.end() ) {
            continue;
        }
        const selector *best = nullptr;
        for( const auto &candidate : bucket->second ) {
            if( best && candidate.states.size() <= best->states.size() ) {
                continue;
            }
            const bool matches = std::all_of( candidate.states.begin(), candidate.states.end(),
            [&]( const auto &constraint ) {
                const auto state = material.states.find( constraint.first );
                return state != material.states.end() &&
                       std::find( constraint.second.begin(), constraint.second.end(), state->second ) !=
                       constraint.second.end();
            } );
            if( matches ) {
                best = &candidate;
            }
        }
        if( best ) {
            return best->id;
        }
    }
    return std::nullopt;
}

size_t material_id_map::unsupported_selectors() const
{
    return unsupported_;
}
} // namespace fp3d_iris
#endif
