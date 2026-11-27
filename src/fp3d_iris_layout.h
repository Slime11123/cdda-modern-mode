#pragma once
#ifndef CATA_SRC_FP3D_IRIS_LAYOUT_H
#define CATA_SRC_FP3D_IRIS_LAYOUT_H
#if defined(TILES) && defined(ENABLE_FP3D)

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fp3d_iris
{
struct sampler_slot {
    std::string name;
    std::string type;
    bool readonly_texture = false;
};

enum class uniform_scalar { floating, signed_integer, unsigned_integer };

using uniform_value = std::variant<std::vector<float>, std::vector<std::int32_t>,
      std::vector<std::uint32_t>>;

struct uniform_member {
    uniform_scalar scalar;
    unsigned offset;
    unsigned rows;
    unsigned columns;
    unsigned matrix_stride;
    std::optional<uniform_value> default_value{};
};

struct texture_sampling {
    bool linear = false;
    bool repeat = false;
};

struct shader_layout {
    std::string stage;
    std::string format;
    std::string bytecode_sha256;
    std::string cache_key;
    std::vector<sampler_slot> samplers;
    unsigned frame_size = 0;
    std::map<std::string, uniform_member> uniforms;
    std::vector<unsigned> input_locations;
    std::vector<unsigned> output_locations;
    std::optional<std::string> target_directive;
    std::map<std::string, std::string> buffer_formats;
    std::map<std::string, bool> buffer_clears;
    std::map<std::string, std::array<float, 4>> buffer_clear_colors;
    std::optional<unsigned> shadow_resolution;
    std::optional<unsigned> noise_resolution;
    std::optional<float> wetness_half_life;
    std::optional<float> dryness_half_life;
    std::string error;
};

shader_layout parse_shader_layout( std::string json );

struct compiled_shader {
    shader_layout layout;
    std::string bytecode;
    std::string error;
};

// Read a compiled artifact and its .json sidecar as a single verified pair.
compiled_shader load_compiled_shader( const std::filesystem::path &bytecode_path,
                                      std::string_view expected_cache_key = {} );

struct sampler_inputs {
    std::vector<std::string> images;
    std::string error;
};

sampler_inputs resolve_sampler_inputs( const shader_layout &layout,
                                       const std::map<std::string, std::string> &resources );

struct pack_fullscreen_program;
// Map declared sampler names to unversioned graph image names. The graph
// resolves each image to the version visible at that stage.
std::string bind_program_sampler_inputs( pack_fullscreen_program &program,
        const shader_layout &vertex, const shader_layout &fragment,
        const std::map<std::string, std::string> &resources );

// The reflected fragment layout must outlive the graph program description.
std::string bind_compiled_program( pack_fullscreen_program &program,
                                  const shader_layout &vertex, const shader_layout &fragment,
                                  const std::map<std::string, std::string> &resources );

using uniform_values = std::map<std::string, uniform_value>;

struct pack_properties;
using uniform_smoothing = std::map<std::pair<std::string, size_t>, double>;
double smooth_uniform_value( double previous, double target, double half_life, double elapsed_seconds );
std::string apply_custom_uniforms( const pack_properties &properties,
                                  const std::set<std::string> &required, uniform_values &values,
                                  uniform_smoothing *smoothing = nullptr, double elapsed_seconds = 0 );

struct frame_inputs;
uniform_values native_frame_uniform_values( const frame_inputs &frame );

struct packed_uniforms {
    std::vector<std::uint8_t> bytes;
    std::vector<std::string> missing;
    std::string error;
};

// Matrices are supplied in GLSL column order. Missing fields are reported
// separately so the caller can apply the pack's explicit fallback policy.
packed_uniforms pack_frame_uniforms( const shader_layout &layout,
                                    const uniform_values &values );
} // namespace fp3d_iris
#endif
#endif
