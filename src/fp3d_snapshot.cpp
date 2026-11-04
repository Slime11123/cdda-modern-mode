#include "fp3d_snapshot.h"
#include "fp3d_interaction.h"
#include "fp3d_regression.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <utility>
#include <algorithm>
#include <chrono>
#include <unordered_map>

#include "game.h"
#include "level_cache.h"
#include "lightmap.h"
#include "npc.h"
#include "omdata.h"
#include "overmapbuffer.h"

#include "game_constants.h"

#include "avatar.h"
#include "character.h"
#include "creature_tracker.h"
#include "enums.h"
#include "field.h"
#include "field_type.h"
#include "fp3d_camera.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "map.h"
#include "map_memory.h"
#include "mapdata.h"
#include "monster.h"
#include "mtype.h"
#include "mutation.h"
#include "submap.h"
#include "type_id.h"

namespace fp3d_snapshot
{
std::string item_sprite_id( const item &object )
{
    const std::string &id = object.typeId().str();
    if( id == "corpse" && object.get_mtype() ) {
        return "corpse_" + object.get_mtype()->id.str();
    }
    return id;
}

namespace
{
float city_building_height( const oter_t &terrain )
{
    const std::string land = terrain.get_land_use_code().str();
    const auto &id = terrain.get_type_id().str();
    if( terrain.has_flag( oter_flags::road ) || terrain.has_flag( oter_flags::highway ) ||
        terrain.has_flag( oter_flags::bridge ) || terrain.is_water() || terrain.is_wooded() ) {
        return 0;
    }
    if( land == "residential_multi" || land == "residential_high" ) {
        return 3 * fp3d_camera::floor_height;
    }
    if( land == "industrial" || land == "urban_public_institutional" ) {
        return 2 * fp3d_camera::floor_height;
    }
    if( land == "commercial" ) {
        return 1.5f * fp3d_camera::floor_height;
    }
    if( land.compare( 0, 12, "residential_" ) == 0 ||
        terrain.has_flag( oter_flags::has_sidewalk ) || id.find( "house" ) != std::string::npos ||
        id.find( "apartment" ) != std::string::npos ) {
        return fp3d_camera::floor_height;
    }
    return 0;
}

void capture_landscape( scene_snapshot &scene )
{
    // Existing overmap topology only: no map generation, entities, interiors,
    // exploration requirements or changes to the simulation's reality bubble.
    if( !scene.sky.visible || scene.sky.daylight < .25f || scene.origin.z() < 0 ) {
        return;
    }
    constexpr int step = landscape_patch_size;
    scene.landscape.reserve( 1600 );
    const int first_x = scene.origin.x() - landscape_radius;
    const int first_y = scene.origin.y() - landscape_radius;
    const int left = scene.origin.x() - radius;
    const int right = scene.origin.x() + radius + 1;
    const int north = scene.origin.y() - radius;
    const int south = scene.origin.y() + radius + 1;
    // Absolute alignment keeps textures and tree cards fixed while walking.
    for( int y = first_y - ( first_y % step + step ) % step;
         y < scene.origin.y() + landscape_radius; y += step ) {
        for( int x = first_x - ( first_x % step + step ) % step;
             x < scene.origin.x() + landscape_radius; x += step ) {
            const tripoint_abs_ms position( x, y, 0 );
            const auto omt = project_to<coords::omt>( position + tripoint_rel_ms( step / 2, step / 2, 0 ) );
            const auto &terrain = overmap_buffer.ter_existing( omt );
            if( terrain == oter_id() ) {
                continue;
            }
            const auto &definition = terrain.obj();
            const auto &id = definition.get_type_id().str();
            const bool wooded = definition.is_wooded();
            const std::string ground = definition.is_water() || definition.is_river() || definition.is_lake() ?
                                       "t_water_dp" : id.find( "road" ) != std::string::npos ||
                                       id.find( "highway" ) != std::string::npos || id.find( "bridge" ) != std::string::npos ?
                                       "t_pavement" : wooded ? "t_dirt" : "t_grass";
            const auto append = [&]( int x0, int y0, int x1, int y1 ) {
                if( x1 > x0 && y1 > y0 ) {
                    scene.landscape.push_back( { tripoint_abs_ms( x0, y0, 0 ), ground,
                                                 wooded, x1 - x0, y1 - y0, city_building_height( definition ) } );
                }
            };
            if( x >= right || x + step <= left || y >= south || y + step <= north ) {
                append( x, y, x + step, y + step );
            } else {
                // Cut out the detailed region exactly, without seams or overlap.
                const int inner_left = std::max( x, left );
                const int inner_right = std::min( x + step, right );
                append( x, y, inner_left, y + step );
                append( inner_right, y, x + step, y + step );
                append( inner_left, y, inner_right, std::max( y, north ) );
                append( inner_left, std::min( y + step, south ), inner_right, y + step );
            }
        }
    }
    // Only city silhouettes extend to the horizon; do not multiply the ground
    // sampling budget or load/generate distant detailed submaps.
    const auto city_origin = project_to<coords::omt>( scene.origin );
    constexpr int city_tiles = city_radius / ( SEEX * 2 );
    for( int dy = -city_tiles; dy <= city_tiles; ++dy ) {
        for( int dx = -city_tiles; dx <= city_tiles; ++dx ) {
            const auto omt = city_origin + tripoint_rel_omt( dx, dy, -city_origin.z() );
            const auto position = project_to<coords::ms>( omt );
            const auto delta = position - scene.origin;
            if( std::abs( delta.x() ) < landscape_radius + SEEX * 2 &&
                std::abs( delta.y() ) < landscape_radius + SEEX * 2 ) {
                continue;
            }
            const auto &terrain = overmap_buffer.ter_existing( omt );
            if( terrain == oter_id() ) {
                continue;
            }
            const float height = city_building_height( terrain.obj() );
            if( height > 0 ) {
                scene.landscape.push_back( { position, "t_pavement", false, SEEX * 2, SEEX * 2, height, false } );
            }
        }
    }
}

float plant_height( const std::string &id )
{
    if( id == "t_grass" || id == "t_grass_dead" ) {
        return 0;
    }
    if( id == "t_grass_long" || id == "t_grass_tall" ) {
        return .65f;
    }
    const auto height = []( const auto & definition ) {
        if( definition.has_flag( ter_furn_flag::TFLAG_TREE ) ) {
            return 5.0f;
        }
        if( definition.has_flag( ter_furn_flag::TFLAG_YOUNG ) ) {
            return 2.2f;
        }
        if( definition.has_flag( ter_furn_flag::TFLAG_SHRUB ) ) {
            return 1.2f;
        }
        return definition.has_flag( ter_furn_flag::TFLAG_FLOWER ) ? .5f : 0.0f;
    };
    if( id.compare( 0, 2, "f_" ) == 0 ) {
        const furn_str_id definition( id );
        return definition.is_valid() ? height( definition.obj() ) : 0;
    }
    const ter_str_id definition( id );
    return definition.is_valid() ? height( definition.obj() ) : 0;
}

terrain_kind classify( const std::string &id )
{
    const ter_str_id terrain( id );
    if( !terrain.is_valid() ) {
        return terrain_kind::obstacle;
    }
    const ter_t &definition = terrain.obj();
    const bool door_id = id.find( "door" ) != std::string::npos;
    if( definition.has_flag( ter_furn_flag::TFLAG_BARRICADABLE_DOOR_DAMAGED ) ||
        ( door_id && definition.has_flag( "BARRICADABLE_DOOR_REINFORCED_DAMAGED" ) ) ) {
        return terrain_kind::damaged_door;
    }
    const bool door = definition.has_flag( ter_furn_flag::TFLAG_DOOR ) ||
                      ( definition.movecost == 0 && door_id &&
                        ( definition.has_flag( ter_furn_flag::TFLAG_LOCKED ) ||
                          definition.has_flag( ter_furn_flag::TFLAG_BARRICADABLE_DOOR ) ||
                          definition.has_flag( ter_furn_flag::TFLAG_BARRICADABLE_DOOR_REINFORCED ) ) );
    if( definition.movecost == 0 && id.find( "glass" ) != std::string::npos &&
        ( door || definition.has_flag( ter_furn_flag::TFLAG_WALL ) ) &&
        ( definition.has_flag( ter_furn_flag::TFLAG_TRANSPARENT ) ||
          definition.has_flag( ter_furn_flag::TFLAG_TRANSLUCENT ) ) ) {
        return terrain_kind::glass;
    }
    if( door ) {
        return terrain_kind::closed_door;
    }
    if( !definition.close.is_null() && definition.close.is_valid() &&
        definition.close->has_flag( ter_furn_flag::TFLAG_DOOR ) ) {
        return terrain_kind::open_door;
    }
    if( definition.movecost > 0 && door_id &&
        id.size() >= 6 && id.compare( id.size() - 6, 6, "_frame" ) == 0 ) {
        return terrain_kind::open_door;
    }
    if( definition.has_flag( ter_furn_flag::TFLAG_WINDOW ) || id.compare( 0, 9, "t_window_" ) == 0 ||
        id == "t_curtains" ) {
        return terrain_kind::window;
    }
    if( definition.has_flag( ter_furn_flag::TFLAG_WALL ) ) {
        return terrain_kind::wall;
    }
    return definition.movecost == 0 ? terrain_kind::obstacle : terrain_kind::floor;
}
sprite describe_creature( const Creature &critter )
{
    sprite image;
    image.position = critter.pos_abs();
    image.mirrored = critter.facing == FacingDirection::LEFT;
    switch( critter.get_size() ) {
        case creature_size::tiny:
            image.height = .35f;
            break;
        case creature_size::small:
            image.height = .8f;
            break;
        case creature_size::large:
            image.height = 2.4f;
            break;
        case creature_size::huge:
            image.height = 3.2f;
            break;
        default:
            break;
    }
    if( const monster *mon = critter.as_monster() ) {
        if( mon->has_flag( mon_flag_COPY_AVATAR_LOOK ) || mon->has_flag( mon_flag_COPY_SUMMONER_LOOK ) ) {
            // Do not expose the true species beneath an apparent copied form.
            // A neutral placeholder is conservative until that form is supported.
            image.id = "unknown_monster";
            image.height = 1.8f;
        } else {
            image.id = mon->type->id.str();
            image.overlays = mon->get_overlay_ids();
        }
    } else if( const Character *character = critter.as_character() ) {
        image.kind = sprite_kind::character;
        image.male = character->male;
        const auto overrides = character->get_functioning_mutations( true, false,
        []( const mutation_branch & mutation ) {
            return mutation.override_look.has_value();
        } );
        if( overrides.empty() ) {
            image.id = image.male ? "npc_male" : "npc_female";
            image.overlays = character->get_overlay_ids();
        } else {
            image.id = overrides.front()->override_look->id;
            image.overlays = character->get_overlay_ids_when_override_look();
        }
    }
    image.height *= 1.2f;
    return image;
}

sprite describe_item( const item &object, const tripoint_abs_ms &position )
{
    sprite image;
    image.position = position;
    image.kind = sprite_kind::item;
    image.id = item_sprite_id( object );
    if( object.has_itype_variant() ) {
        image.variant = object.itype_variant().id;
    }
    image.height = .3f;
    return image;
}

// Native shadowcasting omits some upper facade cells even when native ray LOS
// reaches them. Authorize only a surface touching native-visible open air;
// no furniture, items or creatures may use this presentation-only fallback.
bool exposed_surface( const avatar &you, const map &here, const tripoint_bub_ms &p,
                      bool beside_visible_air, bool blind )
{
    return beside_visible_air && p.z() > you.posz() && !blind && !you.cant_see( p ) &&
           here.sees( you.pos_bub( here ), p, you.unimpaired_range() );
}

void capture_sprites( scene_snapshot &scene, const avatar &you, const map &here,
                      const tripoint_bub_ms &p, const Creature *critter )
{
    // Clear terrain visibility is necessary, but not sufficient for a creature.
    // Keep native concealment checks, but ignore lighting for 3D presentation.
    if( critter && !critter->is_avatar() && you.Creature::sees( here, *critter, true ) ) {
        sprite image = describe_creature( *critter );
        if( !image.id.empty() ) {
            scene.sprites.push_back( std::move( image ) );
        }
    }
    const auto tile = here.maptile_at( p );
    // Most visible cells contain no items. Avoid field/container queries there.
    if( tile.get_item_count() > 0 && tile.get_field().displayed_field_type()->display_items &&
        here.could_see_items( p, you ) ) {
        sprite image = describe_item( tile.get_uppermost_item(), here.get_abs( p ) );
        image.multiple_items = tile.get_item_count() > 1;
        image.height = .7f;
        if( const auto furniture = fp3d_furniture::describe( here.furn( p ).id().str() ) ) {
            image.elevation = fp3d_furniture::item_surface( furniture->shape );
        }
        scene.sprites.push_back( std::move( image ) );
    }
}
} // namespace

terrain_kind terrain_shape( const std::string &id )
{
    return classify( id );
}

const std::vector<fp3d_furniture::part> &door_parts( terrain_kind kind )
{
    constexpr float top = fp3d_camera::floor_height;
    static const std::vector<fp3d_furniture::part> closed{ { 0, 0, .98f, .16f, 0, top } };
    static const std::vector<fp3d_furniture::part> open{
        { -.44f, 0, .12f, .12f, 0, top }, { .44f, 0, .12f, .12f, 0, top },
        { 0, 0, .98f, .16f, 2.1f, top }
    };
    static const std::vector<fp3d_furniture::part> damaged{
        { 0, 0, .98f, .16f, 0, .34f },
        { -.325f, 0, .33f, .16f, .34f, .85f }, { .365f, 0, .25f, .16f, .34f, .85f },
        { 0, 0, .98f, .16f, .85f, .95f },
        { -.3925f, 0, .195f, .16f, .95f, 1.76f }, { .3375f, 0, .305f, .16f, .95f, 1.76f },
        { 0, 0, .98f, .16f, 1.76f, top }
    };
    static const std::vector<fp3d_furniture::part> glass{
        { -.47f, 0, .06f, .10f, 0, top }, { .47f, 0, .06f, .10f, 0, top },
        { 0, 0, .88f, .10f, 0, .06f }, { 0, 0, .88f, .10f, top - .08f, top }
    };
    if( kind == terrain_kind::open_door ) {
        return open;
    }
    if( kind == terrain_kind::damaged_door ) {
        return damaged;
    }
    return kind == terrain_kind::glass ? glass : closed;
}

void prepare_view( avatar &you, map &here )
{
    const auto p = you.pos_bub( here );
    if( !here.inbounds( p ) ) {
        return;
    }
    here.build_map_cache( p.z() );
    if( here.supports_zlevels() ) {
        // build_map_cache prepares shared 3D LOS and this layer's native lightmap.
        // update_visibility_cache is deliberately called only once, at player z.
        for( int z = std::max( -OVERMAP_DEPTH, p.z() - render_below );
             z <= std::min( OVERMAP_HEIGHT, p.z() + render_above ); ++z ) {
            if( z != p.z() ) {
                here.build_map_cache( z );
            }
        }
    }
    here.update_visibility_cache( p.z() );
    you.prepare_map_memory_region(
        here.get_abs( p - tripoint_rel_ms( radius, radius, 0 ) ),
        here.get_abs( p + tripoint_rel_ms( radius, radius, 0 ) ) );
}

scene_snapshot capture( avatar &you, const map &here, int above, int below )
{
    const auto started = std::chrono::steady_clock::now();
    // Equipment and body state cannot change during this synchronous capture.
    const bool blind = you.is_blind();
    scene_snapshot result;
    result.origin = you.pos_abs();
    result.hud = fp3d_hud::capture( you );
    result.season = season_of_year( calendar::turn );
    if( !blind ) {
        if( const auto wielded = you.get_wielded_item() ) {
            result.held_item = describe_item( *wielded, result.origin );
            result.held_item->kind = sprite_kind::held_item;
        }
    }
    const tripoint_bub_ms player = you.pos_bub( here );
    if( !here.inbounds( player ) ) {
        return result;
    }
    result.sky = fp3d_sky::sample( calendar::turn,
                                   player.z() >= 0 && here.is_outside( player ) && !blind );
    const visibility_variables &variables = here.get_visibility_variables_cache();
    const bool fresh = variables.variables_set && !variables.visibility_cache_dirty &&
                       variables.last_pos == player;
    result.min_z = here.supports_zlevels() ? std::max( -OVERMAP_DEPTH,
                   player.z() - std::clamp( below, 0, std::min( render_below, fov_3d_z_range ) ) ) : player.z();
    result.max_z = here.supports_zlevels() ? std::min( OVERMAP_HEIGHT,
                   player.z() + std::clamp( above, 0, std::min( render_above, fov_3d_z_range ) ) ) : player.z();
    // Query native perception once per cell, including a one-cell halo for
    // exposed-surface neighbours. The raw 3D seen/camera cache can reject
    // obstructed cells cheaply; it is not the 2D viewport visibility mask.
    constexpr int side = radius * 2 + 3;
    constexpr unsigned char visible_bit = 1;
    constexpr unsigned char air_bit = 2;
    const int first_x = player.x() - radius - 1;
    const int first_y = player.y() - radius - 1;
    std::vector<unsigned char> perception( ( result.max_z - result.min_z + 1 ) * side * side );
    const auto index = [&]( const tripoint_bub_ms & p ) {
        return ( ( p.z() - result.min_z ) * side + p.y() - first_y ) * side + p.x() - first_x;
    };
    const auto perception_started = std::chrono::steady_clock::now();
    if( fresh && !blind ) {
        const int clairvoyance = you.clairvoyance();
        const int sight_range = you.unimpaired_range();
        for( int z = result.min_z; z <= result.max_z; ++z ) {
            const auto &cache = here.access_cache( z );
            for( int y = first_y; y < first_y + side; ++y ) {
                for( int x = first_x; x < first_x + side; ++x ) {
                    const tripoint_bub_ms p( x, y, z );
                    if( !here.inbounds( p ) ) {
                        continue;
                    }
                    if( !clairvoyance && cache.seen_cache[x][y] <= LIGHT_TRANSPARENCY_SOLID + .1f &&
                        cache.camera_cache[x][y] <= LIGHT_TRANSPARENCY_SOLID + .1f ) {
                        continue;
                    }
                    const int distance = rl_dist( player, p );
                    const bool clairvoyant = distance < MAX_CLAIRVOYANCE && distance < clairvoyance;
                    if( !clairvoyant && square_dist( player, p ) > std::min( sight_range, distance ) &&
                        cache.camera_cache[x][y] == 0 ) {
                        continue;
                    }
                    // 3D keeps native obstruction and range, but dark cells
                    // still contribute terrain, furniture, items and creatures.
                    const auto light = map::apparent_light_helper( cache, p );
                    if( clairvoyant || !light.obstructed ) {
                        perception[index( p )] = visible_bit | ( here.is_open_air( p ) ? air_bit : 0 );
                    }
                }
            }
        }
    }
    const auto perception_finished = std::chrono::steady_clock::now();
    std::unordered_map<tripoint_bub_ms, const Creature *> creatures;
    if( fresh && g ) {
        // Native lookup scans NPCs. Do it at occupied positions, not 100k cells.
        const auto &tracker = get_creature_tracker();
        for( const Creature &critter : g->all_creatures() ) {
            const auto p = critter.pos_bub( here );
            if( critter.is_avatar() || p.z() < result.min_z || p.z() > result.max_z ||
                p.x() <= first_x || p.x() >= first_x + side - 1 ||
                p.y() <= first_y || p.y() >= first_y + side - 1 || !here.inbounds( p ) ||
                !( perception[index( p )] & visible_bit ) ) {
                continue;
            }
            if( tracker.creature_at( p, true ) == &critter ) {
                creatures.emplace( p, &critter );
            }
        }
    }
    result.cells.reserve( ( radius * 2 + 1 ) * ( radius * 2 + 1 ) );
    for( int z = result.min_z; z <= result.max_z; ++z ) {
        for( int dy = -radius; dy <= radius; ++dy ) {
            for( int dx = -radius; dx <= radius; ++dx ) {
                const tripoint_bub_ms p = player + tripoint_rel_ms( dx, dy, z - player.z() );
                if( !here.inbounds( p ) ) {
                    continue;
                }
                cell tile;
                tile.position = here.get_abs( p );
                // 3D visibility uses LOS and range independently of lighting,
                // the 2D visibility mask and previously memorized terrain.
                const bool visible = perception[index( p )] & visible_bit;
                if( visible && p.z() == player.z() && !result.sky.visible && player.z() >= 0 && !blind &&
                    here.is_outside( p ) ) {
                    // Seen outdoors through a window/door also needs a sky
                    // backdrop. Known room ceilings/walls still occlude it.
                    result.sky = fp3d_sky::sample( calendar::turn, true );
                }
                const bool beside_visible_air =
                    ( perception[index( p + tripoint_rel_ms::north )] & air_bit ) ||
                    ( perception[index( p + tripoint_rel_ms::south )] & air_bit ) ||
                    ( perception[index( p + tripoint_rel_ms::east )] & air_bit ) ||
                    ( perception[index( p + tripoint_rel_ms::west )] & air_bit );
                // Geometry in a dark room must still occlude the exterior.
                // Native geometric LOS gates surfaces; contents retain full perception checks.
                const bool room_surface = fresh && !visible && p.z() == player.z() && !blind &&
                                          !here.is_outside( p ) &&
                                          here.access_cache( z ).seen_cache[p.x()][p.y()] > LIGHT_TRANSPARENCY_SOLID + .1f &&
                                          here.sees( player, p, you.unimpaired_range() );
                tile.exterior_only = fresh && !visible && ( room_surface ||
                                     exposed_surface( you, here, p, beside_visible_air, blind ) );
                if( visible || tile.exterior_only ) {
                    tile.terrain = here.ter( p ).id().str();
                    tile.known = knowledge::visible;
                    if( visible ) {
                        const auto creature = creatures.find( p );
                        capture_sprites( result, you, here, p,
                                         creature == creatures.end() ? nullptr : creature->second );
                        if( visible && !tile.exterior_only && p.z() == player.z() && square_dist( p, player ) <= 1 ) {
                            const fp3d_targeting::target target{ you.pos_abs(), tile.position, fp3d_targeting::kind::terrain, tile.terrain };
                            const auto choices = fp3d_interaction::context_entries( target, you, here );
                            if( std::any_of( choices.begin(), choices.end(), []( const auto &choice ) {
                                return choice.visible && choice.enabled && ( choice.action != ACTION_NULL || choice.command );
                            } ) ) { result.interactable.push_back( tile.position ); }
                        }
                        // Empty sky/shaft cells carry no geometry. Keep only an
                        // overhead boundary; flying creatures were copied above.
                        const auto overhead = p + tripoint_rel_ms( 0, 0, 1 );
                        if( ( perception[index( p )] & air_bit ) &&
                            ( !here.inbounds( overhead ) || !here.has_floor( overhead ) ) ) {
                            continue;
                        }
                    }
                } else {
                    // Never query live terrain, furniture, items or creatures here.
                    tile.terrain = you.get_memorized_tile( tile.position ).get_ter_id();
                    tile.known = knowledge::remembered;
                }
                if( tile.terrain.empty() || tile.terrain == "t_null" ) {
                    continue;
                }
                const ter_str_id terrain( tile.terrain );
                if( !terrain.is_valid() ) {
                    continue;
                }
                const ter_t &definition = terrain.obj();
                tile.open_air = ( visible ||
                                  tile.exterior_only ) ? here.is_open_air( p ) : tile.terrain == "t_open_air";
                tile.floor_present = ( visible || tile.exterior_only ) ? here.has_floor( p ) :
                                     !definition.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) &&
                                     !definition.has_flag( ter_furn_flag::TFLAG_NO_FLOOR_WATER );
                if( ( visible || tile.exterior_only ) && !blind && here.supports_zlevels() ) {
                    const auto overhead = p + tripoint_rel_ms( 0, 0, 1 );
                    // Copy only the visible room's enclosing boundary. Never
                    // copy the unseen upstairs terrain ID, contents or layout.
                    tile.ceiling_present = here.inbounds( overhead ) &&
                                           ( here.has_floor( overhead ) || !here.is_outside( p ) );
                }
                if( tile.known == knowledge::remembered && definition.has_flag( ter_furn_flag::TFLAG_INDOORS ) ) {
                    // Remembered indoor geometry must enclose the remembered room
                    // too; never inspect the live, unseen upstairs here.
                    tile.ceiling_present = true;
                }
                if( tile.open_air && !tile.ceiling_present ) {
                    continue;
                }
                tile.goes_up = definition.has_flag( ter_furn_flag::TFLAG_GOES_UP );
                tile.goes_down = definition.has_flag( ter_furn_flag::TFLAG_GOES_DOWN );
                tile.is_ramp = definition.has_flag( ter_furn_flag::TFLAG_RAMP ) ||
                               definition.has_flag( ter_furn_flag::TFLAG_RAMP_UP ) ||
                               definition.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN );
                tile.ramp_delta = !tile.is_ramp ? 0 :
                                  definition.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN ) ? -1 : 1;
                tile.kind = classify( tile.terrain );
                if( tile.kind == terrain_kind::window ) {
                    tile.pane = !definition.has_flag( ter_furn_flag::TFLAG_TRANSPARENT ) ?
                                window_pane::covered : definition.movecost > 0 ?
                                window_pane::opening : window_pane::glazed;
                }
                if( tile.exterior_only && ( tile.open_air ||
                                            tile.kind == terrain_kind::obstacle || plant_height( tile.terrain ) > 0 ) ) {
                    continue;
                }
                tile.vegetation_height = plant_height( tile.terrain );
                if( tile.vegetation_height > 0 ) {
                    tile.vegetation = tile.terrain;
                    tile.kind = terrain_kind::floor;
                }
                // Furniture is read only for native-visible cells. Remembered
                // decorations come from memory, never from hidden live furniture.
                const std::string furniture = tile.exterior_only ? "" : visible ?
                                              here.furn( p ).id().str() :
                                              you.get_memorized_tile( tile.position ).get_dec_id();
                if( const float height = plant_height( furniture ); height > 0 ) {
                    tile.vegetation = furniture;
                    tile.vegetation_height = height;
                }
                if( visible && !tile.exterior_only ) {
                    tile.furniture = fp3d_furniture::describe( furniture );
                }
                if( tile.known == knowledge::visible ) {
                    ++result.visible;
                } else {
                    ++result.remembered;
                }
                result.cells.push_back( std::move( tile ) );
            }
        }
    }
    const auto detail_finished = std::chrono::steady_clock::now();
    if( fresh ) {
        capture_landscape( result );
    }
    const auto finished = std::chrono::steady_clock::now();
    result.capture_ms = std::chrono::duration<double, std::milli>( finished - started ).count();
    fp3d_regression::snapshot_costs(
        std::chrono::duration<double, std::milli>( detail_finished - started ).count(),
        std::chrono::duration<double, std::milli>( finished - detail_finished ).count(),
        std::chrono::duration<double, std::milli>( perception_finished - perception_started ).count(),
        std::chrono::duration<double, std::milli>( detail_finished - perception_finished ).count() );
    return result;
}
} // namespace fp3d_snapshot
#endif
