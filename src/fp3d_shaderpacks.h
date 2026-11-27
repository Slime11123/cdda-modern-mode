#pragma once
#ifndef CATA_SRC_FP3D_SHADERPACKS_H
#define CATA_SRC_FP3D_SHADERPACKS_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "fp3d_iris_layout.h"
#include "fp3d_iris_properties.h"
namespace fp3d_mcshader { struct profile; }
namespace fp3d_iris { struct pack_source; }
namespace fp3d_shaderpacks
{
struct entry {
    std::string id;
    std::string title;
    std::string note;
    bool compatible = false;
};
std::string directory();
bool open_folder();
std::vector<entry> scan( std::string *scan_error = nullptr );
// Refresh the selected pack before the next world frame.
void request_reload();
std::string selected();
struct iris_selection {
    std::uint64_t revision;
    std::shared_ptr<const fp3d_iris::pack_source> source;
};
iris_selection selected_iris_pack();
inline bool is_iris_terrain_program( std::string_view name )
{
    return name == "gbuffers_terrain" || name == "gbuffers_terrain_solid" ||
           name == "gbuffers_terrain_cutout";
}
inline bool is_iris_model_program( std::string_view name )
{
    return name == "gbuffers_basic" || name == "gbuffers_block" || name == "gbuffers_entities";
}

inline bool is_iris_shadow_program( std::string_view name )
{
    return name == "shadow" || name == "shadow_solid" || name == "shadow_cutout" ||
           name == "shadow_water" || name == "shadow_entities" || name == "shadow_block";
}

enum class iris_shadow_caster { unclassified, terrain, water, entity, block };

struct iris_compiled_stage {
    std::string program;
    std::string stage;
    std::filesystem::path bytecode;
    std::string cache_key;
    fp3d_iris::shader_layout layout;
};
enum class iris_texture_format { rgba8, r8, rgba16f };

struct iris_sampled_texture {
    unsigned width = 0;
    unsigned height = 0;
    unsigned depth = 1;
    bool volume = false;
    iris_texture_format format = iris_texture_format::rgba8;
    std::vector<std::uint8_t> bytes;
    fp3d_iris::texture_sampling sampling{ false, true };
};

struct iris_compile_state {
    std::uint64_t revision = 0;
    bool running = false;
    std::vector<iris_compiled_stage> stages;
    std::vector<std::string> diagnostics;
    fp3d_iris::pack_properties properties{};
    std::map<std::string, iris_sampled_texture> textures{};
    std::map<std::string, std::map<std::string, std::string>> texture_bindings{};
    std::vector<std::string> water_diagnostics{};
    std::vector<std::string> unused_minecraft_programs{};
    bool needs_eye_brightness = false;
};
const iris_compile_state &poll_iris_compilation();
void stop_iris_compilation();
const fp3d_mcshader::profile *selected_gpu_profile();
bool choose( const std::string &id );
float screen_brightness();
std::string status();
bool shadows_enabled();
void set_shadows_enabled( bool enabled );
// Real depth/scene-colour raymarching in MC visual bridge only.
bool wet_ssr_enabled();
void set_wet_ssr_enabled( bool enabled );
bool shadow_volume_enabled();
void set_shadow_volume_enabled( bool enabled );
// Dynamic native electric/portable lights. On for compatible shader bridges;
// unsupported direct final.fsh remains unchanged.
bool local_lights_enabled();
void set_local_lights_enabled( bool enabled );
bool local_shadows_enabled();
void set_local_shadows_enabled( bool enabled );
// Opt-in, Ultra-only normal/roughness/metalness lighting prototype; default Off.
bool pbr_enabled();
void set_pbr_enabled( bool enabled );
int pbr_strength_percent();
void set_pbr_strength_percent( int percent );
int quality();
void set_quality( int level );
// Minecraft bridge-only enhancement control, saved next to the shader ZIP.
// The original Minecraft shader source is unaffected in direct-final mode.
int mc_intensity_percent();
void set_mc_intensity_percent( int percent );
} // namespace fp3d_shaderpacks
#endif
#endif
