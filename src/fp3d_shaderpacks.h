#pragma once
#ifndef CATA_SRC_FP3D_SHADERPACKS_H
#define CATA_SRC_FP3D_SHADERPACKS_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <string>
#include <vector>
#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif
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
std::vector<entry> scan();
// Recompile the current GLSL on next world frame without restarting the game.
void request_reload();
std::string selected();
bool choose( const std::string &id );
std::string status();
bool usable();
// Built-in GLSL understands the native shadow-map uniforms; custom post.frag may not.
bool supports_native_lighting();
bool shadows_enabled();
void set_shadows_enabled( bool enabled );
// Real depth/scene-colour raymarching in MC visual bridge only.
bool wet_ssr_enabled();
void set_wet_ssr_enabled( bool enabled );
bool shadow_volume_enabled();
void set_shadow_volume_enabled( bool enabled );
// Compile the selected pack before world rendering so daylight/shadows work
// on the first native gameplay frame rather than one turn later.
bool prepare();
struct environment {
    GLuint depth = 0;
    float daylight = 1;
    float clouds = 0;
    float fog_end = 0;
    bool outdoors = false; // avatar weather exposure; not a sunlight switch
    float sun_u = -1;
    float sun_v = -1;
    float sun_strength = 0;
    GLuint shadow = 0;
    float shadow_radius = 30;
    float shadow_distance = 78;
    float shadow_strength = 0;
    int shadow_resolution = 0;
    float yaw = 0;
    float pitch = 0;
    float aspect = 1;
    float camera_offset_x = 0;
    float camera_offset_z = 0;
    float camera_height = 1.6f;
    float rain_strength = 0.0f;
    float shadow_x[3] = { 1, 0, 0 };
    float shadow_y[3] = { 0, 1, 0 };
    float shadow_sun[3] = { 0, 1, 0 };
    // Material classification FBO; optional when the driver has no extra FBO.
    GLuint reflection_mask = 0;
    bool sky_visible = false; // sky seen through openings, not avatar exposure
};
int quality();
void set_quality( int level );
// Minecraft bridge-only enhancement control, saved next to the shader ZIP.
// The original Minecraft shader source is unaffected in direct-final mode.
int mc_intensity_percent();
void set_mc_intensity_percent( int percent );
float mc_visual_strength();
// Operates solely on the world FBO; never processes native SDL/ImGui menus.
GLuint apply( GLuint scene_color, int width, int height, environment values = {} );
GLuint processed_framebuffer();
void release();
} // namespace fp3d_shaderpacks
#endif
#endif
