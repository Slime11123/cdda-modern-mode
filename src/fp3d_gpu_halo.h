#pragma once
#ifndef CATA_SRC_FP3D_GPU_HALO_H
#define CATA_SRC_FP3D_GPU_HALO_H

#include "fp3d_gpu_textured.h"

namespace fp3d_gpu
{
struct halo_uniforms {
    std::array<float, 4> inverse_extent{};
    std::array<std::array<float, 4>, max_local_lights> centers{};
    std::array<std::array<float, 4>, max_local_lights> colors{};
};

static_assert( sizeof( halo_uniforms ) == 17 * 4 * sizeof( float ) );

// The depth tolerance lets a bulb drawn on the surface of a lamp remain visible.
inline std::optional<std::array<float, 4>> project_halo(
    const fp3d_gpu_camera_math::matrix4 &matrix, const std::array<float, 4> &source,
    float near_clip, float far_clip )
{
    const auto clip = matrix.transform( source[0], source[1], source[2] );
    if( clip[3] <= .30f ) {
        return std::nullopt;
    }
    const float x = clip[0] / clip[3];
    const float y = clip[1] / clip[3];
    if( std::abs( x ) >= 1.f || std::abs( y ) >= 1.f || clip[3] >= far_clip ) {
        return std::nullopt;
    }
    const float distance = std::max( near_clip, clip[3] - .55f );
    const float depth = far_clip / ( far_clip - near_clip ) * ( 1.f - near_clip / distance );
    return std::array<float, 4>{ x, y, depth, 0 };
}

inline constexpr char halo_vertex_hlsl[] = R"hlsl(
cbuffer Halos : register(b0, space1) {
    float4 inverse_extent;
    float4 centers[8];
    float4 colors[8];
};
struct V {
    float4 pos : SV_Position;
    float2 delta : TEXCOORD0;
    nointerpolation float3 depth_sample : TEXCOORD1;
    nointerpolation float4 color : COLOR0;
};
V main(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    const float2 corners[6] = { float2(-1, -1), float2(1, -1), float2(-1, 1),
                                float2(-1, 1), float2(1, -1), float2(1, 1) };
    float2 corner = corners[vertex];
    float4 center = centers[instance];
    V o;
    o.pos = float4(center.xy + corner * 40.0 * inverse_extent.xy, 0, 1);
    o.delta = corner * 4.0;
    o.depth_sample = float3(center.xy * float2(0.5, -0.5) + 0.5, center.z);
    o.color = colors[instance];
    return o;
}
)hlsl";

inline constexpr char halo_fragment_hlsl[] = R"hlsl(
Texture2D<float> scene_depth : register(t0, space2);
SamplerState depth_sampler : register(s0, space2);
float4 main(float4 pos : SV_Position, float2 delta : TEXCOORD0, nointerpolation float3 depth_sample : TEXCOORD1,
            nointerpolation float4 color : COLOR0) : SV_Target0 {
    float depth = scene_depth.SampleLevel(depth_sampler, depth_sample.xy, 0);
    if (depth < depth_sample.z)
        discard;
    float spark = exp(-dot(delta, delta) * 0.45);
    return float4(color.rgb * (color.a * spark * 0.288), 0.0);
}
)hlsl";

class halo_pipeline
{
    public:
        halo_pipeline() = default;
        halo_pipeline( const halo_pipeline & ) = delete;
        halo_pipeline &operator=( const halo_pipeline & ) = delete;
        ~halo_pipeline() {
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
            color.blend_state.enable_blend = true;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
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

        bool draw( SDL_GPUCommandBuffer *cmd, const scene_target &target,
                   const halo_uniforms &halos, Uint32 count ) const {
            SDL_GPUColorTargetInfo color{};
            color.texture = target.native_color();
            color.load_op = SDL_GPU_LOADOP_LOAD;
            color.store_op = SDL_GPU_STOREOP_STORE;
            auto *pass = SDL_BeginGPURenderPass( cmd, &color, 1, nullptr );
            if( !pass ) {
                return false;
            }
            SDL_BindGPUGraphicsPipeline( pass, pipeline_ );
            SDL_PushGPUVertexUniformData( cmd, 0, &halos, sizeof( halos ) );
            const SDL_GPUTextureSamplerBinding depth{ target.depth(), sampler_ };
            SDL_BindGPUFragmentSamplers( pass, 0, &depth, 1 );
            SDL_DrawGPUPrimitives( pass, 6, count, 0, 0 );
            SDL_EndGPURenderPass( pass );
            return true;
        }

    private:
        SDL_GPUDevice *device_ = nullptr;
        SDL_GPUGraphicsPipeline *pipeline_ = nullptr;
        SDL_GPUSampler *sampler_ = nullptr;
};
} // namespace fp3d_gpu
#endif
