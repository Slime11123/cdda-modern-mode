#pragma once
#ifndef CATA_SRC_FP3D_SHADERPACKS_INTERNAL_H
#define CATA_SRC_FP3D_SHADERPACKS_INTERNAL_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdint>
#include <filesystem>
#include <string>

namespace fp3d_shaderpacks::detail
{
extern std::string choice;
extern std::string report;
extern std::uint64_t selection_revision;
extern int quality_level;
extern int mc_intensity;
extern bool wet_ssr;
extern bool shadow_volume;
extern bool native_local_lights;
std::filesystem::path root_path();
void restore_choice();
}
#endif
#endif
