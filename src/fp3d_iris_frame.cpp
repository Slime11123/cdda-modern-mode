#include "fp3d_iris_frame.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>

#include "calendar.h"
#include "fp3d_camera.h"
#include "fp3d_snapshot.h"

namespace fp3d_iris
{
world_clock read_world_clock( const time_point &time )
{
    return { to_turn<int>( time ), to_seconds<int>( time_past_midnight( time ) ) };
}

std::optional<frame_inputs> frame_history::prepare( const fp3d_snapshot::scene_snapshot &scene,
        const fp3d_camera::pose &camera, const fp3d_gpu_camera_math::matrix4 &clip_from_scene,
        int width, int height, bool overhead, world_clock clock, std::uint64_t history_epoch ) const
{
    if( width <= 0 || height <= 0 || !std::isfinite( camera.eye.x ) ||
        !std::isfinite( camera.eye.height ) || !std::isfinite( camera.eye.y ) ||
        !std::all_of( clip_from_scene.elements.begin(), clip_from_scene.elements.end(),
    []( float element ) { return std::isfinite( element ); } ) ) {
        return std::nullopt;
    }
    const frame_view view{
        clip_from_scene, { scene.origin.x(), scene.origin.y(), scene.origin.z() },
        { camera.eye.x, camera.eye.height, camera.eye.y }, width, height, overhead,
        !overhead && scene.player_underwater ? 1 : 0
    };
    std::optional<frame_view> previous;
    if( last_view_ && last_epoch_ == history_epoch && last_view_->width == width &&
        last_view_->height == height && last_view_->overhead == overhead &&
        last_view_->eye_in_water == view.eye_in_water ) {
        previous = last_view_;
    }
    const auto &weather = scene.weather;
    const auto &sky = scene.sky;
    const float precipitation = std::clamp( weather.intensity, 0.f, 1.f );
    return frame_inputs{
        view, previous, clock, history_epoch, { sky.sun_x, sky.sun_y, sky.sun_z },
        { weather.wind_x, weather.wind_z }, sky.daylight, sky.twilight, sky.moonlight,
        weather.particles == fp3d_weather::precipitation::rain ? precipitation : 0.f,
        weather.particles == fp3d_weather::precipitation::snow ? precipitation : 0.f,
        weather.clouds, weather.fog_end, sky.visible, weather.outdoors, weather.lightning
    };
}

void frame_history::presented( const frame_inputs &frame )
{
    last_view_ = frame.current;
    last_epoch_ = frame.history_epoch;
}

void frame_history::reset()
{
    last_view_.reset();
}
} // namespace fp3d_iris
#endif
