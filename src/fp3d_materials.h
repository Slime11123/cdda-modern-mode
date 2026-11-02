#pragma once
#ifndef CATA_SRC_FP3D_MATERIALS_H
#define CATA_SRC_FP3D_MATERIALS_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <memory>
#include <optional>
#include <string>

#include "calendar.h"
#include "sdl_wrappers.h"

class tileset;
struct tile_type;
namespace fp3d_snapshot
{
struct scene_snapshot;
struct sprite;
}
namespace fp3d_materials
{
struct uv_rect {
    float left;
    float top;
    float right;
    float bottom;
};

// Resource lookups only; never consult the map or consume gameplay RNG.
const tile_type *resolve_terrain( const tileset &set, std::string id, season_type season );
const tile_type *resolve_vegetation( const tileset &set, std::string id, season_type season );
const tile_type *resolve_sprite( const tileset &set, const fp3d_snapshot::sprite &image,
                                 season_type season );
const tile_type *resolve_overlay( const tileset &set, bool male, const std::string &overlay,
                                  const std::string &variant );
std::string sprite_key( const fp3d_snapshot::sprite &image );
struct sprite_choice {
    int background = -1;
    int foreground = -1;
};
sprite_choice choose_sprites( const tile_type &tile, const std::string &id );
void set_tileset( std::shared_ptr<const tileset> set, const SDL_Renderer_Ptr &renderer );
void prepare( const fp3d_snapshot::scene_snapshot &scene );
// An independently owned menu icon, not a UV borrowed from the world atlas.
SDL_Texture_Ptr item_icon( const fp3d_snapshot::sprite &image );
unsigned revision();
bool ready();
bool bind();
void unbind();
std::optional<uv_rect> lookup( const std::string &id );
struct furniture_uv {
    std::optional<uv_rect> top;
    std::optional<uv_rect> front;
    std::optional<uv_rect> side;
};
// Some packs combine a top and a front view in one pseudo-3D sprite.
furniture_uv lookup_furniture( const std::string &id );
void release();
} // namespace fp3d_materials
#endif
#endif // CATA_SRC_FP3D_MATERIALS_H
