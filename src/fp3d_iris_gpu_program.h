#pragma once
#ifndef CATA_SRC_FP3D_IRIS_GPU_PROGRAM_H
#define CATA_SRC_FP3D_IRIS_GPU_PROGRAM_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <string_view>

#include "fp3d_iris_gpu_graph.h"
#include "fp3d_iris_layout.h"

namespace fp3d_iris
{
struct compiled_stage_view {
    const shader_layout &layout;
    std::string_view bytecode;
};

// Owns SDL resources shared by every frame that uses this compiled program.
// The artifact loader verifies the identity of each bytecode/layout pair.
class graphics_program
{
    public:
        explicit graphics_program( SDL_GPUDevice *device ) : device_( device ) {}
        graphics_program( const graphics_program & ) = delete;
        graphics_program &operator=( const graphics_program & ) = delete;
        ~graphics_program() {
            if( stage_.pipeline ) {
                SDL_ReleaseGPUGraphicsPipeline( device_, stage_.pipeline );
            }
            for( auto *sampler : samplers_ ) {
                if( sampler ) {
                    SDL_ReleaseGPUSampler( device_, sampler );
                }
            }
        }

        bool initialize( const compiled_stage_view &vertex, const compiled_stage_view &fragment,
                         const SDL_GPUVertexInputState &input,
                         const std::vector<SDL_GPUColorTargetDescription> &colors,
                         SDL_GPUTextureFormat depth = SDL_GPU_TEXTUREFORMAT_INVALID,
                         const std::map<std::string, texture_sampling> &sampling = {} ) {
            if( !device_ || stage_.pipeline || vertex.layout.stage != "vert" || fragment.layout.stage != "frag" ||
                colors.size() != fragment.layout.output_locations.size() || colors.size() > 8 ||
                ( colors.empty() && depth == SDL_GPU_TEXTUREFORMAT_INVALID ) ) {
                return SDL_SetError( "Invalid Iris graphics program attachments or stages" );
            }
            for( size_t i = 0; i < colors.size(); ++i ) {
                if( fragment.layout.output_locations[i] != i ) {
                    return SDL_SetError( "Iris fragment outputs must match their attachment order" );
                }
            }
            for( const auto location : vertex.layout.input_locations ) {
                bool found = false;
                for( Uint32 i = 0; i < input.num_vertex_attributes; ++i ) {
                    found |= input.vertex_attributes[i].location == location;
                }
                if( !found ) {
                    return SDL_SetError( "Missing Iris vertex input location %u", location );
                }
            }
            auto vs = make_shader( vertex, SDL_GPU_SHADERSTAGE_VERTEX );
            if( !vs ) {
                return false;
            }
            auto fs = make_shader( fragment, SDL_GPU_SHADERSTAGE_FRAGMENT );
            if( !fs ) {
                return false;
            }

            graphics_stage next;
            next.readonly_textures = std::count_if( fragment.layout.samplers.begin(),
            fragment.layout.samplers.end(), []( const auto &entry ) {
                return entry.readonly_texture;
            } );
            if( !bind_samplers( vertex.layout, next.vertex_samplers, sampling ) ||
                !bind_samplers( fragment.layout, next.samplers, sampling ) ) {
                return false;
            }
            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader = vs.get();
            info.fragment_shader = fs.get();
            info.vertex_input_state = input;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.target_info.color_target_descriptions = colors.data();
            info.target_info.num_color_targets = static_cast<Uint32>( colors.size() );
            info.target_info.depth_stencil_format = depth;
            info.target_info.has_depth_stencil_target = depth != SDL_GPU_TEXTUREFORMAT_INVALID;
            info.depth_stencil_state.enable_depth_test = info.target_info.has_depth_stencil_target;
            info.depth_stencil_state.enable_depth_write = info.target_info.has_depth_stencil_target;
            info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
            next.pipeline = SDL_CreateGPUGraphicsPipeline( device_, &info );
            if( !next.pipeline ) {
                return false;
            }
            stage_ = std::move( next );
            return true;
        }

        // The caller supplies this frame's uniform data and visible geometry.
        graphics_stage stage() const {
            return stage_;
        }

    private:
        struct release_shader {
            SDL_GPUDevice *device;
            void operator()( SDL_GPUShader *shader ) const {
                SDL_ReleaseGPUShader( device, shader );
            }
        };
        std::unique_ptr<SDL_GPUShader, release_shader> make_shader( const compiled_stage_view &source,
                SDL_GPUShaderStage stage ) const {
            std::unique_ptr<SDL_GPUShader, release_shader> result( nullptr, { device_ } );
            SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
            if( source.layout.format == "dxil" ) {
                format = SDL_GPU_SHADERFORMAT_DXIL;
            } else if( source.layout.format == "dxbc" ) {
                format = SDL_GPU_SHADERFORMAT_DXBC;
            }
            if( !source.layout.error.empty() || source.bytecode.empty() ||
                !( SDL_GetGPUShaderFormats( device_ ) & format ) ) {
                SDL_SetError( "Missing or unsupported Iris %s bytecode", source.layout.stage.c_str() );
                return result;
            }
            constexpr size_t max_samplers = 16;
            const auto readonly = std::count_if( source.layout.samplers.begin(), source.layout.samplers.end(),
            []( const auto &entry ) {
                return entry.readonly_texture;
            } );
            const size_t sampler_count = source.layout.samplers.size() - readonly;
            if( sampler_count > max_samplers ) {
                SDL_SetError( "Iris %s stage needs %u samplers, SDL3 GPU supports %u",
                              source.layout.stage.c_str(), static_cast<unsigned>( sampler_count ),
                              static_cast<unsigned>( max_samplers ) );
                return result;
            }
            if( readonly > 8 ) {
                SDL_SetError( "Iris stage exceeds the SDL3 GPU storage texture budget" );
                return result;
            }
            SDL_GPUShaderCreateInfo info{};
            info.code = reinterpret_cast<const Uint8 *>( source.bytecode.data() );
            info.code_size = source.bytecode.size();
            info.entrypoint = "main";
            info.format = format;
            info.stage = stage;
            info.num_samplers = static_cast<Uint32>( sampler_count );
            info.num_storage_textures = static_cast<Uint32>( readonly );
            info.num_uniform_buffers = source.layout.frame_size ? 1 : 0;
            result.reset( SDL_CreateGPUShader( device_, &info ) );
            return result;
        }

        bool bind_samplers( const shader_layout &layout, std::vector<SDL_GPUSampler *> &slots,
                            const std::map<std::string, texture_sampling> &sampling ) {
            for( const auto &entry : layout.samplers ) {
                if( entry.readonly_texture ) {
                    slots.push_back( nullptr );
                    continue;
                }
                const bool comparison = entry.type == "sampler2DShadow";
                if( entry.type != "sampler2D" && entry.type != "sampler3D" && !comparison ) {
                    return SDL_SetError( "Unsupported Iris sampler %s (%s)", entry.name.c_str(), entry.type.c_str() );
                }
                const auto options = sampling.find( entry.name );
                const bool linear = comparison || ( options != sampling.end() && options->second.linear );
                const bool repeat = options != sampling.end() && options->second.repeat;
                auto *&sampler = samplers_[comparison * 4 + linear * 2 + repeat];
                if( !sampler ) {
                    SDL_GPUSamplerCreateInfo info{};
                    info.min_filter = info.mag_filter = linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
                    info.address_mode_u = info.address_mode_v = info.address_mode_w =
                                              repeat ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT :
                                              SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                    info.enable_compare = comparison;
                    info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
                    sampler = SDL_CreateGPUSampler( device_, &info );
                    if( !sampler ) {
                        return false;
                    }
                }
                slots.push_back( sampler );
            }
            return true;
        }

        SDL_GPUDevice *device_;
        std::array<SDL_GPUSampler *, 8> samplers_{};
        graphics_stage stage_;
};

struct loaded_graphics_program {
    shader_layout vertex;
    shader_layout fragment;
    std::unique_ptr<graphics_program> program;
};

struct compiled_artifact {
    std::filesystem::path file;
    std::string cache_key;
};

inline loaded_graphics_program load_graphics_program( SDL_GPUDevice *device,
        const compiled_artifact &vertex_artifact, const compiled_artifact &fragment_artifact,
        const SDL_GPUVertexInputState &input,
        const std::vector<SDL_GPUColorTargetDescription> &colors,
        SDL_GPUTextureFormat depth = SDL_GPU_TEXTUREFORMAT_INVALID,
        const std::map<std::string, texture_sampling> &sampling = {} )
{
    auto vertex = load_compiled_shader( vertex_artifact.file, vertex_artifact.cache_key );
    if( !vertex.error.empty() ) {
        SDL_SetError( "%s", vertex.error.c_str() );
        return {};
    }
    auto fragment = load_compiled_shader( fragment_artifact.file, fragment_artifact.cache_key );
    if( !fragment.error.empty() ) {
        SDL_SetError( "%s", fragment.error.c_str() );
        return {};
    }
    auto program = std::make_unique<graphics_program>( device );
    if( !program->initialize( { vertex.layout, vertex.bytecode }, { fragment.layout, fragment.bytecode },
                             input, colors, depth, sampling ) ) {
        return {};
    }
    return { std::move( vertex.layout ), std::move( fragment.layout ), std::move( program ) };
}

struct graphics_pass_artifacts {
    compiled_artifact vertex;
    compiled_artifact fragment;
    // Fullscreen passes leave this empty. Geometry streams are supplied by the renderer.
    SDL_GPUVertexInputState input{};
    std::map<std::string, std::string> sampler_images;
    std::map<std::string, texture_sampling> sampling{};
};

using graphics_program_set = std::map<std::string, loaded_graphics_program>;

// Only publish a complete set. The caller retires the old frame fence before
// replacing its pipelines and retains the published resources through submission.
inline bool load_graphics_programs( SDL_GPUDevice *device, const pack_fullscreen_graph &plan,
                                    const std::map<std::string, graphics_pass_artifacts> &artifacts,
                                    const std::map<std::string, graph_image_spec> &specs,
                                    graphics_program_set &active )
{
    if( !plan.error.empty() || plan.passes.empty() ) {
        return SDL_SetError( "Iris program loading requires a valid render graph" );
    }
    const auto valid = validate_graph( plan.external, plan.passes, plan.output );
    if( !valid.error.empty() ) {
        return SDL_SetError( "%s", valid.error.c_str() );
    }
    graphics_program_set candidate;
    for( const auto &pass : plan.passes ) {
        if( pass.kind == pass_kind::compute ) {
            return SDL_SetError( "Iris graphics loader cannot load compute pass %s", pass.program.c_str() );
        }
        const auto found = artifacts.find( pass.program );
        if( found == artifacts.end() || found->second.vertex.cache_key.empty() ||
            found->second.fragment.cache_key.empty() ) {
            return SDL_SetError( "Missing keyed Iris shader artifacts for %s", pass.program.c_str() );
        }
        std::vector<SDL_GPUColorTargetDescription> colors;
        for( const auto &name : pass.writes ) {
            const auto spec = specs.find( name );
            if( spec == specs.end() || spec->second.format == SDL_GPU_TEXTUREFORMAT_INVALID ) {
                return SDL_SetError( "Missing Iris color attachment format: %s", name.c_str() );
            }
            SDL_GPUColorTargetDescription color{};
            color.format = spec->second.format;
            if( spec->second.opaque_alpha ) {
                color.blend_state.enable_color_write_mask = true;
                color.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                                                     SDL_GPU_COLORCOMPONENT_B;
            }
            colors.push_back( color );
        }
        SDL_GPUTextureFormat depth = SDL_GPU_TEXTUREFORMAT_INVALID;
        if( !pass.depth_write.empty() ) {
            const auto spec = specs.find( pass.depth_write );
            if( spec == specs.end() || spec->second.format == SDL_GPU_TEXTUREFORMAT_INVALID ) {
                return SDL_SetError( "Missing Iris depth attachment format: %s", pass.depth_write.c_str() );
            }
            depth = spec->second.format;
        }
        const auto &pair = found->second;
        if( pass.kind == pass_kind::fullscreen &&
            ( pair.input.num_vertex_attributes || pair.input.num_vertex_buffers ) ) {
            return SDL_SetError( "Iris fullscreen pass %s cannot require vertex streams", pass.program.c_str() );
        }
        auto loaded = load_graphics_program( device, pair.vertex, pair.fragment, pair.input, colors, depth, pair.sampling );
        if( !loaded.program ) {
            const std::string error = SDL_GetError();
            return SDL_SetError( "Iris pass %s: %s", pass.program.c_str(), error.c_str() );
        }
        const auto matches = [&]( const shader_layout &layout, const std::vector<std::string> &reads ) {
            const auto bindings = resolve_sampler_inputs( layout, pair.sampler_images );
            if( !bindings.error.empty() || bindings.images.size() != reads.size() ) {
                return false;
            }
            return std::equal( reads.begin(), reads.end(), bindings.images.begin(),
            [&]( const std::string &version, const std::string &base ) {
                const auto alias = pass.sampler_aliases.find( base );
                return alias == pass.sampler_aliases.end() ?
                       version.compare( 0, version.find( ':' ), base ) == 0 : version == alias->second;
            } );
        };
        if( !matches( loaded.vertex, pass.vertex_reads ) || !matches( loaded.fragment, pass.reads ) ) {
            return SDL_SetError( "Iris sampler bindings changed for %s", pass.program.c_str() );
        }
        candidate.emplace( pass.program, std::move( loaded ) );
    }
    active = std::move( candidate );
    return true;
}
} // namespace fp3d_iris
#endif
#endif
