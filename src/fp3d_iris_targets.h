#pragma once
#ifndef CATA_SRC_FP3D_IRIS_TARGETS_H
#define CATA_SRC_FP3D_IRIS_TARGETS_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <string>
#include <string_view>
#include <set>
#include <vector>

#include "fp3d_iris_properties.h"
#include "fp3d_iris_render_graph.h"

namespace fp3d_iris
{
struct render_targets {
    // Empty when the shader has no explicit declaration. Reflection and the
    // program's default attachment rules must supply the layout in that case.
    std::vector<unsigned> slots;
    std::string error;
};

enum class color_buffer_bank { scene, shadow };
enum class target_source { raw, preprocessed };

render_targets read_render_targets( std::string_view fragment,
                                    target_source source = target_source::raw );
std::vector<std::string> graph_color_outputs( const render_targets &targets,
        color_buffer_bank bank, unsigned version );

struct pack_fullscreen_program {
    std::string name;
    std::string_view preprocessed_fragment;
    // Attachment names in the same order as the compiled sampler bindings.
    std::vector<std::string> samplers;
    color_buffer_bank bank = color_buffer_bank::scene;
    pass_kind kind = pass_kind::fullscreen;
    std::string depth_buffer{};
    std::vector<std::string> vertex_samplers{};
    bool needs_native_color = false;
    std::string depth_snapshot{};
};

struct pack_fullscreen_graph {
    std::vector<std::string> external;
    std::vector<graph_pass> passes;
    std::string output;
    std::string error;
    std::map<std::string, std::string> history{};
};

// Build ordered graphics passes. Fullscreen stages write the alternate side,
// while geometry stages update the current version. External images use ":0"
// and supplied alternates use ":alt". An uninitialized side cannot be read.
pack_fullscreen_graph build_pack_fullscreen_graph(
    const std::vector<std::string> &initial_images,
    const std::vector<pack_fullscreen_program> &programs, const std::string &final_image,
    const pack_buffer_flips &flips = {}, const std::vector<std::string> &initial_alternates = {},
    const std::map<std::string, graph_viewport> &viewports = {},
    const std::set<std::string> &clearable = {},
    const std::set<std::string> &persistent = {} );
} // namespace fp3d_iris
#endif
#endif
