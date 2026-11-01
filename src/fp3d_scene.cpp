#include "fp3d_scene.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <functional>
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
          const std::optional<fp3d_materials::uv_rect> &material )
{
    const float l = x - width / 2;
    const float r = x + width / 2;
    const float n = z - depth / 2;
    const float s = z + depth / 2;
    const auto uv = material.value_or( fp3d_materials::uv_rect{ 0, 0, 1, 1 } );
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
    glNormal3f( 0, 0, -1 );
    glTexCoord2f( uv.left, uv.bottom );
    glVertex3f( l, bottom, n );
    glTexCoord2f( uv.right, uv.bottom );
    glVertex3f( r, bottom, n );
    glTexCoord2f( uv.right, uv.top );
    glVertex3f( r, top, n );
    glTexCoord2f( uv.left, uv.top );
    glVertex3f( l, top, n );
    glNormal3f( 0, 0, 1 );
    glTexCoord2f( uv.left, uv.bottom );
    glVertex3f( r, bottom, s );
    glTexCoord2f( uv.right, uv.bottom );
    glVertex3f( l, bottom, s );
    glTexCoord2f( uv.right, uv.top );
    glVertex3f( l, top, s );
    glTexCoord2f( uv.left, uv.top );
    glVertex3f( r, top, s );
    glNormal3f( -1, 0, 0 );
    glTexCoord2f( uv.left, uv.bottom );
    glVertex3f( l, bottom, s );
    glTexCoord2f( uv.right, uv.bottom );
    glVertex3f( l, bottom, n );
    glTexCoord2f( uv.right, uv.top );
    glVertex3f( l, top, n );
    glTexCoord2f( uv.left, uv.top );
    glVertex3f( l, top, s );
    glNormal3f( 1, 0, 0 );
    glTexCoord2f( uv.left, uv.bottom );
    glVertex3f( r, bottom, n );
    glTexCoord2f( uv.right, uv.bottom );
    glVertex3f( r, bottom, s );
    glTexCoord2f( uv.right, uv.top );
    glVertex3f( r, top, s );
    glTexCoord2f( uv.left, uv.top );
    glVertex3f( r, top, n );
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

void window_face( const std::optional<fp3d_materials::uv_rect> &material )
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
    glVertex3f( -.42f, .9f, 0 );
    glTexCoord2f( uv.right, uv.bottom );
    glVertex3f( .42f, .9f, 0 );
    glTexCoord2f( uv.right, uv.top );
    glVertex3f( .42f, 2.1f, 0 );
    glTexCoord2f( uv.left, uv.top );
    glVertex3f( -.42f, 2.1f, 0 );
    glEnd();
}

void draw_cell( const fp3d_snapshot::cell &tile, float x, float z,
                const fp3d_snapshot::cell *below, const fp3d_snapshot::cell *above,
                int ew, int ns, const std::string &wall_surface,
                const std::array<float, 4> *tint = nullptr )
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
    if( tile.floor_present && !tile.is_ramp ) {
        // A matched stair pair has a half-tile opening; never remove an
        // unrelated upper floor just because a lower tile says GOES_UP.
        const float width = stair_opening ? .5f : 1.0f;
        const float center = x - ( stair_opening ? .25f : 0 );
        box( center, z, width, 1, -.08f, 0, material );
        glPushMatrix();
        glTranslatef( center, -.08f, z );
        glScalef( width, 1, 1 );
        glRotatef( 180, 1, 0, 0 );
        floor( 0, 0, 0, material );
        glPopMatrix();
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
    if( tile.goes_up && ( ( above && ( above->goes_down || !above->floor_present ) ) ||
                          ( !above && !tile.ceiling_present && !tile.exterior_only &&
                            tile.known == fp3d_snapshot::knowledge::visible ) ) ) {
        // Presentation steps; native stair movement/collision is untouched.
        for( int step = 0; step < 12; ++step ) {
            box( x + .25f, z + .5f - ( step + .5f ) / 12, .5f, 1.0f / 12,
                 0, fp3d_camera::floor_height * ( step + 1 ) / 12, material );
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
    if( tile.kind == kind::wall || tile.kind == kind::obstacle ) {
        color( memory ? .23f : .58f, memory ? .23f : .60f, memory ? .23f : .64f );
        if( material ) {
            color( brightness, brightness, brightness );
        }
        box( x, z, .98f, .98f, 0, tile.kind == kind::wall ? fp3d_camera::floor_height : 1.0f, material );
    } else if( tile.kind == kind::window ) {
        const auto surround = fp3d_materials::lookup( wall_surface );
        color( brightness, brightness, brightness );
        glPushMatrix();
        glTranslatef( x, 0, z );
        if( ew < ns ) {
            glRotatef( 90, 0, 1, 0 );
        }
        box( 0, 0, 1, .18f, 0, .9f, surround );
        box( 0, 0, 1, .18f, 2.1f, fp3d_camera::floor_height, surround );
        box( -.46f, 0, .08f, .22f, .9f, 2.1f, surround );
        box( .46f, 0, .08f, .22f, .9f, 2.1f, surround );
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
    } else if( tile.kind == kind::closed_door || tile.kind == kind::open_door ) {
        // Orientation uses only permitted snapshot neighbours, never the map.
        const bool along_x = ew >= ns;
        color( memory ? .23f : .62f, memory ? .23f : .36f, memory ? .23f : .15f );
        if( material ) {
            color( brightness, brightness, brightness );
        }
        if( tile.kind == kind::closed_door ) {
            box( x, z, along_x ? .98f : .16f, along_x ? .16f : .98f, 0, fp3d_camera::floor_height, material );
        } else {
            const float dx = along_x ? .44f : 0;
            const float dz = along_x ? 0 : .44f;
            box( x - dx, z - dz, .12f, .12f, 0, fp3d_camera::floor_height, material );
            box( x + dx, z + dz, .12f, .12f, 0, fp3d_camera::floor_height, material );
            box( x, z, along_x ? .98f : .16f, along_x ? .16f : .98f, 2.1f, fp3d_camera::floor_height,
                 material );

        }
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
           float aspect, fp3d_camera::offset camera_offset, const std::optional<blueprint> &preview, bool interactable )
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
        chunks.clear();
        landscape_chunks.clear();
        cells.reserve( scene.cells.size() );
        for( const auto &tile : scene.cells ) {
            cells.emplace( tile.position, &tile );
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
                         tile->kind == fp3d_snapshot::terrain_kind::window );
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
                               tile->kind == fp3d_snapshot::terrain_kind::window ? window_wall( *tile ) : "" );
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
    // Upright cutout billboards retain normal depth occlusion. Alpha testing
    // avoids ordering transparent rectangles or exposing actors through walls.
    glDisable( GL_LIGHTING );
    glEnable( GL_ALPHA_TEST );
    glAlphaFunc( GL_GREATER, .5f );
    const float right_x = view.right_x();
    const float right_z = view.right_z();
    for( const auto &image : scene.sprites ) {
        const tripoint_rel_ms position = image.position - scene.origin;
        if( !view.contains( position.x() - camera_offset.x,
                            position.z() * fp3d_camera::floor_height + image.height / 2 - fp3d_camera::eye_height,
                            position.y() - camera_offset.y, image.height * .74f ) ) {
            continue;
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
        glTranslatef( 0, offset.z() * fp3d_camera::floor_height, 0 );
        glBegin( GL_QUADS );
        glTexCoord2f( uv.left, uv.bottom );
        glVertex3f( x - dx, .04f, z - dz );
        glTexCoord2f( uv.right, uv.bottom );
        glVertex3f( x + dx, .04f, z + dz );
        glTexCoord2f( uv.right, uv.top );
        glVertex3f( x + dx, image.height, z + dz );
        glTexCoord2f( uv.left, uv.top );
        glVertex3f( x - dx, image.height, z - dz );
        glEnd();
        glPopMatrix();
    }
    // Glass is drawn after all opaque geometry and actors, back to front.
    // Depth testing keeps nearer walls/actors opaque; glass never writes depth.
    std::vector<const fp3d_snapshot::cell *> glass;
    for( const auto &tile : scene.cells ) {
        if( tile.kind == fp3d_snapshot::terrain_kind::window &&
            tile.pane == fp3d_snapshot::window_pane::glazed ) {
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
        glColor4f( brightness, brightness, brightness, .32f );
        glPushMatrix();
        glTranslatef( offset.x(), y, offset.y() );
        if( ew < ns ) {
            glRotatef( 90, 0, 1, 0 );
        }
        window_face( fp3d_materials::lookup( "$window:" + tile->terrain ) );
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
            draw_cell( tile, offset.x(), offset.y(), nullptr, nullptr, ew, ns, window_wall( tile ), &tint );
            if( tile.kind == fp3d_snapshot::terrain_kind::window &&
                tile.pane == fp3d_snapshot::window_pane::glazed ) {
                glDisable( GL_LIGHTING );
                glPushMatrix();
                glTranslatef( offset.x(), 0, offset.y() );
                if( ew < ns ) {
                    glRotatef( 90, 0, 1, 0 );
                }
                window_face( fp3d_materials::lookup( "$window:" + tile.terrain ) );
                glPopMatrix();
            }
            glPopMatrix();
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
            glTranslatef( aspect * .62f, -.72f, 0 );
            glRotatef( -20, 0, 0, 1 );
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
