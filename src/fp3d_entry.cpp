#include "fp3d_entry.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>

#include "avatar.h"
#include "calendar.h"
#include "display.h"
#include "catacharset.h"
#include "fp3d_compositor.h"
#include "game.h"
#include "kill_tracker.h"
#include "map.h"
#include "output.h"
#include "overmapbuffer.h"
#include "profession.h"
#include "sdl_font.h"
#include "weather.h"
#include "weather_type.h"

namespace fp3d_entry
{
namespace
{
constexpr std::uint32_t fade_in = 900;
constexpr std::uint32_t row_delay = 100;
constexpr std::uint32_t hold_until = fade_in + 6 * row_delay + 3500;
constexpr std::uint32_t duration = hold_until + 1500;
timeline animation;
summary content;
SDL_Texture_Ptr backdrop;

float smooth( float value )
{
    value = std::clamp( value, 0.0f, 1.0f );
    return value * value * ( 3.0f - 2.0f * value );
}
}

void timeline::restart()
{
    reset();
    playing = true;
}

void timeline::reset()
{
    playing = false;
    elapsed = 0;
    last_frame.reset();
}

void timeline::update( std::uint32_t now, bool visible )
{
    if( !playing || !visible ) {
        last_frame.reset();
        return;
    }
    if( last_frame ) {
        elapsed += std::min( now - *last_frame, duration - elapsed );
    }
    last_frame = now;
    if( elapsed >= duration ) {
        playing = false;
    }
}

float timeline::opacity( int element ) const
{
    if( !playing ) {
        return 0.0f;
    }
    const float reveal = ( static_cast<float>( elapsed ) -
                           std::clamp( element, 0, 6 ) * row_delay ) / fade_in;
    const float fade = static_cast<float>( duration - elapsed ) / ( duration - hold_until );
    return smooth( reveal ) * smooth( fade );
}

bool timeline::active() const
{
    return playing;
}

summary capture( const avatar &you )
{
    summary result;
    result.day = "第 " + std::to_string( std::max( 0,
                                          to_days<int>( calendar::turn - calendar::start_of_game ) ) + 1 ) + " 天";
    result.date = display::date_string() + " " + display::time_string( you );
    const std::string profession = !you.custom_profession.empty() ? you.custom_profession :
                                   you.prof ? you.prof->gender_appropriate_name( you.male ) : "幸存者";
    auto &weather = get_weather();
    std::string location = remove_color_tags( overmap_buffer.get_description_at( you.pos_abs_sm() ) );
    location = location.substr( 0, location.find( '\n' ) );
    result.rows = {{
            { "姓名", you.name },
            { "职业", profession },
            { "位置", location },
            {
                "天气", weather.weather_id->name.translated() + "，" +
                                                  display::get_temp( you )
            },
            { "击杀数", std::to_string( g->get_kill_tracker().total_kill_count() ) }
        }
    };
    return result;
}

void begin( const avatar &you )
{
    reset();
    if( fp3d_compositor::requested() &&
        fp3d_compositor::controls() == fp3d_input::mode::first_person_3d ) {
        content = capture( you );
        animation.restart();
    }
}

void reset()
{
    animation.reset();
    content = {};
    backdrop.reset();
}

void draw( const SDL_Renderer_Ptr &renderer, const GeometryRenderer_Ptr &geometry,
           Font &font, const std::array<int, 4> &area, bool visible )
{
    visible = visible && area[2] > 0 && area[3] > 0;
    animation.update( SDL_GetTicks(), visible );
    if( !visible || !animation.active() ) {
        if( !animation.active() ) {
            backdrop.reset();
        }
        return;
    }
    if( !backdrop ) {
        // A feathered shade, with no scene readback or full-screen blur pass.
        SDL_Surface_Ptr shade( SDL_CreateRGBSurfaceWithFormat( 0, 256, 192, 32,
                               SDL_PIXELFORMAT_RGBA32 ) );
        if( !shade ) {
            return;
        }
        for( int y = 0; y < shade->h; ++y ) {
            auto *pixels = reinterpret_cast<Uint32 *>( static_cast<Uint8 *>( shade->pixels ) +
                           y * shade->pitch );
            for( int x = 0; x < shade->w; ++x ) {
                const float edge_x = std::min( x, shade->w - 1 - x ) / 48.0f;
                const float edge_y = std::min( y, shade->h - 1 - y ) / 38.0f;
                pixels[x] = SDL_MapRGBA( shade->format, 0, 0, 0,
                                         205 * smooth( edge_x ) * smooth( edge_y ) );
            }
        }
        backdrop.reset( SDL_CreateTextureFromSurface( renderer.get(), shade.get() ) );
        if( !backdrop ) {
            return;
        }
        SDL_SetTextureBlendMode( backdrop.get(), SDL_BLENDMODE_BLEND );
    }

    float old_scale_x, old_scale_y;
    SDL_RenderGetScale( renderer.get(), &old_scale_x, &old_scale_y );
    const SDL_bool was_clipped = SDL_RenderIsClipEnabled( renderer.get() );
    SDL_Rect old_clip;
    SDL_RenderGetClipRect( renderer.get(), &old_clip );
    SDL_BlendMode old_blend;
    SDL_GetRenderDrawBlendMode( renderer.get(), &old_blend );
    Uint8 r, green, b, a;
    SDL_GetRenderDrawColor( renderer.get(), &r, &green, &b, &a );

    const int width = 48 * font.width;
    const int height = 15 * font.height;
    const float scale = std::min( area[2] * 0.50f / width, area[3] * 0.50f / height );
    SDL_RenderSetScale( renderer.get(), scale, scale );
    const SDL_Rect clip{ static_cast<int>( area[0] / scale ), static_cast<int>( area[1] / scale ),
                         static_cast<int>( area[2] / scale ), static_cast<int>( area[3] / scale ) };
    SDL_RenderSetClipRect( renderer.get(), &clip );
    const int left = clip.x + ( clip.w - width ) / 2;
    const int top = clip.y + ( clip.h - height ) / 2;
    const SDL_Rect bounds{ left, top, width, height };
    SDL_SetTextureAlphaMod( backdrop.get(), animation.opacity() * 255 );
    SDL_RenderCopy( renderer.get(), backdrop.get(), nullptr, &bounds );

    const auto text = [&]( const std::string & line, int x, int y, unsigned char color,
    float opacity ) {
        utf8_wrapper chars( line );
        for( size_t i = 0; i < chars.size(); ++i ) {
            const std::string ch = chars.substr( i, 1 ).str();
            font.OutputChar( renderer, geometry, ch, point( x, y ), color, opacity );
            x += utf8_width( ch ) * font.width;
        }
    };
    const auto centered = [&]( const std::string & line, int y, int element ) {
        const std::string fitted = utf8_truncate( line, 38 );
        text( fitted, left + ( width - utf8_width( fitted ) * font.width ) / 2, y,
              15, animation.opacity( element ) );
    };
    centered( content.day, top + 2 * font.height, 0 );
    SDL_SetRenderDrawBlendMode( renderer.get(), SDL_BLENDMODE_BLEND );
    SDL_SetRenderDrawColor( renderer.get(), 240, 240, 240, animation.opacity() * 230 );
    SDL_RenderDrawLine( renderer.get(), left + 7 * font.width, top + 3 * font.height + 4,
                        left + width - 7 * font.width, top + 3 * font.height + 4 );
    centered( content.date, top + 4 * font.height, 1 );
    for( size_t i = 0; i < content.rows.size(); ++i ) {
        const auto &row = content.rows[i];
        const int y = top + 6 * font.height + i * font.height * 5 / 4;
        const float opacity = animation.opacity( i + 2 );
        text( row.first, left + 14 * font.width - utf8_width( row.first ) * font.width,
              y, 7, opacity );
        text( utf8_truncate( row.second, 27 ), left + 16 * font.width, y, 15, opacity );
    }
    SDL_SetRenderDrawColor( renderer.get(), r, green, b, a );
    SDL_SetRenderDrawBlendMode( renderer.get(), old_blend );
    SDL_RenderSetScale( renderer.get(), old_scale_x, old_scale_y );
    SDL_RenderSetClipRect( renderer.get(), was_clipped ? &old_clip : nullptr );
}
}
#endif
