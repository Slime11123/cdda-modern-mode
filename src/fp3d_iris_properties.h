#pragma once
#ifndef CATA_SRC_FP3D_IRIS_PROPERTIES_H
#define CATA_SRC_FP3D_IRIS_PROPERTIES_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "fp3d_iris_render_graph.h"

namespace fp3d_iris
{
struct pack_text;
using pack_buffer_flips = std::map<std::string, std::map<std::string, bool>>;

struct buffer_dimension {
    double value = 0;
    bool relative = false;
};

struct property_record {
    std::string key;
    std::string value;
    size_t line = 0;
    bool directive = false;
    bool conditional = false;
};

struct material_rule {
    int id = 0;
    std::vector<std::string> selectors;
    size_t line = 0;
    bool conditional = false;
};

struct custom_uniform_expression {
    std::string type;
    std::string expression;
    size_t line;
    bool variable;
};

struct pack_properties {
    std::string error;
    // Keep directive and property order for later macro preprocessing.
    std::map<std::string, std::vector<property_record>> documents;
    std::map<std::string, std::vector<std::string>> screens;
    std::map<std::string, std::vector<std::string>> profiles;
    std::map<std::string, std::vector<std::string>> dimensions;
    // Present when inspected with resolved shader options. An absent program
    // is enabled by default.
    std::map<std::string, bool> program_enabled;
    std::map<std::string, custom_uniform_expression> uniform_expressions;
    pack_buffer_flips buffer_flips;
    std::string noise_texture;
    // Stage family, sampler, and sampler dimension -> declaration tokens.
    std::map<std::array<std::string, 3>, std::vector<std::string>> custom_textures;
    std::map<std::string, graph_viewport> viewports;
    std::map<std::string, std::array<buffer_dimension, 2>> buffer_sizes;
    std::map<std::string, std::vector<material_rule>> materials;
    std::vector<std::string> sliders;
    std::map<std::string, std::map<std::string, std::string>> translations;
};

using property_macros = std::map<std::string, std::string>;

std::vector<property_record> parse_properties( std::string_view source );
pack_properties inspect_properties( const pack_text &pack, const property_macros *macros = nullptr );
} // namespace fp3d_iris
#endif
#endif
