#pragma once
#ifndef CATA_SRC_FP3D_IRIS_PACK_H
#define CATA_SRC_FP3D_IRIS_PACK_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "fp3d_iris_properties.h"

namespace fp3d_iris
{
enum class shader_stage { vertex, fragment, geometry, tess_control, tess_evaluation, compute };

struct pack_inventory {
    std::string error;
    // Path relative to the package root for ZIPs, or the folder being inspected.
    std::string shader_root;
    // All file paths relative to shader_root, including properties and assets.
    std::vector<std::string> files;
    // The key is a program path without the stage extension. Folder names are
    // preserved for dimension selection, which follows the inventory step.
    std::map<std::string, std::map<shader_stage, std::string>> programs;
};

pack_inventory inspect_pack( const std::filesystem::path &path );

struct pack_text {
    std::string error;
    std::map<std::string, std::string> files;
};

struct expanded_shader {
    std::string error;
    std::string source;
    // Index is the GLSL source-string number used by #line.
    std::vector<std::string> source_paths;
};

pack_text load_pack_text( const std::filesystem::path &path, const pack_inventory &inventory );
expanded_shader expand_includes( const pack_text &pack, const std::string &entry );

struct pack_source {
    std::filesystem::path path;
    pack_inventory inventory;
    pack_text text;
    pack_properties properties;
    std::string program_directory;
    std::vector<std::string> programs;
    std::string error;
};

bool read_pack_asset( const pack_source &pack, const std::string &name, std::string &data );

// CDDA world views resolve against the pack's Overworld programs.
pack_source prepare_pack_source( const std::filesystem::path &path );

struct program_source_job {
    std::string name;
    std::map<shader_stage, std::string> stages;
    std::string error;
};

struct pack_compilation {
    pack_properties properties;
    std::vector<program_source_job> programs;
    std::string error;
};

// Options are resolved once per selection. Expand each job's stages on demand
// so active compiler jobs do not retain all expanded sources at once.
pack_compilation prepare_pack_compilation( const pack_source &pack, const property_macros &options );
} // namespace fp3d_iris
#endif
#endif
