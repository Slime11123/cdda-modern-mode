#include "fp3d_vehicle_renderer.h"

#if defined(TILES) && defined(ENABLE_FP3D)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>


#include "fp3d_geometry_vehicle.h"
#include "fp3d_gpu_camera_math.h"
#include "fp3d_materials.h"
#include "fp3d_snapshot.h"

namespace fp3d_vehicle
{
namespace
{

constexpr float pi = 3.14159265358979323846f;
// 2D vehicle mounts are spaced one map tile apart in X/Z, but original
// procedural cab height was just 1.40 world units (below the player's
// eye level).  Make *only closed automobile cabins* roughly 1.9 units
// tall, without resizing their mount coordinates or stretching the tires.
// This is a presentation-only transform and cannot change CDDA physics.
constexpr float enclosed_cab_vertical_scale = 1.36f;

void blend_hash( std::uint64_t &seed, std::uint64_t value )
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + ( seed << 6 ) + ( seed >> 2 );
}


// One small upright sticker on a real door, not the same sprite stretched
// around every side of the entire body. The rest is same-pack-neutral metal.
template<typename Sink>
void emit_door_sticker( Sink &sink,  const std::optional<fp3d_materials::uv_rect> &art,
                   float width, float bottom, float top )
{
    if( !art ) {
        return;
    }
    sink.textured( true );
    sink.alpha_cutoff( .25f );
    sink.color( 1, 1, 1 );
    for( float side : { -.057f, .057f } ) {
        sink.begin( fp3d_geometry::primitive::quads );
        sink.normal( 0, 0, side > 0 ? 1 : -1 );
        sink.texcoord( art->left, art->bottom );
        sink.point( -.5f * width, bottom, side );
        sink.texcoord( art->right, art->bottom );
        sink.point( .5f * width, bottom, side );
        sink.texcoord( art->right, art->top );
        sink.point( .5f * width, top, side );
        sink.texcoord( art->left, art->top );
        sink.point( -.5f * width, top, side );
        sink.end();
    }
    sink.alpha_cutoff( std::nullopt );
}


struct panel_pose {
    bool along_x = true; // plane runs along local X or local Z
    float outward = 1;
    float nx = 0;
    float nz = 1;
};

// A vehicle can be an L-shaped or single-tile assembly, so never presume
// sedan geometry. Choose a side plane using REAL mounted part positions.
// Cosmetic variant hints disambiguate vertical/horizontal windshields.
// All positions remain local and independent of the car's world heading.
panel_pose panel_at( const vehicle_view &v, const part_view &p )
{
    int lo_x = p.mount_x, hi_x = p.mount_x;
    int lo_z = p.mount_y, hi_z = p.mount_y;
    bool found_frame = false;
    for( const part_view &other : v.parts ) {
        if( other.kind != role::frame ) {
            continue;
        }
        if( !found_frame ) {
            lo_x = hi_x = other.mount_x;
            lo_z = hi_z = other.mount_y;
            found_frame = true;
        } else {
            lo_x = std::min( lo_x, other.mount_x );
            hi_x = std::max( hi_x, other.mount_x );
            lo_z = std::min( lo_z, other.mount_y );
            hi_z = std::max( hi_z, other.mount_y );
        }
    }
    if( !found_frame ) {
        for( const part_view &other : v.parts ) {
            lo_x = std::min( lo_x, other.mount_x );
            hi_x = std::max( hi_x, other.mount_x );
            lo_z = std::min( lo_z, other.mount_y );
            hi_z = std::max( hi_z, other.mount_y );
        }
    }
    const float mid_x = ( lo_x + hi_x ) * .5f;
    const float mid_z = ( lo_z + hi_z ) * .5f;
    const float rx = std::abs( p.mount_x - mid_x );
    const float rz = std::abs( p.mount_y - mid_z );
    panel_pose pose;
    if( p.variant.find( "vertical" ) != std::string::npos ) {
        pose.along_x = true;
    } else if( p.variant.find( "horizontal" ) != std::string::npos ) {
        pose.along_x = false;
    } else {
        // Side panels occur at large lateral offsets, front/rear panels
        // at large longitudinal offsets. Normalize by the body footprint
        // only when one dimension has meaningful extent.
        const float dx = std::max( 1.0f, float( hi_x - lo_x ) );
        const float dz = std::max( 1.0f, float( hi_z - lo_z ) );
        pose.along_x = rz / dz >= rx / dx;
    }
    pose.outward = ( pose.along_x ? p.mount_y >= mid_z : p.mount_x >= mid_x ) ? 1 : -1;
    pose.nx = pose.along_x ? 0.0f : pose.outward;
    pose.nz = pose.along_x ? pose.outward : 0.0f;
    return pose;
}



// A generic CDDA car is NOT an array of independent furniture blocks.
// Different parts (frame, door, window, roof, trunk, equipment, wheels) often
// occupy the SAME mount cell. Combine them into one coherent body footprint;
// only visible, actually installed parts contribute. Bikes and custom open
// frames keep their mechanical presentation instead of gaining phantom walls.
struct shell_cell {
    bool frame = false;
    bool broken_frame = false;
    bool wheel = false; // wheel arch must stay open in painted side panel
    bool body = false;
    bool body_broken = false;
    bool halfboard = false; // thin waist panel, not an opaque greenhouse wall
    bool door = false;
    bool door_open = false;
    bool door_broken = false;
    bool glazing = false;
    bool window = false;
    bool window_broken = false;
    bool roof = false;
    bool broken_roof = false;
    const part_view *body_art = nullptr;
    const part_view *roof_art = nullptr;
};

struct shell_profile {
    using key = std::pair<int, int>;
    std::map<key, shell_cell> cells;
    bool enclosed = false;
    bool pedal_cycle = false;
    // CDDA 0.I's Electric Tandem Cabin Motorcycle inherits a three-wide
    // Supercompact Tandem Car body, but its TWO motorbike wheels share the
    // same centerline. Do not treat its rear wheel as a side car wheel.
    bool centerline_motorcycle_cabin = false;
    std::array<float, 3> paint{ .40f, .43f, .47f };
};

shell_profile build_shell( const vehicle_view &v )
{
    shell_profile profile;
    unsigned frame_count = 0;
    unsigned roof_count = 0;
    unsigned edge_count = 0;
    unsigned doors = 0;
    unsigned windows = 0;
    unsigned narrow_wheels = 0;
    unsigned total_wheels = 0;
    int first_wheel_y = 0;
    bool same_wheel_centerline = true;
    const part_view *paint_source = nullptr;
    int paint_priority = -1;
    for( const part_view &part : v.parts ) {
        if( part.id == "foot_pedals" || part.id.find( "wheel_bicycle" ) != std::string::npos ) {
            profile.pedal_cycle = true;
        }
        if( part.kind == role::wheel ) {
            // Native centreline bicycle/motorbike wheels must not be
            // reinterpreted as left/right wheels of a four-wheeled sedan.
            profile.cells[{part.mount_x,part.mount_y}].wheel = true;
            if( total_wheels == 0 ) {
                first_wheel_y = part.mount_y;
            } else if( part.mount_y != first_wheel_y ) {
                same_wheel_centerline = false;
            }
            ++total_wheels;
            if( part.id.find( "motorbike" ) != std::string::npos ) {
                ++narrow_wheels;
            }
            continue;
        }
        const bool shell_part = part.kind == role::frame || part.kind == role::body ||
                                part.kind == role::roof || part.kind == role::door ||
                                part.kind == role::window || part.kind == role::armor;
        if( !shell_part ) {
            continue;
        }
        auto &cell = profile.cells[{ part.mount_x, part.mount_y }];
        switch( part.kind ) {
            case role::frame:
                cell.frame = true;
                cell.broken_frame = part.broken;
                ++frame_count;
                break;
            case role::body:
                cell.body = true;
                cell.halfboard = cell.halfboard ||
                                 part.id.find( "halfboard" ) != std::string::npos;
                if( !cell.body_art || ( cell.body_art->broken && !part.broken ) ) {
                    cell.body_art = &part;
                    cell.body_broken = part.broken;
                }
                ++edge_count;
                break;
            case role::door:
                cell.door = true;
                cell.door_open = part.open;
                cell.door_broken = part.broken;
                cell.glazing = part.glazed_door && !part.broken;
                ++doors;
                ++edge_count;
                break;
            case role::window:
                cell.window = true;
                cell.window_broken = part.broken;
                ++windows;
                ++edge_count;
                break;
            case role::roof:
                cell.roof = true;
                cell.broken_roof = part.broken;
                cell.roof_art = &part;
                ++roof_count;
                break;
            default:
                break;
        }
        const int priority = part.broken ? 0 :
                             part.kind == role::roof ? 5 :
                             part.kind == role::body ?
                             ( part.id.find( "board" ) != std::string::npos ? 4 : 2 ) :
                             part.kind == role::door ? 3 : -1;
        if( priority > paint_priority &&
            fp3d_materials::vehicle_body_color( fp3d_materials::vehicle_key( part ) ) ) {
            paint_source = &part;
            paint_priority = priority;
        }
    }
    if( paint_source ) {
        profile.paint = *fp3d_materials::vehicle_body_color(
                            fp3d_materials::vehicle_key( *paint_source ) );
    }
    // A car with a torn-off roof is still a car: the former >=2 roof
    // rule silently dropped wrecks back to the bare scaffolding renderer.
    // A bike or pushcart has neither a substantial perimeter nor a
    // cabin opening, and stays in the open-frame rendering path.
    profile.enclosed = frame_count >= 5 && edge_count >= 3 &&
                       ( roof_count >= 1 || ( doors >= 2 && windows >= 1 ) );
    // Pedals installed in a wide custom vehicle do not make it a bicycle.
    if( profile.pedal_cycle ) {
        const auto frame = std::find_if( v.parts.begin(), v.parts.end(), []( const part_view &p ) {
            return p.kind == role::frame;
        } );
        profile.pedal_cycle = !profile.enclosed && frame != v.parts.end() &&
                             std::all_of( v.parts.begin(), v.parts.end(), [&]( const part_view &p ) {
            return p.kind != role::frame || p.mount_y == frame->mount_y;
        } );
    }
    // Detect inherited special compact-bike assemblies from their
    // ACTUAL current mounted parts. Works after swapping batteries, doors,
    // model names or translations; stripped/modified wheel layouts fall
    // back to the generic correct native-part rendering path.
    profile.centerline_motorcycle_cabin =
        profile.enclosed && roof_count >= 1 && frame_count >= 5 &&
        total_wheels == 2 && narrow_wheels == 2 && same_wheel_centerline;
    return profile;
}

// For closed cars draw only a single continuous outer lower body around the
// original mounts. This is what the prior renderer was missing: standalone
// frame slabs and 2m-tall glass squares formed an exposed steel scaffold.
// Every side is conditioned on a real external edge; modded L-shaped vehicles
// retain their exact native outline.

// V5: A car is a continuous front / cabin / rear silhouette, not a stack
// of one red cube for each vpart. +X is the vehicle's native forward axis;
// +Z is its local right side. All dimensions are from real CDDA mounts.
// A special purpose car without enough native roof/body stays a rigid,
// open-frame vehicle rather than receiving a fictitious sedan.
struct point3 {
    float x = 0;
    float y = 0;
    float z = 0;
};
using color3 = std::array<float, 3>;

template<typename Sink>
void emit_painted_quad( Sink &sink,  const point3 &a, const point3 &b,
                   const point3 &c, const point3 &d, const color3 &paint )
{
    const point3 u{ b.x - a.x, b.y - a.y, b.z - a.z };
    const point3 v{ d.x - a.x, d.y - a.y, d.z - a.z };
    const point3 normal{ u.y * v.z - u.z * v.y,
                         u.z * v.x - u.x * v.z,
                         u.x * v.y - u.y * v.x };
    sink.textured( false );
    sink.color( paint[0], paint[1], paint[2] );
    sink.begin( fp3d_geometry::primitive::quads );
    sink.normal( normal.x, normal.y, normal.z );
    for( const point3 &p : { a, b, c, d } ) {
        sink.point( p.x, p.y, p.z );
    }
    sink.end();
}


// V10: A frame is not a GL_LINE and not a paper-thin painted QUAD.
// Construct a real six-sided beam between native-cabin connection points.
// This is entirely visual (no new vehicle parts/collision or seat mounts).
template<typename Quad>
void cabin_frame_beam( const Quad &quad, const point3 &a, const point3 &b,
                       float thickness, const color3 &paint )
{
    const point3 axis{ b.x - a.x, b.y - a.y, b.z - a.z };
    const float length = std::sqrt( axis.x * axis.x +
                                    axis.y * axis.y + axis.z * axis.z );
    if( length < .001f || thickness <= 0 ) {
        return;
    }
    const point3 d{ axis.x / length, axis.y / length, axis.z / length };
    const point3 ref = std::abs( d.y ) > .92f ? point3{ 1.f, 0.f, 0.f } :
                       point3{ 0.f, 1.f, 0.f };
    // Cross products yield an orthonormal frame for beams of any slope:
    // vertical pillars, inward-canted A/C posts and horizontal roof rails.
    const point3 u_raw{ d.y * ref.z - d.z * ref.y,
                        d.z * ref.x - d.x * ref.z,
                        d.x * ref.y - d.y * ref.x };
    const float ul = std::sqrt( u_raw.x * u_raw.x + u_raw.y * u_raw.y + u_raw.z * u_raw.z );
    if( ul < .001f ) {
        return;
    }
    const float h = thickness * .5f;
    const point3 u{ u_raw.x * h / ul, u_raw.y * h / ul, u_raw.z * h / ul };
    const point3 w{ ( d.y * u.z - d.z * u.y ),
                    ( d.z * u.x - d.x * u.z ),
                    ( d.x * u.y - d.y * u.x ) };
    const auto corner = [&]( const point3 &origin, int sx, int sw ) -> point3 {
        return { origin.x + sx * u.x + sw * w.x,
                 origin.y + sx * u.y + sw * w.y,
                 origin.z + sx * u.z + sw * w.z };
    };
    const auto face = [&]( int sx0, int sw0, int sx1, int sw1 ) {
        quad( corner( a, sx0, sw0 ), corner( b, sx0, sw0 ),
                      corner( b, sx1, sw1 ), corner( a, sx1, sw1 ), paint );
    };
    face( 1, 1, -1, 1 );
    face( -1, 1, -1, -1 );
    face( -1, -1, 1, -1 );
    face( 1, -1, 1, 1 );
    quad( corner( a, -1, 1 ), corner( a, 1, 1 ),
                  corner( a, 1, -1 ), corner( a, -1, -1 ), paint );
    quad( corner( b, 1, 1 ), corner( b, -1, 1 ),
                  corner( b, -1, -1 ), corner( b, 1, -1 ), paint );
}



struct silhouette {
    enum class shape { wagon, sedan, hatchback, pickup, van };
    shape body_type = shape::wagon;
    bool usable = false;
    int x_min = 0, x_max = 0;
    int z_min = 0, z_max = 0;
    int roof_x_min = 0, roof_x_max = 0;
    float left = 0, right = 0;
    float roof_left = 0, roof_right = 0;
    bool front_glass = false;
    bool rear_glass = false;
    std::map<int, std::pair<int, int>> rows;
};

silhouette build_silhouette( const vehicle_view &v, const shell_profile &profile )
{
    silhouette result;
    bool first_frame = true;
    bool first_roof = true;
    int roof_z_min = 0;
    int roof_z_max = 0;
    for( const auto &[pos, cell] : profile.cells ) {
        const int x = pos.first;
        const int z = pos.second;
        if( cell.frame ) {
            if( first_frame ) {
                result.x_min = result.x_max = x;
                result.z_min = result.z_max = z;
                first_frame = false;
            } else {
                result.x_min = std::min( result.x_min, x );
                result.x_max = std::max( result.x_max, x );
                result.z_min = std::min( result.z_min, z );
                result.z_max = std::max( result.z_max, z );
            }
            auto it = result.rows.find( x );
            if( it == result.rows.end() ) {
                result.rows.emplace( x, std::make_pair( z, z ) );
            } else {
                it->second.first = std::min( it->second.first, z );
                it->second.second = std::max( it->second.second, z );
            }
        }
        if( cell.roof && !cell.broken_roof ) {
            if( first_roof ) {
                result.roof_x_min = result.roof_x_max = x;
                roof_z_min = roof_z_max = z;
                first_roof = false;
            } else {
                result.roof_x_min = std::min( result.roof_x_min, x );
                result.roof_x_max = std::max( result.roof_x_max, x );
                roof_z_min = std::min( roof_z_min, z );
                roof_z_max = std::max( roof_z_max, z );
            }
        }
    }
    if( first_frame || first_roof || !profile.enclosed ) {
        return result;
    }
    // V12: a longitudinal min/max row is NOT necessarily a solid body.
    // A missing native frame inside a row, a disconnected row, or a gap
    // between adjacent rows must not be covered with an invented car floor,
    // bumper and greenhouse. Use the per-part modular renderer instead.
    bool first_row = true;
    int previous_x = 0;
    std::pair<int, int> previous_span{};
    for( const auto &[x, span] : result.rows ) {
        if( !first_row &&
            ( x != previous_x + 1 ||
              span.first > previous_span.second + 1 ||
              span.second < previous_span.first - 1 ) ) {
            return result;
        }
        for( int z = span.first; z <= span.second; ++z ) {
            const auto frame = profile.cells.find( {x, z} );
            if( frame == profile.cells.end() || !frame->second.frame ||
                frame->second.broken_frame ) {
                return result;
            }
        }
        first_row = false;
        previous_x = x;
        previous_span = span;
    }
    result.usable = true;
    result.left = result.z_min - .46f - v.pivot_y;
    result.right = result.z_max + .46f - v.pivot_y;
    // A 4-tile-wide CDDA sedan has 2 inner roof tiles. Lean the A/B/C
    // pillars inward to connect the bonnet and side walls to that roof.
    result.roof_left = std::max( result.left + .39f,
                                roof_z_min - .78f - v.pivot_y );
    result.roof_right = std::min( result.right - .39f,
                                 roof_z_max + .78f - v.pivot_y );
    const int rear_overhang = result.roof_x_min - result.x_min;
    const int front_overhang = result.x_max - result.roof_x_max;
    bool native_open_cargo_bed = false;
    for( const part_view &part : v.parts ) {
        if( part.mount_x < result.roof_x_min &&
            ( part.id.find( "cargo_space" ) != std::string::npos ||
              part.id.find( "flatbed" ) != std::string::npos ||
              part.id.find( "aisle" ) != std::string::npos ) ) {
            native_open_cargo_bed = true;
            break;
        }
    }
    const int roof_length = result.roof_x_max - result.roof_x_min + 1;
    if( rear_overhang >= 2 && native_open_cargo_bed ) {
        result.body_type = silhouette::shape::pickup;
    } else if( rear_overhang <= 1 && roof_length >= 3 ) {
        // A wagon/SUV extends its long roof almost to the rear bumper.
        result.body_type = silhouette::shape::wagon;
    } else if( rear_overhang >= 1 ) {
        result.body_type = roof_length <= 1 ?
                           silhouette::shape::hatchback :
                           silhouette::shape::sedan;
    } else if( front_overhang < 1 ) {
        result.body_type = silhouette::shape::van;
    }
    for( const part_view &part : v.parts ) {
        if( part.broken || part.open ) {
            continue;
        }
        if( part.kind == role::window &&
            part.id.find( "windshield" ) != std::string::npos &&
            part.mount_x > result.roof_x_max ) {
            result.front_glass = true;
        }
        if( part.kind == role::door && part.glazed_door &&
            part.mount_x == result.x_min ) {
            result.rear_glass = true;
        }
    }
    return result;
}

// V12: each native frame row has its OWN exterior contour. Global min/max
// width is valid only for a rectangular vehicle; tapered cars and irregular
// original assemblies must not inherit a fictitious full-width end cap.
float native_side_at_row( const vehicle_view &v, const silhouette &shape,
                          int x, bool right )
{
    const auto row = shape.rows.find( x );
    if( row == shape.rows.end() ) {
        return right ? shape.right : shape.left;
    }
    return float( right ? row->second.second : row->second.first ) +
           ( right ? .46f : -.46f ) - v.pivot_y;
}

float native_roof_edge_at_row( const vehicle_view &v,
                               const shell_profile &profile,
                               const silhouette &shape, int x, bool right )
{
    bool found = false;
    int min_z = 0, max_z = 0;
    for( const auto &[mount, cell] : profile.cells ) {
        if( mount.first != x || !cell.roof || cell.broken_roof ) {
            continue;
        }
        if( !found ) {
            min_z = max_z = mount.second;
            found = true;
        } else {
            min_z = std::min( min_z, mount.second );
            max_z = std::max( max_z, mount.second );
        }
    }
    if( !found ) {
        return right ? shape.roof_right : shape.roof_left;
    }
    // Native roof graphics can overhang their roof-mount centres, but never
    // overhang beyond the actual side wall of this longitudinal frame row.
    const float lower = native_side_at_row( v, shape, x, false ) + .18f;
    const float upper = native_side_at_row( v, shape, x, true ) - .18f;
    const float roof_edge = float( right ? max_z : min_z ) +
                            ( right ? .78f : -.78f ) - v.pivot_y;
    return std::clamp( roof_edge, lower, upper );
}

bool has_visual_side_glass( const shell_profile &profile,
                            const silhouette &shape, int x, int z )
{
    const auto it = profile.cells.find( {x,z} );
    if( it == profile.cells.end() || it->second.door_open ||
        it->second.door_broken || it->second.window_broken ||
        it->second.broken_frame || it->second.body_broken ) {
        return false;
    }
    if( it->second.glazing || it->second.window ) {
        return true; // actual intact native CDDA glass
    }
    // In original 0.I compact car JSON the door and halfboard do NOT have
    // WINDOW, despite the native 2D tiles depicting car-side glazing.
    // For a real sealed cabin with an intact forward windshield, infer
    // translucent visual-only glazing. It has zero simulation/collision
    // effect and never replaces an explicitly broken native window.
    return shape.front_glass && x >= shape.roof_x_min &&
           x <= shape.roof_x_max && ( it->second.door || it->second.halfboard );
}

bool native_door_open( const shell_profile &profile, int x, int z )
{
    const auto it = profile.cells.find( {x,z} );
    return it != profile.cells.end() && it->second.door && it->second.door_open;
}

bool wheel_at( const shell_profile &profile, int x, int z )
{
    const auto it = profile.cells.find( {x,z} );
    return it != profile.cells.end() && it->second.wheel;
}

bool has_native_side_skin( const shell_profile &profile, int x, int z )
{
    const auto it = profile.cells.find( {x,z} );
    return it != profile.cells.end() &&
           ( it->second.body || it->second.door ||
             it->second.window || it->second.roof );
}

bool should_bridge_side_skin( const shell_profile &profile, int x, int z )
{
    const auto cell = profile.cells.find( {x,z} );
    if( cell == profile.cells.end() || !cell->second.frame ||
        cell->second.broken_frame || cell->second.body_broken ) {
        return false; // removed/broken bodywork must stay missing
    }
    // Native CDDA vehicle cells can contain only a frame and passenger
    // equipment in one longitudinal row. Bridge ONLY a one-cell hole
    // whose immediate two neighbors actually have an exterior panel.
    return has_native_side_skin( profile, x - 1, z ) &&
           has_native_side_skin( profile, x + 1, z );
}

// A single 3D silhouette is generated for the WHOLE native car. The side
// panels follow each longitudinal row of real frame mounts, while long,
// connected surfaces (hood, roof, tailgate) are emitted as a unit. No
// pseudo-3D top-down sprite is used as an upright exterior wall.
template<typename Box, typename Quad, typename Seam>
void emit_closed_silhouette( const vehicle_view &v, const shell_profile &profile,
                             const silhouette &shape, const Box &box,
                             const Quad &quad, const Seam &draw_seam )
{
    if( !shape.usable ) {
        return;
    }
    const color3 paint = profile.paint;
    const color3 dark{ paint[0] * .40f, paint[1] * .40f, paint[2] * .40f };
    const color3 edges{ paint[0] * .66f, paint[1] * .66f, paint[2] * .66f };
    const float front = shape.x_max + .49f - v.pivot_x;
    const float rear = shape.x_min - .49f - v.pivot_x;
    const float roof_rear_x = shape.roof_x_min - .46f - v.pivot_x;
    const float roof_front_x = shape.roof_x_max + .46f - v.pivot_x;
    const float width = shape.right - shape.left;
    const float center = ( shape.left + shape.right ) * .5f;
    const float front_left = native_side_at_row( v, shape, shape.x_max, false );
    const float front_right = native_side_at_row( v, shape, shape.x_max, true );
    const float rear_left = native_side_at_row( v, shape, shape.x_min, false );
    const float rear_right = native_side_at_row( v, shape, shape.x_min, true );
    const float front_width = front_right - front_left;
    const float rear_width = rear_right - rear_left;
    const float front_center = ( front_left + front_right ) * .5f;
    const float rear_center = ( rear_left + rear_right ) * .5f;
    constexpr float belt = .82f;
    constexpr float halfboard_top = .59f;
    constexpr float roof_y = 1.42f;
    constexpr float roof_side_y = 1.36f;

    // Each original mounted frame row contributes only ONE chassis section.
    // The old engine/cargo/roof/door cube stacks were visually independent.
    for( const auto &[ix, span] : shape.rows ) {
        const float x = ix - v.pivot_x;
        const float z_left = span.first - .46f - v.pivot_y;
        const float z_right = span.second + .46f - v.pivot_y;
        const float mid = ( z_left + z_right ) * .5f;
        // Adjacent 1-tile chassis rows overlap by 0.02 units to avoid
        // z-fighting/gaps after perspective projection and rotation.
        box( x, mid, 1.02f, z_right - z_left, .28f, .35f, dark );
        // Side skins are clipped at each real native side segment, leaving
        // openings where an actually present door is opened or a wheel well
        // exists. They never form a platform across empty mount rows.
        for( int side = 0; side < 2; ++side ) {
            const int native_z = side == 0 ? span.first : span.second;
            const float skin_z = side == 0 ? z_left : z_right;
            const float outside = side == 0 ? -.01f : .01f;
            const auto cell = profile.cells.find( {ix,native_z} );
            const bool body = has_native_side_skin( profile, ix, native_z ) ||
                              should_bridge_side_skin( profile, ix, native_z );
            const bool native_frame = cell != profile.cells.end() &&
                                      cell->second.frame && !cell->second.broken_frame;
            if( native_frame && !wheel_at( profile, ix, native_z ) ) {
                // The real frame rails survive an open door. These short
                // rocker beams connect the continuous lower chassis without
                // filling the side-window aperture or sealing a wheel arch.
                cabin_frame_beam( quad, {x - .49f, .31f, skin_z},
                                  {x + .49f, .31f, skin_z}, .085f, edges );
            }
            if( native_door_open( profile, ix, native_z ) ) {
                // The door leaf is animated independently around its native
                // hinge. Leaving a bright red CLOSED slab here blocked it.
                continue;
            }
            const bool weak_skin = cell != profile.cells.end() &&
                                   cell->second.body_broken && !cell->second.door;
            const bool halfboard = cell != profile.cells.end() && cell->second.halfboard;
            if( !body && !native_frame ) {
                continue;
            }
            if( weak_skin ) {
                // Demolished panels are visibly missing; a narrow lower sill
                // is not permission to synthesize the original intact wall.
                box( x, skin_z, .62f, .060f, .31f, .48f, edges );
            } else if( wheel_at( profile, ix, native_z ) ) {
                // Open wheel arch, with only a short sheet above the tire.
                box( x, skin_z, .99f, .060f, .89f, .99f, paint );
                box( x - .45f, skin_z, .07f, .065f, .34f, .84f, paint );
                box( x + .45f, skin_z, .07f, .065f, .34f, .84f, paint );
            } else if( halfboard ) {
                // V11: the tandem bike inherits a three-wide CAR body,
                // but its rear halfboards were incorrectly drawn only
                // to .59. This opened a large hole from the lower side
                // body up to the .852 cabin-glass seal, exposing the
                // rear centreline tire IN the passenger compartment.
                // Extend only this native narrow-bike FAIRING to the
                // .82 window waist. Never refill the upper side window.
                const float top = profile.centerline_motorcycle_cabin ?
                                  belt : halfboard_top;
                box( x, skin_z, 1.005f, .072f, .31f, top, paint );
                box( x, skin_z, 1.005f, .074f,
                        top - .045f, top, edges );
            } else if( body ) {
                // Real closed lower side skin, below the window opening.
                box( x, skin_z, 1.005f, .072f, .31f, belt, paint );
            } else {
                // A centreline motorcycle's front native frame cells
                // hold WIND­SHIELD and headlights, not halfboards; an
                // extremely low .44 sill left the base of the A pillar
                // unattached to the front fairing. The visual front
                // fender should end near the bonnet, not at the floor.
                const float top = profile.centerline_motorcycle_cabin &&
                                  ix > shape.roof_x_max ? .88f : .44f;
                box( x, skin_z, 1.01f, .068f, .29f, top, edges );
            }
            if( cell != profile.cells.end() && cell->second.door ) {
                const float door_top = belt;
                draw_seam( { x - .495f, .34f, skin_z + outside },
                      { x - .495f, door_top, skin_z + outside }, dark );
                draw_seam( { x + .495f, .34f, skin_z + outside },
                      { x + .495f, door_top, skin_z + outside }, dark );
                draw_seam( { x - .48f, door_top, skin_z + outside },
                      { x + .48f, door_top, skin_z + outside }, edges );
                box( x + .23f, skin_z + outside, .12f, .044f,
                        .715f, .75f, color3{ .19f, .20f, .22f } );
            }
        }
    }

    // V11 narrow two-wheel cabin: the two native wheels occupy the
    // MIDDLE of the 3-tile shell (Y=0). Without an enclosed wheel bay
    // they visibly protrude through the cabin floor when viewed from the
    // side. A low painted fairing covers only their *upper* semicircles,
    // preserving the actual round tire BELOW the car and its wheel mount.
    // Unlike a full red cab panel it never rises into the side glazing.
    if( profile.centerline_motorcycle_cabin ) {
        for( const part_view &part : v.parts ) {
            if( part.kind != role::wheel ) {
                continue;
            }
            const float wx = float( part.mount_x - v.pivot_x );
            const float wz = float( part.mount_y - v.pivot_y );
            const float top = part.broken ? .62f : .78f;
            box( wx, wz, .94f, .88f, .34f, top, paint );
            // Compact cover side seams; mark the wheel well without a
            // solid fake body wall crossing the whole passenger bay.
            for( float side : { -.445f, .445f } ) {
                cabin_frame_beam( quad, { wx - .43f, .36f, wz + side },
                                  { wx + .43f, .36f, wz + side },
                                  .052f, dark );
            }
        }
    }

    // The entire hood is one continuous inclined sheet, bounded by the
    // rear of the bonnet and the front-most actual native frame. No small
    // floating 1x1 tile on top of the front engine.
    const float nose_root = roof_front_x + .67f;
    if( front > nose_root + .05f ) {
        quad( {nose_root, 1.02f, shape.roof_left + .12f},
                      {front, .82f, front_left + .17f},
                      {front, .82f, front_right - .17f},
                      {nose_root, 1.02f, shape.roof_right - .12f}, paint );
        draw_seam( {nose_root, 1.025f, shape.roof_left + .14f},
              {front, .825f, front_left + .18f}, edges );
        draw_seam( {nose_root, 1.025f, shape.roof_right - .14f},
              {front, .825f, front_right - .18f}, edges );
    }
    // Actual forward and rear silhouette: bumper/grille on the front (+X);
    // rear lights and boot are on the rear (-X), never interchangeable.
    // End caps connect the underbody to the bonnet and rear deck.
    // They are deliberately painted bodywork (not broad dark vents).
    box( front - .11f, front_center, .22f, front_width * .94f, .36f, .81f, paint );
    box( front, front_center, .11f, front_width * .91f, .32f, .47f, dark );
    box( front + .012f, front_center, .13f, front_width * .42f, .52f, .66f,
            color3{ .17f, .19f, .21f } );
    if( shape.body_type != silhouette::shape::pickup ) {
        box( rear + .11f, rear_center, .22f, rear_width * .93f, .36f, .87f, paint );
    }
    box( rear, rear_center, .13f, rear_width * .91f, .32f, .46f, dark );

    // Native lights are embedded into the bumper. Do not additionally
    // draw every headlight as a full independent beige crate.
    for( const part_view &part : v.parts ) {
        if( part.kind != role::light ) {
            continue;
        }
        const bool tail = part.id.find( "tail" ) != std::string::npos ||
                          part.id.find( "brake" ) != std::string::npos;
        const bool head = part.id.find( "head" ) != std::string::npos;
        if( !head && !tail ) {
            continue; // indicators/flashers are handled separately later
        }
        const float lamp_left = tail ? rear_left : front_left;
        const float lamp_right = tail ? rear_right : front_right;
        const float z = std::clamp( float( part.mount_y - v.pivot_y ),
                                    lamp_left + .28f, lamp_right - .28f );
        const color3 lens = part.broken ?
                            color3{ .12f, .12f, .12f } :
                            tail ? color3{ .63f, .12f, .10f } :
                            color3{ .89f, .85f, .69f };
        // Two-wheel cabin has real paired headlights, but each should
        // resemble a flush lamp, not a 0.32-wide beige cardboard crate.
        const bool bike_cabin = profile.centerline_motorcycle_cabin;
        box( ( tail ? rear - .060f : front + .060f ), z,
                .075f, bike_cabin ? .23f : .32f,
                bike_cabin ? .60f : .62f, bike_cabin ? .73f : .78f, lens );
    }

    // V7: Derive each intact roof surface from the *actual* (X,Z) tiles.
    // V5/V6 created a full-width slab if ANY roof tile survived in each
    // longitudinal row, inadvertently covering roof holes and a damaged
    // sunroof with a pristine floating rectangle. Merge contiguous X
    // runs independently per native Z column, preserving real missing
    // roof modules while leaving no cracks between installed tiles.
    std::map<int, std::vector<int>> roof_columns;
    int roof_native_min_z = 0;
    int roof_native_max_z = 0;
    bool first_roof_column = true;
    for( const auto &[mount, cell] : profile.cells ) {
        if( !cell.roof || cell.broken_roof ) {
            continue;
        }
        roof_columns[mount.second].push_back( mount.first );
        if( first_roof_column ) {
            roof_native_min_z = roof_native_max_z = mount.second;
            first_roof_column = false;
        } else {
            roof_native_min_z = std::min( roof_native_min_z, mount.second );
            roof_native_max_z = std::max( roof_native_max_z, mount.second );
        }
    }
    for( auto &[native_z, xs] : roof_columns ) {
        std::sort( xs.begin(), xs.end() );
        xs.erase( std::unique( xs.begin(), xs.end() ), xs.end() );
        const float z0 = native_z == roof_native_min_z ?
                         shape.roof_left : native_z - .50f - v.pivot_y;
        const float z1 = native_z == roof_native_max_z ?
                         shape.roof_right : native_z + .50f - v.pivot_y;
        const auto roof_run = [&]( int lo, int hi ) {
            const float x0 = lo - .50f - v.pivot_x;
            const float x1 = hi + .50f - v.pivot_x;
            const float center_x = ( x0 + x1 ) * .5f;
            const float center_z = ( z0 + z1 ) * .5f;
            box( center_x, center_z, x1 - x0 + .012f,
                    z1 - z0 + .012f, 1.365f, roof_y, paint );
            if( native_z == roof_native_min_z ) {
                box( center_x, z0 + .038f, x1 - x0, .076f,
                        1.30f, 1.40f, edges );
            }
            if( native_z == roof_native_max_z ) {
                box( center_x, z1 - .038f, x1 - x0, .076f,
                        1.30f, 1.40f, edges );
            }
            // Solid front/rear roof header members are restricted to the
            // ACTUAL intact native roof run. A missing roof tile is never
            // bridged by a fictitious full-width crossbar.
            if( lo == shape.roof_x_min ) {
                cabin_frame_beam( quad, {x0 + .04f, 1.355f, z0},
                                  {x0 + .04f, 1.355f, z1}, .10f, edges );
            }
            if( hi == shape.roof_x_max ) {
                cabin_frame_beam( quad, {x1 - .04f, 1.355f, z0},
                                  {x1 - .04f, 1.355f, z1}, .10f, edges );
            }
        };
        int run_start = xs.front();
        int run_end = run_start;
        for( size_t i = 1; i < xs.size(); ++i ) {
            if( xs[i] == run_end + 1 ) {
                run_end = xs[i];
            } else {
                roof_run( run_start, run_end );
                run_start = run_end = xs[i];
            }
        }
        roof_run( run_start, run_end );
    }

    // For each cabin row, the upper greenhouse joins the shoulder line to
    // an inward-sloping roof edge. Glazed native sides remain open in the
    // opaque pass; alpha glass is supplied in draw_closed_silhouette_glass.
    for( int ix = shape.roof_x_min; ix <= shape.roof_x_max; ++ix ) {
        const float x0 = ix - .49f - v.pivot_x;
        const float x1 = ix + .49f - v.pivot_x;
        const auto row = shape.rows.find( ix );
        if( row == shape.rows.end() ) {
            continue;
        }
        for( int side = 0; side < 2; ++side ) {
            const int native_z = side == 0 ? row->second.first : row->second.second;
            const float outside = native_side_at_row( v, shape, ix, side == 1 );
            const float inside = native_roof_edge_at_row( v, profile, shape, ix, side == 1 );
            const auto cell = profile.cells.find( {ix,native_z} );
            const bool frame_present = cell != profile.cells.end() &&
                                       ( ( cell->second.frame && !cell->second.broken_frame ) ||
                                         ( cell->second.roof && !cell->second.broken_roof ) ||
                                         ( cell->second.door && !cell->second.door_broken ) ||
                                         ( cell->second.halfboard && !cell->second.body_broken ) ||
                                         ( cell->second.window && !cell->second.window_broken ) );
            // This roof row must really have an intact roof mount;
            // a damaged/missing roof must never be repaired cosmetically.
            bool supported_roof = false;
            int nearest_roof_distance = 1000000000;
            for( const auto &[mount, mount_cell] : profile.cells ) {
                if( mount.first != ix || !mount_cell.roof ) {
                    continue;
                }
                // Native roof pieces can be in two lateral columns.
                // Resolve each frame side to its nearest native roof
                // so one intact panel cannot bridge the broken other side.
                const int separation = std::abs( mount.second - native_z );
                if( separation < nearest_roof_distance ) {
                    nearest_roof_distance = separation;
                    supported_roof = !mount_cell.broken_roof;
                }
            }
            if( !frame_present || !supported_roof ) {
                continue;
            }
            // This structure is fixed to native chassis + roof. In
            // particular a door opening does NOT remove the B pillar:
            // only the leaf rotates via the original one_part() hinge.
            const color3 frame = paint;
            constexpr float post = .092f;
            const point3 low_a{ x0, belt, outside };
            const point3 low_b{ x1, belt, outside };
            const point3 high_a{ x0, roof_side_y, inside };
            const point3 high_b{ x1, roof_side_y, inside };
            // Real 3D quadrangular window frame, no opaque filled
            // rectangle between lower skin and roof.
            cabin_frame_beam( quad, low_a, high_a, post, frame );
            cabin_frame_beam( quad, low_b, high_b, post, frame );
            cabin_frame_beam( quad, high_a, high_b, .115f, frame );
            cabin_frame_beam( quad, low_a, low_b, .07f, edges );
        }
    }

    // The windshield's real native position lies immediately ahead of the
    // roof mounts. Join the top of that roof to the bonnet with A pillars.
    if( shape.front_glass ) {
        const float lower_x = std::min( front - .28f, roof_front_x + .78f );
        for( int side = 0; side < 2; ++side ) {
            const float outside = side == 0 ? shape.left : shape.right;
            // Lower side shoulder ties the inclined windshield/A pillar
            // into the front wing. It stops above the wheel arch.
            quad( {roof_front_x, belt, outside},
                          {lower_x + .01f, belt, outside},
                          {lower_x + .01f, 1.025f, outside},
                          {roof_front_x, 1.025f, outside}, paint );
        }
        for( int side = 0; side < 2; ++side ) {
            const float outer = side == 0 ? shape.left + .17f : shape.right - .17f;
            const float upper = side == 0 ? shape.roof_left : shape.roof_right;
            cabin_frame_beam( quad, {lower_x, 1.02f, outer},
                              {roof_front_x, roof_side_y, upper},
                              .118f, paint );
        }
        cabin_frame_beam( quad, {roof_front_x, roof_side_y, shape.roof_left},
                          {roof_front_x, roof_side_y, shape.roof_right},
                          .115f, paint );
    } else {
        quad( {roof_front_x + .40f, .92f, shape.left},
                      {roof_front_x + .40f, .92f, shape.right},
                      {roof_front_x, roof_side_y, shape.roof_right},
                      {roof_front_x, roof_side_y, shape.roof_left}, paint );
    }

    if( shape.body_type == silhouette::shape::sedan ||
        shape.body_type == silhouette::shape::hatchback ) {
        const float deck_to = std::max( rear, roof_rear_x - .74f );
        // Rear glazing slopes down onto the trunk instead of hanging
        // upright like a disconnected car door.
        if( !shape.rear_glass ) {
            // Preserve the native unglazed rear opening. Join the roof
            // with slender C pillars instead of blocking all rear sight.
            for( int side = 0; side < 2; ++side ) {
                const float low_z = side == 0 ? shape.left + .12f : shape.right - .12f;
                const float top_z = side == 0 ? shape.roof_left : shape.roof_right;
                cabin_frame_beam( quad, {roof_rear_x, roof_side_y, top_z},
                                  {deck_to, .90f, low_z}, .105f, paint );
            }
            draw_seam( {deck_to, .92f, shape.left + .10f},
                  {deck_to, .92f, shape.right - .10f}, edges );
        }
        if( deck_to > rear + .05f ) {
            quad( {rear, .83f, shape.left + .10f},
                          {rear, .83f, shape.right - .10f},
                          {deck_to, .94f, shape.right - .12f},
                          {deck_to, .94f, shape.left + .12f}, paint );
        }
    } else if( shape.body_type == silhouette::shape::wagon ||
               shape.body_type == silhouette::shape::van ) {
        // True full-height tailgate, connected to the end of the roof.
        if( !shape.rear_glass ) {
            // Wagon rear has a native hatch outline, not an entire
            // opaque upper surface which obscures all its actual seats.
            for( int side = 0; side < 2; ++side ) {
                const float lower_z = side == 0 ? shape.left : shape.right;
                const float upper_z = side == 0 ? shape.roof_left : shape.roof_right;
                cabin_frame_beam( quad, {rear, belt, lower_z},
                                  {roof_rear_x, roof_side_y, upper_z},
                                  .105f, paint );
            }
            draw_seam( {rear, belt, shape.left},
                  {rear, belt, shape.right}, edges );
        }
    } else {
        // Pickups keep an open bed; never seal the rear with sedan glass.
        for( int side = 0; side < 2; ++side ) {
            const float z = side == 0 ? shape.left : shape.right;
            box( (rear + roof_rear_x) * .5f, z, roof_rear_x - rear,
                    .10f, .47f, .94f, paint );
        }
        box( rear, center, .12f, width, .34f, .93f, paint );
    }
}


using glass_quad = std::array<point3, 4>;

std::vector<glass_quad> closed_glass_panes( const vehicle_view &v,
        const shell_profile &profile, const silhouette &shape, const point3 &eye )
{
    if( !shape.usable ) {
        return {};
    }
    const float roof_front = shape.roof_x_max + .46f - v.pivot_x;
    const float roof_rear = shape.roof_x_min - .46f - v.pivot_x;
    const float front = shape.x_max + .49f - v.pivot_x;
    const float rear = shape.x_min - .49f - v.pivot_x;
    const float front_left = native_side_at_row( v, shape, shape.x_max, false );
    const float front_right = native_side_at_row( v, shape, shape.x_max, true );
    const float rear_left = native_side_at_row( v, shape, shape.x_min, false );
    const float rear_right = native_side_at_row( v, shape, shape.x_min, true );

    const float low_front = std::min( front - .28f, roof_front + .78f );
    // Semi-transparent glazing shared between the actual A pillars, roof
    // frame and bonnet. Unlike P2, one pane spans the cabin's front, not
    // four disjoint glasses drawn from individual mount cells.
    std::vector<glass_quad> panes;
    panes.reserve( ( shape.roof_x_max - shape.roof_x_min + 1 ) * 2 + 2 );
    const auto pane = [&]( const point3 &a, const point3 &b,
                           const point3 &c, const point3 &d ) {
        panes.push_back( { a, b, c, d } );
    };
    if( shape.front_glass ) {
        pane( {low_front, 1.02f, front_left + .23f},
              {low_front, 1.02f, front_right - .23f},
              {roof_front, 1.355f, shape.roof_right - .07f},
              {roof_front, 1.355f, shape.roof_left + .07f} );
    }
    for( int ix = shape.roof_x_min; ix <= shape.roof_x_max; ++ix ) {
        const auto row = shape.rows.find( ix );
        if( row == shape.rows.end() ) {
            continue;
        }
        for( int side = 0; side < 2; ++side ) {
            const int native_z = side == 0 ? row->second.first : row->second.second;
            if( !has_visual_side_glass( profile, shape, ix, native_z ) ) {
                continue;
            }
            // The upper frame spans +/- .49 and its pillar is .075:
            // glazing must reach just inside that pillar, not stop at
            // +/- .40 and expose thin air strips at both frame ends.
            const float x0 = ix - .445f - v.pivot_x;
            const float x1 = ix + .445f - v.pivot_x;
            const float outside = native_side_at_row( v, shape, ix, side == 1 );
            const float inside = native_roof_edge_at_row( v, profile, shape, ix, side == 1 );
            // Start just above the native lower door skin (.82), leaving
            // a realistic rubber window seal rather than a see-through gap.
            pane( {x0, .852f, outside},
                  {x1, .852f, outside},
                  {x1, 1.345f, inside},
                  {x0, 1.345f, inside} );
        }
    }
    // Rear aperture is also part of the enclosed native compact-car
    // outline. For cars with an intact roof/halfboard rear but no WINDOW
    // flag in 0.I JSON, use visual-only clear glazing instead of leaving
    // a gaping hole. Never restore an explicitly broken rear panel.
    bool visual_rear_glass = shape.rear_glass;
    if( !visual_rear_glass && shape.front_glass &&
        shape.body_type != silhouette::shape::pickup ) {
        int rear_supports = 0;
        for( const auto &[pos, cell] : profile.cells ) {
            if( pos.first == shape.roof_x_min &&
                ( cell.halfboard || cell.roof ) &&
                !cell.broken_roof && !cell.body_broken && !cell.broken_frame ) {
                ++rear_supports;
            }
        }
        visual_rear_glass = rear_supports >= 2;
    }
    if( visual_rear_glass ) {
        const bool sedan = shape.body_type == silhouette::shape::sedan ||
                           shape.body_type == silhouette::shape::hatchback;
        const float bottom = sedan ? std::max( rear, roof_rear - .74f ) : rear;
        pane( {roof_rear, 1.34f, shape.roof_left + .13f},
              {roof_rear, 1.34f, shape.roof_right - .13f},
              {bottom, .95f, rear_right - .22f},
              {bottom, .95f, rear_left + .22f} );
    }
    // Panes were previously emitted in a fixed order, even as the car
    // turns and the camera walks around it. Alpha blending is not
    // commutative: the rear glass must be drawn before the front pane
    // when viewed from the front. Sort each real pane by local eye
    // distance so the inside of the vehicle remains readable.
    const auto depth = [&]( const glass_quad &q ) {
        point3 center{};
        for( const point3 &p : q ) {
            center.x += p.x * .25f;
            center.y += p.y * .25f;
            center.z += p.z * .25f;
        }
        const float dx = center.x - eye.x;
        const float dy = center.y - eye.y;
        const float dz = center.z - eye.z;
        return dx * dx + dy * dy + dz * dz;
    };
    std::stable_sort( panes.begin(), panes.end(), [&]( const glass_quad &a,
                      const glass_quad &b ) { return depth( a ) > depth( b ); } );
    return panes;
}

template<typename Sink>
void emit_glass_quad( Sink &sink, const glass_quad &points, const std::array<float, 4> &tint,
                      const std::optional<fp3d_materials::uv_rect> &art = std::nullopt )
{
    sink.textured( art.has_value() );
    sink.color( tint[0], tint[1], tint[2], tint[3] );
    sink.begin( fp3d_geometry::primitive::quads );
    const auto uv = art.value_or( fp3d_materials::uv_rect{ 0, 0, 1, 1 } );
    for( size_t i = 0; i < points.size(); ++i ) {
        if( art ) {
            sink.texcoord( i == 0 || i == 3 ? uv.left : uv.right,
                           i < 2 ? uv.bottom : uv.top );
        }
        sink.point( points[i].x, points[i].y, points[i].z );
    }
    sink.end();
}


template<typename Box>
void emit_cabin_seat( const vehicle_view &v, const part_view &part, const Box &box )
{
    const float x = float( part.mount_x - v.pivot_x );
    const float z = float( part.mount_y - v.pivot_y );
    const bool leather = part.id.find( "leather" ) != std::string::npos;
    const bool reclining = part.id.find( "reclining" ) != std::string::npos;
    // One modest seat per actual CDDA SEAT mount, viewed through the native
    // cab openings. No top-down 2D seat tile pasted onto the cushion.
    const color3 cloth = leather ? color3{ .27f, .23f, .21f } :
                         color3{ .31f, .32f, .34f };
    const color3 shade{ cloth[0] * .64f, cloth[1] * .64f,
                        cloth[2] * .64f };
    const color3 rail{ .15f, .16f, .17f };
    // Frame / legs sit inside the coherent existing car floor.
    for( float side : { -.22f, .22f } ) {
        box( x - .10f, z + side, .27f, .045f, .31f, .47f, rail );
    }
    // Compact, shallow seat cushion and upholstered backrest. Native +X
    // is forward; the upright backrest is to the rear at negative X.
    box( x + .05f, z, .59f, .61f, .46f, .58f, cloth );
    box( x - .235f, z, .105f, .60f, .52f,
            part.broken ? .73f : 1.00f, cloth );
    if( !part.broken ) {
        for( float side : { -.275f, .275f } ) {
            box( x + .015f, z + side, .42f, .050f, .54f, .64f, shade );
        }
        if( reclining ) {
            box( x - .26f, z, .10f, .30f, .96f, 1.08f, cloth );
        }
    }
}


point3 steering_ring_point( float x, float z, int index )
{
    const float angle = 2.f * pi * ( index % 16 ) / 16.f;
    return { x + .37f, 1.01f + .18f * std::sin( angle ), z + .18f * std::cos( angle ) };
}

template<typename Box, typename Line>
void emit_cabin_controls( const vehicle_view &v, const part_view &part, const Box &box, const Line &line )
{
    const float x = float( part.mount_x - v.pivot_x );
    const float z = float( part.mount_y - v.pivot_y );
    if( part.id.find( "dashboard" ) != std::string::npos ) {
        box( x + .33f, z, .37f, .75f, .78f, .91f, { .16f, .16f, .18f } );
    }
    if( part.id == "controls" || part.id.find( "steering_controls" ) != std::string::npos ) {
        const color3 paint{ .15f, .15f, .17f };
        for( int i = 0; i < 16; ++i ) {
            line( steering_ring_point( x, z, i ), steering_ring_point( x, z, i + 1 ), paint, 3.f );
        }
        line( { x + .37f, .85f, z }, { x + .37f, 1.01f, z }, paint, 3.f );
    }
}


// Bicycle tyres have open centres and spokes, unlike the solid car wheel.
template<typename Sink, typename Beam, typename Box>
void emit_bicycle_wheel( Sink &sink, float x, float z, bool broken, const Beam &beam, const Box &box )
{
    const float radius = broken ? .29f : .35f;
    const float y = radius + .012f;
    sink.textured( false );
    for( int i = 0; i < 32; ++i ) {
        const float a = 2.f * pi * i / 32.f;
        const float b = 2.f * pi * ( i + 1 ) / 32.f;
        sink.begin( fp3d_geometry::primitive::quads );
        for( int layer = 0; layer < 3; ++layer ) {
            sink.color( layer == 0 ? .08f : .57f, layer == 0 ? .09f : .60f,
                       layer == 0 ? .09f : .63f );
            if( layer == 0 ) {
                sink.normal( std::cos( .5f * ( a + b ) ), std::sin( .5f * ( a + b ) ), 0 );
            } else {
                sink.normal( 0, 0, layer == 1 ? -1.f : 1.f );
            }
            for( int j = 0; j < 4; ++j ) {
                const float angle = j == 0 || j == 3 ? a : b;
                const float r = layer == 0 || j < 2 ? radius : radius - .035f;
                const float side = layer == 0 ? ( j < 2 ? -.025f : .025f ) :
                                   ( layer == 1 ? -.025f : .025f );
                sink.point( x + r * std::cos( angle ), y + r * std::sin( angle ), z + side );
            }
        }
        sink.end();
    }
    const color3 steel{ .57f, .60f, .63f };
    for( int i = 0; i < 12; ++i ) {
        const float a = 2.f * pi * i / 12.f;
        beam( {x, y, z},
                          {x + ( radius - .035f ) * std::cos( a ),
                           y + ( radius - .035f ) * std::sin( a ), z}, .009f, steel );
    }
    box( x, z, .065f, .10f, y - .035f, y + .035f, steel );
}

template<typename Wheel, typename Beam, typename Box>
bool emit_bicycle_part( const vehicle_view &v, const part_view &part, const Wheel &draw_wheel,
                        const Beam &beam, const Box &box )
{
    const float x = float( part.mount_x - v.pivot_x );
    const float z = float( part.mount_y - v.pivot_y );
    const color3 paint = part.broken ? color3{ .24f, .23f, .22f } :
                         color3{ .22f, .39f, .50f };
    const color3 steel{ .57f, .60f, .63f };
    const color3 rubber{ .10f, .11f, .12f };
    if( part.kind == role::wheel && part.id.find( "bicycle" ) != std::string::npos ) {
        draw_wheel( x, z, part.broken );
        return true;
    }
    if( part.kind == role::frame ) {
        // Join only adjacent surviving native frame mounts, so removing a
        // frame cannot leave an invented complete bicycle behind.
        const point3 crank{ x, .40f, z };
        const point3 seat{ x - .18f, .98f, z };
        const point3 head{ x + .65f, .99f, z };
        const auto has = [&]( int offset, const char *id ) {
            return std::any_of( v.parts.begin(), v.parts.end(), [&]( const part_view &p ) {
                return p.mount_x == part.mount_x + offset && p.mount_y == part.mount_y &&
                       p.id == id;
            } );
        };
        if( has( 0, "foot_pedals" ) ) {
            beam( crank, seat, .045f, paint );
            if( has( 1, "xlframe" ) || has( 1, "frame" ) ) {
                beam( seat, head, .045f, paint );
                beam( crank, head, .045f, paint );
            }
            if( has( -1, "xlframe" ) || has( -1, "frame" ) ) {
                for( float side : { -.045f, .045f } ) {
                    const point3 axle{ x - 1.f, .362f, z + side };
                    beam( axle, seat, .027f, paint );
                    beam( axle, crank, .027f, paint );
                }
            }
        } else if( has( -1, "foot_pedals" ) ) {
            const point3 neck{ x - .35f, .99f, z };
            for( float side : { -.045f, .045f } ) {
                beam( {x, .362f, z + side}, neck, .028f, steel );
            }
            beam( neck, {x - .32f, 1.16f, z}, .035f, steel );
            beam( {x - .32f, 1.16f, z - .27f},
                              {x - .32f, 1.16f, z + .27f}, .032f, steel );
            for( float side : { -.23f, .23f } ) {
                box( x - .32f, z + side, .055f, .12f, 1.135f, 1.185f, rubber );
            }
        }
        return true;
    }
    if( part.id == "saddle" ) {
        beam( {x - .18f, .80f, z}, {x - .18f, 1.035f, z}, .032f, steel );
        box( x - .22f, z, .23f, .22f, 1.025f, 1.08f, rubber );
        box( x - .06f, z, .15f, .085f, 1.035f, 1.075f, rubber );
        return true;
    }
    if( part.id == "foot_pedals" ) {
        beam( {x, .40f, z - .13f}, {x, .40f, z + .13f}, .04f, steel );
        for( float side : { -1.f, 1.f } ) {
            beam( {x, .40f, z + side * .13f},
                              {x + side * .13f, .34f, z + side * .13f}, .022f, steel );
            box( x + side * .13f, z + side * .19f, .11f, .13f, .32f, .36f, rubber );
        }
        return true;
    }
    if( part.id == "basketsm" || part.id == "basketsm_external" ) {
        box( x, z, .40f, .34f, .73f, .76f, steel );
        for( int i = 0; i <= 4; ++i ) {
            const float dx = -.20f + .10f * i;
            const float dz = -.17f + .085f * i;
            for( float side : { -1.f, 1.f } ) {
                beam( {x + dx, .76f, z + side * .17f},
                                  {x + dx, .98f, z + side * .17f}, .012f, steel );
                beam( {x + side * .20f, .76f, z + dz},
                                  {x + side * .20f, .98f, z + dz}, .012f, steel );
            }
        }
        for( float y : { .85f, .98f } ) {
            for( float side : { -1.f, 1.f } ) {
                beam( {x - .20f, y, z + side * .17f},
                                  {x + .20f, y, z + side * .17f}, .012f, steel );
                beam( {x + side * .20f, y, z - .17f},
                                  {x + side * .20f, y, z + .17f}, .012f, steel );
            }
        }
        return true;
    }
    // Small brackets and controls must not become large opaque boxes.
    return part.id.find( "wheel_mount" ) == 0 || part.id == "horn_bicycle" ||
           part.id == "controls" || part.id == "controls_electronic";
}


struct wheel_spec {
    float lateral_offset;
    bool narrow;
};

wheel_spec wheel_placement( const vehicle_view &v, const part_view &part,
                            const shell_profile &profile )
{
    const bool narrow = part.id.find( "bicycle" ) != std::string::npos ||
                        part.id.find( "motorbike" ) != std::string::npos;
    float middle = 0;
    int count = 0;
    for( const part_view &other : v.parts ) {
        if( other.kind == role::frame ) {
            middle += other.mount_y;
            ++count;
        }
    }
    if( count > 0 ) {
        middle /= count;
    }
    const float side = part.mount_y >= middle ? 1.0f : -1.0f;
    // On enclosed vehicles the native wheel cell has a painted
    // perimeter panel near +/- .46 Z. Push the real tyre sideways
    // only far enough for its outer sidewall to remain visible
    // through the wheel arch, not hidden inside the red metal.
    const float lateral = narrow && profile.centerline_motorcycle_cabin ?
                          0.0f : narrow ? .10f :
                          profile.enclosed ? .50f : .22f;
    return { side * lateral, narrow };
}

template<typename Emit>
bool emit_modular_part( const vehicle_view &v, const part_view &part, const shell_profile &profile,
                        const std::optional<fp3d_materials::uv_rect> &art, const Emit &emit )
{
    const float x = float( part.mount_x - v.pivot_x );
    const float z = float( part.mount_y - v.pivot_y );
    const float health = std::clamp( part.health, 0.f, 1.f );
    const float damage = part.broken ? .57f : .55f + .45f * health;
    const std::array<float, 3> metal{ .47f * damage, .50f * damage, .53f * damage };
    const std::array<float, 3> paint{ .40f * damage, .49f * damage, .57f * damage };
    const std::array<float, 3> interior{ .29f * damage, .27f * damage, .25f * damage };
    if( part.id.find( "solar_panel" ) != std::string::npos ) {
        // Conversion to a grid appliance uses a flat panel, never a generic
        // engine/roof box. Ordinary vehicle-mounted panels sit on the roof.
        const bool appliance = part.id.find( "ap_" ) == 0;
        const float base = appliance ? .10f : profile.enclosed ? 1.44f : .65f;
        emit( x, z, .96f, .90f, base, base + .055f, metal );
        const color3 cells{ .055f * damage, .14f * damage, .29f * damage };
        for( int row = 0; row < 4; ++row ) {
            for( int col = 0; col < 6; ++col ) {
                emit( x - .375f + col * .15f, z - .315f + row * .21f,
                        .139f, .195f, base + .055f, base + .064f, cells );
            }
        }
        return true;
    }
    switch( part.kind ) {
        case role::frame: {
            if( profile.enclosed ) {
                // One coherent body shell draws the vehicle floor instead
                // of repeated tile-sized slabs poking out on every side.
                if( part.broken ) {
                    emit( x, z, .87f, .085f, .16f, .25f, metal );
                    emit( x, z, .085f, .87f, .16f, .25f, metal );
                }
            } else {
                emit( x, z, .83f, .095f, .19f, .28f, metal );
                emit( x, z, .095f, .83f, .19f, .28f, metal, art );
            }
            break;
        }
        case role::roof:
            if( profile.enclosed && !part.broken ) {
                // The shell renders a connected roof using the same tinted
                // native texture for all mounted roof cells.
                break;
            }
            if( part.broken ) {
                // A collapsed roof keeps only remaining rails, not an
                // undamaged opaque plate floating above a wreck.
                emit( x, z - .39f, .97f, .16f, 1.26f, 1.39f, metal );
                emit( x - .39f, z, .16f, .92f, 1.26f, 1.39f, metal, art );
            } else {
                emit( x, z, .99f, .96f, 1.31f, 1.43f, paint, art );
            }
            break;
        case role::seat:
            emit( x, z, .66f, .62f, .31f, .54f, interior, art );
            if( !part.broken ) {
                emit( x - .25f, z, .13f, .64f, .49f, 1.03f, interior );
            }
            break;
        case role::engine:
            // Under a vehicle's native roof/hood the engine exists but is
            // not an extra block exposed on top of the painted car bonnet.
            emit( x, z, .70f, .63f, .26f, part.broken ? .49f : .64f, metal,
                    profile.enclosed ? std::nullopt : art );
            break;
        case role::cargo: {
            // Cargo under a native closed vehicle's bonnet/trunk is real,
            // but it must not pop through the painted car's exterior as
            // separate gray boxes. Exposed pickup cargo stays visible.
            const auto cell = profile.cells.find( {part.mount_x, part.mount_y} );
            const bool enclosed_bay = profile.enclosed && cell != profile.cells.end() &&
                                      ( cell->second.body || cell->second.roof );
            if( !enclosed_bay ) {
                emit( x, z, .76f, .77f, .32f, .55f, interior, art );
            }
            break;
        }
        case role::body:
            // Fully integrated into the native outer shell (side panels and
            // hood). Do NOT add an unpainted cube per halfboard / hatch.
            if( !profile.enclosed ) {
                emit( x, z, .90f, .90f, .31f, .58f, profile.paint, art );
            }
            break;
        case role::light: {
            const bool rear = part.id.find( "tail" ) != std::string::npos ||
                              part.id.find( "brake" ) != std::string::npos;
            const std::array<float, 3> lens = rear ?
                                              std::array<float, 3>{ .76f, .11f, .10f } :
                                              std::array<float, 3>{ .89f, .86f, .71f };
            emit( x + ( rear ? -.41f : .41f ), z, .10f, .23f,
                    .59f, .73f, lens, art );
            break;
        }
        case role::other:
            // Radio, horn, dashboard, sensors and batteries mounted inside
            // an enclosed car must not form an exposed row of gray cubes.
            // An unfamiliar mod part without an enclosure still receives a
            // compact visible module rather than silently disappearing.
            if( !profile.enclosed ) {
                emit( x, z, .48f, .46f, .29f, part.broken ? .39f : .50f,
                        metal, art );
            }
            break;
        default:
            return false;
    }
    return true;
}

template<typename Box>
void emit_door( const part_view &part, const shell_profile &profile, const color3 &paint,
                const color3 &metal, const std::optional<fp3d_materials::uv_rect> &art,
                const Box &box )
{
    const auto &coating = profile.enclosed ? profile.paint : paint;
    if( part.broken ) {
        box( 0, 0, .82f, .085f, .33f, .59f, coating );
        box( -.34f, 0, .075f, .09f, .58f, 1.06f, metal );
    } else {
        // In CDDA doors typically ALSO have WINDOW. That is an
        // upper transparent opening, not a full-height glass box.
        box( 0, 0, .86f, .10f, .34f, .88f, coating );
        box( 0, 0, .86f, .10f, 1.21f, 1.29f, coating );
        box( -.41f, 0, .07f, .10f, .87f, 1.28f, coating );
        box( .41f, 0, .07f, .10f, .87f, 1.28f, coating );
        // Sprite belongs on the top lip; a native top-down
        // picture looks absurd on a large vertical car door.
        box( 0, 0, .83f, .104f, .83f, .87f, coating, art );
    }
}

template<typename Box>
void emit_side_window( const color3 &glass_frame, const Box &box )
{
    box( 0, 0, .84f, .09f, .79f, .86f, glass_frame );
    box( -.40f, 0, .07f, .09f, .85f, 1.33f, glass_frame );
    box( .40f, 0, .07f, .09f, .85f, 1.33f, glass_frame );
    box( 0, 0, .84f, .09f, 1.28f, 1.35f, glass_frame );
}

template<typename Box, typename Sticker>
void emit_armor( const part_view &part, const color3 &metal,
                 const std::optional<fp3d_materials::uv_rect> &art, const Box &box,
                 const Sticker &sticker )
{
    if( part.broken ) {
        box( -.20f, 0, .35f, .13f, .35f, .95f, metal, art );
    } else {
        box( 0, 0, .88f, .13f, .30f, 1.15f, metal );
        sticker( art, .82f, .35f, 1.09f );
    }
}

template<typename Line>
void emit_windshield_bars( float x, float z, const color3 &paint, const Line &line )
{
    for( const float side : { -.43f, .43f } ) {
        line( { x + .36f, .88f, z + side }, { x - .34f, 1.33f, z + side }, paint, 1.f );
    }
}



bool has_glazing( const part_view &part )
{
    return !part.broken && !part.open &&
           ( part.kind == role::window || ( part.kind == role::door && part.glazed_door ) );
}

glass_quad modular_glass_quad( const vehicle_view &v, const part_view &part )
{
    const float x = static_cast<float>( part.mount_x - v.pivot_x );
    const float z = static_cast<float>( part.mount_y - v.pivot_y );
    if( part.kind == role::window && part.id.find( "windshield" ) != std::string::npos ) {
        return { { { x + .38f, .90f, z - .42f }, { x + .38f, .90f, z + .42f },
                   { x - .36f, 1.32f, z + .42f }, { x - .36f, 1.32f, z - .42f } } };
    }
    const auto pose = panel_at( v, part );
    const float distance = part.kind == role::door ? .48f : .34f;
    const auto corner = [&]( float side, float height ) -> point3 {
        return { x + pose.nx * distance + ( pose.along_x ? side : .057f ),
                 height, z + pose.nz * distance + ( pose.along_x ? .057f : -side ) };
    };
    return { corner( -.35f, .90f ), corner( .35f, .90f ),
             corner( .35f, 1.23f ), corner( -.35f, 1.23f ) };
}

std::array<float, 4> modular_glass_tint( bool textured )
{
    return textured ? std::array<float, 4>{ .90f, .93f, 1.f, .35f } :
           std::array<float, 4>{ .32f, .49f, .54f, .33f };
}


bool in_view( const vehicle_view &v, const fp3d_snapshot::scene_snapshot &scene,
              const fp3d_camera::frustum &view, fp3d_camera::offset camera )
{
    const tripoint_rel_ms rel = v.origin - scene.origin;
    // The bounding radius is independent of camera direction. Compare each
    // part's squared mount distance and take only one square root per car,
    // instead of one hypot() for every part on every 3D frame.
    float farthest_mount_squared = 0.0f;
    for( const part_view &part : v.parts ) {
        const float dx = float( part.mount_x - v.pivot_x );
        const float dy = float( part.mount_y - v.pivot_y );
        farthest_mount_squared = std::max( farthest_mount_squared, dx * dx + dy * dy );
    }
    const float extent = std::sqrt( farthest_mount_squared ) + 1.8f;
    return view.contains( rel.x() - camera.x,
                          rel.z() * fp3d_camera::floor_height + .73f - fp3d_camera::eye_height,
                          rel.y() - camera.y, extent );
}


std::vector<const vehicle_view *> visible_glass_vehicles(
    const fp3d_snapshot::scene_snapshot &scene, const fp3d_camera::frustum &view,
    fp3d_camera::offset camera )
{
    std::vector<const vehicle_view *> visible;
    for( const vehicle_view &v : scene.vehicles ) {
        if( in_view( v, scene, view, camera ) &&
            std::any_of( v.parts.begin(), v.parts.end(), has_glazing ) ) {
            visible.push_back( &v );
        }
    }
    const auto distance = [&]( const vehicle_view *v ) {
        const auto p = v->origin - scene.origin;
        const float x = p.x() - camera.x;
        const float z = p.y() - camera.y;
        const float y = p.z() * fp3d_camera::floor_height - fp3d_camera::eye_height;
        return x * x + y * y + z * z;
    };
    std::sort( visible.begin(), visible.end(), [&]( const auto *a, const auto *b ) {
        return distance( a ) > distance( b );
    } );

    return visible;
}

point3 local_glass_eye( const tripoint_rel_ms &offset, fp3d_camera::offset camera,
                        float cosine, float sine, float vertical_scale )
{
    const float dx = camera.x - offset.x();
    const float dz = camera.y - offset.y();
    return { cosine * dx + sine * dz,
             ( camera.height - offset.z() * fp3d_camera::floor_height ) / vertical_scale,
             -sine * dx + cosine * dz };
}
// Expand a camera-clipped segment to the same pixel width at both ends.
std::optional<glass_quad> screen_line_quad( point3 a, point3 b, float pixels,
        const fp3d_gpu_camera_math::matrix4 &projection, const fp3d_camera::frustum &view,
        int width, int height )
{
    const auto project = [&]( const point3 &p ) { return projection.transform( p.x, p.y, p.z ); };
    auto ca = project( a );
    auto cb = project( b );
    const auto clip_depth = [&]( float plane, bool lower ) {
        const bool a_out = lower ? ca[3] < plane : ca[3] > plane;
        const bool b_out = lower ? cb[3] < plane : cb[3] > plane;
        if( a_out && b_out ) {
            return false;
        }
        if( a_out != b_out ) {
            const float t = ( plane - ca[3] ) / ( cb[3] - ca[3] );
            const point3 clipped{ a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t,
                                  a.z + ( b.z - a.z ) * t };
            if( a_out ) {
                a = clipped;
                ca = project( a );
            } else {
                b = clipped;
                cb = project( b );
            }
        }
        return true;
    };
    if( !clip_depth( fp3d_camera::near_clip, true ) ||
        !clip_depth( fp3d_camera::far_clip, false ) ) {
        return std::nullopt;
    }
    const float dx = ( cb[0] / cb[3] - ca[0] / ca[3] ) * width;
    const float dy = ( cb[1] / cb[3] - ca[1] / ca[3] ) * height;
    const float length = std::hypot( dx, dy );
    if( length <= .0001f ) {
        return std::nullopt;
    }
    const float tangent = fp3d_camera::horizontal_tangent();
    const float rx = -dy / length * pixels * tangent / width;
    const float uy = dx / length * pixels * tangent / width;
    const point3 side{ view.right_x() * rx + view.up_x() * uy, view.up_y() * uy,
                       view.right_z() * rx + view.up_z() * uy };
    const auto corner = [&]( const point3 &p, float distance ) -> point3 {
        return { p.x + side.x * distance, p.y + side.y * distance, p.z + side.z * distance };
    };
    return glass_quad{ corner( a, ca[3] ), corner( b, cb[3] ),
                       corner( b, -cb[3] ), corner( a, -ca[3] ) };
}

} // namespace

gpu_vehicle_counts append_gpu_vehicle_geometry( const fp3d_snapshot::scene_snapshot &scene,
                            const fp3d_camera::pose &pose,
                            const fp3d_gpu_camera_math::matrix4 &projection, int width, int height,
                            fp3d_geometry::mesh &mesh,
                            std::vector<fp3d_geometry::draw_range> &ranges )
{
    fp3d_geometry::primitive_mesh_sink sink( mesh, ranges );
    gpu_vehicle_counts counts;
    const fp3d_camera::frustum view( pose.yaw, pose.pitch, static_cast<float>( width ) / height );
    for( const auto &v : scene.vehicles ) {
        const auto first = mesh.vertices.size();
        const auto profile = build_shell( v );
        const silhouette shape = build_silhouette( v, profile );
        const bool connected_shell = shape.usable;
        shell_profile modular = profile;
        modular.enclosed = false;
        modular.centerline_motorcycle_cabin = false;
        const auto emit = [&]( float x, float z, float width, float depth, float bottom, float top,
        const color3 &color, const std::optional<fp3d_materials::uv_rect> &decal = std::nullopt ) {
            fp3d_geometry::vehicle_cuboid( sink, x, z, width, depth, bottom, top, color, decal );
        };
        struct line_segment {
            point3 a;
            point3 b;
            color3 paint;
            float width;
        };
        std::vector<line_segment> details;
        const auto line = [&]( const point3 &a, const point3 &b, const color3 &paint, float pixels ) {
            details.push_back( { a, b, paint, pixels } );
        };
        const auto quad = [&]( const point3 &a, const point3 &b, const point3 &c,
                               const point3 &d, const color3 &paint ) {
            emit_painted_quad( sink, a, b, c, d, paint );
        };
        const auto bike_beam = [&]( const point3 &a, const point3 &b, float thickness, const color3 &paint ) {
            cabin_frame_beam( quad, a, b, thickness, paint );
        };
        const auto bike_wheel = [&]( float x, float z, bool broken ) {
            emit_bicycle_wheel( sink, x, z, broken, bike_beam, emit );
        };
        if( connected_shell ) {
            const auto panel_seam = [&]( const point3 &a, const point3 &b, const color3 &paint ) {
                line( a, b, paint, 1.35f );
            };
            emit_closed_silhouette( v, profile, shape, emit, quad, panel_seam );
            ++counts.connected_shells;
            for( const auto &part : v.parts ) {
                if( part.kind == role::seat ) {
                    emit_cabin_seat( v, part, emit );
                } else if( part.kind == role::other &&
                           ( part.id == "controls" || part.id.find( "dashboard" ) != std::string::npos ) ) {
                    emit_cabin_controls( v, part, emit, line );
                }
            }
        }
        for( const auto &part : v.parts ) {
            if( part.kind == role::wheel ||
                ( connected_shell && part.id.find( "solar_panel" ) == std::string::npos &&
                  !( part.kind == role::door && part.open ) ) ) {
                continue;
            }
            const auto before = mesh.vertices.size();
            if( !connected_shell && modular.pedal_cycle &&
                emit_bicycle_part( v, part, bike_wheel, bike_beam, emit ) ) {
                counts.modular_parts += mesh.vertices.size() != before;
                continue;
            }
            const auto uv = fp3d_materials::lookup_gpu( fp3d_materials::vehicle_key( part ) );
            const auto &body = connected_shell ? profile : modular;
            if( !emit_modular_part( v, part, body, uv, emit ) ) {
                const bool side_window = part.kind == role::window &&
                                         part.id.find( "windshield" ) == std::string::npos;
                if( part.kind == role::window && !side_window ) {
                    const float damage = part.broken ? .57f : .55f + .45f * std::clamp( part.health, 0.f, 1.f );
                    emit_windshield_bars( part.mount_x - v.pivot_x, part.mount_y - v.pivot_y,
                                          { .34f * damage, .45f * damage, .50f * damage }, line );
                }
                if( part.kind == role::door || part.kind == role::armor || side_window ) {
                    const float damage = part.broken ? .57f : .55f + .45f * std::clamp( part.health, 0.f, 1.f );
                    const float distance = part.kind == role::door ? .48f : part.kind == role::armor ? .49f : .34f;
                    const auto panel = panel_at( v, part );
                    if( part.kind == role::door ) {
                        emit_door( part, body, { .40f * damage, .49f * damage, .57f * damage },
                                   { .47f * damage, .50f * damage, .53f * damage }, uv, emit );
                    } else if( side_window ) {
                        emit_side_window( { .34f * damage, .45f * damage, .50f * damage }, emit );
                    } else {
                        const auto sticker = [&]( const auto &art, float width, float bottom, float top ) {
                            emit_door_sticker( sink, art, width, bottom, top );
                        };
                        emit_armor( part, { .47f * damage, .50f * damage, .53f * damage }, uv, emit, sticker );
                    }
                    const bool open = part.kind == role::door && part.open;
                    const float angle = open ? panel.outward * 77.f * pi / 180.f : 0.f;
                    const float sine = std::sin( angle );
                    const float cosine = std::cos( angle );
                    const float tx = part.mount_x - v.pivot_x + panel.nx * distance;
                    const float tz = part.mount_y - v.pivot_y + panel.nz * distance;
                    for( size_t i = before; i < mesh.vertices.size(); ++i ) {
                        auto &vertex = mesh.vertices[i];
                        auto &p = vertex.position;
                        auto &n = vertex.normal;
                        const float x = open ? cosine * ( p[0] + .36f ) + sine * p[2] - .36f : p[0];
                        const float z = open ? -sine * ( p[0] + .36f ) + cosine * p[2] : p[2];
                        const float nx = cosine * n[0] + sine * n[2];
                        const float nz = -sine * n[0] + cosine * n[2];
                        p = { tx + ( panel.along_x ? x : z ), p[1], tz + ( panel.along_x ? z : -x ) };
                        n = { panel.along_x ? nx : nz, n[1], panel.along_x ? nz : -nx };
                    }
                }
            }
            counts.modular_parts += mesh.vertices.size() != before;
        }
        const auto first_wheel_vertex = mesh.vertices.size();
        for( const auto &part : v.parts ) {
            if( part.kind != role::wheel ) {
                continue;
            }
            if( profile.pedal_cycle && !profile.enclosed &&
                emit_bicycle_part( v, part, bike_wheel, bike_beam, emit ) ) {
                ++counts.wheels;
                continue;
            }
            const auto spec = wheel_placement( v, part, profile );
            const auto uv = fp3d_materials::lookup_gpu( fp3d_materials::vehicle_key( part ) );
            fp3d_geometry::vehicle_wheel( sink, part.mount_x - v.pivot_x,
                                          part.mount_y - v.pivot_y + spec.lateral_offset,
                                          part.broken, spec.narrow, uv );
            ++counts.wheels;
        }
        const auto rel = v.origin - scene.origin;
        const float angle = v.angle_degrees * pi / 180;
        const float cosine = std::cos( angle );
        const float sine = std::sin( angle );
        const float storey = rel.z() * fp3d_camera::floor_height;
        for( size_t i = first; i < mesh.vertices.size(); ++i ) {
            auto &vertex = mesh.vertices[i];
            const auto [x, y, z] = vertex.position;
            const float scale = connected_shell && i < first_wheel_vertex ? enclosed_cab_vertical_scale : 1.f;
            vertex.position = { rel.x() + cosine * x - sine * z, storey + y * scale,
                                rel.y() + sine * x + cosine * z };
            const auto [nx, ny, nz] = vertex.normal;
            const float sy = ny / scale;
            const float length = std::hypot( nx, sy, nz );
            if( length > 0.f ) {
                vertex.normal = { ( cosine * nx - sine * nz ) / length, sy / length,
                                  ( sine * nx + cosine * nz ) / length };
            }
        }
        const auto line_to_world = [&]( const point3 &p ) -> point3 {
            return { rel.x() + cosine * p.x - sine * p.z,
                     storey + p.y * ( connected_shell ? enclosed_cab_vertical_scale : 1.f ),
                     rel.y() + sine * p.x + cosine * p.z };
        };
        sink.unlit( true );
        for( const auto &segment : details ) {
            if( const auto quad = screen_line_quad( line_to_world( segment.a ), line_to_world( segment.b ),
                                  segment.width, projection, view, width, height ) ) {
                emit_painted_quad( sink, ( *quad )[0], ( *quad )[1], ( *quad )[2],
                                   ( *quad )[3], segment.paint );
            }
        }
        sink.unlit( false );
    }
    return counts;
}

unsigned append_gpu_vehicle_glass( const fp3d_snapshot::scene_snapshot &scene,
                                   const fp3d_camera::frustum &view, fp3d_camera::offset camera,
                                   fp3d_geometry::mesh &mesh,
                                   std::vector<fp3d_geometry::draw_range> &ranges )
{
    fp3d_geometry::primitive_mesh_sink sink( mesh, ranges );
    unsigned count = 0;
    for( const auto *v : visible_glass_vehicles( scene, view, camera ) ) {
        const auto profile = build_shell( *v );
        const auto shape = build_silhouette( *v, profile );
        const auto rel = v->origin - scene.origin;
        const float angle = v->angle_degrees * pi / 180.f;
        const float cosine = std::cos( angle );
        const float sine = std::sin( angle );
        const float scale = profile.enclosed ? enclosed_cab_vertical_scale : 1.f;
        const auto append = [&]( glass_quad quad, const std::array<float, 4> &tint,
                                 const std::optional<fp3d_materials::uv_rect> &art = std::nullopt ) {
            for( auto &point : quad ) {
                const auto [x, y, z] = point;
                point = { rel.x() + cosine * x - sine * z,
                          rel.z() * fp3d_camera::floor_height + y * scale,
                          rel.y() + sine * x + cosine * z };
            }
            emit_glass_quad( sink, quad, tint, art );
            ++count;
        };
        if( shape.usable ) {
            const auto eye = local_glass_eye( rel, camera, cosine, sine, scale );
            for( const auto &pane : closed_glass_panes( *v, profile, shape, eye ) ) {
                append( pane, { .29f, .46f, .56f, .32f } );
            }
        } else {
            for( const auto &part : v->parts ) {
                if( has_glazing( part ) ) {
                    const auto art = fp3d_materials::lookup_gpu( fp3d_materials::vehicle_key( part ) );
                    append( modular_glass_quad( *v, part ), modular_glass_tint( art.has_value() ), art );
                }
            }
        }
    }
    return count;
}

std::uint64_t topology_signature( const vehicle_view &v )
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    blend_hash( hash, std::hash<int>{}( v.pivot_x ) );
    blend_hash( hash, std::hash<int>{}( v.pivot_y ) );
    for( const part_view &p : v.parts ) {
        blend_hash( hash, std::hash<int>{}( p.index ) );
        blend_hash( hash, std::hash<int>{}( p.mount_x ) );
        blend_hash( hash, std::hash<int>{}( p.mount_y ) );
        blend_hash( hash, std::hash<std::string>{}( p.id ) );
        blend_hash( hash, std::hash<std::string>{}( p.variant ) );
        blend_hash( hash, std::hash<unsigned>{}( static_cast<unsigned>( p.kind ) ) );
        blend_hash( hash, p.broken );
        blend_hash( hash, p.open );
        blend_hash( hash, p.glazed_door );
        blend_hash( hash, static_cast<unsigned>( std::clamp( p.health, 0.f, 1.f ) * 20 ) );
    }
    return hash;
}


} // namespace fp3d_vehicle
#endif
