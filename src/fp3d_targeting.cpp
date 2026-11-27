#include "fp3d_targeting.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <unordered_map>
#include "fp3d_camera.h"
#include "fp3d_materials.h"
#include "fp3d_snapshot.h"
#include "fp3d_ramp.h"
#include "fp3d_scene.h"
namespace fp3d_targeting
{
namespace
{
using vector3 = std::array<float, 3>;

// A slab test also handles horizontal floors and vertical sprite planes.
std::optional<float> intersect( const vector3 &eye, const vector3 &direction,
                                const vector3 &low, const vector3 &high, float max_reach, bool round = false )
{
    float enter = 0;
    float leave = max_reach;
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
    if( round ) {
        const float radius_x = ( high[0] - low[0] ) / 2;
        const float radius_y = ( high[1] - low[1] ) / 2;
        const float x = ( eye[0] - ( low[0] + high[0] ) / 2 ) / radius_x;
        const float y = ( eye[1] - ( low[1] + high[1] ) / 2 ) / radius_y;
        const float dx = direction[0] / radius_x;
        const float dy = direction[1] / radius_y;
        const float a = dx * dx + dy * dy;
        const float b = x * dx + y * dy;
        const float c = x * x + y * y - 1;
        if( a < .000001f ) {
            if( c > 0 ) {
                return std::nullopt;
            }
        } else {
            const float discriminant = b * b - a * c;
            if( discriminant < 0 ) {
                return std::nullopt;
            }
            enter = std::max( enter, ( -b - std::sqrt( discriminant ) ) / a );
            leave = std::min( leave, ( -b + std::sqrt( discriminant ) ) / a );
            if( enter > leave ) {
                return std::nullopt;
            }
        }
    }
    return enter >= fp3d_camera::near_clip ? std::optional<float>( enter ) : std::nullopt;
}
}

std::optional<target> ray( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch,
                           fp3d_camera::offset offset, float max_reach,
                           float screen_x, float screen_y, float aspect, bool camera_facing_sprites,
                           bool current_storey_only )
{
    if( !std::isfinite( yaw ) || !std::isfinite( pitch ) ||
        !std::isfinite( offset.x ) || !std::isfinite( offset.y ) ||
        !std::isfinite( offset.height ) ||
        !std::isfinite( screen_x ) || !std::isfinite( screen_y ) ) {
        return std::nullopt;
    }
    // The overhead camera stands tens of tiles from its pivot, so the bound has
    // to cover the whole distance the caller may legitimately pick through.
    const float reach = std::clamp( max_reach, 1.0f, 160.0f );
    constexpr float radians = 3.14159265358979323846f / 180;
    const float sy = std::sin( yaw * radians );
    const float cy = std::cos( yaw * radians );
    const float cp = std::cos( pitch * radians );
    const float sp = std::sin( pitch * radians );
    // x/y are native horizontal coordinates; z is height in render units.
    const vector3 eye{ offset.x, offset.y, offset.height };
    // Centre ray, exactly the camera direction the renderer uses.
    const vector3 forward{ sy * cp, -cy * cp, -sp };
    // Off-centre rays reuse the same camera basis as the projection: a plain
    // rotation of the view direction by the frustum tangents.
    const float tangent = fp3d_camera::horizontal_tangent();
    const float safe_aspect = aspect > .000001f ? aspect : 1.0f;
    const float tan_h = screen_x * tangent;
    const float tan_v = screen_y * tangent / safe_aspect;
    // right = ( cos(yaw), sin(yaw), 0 ), up = ( sp*sin(yaw), -sp*cos(yaw), cp ).
    vector3 direction{
        forward[0] + cy * tan_h + sp * sy * tan_v,
        forward[1] + sy * tan_h - sp * cy * tan_v,
        forward[2] + cp * tan_v
    };
    const float length = std::sqrt( direction[0] * direction[0] + direction[1] * direction[1] +
                                    direction[2] * direction[2] );
    if( length < .000001f ) {
        return std::nullopt;
    }
    direction[0] /= length;
    direction[1] /= length;
    direction[2] /= length;
    float nearest = reach + .001f;
    float nearest_opaque = nearest;
    // Tile of the nearest opaque surface. A visible actor is not hidden by the
    // floor, the ceiling or the low furniture of the tile it stands on.
    tripoint_abs_ms nearest_opaque_position = tripoint_abs_ms::zero;
    std::vector<std::pair<tripoint_abs_ms, float>> storage_hits;
    std::optional<target> result;
    std::vector<const fp3d_snapshot::cell *> local;
    // Swept box of the tested ray: it keeps the cell scan proportional to the
    // segment instead of the whole snapshot, and it also works when the eye is
    // far outside the visible square (the overhead camera).
    const vector3 far{ eye[0] + direction[0] * reach, eye[1] + direction[1] * reach,
                       eye[2] + direction[2] * reach };
    const float low_x = std::min( eye[0], far[0] ) - 1.5f;
    const float high_x = std::max( eye[0], far[0] ) + 1.5f;
    const float low_y = std::min( eye[1], far[1] ) - 1.5f;
    const float high_y = std::max( eye[1], far[1] ) + 1.5f;
    const float low_z = std::min( eye[2], far[2] ) - fp3d_camera::floor_height;
    const float high_z = std::max( eye[2], far[2] ) + fp3d_camera::floor_height;
    for( const auto &tile : scene.cells ) {
        if( ( current_storey_only && tile.position.z() != scene.origin.z() ) ||
            fp3d_snapshot::hidden_by_cutaway( scene, tile ) ) { continue; }
        const auto p = tile.position - scene.origin;
        const float height = p.z() * fp3d_camera::floor_height;
        if( p.x() >= low_x && p.x() <= high_x && p.y() >= low_y && p.y() <= high_y &&
            height + fp3d_camera::floor_height >= low_z && height <= high_z ) {
            local.push_back( &tile );
        }
    }
    std::unordered_map<tripoint_abs_ms, const fp3d_snapshot::cell *> indexed;
    indexed.reserve( scene.cells.size() );
    for( const auto &tile : scene.cells ) {
        if( fp3d_snapshot::hidden_by_cutaway( scene, tile ) ) { continue; }
        indexed.emplace( tile.position, &tile );
    }
    const auto cell_at = [&]( const tripoint_abs_ms & p ) -> const fp3d_snapshot::cell * {
        const auto it = indexed.find( p );
        return it == indexed.end() ? nullptr : it->second;
    };
    const auto monster_walls = camera_facing_sprites ?
                               fp3d_scene::monster_cutaway_walls( scene, offset, cell_at ) :
                               std::unordered_set<tripoint_abs_ms>{};
    const auto wall_at = [&]( const tripoint_abs_ms & p ) {
        const auto *tile = cell_at( p );
        return tile && ( tile->kind == fp3d_snapshot::terrain_kind::wall ||
                         tile->kind == fp3d_snapshot::terrain_kind::glass ||
                         tile->kind == fp3d_snapshot::terrain_kind::window );
    };
    const auto table_at = [&]( const tripoint_abs_ms &p ) {
        const auto *tile = cell_at( p );
        return tile && fp3d_furniture::chair_table( tile->furniture );
    };
    for( const auto *tile : local ) {
        const auto p = tile->position - scene.origin;
        const float x = p.x();
        const float y = p.y();
        const float z = p.z() * fp3d_camera::floor_height;
        const auto *support = fp3d_ramp::support( *tile, cell_at );
        const bool slope_guardrail = support && support != tile;
        if( slope_guardrail && fp3d_ramp::covered( *support,
            cell_at( support->position + tripoint_rel_ms::above ) ) &&
            cell_at( tile->position + tripoint_rel_ms::above ) ) {
            continue;
        }
        const auto rail_plane = slope_guardrail ? fp3d_ramp::plane( *support, cell_at ) :
                                fp3d_ramp::surface{};
        vector3 box_eye = eye;
        vector3 box_direction = direction;
        box_eye[2] -= rail_plane.height( eye[0] - x, eye[1] - y );
        box_direction[2] -= rail_plane.dx * direction[0] + rail_plane.dy * direction[1];
        const auto accept = [&]( const std::optional<float> &hit, const edge_outline &outline,
                                 kind type = kind::terrain ) {
            if( !hit || *hit < fp3d_camera::near_clip || *hit > reach ) {
                return;
            }
            const bool visible = tile->known == fp3d_snapshot::knowledge::visible && !tile->exterior_only;
            if( visible && ( type == kind::furniture || type == kind::appliance ) &&
                fp3d_furniture::shows_contents( tile->furniture->shape ) ) {
                storage_hits.emplace_back( tile->position, *hit );
            } else if( *hit < nearest_opaque ) {
                nearest_opaque = *hit;
                nearest_opaque_position = tile->position;
            }
            if( *hit < nearest ) {
                nearest = *hit;
                result.reset();
                if( visible ) {
                    result = target{ scene.origin, tile->position, type,
                                     type == kind::appliance ? tile->appliance :
                                     type == kind::furniture ? tile->furniture->id : tile->terrain, outline };
                }
            }
        };
        const auto box = [&]( float dx, float dy, float width, float depth, float bottom, float top,
                              kind type = kind::terrain, bool flat = false, bool round = false ) {
            const float left = x + dx - width / 2;
            const float right = x + dx + width / 2;
            const float north = y + dy - depth / 2;
            const float south = y + dy + depth / 2;
            const float base = z + ( flat ? top : bottom );
            accept( intersect( box_eye, box_direction, { left, north, z + bottom },
                               { right, south, z + top }, reach, round ),
            { {{ { left, base + rail_plane.height( left - x, north - y ), north },
                    { right, base + rail_plane.height( right - x, north - y ), north },
                    { right, base + rail_plane.height( right - x, south - y ), south },
                    { left, base + rail_plane.height( left - x, south - y ), south } }},
                flat ? 0 : top - bottom }, type );
        };
        if( tile->furniture ) {
            const auto shape = tile->furniture->shape;
            const unsigned links = fp3d_scene::bed_links( *tile, cell_at );
            const unsigned facing = links ? 0 : shape == fp3d_furniture::kind::chair ||
                                    shape == fp3d_furniture::kind::camp_chair ?
                                    fp3d_furniture::facing( shape, {},
            { table_at( tile->position + tripoint_rel_ms::north ),
              table_at( tile->position + tripoint_rel_ms::east ),
              table_at( tile->position + tripoint_rel_ms::south ),
              table_at( tile->position + tripoint_rel_ms::west ) } ) :
            fp3d_furniture::facing( shape, {
                wall_at( tile->position + tripoint_rel_ms::north ),
                wall_at( tile->position + tripoint_rel_ms::east ),
                wall_at( tile->position + tripoint_rel_ms::south ),
                wall_at( tile->position + tripoint_rel_ms::west )
            } );
            unsigned index = 0;
            for( const auto &original : fp3d_furniture::parts( shape ) ) {
                const auto segment = links ? fp3d_furniture::bed_segment_part( shape, index, links ) :
                                     std::optional<fp3d_furniture::part>( original );
                ++index;
                if( !segment ) {
                    continue;
                }
                const auto part = fp3d_furniture::rotated( *segment, facing );
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top,
                     tile->appliance.empty() ? kind::furniture : kind::appliance, false, part.round );
            }
        }
        if( tile->kind == fp3d_snapshot::terrain_kind::ladder ||
            tile->kind == fp3d_snapshot::terrain_kind::downspout ) {
            const auto facing = fp3d_furniture::facing( fp3d_furniture::kind::cabinet,
            { wall_at( tile->position + tripoint_rel_ms::north ),
              wall_at( tile->position + tripoint_rel_ms::east ),
              wall_at( tile->position + tripoint_rel_ms::south ),
              wall_at( tile->position + tripoint_rel_ms::west ) } );
            for( const auto &original : fp3d_snapshot::climb_parts( *tile ) ) {
                const auto part = fp3d_furniture::rotated( original, facing );
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top,
                     kind::terrain, false, part.round );
            }
        }
        if( tile->vegetation_height > 0 ) {
            const float height = tile->vegetation_height;
            const float base = z + fp3d_scene::water_surface_offset( *tile ) + .015f;
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
                const float up = eye[2] + distance * direction[2] - base;
                if( std::abs( across ) <= width / 2 && up >= 0.f && up <= height ) {
                    const float half = width / 2;
                    accept( distance, { {{ { x - half, base, y - diagonal * half },
                            { x + half, base, y + diagonal * half },
                            { x + half, base + height, y + diagonal * half },
                            { x - half, base + height, y - diagonal * half } }} } );
                }
            }
        }
        const auto *above = tile->ceiling_present || tile->goes_up || tile->is_ramp ?
                            cell_at( tile->position + tripoint_rel_ms::above ) : nullptr;
        if( tile->is_ramp && !fp3d_ramp::covered( *tile, above ) ) {
            const auto ramp = fp3d_ramp::plane( *tile, cell_at );
            const float denominator = direction[2] - ramp.dx * direction[0] - ramp.dy * direction[1];
            if( std::abs( denominator ) > .000001f ) {
                const float distance = ( z + ramp.height( eye[0] - x, eye[1] - y ) - eye[2] ) /
                                       denominator;
                if( std::abs( eye[0] + distance * direction[0] - x ) <= .5f &&
                    std::abs( eye[1] + distance * direction[1] - y ) <= .5f ) {
                    accept( distance, { {{ { x - .5f, z + ramp.height( -.5f, -.5f ), y - .5f },
                            { x + .5f, z + ramp.height( .5f, -.5f ), y - .5f },
                            { x + .5f, z + ramp.height( .5f, .5f ), y + .5f },
                            { x - .5f, z + ramp.height( -.5f, .5f ), y + .5f } }} } );
                }
            }
        }
        if( tile->floor_present && !tile->is_ramp ) {
            const auto *below = tile->goes_down || tile->terrain == "t_radio_tower" ?
                                cell_at( tile->position + tripoint_rel_ms::below ) : nullptr;
            const bool opening = tile->goes_down && ( tile->kind == fp3d_snapshot::terrain_kind::ladder ||
                                 ( below && below->goes_up ) );
            const bool tower_gap = tile->terrain == "t_radio_tower" &&
                                   below && below->terrain == "t_radio_tower";
            if( !opening && !tower_gap ) {
                box( 0, 0, 1, 1, -.08f, 0, kind::terrain, true );
            }
        }
        const bool upper_floor_surface = above && ( above->floor_present || above->is_ramp );
        if( !current_storey_only && tile->ceiling_present && !slope_guardrail && !upper_floor_surface &&
            !( tile->terrain == "t_radio_tower" && above && above->terrain == "t_radio_tower" ) ) {
            box( 0, 0, 1, 1, fp3d_camera::floor_height - .08f, fp3d_camera::floor_height - .08f );
        }
        if( tile->kind != fp3d_snapshot::terrain_kind::ladder && tile->goes_up && ( ( above && ( above->goes_down || !above->floor_present ) ) ||
                               ( !above && !tile->ceiling_present && !tile->exterior_only &&
                                 tile->known == fp3d_snapshot::knowledge::visible ) ) ) {
            for( const auto &part : fp3d_snapshot::stair_parts( *tile ) ) {
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top );
            }
        }
        using terrain = fp3d_snapshot::terrain_kind;
        const auto model_parts = [&] {
            // Avoid four neighbour lookups for every ordinary floor tile.
            const bool special = tile->kind == terrain::fence ||
                                 fp3d_snapshot::is_chain_gate( tile->terrain ) ||
                                 fp3d_snapshot::is_metal_gate( tile->terrain ) ||
                                 fp3d_snapshot::is_picket_gate( tile->terrain ) ||
                                 tile->terrain == "t_radio_tower" ||
                                 tile->terrain == "t_column" ||
                                 tile->terrain == "t_pillar" ||
                                 tile->terrain == "t_little_column" ||
                                 tile->terrain == "t_column_halfway";
            if( !special ) {
                return std::vector<fp3d_furniture::part>{};
            }
            const auto adjacent = [&]( const tripoint_rel_ms &direction ) {
                const auto *other = cell_at( tile->position + direction );
                return other && fp3d_snapshot::fence_connects( tile->terrain, other->terrain );
            };
            const auto links = fp3d_snapshot::is_prison_barrier( tile->terrain ) ?
                               fp3d_scene::prison_barrier_links( *tile, cell_at ) :
                               std::array<bool, 4>{ adjacent( tripoint_rel_ms::north ),
                                   adjacent( tripoint_rel_ms::east ), adjacent( tripoint_rel_ms::south ),
                                   adjacent( tripoint_rel_ms::west ) };
            const bool along_x = fp3d_snapshot::chain_gate_along_x( links );
            const bool open = tile->terrain.size() >= 2 &&
                              tile->terrain.compare( tile->terrain.size() - 2, 2, "_o" ) == 0;
            const float inward = open ? fp3d_scene::door_inward( *tile, along_x, false, cell_at ) : 0.f;
            if( fp3d_snapshot::is_chain_gate( tile->terrain ) ) {
                return fp3d_snapshot::chain_gate_parts( tile->terrain, along_x, inward );
            }
            return fp3d_snapshot::terrain_model_parts( tile->terrain, links, inward );
        }();
        if( const auto *prop = fp3d_furniture::world_prop( tile->terrain ) ) {
            const unsigned turn = fp3d_scene::world_prop_turn( *tile, *prop, cell_at );
            for( const auto &original : fp3d_furniture::parts( prop->shape ) ) {
                const auto part = fp3d_furniture::rotated( original, ( 4 - turn ) % 4 );
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top );
            }
        } else if( fp3d_scene::glass_wall( *tile ) || tile->terrain == "t_glass_railing" ) {
            for( const auto &part : fp3d_snapshot::glass_barrier_parts(
                     tile->terrain == "t_glass_railing", fp3d_scene::glass_barrier_links( *tile, cell_at ) ) ) {
                if( !part.glass || !fp3d_snapshot::broken_glass_wall( tile->terrain ) ) {
                    box( part.x, part.z, part.width, part.depth, part.bottom, part.top );
                }
            }
        } else if( !model_parts.empty() ) {
            for( const auto &part : model_parts ) {
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top );
            }
        } else if( tile->kind == terrain::fence ) {
            const auto linked = [&]( const tripoint_rel_ms &direction ) {
                const auto *other = cell_at( tile->position + direction );
                return other && fp3d_snapshot::fence_connects( tile->terrain, other->terrain );
            };
            for( const auto &part : fp3d_snapshot::fence_parts(
                { linked( tripoint_rel_ms::north ), linked( tripoint_rel_ms::east ),
                  linked( tripoint_rel_ms::south ), linked( tripoint_rel_ms::west ) },
                tile->terrain == "t_chainfence_posts" ||
                tile->terrain == "t_chickenwire_fence_post" ||
                tile->terrain == "t_fence_post" ) ) {
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top );
            }
        } else if( tile->kind == terrain::wall || tile->kind == terrain::obstacle ) {
            const float top = monster_walls.count( tile->position ) ? fp3d_scene::cutaway_wall_height :
                              tile->kind == terrain::wall ? fp3d_camera::floor_height : 1.f;
            box( 0, 0, .98f, .98f, 0, top );
        } else if( tile->kind == terrain::closed_door || tile->kind == terrain::open_door ||
                   tile->kind == terrain::damaged_door || tile->kind == terrain::glass ||
                   tile->kind == terrain::window ) {
            const int ew = wall_at( tile->position + tripoint_rel_ms::east ) +
                           wall_at( tile->position + tripoint_rel_ms::west );
            const int ns = wall_at( tile->position + tripoint_rel_ms::north ) +
                           wall_at( tile->position + tripoint_rel_ms::south );
            const bool along_x = ew >= ns;
            if( tile->kind == terrain::window ) {
                box( 0, 0, along_x ? 1 : .18f, along_x ? .18f : 1, 0, .9f );
                box( 0, 0, along_x ? 1 : .18f, along_x ? .18f : 1, 2.1f, fp3d_camera::floor_height );
                box( along_x ? -.46f : 0, along_x ? 0 : -.46f, along_x ? .08f : .22f,
                     along_x ? .22f : .08f, .9f, 2.1f );
                box( along_x ? .46f : 0, along_x ? 0 : .46f, along_x ? .08f : .22f,
                     along_x ? .22f : .08f, .9f, 2.1f );
                if( tile->pane != fp3d_snapshot::window_pane::opening &&
                    !fp3d_snapshot::broken_window_glass( tile->terrain ) ) {
                    box( 0, 0, along_x ? .84f : 0, along_x ? 0 : .84f, .9f, 2.1f );
                }
            } else {
                for( const auto &original : fp3d_snapshot::door_parts( tile->kind ) ) {
                    const auto part = fp3d_furniture::rotated( original, along_x ? 0 : 3 );
                    box( part.x, part.z, part.width, part.depth, part.bottom, part.top );
                }
                if( tile->kind == terrain::open_door &&
                    !fp3d_snapshot::closed_door_id( tile->terrain ).empty() ) {
                    const float inward = fp3d_scene::door_inward( *tile, along_x, true, cell_at );
                    const auto leaf = fp3d_furniture::rotated( fp3d_snapshot::swing_door_part(
                        { 0, 0, .88f, .075f, .035f, 2.09f }, inward ), along_x ? 0 : 3 );
                    box( leaf.x, leaf.z, leaf.width, leaf.depth, leaf.bottom, leaf.top );
                }
                if( tile->kind == terrain::glass ) {
                    box( 0, 0, along_x ? .88f : 0, along_x ? 0 : .88f,
                         .06f, fp3d_camera::floor_height - .08f );
                }
            }
        }
    }
    // P3-B: pick the actual native-mounted 3D car parts rather than the
    // terrain tile below the vehicle. Geometry stays in continuous mount
    // space, transformed by the SAME rotation as fp3d_vehicle::root().
    // The target carries the exact native occupancy tile plus part index:
    // the game thread MUST revalidate that identity before any action.
    for( const fp3d_vehicle::vehicle_view &car : scene.vehicles ) {
        if( current_storey_only && car.origin.z() != scene.origin.z() ) { continue; }
        const tripoint_rel_ms rel = car.origin - scene.origin;
        const float cx = static_cast<float>( rel.x() );
        const float cz = static_cast<float>( rel.y() );
        const float base = rel.z() * fp3d_camera::floor_height;
        const float angle = car.angle_degrees * radians;
        const float cosine = std::cos( angle );
        const float sine = std::sin( angle );
        const auto local_point = [&]( const vector3 &point ) {
            const float wx = point[0] - cx;
            const float wz = point[1] - cz;
            return vector3{ cosine * wx + sine * wz, -sine * wx + cosine * wz, point[2] - base };
        };
        const vector3 eye_local = local_point( eye );
        const vector3 ray_local{ cosine * direction[0] + sine * direction[1],
                                 -sine * direction[0] + cosine * direction[1], direction[2] };
        // Matching the enclosed-cabin geometry scale used for the opaque
        // vehicle pass. A detached or open frame remains natural height.
        const int frame_count = std::count_if( car.parts.begin(), car.parts.end(),
        []( const auto &p ) { return p.kind == fp3d_vehicle::role::frame; } );
        const int roof_count = std::count_if( car.parts.begin(), car.parts.end(),
        []( const auto &p ) { return p.kind == fp3d_vehicle::role::roof && !p.broken; } );
        const int door_count = std::count_if( car.parts.begin(), car.parts.end(),
        []( const auto &p ) { return p.kind == fp3d_vehicle::role::door; } );
        const int window_count = std::count_if( car.parts.begin(), car.parts.end(),
        []( const auto &p ) { return p.kind == fp3d_vehicle::role::window; } );
        const float vertical_scale = frame_count >= 5 &&
                                     ( roof_count >= 1 || ( door_count >= 2 && window_count >= 1 ) ) ?
                                     1.36f : 1.0f;
        for( const fp3d_vehicle::part_view &part : car.parts ) {
            const auto *tile = cell_at( part.native_position );
            if( part.index < 0 || part.hidden || !tile ||
                tile->known != fp3d_snapshot::knowledge::visible || tile->exterior_only ) {
                continue; // never reveal a vehicle component behind LOS
            }
            using role = fp3d_vehicle::role;
            float width = .82f, depth = .78f, bottom = .20f, top = 1.3f;
            switch( part.kind ) {
                case role::wheel:
                    width = .82f; depth = .82f;
                    bottom = .015f; top = .93f;
                    break;
                case role::door:
                    width = .90f; depth = .90f;
                    // V9 closed native door stops below the upper window
                    // aperture. A selected closed door must not block a
                    // ray aimed at the sky through its missing glazing.
                    bottom = .31f;
                    top = part.open ? 1.29f :
                          part.broken ? .65f : .84f;
                    break;
                case role::roof:
                    width = .93f; depth = .93f;
                    bottom = 1.30f; top = 1.43f;
                    break;
                case role::seat:
                    width = .61f; depth = .63f;
                    bottom = .31f; top = part.broken ? .75f : 1.09f;
                    break;
                case role::cargo:
                    width = .75f; depth = .75f;
                    bottom = .34f; top = .61f;
                    break;
                case role::window:
                    bottom = .80f; top = 1.43f;
                    break;
                case role::frame:
                    width = .78f; depth = .78f;
                    bottom = .10f; top = .27f;
                    break;
                case role::engine:
                    bottom = .30f; top = .72f;
                    break;
                default:
                    break;
            }
            bottom *= vertical_scale;
            top *= vertical_scale;
            const float center_x = part.mount_x - car.pivot_x;
            const float center_z = part.mount_y - car.pivot_y;
            const float left = center_x - width * .5f;
            const float right = center_x + width * .5f;
            const float north = center_z - depth * .5f;
            const float south = center_z + depth * .5f;
            const auto hit = intersect( eye_local, ray_local,
                                       { left, north, bottom }, { right, south, top }, reach );
            if( !hit || *hit >= nearest || *hit >= nearest_opaque - .015f ) {
                continue;
            }
            // Rendered outline shares the same rigid local mount root:
            // XZ corners rotate with the car; heights follow its cabin scale.
            edge_outline outline;
            outline.height = top - bottom;
            for( int corner = 0; corner != 4; ++corner ) {
                const float lx = ( corner == 0 || corner == 3 ) ? left : right;
                const float lz = corner <= 1 ? north : south;
                outline.corners[corner] = {
                    cx + cosine * lx - sine * lz,
                    base + bottom,
                    cz + sine * lx + cosine * lz
                };
            }
            nearest = *hit;
            // The same selected car panel also occludes item/sprite picking;
            // otherwise items behind a solid door could replace this hit.
            if( nearest < nearest_opaque ) {
                nearest_opaque = nearest;
                nearest_opaque_position = part.native_position;
            }
            result = target{ scene.origin, part.native_position, kind::vehicle, part.id,
                             outline, part.index, car.origin, part.mount_x, part.mount_y };
        }
    }
    float nearest_sprite = reach + .001f;
    for( const auto &image : scene.sprites ) {
        if( image.kind == fp3d_snapshot::sprite_kind::held_item ||
            image.kind == fp3d_snapshot::sprite_kind::field ) {
            continue;
        }
        // The overhead avatar sprite is the pointer target's own tile, never a
        // creature to attack or examine through itself.
        if( image.is_avatar || ( current_storey_only && image.position.z() != scene.origin.z() ) ) {
            continue;
        }
        const auto p = image.position - scene.origin;
        const auto *on = cell_at( image.position );
        const bool lying_bedding = image.kind == fp3d_snapshot::sprite_kind::item && on &&
                                  on->furniture &&
                                  ( on->furniture->shape == fp3d_furniture::kind::bed ||
                                    on->furniture->shape == fp3d_furniture::kind::bed_frame );
        if( lying_bedding ) {
            // Bed items are rendered as a flat surface, not a camera-facing
            // upright sprite; use the matching horizontal picking plane.
            if( std::abs( direction[2] ) < .000001f ) {
                continue;
            }
            const float plane = p.z() * fp3d_camera::floor_height +
                                image.elevation + .045f;
            const float distance = ( plane - eye[2] ) / direction[2];
            if( distance < fp3d_camera::near_clip || distance > reach ||
                distance >= nearest_sprite ||
                std::abs( eye[0] + distance * direction[0] - p.x() ) > .34f ||
                std::abs( eye[1] + distance * direction[1] - p.y() ) > .35f ) {
                continue;
            }
            if( distance >= nearest ) {
                const bool blocked = distance >= nearest_opaque ||
                                     std::any_of( storage_hits.begin(), storage_hits.end(),
                                     [&]( const auto &hit ) {
                    return hit.first != image.position && hit.second <= distance;
                } );
                if( blocked ) {
                    continue;
                }
            }
            nearest_sprite = distance;
            result = target{ scene.origin, image.position, kind::item, image.id };
            continue;
        }
        // The sprite quad is upright and camera-facing. Intersect the (possibly
        // off-centre) ray with its own vertical plane, then compare in the
        // camera's own screen axes so the overhead pointer stays exact.
        const vector3 right{ cy, sy, 0 };
        const vector3 up = camera_facing_sprites ? vector3{ sp * sy, -sp * cy, cp } : vector3{ 0, 0, 1 };
        const vector3 normal{ right[1] * up[2], -right[0] * up[2], right[0] * up[1] - right[1] * up[0] };
        const float denominator = direction[0] * normal[0] + direction[1] * normal[1] + direction[2] * normal[2];
        if( std::abs( denominator ) < .000001f ) { continue; }
        const vector3 base{ static_cast<float>( p.x() ), static_cast<float>( p.y() ),
                            p.z() * fp3d_camera::floor_height + image.elevation + .04f };
        const float distance = ( ( base[0] - eye[0] ) * normal[0] +
                                 ( base[1] - eye[1] ) * normal[1] + ( base[2] - eye[2] ) * normal[2] ) / denominator;
        const vector3 hit{ eye[0] + distance * direction[0] - base[0],
                           eye[1] + distance * direction[1] - base[1],
                           eye[2] + distance * direction[2] - base[2] };
        const float side = hit[0] * right[0] + hit[1] * right[1];
        const float rise = hit[0] * up[0] + hit[1] * up[1] + hit[2] * up[2];
        const float height = image.height * image.display_scale;
        const float top = height - .04f;
        if( distance < fp3d_camera::near_clip || distance > reach || distance >= nearest_sprite ||
            height <= .04f || std::abs( side ) > height / 2 || rise < 0 || rise > top ) { continue; }
        if( const auto *alpha = fp3d_materials::sprite_alpha( image, SDL_GetTicks() ) ) {
            float u = side / height + .5f;
            if( image.mirrored ) { u = 1.f - u; }
            const float v = 1.f - rise / top;
            if( !alpha->opaque( std::min( alpha->width - 1, static_cast<int>( u * alpha->width ) ),
                                std::min( alpha->height - 1, static_cast<int>( v * alpha->height ) ) ) ) { continue; }
        }
        const bool item = image.kind == fp3d_snapshot::sprite_kind::item;
        // Hover priority: a visible creature or NPC outranks the surfaces of
        // the tile it stands on, because the pointer rests exactly there when
        // you look at it - its own floor, its own ceiling and the low furniture
        // it shares the tile with can never hide it. Anything genuinely in
        // front still blocks: walls, doors, windows, closed vehicles, terrain
        // on another tile, and storage cabinets, which keep hiding whoever is
        // inside them. Items keep their original surface rule unchanged.
        const bool storage_blocks = std::any_of( storage_hits.begin(), storage_hits.end(),
        [&]( const auto &hit ) {
            return hit.second <= distance && ( !item || hit.first != image.position );
        } );
        const bool opaque_blocks = item ?
                                   distance >= nearest_opaque :
                                   ( distance >= nearest_opaque &&
                                     nearest_opaque_position != image.position );
        if( opaque_blocks || storage_blocks ) {
            continue;
        }
        nearest_sprite = distance;
        result = target{ scene.origin, image.position, item ? kind::item : kind::creature, image.id };
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
