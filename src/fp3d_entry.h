#pragma once
#ifndef CATA_SRC_FP3D_ENTRY_H
#define CATA_SRC_FP3D_ENTRY_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "sdl_geometry.h"
#include "sdl_wrappers.h"

class avatar;
class Font;
namespace fp3d_entry
{
struct summary {
    std::string day;
    std::string date;
    std::array<std::pair<std::string, std::string>, 5> rows;
};

// Wall-clock animation: loading and hidden/menu frames consume no display time.
class timeline
{
    public:
        void restart();
        void reset();
        void update( std::uint32_t now, bool visible );
        float opacity( int element = 0 ) const;
        bool active() const;
    private:
        bool playing = false;
        std::uint32_t elapsed = 0;
        std::optional<std::uint32_t> last_frame;
};

summary capture( const avatar &you );
void begin( const avatar &you );
void reset();
void draw( const SDL_Renderer_Ptr &renderer, const GeometryRenderer_Ptr &geometry,
           Font &font, const std::array<int, 4> &area, bool visible );
}
#endif
#endif
