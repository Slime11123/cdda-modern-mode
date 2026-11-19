#include "fp3d_mcshader.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include "fp3d_shader_archive.h"

namespace fp3d_mcshader
{
namespace
{
namespace fs = std::filesystem;
bool read_file( const fs::path &file, std::string &source )
{
    std::error_code error;
    if( !fs::is_regular_file( file, error ) || error ||
        fs::file_size( file, error ) > 256 * 1024 || error ) {
        return false;
    }
    std::ifstream stream( file, std::ios::binary );
    if( !stream ) { return false; }
    source.assign( ( std::istreambuf_iterator<char>( stream ) ),
                    std::istreambuf_iterator<char>() );
    return !source.empty() && source.find( '\0' ) == std::string::npos;
}
bool read( const fs::path &file, const std::string &name, std::string &source )
{
    if( file.extension() == ".zip" ) {
        return fp3d_shader_archive::read_mc_entry( file, name, source );
    }
    return read_file( file / fs::u8path( name ), source );
}
bool defined( const std::string &source, const std::string &name, bool fallback )
{
    if( source.empty() ) { return fallback; }
    std::istringstream stream( source );
    std::string line;
    const std::string name_prefix = "#define " + name;
    bool found = false;
    while( std::getline( stream, line ) ) {
        const auto first = line.find_first_not_of( " \t" );
        if( first == std::string::npos ) { continue; }
        const std::string clean = line.substr( first );
        if( clean.compare( 0, name_prefix.size(), name_prefix ) == 0 &&
            ( clean.size() == name_prefix.size() ||
              std::isspace( static_cast<unsigned char>( clean[name_prefix.size()] ) ) ) ) {
            found = true;
        }
    }
    return found ? true : fallback;
}
void replace_once( std::string &source, const std::string &old_text,
                   const std::string &replacement )
{
    const auto at = source.find( old_text );
    if( at != std::string::npos ) {
        source.replace( at, old_text.size(), replacement );
    }
}
float setting_number( const std::string &settings, const char *name, float fallback,
                      float minimum, float maximum )
{
    std::istringstream lines( settings );
    std::string line, directive, key;
    float value;
    while( std::getline( lines, line ) ) {
        std::istringstream tokens( line );
        if( tokens >> directive >> key >> value && directive == "#define" && key == name ) {
            return std::clamp( value, minimum, maximum );
        }
    }
    return fallback;
}

std::string photon_water_source( const fs::path &file, const std::string &settings )
{
    std::string waves;
    if( !read( file, "shaders/include/surface/water_normal.glsl", waves ) ) { return {}; }
    const auto begin = waves.find( "float gerstner_wave(" );
    const auto end = waves.find( "vec3 get_water_normal(" );
    if( begin == std::string::npos || end == std::string::npos || end <= begin ) { return {}; }
    waves = waves.substr( begin, end - begin );
    // The installed pack supplies the actual Gerstner/setup/height functions.
    // Adapt only GLSL 4 syntax and Iris's noise/time inputs to the GL2 bridge.
    waves = std::regex_replace( waves, std::regex( "float\\[WATER_WAVE_ITERATIONS\\] wave_noise" ),
                               "float wave_noise[WATER_WAVE_ITERATIONS]" );
    waves = std::regex_replace( waves, std::regex( "\\buint\\b" ), "int" );
    waves = std::regex_replace( waves, std::regex( "\\b([0-9]+)u\\b" ), "$1" );
    waves = std::regex_replace( waves, std::regex( "texture\\(\\s*noisetex\\s*," ),
                               "mc_photon_noise(" );
    std::string definitions = "\n#define CDDA_PACK_WATER\n";
    for( const auto *key : { "WATER_WAVE_ITERATIONS", "WATER_WAVE_FREQUENCY",
                            "WATER_WAVE_PERSISTENCE", "WATER_WAVE_LACUNARITY",
                            "WATER_WAVE_SPEED_STILL", "WATER_WAVE_SPEED_FLOWING" } ) {
        const bool iterations = std::string( key ) == "WATER_WAVE_ITERATIONS";
        const float value = setting_number( settings, key, iterations ? 3 : 1,
                                           iterations ? 1 : 0, iterations ? 8 : 1.9f );
        definitions += "#define " + std::string( key ) + " " +
                       ( iterations ? std::to_string( static_cast<int>( value ) ) :
                         std::to_string( value ) ) + "\n";
    }
    if( defined( settings, "WATER_WAVES_HEIGHT_VARIATION", false ) ) {
        definitions += "#define WATER_WAVES_HEIGHT_VARIATION\n";
    }
    definitions += R"glsl(
#define tau 6.28318530718
#define degree 0.01745329252
#define golden_angle 2.39996322973
#define frameTimeCounter u_time
#define sqr(x) ((x)*(x))
vec4 mc_photon_noise(vec2 coord) {
    return vec4(mc_noise(coord*256.0));
}
)glsl";
    std::string cleanup;
    for( const auto *key : { "tau", "degree", "golden_angle", "frameTimeCounter", "sqr" } ) {
        cleanup += "\n#undef " + std::string( key );
    }
    return definitions + waves + cleanup + "\n";
}
bool self_contained_final( const std::string &source )
{
    if( source.empty() || source.size() > 64 * 1024 ||
        source.find( "#include" ) != std::string::npos ||
        source.find( "void main(" ) == std::string::npos ||
        source.find( "#version 120" ) == std::string::npos ) {
        return false;
    }
    // A true Minecraft standalone final.fsh can be attempted only if it uses
    // the framebuffer aliases this backend can bind. Multi-pass attachments,
    // textures, compute shaders and MC vertex attributes are not emulated.
    for( const char *forbidden : { "colortex1", "colortex2", "colortex3", "colortex4",
                                    "colortex5", "colortex6", "colortex7",
                                    "noisetex", "shadowcolor", "gbufferProjection",
                                    "sampler3D", "sampler2DArray",
                                    "gl_FragData[1]", "viewInverse", "previousCameraPosition" } ) {
        if( source.find( forbidden ) != std::string::npos ) {
            return false;
        }
    }
    return true;
}
} // namespace

profile inspect( const fs::path &file )
{
    profile p;
    std::string properties, settings, final;
    const bool props = read( file, "shaders/shaders.properties", properties );
    const bool config = read( file, "shaders/settings.glsl", settings );
    const bool final_source = read( file, "shaders/world0/final.fsh", final ) ||
                              read( file, "shaders/final.fsh", final );
    if( !props && !config && !final_source ) {
        return p;
    }
    p.recognized = true;
    std::string id = file.filename().u8string();
    std::transform( id.begin(), id.end(), id.begin(), []( unsigned char c ) {
        return static_cast<char>( std::tolower( c ) );
    } );
    p.photon = id.find( "photon" ) != std::string::npos ||
               properties.find( "PHOTON" ) != std::string::npos;
    p.label = p.photon ? "Photon" : "Minecraft/OptiFine";
    p.direct_final = final_source && self_contained_final( final );
    if( p.direct_final ) {
        p.final_fragment = std::move( final );
        p.note = "Minecraft GLSL 1.20 独立 final.fsh：尝试原代码执行，失败时回退适配。";
    } else {
        p.note = p.photon ? "Photon 视觉适配：天空/光照/阴影/Bloom；非原版 Iris 多 Pass。" :
                           "Minecraft 光影视觉适配；不执行原版多 Pass/G-buffer/材质指令。";
    }
    p.shadows = defined( settings, "SHADOW", true );
    p.bloom = defined( settings, "BLOOM", true );
    p.volumetric = defined( settings, "VOLUMETRIC_FOG", true ) ||
                   defined( settings, "VOLUMETRIC_LIGHT", false );
    p.clouds = defined( settings, "CLOUDS", true );
    p.ao = defined( settings, "SHADER_AO", true );
    p.reflections = defined( settings, "SSR", false );
    p.saturation = p.photon ? 1.22f : 1.13f;
    p.warmness = p.photon ? 0.16f : 0.07f;
    p.glow = p.bloom ? ( p.photon ? 1.05f : .82f ) : 0.0f;
    if( p.photon ) {
        p.water_source = photon_water_source( file, settings );
        p.water_wave_strength = defined( settings, "WATER_WAVES", false ) ?
                               setting_number( settings, "WATER_WAVE_STRENGTH", 1, 0, 2 ) : 0;
        p.water_texture = settings.find( "#define WATER_TEXTURE WATER_TEXTURE_VANILLA" ) !=
                          std::string::npos ? 1.0f : 0.0f;
        p.water_absorption[0] = setting_number( settings, "WATER_ABSORPTION_R", .39f, 0, 1 );
        p.water_absorption[1] = setting_number( settings, "WATER_ABSORPTION_G", .14f, 0, 1 );
        p.water_absorption[2] = setting_number( settings, "WATER_ABSORPTION_B", .07f, 0, 1 );
        if( !p.water_source.empty() ) {
            p.note = "Photon 视觉适配 + 包内水波函数；反射/水色适配 CDDA，非完整 Iris 多 Pass。";
        }
    }
    return p;
}

std::string bridge_fragment( const profile &pack, const std::string &builtin )
{
    // Re-use the native CDDA screen-space renderer and supply shader-pack
    // appearance parameters and optional installed-pack wave functions.
    // Minecraft Iris materials and multi-pass G-buffers are not emulated.
    std::string source = builtin;
    replace_once( source, "uniform float u_style;\n",
                  "uniform float u_style;\n"
                  "uniform float u_mc_saturation;\n"
                  "uniform float u_mc_warmness;\n"
                  "uniform float u_mc_glow;\n"
                  "uniform float u_mc_volumetric;\n"
                  "uniform float u_mc_clouds;\n"
                  "uniform float u_mc_intensity;\n"
                  "uniform float u_mc_ao;\n"
                  "uniform float u_rain_strength;\n"
                  "uniform float u_mc_ssr_enabled;\n"
                  "uniform sampler2D u_mc_reflect_mask;\n"
                  "uniform float u_has_mc_reflect_mask;\n"
                  "uniform float u_mc_volume_enabled;\n" );
    // Reconstruct a sky ray from the native 120-degree camera view.
    // Clouds track world orientation rather than sticking to the screen.
    replace_once( source, "varying vec2 v_uv;\nvoid main() {",
        "varying vec2 v_uv;\n"
        "float mc_hash(vec2 x) {\n"
        "    return fract(sin(dot(x,vec2(127.1,311.7)))*43758.5453);\n"
        "}\n"
        "float mc_noise(vec2 p) {\n"
        "    vec2 i=floor(p), f=fract(p);\n"
        "    f=f*f*(3.0-2.0*f);\n"
        "    return mix(mix(mc_hash(i),mc_hash(i+vec2(1.0,0.0)),f.x),\n"
        "               mix(mc_hash(i+vec2(0.0,1.0)),mc_hash(i+vec2(1.0,1.0)),f.x),f.y);\n"
        "}\n"
        "void main() {" );
    replace_once( source, "    vec3 color = original.rgb + glow * (u_style < 1.5 ? 0.75 : 1.1);",
                  "    vec3 color = original.rgb + glow * u_mc_glow * "
                  "(0.55 + 0.55 * u_mc_intensity);" );
    replace_once( source, "glow += max(s - vec3(0.65), vec3(0.0));",
                  "glow += max(s - vec3(0.59 - 0.035*u_mc_intensity), vec3(0.0));" );

    // True scene-depth world reconstruction shared by the reflection and
    // shadow-volume raymarch. No guessed surface position or generated object.
    const std::string raymarch_helpers = R"glsl(
float mc_linear_depth(float z) {
    const float n=0.2;
    const float f=1536.0;
    return n*f / max(0.001, f-z*(f-n));
}
vec3 mc_view_at(vec2 uv, float depth) {
    float d=mc_linear_depth(depth);
    return vec3((uv.x*2.0-1.0)*1.7320508*d,
                (uv.y*2.0-1.0)*1.7320508*d/max(0.25,u_aspect),-d);
}
vec3 mc_world_at(vec3 v) {
    float cy=cos(u_yaw_pitch.x),sy=sin(u_yaw_pitch.x);
    float cp=cos(u_yaw_pitch.y),sp=sin(u_yaw_pitch.y);
    float vy=cp*v.y+sp*v.z;
    float vz=-sp*v.y+cp*v.z;
    return vec3(cy*v.x-sy*vz+u_camera_offset.x,
                vy+u_camera_height,sy*v.x+cy*vz+u_camera_offset.y);
}
vec2 mc_project_view(vec3 p) {
    vec2 scale=vec2(1.7320508,
                    1.7320508/max(u_aspect,0.25));
    return vec2(0.5)+p.xy/(-p.z*scale)*0.5;
}
float mc_sun_occlusion(vec3 pos) {
    float sd=(u_shadow_distance-dot(pos,u_shadow_sun)-0.1) /
             max(0.1,2.0*u_shadow_distance-0.1);
    if(sd<0.002 || sd>0.998) { return 0.0; }
    vec2 projected=vec2(dot(pos,u_shadow_x),dot(pos,u_shadow_y));
    vec2 near_uv=(projected+u_shadow_offset) /
                 (2.0*max(1.0,u_shadow_radius))+0.5;
    float near_edge=min(min(near_uv.x,near_uv.y),
                        min(1.0-near_uv.x,1.0-near_uv.y));
    float near_weight=u_has_shadow_far>0.5 ?
        smoothstep(0.075,0.175,near_edge) :
        smoothstep(0.01,0.04,near_edge);
    float near_vis=0.0;
    if(near_weight>0.001 && near_edge>0.006) {
        float depth=texture2D(u_shadow,near_uv).r;
        near_vis=(depth>0.99999 || depth+0.0025>=sd) ? 1.0 : 0.0;
    }
    if(u_has_shadow_far<0.5 || near_weight>=0.999) {
        return near_vis*near_weight;
    }
    vec2 far_uv=(projected+u_shadow_far_offset) /
                (2.0*max(1.0,u_shadow_far_radius))+0.5;
    float far_edge=min(min(far_uv.x,far_uv.y),
                       min(1.0-far_uv.x,1.0-far_uv.y));
    float far_vis=0.0;
    if(far_edge>0.008) {
        float depth=texture2D(u_shadow_far,far_uv).r;
        far_vis=(depth>0.99999 || depth+0.0025>=sd) ?
            smoothstep(0.013,0.043,far_edge) : 0.0;
    }
    return mix(far_vis,near_vis,near_weight);
}
vec3 mc_sun_volume(vec2 uv,float depth) {
    // This is actual shadow-map occlusion integrated along the camera ray,
    // unlike a radial screen-space sunshaft. Only visible scene geometry
    // and first-person view direction are sampled.
    vec3 view=mc_view_at(uv,depth);
    float raylen=min(length(view),32.0);
    if(depth>=0.99998) {
        view=mc_view_at(uv,0.9999);
        raylen=32.0;
    }
    if(raylen<1.25) { return vec3(0.0); }
    vec3 dir=normalize(view);
    vec3 origin=vec3(u_camera_offset.x,u_camera_height,u_camera_offset.y);
    vec3 farpoint=mc_world_at(dir);
    vec3 wray=normalize(farpoint-origin);
    float phase=0.20+0.80*pow(max(0.0,dot(-wray,u_shadow_sun)),6.0);
    float light=0.0;
    for(int i=0;i<8;i++) {
        float t=(float(i)+0.5)/8.0;
        vec3 sample_pos=origin+wray*(raylen*t);
        float visibility=mc_sun_occlusion(sample_pos);
        light+=visibility*exp(-raylen*t*0.033);
    }
    float fog=0.60+0.55*clamp(u_clouds,0.0,1.0)+
              0.35*u_rain_strength;
    float integration=(light/8.0)*(1.0-exp(-raylen*0.045));
    // A sunshaft is light SCATTERING in air, not a flat bright decal on
    // asphalt/vehicle material. The earlier unbounded additive term washed
    // dark roads into wide light-gray regions near nearby sun occluders.
    // For opaque pixels attenuate short camera-to-surface paths; real sky
    // rays and distant atmospheric beams still receive their full light.
    float surface_attenuation=depth<0.99998 ?
        mix(0.32,0.68,smoothstep(8.0,38.0,raylen)) : 1.0;
    return vec3(1.0,0.82,0.65)*integration*phase*fog*
           u_shadow_strength*(0.42+0.20*u_mc_intensity)*
           surface_attenuation;
}
vec4 mc_reflection_tap(vec2 uv,float center_depth,float tolerance,float weight) {
    // Roughness filtering must not smear a foreground car or window frame
    // into a reflected background. Missing/sky taps contribute no colour.
    float edge=min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
    if(edge<=0.0) { return vec4(0.0); }
    if(texture2D(u_mc_reflect_mask,uv).a>0.1) { return vec4(0.0); }
    float depth=texture2D(u_depth,uv).r;
    if(depth>=0.99998) { return vec4(0.0); }
    float agreement=1.0-smoothstep(tolerance,2.0*tolerance,
                                  abs(mc_linear_depth(depth)-center_depth));
    float accepted=weight*agreement;
    return vec4(texture2D(u_scene,uv).rgb*accepted,accepted);
}
vec3 mc_wet_reflection(vec2 uv, float d, vec3 original_color, vec4 material) {
    if(d>=0.9999 || d<=0.0 || material.r<=0.02) {
        return original_color;
    }
    // The native-visible mask only marks horizontal, real terrain floors.
    // Reconstructing a normal from adjacent scene depth samples near cars,
    // tile boundaries or sprites used to create large zero-reflection holes.
    vec3 p=mc_view_at(uv,d);
    float roughness=clamp(material.g>0.01 ? material.g : 0.53,0.08,0.95);
    float bump=clamp(material.b,0.0,0.12);
    // Procedural micro-normal for the classified real water/ground only.
    // Use world coordinates so turning the camera doesn't move the pattern.
    // Not a full tileset normal map and it never bends the geometry depth.
    vec3 world=mc_world_at(p);
    vec3 world_incident=normalize(world-vec3(u_camera_offset.x,
                                          u_camera_height,u_camera_offset.y));
    float water_footprint=max(length(dFdx(world.xz)),length(dFdy(world.xz)));
    bool water=material.a>0.1;
    float phase=world.x*1.55+world.z*0.89+u_time*(roughness<0.25?0.5:0.0);
    float phase2=world.z*1.41-world.x*0.37+u_time*(roughness<0.25?0.32:0.0);
    vec3 nworld=normalize(vec3(bump*cos(phase),1.0,bump*cos(phase2)));
#ifdef CDDA_PACK_WATER
    if(water) {
        vec2 wave_dir;
        mat2 wave_rot;
        float wave_time;
        water_waves_setup(false,vec2(0.0),wave_dir,wave_rot,wave_time);
        // A distant pixel spans multiple ripples. Widen the normal sample
        // and fade unresolved slopes instead of aliasing them into black dots.
        float wave_step=clamp(water_footprint,0.1,1.0);
        float h0=get_water_height(world.xz,wave_dir,wave_rot,wave_time);
        float hx=get_water_height(world.xz+vec2(wave_step,0.0),wave_dir,wave_rot,wave_time);
        float hz=get_water_height(world.xz+vec2(0.0,wave_step),wave_dir,wave_rot,wave_time);
        float influence=(0.04+0.15*u_rain_strength)*CDDA_WATER_STRENGTH;
        influence*=smoothstep(0.0,0.15,abs(world_incident.y));
        influence/=1.0+4.0*water_footprint*water_footprint;
        nworld=normalize(vec3((hx-h0)*influence,wave_step,(hz-h0)*influence));
    }
#endif
    float cy=cos(u_yaw_pitch.x),sy=sin(u_yaw_pitch.x);
    float cp=cos(u_yaw_pitch.y),sp=sin(u_yaw_pitch.y);
    float intermediate=-sy*nworld.x+cy*nworld.z;
    vec3 normal=normalize(vec3(cy*nworld.x+sy*nworld.z,
                               cp*nworld.y-sp*intermediate,
                               sp*nworld.y+cp*intermediate));
    vec3 incident=normalize(p);
    float up_angle=clamp(-dot(normal,incident),0.0,1.0);
    // At grazing angles a wave normal can point away from the eye. Water
    // still owns that pixel; returning the scene here restored vanilla art
    // in large moving patches between the pack-coloured regions.
    if(up_angle<=0.015 && !water) { return original_color; }
    float grazing=1.0-up_angle;
    float wet=clamp(material.r,0.0,1.0);
    float range=mc_linear_depth(d);
    // Continuous weak wet sheen across EVERY classified native road tile,
    // including pixels where limited screen-space ray casting cannot hit.
    vec3 atmosphere=mix(vec3(0.12,0.14,0.17),vec3(0.35,0.38,0.42),
                        clamp(u_daylight,0.0,1.0));
    atmosphere=mix(atmosphere,vec3(0.31,0.33,0.35),
                   clamp(u_clouds,0.0,1.0)*0.68);
    // Schlick Fresnel for a dielectric wet surface (F0 near 0.04);
    // rough asphalt gives a weaker, broader response than smooth water.
    float fresnel=0.04+0.96*pow(grazing,5.0);
    float film=wet*(0.025+0.070*fresnel)*(1.0-0.40*roughness);
    vec3 result=mix(original_color,atmosphere,film);
#ifdef CDDA_PACK_WATER
    if(water) {
        // Photon defaults to clean water rather than the vanilla tile albedo.
        // Optical depth comes from CDDA shallow/deep classes, not a bed buffer.
        float optical_depth=mix(0.65,4.5,clamp(material.a,0.0,1.0));
        vec3 transmission=exp(-CDDA_WATER_ABSORPTION*optical_depth*2.0);
        float illumination=0.12+0.65*clamp(u_daylight,0.0,1.0);
        if(u_has_shadow>0.5) {
            illumination*=mix(0.45,1.0,mc_sun_occlusion(world));
        }
        vec3 water_color=(vec3(0.025,0.12,0.14)+
            vec3(0.13,0.24,0.20)*transmission)*illumination;
        vec3 sky_ray=reflect(world_incident,nworld);
        vec3 environment=atmosphere*0.15;
        // A covered pool must not acquire a bright, imaginary outdoor sky.
        if(u_sky_visible>0.5) {
            // Waves can reflect just below the horizon. Continue the horizon
            // colour there; switching to the dark indoor fallback caused a
            // discontinuity amplified by grazing-angle Fresnel reflection.
            float elevation=smoothstep(0.0,0.65,sky_ray.y);
            environment=mix(vec3(0.52,0.64,0.73),vec3(0.12,0.36,0.68),elevation);
            vec2 coord=sky_ray.xz/max(sky_ray.y,0.07)*1.4+
                       vec2(u_time*0.009,u_time*0.003);
            float cloud=0.55*mc_noise(coord)+0.3*mc_noise(coord*2.0)+
                        0.15*mc_noise(coord*4.0);
            float coverage=smoothstep(0.55-u_clouds*0.07,0.69-u_clouds*0.07,cloud);
            environment=mix(environment,vec3(0.91,0.94,0.97),
                            coverage*0.72*u_mc_clouds*smoothstep(0.02,0.12,sky_ray.y));
            environment*=0.04+0.96*clamp(u_daylight,0.0,1.0);
        }
        fresnel=0.02+0.98*pow(grazing,5.0);
        float reflection_weight=0.12+0.88*fresnel;
        result=mix(mix(water_color,original_color,CDDA_WATER_TEXTURE),
                   environment,reflection_weight);
    }
#endif
    // The old abrupt 20-m cutoff looked like bands of dry tiles. Fade only
    // the extra screen-space reflection; the underlying rain film stays.
    float distance_fade=1.0-smoothstep(water?32.0:15.0,water?72.0:36.0,range);
    if(distance_fade<=0.01 || u_mc_ssr_enabled<0.5 || u_quality<3.0) { return result; }
    vec3 reflected=normalize(reflect(incident,normal));
    vec3 origin=p+normal*0.085;
    vec2 hituv=vec2(-1.0);
    float hitfade=0.0;
    float hit_depth=0.0;
    float hit_length=0.0;
    float previous_length=0.0;
    float previous_delta=-0.01;
    for(int i=0;i<16;i++) {
        float length_m=0.35+float(i)*(water?1.55:0.83);
        vec3 testpos=origin+reflected*length_m;
        if(testpos.z>=-0.22) { break; }
        vec2 testuv=mc_project_view(testpos);
        if(testuv.x<=0.001 || testuv.y<=0.001 ||
           testuv.x>=0.999 || testuv.y>=0.999) { break; }
        float dz=texture2D(u_depth,testuv).r;
        if(dz>=0.99998) {
            // Sky is not a geometry hit. Keep the continuous environment
            // sheen instead of reusing a stale sky sample after ray exit.
            previous_length=length_m;
            previous_delta=-0.01;
            continue;
        }
        float delta=-testpos.z-mc_linear_depth(dz);
        if(delta>=0.0 && previous_delta<0.0) {
            // Refine only the first front-to-back crossing. Four bisections
            // reduce the original 0.83m march step to about five centimetres.
            float lo=previous_length, hi=length_m;
            for(int refinement=0;refinement<4;++refinement) {
                float middle=(lo+hi)*0.5;
                vec3 candidate=origin+reflected*middle;
                vec2 sample_uv=mc_project_view(candidate);
                float sample_depth=texture2D(u_depth,sample_uv).r;
                float difference=-candidate.z-mc_linear_depth(sample_depth);
                if(sample_depth>=0.99998 || difference<0.0) { lo=middle; }
                else { hi=middle; }
            }
            vec3 refined=origin+reflected*hi;
            vec2 refined_uv=mc_project_view(refined);
            float refined_depth=texture2D(u_depth,refined_uv).r;
            float scene_depth=mc_linear_depth(refined_depth);
            float error=-refined.z-scene_depth;
            // Bounded view-space thickness avoids accepting a ray that
            // jumped behind a thin vehicle edge or a nearby wall silhouette.
            float pixel_width=scene_depth*3.4641016*
                              max(u_texel.x,u_texel.y/max(u_aspect,0.25));
            float thickness=clamp(0.06+pixel_width*1.5,0.08,0.28);
            if(refined_depth<0.99998 && error>=0.0 && error<thickness) {
                // Water is not an opaque reflected object. A near-horizontal
                // ray can otherwise hit the same water plane through depth
                // quantization and paste its old tile image over the waves.
                if(texture2D(u_mc_reflect_mask,refined_uv).a>0.1) { break; }
                hituv=refined_uv;
                hit_depth=scene_depth;
                hit_length=hi;
                hitfade=(1.0-smoothstep(thickness*0.35,thickness,error))*
                        (1.0-smoothstep(water?18.0:9.0,water?24.0:13.0,hi));
            }
            // Do not tunnel through a rejected foreground surface looking
            // for an unrelated background object on later march steps.
            break;
        }
        if(delta>=0.0) { break; }
        previous_length=length_m;
        previous_delta=delta;
    }
    if(hitfade<=0.0) { return result; }
    // A longer reflection path broadens the rough-surface footprint. Keep
    // water crisp and normalize only taps belonging to the same depth layer.
    vec2 spread=u_texel*(1.0+roughness*(3.0+min(hit_length,12.0)*0.4));
    float tolerance=max(0.12,hit_depth*0.025);
    vec4 filtered=vec4(texture2D(u_scene,hituv).rgb,1.0)*(0.66-0.30*roughness);
    float side_weight=0.085+0.075*roughness;
    filtered+=mc_reflection_tap(hituv+vec2(spread.x,0.0),hit_depth,tolerance,side_weight);
    filtered+=mc_reflection_tap(hituv-vec2(spread.x,0.0),hit_depth,tolerance,side_weight);
    filtered+=mc_reflection_tap(hituv+vec2(0.0,spread.y),hit_depth,tolerance,side_weight);
    filtered+=mc_reflection_tap(hituv-vec2(0.0,spread.y),hit_depth,tolerance,side_weight);
    vec3 reflection=filtered.rgb/max(filtered.a,0.001);
    float edge=smoothstep(0.02,0.14,min(min(hituv.x,hituv.y),
                                        min(1.0-hituv.x,1.0-hituv.y)));
    float confidence=hitfade*edge*distance_fade;
    float strength=wet*(0.08+0.22*fresnel)*(1.0-0.55*roughness)*confidence;
#ifdef CDDA_PACK_WATER
    if(water) { strength=(0.12+0.88*fresnel)*confidence; }
#endif
    return mix(result,reflection,strength);
}
)glsl";
    replace_once( source, "void main() {",
                  pack.water_source + raymarch_helpers + "void main() {" );
    if( !pack.water_source.empty() ) {
        replace_once( source, "#define CDDA_PACK_WATER\n",
                      "#define CDDA_PACK_WATER\n#define CDDA_WATER_STRENGTH " +
                      std::to_string( pack.water_wave_strength ) +
                      "\n#define CDDA_WATER_TEXTURE " + std::to_string( pack.water_texture ) +
                      "\n#define CDDA_WATER_ABSORPTION vec3(" +
                      std::to_string( pack.water_absorption[0] ) + "," +
                      std::to_string( pack.water_absorption[1] ) + "," +
                      std::to_string( pack.water_absorption[2] ) + ")\n" );
    }

    // Layered sky only, keyed by depth=1 so world textures/HUD remain
    // untouched. Generated entirely by this project; no Photon sources copied.
    replace_once( source, "    // Ambient occlusion on short screen-space depth discontinuities, not on UI.",
        "    if(u_mc_clouds > 0.5 && u_quality > 0.5 &&\n"
        "       u_sky_visible > 0.5 && u_has_depth > 0.5 &&\n"
        "       texture2D(u_depth,v_uv).r > 0.99998) {\n"
        "        vec3 ray=normalize(vec3((v_uv.x*2.0-1.0)*1.7320508,\n"
        "            (v_uv.y*2.0-1.0)*1.7320508/max(u_aspect,0.2),-1.0));\n"
        "        float cy=cos(u_yaw_pitch.x), sy=sin(u_yaw_pitch.x);\n"
        "        float cp=cos(u_yaw_pitch.y), sp=sin(u_yaw_pitch.y);\n"
        "        float vy=cp*ray.y + sp*ray.z;\n"
        "        float vz=-sp*ray.y + cp*ray.z;\n"
        "        vec3 sky_ray=vec3(cy*ray.x-sy*vz,vy,sy*ray.x+cy*vz);\n"
        "        if(sky_ray.y > 0.025) {\n"
        "            vec2 coord=(sky_ray.xz/max(sky_ray.y,0.07))*1.4 +\n"
        "                 vec2(u_time*0.009,u_time*0.003);\n"
        "            float clouds=0.55*mc_noise(coord) +\n"
        "                 0.3*mc_noise(coord*2.0) + 0.15*mc_noise(coord*4.0);\n"
        "            float coverage=smoothstep(0.55-u_clouds*0.07,0.69-u_clouds*0.07,clouds);\n"
        "            float horizon=smoothstep(0.025,0.20,sky_ray.y);\n"
        "            vec3 cloud_color=mix(vec3(0.29,0.31,0.34),\n"
        "                 vec3(0.96,0.97,0.98),clamp(u_daylight,0.0,1.0));\n"
        "            color=mix(color,cloud_color,coverage*horizon*0.72);\n"
        "        }\n"
        "    }\n"
        "    // Ambient occlusion on short screen-space depth discontinuities, not on UI." );
    // Darker real sun-shadow footprints and contact occlusion. Both
    // still use only the original renderer's visible depth geometry.
    replace_once( source, "color *= 1.0 - occupied * 0.035;",
                  "color *= 1.0 - occupied * (0.025 + 0.024*u_mc_intensity) * u_mc_ao;" );
    replace_once( source,
                  "color *= 1.0 - shadow_factor * u_shadow_strength * 0.36;",
                  "float shadow_term=shadow_factor*u_shadow_strength*"
                  "(0.22+0.18*u_mc_intensity);\n"
                  "                color *= 1.0 - min(0.48, shadow_term);" );
    replace_once( source,
                  "lit * focus * foreground * 0.24;",
                  "lit * focus * foreground * (0.20 + 0.22*u_mc_intensity);" );

    // CDDA depth effects with optional runtime-loaded pack wave functions.
    const std::string raymarch_main = R"glsl(
    // Use world-depth and shadow-map *per pixel*, not the player outdoors
    // flag. Outdoor roads remain reflective when viewed through windows;
    // sunlit shafts can reach room surfaces through open door/window holes.
    if(u_has_depth>0.5 && u_quality>=1.0) {
        float surface_depth=texture2D(u_depth,v_uv).r;
        if(u_has_mc_reflect_mask>0.5) {
            vec4 surface_material=texture2D(u_mc_reflect_mask,v_uv);
            if(surface_material.r>0.025 &&
               (surface_material.a>0.1 || (u_mc_ssr_enabled>0.5 && u_quality>=3.0))) {
                color=mc_wet_reflection(v_uv,surface_depth,color,surface_material);
            }
        }
        if(u_quality>=3.0 && u_has_shadow>0.5 && u_mc_volume_enabled>0.5 &&
           u_daylight>0.15 && u_shadow_strength>0.01) {
            color+=mc_sun_volume(v_uv,surface_depth);
        }
    }
)glsl";
    replace_once( source,
                  "    // Lightweight sun shafts: accumulate only sky depth samples toward the",
                  raymarch_main +
                  "    // Lightweight sun shafts: accumulate only sky depth samples toward the" );

    // Sky disc halo only where the depth texture says 'sky', never drawn over
    // walls, road geometry or HUD. Mimics atmospheric glow, not a Photon pass.
    replace_once( source,
                  "    color = 1.0 - exp(-color * (u_style < 1.5 ? 1.25 : 1.10));",
                  "    if(u_has_depth > 0.5 && u_sun_strength > 0.001 &&\n"
                  "       texture2D(u_depth,v_uv).r >= 0.99998) {\n"
                  "        vec2 sun_delta=(v_uv-u_sun_position)*vec2(u_aspect,1.0);\n"
                  "        float aureole=exp(-dot(sun_delta,sun_delta)*28.0);\n"
                  "        color+=vec3(1.02,0.70,0.38)*aureole*"
                  "u_sun_strength*(0.035+0.10*u_mc_intensity);\n"
                  "    }\n"
                  "    color = 1.0 - exp(-color * (u_style < 1.5 ? 1.25 : 1.10));" );
    replace_once( source, "color = mix(color, vec3(grey * 0.92, grey * 0.98, grey), fog * 0.32);",
                  "color = mix(color, vec3(grey * 0.92, grey * 0.98, grey), "
                  "fog * (0.15 + 0.15 * u_mc_volumetric));" );
    replace_once( source, "    color = 1.0 - exp(-color * (u_style < 1.5 ? 1.25 : 1.10));",
                  "    color = 1.0 - exp(-color * 1.19);\n"
                  "    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
                  "    color = mix(vec3(luminance), color, u_mc_saturation);\n"
                  "    color *= mix(vec3(1.0), vec3(1.06, 1.0, 0.94), u_mc_warmness);\n"
                  "    if(u_has_depth > 0.5 && texture2D(u_depth, v_uv).r >= 0.99998) {\n"
                  "        color = mix(color, color * vec3(0.91, 0.99, 1.12), 0.18 * u_daylight);\n"
                  "    }" );
    replace_once( source,
                  "    if(u_style > 1.5) {\n        color *= vec3(1.09, 1.025, 0.93);\n    }",
                  "    // Contrast lift is intentionally bounded to protect"
                  " texture detail in dark streets.\n"
                  "    float mc_contrast=1.0 + 0.12*(u_mc_intensity-0.6);\n"
                  "    color=clamp((color-0.36)*mc_contrast+0.36,0.0,1.0);\n"
                  "    float mc_blend=clamp(0.68+0.28*u_mc_intensity,0.45,1.22);\n"
                  "#ifdef CDDA_PACK_WATER\n"
                  "    if(u_quality>=1.0 && u_has_mc_reflect_mask>0.5 &&\n"
                  "       texture2D(u_mc_reflect_mask,v_uv).a>0.1 && CDDA_WATER_TEXTURE<0.5) {\n"
                  "        mc_blend=1.0;\n"
                  "    }\n"
                  "#endif\n"
                  "    color=mix(original.rgb,color,mc_blend);" );
    return source;
}
} // namespace fp3d_mcshader
#endif
