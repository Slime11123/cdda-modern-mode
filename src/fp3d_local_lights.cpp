#include "fp3d_local_lights.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace fp3d_local_lights
{
namespace
{
struct ranked_light {
    float score = 0.0f;
    selected value;
};

bool contains( const std::string &id, const char *word )
{
    return id.find( word ) != std::string::npos;
}

// Shader handles a fixed maximum of eight visible emitters for old GL2.1.
const char *glsl = R"glsl(
uniform int u_native_local_count;
uniform vec4 u_native_local_pos[8];
uniform vec4 u_native_local_color[8];
uniform float u_native_local_beam[8];

float native_lamp_depth(float z) {
    const float n=0.2;
    const float f=1536.0;
    return n*f/max(0.001,f-z*(f-n));
}
vec3 native_lamp_world(vec2 uv,float d) {
    float dist=native_lamp_depth(d);
    vec3 v=vec3((uv.x*2.0-1.0)*1.7320508*dist,
                (uv.y*2.0-1.0)*1.7320508*dist/max(0.25,u_aspect),
                -dist);
    float cy=cos(u_yaw_pitch.x), sy=sin(u_yaw_pitch.x);
    float cp=cos(u_yaw_pitch.y), sp=sin(u_yaw_pitch.y);
    float vy=cp*v.y+sp*v.z;
    float vz=-sp*v.y+cp*v.z;
    return vec3(cy*v.x-sy*vz+u_camera_offset.x,
                vy+u_camera_height,
                sy*v.x+cy*vz+u_camera_offset.y);
}
vec3 native_lamp_view(vec3 world) {
    vec3 v=world-vec3(u_camera_offset.x,u_camera_height,
                      u_camera_offset.y);
    float cy=cos(u_yaw_pitch.x), sy=sin(u_yaw_pitch.x);
    float cp=cos(u_yaw_pitch.y), sp=sin(u_yaw_pitch.y);
    float vz=-sy*v.x+cy*v.z;
    return vec3(cy*v.x+sy*v.z, cp*v.y-sp*vz, sp*v.y+cp*vz);
}
vec3 native_local_lighting(vec2 uv) {
    if(u_native_local_count <= 0 || u_has_depth < 0.5) {
        return vec3(0.0);
    }
    float depth=texture2D(u_depth,uv).r;
    bool surface=depth < 0.99995 && depth > 0.000001;
    vec3 world=surface ? native_lamp_world(uv,depth) : vec3(0.0);
    vec3 n_raw=cross(dFdx(world),dFdy(world));
    float n_length=length(n_raw);
    vec3 receiver=n_length>0.0000001 ? n_raw/n_length : vec3(0.0);
    vec3 eye=vec3(u_camera_offset.x,u_camera_height,u_camera_offset.y);
    if(dot(receiver,eye-world)<0.0) {
        receiver=-receiver;
    }
    vec3 colour=vec3(0.0);
    // Follow the same center ray as native_lamp_world / the 3D crosshair:
    // positive yaw turns toward +X and positive pitch looks down (-Y).
    // Reconstructing from the actual inverse view transform prevents
    // flashlight beams from turning the opposite way to mouse aim.
    float cp=cos(u_yaw_pitch.y), sp=sin(u_yaw_pitch.y);
    vec3 forward=vec3(sin(u_yaw_pitch.x)*cp,
                      -sp,-cos(u_yaw_pitch.x)*cp);
    for(int i=0;i<8;++i) {
        if(i>=u_native_local_count) { break; }
        vec4 source=u_native_local_pos[i];
        vec4 colour_energy=u_native_local_color[i];
        if(source.w<0.1 || colour_energy.w<0.001) { continue; }
        if(surface) {
            vec3 tolight=source.xyz-world;
            float r=length(tolight);
            if(r<source.w && r>0.001) {
                float falloff=max(0.0,1.0-r/source.w);
                // Quadratic attenuation preserves sharp tile detail and
                // avoids a grey constant-brightness patch across the room.
                falloff=falloff*falloff;
                float cone=1.0;
                if(u_native_local_beam[i]>0.5) {
                    float facing=dot(-tolight/r,forward);
                    // A real head-directed flashlight cone: strong center,
                    // feathered edge, only a small spill behind the aim.
                    cone=0.04+0.96*smoothstep(0.67,0.93,facing);
                }
                // Reject lamps behind the actual facing wall/floor, rather
                // than making walls across a room glow through solid panels.
                // Full per-light shadowmaps are a separate future feature.
                float front=n_length>0.0000001 ?
                    smoothstep(-0.06,0.23,dot(receiver,tolight/r)) : 0.48;
                // Directional sources and powered fixtures do not disappear
                // in daylight, but ambient sunshine masks their contribution.
                colour+=colour_energy.rgb*colour_energy.w*
                        falloff*cone*front*
                        (1.0-0.22*clamp(u_daylight,0.0,1.0));
            }
        }
        // Bloom-like emitter, depth-tested by sampling the native scene at
        // the projected bulb centre. The bloom is just a small soft halo;
        // there is no fake light visible through an opaque foreground wall.
        vec3 lp=native_lamp_view(source.xyz);
        if(lp.z < -0.30) {
            vec2 projected=vec2(0.5)+lp.xy/
                (-lp.z*vec2(1.7320508,1.7320508/max(u_aspect,0.25)))*0.5;
            if(projected.x>0.0 && projected.x<1.0 &&
               projected.y>0.0 && projected.y<1.0) {
                float cover=native_lamp_depth(texture2D(u_depth,projected).r);
                float visible=step(-lp.z-0.55,cover);
                vec2 delta=(uv-projected)/max(u_texel*5.0,vec2(0.0001));
                float spark=exp(-dot(delta,delta)*0.45);
                colour+=colour_energy.rgb*colour_energy.w*spark*
                        visible*0.40;
            }
        }
    }
    return min(colour,vec3(1.20));
}
)glsl";
} // namespace

std::vector<selected> select( const fp3d_snapshot::scene_snapshot &scene,
                              fp3d_camera::offset camera )
{
    std::vector<ranked_light> ranked;
    ranked.reserve( scene.lights.size() );
    for( const auto &source : scene.lights ) {
        if( source.intensity <= 0.0f ) {
            continue;
        }
        const auto rel = source.position - scene.origin;
        // This is a player-held flashlight/phone, not a static world lamp.
        // The game snapshot is turn-based but camera movement is smooth;
        // attach the emitter to the *current* camera every rendered frame.
        // Otherwise the cone pivots around the old tile during movement.
        const bool held_beam = source.kind == fp3d_snapshot::light_kind::beam &&
                               ( contains( source.source, "native:flashlight_active_light" ) ||
                                 contains( source.source, "native:phone_active_light" ) );
        const float x = held_beam ? camera.x : static_cast<float>( rel.x() );
        const float z = held_beam ? camera.y : static_cast<float>( rel.y() );
        const float y = held_beam ? camera.height - 0.18f :
                        static_cast<float>( rel.z() ) * fp3d_camera::floor_height +
                        source.elevation;
        const float dx = x - camera.x;
        const float dz = z - camera.y;
        const float dy = y - camera.height;
        const float distance = std::sqrt( dx*dx + dy*dy + dz*dz );
        float radius = std::clamp( 5.0f + std::sqrt( source.intensity ) * 1.4f,
                                   6.0f, 21.0f );
        if( source.kind == fp3d_snapshot::light_kind::beam ) {
            radius = std::clamp( 9.0f + std::sqrt( source.intensity ), 11.0f, 24.0f );
        }
        if( distance > radius + 5.0f ) {
            continue;
        }
        selected light;
        light.position = { x, y, z, radius };
        const bool cold = contains( source.source, "phone" ) ||
                          contains( source.source, "flashlight" ) ||
                          contains( source.source, "led" ) ||
                          contains( source.source, "fluor" ) ||
                          contains( source.source, "arc" );
        const bool fire = source.kind == fp3d_snapshot::light_kind::flame ||
                          contains( source.source, "fire" ) ||
                          contains( source.source, "torch" );
        const float strength = std::clamp( .20f + std::sqrt( source.intensity ) *
                                           .13f, .28f, 1.45f );
        light.color = fire ? std::array<float,4>{ 1.0f,.56f,.28f,strength } :
                      cold ? std::array<float,4>{ .77f,.87f,1.0f,strength } :
                             std::array<float,4>{ 1.0f,.81f,.57f,strength };
        light.beam = source.kind == fp3d_snapshot::light_kind::beam ? 1.0f : 0.0f;
        ranked.push_back( { strength * radius / ( 3.0f + distance ), light } );
    }
    std::sort( ranked.begin(), ranked.end(), []( const ranked_light &a,
    const ranked_light &b ) { return a.score > b.score; } );
    std::vector<selected> chosen;
    const int n = std::min( static_cast<int>( ranked.size() ), max_active );
    chosen.reserve( n );
    for( int i = 0; i < n; ++i ) {
        chosen.push_back( ranked[i].value );
    }
    return chosen;
}

std::string augment_fragment( const std::string &fragment )
{
    // Opt into the native built-in source only. Imported OptiFine final.fsh
    // is user content and should not be rewritten behind the user's back.
    constexpr const char *uniform_anchor = "varying vec2 v_uv;";
    constexpr const char *output_anchor = "    gl_FragColor = vec4(color, original.a);";
    if( fragment.find( "uniform vec2 u_yaw_pitch;" ) == std::string::npos ||
        fragment.find( "uniform sampler2D u_depth;" ) == std::string::npos ||
        fragment.find( uniform_anchor ) == std::string::npos ||
        fragment.find( output_anchor ) == std::string::npos ) {
        return fragment;
    }
    std::string extended = fragment;
    extended.insert( extended.find( uniform_anchor ), glsl );
    // Feed linear lighting into the ORIGINAL Photon/CDDA tonemap, not after
    // it. Adding RGB after tonemap previously made nearby walls flat white.
    const std::string native_tonemap =
        "    color = 1.0 - exp(-color * (u_style < 1.5 ? 1.25 : 1.10));";
    const std::string mc_tonemap = "    color = 1.0 - exp(-color * 1.19);";
    const auto insertion = extended.find( native_tonemap ) != std::string::npos ?
                           extended.find( native_tonemap ) : extended.find( mc_tonemap );
    if( insertion == std::string::npos ) {
        return fragment;
    }
    extended.insert( insertion, "    color += native_local_lighting(v_uv) * 0.72;\n" );
    return extended;
}
} // namespace fp3d_local_lights
#endif
