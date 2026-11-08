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

#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

#include "fp3d_materials.h"
#include "fp3d_snapshot.h"

namespace fp3d_vehicle
{
namespace
{
std::unordered_map<std::uint64_t, GLuint> meshes;
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

void cuboid( float x, float z, float w, float d, float bottom, float top,
             const std::array<float, 3> &color,
             const std::optional<fp3d_materials::uv_rect> &decal = std::nullopt )
{
    const float l = x - w * .5f, r = x + w * .5f;
    const float n = z - d * .5f, s = z + d * .5f;
    glDisable( GL_TEXTURE_2D );
    glColor3fv( color.data() );
    glBegin( GL_QUADS );
    glNormal3f( 0, 1, 0 );
    glVertex3f( l, top, n ); glVertex3f( l, top, s );
    glVertex3f( r, top, s ); glVertex3f( r, top, n );
    glNormal3f( 0, 0, -1 );
    glVertex3f( r, bottom, n ); glVertex3f( l, bottom, n );
    glVertex3f( l, top, n ); glVertex3f( r, top, n );
    glNormal3f( 0, 0, 1 );
    glVertex3f( l, bottom, s ); glVertex3f( r, bottom, s );
    glVertex3f( r, top, s ); glVertex3f( l, top, s );
    glNormal3f( -1, 0, 0 );
    glVertex3f( l, bottom, n ); glVertex3f( l, bottom, s );
    glVertex3f( l, top, s ); glVertex3f( l, top, n );
    glNormal3f( 1, 0, 0 );
    glVertex3f( r, bottom, s ); glVertex3f( r, bottom, n );
    glVertex3f( r, top, n ); glVertex3f( r, top, s );
    glEnd();

    // A top-down sprite belongs on the part's upper face, in its own
    // mount space. Its UVs never slide when the entire vehicle turns.
    if( decal ) {
        glEnable( GL_TEXTURE_2D );
        glEnable( GL_ALPHA_TEST );
        glAlphaFunc( GL_GREATER, .25f );
        glColor3f( 1, 1, 1 );
        glBegin( GL_QUADS );
        glNormal3f( 0, 1, 0 );
        glTexCoord2f( decal->left, decal->top );
        glVertex3f( l + .004f, top + .003f, n + .004f );
        glTexCoord2f( decal->left, decal->bottom );
        glVertex3f( l + .004f, top + .003f, s - .004f );
        glTexCoord2f( decal->right, decal->bottom );
        glVertex3f( r - .004f, top + .003f, s - .004f );
        glTexCoord2f( decal->right, decal->top );
        glVertex3f( r - .004f, top + .003f, n + .004f );
        glEnd();
        glDisable( GL_ALPHA_TEST );
    }
}

// One small upright sticker on a real door, not the same sprite stretched
// around every side of the entire body. The rest is same-pack-neutral metal.
void door_sticker( const std::optional<fp3d_materials::uv_rect> &art,
                   float width, float bottom, float top )
{
    if( !art ) {
        return;
    }
    glEnable( GL_TEXTURE_2D );
    glEnable( GL_ALPHA_TEST );
    glAlphaFunc( GL_GREATER, .25f );
    glColor3f( 1, 1, 1 );
    for( float side : { -.057f, .057f } ) {
        glBegin( GL_QUADS );
        glNormal3f( 0, 0, side > 0 ? 1 : -1 );
        glTexCoord2f( art->left, art->bottom );
        glVertex3f( -.5f * width, bottom, side );
        glTexCoord2f( art->right, art->bottom );
        glVertex3f( .5f * width, bottom, side );
        glTexCoord2f( art->right, art->top );
        glVertex3f( .5f * width, top, side );
        glTexCoord2f( art->left, art->top );
        glVertex3f( -.5f * width, top, side );
        glEnd();
    }
    glDisable( GL_ALPHA_TEST );
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

void with_panel( const vehicle_view &v, const part_view &part,
                 float outer_distance, bool hinge, const std::function<void()> &callback )
{
    const panel_pose pose = panel_at( v, part );
    glPushMatrix();
    glTranslatef( part.mount_x - v.pivot_x + pose.nx * outer_distance,
                  0, part.mount_y - v.pivot_y + pose.nz * outer_distance );
    // A single local-X panel builder handles all four sides. A door's
    // hinge rotates in its own local space AFTER the vehicle root.
    if( !pose.along_x ) {
        glRotatef( 90, 0, 1, 0 );
    }
    if( hinge ) {
        glTranslatef( -.36f, 0, 0 );
        if( part.open ) {
            glRotatef( pose.outward * 77.0f, 0, 1, 0 );
        }
        glTranslatef( .36f, 0, 0 );
    }
    callback();
    glPopMatrix();
}

void wheel( float x, float z, bool damaged, bool narrow,
            const std::optional<fp3d_materials::uv_rect> &decal )
{
    const float radius = damaged ? .28f : narrow ? .34f : .45f;
    const float y = radius + .012f;
    const float half_thickness = narrow ? .105f : .20f;
    constexpr int sides = 16;
    glDisable( GL_TEXTURE_2D );
    glColor3f( damaged ? .23f : .085f, damaged ? .19f : .095f, .09f );
    glBegin( GL_QUAD_STRIP );
    for( int i = 0; i <= sides; ++i ) {
        const float a = 2 * pi * i / sides;
        glNormal3f( std::cos( a ), std::sin( a ), 0 );
        glVertex3f( x + radius * std::cos( a ), y + radius * std::sin( a ), z - half_thickness );
        glVertex3f( x + radius * std::cos( a ), y + radius * std::sin( a ), z + half_thickness );
    }
    glEnd();
    for( float side : { z - half_thickness, z + half_thickness } ) {
        glDisable( GL_TEXTURE_2D );
        glColor3f( .14f, .15f, .15f );
        glBegin( GL_TRIANGLE_FAN );
        glNormal3f( 0, 0, side < z ? -1 : 1 );
        glVertex3f( x, y, side );
        for( int i = 0; i <= sides; ++i ) {
            const float a = 2 * pi * i / sides;
            glVertex3f( x + radius * std::cos( a ), y + radius * std::sin( a ), side );
        }
        glEnd();
        if( decal ) {
            glEnable( GL_TEXTURE_2D );
            glEnable( GL_ALPHA_TEST );
            glAlphaFunc( GL_GREATER, .22f );
            glColor3f( 1, 1, 1 );
            glBegin( GL_TRIANGLE_FAN );
            glTexCoord2f( ( decal->left + decal->right ) * .5f,
                          ( decal->top + decal->bottom ) * .5f );
            glVertex3f( x, y, side + ( side < z ? -.002f : .002f ) );
            for( int i = 0; i <= sides; ++i ) {
                const float a = 2 * pi * i / sides;
                glTexCoord2f( ( decal->left + decal->right ) * .5f +
                              ( decal->right - decal->left ) * .5f * std::cos( a ),
                              ( decal->top + decal->bottom ) * .5f +
                              ( decal->bottom - decal->top ) * .5f * std::sin( a ) );
                glVertex3f( x + radius * .88f * std::cos( a ),
                            y + radius * .88f * std::sin( a ),
                            side + ( side < z ? -.002f : .002f ) );
            }
            glEnd();
            glDisable( GL_ALPHA_TEST );
        }
        glDisable( GL_TEXTURE_2D );
        glColor3f( damaged ? .30f : .62f, damaged ? .29f : .63f, .65f );
        glBegin( GL_TRIANGLE_FAN );
        glVertex3f( x, y, side + ( side < z ? -.004f : .004f ) );
        for( int i = 0; i <= sides; ++i ) {
            const float a = 2 * pi * i / sides;
            glVertex3f( x + radius * .29f * std::cos( a ),
                        y + radius * .29f * std::sin( a ), side );
        }
        glEnd();
    }
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

void painted_quad( const point3 &a, const point3 &b,
                   const point3 &c, const point3 &d, const color3 &paint )
{
    const point3 u{ b.x - a.x, b.y - a.y, b.z - a.z };
    const point3 v{ d.x - a.x, d.y - a.y, d.z - a.z };
    const point3 normal{ u.y * v.z - u.z * v.y,
                         u.z * v.x - u.x * v.z,
                         u.x * v.y - u.y * v.x };
    glDisable( GL_TEXTURE_2D );
    glColor3fv( paint.data() );
    glBegin( GL_QUADS );
    glNormal3f( normal.x, normal.y, normal.z );
    for( const point3 &p : { a, b, c, d } ) {
        glVertex3f( p.x, p.y, p.z );
    }
    glEnd();
}

// V10: A frame is not a GL_LINE and not a paper-thin painted QUAD.
// Construct a real six-sided beam between native-cabin connection points.
// This is entirely visual (no new vehicle parts/collision or seat mounts).
void cabin_frame_beam( const point3 &a, const point3 &b,
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
        painted_quad( corner( a, sx0, sw0 ), corner( b, sx0, sw0 ),
                      corner( b, sx1, sw1 ), corner( a, sx1, sw1 ), paint );
    };
    glEnable( GL_NORMALIZE );
    face( 1, 1, -1, 1 );
    face( -1, 1, -1, -1 );
    face( -1, -1, 1, -1 );
    face( 1, -1, 1, 1 );
    painted_quad( corner( a, -1, 1 ), corner( a, 1, 1 ),
                  corner( a, 1, -1 ), corner( a, -1, -1 ), paint );
    painted_quad( corner( b, 1, 1 ), corner( b, -1, 1 ),
                  corner( b, -1, -1 ), corner( b, 1, -1 ), paint );
}

void seam( const point3 &a, const point3 &b, const color3 &paint )
{
    glPushAttrib( GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT );
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glColor3fv( paint.data() );
    glLineWidth( 1.35f );
    glBegin( GL_LINES );
    glVertex3f( a.x, a.y, a.z );
    glVertex3f( b.x, b.y, b.z );
    glEnd();
    glPopAttrib();
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
void draw_closed_silhouette( const vehicle_view &v, const shell_profile &profile,
                             const silhouette &shape )
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
        cuboid( x, mid, 1.02f, z_right - z_left, .28f, .35f, dark );
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
                cabin_frame_beam( {x - .49f, .31f, skin_z},
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
                cuboid( x, skin_z, .62f, .060f, .31f, .48f, edges );
            } else if( wheel_at( profile, ix, native_z ) ) {
                // Open wheel arch, with only a short sheet above the tire.
                cuboid( x, skin_z, .99f, .060f, .89f, .99f, paint );
                cuboid( x - .45f, skin_z, .07f, .065f, .34f, .84f, paint );
                cuboid( x + .45f, skin_z, .07f, .065f, .34f, .84f, paint );
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
                cuboid( x, skin_z, 1.005f, .072f, .31f, top, paint );
                cuboid( x, skin_z, 1.005f, .074f,
                        top - .045f, top, edges );
            } else if( body ) {
                // Real closed lower side skin, below the window opening.
                cuboid( x, skin_z, 1.005f, .072f, .31f, belt, paint );
            } else {
                // A centreline motorcycle's front native frame cells
                // hold WIND­SHIELD and headlights, not halfboards; an
                // extremely low .44 sill left the base of the A pillar
                // unattached to the front fairing. The visual front
                // fender should end near the bonnet, not at the floor.
                const float top = profile.centerline_motorcycle_cabin &&
                                  ix > shape.roof_x_max ? .88f : .44f;
                cuboid( x, skin_z, 1.01f, .068f, .29f, top, edges );
            }
            if( cell != profile.cells.end() && cell->second.door ) {
                const float door_top = belt;
                seam( { x - .495f, .34f, skin_z + outside },
                      { x - .495f, door_top, skin_z + outside }, dark );
                seam( { x + .495f, .34f, skin_z + outside },
                      { x + .495f, door_top, skin_z + outside }, dark );
                seam( { x - .48f, door_top, skin_z + outside },
                      { x + .48f, door_top, skin_z + outside }, edges );
                cuboid( x + .23f, skin_z + outside, .12f, .044f,
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
            cuboid( wx, wz, .94f, .88f, .34f, top, paint );
            // Compact cover side seams; mark the wheel well without a
            // solid fake body wall crossing the whole passenger bay.
            for( float side : { -.445f, .445f } ) {
                cabin_frame_beam( { wx - .43f, .36f, wz + side },
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
        painted_quad( {nose_root, 1.02f, shape.left + .12f},
                      {front, .82f, shape.left + .17f},
                      {front, .82f, shape.right - .17f},
                      {nose_root, 1.02f, shape.right - .12f}, paint );
        seam( {nose_root, 1.025f, shape.left + .14f},
              {front, .825f, shape.left + .18f}, edges );
        seam( {nose_root, 1.025f, shape.right - .14f},
              {front, .825f, shape.right - .18f}, edges );
    }
    // Actual forward and rear silhouette: bumper/grille on the front (+X);
    // rear lights and boot are on the rear (-X), never interchangeable.
    // End caps connect the underbody to the bonnet and rear deck.
    // They are deliberately painted bodywork (not broad dark vents).
    cuboid( front - .11f, center, .22f, width * .94f, .36f, .81f, paint );
    cuboid( front, center, .11f, width * .91f, .32f, .47f, dark );
    cuboid( front + .012f, center, .13f, width * .42f, .52f, .66f,
            color3{ .17f, .19f, .21f } );
    if( shape.body_type != silhouette::shape::pickup ) {
        cuboid( rear + .11f, center, .22f, width * .93f, .36f, .87f, paint );
    }
    cuboid( rear, center, .13f, width * .91f, .32f, .46f, dark );

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
        const float z = std::clamp( float( part.mount_y - v.pivot_y ),
                                    shape.left + .28f, shape.right - .28f );
        const color3 lens = part.broken ?
                            color3{ .12f, .12f, .12f } :
                            tail ? color3{ .63f, .12f, .10f } :
                            color3{ .89f, .85f, .69f };
        // Two-wheel cabin has real paired headlights, but each should
        // resemble a flush lamp, not a 0.32-wide beige cardboard crate.
        const bool bike_cabin = profile.centerline_motorcycle_cabin;
        cuboid( ( tail ? rear - .060f : front + .060f ), z,
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
            cuboid( center_x, center_z, x1 - x0 + .012f,
                    z1 - z0 + .012f, 1.365f, roof_y, paint );
            if( native_z == roof_native_min_z ) {
                cuboid( center_x, z0 + .038f, x1 - x0, .076f,
                        1.30f, 1.40f, edges );
            }
            if( native_z == roof_native_max_z ) {
                cuboid( center_x, z1 - .038f, x1 - x0, .076f,
                        1.30f, 1.40f, edges );
            }
            // Solid front/rear roof header members are restricted to the
            // ACTUAL intact native roof run. A missing roof tile is never
            // bridged by a fictitious full-width crossbar.
            if( lo == shape.roof_x_min ) {
                cabin_frame_beam( {x0 + .04f, 1.355f, z0},
                                  {x0 + .04f, 1.355f, z1}, .10f, edges );
            }
            if( hi == shape.roof_x_max ) {
                cabin_frame_beam( {x1 - .04f, 1.355f, z0},
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
            const float outside = side == 0 ? shape.left : shape.right;
            const float inside = side == 0 ? shape.roof_left : shape.roof_right;
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
            cabin_frame_beam( low_a, high_a, post, frame );
            cabin_frame_beam( low_b, high_b, post, frame );
            cabin_frame_beam( high_a, high_b, .115f, frame );
            cabin_frame_beam( low_a, low_b, .07f, edges );
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
            painted_quad( {roof_front_x, belt, outside},
                          {lower_x + .01f, belt, outside},
                          {lower_x + .01f, 1.025f, outside},
                          {roof_front_x, 1.025f, outside}, paint );
        }
        for( int side = 0; side < 2; ++side ) {
            const float outer = side == 0 ? shape.left + .17f : shape.right - .17f;
            const float upper = side == 0 ? shape.roof_left : shape.roof_right;
            cabin_frame_beam( {lower_x, 1.02f, outer},
                              {roof_front_x, roof_side_y, upper},
                              .118f, paint );
        }
        cabin_frame_beam( {roof_front_x, roof_side_y, shape.roof_left},
                          {roof_front_x, roof_side_y, shape.roof_right},
                          .115f, paint );
    } else {
        painted_quad( {roof_front_x + .40f, .92f, shape.left},
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
                cabin_frame_beam( {roof_rear_x, roof_side_y, top_z},
                                  {deck_to, .90f, low_z}, .105f, paint );
            }
            seam( {deck_to, .92f, shape.left + .10f},
                  {deck_to, .92f, shape.right - .10f}, edges );
        }
        if( deck_to > rear + .05f ) {
            painted_quad( {rear, .83f, shape.left + .10f},
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
                cabin_frame_beam( {rear, belt, lower_z},
                                  {roof_rear_x, roof_side_y, upper_z},
                                  .105f, paint );
            }
            seam( {rear, belt, shape.left},
                  {rear, belt, shape.right}, edges );
        }
    } else {
        // Pickups keep an open bed; never seal the rear with sedan glass.
        for( int side = 0; side < 2; ++side ) {
            const float z = side == 0 ? shape.left : shape.right;
            cuboid( (rear + roof_rear_x) * .5f, z, roof_rear_x - rear,
                    .10f, .47f, .94f, paint );
        }
        cuboid( rear, center, .12f, width, .34f, .93f, paint );
    }
}

void draw_closed_silhouette_glass( const vehicle_view &v,
                                   const shell_profile &profile,
                                   const silhouette &shape, const point3 &eye )
{
    if( !shape.usable ) {
        return;
    }
    glDisable( GL_TEXTURE_2D );
    const float roof_front = shape.roof_x_max + .46f - v.pivot_x;
    const float roof_rear = shape.roof_x_min - .46f - v.pivot_x;
    const float front = shape.x_max + .49f - v.pivot_x;
    const float rear = shape.x_min - .49f - v.pivot_x;
    const float low_front = std::min( front - .28f, roof_front + .78f );
    // Semi-transparent glazing shared between the actual A pillars, roof
    // frame and bonnet. Unlike P2, one pane spans the cabin's front, not
    // four disjoint glasses drawn from individual mount cells.
    using glass_quad = std::array<point3, 4>;
    std::vector<glass_quad> panes;
    panes.reserve( ( shape.roof_x_max - shape.roof_x_min + 1 ) * 2 + 2 );
    const auto pane = [&]( const point3 &a, const point3 &b,
                           const point3 &c, const point3 &d ) {
        panes.push_back( { a, b, c, d } );
    };
    if( shape.front_glass ) {
        pane( {low_front, 1.02f, shape.left + .23f},
              {low_front, 1.02f, shape.right - .23f},
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
            const float outside = side == 0 ? shape.left : shape.right;
            const float inside = side == 0 ? shape.roof_left : shape.roof_right;
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
              {bottom, .95f, shape.right - .22f},
              {bottom, .95f, shape.left + .22f} );
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
    glDisable( GL_TEXTURE_2D );
    glColor4f( .29f, .46f, .56f, .32f );
    for( const glass_quad &q : panes ) {
        glBegin( GL_QUADS );
        for( const point3 &p : q ) {
            glVertex3f( p.x, p.y, p.z );
        }
        glEnd();
    }
}

void cabin_seat( const vehicle_view &v, const part_view &part )
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
        cuboid( x - .10f, z + side, .27f, .045f, .31f, .47f, rail );
    }
    // Compact, shallow seat cushion and upholstered backrest. Native +X
    // is forward; the upright backrest is to the rear at negative X.
    cuboid( x + .05f, z, .59f, .61f, .46f, .58f, cloth );
    cuboid( x - .235f, z, .105f, .60f, .52f,
            part.broken ? .73f : 1.00f, cloth );
    if( !part.broken ) {
        for( float side : { -.275f, .275f } ) {
            cuboid( x + .015f, z + side, .42f, .050f, .54f, .64f, shade );
        }
        if( reclining ) {
            cuboid( x - .26f, z, .10f, .30f, .96f, 1.08f, cloth );
        }
    }
}

void cabin_controls( const vehicle_view &v, const part_view &part )
{
    const float x = float( part.mount_x - v.pivot_x );
    const float z = float( part.mount_y - v.pivot_y );
    const color3 dark{ .16f, .16f, .18f };
    if( part.id.find( "dashboard" ) != std::string::npos ) {
        cuboid( x + .33f, z, .37f, .75f, .78f, .91f, dark );
    }
    if( part.id == "controls" || part.id.find( "steering_controls" ) != std::string::npos ) {
        // Plane of the ring faces the driver at negative local X.
        glPushAttrib( GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT );
        glDisable( GL_TEXTURE_2D );
        glDisable( GL_LIGHTING );
        glColor3f( .15f, .15f, .17f );
        glLineWidth( 3.f );
        glBegin( GL_LINE_LOOP );
        for( int i = 0; i < 16; ++i ) {
            const float angle = ( 2.f * pi * i ) / 16.f;
            glVertex3f( x + .37f, 1.01f + .18f * std::sin( angle ),
                        z + .18f * std::cos( angle ) );
        }
        glEnd();
        glBegin( GL_LINES );
        glVertex3f( x + .37f, .85f, z );
        glVertex3f( x + .37f, 1.01f, z );
        glEnd();
        glPopAttrib();
    }
}

void one_part( const vehicle_view &v, const part_view &part, const shell_profile &profile )
{
    const float x = float( part.mount_x - v.pivot_x );
    const float z = float( part.mount_y - v.pivot_y );
    const auto art = fp3d_materials::lookup( fp3d_materials::vehicle_key( part ) );
    const float health = std::clamp( part.health, 0.f, 1.f );
    const float damage = part.broken ? .57f : .55f + .45f * health;
    const std::array<float, 3> metal{ .47f * damage, .50f * damage, .53f * damage };
    const std::array<float, 3> paint{ .40f * damage, .49f * damage, .57f * damage };
    const std::array<float, 3> glass_frame{ .34f * damage, .45f * damage, .50f * damage };
    const std::array<float, 3> interior{ .29f * damage, .27f * damage, .25f * damage };
    switch( part.kind ) {
        case role::frame: {
            if( profile.enclosed ) {
                // One coherent body shell draws the vehicle floor instead
                // of repeated tile-sized slabs poking out on every side.
                if( part.broken ) {
                    cuboid( x, z, .87f, .085f, .16f, .25f, metal );
                    cuboid( x, z, .085f, .87f, .16f, .25f, metal );
                }
            } else {
                cuboid( x, z, .83f, .095f, .19f, .28f, metal );
                cuboid( x, z, .095f, .83f, .19f, .28f, metal, art );
            }
            break;
        }
        case role::wheel: {
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
            wheel( x, z + side * lateral, part.broken, narrow, art );
            break;
        }
        case role::door:
            with_panel( v, part, .48f, true, [&]() {
                const auto &coating = profile.enclosed ? profile.paint : paint;
                if( part.broken ) {
                    cuboid( 0, 0, .82f, .085f, .33f, .59f, coating );
                    cuboid( -.34f, 0, .075f, .09f, .58f, 1.06f, metal );
                } else {
                    // In CDDA doors typically ALSO have WINDOW. That is an
                    // upper transparent opening, not a full-height glass box.
                    cuboid( 0, 0, .86f, .10f, .34f, .88f, coating );
                    cuboid( 0, 0, .86f, .10f, 1.21f, 1.29f, coating );
                    cuboid( -.41f, 0, .07f, .10f, .87f, 1.28f, coating );
                    cuboid( .41f, 0, .07f, .10f, .87f, 1.28f, coating );
                    // Sprite belongs on the top lip; a native top-down
                    // picture looks absurd on a large vertical car door.
                    cuboid( 0, 0, .83f, .104f, .83f, .87f, coating, art );
                }
            } );
            break;
        case role::window:
            if( part.id.find( "windshield" ) != std::string::npos ) {
                // CDDA windshields lie across the front of the cab. One
                // sloping low glass pane per mount, not four giant upright
                // glass cubes towering over the vehicle.
                glDisable( GL_TEXTURE_2D );
                glColor3fv( glass_frame.data() );
                glBegin( GL_LINES );
                for( const float side : { -.43f, .43f } ) {
                    glVertex3f( x + .36f, .88f, z + side );
                    glVertex3f( x - .34f, 1.33f, z + side );
                }
                glEnd();
            } else {
                with_panel( v, part, .34f, false, [&]() {
                    cuboid( 0, 0, .84f, .09f, .79f, .86f, glass_frame );
                    cuboid( -.40f, 0, .07f, .09f, .85f, 1.33f, glass_frame );
                    cuboid( .40f, 0, .07f, .09f, .85f, 1.33f, glass_frame );
                    cuboid( 0, 0, .84f, .09f, 1.28f, 1.35f, glass_frame );
                } );
            }
            break;
        case role::roof:
            if( profile.enclosed && !part.broken ) {
                // The shell renders a connected roof using the same tinted
                // native texture for all mounted roof cells.
                break;
            }
            if( part.broken ) {
                // A collapsed roof keeps only remaining rails, not an
                // undamaged opaque plate floating above a wreck.
                cuboid( x, z - .39f, .97f, .16f, 1.26f, 1.39f, metal );
                cuboid( x - .39f, z, .16f, .92f, 1.26f, 1.39f, metal, art );
            } else {
                cuboid( x, z, .99f, .96f, 1.31f, 1.43f, paint, art );
            }
            break;
        case role::seat:
            cuboid( x, z, .66f, .62f, .31f, .54f, interior, art );
            if( !part.broken ) {
                cuboid( x - .25f, z, .13f, .64f, .49f, 1.03f, interior );
            }
            break;
        case role::engine:
            // Under a vehicle's native roof/hood the engine exists but is
            // not an extra block exposed on top of the painted car bonnet.
            cuboid( x, z, .70f, .63f, .26f, part.broken ? .49f : .64f, metal,
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
                cuboid( x, z, .76f, .77f, .32f, .55f, interior, art );
            }
            break;
        }
        case role::armor:
            with_panel( v, part, .49f, false, [&]() {
                if( part.broken ) {
                    cuboid( -.20f, 0, .35f, .13f, .35f, .95f, metal, art );
                } else {
                    cuboid( 0, 0, .88f, .13f, .30f, 1.15f, metal );
                    door_sticker( art, .82f, .35f, 1.09f );
                }
            } );
            break;
        case role::body:
            // Fully integrated into the native outer shell (side panels and
            // hood). Do NOT add an unpainted cube per halfboard / hatch.
            if( !profile.enclosed ) {
                cuboid( x, z, .90f, .90f, .31f, .58f, profile.paint, art );
            }
            break;
        case role::light: {
            const bool rear = part.id.find( "tail" ) != std::string::npos ||
                              part.id.find( "brake" ) != std::string::npos;
            const std::array<float, 3> lens = rear ?
                                              std::array<float, 3>{ .76f, .11f, .10f } :
                                              std::array<float, 3>{ .89f, .86f, .71f };
            cuboid( x + ( rear ? -.41f : .41f ), z, .10f, .23f,
                    .59f, .73f, lens, art );
            break;
        }
        case role::other:
            // Radio, horn, dashboard, sensors and batteries mounted inside
            // an enclosed car must not form an exposed row of gray cubes.
            // An unfamiliar mod part without an enclosure still receives a
            // compact visible module rather than silently disappearing.
            if( !profile.enclosed ) {
                cuboid( x, z, .48f, .46f, .29f, part.broken ? .39f : .50f,
                        metal, art );
            }
            break;
    }
}

void geometry( const vehicle_view &v )
{
    glPushAttrib( GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT |
                  GL_LIGHTING_BIT );
    glDisable( GL_CULL_FACE );
    glEnable( GL_LIGHTING );
    glEnable( GL_COLOR_MATERIAL );
    glColorMaterial( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE );
    glEnable( GL_NORMALIZE );
    glDisable( GL_BLEND );
    const shell_profile profile = build_shell( v );
    // Render the complete painted cabin at its intended adult-height
    // proportion. This Y-only matrix is LOCAL to the rigid body, and
    // applies equally to all doors, side windows, glass supports and
    // interior, preserving their mutual alignment through 360-degree
    // turns. Genuine wheels are drawn separately with round tires.
    glPushMatrix();
    if( profile.enclosed ) {
        glScalef( 1.f, enclosed_cab_vertical_scale, 1.f );
    }
    const silhouette shape = build_silhouette( v, profile );
    if( shape.usable ) {
        // V5 main pipeline: emit ONE car body with front, cabin and rear.
        // Keep only real, independently visible equipment. The old P1-P4
        // pipeline had generated an extra red box for every roof/door/frame.
        draw_closed_silhouette( v, profile, shape );
        for( const part_view &part : v.parts ) {
            if( part.kind == role::seat ) {
                // Any actual mounted seat remains visible, even if the
                // roof over that seat was destroyed or removed.
                cabin_seat( v, part );
            } else if( part.kind == role::other &&
                       ( part.id == "controls" ||
                         part.id.find( "dashboard" ) != std::string::npos ) ) {
                cabin_controls( v, part );
            } else if( part.kind == role::door && part.open ) {
                one_part( v, part, profile );
            }
        }
    } else {
        // Bicycles, open DIY frames, fully stripped wrecks and modded
        // structures still use their own true, individual native parts.
        for( const part_view &part : v.parts ) {
            if( part.kind != role::wheel ) {
                one_part( v, part, profile );
            }
        }
    }
    glPopMatrix();
    for( const part_view &part : v.parts ) {
        if( part.kind == role::wheel ) {
            one_part( v, part, profile );
        }
    }
    glPopAttrib();
}

void glass_pane( const vehicle_view &v, const part_view &part )
{
    if( part.broken || part.open ||
        ( part.kind != role::window && ( part.kind != role::door || !part.glazed_door ) ) ) {
        return;
    }
    const auto art = fp3d_materials::lookup( fp3d_materials::vehicle_key( part ) );
    const auto begin_pane = [&]() {
        if( art ) {
            glEnable( GL_TEXTURE_2D );
            glColor4f( .90f, .93f, 1.f, .35f );
        } else {
            glDisable( GL_TEXTURE_2D );
            glColor4f( .32f, .49f, .54f, .33f );
        }
    };
    if( part.kind == role::window && part.id.find( "windshield" ) != std::string::npos ) {
        const float x = float( part.mount_x - v.pivot_x );
        const float z = float( part.mount_y - v.pivot_y );
        begin_pane();
        // The front glass lies on a shallow rake across the actual native
        // windshield mount; a parked car no longer has multiple 2m glass
        // rectangles protruding vertically above its roof.
        glBegin( GL_QUADS );
        glNormal3f( .52f, .85f, 0 );
        if( art ) {
            glTexCoord2f( art->left, art->bottom );
        }
        glVertex3f( x + .38f, .90f, z - .42f );
        if( art ) {
            glTexCoord2f( art->right, art->bottom );
        }
        glVertex3f( x + .38f, .90f, z + .42f );
        if( art ) {
            glTexCoord2f( art->right, art->top );
        }
        glVertex3f( x - .36f, 1.32f, z + .42f );
        if( art ) {
            glTexCoord2f( art->left, art->top );
        }
        glVertex3f( x - .36f, 1.32f, z - .42f );
        glEnd();
    } else {
        with_panel( v, part, part.kind == role::door ? .48f : .34f,
                    part.kind == role::door, [&]() {
            begin_pane();
            glBegin( GL_QUADS );
            glNormal3f( 0, 0, 1 );
            if( art ) {
                glTexCoord2f( art->left, art->bottom );
            }
            glVertex3f( -.35f, .90f, .057f );
            if( art ) {
                glTexCoord2f( art->right, art->bottom );
            }
            glVertex3f( .35f, .90f, .057f );
            if( art ) {
                glTexCoord2f( art->right, art->top );
            }
            glVertex3f( .35f, 1.23f, .057f );
            if( art ) {
                glTexCoord2f( art->left, art->top );
            }
            glVertex3f( -.35f, 1.23f, .057f );
            glEnd();
        } );
    }
    glDisable( GL_TEXTURE_2D );
}

bool in_view( const vehicle_view &v, const fp3d_snapshot::scene_snapshot &scene,
              const fp3d_camera::frustum &view, fp3d_camera::offset camera )
{
    const tripoint_rel_ms rel = v.origin - scene.origin;
    float extent = 1.8f;
    for( const part_view &part : v.parts ) {
        extent = std::max( extent, std::hypot( float( part.mount_x - v.pivot_x ),
                                              float( part.mount_y - v.pivot_y ) ) + 1.8f );
    }
    return view.contains( rel.x() - camera.x,
                          rel.z() * fp3d_camera::floor_height + .73f - fp3d_camera::eye_height,
                          rel.y() - camera.y, extent );
}

void root( const vehicle_view &v, const fp3d_snapshot::scene_snapshot &scene )
{
    const tripoint_rel_ms rel = v.origin - scene.origin;
    glTranslatef( rel.x(), rel.z() * fp3d_camera::floor_height, rel.y() );
    // CDDA's local +X is forward, +Y is right, matching world GL X/Z when
    // applying a negative Y-axis angle. No rounded precalc vertex positions.
    glRotatef( -v.angle_degrees, 0, 1, 0 );
}
} // namespace

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

void release()
{
    for( const auto &mesh : meshes ) {
        if( mesh.second ) {
            glDeleteLists( mesh.second, 1 );
        }
    }
    meshes.clear();
}

void draw( const fp3d_snapshot::scene_snapshot &scene,
           const fp3d_camera::frustum &view, fp3d_camera::offset camera )
{
    for( const vehicle_view &v : scene.vehicles ) {
        if( v.parts.empty() || !in_view( v, scene, view, camera ) ) {
            continue;
        }
        const std::uint64_t signature = topology_signature( v );
        auto mesh = meshes.find( signature );
        if( mesh == meshes.end() && meshes.size() < 160 ) {
            const GLuint list = glGenLists( 1 );
            if( list ) {
                glNewList( list, GL_COMPILE );
                geometry( v );
                glEndList();
                mesh = meshes.emplace( signature, list ).first;
            }
        }
        glPushMatrix();
        root( v, scene );
        if( mesh != meshes.end() ) {
            glCallList( mesh->second );
        } else {
            geometry( v );
        }
        glPopMatrix();
    }
}

void draw_shadow( const fp3d_snapshot::scene_snapshot &scene, float radius )
{
    // The normal 3D pass has already populated the display list cache for
    // nearby cars. Reusing it guarantees exactly the same intact roof gaps,
    // open doors, inclined bodywork and wheel positions as the visible car.
    // Never draw separate sprite-only transparent glass as an opaque occluder.
    // Do not cull by the player camera frustum: a car outside the view can
    // still cast a shadow into it.
    // The opaque geometry depends on the same materials::lookup() atlas
    // used for the regular visible pass. Without binding the atlas here,
    // uncached cars can get a different cutout/outline in the depth pass.
    if( scene.vehicles.empty() ) {
        return;
    }
    fp3d_materials::bind();
    for( const vehicle_view &v : scene.vehicles ) {
        if( v.parts.empty() ) {
            continue;
        }
        const auto rel = v.origin - scene.origin;
        const float cx = static_cast<float>( rel.x() );
        const float cz = static_cast<float>( rel.y() );
        // Same coarse light-volume margin as the previous shadow pass; the
        // vehicle is based on native-visible snapshot geometry only.
        if( std::abs( cx ) > radius + 12.0f ||
            std::abs( cz ) > radius + 12.0f ) {
            continue;
        }
        glPushMatrix();
        root( v, scene );
        const auto it = meshes.find( topology_signature( v ) );
        if( it != meshes.end() ) {
            glCallList( it->second );
        } else {
            // A sun-facing occluder may be outside the player's view, so it
            // may have no display list yet. Render only once per new snapshot.
            geometry( v );
        }
        glPopMatrix();
    }
    fp3d_materials::unbind();
}

void draw_glass( const fp3d_snapshot::scene_snapshot &scene,
                 const fp3d_camera::frustum &view, fp3d_camera::offset camera )
{
    std::vector<const vehicle_view *> visible;
    for( const vehicle_view &v : scene.vehicles ) {
        if( in_view( v, scene, view, camera ) &&
            std::any_of( v.parts.begin(), v.parts.end(), []( const part_view & part ) {
            return ( part.kind == role::window ||
                     ( part.kind == role::door && part.glazed_door ) ) &&
                   !part.broken && !part.open;
        } ) ) {
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

    glPushAttrib( GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                  GL_CURRENT_BIT | GL_LIGHTING_BIT );
    glDisable( GL_CULL_FACE );
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_ALPHA_TEST );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glDepthMask( GL_FALSE );
    for( const vehicle_view *v : visible ) {
        glPushMatrix();
        root( *v, scene );
        // The alpha pass must use precisely the same cabin transform as
        // the opaque geometry or every pane would float below the frame.
        const shell_profile profile = build_shell( *v );
        const silhouette shape = build_silhouette( *v, profile );
        if( profile.enclosed ) {
            glScalef( 1.f, enclosed_cab_vertical_scale, 1.f );
        }
        if( shape.usable ) {
            // Convert world camera coordinates to the native car's
            // unrounded local mount coordinates for transparent sorting.
            const auto rel = v->origin - scene.origin;
            const float delta_x = camera.x - rel.x();
            const float delta_z = camera.y - rel.y();
            const float angle = v->angle_degrees * pi / 180.f;
            const float cs = std::cos( angle );
            const float sn = std::sin( angle );
            const point3 eye{ cs * delta_x + sn * delta_z,
                              ( camera.height -
                                rel.z() * fp3d_camera::floor_height ) /
                              ( profile.enclosed ? enclosed_cab_vertical_scale : 1.f ),
                              -sn * delta_x + cs * delta_z };
            draw_closed_silhouette_glass( *v, profile, shape, eye );
        } else {
            for( const part_view &part : v->parts ) {
                if( part.kind == role::window ||
                    ( part.kind == role::door && part.glazed_door ) ) {
                    glass_pane( *v, part );
                }
            }
        }
        glPopMatrix();
    }
    glPopAttrib();
}
} // namespace fp3d_vehicle
#endif
