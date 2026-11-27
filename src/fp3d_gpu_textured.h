#pragma once
#ifndef CATA_SRC_FP3D_GPU_TEXTURED_H
#define CATA_SRC_FP3D_GPU_TEXTURED_H

#include "fp3d_gpu_camera_math.h"
#include "fp3d_gpu_mesh.h"
#include "fp3d_gpu_target.h"
#include <cstddef>

namespace fp3d_gpu
{
// SDL3 resource binding convention for D3D12. Shader compiler is supplied
// by the caller; the render module itself has no Direct3D or GL API calls.
// Base pass only: lighting and blended surfaces need independent pipelines.
inline constexpr char textured_vertex_hlsl[] = R"hlsl(
cbuffer Camera : register(b0, space1) {
    row_major float4x4 clip_from_world;
    float4 light_direction_ambient;
    float4 direct_and_padding;
};
struct V {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation float material_mode : TEXCOORD1;
    float forward_depth : TEXCOORD2;
    float3 world_position : TEXCOORD3;
    float3 world_normal : TEXCOORD4;
    float3 ambient_color : TEXCOORD5;
};
V main(float3 pos : TEXCOORD0, float2 uv : TEXCOORD1,
       float4 color : TEXCOORD2, float4 offset : TEXCOORD3,
       float4 uv_rect : TEXCOORD4, float4 tint : TEXCOORD5,
       float3 normal : TEXCOORD6) {
    V o;
    o.world_position = pos + (offset.xyz + direct_and_padding.yzw);
    o.world_normal = normal;
    o.pos = mul(clip_from_world, float4(o.world_position, 1));
    o.uv = lerp(uv_rect.xy, uv_rect.zw, uv);
    o.color = color * tint;
    o.ambient_color = o.color.rgb;
    float diffuse = max(0.0, dot(normal, light_direction_ambient.xyz)) * direct_and_padding.x;
    if ((offset.w >= -1.0 && offset.w <= 1.0) || offset.w == -3.0 ||
        offset.w == -3.5 || offset.w == 1.5) {
        o.ambient_color = saturate(o.color.rgb * light_direction_ambient.w);
        o.color.rgb = saturate(o.color.rgb * (light_direction_ambient.w + diffuse));
    }
    o.material_mode = offset.w;
    o.forward_depth = o.pos.w;
    return o;
}
)hlsl";
inline constexpr char textured_fragment_hlsl[] = R"hlsl(
Texture2D<float4> atlas : register(t0, space2);
SamplerState atlas_sampler : register(s0, space2);
float4 material_color(float2 uv, float4 color, float mode) {
    if (mode == -5.0) {
        float4 fabric = atlas.Sample(atlas_sampler, uv) * 2.0;
        fabric += atlas.Sample(atlas_sampler, uv, int2(-2, 0));
        fabric += atlas.Sample(atlas_sampler, uv, int2(2, 0));
        fabric += atlas.Sample(atlas_sampler, uv, int2(0, -2));
        fabric += atlas.Sample(atlas_sampler, uv, int2(0, 2));
        return fabric * color / 6.0;
    }
    if (mode == -4.0) {
        if (atlas.Sample(atlas_sampler, uv).a <= 0.5) discard;
        return float4(1, 1, 1, 1);
    }
    if (mode == 4.0)
        return float4(1, 1, 1, 1);
    if (mode == -3.5) {
        float4 texel = atlas.Sample(atlas_sampler, uv);
        if (texel.a <= 0.5) discard;
        return texel * color;
    }
    float4 c = (mode > 0.0 ? float4(1, 1, 1, 1) : atlas.Sample(atlas_sampler, uv)) * color;
    if (mode > -2.0) {
        float cutoff = mode < 0.0 ? -mode : (mode > 1.0 ? 0.0 : 0.5);
        if (mode < -1.0) cutoff -= 1.0;
        if (c.a <= cutoff) discard;
    }
    return c;
}
#ifdef FP3D_REFLECTIVITY_MASK
cbuffer Reflectivity : register(b0, space3) {
    float4 surface_material;
};
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0,
            nointerpolation float material_mode : TEXCOORD1, float forward_depth : TEXCOORD2,
            float3 world_position : TEXCOORD3, float3 normal : TEXCOORD4) : SV_Target0 {
    if (normal.y < 0.5) discard;
    material_color(uv, color, material_mode);
    return surface_material;
}
#elif defined(FP3D_SHADOW_CASTER)
void main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0,
          nointerpolation float material_mode : TEXCOORD1) {
    material_color(uv, color, material_mode);
}
#else
Texture2D<float> beam_depth : register(t1, space2);
SamplerState beam_sampler : register(s1, space2);
Texture2D<float> sun_depth : register(t2, space2);
SamplerState sun_sampler : register(s2, space2);
Texture2D<float> far_sun_depth : register(t3, space2);
SamplerState far_sun_sampler : register(s3, space2);
Texture2D<float> point_depth : register(t4, space2);
SamplerState point_sampler : register(s4, space2);
Texture2D<float4> pbr_material : register(t5, space2);
SamplerState pbr_sampler : register(s5, space2);
#ifdef FP3D_BEHIND_WATER
Texture2D<float> water_depth : register(t6, space2);
SamplerState water_sampler : register(s6, space2);
#endif
cbuffer Atmosphere : register(b0, space3) {
    float4 fog_color_start;
    float4 fog_inverse_range_and_padding;
    float4 local_eye_and_count;
    float4 local_forward_and_daylight;
    float4 local_position[8];
    float4 local_energy[8];
    float4 local_beam_flags[2];
    row_major float4x4 beam_clip_from_world;
    float4 beam_index_and_texel;
    row_major float4x4 sun_clip_from_world;
    row_major float4x4 far_sun_clip_from_world;
    float4 sun_direction_strength;
    float4 sun_texel_sizes;
    float4 point_index_and_texel;
    float4 pbr_eye_strength;
    float4 pbr_sun_daylight;
};

float depth_visibility(Texture2D<float> map, SamplerState map_sampler, float2 uv,
                       float depth, float bias, float texel) {
    float visibility = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            float blocker = map.SampleLevel(map_sampler, uv + float2(x, y) * texel, 0);
            visibility += step(depth - bias, blocker);
        }
    }
    return visibility / 9.0;
}

float beam_visibility(float3 world) {
    float4 clip = mul(beam_clip_from_world, float4(world, 1.0));
    if (clip.w <= 0.05)
        return 1.0;
    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0))
        return 0.0;
    float bias = 0.00008 + 0.0015 / max(1.0, clip.w * clip.w);
    return depth_visibility(beam_depth, beam_sampler, uv, ndc.z, bias, beam_index_and_texel.y);
}

float sun_occlusion(float3 world, float3 normal) {
    float facing = saturate(dot(normal, sun_direction_strength.xyz));
    if (facing <= 0.03)
        return 0.0;
    float3 receiver = world + normal * (abs(normal.y) < 0.5 ? 0.055 : 0.014);
    float3 ndc = mul(sun_clip_from_world, float4(receiver, 1.0)).xyz;
    float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    float edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    float bias = 0.00070 + 0.00135 * (1.0 - facing);
    float near_texel = sun_texel_sizes.x;
    float far_texel = sun_texel_sizes.y;
    float near_weight = far_texel > 0.0 ? smoothstep(0.075, 0.175, edge) :
                        smoothstep(2.0 * near_texel, 0.075, edge);
    if (ndc.z <= 0.0 || ndc.z >= 1.0)
        near_weight = 0.0;
    float occlusion = 0.0;
    if (near_weight > 0.001 && edge > 2.0 * near_texel) {
        occlusion = (1.0 - depth_visibility(sun_depth, sun_sampler, uv, ndc.z, bias, near_texel)) *
                    near_weight;
    }
    if (far_texel > 0.0 && near_weight < 0.999) {
        float3 far_ndc = mul(far_sun_clip_from_world, float4(receiver, 1.0)).xyz;
        float2 far_uv = far_ndc.xy * float2(0.5, -0.5) + 0.5;
        float far_edge = min(min(far_uv.x, far_uv.y), min(1.0 - far_uv.x, 1.0 - far_uv.y));
        if (far_edge > 2.0 * far_texel && far_ndc.z > 0.0 && far_ndc.z < 1.0) {
            float far_occlusion = 1.0 - depth_visibility(
                far_sun_depth, far_sun_sampler, far_uv, far_ndc.z, bias + 0.00035, far_texel);
            occlusion += far_occlusion * (1.0 - near_weight) * smoothstep(0.015, 0.045, far_edge);
        }
    }
    return occlusion * smoothstep(0.03, 0.23, facing) * sun_direction_strength.w;
}

float point_depth_sample(float3 ray) {
    float3 extent = abs(ray);
    float2 uv;
    float face;
    if (extent.x >= max(extent.y, extent.z)) {
        face = ray.x >= 0.0 ? 0.0 : 1.0;
        uv = float2(ray.x >= 0.0 ? -ray.z : ray.z, -ray.y) / extent.x;
    } else if (extent.y >= extent.z) {
        face = ray.y >= 0.0 ? 2.0 : 3.0;
        uv = float2(ray.x, ray.y >= 0.0 ? ray.z : -ray.z) / extent.y;
    } else {
        face = ray.z >= 0.0 ? 4.0 : 5.0;
        uv = float2(ray.z >= 0.0 ? ray.x : -ray.x, -ray.y) / extent.z;
    }
    float inset = 0.5 * point_index_and_texel.y;
    uv = clamp(uv * 0.5 + 0.5, inset, 1.0 - inset);
    float row = floor(face / 3.0);
    return point_depth.SampleLevel(point_sampler,
        (uv + float2(face - row * 3.0, row)) / float2(3.0, 2.0), 0);
}

float point_visibility(float3 world, float4 light) {
    float3 ray = world - light.xyz;
    float3 extent = abs(ray);
    float distance = max(max(extent.x, extent.y), extent.z);
    const float near_plane = 0.05;
    if (distance <= near_plane)
        return 1.0;
    float depth = light.w / (light.w - near_plane) * (1.0 - near_plane / distance);
    float bias = 0.00015 + 0.0015 / max(1.0, distance * distance);
    float3 direction = normalize(ray);
    float3 axis = abs(direction.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 side = normalize(cross(direction, axis));
    float3 up = cross(direction, side);
    float spread = 2.0 * distance * point_index_and_texel.y;
    float visible = step(depth - bias, point_depth_sample(ray));
    visible += step(depth - bias, point_depth_sample(ray + side * spread));
    visible += step(depth - bias, point_depth_sample(ray - side * spread));
    visible += step(depth - bias, point_depth_sample(ray + up * spread));
    visible += step(depth - bias, point_depth_sample(ray - up * spread));
    return visible * 0.2;
}

float3 local_lighting(float3 world, float3 normal) {
    float normal_length = length(normal);
    float3 receiver = normal_length > 0.0000001 ? normal / normal_length : float3(0, 0, 0);
    if (dot(receiver, local_eye_and_count.xyz - world) < 0.0)
        receiver = -receiver;
    float3 illumination = float3(0, 0, 0);
    [loop] for (int i = 0; i < (int)local_eye_and_count.w; ++i) {
        float4 source = local_position[i];
        float4 energy = local_energy[i];
        float3 delta = source.xyz - world;
        float distance = length(delta);
        if (distance <= 0.001 || distance >= source.w)
            continue;
        float3 to_light = delta / distance;
        float attenuation = 1.0 - distance / source.w;
        float cone = local_beam_flags[i / 4][i % 4] > 0.5 ?
                     0.04 + 0.96 * smoothstep(0.67, 0.93, dot(-to_light, local_forward_and_daylight.xyz)) : 1.0;
        if (i + 1 == (int)beam_index_and_texel.x) {
            cone = smoothstep(0.67, 0.93, dot(-to_light, local_forward_and_daylight.xyz)) *
                   beam_visibility(world + receiver * 0.015);
        }
        if (i + 1 == (int)point_index_and_texel.x)
            cone *= point_visibility(world + receiver * 0.015, source);
        float front = normal_length > 0.0000001 ? smoothstep(-0.06, 0.23, dot(receiver, to_light)) : 0.48;
        illumination += energy.rgb * energy.w * attenuation * attenuation * cone * front;
    }
    return min(illumination * (1.0 - 0.22 * local_forward_and_daylight.w), 1.20);
}
float3 microfacet(float3 base_color, float3 world, float3 normal, float2 uv,
                  float3 px, float3 py, float2 tx, float2 ty) {
    float nlen = dot(normal, normal);
    float determinant = tx.x * ty.y - tx.y * ty.x;
    if (nlen < 0.0001 || abs(determinant) < 1e-15)
        return base_color;
    float3 n = normal * rsqrt(nlen);
    float3 view_ray = normalize(pbr_eye_strength.xyz - world);
    if (dot(n, view_ray) < 0.0)
        n = -n;
    float3 tangent = (px * ty.y - py * tx.y) * sign(determinant);
    tangent -= n * dot(n, tangent);
    float tlen = dot(tangent, tangent);
    if (tlen < 1e-20)
        return base_color;
    tangent *= rsqrt(tlen);
    float3 b = cross(n, tangent);
    float3 bitangent = (py * tx.x - px * ty.x) * sign(determinant);
    if (dot(b, bitangent) < 0.0)
        b = -b;
    float4 material = pbr_material.SampleLevel(pbr_sampler, uv, 0);
    if (material.z <= 0.0)
        return base_color;
    float2 bump = (material.xy * 255.0 - 128.0) / 127.0;
    float3 surface_normal = normalize(n + (tangent * bump.x + b * bump.y) *
                                     min(pbr_eye_strength.w, 1.5));
    float3 sun = pbr_sun_daylight.xyz;
    float sun_angle = saturate(dot(surface_normal, sun));
    float flat = saturate(dot(n, sun));
    float daylight = pbr_sun_daylight.w;
    float3 color = base_color *
                   (1.0 + clamp((sun_angle - flat) * 0.48 * daylight, -0.18, 0.22));
    if (sun_angle > 0.0 && flat > 0.0) {
        float3 h = normalize(sun + view_ray);
        float nv = max(0.01, saturate(dot(surface_normal, view_ray)));
        float nh = saturate(dot(surface_normal, h));
        float vh = saturate(dot(view_ray, h));
        float roughness = clamp(material.z, 0.14, 0.98);
        float alpha = roughness * roughness;
        float alpha2 = alpha * alpha;
        float d = nh * nh * (alpha2 - 1.0) + 1.0;
        float distribution = alpha2 / max(0.0001, 3.14159265 * d * d);
        float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
        float geometry = nv / (nv * (1.0 - k) + k) *
                         sun_angle / (sun_angle * (1.0 - k) + k);
        float3 f0 = lerp(float3(0.04, 0.04, 0.04), saturate(base_color), material.w);
        float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
        float3 specular = min(0.24, distribution * geometry * fresnel / max(0.02, 4.0 * nv * sun_angle));
        color += specular * (daylight * sun_angle * 0.28 * pbr_eye_strength.w);
    }
    return color;
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0,
            float4 color : COLOR0, nointerpolation float material_mode : TEXCOORD1,
            float forward_depth : TEXCOORD2, float3 world_position : TEXCOORD3,
            float3 world_normal : TEXCOORD4, float3 ambient_color : TEXCOORD5) : SV_Target0 {
    float4 c = material_color(uv, color, material_mode);
#ifdef FP3D_BEHIND_WATER
    uint width, height;
    water_depth.GetDimensions(width, height);
    float surface = water_depth.SampleLevel(water_sampler, pos.xy / float2(width, height), 0);
    if (pos.z <= surface)
        discard;
#endif
    if (material_mode == 3.0 || abs(material_mode) == 4.0)
        return c;
    bool lit = (material_mode >= -1.0 && material_mode <= 1.0) || material_mode == -3.0 ||
               material_mode == -3.5 || material_mode == 1.5;
    float shadow = 0.0;
    float3 ambient = 0.0;
    if (lit && sun_direction_strength.w > 0.0) {
        shadow = sun_occlusion(world_position, world_normal);
        if (shadow > 0.0)
            ambient = c.rgb * saturate(ambient_color / max(color.rgb, 0.0001));
    }
    if (pbr_eye_strength.w > 0.0 && lit && material_mode <= 0.0)
        c.rgb = microfacet(c.rgb, world_position, world_normal, uv,
                           ddx(world_position), ddy(world_position), ddx(uv), ddy(uv));
    if (shadow > 0.0)
        c.rgb = lerp(c.rgb, min(c.rgb, ambient), shadow);
    if (lit && local_eye_and_count.w > 0.0)
        c.rgb += local_lighting(world_position, world_normal) * 0.72;
    float fog = saturate((forward_depth - fog_color_start.w) * fog_inverse_range_and_padding.x);
    c.rgb = lerp(c.rgb, fog_color_start.rgb, fog);
    return c;
}
#endif
)hlsl";
inline constexpr float lit_texture_without_cutout = -3.f;
inline constexpr float faded_texture_material = -3.5f;
constexpr float faded_solid_material = 1.5f;
constexpr float unknown_background_material = 3.f;
inline constexpr float white_model_material = 4.f;
inline constexpr float white_sprite_material = -4.f;
inline constexpr float soft_curtain_material = -5.f;

constexpr float unlit_cutout( float threshold )
{
    return -1.f - threshold;
}

struct mesh_instance {
    // XYZ translation. W is zero for the default cutout, positive for solid
    // colour (2 selects an unlit solid), or negative for a custom cutoff.
    // (-2,-1) selects an unlit texture with cutoff -mode - 1.
    // -2 and -3 disable alpha testing on unlit and lit textures, respectively.
    std::array<float, 4> offset{0,0,0,0};
    std::array<float, 4> uv_rect{0,0,1,1};
    std::array<float, 4> tint{1,1,1,1};
};

inline SDL_GPUVertexInputState native_mesh_vertex_input( bool terrain_light = false )
{
    static constexpr SDL_GPUVertexBufferDescription buffers[] = {
        { 0, sizeof( fp3d_geometry::vertex ), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 },
        { 1, sizeof( mesh_instance ), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0 },
        { 2, sizeof( Uint32 ), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0 }
    };
    static constexpr SDL_GPUVertexAttribute attributes[] = {
        { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof( fp3d_geometry::vertex, position ) },
        { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof( fp3d_geometry::vertex, uv ) },
        { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof( fp3d_geometry::vertex, color ) },
        { 3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof( mesh_instance, offset ) },
        { 4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof( mesh_instance, uv_rect ) },
        { 5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof( mesh_instance, tint ) },
        { 6, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof( fp3d_geometry::vertex, normal ) },
        { 7, 2, SDL_GPU_VERTEXELEMENTFORMAT_UINT, 0 }
    };
    const Uint32 extra = terrain_light ? 0 : 1;
    return { buffers, static_cast<Uint32>( SDL_arraysize( buffers ) ) - extra,
             attributes, static_cast<Uint32>( SDL_arraysize( attributes ) ) - extra };
}

struct directional_light {
    std::array<float, 4> direction_ambient{ 0, 1, 0, 1 };
    std::array<float, 4> direct_and_padding{};
};

struct vertex_uniforms {
    fp3d_gpu_camera_math::matrix4 camera;
    directional_light light;
};

static_assert( sizeof( vertex_uniforms ) == 24 * sizeof( float ) );

inline constexpr size_t max_local_lights = 8;
inline constexpr Uint32 beam_shadow_size = 1024;
inline constexpr Uint32 sun_shadow_size = 2048;
inline constexpr Uint32 far_sun_shadow_size = 1536;
inline constexpr Uint32 point_shadow_face_size = 512;

struct atmosphere_uniforms {
    std::array<float, 4> color_start{};
    std::array<float, 4> inverse_range_and_padding{};
    std::array<float, 4> eye_and_light_count{};
    std::array<float, 4> forward_and_daylight{};
    std::array<std::array<float, 4>, max_local_lights> local_position{};
    std::array<std::array<float, 4>, max_local_lights> local_energy{};
    std::array<std::array<float, 4>, 2> local_beam_flags{};
    fp3d_gpu_camera_math::matrix4 beam_clip_from_world{};
    std::array<float, 4> beam_index_and_texel{};
    fp3d_gpu_camera_math::matrix4 sun_clip_from_world{};
    fp3d_gpu_camera_math::matrix4 far_sun_clip_from_world{};
    std::array<float, 4> sun_direction_strength{};
    std::array<float, 4> sun_texel_sizes{};
    std::array<float, 4> point_index_and_texel{};
    std::array<float, 4> pbr_eye_strength{};
    std::array<float, 4> pbr_sun_daylight{};
};

static_assert( sizeof( atmosphere_uniforms ) == 40 * 4 * sizeof( float ) );

struct shadow_maps {
    SDL_GPUTexture *beam = nullptr;
    SDL_GPUTexture *sun = nullptr;
    SDL_GPUTexture *far_sun = nullptr;
    SDL_GPUTexture *point = nullptr;
};

enum class mesh_pass { opaque, badges, blend, overlay, count, shadow_caster, reflectivity_mask, unknown_background };

class textured_pipeline
{
    public:
        textured_pipeline() = default;
        textured_pipeline( const textured_pipeline & ) = delete;
        textured_pipeline &operator=( const textured_pipeline & ) = delete;
        ~textured_pipeline() { reset(); }

        // Vertex and fragment resources can be released after pipeline creation.
        // Caller owns borrowed atlas and instance buffer until frame retirement.
        bool initialize( SDL_GPUDevice *device, SDL_GPUShader *vs, SDL_GPUShader *fs,
                         mesh_pass state = mesh_pass::opaque,
                         SDL_GPUTextureFormat format = scene_target::color_format() ) {
            if( !device || !vs || !fs || pipeline_ || sampler_ ) {
                return SDL_SetError( "Invalid FP3D textured pipeline" );
            }
            device_ = device;
            SDL_GPUSamplerCreateInfo sampler{};
            sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
            sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
                                         SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_ = SDL_CreateGPUSampler( device_, &sampler );
            if( !sampler_ ) {
                reset();
                return false;
            }
            SDL_GPUColorTargetDescription color{};
            color.format = format;
            if( state == mesh_pass::blend ) {
                color.blend_state.enable_blend = true;
                color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
                color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            }
            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader = vs;
            info.fragment_shader = fs;
            info.vertex_input_state = native_mesh_vertex_input();
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            info.depth_stencil_state.enable_depth_test = state != mesh_pass::overlay;
            shadow_caster_ = state == mesh_pass::shadow_caster;
            mask_ = state == mesh_pass::reflectivity_mask;
            info.depth_stencil_state.enable_depth_write = state == mesh_pass::opaque || shadow_caster_;
            info.depth_stencil_state.compare_op = mask_ || state == mesh_pass::unknown_background ?
                                                 SDL_GPU_COMPAREOP_EQUAL : SDL_GPU_COMPAREOP_LESS;
            if( !shadow_caster_ ) {
                info.target_info.color_target_descriptions = &color;
                info.target_info.num_color_targets = 1;
            }
            info.target_info.has_depth_stencil_target = true;
            info.target_info.depth_stencil_format = scene_target::depth_format();
            pipeline_ = SDL_CreateGPUGraphicsPipeline( device_, &info );
            if( !pipeline_ ) {
                reset();
                return false;
            }
            return true;
        }
        bool bind( SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                   SDL_GPUTexture *atlas, const fp3d_gpu_camera_math::matrix4 &camera,
                   SDL_GPUBuffer *instances, const directional_light &light = {},
                   const atmosphere_uniforms &atmosphere = {},
                   const shadow_maps &maps = {}, SDL_GPUTexture *pbr = nullptr,
                   SDL_GPUTexture *water_depth = nullptr ) const {
            if( !cmd || !pass || !atlas || !instances || !pipeline_ || !sampler_ ) {
                return SDL_SetError( "FP3D textured pipeline resources missing" );
            }
            SDL_BindGPUGraphicsPipeline( pass, pipeline_ );
            const vertex_uniforms uniforms{ camera, light };
            SDL_PushGPUVertexUniformData( cmd, 0, &uniforms, sizeof( uniforms ) );
            if( !shadow_caster_ && !mask_ ) {
                SDL_PushGPUFragmentUniformData( cmd, 0, &atmosphere, sizeof( atmosphere ) );
            }
            const SDL_GPUTextureSamplerBinding textures[] = {
                { atlas, sampler_ },
                { maps.beam ? maps.beam : atlas, sampler_ },
                { maps.sun ? maps.sun : atlas, sampler_ },
                { maps.far_sun ? maps.far_sun : atlas, sampler_ },
                { maps.point ? maps.point : atlas, sampler_ },
                { pbr ? pbr : atlas, sampler_ },
                { water_depth, sampler_ }
            };
            const Uint32 sampler_count = shadow_caster_ || mask_ ? 1 : ( water_depth ? 7 : 6 );
            SDL_BindGPUFragmentSamplers( pass, 0, textures, sampler_count );
            const SDL_GPUBufferBinding data{ instances, 0 };
            SDL_BindGPUVertexBuffers( pass, 1, &data, 1 );
            return true;
        }
        bool ready() const {
            return pipeline_ != nullptr;
        }
        void reset() {
            if( pipeline_ ) SDL_ReleaseGPUGraphicsPipeline( device_, pipeline_ );
            if( sampler_ ) SDL_ReleaseGPUSampler( device_, sampler_ );
            pipeline_ = nullptr;
            sampler_ = nullptr;
            device_ = nullptr;
        }
    private:
        SDL_GPUDevice *device_ = nullptr;
        SDL_GPUGraphicsPipeline *pipeline_ = nullptr;
        SDL_GPUSampler *sampler_ = nullptr;
        bool shadow_caster_ = false;
        bool mask_ = false;
};

// Borrow only from the same SDL renderer/device, with no GPU readback.
// Caller must ensure SDL atlas writes have been submitted, not only flushed,
// and retain SDL_Texture ownership through GPU use.
inline SDL_GPUTexture *native_atlas( SDL_Renderer *renderer, SDL_Texture *atlas )
{
    if( !renderer || !atlas || !SDL_GetGPURendererDevice( renderer ) ||
        SDL_GetRendererFromTexture( atlas ) != renderer ) {
        SDL_SetError( "FP3D atlas belongs to a different renderer or backend" );
        return nullptr;
    }
    return static_cast<SDL_GPUTexture *>( SDL_GetPointerProperty(
            SDL_GetTextureProperties( atlas ), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr ) );
}
} // namespace fp3d_gpu
#endif
