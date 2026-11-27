#pragma once
#ifndef CATA_SRC_FP3D_IRIS_GPU_IMAGES_H
#define CATA_SRC_FP3D_IRIS_GPU_IMAGES_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>

#include "fp3d_iris_render_graph.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include "fp3d_gpu_target.h"
#include "fp3d_iris_targets.h"
#endif

namespace fp3d_iris
{
struct graph_image_spec {
    SDL_GPUTextureFormat format;
    Uint32 width;
    Uint32 height;
    Uint32 depth = 1;
    SDL_GPUTextureType type = SDL_GPU_TEXTURETYPE_2D;
    bool opaque_alpha = false;
    std::array<float, 4> clear_color{};

    bool operator==( const graph_image_spec &other ) const {
        return format == other.format && width == other.width && height == other.height &&
               depth == other.depth && type == other.type && opaque_alpha == other.opaque_alpha &&
               clear_color == other.clear_color;
    }
};

#if defined(TILES) && defined(ENABLE_FP3D)
struct borrowed_graph_image {
    SDL_GPUTexture *texture = nullptr;
    graph_image_spec spec{};
};

struct borrowed_scene_images {
    borrowed_graph_image color;
    borrowed_graph_image depth;
};

// Call after recording the native pass that stores both attachments. The
// frame slot owns the target until the Iris command buffer has retired.
inline borrowed_scene_images borrow_scene_images( const fp3d_gpu::scene_target &target )
{
    return {
        { target.native_color(), { target.color_format(), target.width(), target.height() } },
        { target.sampleable_depth() ? target.depth() : nullptr,
          { target.depth_format(), target.width(), target.height() } }
    };
}

// Resolve every logical version against the same description of its buffer.
// The caller uses the resulting specs for external textures as well as graph outputs.
inline bool resolve_pack_image_specs( const pack_fullscreen_graph &plan,
                                      const pack_properties &properties,
                                      const std::map<std::string, graph_image_spec> &base_specs,
                                      std::map<std::string, graph_image_spec> &resolved )
{
    if( !plan.error.empty() || !properties.error.empty() ) {
        return SDL_SetError( "Cannot resolve image sizes for an invalid Iris pack plan" );
    }
    std::vector<std::string> names = plan.external;
    for( const auto &pass : plan.passes ) {
        const auto outputs = graph_outputs( pass );
        names.insert( names.end(), outputs.begin(), outputs.end() );
    }
    std::map<std::string, graph_image_spec> result;
    for( const auto &name : names ) {
        const auto base = name.substr( 0, name.find( ':' ) );
        const auto entry = base_specs.find( base );
        if( entry == base_specs.end() ) {
            return SDL_SetError( "Missing Iris pack image description: %s", base.c_str() );
        }
        auto spec = entry->second;
        const auto size = properties.buffer_sizes.find( base );
        if( size != properties.buffer_sizes.end() ) {
            const auto axis = []( buffer_dimension dimension, Uint32 original ) -> Uint32 {
                const double size = dimension.relative ?
                                    static_cast<float>( dimension.value ) * static_cast<float>( original ) :
                                    dimension.value;
                return size >= 1 && size <= std::numeric_limits<Uint32>::max() ?
                       static_cast<Uint32>( size ) : 0;
            };
            spec.width = axis( size->second[0], spec.width );
            spec.height = axis( size->second[1], spec.height );
        }
        if( !spec.width || !spec.height ) {
            return SDL_SetError( "Iris pack image size out of range: %s", base.c_str() );
        }
        result.emplace( name, spec );
    }
    resolved = std::move( result );
    return true;
}

// Borrowed textures are keyed by their exact graph versions. Resolve their
// descriptions alongside new attachments and check the real dimensions
// against the pack settings before any command can use a borrowed resource.
inline bool resolve_pack_resources( const pack_fullscreen_graph &plan,
                                    const pack_properties &properties,
                                    std::map<std::string, graph_image_spec> base_specs,
                                    const std::map<std::string, borrowed_graph_image> &available,
                                    std::map<std::string, SDL_GPUTexture *> &external,
                                    std::map<std::string, graph_image_spec> &specs )
{
    std::map<std::string, SDL_GPUTexture *> imports;
    for( const auto &name : plan.external ) {
        const auto image = available.find( name );
        if( image == available.end() || !image->second.texture ) {
            return SDL_SetError( "Missing Iris frame texture: %s", name.c_str() );
        }
        base_specs.emplace( name.substr( 0, name.find( ':' ) ), image->second.spec );
        imports.emplace( name, image->second.texture );
    }
    std::map<std::string, graph_image_spec> layout;
    if( !resolve_pack_image_specs( plan, properties, base_specs, layout ) ) {
        return false;
    }
    for( const auto &name : plan.external ) {
        if( !( available.at( name ).spec == layout.at( name ) ) ) {
            return SDL_SetError( "Iris frame texture description mismatch: %s", name.c_str() );
        }
    }
    external = std::move( imports );
    specs = std::move( layout );
    return true;
}
#endif

struct release_graph_texture {
    SDL_GPUDevice *device = nullptr;
    void operator()( SDL_GPUTexture *texture ) const {
        SDL_ReleaseGPUTexture( device, texture );
    }
};
using graph_texture = std::unique_ptr<SDL_GPUTexture, release_graph_texture>;

inline graph_texture create_graph_texture( SDL_GPUDevice *device, const graph_image_spec &spec,
        SDL_GPUTextureUsageFlags usage )
{
    graph_texture texture( nullptr, release_graph_texture{ device } );
    if( !device || !spec.width || !spec.height ||
        !spec.depth || !SDL_GPUTextureSupportsFormat( device, spec.format, spec.type, usage ) ) {
        SDL_SetError( "Unsupported Iris texture description" );
        return texture;
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = spec.type;
    info.format = spec.format;
    info.usage = usage;
    info.width = spec.width;
    info.height = spec.height;
    info.layer_count_or_depth = spec.depth;
    info.num_levels = 1;
    texture.reset( SDL_CreateGPUTexture( device, &info ) );
    return texture;
}

// Own one frame slot's temporary images. Keep the device and borrowed
// resources alive until the slot's GPU fence retires.
class graph_images
{
    public:
        bool initialize( SDL_GPUDevice *device,
                         const std::map<std::string, SDL_GPUTexture *> &external,
                         const std::vector<graph_pass> &passes, const std::string &output,
                         const std::map<std::string, graph_image_spec> &declared,
                         const std::map<std::string, std::string> &history = {} ) {
            if( !device || initialized_ ) {
                return SDL_SetError( "Invalid or already initialized Iris graph images" );
            }
            std::vector<std::string> external_names;
            for( const auto &[name, texture] : external ) {
                if( !texture ) {
                    return SDL_SetError( "Missing external Iris texture: %s", name.c_str() );
                }
                external_names.push_back( name );
            }
            const auto plan = validate_graph( external_names, passes, output );
            if( !plan.error.empty() ) {
                return SDL_SetError( "%s", plan.error.c_str() );
            }
            for( const auto &pass : passes ) {
                const graph_image_spec *first = nullptr;
                for( const auto &name : graph_outputs( pass ) ) {
                    const auto definition = declared.find( name );
                    if( definition == declared.end() || !definition->second.width || !definition->second.height ) {
                        return SDL_SetError( "Missing or invalid Iris texture description: %s", name.c_str() );
                    }
                    const auto &spec = definition->second;
                    if( pass.kind != pass_kind::compute && first &&
                        ( first->width != spec.width || first->height != spec.height ) ) {
                        return SDL_SetError( "Mismatched Iris attachments in %s", pass.program.c_str() );
                    }
                    first = &spec;
                }
            }
            std::set<std::string> retained;
            for( const auto &[initial, final] : history ) {
                retained.insert( final );
            }
            std::vector<slot> owned;
            auto images = external;
            for( size_t first_write = 0; first_write < passes.size(); ++first_write ) {
                const auto &pass = passes[first_write];
                for( const auto &name : graph_outputs( pass ) ) {
                    const auto &spec = declared.at( name );
                    const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                            ( pass.kind == pass_kind::compute ? SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE :
                              name == pass.depth_write ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET :
                              SDL_GPU_TEXTUREUSAGE_COLOR_TARGET );
                    const auto last_use = retained.count( name ) ? passes.size() : plan.last_use.at( name );
                    const auto available = std::find_if( owned.begin(), owned.end(), [&]( const slot &entry ) {
                        return entry.last_use < first_write && entry.spec == spec && entry.usage == usage;
                    } );
                    if( available != owned.end() ) {
                        images.emplace( name, available->texture.get() );
                        available->last_use = last_use;
                        continue;
                    }
                    auto texture = create_graph_texture( device, spec, usage );
                    if( !texture ) {
                        const std::string error = SDL_GetError();
                        return SDL_SetError( "Iris texture %s: %s", name.c_str(), error.c_str() );
                    }
                    images.emplace( name, texture.get() );
                    owned.push_back( { spec, usage, last_use, std::move( texture ) } );
                }
            }
            images_ = std::move( images );
            owned_ = std::move( owned );
            initialized_ = true;
            return true;
        }

        const std::map<std::string, SDL_GPUTexture *> &images() const {
            return images_;
        }

    private:
        struct slot {
            graph_image_spec spec;
            SDL_GPUTextureUsageFlags usage;
            size_t last_use;
            graph_texture texture;
        };
        std::map<std::string, SDL_GPUTexture *> images_;
        std::vector<slot> owned_;
        bool initialized_ = false;
};

#if defined(TILES) && defined(ENABLE_FP3D)
struct pack_graph_images {
    graph_images textures;
    std::map<std::string, graph_image_spec> specs;
};

inline std::unique_ptr<pack_graph_images> prepare_pack_graph_images( SDL_GPUDevice *device,
        const pack_fullscreen_graph &plan, const pack_properties &properties,
        const std::map<std::string, graph_image_spec> &outputs,
        const std::map<std::string, borrowed_graph_image> &available )
{
    auto frame = std::make_unique<pack_graph_images>();
    std::map<std::string, SDL_GPUTexture *> external;
    if( !resolve_pack_resources( plan, properties, outputs, available, external, frame->specs ) ||
        !frame->textures.initialize( device, external, plan.passes, plan.output, frame->specs, plan.history ) ) {
        return nullptr;
    }
    return frame;
}
#endif

// Own a pair of persistent color attachments. The caller serializes history
// writes on the GPU queue and keeps both textures alive until its fences retire.
class history_images
{
    public:
        struct frame {
            SDL_GPUTexture *previous;
            SDL_GPUTexture *target;
            std::uint64_t epoch;
            std::uint64_t serial;
        };

        bool initialize( SDL_GPUDevice *device, const graph_image_spec &spec ) {
            if( textures_[0] ) {
                return SDL_SetError( "Iris history images are already initialized" );
            }
            constexpr auto usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            auto first = create_graph_texture( device, spec, usage );
            if( !first ) {
                return false;
            }
            auto second = create_graph_texture( device, spec, usage );
            if( !second ) {
                return false;
            }
            textures_ = { std::move( first ), std::move( second ) };
            return true;
        }

        frame begin( std::uint64_t epoch ) {
            auto *previous = valid_ && epoch_ == epoch ? textures_[write_index_ ^ 1].get() : nullptr;
            return { previous, textures_[write_index_].get(), epoch, ++serial_ };
        }

        bool presented( const frame &accepted ) {
            if( !accepted.target || accepted.serial != serial_ ||
                accepted.target != textures_[write_index_].get() ) {
                return false;
            }
            epoch_ = accepted.epoch;
            valid_ = true;
            write_index_ ^= 1;
            ++serial_;
            return true;
        }

        void invalidate() {
            valid_ = false;
            ++serial_;
        }

    private:
        std::array<graph_texture, 2> textures_;
        std::uint64_t epoch_ = 0;
        std::uint64_t serial_ = 0;
        size_t write_index_ = 0;
        bool valid_ = false;
};
} // namespace fp3d_iris

#endif
