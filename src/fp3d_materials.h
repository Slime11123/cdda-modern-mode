#pragma once
#ifndef CATA_SRC_FP3D_MATERIALS_H
#define CATA_SRC_FP3D_MATERIALS_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fp3d_material_aux.h"
#include "calendar.h"
#include "sdl_wrappers.h"
#include "sprite_alpha_mask.h"

class Character;
class tileset;
struct tile_type;
namespace fp3d_snapshot
{
struct cell;
struct ground_field;
struct scene_snapshot;
struct sprite;
}
namespace fp3d_vehicle
{
struct part_view;
}
namespace fp3d_materials
{
struct uv_rect {
    float left;
    float top;
    float right;
    float bottom;
};
struct field_frame {
    int ticks;
    std::optional<uv_rect> uv;
};
std::vector<field_frame> field_animation( const fp3d_snapshot::sprite &image );
std::vector<field_frame> field_animation_gpu( const fp3d_snapshot::sprite &image );
// Native idle animation of one visible creature/NPC/item sprite, taken from the
// current tileset's fg/bg frame lists exactly as the 2D renderer reads them.
// A static sprite yields a single frame, so callers never need a special case.
std::vector<field_frame> sprite_animation_gpu( const fp3d_snapshot::sprite &image );
// Atlas UV of the frame that native 2D tiles would show at this moment.
// now_ms follows cata_tiles: one animation tick per 17 ms of wall clock, so the
// 3D view never runs faster or slower with the frame rate. phase desynchronises
// equal creatures standing on different tiles. Null when nothing is resident.
const sprite_alpha_mask *sprite_alpha( const fp3d_snapshot::sprite &image, std::int64_t now_ms );
std::optional<uv_rect> sprite_frame_uv( const fp3d_snapshot::sprite &image,
                                       std::int64_t now_ms, unsigned phase );

// Resource lookups only; never consult the map or consume gameplay RNG.
const tile_type *resolve_terrain( const tileset &set, std::string id, season_type season );
const tile_type *resolve_vegetation( const tileset &set, std::string id, season_type season );
const tile_type *resolve_sprite( const tileset &set, const fp3d_snapshot::sprite &image,
                                 season_type season );
const tile_type *resolve_overlay( const tileset &set, bool male, const std::string &overlay,
                                  const std::string &variant );
std::string terrain_key( const fp3d_snapshot::cell &tile );
std::string sprite_key( const fp3d_snapshot::sprite &image );
// Native vpart_id plus cosmetic variant; independent of world orientation.
std::string vehicle_key( const fp3d_vehicle::part_view &part );
std::string ground_field_key( const fp3d_snapshot::ground_field &field );
const tile_type *resolve_ground_field( const tileset &set,
                                      const fp3d_snapshot::ground_field &field,
                                      season_type season, int &rotation );
// Dominant paint tint sampled from the currently selected tileset's
// real bodywork image, never a hard-coded UndeadPeople-only color.
std::optional<std::array<float, 3>> vehicle_body_color( const std::string &key );
struct sprite_choice {
    int background = -1;
    int foreground = -1;
};
sprite_choice choose_sprites( const tile_type &tile, const std::string &id );
// The same weighted-list selection driven by an explicit native animation
// tick instead of a stable per-ID seed: one real 2D frame per tick offset.
sprite_choice choose_sprites_at( const tile_type &tile, unsigned loc_rand );
void set_tileset( std::shared_ptr<const tileset> set, const SDL_Renderer_Ptr &renderer );
void prepare( const fp3d_snapshot::scene_snapshot &scene,
              const fp3d_snapshot::cell *preview = nullptr,
              const fp3d_snapshot::sprite *pickup_item = nullptr );
// An independently owned menu icon, not a UV borrowed from the world atlas.
SDL_Texture_Ptr character_icon( const Character &character );
SDL_Texture_Ptr item_icon( const fp3d_snapshot::sprite &image );
// Draw a small genuine tile from whichever tileset the native renderer loaded.
// Unlike item_icon this has no fake/magenta placeholder on absent art.
SDL_Texture_Ptr tileset_icon( const std::string &id );
unsigned revision();
std::size_t atlas_slot_count();
bool ready();
bool bind();
// Borrow the exact existing colour atlas from its SDL GPU renderer.
// The caller must wait for submission of atlas writes and preserve ownership
// until GPU fences retire; never use the handle after a revision/reset.
SDL_Texture *gpu_colour_atlas( SDL_Renderer *renderer );
std::optional<uv_rect> lookup_gpu( const std::string &id );
bool gpu_rect_is_opaque( const uv_rect &uv );
std::optional<uv_rect> picket_wood_gpu();
void unbind();
std::optional<uv_rect> lookup( const std::string &id );
// Same UVs as colour atlas, usable while the auxiliary shader owns GL textures.
// Read-only atlas coordinates for an already registered material.
std::optional<uv_rect> lookup_auxiliary_uv( const std::string &id );
// Lazily generate and cache normal/ORM from the actual current tileset slot.
// Does not alter the colour atlas or enable PBR lighting by itself.
// Call only with the SDL renderer active, before binding the atlas to GL.
const fp3d_material_aux::maps *auxiliary( const std::string &id );
// Pixel generation must happen on the SDL side, before SDL_FlushRenderer.
// Runs at most 'budget' 64x64 tile readbacks; result is CPU-only.
unsigned prefetch_auxiliary( unsigned budget );
// Requests use current atlas slots and prioritize nearby visible surfaces.
inline constexpr unsigned auxiliary_read_limit = 2;
std::vector<unsigned> visible_auxiliary_slots( const fp3d_snapshot::scene_snapshot &scene );
void cache_auxiliary_readback( unsigned source_revision, unsigned slot, const Uint8 *pixels );
struct auxiliary_tile {
    unsigned slot = 0;
    const fp3d_material_aux::maps *data = nullptr;
};
// Borrow the generated maps in colour-atlas slot order until the next mutation.
const std::vector<auxiliary_tile> &auxiliary_tiles();
struct furniture_uv {
    std::optional<uv_rect> top;
    std::optional<uv_rect> front;
    std::optional<uv_rect> side;
    std::optional<uv_rect> back = std::nullopt;
};
// Some packs combine a top and a front view in one pseudo-3D sprite.
furniture_uv lookup_furniture( const std::string &id, unsigned part_index = 0 );
furniture_uv lookup_furniture_gpu( const std::string &id, unsigned part_index );
void release();
} // namespace fp3d_materials
#endif
#endif // CATA_SRC_FP3D_MATERIALS_H
