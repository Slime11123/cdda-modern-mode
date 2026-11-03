#include "fp3d_targeting.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <unordered_map>
#include "fp3d_camera.h"
#include "fp3d_snapshot.h"
namespace fp3d_targeting
{
namespace
{
using vector3 = std::array<float, 3>;
constexpr float reach = 6;

// A slab test also handles horizontal floors and vertical sprite planes.
std::optional<float> intersect( const vector3 &eye, const vector3 &direction,
                                const vector3 &low, const vector3 &high )
{
    float enter = 0;
    float leave = reach;
    for( int axis = 0; axis < 3; ++axis ) {
        if( std::abs( direction[axis] ) < .000001f ) {
            if( eye[axis] < low[axis] || eye[axis] > high[axis] ) {
                return std::nullopt;
            }
        } else {
            float a = ( low[axis] - eye[axis] ) / direction[axis];
            float b = ( high[axis] - eye[axis] ) / direction[axis];
            if( a > b ) {
                std::swap( a, b );
            }
            enter = std::max( enter, a );
            leave = std::min( leave, b );
            if( enter > leave ) {
                return std::nullopt;
            }
        }
    }
    return enter >= fp3d_camera::near_clip ? std::optional<float>( enter ) : std::nullopt;
}
}

std::optional<target> ray( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch,
                           fp3d_camera::offset offset )
{
    if( !std::isfinite( yaw ) || !std::isfinite( pitch ) ||
        !std::isfinite( offset.x ) || !std::isfinite( offset.y ) ) {
        return std::nullopt;
    }
    constexpr float radians = 3.14159265358979323846f / 180;
    const float sy = std::sin( yaw * radians );
    const float cy = std::cos( yaw * radians );
    const float cp = std::cos( pitch * radians );
    // x/y are native horizontal coordinates; z is height in render units.
    const vector3 eye{ offset.x, offset.y, fp3d_camera::eye_height };
    const vector3 direction{ sy * cp, -cy * cp, -std::sin( pitch * radians ) };
    float nearest = reach + .001f;
    std::optional<target> result;
    std::vector<const fp3d_snapshot::cell *> local;
    for( const auto &tile : scene.cells ) {
        const auto p = tile.position - scene.origin;
        if( std::abs( p.x() - offset.x ) <= reach + 1 &&
            std::abs( p.y() - offset.y ) <= reach + 1 &&
            std::abs( p.z() * fp3d_camera::floor_height - eye[2] ) <= reach + fp3d_camera::floor_height ) {
            local.push_back( &tile );
        }
    }
    std::unordered_map<tripoint_abs_ms, const fp3d_snapshot::cell *> indexed;
    indexed.reserve( local.size() );
    for( const auto *tile : local ) {
        indexed.emplace( tile->position, tile );
    }
    const auto cell_at = [&]( const tripoint_abs_ms & p ) -> const fp3d_snapshot::cell * {
        const auto it = indexed.find( p );
        return it == indexed.end() ? nullptr : it->second;
    };
    const auto wall_at = [&]( const tripoint_abs_ms & p ) {
        const auto *tile = cell_at( p );
        return tile && ( tile->kind == fp3d_snapshot::terrain_kind::wall ||
                         tile->kind == fp3d_snapshot::terrain_kind::window );
    };
    for( const auto *tile : local ) {
        const auto p = tile->position - scene.origin;
        const float x = p.x();
        const float y = p.y();
        const float z = p.z() * fp3d_camera::floor_height;
        const auto accept = [&]( const std::optional<float> &hit ) {
            if( hit && *hit >= fp3d_camera::near_clip && *hit <= reach && *hit < nearest ) {
                nearest = *hit;
                result.reset();
                if( tile->known == fp3d_snapshot::knowledge::visible && !tile->exterior_only ) {
                    result = target{ scene.origin, tile->position, kind::terrain, tile->terrain };
                }
            }
        };
        const auto box = [&]( float dx, float dy, float width, float depth, float bottom, float top ) {
            accept( intersect( eye, direction,
            { x + dx - width / 2, y + dy - depth / 2, z + bottom },
            { x + dx + width / 2, y + dy + depth / 2, z + top } ) );
        };
        if( tile->furniture ) {
            const unsigned facing = fp3d_furniture::facing( tile->furniture->shape, {
                wall_at( tile->position + tripoint_rel_ms::north ),
                wall_at( tile->position + tripoint_rel_ms::east ),
                wall_at( tile->position + tripoint_rel_ms::south ),
                wall_at( tile->position + tripoint_rel_ms::west )
            } );
            for( const auto &original : fp3d_furniture::parts( tile->furniture->shape ) ) {
                const auto part = fp3d_furniture::rotated( original, facing );
                const auto hit = intersect( eye, direction,
                { x + part.x - part.width / 2, y + part.z - part.depth / 2, z + part.bottom },
                { x + part.x + part.width / 2, y + part.z + part.depth / 2, z + part.top } );
                if( hit && *hit >= fp3d_camera::near_clip && *hit <= reach && *hit < nearest ) {
                    nearest = *hit;
                    result.reset();
                    if( tile->known == fp3d_snapshot::knowledge::visible && !tile->exterior_only ) {
                        result = target{ scene.origin, tile->position, kind::furniture, tile->furniture->id };
                    }
                }
            }
        }
        if( tile->vegetation_height > 0 ) {
            const float height = tile->vegetation_height;
            const float width = std::max( .9f, height <= 1.2f ? height : height * .7f );
            for( const float diagonal : {
                     -1.0f, 1.0f
                 } ) {
                const float denominator = direction[1] - diagonal * direction[0];
                if( std::abs( denominator ) < .000001f ) {
                    continue;
                }
                const float distance = ( y - eye[1] + diagonal * ( eye[0] - x ) ) / denominator;
                const float across = eye[0] + distance * direction[0] - x;
                const float up = eye[2] + distance * direction[2] - z;
                if( std::abs( across ) <= width / 2 && up >= .015f && up <= height + .015f ) {
                    accept( distance );
                }
            }
        }
        const auto *above = tile->ceiling_present || tile->goes_up ?
                            cell_at( tile->position + tripoint_rel_ms::above ) : nullptr;
        if( tile->is_ramp ) {
            // The renderer slopes along native y, inside this tile only.
            const float slope = tile->ramp_delta * fp3d_camera::floor_height;
            const float denominator = direction[2] + slope * direction[1];
            if( std::abs( denominator ) > .000001f ) {
                const float distance = ( z + slope * ( .5f - eye[1] + y ) - eye[2] ) / denominator;
                if( std::abs( eye[0] + distance * direction[0] - x ) <= .5f &&
                    std::abs( eye[1] + distance * direction[1] - y ) <= .5f ) {
                    accept( distance );
                }
            }
        }
        if( tile->floor_present && !tile->is_ramp ) {
            const auto *below = tile->goes_down ? cell_at( tile->position + tripoint_rel_ms::below ) : nullptr;
            const bool opening = below && below->goes_up;
            box( opening ? -.25f : 0, 0, opening ? .5f : 1, 1, -.08f, 0 );
        }
        const bool upper_floor_surface = above && above->floor_present && !above->is_ramp;
        if( tile->ceiling_present && !upper_floor_surface ) {
            box( 0, 0, 1, 1, fp3d_camera::floor_height - .08f, fp3d_camera::floor_height - .08f );
        }
        if( tile->goes_up && ( ( above && ( above->goes_down || !above->floor_present ) ) ||
                               ( !above && !tile->ceiling_present && !tile->exterior_only &&
                                 tile->known == fp3d_snapshot::knowledge::visible ) ) ) {
            for( int step = 0; step < 12; ++step ) {
                box( .25f, .5f - ( step + .5f ) / 12, .5f, 1.0f / 12,
                     0, fp3d_camera::floor_height * ( step + 1 ) / 12 );
            }
        }
        using terrain = fp3d_snapshot::terrain_kind;
        if( tile->kind == terrain::wall || tile->kind == terrain::obstacle ) {
            box( 0, 0, .98f, .98f, 0, tile->kind == terrain::wall ? fp3d_camera::floor_height : 1 );
        } else if( tile->kind == terrain::closed_door || tile->kind == terrain::open_door ||
                   tile->kind == terrain::window ) {
            const int ew = wall_at( tile->position + tripoint_rel_ms::east ) +
                           wall_at( tile->position + tripoint_rel_ms::west );
            const int ns = wall_at( tile->position + tripoint_rel_ms::north ) +
                           wall_at( tile->position + tripoint_rel_ms::south );
            const bool along_x = ew >= ns;
            const float width = along_x ? .98f : .16f;
            const float depth = along_x ? .16f : .98f;
            if( tile->kind == terrain::closed_door ) {
                box( 0, 0, width, depth, 0, fp3d_camera::floor_height );
            } else if( tile->kind == terrain::open_door ) {
                box( along_x ? -.44f : 0, along_x ? 0 : -.44f, .12f, .12f, 0, fp3d_camera::floor_height );
                box( along_x ? .44f : 0, along_x ? 0 : .44f, .12f, .12f, 0, fp3d_camera::floor_height );
                box( 0, 0, width, depth, 2.1f, fp3d_camera::floor_height );
            } else {
                box( 0, 0, along_x ? 1 : .18f, along_x ? .18f : 1, 0, .9f );
                box( 0, 0, along_x ? 1 : .18f, along_x ? .18f : 1, 2.1f, fp3d_camera::floor_height );
                box( along_x ? -.46f : 0, along_x ? 0 : -.46f, along_x ? .08f : .22f,
                     along_x ? .22f : .08f, .9f, 2.1f );
                box( along_x ? .46f : 0, along_x ? 0 : .46f, along_x ? .08f : .22f,
                     along_x ? .22f : .08f, .9f, 2.1f );
                if( tile->pane != fp3d_snapshot::window_pane::opening ) {
                    box( 0, 0, along_x ? .84f : 0, along_x ? 0 : .84f, .9f, 2.1f );
                }
            }
        }
    }
    for( const auto &image : scene.sprites ) {
        if( image.kind == fp3d_snapshot::sprite_kind::held_item ) {
            continue;
        }
        const auto p = image.position - scene.origin;
        // The sprite quad is upright and camera-facing, as in fp3d_scene.
        const float forward = sy * ( p.x() - eye[0] ) - cy * ( p.y() - eye[1] );
        if( cp <= .000001f ) {
            continue;
        }
        const float distance = forward / cp;
        const float side = cy * ( eye[0] + distance * direction[0] - p.x() ) +
                           sy * ( eye[1] + distance * direction[1] - p.y() );
        const float height = eye[2] + distance * direction[2] -
                             p.z() * fp3d_camera::floor_height - image.elevation;
        if( distance >= fp3d_camera::near_clip && distance <= reach && distance < nearest &&
            std::abs( side ) <= image.height / 2 && height >= .04f && height <= image.height ) {
            nearest = distance;
            result = target{ scene.origin, image.position,
                             image.kind == fp3d_snapshot::sprite_kind::item ? kind::item : kind::creature, image.id };
        }
    }
    return result;
}

std::optional<target> adjacent( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch )
{
    if( pitch < -65 ) {
        return std::nullopt;
    }
    const auto position = scene.origin + ( pitch > 65 ? tripoint_rel_ms::zero :
                                           fp3d_camera::movement( yaw, 1, 0 ) );
    const auto cell = std::find_if( scene.cells.begin(), scene.cells.end(), [&]( const auto & tile ) {
        return tile.position == position && tile.known == fp3d_snapshot::knowledge::visible &&
               !tile.exterior_only;
    } );
    if( cell == scene.cells.end() ) {
        return std::nullopt;
    }
    target result{ scene.origin, position, kind::terrain, cell->terrain };
    for( const auto &image : scene.sprites ) {
        if( image.position != position ) {
            continue;
        }
        if( image.kind == fp3d_snapshot::sprite_kind::monster ||
            image.kind == fp3d_snapshot::sprite_kind::character ) {
            return target{ scene.origin, position, kind::creature, image.id };
        }
        if( image.kind == fp3d_snapshot::sprite_kind::item ) {
            result.type = kind::item;
            result.id = image.id;
        }
    }
    return result;
}
}
#endif
