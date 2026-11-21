#pragma once
#ifndef CATA_SRC_FP3D_SHADERPACKS_H
#define CATA_SRC_FP3D_SHADERPACKS_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <string>
#include <vector>
namespace fp3d_mcshader { struct profile; }
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
const fp3d_mcshader::profile *selected_gpu_profile();
bool choose( const std::string &id );
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
