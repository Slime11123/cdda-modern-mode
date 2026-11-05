#include "fp3d_scene.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>
#include <unordered_map>

#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

#include "fp3d_snapshot.h"
#include "fp3d_materials.h"

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

void furniture_geometry( const fp3d_furniture::model &model, float x, float z,
                         unsigned facing = 0 )
{
    glPushMatrix();
    glTranslatef( x, 0, z );
    glRotatef( -90.0f * facing, 0, 1, 0 );
    unsigned part_index = 0;
    for( const auto &part : fp3d_furniture::parts( model.shape ) ) {
        const auto surfaces = fp3d_materials::lookup_furniture( model.id, part_index++ );
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
    glPopMatrix();
}

void draw_cell( const fp3d_snapshot::cell &tile, float x, float z,
                const fp3d_snapshot::cell *below, const fp3d_snapshot::cell *above,
                int ew, int ns, const std::string &wall_surface,
                const std::array<float, 4> *tint = nullptr, bool defer_storage = false,
                unsigned furniture_facing = 0 )
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
    const bool grass = tile.vegetation.compare( 0, 7, "t_grass" ) == 0;
    const bool door = tile.kind == kind::closed_door || tile.kind == kind::open_door ||
                      tile.kind == kind::damaged_door || tile.kind == kind::glass;
    const auto material = fp3d_materials::lookup(
                              tile.kind == kind::window ? "t_floor" :
                              tile.vegetation == tile.terrain && !grass ? "t_dirt" : tile.terrain );
    const float brightness = memory ? .23f : .9f;
    color( memory ? .13f : .32f, memory ? .13f : .36f, memory ? .13f : .28f );
    if( material ) {
        color( brightness, brightness, brightness );
    }
    glPushMatrix();
    const bool stair_opening = tile.goes_down && below && below->goes_up;
    if( tile.floor_present && !tile.is_ramp && !stair_opening ) {
        const auto floor_material = door ? fp3d_materials::lookup( "t_floor" ) : material;
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
                glDisable( GL_LIGHTING );
                glDepthMask( GL_FALSE );
                color( brightness, brightness, brightness );
                floor( x, z, .006f, blood );
                glDepthMask( GL_TRUE );
                glDisable( GL_BLEND );
                glEnable( GL_LIGHTING );
            }
        }
    }
    const bool upper_floor_surface = above && above->floor_present && !above->is_ramp;
    if( tile.ceiling_present && !upper_floor_surface ) {
        // Neutral underside only: this is not an upstairs terrain cell.
        glPushMatrix();
        glTranslatef( x, fp3d_camera::floor_height - .08f, z );
        glRotatef( 180, 1, 0, 0 );
        floor( 0, 0, 0, fp3d_materials::lookup( "t_floor" ) );
        glPopMatrix();
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
    if( tile.goes_up && ( ( above && ( above->goes_down || !above->floor_present ) ) ||
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
            glDisable( GL_LIGHTING );
            glEnable( GL_ALPHA_TEST );
            glAlphaFunc( GL_GREATER, .5f );
            color( brightness, brightness, brightness );
            crossed_plant( x, z, tile.vegetation_height,
                           tile.vegetation.compare( 0, 7, "t_grass" ) == 0, *plant );
            glDisable( GL_ALPHA_TEST );
            glEnable( GL_LIGHTING );
        }
    }
    if( tile.furniture && ( !defer_storage ||
                           !fp3d_furniture::shows_contents( tile.furniture->shape ) ) ) {
        color( brightness, brightness, brightness );
        furniture_geometry( *tile.furniture, x, z, furniture_facing );
    }
    if( tile.kind == kind::wall || tile.kind == kind::obstacle ) {
        color( memory ? .23f : .58f, memory ? .23f : .60f, memory ? .23f : .64f );
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
        color( memory ? .23f : .62f, memory ? .23f : .36f, memory ? .23f : .15f );
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
    chunks.clear();
    landscape_chunks.clear();
    indexed_generation = 0;
}

draw_statistics statistics()
{
    return last_statistics;
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
    fp3d_sky::draw( scene.sky );
    glTranslatef( -camera_offset.x, -fp3d_camera::eye_height, -camera_offset.y );
    const GLfloat ambient[] = { .35f, .35f, .35f, 1 };
    const GLfloat diffuse[] = { .75f, .75f, .75f, 1 };
    const GLfloat direction[] = { -.4f, .8f, .5f, 0 };
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
                tile.terrain.find( "stairs" ) != std::string::npos && ( tile.goes_up || tile.goes_down ) &&
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
                    glColor3f( .65f, .65f, .65f );
                    if( patch->ground_present ) {
                        glPushMatrix();
                        glScalef( patch->width, 1, patch->depth );
                        floor( 0, 0, 0, fp3d_materials::lookup( patch->terrain ) );
                        glPopMatrix();
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
        return fp3d_furniture::facing( tile.furniture->shape,
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
                hash_value( signature, std::hash<std::string> {}( tile->vegetation ) );
                hash_value( signature, std::hash<std::string> {}( tile->blood ) );
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
                hash_value( signature, std::hash<float> {}( tile->vegetation_height ) );
                hash_value( signature, static_cast<unsigned>( tile->kind ) );
                if( tile->kind == fp3d_snapshot::terrain_kind::window ) {
                    hash_value( signature, static_cast<unsigned>( tile->pane ) );
                    hash_value( signature, std::hash<std::string> {}( window_wall( *tile ) ) );
                }
                hash_value( signature, std::hash<int> {}( tile->ramp_delta ) );
                hash_value( signature, static_cast<unsigned>( tile->known ) );
                hash_value( signature, tile->floor_present | tile->ceiling_present << 1 |
                            tile->goes_up << 2 | tile->goes_down << 3 | tile->is_ramp << 4 );
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
                               nullptr, true, furniture_facing( *tile ) );
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
        const float brightness = tile.known == fp3d_snapshot::knowledge::remembered ? .23f : .9f;
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
        using kind = fp3d_targeting::kind;
        const auto target_kind = image.kind == fp3d_snapshot::sprite_kind::item ? kind::item : kind::creature;
        if( aim && image.kind != fp3d_snapshot::sprite_kind::held_item &&
            aim->type == target_kind && aim->position == image.position && aim->id == image.id ) {
            GLint viewport[4];
            glGetIntegerv( GL_VIEWPORT, viewport );
            const float distance = right_z * ( x - camera_offset.x ) - right_x * ( z - camera_offset.y );
            const float edge = 3 * fp3d_camera::horizontal_tangent() * distance / std::max( 1, viewport[2] );
            // Preserve the tileset's alpha while painting white, then draw the
            // unshifted sprite over the interior. Opaque world depth still clips it.
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
        glDisable( GL_LIGHTING );
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
    glDisable( GL_LIGHTING );
    glEnable( GL_ALPHA_TEST );
    // Actors and ordinary ground items still cannot be seen through cabinets.
    for( const auto &image : scene.sprites ) {
        if( !storage_item( image ) ) {
            draw_sprite( image );
        }
    }
    // Animated signs are separate from cached stair meshes and native targets.
    // Use presentation time so the arrows move while the turn-based world is idle.
    const auto animation_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch() ).count();
    const float bob = .09f * std::sin( ( animation_ms % 1800 ) * 6.2831853f / 1800 );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_ALPHA_TEST );
    for( const auto *tile : stair_cells ) {
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
            if( direction < 0 ? !tile->goes_down : !tile->goes_up ) {
                continue;
            }
            const float separation = tile->goes_up && tile->goes_down ? direction * .19f : 0;
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
        const float brightness = memory ? .23f : .9f;
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
                       &tint, false, furniture_facing( tile ) );
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
            const float y = corner[1] + height - fp3d_camera::eye_height;
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
