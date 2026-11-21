#include "fp3d_materials.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cata_tiles.h"
#include "cursesport.h"
#include "debug.h"
#include "field_type.h"
#include <SDL3/SDL_gpu.h>
#include "fp3d_snapshot.h"
#include "mapdata.h"
#include "item.h"
#include "itype.h"
#include "mtype.h"
#include "sdltiles.h"
#include "type_id.h"
#include "veh_type.h"

namespace fp3d_materials
{
namespace
{
// Terrain, sprites and all native-visible vehicle part variants share one atlas.
// 2048^2 supplies 1024 independent 64px slots (16 MiB RGBA8888), rather
// than silently dropping traffic and parked wreck vehicle art at 256 slots.
constexpr int atlas_size = 2048;
constexpr int slot_size = 64;
constexpr int slots_per_row = atlas_size / slot_size;
constexpr int max_slots = slots_per_row * slots_per_row;
std::shared_ptr<const tileset> selected;
const SDL_Renderer_Ptr *renderer_owner = nullptr;
SDL_Texture_Ptr atlas;
unsigned material_revision = 0;
std::unordered_map<std::string, unsigned> slots;
// Colour-atlas insertion order is the authoritative 64x64 slot order.
std::vector<std::string> slot_keys;
std::size_t auxiliary_cursor = 0;
std::unordered_map<std::string, fp3d_material_aux::maps> auxiliary_cache;
std::vector<auxiliary_tile> auxiliary_slots;
std::unordered_map<std::string, std::array<float, 3>> vehicle_colors;
season_type season = SPRING;
float texture_width = 1;
float texture_height = 1;
bool bound = false;
bool failed = false;

void clear_slots()
{
    slots.clear();
    slot_keys.clear();
    auxiliary_cursor = 0;
    auxiliary_slots.clear();
    auxiliary_cache.clear();
    vehicle_colors.clear();
}

unsigned stable_seed( const std::string &id )
{
    std::uint32_t hash = 2166136261u;
    for( unsigned char c : id ) {
        hash = ( hash ^ c ) * 16777619u;
    }
    return hash;
}

// SDL target switches alter viewport/scale. Restore both renderer and target
// state before returning to the game's display-buffer composition.
struct saved_renderer_state {
    SDL_Renderer *renderer;
    SDL_Texture *target;
    SDL_Rect viewport;
    SDL_Rect clip;
    bool clipping;
    bool integer_scale;
    int logical_width;
    int logical_height;
    float scale_x;
    float scale_y;
    SDL_BlendMode blend;
    Uint8 r, g, b, a;

    explicit saved_renderer_state( SDL_Renderer *value ) : renderer( value ),
        target( SDL_GetRenderTarget( value ) ), clipping( SDL_RenderClipEnabled( value ) ),
        integer_scale( GetIntegerRenderScale( value ) ) {
        SDL_GetRenderViewport( renderer, &viewport );
        SDL_GetRenderClipRect( renderer, &clip );
        GetLogicalRenderSize( renderer, &logical_width, &logical_height );
        SDL_GetRenderScale( renderer, &scale_x, &scale_y );
        SDL_GetRenderDrawBlendMode( renderer, &blend );
        SDL_GetRenderDrawColor( renderer, &r, &g, &b, &a );
    }
    ~saved_renderer_state() {
        SDL_SetRenderTarget( renderer, target );
        SetIntegerRenderScale( renderer, integer_scale );
        SetLogicalRenderSize( renderer, logical_width, logical_height );
        SDL_SetRenderViewport( renderer, &viewport );
        SDL_SetRenderScale( renderer, scale_x, scale_y );
        SDL_SetRenderClipRect( renderer, clipping ? &clip : nullptr );
        SDL_SetRenderDrawBlendMode( renderer, blend );
        SDL_SetRenderDrawColor( renderer, r, g, b, a );
    }
};

bool paint( int index, const SDL_Rect &slot )
{
    if( index < 0 ) {
        return false;
    }
    const texture *sprite = selected->get_tile( index );
    if( !sprite ) {
        return false;
    }
    const auto [width, height] = sprite->dimension();
    if( width <= 0 || height <= 0 ) {
        return false;
    }
    const float scale = std::min( ( slot_size - 4.0f ) / width, ( slot_size - 4.0f ) / height );
    const int w = std::max( 1, static_cast<int>( width * scale ) );
    const int h = std::max( 1, static_cast<int>( height * scale ) );
    const SDL_Rect dest{ slot.x + ( slot_size - w ) / 2, slot.y + ( slot_size - h ) / 2, w, h };
    return sprite->render_copy_ex( *renderer_owner, &dest, 0, nullptr, SDL_FLIP_NONE ) == 0;
}

bool paint_sprite( const fp3d_snapshot::sprite &image, const SDL_Rect &slot )
{
    struct layer {
        const texture *source;
        float x, y, width, height;
    };
    std::vector<layer> layers;
    float min_x = 0;
    float min_y = 0;
    float max_x = std::max( 1, selected->get_tile_width() );
    float max_y = std::max( 1, selected->get_tile_height() );
    const auto add = [&]( const tile_type & tile, const std::string & seed ) {
        const auto sprites = choose_sprites( tile, seed );
        for( const int index : {
                 image.kind == fp3d_snapshot::sprite_kind::held_item && sprites.foreground >= 0 ?
                 -1 : sprites.background, sprites.foreground
             } ) {
            const texture *source = index >= 0 ? selected->get_tile( index ) : nullptr;
            if( !source ) {
                continue;
            }
            const auto [width, height] = source->dimension();
            if( width <= 0 || height <= 0 ) {
                continue;
            }
            const float w = width * tile.pixelscale;
            const float h = height * tile.pixelscale;
            layers.push_back( { source, static_cast<float>( tile.offset.x ),
                                static_cast<float>( tile.offset.y ), w, h } );
            min_x = std::min( min_x, static_cast<float>( tile.offset.x ) );
            min_y = std::min( min_y, static_cast<float>( tile.offset.y ) );
            max_x = std::max( max_x, tile.offset.x + w );
            max_y = std::max( max_y, tile.offset.y + h );
        }
    };
    const tile_type *base = resolve_sprite( *selected, image, season );
    if( !base ) {
        return false;
    }
    add( *base, image.id );
    if( image.kind == fp3d_snapshot::sprite_kind::item && image.multiple_items ) {
        if( const tile_type *highlight = selected->find_tile_type( "highlight_item" ) ) {
            add( *highlight, "highlight_item" );
        }
    }
    for( const auto &overlay : image.overlays ) {
        if( const tile_type *tile = resolve_overlay( *selected, image.male, overlay.first,
                                    overlay.second ) ) {
            add( *tile, overlay.first + overlay.second );
        }
    }
    const float width = max_x - min_x;
    const float height = max_y - min_y;
    const float scale = std::min( ( slot_size - 4.0f ) / width, ( slot_size - 4.0f ) / height );
    const float origin_x = slot.x + ( slot_size - width * scale ) / 2 - min_x * scale;
    const float origin_y = slot.y + ( slot_size - height * scale ) / 2 - min_y * scale;
    bool painted = false;
    for( const layer &entry : layers ) {
        const SDL_Rect destination{ static_cast<int>( origin_x + entry.x * scale ),
                                    static_cast<int>( origin_y + entry.y * scale ),
                                    std::max( 1, static_cast<int>( entry.width * scale ) ),
                                    std::max( 1, static_cast<int>( entry.height * scale ) ) };
        painted = entry.source->render_copy_ex( *renderer_owner, &destination, 0, nullptr,
                                                SDL_FLIP_NONE ) == 0 || painted;
    }
    return painted;
}
} // namespace

std::string terrain_key( const fp3d_snapshot::cell &tile )
{
    if( tile.ground_connections >= 0 ) {
        return "$ground:" + std::to_string( tile.ground_connections ) + ":" + tile.terrain;
    }
    return tile.roof_connections < 0 ? tile.terrain :
           "$roof:" + std::to_string( tile.roof_connections ) + ":" + tile.terrain;
}

sprite_choice choose_sprites( const tile_type &tile, const std::string &id )
{
    const unsigned seed = stable_seed( id );
    const auto *bg = tile.bg.pick( seed );
    const auto *fg = tile.fg.pick( seed );
    return { bg && !bg->empty() ? bg->front() : -1,
             fg && !fg->empty() ? fg->front() : -1 };
}

const tile_type *resolve_terrain( const tileset &set, std::string id, season_type current_season )
{
    // Native transition tiles can be only a top-down up/down marker. The mesh
    // supplies the slope; texture it with the corresponding physical surface.
    if( id.compare( 0, 7, "t_ramp_" ) == 0 ) {
        id = "t_pavement";
    } else if( id.compare( 0, 13, "t_earth_ramp_" ) == 0 ) {
        id = "t_dirt";
    } else if( id.compare( 0, 16, "t_sidewalk_ramp_" ) == 0 ) {
        id = "t_sidewalk";
    } else if( id.find( "stairs" ) != std::string::npos && ter_str_id( id ).is_valid() ) {
        if( id.compare( 0, 7, "t_wood_" ) == 0 ) {
            id = "t_floor";
        } else if( id.compare( 0, 12, "t_vitrified_" ) == 0 ) {
            id = "t_vitrified_floor";
        } else {
            id = "t_concrete";
        }
    }
    for( int jumps = 0; jumps < 10 && !id.empty(); ++jumps ) {
        if( auto tile = set.find_tile_type_by_season( id, current_season ) ) {
            return &tile->tile();
        }
        const ter_str_id definition( id );
        if( !definition.is_valid() ) {
            return nullptr;
        }
        id = definition->looks_like;
    }
    return nullptr;
}

const tile_type *resolve_sprite( const tileset &set, const fp3d_snapshot::sprite &image,
                                 season_type current_season )
{
    std::string id = image.id;
    for( int jumps = 0; jumps < 10 && !id.empty(); ++jumps ) {
        if( !image.variant.empty() ) {
            if( auto tile = set.find_tile_type_by_season( id + "_var_" + image.variant, current_season ) ) {
                return &tile->tile();
            }
        }
        if( auto tile = set.find_tile_type_by_season( id, current_season ) ) {
            return &tile->tile();
        }
        if( image.kind == fp3d_snapshot::sprite_kind::monster ) {
            const mtype_id type( id );
            id = type.is_valid() ? type->looks_like : "";
        } else if( image.kind == fp3d_snapshot::sprite_kind::item ||
                   image.kind == fp3d_snapshot::sprite_kind::held_item ) {
            if( id.compare( 0, 7, "corpse_" ) == 0 ) {
                id = "corpse";
            } else {
                id = item::type_is_defined( itype_id( id ) ) ?
                     item::find_type( itype_id( id ) )->looks_like.str() : "";
            }
        } else {
            id.clear();
        }
    }
    return nullptr;
}

const tile_type *resolve_vegetation( const tileset &set, std::string id,
                                     season_type current_season )
{
    if( id.compare( 0, 2, "f_" ) != 0 ) {
        return resolve_terrain( set, std::move( id ), current_season );
    }
    for( int jumps = 0; jumps < 10 && !id.empty(); ++jumps ) {
        if( auto tile = set.find_tile_type_by_season( id, current_season ) ) {
            return &tile->tile();
        }
        const furn_str_id definition( id );
        if( !definition.is_valid() ) {
            break;
        }
        id = definition->looks_like;
    }
    return nullptr;
}

const tile_type *resolve_overlay( const tileset &set, bool male, const std::string &overlay,
                                  const std::string &variant )
{
    std::string id = overlay;
    std::string type;
    if( id.compare( 0, 5, "worn_" ) == 0 ) {
        type = "worn_";
        id.erase( 0, 5 );
    } else if( id.compare( 0, 8, "wielded_" ) == 0 ) {
        type = "wielded_";
        id.erase( 0, 8 );
    }
    const std::string gender = male ? "overlay_male_" : "overlay_female_";
    if( !variant.empty() ) {
        std::string variant_id = id;
        for( int attempt = 0; attempt < 2; ++attempt ) {
            for( const std::string &prefix : {
                     gender, std::string( "overlay_" )
                 } ) {
                if( const tile_type *tile = set.find_tile_type( prefix + type + variant_id + "_var_" + variant ) ) {
                    return tile;
                }
            }
            if( variant_id.compare( 0, 16, "mutation_active_" ) != 0 ) {
                break;
            }
            variant_id = "mutation_" + variant_id.substr( 16 );
        }
    }
    for( int jumps = 0; jumps < 10 && !id.empty(); ++jumps ) {
        for( const std::string &prefix : {
                 gender, std::string( "overlay_" )
             } ) {
            if( const tile_type *tile = set.find_tile_type( prefix + type + id ) ) {
                return tile;
            }
        }
        if( id.compare( 0, 16, "mutation_active_" ) == 0 ) {
            id = "mutation_" + id.substr( 16 );
        } else {
            id = item::type_is_defined( itype_id( id ) ) ?
                 item::find_type( itype_id( id ) )->looks_like.str() : "";
        }
    }
    return nullptr;
}

std::string sprite_key( const fp3d_snapshot::sprite &image )
{
    std::string key = "$sprite:" + std::to_string( static_cast<int>( image.kind ) ) +
                      ( image.male ? "m" : "f" );
    const auto append = [&]( const std::string & part ) {
        key += std::to_string( part.size() ) + ":" + part;
    };
    append( image.id );
    append( image.variant );
    if( image.kind == fp3d_snapshot::sprite_kind::field ) {
        append( std::to_string( image.field_intensity ) );
    }
    if( image.kind == fp3d_snapshot::sprite_kind::item && image.multiple_items ) {
        append( "highlight_item" );
    }
    for( const auto &overlay : image.overlays ) {
        append( overlay.first );
        append( overlay.second );
    }
    return key;
}

void set_tileset( std::shared_ptr<const tileset> set, const SDL_Renderer_Ptr &renderer )
{
    ++material_revision;
    // Called only after the native loader finishes, including forced reloads.
    atlas.reset();
    clear_slots();
    selected = std::move( set );
    renderer_owner = &renderer;
    failed = false;
    DebugLog( D_INFO, D_MAIN ) << "fp3d: material source=" << selected->get_tileset_id()
                               << "; invalidated atlas";
}

SDL_Texture_Ptr item_icon( const fp3d_snapshot::sprite &image )
{
    if( !selected || !renderer_owner || !*renderer_owner ) {
        return {};
    }
    SDL_Renderer *renderer = renderer_owner->get();
    saved_renderer_state saved( renderer );
    SDL_Texture_Ptr icon( SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA32,
                          SDL_TEXTUREACCESS_TARGET, slot_size, slot_size ) );
    if( !icon || !SDL_SetRenderTarget( renderer, icon.get() ) ) {
        return {};
    }
    SetLogicalRenderSize( renderer, 0, 0 );
    SDL_SetRenderScale( renderer, 1, 1 );
    SDL_SetRenderViewport( renderer, nullptr );
    SDL_SetRenderClipRect( renderer, nullptr );
    SDL_SetRenderDrawColor( renderer, 0, 0, 0, 0 );
    SDL_RenderClear( renderer );
    if( !paint_sprite( image, SDL_Rect{ 0, 0, slot_size, slot_size } ) ) {
        SDL_SetRenderDrawColor( renderer, 150, 155, 165, 255 );
        const SDL_Rect placeholder{ 16, 16, 32, 32 };
        RenderFillRectPixels( renderer, &placeholder );
    }
    SDL_SetTextureBlendMode( icon.get(), SDL_BLENDMODE_BLEND );
    SDL_FlushRenderer( renderer );
    return icon;
}

SDL_Texture_Ptr tileset_icon( const std::string &id )
{
    if( !selected || !renderer_owner || !*renderer_owner || id.empty() ) {
        return {};
    }
    // Native overmap IDs can carry the om# namespace prefix. Preserve the
    // actual ID from the map loader, rather than inventing terrain artwork.
    const std::string name = id.rfind( "om#", 0 ) == 0 ? id.substr( 3 ) : id;
    const tile_type *tile = nullptr;
    if( auto found = selected->find_tile_type_by_season( name, season ) ) {
        tile = &found->tile();
    } else if( auto found = selected->find_tile_type_by_season( "overmap_terrain_" + name,
                          season ) ) {
        tile = &found->tile();
    }
    if( !tile ) {
        return {};
    }
    SDL_Renderer *renderer = renderer_owner->get();
    saved_renderer_state saved( renderer );
    SDL_Texture_Ptr icon( SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA32,
                          SDL_TEXTUREACCESS_TARGET, slot_size, slot_size ) );
    if( !icon || !SDL_SetRenderTarget( renderer, icon.get() ) ) {
        return {};
    }
    SetLogicalRenderSize( renderer, 0, 0 );
    SDL_SetRenderScale( renderer, 1, 1 );
    SDL_SetRenderViewport( renderer, nullptr );
    SDL_SetRenderClipRect( renderer, nullptr );
    SDL_SetRenderDrawColor( renderer, 0, 0, 0, 0 );
    SDL_RenderClear( renderer );
    const auto sprites = choose_sprites( *tile, name );
    const SDL_Rect dest{ 0, 0, slot_size, slot_size };
    const bool painted_bg = paint( sprites.background, dest );
    const bool painted_fg = paint( sprites.foreground, dest );
    if( !painted_bg && !painted_fg ) {
        return {};
    }
    SDL_SetTextureBlendMode( icon.get(), SDL_BLENDMODE_BLEND );
    SDL_FlushRenderer( renderer );
    return icon;
}

std::string vehicle_key( const fp3d_vehicle::part_view &part )
{
    return "$vehicle:" + part.id +
           ( part.variant.empty() ? "" : "#" + part.variant );
}

std::string ground_field_key( const fp3d_snapshot::ground_field &field )
{
    return "$field:" + field.id + "#" + std::to_string( field.intensity ) +
           ":" + std::to_string( field.connections );
}

const tile_type *resolve_ground_field( const tileset &set,
                                      const fp3d_snapshot::ground_field &field,
                                      season_type season, int &rotation )
{
    static const std::array<const char *, 16> subtiles{
        "unconnected", "end_piece", "end_piece", "corner",
        "end_piece", "corner", "edge", "t_connection",
        "end_piece", "edge", "corner", "t_connection",
        "corner", "t_connection", "t_connection", "center" };
    static const std::array<int, 16> rotations{ 0, 0, 1, 0, 3, 3, 1, 0,
                                               2, 0, 1, 1, 2, 3, 2, 0 };
    rotation = 0;
    std::string name = field.id;
    std::unordered_set<std::string> visited;
    for( int tries = 0; tries < 10 && !name.empty() && visited.insert( name ).second;
         ++tries ) {
        for( const auto &candidate : { name + "_int" + std::to_string( field.intensity ), name } ) {
            if( auto found = set.find_tile_type_by_season( candidate, season ) ) {
                const tile_type &base = found->tile();
                const int mask = field.connections & 15;
                const std::string subtile = subtiles[mask];
                if( base.multitile && std::find( base.available_subtiles.begin(),
                                               base.available_subtiles.end(), subtile ) != base.available_subtiles.end() ) {
                    if( auto connected = set.find_tile_type_by_season( candidate + "_" + subtile, season ) ) {
                        rotation = connected->tile().rotates ? rotations[mask] : 0;
                        return &connected->tile();
                    }
                }
                return &base;
            }
        }
        const field_type_str_id definition( name );
        name = definition.is_valid() ? definition->looks_like : "";
    }
    return nullptr;
}

namespace
{
struct field_frame_source {
    std::string key;
    int sprite;
    int ticks;
};

std::vector<field_frame_source> field_frame_sources( const fp3d_snapshot::sprite &image )
{
    int rotation = 0;
    const tile_type *tile = selected ? resolve_ground_field( *selected,
                            { image.id, image.field_intensity }, season, rotation ) : nullptr;
    if( !tile ) {
        return {};
    }
    const std::string key = sprite_key( image );
    const auto &layer = tile->fg.get_weight() > 0 ? tile->fg : tile->bg;
    std::vector<field_frame_source> frames;
    if( tile->animated ) {
        for( const auto &frame : layer ) {
            if( !frame.obj.empty() && frame.obj.front() >= 0 ) {
                frames.push_back( { key + ":" + std::to_string( frames.size() ),
                                    frame.obj.front(), frame.weight } );
            }
        }
    } else if( const auto *frame = layer.pick( stable_seed( image.id ) );
               frame && !frame->empty() && frame->front() >= 0 ) {
        frames.push_back( { key, frame->front(), 1 } );
    }
    return frames;
}
} // namespace

static std::vector<field_frame> resolve_field_animation( const fp3d_snapshot::sprite &image,
        std::optional<uv_rect> ( *resolve )( const std::string & ) )
{
    std::vector<field_frame> result;
    for( const auto &frame : field_frame_sources( image ) ) {
        result.push_back( { frame.ticks, resolve( frame.key ) } );
    }
    return result;
}


std::vector<field_frame> field_animation_gpu( const fp3d_snapshot::sprite &image )
{
    return resolve_field_animation( image, lookup_gpu );
}

void prepare( const fp3d_snapshot::scene_snapshot &scene, const fp3d_snapshot::cell *preview )
{
    if( !selected || !renderer_owner || !*renderer_owner || failed ) {
        return;
    }
    if( season != scene.season ) {
        ++material_revision;
        season = scene.season;
        atlas.reset();
        clear_slots();
    }
    // Material requests depend on types, not on the number of equal wall
    // tiles. Retained geometry also retains these representative pointers.
    static unsigned request_geometry = 0;
    static const fp3d_snapshot::cell *request_allocation = nullptr;
    static std::vector<const fp3d_snapshot::cell *> material_cells;
    if( !scene.geometry_generation || request_geometry != scene.geometry_generation ||
        request_allocation != scene.cells.data() ) {
        material_cells.clear();
        std::unordered_set<std::string> unique;
        // Reuse the scratch capacity for equal materials; only new keys
        // need an owned string in the set.
        std::string key;
        for( const auto &tile : scene.cells ) {
            key = tile.terrain;
            const auto append = [&]( const std::string &value ) { key.push_back( 0 ); key += value; };
            append( std::to_string( tile.roof_connections ) );
            append( std::to_string( tile.ground_connections ) );
            append( std::to_string( static_cast<unsigned>( tile.kind ) ) );
            append( tile.vegetation );
            append( tile.ceiling_surface );
            append( tile.ceiling_present ? "1" : "0" );
            append( tile.furniture ? tile.furniture->id : "" );
            if( tile.furniture ) { append( std::to_string( static_cast<unsigned>( tile.furniture->shape ) ) ); }
            for( const auto &field : tile.ground_fields ) { append( ground_field_key( field ) ); }
            if( unique.insert( key ).second ) { material_cells.push_back( &tile ); }
        }
        request_geometry = scene.geometry_generation;
        request_allocation = scene.cells.data();
    }
    struct request {
        std::string key;
        const fp3d_snapshot::sprite *image = nullptr;
        bool plant = false;
        bool window = false;
        bool furniture = false;
        bool field = false;
        bool fence = false;
        bool vehicle = false;
        std::string vehicle_looks_like = {};
        bool status = false; // Camera-facing badge using a native tileset icon.
        std::string field_id = {};
        int field_intensity = 1;
        int field_connections = 0;
        int field_sprite = -1;
    };
    std::vector<request> pending;
    std::unordered_set<std::string> model_keys;
    // Reserve the tiny shared status icons before terrain fills the atlas.
    // Missing icons in another tileset stay absent rather than purple squares.
    std::unordered_set<std::string> unique_status_icons;
    const auto queue_status = [&]( const std::string &icon ) {
        const std::string key = "$status:" + icon;
        if( !unique_status_icons.insert( icon ).second || slots.count( key ) ||
            pending.size() >= max_slots || !selected->find_tile_type( icon ) ) {
            return;
        }
        request entry;
        entry.key = key;
        entry.status = true;
        pending.push_back( std::move( entry ) );
    };
    // A badge occupies one shared atlas slot regardless of how many actors
    // carry that effect. All icons come from the currently selected tileset.
    for( const auto &image : scene.sprites ) {
        if( image.revivable ) {
            queue_status( "zombie_revival_indicator" );
        }
        if( image.alerted ) {
            queue_status( "overlay_hostile_sees_player" );
        }
        if( image.bleeding ) {
            queue_status( "overlay_effect_bleed" );
        }
        for( const std::string &icon : image.status_icons ) {
            queue_status( icon );
        }
    }
    std::unordered_set<std::string> prepared_fields;
    for( const auto &image : scene.sprites ) {
        if( image.kind == fp3d_snapshot::sprite_kind::field &&
            prepared_fields.insert( sprite_key( image ) ).second ) {
            for( const auto &frame : field_frame_sources( image ) ) {
                if( pending.size() == max_slots ) {
                    break;
                }
                request entry;
                entry.key = frame.key;
                entry.field_sprite = frame.sprite;
                pending.push_back( std::move( entry ) );
            }
        }
    }
    const auto prioritize_cell = [&]( const fp3d_snapshot::cell &tile ) {
        if( ( tile.kind == fp3d_snapshot::terrain_kind::ladder ||
              tile.kind == fp3d_snapshot::terrain_kind::downspout ) && pending.size() < max_slots ) {
            std::string key = tile.terrain == "t_vitrified_downspout" ? "t_vitrified_floor" : "t_metal_floor";
            if( model_keys.insert( key ).second ) {
                pending.push_back( { std::move( key ), nullptr } );
            }
        }
        if( tile.kind == fp3d_snapshot::terrain_kind::fence && pending.size() < max_slots ) {
            std::string key = "$fence:" + tile.terrain;
            if( model_keys.insert( key ).second ) {
                pending.push_back( { std::move( key ), nullptr, false, false, false, false, true } );
            }
        }
        if( tile.furniture && pending.size() < max_slots ) {
            // The UndeadPeople bed picture is cloth only. Fetch a second
            // opaque, real wood tile for frame rails instead of stretching
            // beige upholstery on all upright surfaces.
            if( ( tile.furniture->shape == fp3d_furniture::kind::bed ||
                  tile.furniture->shape == fp3d_furniture::kind::bed_frame ) &&
                model_keys.insert( "$furniture:f_table" ).second ) {
                pending.push_back( { "$furniture:f_table", nullptr, false, false, true } );
            }
            if( ( tile.furniture->shape == fp3d_furniture::kind::street_light ||
                  tile.furniture->shape == fp3d_furniture::kind::traffic_light ||
                  tile.furniture->shape == fp3d_furniture::kind::floor_lamp ) &&
                model_keys.insert( "t_metal_floor" ).second ) {
                pending.push_back( { "t_metal_floor", nullptr } );
            }
            std::string key = "$furniture:" + tile.furniture->id;
            if( model_keys.insert( key ).second ) {
                pending.push_back( { std::move( key ), nullptr, false, false, true } );
            }
        }
        for( const auto &field : tile.ground_fields ) {
            std::string key = ground_field_key( field );
            if( pending.size() >= max_slots ) {
                break;
            }
            if( model_keys.insert( key ).second ) {
                request entry;
                entry.key = std::move( key );
                entry.field = true;
                entry.field_id = field.id;
                entry.field_intensity = field.intensity;
                entry.field_connections = field.connections;
                pending.push_back( std::move( entry ) );
            }
        }
    };
    if( scene.held_item ) {
        pending.push_back( { sprite_key( *scene.held_item ), &*scene.held_item } );
    }
    if( preview ) {
        prioritize_cell( *preview );
    }
    for( const auto *tile : material_cells ) {
        prioritize_cell( *tile );
    }
    // Every native-visible vehicle shares this atlas with the terrain and
    // its selected tileset. One slot per vpart_id, never one per vehicle.
    // If the atlas is full, the vehicle mesh still renders neutral geometry.
    unsigned excluded_vehicle_art = 0;
    for( const fp3d_vehicle::vehicle_view &vehicle : scene.vehicles ) {
        for( const fp3d_vehicle::part_view &part : vehicle.parts ) {
            const std::string key = vehicle_key( part );
            if( model_keys.insert( key ).second ) {
                if( pending.size() < max_slots ) {
                    pending.push_back( { key, nullptr, false, false, false, false, false,
                                         true, part.looks_like } );
                } else {
                    ++excluded_vehicle_art;
                }
            }
        }
    }
    if( excluded_vehicle_art ) {
        DebugLog( D_WARNING, D_MAIN ) << "fp3d: " << excluded_vehicle_art
                                      << " visible vehicle material keys exceed atlas "
                                      << max_slots << " slots; neutral 3D geometry remains visible";
    }
    const auto resident = [&]( const request &entry ) {
        return slots.count( entry.key ) != 0;
    };
    const auto missing_priority = std::count_if( pending.begin(), pending.end(),
                                  [&]( const request &entry ) { return !resident( entry ); } );
    // Reclaim old entries when the current models cannot fit. Geometry using
    // the old UVs is invalidated before the reused slots are drawn again.
    if( slots.size() + missing_priority > max_slots ) {
        ++material_revision;
        clear_slots();
    }
    pending.erase( std::remove_if( pending.begin(), pending.end(), resident ), pending.end() );
    const auto add = [&]( const std::string & key, const fp3d_snapshot::sprite * image ) {
        if( !slots.count( key ) && slots.size() + pending.size() < max_slots &&
        std::none_of( pending.begin(), pending.end(), [&]( const request & entry ) {
        return entry.key == key;
    } ) ) {
            pending.push_back( { key, image } );
        }
    };
    const auto add_cell = [&]( const fp3d_snapshot::cell &tile ) {
        if( tile.terrain == "t_column" || tile.terrain == "t_pillar" ||
            tile.terrain == "t_little_column" || tile.terrain == "t_column_halfway" ||
            tile.terrain == "t_concrete_railing" ||
            tile.terrain.compare( 0, 11, "t_guardrail" ) == 0 ) {
            add( "t_concrete", nullptr );
        }
        if( tile.kind == fp3d_snapshot::terrain_kind::ladder ) {
            add( "t_concrete", nullptr );
        } else if( tile.kind == fp3d_snapshot::terrain_kind::downspout || tile.kind == fp3d_snapshot::terrain_kind::fence ) {
            add( "t_dirt", nullptr );
            if( tile.terrain == "t_railing" ) {
                // When a railing borders a metal tower deck, its lower
                // 8cm foundation uses the same metal atlas material.
                add( "t_metal_floor_no_roof", nullptr );
            }
        } else if( tile.kind == fp3d_snapshot::terrain_kind::window ) {
            add( "t_floor", nullptr );
            add( "t_wall", nullptr );
            const auto before = pending.size();
            add( "$window:" + tile.terrain, nullptr );
            if( pending.size() > before ) {
                pending.back().window = true;
            }
        } else if( tile.terrain.compare( 0, 9, "t_treetop" ) == 0 ) {
            // The native treetop belongs to a higher z-level, but its art
            // must come from the corresponding tree, not a missing ground
            // tile. This is an alpha-cutout atlas slot for crossed cards.
            const size_t before = pending.size();
            add( treetop_plant_key( tile.terrain ), nullptr );
            if( pending.size() > before ) {
                pending.back().plant = true;
            }
        } else if( tile.kind != fp3d_snapshot::terrain_kind::glass ) {
            add( terrain_key( tile ), nullptr );
        }
        if( !tile.ceiling_surface.empty() ) {
            // An exposed metal walkway can be visible from underneath even
            // when its upper floor is outside the native endpoint cache.
            add( tile.ceiling_surface, nullptr );
        }
        if( tile.ceiling_present || tile.kind == fp3d_snapshot::terrain_kind::glass ||
            tile.kind == fp3d_snapshot::terrain_kind::closed_door ||
            tile.kind == fp3d_snapshot::terrain_kind::open_door ||
            tile.kind == fp3d_snapshot::terrain_kind::damaged_door ) {
            add( "t_floor", nullptr );
        }
        if( !tile.vegetation.empty() ) {
            if( tile.vegetation == tile.terrain && tile.vegetation.compare( 0, 7, "t_grass" ) != 0 ) {
                add( "t_dirt", nullptr );
            }
            const size_t before = pending.size();
            add( "$plant:" + tile.vegetation, nullptr );
            if( pending.size() > before ) {
                pending.back().plant = true;
            }
        }
    };
    if( preview ) {
        add_cell( *preview );
    }
    for( const auto *tile : material_cells ) {
        add_cell( *tile );
    }
    for( const auto &patch : scene.landscape ) {
        if( patch.ground_present ) {
            add( patch.terrain, nullptr );
        }
        if( patch.building_height > 0 ) {
            add( patch.wall_material, nullptr );
            add( "t_flat_roof", nullptr );
        }
        if( patch.wooded ) {
            const auto before = pending.size();
            add( "$plant:t_tree", nullptr );
            if( pending.size() > before ) {
                pending.back().plant = true;
            }
        }
    }
    for( const auto &image : scene.sprites ) {
        if( image.kind != fp3d_snapshot::sprite_kind::field ) {
            add( sprite_key( image ), &image );
        }
    }
    if( pending.empty() ) {
        return;
    }
    SDL_Renderer *renderer = renderer_owner->get();
    saved_renderer_state saved( renderer );
    if( !atlas ) {
        atlas.reset( SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_TARGET, atlas_size, atlas_size ) );
        if( !atlas ) {
            failed = true;
            DebugLog( D_WARNING, D_MAIN ) << "fp3d: material atlas unavailable: " << SDL_GetError();
            return;
        }
        SDL_SetTextureBlendMode( atlas.get(), SDL_BLENDMODE_NONE );
        SDL_SetTextureScaleMode( atlas.get(), SDL_SCALEMODE_NEAREST );
    }
    if( !SDL_SetRenderTarget( renderer, atlas.get() ) ) {
        failed = true;
        return;
    }
    SetLogicalRenderSize( renderer, 0, 0 );
    SetIntegerRenderScale( renderer, false );
    SDL_SetRenderScale( renderer, 1, 1 );
    SDL_SetRenderViewport( renderer, nullptr );
    SDL_SetRenderClipRect( renderer, nullptr );
    SDL_SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_NONE );
    unsigned missing = 0;
    for( const request &entry : pending ) {
        const std::string &id = entry.key;
        const unsigned index = slots.size();
        const SDL_Rect slot{ static_cast<int>( index % slots_per_row ) * slot_size,
                             static_cast<int>( index / slots_per_row ) * slot_size, slot_size, slot_size };
        const bool transparent = entry.image || entry.plant || entry.window || entry.field ||
                                 entry.field_sprite >= 0 ||
                                 entry.fence || entry.vehicle || entry.status;
        SDL_SetRenderDrawColor( renderer, transparent ? 0 : 180, transparent ? 0 : 180,
                                transparent ? 0 : 180, transparent ? 0 : 255 );
        RenderFillRectPixels( renderer, &slot );
        const tile_type *tile = nullptr;
        int field_rotation = 0;
        if( entry.vehicle ) {
            // Match CDDA's own vehicle sprite convention: vp_<vpart_id>.
            // Follow only the CURRENT selected tileset and native vpart
            // looks_like links. Never read fixed UndeadPeople disk files.
            std::string name = id.substr( 9 );
            std::string variant;
            const size_t separator = name.find( '#' );
            if( separator != std::string::npos ) {
                variant = name.substr( separator + 1 );
                name.resize( separator );
            }
            std::unordered_set<std::string> visited;
            for( int jumps = 0; jumps < 10 && !name.empty() &&
                 visited.insert( name ).second; ++jumps ) {
                if( name.compare( 0, 3, "vp_" ) == 0 ) {
                    name.erase( 0, 3 );
                }
                // Mirror native cata_tiles.cpp VEHICLE_PART variant fallback:
                // vp_<id>_<full_variant>, then progressively shorter
                // underscore-delimited variants, then vp_<id>.
                std::string variant_chunk = variant;
                while( !variant_chunk.empty() && !tile ) {
                    if( auto found = selected->find_tile_type_by_season(
                                       "vp_" + name + "_" + variant_chunk, season ) ) {
                        tile = &found->tile();
                        break;
                    }
                    const size_t trim = variant_chunk.rfind( '_' );
                    if( trim == std::string::npos ) {
                        break;
                    }
                    variant_chunk.resize( trim );
                }
                if( !tile ) {
                    if( auto found = selected->find_tile_type_by_season( "vp_" + name, season ) ) {
                        tile = &found->tile();
                    } else if( jumps > 0 ) {
                        // Native vehicle looks_like also accepts an unprefixed
                        // same-pack tile, e.g. a furniture-style fallback.
                        if( auto plain = selected->find_tile_type_by_season( name, season ) ) {
                            tile = &plain->tile();
                        }
                    }
                }
                if( tile ) {
                    break;
                }
                const vpart_id definition( name );
                const std::string next = definition.is_valid() ? definition->looks_like :
                                         jumps == 0 ? entry.vehicle_looks_like : "";
                if( next == name ) {
                    break;
                }
                name = next;
            }
        } else if( entry.status ) {
            if( auto found = selected->find_tile_type_by_season( id.substr( 8 ), season ) ) {
                tile = &found->tile();
            }
        } else if( entry.field ) {
            tile = resolve_ground_field( *selected,
            { entry.field_id, entry.field_intensity, entry.field_connections }, season, field_rotation );
        } else {
            tile = entry.fence ? resolve_terrain( *selected, id.substr( 7 ), season ) :
                   entry.image ? nullptr : entry.furniture ?
                                resolve_vegetation( *selected, id.substr( 11 ), season ) : entry.window ?
                                resolve_terrain( *selected, id.substr( 8 ), season ) : entry.plant ?
                                resolve_vegetation( *selected, id.substr( 7 ), season ) :
                                resolve_terrain( *selected, id, season );
        }
        if( id == "$furniture:f_bed_frame" && selected->get_tileset_id() == "UNDEAD_PEOPLE_BASE" ) {
            // The top-down spring grid is unsuitable for the wooden rails and slats.
            if( const auto *wood = resolve_vegetation( *selected, "f_table", season ) ) {
                tile = wood;
            }
        }
        int roof_rotation = 0;
        const bool roof_art = id.compare( 0, 6, "$roof:" ) == 0;
        const bool ground_art = id.compare( 0, 8, "$ground:" ) == 0;
        if( roof_art || ground_art ) {
            const std::size_t prefix = ground_art ? 8 : 6;
            const auto separator = id.find( ':', prefix );
            const int mask = std::stoi( id.substr( prefix, separator - prefix ) ) & 15;
            std::string name = id.substr( separator + 1 );
            if( ground_art ) {
                // Additional tiles belong to the resolved native looks_like ID.
                for( int jumps = 0; jumps < 10 && !name.empty(); ++jumps ) {
                    if( selected->find_tile_type_by_season( name, season ) ) { break; }
                    const ter_str_id definition( name );
                    if( !definition.is_valid() ) { break; }
                    name = definition->looks_like;
                }
            }
            static const std::array<const char *, 16> subtiles{
                "unconnected", "end_piece", "end_piece", "corner",
                "end_piece", "corner", "edge", "t_connection",
                "end_piece", "edge", "corner", "t_connection",
                "corner", "t_connection", "t_connection", "center" };
            static const std::array<int, 16> rotations{ 0, 0, 1, 0, 3, 3, 1, 0,
                                                       2, 0, 1, 1, 2, 3, 2, 0 };
            tile = resolve_terrain( *selected, name, season );
            if( auto connected = selected->find_tile_type_by_season(
                    name + "_" + subtiles[mask], season ); connected &&
                    ( roof_art || ( tile && tile->multitile &&
                      std::find( tile->available_subtiles.begin(), tile->available_subtiles.end(),
                                 subtiles[mask] ) != tile->available_subtiles.end() ) ) ) {
                tile = &connected->tile();
                roof_rotation = rotations[mask];
            }
        }
        bool painted = false;
        if( tile ) {
            const auto sprites = choose_sprites( *tile, id );
            if( ( !roof_art && !ground_art && !entry.plant && !entry.window && !entry.field && !entry.fence ) ||
                ( entry.window && sprites.foreground < 0 ) ) {
                painted = paint( sprites.background, slot );
            }
            if( roof_art || ground_art || entry.field ) {
                // Connected ground, coverings and roof edges fill the tile without padding.
                const int rotation = entry.field ? field_rotation : roof_rotation;
                const unsigned seed = stable_seed( entry.field ? entry.field_id + ":" +
                                                  std::to_string( entry.field_connections ) : id );
                for( bool foreground : { false, true } ) {
                    const auto *choices = ( foreground ? tile->fg : tile->bg ).pick( seed );
                    if( !choices || choices->empty() ) { continue; }
                    const int index = ( *choices )[choices->size() > 1 ?
                                      rotation % choices->size() : 0];
                    const texture *source = selected->get_tile( index );
                    if( source ) {
                        const double angle = foreground && choices->size() == 1 ? -90.0 * rotation : 0;
                        painted = source->render_copy_ex( *renderer_owner, &slot,
                                  angle, nullptr, SDL_FLIP_NONE ) == 0 || painted;
                    }
                }
            } else {
                painted = paint( sprites.foreground, slot ) || painted;
            }
        }
        if( entry.image ) {
            painted = paint_sprite( *entry.image, slot );
        }
        if( entry.field_sprite >= 0 ) {
            painted = paint( entry.field_sprite, slot );
        }
        if( painted && entry.status ) {
            // Native badges are positioned within a full creature-sized canvas.
            // Crop transparent padding before placing them on the small 3D quad.
            std::array<Uint8, slot_size * slot_size * 4> pixels{};
            if( ReadRenderPixels( renderer, &slot, SDL_PIXELFORMAT_RGBA32,
                                      pixels.data(), slot_size * 4 ) == 0 ) {
                int left = slot_size;
                int top = slot_size;
                int right = -1;
                int bottom = -1;
                for( int y = 0; y < slot_size; ++y ) {
                    for( int x = 0; x < slot_size; ++x ) {
                        if( pixels[( y * slot_size + x ) * 4 + 3] > 0 ) {
                            left = std::min( left, x );
                            top = std::min( top, y );
                            right = std::max( right, x );
                            bottom = std::max( bottom, y );
                        }
                    }
                }
                if( right >= left && bottom >= top ) {
                    SDL_Surface_Ptr surface( CreateSurfaceFromPixels( pixels.data(),
                                             slot_size, slot_size, 32, slot_size * 4, SDL_PIXELFORMAT_RGBA32 ) );
                    SDL_Texture_Ptr cropped( surface ? SDL_CreateTextureFromSurface( renderer, surface.get() ) :
                                             nullptr );
                    if( cropped ) {
                        SDL_SetRenderDrawColor( renderer, 0, 0, 0, 0 );
                        RenderFillRectPixels( renderer, &slot );
                        const SDL_Rect source{ left, top, right - left + 1, bottom - top + 1 };
                        const float scale = ( slot_size - 4.f ) / std::max( source.w, source.h );
                        const int w = std::max( 1, static_cast<int>( source.w * scale ) );
                        const int h = std::max( 1, static_cast<int>( source.h * scale ) );
                        const SDL_Rect dest{ slot.x + ( slot_size - w ) / 2,
                                             slot.y + ( slot_size - h ) / 2, w, h };
                        RenderTexturePixels( renderer, cropped.get(), &source, &dest );
                    }
                }
            }
        }
        if( !painted && entry.field ) {
            // Sparse packs may omit both the field and its looks_like chain.
            // Retain a readable transparent covering in the native field colour.
            const field_type_str_id definition( entry.field_id );
            if( definition.is_valid() ) {
                const nc_color native = definition->get_intensity_level( entry.field_intensity - 1 ).color;
                const auto &pair = cata_cursesport::colorpairs[native.to_color_pair_index()];
                const SDL_Color shade = windowsPalette[( pair.FG != 0 ? pair.FG : pair.BG ) +
                                                       ( native.is_bold() ? 8 : 0 )];
                SDL_SetRenderDrawColor( renderer, shade.r, shade.g, shade.b, 210 );
                const bool web = entry.field_id.find( "web" ) != std::string::npos;
                const bool debris = entry.field_id.find( "gibs" ) != std::string::npos ||
                                    entry.field_id == "fd_rubble";
                for( int n = 0; n < 4 + 3 * entry.field_intensity; ++n ) {
                    const int x = slot.x + 8 + ( n * 19 ) % 44;
                    const int y = slot.y + 8 + ( n * 29 ) % 44;
                    if( web ) {
                        SDL_RenderLine( renderer, x, slot.y + 5, slot.x + 59 - ( n * 11 ) % 48,
                                            slot.y + 59 );
                        SDL_RenderLine( renderer, slot.x + 5, y, slot.x + 59,
                                            slot.y + 59 - ( n * 13 ) % 48 );
                    } else {
                        const int size = debris ? 4 + n % 5 : 7 + 2 * entry.field_intensity;
                        const SDL_Rect patch{ x - size / 2, y - size / 2, size,
                                              debris ? size : size * 2 / 3 };
                        RenderFillRectPixels( renderer, &patch );
                    }
                }
            }
        } else if( !painted && entry.vehicle ) {
            // VT fallback is a readable neutral solid, not a magenta tile.
            // It must never remove a visible vehicle from the scene.
            ++missing;
            DebugLog( D_INFO, D_MAIN ) << "fp3d: vehicle part missing in selected tileset: " << id;
            SDL_SetRenderDrawColor( renderer, 115, 122, 127, 255 );
            RenderFillRectPixels( renderer, &slot );
        } else if( !painted && ( entry.status || entry.field_sprite >= 0 ) ) {
            // A tileset can intentionally omit this marker: no magenta fallback.
        } else if( !painted && entry.plant ) {
            // Missing cutout vegetation must stay transparent. An opaque
            // debug checker is not foliage and was especially conspicuous
            // when a tree crown spans a second native z-level.
            ++missing;
            DebugLog( D_INFO, D_MAIN ) << "fp3d: plant sprite absent in selected tileset: " << id;
        } else if( !painted ) {
            ++missing;
            SDL_SetRenderDrawColor( renderer, 80, 40, 95, 255 );
            for( int y = 0; y < 4; ++y ) {
                for( int x = 0; x < 4; ++x ) {
                    if( ( x + y ) % 2 == 0 ) {
                        const SDL_Rect square{ slot.x + x * 16, slot.y + y * 16, 16, 16 };
                        RenderFillRectPixels( renderer, &square );
                    }
                }
            }
        }
        // The native red/blue/white paint color is encoded in the 2D
        // tileset, not the vehicle JSON definition.  Read only body/roof
        // swatches as the atlas is being populated; never read pixels in
        // the per-frame GL renderer and never copy a different tileset.
        if( entry.vehicle && painted &&
            ( id.find( "roof" ) != std::string::npos ||
              id.find( "board" ) != std::string::npos ||
              id.find( "door" ) != std::string::npos ||
              id.find( "hood" ) != std::string::npos ||
              id.find( "hatch" ) != std::string::npos ||
              id.find( "fender" ) != std::string::npos ) ) {
            constexpr int sample_size = 48;
            std::array<Uint8, sample_size * sample_size * 4> rgba{};
            const SDL_Rect sample{ slot.x + 8, slot.y + 8, sample_size, sample_size };
            if( ReadRenderPixels( renderer, &sample, SDL_PIXELFORMAT_RGBA32,
                                      rgba.data(), sample_size * 4 ) == 0 ) {
                std::array<float, 3> sums{};
                unsigned colored = 0;
                for( int i = 0; i < sample_size * sample_size; ++i ) {
                    const unsigned char red = rgba[i * 4];
                    const unsigned char green = rgba[i * 4 + 1];
                    const unsigned char blue = rgba[i * 4 + 2];
                    const unsigned char alpha = rgba[i * 4 + 3];
                    const auto hi = std::max( { red, green, blue } );
                    const auto lo = std::min( { red, green, blue } );
                    if( alpha < 140 || hi < 61 || hi - lo < 26 || hi > 245 ) {
                        continue; // dark fake shadows, neutral metal and glare
                    }
                    sums[0] += red;
                    sums[1] += green;
                    sums[2] += blue;
                    ++colored;
                }
                if( colored >= 15 ) {
                    const float intensity = 1.08f / ( 255.f * colored );
                    vehicle_colors[id] = { std::clamp( sums[0] * intensity, .0f, 1.0f ),
                                           std::clamp( sums[1] * intensity, .0f, 1.0f ),
                                           std::clamp( sums[2] * intensity, .0f, 1.0f ) };
                }
            }
        }
        slots.emplace( id, index );
        slot_keys.push_back( id );
    }
    // Appending slots preserves every existing UV. Only atlas resets above
    // invalidate cached geometry; a new terrain ID changes its own chunk key.
    SDL_FlushRenderer( renderer );
    DebugLog( D_INFO, D_MAIN ) << "fp3d: material atlas slots=" << slots.size()
                               << ", added=" << pending.size() << ", missing=" << missing;
}

std::optional<std::array<float, 3>> vehicle_body_color( const std::string &key )
{
    const auto it = vehicle_colors.find( key );
    if( it == vehicle_colors.end() ) {
        return std::nullopt;
    }
    return it->second;
}

SDL_Texture *gpu_colour_atlas( SDL_Renderer *renderer )
{
    if( !renderer || !renderer_owner || renderer_owner->get() != renderer ||
        !SDL_GetGPURendererDevice( renderer ) || !atlas || failed || slots.empty() ) {
        return nullptr;
    }
    return atlas.get();
}

std::optional<uv_rect> lookup_gpu( const std::string &id )
{
    const auto it = slots.find( id );
    if( !atlas || failed || it == slots.end() ) {
        return std::nullopt;
    }
    const float x = ( it->second % slots_per_row ) * slot_size;
    const float y = ( it->second / slots_per_row ) * slot_size;
    const float inset = ( id.compare( 0, 6, "$roof:" ) == 0 ||
                          id.compare( 0, 7, "$field:" ) == 0 ||
                          id.compare( 0, 8, "$ground:" ) == 0 ) ? .5f : 2.5f;
    // GPU renderer texture coordinates are normalized; no GL binding scale.
    return uv_rect{ ( x + inset ) / atlas_size, ( y + inset ) / atlas_size,
                    ( x + slot_size - inset ) / atlas_size,
                    ( y + slot_size - inset ) / atlas_size };
}


unsigned revision()
{
    return material_revision;
}

std::size_t atlas_slot_count()
{
    return slots.size();
}

bool ready()
{
    return atlas && !failed && !slots.empty();
}


std::optional<uv_rect> lookup_auxiliary_uv( const std::string &id )
{
    const auto it = slots.find( id );
    if( !atlas || failed || it == slots.end() ) {
        return std::nullopt;
    }
    // Keep identical texel padding and texture extent as the live
    // colour atlas: same per-material UVs across all three atlas textures.
    const float x = ( it->second % slots_per_row ) * slot_size;
    const float y = ( it->second / slots_per_row ) * slot_size;
    const float inset = ( id.compare( 0, 6, "$roof:" ) == 0 ||
                          id.compare( 0, 7, "$field:" ) == 0 ||
                          id.compare( 0, 8, "$ground:" ) == 0 ) ? .5f : 2.5f;
    return uv_rect{ ( x + inset ) / atlas_size * texture_width,
                    ( y + inset ) / atlas_size * texture_height,
                    ( x + slot_size - inset ) / atlas_size * texture_width,
                    ( y + slot_size - inset ) / atlas_size * texture_height };
}

static const fp3d_material_aux::maps *cache_auxiliary( const std::string &id, unsigned slot,
        const Uint8 *pixels )
{
    const auto inserted = auxiliary_cache.try_emplace( id );
    auto &maps = inserted.first->second;
    if( inserted.second ) {
        maps = fp3d_material_aux::generate( id, pixels, slot_size, slot_size, slot_size * 4 );
        if( maps.width == slot_size && maps.height == slot_size ) {
            const auto where = std::lower_bound( auxiliary_slots.begin(), auxiliary_slots.end(),
            slot, []( const auxiliary_tile &entry, unsigned index ) {
                return entry.slot < index;
            } );
            auxiliary_slots.insert( where, { slot, &maps } );
        }
    }
    return &maps;
}

void cache_auxiliary_readback( unsigned source_revision, unsigned slot, const Uint8 *pixels )
{
    if( pixels && source_revision == material_revision && slot < slot_keys.size() ) {
        cache_auxiliary( slot_keys[slot], slot, pixels );
    }
}

const fp3d_material_aux::maps *auxiliary( const std::string &id )
{
    // No GPU-side effects until a caller explicitly requests PBR data.
    const auto cached = auxiliary_cache.find( id );
    if( cached != auxiliary_cache.end() ) {
        return &cached->second;
    }
    const auto index = slots.find( id );
    if( index == slots.end() || !atlas || failed || bound || !renderer_owner ||
        !*renderer_owner ) {
        return nullptr;
    }
    SDL_Renderer *renderer = renderer_owner->get();
    saved_renderer_state saved( renderer );
    if( !SDL_SetRenderTarget( renderer, atlas.get() ) ) {
        return nullptr;
    }
    // Reading this *one* existing 64x64 atlas tile is deliberately lazy:
    // no 2048x2048 framebuffer readback on startup or every 3D frame.
    const SDL_Rect region{ static_cast<int>( index->second % slots_per_row ) * slot_size,
                           static_cast<int>( index->second / slots_per_row ) * slot_size,
                           slot_size, slot_size };
    std::array<std::uint8_t, slot_size * slot_size * 4> pixels{};
    SDL_FlushRenderer( renderer );
    if( ReadRenderPixels( renderer, &region, SDL_PIXELFORMAT_RGBA32,
                              pixels.data(), slot_size * 4 ) != 0 ) {
        DebugLog( D_WARNING, D_MAIN ) << "fp3d: auxiliary texture sampling unavailable: "
                                       << SDL_GetError();
        return nullptr;
    }
    return cache_auxiliary( id, index->second, pixels.data() );
}

unsigned prefetch_auxiliary( unsigned budget )
{
    unsigned generated = 0;
    // Deterministic traversal; retry a failed read only after a tileset reset,
    // not on every idle game frame.
    while( auxiliary_cursor < slot_keys.size() && generated < budget ) {
        const std::string &id = slot_keys[auxiliary_cursor++];
        const auto material = fp3d_material_aux::classify( id );
        // Sprite cutouts, water, sky, or unknown keys need no invented bump.
        if( material.normal_strength == 0.0f && material.metallic == 0.0f ) {
            continue;
        }
        if( auxiliary( id ) ) {
            ++generated;
        }
    }
    return generated;
}

std::vector<unsigned> visible_auxiliary_slots( const fp3d_snapshot::scene_snapshot &scene )
{
    if( !atlas || failed || bound ) {
        return {};
    }
    struct candidate {
        int distance = 0;
        std::string id;
        unsigned slot = 0;
    };
    std::vector<candidate> wanted;
    std::unordered_set<std::string> found;
    for( const auto &cell : scene.cells ) {
        if( cell.known != fp3d_snapshot::knowledge::visible || cell.exterior_only ) {
            continue;
        }
        const auto relative = cell.position - scene.origin;
        const int dx = std::abs( relative.x() );
        const int dz = std::abs( relative.y() );
        if( dx > 32 || dz > 32 || std::abs( relative.z() ) > 3 ) {
            continue;
        }
        const std::string key = terrain_key( cell );
        const auto slot = slots.find( key );
        if( slot == slots.end() ||
            auxiliary_cache.find( key ) != auxiliary_cache.end() ||
            !found.insert( key ).second ) {
            continue;
        }
        const auto properties = fp3d_material_aux::classify( key );
        if( properties.normal_strength == 0.0f && properties.metallic == 0.0f ) {
            continue;
        }
        // Priority for nearby wall faces: their large, screen-filling texels
        // make an arbitrary atlas-order warmup particularly ineffective.
        const bool wall = cell.kind == fp3d_snapshot::terrain_kind::wall;
        wanted.push_back( { dx * dx + dz * dz - ( wall ? 8 : 0 ), key, slot->second } );
    }
    std::sort( wanted.begin(), wanted.end(), []( const candidate &a,
    const candidate &b ) {
        return a.distance != b.distance ? a.distance < b.distance :
               a.id < b.id;
    } );
    std::vector<unsigned> requested;
    const size_t count = std::min<size_t>( wanted.size(), auxiliary_read_limit );
    requested.reserve( count );
    for( size_t i = 0; i < count; ++i ) {
        requested.push_back( wanted[i].slot );
    }
    return requested;
}

const std::vector<auxiliary_tile> &auxiliary_tiles()
{
    return auxiliary_slots;
}

static furniture_uv furniture_surfaces( const std::string &id, unsigned part_index,
        std::optional<uv_rect> ( *lookup_surface )( const std::string & ) )
{
    const auto surface = lookup_surface( "$furniture:" + id );
    furniture_uv result{ surface, surface, surface };
    if( !surface || !selected ) {
        return result;
    }
    const bool retro = selected->get_tileset_id() == "retrodays";
    if( !retro && selected->get_tileset_id() != "UNDEAD_PEOPLE_BASE" ) {
        return result;
    }
    const auto *appearance = resolve_vegetation( *selected, id, season );
    const auto uses = [&]( const std::string &base ) {
        return appearance && appearance == resolve_vegetation( *selected, base, season );
    };
    const auto crop = [&]( float x0, float y0, float x1, float y1 ) {
        const float width = surface->right - surface->left;
        const float height = surface->bottom - surface->top;
        // Endpoints are source pixel centres within the padded 32x32 slot.
        return uv_rect{ surface->left + width *x0 / 31,
                        surface->top + height *y0 / 31,
                        surface->left + width *x1 / 31,
                        surface->top + height *y1 / 31 };
    };
    if( retro ) {
        // These 10x10 glyphs have black backgrounds. Sample the ceramic
        // strokes so the 3D fixtures keep their silhouette from every angle.
        if( uses( "f_sink" ) ) {
            result.top = result.front = result.side = crop( 14, 23, 17, 23 );
        } else if( uses( "f_bathtub" ) ) {
            result.top = result.front = result.side = crop( 14, 14, 17, 14 );
        } else if( uses( "f_toilet" ) ) {
            result.top = result.front = result.side = crop( 17, 20, 20, 20 );
        }
        return result;
    }
    // The remaining layouts are specific to the Undead People artwork.
    if( uses( "f_coffee_table" ) ) {
        // The artwork is a low table silhouette surrounded by transparency.
        // Map the wooden top, apron and leg wood to their physical parts only.
        const auto wood = crop( 3, 14, 5, 18 );
        result.top = result.front = result.side = wood;
        if( part_index == 0 ) {
            result.top = crop( 3, 13, 28, 18 );
            result.front = result.side = crop( 3, 19, 28, 19 );
        }
    } else if( uses( "f_cupboard" ) ) {
        result.top = crop( 1, 1, 30, 9 );
        result.front = crop( 1, 11, 30, 30 );
        result.side = crop( 1, 12, 3, 29 );
    } else if( uses( "f_crate_c" ) ) {
        result.top = crop( 1, 1, 30, 16 );
        result.front = crop( 1, 18, 30, 30 );
        result.side = crop( 2, 19, 4, 29 );
    } else if( uses( "f_crate_o" ) ) {
        result.top = crop( 3, 3, 28, 13 );
        result.front = crop( 1, 18, 30, 30 );
        result.side = crop( 2, 19, 4, 29 );
    } else if( uses( "f_solar_unit" ) || uses( "f_solar_unit_v2" ) ) {
        result.top = part_index < 2 ? crop( 3, 8, 6, 20 ) : crop( 15, 5, 16, 22 );
        result.front = result.side = crop( 15, 5, 16, 22 );
    } else if( uses( "f_locker" ) ) {
        // Preserve both doors together on the cabinet front. The top band
        // belongs only on top; a small panel patch supplies unstriped side metal.
        result.top = crop( 5, 1, 26, 8 );
        result.front = crop( 5, 10, 26, 29 );
        result.side = crop( 8, 21, 12, 26 );
    } else if( uses( "f_wardrobe" ) ) {
        result.top = crop( 3, 2, 28, 6 );
        result.front = crop( 3, 9, 28, 30 );
        result.side = crop( 4, 2, 26, 6 );
    } else if( uses( "f_bookcase" ) ) {
        result.top = crop( 2, 1, 29, 6 );
        // The books are the front of the case, not the side or top.
        result.front = crop( 3, 9, 29, 29 );
        result.side = crop( 1, 10, 3, 29 );
    } else if( uses( "f_rack" ) ) {
        result.top = crop( 2, 1, 29, 8 );
        result.front = result.side = crop( 1, 10, 2, 28 );
    } else if( uses( "f_dresser" ) ) {
        result.top = crop( 1, 1, 30, 8 );
        result.front = crop( 1, 10, 30, 30 );
        result.side = crop( 2, 11, 5, 29 );
    } else if( uses( "f_sink" ) ) {
        const auto rim = crop( 7, 10, 25, 10 );
        const auto metal = crop( 12, 2, 12, 4 );
        if( part_index == 0 ) {
            result.front = crop( 2, 14, 29, 30 );
            result.side = crop( 3, 16, 6, 28 );
            result.top = crop( 7, 6, 24, 8 );
        } else if( part_index < 5 ) {
            result.top = result.front = result.side = rim;
        } else {
            result.top = result.front = result.side = metal;
        }
    } else if( uses( "f_toilet" ) ) {
        // The toilet sprite is 64x80 and centered horizontally in the slot.
        const auto porcelain = crop( 14, 19, 17, 22 );
        result.top = result.front = result.side = porcelain;
        if( part_index == 2 ) {
            result.top = crop( 14, 24, 17, 24 );
        }
    } else if( uses( "f_washer" ) || uses( "f_dryer" ) ) {
        result.top = crop( 3, 1, 28, 4 );
        result.front = crop( 1, 6, 30, 30 );
        result.side = crop( 2, 11, 5, 25 );
    } else if( uses( "f_dishwasher" ) ) {
        result.top = crop( 4, 1, 27, 2 );
        result.front = crop( 1, 8, 30, 30 );
        result.side = crop( 3, 12, 6, 24 );
    } else if( uses( "f_woodstove" ) ) {
        const auto metal = crop( 12, 2, 15, 8 );
        result.top = result.front = result.side = metal;
        if( part_index == 0 ) {
            result.front = crop( 10, 13, 22, 25 );
            result.side = crop( 11, 14, 16, 20 );
        } else if( part_index == 1 ) {
            result.top = crop( 11, 11, 20, 12 );
        }
    } else if( uses( "f_oven" ) ) {
        if( part_index == 0 ) {
            result.top = crop( 3, 6, 28, 10 );
            result.front = crop( 1, 11, 30, 30 );
            result.side = crop( 3, 12, 5, 29 );
        } else {
            result.front = crop( 3, 2, 28, 5 );
            result.top = result.side = crop( 4, 2, 6, 4 );
        }
    } else if( uses( "f_glass_fridge" ) ) {
        result.top = result.front = result.side = crop( 2, 21, 29, 29 );
        if( part_index == 1 ) {
            // The upper blue rectangle is the horizontal sliding glass lid.
            result.top = crop( 0, 0, 31, 19 );
            result.front = result.side = crop( 1, 1, 30, 2 );
        }
    } else if( uses( "f_fridge" ) ) {
        // The 64x80 sprite is letterboxed in the atlas. Sample its opaque
        // appliance pixels after that fit, away from the surrounding shadow.
        result.top = crop( 11, 14, 20, 17 );
        result.front = crop( 10, 17, 21, 30 );
        result.side = crop( 14, 19, 18, 22 );
    } else if( uses( "f_sofa" ) ) {
        result.top = part_index >= 3 ? crop( 2, 4, 5, 27 ) : crop( 8, 11, 22, 16 );
        result.front = result.side = crop( 9, 12, 21, 15 );
    } else if( uses( "f_bench" ) ) {
        const auto wood = crop( 8, 7, 23, 25 );
        const auto frame = crop( 1, 8, 4, 24 );
        result.top = result.front = result.side = part_index == 0 || part_index == 3 ? wood : frame;
    } else if( uses( "f_armchair" ) ) {
        const auto fabric = crop( 10, 5, 21, 10 );
        const auto arm = crop( 4, 13, 7, 20 );
        result.top = result.front = result.side = part_index >= 3 ? arm : fabric;
    } else if( uses( "f_fireplace" ) ) {
        const auto stone = crop( 11, 5, 16, 7 );
        result.top = result.front = result.side = stone;
        if( part_index == 3 ) {
            result.front = crop( 12, 14, 16, 14 );
        } else if( part_index == 4 ) {
            result.front = crop( 3, 15, 28, 15 );
        } else if( part_index == 5 ) {
            result.front = crop( 7, 3, 25, 12 );
        }
    } else if( uses( "f_stool" ) ) {
        const auto metal = crop( 15, 23, 17, 28 );
        result.top = result.front = result.side = metal;
        if( part_index == 0 ) {
            result.top = crop( 8, 10, 25, 19 );
            result.front = result.side = crop( 12, 16, 18, 18 );
        }
    } else if( uses( "f_table" ) ) {
        // Flat overhead grain. Side/legs use a small neutral grain patch.
        result.top = crop( 1, 1, 30, 30 );
        result.front = result.side = crop( 12, 13, 16, 18 );
    } else if( uses( "f_desk" ) ) {
        result.top = crop( 2, 2, 29, 29 );
        result.front = result.side = crop( 9, 12, 13, 18 );
    } else if( uses( "f_counter" ) ) {
        result.top = crop( 2, 2, 29, 29 );
        result.front = result.side = crop( 11, 10, 16, 15 );
    } else if( uses( "f_bed" ) || uses( "f_bed_down" ) ) {
        // UndeadPeople bed sprite = top-down mattress weave only; use wood
        // from another real furniture texture for bed frame and four legs.
        const auto table = lookup_surface( "$furniture:f_table" );
        const auto separate = []( const uv_rect &rect, float x0, float y0, float x1, float y1 ) {
            const float du = rect.right - rect.left;
            const float dv = rect.bottom - rect.top;
            return uv_rect{ rect.left + du * x0 / 31, rect.top + dv * y0 / 31,
                            rect.left + du * x1 / 31, rect.top + dv * y1 / 31 };
        };
        const auto wood = table ? std::optional<uv_rect>(
                              separate( *table, 12, 12, 17, 20 ) ) : surface;
        result.top = result.front = result.side = wood;
        if( part_index == 1 ) { // mattress, no fabric on wooden frame
            result.top = crop( 3, 3, 28, 28 );
            result.front = result.side = crop( 13, 11, 19, 17 );
        } else if( part_index == 2 ) { // blanket top
            result.top = crop( 3, 16, 28, 28 );
            result.front = result.side = crop( 13, 12, 19, 17 );
        } else if( part_index == 3 ) { // pillow top
            result.top = crop( 4, 3, 27, 11 );
            result.front = result.side = crop( 12, 10, 17, 13 );
        }
    } else if( id == "f_mailbox" || id == "f_vitrified_mailbox" ) {
        // Small mailbox sprite: red painted box plus a *separate* wooden
        // pedestal. The fake 2D cast shadow is not a 3D material.
        const auto post = crop( 13, 20, 16, 28 );
        result.top = result.front = result.side = post;
        if( part_index == 2 || part_index == 3 ) {
            result.top = crop( 10, 7, 25, 10 );
            result.front = crop( 10, 11, 25, 21 );
            result.side = crop( 10, 12, 15, 18 );
        } else if( part_index == 4 ) {
            result.top = result.front = result.side = crop( 25, 10, 27, 16 );
        }
    } else if( id == "f_utility_pole" ) {
        // The large 2D pole has its own shadow and the ground baked in.
        // Only the vertical trunk is used on the solid wooden column.
        const auto timber = crop( 16, 10, 19, 24 );
        result.top = result.front = result.side = timber;
        if( part_index >= 1 ) {
            result.top = result.front = result.side = crop( 11, 11, 14, 14 );
        }
    } else if( id == "f_battery" ) {
        // A battery has steel outer rails, recessed dark centre and two
        // different indicator lights.  The 2D sprite already contains a
        // pseudo-3D rim: only sample *its corresponding physical component*.
        const auto dark_steel = crop( 3, 9, 5, 22 );
        const auto silver = crop( 8, 4, 23, 6 );
        const auto black = crop( 11, 15, 12, 17 ); // Plain dark panel; no duplicate painted LEDs.
        result.top = result.front = result.side = dark_steel;
        if( part_index == 3 || part_index == 4 ) {
            result.top = result.front = result.side = silver;
        } else if( part_index == 5 ) {
            result.top = result.front = result.side = black;
        } else if( part_index == 6 || part_index == 7 ) {
            result.top = result.front = result.side = crop( 7, 12, 9, 21 );
        } else if( part_index == 8 ) {
            result.top = result.front = result.side = crop( 14, 9, 18, 13 );
        } else if( part_index == 9 ) {
            result.top = result.front = result.side = crop( 14, 18, 18, 22 );
        } else if( part_index >= 10 ) {
            result.top = result.front = result.side = crop( 10, 3, 11, 5 );
        }
    } else if( id == "f_street_light" || id == "f_traffic_light" ||
               id == "f_floor_lamp" || id == "f_floor_lamp_on" ) {
        // Keep the real metal pole neutral; the complete large 2D sprite
        // contains a substantial cast shadow and must not be extruded.
        if( const auto metal = lookup_surface( "t_metal_floor" ) ) {
            result.top = result.front = result.side = metal;
        }
        if( id == "f_street_light" && part_index >= 3 ) {
            result.top = crop( 10, 7, 24, 12 );
            result.front = result.side = crop( 10, 8, 23, 12 );
        } else if( id == "f_traffic_light" && part_index >= 2 ) {
            result.top = crop( 13, 7, 20, 10 );
            result.front = crop( 12, 8, 22, 27 );
            result.side = crop( 12, 11, 14, 23 );
        } else if( id == "f_floor_lamp" || id == "f_floor_lamp_on" ) {
            // The source shade is trapezoidal and the powered frame includes
            // a huge fake 2D light halo.  Only sample opaque shade pixels.
            const auto metal = crop( 14, 20, 18, 25 );
            result.top = result.front = result.side = metal;
            if( part_index == 0 || part_index == 1 ) {
                result.top = crop( 12, 27, 20, 28 );
                result.front = result.side = crop( 12, 28, 20, 30 );
            } else if( part_index == 3 ) {
                result.top = crop( 13, 3, 19, 5 );
                result.front = result.side = crop( 13, 6, 19, 11 );
            } else if( part_index == 4 ) {
                result.top = result.front = result.side = crop( 8, 12, 23, 13 );
            } else if( part_index == 5 ) {
                result.top = result.front = result.side = crop( 13, 3, 19, 4 );
            }
        }
    } else if( uses( "f_workbench" ) ) {
        // Workbench sprite: top-down patterned surface plus a fake shadow.
        const auto wood = crop( 5, 8, 9, 12 );
        result.top = result.front = result.side = wood;
        if( part_index == 0 ) {
            result.top = crop( 2, 2, 27, 27 );
        }
    } else if( uses( "f_displaycase" ) || uses( "f_displaycase_b" ) ) {
        // The actual 2D sprite is front-elevation art: its upper blue area
        // must NOT be turned into an extra glass cabinet. Use only the
        // lower pair of drawers (pixel rows 15..30).
        const auto painted_metal = crop( 11, 16, 16, 20 );
        result.top = result.front = result.side = painted_metal;
        if( part_index == 1 ) {
            result.front = crop( 2, 15, 29, 23 ); // upper drawer
        } else if( part_index == 2 ) {
            result.front = crop( 2, 24, 29, 30 ); // lower drawer
        } else if( part_index == 3 ) {
            result.top = crop( 3, 16, 6, 17 );   // plain cabinet paint; no drawer handles
        }
    } else if( uses( "f_filing_cabinet" ) ) {
        // The 2D sprite contains three drawn drawers, while the desired 3D
        // cabinet has four. Repeat the drawn handle motif on every separate
        // front slab: do not sample the sprite's black left-side shadow.
        const auto steel = crop( 10, 7, 15, 10 );
        result.top = result.front = result.side = steel;
        if( part_index == 1 ) {
            result.top = crop( 8, 5, 23, 9 );
        } else if( part_index >= 2 && part_index <= 5 ) {
            result.front = crop( 8, 13, 22, 17 );
            result.top = crop( 10, 9, 20, 11 );
        }
    } else if( uses( "f_vending_c" ) || uses( "f_vending_o" ) ||
               uses( "f_vending_reinforced" ) ) {
        const bool reinforced = uses( "f_vending_reinforced" );
        // All images are front-elevation 2D drawings with black outside
        // x=6..24. Extract painted chassis, red header, real product window,
        // coin controls and the delivery hatch as distinct 3D faces.
        const auto shell = reinforced ? crop( 9, 3, 14, 5 ) :
                           crop( 10, 3, 15, 5 );
        result.top = result.front = result.side = shell;
        if( part_index == 1 ) {
            result.top = crop( 8, 2, 22, 5 );
            result.front = crop( 8, 3, 22, 6 );
        } else if( part_index == 2 ) {
            result.front = reinforced ? crop( 8, 10, 21, 19 ) :
                           crop( 8, 10, 19, 20 );
            result.top = crop( 9, 9, 19, 11 );
        } else if( part_index == 3 ) {
            result.front = reinforced ? crop( 19, 17, 23, 24 ) :
                           crop( 19, 18, 23, 24 );
        } else if( part_index == 4 ) {
            result.front = crop( 9, 24, 20, 27 );
        } else if( part_index == 5 ) {
            result.front = crop( 8, 27, 23, 29 );
        }
    } else if( uses( "f_piano" ) ) {
        // The sprite is an oblique miniature piano; never stamp the whole
        // isometric drawing onto the top or the vertical body.
        const auto black = crop( 7, 12, 10, 16 );
        result.top = result.front = result.side = black;
        if( part_index == 1 ) {
            result.top = crop( 10, 22, 23, 25 ); // actual white-key strip
            result.front = crop( 10, 24, 23, 25 );
        } else if( part_index == 2 ) {
            result.top = crop( 7, 6, 22, 12 ); // black polished lid
        }
    } else if( uses( "f_pool_table" ) ) {
        // One pure green overhead felt tile; never map the full square as a
        // vertical elevation. The wood is inferred from darker pixel patches.
        const auto rail = crop( 2, 23, 4, 26 );
        result.top = result.front = result.side = rail;
        if( part_index == 1 ) {
            result.top = crop( 3, 3, 28, 28 );
        }
    } else if( uses( "f_anvil" ) ) {
        // Actual forged grey lies in the silhouette's y=17..27 region.
        // Old y=13..17 crops sampled transparent black above the anvil.
        const auto steel = crop( 13, 19, 17, 22 );
        result.top = result.front = result.side = steel;
        if( part_index == 0 || part_index == 1 ) {
            result.front = result.side = crop( 13, 24, 18, 27 );
        } else if( part_index == 3 || part_index == 4 || part_index == 5 ) {
            result.top = crop( 11, 18, 21, 21 );
        }
    } else if( uses( "f_kiln_empty" ) || uses( "f_kiln_full" ) ||
               uses( "f_clay_kiln" ) ) {
        // The source art is a 32x32 2D overhead/oblique sprite. Sample only
        // its actual hand-laid rock pixels for 3D blocks, not the black
        // sprite border or the painted fake hole. Index 0 is the coal bed
        // and 1 is the sooty inner backing; every other part is one stone.
        const auto pale = crop( 22, 18, 27, 21 );
        const auto mid = crop( 4, 19, 8, 22 );
        const auto highlight = crop( 24, 19, 28, 22 );
        if( part_index == 0 ) {
            result.top = crop( 12, 25, 19, 28 );
            result.front = result.side = crop( 11, 22, 20, 24 );
        } else if( part_index == 1 ) {
            result.top = result.front = result.side = crop( 11, 22, 20, 24 );
        } else {
            switch( part_index % 4 ) {
                case 0:
                    result.top = highlight;
                    result.front = mid;
                    result.side = pale;
                    break;
                case 1:
                    result.top = pale;
                    result.front = highlight;
                    result.side = mid;
                    break;
                case 2:
                    result.top = pale;
                    result.front = mid;
                    result.side = highlight;
                    break;
                default:
                    result.top = highlight;
                    result.front = pale;
                    result.side = mid;
                    break;
            }
        }
    } else if( uses( "f_kiln_metal_empty" ) || uses( "f_kiln_metal_full" ) ||
               uses( "f_kiln_portable_empty" ) || uses( "f_kiln_portable_full" ) ) {
        // The steel kiln is a taller appliance with a vent/chimney, not
        // the short round stone brazier. No black background patches.
        const auto shell = crop( 12, 14, 17, 18 );
        result.top = result.front = result.side = shell;
        if( part_index == 5 ) {
            result.front = crop( 8, 20, 15, 26 ); // furnace opening
        } else if( part_index == 6 ) {
            result.front = crop( 5, 17, 7, 24 ); // indicator LEDs
        } else if( part_index == 3 || part_index == 4 ) {
            result.top = result.front = result.side = crop( 19, 7, 21, 13 );
        }
    } else if( uses( "f_tablesaw" ) ) {
        const auto metal = crop( 5, 11, 9, 16 );
        result.top = result.front = result.side = metal;
        if( part_index == 0 ) {
            result.top = crop( 3, 3, 28, 9 );
            result.front = crop( 3, 12, 28, 24 );
        } else if( part_index == 5 ) {
            result.top = result.front = result.side = crop( 13, 3, 18, 8 );
        }
    } else if( uses( "f_planter" ) || uses( "f_planter_harvest" ) ||
               uses( "f_planter_mature" ) || uses( "f_planter_seed" ) ||
               uses( "f_planter_seedling" ) ) {
        // Dark dirt inside a wooden top-down planter, framed with real rails.
        const auto wood = crop( 3, 26, 6, 29 );
        result.top = result.front = result.side = wood;
        if( part_index == 1 ) {
            result.top = crop( 6, 7, 26, 22 );
        } else if( part_index >= 2 ) {
            result.top = crop( 2, 3, 29, 5 );
        }
    } else if( uses( "f_chair" ) ) {
        const auto wood = crop( 6, 23, 7, 29 );
        if( part_index == 0 ) {
            result.top = crop( 7, 17, 24, 18 );
            result.front = crop( 6, 19, 25, 20 );
            result.side = wood;
        } else if( part_index == 1 ) {
            result.top = result.side = wood;
            result.front = crop( 8, 3, 23, 13 );
            result.back = result.front;
        } else {
            result.top = result.front = result.side = wood;
        }
    } else if( const auto catalog = fp3d_furniture::describe( id );
               catalog && static_cast<unsigned>( catalog->shape ) >=
               static_cast<unsigned>( fp3d_furniture::kind::udp_alien ) ) {
        // UndeadPeople art frequently combines a drawn top plane above a
        // frontal pseudo-3D elevation. These must be split in UV space:
        // the whole image must NEVER be wrapped around all six cuboid faces.
        // Our generic extraction keeps the silhouette's top and lower body
        // separate, and supplies narrow neutral material strips for 3D sides.
        const auto &shape = catalog->shape;
        const bool flat = shape == fp3d_furniture::kind::udp_mat ||
                          shape == fp3d_furniture::kind::udp_ground_cable ||
                          shape == fp3d_furniture::kind::udp_alien_scar ||
                          shape == fp3d_furniture::kind::udp_bridge_flat ||
                          shape == fp3d_furniture::kind::udp_ash_decal ||
                          shape == fp3d_furniture::kind::udp_leaf_litter ||
                          shape == fp3d_furniture::kind::udp_rubble;
        const bool bale = shape == fp3d_furniture::kind::udp_hay_bale ||
                          shape == fp3d_furniture::kind::udp_archery_bale;
        const bool chimney = shape == fp3d_furniture::kind::udp_chimney;
        const bool scanner = shape == fp3d_furniture::kind::udp_mri ||
                             shape == fp3d_furniture::kind::udp_ct;
        const auto top = flat ? crop( 2, 2, 29, 29 ) :
                         bale ? crop( 9, 9, 23, 21 ) :
                         scanner ? crop( 8, 4, 25, 14 ) :
                         crop( 5, 3, 26, 11 );
        const auto front = bale ? crop( 7, 9, 26, 26 ) :
                           scanner ? crop( 6, 7, 26, 26 ) :
                           crop( 5, 13, 26, 28 );
        const auto neutral = flat ? top :
                             bale ? crop( 7, 10, 12, 19 ) :
                             scanner ? crop( 3, 13, 7, 22 ) :
                             crop( 9, 16, 13, 22 );
        result.top = top;
        result.front = front;
        result.side = result.back = neutral;
        if( chimney ) {
            // Stone shaft and painted red tile roof are DIFFERENT regions.
            const bool roof = part_index >= 2;
            result.top = roof ? crop( 8, 2, 23, 7 ) : crop( 10, 15, 22, 22 );
            result.front = roof ? crop( 7, 3, 25, 12 ) : crop( 11, 14, 21, 28 );
            result.side = result.back = roof ? crop( 9, 5, 15, 10 ) :
                                                crop( 11, 17, 15, 24 );
        }
        if( shape == fp3d_furniture::kind::udp_archery_bale ) {
            // Use the bulls-eye on the end cap and straw for the curved drum.
            result.front = crop( 6, 9, 26, 26 );
            result.side = result.back = crop( 20, 11, 27, 24 );
        }
        if( shape == fp3d_furniture::kind::udp_punching_bag && part_index >= 3 &&
            part_index <= 4 ) {
            result.top = result.front = result.side = result.back =
                crop( 10, 12, 21, 24 );
        }
        // Supporting legs/bolts/rails use actual color patches, not a tiny
        // repetition of the original entire painted cabinet/medical scanner.
        if( part_index != 0 ) {
            const auto &pieces = fp3d_furniture::parts( shape );
            if( part_index < pieces.size() ) {
                const auto &p = pieces[part_index];
                const bool front_panel = p.z > .20f && p.depth < .20f &&
                                         p.top - p.bottom > .16f;
                if( !chimney ) {
                    result.front = front_panel ? front : neutral;
                    result.top = p.width > .35f && p.depth > .25f ? top : neutral;
                }
            }
        }
    }
    return result;
}


furniture_uv lookup_furniture_gpu( const std::string &id, unsigned part_index )
{
    return furniture_surfaces( id, part_index, lookup_gpu );
}

void release()
{
    ++material_revision;
    atlas.reset();
    clear_slots();
    selected.reset();
    renderer_owner = nullptr;
    failed = false;
}
} // namespace fp3d_materials
#endif
