#pragma once
#ifndef CATA_SRC_FP3D_GPU_SKY_H
#define CATA_SRC_FP3D_GPU_SKY_H

#include <array>

#include "fp3d_gpu_target.h"

namespace fp3d_gpu
{
inline constexpr int sky_star_count = 240;
inline constexpr int sky_cloud_count = 36;

struct sky_uniforms {
    std::array<float, 4> right_daylight{};
    std::array<float, 4> up_twilight{};
    std::array<float, 4> forward_clouds{};
    std::array<float, 4> sun_moonlight{};
    std::array<float, 4> screen_flash_time{};
    std::array<float, 4> star_cloud_mode{};
};

static_assert( sizeof( sky_uniforms ) == 24 * sizeof( float ) );

inline constexpr char sky_vertex_hlsl[] = R"hlsl(
cbuffer Sky : register(b0, space1) {
    float4 right_daylight;
    float4 up_twilight;
    float4 forward_clouds;
    float4 sun_moonlight;
    float4 screen_flash_time;
    float4 star_cloud_mode;
};
struct V {
    float4 position : SV_Position;
    float3 ray : TEXCOORD0;
    float2 disc : TEXCOORD1;
    nointerpolation float4 color : COLOR0;
};

float4 project_direction(float3 direction) {
    float depth = dot(direction, forward_clouds.xyz);
    return float4(dot(direction, right_daylight.xyz), dot(direction, up_twilight.xyz),
                  depth, depth);
}

V main(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    V o = (V)0;
    if (star_cloud_mode.z == 0) {
        const float2 corners[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };
        float2 ndc = corners[vertex];
        o.position = float4(ndc, 1.0, 1.0);
        o.ray = forward_clouds.xyz +
                ndc.x * right_daylight.xyz / dot(right_daylight.xyz, right_daylight.xyz) +
                ndc.y * up_twilight.xyz / dot(up_twilight.xyz, up_twilight.xyz);
        return o;
    }
    const float2 square[6] = { float2(-1, -1), float2(1, -1), float2(-1, 1),
                               float2(-1, 1), float2(1, -1), float2(1, 1) };
    float2 corner = square[vertex];
    uint stars = (uint)star_cloud_mode.x;
    float night = saturate(1.0 - right_daylight.w * 4.0) * (1.0 - forward_clouds.w);
    if (instance < stars) {
        float y = (instance + 0.5) / stars;
        float angle = instance * 2.39996323;
        float horizontal = sqrt(1.0 - y * y);
        float3 direction = float3(horizontal * cos(angle), y, horizontal * sin(angle));
        o.position = project_direction(direction);
        o.position.xy += corner * screen_flash_time.xy * o.position.w;
        float brightness = night * (0.35 + (instance % 7) * 0.08);
        o.color = float4(brightness.xxx, 1.0);
    } else {
        uint cloud = instance - stars;
        float y = 0.18 + (cloud % 5) * 0.13;
        float angle = cloud * 2.399963 + screen_flash_time.w * 0.002;
        float horizontal = sqrt(1.0 - y * y);
        float3 direction = float3(horizontal * cos(angle), y, horizontal * sin(angle));
        float3 right = float3(sin(angle), 0.0, -cos(angle));
        float3 up = cross(direction, right);
        float size = (2.5 + (cloud % 7) * 0.6) / 40.0;
        o.position = project_direction(direction + size * (corner.x * right + corner.y * up));
        o.disc = corner;
        float grey = 0.04 + right_daylight.w * 0.48 + screen_flash_time.z * 0.4;
        o.color = float4(grey, grey, grey * 1.02, forward_clouds.w * 0.22);
    }
    return o;
}
)hlsl";

inline constexpr char sky_fragment_hlsl[] = R"hlsl(
cbuffer Sky : register(b0, space3) {
    float4 right_daylight;
    float4 up_twilight;
    float4 forward_clouds;
    float4 sun_moonlight;
    float4 screen_flash_time;
    float4 star_cloud_mode;
};
[earlydepthstencil]
float4 main(float4 pos : SV_Position, float3 ray : TEXCOORD0, float2 disc : TEXCOORD1,
            nointerpolation float4 color : COLOR0) : SV_Target0 {
    if (color.a > 0.0) {
        if (dot(disc, disc) > 1.0) discard;
        return color;
    }
    float3 direction = normalize(ray);
    float horizon = 1.0 - asin(saturate(direction.y)) * 0.6366197724;
    float day = right_daylight.w;
    float glow = up_twilight.w * horizon * horizon;
    float clouds = forward_clouds.w;
    float grey = 0.025 + day * (0.31 + 0.16 * horizon);
    float3 clear = float3(0.008, 0.012, 0.035) +
                   day * (float3(0.20, 0.43, 0.76) + horizon * float3(0.35, 0.26, 0.10)) +
                   glow * float3(0.35, 0.12, -0.015);
    float3 background = lerp(clear, grey.xxx, clouds * 0.85) + screen_flash_time.z * 0.5;
    float sun_alignment = dot(direction, sun_moonlight.xyz);
    if (sun_moonlight.y > 0.0 && clouds < 0.8) {
        if (sun_alignment >= 0.999868)
            background = float3(1, 0.85 + 0.13 * day, 0.55 + 0.4 * day);
    } else {
        float night = saturate(1.0 - day * 4.0) * (1.0 - clouds);
        if (night > 0.0 && sun_moonlight.w > 0.01 && sun_alignment <= -0.999922) {
            float brightness = night * (0.25 + 0.65 * sun_moonlight.w);
            background = brightness * float3(1.0, 1.0, 0.95);
        }
    }
    return float4(background, 1.0);
}
)hlsl";

class sky_pipeline
{
    public:
        sky_pipeline() = default;
        sky_pipeline( const sky_pipeline & ) = delete;
        sky_pipeline &operator=( const sky_pipeline & ) = delete;
        ~sky_pipeline() {
            if( pipeline_ ) {
                SDL_ReleaseGPUGraphicsPipeline( device_, pipeline_ );
            }
        }

        bool initialize( SDL_GPUDevice *device, SDL_GPUShader *vs, SDL_GPUShader *fs ) {
            if( !vs || !fs ) {
                return false;
            }
            SDL_GPUColorTargetDescription color{};
            color.format = scene_target::color_format();
            color.blend_state.enable_blend = true;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader = vs;
            info.fragment_shader = fs;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            info.depth_stencil_state.enable_depth_test = true;
            info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            info.target_info.color_target_descriptions = &color;
            info.target_info.num_color_targets = 1;
            info.target_info.has_depth_stencil_target = true;
            info.target_info.depth_stencil_format = scene_target::depth_format();
            device_ = device;
            pipeline_ = SDL_CreateGPUGraphicsPipeline( device, &info );
            return pipeline_ != nullptr;
        }

        void draw( SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, sky_uniforms sky ) const {
            SDL_BindGPUGraphicsPipeline( pass, pipeline_ );
            SDL_PushGPUVertexUniformData( cmd, 0, &sky, sizeof( sky ) );
            SDL_PushGPUFragmentUniformData( cmd, 0, &sky, sizeof( sky ) );
            SDL_DrawGPUPrimitives( pass, 3, 1, 0, 0 );
            const auto details = static_cast<Uint32>( sky.star_cloud_mode[0] + sky.star_cloud_mode[1] );
            if( details ) {
                sky.star_cloud_mode[2] = 1;
                SDL_PushGPUVertexUniformData( cmd, 0, &sky, sizeof( sky ) );
                SDL_DrawGPUPrimitives( pass, 6, details, 0, 0 );
            }
        }

    private:
        SDL_GPUDevice *device_ = nullptr;
        SDL_GPUGraphicsPipeline *pipeline_ = nullptr;
};
} // namespace fp3d_gpu
#endif
