#pragma once
#ifndef CATA_SRC_FP3D_GPU_PHOTON_H
#define CATA_SRC_FP3D_GPU_PHOTON_H

#include <array>
#include "fp3d_gpu_target.h"

namespace fp3d_gpu
{
struct photon_uniforms {
    std::array<float, 4> texel_quality_daylight{};
    std::array<float, 4> grade{};
    std::array<float, 4> depth_and_ao{};
    std::array<float, 4> sun_position_strength{};
    std::array<float, 4> projection{};
    std::array<float, 4> view_up_and_reflections{};
    std::array<float, 4> water_east{};
    std::array<float, 4> water_south{};
    std::array<float, 4> water_parameters{};
    std::array<float, 4> wave_shape{};
    std::array<float, 4> wave_options{};
    std::array<float, 4> water_absorption{};
    std::array<float, 4> eye_volume{};
    std::array<float, 4> sun_volume{};
    std::array<float, 16> sun_matrix{};
    std::array<float, 16> far_sun_matrix{};
};

static_assert( sizeof( photon_uniforms ) == 88 * sizeof( float ) );

inline constexpr char photon_vertex_hlsl[] = R"hlsl(
struct V {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
V main(uint vertex : SV_VertexID) {
    const float2 corners[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };
    V o;
    o.pos = float4(corners[vertex], 0, 1);
    o.uv = corners[vertex] * float2(0.5, -0.5) + 0.5;
    return o;
}
)hlsl";

inline constexpr char photon_fragment_hlsl[] = R"hlsl(
Texture2D<float4> scene_color : register(t0, space2);
Texture2D<float> scene_depth : register(t1, space2);
Texture2D<float4> reflectivity_mask : register(t2, space2);
Texture2D<float> near_sun_depth : register(t3, space2);
Texture2D<float> far_sun_depth : register(t4, space2);
SamplerState color_sampler : register(s0, space2);
SamplerState depth_sampler : register(s1, space2);
SamplerState mask_sampler : register(s2, space2);
SamplerState near_sun_sampler : register(s3, space2);
SamplerState far_sun_sampler : register(s4, space2);
cbuffer Photon : register(b0, space3) {
    float4 texel_quality_daylight;
    float4 grade;
    float4 depth_and_ao;
    float4 sun_position_strength;
    float4 projection;
    float4 view_up_and_reflections;
    float4 water_east;
    float4 water_south;
    float4 water_parameters;
    float4 wave_shape;
    float4 wave_options;
    float4 water_absorption;
    float4 eye_volume;
    float4 sun_volume;
    row_major float4x4 sun_matrix;
    row_major float4x4 far_sun_matrix;
};

float linear_depth(float z) {
    return projection.z * projection.w /
           (projection.w - z * (projection.w - projection.z));
}
float2 view_uv(float3 p) {
    return p.xy / p.z * projection.xy * float2(0.5, -0.5) + 0.5;
}

float sun_visibility(float3 world) {
    float3 sample_pos = mul(sun_matrix, float4(world, 1.0)).xyz;
    float2 uv = sample_pos.xy * float2(0.5, -0.5) + 0.5;
    float2 border = min(uv, 1.0 - uv);
    float edge = min(border.x, border.y);
    bool has_far = eye_volume.w > 1.5;
    float near_weight = has_far ? smoothstep(0.075, 0.175, edge) :
                        smoothstep(0.01, 0.04, edge);
    if (sample_pos.z <= 0.002 || sample_pos.z >= 0.998)
        near_weight = 0.0;
    float near_visible = 0.0;
    if (near_weight > 0.001 && edge > 0.006)
        near_visible = step(sample_pos.z - 0.0025, near_sun_depth.SampleLevel(near_sun_sampler, uv, 0));
    float visibility = near_visible * near_weight;
    if (has_far && near_weight < 0.999) {
        float3 far_pos = mul(far_sun_matrix, float4(world, 1.0)).xyz;
        float2 far_uv = far_pos.xy * float2(0.5, -0.5) + 0.5;
        float2 far_border = min(far_uv, 1.0 - far_uv);
        float far_edge = min(far_border.x, far_border.y);
        float far_visible = 0.0;
        if (far_edge > 0.008 && far_pos.z > 0.002 && far_pos.z < 0.998)
            far_visible = step(far_pos.z - 0.0025, far_sun_depth.SampleLevel(far_sun_sampler, far_uv, 0)) *
                          smoothstep(0.013, 0.043, far_edge);
        visibility = lerp(far_visible, near_visible, near_weight);
    }
    return visibility;
}

float3 view_ray(float2 uv) {
    return float3((uv * 2.0 - 1.0) * float2(1.0, -1.0) / projection.xy, 1.0);
}

float4 ao_position(float2 uv) {
    float4 sample_position = 0.0;
    if (all(uv > 0.0) && all(uv < 1.0)) {
        float sample_depth = scene_depth.SampleLevel(depth_sampler, uv, 0);
        if (sample_depth > 0.0 && sample_depth < 0.99998)
            sample_position = float4(view_ray(uv) * linear_depth(sample_depth), 1.0);
    }
    return sample_position;
}

float3 ao_tangent(float4 negative_side, float4 positive_side, float3 receiver) {
    // At a silhouette, choose the side most likely to be on the receiver.
    float negative_gap = negative_side.w > 0.0 ? abs(negative_side.z - receiver.z) : 1e10;
    float positive_gap = positive_side.w > 0.0 ? abs(positive_side.z - receiver.z) : 1e10;
    return min(negative_gap, positive_gap) >= 1e10 ? float3(0, 0, 0) :
           positive_gap < negative_gap ? positive_side.xyz - receiver : receiver - negative_side.xyz;
}

float contact_occlusion(float2 uv, float z) {
    float3 receiver = view_ray(uv) * linear_depth(z);
    float2 d = texel_quality_daylight.xy * 2.0;
    float4 samples[4] = {
        ao_position(uv - float2(d.x, 0)), ao_position(uv + float2(d.x, 0)),
        ao_position(uv - float2(0, d.y)), ao_position(uv + float2(0, d.y))
    };
    float3 normal = cross(ao_tangent(samples[0], samples[1], receiver),
                          ao_tangent(samples[2], samples[3], receiver));
    float area = dot(normal, normal);
    float occupied = 0.0;
    if (area > 1e-16) {
        normal *= rsqrt(area) * (dot(normal, receiver) > 0.0 ? -1.0 : 1.0);
        [unroll] for (int i = 0; i < 4; ++i) {
            if (samples[i].w > 0.0) {
                float3 to_sample = samples[i].xyz - receiver;
                float relief = smoothstep(0.025, 0.12, dot(normal, to_sample));
                float proximity = 1.0 - smoothstep(0.35, 1.15, length(to_sample));
                occupied += relief * proximity;
            }
        }
    }
    return occupied;
}

bool reflection_target(float2 uv, float depth, float3 receiver, float3 surface_up) {
    bool valid = all(uv > 0.0) && all(uv < 1.0) && depth > 0.0 && depth < 0.99998;
    if (valid) {
        float4 material = reflectivity_mask.SampleLevel(mask_sampler, uv, 0);
        // Reject neighboring wet tiles on the same plane. Keep raised
        // surfaces eligible, including the floors of other storeys.
        float3 hit = view_ray(uv) * linear_depth(depth);
        valid = material.a < 0.1 &&
                (material.r <= 0.0 || abs(dot(hit - receiver, surface_up)) >= 0.10);
    }
    return valid;
}

float3 world_direction(float3 direction) {
    return float3(dot(direction, water_east.xyz), dot(direction, view_up_and_reflections.xyz),
                  dot(direction, water_south.xyz));
}

float3 volumetric_sun(float2 uv, float z) {
    float3 ray = view_ray(uv);
    bool sky = z >= 0.99998;
    float ray_length = sky ? 32.0 : min(length(ray) * linear_depth(z), 32.0);
    if (ray_length < 1.25)
        return 0.0;
    float3 direction = world_direction(normalize(ray));
    float phase = 0.20 + 0.80 * pow(max(0.0, dot(-direction, sun_volume.xyz)), 6.0);
    float light = 0.0;
    [loop] for (int i = 0; i < 8; ++i) {
        float distance = (i + 0.5) * (ray_length / 8.0);
        light += sun_visibility(eye_volume.xyz + direction * distance) * exp(-distance * 0.033);
    }
    float fog = 0.60 + 0.55 * wave_options.w + 0.35 * water_parameters.z;
    float integration = (light / 8.0) * (1.0 - exp(-ray_length * 0.045));
    float surface = sky ? 1.0 : lerp(0.32, 0.68, smoothstep(8.0, 38.0, ray_length));
    return float3(1.0, 0.82, 0.65) * integration * phase * fog *
           sun_volume.w * (0.42 + 0.20 * grade.w) * surface;
}

float wave_hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float wave_noise(float2 p) {
    float2 cell = floor(p);
    float2 mix_weight = smoothstep(0.0, 1.0, frac(p));
    float a = lerp(wave_hash(cell), wave_hash(cell + float2(1, 0)), mix_weight.x);
    float b = lerp(wave_hash(cell + float2(0, 1)), wave_hash(cell + 1.0), mix_weight.x);
    return lerp(a, b, mix_weight.y);
}

// Port the installed Photon profile's still-water Gerstner height calculation.
// Use CDDA's procedural replacement for the Iris noise texture.
float water_height(float2 world, float time) {
    float2 direction = float2(0.8660254, 0.5);
    float2 noise_pos = (world + float2(0, 0.25 * time)) * (0.007 * 256.0);
    float frequency = 0.7 * wave_shape.y;
    float amplitude = 1.0;
    float wavelength = 1.0;
    float amplitude_sum = 0.0;
    float height = 0.0;
    [loop] for (int i = 0; i < (int)wave_shape.x; ++i) {
        float k = 6.2831853 / wavelength;
        float phase = sqrt(9.8 * k) * time -
                      k * (frequency * dot(direction, world) + wave_noise(noise_pos) * 2.0);
        float wave = sin(phase) * 0.5 + 0.5;
        height += amplitude * wave * wave;
        amplitude_sum += amplitude;
        amplitude *= 0.5 * wave_shape.z;
        frequency *= 1.7 * wave_shape.w;
        wavelength *= 1.5;
        noise_pos *= 2.5;
        direction = float2(dot(direction, float2(-0.7373689, 0.6754903)),
                           dot(direction, float2(-0.6754903, -0.7373689)));
    }
    if (wave_options.y > 0.0) {
        float variation = wave_noise((world + float2(0, 0.1 * time)) * 0.256);
        height *= max(0.4, variation * 2.0 - 0.5);
    }
    return height / max(1.0, amplitude_sum);
}

float3 surface_reflection(float2 uv, float z, float3 base_color, float4 material) {
    float3 eye_ray = view_ray(uv);
    float3 incident = normalize(eye_ray);
    float3 normal = view_up_and_reflections.xyz;
    float facing = -dot(incident, normal);
    if (facing <= 0.0)
        return base_color;
    bool water = material.a > 0.1;
    bool pack_water = water && water_parameters.w > 0.0;
    float distance = linear_depth(z);
    float3 position = eye_ray * distance;
    float3 origin = position + normal * 0.08;
    if (pack_water && water_parameters.y > 0.0) {
        float footprint = 2.0 * distance * max(texel_quality_daylight.x / projection.x,
                                               texel_quality_daylight.y / projection.y) / max(0.08, facing);
        float influence = (0.04 + 0.15 * water_parameters.z) * water_parameters.y *
                          smoothstep(0.0, 0.15, facing) / (1.0 + 4.0 * footprint * footprint);
        if (influence > 0.001) {
            float2 world = float2(dot(position, water_east.xyz) + water_east.w,
                                  dot(position, water_south.xyz) + water_south.w);
            float time = water_parameters.x * (0.5 * wave_options.x);
            float step_size = clamp(footprint, 0.1, 1.0);
            float h = water_height(world, time);
            float hx = water_height(world + float2(step_size, 0), time);
            float hz = water_height(world + float2(0, step_size), time);
            normal = normalize(normal * step_size + influence *
                               (water_east.xyz * (hx - h) + water_south.xyz * (hz - h)));
            facing = saturate(-dot(incident, normal));
        }
    }
    float3 ray = reflect(incident, normal);
    float fresnel = pow(saturate(1.0 - facing), 5.0);
    float daylight = texel_quality_daylight.w;
    float3 sky = lerp(float3(0.025, 0.04, 0.075), float3(0.37, 0.59, 0.82), daylight);
    float wetness = material.r;
    float roughness = material.g;
    float3 surface = water ? lerp(base_color, sky, 0.07 + 0.28 * fresnel) :
                     lerp(base_color * (1.0 - 0.22 * wetness), sky, wetness * (0.025 + 0.10 * fresnel));
    if (pack_water) {
        float optical_depth = lerp(0.65, 4.5, material.a);
        float3 transmission = exp(-water_absorption.rgb * optical_depth * 2.0);
        float illumination = 0.12 + 0.65 * daylight;
        if (eye_volume.w > 0.0)
            illumination *= lerp(0.45, 1.0, sun_visibility(eye_volume.xyz + world_direction(position)));
        float3 water_color = (float3(0.025, 0.12, 0.14) +
                              float3(0.13, 0.24, 0.20) * transmission) * illumination;
        float elevation = smoothstep(0.0, 0.65, dot(ray, view_up_and_reflections.xyz));
        float3 environment = float3(0.018, 0.021, 0.026);
        if (material.b > 0.5) {
            environment = lerp(float3(0.52, 0.64, 0.73), float3(0.12, 0.36, 0.68), elevation);
            environment = lerp(environment, float3(0.37, 0.41, 0.45), wave_options.w * 0.68);
            environment *= 0.04 + 0.96 * daylight;
        }
        surface = lerp(lerp(water_color, base_color, wave_options.z), environment,
                       0.12 + 0.88 * (0.02 + 0.98 * fresnel));
    }
    if (view_up_and_reflections.w <= 0.0 || texel_quality_daylight.z < 3.0)
        return water ? surface : base_color;
    float previous = 0.20;
    [loop] for (int i = 0; i < 20; ++i) {
        float t = 0.35 + 0.12 * i + 0.075 * i * i;
        float3 p = origin + ray * t;
        if (p.z <= projection.z)
            break;
        float2 hit_uv = view_uv(p);
        if (any(hit_uv <= 0.0) || any(hit_uv >= 1.0))
            break;
        float sampled_z = scene_depth.SampleLevel(depth_sampler, hit_uv, 0);
        if (sampled_z < 0.99998 && p.z >= linear_depth(sampled_z)) {
            float lo = previous;
            float hi = t;
            [unroll] for (int j = 0; j < 4; ++j) {
                float mid = (lo + hi) * 0.5;
                float3 candidate = origin + ray * mid;
                float d = scene_depth.SampleLevel(depth_sampler, view_uv(candidate), 0);
                if (d < 0.99998 && candidate.z >= linear_depth(d))
                    hi = mid;
                else
                    lo = mid;
            }
            p = origin + ray * hi;
            hit_uv = view_uv(p);
            sampled_z = scene_depth.SampleLevel(depth_sampler, hit_uv, 0);
            float gap = p.z - linear_depth(sampled_z);
            if (gap >= 0.0 && gap < 0.16 + p.z * 0.012 &&
                reflection_target(hit_uv, sampled_z, position, view_up_and_reflections.xyz)) {
                float2 border = min(hit_uv, 1.0 - hit_uv);
                float fade = smoothstep(0.01, 0.12, min(border.x, border.y)) *
                             (1.0 - smoothstep(15.0, 30.0, hi));
                float3 reflected = scene_color.SampleLevel(color_sampler, hit_uv, 0).rgb;
                if (!water) {
                    const float2 taps[4] = { float2(1,0), float2(-1,0), float2(0,1), float2(0,-1) };
                    float weight = 1.0;
                    float hit_depth = linear_depth(sampled_z);
                    float2 blur = texel_quality_daylight.xy * roughness * (1.0 + min(hi, 18.0));
                    [unroll] for (int tap = 0; tap < 4; ++tap) {
                        float2 tap_uv = hit_uv + taps[tap] * blur;
                        float tap_depth = scene_depth.SampleLevel(depth_sampler, tap_uv, 0);
                        if (abs(linear_depth(tap_depth) - hit_depth) < 0.2 + hit_depth * 0.015 &&
                            reflection_target(tap_uv, tap_depth, position, view_up_and_reflections.xyz)) {
                            reflected += scene_color.SampleLevel(color_sampler, tap_uv, 0).rgb;
                            weight += 1.0;
                        }
                    }
                    reflected /= weight;
                }
                float strength = water ? (0.30 + 0.61 * fresnel) :
                                 wetness * (0.12 + 0.70 * fresnel) * (1.0 - 0.45 * roughness);
                return lerp(surface, reflected, strength * fade);
            }
        }
        previous = t;
    }
    return surface;
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    float4 original = scene_color.SampleLevel(color_sampler, uv, 0);
    float quality = texel_quality_daylight.z;
    float intensity = grade.w;
    float style = depth_and_ao.w;
    bool photon = style < 0.5;
    float3 color = original.rgb;
    if (grade.z > 0.0) {
        int radius = quality < 0.5 ? 0 : quality < 1.5 ? 1 : 2;
        float2 stride = texel_quality_daylight.xy * (quality > 2.5 ? 3.0 : 2.0);
        float3 glow = 0.0;
        float cutoff = photon ? 0.59 - 0.035 * intensity : 0.65;
        [loop] for (int y = -radius; y <= radius; ++y) {
            [loop] for (int x = -radius; x <= radius; ++x) {
                float3 sample_color = scene_color.SampleLevel(
                    color_sampler, uv + float2(x, y) * stride, 0).rgb;
                glow += max(sample_color - cutoff, 0.0);
            }
        }
        int side = 2 * radius + 1;
        float bloom = photon ? grade.z * (0.55 + 0.55 * intensity) : grade.z;
        color += glow * (bloom / (side * side));
    }
    float z = 0.0;
    if (depth_and_ao.x > 0.0) {
        z = scene_depth.SampleLevel(depth_sampler, uv, 0);
        if (depth_and_ao.z > 0.0 && z < 0.99998) {
            float4 material = reflectivity_mask.SampleLevel(mask_sampler, uv, 0);
            if (material.r > 0.0)
                color = surface_reflection(uv, z, color, material);
        }
        if (eye_volume.w > 0.0 && sun_volume.w > 0.0)
            color += volumetric_sun(uv, z);
        if (z > 0.0 && z < 0.9999 && quality > 0.5 && depth_and_ao.y > 0.0) {
            if (photon) {
                color *= 1.0 - contact_occlusion(uv, z) * (0.025 + 0.024 * intensity);
            } else {
                float2 d = texel_quality_daylight.xy * 2.0;
                float occupied = step(0.00006, z - scene_depth.SampleLevel(depth_sampler, uv + float2(d.x, 0), 0));
                occupied += step(0.00006, z - scene_depth.SampleLevel(depth_sampler, uv - float2(d.x, 0), 0));
                occupied += step(0.00006, z - scene_depth.SampleLevel(depth_sampler, uv + float2(0, d.y), 0));
                occupied += step(0.00006, z - scene_depth.SampleLevel(depth_sampler, uv - float2(0, d.y), 0));
                color *= 1.0 - occupied * 0.035;
            }
        }
        if (quality >= 2.0 && sun_position_strength.z > 0.0) {
            float2 toward_sun = sun_position_strength.xy - uv;
            int count = quality < 2.5 ? 8 : 16;
            float stride = quality < 2.5 ? 0.075 : 0.042;
            float visibility = 0.0;
            float total_weight = 0.0;
            [loop] for (int i = 1; i <= count; ++i) {
                float t = i * stride;
                float2 sample_uv = uv + toward_sun * t;
                if (any(sample_uv < 0.0) || any(sample_uv > 1.0))
                    continue;
                float sample_depth = scene_depth.SampleLevel(depth_sampler, sample_uv, 0);
                visibility += step(0.99998, sample_depth) * (1.0 - t);
                total_weight += 1.0 - t;
            }
            float focus = 1.0 / (1.0 + 14.0 * dot(toward_sun, toward_sun));
            float foreground = z < 0.99998 ? 0.38 : 1.0;
            color += float3(1.0, 0.77, 0.51) * sun_position_strength.z *
                     visibility / max(total_weight, 1.0) * focus * foreground *
                     (photon ? (0.20 + 0.22 * intensity) : 0.24);
        }
    }
    color = 1.0 - exp(-color * (photon ? 1.19 : style < 1.5 ? 1.25 : 1.10));
    if (!photon) {
        if (style > 1.5)
            color *= float3(1.09, 1.025, 0.93);
        return float4(pow(saturate(color), 0.96), original.a);
    }
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(luminance.xxx, color, grade.x);
    color *= lerp(float3(1, 1, 1), float3(1.06, 1.0, 0.94), grade.y);
    if (z >= 0.99998)
        color = lerp(color, color * float3(0.91, 0.99, 1.12), 0.18 * texel_quality_daylight.w);
    float contrast = 1.0 + 0.12 * (intensity - 0.6);
    color = saturate((color - 0.36) * contrast + 0.36);
    color = lerp(original.rgb, color, clamp(0.68 + 0.28 * intensity, 0.45, 1.22));
    return float4(pow(saturate(color), 0.96), original.a);
}
)hlsl";

class photon_pipeline
{
    public:
        photon_pipeline() = default;
        photon_pipeline( const photon_pipeline & ) = delete;
        photon_pipeline &operator=( const photon_pipeline & ) = delete;
        ~photon_pipeline() {
            if( pipeline_ ) {
                SDL_ReleaseGPUGraphicsPipeline( device_, pipeline_ );
            }
            if( sampler_ ) {
                SDL_ReleaseGPUSampler( device_, sampler_ );
            }
        }

        bool initialize( SDL_GPUDevice *device, SDL_GPUShader *vs, SDL_GPUShader *fs ) {
            if( !vs || !fs ) {
                return false;
            }
            device_ = device;
            SDL_GPUSamplerCreateInfo sampler{};
            sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
            sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
                                         SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_ = SDL_CreateGPUSampler( device_, &sampler );
            if( !sampler_ ) {
                return false;
            }
            SDL_GPUColorTargetDescription color{};
            color.format = scene_target::color_format();
            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader = vs;
            info.fragment_shader = fs;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
            info.target_info.color_target_descriptions = &color;
            info.target_info.num_color_targets = 1;
            pipeline_ = SDL_CreateGPUGraphicsPipeline( device_, &info );
            return pipeline_ != nullptr;
        }

        bool draw( SDL_GPUCommandBuffer *cmd, const scene_target &scene, SDL_GPUTexture *output,
                   photon_uniforms uniforms, SDL_GPUTexture *mask, SDL_GPUTexture *sun,
                   SDL_GPUTexture *far_sun ) const {
            SDL_GPUColorTargetInfo color{};
            color.texture = output;
            color.load_op = SDL_GPU_LOADOP_DONT_CARE;
            color.store_op = SDL_GPU_STOREOP_STORE;
            auto *pass = SDL_BeginGPURenderPass( cmd, &color, 1, nullptr );
            if( !pass ) {
                return false;
            }
            const bool has_depth = scene.sampleable_depth();
            uniforms.depth_and_ao[0] = has_depth ? 1.f : 0.f;
            uniforms.depth_and_ao[2] = mask ? 1.f : 0.f;
            uniforms.eye_volume[3] = far_sun ? 2.f : sun ? 1.f : 0.f;
            const SDL_GPUTextureSamplerBinding inputs[] = {
                { scene.native_color(), sampler_ },
                { has_depth ? scene.depth() : scene.native_color(), sampler_ },
                { mask ? mask : scene.native_color(), sampler_ },
                { sun ? sun : scene.native_color(), sampler_ },
                { far_sun ? far_sun : scene.native_color(), sampler_ }
            };
            SDL_BindGPUGraphicsPipeline( pass, pipeline_ );
            SDL_PushGPUFragmentUniformData( cmd, 0, &uniforms, sizeof( uniforms ) );
            SDL_BindGPUFragmentSamplers( pass, 0, inputs, 5 );
            SDL_DrawGPUPrimitives( pass, 3, 1, 0, 0 );
            SDL_EndGPURenderPass( pass );
            return true;
        }

    private:
        SDL_GPUDevice *device_ = nullptr;
        SDL_GPUGraphicsPipeline *pipeline_ = nullptr;
        SDL_GPUSampler *sampler_ = nullptr;
};
}
#endif
