#include "fp3d_iris_frame.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>

#include "calendar.h"
#include "fp3d_camera.h"
#include "fp3d_iris_block_light.h"
#include "fp3d_snapshot.h"

namespace fp3d_iris
{
namespace
{
// Iris publishes category ordinals. Biome IDs instead belong to the host's
// biome registry, so CDDA uses a small virtual registry with matching names.
constexpr std::array<std::string_view, 19> categories{
    "NONE", "TAIGA", "EXTREME_HILLS", "JUNGLE", "MESA", "PLAINS", "SAVANNA", "ICY", "THE_END",
    "BEACH", "FOREST", "OCEAN", "DESERT", "RIVER", "SWAMP", "MUSHROOM", "NETHER", "MOUNTAIN", "UNDERGROUND"
};
constexpr std::array<std::string_view, 13> biomes{
    "PLAINS", "FOREST", "SWAMP", "RIVER", "OCEAN", "BEACH", "DRIPSTONE_CAVES", "DESERT",
    "SNOWY_PLAINS", "TAIGA", "JUNGLE", "PALE_GARDEN", "BADLANDS"
};
constexpr std::array<std::string_view, 3> precipitation_types{ "NONE", "RAIN", "SNOW" };

enum biome_id { plains, forest, swamp, river, ocean, beach, caves, desert, snowy_plains, taiga, jungle };
enum biome_category { cat_taiga = 1, cat_jungle = 3, cat_plains = 5, cat_icy = 7, cat_beach = 9,
                      cat_forest, cat_ocean, cat_desert, cat_river, cat_swamp, cat_underground = 18 };
} // namespace

std::optional<int> biome_constant( std::string_view name )
{
    const auto lookup = [&]( std::string_view prefix, const auto &table ) -> std::optional<int> {
        if( name.substr( 0, prefix.size() ) != prefix ) {
            return std::nullopt;
        }
        const auto found = std::find( table.begin(), table.end(), name.substr( prefix.size() ) );
        return found != table.end() ? std::optional<int>( found - table.begin() ) : std::nullopt;
    };
    if( const auto category = lookup( "CAT_", categories ) ) {
        return category;
    }
    if( const auto precip = lookup( "PPT_", precipitation_types ) ) {
        return precip;
    }
    return lookup( "BIOME_", biomes );
}

biome_inputs map_scene_biome( const fp3d_snapshot::scene_biome &biome )
{
    using landscape = fp3d_snapshot::landscape_climate;
    const float humidity = std::clamp( biome.mean_humidity / 100.f, 0.f, 1.f );
    const float celsius = biome.mean_temperature_c;
    // CDDA has regional mean humidity, not annual precipitation totals.
    // Use the mean as a stable climate estimate independent of today's rain.
    biome_inputs mapped{ plains, cat_plains, celsius <= 0.f ? 2 : 1, humidity,
                         std::clamp( celsius / 25.f + 0.15f, -0.7f, 2.f ) };
    switch( biome.landscape ) {
        case landscape::underground:
            mapped.id = caves;
            mapped.category = cat_underground;
            mapped.precipitation = 0;
            break;
        case landscape::river:
        case landscape::lake:
            mapped.id = river;
            mapped.category = cat_river;
            break;
        case landscape::ocean:
            mapped.id = ocean;
            mapped.category = cat_ocean;
            break;
        case landscape::beach:
            mapped.id = beach;
            mapped.category = cat_beach;
            break;
        case landscape::wetland:
            mapped.id = swamp;
            mapped.category = cat_swamp;
            break;
        case landscape::forest:
            if( celsius <= 0.f ) {
                mapped.id = taiga;
                mapped.category = cat_taiga;
            } else if( celsius >= 15.f && humidity >= 0.75f ) {
                mapped.id = jungle;
                mapped.category = cat_jungle;
            } else {
                mapped.id = forest;
                mapped.category = cat_forest;
            }
            break;
        case landscape::plains:
            if( humidity <= 0.25f && celsius >= 5.f ) {
                mapped.id = desert;
                mapped.category = cat_desert;
                mapped.precipitation = 0;
            } else if( celsius <= 0.f ) {
                mapped.id = snowy_plains;
                mapped.category = cat_icy;
            }
            break;
    }
    return mapped;
}

world_clock read_world_clock( const time_point &time )
{
    constexpr std::int64_t seconds_per_day = to_seconds<std::int64_t>( 1_days );
    constexpr std::int64_t ticks_per_day = 24000;
    // Iris day tick zero is at 06:00. Use the same offset for the
    // elapsed day and the within-day tick so the pair rolls over together.
    const auto seconds = to_seconds<std::int64_t>( time - calendar::turn_zero );
    const auto shifted = seconds + to_seconds<std::int64_t>( 18_hours );
    const auto cycle = ( shifted % seconds_per_day + seconds_per_day ) % seconds_per_day;
    const auto dawn = sunrise( time );
    const auto dusk = sunset( time );
    float sun_angle = static_cast<float>( cycle ) / seconds_per_day;
    if( dawn < dusk ) {
        if( time >= dawn && time < dusk ) {
            sun_angle = static_cast<float>( 0.5 * ( ( time - dawn ) / ( dusk - dawn ) ) );
        } else {
            const auto night_start = time < dawn ? sunset( time - 1_days ) : dusk;
            const auto night_end = time < dawn ? dawn : sunrise( time + 1_days );
            if( night_end > night_start ) {
                sun_angle = static_cast<float>( 0.5 + 0.5 * ( ( time - night_start ) / ( night_end - night_start ) ) );
            }
        }
        sun_angle = std::clamp( sun_angle, 0.f, std::nextafter( 1.f, 0.f ) );
    }
    // CDDA starts with a new moon. Iris starts with a full moon four phases later.
    const int moon = ( static_cast<int>( get_moon_phase( time ) ) + 4 ) % MOON_PHASE_MAX;
    return { static_cast<int>( cycle * ticks_per_day / seconds_per_day ),
             static_cast<int>( std::max<std::int64_t>( 0, ( shifted - cycle ) / seconds_per_day ) ),
             sun_angle, moon };
}

std::optional<frame_inputs> frame_history::prepare( const fp3d_snapshot::scene_snapshot &scene,
        const fp3d_camera::pose &camera, const fp3d_gpu_camera_math::matrix4 &clip_from_scene,
        int width, int height, bool overhead, world_clock clock, std::uint64_t history_epoch,
        std::array<int, 2> eye_brightness, float lightning_flash ) const
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
        last_view_->eye_in_water == view.eye_in_water && last_view_->map_origin == view.map_origin ) {
        previous = last_view_;
    }
    const auto &weather = scene.weather;
    const auto &sky = scene.sky;
    const float precipitation = std::clamp( weather.intensity, 0.f, 1.f );
    const float daylight = std::clamp( sky.daylight, 0.f, 1.f );
    const float clouds = std::clamp( weather.clouds, 0.f, 1.f ) * .85f;
    const float flash = std::clamp( lightning_flash, 0.f, 1.f );
    // Sample the native sky gradient at the zenith and horizon, excluding
    // directional sun and moon discs. The Iris flash is achromatic.
    const auto sky_sample = [&]( float horizon ) {
        const float glow = std::clamp( sky.twilight, 0.f, 1.f ) * horizon * horizon;
        std::array<float, 3> clear{
            .008f + daylight * ( .20f + .35f * horizon ) + .35f * glow,
            .012f + daylight * ( .43f + .26f * horizon ) + .12f * glow,
            .035f + daylight * ( .76f + .10f * horizon ) - .015f * glow
        };
        const float overcast = .025f + daylight * ( .31f + .16f * horizon );
        for( float &channel : clear ) {
            channel = std::clamp( channel * ( 1.f - clouds ) + overcast * clouds, 0.f, 1.f );
        }
        const float grey = ( clear[0] + clear[1] + clear[2] ) / 3.f;
        for( float &channel : clear ) {
            channel = std::clamp( channel * ( 1.f - flash ) + ( grey + .5f ) * flash, 0.f, 1.f );
        }
        return clear;
    };
    const auto fog = fp3d_weather::fog_parameters( weather, daylight, flash,
                     overhead ? std::max( 1.f, camera.eye.height / fp3d_camera::eye_height ) : 1.f );
    return frame_inputs{
        view, previous, clock, history_epoch, { sky.sun_x, sky.sun_y, sky.sun_z },
        { weather.wind_x, weather.wind_z }, sky.daylight, sky.twilight, sky.moonlight,
        weather.particles == fp3d_weather::precipitation::rain ? precipitation : 0.f,
        weather.particles == fp3d_weather::precipitation::snow ? precipitation : 0.f,
        weather.clouds, weather.fog_end, sky.visible, weather.outdoors, weather.lightning,
        previous ? next_frame_counter_ : 0, map_scene_biome( scene.biome ), eye_brightness,
        sky_sample( 0.f ),
        fog.end > fog.start ? std::array<float, 3>{ fog.color[0], fog.color[1], fog.color[2] } : sky_sample( 1.f ),
        fog.start, fog.end, scene.player_blind, scene.player_darkness,
        scene.player_blind ? 0.f : std::clamp( scene.player_night_vision, 0.f, 1.f ),
        std::clamp( scene.held_light, 0, max_block_light )
    };
}

void frame_history::presented( const frame_inputs &frame )
{
    last_view_ = frame.current;
    last_epoch_ = frame.history_epoch;
    next_frame_counter_ = ( frame.frame_counter + 1 ) % 720720;
}

void frame_history::reset()
{
    last_view_.reset();
}
} // namespace fp3d_iris
#endif
