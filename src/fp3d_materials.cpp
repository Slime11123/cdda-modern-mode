#include "fp3d_materials.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cata_tiles.h"
#include "debug.h"
#include "field_type.h"
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

void prepare( const fp3d_snapshot::scene_snapshot &scene, const fp3d_snapshot::cell *preview )
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
    struct request {
        std::string key;
        const fp3d_snapshot::sprite *image = nullptr;
        bool plant = false;
        bool window = false;
        bool furniture = false;
        bool blood = false;
        bool fence = false;
    };
    std::vector<request> pending;
    std::unordered_set<std::string> model_keys;
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
            std::string key = "$furniture:" + tile.furniture->id;
            if( model_keys.insert( key ).second ) {
                pending.push_back( { std::move( key ), nullptr, false, false, true } );
            }
        }
        if( !tile.blood.empty() && pending.size() < max_slots ) {
            std::string key = "$blood:" + tile.blood;
            if( model_keys.insert( key ).second ) {
                pending.push_back( { std::move( key ), nullptr, false, false, false, true } );
            }
        }
    };
    if( scene.held_item ) {
        pending.push_back( { sprite_key( *scene.held_item ), &*scene.held_item } );
    }
    if( preview ) {
        prioritize_cell( *preview );
    }
    for( const auto &tile : scene.cells ) {
        prioritize_cell( tile );
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
        slots.clear();
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
        if( tile.kind == fp3d_snapshot::terrain_kind::ladder ) {
            add( "t_concrete", nullptr );
        } else if( tile.kind == fp3d_snapshot::terrain_kind::downspout || tile.kind == fp3d_snapshot::terrain_kind::fence ) {
            add( "t_dirt", nullptr );
        } else if( tile.kind == fp3d_snapshot::terrain_kind::window ) {
            add( "t_floor", nullptr );
            add( "t_wall", nullptr );
            const auto before = pending.size();
            add( "$window:" + tile.terrain, nullptr );
            if( pending.size() > before ) {
                pending.back().window = true;
            }
        } else if( tile.kind != fp3d_snapshot::terrain_kind::glass ) {
            add( tile.terrain, nullptr );
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
    for( const auto &tile : scene.cells ) {
        add_cell( tile );
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
        const bool transparent = entry.image || entry.plant || entry.window || entry.blood || entry.fence;
        SDL_SetRenderDrawColor( renderer, transparent ? 0 : 180, transparent ? 0 : 180,
                                transparent ? 0 : 180, transparent ? 0 : 255 );
        SDL_RenderFillRect( renderer, &slot );
        const tile_type *tile = nullptr;
        if( entry.blood ) {
            std::string name = id.substr( 7 );
            for( int tries = 0; tries < 10 && !name.empty(); ++tries ) {
                if( auto found = selected->find_tile_type_by_season( name, season ) ) {
                    tile = &found->tile();
                    break;
                }
                const field_type_str_id definition( name );
                name = definition.is_valid() ? definition->looks_like : "";
            }
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
        bool painted = false;
        if( tile ) {
            const auto sprites = choose_sprites( *tile, id );
            if( ( !entry.plant && !entry.window && !entry.blood && !entry.fence ) ||
                ( entry.window && sprites.foreground < 0 ) ) {
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

furniture_uv lookup_furniture( const std::string &id, unsigned part_index )
{
    const auto surface = lookup( "$furniture:" + id );
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
    if( uses( "f_cupboard" ) ) {
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
        result.front = result.side = crop( 1, 10, 3, 29 );
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
    }
    return result;
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
