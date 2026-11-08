#include "fp3d_shaderpacks.h"
#include "fp3d_shader_archive.h"
#include "fp3d_mcshader.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif
#include "sdl_wrappers.h"

namespace fp3d_shaderpacks
{
namespace
{
namespace fs = std::filesystem;
constexpr size_t max_shader_bytes = 128 * 1024;
std::string choice;
std::string report;
std::string compiled_choice;
bool choice_loaded = false;
int quality_level = 2;
int mc_intensity = 145;
bool native_shadows_enabled = true;
bool wet_ssr = true;
bool shadow_volume = true;
bool mc_bridge = false;
bool mc_direct = false;
fp3d_mcshader::profile mc_profile;
GLuint program = 0;
GLuint output_fbo = 0;
GLuint output_texture = 0;
GLuint last_processed_fbo = 0;
int output_width = 0;
int output_height = 0;
bool proc_loaded = false;

PFNGLCREATESHADERPROC create_shader = nullptr;
PFNGLSHADERSOURCEPROC shader_source = nullptr;
PFNGLCOMPILESHADERPROC compile_shader = nullptr;
PFNGLGETSHADERIVPROC get_shader_iv = nullptr;
PFNGLGETSHADERINFOLOGPROC get_shader_log = nullptr;
PFNGLDELETESHADERPROC delete_shader = nullptr;
PFNGLCREATEPROGRAMPROC create_program = nullptr;
PFNGLATTACHSHADERPROC attach_shader = nullptr;
PFNGLLINKPROGRAMPROC link_program = nullptr;
PFNGLGETPROGRAMIVPROC get_program_iv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC get_program_log = nullptr;
PFNGLDELETEPROGRAMPROC delete_program = nullptr;
PFNGLUSEPROGRAMPROC use_program = nullptr;
PFNGLACTIVETEXTUREPROC active_texture = nullptr;
PFNGLGETUNIFORMLOCATIONPROC uniform_location = nullptr;
PFNGLUNIFORM1IPROC uniform_1i = nullptr;
PFNGLUNIFORM1FPROC uniform_1f = nullptr;
PFNGLUNIFORM2FPROC uniform_2f = nullptr;
PFNGLUNIFORM3FPROC uniform_3f = nullptr;
PFNGLGENFRAMEBUFFERSPROC gen_fbo = nullptr;
PFNGLBINDFRAMEBUFFERPROC bind_fbo = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC attach_texture = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC check_fbo = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC delete_fbo = nullptr;

template<typename T> bool load( T &fn, const char *name )
{
    fn = reinterpret_cast<T>( SDL_GL_GetProcAddress( name ) );
    return fn != nullptr;
}

bool get_procs()
{
    if( proc_loaded ) {
        return true;
    }
    proc_loaded =
        load( create_shader, "glCreateShader" ) &&
        load( shader_source, "glShaderSource" ) &&
        load( compile_shader, "glCompileShader" ) &&
        load( get_shader_iv, "glGetShaderiv" ) &&
        load( get_shader_log, "glGetShaderInfoLog" ) &&
        load( delete_shader, "glDeleteShader" ) &&
        load( create_program, "glCreateProgram" ) &&
        load( attach_shader, "glAttachShader" ) &&
        load( link_program, "glLinkProgram" ) &&
        load( get_program_iv, "glGetProgramiv" ) &&
        load( get_program_log, "glGetProgramInfoLog" ) &&
        load( delete_program, "glDeleteProgram" ) &&
        load( use_program, "glUseProgram" ) &&
        load( active_texture, "glActiveTexture" ) &&
        load( uniform_location, "glGetUniformLocation" ) &&
        load( uniform_1i, "glUniform1i" ) &&
        load( uniform_1f, "glUniform1f" ) &&
        load( uniform_2f, "glUniform2f" ) &&
        load( uniform_3f, "glUniform3f" ) &&
        load( gen_fbo, "glGenFramebuffers" ) &&
        load( bind_fbo, "glBindFramebuffer" ) &&
        load( attach_texture, "glFramebufferTexture2D" ) &&
        load( check_fbo, "glCheckFramebufferStatus" ) &&
        load( delete_fbo, "glDeleteFramebuffers" );
    if( !proc_loaded ) {
        report = "当前 OpenGL 不提供可编程后处理；已回退原版 3D。";
    }
    return proc_loaded;
}

const char *vertex_source = R"glsl(#version 120
varying vec2 v_uv;
varying vec2 texcoord;
void main() {
    v_uv = gl_MultiTexCoord0.xy;
    texcoord = v_uv;
    gl_Position = gl_Vertex;
}
)glsl";

const char *builtin_source = R"glsl(#version 120
uniform sampler2D u_scene;
uniform vec2 u_texel;
uniform float u_time;
uniform float u_style;
uniform float u_quality;
uniform float u_daylight;
uniform float u_clouds;
uniform float u_fog_end;
uniform float u_outdoors;
uniform float u_sky_visible;
uniform sampler2D u_depth;
uniform float u_has_depth;
uniform vec2 u_sun_position;
uniform float u_sun_strength;
uniform sampler2D u_shadow;
uniform sampler2D u_shadow_far;
uniform float u_has_shadow;
uniform float u_has_shadow_far;
uniform float u_shadow_far_radius;
uniform float u_shadow_far_resolution;
uniform vec2 u_shadow_offset;
uniform vec2 u_shadow_far_offset;
uniform float u_shadow_radius;
uniform float u_shadow_distance;
uniform float u_shadow_strength;
uniform float u_shadow_resolution;
uniform vec2 u_yaw_pitch;
uniform float u_aspect;
uniform vec2 u_camera_offset;
uniform float u_camera_height;
uniform vec3 u_shadow_x;
uniform vec3 u_shadow_y;
uniform vec3 u_shadow_sun;
varying vec2 v_uv;
void main() {
    vec4 original = texture2D(u_scene, v_uv);
    float radius = u_quality < 0.5 ? 0.0 : u_quality < 1.5 ? 1.0 : 2.0;
    vec3 glow = vec3(0.0);
    float samples = 0.0;
    for(int y = -2; y <= 2; ++y) {
        for(int x = -2; x <= 2; ++x) {
            if(abs(float(x)) > radius || abs(float(y)) > radius) { continue; }
            vec3 s = texture2D(u_scene, v_uv + vec2(float(x),float(y)) *
                               u_texel * (u_quality > 2.5 ? 3.0 : 2.0)).rgb;
            glow += max(s - vec3(0.65), vec3(0.0));
            samples += 1.0;
        }
    }
    glow /= max(samples, 1.0);
    vec3 color = original.rgb + glow * (u_style < 1.5 ? 0.75 : 1.1);
    // Ambient occlusion on short screen-space depth discontinuities, not on UI.
    if(u_has_depth > 0.5 && u_quality > 0.5) {
        float z = texture2D(u_depth, v_uv).r;
        if(z < 0.9999) {
            float occupied = 0.0;
            vec2 d = u_texel * 2.0;
            occupied += step(0.00006, z - texture2D(u_depth,v_uv + vec2(d.x,0.0)).r);
            occupied += step(0.00006, z - texture2D(u_depth,v_uv - vec2(d.x,0.0)).r);
            occupied += step(0.00006, z - texture2D(u_depth,v_uv + vec2(0.0,d.y)).r);
            occupied += step(0.00006, z - texture2D(u_depth,v_uv - vec2(0.0,d.y)).r);
            color *= 1.0 - occupied * 0.035;
            // Depth-based atmospheric fog cannot flip for the whole scene
            // as the avatar crosses a doorway. Use actual distance, and keep
            // near interior surfaces (< 8m) free of global sky haze.
            if(u_fog_end > 0.0) {
                float near_p = 0.20;
                float far_p = 1536.0;
                float linear_depth = (near_p * far_p) /
                    (far_p - z * (far_p - near_p));
                float fog = clamp((linear_depth - 8.0) /
                    max(u_fog_end - 8.0, 1.0), 0.0, 1.0);
                float grey = 0.025 + u_daylight * (0.33 + u_clouds * 0.15);
                color = mix(color, vec3(grey * 0.92, grey * 0.98, grey), fog * 0.32);
            }
        }
    }
    // One near-range directional shadow map from exposed native geometry.
    // Reconstruct the view ray from the current scene depth before projection
    // to the solar depth map; no invisible game entities are synthesized.
    if(u_has_shadow > 0.5 && u_has_depth > 0.5 && u_quality >= 1.0) {
        float z = texture2D(u_depth, v_uv).r;
        if(z < 0.99998) {
            float n = 0.2;
            float f = 1536.0;
            float view_distance = n * f / (f - z * (f - n));
            vec3 cam = vec3((v_uv.x * 2.0 - 1.0) * view_distance * 1.7320508,
                            (v_uv.y * 2.0 - 1.0) * view_distance *
                            (1.7320508 / u_aspect), -view_distance);
            float cy = cos(u_yaw_pitch.x);
            float sy = sin(u_yaw_pitch.x);
            float cp = cos(u_yaw_pitch.y);
            float sp = sin(u_yaw_pitch.y);
            float vy = cp * cam.y + sp * cam.z;
            float vz = -sp * cam.y + cp * cam.z;
            vec3 world = vec3(cy * cam.x - sy * vz, vy + u_camera_height,
                              sy * cam.x + cy * vz);
            world.xz += u_camera_offset;
            float texel = 1.0 / max(1.0, u_shadow_resolution);
            // The fixed receiver bias previously applied the same strong
            // sun occlusion to walls, floors and underside ceilings. A
            // back-facing wall receives NO direct sunlight in native GL,
            // so its ambient term must not be shadowed again. Reconstruct
            // the actual rendered surface normal from local depth-plane
            // derivatives. Orient toward camera (visible face), not the
            // arbitrary triangle winding of the wall atlas.
            // Scale-invariant receiver normal for near-camera surfaces.
            // A 1-2 metre wall at 2520px width has dFdx/dFdy areas
            // around 1e-6 metres^2. The former absolute 1e-5 cross-length
            // threshold incorrectly replaced that valid vertical normal
            // with an UPWARD one, creating huge camera-dependent black
            // wall triangles even when the real wall does not face the sun.
            //
            // Normalize each derivative before taking their cross, then
            // test only the angle. Degenerate/edge pixels get NO direct
            // solar shadow, never a fabricated floor normal.
            vec3 dpdx=dFdx(world);
            vec3 dpdy=dFdy(world);
            float dx_len=length(dpdx);
            float dy_len=length(dpdy);
            vec3 receiver_normal=vec3(0.0);
            if(dx_len>0.0000001 && dy_len>0.0000001) {
                vec3 oriented=cross(dpdx/dx_len,dpdy/dy_len);
                float sine_angle=length(oriented);
                if(sine_angle>0.05) {
                    receiver_normal=oriented/sine_angle;
                }
            }
            vec3 toward_eye=vec3(u_camera_offset.x,u_camera_height,
                                 u_camera_offset.y)-world;
            if(dot(receiver_normal,toward_eye)<0.0) {
                receiver_normal=-receiver_normal;
            }
            // Most world wall/floor/ceiling faces are axis-aligned. Screen
            // depth derivatives at seams and near the camera can rotate a
            // reconstructed normal as the player turns, producing huge
            // camera-dependent triangular shadow bands on an unchanged wall.
            // Snap only confidently planar faces; preserve angled vehicles
            // and ramps with their actual reconstructed orientation.
            vec3 axis_strength=abs(receiver_normal);
            float largest=max(axis_strength.x,max(axis_strength.y,axis_strength.z));
            if(largest>0.86) {
                if(axis_strength.x>=axis_strength.y && axis_strength.x>=axis_strength.z) {
                    receiver_normal=vec3(sign(receiver_normal.x),0.0,0.0);
                } else if(axis_strength.y>=axis_strength.z) {
                    receiver_normal=vec3(0.0,sign(receiver_normal.y),0.0);
                } else {
                    receiver_normal=vec3(0.0,0.0,sign(receiver_normal.z));
                }
            }
            // Push the receiver toward its visible surface normal before
            // projecting to the sun map. The shadow caster is inset inside
            // the painted wall; this keeps an oblique near-camera ray from
            // comparing against the wall's own slightly displaced depth.
            // Outside buildings and through doorways real occluders still
            // cast sun shadows, unlike globally disabling indoor shadows.
            float receiver_offset=abs(receiver_normal.y)<0.5 ? 0.055 : 0.014;
            vec3 shadow_world=world+receiver_normal*receiver_offset;
            vec2 light_uv=(vec2(dot(shadow_world,u_shadow_x),
                                dot(shadow_world,u_shadow_y))+u_shadow_offset) /
                            (2.0*u_shadow_radius)+0.5;
            float current_light_depth=(u_shadow_distance-
                dot(shadow_world,u_shadow_sun)-0.1) /
                (2.0*u_shadow_distance-0.1);
            float facing_sun=clamp(dot(receiver_normal,u_shadow_sun),0.0,1.0);
            // Shadow only directly sunlit receivers. A back-facing wall
            // has zero native direct sunlight, so darkening its ambient
            // creates false diagonal tile-by-tile self-shadow bands. The
            // real shadows on sunlit roads, cars and building faces remain.
            float direct_receiver=smoothstep(0.03,0.23,facing_sun);
            float slope_bias=1.0-abs(dot(receiver_normal,u_shadow_sun));
            float bias_near=0.00070+0.00135*slope_bias;
            float bias_far=bias_near+0.00035;
            float near_edge = min(min(light_uv.x,light_uv.y),
                                  min(1.0-light_uv.x,1.0-light_uv.y));
            if(current_light_depth > 0.0 && current_light_depth < 1.0) {
                // Bias depends on the actual receiver plane slope, not
                // just the vertical component of the sun direction.
                // Blend two independently rasterized solar depth maps
                // across a wide light-space band; never linearly blend
                // depth values (which would create bogus ghost casters).
                float near_weight = u_has_shadow_far>0.5 ?
                    smoothstep(0.075,0.175,near_edge) :
                    smoothstep(texel*2.0,max(0.075,texel*4.0),near_edge);
                float near_shadow=0.0;
                if(near_weight>0.001 && near_edge>texel*2.0) {
                    for(int iy=-1;iy<=1;++iy) {
                        for(int ix=-1;ix<=1;++ix) {
                            vec2 sample_uv=light_uv+vec2(float(ix),float(iy))*texel;
                            float blocker=texture2D(u_shadow,sample_uv).r;
                            near_shadow += (blocker<0.99999 &&
                                current_light_depth>blocker+bias_near) ? 1.0 : 0.0;
                        }
                    }
                    near_shadow/=9.0;
                }
                float shadow_factor=near_shadow*near_weight;
                if(u_has_shadow_far>0.5 && near_weight<0.999) {
                    vec2 far_uv = (vec2(dot(shadow_world,u_shadow_x),dot(shadow_world,u_shadow_y)) +
                                   u_shadow_far_offset) /
                                  (2.0*max(1.0,u_shadow_far_radius)) + 0.5;
                    float far_texel=1.0/max(1.0,u_shadow_far_resolution);
                    float far_edge=min(min(far_uv.x,far_uv.y),
                                       min(1.0-far_uv.x,1.0-far_uv.y));
                    if(far_edge>far_texel*2.0) {
                        float far_shadow=0.0;
                        for(int iy=-1;iy<=1;++iy) {
                            for(int ix=-1;ix<=1;++ix) {
                                vec2 uv=far_uv+vec2(float(ix),float(iy))*far_texel;
                                float blocker=texture2D(u_shadow_far,uv).r;
                                far_shadow += (blocker<0.99999 &&
                                    current_light_depth>blocker+bias_far) ? 1.0 : 0.0;
                            }
                        }
                        // Fade at the distant light volume edge, not to a
                        // clamped border texel that darkens the whole frame.
                        float far_fade=smoothstep(0.015,0.045,far_edge);
                        shadow_factor += far_shadow/9.0 *
                                         (1.0-near_weight)*far_fade;
                    }
                }
                // Restore distinct, properly DARK directional silhouettes
                // (car, street light, mailbox, buildings). The old 0.36
                // multiplier barely separated dark asphalt from shadow,
                // especially with bright indirect daylight and Photon.
                // Ultra uses the full dual-cascade PCF; lower settings retain
                // a gentler fallback. Never add tinted gray to shadows.
                float shadow_contrast = u_quality >= 2.5 ? 0.68 : 0.48;
                color *= 1.0 - shadow_factor * u_shadow_strength *
                         shadow_contrast * direct_receiver;
                // IMPORTANT: never ADD a fixed blue/grey colour to a
                // receiver simply because it is in shadow. On dark asphalt
                // the old +vec3(0.088,0.097,0.110) term made car shadows
                // BRIGHTER than the surrounding road, creating the large
                // flat grey patch seen under/behind vehicles. The native
                // scene's diffuse ambient already provides indirect sky
                // illumination for rooms and shadowed ground. This pass
                // exclusively removes occluded *directional* sunlight.
            }
        }
    }
    // Lightweight sun shafts: accumulate only sky depth samples toward the
    // visible sun. This is not shadow-map based volumetric lighting or GI.
    if(u_has_depth > 0.5 && u_quality >= 2.0 && u_sun_strength > 0.0) {
        vec2 towards_sun = u_sun_position - v_uv;
        float distance_sun = length(towards_sun);
        float visibility = 0.0;
        float weight = 0.0;
        for(int ray_step = 1; ray_step <= 16; ++ray_step) {
            if(u_quality < 2.5 && ray_step > 8) { break; }
            float step_size = float(ray_step) * (u_quality < 2.5 ? 0.075 : 0.042);
            vec2 sample_uv = v_uv + towards_sun * step_size;
            if(sample_uv.x < 0.0 || sample_uv.x > 1.0 ||
               sample_uv.y < 0.0 || sample_uv.y > 1.0) { continue; }
            float sky_depth = texture2D(u_depth, sample_uv).r;
            float shaft_weight = 1.0 - step_size;
            visibility += step(0.99998, sky_depth) * shaft_weight;
            weight += shaft_weight;
        }
        float focus = 1.0 / (1.0 + 14.0 * distance_sun * distance_sun);
        float lit = visibility / max(weight, 1.0);
        float local_depth = texture2D(u_depth, v_uv).r;
        float foreground = local_depth < 0.99998 ? 0.38 : 1.0;
        color += vec3(1.0, 0.77, 0.51) * u_sun_strength *
                 lit * focus * foreground * 0.24;
    }
    color = 1.0 - exp(-color * (u_style < 1.5 ? 1.25 : 1.10));
    if(u_style > 1.5) {
        color *= vec3(1.09, 1.025, 0.93);
    }
    color = pow(clamp(color, 0.0, 1.0), vec3(0.96));
    gl_FragColor = vec4(color, original.a);
}
)glsl";

fs::path root_path()
{
    char *base = SDL_GetBasePath();
    if( base ) {
        const fs::path root = fs::u8path( base ) / "shaderpacks";
        SDL_free( base );
        return root;
    }
    std::error_code error;
    return fs::current_path( error ) / "shaderpacks";
}

fs::path shader_file( const std::string &id )
{
    // Never interpret a raw UI-provided name as a path. Resolve only a scanned entry.
    std::error_code directory_error;
    for( const auto &item : fs::directory_iterator( root_path(), fs::directory_options::skip_permission_denied, directory_error ) ) {
        if( !item.is_directory() || item.path().filename().u8string() != id ) {
            continue;
        }
        for( const auto &candidate : { item.path() / "post.frag",
                 item.path() / "shaders" / "post.frag"
             } ) {
            if( fs::is_regular_file( candidate ) ) {
                return candidate;
            }
        }
    }
    return {};
}

void restore_choice()
{
    if( choice_loaded ) {
        return;
    }
    choice_loaded = true;
    choice = "off";
    std::ifstream quality_file( root_path() / ".quality", std::ios::binary );
    int saved_quality = 2;
    if( quality_file >> saved_quality && saved_quality >= 0 && saved_quality <= 3 ) {
        quality_level = saved_quality;
    }
    int stored_intensity = 145;
    std::ifstream strength_file( root_path() / ".intensity", std::ios::binary );
    if( strength_file >> stored_intensity && stored_intensity >= 50 &&
        stored_intensity <= 200 ) {
        mc_intensity = stored_intensity;
    }
    int stored_wet = 1;
    std::ifstream wet_file( root_path() / ".wet_ssr", std::ios::binary );
    if( wet_file >> stored_wet && ( stored_wet == 0 || stored_wet == 1 ) ) {
        wet_ssr = stored_wet != 0;
    }
    int stored_volume = 1;
    std::ifstream volume_file( root_path() / ".shadow_volume", std::ios::binary );
    if( volume_file >> stored_volume && ( stored_volume == 0 || stored_volume == 1 ) ) {
        shadow_volume = stored_volume != 0;
    }
    int stored_shadow = 1;
    std::ifstream shadow_file( root_path() / ".shadows", std::ios::binary );
    if( shadow_file >> stored_shadow && ( stored_shadow == 0 || stored_shadow == 1 ) ) {
        native_shadows_enabled = stored_shadow != 0;
    }
    std::ifstream file( root_path() / ".selected", std::ios::binary );
    std::string saved;
    if( file && std::getline( file, saved ) ) {
        // Windows Notepad and Python's text mode commonly write CRLF.
        // getline() removes LF only; reject embedded control characters,
        // but accept a single trailing CR from the platform line ending.
        if( !saved.empty() && saved.back() == '\r' ) {
            saved.pop_back();
        }
        if( !saved.empty() && saved.size() < 256 &&
            saved.find_first_of( "\r\n/" ) == std::string::npos &&
            saved.find( '\\' ) == std::string::npos ) {
            choice = saved;
        }
    }
}

GLuint compile_part( GLenum type, const std::string &source )
{
    GLuint shader = create_shader( type );
    const char *ptr = source.c_str();
    shader_source( shader, 1, &ptr, nullptr );
    compile_shader( shader );
    GLint ok = 0;
    get_shader_iv( shader, GL_COMPILE_STATUS, &ok );
    if( ok ) {
        return shader;
    }
    char message[768] = {};
    GLsizei size = 0;
    get_shader_log( shader, sizeof( message ) - 1, &size, message );
    report = "GLSL 编译失败：" + std::string( message, std::max( 0, static_cast<int>( size ) ) );
    delete_shader( shader );
    return 0;
}

void compile_current()
{
    compiled_choice = choice;
    if( program ) {
        delete_program( program );
        program = 0;
    }
    mc_bridge = mc_direct = false;
    mc_profile = {};
    if( choice == "off" || !get_procs() ) {
        return;
    }
    std::string fragment;
    if( choice == "cinematic" || choice == "warm" ) {
        fragment = builtin_source;
    } else {
        const fs::path path = root_path() / fs::u8path( choice );
        if( path.extension() == ".zip" &&
            fp3d_shader_archive::read_post( path, fragment ) ) {
            // The CDDA native ZIP format still takes priority.
        } else {
            const auto native = shader_file( choice );
            std::error_code error;
            if( !native.empty() && !error &&
                fs::file_size( native, error ) <= max_shader_bytes && !error ) {
                std::ifstream stream( native, std::ios::binary );
                std::ostringstream buffer;
                buffer << stream.rdbuf();
                fragment = buffer.str();
            } else {
                mc_profile = fp3d_mcshader::inspect( path );
                if( !mc_profile.recognized ) {
                    report = "光影包内容不受支持，已保留原版 3D。";
                    return;
                }
                mc_direct = mc_profile.direct_final;
                mc_bridge = !mc_direct;
                fragment = mc_direct ? mc_profile.final_fragment :
                           fp3d_mcshader::bridge_fragment( mc_profile, builtin_source );
            }
            if( fragment.empty() ) {
                report = "光影源码为空，已保留原版 3D。";
                return;
            }
        }
    }
    GLuint v = compile_part( GL_VERTEX_SHADER, vertex_source );
    if( !v ) {
        return;
    }
    GLuint f = compile_part( GL_FRAGMENT_SHADER, fragment );
    if( !f && mc_direct ) {
        // Some standalone Minecraft final passes reference extra uniforms
        // not provided by CDDA; retain a visible effect via safe adaptation.
        mc_direct = false;
        mc_bridge = true;
        fragment = fp3d_mcshader::bridge_fragment( mc_profile, builtin_source );
        f = compile_part( GL_FRAGMENT_SHADER, fragment );
    }
    if( !f ) {
        delete_shader( v );
        return;
    }
    program = create_program();
    attach_shader( program, v );
    attach_shader( program, f );
    link_program( program );
    delete_shader( v );
    delete_shader( f );
    GLint ok = 0;
    get_program_iv( program, GL_LINK_STATUS, &ok );
    if( !ok ) {
        char message[768] = {};
        GLsizei size = 0;
        get_program_log( program, sizeof( message ) - 1, &size, message );
        report = "GLSL 链接失败：" + std::string( message, std::max( 0, static_cast<int>( size ) ) );
        delete_program( program );
        program = 0;
    } else {
        report = mc_direct ? "Minecraft final.fsh 已编译，使用有限的直接最终 Pass。" :
                 mc_bridge ? mc_profile.label +
                             " 已通过 CDDA 光照/后处理适配启用（非原版 Iris 多 Pass）。" :
                 "当前光影已启用。";
    }
}

bool ensure_target( int w, int h )
{
    if( output_fbo && output_width == w && output_height == h ) {
        return true;
    }
    if( output_fbo ) {
        delete_fbo( 1, &output_fbo );
        glDeleteTextures( 1, &output_texture );
        output_fbo = output_texture = 0;
    }
    output_width = w;
    output_height = h;
    gen_fbo( 1, &output_fbo );
    bind_fbo( GL_FRAMEBUFFER, output_fbo );
    glGenTextures( 1, &output_texture );
    glBindTexture( GL_TEXTURE_2D, output_texture );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    attach_texture( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_texture, 0 );
    if( check_fbo( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
        report = "后处理帧缓冲不可用；已回退原版 3D。";
        delete_fbo( 1, &output_fbo );
        glDeleteTextures( 1, &output_texture );
        output_fbo = output_texture = 0;
        return false;
    }
    return true;
}
} // namespace

std::string directory()
{
    return root_path().u8string();
}

bool open_folder()
{
    std::error_code error;
    fs::create_directories( root_path(), error );
    if( error ) {
        report = "无法打开光影包文件夹：" + error.message();
        return false;
    }
#if defined(_WIN32)
    const std::wstring folder = root_path().wstring();
    const auto launched = reinterpret_cast<std::intptr_t>(
                              ShellExecuteW( nullptr, L"open", folder.c_str(),
                                             nullptr, nullptr, SW_SHOWNORMAL ) );
    if( launched <= 32 ) {
        report = "Windows 无法打开光影包文件夹；请复制文件夹路径手动打开。";
        return false;
    }
    report = "已在 Windows 资源管理器中打开光影包文件夹。";
    return true;
#else
    report = "当前系统暂不支持直接打开目录；请复制路径。";
    return false;
#endif
}

std::vector<entry> scan()
{
    restore_choice();
    std::vector<entry> result{
        { "off", "关闭光影", "使用原版 3D 画面", true },
        { "cinematic", "内置 · 柔光", "柔和 Bloom 与色调映射", true },
        { "warm", "内置 · 暖色电影", "Bloom、暖色调与色调映射", true }
    };
    std::error_code error;
    fs::create_directories( root_path(), error );
    if( error ) {
        report = "无法创建 shaderpacks 文件夹：" + error.message();
        return result;
    }
    for( fs::directory_iterator it( root_path(), fs::directory_options::skip_permission_denied, error ),
         end; !error && it != end; it.increment( error ) ) {
        const auto &path = it->path();
        if( it->is_directory( error ) ) {
            const auto id = path.filename().u8string();
            if( id == "off" || id == "cinematic" || id == "warm" ) {
                continue;
            }
            const bool native = fs::is_regular_file( path / "post.frag" ) ||
                                fs::is_regular_file( path / "shaders" / "post.frag" );
            const auto mc = native ? fp3d_mcshader::profile{} : fp3d_mcshader::inspect( path );
            result.push_back( { id, id, native ? "CDDA GLSL 后处理包" :
                                mc.recognized ? mc.note :
                                "无可识别光影入口", native || mc.recognized } );
        } else if( path.extension() == ".zip" ) {
            const auto id = path.filename().u8string();
            std::string source;
            const bool native = fp3d_shader_archive::read_post( path, source );
            const auto mc = native ? fp3d_mcshader::profile{} : fp3d_mcshader::inspect( path );
            result.push_back( { id, id, native ? "CDDA ZIP 后处理光影包" :
                                mc.recognized ? mc.note :
                                "无法识别 CDDA/Minecraft 光影入口", native || mc.recognized } );
        }
    }
    if( error ) {
        report = "扫描光影文件夹时发生错误：" + error.message();
    }
    return result;
}

void request_reload()
{
    restore_choice();
    compiled_choice.clear();
    report = "已刷新光影包；返回 3D 世界后重新编译当前光影。";
}

std::string selected()
{
    restore_choice();
    return choice;
}

bool choose( const std::string &id )
{
    const auto entries = scan();
    const auto found = std::find_if( entries.begin(), entries.end(), [&]( const entry &item ) {
        return item.id == id && item.compatible;
    } );
    if( found == entries.end() ) {
        report = "该光影包格式暂不支持。";
        return false;
    }
    choice = id;
    compiled_choice.clear();
    report = id == "off" ? "光影已关闭，返回游戏后恢复原版 3D。" :
             "已选择光影；返回 3D 世界后编译并应用。";
    std::ofstream file( root_path() / ".selected", std::ios::binary | std::ios::trunc );
    if( file ) {
        file << id << '\n';
    } else {
        report = "光影已选择，但偏好无法写入文件。";
    }
    return true;
}

std::string status()
{
    return report;
}

bool usable()
{
    restore_choice();
    return choice != "off" && program != 0;
}

bool supports_native_lighting()
{
    restore_choice();
    return choice == "warm" || choice == "cinematic" ||
           ( mc_bridge && mc_profile.shadows );
}

bool shadows_enabled()
{
    restore_choice();
    return native_shadows_enabled;
}

void set_shadows_enabled( bool enabled )
{
    restore_choice();
    native_shadows_enabled = enabled;
    std::ofstream file( root_path() / ".shadows", std::ios::trunc );
    if( file ) { file << static_cast<int>( enabled ) << '\n'; }
    else { report = "太阳阴影选项已改变，但无法保存到游戏目录。"; }
}

bool wet_ssr_enabled()
{
    restore_choice();
    return wet_ssr;
}

void set_wet_ssr_enabled( bool enabled )
{
    restore_choice();
    wet_ssr = enabled;
    std::ofstream file( root_path() / ".wet_ssr", std::ios::trunc );
    if( file ) { file << static_cast<int>( enabled ) << '\n'; }
    else { report = "湿地反射已更改，但无法保存选项。"; }
}

bool shadow_volume_enabled()
{
    restore_choice();
    return shadow_volume;
}

void set_shadow_volume_enabled( bool enabled )
{
    restore_choice();
    shadow_volume = enabled;
    std::ofstream file( root_path() / ".shadow_volume", std::ios::trunc );
    if( file ) { file << static_cast<int>( enabled ) << '\n'; }
    else { report = "体积太阳光已更改，但无法保存选项。"; }
}

bool prepare()
{
    restore_choice();
    if( choice == "off" || !get_procs() ) {
        return false;
    }
    if( choice != compiled_choice ) {
        compile_current();
    }
    return program != 0;
}

int mc_intensity_percent()
{
    restore_choice();
    return mc_intensity;
}

void set_mc_intensity_percent( int percent )
{
    restore_choice();
    mc_intensity = std::clamp( percent, 50, 200 );
    std::ofstream file( root_path() / ".intensity", std::ios::trunc );
    if( file ) {
        file << mc_intensity << '\n';
    } else {
        report = "光影视觉强度已更新，但无法保存设置。";
    }
}

float mc_visual_strength()
{
    restore_choice();
    return mc_bridge && program != 0 ? mc_intensity / 100.0f : 0.0f;
}

int quality()
{
    restore_choice();
    return quality_level;
}

void set_quality( int level )
{
    restore_choice();
    quality_level = std::clamp( level, 0, 3 );
    std::ofstream file( root_path() / ".quality", std::ios::trunc );
    if( file ) { file << quality_level << '\n'; }
    else { report = "质量已改变，但无法保存到游戏目录。"; }
}

GLuint apply( GLuint scene_color, int width, int height, environment values )
{
    restore_choice();
    if( choice == "off" ) {
        last_processed_fbo = 0;
        if( proc_loaded ) {
            if( program ) { delete_program( program ); }
            if( output_fbo ) { delete_fbo( 1, &output_fbo ); }
        }
        if( output_texture ) { glDeleteTextures( 1, &output_texture ); }
        program = output_fbo = output_texture = 0;
        output_width = output_height = 0;
        compiled_choice.clear();
        mc_bridge = mc_direct = false;
        mc_profile = {};
        return scene_color;
    }
    last_processed_fbo = 0;
    if( !prepare() ) {
        return scene_color;
    }
    if( !ensure_target( width, height ) ) {
        return scene_color;
    }
    bind_fbo( GL_FRAMEBUFFER, output_fbo );
    glViewport( 0, 0, width, height );
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_BLEND );
    glDisable( GL_ALPHA_TEST );
    glDisable( GL_LIGHTING );
    glDisable( GL_FOG );
    glDisable( GL_SCISSOR_TEST );
    glDisable( GL_CULL_FACE );
    glEnable( GL_TEXTURE_2D );
    active_texture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, scene_color );
    if( values.depth ) {
        active_texture( GL_TEXTURE1 );
        glBindTexture( GL_TEXTURE_2D, values.depth );
    }
    if( values.shadow ) {
        active_texture( GL_TEXTURE2 );
        glBindTexture( GL_TEXTURE_2D, values.shadow );
    }
    if( values.reflection_mask ) {
        active_texture( GL_TEXTURE3 );
        glBindTexture( GL_TEXTURE_2D, values.reflection_mask );
    }
    if( values.shadow_far ) {
        active_texture( GL_TEXTURE4 );
        glBindTexture( GL_TEXTURE_2D, values.shadow_far );
    }
    active_texture( GL_TEXTURE0 );
    use_program( program );
    const auto set_int = [&]( const char *name, int value ) {
        const GLint loc = uniform_location( program, name );
        if( loc >= 0 ) { uniform_1i( loc, value ); }
    };
    const auto set_float = [&]( const char *name, float value ) {
        const GLint loc = uniform_location( program, name );
        if( loc >= 0 ) { uniform_1f( loc, value ); }
    };
    set_int( "u_scene", 0 );
    set_int( "u_depth", 1 );
    set_int( "u_shadow", 2 );
    set_int( "u_shadow_far", 4 );
    set_int( "u_mc_reflect_mask", 3 );
    set_int( "colortex0", 0 );
    set_int( "gcolor", 0 );
    set_int( "gdepthtex", 1 );
    set_int( "depthtex0", 1 );
    set_int( "shadowtex0", 2 );
    set_float( "u_mc_saturation", mc_profile.saturation );
    set_float( "u_mc_warmness", mc_profile.warmness );
    set_float( "u_mc_glow", mc_profile.glow );
    set_float( "u_mc_volumetric", mc_profile.volumetric ? 1.0f : 0.0f );
    set_float( "u_mc_clouds", mc_profile.clouds ? 1.0f : 0.0f );
    set_float( "u_mc_ao", mc_profile.ao ? 1.0f : 0.0f );
    set_float( "u_mc_intensity", mc_intensity / 100.0f );
    set_float( "u_camera_height", values.camera_height );
    set_float( "u_rain_strength", values.rain_strength );
    set_float( "u_mc_ssr_enabled", mc_bridge && wet_ssr ? 1.0f : 0.0f );
    set_float( "u_has_mc_reflect_mask", values.reflection_mask ? 1.0f : 0.0f );
    set_float( "u_mc_volume_enabled", mc_bridge && shadow_volume ? 1.0f : 0.0f );
    set_float( "u_has_depth", values.depth ? 1.0f : 0.0f );
    set_float( "u_has_shadow", values.shadow ? 1.0f : 0.0f );
    set_float( "u_has_shadow_far", values.shadow_far ? 1.0f : 0.0f );
    set_float( "u_shadow_far_radius", values.shadow_far_radius );
    set_float( "u_shadow_far_resolution",
               static_cast<float>( values.shadow_far_resolution ) );
    const GLint near_snap = uniform_location( program, "u_shadow_offset" );
    if( near_snap >= 0 ) {
        uniform_2f( near_snap, values.shadow_snap_offset[0],
                    values.shadow_snap_offset[1] );
    }
    const GLint far_snap = uniform_location( program, "u_shadow_far_offset" );
    if( far_snap >= 0 ) {
        uniform_2f( far_snap, values.shadow_far_snap_offset[0],
                    values.shadow_far_snap_offset[1] );
    }
    set_float( "u_shadow_radius", values.shadow_radius );
    set_float( "u_shadow_distance", values.shadow_distance );
    set_float( "u_shadow_strength", values.shadow_strength );
    set_float( "u_shadow_resolution", static_cast<float>( values.shadow_resolution ) );
    set_float( "u_aspect", values.aspect );
    const GLint view_angles = uniform_location( program, "u_yaw_pitch" );
    if( view_angles >= 0 ) { uniform_2f( view_angles, values.yaw, values.pitch ); }
    const GLint view_offset = uniform_location( program, "u_camera_offset" );
    if( view_offset >= 0 ) {
        uniform_2f( view_offset, values.camera_offset_x, values.camera_offset_z );
    }
    const auto set_vec3 = [&]( const char *name, const float (&xyz)[3] ) {
        const GLint loc = uniform_location( program, name );
        if( loc >= 0 ) { uniform_3f( loc, xyz[0], xyz[1], xyz[2] ); }
    };
    set_vec3( "u_shadow_x", values.shadow_x );
    set_vec3( "u_shadow_y", values.shadow_y );
    set_vec3( "u_shadow_sun", values.shadow_sun );
    set_float( "u_quality", static_cast<float>( quality_level ) );
    set_float( "u_daylight", values.daylight );
    set_float( "u_clouds", values.clouds );
    set_float( "u_fog_end", values.fog_end );
    set_float( "u_outdoors", values.outdoors ? 1.0f : 0.0f );
    set_float( "u_sky_visible", values.sky_visible ? 1.0f : 0.0f );
    set_float( "u_time", SDL_GetTicks() / 1000.0f );
    set_float( "u_style", choice == "warm" ? 2.0f : 1.0f );
    set_float( "u_sun_strength", values.sun_strength );
    const GLint sun_pos = uniform_location( program, "u_sun_position" );
    if( sun_pos >= 0 ) {
        uniform_2f( sun_pos, values.sun_u, values.sun_v );
    }
    const GLint texel = uniform_location( program, "u_texel" );
    if( texel >= 0 ) {
        uniform_2f( texel, 1.0f / width, 1.0f / height );
    }
    // Vertex attributes intentionally use compatibility-profile builtins.
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    glColor4f( 1, 1, 1, 1 );
    glBegin( GL_QUADS );
    glTexCoord2f( 0, 0 ); glVertex2f( -1, -1 );
    glTexCoord2f( 1, 0 ); glVertex2f( 1, -1 );
    glTexCoord2f( 1, 1 ); glVertex2f( 1, 1 );
    glTexCoord2f( 0, 1 ); glVertex2f( -1, 1 );
    glEnd();
    use_program( 0 );
    if( values.depth ) {
        active_texture( GL_TEXTURE1 );
        glBindTexture( GL_TEXTURE_2D, 0 );
    }
    if( values.shadow ) {
        active_texture( GL_TEXTURE2 );
        glBindTexture( GL_TEXTURE_2D, 0 );
    }
    if( values.reflection_mask ) {
        active_texture( GL_TEXTURE3 );
        glBindTexture( GL_TEXTURE_2D, 0 );
    }
    if( values.shadow_far ) {
        active_texture( GL_TEXTURE4 );
        glBindTexture( GL_TEXTURE_2D, 0 );
    }
    active_texture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, 0 );
    last_processed_fbo = output_fbo;
    return output_texture;
}

GLuint processed_framebuffer()
{
    return last_processed_fbo;
}

void release()
{
    last_processed_fbo = 0;
    if( proc_loaded ) {
        if( program ) { delete_program( program ); }
        if( output_fbo ) { delete_fbo( 1, &output_fbo ); }
    }
    if( output_texture ) {
        glDeleteTextures( 1, &output_texture );
    }
    program = output_fbo = output_texture = 0;
    output_width = output_height = 0;
    compiled_choice.clear();
    proc_loaded = false;
}
} // namespace fp3d_shaderpacks
#endif
