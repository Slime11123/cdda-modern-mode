#pragma once
#ifndef CATA_SRC_FP3D_IRIS_SCENE_POST_H
#define CATA_SRC_FP3D_IRIS_SCENE_POST_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <charconv>
#include <functional>
#include <set>

#include "fp3d_iris_gpu_program.h"
#include "fp3d_iris_materials.h"
#include "fp3d_gpu_textured.h"
#include "fp3d_shaderpacks.h"

namespace fp3d_iris
{
// Executes the composite and final stages of a pack using the native scene.
// The same graph recorder is used for staged world geometry and MRT.
class scene_post
{
    struct gpu_sampled_texture {
        graph_texture image;
        graph_image_spec spec;
    };
    using gpu_texture_set = std::map<std::string, gpu_sampled_texture>;

    public:
        struct frame_slot {
            std::uint64_t revision = 0;
            std::unique_ptr<pack_graph_images> images;
            graph_texture scene_color;
            std::map<std::string, graph_texture> cleared_inputs;
            std::map<std::string, graph_texture> readonly_images;
            uniform_smoothing smoothing;
            std::optional<float> uniform_time;
            float wetness = 0.f;
            SDL_GPUTexture *atlas = nullptr;
            std::shared_ptr<const gpu_texture_set> pack_textures;
        };
        using geometry_binder = std::function<bool( const shader_layout &, const uniform_values &,
                             graphics_stage & )>;
        using opaque_binder = std::function<bool( std::string_view, const shader_layout &,
                               const shader_layout &, const uniform_values &, graphics_stage & )>;
        using water_overlays = std::function<bool( SDL_GPUTexture *, SDL_GPUTexture *,
                               SDL_GPUTextureFormat )>;

        using water_background = std::function<bool( SDL_GPUTexture *, SDL_GPUTexture *,
                                 SDL_GPUTexture *, SDL_GPUTextureFormat )>;

        using shadow_binder = std::function<bool( std::string_view, const shader_layout &,
                              const uniform_values &, graphics_stage & )>;

        bool update( SDL_GPUDevice *device, const fp3d_shaderpacks::iris_compile_state &compiled ) {
            if( compiled.running ) {
                return false;
            }
            if( revision_ == compiled.revision ) {
                return !programs_.empty();
            }
            revision_ = compiled.revision;
            programs_.clear();
            pack_textures_.reset();
            held_items_.reset();
            water_fallback_reason_.clear();
            if( compiled.stages.empty() ) {
                return false;
            }
            if( !compiled.diagnostics.empty() ) {
                return SDL_SetError( "Iris pack has incomplete stages" );
            }
            if( compiled.properties.documents.count( "item.properties" ) ) {
                const auto rules = compiled.properties.materials.find( "item.properties" );
                held_items_.emplace( rules != compiled.properties.materials.end() ?
                                     rules->second : std::vector<material_rule>{} );
            }
            const bool pack_water = std::any_of( compiled.stages.begin(), compiled.stages.end(),
            []( const auto &shader ) { return shader.program == "gbuffers_water"; } );
            if( !compiled.water_diagnostics.empty() ) {
                water_fallback_reason_ = "gbuffers_water compilation failed: " +
                                         compiled.water_diagnostics.front().substr( 0, 260 );
            } else if( !pack_water ) {
                water_fallback_reason_ = "pack does not provide gbuffers_water";
            } else {
                const auto block = compiled.properties.materials.find( "block.properties" );
                virtual_material water;
                water.shader_id = "minecraft:water";
                water.surface = material_surface::water;
                if( block == compiled.properties.materials.end() ||
                    !material_id_map( block->second ).resolve( water ) ) {
                    water_fallback_reason_ = "pack does not map minecraft:water in block.properties";
                }
            }
            if( !build_graph( device, compiled ) ) {
                if( !water_fallback_reason_.empty() ) {
                    return false;
                }
                // Run the same strict validation again without the optional
                // water stage. Shared errors and missing required outputs
                // still fail, so only a viable graph enables the fallback.
                const std::string primary_error = SDL_GetError();
                water_fallback_reason_ = "gbuffers_water GPU setup failed: " + primary_error;
                SDL_ClearError();
                if( !build_graph( device, compiled ) ) {
                    const std::string fallback_error = SDL_GetError();
                    water_fallback_reason_.clear();
                    return SDL_SetError( "%s (without gbuffers_water: %s)", primary_error.c_str(),
                                         fallback_error.c_str() );
                }
            }
            if( !water_fallback_reason_.empty() ) {
                SDL_LogInfo( SDL_LOG_CATEGORY_RENDER, "Iris native water fallback: %s",
                             water_fallback_reason_.c_str() );
            }
            return true;
        }

        bool record( SDL_GPUCommandBuffer *command, const fp3d_gpu::scene_target &target,
                     frame_slot &slot, float time_seconds, uniform_values values = {},
                     const borrowed_graph_image &atlas = {}, const geometry_binder &bind_water = {},
                     const water_overlays &record_overlays = {},
                     const water_background &record_background = {},
                     const opaque_binder &bind_opaque = {},
                     const shadow_binder &bind_shadow = {}, const frame_slot *previous = nullptr,
                     bool retain_render_history = true ) const {
            const bool same_history = previous && previous != &slot && previous->revision == revision_ &&
                                      previous->uniform_time && time_seconds >= *previous->uniform_time;
            const float elapsed = same_history ? time_seconds - *previous->uniform_time : 0.f;
            float rain = 0.f;
            if( const auto value = values.find( "rainStrength" ); value != values.end() ) {
                const auto *amount = std::get_if<std::vector<float>>( &value->second );
                if( !amount || amount->size() != 1 || !std::isfinite( amount->front() ) ) {
                    return SDL_SetError( "Invalid Iris rainStrength" );
                }
                rain = std::clamp( amount->front(), 0.f, 1.f );
            }
            const float wetness = same_history ? static_cast<float>( smooth_uniform_value(
                                      previous->wetness, rain,
                                      rain > previous->wetness ? wetness_half_life_ : dryness_half_life_, elapsed ) ) : rain;
            values.insert_or_assign( "wetness", std::vector<float>{ wetness } );
            const int empty_hand = held_items_ ? -1 : 0;
            values.try_emplace( "heldItemId", std::vector<std::int32_t>{ empty_hand } );
            values.try_emplace( "heldItemId2", std::vector<std::int32_t>{ empty_hand } );
            values.try_emplace( "screenBrightness",
                                std::vector<float>{ fp3d_shaderpacks::screen_brightness() } );
            uniform_smoothing smoothing;
            values.insert( {
                { "viewWidth", std::vector<float>{ static_cast<float>( target.width() ) } },
                { "viewHeight", std::vector<float>{ static_cast<float>( target.height() ) } },
                { "frameTimeCounter", std::vector<float>{ time_seconds } },
                { "frameTime", std::vector<float>{ elapsed } },
                { "view_res", std::vector<float>{ static_cast<float>( target.width() ),
                                                  static_cast<float>( target.height() ) } },
                { "view_pixel_size", std::vector<float>{ 1.f / target.width(), 1.f / target.height() } }
            } );
            if( !properties_.uniform_expressions.empty() ) {
                if( same_history ) {
                    smoothing = previous->smoothing;
                }
                std::set<std::string> required;
                for( const auto &[name, program] : programs_ ) {
                    for( const auto *layout : { &program.vertex, &program.fragment } ) {
                        for( const auto &[uniform, member] : layout->uniforms ) {
                            if( properties_.uniform_expressions.count( uniform ) ) {
                                required.insert( uniform );
                            }
                        }
                    }
                }
                const auto error = apply_custom_uniforms( properties_, required, values, &smoothing, elapsed );
                if( !error.empty() ) {
                    return SDL_SetError( "%s", error.c_str() );
                }
            }
            std::map<std::string, graphics_stage> stages;
            for( const auto &[name, program] : programs_ ) {
                auto stage = program.program->stage();
                packed_uniforms vertex;
                const bool model = fp3d_shaderpacks::is_iris_model_program( name );
                if( model || fp3d_shaderpacks::is_iris_terrain_program( name ) ) {
                    if( !bind_opaque ) {
                        return SDL_SetError( "Missing native Iris draw provider for %s", name.c_str() );
                    }
                    if( !bind_opaque( name, program.vertex, program.fragment, values, stage ) ) {
                        return false;
                    }
                    stage.allow_empty_geometry = true;
                } else if( name == "gbuffers_water" ) {
                    if( !bind_water ) {
                        return SDL_SetError( "Missing native Iris water draw provider" );
                    }
                    if( !bind_water( program.vertex, values, stage ) ) {
                        return false;
                    }
                    stage.allow_empty_geometry = true;
                } else if( fp3d_shaderpacks::is_iris_shadow_program( name ) || shadow_is_translucent( name ) ) {
                    if( !bind_shadow ) {
                        return SDL_SetError( "Missing native Iris shadow draw provider" );
                    }
                    if( !bind_shadow( name, program.vertex, values, stage ) ) {
                        return false;
                    }
                    stage.allow_empty_geometry = true;
                } else {
                    vertex = pack_frame_uniforms( program.vertex, values );
                }
                auto fragment = model ? packed_uniforms{} : pack_frame_uniforms( program.fragment, values );
                for( const auto *uniforms : { &vertex, &fragment } ) {
                    if( !uniforms->error.empty() ) {
                        return SDL_SetError( "%s", uniforms->error.c_str() );
                    }
                    if( !uniforms->missing.empty() ) {
                        return SDL_SetError( "Unbound Iris %s constant: %s", name.c_str(), uniforms->missing.front().c_str() );
                    }
                }
                stage.vertex_constants = std::move( vertex.bytes );
                stage.fragment_constants = std::move( fragment.bytes );
                stages.emplace( name, std::move( stage ) );
            }

            if( slot.revision != revision_ || !slot.images || slot.atlas != atlas.texture ) {
                const auto scene = borrow_scene_images( target );
                std::map<std::string, graph_image_spec> specs;
                for( const auto &[name, format] : formats_ ) {
                    const bool shadow = name.compare( 0, 6, "shadow" ) == 0;
                    specs.emplace( name, graph_image_spec{ format, shadow ? shadow_resolution_ : target.width(),
                                   shadow ? shadow_resolution_ : target.height(), 1, SDL_GPU_TEXTURETYPE_2D,
                                   rgb_attachments_.count( name ) != 0,
                                   clear_colors_.count( name ) ? clear_colors_.at( name ) : std::array<float, 4>{} } );
                }
                auto scene_color = scene.color;
                slot.scene_color.reset();
                const auto &scene_spec = specs.at( "colortex0" );
                if( scene_spec.format != scene_color.spec.format ) {
                    slot.scene_color = create_graph_texture( target.device(), scene_spec,
                                       SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER );
                    if( !slot.scene_color ) {
                        return false;
                    }
                    scene_color = { slot.scene_color.get(), scene_spec };
                }
                std::map<std::string, borrowed_graph_image> borrowed{
                    { "colortex0:0", scene_color }, { "depthtex0:0", scene.depth },
                    { "depthtex1:0", scene.depth }, { "worldAtlas:0", atlas }
                };
                slot.cleared_inputs.clear();
                if( !clear_imports_.empty() ) {
                    pack_fullscreen_graph imports;
                    imports.external.assign( clear_imports_.begin(), clear_imports_.end() );
                    std::map<std::string, graph_image_spec> import_specs;
                    if( !resolve_pack_image_specs( imports, properties_, specs, import_specs ) ) {
                        return false;
                    }
                    for( const auto &[name, spec] : import_specs ) {
                        auto image = create_graph_texture( target.device(), spec,
                                     SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER );
                        if( !image ) {
                            return false;
                        }
                        borrowed.emplace( name, borrowed_graph_image{ image.get(), spec } );
                        slot.cleared_inputs.emplace( name, std::move( image ) );
                    }
                }
                if( pack_textures_ ) {
                    for( const auto &[name, texture] : *pack_textures_ ) {
                        const borrowed_graph_image image{ texture.image.get(), texture.spec };
                        borrowed.emplace( name + ":0", image );
                        if( name == "shadowtex1" ) {
                            borrowed.emplace( "shadowtex0:0", image );
                        }
                    }
                }
                slot.images = prepare_pack_graph_images( target.device(), graph_, properties_, specs, borrowed );
                slot.readonly_images.clear();
                if( !slot.images ) {
                    return false;
                }
                // SDL forbids combining sampled and graphics storage usage.
                for( const auto &pass : graph_.passes ) {
                    const auto count = stages.at( pass.program ).readonly_textures;
                    for( auto read = pass.reads.end() - count; read != pass.reads.end(); ++read ) {
                        if( slot.readonly_images.count( *read ) ) {
                            continue;
                        }
                        auto copy = create_graph_texture( target.device(), slot.images->specs.at( *read ),
                                                         SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ );
                        if( !copy ) {
                            slot.images.reset();
                            return false;
                        }
                        slot.readonly_images.emplace( *read, std::move( copy ) );
                    }
                }
                slot.pack_textures = pack_textures_;
                slot.revision = revision_;
                slot.atlas = atlas.texture;
            }
            if( slot.scene_color ) {
                SDL_GPUBlitInfo blit{};
                blit.source.texture = target.native_color();
                blit.source.w = blit.destination.w = target.width();
                blit.source.h = blit.destination.h = target.height();
                blit.destination.texture = slot.scene_color.get();
                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_NEAREST;
                SDL_BlitGPUTexture( command, &blit );
            }
            const auto &images = *slot.images;
            const auto &textures = images.textures.images();
            const auto *history = retain_render_history && previous && previous != &slot && previous->images &&
                                  previous->revision == revision_ ? previous->images.get() : nullptr;
            for( const auto &[name, image] : slot.cleared_inputs ) {
                const auto &spec = images.specs.at( name );
                const auto prior = graph_.history.find( name );
                if( history && prior != graph_.history.end() &&
                    history->specs.at( prior->second ) == spec ) {
                    auto *copy = SDL_BeginGPUCopyPass( command );
                    if( !copy ) {
                        return false;
                    }
                    SDL_GPUTextureLocation source{};
                    source.texture = history->textures.images().at( prior->second );
                    SDL_GPUTextureLocation destination{};
                    destination.texture = image.get();
                    SDL_CopyGPUTextureToTexture( copy, &source, &destination, spec.width, spec.height, 1, false );
                    SDL_EndGPUCopyPass( copy );
                    continue;
                }
                const auto &clear = spec.clear_color;
                SDL_GPUColorTargetInfo color{};
                color.texture = image.get();
                color.clear_color = { clear[0], clear[1], clear[2], spec.opaque_alpha ? 1.f : clear[3] };
                color.load_op = SDL_GPU_LOADOP_CLEAR;
                color.store_op = SDL_GPU_STOREOP_STORE;
                auto *pass = SDL_BeginGPURenderPass( command, &color, 1, nullptr );
                if( !pass ) {
                    return false;
                }
                SDL_EndGPURenderPass( pass );
            }
            const auto native_size = [&]( const std::string &name ) {
                const auto &spec = images.specs.at( name );
                return spec.width == target.width() &&
                       spec.height == target.height();
            };
            const auto before_water = [&]( const graph_pass &pass ) {
                if( !record_background || pass.program != "gbuffers_water" ||
                    stages.at( pass.program ).draws.empty() ) {
                    return true;
                }
                if( !native_size( pass.native_color ) ) {
                    return SDL_SetError( "Iris water input cannot accept native translucency" );
                }
                // The depth prepass reads the same inputs as water, after deferred
                // has produced them. Water then samples the scene with the glass added.
                std::set<std::string> inputs{ pass.native_color, pass.depth_load };
                inputs.insert( pass.reads.begin(), pass.reads.end() );
                inputs.insert( pass.vertex_reads.begin(), pass.vertex_reads.end() );
                for( const auto &load : pass.color_loads ) {
                    inputs.insert( load.second );
                }
                return record_gpu_graph( command, { inputs.begin(), inputs.end() }, { pass },
                                         pass.depth_write, textures, stages, {}, images.specs ) &&
                       record_background( textures.at( pass.native_color ), textures.at( pass.depth_load ),
                                          textures.at( pass.depth_write ),
                                          images.specs.at( pass.native_color ).format );
            };
            const std::string_view overlay_pass = uses_water() ? "gbuffers_water" :
                    uses_basic() ? "gbuffers_basic" :
                    programs_.count( "gbuffers_entities" ) ? "gbuffers_entities" :
                    programs_.count( "gbuffers_block" ) ? "gbuffers_block" :
                    programs_.count( "gbuffers_terrain" ) ? "gbuffers_terrain" :
                    programs_.count( "gbuffers_terrain_cutout" ) ? "gbuffers_terrain_cutout" : "gbuffers_terrain_solid";
            const auto after_geometry = [&]( const graph_pass &pass ) {
                if( !record_overlays || pass.program != overlay_pass ) {
                    return true;
                }
                const auto color = std::find_if( pass.writes.begin(), pass.writes.end(), []( const std::string &name ) {
                    return name.compare( 0, 10, "colortex0:" ) == 0;
                } );
                const std::string &color_name = color == pass.writes.end() ? pass.native_color : *color;
                if( !native_size( color_name ) ) {
                    return SDL_SetError( "Iris water color cannot accept native overlays" );
                }
                return record_overlays( textures.at( color_name ), textures.at( pass.depth_write ),
                                        images.specs.at( color_name ).format );
            };
            if( !record_gpu_graph( command, graph_.external, graph_.passes, graph_.output,
                                   textures, stages,
                                   {}, images.specs, after_geometry, before_water, slot.readonly_images ) ||
                !record_pack_display( command, graph_, images, target ) ) {
                return false;
            }
            slot.smoothing = std::move( smoothing );
            slot.uniform_time = time_seconds;
            slot.wetness = wetness;
            return true;
        }

        bool needs_depth() const {
            return std::any_of( graph_.external.begin(), graph_.external.end(), []( const std::string &name ) {
                return name == "depthtex0:0" || name == "depthtex1:0";
            } );
        }

        bool uses_water() const {
            return programs_.count( "gbuffers_water" ) != 0;
        }

        const std::string &water_fallback_reason() const {
            return water_fallback_reason_;
        }

        std::string_view shadow_program( bool translucent, fp3d_shaderpacks::iris_shadow_caster caster,
                                         bool solid = false ) const {
            using caster_kind = fp3d_shaderpacks::iris_shadow_caster;
            const char *specialized = "";
            switch( caster ) {
                case caster_kind::water:
                    specialized = "shadow_water";
                    break;
                case caster_kind::terrain:
                    if( !translucent ) {
                        specialized = solid ? "shadow_solid" : "shadow_cutout";
                    }
                    break;
                case caster_kind::block:
                    specialized = translucent ? shadow_translucent_name( "shadow_block" ) : "shadow_block";
                    break;
                case caster_kind::entity:
                    specialized = translucent ? shadow_translucent_name( "shadow_entities" ) : "shadow_entities";
                    break;
                case caster_kind::unclassified:
                    break;
            }
            if( *specialized && programs_.count( specialized ) ) {
                return specialized;
            }
            const char *fallback = translucent ? shadow_translucent_name( "shadow" ) : "shadow";
            return programs_.count( fallback ) ? fallback : "";
        }

        static bool shadow_is_translucent( std::string_view name ) {
            return name == "shadow_water" || name == shadow_translucent_name( "shadow" ) ||
                   name == shadow_translucent_name( "shadow_block" ) || name == shadow_translucent_name( "shadow_entities" );
        }

        bool uses_shadow() const {
            return std::any_of( programs_.begin(), programs_.end(), []( const auto &program ) {
                return fp3d_shaderpacks::is_iris_shadow_program( program.first );
            } );
        }

        unsigned shadow_resolution() const {
            return shadow_resolution_;
        }

        int held_item_id( std::string_view native_id ) const {
            if( !held_items_ ) {
                return 0;
            }
            if( native_id.empty() ) {
                return -1;
            }
            virtual_material item;
            item.native_id = "cdda:" + std::string( native_id );
            return held_items_->resolve( item ).value_or( -1 );
        }

        std::string_view terrain_program( bool solid ) const {
            const char *specialized = solid ? "gbuffers_terrain_solid" : "gbuffers_terrain_cutout";
            if( programs_.count( specialized ) ) {
                return specialized;
            }
            return programs_.count( "gbuffers_terrain" ) ? "gbuffers_terrain" : "";
        }

        std::string_view model_program( fp3d_shaderpacks::iris_shadow_caster caster ) const {
            using kind = fp3d_shaderpacks::iris_shadow_caster;
            const char *name = caster == kind::block ? "gbuffers_block" :
                               caster == kind::entity ? "gbuffers_entities" : "";
            return programs_.count( name ) ? name : "";
        }

        bool uses_basic() const {
            return programs_.count( "gbuffers_basic" ) != 0;
        }

        bool uses_opaque_geometry() const {
            using kind = fp3d_shaderpacks::iris_shadow_caster;
            return uses_basic() || !terrain_program( true ).empty() || !terrain_program( false ).empty() ||
                   !model_program( kind::block ).empty() || !model_program( kind::entity ).empty();
        }

        const pack_properties &properties() const {
            return properties_;
        }

    private:
        bool build_graph( SDL_GPUDevice *device, const fp3d_shaderpacks::iris_compile_state &compiled ) {
            using stage = fp3d_shaderpacks::iris_compiled_stage;
            std::map<unsigned, std::array<const stage *, 2>> pairs;
            for( const auto &shader : compiled.stages ) {
                if( shader.program == "gbuffers_water" && !water_fallback_reason_.empty() ) {
                    continue;
                }
                const auto order = pass_order( shader.program );
                if( !order || ( shader.stage != "vert" && shader.stage != "frag" ) ) {
                    return SDL_SetError( "Iris pack requires unsupported stage %s", shader.program.c_str() );
                }
                auto *&entry = pairs[*order][shader.stage == "frag"];
                if( entry ) {
                    return SDL_SetError( "Duplicate Iris stage for %s", shader.program.c_str() );
                }
                entry = &shader;
            }
            std::map<std::string, std::string> samplers{
                { "gcolor", "colortex0" }, { "depthtex0", "depthtex0" }, { "gdepthtex", "depthtex0" },
                { "depthtex1", "depthtex1" }, { "shadowtex0", "shadowtex0" }, { "shadowtex1", "shadowtex1" },
                { "noisetex", "noisetex" }
            };
            for( int i = 0; i < 32; ++i ) {
                const std::string name = "colortex" + std::to_string( i );
                samplers.emplace( name, name );
                const std::string shadow = "shadowcolor" + std::to_string( i );
                samplers.emplace( shadow, shadow );
            }
            std::vector<pack_fullscreen_program> descriptors;
            std::map<std::string, graphics_pass_artifacts> artifacts;
            std::vector<pack_fullscreen_program> translucent_shadows;
            std::map<std::string, std::string> declarations;
            std::map<std::string, bool> clear_flags;
            clear_colors_.clear();
            clear_imports_.clear();
            const auto merge_declarations = []( auto &destination, const auto &source ) {
                for( const auto &[name, value] : source ) {
                    const auto [entry, inserted] = destination.emplace( name, value );
                    if( !inserted && entry->second != value ) {
                        return SDL_SetError( "Conflicting Iris clear declaration for %s", name.c_str() );
                    }
                }
                return true;
            };
            bool depth = false, opaque_depth = false, atlas = false;
            std::set<std::string> pack_images;
            std::optional<unsigned> shadow_resolution;
            std::optional<float> wetness_half_life;
            std::optional<float> dryness_half_life;
            const auto merge_half_life = []( std::optional<float> &destination, std::optional<float> value ) {
                if( !value ) {
                    return true;
                }
                if( destination && destination != value ) {
                    return false;
                }
                destination = value;
                return true;
            };
            for( const auto &shader : compiled.stages ) {
                if( shader.program == "gbuffers_water" && !water_fallback_reason_.empty() ) {
                    continue;
                }
                if( !merge_half_life( wetness_half_life, shader.layout.wetness_half_life ) ||
                    !merge_half_life( dryness_half_life, shader.layout.dryness_half_life ) ) {
                    return SDL_SetError( "Conflicting Iris wetness or dryness half-life" );
                }
                if( shader.layout.shadow_resolution ) {
                    if( shadow_resolution && shadow_resolution != shader.layout.shadow_resolution ) {
                        return SDL_SetError( "Conflicting Iris shadow map resolutions" );
                    }
                    shadow_resolution = shader.layout.shadow_resolution;
                }
            }
            shadow_resolution_ = shadow_resolution.value_or( 1024 );
            wetness_half_life_ = wetness_half_life.value_or( 600.f );
            dryness_half_life_ = dryness_half_life.value_or( 200.f );
            for( const auto &[order, pair] : pairs ) {
                const auto *vertex = pair[0];
                const auto *fragment = pair[1];
                if( !vertex || !fragment || vertex->program != fragment->program ) {
                    return SDL_SetError( "Incomplete Iris stage pair at pass %u", order );
                }
                pack_fullscreen_program pass{ fragment->program, {}, {} };
                const bool water = pass.name == "gbuffers_water";
                const bool opaque = fp3d_shaderpacks::is_iris_terrain_program( pass.name ) ||
                                    fp3d_shaderpacks::is_iris_model_program( pass.name );
                auto stage_samplers = samplers;
                const bool shadow = fp3d_shaderpacks::is_iris_shadow_program( pass.name );
                if( water || opaque || shadow ) {
                    pass.kind = pass_kind::geometry;
                    pass.depth_buffer = shadow ? ( pass.name == "shadow_water" ? "shadowtex0" : "shadowtex1" ) :
                                        "depthtex0";
                    pass.needs_native_color = !shadow;
                    if( shadow ) {
                        pass.bank = color_buffer_bank::shadow;
                        if( pass.name != "shadow_water" ) {
                            pass.depth_snapshot = "shadowtex0";
                        }
                    }
                    if( opaque ) {
                        pass.depth_snapshot = "depthtex1";
                    }
                    stage_samplers.emplace( "tex", "worldAtlas" );
                    stage_samplers.emplace( "gtexture", "worldAtlas" );
                    depth |= !shadow;
                }
                std::map<std::string, texture_sampling> sampling;
                const auto bindings = compiled.texture_bindings.find( pass.name );
                for( const auto *layout : { &vertex->layout, &fragment->layout } ) {
                    for( const auto &sampler : layout->samplers ) {
                        if( bindings != compiled.texture_bindings.end() ) {
                            const auto binding = bindings->second.find( sampler.name );
                            if( binding != bindings->second.end() ) {
                                stage_samplers[sampler.name] = binding->second;
                            }
                        }
                        const auto image = stage_samplers.find( sampler.name );
                        const auto asset = image == stage_samplers.end() ? compiled.textures.end() :
                                           compiled.textures.find( image->second );
                        if( asset != compiled.textures.end() ) {
                            const bool type_matches = sampler.type == ( asset->second.volume ? "sampler3D" : "sampler2D" );
                            if( !type_matches ) {
                                return SDL_SetError( "Iris texture type does not match %s in %s",
                                                     sampler.name.c_str(), pass.name.c_str() );
                            }
                            pack_images.insert( asset->first );
                            sampling[sampler.name] = asset->second.sampling;
                        } else if( sampler.type == "sampler3D" || sampler.name == "noisetex" ) {
                            return SDL_SetError( "Missing Iris texture %s in %s",
                                                 sampler.name.c_str(), pass.name.c_str() );
                        }
                    }
                }
                const auto error = bind_compiled_program( pass, vertex->layout, fragment->layout, stage_samplers );
                if( !error.empty() ) {
                    return SDL_SetError( "%s", error.c_str() );
                }
                for( const auto *inputs : { &pass.vertex_samplers, &pass.samplers } ) {
                    depth |= std::find( inputs->begin(), inputs->end(), "depthtex0" ) != inputs->end();
                    opaque_depth |= std::find( inputs->begin(), inputs->end(), "depthtex1" ) != inputs->end();
                    atlas |= std::find( inputs->begin(), inputs->end(), "worldAtlas" ) != inputs->end();
                }
                if( !merge_declarations( clear_flags, fragment->layout.buffer_clears ) ||
                    !merge_declarations( clear_colors_, fragment->layout.buffer_clear_colors ) ) {
                    return false;
                }
                for( const auto &[name, format] : fragment->layout.buffer_formats ) {
                    const auto [entry, inserted] = declarations.emplace( name, format );
                    if( !inserted && entry->second != format ) {
                        return SDL_SetError( "Conflicting Iris format for %s", name.c_str() );
                    }
                }
                if( pass.name.compare( 0, 10, "shadowcomp" ) == 0 ) {
                    pass.bank = color_buffer_bank::shadow;
                }
                const graphics_pass_artifacts pair_artifacts{
                    { vertex->bytecode, vertex->cache_key }, { fragment->bytecode, fragment->cache_key },
                    water || opaque || shadow ? fp3d_gpu::native_mesh_vertex_input( opaque ) :
                    SDL_GPUVertexInputState{}, std::move( stage_samplers ), sampling
                };
                if( order >= 6 && !translucent_shadows.empty() ) {
                    std::move( translucent_shadows.begin(), translucent_shadows.end(),
                               std::back_inserter( descriptors ) );
                    translucent_shadows.clear();
                }
                descriptors.push_back( pass );
                artifacts.emplace( pass.name, pair_artifacts );
                const auto late = shadow_translucent_name( pass.name );
                if( *late ) {
                    pass.name = late;
                    pass.depth_buffer = "shadowtex0";
                    pass.depth_snapshot.clear();
                    artifacts.emplace( pass.name, pair_artifacts );
                    translucent_shadows.push_back( std::move( pass ) );
                }
            }
            properties_ = compiled.properties;
            std::vector<std::string> external{ "colortex0" };
            if( depth ) {
                external.emplace_back( "depthtex0" );
            }
            if( opaque_depth ) {
                external.emplace_back( "depthtex1" );
            }
            if( atlas ) {
                external.emplace_back( "worldAtlas" );
            }
            // A shadow shader can sample the earlier depth version, including
            // the first opaque shader before any casters have been drawn.
            bool seed_shadow = false, shadow_has_geometry = false;
            std::set<std::string> shadow_depths;
            for( const auto &pass : descriptors ) {
                for( const auto *inputs : { &pass.samplers, &pass.vertex_samplers } ) {
                    for( const auto &input : *inputs ) {
                        if( ( input == "shadowtex0" || input == "shadowtex1" ) &&
                            shadow_depths.count( input ) == 0 ) {
                            seed_shadow = true;
                        }
                    }
                }
                if( pass.depth_buffer == "shadowtex0" || pass.depth_buffer == "shadowtex1" ) {
                    shadow_has_geometry = true;
                    shadow_depths.insert( pass.depth_buffer );
                    if( !pass.depth_snapshot.empty() ) {
                        shadow_depths.insert( pass.depth_snapshot );
                    }
                }
            }
            seed_shadow &= shadow_has_geometry;
            if( seed_shadow ) {
                external.emplace_back( "shadowtex0" );
                external.emplace_back( "shadowtex1" );
            }
            external.insert( external.end(), pack_images.begin(), pack_images.end() );
            std::set<std::string> clearable;
            std::set<std::string> persistent;
            for( const auto &[name, enabled] : clear_flags ) {
                if( enabled ) {
                    clearable.insert( name );
                } else {
                    clear_colors_.erase( name );
                    if( name != "colortex0" ) {
                        persistent.insert( name );
                    }
                }
            }
            for( const auto &[name, color] : clear_colors_ ) {
                clearable.insert( name );
            }
            graph_ = build_pack_fullscreen_graph( external, descriptors, "display",
                                                  properties_.buffer_flips, {}, properties_.viewports, clearable, persistent );
            if( !graph_.error.empty() ) {
                return SDL_SetError( "%s", graph_.error.c_str() );
            }
            formats_.clear();
            rgb_attachments_.clear();
            constexpr auto rgba8 = fp3d_gpu::scene_target::color_format();
            const auto imported = declarations.find( "colortex0" );
            const auto scene_format = imported == declarations.end() ? rgba8 : color_format( imported->second );
            // RGB emulation needs a write mask. Native scene blending writes
            // RGBA and cannot borrow these emulated RGB attachments.
            if( imported != declarations.end() && rgb_emulated( imported->second ) ) {
                return SDL_SetError( "Emulated Iris RGB format is unsupported for colortex0" );
            }
            if( scene_format == SDL_GPU_TEXTUREFORMAT_INVALID ||
                !SDL_GPUTextureSupportsFormat( device, scene_format, SDL_GPU_TEXTURETYPE_2D,
                                               SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER ) ) {
                return SDL_SetError( "Unsupported Iris colortex0 format" );
            }
            formats_.emplace( "colortex0", scene_format );
            for( const auto &version : graph_.external ) {
                const auto name = version.substr( 0, version.find( ':' ) );
                if( ( !clearable.count( name ) && !graph_.history.count( version ) ) ||
                    std::find( external.begin(), external.end(), name ) != external.end() ) {
                    continue;
                }
                const auto declaration = declarations.find( name );
                const auto format = declaration == declarations.end() ? rgba8 : color_format( declaration->second );
                if( format == SDL_GPU_TEXTUREFORMAT_INVALID ) {
                    return SDL_SetError( "Unsupported Iris clear input format: %s", name.c_str() );
                }
                if( declaration != declarations.end() && rgb_emulated( declaration->second ) ) {
                    rgb_attachments_.insert( name );
                }
                formats_.emplace( name, format );
                clear_imports_.insert( version );
            }
            std::map<std::string, graph_image_spec> specs;
            for( const auto &pass : graph_.passes ) {
                if( !pass.depth_write.empty() ) {
                    const auto base = pass.depth_write.substr( 0, pass.depth_write.find( ':' ) );
                    constexpr auto depth_format = fp3d_gpu::scene_target::depth_format();
                    formats_.emplace( base, depth_format );
                    specs.emplace( pass.depth_write, graph_image_spec{ depth_format, 1, 1 } );
                }
                for( const auto &name : pass.writes ) {
                    const auto base = name.substr( 0, name.find( ':' ) );
                    const auto declaration = declarations.find( base );
                    const auto format = declaration == declarations.end() ? rgba8 : color_format( declaration->second );
                    if( format == SDL_GPU_TEXTUREFORMAT_INVALID ) {
                        return SDL_SetError( "Unsupported Iris color format %s for %s",
                                             declaration->second.c_str(), base.c_str() );
                    }
                    if( !SDL_GPUTextureSupportsFormat( device, format, SDL_GPU_TEXTURETYPE_2D,
                            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER ) ) {
                        return SDL_SetError( "GPU does not support Iris color attachment %s in %s",
                                             name.c_str(), pass.program.c_str() );
                    }
                    formats_.emplace( base, format );
                    const bool opaque_alpha = declaration != declarations.end() && rgb_emulated( declaration->second );
                    if( opaque_alpha ) {
                        rgb_attachments_.insert( base );
                    }
                    specs.emplace( name, graph_image_spec{ format, 1, 1, 1, SDL_GPU_TEXTURETYPE_2D, opaque_alpha } );
                }
            }
            graphics_program_set programs;
            if( !load_graphics_programs( device, graph_, artifacts, specs, programs ) ) {
                return false;
            }
            if( !pack_images.empty() || seed_shadow ) {
                auto textures = std::make_shared<gpu_texture_set>();
                if( seed_shadow ) {
                    auto empty = make_empty_shadow_depth( device, shadow_resolution_ );
                    if( !empty.image ) {
                        return false;
                    }
                    textures->emplace( "shadowtex1", std::move( empty ) );
                }
                for( const auto &image : pack_images ) {
                    auto uploaded = upload_texture( device, compiled.textures.at( image ) );
                    if( !uploaded.image ) {
                        return false;
                    }
                    textures->emplace( image, std::move( uploaded ) );
                }
                pack_textures_ = std::move( textures );
            }
            programs_ = std::move( programs );
            return true;
        }

        static gpu_sampled_texture make_empty_shadow_depth( SDL_GPUDevice *device, unsigned resolution ) {
            const graph_image_spec spec{ fp3d_gpu::scene_target::depth_format(), resolution, resolution };
            auto texture = create_graph_texture( device, spec,
                           SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET );
            if( !texture ) {
                return {};
            }
            auto *command = SDL_AcquireGPUCommandBuffer( device );
            if( !command ) {
                return {};
            }
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture = texture.get();
            depth.clear_depth = 1.f;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            auto *clear = SDL_BeginGPURenderPass( command, nullptr, 0, &depth );
            if( !clear ) {
                SDL_CancelGPUCommandBuffer( command );
                return {};
            }
            SDL_EndGPURenderPass( clear );
            return SDL_SubmitGPUCommandBuffer( command ) ?
                   gpu_sampled_texture{ std::move( texture ), spec } : gpu_sampled_texture{};
        }

        static gpu_sampled_texture upload_texture( SDL_GPUDevice *device,
                const fp3d_shaderpacks::iris_sampled_texture &data ) {
            using format_kind = fp3d_shaderpacks::iris_texture_format;
            constexpr std::array formats{ SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                         SDL_GPU_TEXTUREFORMAT_R8_UNORM,
                                         SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT };
            const graph_image_spec spec{
                formats[static_cast<size_t>( data.format )], data.width, data.height, data.depth,
                data.volume ? SDL_GPU_TEXTURETYPE_3D : SDL_GPU_TEXTURETYPE_2D
            };
            const auto component_bytes = data.format == format_kind::r8 ? 1u :
                                         data.format == format_kind::rgba16f ? 8u : 4u;
            if( static_cast<Uint64>( spec.width ) * spec.height * spec.depth * component_bytes != data.bytes.size() ||
                data.bytes.empty() || data.bytes.size() > SDL_MAX_UINT32 ) {
                SDL_SetError( "Invalid Iris texture upload payload" );
                return {};
            }
            auto texture = create_graph_texture( device, spec, SDL_GPU_TEXTUREUSAGE_SAMPLER );
            if( !texture ) {
                return {};
            }
            const SDL_GPUTransferBufferCreateInfo info{
                SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>( data.bytes.size() ), 0
            };
            const auto release = [device]( SDL_GPUTransferBuffer *buffer ) {
                SDL_ReleaseGPUTransferBuffer( device, buffer );
            };
            std::unique_ptr<SDL_GPUTransferBuffer, decltype( release )> transfer(
                SDL_CreateGPUTransferBuffer( device, &info ), release );
            auto *mapped = transfer ? SDL_MapGPUTransferBuffer( device, transfer.get(), false ) : nullptr;
            if( !mapped ) {
                return {};
            }
            SDL_memcpy( mapped, data.bytes.data(), data.bytes.size() );
            SDL_UnmapGPUTransferBuffer( device, transfer.get() );

            auto *command = SDL_AcquireGPUCommandBuffer( device );
            if( !command ) {
                return {};
            }
            auto *copy = SDL_BeginGPUCopyPass( command );
            if( !copy ) {
                SDL_CancelGPUCommandBuffer( command );
                return {};
            }
            const SDL_GPUTextureTransferInfo source{ transfer.get(), 0, spec.width, spec.height };
            const SDL_GPUTextureRegion destination{ texture.get(), 0, 0, 0, 0, 0, spec.width, spec.height, spec.depth };
            SDL_UploadToGPUTexture( copy, &source, &destination, false );
            SDL_EndGPUCopyPass( copy );
            if( !SDL_SubmitGPUCommandBuffer( command ) ) {
                return {};
            }
            return { std::move( texture ), spec };
        }

        static const char *shadow_translucent_name( std::string_view name ) {
            if( name == "shadow" ) {
                return "shadow_translucent_geometry";
            }
            if( name == "shadow_block" ) {
                return "shadow_translucent_block";
            }
            if( name == "shadow_entities" ) {
                return "shadow_translucent_entities";
            }
            return "";
        }

        static std::optional<unsigned> pass_order( std::string_view name ) {
            if( name == "final" ) {
                return 412;
            }
            if( name == "shadow_solid" ) {
                return 0;
            }
            if( name == "shadow_cutout" ) {
                return 1;
            }
            if( name == "shadow_block" ) {
                return 2;
            }
            if( name == "shadow_entities" ) {
                return 3;
            }
            if( name == "shadow" ) {
                return 4;
            }
            if( name == "shadow_water" ) {
                return 5;
            }
            if( name == "gbuffers_terrain_solid" ) {
                return 205;
            }
            if( name == "gbuffers_terrain_cutout" ) {
                return 206;
            }
            if( name == "gbuffers_terrain" ) {
                return 207;
            }
            if( name == "gbuffers_block" ) {
                return 208;
            }
            if( name == "gbuffers_entities" ) {
                return 209;
            }
            if( name == "gbuffers_basic" ) {
                return 210;
            }
            if( name == "gbuffers_water" ) {
                return 311;
            }
            const std::array<std::pair<std::string_view, unsigned>, 4> families{{
                { "shadowcomp", 6 }, { "prepare", 105 }, { "deferred", 211 }, { "composite", 312 }
            }};
            for( const auto &[family, first] : families ) {
                if( name.substr( 0, family.size() ) != family ) {
                    continue;
                }
                name.remove_prefix( family.size() );
                if( name.empty() ) {
                    return first;
                }
                unsigned number = 0;
                const auto end = name.data() + name.size();
                const auto parsed = std::from_chars( name.data(), end, number );
                if( parsed.ptr == end && parsed.ec == std::errc{} && name.front() != '0' &&
                    number < ( family == "shadowcomp" ? 99 : 100 ) ) {
                    return first + number;
                }
                break;
            }
            return std::nullopt;
        }

        static bool rgb_emulated( const std::string &name ) {
            return name == "RGB8" || name == "RGB16F";
        }

        static SDL_GPUTextureFormat color_format( const std::string &name ) {
            if( name == "RG16F" ) {
                return SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
            }
            if( name == "R32F" ) {
                return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
            }
            if( name == "RGBA8" || name == "RGB8" ) {
                return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            }
            if( name == "RGBA16" ) {
                return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM;
            }
            if( name == "RGBA16F" || name == "RGB16F" ) {
                return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            }
            if( name == "RGBA32F" ) {
                return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
            }
            if( name == "R11F_G11F_B10F" ) {
                return SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT;
            }
            return SDL_GPU_TEXTUREFORMAT_INVALID;
        }

        std::uint64_t revision_ = 0;
        unsigned shadow_resolution_ = 1024;
        float wetness_half_life_ = 600.f;
        float dryness_half_life_ = 200.f;
        pack_fullscreen_graph graph_;
        graphics_program_set programs_;
        std::shared_ptr<const gpu_texture_set> pack_textures_;
        pack_properties properties_;
        std::optional<material_id_map> held_items_;
        std::string water_fallback_reason_;
        std::map<std::string, SDL_GPUTextureFormat> formats_;
        std::set<std::string> rgb_attachments_;
        std::map<std::string, std::array<float, 4>> clear_colors_;
        std::set<std::string> clear_imports_;
};
} // namespace fp3d_iris
#endif
#endif
