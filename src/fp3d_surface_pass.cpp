#include "fp3d_surface_pass.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "fp3d_materials.h"
#include "fp3d_snapshot.h"
#include "fp3d_material_aux.h"

namespace fp3d_surface_pass
{
namespace
{
PFNGLGENFRAMEBUFFERSPROC gen_fbos = nullptr;
PFNGLBINDFRAMEBUFFERPROC bind_fbo = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC delete_fbos = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC attach_tex = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC check_fbo = nullptr;
PFNGLCREATESHADERPROC create_shader = nullptr;
PFNGLSHADERSOURCEPROC shader_source = nullptr;
PFNGLCOMPILESHADERPROC compile_shader = nullptr;
PFNGLGETSHADERIVPROC get_shader_iv = nullptr;
PFNGLDELETESHADERPROC delete_shader = nullptr;
PFNGLCREATEPROGRAMPROC create_program = nullptr;
PFNGLATTACHSHADERPROC attach_shader = nullptr;
PFNGLLINKPROGRAMPROC link_program = nullptr;
PFNGLGETPROGRAMIVPROC get_program_iv = nullptr;
PFNGLDELETEPROGRAMPROC delete_program = nullptr;
PFNGLUSEPROGRAMPROC use_program = nullptr;
PFNGLGETUNIFORMLOCATIONPROC uniform_location = nullptr;
PFNGLUNIFORM1IPROC uniform_int = nullptr;
PFNGLUNIFORM3FPROC uniform_vec3 = nullptr;
PFNGLACTIVETEXTUREPROC active_texture = nullptr;

GLuint surface_fbo = 0;
GLuint surface_texture = 0;
GLuint shader_program = 0;
GLuint fragment_shader = 0;
GLuint attached_depth = 0;
int target_width = 0;
int target_height = 0;
bool unavailable = false;
result last;

template<typename T>
bool load( T &f, const char *name )
{
    f = reinterpret_cast<T>( SDL_GL_GetProcAddress( name ) );
    return f != nullptr;
}

bool resolve()
{
    return load( gen_fbos, "glGenFramebuffers" ) &&
           load( bind_fbo, "glBindFramebuffer" ) &&
           load( delete_fbos, "glDeleteFramebuffers" ) &&
           load( attach_tex, "glFramebufferTexture2D" ) &&
           load( check_fbo, "glCheckFramebufferStatus" ) &&
           load( create_shader, "glCreateShader" ) &&
           load( shader_source, "glShaderSource" ) &&
           load( compile_shader, "glCompileShader" ) &&
           load( get_shader_iv, "glGetShaderiv" ) &&
           load( delete_shader, "glDeleteShader" ) &&
           load( create_program, "glCreateProgram" ) &&
           load( attach_shader, "glAttachShader" ) &&
           load( link_program, "glLinkProgram" ) &&
           load( get_program_iv, "glGetProgramiv" ) &&
           load( delete_program, "glDeleteProgram" ) &&
           load( use_program, "glUseProgram" ) &&
           load( uniform_location, "glGetUniformLocation" ) &&
           load( uniform_int, "glUniform1i" ) &&
           load( uniform_vec3, "glUniform3f" ) &&
           load( active_texture, "glActiveTexture" );
}

const char *fragment_source = R"GLSL(
#version 120
uniform sampler2D u_normal_map;
uniform sampler2D u_orm_map;
uniform vec3 u_surface_normal;
uniform vec3 u_tangent;
uniform vec3 u_bitangent;
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec3 bump = texture2D(u_normal_map, uv).rgb * 2.0 - 1.0;
    bump = normalize(bump);
    vec3 world_normal = normalize(
        u_tangent * bump.x + u_bitangent * bump.y + u_surface_normal * bump.z);
    vec2 orm = texture2D(u_orm_map, uv).gb;
    // RGBA8 without MRT: R/G/B contain world normal; A packs four bits
    // of roughness and four bits of metalness. A=0 remains cleared sky.
    float rough_q = floor(clamp(orm.x, 0.12, 1.0) * 15.0 + 0.5);
    float metal_q = floor(clamp(orm.y, 0.0, 1.0) * 15.0 + 0.5);
    float packed = (rough_q * 16.0 + metal_q + 0.5) / 256.0;
    gl_FragColor = vec4(world_normal * 0.5 + 0.5, packed);
}
)GLSL";

bool initialize( int width, int height, GLuint depth )
{
    if( !shader_program ) {
        fragment_shader = create_shader( GL_FRAGMENT_SHADER );
        if( !fragment_shader ) {
            return false;
        }
        const GLchar *source = fragment_source;
        shader_source( fragment_shader, 1, &source, nullptr );
        compile_shader( fragment_shader );
        GLint ok = GL_FALSE;
        get_shader_iv( fragment_shader, GL_COMPILE_STATUS, &ok );
        if( !ok ) {
            return false;
        }
        shader_program = create_program();
        if( !shader_program ) {
            return false;
        }
        attach_shader( shader_program, fragment_shader );
        link_program( shader_program );
        get_program_iv( shader_program, GL_LINK_STATUS, &ok );
        if( !ok ) {
            return false;
        }
    }
    if( surface_texture && ( width != target_width || height != target_height ) ) {
        glDeleteTextures( 1, &surface_texture );
        surface_texture = 0;
    }
    if( !surface_texture ) {
        glGenTextures( 1, &surface_texture );
        if( !surface_texture ) {
            return false;
        }
        glBindTexture( GL_TEXTURE_2D, surface_texture );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        target_width = width;
        target_height = height;
    }
    if( !surface_fbo ) {
        gen_fbos( 1, &surface_fbo );
        if( !surface_fbo ) {
            return false;
        }
    }
    bind_fbo( GL_FRAMEBUFFER, surface_fbo );
    attach_tex( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                surface_texture, 0 );
    // Always reattach: a resized scene FBO may recycle the same GLuint name.
    attach_tex( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0 );
    attached_depth = depth;
    return glGetError() == GL_NO_ERROR &&
           check_fbo( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
}

struct restore_gl {
    GLint framebuffer = 0, program = 0, current_texture = 0;
    GLint binding0 = 0, binding1 = 0, matrix_mode = 0, pack_alignment = 4;
    GLint pack_row = 0, pack_skip_rows = 0, pack_skip_pixels = 0;
    restore_gl() {
        glGetIntegerv( GL_FRAMEBUFFER_BINDING, &framebuffer );
        glGetIntegerv( GL_CURRENT_PROGRAM, &program );
        glGetIntegerv( GL_ACTIVE_TEXTURE, &current_texture );
        glGetIntegerv( GL_MATRIX_MODE, &matrix_mode );
        active_texture( GL_TEXTURE0 );
        glGetIntegerv( GL_TEXTURE_BINDING_2D, &binding0 );
        active_texture( GL_TEXTURE1 );
        glGetIntegerv( GL_TEXTURE_BINDING_2D, &binding1 );
        active_texture( GL_TEXTURE0 );
        glGetIntegerv( GL_PACK_ALIGNMENT, &pack_alignment );
        glGetIntegerv( GL_PACK_ROW_LENGTH, &pack_row );
        glGetIntegerv( GL_PACK_SKIP_ROWS, &pack_skip_rows );
        glGetIntegerv( GL_PACK_SKIP_PIXELS, &pack_skip_pixels );
        glPushAttrib( GL_ALL_ATTRIB_BITS );
        glMatrixMode( GL_PROJECTION );
        glPushMatrix();
        glMatrixMode( GL_MODELVIEW );
        glPushMatrix();
    }
    ~restore_gl() {
        glMatrixMode( GL_MODELVIEW );
        glPopMatrix();
        glMatrixMode( GL_PROJECTION );
        glPopMatrix();
        use_program( static_cast<GLuint>( program ) );
        bind_fbo( GL_FRAMEBUFFER, static_cast<GLuint>( framebuffer ) );
        active_texture( GL_TEXTURE1 );
        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( binding1 ) );
        active_texture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( binding0 ) );
        glPopAttrib();
        active_texture( static_cast<GLenum>( current_texture ) );
        glPixelStorei( GL_PACK_ALIGNMENT, pack_alignment );
        glPixelStorei( GL_PACK_ROW_LENGTH, pack_row );
        glPixelStorei( GL_PACK_SKIP_ROWS, pack_skip_rows );
        glPixelStorei( GL_PACK_SKIP_PIXELS, pack_skip_pixels );
        glMatrixMode( static_cast<GLenum>( matrix_mode ) );
    }
};

void basis( float nx, float ny, float nz,
            float tx, float ty, float tz,
            float bx, float by, float bz )
{
    uniform_vec3( uniform_location( shader_program, "u_surface_normal" ), nx, ny, nz );
    uniform_vec3( uniform_location( shader_program, "u_tangent" ), tx, ty, tz );
    uniform_vec3( uniform_location( shader_program, "u_bitangent" ), bx, by, bz );
}

void draw_floor( float x, float y, float z, const fp3d_materials::uv_rect &uv )
{
    basis( 0, 1, 0, 1, 0, 0, 0, 0, 1 );
    glBegin( GL_QUADS );
    glTexCoord2f( uv.left, uv.top ); glVertex3f( x - .5f, y, z - .5f );
    glTexCoord2f( uv.left, uv.bottom ); glVertex3f( x - .5f, y, z + .5f );
    glTexCoord2f( uv.right, uv.bottom ); glVertex3f( x + .5f, y, z + .5f );
    glTexCoord2f( uv.right, uv.top ); glVertex3f( x + .5f, y, z - .5f );
    glEnd();
}

// Match exactly the original box(...,tile_sides=true) one-meter wall strips.
void draw_wall_face( float x0, float z0, float x1, float z1,
                     float nx, float nz, const fp3d_materials::uv_rect &uv )
{
    basis( nx, 0, nz, x1 - x0, 0, z1 - z0, 0, -1, 0 );
    const float height = fp3d_camera::floor_height;
    const int limit = static_cast<int>( std::ceil( height ) );
    glBegin( GL_QUADS );
    for( int row = 0; row < limit; ++row ) {
        const float y0 = static_cast<float>( row );
        const float y1 = std::min( height, static_cast<float>( row + 1 ) );
        const float v0 = uv.bottom + ( uv.top - uv.bottom ) * ( y0 - row );
        const float v1 = uv.bottom + ( uv.top - uv.bottom ) * ( y1 - row );
        glTexCoord2f( uv.left, v0 ); glVertex3f( x0, y0, z0 );
        glTexCoord2f( uv.right, v0 ); glVertex3f( x1, y0, z1 );
        glTexCoord2f( uv.right, v1 ); glVertex3f( x1, y1, z1 );
        glTexCoord2f( uv.left, v1 ); glVertex3f( x0, y1, z0 );
    }
    glEnd();
}
} // namespace

result render( const fp3d_snapshot::scene_snapshot &scene, float yaw, float pitch,
               float aspect, fp3d_camera::offset camera, int width, int height,
               GLuint existing_scene_depth, GLuint normal_atlas, GLuint orm_atlas,
               bool readback_stats )
{
    last = {};
    const auto started = std::chrono::steady_clock::now();
    if( unavailable || !existing_scene_depth || !normal_atlas || !orm_atlas ||
        width < 16 || height < 16 || aspect <= 0.0f ) {
        return last;
    }
    if( !resolve() ) {
        unavailable = true;
        return last;
    }
    // Failures only disable this optional QA-only pass, never the world HDR FBO.
    restore_gl restore;
    if( !initialize( width, height, existing_scene_depth ) ) {
        unavailable = true;
        return last;
    }
    bind_fbo( GL_FRAMEBUFFER, surface_fbo );
    glViewport( 0, 0, width, height );
    glDisable( GL_SCISSOR_TEST );
    glDisable( GL_BLEND );
    glDisable( GL_CULL_FACE );
    glDisable( GL_ALPHA_TEST );
    glDisable( GL_LIGHTING );
    glDisable( GL_FOG );
    glDisable( GL_POLYGON_OFFSET_FILL );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );
    glDepthMask( GL_FALSE );
    glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
    glClearColor( 0, 0, 0, 0 );
    glClear( GL_COLOR_BUFFER_BIT );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    const double hw = fp3d_camera::near_clip * fp3d_camera::horizontal_tangent();
    glFrustum( -hw, hw, -hw / aspect, hw / aspect,
               fp3d_camera::near_clip, fp3d_camera::far_clip );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    glRotatef( pitch, 1, 0, 0 );
    glRotatef( yaw, 0, 1, 0 );
    glTranslatef( -camera.x, -camera.height, -camera.y );

    active_texture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, normal_atlas );
    active_texture( GL_TEXTURE1 );
    glBindTexture( GL_TEXTURE_2D, orm_atlas );
    active_texture( GL_TEXTURE0 );
    use_program( shader_program );
    uniform_int( uniform_location( shader_program, "u_normal_map" ), 0 );
    uniform_int( uniform_location( shader_program, "u_orm_map" ), 1 );
    const fp3d_camera::frustum view( yaw, pitch, aspect );
    for( const auto &cell : scene.cells ) {
        if( cell.known != fp3d_snapshot::knowledge::visible || cell.exterior_only ) {
            continue;
        }
        const auto rel = cell.position - scene.origin;
        const float x = static_cast<float>( rel.x() );
        const float z = static_cast<float>( rel.y() );
        const float y = static_cast<float>( rel.z() ) * fp3d_camera::floor_height;
        if( std::abs( x - camera.x ) > 38 || std::abs( z - camera.y ) > 38 ||
            std::abs( y - camera.height ) > fp3d_camera::floor_height * 4 ||
            !view.contains( x - camera.x, y + 1.0f - camera.height,
                            z - camera.y, 1.6f ) ) {
            continue;
        }
        const bool treetop = cell.terrain.compare( 0, 9, "t_treetop" ) == 0;
        const bool floor_ok = cell.floor_present && !cell.is_ramp && !cell.goes_down &&
                              cell.terrain != "t_radio_tower" && !treetop;
        if( floor_ok ) {
            const bool door = cell.kind == fp3d_snapshot::terrain_kind::closed_door ||
                              cell.kind == fp3d_snapshot::terrain_kind::open_door ||
                              cell.kind == fp3d_snapshot::terrain_kind::damaged_door ||
                              cell.kind == fp3d_snapshot::terrain_kind::glass;
            const std::string floor_key = door || cell.kind == fp3d_snapshot::terrain_kind::window ?
                                          "t_floor" : cell.metal_deck_railing ?
                                          "t_metal_floor_no_roof" : cell.terrain;
            if( auto uv = fp3d_materials::lookup_auxiliary_uv( floor_key ) ) {
                draw_floor( x, y, z, *uv );
                ++last.floors;
            }
        }
        if( cell.kind == fp3d_snapshot::terrain_kind::wall ) {
            if( auto uv = fp3d_materials::lookup_auxiliary_uv( cell.terrain ) ) {
                glPushMatrix();
                glTranslatef( 0, y, 0 );
                constexpr float half = .49f;
                draw_wall_face( x - half, z - half, x + half, z - half, 0, -1, *uv );
                draw_wall_face( x + half, z + half, x - half, z + half, 0, 1, *uv );
                draw_wall_face( x - half, z + half, x - half, z - half, -1, 0, *uv );
                draw_wall_face( x + half, z - half, x + half, z + half, 1, 0, *uv );
                glPopMatrix();
                last.wall_faces += 4;
            }
        }
    }
    use_program( 0 );
    if( glGetError() != GL_NO_ERROR ) {
        unavailable = true;
        return last;
    }
    // The playable PBR path MUST remain GPU-only: never synchronize the
    // entire framebuffer with CPU per frame. The old pixel QA remains opt-in.
    if( readback_stats ) {
        glPixelStorei( GL_PACK_ALIGNMENT, 1 );
        glPixelStorei( GL_PACK_ROW_LENGTH, 0 );
        glPixelStorei( GL_PACK_SKIP_ROWS, 0 );
        glPixelStorei( GL_PACK_SKIP_PIXELS, 0 );
        std::vector<std::uint8_t> pixels( static_cast<std::size_t>( width ) * height * 4 );
        glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
        if( glGetError() != GL_NO_ERROR ) {
            unavailable = true;
            return last;
        }
        for( std::size_t i = 0; i < pixels.size(); i += 4 ) {
            if( pixels[i + 3] == 0 ) {
                continue;
            }
            ++last.covered_pixels;
            if( pixels[i + 1] > 200 ) {
                ++last.upward_pixels;
            } else if( pixels[i + 1] > 73 && pixels[i + 1] < 183 &&
                       ( pixels[i] < 72 || pixels[i] > 183 ||
                         pixels[i + 2] < 72 || pixels[i + 2] > 183 ) ) {
                ++last.upright_wall_pixels;
            }
        }
        last.readback_performed = true;
    }
    last.texture = surface_texture;
    last.framebuffer = surface_fbo;
    last.valid = ( last.floors > 0 || last.wall_faces > 0 ) &&
                 ( !readback_stats || last.covered_pixels > 0 );
    last.cpu_readback_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - started ).count();
    return last;
}

bool draw_qa_preview()
{
    if( !last.valid || !surface_texture || !active_texture || !use_program ) {
        return false;
    }
    restore_gl restore;
    use_program( 0 );
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_BLEND );
    glDisable( GL_SCISSOR_TEST );
    glDisable( GL_LIGHTING );
    glDisable( GL_FOG );
    glDisable( GL_CULL_FACE );
    glDisable( GL_ALPHA_TEST );
    glEnable( GL_TEXTURE_2D );
    active_texture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, surface_texture );
    glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
    glColor4f( 1, 1, 1, 1 );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    // Occupies top-right only in isolated QA captures; existing post/HUD unaffected.
    glBegin( GL_QUADS );
    glTexCoord2f( 0, 0 ); glVertex2f( .47f, .94f );
    glTexCoord2f( 1, 0 ); glVertex2f( .95f, .94f );
    glTexCoord2f( 1, 1 ); glVertex2f( .95f, .30f );
    glTexCoord2f( 0, 1 ); glVertex2f( .47f, .30f );
    glEnd();
    return glGetError() == GL_NO_ERROR;
}

void release()
{
    if( surface_fbo && delete_fbos ) {
        delete_fbos( 1, &surface_fbo );
    }
    if( surface_texture ) {
        glDeleteTextures( 1, &surface_texture );
    }
    if( shader_program && delete_program ) {
        delete_program( shader_program );
    }
    if( fragment_shader && delete_shader ) {
        delete_shader( fragment_shader );
    }
    surface_fbo = surface_texture = shader_program = fragment_shader = attached_depth = 0;
    target_width = target_height = 0;
    unavailable = false;
    last = {};
}
} // namespace fp3d_surface_pass
#endif
