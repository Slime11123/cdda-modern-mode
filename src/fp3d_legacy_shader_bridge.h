#pragma once
#ifndef CATA_SRC_FP3D_LEGACY_SHADER_BRIDGE_H
#define CATA_SRC_FP3D_LEGACY_SHADER_BRIDGE_H

#include <algorithm>
#include <cmath>
#include <string>

#include "fp3d_gpu_world_qa.h"
#include "fp3d_mcshader.h"
#include "fp3d_shaderpacks.h"

// Temporary comparison path for the fixed Minecraft visual bridge.
namespace fp3d_legacy_shader_bridge
{
struct selection {
    const fp3d_mcshader::profile *pack;
    float builtin_style;

    bool active() const {
        return pack || builtin_style > 0;
    }
};

inline selection current( bool post_enabled )
{
    const std::string id = post_enabled ? fp3d_shaderpacks::selected() : "off";
    const float builtin_style = id == "cinematic" ? 1.f : id == "warm" ? 2.f : 0.f;
    const auto *pack = post_enabled && builtin_style == 0 ?
                       fp3d_shaderpacks::selected_gpu_profile() : nullptr;
    return { pack, builtin_style };
}

inline void configure_frame( const selection &selected, fp3d_gpu_world_qa::prepared_world &world,
                             const fp3d_snapshot::scene_snapshot &scene, const fp3d_camera::pose &view,
                             bool overhead, int width, int height )
{
    const auto *pack = selected.pack;
    const float builtin_style = selected.builtin_style;
    if( fp3d_shaderpacks::quality() == 3 && fp3d_shaderpacks::pbr_enabled() &&
        fp3d_shaderpacks::pbr_strength_percent() > 0 && scene.sky.sun_y > .08f ) {
        auto &lighting = world.atmosphere;
        lighting.pbr_eye_strength = { view.eye.x, view.eye.height, view.eye.y,
                                      fp3d_shaderpacks::pbr_strength_percent() / 100.f };
        lighting.pbr_sun_daylight = { scene.sky.sun_x, scene.sky.sun_y, scene.sky.sun_z,
                                      scene.sky.daylight * ( 1.f - .6f * scene.weather.clouds ) };
    }
    auto &photon = world.photon.emplace();
    photon.texel_quality_daylight = { 1.f / width, 1.f / height,
                                      static_cast<float>( fp3d_shaderpacks::quality() ),
                                      scene.sky.daylight };
    photon.grade = { 1.f, 0.f, builtin_style == 2.f ? 1.1f : .75f, 1.f };
    photon.depth_and_ao[1] = !pack || pack->ao ? 1.f : 0.f;
    photon.depth_and_ao[3] = builtin_style;
    const auto &m = world.camera.elements;
    const float horizontal = std::hypot( m[0], m[1], m[2] );
    const float vertical = std::hypot( m[4], m[5], m[6] );
    photon.projection = { horizontal, vertical, fp3d_camera::near_clip, fp3d_camera::far_clip };
    photon.view_up_and_reflections = { m[1] / horizontal, m[5] / vertical, m[13],
                                       pack && fp3d_shaderpacks::wet_ssr_enabled() ? 1.f : 0.f };
    photon.water_east = { m[0] / horizontal, m[4] / vertical, m[12],
                          static_cast<float>( scene.origin.x() ) + view.eye.x };
    photon.water_south = { m[2] / horizontal, m[6] / vertical, m[14],
                           static_cast<float>( scene.origin.y() ) + view.eye.y };
    const float rain = scene.weather.particles == fp3d_weather::precipitation::rain ?
                       std::clamp( scene.weather.intensity, 0.f, 1.f ) : 0.f;
    if( pack ) {
        photon.grade = { pack->saturation, pack->warmness, pack->bloom ? pack->glow : 0.f,
                         fp3d_shaderpacks::mc_intensity_percent() / 100.f };
        photon.water_parameters = { SDL_GetTicks() / 1000.f, pack->water_wave_strength, rain,
                                pack->has_water_waves ? 1.f : 0.f };
        photon.wave_shape = { static_cast<float>( pack->water_waves.iterations ),
                          pack->water_waves.frequency, pack->water_waves.persistence,
                          pack->water_waves.lacunarity };
        photon.wave_options = { pack->water_waves.speed_still,
                            pack->water_waves.height_variation ? 1.f : 0.f,
                            pack->water_texture, std::clamp( scene.weather.clouds, 0.f, 1.f ) };
        photon.water_absorption = { pack->water_absorption[0], pack->water_absorption[1],
                                 pack->water_absorption[2], 0.f };
    }
    photon.eye_volume = { view.eye.x, view.eye.height, view.eye.y, 0.f };
    if( !overhead && pack && pack->volumetric && fp3d_shaderpacks::shadow_volume_enabled() ) {
        const auto &sun = world.atmosphere.sun_direction_strength;
        photon.sun_volume = { sun[0], sun[1], sun[2], sun[3] / .68f };
    }
    if( ( !pack || pack->volumetric ) && scene.origin.z() >= 0 && scene.sky.sun_y > .08f ) {
        const auto sun = world.camera.transform(
                             scene.sky.sun_x, scene.sky.sun_y, scene.sky.sun_z, 0 );
        if( sun[3] > .01f ) {
            const float u = .5f + .5f * sun[0] / sun[3];
            const float v = .5f - .5f * sun[1] / sun[3];
            if( u > -.3f && u < 1.3f && v > -.3f && v < 1.3f ) {
                const float strength = scene.sky.daylight *
                                       std::clamp( 1.f - scene.weather.clouds, 0.f, 1.f ) *
                                       std::min( 1.f, scene.sky.sun_y * 4.f );
                photon.sun_position_strength = { u, v, strength, 0 };
            }
        }
    }
}
} // namespace fp3d_legacy_shader_bridge
#endif
