#include "fp3d_scene.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <utility>
#include <unordered_map>
#include <unordered_set>

#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

#include "fp3d_snapshot.h"
#include "fp3d_materials.h"
#include "fp3d_shaderpacks.h"
#include "fp3d_vehicle_renderer.h"

namespace fp3d_scene
{
namespace
{
void floor( float x, float z, float y, const std::optional<fp3d_materials::uv_rect> &material )
{
    const auto uv = material.value_or( fp3d_materials::uv_rect{ 0, 0, 1, 1 } );
    if( material ) {
        glEnable( GL_TEXTURE_2D );
    } else {
        glDisable( GL_TEXTURE_2D );
    }
    glBegin( GL_QUADS );
    glNormal3f( 0, 1, 0 );
    glTexCoord2f( uv.left, uv.top );
    glVertex3f( x - .5f, y, z - .5f );
    glTexCoord2f( uv.left, uv.bottom );
    glVertex3f( x - .5f, y, z + .5f );
    glTexCoord2f( uv.right, uv.bottom );
    glVertex3f( x + .5f, y, z + .5f );
    glTexCoord2f( uv.right, uv.top );
    glVertex3f( x + .5f, y, z - .5f );
    glEnd();
}

void box( float x, float z, float width, float depth, float bottom, float top,
          const std::optional<fp3d_materials::uv_rect> &material, bool tile_sides = false,
          const fp3d_materials::furniture_uv *faces = nullptr )
{
    const float l = x - width / 2;
    const float r = x + width / 2;
    const float n = z - depth / 2;
    const float s = z + depth / 2;
    const auto uv = ( faces ? faces->top : material ).value_or(
                        fp3d_materials::uv_rect{ 0, 0, 1, 1 } );
    if( material ) {
        glEnable( GL_TEXTURE_2D );
    } else {
        glDisable( GL_TEXTURE_2D );
    }
    glBegin( GL_QUADS );
    glNormal3f( 0, 1, 0 );
    glTexCoord2f( uv.left, uv.top );
    glVertex3f( l, top, n );
    glTexCoord2f( uv.left, uv.bottom );
    glVertex3f( l, top, s );
    glTexCoord2f( uv.right, uv.bottom );
    glVertex3f( r, top, s );
    glTexCoord2f( uv.right, uv.top );
    glVertex3f( r, top, n );
    const auto side = [&]( float x0, float z0, float x1, float z1, bool front = false,
                          bool back = false ) {
        const auto side_uv = faces ? ( front ? faces->front :
                                      back && faces->back ? faces->back : faces->side ).value_or( uv ) : uv;
        // Atlas slots cannot use GL_REPEAT. Split upright wall faces into
        // one-metre rows and crop partial rows instead of stretching a square
        // terrain image over the full storey. Keep window surrounds aligned.
        const float u1 = tile_sides ? side_uv.left + ( side_uv.right - side_uv.left ) *
                         std::min( 1.0f, std::hypot( x1 - x0, z1 - z0 ) ) : side_uv.right;
        const int first = tile_sides ? static_cast<int>( std::floor( bottom ) ) : 0;
        const int last = tile_sides ? static_cast<int>( std::ceil( top ) ) : 1;
        for( int row = first; row < last; ++row ) {
            const float y0 = tile_sides ? std::max( bottom, static_cast<float>( row ) ) : bottom;
            const float y1 = tile_sides ? std::min( top, static_cast<float>( row + 1 ) ) : top;
            const float v0 = tile_sides ? side_uv.bottom + ( side_uv.top - side_uv.bottom ) * ( y0 - row ) : side_uv.bottom;
            const float v1 = tile_sides ? side_uv.bottom + ( side_uv.top - side_uv.bottom ) * ( y1 - row ) : side_uv.top;
            glTexCoord2f( side_uv.left, v0 );
            glVertex3f( x0, y0, z0 );
            glTexCoord2f( u1, v0 );
            glVertex3f( x1, y0, z1 );
            glTexCoord2f( u1, v1 );
            glVertex3f( x1, y1, z1 );
            glTexCoord2f( side_uv.left, v1 );
            glVertex3f( x0, y1, z0 );
        }
    };
    glNormal3f( 0, 0, -1 );
    side( faces ? r : l, n, faces ? l : r, n, false, true );
    glNormal3f( 0, 0, 1 );
    side( faces ? l : r, s, faces ? r : l, s, true );
    glNormal3f( -1, 0, 0 );
    side( l, faces ? n : s, l, faces ? s : n );
    glNormal3f( 1, 0, 0 );
    side( r, faces ? s : n, r, faces ? n : s );
    glEnd();
}

float plant_width( float height )
{
    return std::max( .9f, height <= 1.2f ? height : height * .7f );
}

void crossed_plant( float x, float z, float height, bool grass,
                    const fp3d_materials::uv_rect &uv )
{
    // Two fixed, perpendicular diagonal planes; both sides remain visible.
    // Ground-cover textures are opaque, so cut blade silhouettes in geometry.
    for( const float diagonal : {
             -1.0f, 1.0f
         } ) {
        const float width = plant_width( height );
        const auto vertex = [&]( float u, float v ) {
            glTexCoord2f( uv.left + ( uv.right - uv.left ) * u,
                          uv.bottom + ( uv.top - uv.bottom ) * v );
            glVertex3f( x + ( u - .5f ) * width, .015f + v * height,
                        z + diagonal * ( u - .5f ) * width );
        };
        if( grass ) {
            glBegin( GL_TRIANGLES );
            for( int blade = 0; blade < 7; ++blade ) {
                const float left = blade / 7.0f;
                const float right = ( blade + 1 ) / 7.0f;
                vertex( left, 0 );
                vertex( right, 0 );
                vertex( left + ( right - left ) * .65f, blade % 2 ? .7f : 1.0f );
            }
            glEnd();
        } else {
            glBegin( GL_QUADS );
            vertex( 0, 0 );
            vertex( 1, 0 );
            vertex( 1, 1 );
            vertex( 0, 1 );
            glEnd();
        }
    }
}

// A native treetop is a walkable z-level, but visually it is the top of
// the tree below. Crop that tree's currently selected tileset texture and
// display it on two perpendicular alpha-tested cards. No 3D twig boxes,
// duplicate full trunks or missing-terrain checkerboard sheets.
void treetop_crossed( float x, float z, const fp3d_materials::uv_rect &tree_uv )
{
    const float extent = tree_uv.bottom - tree_uv.top;
    const fp3d_materials::uv_rect crown{
        tree_uv.left, tree_uv.top + extent * .04f,
        tree_uv.right, tree_uv.top + extent * .66f
    };
    glPushAttrib( GL_ENABLE_BIT );
    glEnable( GL_TEXTURE_2D );
    glEnable( GL_ALPHA_TEST );
    glAlphaFunc( GL_GREATER, .5f );
    glNormal3f( 0, 1, 0 );
    crossed_plant( x, z, 1.9f, false, crown );
    glPopAttrib();
}

void window_face( const std::optional<fp3d_materials::uv_rect> &material,
                  float half_width = .42f, float bottom = .9f, float top = 2.1f )
{
    const auto uv = material.value_or( fp3d_materials::uv_rect{ 0, 0, 1, 1 } );
    if( material ) {
        glEnable( GL_TEXTURE_2D );
    } else {
        glDisable( GL_TEXTURE_2D );
    }
    // Exactly one upright image in the aperture; never tile it over the sill,
    // lintel, jambs or ground. Culling is disabled, so both sides are visible.
    glBegin( GL_QUADS );
    glTexCoord2f( uv.left, uv.bottom );
    glVertex3f( -half_width, bottom, 0 );
    glTexCoord2f( uv.right, uv.bottom );
    glVertex3f( half_width, bottom, 0 );
    glTexCoord2f( uv.right, uv.top );
    glVertex3f( half_width, top, 0 );
    glTexCoord2f( uv.left, uv.top );
    glVertex3f( -half_width, top, 0 );
    glEnd();
}

void round_furniture_part( const fp3d_furniture::part &part,
                           const fp3d_materials::furniture_uv &surfaces )
{
    constexpr int sides = 16;
    for( const bool top : { true, false } ) {
        const auto material = top ? surfaces.top : surfaces.side;
        const auto uv = material.value_or( fp3d_materials::uv_rect{ 0, 0, 1, 1 } );
        if( material ) {
            glEnable( GL_TEXTURE_2D );
        } else {
            glDisable( GL_TEXTURE_2D );
        }
        const float height = top ? part.top : part.bottom;
        glNormal3f( 0, top ? 1 : -1, 0 );
        glBegin( GL_TRIANGLE_FAN );
        glTexCoord2f( ( uv.left + uv.right ) / 2, ( uv.top + uv.bottom ) / 2 );
        glVertex3f( part.x, height, part.z );
        for( int side = 0; side <= sides; ++side ) {
            const float angle = side * 6.2831853f / sides;
            const float x = std::cos( angle );
            const float z = std::sin( angle ) * ( top ? -1 : 1 );
            glTexCoord2f( uv.left + ( x + 1 ) / 2 * ( uv.right - uv.left ),
                          uv.top + ( z + 1 ) / 2 * ( uv.bottom - uv.top ) );
            glVertex3f( part.x + x * part.width / 2, height, part.z + z * part.depth / 2 );
        }
        glEnd();
    }
    const auto uv = surfaces.side.value_or( fp3d_materials::uv_rect{ 0, 0, 1, 1 } );
    if( surfaces.side ) {
        glEnable( GL_TEXTURE_2D );
    } else {
        glDisable( GL_TEXTURE_2D );
    }
    glBegin( GL_QUADS );
    for( int side = 0; side < sides; ++side ) {
        for( const auto corner : { std::pair{ side, false }, std::pair{ side, true },
                                   std::pair{ side + 1, true }, std::pair{ side + 1, false } } ) {
            const float angle = corner.first * 6.2831853f / sides;
            const float x = std::cos( angle );
            const float z = std::sin( angle );
            glNormal3f( x, 0, z );
            glTexCoord2f( uv.left + ( uv.right - uv.left ) * corner.first / sides,
                          corner.second ? uv.top : uv.bottom );
            glVertex3f( part.x + x * part.width / 2, corner.second ? part.top : part.bottom,
                        part.z + z * part.depth / 2 );
        }
    }
    glEnd();
}

// A few crossed, irregular fire tongues sit behind the real stone arch,
// above the coal bed. They render without a square sprite background and
// preserve the GL state used by the rest of the world renderer.
void kiln_cavity_fire()
{
    glPushAttrib( GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
                  GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_LIGHTING );
    glDisable( GL_CULL_FACE );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glDepthMask( GL_FALSE );
    struct flame {
        float x, z, width, height;
    };
    constexpr std::array<flame, 5> flames{ {
            { -.115f, .105f, .085f, .305f },
            { -.038f, .046f, .105f, .420f },
            { .071f, .130f, .083f, .354f },
            { .012f, -.040f, .064f, .283f },
            { .148f, .000f, .048f, .246f }
        } };
    for( const flame &f : flames ) {
        const float y0 = .111f;
        const float tip = y0 + f.height;
        for( const bool across : { true, false } ) {
            glBegin( GL_TRIANGLES );
            glColor4f( 1.0f, .27f, .015f, .86f );
            glVertex3f( f.x - ( across ? f.width : 0.0f ), y0,
                        f.z - ( across ? 0.0f : f.width ) );
            glColor4f( 1.0f, .83f, .12f, .94f );
            glVertex3f( f.x + .018f, tip, f.z - .018f );
            glColor4f( 1.0f, .32f, .025f, .84f );
            glVertex3f( f.x + ( across ? f.width : 0.0f ), y0,
                        f.z + ( across ? 0.0f : f.width ) );
            glEnd();
        }
    }
    glPopAttrib();
}

void furniture_geometry( const fp3d_furniture::model &model, float x, float z,
                         unsigned facing = 0 )
{
    glPushMatrix();
    glTranslatef( x, 0, z );
    glRotatef( -90.0f * facing, 0, 1, 0 );
    unsigned part_index = 0;
    for( const auto &part : fp3d_furniture::parts( model.shape ) ) {
        const unsigned index = part_index++;
        const auto surfaces = fp3d_materials::lookup_furniture( model.id, index );
        // The UndeadPeople mailbox and streetlamp are 2D pseudo-3D art with
        // cast shadows baked in.  Their entire sprite cannot be mapped onto
        // a three-dimensional box without drawing the shadow on its sides.
        // Preserve the actual sprite palette but paint distinct physical
        // surfaces with flat metal, timber and a clean light diffuser.
        const bool mailbox = model.shape == fp3d_furniture::kind::mailbox;
        const bool lamp_head = model.shape == fp3d_furniture::kind::street_light &&
                               index >= 3;
        if( mailbox || lamp_head ) {
            glPushAttrib( GL_CURRENT_BIT | GL_ENABLE_BIT );
            if( mailbox ) {
                switch( index ) {
                    case 0: // weathered brown wooden support
                    case 1:
                        glColor3f( .43f, .32f, .23f );
                        break;
                    case 2: // red, enamel-painted metal letterbox
                        glColor3f( .68f, .21f, .19f );
                        break;
                    case 3: // dark front hatch and small outline
                        glColor3f( .25f, .21f, .21f );
                        break;
                    default: // raised red collection flag
                        glColor3f( .85f, .26f, .18f );
                        break;
                }
            } else if( index == 3 ) {
                glColor3f( .58f, .63f, .67f ); // opaque gray metal lamp shell
            } else {
                glDisable( GL_LIGHTING );
                glColor3f( .95f, .92f, .76f ); // softly lit underside
            }
            // Untextured surfaces prevent the giant false 2D shadow/photo
            // from stretching over mailbox sides and the underside lamp.
            const fp3d_materials::furniture_uv flat{};
            if( part.round ) {
                round_furniture_part( part, flat );
            } else {
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top,
                     std::nullopt );
                glPushMatrix();
                glTranslatef( part.x, part.bottom, part.z );
                glScalef( part.width, 1, part.depth );
                glRotatef( 180, 1, 0, 0 );
                floor( 0, 0, 0, std::nullopt );
                glPopMatrix();
            }
            glPopAttrib();
            continue;
        }
        if( part.glass ) {
            // A display case consists of visible open space, not a solid
            // blue-painted cube.  State restoration also works in GL lists.
            glPushAttrib( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                          GL_ENABLE_BIT | GL_CURRENT_BIT );
            glEnable( GL_BLEND );
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
            glDepthMask( GL_FALSE );
            glDisable( GL_LIGHTING );
            glColor4f( .57f, .83f, .94f, .27f );
            box( part.x, part.z, part.width, part.depth, part.bottom, part.top,
                 std::nullopt );
            glPopAttrib();
            continue;
        }
        if( part.round ) {
            round_furniture_part( part, surfaces );
            continue;
        }
        box( part.x, part.z, part.width, part.depth, part.bottom, part.top,
             surfaces.side, false, &surfaces );
        glPushMatrix();
        glTranslatef( part.x, part.bottom, part.z );
        glScalef( part.width, 1, part.depth );
        glRotatef( 180, 1, 0, 0 );
        floor( 0, 0, 0, surfaces.side );
        glPopMatrix();
    }
    if( model.shape == fp3d_furniture::kind::kiln ) {
        kiln_cavity_fire();
    }
    glPopMatrix();
}

// Steel tubing following any 3D diagonal, with square cross-section and
// real six-sided depth, rather than a widthless GL line or an opaque sprite.
void structural_tube( float ax, float ay, float az,
                      float bx, float by, float bz, float gauge )
{
    const float dx = bx - ax;
    const float dy = by - ay;
    const float dz = bz - az;
    const float horizontal = std::hypot( dx, dz );
    const float length = std::hypot( horizontal, dy );
    if( length < .0001f ) {
        return;
    }
    glPushMatrix();
    glTranslatef( ( ax + bx ) * .5f, ( ay + by ) * .5f, ( az + bz ) * .5f );
    if( horizontal > .0001f ) {
        glRotatef( std::atan2( horizontal, dy ) * 180.f /
                   3.14159265358979323846f, dz, 0, -dx );
    }
    box( 0, 0, gauge, gauge, -length * .5f, length * .5f, std::nullopt );
    glPopMatrix();
}

// t_radio_tower is a transparent, impassable steel frame in native CDDA.
// Build a genuine open lattice per z-level rather than stretching its X-shaped
// 2D icon onto a one-metre opaque cube. Adjacent storeys meet at the same rails.
void radio_tower_geometry( float x, float z, float brightness )
{
    constexpr float half = .34f;
    const float height = fp3d_camera::floor_height;
    const auto beam = [&]( float x0, float y0, float z0, float x1, float y1, float z1,
                           float thickness, bool painted ) {
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float dz = z1 - z0;
        const float horizontal = std::hypot( dx, dz );
        const float length = std::hypot( horizontal, dy );
        if( length < .0001f ) {
            return;
        }
        if( painted ) {
            glColor3f( brightness * .68f, brightness * .15f, brightness * .14f );
        } else {
            glColor3f( brightness * .56f, brightness * .60f, brightness * .65f );
        }
        glPushMatrix();
        glTranslatef( x + ( x0 + x1 ) * .5f, ( y0 + y1 ) * .5f,
                      z + ( z0 + z1 ) * .5f );
        if( horizontal > .0001f ) {
            glRotatef( std::atan2( horizontal, dy ) * 180.f / 3.14159265358979323846f,
                       dz, 0, -dx );
        }
        box( 0, 0, thickness, thickness, -length * .5f, length * .5f, std::nullopt );
        glPopMatrix();
    };
    glPushAttrib( GL_CURRENT_BIT | GL_ENABLE_BIT );
    glDisable( GL_TEXTURE_2D );
    // Corner posts: same offsets on each storey, with no zig-zag seams.
    for( const float sx : { -half, half } ) {
        for( const float sz : { -half, half } ) {
            beam( sx, 0, sz, sx, height, sz, .075f, false );
        }
    }
    // Horizontal square rings give the cross-bracing real anchor points.
    for( int level = 0; level <= 2; ++level ) {
        const float y = height * level / 2.f;
        for( const float side : { -half, half } ) {
            beam( -half, y, side, half, y, side, .052f, false );
            beam( side, y, -half, side, y, half, .052f, false );
        }
    }
    // Red diagonal X braces on each of the four open steel faces.
    for( int level = 0; level < 2; ++level ) {
        const float bottom = height * level / 2.f;
        const float top = height * ( level + 1 ) / 2.f;
        for( const float side : { -half, half } ) {
            beam( -half, bottom, side, half, top, side, .041f, true );
            beam( half, bottom, side, -half, top, side, .041f, true );
            beam( side, bottom, -half, side, top, half, .041f, true );
            beam( side, bottom, half, side, top, -half, .041f, true );
        }
    }
    glPopAttrib();
}

// Actual open chain-link wire.  One continuous narrow metal post per tile
// and a diagonal lattice on each half-span.  Unlike the UndeadPeople source
// icon, this has no opaque background, fake projected shadow or billboard.
void chain_link_mesh( float x, float z, const fp3d_furniture::part &span,
                      float brightness )
{
    const bool along_x = span.width > span.depth;
    const float width = along_x ? span.width : span.depth;
    const float low = -width * .5f;
    const float high = width * .5f;
    constexpr int divisions = 8;
    constexpr int levels = 13;
    glPushAttrib( GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_LIGHTING );
    glDisable( GL_ALPHA_TEST );
    glLineWidth( 1.85f );
    glColor3f( brightness * .75f, brightness * .81f, brightness * .86f );
    glBegin( GL_LINES );
    const auto vertex = [&]( float u, float y ) {
        glVertex3f( x + span.x + ( along_x ? u : 0.f ), y,
                    z + span.z + ( along_x ? 0.f : u ) );
    };
    for( int col = 0; col < divisions; ++col ) {
        const float u0 = low + ( high - low ) * col / divisions;
        const float u1 = low + ( high - low ) * ( col + 1 ) / divisions;
        for( int row = 0; row < levels; ++row ) {
            const float y0 = .13f + 1.70f * row / levels;
            const float y1 = .13f + 1.70f * ( row + 1 ) / levels;
            vertex( u0, y0 );
            vertex( u1, y1 );
            vertex( u0, y1 );
            vertex( u1, y0 );
        }
    }
    glEnd();
    glPopAttrib();
}

// Locked, closed and swung-open wire gates are actual hollow metal frames.
// The leaf mesh is built in the gate's orientation, not from its opaque 2D icon.
void chain_gate_geometry( const std::string &id, float x, float z,
                          bool along_x, float brightness )
{
    if( !fp3d_snapshot::is_chain_gate( id ) ) {
        return;
    }
    const bool opened = id == "t_chaingate_o" || id == "t_chickenwire_gate_o";
    const bool locked = id == "t_chaingate_l";
    const bool wooden_frame = id == "t_chickenwire_gate_o" ||
                              id == "t_chickenwire_gate_c";
    glPushAttrib( GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT );
    glDisable( GL_TEXTURE_2D );
    glPushMatrix();
    glTranslatef( x, 0, z );
    // Fence connection direction comes only from the captured neighbour
    // states and is shared with the native-ray hit proxies.
    if( !along_x ) {
        glRotatef( -90.f, 0, 1, 0 );
    }
    const auto steel = [&]( float factor = 1.f ) {
        // The native chickenwire gate is a timber frame carrying metal
        // chicken wire; plain chaingates remain galvanized steel.
        glColor3f( brightness * ( wooden_frame ? .49f : .59f ) * factor,
                   brightness * ( wooden_frame ? .34f : .66f ) * factor,
                   brightness * ( wooden_frame ? .19f : .72f ) * factor );
    };
    // Two anchor posts are permanent, regardless of open/closed/locked state.
    steel( .78f );
    for( const float side : { -.465f, .465f } ) {
        box( side, 0, .082f, .082f, 0, 2.04f, std::nullopt );
        box( side, 0, .126f, .126f, 1.98f, 2.075f, std::nullopt );
        box( side, 0, .16f, .16f, 0, .13f, std::nullopt );
    }
    // Top fixed lintel connects the two posts. The aperture is NOT a wall:
    // no opaque panel or invisible 1x2m hit plane is inserted here.
    structural_tube( -.465f, 2.00f, 0, .465f, 2.00f, 0, .052f );
    // The leaf has one real hinge at x=-.42. Turning it swings the whole
    // framed diamond-mesh panel; it does not dissolve into five posts.
    glPushMatrix();
    glTranslatef( -.425f, 0, 0 );
    if( opened ) {
        glRotatef( 88.f, 0, 1, 0 );
    }
    steel();
    constexpr float len = .76f;
    for( const float u : { .028f, len - .028f } ) {
        box( u, 0, .054f, .058f, .18f, 1.87f, std::nullopt );
    }
    for( const float y : { .20f, .985f, 1.85f } ) {
        box( len * .5f, 0, len, .052f, y - .028f, y + .028f, std::nullopt );
    }
    // Real diagonal tubes give the gate a recognizable shape at normal
    // game distance, even when thin mesh lines are subpixel on screen.
    steel( .91f );
    structural_tube( .055f, .24f, 0, len - .055f, 1.80f, 0, .028f );
    structural_tube( .055f, 1.80f, 0, len - .055f, .24f, 0, .028f );
    // Diamond wire lattice is optically transparent and casts no solid
    // black square over the 3D world or onto the ground.
    chain_link_mesh( 0, 0, fp3d_furniture::part{
        len * .5f, 0, len - .11f, 0, .20f, 1.85f }, brightness );
    steel( .71f );
    box( len - .025f, .05f, .11f, .085f, .91f, 1.08f, std::nullopt );
    if( locked ) {
        glColor3f( brightness * .84f, brightness * .73f, brightness * .33f );
        box( len - .025f, .09f, .10f, .055f, .82f, .92f, std::nullopt );
    }
    glPopMatrix();
    glPopMatrix();
    glPopAttrib();
}

void draw_cell( const fp3d_snapshot::cell &tile, float x, float z,
                const fp3d_snapshot::cell *below, const fp3d_snapshot::cell *above,
                int ew, int ns, const std::string &wall_surface,
                const std::array<float, 4> *tint = nullptr, bool defer_storage = false,
                unsigned furniture_facing = 0, const std::array<bool, 4> &fence_neighbours = {},
                unsigned climb_facing = 0, bool hide_vehicle_roof_ghost = false,
                bool lower_storey_memory = false )
{
    const auto color = [&]( float r, float g, float b ) {
        if( tint ) {
            glColor4fv( tint->data() );
        } else {
            glColor3f( r, g, b );
        }
    };
    using kind = fp3d_snapshot::terrain_kind;
    const bool memory = tile.known == fp3d_snapshot::knowledge::remembered;
    const bool treetop = tile.terrain.compare( 0, 9, "t_treetop" ) == 0;
    const bool grass = tile.vegetation.compare( 0, 7, "t_grass" ) == 0;
    const bool door = tile.kind == kind::closed_door || tile.kind == kind::open_door ||
                      tile.kind == kind::damaged_door || tile.kind == kind::glass;
    const bool pillar = tile.terrain == "t_column" || tile.terrain == "t_pillar" ||
                        tile.terrain == "t_little_column" || tile.terrain == "t_column_halfway";
    const auto material = fp3d_materials::lookup(
                              pillar ? "t_concrete" :
                              tile.kind == kind::ladder ? "t_concrete" :
                              tile.kind == kind::downspout ? "t_dirt" :
                              tile.kind == kind::fence ?
                              tile.terrain == "t_concrete_railing" ||
                              tile.terrain.compare( 0, 11, "t_guardrail" ) == 0 ?
                              "t_concrete" : "t_dirt" :
                              tile.kind == kind::window ? "t_floor" :
                              treetop || ( tile.vegetation == tile.terrain && !grass ) ?
                              "t_dirt" : tile.terrain );
    // On a higher storey the native LOS may downgrade already explored
    // lower-level surfaces to remembered cells. The usual 0.23 multiplier
    // combined with the world's ambient lighting makes those walls black.
    // Keep them visibly remembered (below 0.9) without revealing unknown
    // terrain or changing the simulation's perception rules.
    const float remembered_brightness = lower_storey_memory ? .60f : .23f;
    const float brightness = memory ? remembered_brightness : .9f;
    color( memory ? remembered_brightness * .57f : .32f,
           memory ? remembered_brightness * .57f : .36f,
           memory ? remembered_brightness * .57f : .28f );
    if( material ) {
        color( brightness, brightness, brightness );
    }
    glPushMatrix();
    // Opening ownership must not flicker when a lower level is outside the
    // snapshot or temporarily hidden by cross-z visibility.
    const bool stair_opening = tile.goes_down &&
                               ( tile.kind == kind::ladder ||
                                 tile.terrain.find( "stairs" ) != std::string::npos ||
                                 ( below && below->goes_up ) );
    // V8: omit the phantom upper level sheet only where a native car
    // stands on an exposed road and its overhead support is isolated.
    // A radio tower stacked through z-levels has open steelwork between floors.
    // This is presentation-only; the native tower remains impassable.
    const bool tower_floor_gap = tile.terrain == "t_radio_tower" &&
                                 below && below->terrain == "t_radio_tower";
    const bool tower_ceiling_gap = tile.terrain == "t_radio_tower" &&
                                   above && above->terrain == "t_radio_tower";
    if( tile.floor_present && !treetop && !hide_vehicle_roof_ghost &&
        !tile.is_ramp && !stair_opening && !tower_floor_gap ) {
        // The 2D chain gate/tower sprites contain fake shadow/perspective
        // art. Keep a neutral native floor, not a giant top-down X decal.
        const auto floor_material = tile.terrain == "t_radio_tower" ?
                                    fp3d_materials::lookup( "t_concrete" ) :
                                    fp3d_snapshot::is_chain_gate( tile.terrain ) ?
                                    fp3d_materials::lookup( "t_dirt" ) :
                                    door ? fp3d_materials::lookup( "t_floor" ) : material;
        if( floor_material ) {
            color( brightness, brightness, brightness );
        }
        // Open matched stair tiles across the full flight width.
        const float width = 1.0f;
        const float center = x;
        box( center, z, width, 1, -.08f, 0, floor_material );
        glPushMatrix();
        glTranslatef( center, -.08f, z );
        glScalef( width, 1, 1 );
        glRotatef( 180, 1, 0, 0 );
        floor( 0, 0, 0, floor_material );
        glPopMatrix();
        if( !tile.blood.empty() ) {
            if( const auto blood = fp3d_materials::lookup( "$blood:" + tile.blood ) ) {
                glEnable( GL_BLEND );
                glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                // Blood is a surface decal, not a self-illuminating HUD
                // overlay. Leave GL_LIGHTING active: floor() already emits
                // the true upward normal and the same native world light
                // as the concrete/asphalt beneath it. The world post-pass
                // then applies the same directional shadow map to both.
                glDepthMask( GL_FALSE );
                color( brightness, brightness, brightness );
                floor( x, z, .006f, blood );
                glDepthMask( GL_TRUE );
                glDisable( GL_BLEND );
            }
        }
    }
    const bool upper_floor_surface = above && above->floor_present && !above->is_ramp;
    if( tile.ceiling_present && !upper_floor_surface && !treetop &&
        !( tile.vegetation == tile.terrain && tile.vegetation_height >= 4.0f ) &&
        !hide_vehicle_roof_ghost && !tower_ceiling_gap &&
        tile.terrain != "t_radio_tower" ) {
        // This is the lower face of the overhead floor, not another wooden
        // roof. The floor above provides its material when it is an exposed
        // metal deck; otherwise keep the neutral interior ceiling fallback.
        const auto underside = fp3d_materials::lookup(
                                   tile.ceiling_surface.empty() ? "t_floor" :
                                   tile.ceiling_surface );
        color( brightness, brightness, brightness );
        glPushMatrix();
        glTranslatef( x, fp3d_camera::floor_height - .08f, z );
        glRotatef( 180, 1, 0, 0 );
        floor( 0, 0, 0, underside );
        glPopMatrix();
    }
    if( treetop ) {
        // Use the corresponding original tree art, not a synthesized 3D
        // crown. Cropping to the upper part avoids floating duplicate trunks.
        if( const auto tree = fp3d_materials::lookup(
                fp3d_materials::treetop_plant_key( tile.terrain ) ) ) {
            color( brightness, brightness, brightness );
            treetop_crossed( x, z, *tree );
        }
    }
    if( tile.terrain == "t_wood_stairs_up_half" ) {
        // The native unfinished stage has stringers, but no usable stair link.
        // Its presentation never grants GOES_UP to the actual terrain.
        glPushMatrix();
        glTranslatef( x + .25f, fp3d_camera::floor_height / 2, z );
        glRotatef( std::atan( fp3d_camera::floor_height ) * 180 / 3.14159265f, 1, 0, 0 );
        const float length = std::sqrt( 1 + fp3d_camera::floor_height * fp3d_camera::floor_height );
        for( const float side : { -.21f, .21f } ) {
            box( side, 0, .07f, length, -.04f, .04f, material );
        }
        glPopMatrix();
    }
    if( tile.kind == kind::ladder || tile.kind == kind::downspout ) {
        const auto surface = fp3d_materials::lookup( tile.terrain == "t_vitrified_downspout" ?
                             "t_vitrified_floor" : "t_metal_floor" );
        const fp3d_materials::furniture_uv surfaces{ surface, surface, surface };
        color( brightness, brightness, brightness );
        glPushMatrix();
        glTranslatef( x, 0, z );
        glRotatef( -90.0f * climb_facing, 0, 1, 0 );
        for( const auto &part : fp3d_snapshot::climb_parts( tile ) ) {
            if( part.round ) {
                round_furniture_part( part, surfaces );
            } else {
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top, surface );
            }
        }
        glPopMatrix();
    }
    if( tile.kind != kind::ladder && tile.goes_up && ( ( above && ( above->goes_down || !above->floor_present ) ) ||
                          ( !above && !tile.ceiling_present && !tile.exterior_only &&
                            tile.known == fp3d_snapshot::knowledge::visible ) ) ) {
        // Presentation steps; native stair movement/collision is untouched.
        auto surface = material;
        if( surface && tile.stairs ) {
            const float width = surface->right - surface->left;
            surface->left += width * ( tile.stairs->width - 1 - tile.stairs->east ) / tile.stairs->width;
            surface->right = surface->left + width / tile.stairs->width;
        }
        for( const auto &part : fp3d_snapshot::stair_parts( tile ) ) {
            box( x, z + part.z, part.width, part.depth, part.bottom, part.top, surface );
        }
    }
    if( tile.is_ramp ) {
        // A ramp stays inside its native tile; no synthetic upper opening.
        glPushMatrix();
        glTranslatef( x, tile.ramp_delta * fp3d_camera::floor_height / 2, z );
        glRotatef( std::atan( tile.ramp_delta * fp3d_camera::floor_height ) * 180 / 3.14159265f, 1, 0, 0 );
        glScalef( 1, 1, std::sqrt( 1 + fp3d_camera::floor_height * fp3d_camera::floor_height ) );
        floor( 0, 0, 0, material );
        glPopMatrix();
    }
    if( !tile.vegetation.empty() ) {
        if( const auto plant = fp3d_materials::lookup( "$plant:" + tile.vegetation ) ) {
            glEnable( GL_TEXTURE_2D );
            // Crossed foliage was permanently full-bright at night. Use the
            // same stable world sun/ambient as the underlying terrain.
            glEnable( GL_ALPHA_TEST );
            glAlphaFunc( GL_GREATER, .5f );
            glNormal3f( 0, 1, 0 );
            color( brightness, brightness, brightness );
            crossed_plant( x, z, tile.vegetation_height,
                           tile.vegetation.compare( 0, 7, "t_grass" ) == 0, *plant );
            glDisable( GL_ALPHA_TEST );
        }
    }
    if( tile.furniture && ( !defer_storage ||
                           !fp3d_furniture::shows_contents( tile.furniture->shape ) ) ) {
        color( brightness, brightness, brightness );
        furniture_geometry( *tile.furniture, x, z, furniture_facing );
    }
    const bool modeled_terrain = pillar || tile.kind == kind::fence;
    const auto built_parts = modeled_terrain ?
                             fp3d_snapshot::terrain_model_parts( tile.terrain, fence_neighbours ) :
                             std::vector<fp3d_furniture::part>{};
    if( !built_parts.empty() ) {
        // The terrain tile remains native terrain (same collisions,
        // interaction, movement and save data). Its 3D rendering now uses
        // the same per-piece dimensions as pointer ray casting.
        const auto source = tile.kind == kind::fence ?
                            fp3d_materials::lookup( "$fence:" + tile.terrain ) :
                            fp3d_materials::lookup( "t_concrete" );
        const auto crop = [&]( float x0, float y0, float x1, float y1 ) -> std::optional<fp3d_materials::uv_rect> {
            if( !source ) {
                return std::nullopt;
            }
            const float du = source->right - source->left;
            const float dv = source->bottom - source->top;
            return fp3d_materials::uv_rect{ source->left + du * x0 / 31,
                                            source->top + dv * y0 / 31,
                                            source->left + du * x1 / 31,
                                            source->top + dv * y1 / 31 };
        };
        std::optional<fp3d_materials::uv_rect> surface = source;
        std::optional<fp3d_materials::uv_rect> rail = surface;
        if( tile.terrain == "t_fence" ) {
            surface = crop( 13, 8, 18, 16 ); // painted white picket, no shadows
            rail = crop( 10, 13, 23, 17 );
        } else if( tile.terrain == "t_privacy_fence" ) {
            surface = crop( 9, 6, 17, 20 ); // opaque redwood, not perspective
            rail = crop( 6, 15, 25, 19 );
        } else if( tile.terrain == "t_concrete_railing" ) {
            surface = crop( 3, 4, 10, 10 );
            rail = crop( 12, 15, 21, 18 );
        } else if( tile.terrain == "t_railing" ) {
            surface = crop( 12, 5, 19, 12 );
            rail = crop( 8, 12, 23, 17 );
        } else if( tile.terrain == "t_metal_railing" ) {
            surface = crop( 12, 7, 19, 12 );
            rail = crop( 9, 13, 24, 17 );
        } else if( tile.terrain.compare( 0, 11, "t_guardrail" ) == 0 ) {
            surface = crop( 14, 13, 19, 20 );
            rail = crop( 7, 13, 24, 20 );
        }
        color( brightness, brightness, brightness );
        for( size_t i = 0; i < built_parts.size(); ++i ) {
            const auto &p = built_parts[i];
            // UndeadPeople's picket/metal railing pixels include black
            // outlines and faux depth.  Repeating those on every upright
            // produced harsh zebra stripes; keep their actual paint colors.
            const bool matte = tile.terrain == "t_fence" ||
                               tile.terrain == "t_metal_railing" ||
                               tile.terrain == "t_railing";
            if( matte ) {
                glPushAttrib( GL_CURRENT_BIT );
                const bool wood = tile.terrain == "t_railing";
                const bool metal = tile.terrain == "t_metal_railing";
                const float r = wood ? .53f : metal ? .64f : .83f;
                const float g = wood ? .41f : metal ? .67f : .83f;
                const float b = wood ? .29f : metal ? .69f : .79f;
                const float factor = i > 1 ? .96f : 1.0f;
                glColor3f( brightness * r * factor, brightness * g * factor,
                           brightness * b * factor );
                box( x + p.x, z + p.z, p.width, p.depth, p.bottom, p.top,
                     std::nullopt );
                glPopAttrib();
            } else {
                const auto &paint = i > 1 && tile.terrain == "t_concrete_railing" ?
                                    rail : surface;
                box( x + p.x, z + p.z, p.width, p.depth, p.bottom, p.top, paint );
            }
        }
    } else if( fp3d_snapshot::is_chain_gate( tile.terrain ) ) {
        chain_gate_geometry( tile.terrain, x, z,
                             fp3d_snapshot::chain_gate_along_x( fence_neighbours ), brightness );
    } else if( tile.kind == kind::fence ) {
        const auto parts = fp3d_snapshot::fence_parts( fence_neighbours,
                           tile.terrain == "t_chainfence_posts" ||
                           tile.terrain == "t_chickenwire_fence_post" ||
                           tile.terrain == "t_fence_post" );
        // Galvanized tubes and actual see-through wirework instead of
        // stretching a 32px faux wire texture over a black 2-metre slab.
        // Restore the texture-enable state too: round_furniture_part()
        // changes GL_TEXTURE_2D when drawing the untextured post.
        glPushAttrib( GL_CURRENT_BIT | GL_ENABLE_BIT );
        const bool timber = tile.terrain.compare( 0, 19, "t_chickenwire_fence" ) == 0 ||
                            tile.terrain == "t_screened_porch_wall" ||
                            tile.terrain == "t_fence_post";
        glColor3f( brightness * ( timber ? .49f : .61f ),
                   brightness * ( timber ? .34f : .66f ),
                   brightness * ( timber ? .19f : .70f ) );
        const auto &post = parts.front();
        const fp3d_materials::furniture_uv plain{};
        // Round-part vertices are local to the tile.  Unlike box(), this
        // helper does not receive world x/z; without this translation each
        // fence post was drawn at the chunk origin instead of its own tile.
        glPushMatrix();
        glTranslatef( x, 0, z );
        round_furniture_part( post, plain );
        glPopMatrix();
        box( x, z, .095f, .095f, 1.95f, 2.02f, std::nullopt );
        for( size_t i = 1; i < parts.size(); ++i ) {
            const auto &span = parts[i];
            if( span.width > span.depth ) {
                box( x + span.x, z, span.width, .030f, 1.86f, 1.915f, std::nullopt );
                box( x + span.x, z, span.width, .025f, .11f, .145f, std::nullopt );
            } else {
                box( x, z + span.z, .030f, span.depth, 1.86f, 1.915f, std::nullopt );
                box( x, z + span.z, .025f, span.depth, .11f, .145f, std::nullopt );
            }
            chain_link_mesh( x, z, span, brightness );
        }
        glPopAttrib();
    } else if( tile.terrain == "t_radio_tower" ) {
        radio_tower_geometry( x, z, brightness );
    } else if( tile.kind == kind::wall || tile.kind == kind::obstacle ) {
        color( memory ? remembered_brightness : .58f,
               memory ? remembered_brightness : .60f,
               memory ? remembered_brightness : .64f );
        if( material ) {
            color( brightness, brightness, brightness );
        }
        box( x, z, .98f, .98f, 0, tile.kind == kind::wall ? fp3d_camera::floor_height : 1.0f,
             material, tile.kind == kind::wall );
    } else if( tile.kind == kind::window ) {
        const auto surround = fp3d_materials::lookup( wall_surface );
        color( brightness, brightness, brightness );
        glPushMatrix();
        glTranslatef( x, 0, z );
        if( ew < ns ) {
            glRotatef( 90, 0, 1, 0 );
        }
        box( 0, 0, 1, .18f, 0, .9f, surround, true );
        box( 0, 0, 1, .18f, 2.1f, fp3d_camera::floor_height, surround, true );
        box( -.46f, 0, .08f, .22f, .9f, 2.1f, surround, true );
        box( .46f, 0, .08f, .22f, .9f, 2.1f, surround, true );
        // The box primitive has no bottom, so close the lintel underside.
        glPushMatrix();
        glTranslatef( 0, 2.1f, 0 );
        glScalef( 1, 1, .18f );
        glRotatef( 180, 1, 0, 0 );
        floor( 0, 0, 0, surround );
        glPopMatrix();
        if( tile.pane == fp3d_snapshot::window_pane::covered ) {
            glDisable( GL_LIGHTING );
            glDisable( GL_ALPHA_TEST );
            window_face( fp3d_materials::lookup( "$window:" + tile.terrain ) );
            glEnable( GL_LIGHTING );
        }
        glPopMatrix();
    } else if( door ) {
        // Orientation uses only permitted snapshot neighbours, never the map.
        const bool along_x = ew >= ns;
        color( memory ? remembered_brightness : .62f,
               memory ? remembered_brightness : .36f,
               memory ? remembered_brightness : .15f );
        if( material ) {
            color( brightness, brightness, brightness );
        }
        glPushMatrix();
        glTranslatef( x, 0, z );
        if( !along_x ) {
            glRotatef( 90, 0, 1, 0 );
        }
        for( const auto &part : fp3d_snapshot::door_parts( tile.kind ) ) {
            if( tile.kind == kind::glass ) {
                color( brightness * .46f, brightness * .52f, brightness * .56f );
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top, std::nullopt );
            } else if( tile.kind == kind::damaged_door && material ) {
                // Map each surviving piece to its region of the original door art.
                const float u = material->right - material->left;
                const float v = material->bottom - material->top;
                const fp3d_materials::uv_rect front{
                    material->left + u * ( .5f + ( part.x - part.width / 2 ) / .98f ),
                    material->bottom - v * part.top / fp3d_camera::floor_height,
                    material->left + u * ( .5f + ( part.x + part.width / 2 ) / .98f ),
                    material->bottom - v * part.bottom / fp3d_camera::floor_height
                };
                const fp3d_materials::furniture_uv faces{ front, front, front };
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top, material, false, &faces );
            } else {
                box( part.x, part.z, part.width, part.depth, part.bottom, part.top, material );
            }
        }
        glPopMatrix();
    }
    glPopMatrix();
}

constexpr int chunk_size = 8;
struct chunk_mesh {
    GLuint list = 0;
    std::uint64_t signature = 0;
    unsigned generation = 0;
};
struct chunk_data {
    std::vector<const fp3d_snapshot::cell *> tiles;
    float height = fp3d_camera::floor_height;
    float bottom = 0;
};
std::unordered_map<tripoint_abs_ms, const fp3d_snapshot::cell *> cells;
std::unordered_set<tripoint_abs_ms> vehicle_roof_ghosts;
std::unordered_map<tripoint_abs_ms, chunk_data> chunks;
std::unordered_map<tripoint_abs_ms, std::vector<const fp3d_snapshot::landscape_patch *>>
landscape_chunks;
unsigned indexed_generation = 0;
std::unordered_map<tripoint_abs_ms, chunk_mesh> landscape_meshes;
std::unordered_map<tripoint_abs_ms, chunk_mesh> meshes;
std::vector<const fp3d_snapshot::cell *> storage_cells;
std::vector<const fp3d_snapshot::cell *> stair_cells;
std::unordered_map<std::string, GLuint> storage_meshes;
unsigned mesh_material_revision = 0;
draw_statistics last_statistics;

// Hash only permitted presentation values, including neighbour dependencies.
void hash_value( std::uint64_t &hash, std::size_t value )
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + ( hash << 6 ) + ( hash >> 2 );
}
} // namespace

void release()
{
    // Dynamic vehicle display lists share the selected tileset atlas revision.
    fp3d_vehicle::release();
    for( const auto &entry : meshes ) {
        if( entry.second.list ) {
            glDeleteLists( entry.second.list, 1 );
        }
    }
    meshes.clear();
    for( const auto &entry : storage_meshes ) {
        glDeleteLists( entry.second, 1 );
    }
    storage_meshes.clear();
    storage_cells.clear();
    stair_cells.clear();
    for( const auto &entry : landscape_meshes ) {
        if( entry.second.list ) {
            glDeleteLists( entry.second.list, 1 );
        }
    }
    landscape_meshes.clear();
    cells.clear();
    vehicle_roof_ghosts.clear();
    chunks.clear();
    landscape_chunks.clear();
    indexed_generation = 0;
}

draw_statistics statistics()
{
    return last_statistics;
}

void draw_reflectivity_mask( const fp3d_snapshot::scene_snapshot &scene,
                             float yaw, float pitch, float aspect,
                             fp3d_camera::offset camera, float rain )
{
    glPushAttrib( GL_ENABLE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT |
                  GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_ALPHA_TEST );
    glDisable( GL_FOG );
    glDisable( GL_LIGHTING );
    glDisable( GL_BLEND );
    glDisable( GL_CULL_FACE );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );
    glDepthMask( GL_FALSE );
    glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadIdentity();
    const double tan_x=fp3d_camera::horizontal_tangent()*fp3d_camera::near_clip;
    glFrustum( -tan_x, tan_x, -tan_x/aspect, tan_x/aspect,
               fp3d_camera::near_clip, fp3d_camera::far_clip );
    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadIdentity();
    glRotatef( pitch, 1, 0, 0 );
    glRotatef( yaw, 0, 1, 0 );
    glTranslatef( -camera.x, -camera.height, -camera.y );
    // Native-visible roads are a CONTINUOUS wet surface: do not draw 0.97m
    // islands with a 0.03m unmasked crack at EVERY terrain boundary.
    // Existing world depth still occludes walls, parked cars and furniture.
    // Material G-buffer-lite: R=wet reflectivity, G=surface roughness,
    // B=micro-normal strength. Preserve native classification and scene
    // occlusion, and never invent roughness for hidden terrain.
    // Batch all tiles in one draw to avoid immediate-mode calls per cell.
    glBegin( GL_QUADS );
    for( const auto &cell : scene.cells ) {
        if( cell.known != fp3d_snapshot::knowledge::visible ||
            cell.exterior_only || !cell.floor_present || cell.is_ramp ||
            cell.goes_down ) {
            continue;
        }
        const std::string &id=cell.terrain;
        const bool water=id.find( "water" ) != std::string::npos ||
                         id.find( "t_sewage" ) == 0;
        const bool paved=id.find( "t_road" ) == 0 ||
                         id.find( "t_pavement" ) == 0 ||
                         id.find( "t_asphalt" ) == 0 ||
                         id.find( "t_sidewalk" ) == 0 ||
                         id.find( "t_concrete" ) == 0;
        const float reflectivity=water ? .84f :
                                 paved ? std::clamp( rain * .64f, 0.0f, .64f ) : 0.0f;
        if( reflectivity < .02f ) { continue; }
        const auto rel=cell.position-scene.origin;
        const float x=static_cast<float>( rel.x() );
        const float z=static_cast<float>( rel.y() );
        const float y=rel.z()*fp3d_camera::floor_height+.008f;
        // Distinguish smooth water from wet asphalt and textured concrete.
        // Values are visual approximations of material roughness, not full
        // Minecraft PBR atlas data; independent of viewing direction.
        const bool concrete = id.find( "t_concrete" ) == 0 ||
                              id.find( "t_sidewalk" ) == 0;
        const float roughness = water ? .13f : concrete ? .69f : .48f;
        const float bump = water ? .08f : concrete ? .018f : .025f;
        glColor4f( reflectivity, roughness, bump, 1 );
        // The real 1-m floor is continuous. Old half-width .485 left a
        // visible 3-cm unreflective gap at every native tile boundary.
        // Overlap by 2 mm; shared actual depth still occludes cars/walls.
        constexpr float half = .502f;
        glVertex3f( x-half, y, z-half );
        glVertex3f( x+half, y, z-half );
        glVertex3f( x+half, y, z+half );
        glVertex3f( x-half, y, z+half );
    }
    glEnd();
    glPopMatrix();
    glMatrixMode( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode( GL_MODELVIEW );
    glPopAttrib();
}

bool vehicle_roof_sheet_suppressed( const tripoint_abs_ms &position,
                                    unsigned render_generation )
{
    return render_generation != 0 && indexed_generation == render_generation &&
           vehicle_roof_ghosts.find( position ) != vehicle_roof_ghosts.end();
}

void draw( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch,
           float aspect, fp3d_camera::offset camera_offset, const std::optional<blueprint> &preview,
           bool interactable, float swing, const std::optional<fp3d_targeting::target> &aim )
{
    const auto started = std::chrono::steady_clock::now();
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    // Horizontal FOV stays wide even when the original sidebar leaves a nearly
    // square map viewport. The far plane covers the native visibility square.
    const double half_width = fp3d_camera::near_clip * fp3d_camera::horizontal_tangent();
    const double half_height = half_width / aspect;
    glFrustum( -half_width, half_width, -half_height, half_height,
               fp3d_camera::near_clip, fp3d_camera::far_clip );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    glRotatef( pitch, 1, 0, 0 );
    glRotatef( yaw, 0, 1, 0 );
    const auto weather_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch() ).count();
    // scene.sky.visible is the AVATAR'S overhead/outside test. It can
    // switch in a single step when moving under a door lintel. It is NOT
    // a scene-wide sky visibility flag: a surface-level window may still
    // show daylight behind the room, and roofs already depth-occlude it.
    // Draw a stable world sky background at surface level, letting the
    // actual scene depth/roof/window pixels reveal or conceal it.
    fp3d_sky::state backdrop = scene.sky;
    backdrop.visible = scene.origin.z() >= 0;
    const float lightning = fp3d_weather::flash( scene.weather, weather_ms );
    fp3d_sky::draw( backdrop, scene.weather.clouds, lightning,
                    ( weather_ms % 600000 ) / 1000.0f );
    glTranslatef( -camera_offset.x, -camera_offset.height, -camera_offset.y );
    fp3d_weather::begin_atmosphere( scene.weather, scene.sky.daylight, lightning );
    // Native calendar-driven lighting: retain a small indoor visibility floor,
    // but never make midnight terrain as bright as the midday sun.
    // This is presentation only; native creature visibility is unchanged.
    // Isolated QA-only dark-light test: leave all scene geometry, sprites,
    // weather and native simulation untouched, but lower the fixed-function
    // light source to validate that world decals/items respond to darkness.
    // Never active in normal gameplay or without a frame-capture request.
    const char *qa_capture = std::getenv( "CDDA_FP3D_CAPTURE_FRAME" );
    const char *qa_dark = std::getenv( "CDDA_FP3D_QA_DARK_LIGHT" );
    const bool test_dark_world = qa_capture && qa_capture[0] == '1' &&
                                 qa_capture[1] == '\0' &&
                                 qa_dark && qa_dark[0] == '1' && qa_dark[1] == '\0';
    const float day = test_dark_world ? 0.0f :
                      std::clamp( scene.sky.daylight, 0.0f, 1.0f );
    const float clouds = std::clamp( scene.weather.clouds, 0.0f, 1.0f );
    const bool enhanced = fp3d_shaderpacks::usable();
    const float moon = scene.sky.moonlight * ( 1.0f - day );
    // GL_LIGHT0 is scene-wide. Tying it to the PLAYER's is_outside()
    // brightened the entire exterior as soon as the avatar entered a room,
    // and dimmed the street on the reverse transition. Use one physically
    // consistent sky illumination and let depth-shadow occluders determine
    // which *receivers* get direct sun. Existing Off mode is unchanged.
    // Daytime indoor surfaces still receive indirect skylight even when
    // the roof correctly blocks direct sun. Keep this scene-wide component
    // continuous across windows/doorways (never based on player outdoors).
    // Original unenhanced lighting remains unchanged.
    const float native_ambient = .105f +
                                 day * ( .38f - clouds * .06f ) +
                                 moon * .055f + lightning * .4f;
    const float native_direct = .065f + day * .72f * ( 1.0f - clouds * .55f ) +
                                moon * .065f + lightning * .6f;
    // The Off entry restores the exact previous 3D light strengths/direction.
    const float old_ambient = .35f * ( 1 - clouds * day * .4f ) + lightning * .5f;
    // On complex MC pack bridges favor sun-facing detail over flat ambient.
    // The controls are inert for 2D, Off and direct original final.fsh programs.
    const float mc_visual = fp3d_shaderpacks::mc_visual_strength();
    const float daylight_mix = std::clamp( day * ( 1.0f - clouds * .40f ), 0.0f, 1.0f );
    const float ambient_level = enhanced ?
                                native_ambient * ( 1.0f - 0.11f * mc_visual * daylight_mix ) :
                                old_ambient;
    const float direct_level = enhanced ?
                               native_direct * ( 1.0f + 0.33f * mc_visual * daylight_mix ) :
                               .75f;
    const GLfloat ambient[] = { ambient_level, enhanced ? ambient_level * .98f : ambient_level,
                                enhanced ? ambient_level * .96f : ambient_level, 1 };
    const GLfloat diffuse[] = { direct_level, enhanced ? direct_level * .98f : direct_level,
                                enhanced ? direct_level * .93f : direct_level, 1 };
    const bool sun_up = scene.sky.sun_y >= 0.0f;
    const GLfloat direction[] = {
        enhanced ? ( sun_up ? 1.0f : -1.0f ) * scene.sky.sun_x : -.4f,
        enhanced ? std::max( .08f, std::abs( scene.sky.sun_y ) ) : .8f,
        enhanced ? ( sun_up ? 1.0f : -1.0f ) * scene.sky.sun_z : .5f, 0
    };
    glEnable( GL_LIGHTING );
    glEnable( GL_LIGHT0 );
    glEnable( GL_COLOR_MATERIAL );
    glColorMaterial( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE );
    glLightfv( GL_LIGHT0, GL_AMBIENT, ambient );
    glLightfv( GL_LIGHT0, GL_DIFFUSE, diffuse );
    glLightfv( GL_LIGHT0, GL_POSITION, direction );
    const fp3d_camera::frustum view( yaw, pitch, aspect );
    if( mesh_material_revision != fp3d_materials::revision() ) {
        release();
        mesh_material_revision = fp3d_materials::revision();
    }
    const bool new_scene = scene.render_generation == 0 ||
                           indexed_generation != scene.render_generation;
    last_statistics.indexed_cells = 0;
    last_statistics.native_loaded_vehicles = scene.enumerated_vehicles;
    last_statistics.visible_vehicles = scene.vehicles.size();
    last_statistics.visible_vehicle_parts = scene.visible_vehicle_parts;
    if( new_scene ) {
        cells.clear();
        storage_cells.clear();
        stair_cells.clear();
        chunks.clear();
        landscape_chunks.clear();
        cells.reserve( scene.cells.size() );
        for( const auto &tile : scene.cells ) {
            cells.emplace( tile.position, &tile );
            if( tile.furniture && fp3d_furniture::shows_contents( tile.furniture->shape ) ) {
                storage_cells.push_back( &tile );
            }
            if( tile.known == fp3d_snapshot::knowledge::visible && !tile.exterior_only &&
                ( tile.terrain.find( "stairs" ) != std::string::npos ||
                  tile.kind == fp3d_snapshot::terrain_kind::ladder || tile.kind == fp3d_snapshot::terrain_kind::downspout ) &&
                ( tile.goes_up || tile.goes_down || tile.kind == fp3d_snapshot::terrain_kind::downspout ) &&
                ( !tile.stairs || ( tile.stairs->south == tile.stairs->rows - 1 &&
                                   tile.stairs->east == tile.stairs->width - 1 ) ) ) {
                stair_cells.push_back( &tile );
            }
        }
        last_statistics.indexed_cells = scene.cells.size();
        constexpr int landscape_chunk_size = 48;
        for( const auto &patch : scene.landscape ) {
            const auto &p = patch.position;
            const tripoint_abs_ms key( p.x() - ( p.x() % landscape_chunk_size + landscape_chunk_size ) %
                                       landscape_chunk_size,
                                       p.y() - ( p.y() % landscape_chunk_size + landscape_chunk_size ) % landscape_chunk_size, p.z() );
            landscape_chunks[key].push_back( &patch );
        }
        indexed_generation = scene.render_generation;
    }
    last_statistics.landscape_chunks = last_statistics.landscape_rebuilt = 0;
    if( new_scene ) {
        for( auto it = landscape_meshes.begin(); it != landscape_meshes.end(); ) {
            if( !landscape_chunks.count( it->first ) ) {
                if( it->second.list ) {
                    glDeleteLists( it->second.list, 1 );
                }
                it = landscape_meshes.erase( it );
            } else {
                ++it;
            }
        }
    }
    for( const auto &entry : landscape_chunks ) {
        const auto &key = entry.first;
        const auto offset = key - scene.origin;
        if( !view.contains( offset.x() + 23.5f - camera_offset.x,
                            offset.z() * fp3d_camera::floor_height + 4 - fp3d_camera::eye_height,
                            offset.y() + 23.5f - camera_offset.y, 38 ) ) {
            continue;
        }
        ++last_statistics.landscape_chunks;
        auto &mesh = landscape_meshes[key];
        if( !mesh.list || mesh.generation != scene.render_generation || scene.render_generation == 0 ) {
            std::uint64_t signature = 0;
            for( const auto *patch : entry.second ) {
                hash_value( signature, std::hash<tripoint_abs_ms> {}( patch->position ) );
                hash_value( signature, std::hash<std::string> {}( patch->terrain ) );
                hash_value( signature, patch->width );
                hash_value( signature, patch->depth );
                hash_value( signature, patch->wooded | patch->ground_present << 1 );
                hash_value( signature, std::hash<float> {}( patch->building_height ) );
            }
            if( !mesh.list || mesh.signature != signature ) {
                if( !mesh.list ) {
                    mesh.list = glGenLists( 1 );
                }
                if( mesh.list ) {
                    glNewList( mesh.list, GL_COMPILE );
                } else {
                    glPushMatrix();
                    glTranslatef( offset.x(), offset.z() * fp3d_camera::floor_height, offset.y() );
                }
                for( const auto *patch : entry.second ) {
                    const auto local = patch->position - key;
                    const float x = local.x() + ( patch->width - 1 ) / 2.0f;
                    const float z = local.y() + ( patch->depth - 1 ) / 2.0f;
                    glPushMatrix();
                    glTranslatef( x, -.06f, z );
                    const bool overmap_road = patch->terrain == "t_pavement";
                    // Native 1m paving tiles use brightness .9. A single
                    // source 32px tile stretched to an entire 12x12 overmap
                    // patch became a large flat grey rectangle, falsely
                    // resembling a shadow beside the car. Make nearby
                    // coarse roads follow the same tile frequency and tint
                    // as native streets without loading hidden map tiles.
                    glColor3f( overmap_road ? .9f : .65f,
                               overmap_road ? .9f : .65f,
                               overmap_road ? .9f : .65f );
                    if( patch->ground_present ) {
                        const auto material = fp3d_materials::lookup( patch->terrain );
                        const bool near_road = overmap_road &&
                            std::abs( patch->position.x() - scene.origin.x() ) < 104 &&
                            std::abs( patch->position.y() - scene.origin.y() ) < 104;
                        if( near_road && patch->width * patch->depth <= 576 ) {
                            // Display-list compile only; never query offscreen
                            // submaps or generate another map chunk. Each UV
                            // rectangle samples a real 1m atlas road tile.
                            const float left = -.5f * patch->width;
                            const float north = -.5f * patch->depth;
                            for( int dz = 0; dz < patch->depth; ++dz ) {
                                for( int dx = 0; dx < patch->width; ++dx ) {
                                    floor( left + dx + .5f, north + dz + .5f,
                                           0, material );
                                }
                            }
                        } else {
                            // Distant visual-only landscape keeps low cost.
                            glPushMatrix();
                            glScalef( patch->width, 1, patch->depth );
                            floor( 0, 0, 0, material );
                            glPopMatrix();
                        }
                    }
                    if( patch->building_height > 0 && patch->width >= 8 && patch->depth >= 8 ) {
                        box( 0, 0, patch->width * .72f, patch->depth * .72f, 0,
                             patch->building_height, fp3d_materials::lookup( "t_wall" ) );
                        glPushMatrix();
                        glTranslatef( 0, patch->building_height + .02f, 0 );
                        glScalef( patch->width * .76f, 1, patch->depth * .76f );
                        floor( 0, 0, 0, fp3d_materials::lookup( "t_flat_roof" ) );
                        glPopMatrix();
                    }
                    if( patch->wooded && patch->width >= 8 && patch->depth >= 8 ) {
                        if( const auto tree = fp3d_materials::lookup( "$plant:t_tree" ) ) {
                            glEnable( GL_TEXTURE_2D );
                            glDisable( GL_LIGHTING );
                            glEnable( GL_ALPHA_TEST );
                            glAlphaFunc( GL_GREATER, .5f );
                            crossed_plant( -3, -2, 5, false, *tree );
                            crossed_plant( 3, 2, 5, false, *tree );
                            crossed_plant( -1, 4, 5, false, *tree );
                            glDisable( GL_ALPHA_TEST );
                            glEnable( GL_LIGHTING );
                        }
                    }
                    glPopMatrix();
                }
                if( mesh.list ) {
                    glEndList();
                } else {
                    glPopMatrix();
                }
                mesh.signature = signature;
                ++last_statistics.landscape_rebuilt;
            }
            mesh.generation = scene.render_generation;
        }
        glPushMatrix();
        glTranslatef( offset.x(), offset.z() * fp3d_camera::floor_height, offset.y() );
        if( mesh.list ) {
            glCallList( mesh.list );
        }
        glPopMatrix();
    }
    const auto known_cell = [&]( const tripoint_abs_ms & p ) -> const fp3d_snapshot::cell * {
        const auto it = cells.find( p );
        return it == cells.end() ? nullptr : it->second;
    };
    const auto known_wall = [&]( const tripoint_abs_ms & p ) {
        const auto *tile = known_cell( p );
        return tile && ( tile->kind == fp3d_snapshot::terrain_kind::wall ||
                         tile->kind == fp3d_snapshot::terrain_kind::glass ||
                         tile->kind == fp3d_snapshot::terrain_kind::window );
    };
    // Floor z+1 is 2.2 m, only 0.27 m above a V7 car's 1.93 m roof.
    // An isolated native floor/ceiling at that elevation appears as a
    // dark wooden floating polygon. Only a real, intact vehicle roof on
    // an outdoor road may suppress the extra sheet; actual building
    // upper floors, staircases and wide bridges remain visible.
    if( new_scene ) {
        vehicle_roof_ghosts.clear();
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
                    vehicle_roof_ghosts.insert( ground );
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
                        vehicle_roof_ghosts.insert( tile );
                    }
                }
            }
        }
    }
    const auto fence_links = [&]( const fp3d_snapshot::cell &tile ) {
        std::array<bool, 4> linked{};
        const std::array<tripoint_rel_ms, 4> directions{ tripoint_rel_ms::north, tripoint_rel_ms::east,
                                                       tripoint_rel_ms::south, tripoint_rel_ms::west };
        for( size_t i = 0; i < directions.size(); ++i ) {
            const auto *other = known_cell( tile.position + directions[i] );
            linked[i] = other &&
                        fp3d_snapshot::fence_connects( tile.terrain, other->terrain );
        }
        return linked;
    };
    const auto table_at = [&]( const tripoint_abs_ms &p ) {
        const auto *tile = known_cell( p );
        return tile && fp3d_furniture::chair_table( tile->furniture );
    };
    const auto furniture_facing = [&]( const fp3d_snapshot::cell &tile ) {
        if( !tile.furniture ) {
            return 0u;
        }
        if( tile.furniture->shape == fp3d_furniture::kind::chair ) {
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
    };
    const auto climb_facing = [&]( const fp3d_snapshot::cell &tile ) {
        if( tile.kind != fp3d_snapshot::terrain_kind::ladder &&
            tile.kind != fp3d_snapshot::terrain_kind::downspout ) { return 0u; }
        return fp3d_furniture::facing( fp3d_furniture::kind::cabinet,
        { known_wall( tile.position + tripoint_rel_ms::north ),
          known_wall( tile.position + tripoint_rel_ms::east ),
          known_wall( tile.position + tripoint_rel_ms::south ),
          known_wall( tile.position + tripoint_rel_ms::west ) } );
    };
    const auto window_wall = [&]( const fp3d_snapshot::cell & tile ) {
        for( const auto &direction : {
                 tripoint_rel_ms::east, tripoint_rel_ms::west,
                 tripoint_rel_ms::north, tripoint_rel_ms::south
             } ) {
            const auto *neighbour = known_cell( tile.position + direction );
            if( neighbour && neighbour->kind == fp3d_snapshot::terrain_kind::wall ) {
                return neighbour->terrain;
            }
        }
        return std::string( "t_wall" );
    };
    if( new_scene ) {
        for( const auto &tile : scene.cells ) {
            if( tile.open_air && !tile.ceiling_present ) {
                continue;
            }
            const auto &p = tile.position;
            const tripoint_abs_ms key( p.x() - ( p.x() % chunk_size + chunk_size ) % chunk_size,
                                       p.y() - ( p.y() % chunk_size + chunk_size ) % chunk_size, p.z() );
            auto &chunk = chunks[key];
            chunk.tiles.push_back( &tile );
            chunk.height = std::max( chunk.height, tile.vegetation_height );
            if( tile.terrain == "t_radio_tower" ) {
                chunk.height = std::max( chunk.height, fp3d_camera::floor_height + .75f );
            }
            if( tile.furniture ) {
                for( const auto &p : fp3d_furniture::parts( tile.furniture->shape ) ) {
                    chunk.height = std::max( chunk.height, p.top );
                }
            }
            if( tile.ramp_delta < 0 ) {
                chunk.bottom = -fp3d_camera::floor_height;
            }
        }
        for( auto it = meshes.begin(); it != meshes.end(); ) {
            if( !chunks.count( it->first ) ) {
                if( it->second.list ) {
                    glDeleteLists( it->second.list, 1 );
                }
                it = meshes.erase( it );
            } else {
                ++it;
            }
        }
    }
    last_statistics.chunks = last_statistics.rebuilt = 0;
    for( const auto &entry : chunks ) {
        const auto &key = entry.first;
        const auto &chunk = entry.second;
        const auto offset = key - scene.origin;
        const float y = offset.z() * fp3d_camera::floor_height;
        const float half_width = chunk_size / 2.0f + std::max( 0.0f,
                                 plant_width( chunk.height ) / 2 - .5f );
        const float radius = std::sqrt( 2 * half_width * half_width +
                                        ( chunk.height - chunk.bottom ) * ( chunk.height - chunk.bottom ) / 4 ) + .1f;
        if( !view.contains( offset.x() + ( chunk_size - 1 ) / 2.0f - camera_offset.x,
                            y + ( chunk.height + chunk.bottom ) / 2 - fp3d_camera::eye_height,
                            offset.y() + ( chunk_size - 1 ) / 2.0f - camera_offset.y, radius ) ) {
            continue;
        }
        ++last_statistics.chunks;
        auto &mesh = meshes[key];
        if( !mesh.list || mesh.generation != scene.render_generation || scene.render_generation == 0 ) {
            std::uint64_t signature = 0;
            for( const auto *tile : chunk.tiles ) {
                hash_value( signature, std::hash<tripoint_abs_ms> {}( tile->position ) );
                hash_value( signature, std::hash<std::string> {}( tile->terrain ) );
                hash_value( signature, std::hash<std::string> {}( tile->ceiling_surface ) );
                hash_value( signature, std::hash<std::string> {}( tile->vegetation ) );
                hash_value( signature, std::hash<std::string> {}( tile->blood ) );
                hash_value( signature, std::hash<std::string> {}( tile->appliance ) );
                if( tile->kind == fp3d_snapshot::terrain_kind::fence ||
                    fp3d_snapshot::is_chain_gate( tile->terrain ) ) {
                    for( bool linked : fence_links( *tile ) ) {
                        hash_value( signature, linked );
                    }
                }
                hash_value( signature, tile->blood_intensity );
                if( tile->stairs ) {
                    hash_value( signature, tile->stairs->rows );
                    hash_value( signature, tile->stairs->south );
                    hash_value( signature, tile->stairs->width );
                    hash_value( signature, tile->stairs->east );
                }
                hash_value( signature, std::hash<std::string> {}( tile->furniture ? tile->furniture->id : "" ) );
                if( tile->furniture ) {
                    hash_value( signature, static_cast<unsigned>( tile->furniture->shape ) );
                    hash_value( signature, furniture_facing( *tile ) );
                }
                hash_value( signature, climb_facing( *tile ) );
                hash_value( signature, std::hash<float> {}( tile->vegetation_height ) );
                hash_value( signature, static_cast<unsigned>( tile->kind ) );
                if( tile->kind == fp3d_snapshot::terrain_kind::window ) {
                    hash_value( signature, static_cast<unsigned>( tile->pane ) );
                    hash_value( signature, std::hash<std::string> {}( window_wall( *tile ) ) );
                }
                hash_value( signature, std::hash<int> {}( tile->ramp_delta ) );
                hash_value( signature, static_cast<unsigned>( tile->known ) );
                // A remembered lower storey gets a different luminance from
                // the same terrain viewed at its own level. Rebuild its list.
                hash_value( signature, tile->position.z() < scene.origin.z() );
                hash_value( signature, tile->floor_present | tile->ceiling_present << 1 |
                            tile->goes_up << 2 | tile->goes_down << 3 | tile->is_ramp << 4 );
                hash_value( signature, vehicle_roof_ghosts.count( tile->position ) );
                const auto *above = known_cell( tile->position + tripoint_rel_ms( 0, 0, 1 ) );
                const auto *below = known_cell( tile->position + tripoint_rel_ms( 0, 0, -1 ) );
                hash_value( signature, above ? 1 | above->floor_present << 1 | above->goes_down << 2 |
                            above->is_ramp << 3 : 0 );
                hash_value( signature, below ? 1 | below->goes_up << 1 : 0 );
                for( const auto &direction : {
                         tripoint_rel_ms::east, tripoint_rel_ms::west,
                         tripoint_rel_ms::north, tripoint_rel_ms::south
                     } ) {
                    hash_value( signature, known_wall( tile->position + direction ) );
                }
            }
            const bool rebuild = !mesh.list || mesh.signature != signature;
            if( rebuild ) {
                if( !mesh.list ) {
                    mesh.list = glGenLists( 1 );
                }
                if( mesh.list ) {
                    glNewList( mesh.list, GL_COMPILE );
                }
                if( !mesh.list ) {
                    glPushMatrix();
                    glTranslatef( offset.x(), y, offset.y() );
                }
                for( const auto *tile : chunk.tiles ) {
                    const auto local = tile->position - key;
                    const int ew = known_wall( tile->position + tripoint_rel_ms::east ) +
                                   known_wall( tile->position + tripoint_rel_ms::west );
                    const int ns = known_wall( tile->position + tripoint_rel_ms::north ) +
                                   known_wall( tile->position + tripoint_rel_ms::south );
                    draw_cell( *tile, local.x(), local.y(),
                               known_cell( tile->position + tripoint_rel_ms( 0, 0, -1 ) ),
                               known_cell( tile->position + tripoint_rel_ms( 0, 0, 1 ) ), ew, ns,
                               tile->kind == fp3d_snapshot::terrain_kind::window ? window_wall( *tile ) : "",
                               nullptr, true, furniture_facing( *tile ), fence_links( *tile ),
                               climb_facing( *tile ),
                               vehicle_roof_ghosts.count( tile->position ) != 0,
                               tile->known == fp3d_snapshot::knowledge::remembered &&
                               tile->position.z() < scene.origin.z() );
                }
                if( mesh.list ) {
                    glEndList();
                } else {
                    glPopMatrix();
                }
                mesh.signature = signature;
                ++last_statistics.rebuilt;
            }
            mesh.generation = scene.render_generation;
        }
        glPushMatrix();
        glTranslatef( offset.x(), y, offset.y() );
        if( mesh.list ) {
            glCallList( mesh.list );
        }
        glPopMatrix();
    }
    // P1-V: every native-visible car, parked wreck, custom frame and NPC
    // vehicle takes the same rigid renderer. Opaque terrain has written
    // depth, vehicles are before actor sprites and transparent glass.
    fp3d_vehicle::draw( scene, view, camera_offset );
    // Each cabinet exposes only its own contents. Draw back to front, writing
    // the cabinet depth before continuing to the next cabinet or the actors.
    std::vector<const fp3d_snapshot::cell *> visible_storage;
    for( const auto *tile : storage_cells ) {
        const auto offset = tile->position - scene.origin;
        if( view.contains( offset.x() - camera_offset.x,
                           offset.z() * fp3d_camera::floor_height + .9f - fp3d_camera::eye_height,
                           offset.y() - camera_offset.y, 1.4f ) ) {
            visible_storage.push_back( tile );
        }
    }
    const auto storage_distance = [&]( const fp3d_snapshot::cell *tile ) {
        const auto offset = tile->position - scene.origin;
        const float x = offset.x() - camera_offset.x;
        const float z = offset.y() - camera_offset.y;
        const float y = offset.z() * fp3d_camera::floor_height + .9f - fp3d_camera::eye_height;
        return x * x + y * y + z * z;
    };
    std::sort( visible_storage.begin(), visible_storage.end(), [&]( const auto *a, const auto *b ) {
        return storage_distance( a ) > storage_distance( b );
    } );
    const auto draw_storage = [&]( const fp3d_snapshot::cell &tile ) {
        const auto &model = *tile.furniture;
        const auto key = model.id + ":" + std::to_string( static_cast<unsigned>( model.shape ) );
        auto it = storage_meshes.find( key );
        if( it == storage_meshes.end() ) {
            const GLuint list = glGenLists( 1 );
            if( list ) {
                glNewList( list, GL_COMPILE );
                furniture_geometry( model, 0, 0 );
                glEndList();
                it = storage_meshes.emplace( key, list ).first;
            }
        }
        const float brightness = tile.known == fp3d_snapshot::knowledge::remembered ?
                                 ( tile.position.z() < scene.origin.z() ? .60f : .23f ) : .9f;
        glColor3f( brightness, brightness, brightness );
        const auto offset = tile.position - scene.origin;
        glPushMatrix();
        glTranslatef( offset.x(), offset.z() * fp3d_camera::floor_height, offset.y() );
        glRotatef( -90.0f * furniture_facing( tile ), 0, 1, 0 );
        if( it != storage_meshes.end() ) {
            glCallList( it->second );
        } else {
            furniture_geometry( model, 0, 0 );
        }
        glPopMatrix();
    };
    glAlphaFunc( GL_GREATER, .5f );
    const float right_x = view.right_x();
    const float right_z = view.right_z();
    const auto draw_sprite = [&]( const fp3d_snapshot::sprite &image ) {
        const tripoint_rel_ms position = image.position - scene.origin;
        if( !view.contains( position.x() - camera_offset.x,
                            position.z() * fp3d_camera::floor_height + image.elevation +
                            image.height / 2 - fp3d_camera::eye_height,
                            position.y() - camera_offset.y, image.height * .74f ) ) {
            return;
        }
        const auto material = fp3d_materials::lookup( fp3d_materials::sprite_key( image ) );
        if( material ) {
            glEnable( GL_TEXTURE_2D );
            glColor3f( .9f, .9f, .9f );
        } else {
            glDisable( GL_TEXTURE_2D );
            glColor3f( .8f, .25f, .7f );
        }
        auto uv = material.value_or( fp3d_materials::uv_rect{ 0, 0, 1, 1 } );
        if( image.mirrored ) {
            std::swap( uv.left, uv.right );
        }
        const tripoint_rel_ms offset = image.position - scene.origin;
        // Atlas slots are square and already preserve source proportions,
        // including non-square source images. Keep the quad square as well.
        const float half_width = image.height * .5f;
        const float dx = right_x * half_width;
        const float dz = right_z * half_width;
        const float x = offset.x();
        const float z = offset.y();
        glPushMatrix();
        glTranslatef( 0, offset.z() * fp3d_camera::floor_height + image.elevation, 0 );
        const auto quad = [&]( float sideways, float up ) {
            // The billboard is visual only; shade it with the ground's
            // upward normal so changing camera yaw doesn't flicker its
            // illumination. GL_LIGHTING is enabled by the world-sprite
            // passes below, not for the first-person held item.
            glNormal3f( 0, 1, 0 );
            const float cx = x + right_x * sideways;
            const float cz = z + right_z * sideways;
            glBegin( GL_QUADS );
            glTexCoord2f( uv.left, uv.bottom );
            glVertex3f( cx - dx, .04f + up, cz - dz );
            glTexCoord2f( uv.right, uv.bottom );
            glVertex3f( cx + dx, .04f + up, cz + dz );
            glTexCoord2f( uv.right, uv.top );
            glVertex3f( cx + dx, image.height + up, cz + dz );
            glTexCoord2f( uv.left, uv.top );
            glVertex3f( cx - dx, image.height + up, cz - dz );
            glEnd();
        };
        // Dropped sleeping bags and bedding on a mattress must lie FLAT on
        // it. In the old scene every item was a vertical 0.7m billboard,
        // making red/pink bedding appear to float above beds.
        const auto *support = known_cell( image.position );
        if( image.kind == fp3d_snapshot::sprite_kind::item && support &&
            support->furniture &&
            ( support->furniture->shape == fp3d_furniture::kind::bed ||
              support->furniture->shape == fp3d_furniture::kind::bed_frame ) ) {
            constexpr float half_w = .34f;
            constexpr float half_d = .35f;
            const float height = .045f;
            glBegin( GL_QUADS );
            glNormal3f( 0, 1, 0 );
            glTexCoord2f( uv.left, uv.top );
            glVertex3f( x - half_w, height, z - half_d );
            glTexCoord2f( uv.left, uv.bottom );
            glVertex3f( x - half_w, height, z + half_d );
            glTexCoord2f( uv.right, uv.bottom );
            glVertex3f( x + half_w, height, z + half_d );
            glTexCoord2f( uv.right, uv.top );
            glVertex3f( x + half_w, height, z - half_d );
            glEnd();
            glPopMatrix();
            return;
        }
        using kind = fp3d_targeting::kind;
        const auto target_kind = image.kind == fp3d_snapshot::sprite_kind::item ? kind::item : kind::creature;
        if( aim && image.kind != fp3d_snapshot::sprite_kind::held_item &&
            aim->type == target_kind && aim->position == image.position && aim->id == image.id ) {
            GLint viewport[4];
            glGetIntegerv( GL_VIEWPORT, viewport );
            const float distance = right_z * ( x - camera_offset.x ) - right_x * ( z - camera_offset.y );
            const float edge = 3 * fp3d_camera::horizontal_tangent() * distance / std::max( 1, viewport[2] );
            // Selection edges are UI feedback rather than emissive world
            // geometry. Keep them visible even when a selected item is dark.
            glDisable( GL_LIGHTING );
            glColor3f( 1, 1, 1 );
            glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );
            glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE );
            glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR );
            glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
            glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
            glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE );
            glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
            glDepthMask( GL_FALSE );
            quad( -edge, 0 );
            quad( edge, 0 );
            quad( 0, -edge );
            quad( 0, edge );
            glDepthMask( GL_TRUE );
            glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
            glEnable( GL_LIGHTING );
            if( material ) {
                glColor3f( .9f, .9f, .9f );
            } else {
                glColor3f( .8f, .25f, .7f );
            }
        }
        quad( 0, 0 );
        glPopMatrix();
    };
    const auto storage_item = [&]( const fp3d_snapshot::sprite &image ) {
        const auto *owner = known_cell( image.position );
        return image.kind == fp3d_snapshot::sprite_kind::item && owner && owner->furniture &&
               owner->known == fp3d_snapshot::knowledge::visible && !owner->exterior_only &&
               fp3d_furniture::shows_contents( owner->furniture->shape );
    };
    std::unordered_map<tripoint_abs_ms, const fp3d_snapshot::sprite *> contents;
    for( const auto &image : scene.sprites ) {
        if( storage_item( image ) ) {
            contents.emplace( image.position, &image );
        }
    }
    glEnable( GL_CULL_FACE );
    glFrontFace( GL_CCW );
    glCullFace( GL_BACK );
    glEnable( GL_LIGHTING );
    glDisable( GL_ALPHA_TEST );
    for( const auto *tile : visible_storage ) {
        const auto item = contents.find( tile->position );
        if( item == contents.end() ) {
            draw_storage( *tile );
            continue;
        }
        glDepthMask( GL_FALSE );
        draw_storage( *tile );
        glDepthMask( GL_TRUE );
        // Render contained world items as lit geometry. Before this change
        // these used constant .9 texture colour even in a blacked-out room.
        glDisable( GL_CULL_FACE );
        glEnable( GL_ALPHA_TEST );
        draw_sprite( *item->second );
        glDisable( GL_ALPHA_TEST );
        glEnable( GL_CULL_FACE );
        glEnable( GL_LIGHTING );
        glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
        draw_storage( *tile );
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
    }
    glDisable( GL_CULL_FACE );
    // Native-visible world actors and dropped items share the same colour
    // material/GL_LIGHT0 as the 3D floor and objects. This fixes extremely
    // bright dropped sprites against nearly black streets at night. The
    // first-person held item is drawn separately and remains unaffected.
    glEnable( GL_LIGHTING );
    glEnable( GL_ALPHA_TEST );
    // Actors and ordinary ground items still cannot be seen through cabinets.
    for( const auto &image : scene.sprites ) {
        if( !storage_item( image ) ) {
            draw_sprite( image );
        }
    }
    glDisable( GL_LIGHTING );
    // Animated signs are separate from cached stair meshes and native targets.
    // Use presentation time so the arrows move while the turn-based world is idle.
    const auto animation_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch() ).count();
    const float bob = .09f * std::sin( ( animation_ms % 1800 ) * 6.2831853f / 1800 );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_ALPHA_TEST );
    for( const auto *tile : stair_cells ) {
        const bool up = tile->goes_up || tile->kind == fp3d_snapshot::terrain_kind::downspout;
        const bool down = tile->goes_down;
        const auto position = tile->position - scene.origin;
        const auto section = tile->stairs.value_or( fp3d_snapshot::stair_section{} );
        const float y = position.z() * fp3d_camera::floor_height + 1.45f + bob;
        const float center_x = position.x() + ( section.width - 1 ) / 2.0f;
        const float front_z = position.y() + section.rows - 1 + .18f;
        if( !view.contains( center_x - camera_offset.x,
                            y - fp3d_camera::eye_height, front_z - camera_offset.y, .6f ) ) {
            continue;
        }
        for( const int direction : { -1, 1 } ) {
            if( direction < 0 ? !down : !up ) {
                continue;
            }
            const float separation = up && down ? direction * .19f : 0;
            const float x = center_x + separation * right_x;
            const float z = front_z + separation * right_z;
            glColor3f( 1.0f, .78f, .25f );
            box( x, z, .08f, .08f, y - .17f, y + .17f, std::nullopt );
            glBegin( GL_TRIANGLES );
            const std::array<std::array<float, 2>, 4> corners{{
                {{ -.17f, -.17f }}, {{ .17f, -.17f }},
                {{ .17f, .17f }}, {{ -.17f, .17f }}
            }};
            for( size_t i = 0; i < corners.size(); ++i ) {
                const auto &a = corners[i];
                const auto &b = corners[( i + 1 ) % corners.size()];
                glVertex3f( x, y + direction * .39f, z );
                glVertex3f( x + a[0], y + direction * .12f, z + a[1] );
                glVertex3f( x + b[0], y + direction * .12f, z + b[1] );
            }
            glEnd();
        }
    }
    glEnable( GL_ALPHA_TEST );
    fp3d_weather::draw( scene.weather, scene.origin, yaw, scene.sky.daylight, weather_ms );
    fp3d_vehicle::draw_glass( scene, view, camera_offset );
    // Glass is drawn after all opaque geometry and actors, back to front.
    // Depth testing keeps nearer walls/actors opaque; glass never writes depth.
    std::vector<const fp3d_snapshot::cell *> glass;
    for( const auto &tile : scene.cells ) {
        if( tile.kind == fp3d_snapshot::terrain_kind::glass ||
            ( tile.kind == fp3d_snapshot::terrain_kind::window &&
              tile.pane == fp3d_snapshot::window_pane::glazed ) ) {
            glass.push_back( &tile );
        }
    }
    const auto distance = [&]( const fp3d_snapshot::cell * tile ) {
        const auto offset = tile->position - scene.origin;
        const float x = offset.x() - camera_offset.x;
        const float y = offset.z() * fp3d_camera::floor_height + 1.5f - fp3d_camera::eye_height;
        const float z = offset.y() - camera_offset.y;
        return x * x + y * y + z * z;
    };
    std::sort( glass.begin(), glass.end(), [&]( const auto * a, const auto * b ) {
        return distance( a ) > distance( b );
    } );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glDepthMask( GL_FALSE );
    glAlphaFunc( GL_GREATER, .01f );
    for( const auto *tile : glass ) {
        const auto offset = tile->position - scene.origin;
        const float y = offset.z() * fp3d_camera::floor_height;
        if( !view.contains( offset.x() - camera_offset.x, y + 1.5f - fp3d_camera::eye_height,
                            offset.y() - camera_offset.y, .8f ) ) {
            continue;
        }
        const int ew = known_wall( tile->position + tripoint_rel_ms::east ) +
                       known_wall( tile->position + tripoint_rel_ms::west );
        const int ns = known_wall( tile->position + tripoint_rel_ms::north ) +
                       known_wall( tile->position + tripoint_rel_ms::south );
        const bool memory = tile->known == fp3d_snapshot::knowledge::remembered;
        const float brightness = memory ?
                                 ( tile->position.z() < scene.origin.z() ? .60f : .23f ) : .9f;
        const bool full_glass = tile->kind == fp3d_snapshot::terrain_kind::glass;
        if( full_glass ) {
            const float opacity = tile->terrain.find( "frosted" ) != std::string::npos ? .60f : .32f;
            glColor4f( brightness * .66f, brightness * .89f, brightness, opacity );
        } else {
            glColor4f( brightness, brightness, brightness, .32f );
        }
        glPushMatrix();
        glTranslatef( offset.x(), y, offset.y() );
        if( ew < ns ) {
            glRotatef( 90, 0, 1, 0 );
        }
        if( full_glass ) {
            window_face( std::nullopt, .44f, .06f, fp3d_camera::floor_height - .08f );
        } else {
            window_face( fp3d_materials::lookup( "$window:" + tile->terrain ) );
        }
        glPopMatrix();
    }
    if( preview ) {
        // A presentation-only ghost; depth testing still occludes it behind walls.
        const auto offset = preview->position - scene.origin;
        glDisable( GL_LIGHTING );
        glDisable( GL_ALPHA_TEST );
        const std::array<float, 4> tint{ preview->valid ? .15f : .95f,
                                       preview->valid ? .9f : .15f, .3f, .35f };
        glColor4fv( tint.data() );
        const float y = offset.z() * fp3d_camera::floor_height;
        if( preview->terrain ) {
            const auto &tile = *preview->terrain;
            const int ew = known_wall( tile.position + tripoint_rel_ms::east ) +
                           known_wall( tile.position + tripoint_rel_ms::west );
            const int ns = known_wall( tile.position + tripoint_rel_ms::north ) +
                           known_wall( tile.position + tripoint_rel_ms::south );
            glPushMatrix();
            glTranslatef( 0, y + .025f, 0 );
            draw_cell( tile, offset.x(), offset.y(), nullptr, nullptr, ew, ns, window_wall( tile ),
                       &tint, false, furniture_facing( tile ), {}, climb_facing( tile ) );
            const bool full_glass = tile.kind == fp3d_snapshot::terrain_kind::glass;
            if( full_glass || ( tile.kind == fp3d_snapshot::terrain_kind::window &&
                               tile.pane == fp3d_snapshot::window_pane::glazed ) ) {
                glDisable( GL_LIGHTING );
                glPushMatrix();
                glTranslatef( offset.x(), 0, offset.y() );
                if( ew < ns ) {
                    glRotatef( 90, 0, 1, 0 );
                }
                if( full_glass ) {
                    window_face( std::nullopt, .44f, .06f, fp3d_camera::floor_height - .08f );
                } else {
                    window_face( fp3d_materials::lookup( "$window:" + tile.terrain ) );
                }
                glPopMatrix();
            }
            glPopMatrix();
        } else if( preview->vertical_hint != 0 ) {
            // Keep all of this planning symbol above the real floor. It must
            // not masquerade as a completed opening or reveal hidden geometry.
            const float x = offset.x();
            const float z = offset.y();
            for( const float side : { -.45f, .45f } ) {
                box( x + side, z, .04f, .94f, y + .03f, y + .06f, std::nullopt );
                box( x, z + side, .94f, .04f, y + .03f, y + .06f, std::nullopt );
            }
            const bool down = preview->vertical_hint < 0;
            const float tip = y + ( down ? .18f : 1.15f );
            const float base = y + ( down ? .5f : .83f );
            box( x, z, .09f, .09f, y + ( down ? .5f : .18f ),
                 y + ( down ? 1.15f : .83f ), std::nullopt );
            glBegin( GL_TRIANGLES );
            const std::array<std::array<float, 2>, 4> corners{{
                {{ -.24f, -.24f }}, {{ .24f, -.24f }},
                {{ .24f, .24f }}, {{ -.24f, .24f }}
            }};
            for( size_t i = 0; i < corners.size(); ++i ) {
                const auto &a = corners[i];
                const auto &b = corners[( i + 1 ) % corners.size()];
                glVertex3f( x, tip, z );
                glVertex3f( x + a[0], base, z + a[1] );
                glVertex3f( x + b[0], base, z + b[1] );
            }
            glEnd();
        } else {
            box( offset.x(), offset.y(), .94f, .94f, y + .03f, y + preview->height, std::nullopt );
        }
    }
    glDisable( GL_BLEND );
    glDepthMask( GL_TRUE );
    glDisable( GL_ALPHA_TEST );
    glDisable( GL_COLOR_MATERIAL );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_LIGHTING );
    if( aim && aim->outline ) {
        const auto &outline = *aim->outline;
        const auto vertex = [&]( const std::array<float, 3> &corner, float height ) {
            const float x = corner[0] - camera_offset.x;
            const float y = corner[1] + height - camera_offset.height;
            const float z = corner[2] - camera_offset.y;
            // Move a few millimetres along the view ray to clear coplanar depth
            // without expanding the outline or drawing through other surfaces.
            const float bias = .006f / std::max( .05f, std::hypot( x, y, z ) );
            glVertex3f( corner[0] - x * bias, corner[1] + height - y * bias, corner[2] - z * bias );
        };
        glDepthMask( GL_FALSE );
        glLineWidth( 2 );
        glColor3f( 1, 1, 1 );
        glBegin( GL_LINES );
        for( size_t i = 0; i < outline.corners.size(); ++i ) {
            const auto &corner = outline.corners[i];
            const auto &next = outline.corners[( i + 1 ) % outline.corners.size()];
            vertex( corner, 0 );
            vertex( next, 0 );
            if( outline.height > 0 ) {
                vertex( corner, outline.height );
                vertex( next, outline.height );
                vertex( corner, 0 );
                vertex( corner, outline.height );
            }
        }
        glEnd();
        glLineWidth( 1 );
        glDepthMask( GL_TRUE );
    }
    glDisable( GL_FOG );
    glDisable( GL_DEPTH_TEST );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glOrtho( -aspect, aspect, -1, 1, -1, 1 );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    if( scene.held_item ) {
        if( const auto material = fp3d_materials::lookup(
                                      fp3d_materials::sprite_key( *scene.held_item ) ) ) {
            glEnable( GL_TEXTURE_2D );
            glEnable( GL_ALPHA_TEST );
            glAlphaFunc( GL_GREATER, .5f );
            glColor3f( 1, 1, 1 );
            glPushMatrix();
            const float strike = std::clamp( swing, 0.0f, 1.0f );
            glTranslatef( aspect * ( .62f - .32f * strike ), -.72f + .5f * strike, 0 );
            glRotatef( -20 - 65 * strike, 0, 0, 1 );
            glScalef( 1 - .22f * strike, 1 - .22f * strike, 1 );
            // Square atlas coordinates preserve the pack's source proportions.
            glBegin( GL_QUADS );
            glTexCoord2f( material->right, material->bottom );
            glVertex2f( -.525f, -.525f );
            glTexCoord2f( material->left, material->bottom );
            glVertex2f( .525f, -.525f );
            glTexCoord2f( material->left, material->top );
            glVertex2f( .525f, .525f );
            glTexCoord2f( material->right, material->top );
            glVertex2f( -.525f, .525f );
            glEnd();
            glPopMatrix();
            glDisable( GL_ALPHA_TEST );
            glDisable( GL_TEXTURE_2D );
        }
    }
    if( interactable ) {
        glColor3f( 1, .75f, .3f );
        glBegin( GL_LINE_LOOP );
        glVertex2f( 0, -.018f );
        glVertex2f( .018f, 0 );
        glVertex2f( 0, .018f );
        glVertex2f( -.018f, 0 );
        glEnd();
    } else {
        glColor3f( .8f, .8f, .8f );
        glBegin( GL_LINES );
        glVertex2f( -.012f, 0 );
        glVertex2f( .012f, 0 );
        glVertex2f( 0, -.012f );
        glVertex2f( 0, .012f );
        glEnd();
    }
    last_statistics.cpu_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - started ).count();
}
} // namespace fp3d_scene
#endif
