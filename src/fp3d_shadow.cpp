#include "fp3d_shadow.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "fp3d_furniture.h"
#include "fp3d_materials.h"
#include "fp3d_camera.h"
#include "fp3d_snapshot.h"
#include "fp3d_scene.h"
#include "fp3d_regression.h"
#include "debug.h"
#include "sdl_wrappers.h"

namespace fp3d_shadow
{
namespace
{
GLuint shadow_fbo = 0;
GLuint shadow_texture = 0;
int target_size = 0;
GLuint far_shadow_fbo = 0;
GLuint far_shadow_texture = 0;
struct local_target {
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint occluders = 0;
    unsigned geometry_generation = 0;
    unsigned render_generation = 0;
    tripoint_abs_ms origin;
    tripoint_abs_ms geometry_origin;
    std::array<float, 4> position = {};
    float yaw = 0;
    float pitch = 0;
    bool failed = false;
    bool had_vehicles = false;
};
local_target beam_target;
local_target point_target;
int far_target_size = 0;
bool loaded = false;
bool unavailable = false;
unsigned cached_generation = 0;
int cached_quality = -1;
data cached_shadow;
GLuint occluder_list = 0;
unsigned occluder_generation = 0;
tripoint_abs_ms occluder_origin;
bool occluder_no_props = false;
bool occluder_only_props = false;
bool occluder_had_vehicles = false;
data occluder_statistics;
PFNGLGENFRAMEBUFFERSPROC gen_framebuffer = nullptr;
PFNGLBINDFRAMEBUFFERPROC bind_framebuffer = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC attach_texture = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC check_framebuffer = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC delete_framebuffer = nullptr;
PFNGLUSEPROGRAMPROC use_program = nullptr;

template<typename T> bool load( T &fn, const char *name )
{
    fn = reinterpret_cast<T>( SDL_GL_GetProcAddress( name ) );
    return fn != nullptr;
}

bool load_procs()
{
    if( loaded ) { return true; }
    if( unavailable ) { return false; }
    loaded = load( gen_framebuffer, "glGenFramebuffers" ) &&
             load( bind_framebuffer, "glBindFramebuffer" ) &&
             load( attach_texture, "glFramebufferTexture2D" ) &&
             load( check_framebuffer, "glCheckFramebufferStatus" ) &&
             load( delete_framebuffer, "glDeleteFramebuffers" ) &&
             load( use_program, "glUseProgram" );
    unavailable = !loaded;
    return loaded;
}

void clear_targets()
{
    if( shadow_fbo ) { delete_framebuffer( 1, &shadow_fbo ); }
    if( shadow_texture ) { glDeleteTextures( 1, &shadow_texture ); }
    if( far_shadow_fbo ) { delete_framebuffer( 1, &far_shadow_fbo ); }
    if( far_shadow_texture ) { glDeleteTextures( 1, &far_shadow_texture ); }
    shadow_fbo = shadow_texture = 0;
    far_shadow_fbo = far_shadow_texture = 0;
    far_target_size = 0;
    target_size = 0;
    cached_shadow = {};
    cached_generation = 0;
    cached_quality = -1;
}

bool ensure_target( int size )
{
    if( shadow_fbo && target_size == size ) { return true; }
    clear_targets();
    gen_framebuffer( 1, &shadow_fbo );
    bind_framebuffer( GL_FRAMEBUFFER, shadow_fbo );
    glGenTextures( 1, &shadow_texture );
    glBindTexture( GL_TEXTURE_2D, shadow_texture );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0,
                  GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    attach_texture( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_texture, 0 );
    glDrawBuffer( GL_NONE );
    glReadBuffer( GL_NONE );
    if( glGetError() != GL_NO_ERROR ||
        check_framebuffer( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
        clear_targets();
        unavailable = true;
        return false;
    }
    target_size = size;
    return true;
}

// A failed distant FBO must never damage the existing near depth target.
bool ensure_far_target( int size )
{
    if( far_shadow_fbo && far_target_size == size ) { return true; }
    if( far_shadow_fbo ) { delete_framebuffer( 1, &far_shadow_fbo ); }
    if( far_shadow_texture ) { glDeleteTextures( 1, &far_shadow_texture ); }
    far_shadow_fbo = far_shadow_texture = 0;
    far_target_size = 0;
    gen_framebuffer( 1, &far_shadow_fbo );
    bind_framebuffer( GL_FRAMEBUFFER, far_shadow_fbo );
    glGenTextures( 1, &far_shadow_texture );
    glBindTexture( GL_TEXTURE_2D, far_shadow_texture );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0,
                  GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    attach_texture( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                    far_shadow_texture, 0 );
    glDrawBuffer( GL_NONE );
    glReadBuffer( GL_NONE );
    if( glGetError() != GL_NO_ERROR ||
        check_framebuffer( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
        if( far_shadow_fbo ) { delete_framebuffer( 1, &far_shadow_fbo ); }
        if( far_shadow_texture ) { glDeleteTextures( 1, &far_shadow_texture ); }
        far_shadow_fbo = far_shadow_texture = 0;
        return false;
    }
    far_target_size = size;
    return true;
}

void box( float x, float z, float bottom, float top, float half = .5f, float half_z = -1.f )
{
    const float left = x - half, right = x + half;
    if( half_z < 0 ) { half_z = half; }
    const float north = z - half_z, south = z + half_z;
    glBegin( GL_QUADS );
    // Top
    glVertex3f( left, top, north ); glVertex3f( left, top, south );
    glVertex3f( right, top, south ); glVertex3f( right, top, north );
    // North and south
    glVertex3f( left, bottom, north ); glVertex3f( right, bottom, north );
    glVertex3f( right, top, north ); glVertex3f( left, top, north );
    glVertex3f( right, bottom, south ); glVertex3f( left, bottom, south );
    glVertex3f( left, top, south ); glVertex3f( right, top, south );
    // West and east
    glVertex3f( left, bottom, south ); glVertex3f( left, bottom, north );
    glVertex3f( left, top, north ); glVertex3f( left, top, south );
    glVertex3f( right, bottom, north ); glVertex3f( right, bottom, south );
    glVertex3f( right, top, south ); glVertex3f( right, top, north );
    glEnd();
}

// A sun-facing wall should not cast a shadow on its own visible facade.
// Render a contiguous solid wall run as one inset depth-only prism: the
// visible mesh has 0.98m tiles, while individual oversized shadow boxes
// produced one diagonal self-shadow wedge at EACH internal tile cap.
void wall_run( float x, float z, float bottom, float top,
               float half_x, float half_z )
{
    box( x, z, bottom, top, half_x, half_z );
}

// The native radio tower is an open red/grey steel lattice, NOT a solid
// one-metre obstruction. Its caster must match the thin beams in
// fp3d_scene::radio_tower_geometry; otherwise Ultra draws large black
// rectangles on every walkway and on the tower itself.
void radio_tower_shadow( float x, float z, float bottom, float top )
{
    constexpr float half = .34f;
    const float height = top - bottom;
    const auto beam = [&]( float x0, float y0, float z0,
                           float x1, float y1, float z1, float thickness ) {
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float dz = z1 - z0;
        const float horizontal = std::hypot( dx, dz );
        const float length = std::hypot( horizontal, dy );
        if( length < .0001f ) {
            return;
        }
        glPushMatrix();
        glTranslatef( x + ( x0 + x1 ) * .5f,
                      bottom + ( y0 + y1 ) * .5f,
                      z + ( z0 + z1 ) * .5f );
        if( horizontal > .0001f ) {
            glRotatef( std::atan2( horizontal, dy ) * 180.f / 3.14159265f,
                       dz, 0, -dx );
        }
        glScalef( thickness, length, thickness );
        box( 0, 0, -.5f, .5f );
        glPopMatrix();
    };
    for( const float sx : { -half, half } ) {
        for( const float sz : { -half, half } ) {
            beam( sx, 0, sz, sx, height, sz, .075f );
        }
    }
    for( int level = 0; level <= 2; ++level ) {
        const float y = height * level / 2.f;
        for( const float side : { -half, half } ) {
            beam( -half, y, side, half, y, side, .052f );
            beam( side, y, -half, side, y, half, .052f );
        }
    }
    for( int level = 0; level < 2; ++level ) {
        const float lo = height * level / 2.f;
        const float hi = height * ( level + 1 ) / 2.f;
        for( const float side : { -half, half } ) {
            beam( -half, lo, side, half, hi, side, .041f );
            beam( half, lo, side, -half, hi, side, .041f );
            beam( side, lo, -half, side, hi, half, .041f );
            beam( side, lo, half, side, hi, -half, .041f );
        }
    }
}

// Match the scene's 16-sided pole geometry instead of giving a thin lamp
// post a wide square shadow. Both box and cylinder read the SAME native
// furniture part data as the player's actual 3D scene.
void furniture_shadow_part( const fp3d_furniture::part &part )
{
    if( part.glass || part.width <= 0 || part.depth <= 0 ||
        part.top <= part.bottom ) {
        return;
    }
    if( !part.round ) {
        if( part.roll == 0 ) {
            box( part.x, part.z, part.bottom, part.top, part.width * .5f, part.depth * .5f );
            return;
        }
        glPushMatrix();
        const float mid = .5f * ( part.bottom + part.top );
        glTranslatef( part.x, mid, part.z );
        glRotatef( part.roll, 0, 0, 1 );
        glScalef( part.width, 1.0f, part.depth );
        box( 0, 0, part.bottom - mid, part.top - mid, .5f );
        glPopMatrix();
        return;
    }
    constexpr int sides = 16;
    constexpr float tau = 6.28318530718f;
    for( const bool top : { true, false } ) {
        glBegin( GL_TRIANGLE_FAN );
        const float y = top ? part.top : part.bottom;
        glVertex3f( part.x, y, part.z );
        for( int i = 0; i <= sides; ++i ) {
            const float a = tau * i / sides;
            glVertex3f( part.x + std::cos( a ) * part.width * .5f, y,
                        part.z + std::sin( a ) * part.depth * .5f );
        }
        glEnd();
    }
    glBegin( GL_QUADS );
    for( int i = 0; i < sides; ++i ) {
        const float a = tau * i / sides;
        const float b = tau * ( i + 1 ) / sides;
        const float xa = part.x + std::cos( a ) * part.width * .5f;
        const float za = part.z + std::sin( a ) * part.depth * .5f;
        const float xb = part.x + std::cos( b ) * part.width * .5f;
        const float zb = part.z + std::sin( b ) * part.depth * .5f;
        glVertex3f( xa, part.bottom, za ); glVertex3f( xb, part.bottom, zb );
        glVertex3f( xb, part.top, zb ); glVertex3f( xa, part.top, za );
    }
    glEnd();
}

void draw_known_occluders( const fp3d_snapshot::scene_snapshot &scene, float radius,
                           data &statistics, bool qa_suppress_props, bool qa_only_props,
                           const std::array<float, 4> *emitter = nullptr )
{
    // Door orientation follows the native scene's known wall neighbours.
    // Index once rather than scanning all tiles 4x for every damaged door.
    // These are already-permitted snapshot tiles, never hidden map queries.
    std::unordered_set<tripoint_abs_ms> known_walls;
    known_walls.reserve( scene.cells.size() / 3 + 1 );
    std::unordered_map<tripoint_abs_ms, const fp3d_snapshot::cell *> known_cells;
    known_cells.reserve( scene.cells.size() );
    for( const auto &known : scene.cells ) {
        if( known.known != fp3d_snapshot::knowledge::visible || known.exterior_only ) {
            continue;
        }
        known_cells.emplace( known.position, &known );
        if( known.kind == fp3d_snapshot::terrain_kind::wall ||
            known.kind == fp3d_snapshot::terrain_kind::glass ||
            known.kind == fp3d_snapshot::terrain_kind::window ) {
            known_walls.insert( known.position );
        }
    }
    const auto known_cell = [&]( const tripoint_abs_ms &pos ) {
        const auto it = known_cells.find( pos );
        return it == known_cells.end() ? nullptr : it->second;
    };
    const auto known_wall = [&]( const tripoint_abs_ms &pos ) {
        return known_walls.find( pos ) != known_walls.end();
    };
    const auto water_above = [&]( const tripoint_abs_ms &pos ) {
        const auto *above = known_cell( pos + tripoint_rel_ms::above );
        return above && !above->is_ramp ? fp3d_scene::water_surface_offset( above->terrain ) : 0.f;
    };
    const auto table_at = [&]( const tripoint_abs_ms &pos ) {
        const auto *c = known_cell( pos );
        return c && fp3d_furniture::chair_table( c->furniture );
    };
    const auto furniture_facing = [&]( const fp3d_snapshot::cell &tile ) -> unsigned {
        if( !tile.furniture ) { return 0u; }
        const auto shape = tile.furniture->shape;
        if( shape == fp3d_furniture::kind::chair ) {
            return fp3d_furniture::facing( shape, {},
                { table_at( tile.position + tripoint_rel_ms::north ),
                  table_at( tile.position + tripoint_rel_ms::east ),
                  table_at( tile.position + tripoint_rel_ms::south ),
                  table_at( tile.position + tripoint_rel_ms::west ) } );
        }
        const auto grouped = []( fp3d_furniture::kind k ) {
            return fp3d_furniture::shows_contents( k ) ||
                   k == fp3d_furniture::kind::counter ||
                   k == fp3d_furniture::kind::cabinet ||
                   k == fp3d_furniture::kind::desk ||
                   k == fp3d_furniture::kind::rack;
        };
        const auto linked = [&]( tripoint_rel_ms direction ) {
            const auto *other = known_cell( tile.position + direction );
            return other && other->furniture &&
                   ( other->furniture->shape == shape ||
                     ( grouped( shape ) && grouped( other->furniture->shape ) ) );
        };
        const bool row_ew = linked( tripoint_rel_ms::east ) ||
                            linked( tripoint_rel_ms::west );
        const bool row_ns = linked( tripoint_rel_ms::north ) ||
                            linked( tripoint_rel_ms::south );
        if( row_ew != row_ns ) {
            const unsigned a = row_ew ? 0u : 1u;
            const unsigned b = ( a + 2 ) % 4;
            const std::array<tripoint_rel_ms, 4> dirs{
                tripoint_rel_ms::north, tripoint_rel_ms::east,
                tripoint_rel_ms::south, tripoint_rel_ms::west };
            const bool wa = known_wall( tile.position + dirs[a] );
            const bool wb = known_wall( tile.position + dirs[b] );
            return wa != wb ? ( wa ? a : b ) : a;
        }
        return fp3d_furniture::facing( shape,
            { known_wall( tile.position + tripoint_rel_ms::north ),
              known_wall( tile.position + tripoint_rel_ms::east ),
              known_wall( tile.position + tripoint_rel_ms::south ),
              known_wall( tile.position + tripoint_rel_ms::west ) } );
    };
    // Merge opaque wall casters only within the actually visible snapshot.
    // No hidden map cells are queried, and windows/doors interrupt a run.
    std::unordered_set<tripoint_abs_ms> drawn_walls;
    drawn_walls.reserve( known_walls.size() );
    const auto castable_wall = [&]( const tripoint_abs_ms &pos ) {
        const auto *c = known_cell( pos );
        if( !c || c->kind != fp3d_snapshot::terrain_kind::wall ) {
            return false;
        }
        const auto delta = pos - scene.origin;
        return std::abs( delta.x() ) <= radius - 2.0f &&
               std::abs( delta.y() ) <= radius - 2.0f;
    };
    for( const auto &cell : scene.cells ) {
        if( cell.known != fp3d_snapshot::knowledge::visible || cell.exterior_only ) {
            continue;
        }
        const auto offset = cell.position - scene.origin;
        const float x = static_cast<float>( offset.x() );
        const float z = static_cast<float>( offset.y() );
        if( std::abs( x ) > radius - 2.0f || std::abs( z ) > radius - 2.0f ) {
            continue;
        }
        const float bottom = static_cast<float>( offset.z() ) * fp3d_camera::floor_height;
        const float top = bottom + fp3d_camera::floor_height;
        // The native 3D renderer still owns all textured scene meshes; shadow
        // occluders are conservative approximations, excluding foliage and
        // alpha blended windows to avoid camera-facing sprite silhouettes.
        if( !qa_only_props && cell.kind == fp3d_snapshot::terrain_kind::wall ) {
            if( drawn_walls.find( cell.position ) == drawn_walls.end() ) {
                const bool along_x =
                    castable_wall( cell.position + tripoint_rel_ms::east ) ||
                    castable_wall( cell.position + tripoint_rel_ms::west );
                const auto back = along_x ? tripoint_rel_ms::west : tripoint_rel_ms::north;
                const auto forward = along_x ? tripoint_rel_ms::east : tripoint_rel_ms::south;
                auto start = cell.position;
                const float surface_offset = water_above( cell.position );
                while( castable_wall( start + back ) &&
                       water_above( start + back ) == surface_offset &&
                       drawn_walls.find( start + back ) == drawn_walls.end() ) {
                    start += back;
                }
                int tiles = 0;
                auto end = start;
                while( castable_wall( end ) && water_above( end ) == surface_offset &&
                       drawn_walls.insert( end ).second ) {
                    ++tiles;
                    end += forward;
                }
                if( tiles > 0 ) {
                    const auto first = start - scene.origin;
                    const float run_x = static_cast<float>( first.x() ) +
                                        ( along_x ? .5f * ( tiles - 1 ) : 0.0f );
                    const float run_z = static_cast<float>( first.y() ) +
                                        ( along_x ? 0.0f : .5f * ( tiles - 1 ) );
                    // End caps stop at the visible model silhouette; the
                    // other axis is inset 12mm from the rendered face.
                    // Internal wall-cap faces no longer cast triangular
                    // shadows on adjacent wall tiles in oblique sunlight.
                    const float half_x = along_x ? tiles * .5f - .01f : .478f;
                    const float half_z = along_x ? .478f : tiles * .5f - .01f;
                    wall_run( run_x, run_z, bottom, top + surface_offset, half_x, half_z );
                }
            }
        } else if( !qa_only_props &&
                   ( cell.kind == fp3d_snapshot::terrain_kind::closed_door ||
                     cell.kind == fp3d_snapshot::terrain_kind::damaged_door ) ) {
            // A door is a thin .16-m panel, not a solid 1-m wall. Use the
            // identical fractured-door pieces as the main geometry so broken
            // openings transmit sunlight instead of making phantom shadows.
            const auto known_wall = [&]( const tripoint_abs_ms &p ) {
                return known_walls.find( p ) != known_walls.end();
            };
            const int ew = static_cast<int>( known_wall( cell.position + tripoint_rel_ms::east ) ) +
                           static_cast<int>( known_wall( cell.position + tripoint_rel_ms::west ) );
            const int ns = static_cast<int>( known_wall( cell.position + tripoint_rel_ms::north ) ) +
                           static_cast<int>( known_wall( cell.position + tripoint_rel_ms::south ) );
            glPushMatrix();
            glTranslatef( x, bottom, z );
            if( ew < ns ) { glRotatef( 90.0f, 0, 1, 0 ); }
            for( const auto &part : fp3d_snapshot::door_parts( cell.kind ) ) {
                glPushMatrix();
                glTranslatef( part.x, 0, part.z );
                glScalef( part.width, 1, part.depth );
                box( 0, 0, part.bottom, part.top, .5f );
                glPopMatrix();
            }
            glPopMatrix();
        } else if( !qa_only_props &&
                   ( cell.kind == fp3d_snapshot::terrain_kind::fence ||
                     fp3d_snapshot::is_chain_gate( cell.terrain ) ) ) {
            const auto connects = [&]( tripoint_rel_ms direction ) {
                const auto *other = known_cell( cell.position + direction );
                return other && fp3d_snapshot::fence_connects( cell.terrain, other->terrain );
            };
            const std::array<bool, 4> neighbours{
                connects( tripoint_rel_ms::north ), connects( tripoint_rel_ms::east ),
                connects( tripoint_rel_ms::south ), connects( tripoint_rel_ms::west ) };
            auto parts = fp3d_snapshot::terrain_model_parts( cell.terrain, neighbours );
            if( parts.empty() ) {
                parts = fp3d_snapshot::is_chain_gate( cell.terrain ) ?
                        fp3d_snapshot::chain_gate_parts( cell.terrain,
                                fp3d_snapshot::chain_gate_along_x( neighbours ) ) :
                        fp3d_snapshot::fence_parts( neighbours,
                                cell.terrain == "t_chainfence_posts" ||
                                cell.terrain == "t_chickenwire_fence_post" ||
                                cell.terrain == "t_fence_post" );
            }
            glPushMatrix();
            glTranslatef( x, bottom, z );
            for( const auto &part : parts ) {
                if( part.width > 0 && part.depth > 0 ) {
                    furniture_shadow_part( part );
                    continue;
                }
                // Native chain-link spans have zero thickness in the shared
                // part list. Rasterize their rails and open diamond wire, not
                // a solid panel or an empty shadow.
                const bool along_x = part.width > part.depth;
                const float width = along_x ? part.width : part.depth;
                furniture_shadow_part( { part.x, part.z, along_x ? width : .03f,
                                        along_x ? .03f : width, 1.86f, 1.915f } );
                furniture_shadow_part( { part.x, part.z, along_x ? width : .025f,
                                        along_x ? .025f : width, .11f, .145f } );
                glPushAttrib( GL_LINE_BIT );
                glLineWidth( 1.85f );
                glBegin( GL_LINES );
                const auto vertex = [&]( float u, float y ) {
                    glVertex3f( part.x + ( along_x ? u : 0.f ), y,
                                part.z + ( along_x ? 0.f : u ) );
                };
                for( int col = 0; col < 8; ++col ) {
                    for( int row = 0; row < 13; ++row ) {
                        const float a = width * ( col / 8.f - .5f );
                        const float b = width * ( ( col + 1 ) / 8.f - .5f );
                        const float lo = .13f + 1.70f * row / 13;
                        const float hi = .13f + 1.70f * ( row + 1 ) / 13;
                        vertex( a, lo ); vertex( b, hi );
                        vertex( a, hi ); vertex( b, lo );
                    }
                }
                glEnd();
                glPopAttrib();
            }
            glPopMatrix();
            ++statistics.fence_casters;
        } else if( !qa_only_props && cell.terrain == "t_radio_tower" ) {
            // Match the visible, transparent steel tower; NEVER cast an
            // opaque one-metre cube from its native obstacle category.
            radio_tower_shadow( x, z, bottom, top );
        } else if( !qa_only_props &&
                   cell.kind == fp3d_snapshot::terrain_kind::obstacle ) {
            // Other solid native obstacles retain their box shadow.
            box( x, z, bottom, bottom + 1.0f, .49f );
        }
        if( cell.furniture && !qa_suppress_props ) {
            ++statistics.furniture_casters;
            if( cell.furniture->shape == fp3d_furniture::kind::street_light ) {
                ++statistics.street_light_casters;
                if( cell.ceiling_present ) {
                    ++statistics.streetlights_with_ceiling;
                }
            }
            if( cell.furniture->shape == fp3d_furniture::kind::mailbox ) {
                ++statistics.mailbox_casters;
            }
            // Existing scene models for road lamps, mailboxes, indoor tables,
            // cabinets and poles were previously absent from the solar map.
            // Render the SAME opaque component dimensions and quarter-turn
            // facing as the real world; skip transparent display-case glass.
            glPushMatrix();
            glTranslatef( x, bottom, z );
            glRotatef( -90.0f * furniture_facing( cell ), 0, 1, 0 );
            const auto &parts = fp3d_furniture::parts( cell.furniture->shape );
            for( size_t i = 0; i < parts.size(); ++i ) {
                // Illuminated light diffuser is not an opaque lamp shell.
                if( ( cell.furniture->shape == fp3d_furniture::kind::street_light &&
                      i == 4 ) || parts[i].glass ) {
                    continue;
                }
                if( emitter ) {
                    const float angle = furniture_facing( cell ) * 1.5707963268f;
                    const float dx = ( *emitter )[0] - x, dz = ( *emitter )[2] - z;
                    const float local_x = std::cos( angle ) * dx + std::sin( angle ) * dz;
                    const float local_z = -std::sin( angle ) * dx + std::cos( angle ) * dz;
                    const auto &part = parts[i];
                    // Do not let an approximate lamp shell enclosing its own
                    // native emitter extinguish every outgoing light ray.
                    if( std::abs( local_x - part.x ) <= part.width * .5f + .02f &&
                        std::abs( local_z - part.z ) <= part.depth * .5f + .02f &&
                        ( *emitter )[1] >= bottom + part.bottom - .02f &&
                        ( *emitter )[1] <= bottom + part.top + .02f ) {
                        continue;
                    }
                }
                furniture_shadow_part( parts[i] );
            }
            glPopMatrix();
        }
        // A ceiling is only used when the game exposes that actual boundary.
        // The visible mesh sometimes suppresses a known phantom overhead
        // sheet above a car parked on a road. The shadow pass MUST use the
        // same mask, otherwise an invisible slab still casts dark footprints.
        // Do not invent occluding upper floors from unknown map cells.
        // Hiding a room's roof for the camera never opens it to the light.
        if( !qa_only_props && ( cell.ceiling_present || cell.cutaway_ceiling ) && !cell.open_air &&
            // Roof glass is a skylight: the sun must reach the room below
            // just as it reaches the street through a glass wall.
            !cell.glass_ceiling &&
            // A tower is open lattice and has NO full-width ceiling mesh.
            // Its steel members already cast their individual shadows.
            cell.terrain != "t_radio_tower" &&
            // Tree crowns have sparse branches rather than an opaque roof.
            // Never cast a full-tile shadow from an invisible canopy slab.
            cell.terrain.compare( 0, 9, "t_treetop" ) != 0 &&
            !( cell.vegetation == cell.terrain && cell.vegetation_height >= 4.0f ) &&
            !fp3d_scene::vehicle_roof_sheet_suppressed( cell.position,
                    scene.render_generation ) ) {
            const float ceiling_top = top + water_above( cell.position );
            glBegin( GL_QUADS );
            glVertex3f( x - .5f, ceiling_top, z - .5f );
            glVertex3f( x - .5f, ceiling_top, z + .5f );
            glVertex3f( x + .5f, ceiling_top, z + .5f );
            glVertex3f( x + .5f, ceiling_top, z - .5f );
            glEnd();
        }
    }
}

// Vehicle shadow casting is delegated to fp3d_vehicle::draw_shadow() so the
// same opaque mesh is rasterized for both visible cars and their sun shadows.
// Rebuilding separate per-part cuboids here would recreate roofs on wrecks,
// fill open doors and misalign the true raised cab/body silhouette.

void draw_creature_occluders( const fp3d_snapshot::scene_snapshot &scene,
                              float right_x, float right_z, float radius,
                              const std::array<float, 4> *emitter = nullptr, bool skip_avatar = false )
{
    // Dynamic, alpha-cut-out casters stay outside the static building lists.
    // Face the light rather than the camera, so mouse-look cannot move shadows.
    glPushAttrib( GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT );
    if( fp3d_materials::bind() ) {
        glEnable( GL_TEXTURE_2D );
        glEnable( GL_ALPHA_TEST );
        glAlphaFunc( GL_GREATER, .5f );
        glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
        glColor4f( 1, 1, 1, 1 );
        for( const auto &image : scene.sprites ) {
            if( image.kind != fp3d_snapshot::sprite_kind::monster &&
                image.kind != fp3d_snapshot::sprite_kind::character ) {
                continue;
            }
            // A held light must not be blocked by its owner's billboard.
            if( skip_avatar && image.is_avatar ) { continue; }
            const auto offset = image.position - scene.origin;
            if( std::hypot( offset.x(), offset.y() ) > radius + image.height ) { continue; }
            auto uv = fp3d_materials::lookup( fp3d_materials::sprite_key( image ) );
            if( !uv ) { continue; }
            if( image.mirrored ) { std::swap( uv->left, uv->right ); }
            float rx = right_x, rz = right_z;
            if( emitter ) {
                const float dx = ( *emitter )[0] - offset.x();
                const float dz = ( *emitter )[2] - offset.y();
                const float length = std::hypot( dx, dz );
                rx = length > .005f ? dz / length : 1.f;
                rz = length > .005f ? -dx / length : 0.f;
            }
            const float half_width = image.height * .5f;
            const float bottom = offset.z() * fp3d_camera::floor_height + image.elevation + .04f;
            const float top = bottom + image.height - .04f;
            glBegin( GL_QUADS );
            glTexCoord2f( uv->left, uv->bottom );
            glVertex3f( offset.x() - rx * half_width, bottom, offset.y() - rz * half_width );
            glTexCoord2f( uv->right, uv->bottom );
            glVertex3f( offset.x() + rx * half_width, bottom, offset.y() + rz * half_width );
            glTexCoord2f( uv->right, uv->top );
            glVertex3f( offset.x() + rx * half_width, top, offset.y() + rz * half_width );
            glTexCoord2f( uv->left, uv->top );
            glVertex3f( offset.x() - rx * half_width, top, offset.y() - rz * half_width );
            glEnd();
        }
    }
    glPopAttrib();
}

} // namespace

data render( const fp3d_snapshot::scene_snapshot &scene, int quality )
{
    data result;
    // Sunlight belongs to the calendar/weather and world geometry, not to
    // whether the avatar currently stands under a roof. Generate this map
    // inside buildings as well, so open doors/windows transmit the same sun
    // and outdoor geometry keeps casting shadows across the doorway.
    // Fully underground, unexposed worlds retain the old darkness safety gate.
    if( quality <= 0 || ( scene.origin.z() < 0 && !scene.sky.visible ) ||
        scene.sky.sun_y < .12f || scene.sky.daylight < .1f || !load_procs() ) {
        static bool reported_skip = false;
        if( !reported_skip ) {
            DebugLog( D_INFO, D_MAIN ) << "fp3d: shadow pass skipped by sun/time/underground: quality=" << quality
                                       << ", sky_visible=" << scene.sky.visible
                                       << ", outdoors=" << scene.weather.outdoors
                                       << ", sun_y=" << scene.sky.sun_y
                                       << ", daylight=" << scene.sky.daylight;
            reported_skip = true;
        }
        return result;
    }
    // World snapshots have a monotonic generation assigned at publication.
    // Don't rerender a 2048² depth map on every camera animation frame.
    // Doors, terrain and in-game sun angle are captured in that generation.
    if( cached_shadow.texture && cached_generation == scene.render_generation &&
        cached_quality == quality && shadow_texture == cached_shadow.texture &&
        ( !cached_shadow.far_texture || far_shadow_texture == cached_shadow.far_texture ) ) {
        return cached_shadow;
    }
    fp3d_regression::turn_probe profile( "shadow_setup" );
    const int desired_size = quality >= 3 ? 2048 : quality >= 2 ? 1536 : 1024;
    const float radius = quality >= 3 ? 38.0f : quality >= 2 ? 30.0f : 23.0f;
    constexpr float distance = 78.0f;
    const std::array<float, 3> sun = { scene.sky.sun_x, scene.sky.sun_y, scene.sky.sun_z };
    const float horizontal = std::hypot( sun[0], sun[2] );
    const std::array<float, 3> right = horizontal > .005f ?
                                       std::array<float, 3>{ sun[2] / horizontal, 0, -sun[0] / horizontal } :
                                       std::array<float, 3>{ 1, 0, 0 };
    const std::array<float, 3> up = {
        sun[1] * right[2],
        sun[2] * right[0] - sun[0] * right[2],
        -sun[1] * right[0]
    };
    GLint previous_fbo = 0, previous_viewport[4]{}, previous_program = 0;
    GLint previous_matrix_mode = 0;
    profile.stage( "shadow_state" );
    glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previous_fbo );
    glGetIntegerv( GL_VIEWPORT, previous_viewport );
    glGetIntegerv( GL_CURRENT_PROGRAM, &previous_program );
    glGetIntegerv( GL_MATRIX_MODE, &previous_matrix_mode );
    glPushAttrib( GL_ALL_ATTRIB_BITS );
    glMatrixMode( GL_PROJECTION ); glPushMatrix();
    glMatrixMode( GL_MODELVIEW ); glPushMatrix();
    bool success = ensure_target( desired_size );
    const auto qa_flag = []( const char *name ) {
        const char *value = std::getenv( name );
        return value && std::strcmp( value, "1" ) == 0;
    };
    const bool qa_capture = qa_flag( "CDDA_FP3D_CAPTURE_FRAME" );
    const bool qa_no_props = qa_capture && qa_flag( "CDDA_FP3D_QA_NO_PROP_SHADOWS" );
    const bool qa_only_props = qa_capture && qa_flag( "CDDA_FP3D_QA_ONLY_PROP_SHADOWS" );
    const bool qa_disable_far = qa_capture &&
                                qa_flag( "CDDA_FP3D_QA_DISABLE_FAR_SHADOW" );
    // Compile static world casters once for both cascades. The light-space
    // projection clips the full loaded geometry; walking translates that same
    // geometry instead of re-indexing and submitting every wall twice.
    const unsigned geometry_generation = scene.geometry_generation ?
                                         scene.geometry_generation : scene.render_generation;
    profile.stage( "shadow_geometry" );
    if( !occluder_list || !geometry_generation || occluder_generation != geometry_generation ||
        qa_no_props != occluder_no_props || qa_only_props != occluder_only_props ||
        ( occluder_had_vehicles && scene.vehicles.empty() ) ||
        ( !scene.vehicles.empty() && cached_generation != scene.render_generation ) ) {
        if( !occluder_list ) { occluder_list = glGenLists( 1 ); }
        occluder_statistics = {};
        if( occluder_list ) { glNewList( occluder_list, GL_COMPILE ); }
        draw_known_occluders( scene, 1000000.f, occluder_statistics, qa_no_props, qa_only_props );
        if( occluder_list ) { glEndList(); }
        occluder_origin = scene.origin;
        occluder_generation = geometry_generation;
        occluder_no_props = qa_no_props;
        occluder_only_props = qa_only_props;
        occluder_had_vehicles = !scene.vehicles.empty();
    }
    // Two stable, camera-rotation-independent solar light-space maps.
    profile.stage( "shadow_targets" );
    // Near: 28m at 2048; far: 96m at 1536. FBO failure -> original 38m
    // near map, without changing source geometry or scene simulation.
    const bool far_ready = success && quality >= 3 && !qa_disable_far &&
                           ensure_far_target( 1536 );
    float near_radius = far_ready ? 28.0f : radius;
    constexpr float outer_radius = 96.0f;
    const auto capture_depth = [&]( int size, data &destination, bool outer ) {
        if( !qa_capture ) { return; }
        std::vector<float> pixels( static_cast<size_t>( size ) * size );
        GLint old_row = 0, old_rows = 0, old_pixels = 0, old_alignment = 0;
        glGetIntegerv( GL_PACK_ROW_LENGTH, &old_row );
        glGetIntegerv( GL_PACK_SKIP_ROWS, &old_rows );
        glGetIntegerv( GL_PACK_SKIP_PIXELS, &old_pixels );
        glGetIntegerv( GL_PACK_ALIGNMENT, &old_alignment );
        glPixelStorei( GL_PACK_ROW_LENGTH, 0 );
        glPixelStorei( GL_PACK_SKIP_ROWS, 0 );
        glPixelStorei( GL_PACK_SKIP_PIXELS, 0 );
        glPixelStorei( GL_PACK_ALIGNMENT, 1 );
        glReadPixels( 0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, pixels.data() );
        const GLenum qa_error = glGetError();
        glPixelStorei( GL_PACK_ROW_LENGTH, old_row );
        glPixelStorei( GL_PACK_SKIP_ROWS, old_rows );
        glPixelStorei( GL_PACK_SKIP_PIXELS, old_pixels );
        glPixelStorei( GL_PACK_ALIGNMENT, old_alignment );
        if( qa_error != GL_NO_ERROR ) { return; }
        unsigned occupied = 0;
        std::uint64_t hash = 1469598103934665603ULL;
        for( const float value : pixels ) {
            if( value < .99999f ) { ++occupied; }
            const auto quantized = static_cast<std::uint32_t>(
                std::clamp( value, 0.0f, 1.0f ) * 16777215.0f );
            hash ^= quantized;
            hash *= 1099511628211ULL;
        }
        if( outer ) {
            destination.qa_far_depth_occupied_texels = occupied;
            destination.qa_far_depth_hash = hash;
        } else {
            destination.qa_depth_occupied_texels = occupied;
            destination.qa_depth_hash = hash;
        }
    };
    const auto draw_cascade = [&]( GLuint fbo, int size, float cast_radius,
                                   data &stats, bool outer ) -> bool {
        fp3d_regression::gpu_probe timer( outer ? "shadow_far" : "shadow_near" );
        bind_framebuffer( GL_FRAMEBUFFER, fbo );
        glViewport( 0, 0, size, size );
        glDisable( GL_BLEND );
        glDisable( GL_ALPHA_TEST );
        glDisable( GL_LIGHTING );
        glDisable( GL_FOG );
        glDisable( GL_TEXTURE_2D );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_CULL_FACE );
        glEnable( GL_DEPTH_TEST );
        glDepthFunc( GL_LESS );
        glDepthMask( GL_TRUE );
        glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
        glEnable( GL_POLYGON_OFFSET_FILL );
        glPolygonOffset( 1.0f, 1.0f );
        use_program( 0 );
        glClearDepth( 1.0 );
        glClear( GL_DEPTH_BUFFER_BIT );
        glMatrixMode( GL_PROJECTION );
        glLoadIdentity();
        glOrtho( -cast_radius, cast_radius, -cast_radius, cast_radius, .1f,
                 distance * 2 );
        // Snap in absolute light-space coordinates, not camera-relative
        // coordinates, so moving 1 native tile does not slide the entire
        // shadow texel grid. Double precision prevents huge-map-coordinate
        // rounding artifacts, while the GPU still receives local floats.
        const double anchor_x = static_cast<double>( scene.origin.x() );
        const double anchor_y = static_cast<double>( scene.origin.z() ) *
                                fp3d_camera::floor_height;
        const double anchor_z = static_cast<double>( scene.origin.y() );
        const auto snap = [&]( const std::array<float, 3> &axis ) {
            const double anchor = axis[0] * anchor_x + axis[1] * anchor_y +
                                  axis[2] * anchor_z;
            const double grid = 2.0 * cast_radius / size;
            return static_cast<float>( anchor - std::round( anchor / grid ) * grid );
        };
        const float snap_x = snap( right );
        const float snap_y = snap( up );
        if( outer ) {
            result.far_snap_offset = { snap_x, snap_y };
        } else {
            result.snap_offset = { snap_x, snap_y };
        }
        const GLfloat view[] = {
            right[0], up[0], sun[0], 0,
            right[1], up[1], sun[1], 0,
            right[2], up[2], sun[2], 0,
            snap_x, snap_y, -distance, 1
        };
        glMatrixMode( GL_MODELVIEW );
        glLoadMatrixf( view );
        if( occluder_list ) {
            const auto offset = occluder_origin - scene.origin;
            glPushMatrix();
            glTranslatef( offset.x(), offset.z() * fp3d_camera::floor_height, offset.y() );
            glCallList( occluder_list );
            glPopMatrix();
            stats.furniture_casters = occluder_statistics.furniture_casters;
            stats.fence_casters = occluder_statistics.fence_casters;
            stats.street_light_casters = occluder_statistics.street_light_casters;
            stats.mailbox_casters = occluder_statistics.mailbox_casters;
            stats.streetlights_with_ceiling = occluder_statistics.streetlights_with_ceiling;
        } else {
            draw_known_occluders( scene, cast_radius, stats, qa_no_props, qa_only_props );
        }
        if( !qa_only_props ) { fp3d_vehicle::draw_shadow( scene, cast_radius ); }
        if( !qa_only_props ) {
            draw_creature_occluders( scene, right[0], right[2], cast_radius );
        }
        timer.finish();
        const bool ok = glGetError() == GL_NO_ERROR;
        if( ok ) { capture_depth( size, result, outer ); }
        return ok;
    };
    profile.stage( "shadow_near" );
    if( success ) {
        success = draw_cascade( shadow_fbo, desired_size, near_radius, result, false );
    }
    profile.stage( "shadow_far" );
    if( success && far_ready ) {
        data far_stats;
        if( draw_cascade( far_shadow_fbo, 1536, outer_radius, far_stats, true ) ) {
            result.far_texture = far_shadow_texture;
            result.far_radius = outer_radius;
            result.far_resolution = 1536;
        } else {
            // Real driver/device issues must not leave a small unsupported
            // near-only light volume. Restore the proven 38m single-map FBO.
            near_radius = radius;
            success = draw_cascade( shadow_fbo, desired_size,
                                    near_radius, result, false );
        }
    }
    profile.stage( "shadow_finish" );
    bind_framebuffer( GL_FRAMEBUFFER, previous_fbo );
    glViewport( previous_viewport[0], previous_viewport[1],
                previous_viewport[2], previous_viewport[3] );
    glMatrixMode( GL_MODELVIEW ); glPopMatrix();
    glMatrixMode( GL_PROJECTION ); glPopMatrix();
    use_program( previous_program );
    glPopAttrib();
    glMatrixMode( previous_matrix_mode );
    if( success ) {
        result.texture = shadow_texture;
        result.radius = near_radius;
        result.distance = distance;
        result.strength = scene.sky.daylight *
                          std::max( .0f, 1.0f - scene.weather.clouds * .6f );
        result.x = right;
        result.y = up;
        result.sun = sun;
        result.resolution = desired_size;
        cached_shadow = result;
        cached_generation = scene.render_generation;
        cached_quality = quality;
        static int reported_size = 0;
        if( reported_size != desired_size ) {
            DebugLog( D_INFO, D_MAIN ) << "fp3d: nearby directional shadow map "
                                       << desired_size << "x" << desired_size
                                       << ", radius=" << radius
                                       << ", native-visible geometry only";
            reported_size = desired_size;
        }
    }
    return result;
}

namespace
{
GLuint render_local( const fp3d_snapshot::scene_snapshot &scene,
                     const std::array<float, 4> &position, float yaw, float pitch,
                     bool cube )
{
    local_target &target = cube ? point_target : beam_target;
    if( position[3] <= .1f || target.failed || !load_procs() ) { return 0; }
    if( target.texture && target.render_generation == scene.render_generation &&
        target.origin == scene.origin && target.position == position &&
        target.yaw == yaw && target.pitch == pitch ) { return target.texture; }

    GLint old_fbo = 0, old_program = 0, old_mode = 0;
    glGetIntegerv( GL_FRAMEBUFFER_BINDING, &old_fbo );
    glGetIntegerv( GL_CURRENT_PROGRAM, &old_program );
    glGetIntegerv( GL_MATRIX_MODE, &old_mode );
    glPushAttrib( GL_ALL_ATTRIB_BITS );
    glMatrixMode( GL_PROJECTION ); glPushMatrix();
    glMatrixMode( GL_MODELVIEW ); glPushMatrix();
    const int size = cube ? 512 : 1024;
    const GLenum texture_type = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    const int faces = cube ? 6 : 1;
    if( !target.fbo ) {
        gen_framebuffer( 1, &target.fbo );
        bind_framebuffer( GL_FRAMEBUFFER, target.fbo );
        glGenTextures( 1, &target.texture );
        glBindTexture( texture_type, target.texture );
        for( int face = 0; face < faces; ++face ) {
            const GLenum image = cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : GL_TEXTURE_2D;
            glTexImage2D( image, 0, GL_DEPTH_COMPONENT24, size, size, 0,
                          GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr );
        }
        glTexParameteri( texture_type, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        glTexParameteri( texture_type, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        glTexParameteri( texture_type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( texture_type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        if( cube ) { glTexParameteri( texture_type, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE ); }
        attach_texture( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                        cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : GL_TEXTURE_2D, target.texture, 0 );
        glDrawBuffer( GL_NONE );
        glReadBuffer( GL_NONE );
        target.failed = check_framebuffer( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE;
    }
    if( !target.failed ) {
        bind_framebuffer( GL_FRAMEBUFFER, target.fbo );
        glViewport( 0, 0, size, size );
        use_program( 0 );
        glDisable( GL_BLEND );
        glDisable( GL_ALPHA_TEST );
        glDisable( GL_LIGHTING );
        glDisable( GL_FOG );
        glDisable( GL_TEXTURE_2D );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_CULL_FACE );
        glEnable( GL_DEPTH_TEST );
        glDepthFunc( GL_LESS );
        glDepthMask( GL_TRUE );
        glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
        glEnable( GL_POLYGON_OFFSET_FILL );
        glPolygonOffset( 1.0f, 1.0f );
        glClearDepth( 1.0 );
        // This cache is independent of sunlight, so it also updates at night
        // and underground. Door/roof changes invalidate the snapshot generation.
        const unsigned generation = scene.geometry_generation ?
                                    scene.geometry_generation : scene.render_generation;
        if( !target.occluders || !generation || target.geometry_generation != generation ||
            ( cube && ( target.position != position || target.origin != scene.origin ) ) ||
            ( target.had_vehicles && scene.vehicles.empty() ) ||
            ( !scene.vehicles.empty() && target.render_generation != scene.render_generation ) ) {
            if( !target.occluders ) { target.occluders = glGenLists( 1 ); }
            if( target.occluders ) { glNewList( target.occluders, GL_COMPILE ); }
            data ignored;
            draw_known_occluders( scene, 1000000.f, ignored, false, false,
                                  cube ? &position : nullptr );
            // Solar casters need ceilings; local lights also
            // need the actual known floor to stop light crossing storeys.
            for( const auto &cell : scene.cells ) {
                if( cell.known != fp3d_snapshot::knowledge::visible || cell.exterior_only ||
                    !cell.floor_present || cell.open_air ) { continue; }
                const auto rel = cell.position - scene.origin;
                const float x = rel.x(), z = rel.y();
                const float y = rel.z() * fp3d_camera::floor_height +
                                fp3d_scene::water_surface_offset( cell.terrain );
                glBegin( GL_QUADS );
                glVertex3f( x-.5f, y, z-.5f ); glVertex3f( x+.5f, y, z-.5f );
                glVertex3f( x+.5f, y, z+.5f ); glVertex3f( x-.5f, y, z+.5f );
                glEnd();
            }
            if( target.occluders ) { glEndList(); }
            target.geometry_generation = generation;
            target.geometry_origin = scene.origin;
            target.had_vehicles = !scene.vehicles.empty();
        }
        // Cubemap directions follow OpenGL's face orientation, including Y.
        constexpr float right[6][3] = {
            {0,0,-1}, {0,0,1}, {1,0,0}, {1,0,0}, {1,0,0}, {-1,0,0}
        };
        constexpr float up[6][3] = {
            {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}
        };
        constexpr float back[6][3] = {
            {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}
        };
        for( int face = 0; face < faces; ++face ) {
            if( cube ) {
                attach_texture( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, target.texture, 0 );
            }
            if( check_framebuffer( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
                target.failed = true;
                break;
            }
            glClear( GL_DEPTH_BUFFER_BIT );
            glMatrixMode( GL_PROJECTION );
            glLoadIdentity();
            constexpr float near_plane = .05f;
            const float edge = near_plane * ( cube ? 1.0f : 1.108f );
            glFrustum( -edge, edge, -edge, edge, near_plane, position[3] );
            const float cy = std::cos( yaw ), sy = std::sin( yaw );
            const float cp = std::cos( pitch ), sp = std::sin( pitch );
            GLfloat view[] = {
                cy, sy*sp, -sy*cp, 0,
                0, cp, sp, 0,
                sy, -cy*sp, cy*cp, 0,
                0, 0, 0, 1
            };
            if( cube ) {
                for( int axis = 0; axis < 3; ++axis ) {
                    view[axis*4] = right[face][axis];
                    view[axis*4+1] = up[face][axis];
                    view[axis*4+2] = back[face][axis];
                }
            }
            glMatrixMode( GL_MODELVIEW );
            glLoadMatrixf( view );
            glTranslatef( -position[0], -position[1], -position[2] );
            if( target.occluders ) {
                const auto offset = target.geometry_origin - scene.origin;
                glPushMatrix();
                glTranslatef( offset.x(), offset.z() * fp3d_camera::floor_height, offset.y() );
                glCallList( target.occluders );
                glPopMatrix();
            }
            fp3d_vehicle::draw_shadow( scene, position[3] +
                                      std::hypot( position[0], position[2] ) + 2.0f );
            draw_creature_occluders( scene, 1, 0, position[3] +
                                    std::hypot( position[0], position[2] ) + 2.0f, &position, !cube );
        }
        target.failed = target.failed || !target.occluders || glGetError() != GL_NO_ERROR;
    }
    bind_framebuffer( GL_FRAMEBUFFER, old_fbo );
    glMatrixMode( GL_MODELVIEW ); glPopMatrix();
    glMatrixMode( GL_PROJECTION ); glPopMatrix();
    use_program( old_program );
    glPopAttrib();
    glMatrixMode( old_mode );
    if( target.failed ) { return 0; }
    target.render_generation = scene.render_generation;
    target.origin = scene.origin;
    target.position = position;
    target.yaw = yaw;
    target.pitch = pitch;
    return target.texture;
}

} // namespace

GLuint render_beam( const fp3d_snapshot::scene_snapshot &scene,
                    const std::array<float, 4> &position, float yaw, float pitch )
{
    return render_local( scene, position, yaw, pitch, false );
}

GLuint render_point( const fp3d_snapshot::scene_snapshot &scene,
                     const std::array<float, 4> &position )
{
    return render_local( scene, position, 0, 0, true );
}

void release()
{
    for( local_target *target : { &beam_target, &point_target } ) {
        if( target->occluders ) { glDeleteLists( target->occluders, 1 ); }
        if( loaded ) {
            if( target->fbo ) { delete_framebuffer( 1, &target->fbo ); }
            if( target->texture ) { glDeleteTextures( 1, &target->texture ); }
        }
        *target = {};
    }
    if( occluder_list ) { glDeleteLists( occluder_list, 1 ); }
    occluder_list = 0;
    occluder_generation = 0;
    if( loaded ) {
        clear_targets();
    }
    loaded = false;
    unavailable = false;
}
} // namespace fp3d_shadow
#endif
