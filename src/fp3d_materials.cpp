#include "fp3d_materials.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cata_tiles.h"
#include "debug.h"
#include "fp3d_snapshot.h"
#include "mapdata.h"
#include "item.h"
#include "itype.h"
#include "mtype.h"
#include "type_id.h"

namespace fp3d_materials
{
namespace
{
constexpr int atlas_size = 1024;
constexpr int slot_size = 64;
constexpr int slots_per_row = atlas_size / slot_size;
constexpr int max_slots = slots_per_row * slots_per_row;
std::shared_ptr<const tileset> selected;
const SDL_Renderer_Ptr *renderer_owner = nullptr;
SDL_Texture_Ptr atlas;
unsigned material_revision = 0;
std::unordered_map<std::string, unsigned> slots;
season_type season = SPRING;
float texture_width = 1;
float texture_height = 1;
bool bound = false;
bool failed = false;

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
    SDL_bool clipping;
    SDL_bool integer_scale;
    int logical_width;
    int logical_height;
    float scale_x;
    float scale_y;
    SDL_BlendMode blend;
    Uint8 r, g, b, a;

    explicit saved_renderer_state( SDL_Renderer *value ) : renderer( value ),
        target( SDL_GetRenderTarget( value ) ), clipping( SDL_RenderIsClipEnabled( value ) ),
        integer_scale( SDL_RenderGetIntegerScale( value ) ) {
        SDL_RenderGetViewport( renderer, &viewport );
        SDL_RenderGetClipRect( renderer, &clip );
        SDL_RenderGetLogicalSize( renderer, &logical_width, &logical_height );
        SDL_RenderGetScale( renderer, &scale_x, &scale_y );
        SDL_GetRenderDrawBlendMode( renderer, &blend );
        SDL_GetRenderDrawColor( renderer, &r, &g, &b, &a );
    }
    ~saved_renderer_state() {
        SDL_SetRenderTarget( renderer, target );
        SDL_RenderSetIntegerScale( renderer, integer_scale );
        SDL_RenderSetLogicalSize( renderer, logical_width, logical_height );
        SDL_RenderSetViewport( renderer, &viewport );
        SDL_RenderSetScale( renderer, scale_x, scale_y );
        SDL_RenderSetClipRect( renderer, clipping ? &clip : nullptr );
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
    slots.clear();
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
    SDL_Texture_Ptr icon( SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA8888,
                          SDL_TEXTUREACCESS_TARGET, slot_size, slot_size ) );
    if( !icon || SDL_SetRenderTarget( renderer, icon.get() ) != 0 ) {
        return {};
    }
    SDL_RenderSetLogicalSize( renderer, 0, 0 );
    SDL_RenderSetScale( renderer, 1, 1 );
    SDL_RenderSetViewport( renderer, nullptr );
    SDL_RenderSetClipRect( renderer, nullptr );
    SDL_SetRenderDrawColor( renderer, 0, 0, 0, 0 );
    SDL_RenderClear( renderer );
    if( !paint_sprite( image, SDL_Rect{ 0, 0, slot_size, slot_size } ) ) {
        SDL_SetRenderDrawColor( renderer, 150, 155, 165, 255 );
        const SDL_Rect placeholder{ 16, 16, 32, 32 };
        SDL_RenderFillRect( renderer, &placeholder );
    }
    SDL_SetTextureBlendMode( icon.get(), SDL_BLENDMODE_BLEND );
    SDL_RenderFlush( renderer );
    return icon;
}

void prepare( const fp3d_snapshot::scene_snapshot &scene )
{
    if( !selected || !renderer_owner || !*renderer_owner || failed ) {
        return;
    }
    if( season != scene.season ) {
        ++material_revision;
        season = scene.season;
        atlas.reset();
        slots.clear();
    }
    // Keep a newly equipped item drawable after world materials fill the atlas.
    if( scene.held_item && slots.size() == max_slots &&
        !slots.count( sprite_key( *scene.held_item ) ) ) {
        ++material_revision;
        atlas.reset();
        slots.clear();
    }
    struct request {
        std::string key;
        const fp3d_snapshot::sprite *image = nullptr;
        bool plant = false;
        bool window = false;
        bool furniture = false;
    };
    std::vector<request> pending;
    const auto add = [&]( const std::string & key, const fp3d_snapshot::sprite * image ) {
        if( !slots.count( key ) && slots.size() + pending.size() < max_slots &&
        std::none_of( pending.begin(), pending.end(), [&]( const request & entry ) {
        return entry.key == key;
    } ) ) {
            pending.push_back( { key, image } );
        }
    };
    if( scene.held_item ) {
        add( sprite_key( *scene.held_item ), &*scene.held_item );
    }
    for( const auto &tile : scene.cells ) {
        if( tile.furniture ) {
            const auto before = pending.size();
            add( "$furniture:" + tile.furniture->id, nullptr );
            if( pending.size() > before ) {
                pending.back().furniture = true;
            }
        }
        if( tile.kind == fp3d_snapshot::terrain_kind::window ) {
            add( "t_floor", nullptr );
            add( "t_wall", nullptr );
            const auto before = pending.size();
            add( "$window:" + tile.terrain, nullptr );
            if( pending.size() > before ) {
                pending.back().window = true;
            }
        } else {
            add( tile.terrain, nullptr );
        }
        if( tile.ceiling_present ) {
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
    }
    for( const auto &patch : scene.landscape ) {
        if( patch.ground_present ) {
            add( patch.terrain, nullptr );
        }
        if( patch.building_height > 0 ) {
            add( "t_wall", nullptr );
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
        add( sprite_key( image ), &image );
    }
    if( pending.empty() ) {
        return;
    }
    SDL_Renderer *renderer = renderer_owner->get();
    saved_renderer_state saved( renderer );
    if( !atlas ) {
        atlas.reset( SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_TARGET, atlas_size, atlas_size ) );
        if( !atlas ) {
            failed = true;
            DebugLog( D_WARNING, D_MAIN ) << "fp3d: material atlas unavailable: " << SDL_GetError();
            return;
        }
        SDL_SetTextureBlendMode( atlas.get(), SDL_BLENDMODE_NONE );
        SDL_SetTextureScaleMode( atlas.get(), SDL_ScaleModeNearest );
    }
    if( SDL_SetRenderTarget( renderer, atlas.get() ) != 0 ) {
        failed = true;
        return;
    }
    SDL_RenderSetLogicalSize( renderer, 0, 0 );
    SDL_RenderSetIntegerScale( renderer, SDL_FALSE );
    SDL_RenderSetScale( renderer, 1, 1 );
    SDL_RenderSetViewport( renderer, nullptr );
    SDL_RenderSetClipRect( renderer, nullptr );
    SDL_SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_NONE );
    unsigned missing = 0;
    for( const request &entry : pending ) {
        const std::string &id = entry.key;
        const unsigned index = slots.size();
        const SDL_Rect slot{ static_cast<int>( index % slots_per_row ) * slot_size,
                             static_cast<int>( index / slots_per_row ) * slot_size, slot_size, slot_size };
        const bool transparent = entry.image || entry.plant || entry.window;
        SDL_SetRenderDrawColor( renderer, transparent ? 0 : 180, transparent ? 0 : 180,
                                transparent ? 0 : 180, transparent ? 0 : 255 );
        SDL_RenderFillRect( renderer, &slot );
        const tile_type *tile = entry.image ? nullptr : entry.furniture ?
                                resolve_vegetation( *selected, id.substr( 11 ), season ) : entry.window ?
                                resolve_terrain( *selected, id.substr( 8 ), season ) : entry.plant ?
                                resolve_vegetation( *selected, id.substr( 7 ), season ) :
                                resolve_terrain( *selected, id, season );
        bool painted = false;
        if( tile ) {
            const auto sprites = choose_sprites( *tile, id );
            if( ( !entry.plant && !entry.window ) || ( entry.window && sprites.foreground < 0 ) ) {
                painted = paint( sprites.background, slot );
            }
            painted = paint( sprites.foreground, slot ) || painted;
        }
        if( entry.image ) {
            painted = paint_sprite( *entry.image, slot );
        }
        if( !painted ) {
            ++missing;
            SDL_SetRenderDrawColor( renderer, 80, 40, 95, 255 );
            for( int y = 0; y < 4; ++y ) {
                for( int x = 0; x < 4; ++x ) {
                    if( ( x + y ) % 2 == 0 ) {
                        const SDL_Rect square{ slot.x + x * 16, slot.y + y * 16, 16, 16 };
                        SDL_RenderFillRect( renderer, &square );
                    }
                }
            }
        }
        slots.emplace( id, index );
    }
    // Appending slots preserves every existing UV. Only atlas resets above
    // invalidate cached geometry; a new terrain ID changes its own chunk key.
    SDL_RenderFlush( renderer );
    DebugLog( D_INFO, D_MAIN ) << "fp3d: material atlas slots=" << slots.size()
                               << ", added=" << pending.size() << ", missing=" << missing;
}

bool bind()
{
    bound = atlas && !failed && SDL_GL_BindTexture( atlas.get(), &texture_width, &texture_height ) == 0;
    if( atlas && !failed && !bound ) {
        failed = true;
        DebugLog( D_WARNING, D_MAIN ) << "fp3d: GL texture interop unavailable: " << SDL_GetError();
    }
    return bound;
}

unsigned revision()
{
    return material_revision;
}

bool ready()
{
    return atlas && !failed && !slots.empty();
}

void unbind()
{
    if( bound ) {
        SDL_GL_UnbindTexture( atlas.get() );
        bound = false;
    }
}

std::optional<uv_rect> lookup( const std::string &id )
{
    const auto it = slots.find( id );
    if( !bound || it == slots.end() ) {
        return std::nullopt;
    }
    const float x = ( it->second % slots_per_row ) * slot_size;
    const float y = ( it->second / slots_per_row ) * slot_size;
    return uv_rect{ ( x + 2.5f ) / atlas_size * texture_width,
                    ( y + 2.5f ) / atlas_size * texture_height,
                    ( x + slot_size - 2.5f ) / atlas_size * texture_width,
                    ( y + slot_size - 2.5f ) / atlas_size * texture_height };
}

void release()
{
    ++material_revision;
    unbind();
    atlas.reset();
    slots.clear();
    selected.reset();
    renderer_owner = nullptr;
    failed = false;
}
} // namespace fp3d_materials
#endif
