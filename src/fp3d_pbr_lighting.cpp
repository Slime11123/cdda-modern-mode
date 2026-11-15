#include "fp3d_pbr_lighting.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <chrono>
#include <cmath>

namespace fp3d_pbr_lighting
{
namespace
{
PFNGLGENFRAMEBUFFERSPROC gen_framebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC bind_framebuffer = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC delete_framebuffers = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC framebuffer_texture = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC check_framebuffer = nullptr;
PFNGLCREATESHADERPROC create_shader = nullptr;
PFNGLSHADERSOURCEPROC shader_source = nullptr;
PFNGLCOMPILESHADERPROC compile_shader = nullptr;
PFNGLGETSHADERIVPROC shader_iv = nullptr;
PFNGLDELETESHADERPROC delete_shader = nullptr;
PFNGLCREATEPROGRAMPROC create_program = nullptr;
PFNGLATTACHSHADERPROC attach_shader = nullptr;
PFNGLLINKPROGRAMPROC link_program = nullptr;
PFNGLGETPROGRAMIVPROC program_iv = nullptr;
PFNGLDELETEPROGRAMPROC delete_program = nullptr;
PFNGLUSEPROGRAMPROC use_program = nullptr;
PFNGLGETUNIFORMLOCATIONPROC uniform_location = nullptr;
PFNGLUNIFORM1IPROC uniform_int = nullptr;
PFNGLUNIFORM1FPROC uniform_float = nullptr;
PFNGLUNIFORM3FPROC uniform_vec3 = nullptr;
PFNGLACTIVETEXTUREPROC active_texture = nullptr;

GLuint output_texture = 0;
GLuint output_fbo = 0;
GLuint fragment = 0;
GLuint program = 0;
int output_width = 0;
int output_height = 0;
bool disabled = false;
bool resolved = false;

template<typename T>
bool load( T &proc, const char *name )
{
    proc = reinterpret_cast<T>( SDL_GL_GetProcAddress( name ) );
    return proc != nullptr;
}

bool resolve()
{
    if( resolved ) return true;
    resolved = load( gen_framebuffers, "glGenFramebuffers" ) &&
           load( bind_framebuffer, "glBindFramebuffer" ) &&
           load( delete_framebuffers, "glDeleteFramebuffers" ) &&
           load( framebuffer_texture, "glFramebufferTexture2D" ) &&
           load( check_framebuffer, "glCheckFramebufferStatus" ) &&
           load( create_shader, "glCreateShader" ) &&
           load( shader_source, "glShaderSource" ) &&
           load( compile_shader, "glCompileShader" ) &&
           load( shader_iv, "glGetShaderiv" ) &&
           load( delete_shader, "glDeleteShader" ) &&
           load( create_program, "glCreateProgram" ) &&
           load( attach_shader, "glAttachShader" ) &&
           load( link_program, "glLinkProgram" ) &&
           load( program_iv, "glGetProgramiv" ) &&
           load( delete_program, "glDeleteProgram" ) &&
           load( use_program, "glUseProgram" ) &&
           load( uniform_location, "glGetUniformLocation" ) &&
           load( uniform_int, "glUniform1i" ) &&
           load( uniform_float, "glUniform1f" ) &&
           load( uniform_vec3, "glUniform3f" ) &&
           load( active_texture, "glActiveTexture" );
    return resolved;
}

const char *fragment_source = R"GLSL(
#version 120
uniform sampler2D u_color;
uniform sampler2D u_depth;
uniform sampler2D u_surface;
uniform vec3 u_sun;
uniform float u_daylight;
uniform float u_clouds;
uniform float u_yaw;
uniform float u_pitch;
uniform float u_aspect;
uniform float u_intensity;

// Surface metadata stored in GL_RGBA8 to support old OpenGL2 FBOs:
// RGB world-space normal; alpha stores two quantized 4-bit ORM channels.
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec4 source = texture2D(u_color, uv);
    vec4 surf = texture2D(u_surface, uv);
    float depth = texture2D(u_depth, uv).r;
    if(surf.a < 0.025 || depth > 0.99995 || u_daylight < 0.05 ||
       u_sun.y < 0.02) {
        gl_FragColor = source;
        return;
    }
    float packed = floor(surf.a * 255.0 + 0.5);
    float roughness = clamp(floor(packed / 16.0) / 15.0, 0.14, 0.98);
    float metalness = clamp(mod(packed, 16.0) / 15.0, 0.0, 0.90);
    vec3 n = normalize(surf.rgb * 2.0 - 1.0);
    // Reconstruct the original flat, axis-aligned world face. Use the
    // *difference* to add microrelief to the existing native/Photon lighting;
    // do not multiply directional sunlight a second time or darken interiors.
    vec3 a = abs(n);
    vec3 geom = a.y >= a.x && a.y >= a.z ?
        vec3(0.0, sign(n.y), 0.0) :
        (a.x >= a.z ? vec3(sign(n.x), 0.0, 0.0) :
                      vec3(0.0, 0.0, sign(n.z)));
    vec3 light = normalize(u_sun);
    float geometric_nl = max(dot(geom, light), 0.0);
    float actual_nl = max(dot(n, light), 0.0);
    float sun = clamp(u_daylight, 0.0, 1.0) *
                (1.0 - 0.65 * clamp(u_clouds, 0.0, 1.0));
    // Restrict the normal contrast to avoid jagged pixel-art edges turning
    // walls into coarse black triangles, including the old v18 artifact.
    float relief = clamp(actual_nl - geometric_nl, -0.26, 0.26);
    float diffuse_factor = 1.0 + relief * 0.34 * sun * u_intensity;

    // Actual camera view ray from the original scene depth. Rotate back to
    // world space using the exact yaw/pitch convention of the 3D renderer.
    const float near_z = 0.2;
    const float far_z = 1536.0;
    float z = near_z * far_z / max(0.001, far_z - depth * (far_z - near_z));
    vec3 view_point = vec3((2.0 * uv.x - 1.0) * 1.7320508 * z,
                           (2.0 * uv.y - 1.0) * 1.7320508 *
                               z / max(0.25, u_aspect), -z);
    vec3 view_local = normalize(-view_point);
    float cy = cos(u_yaw), sy = sin(u_yaw);
    float cp = cos(u_pitch), sp = sin(u_pitch);
    float vy = cp * view_local.y + sp * view_local.z;
    float vz = -sp * view_local.y + cp * view_local.z;
    vec3 v = normalize(vec3(cy * view_local.x - sy * vz, vy,
                            sy * view_local.x + cy * vz));
    float nv = max(dot(n, v), 0.0);
    float nl = actual_nl;
    vec3 specular = vec3(0.0);
    if(nl > 0.015 && nv > 0.025) {
        // Limited microfacet GGX/Schlick specular. Explicitly capped and
        // energy-controlled because the source was already lit by the native
        // forward pass. Future work will replace that double-lighting bridge.
        vec3 h = normalize(v + light);
        float nh = max(dot(n, h), 0.0);
        float vh = max(dot(v, h), 0.0);
        float alpha = roughness * roughness;
        float alpha2 = alpha * alpha;
        float denom = nh * nh * (alpha2 - 1.0) + 1.0;
        float distribution = alpha2 /
                             max(0.002, 3.14159265 * denom * denom);
        float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
        float geometry = (nv / max(0.001, nv * (1.0 - k) + k)) *
                         (nl / max(0.001, nl * (1.0 - k) + k));
        vec3 f0 = mix(vec3(0.04), clamp(source.rgb, 0.0, 1.0), metalness);
        vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(1.0 - vh, 5.0);
        specular = distribution * geometry * fresnel /
                   max(0.05, 4.0 * nv * nl);
        specular = min(specular, vec3(0.24));
    }
    // Narrow contribution: retain existing tonemap/ambient colour and let
    // the original following shadow pass occlude the new sun highlights too.
    vec3 light_color = vec3(1.0, 0.94, 0.85);
    vec3 result = max(source.rgb * diffuse_factor, vec3(0.0)) +
                  light_color * specular * sun * nl * 0.28 * u_intensity;
    gl_FragColor = vec4(result, source.a);
}
)GLSL";

struct preserve {
    GLint framebuffer = 0, current_program = 0, active_unit = GL_TEXTURE0;
    GLint original_mode = GL_MODELVIEW;
    GLint binding0 = 0, binding1 = 0, binding2 = 0;
    preserve() {
        glGetIntegerv( GL_FRAMEBUFFER_BINDING, &framebuffer );
        glGetIntegerv( GL_CURRENT_PROGRAM, &current_program );
        glGetIntegerv( GL_ACTIVE_TEXTURE, &active_unit );
        glGetIntegerv( GL_MATRIX_MODE, &original_mode );
        glPushAttrib( GL_ALL_ATTRIB_BITS );
        active_texture( GL_TEXTURE0 ); glGetIntegerv( GL_TEXTURE_BINDING_2D, &binding0 );
        active_texture( GL_TEXTURE1 ); glGetIntegerv( GL_TEXTURE_BINDING_2D, &binding1 );
        active_texture( GL_TEXTURE2 ); glGetIntegerv( GL_TEXTURE_BINDING_2D, &binding2 );
        active_texture( GL_TEXTURE0 );
        glMatrixMode( GL_PROJECTION ); glPushMatrix();
        glMatrixMode( GL_MODELVIEW ); glPushMatrix();
    }
    ~preserve() {
        glMatrixMode( GL_MODELVIEW ); glPopMatrix();
        glMatrixMode( GL_PROJECTION ); glPopMatrix();
        use_program( static_cast<GLuint>( current_program ) );
        bind_framebuffer( GL_FRAMEBUFFER, static_cast<GLuint>( framebuffer ) );
        active_texture( GL_TEXTURE2 ); glBindTexture( GL_TEXTURE_2D, binding2 );
        active_texture( GL_TEXTURE1 ); glBindTexture( GL_TEXTURE_2D, binding1 );
        active_texture( GL_TEXTURE0 ); glBindTexture( GL_TEXTURE_2D, binding0 );
        glPopAttrib();
        active_texture( static_cast<GLenum>( active_unit ) );
        glMatrixMode( static_cast<GLenum>( original_mode ) );
    }
};

bool prepare( int width, int height )
{
    if( !program ) {
        fragment = create_shader( GL_FRAGMENT_SHADER );
        if( !fragment ) return false;
        const GLchar *source = fragment_source;
        shader_source( fragment, 1, &source, nullptr );
        compile_shader( fragment );
        GLint status = GL_FALSE;
        shader_iv( fragment, GL_COMPILE_STATUS, &status );
        if( !status ) return false;
        program = create_program();
        if( !program ) return false;
        attach_shader( program, fragment );
        link_program( program );
        program_iv( program, GL_LINK_STATUS, &status );
        if( !status ) return false;
    }
    if( output_texture && ( width != output_width || height != output_height ) ) {
        glDeleteTextures( 1, &output_texture );
        output_texture = 0;
    }
    if( !output_texture ) {
        glGenTextures( 1, &output_texture );
        if( !output_texture ) return false;
        glBindTexture( GL_TEXTURE_2D, output_texture );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, width, height,
                      0, GL_RGBA, GL_FLOAT, nullptr );
        if( glGetError() != GL_NO_ERROR ) {
            // Capability fallback: preserve output even without HDR colour.
            glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height,
                          0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
        }
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        output_width = width; output_height = height;
    }
    if( !output_fbo ) gen_framebuffers( 1, &output_fbo );
    if( !output_fbo ) return false;
    bind_framebuffer( GL_FRAMEBUFFER, output_fbo );
    framebuffer_texture( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         output_texture, 0 );
    return glGetError() == GL_NO_ERROR &&
           check_framebuffer( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
}
} // namespace

result apply( GLuint scene_color, GLuint scene_depth, GLuint surface,
              int width, int height, const float ( &sun )[3],
              float daylight, float clouds, float yaw, float pitch, float aspect,
              float intensity )
{
    result out;
    if( disabled || !scene_color || !scene_depth || !surface ||
        width <= 0 || height <= 0 || aspect < 0.25f ) {
        return out;
    }
    if( !resolve() ) {
        disabled = true;
        out.failed = true;
        return out;
    }
    const auto started = std::chrono::steady_clock::now();
    preserve state;
    if( !prepare( width, height ) ) {
        disabled = true;
        out.failed = true;
        return out;
    }
    bind_framebuffer( GL_FRAMEBUFFER, output_fbo );
    glViewport( 0, 0, width, height );
    glDisable( GL_DEPTH_TEST ); glDisable( GL_BLEND );
    glDisable( GL_ALPHA_TEST ); glDisable( GL_CULL_FACE );
    glDisable( GL_LIGHTING ); glDisable( GL_FOG );
    glDisable( GL_SCISSOR_TEST );
    glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
    glEnable( GL_TEXTURE_2D );
    active_texture( GL_TEXTURE0 ); glBindTexture( GL_TEXTURE_2D, scene_color );
    active_texture( GL_TEXTURE1 ); glBindTexture( GL_TEXTURE_2D, scene_depth );
    active_texture( GL_TEXTURE2 ); glBindTexture( GL_TEXTURE_2D, surface );
    active_texture( GL_TEXTURE0 );
    use_program( program );
    uniform_int( uniform_location( program, "u_color" ), 0 );
    uniform_int( uniform_location( program, "u_depth" ), 1 );
    uniform_int( uniform_location( program, "u_surface" ), 2 );
    uniform_vec3( uniform_location( program, "u_sun" ), sun[0], sun[1], sun[2] );
    uniform_float( uniform_location( program, "u_daylight" ), daylight );
    uniform_float( uniform_location( program, "u_clouds" ), clouds );
    uniform_float( uniform_location( program, "u_yaw" ), yaw );
    uniform_float( uniform_location( program, "u_pitch" ), pitch );
    uniform_float( uniform_location( program, "u_aspect" ), aspect );
    uniform_float( uniform_location( program, "u_intensity" ),
                   std::clamp( intensity, 0.0f, 2.0f ) );
    glMatrixMode( GL_PROJECTION ); glLoadIdentity();
    glMatrixMode( GL_MODELVIEW ); glLoadIdentity();
    glColor4f( 1, 1, 1, 1 );
    glBegin( GL_QUADS );
    glTexCoord2f( 0, 0 ); glVertex2f( -1, -1 );
    glTexCoord2f( 1, 0 ); glVertex2f( 1, -1 );
    glTexCoord2f( 1, 1 ); glVertex2f( 1, 1 );
    glTexCoord2f( 0, 1 ); glVertex2f( -1, 1 );
    glEnd();
    if( glGetError() != GL_NO_ERROR ) {
        disabled = true;
        out.failed = true;
        return out;
    }
    out.texture = output_texture;
    out.framebuffer = output_fbo;
    out.applied = true;
    out.submit_ms = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - started ).count();
    return out;
}

void release()
{
    if( output_fbo && delete_framebuffers ) delete_framebuffers( 1, &output_fbo );
    if( output_texture ) glDeleteTextures( 1, &output_texture );
    if( program && delete_program ) delete_program( program );
    if( fragment && delete_shader ) delete_shader( fragment );
    output_fbo = output_texture = program = fragment = 0;
    output_width = output_height = 0;
    disabled = false;
    resolved = false;
}
} // namespace fp3d_pbr_lighting
#endif
