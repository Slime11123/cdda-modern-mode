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
#include "fp3d_camera.h"
#include "fp3d_snapshot.h"
#include "fp3d_scene.h"
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
int far_target_size = 0;
bool loaded = false;
bool unavailable = false;
unsigned cached_generation = 0;
int cached_quality = -1;
data cached_shadow;
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

void box( float x, float z, float bottom, float top, float half = .5f )
{
    const float left = x - half, right = x + half;
    const float north = z - half, south = z + half;
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
    glPushMatrix();
    glTranslatef( x, 0.0f, z );
    glScalef( 2.0f * half_x, 1.0f, 2.0f * half_z );
    box( 0.0f, 0.0f, bottom, top );
    glPopMatrix();
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
        glPushMatrix();
        glTranslatef( part.x, 0, part.z );
        glScalef( part.width, 1.0f, part.depth );
        box( 0, 0, part.bottom, part.top, .5f );
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
                           data &statistics, bool qa_suppress_props, bool qa_only_props )
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
                while( castable_wall( start + back ) &&
                       drawn_walls.find( start + back ) == drawn_walls.end() ) {
                    start += back;
                }
                int tiles = 0;
                auto end = start;
                while( castable_wall( end ) && drawn_walls.insert( end ).second ) {
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
                    wall_run( run_x, run_z, bottom, top, half_x, half_z );
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
                   cell.kind == fp3d_snapshot::terrain_kind::obstacle ) {
            // Actual native 3D terrain obstacle uses a .98-m box 1-m tall.
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
                furniture_shadow_part( parts[i] );
            }
            glPopMatrix();
        }
        // A ceiling is only used when the game exposes that actual boundary.
        // The visible mesh sometimes suppresses a known phantom overhead
        // sheet above a car parked on a road. The shadow pass MUST use the
        // same mask, otherwise an invisible slab still casts dark footprints.
        // Do not invent occluding upper floors from unknown map cells.
        if( !qa_only_props && cell.ceiling_present && !cell.open_air &&
            // Tree crowns have sparse branches rather than an opaque roof.
            // Never cast a full-tile shadow from an invisible canopy slab.
            cell.terrain.compare( 0, 9, "t_treetop" ) != 0 &&
            !( cell.vegetation == cell.terrain && cell.vegetation_height >= 4.0f ) &&
            !fp3d_scene::vehicle_roof_sheet_suppressed( cell.position,
                    scene.render_generation ) ) {
            glBegin( GL_QUADS );
            glVertex3f( x - .5f, top, z - .5f );
            glVertex3f( x - .5f, top, z + .5f );
            glVertex3f( x + .5f, top, z + .5f );
            glVertex3f( x + .5f, top, z - .5f );
            glEnd();
        }
    }
}

// Vehicle shadow casting is delegated to fp3d_vehicle::draw_shadow() so the
// same opaque mesh is rasterized for both visible cars and their sun shadows.
// Rebuilding separate per-part cuboids here would recreate roofs on wrecks,
// fill open doors and misalign the true raised cab/body silhouette.

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
    // Two stable, camera-rotation-independent solar light-space maps.
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
        draw_known_occluders( scene, cast_radius, stats, qa_no_props, qa_only_props );
        if( !qa_only_props ) { fp3d_vehicle::draw_shadow( scene, cast_radius ); }
        const bool ok = glGetError() == GL_NO_ERROR;
        if( ok ) { capture_depth( size, result, outer ); }
        return ok;
    };
    if( success ) {
        success = draw_cascade( shadow_fbo, desired_size, near_radius, result, false );
    }
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

void release()
{
    if( loaded ) {
        clear_targets();
    }
    loaded = false;
    unavailable = false;
}
} // namespace fp3d_shadow
#endif
