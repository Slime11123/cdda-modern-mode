#include "fp3d_snapshot.h"
#include "fp3d_interaction.h"
#include "fp3d_regression.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <utility>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <tuple>

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
#include "effect.h"
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
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"

namespace fp3d_snapshot
{
void reuse_geometry( scene_snapshot &scene, scene_snapshot *previous )
{
    const auto equal = []( const cell &a, const cell &b ) {
        if( std::tie( a.position, a.terrain, a.roof_connections, a.kind, a.known,
                     a.vegetation, a.appliance, a.vegetation_height, a.pane, a.exterior_only,
                     a.ceiling_present, a.glass_ceiling, a.ceiling_surface, a.floor_present,
                     a.metal_deck_railing, a.open_air, a.goes_up, a.goes_down, a.is_ramp,
                     a.ramp_delta, a.over_avatar_region ) !=
            std::tie( b.position, b.terrain, b.roof_connections, b.kind, b.known,
                     b.vegetation, b.appliance, b.vegetation_height, b.pane, b.exterior_only,
                     b.ceiling_present, b.glass_ceiling, b.ceiling_surface, b.floor_present,
                     b.metal_deck_railing, b.open_air, b.goes_up, b.goes_down, b.is_ramp,
                     b.ramp_delta, b.over_avatar_region ) ||
            bool( a.furniture ) != bool( b.furniture ) || bool( a.stairs ) != bool( b.stairs ) ) {
            return false;
        }
        if( a.furniture && ( a.furniture->id != b.furniture->id ||
                             a.furniture->shape != b.furniture->shape ) ) { return false; }
        if( a.stairs && std::tie( a.stairs->rows, a.stairs->width, a.stairs->south, a.stairs->east ) !=
            std::tie( b.stairs->rows, b.stairs->width, b.stairs->south, b.stairs->east ) ) { return false; }
        return a.ground_fields.size() == b.ground_fields.size() &&
               std::equal( a.ground_fields.begin(), a.ground_fields.end(), b.ground_fields.begin(),
        []( const ground_field &x, const ground_field &y ) {
            return x.id == y.id && x.intensity == y.intensity;
        } );
    };
    scene.geometry_generation = scene.render_generation;
    if( previous && previous->geometry_generation &&
        scene.cells.size() == previous->cells.size() &&
        std::equal( scene.cells.begin(), scene.cells.end(), previous->cells.begin(), equal ) ) {
        scene.cells = std::move( previous->cells );
        scene.geometry_generation = previous->geometry_generation;
    }
}

std::vector<fp3d_furniture::part> climb_parts( const cell &tile )
{
    if( tile.kind == terrain_kind::downspout ) {
        return {
            { 0, -.33f, .13f, .13f, .10f, fp3d_camera::floor_height, true },
            { 0, -.33f, .18f, .18f, .48f, .54f, true },
            { 0, -.33f, .18f, .18f, 1.60f, 1.66f, true },
            { 0, -.23f, .13f, .30f, .04f, .17f },
            { 0, -.39f, .13f, .24f, 2.05f, fp3d_camera::floor_height }
        };
    }
    if( tile.kind != terrain_kind::ladder ) {
        return {};
    }
    const float top = tile.goes_up ? fp3d_camera::floor_height : .62f;
    std::vector<fp3d_furniture::part> result{
        { -.26f, -.32f, .06f, .07f, 0, top },
        { .26f, -.32f, .06f, .07f, 0, top }
    };
    for( float height = .22f; height + .025f <= top; height += .28f ) {
        result.push_back( { 0, -.32f, .52f, .07f, height - .025f, height + .025f } );
    }
    return result;
}

std::vector<fp3d_furniture::part> stair_parts( const cell &tile )
{
    const auto section = tile.stairs.value_or( stair_section{} );
    std::vector<fp3d_furniture::part> result;
    for( int step = 0; step < 12; ++step ) {
        const float start = std::max( 0.0f, step * section.rows / 12.0f - section.south );
        const float end = std::min( 1.0f, ( step + 1 ) * section.rows / 12.0f - section.south );
        if( end > start ) {
            result.push_back( { 0, .5f - ( start + end ) / 2, 1, end - start,
                                0, fp3d_camera::floor_height * ( step + 1 ) / 12 } );
        }
    }
    return result;
}

void connect_stairs( scene_snapshot &scene )
{
    std::unordered_map<tripoint_abs_ms, size_t> remaining;
    std::vector<size_t> ordered;
    for( size_t i = 0; i < scene.cells.size(); ++i ) {
        auto &tile = scene.cells[i];
        tile.stairs.reset();
        if( !tile.exterior_only && ( tile.goes_up || tile.goes_down ) &&
            tile.terrain.find( "stairs" ) != std::string::npos ) {
            remaining.emplace( tile.position, i );
            ordered.push_back( i );
        }
    }
    std::sort( ordered.begin(), ordered.end(), [&]( size_t a, size_t b ) {
        const auto &p = scene.cells[a].position;
        const auto &q = scene.cells[b].position;
        return std::make_tuple( p.z(), p.y(), p.x() ) < std::make_tuple( q.z(), q.y(), q.x() );
    } );
    for( size_t index : ordered ) {
        const auto &anchor = scene.cells[index];
        if( !remaining.count( anchor.position ) ) {
            continue;
        }
        const auto matches = [&]( int x, int y ) {
            const auto it = remaining.find( anchor.position + tripoint_rel_ms( x, y, 0 ) );
            if( it == remaining.end() ) {
                return false;
            }
            const auto &other = scene.cells[it->second];
            return other.terrain == anchor.terrain && other.known == anchor.known &&
                   other.goes_up == anchor.goes_up && other.goes_down == anchor.goes_down;
        };
        int width = 1;
        while( matches( width, 0 ) ) {
            ++width;
        }
        int rows = 1;
        while( true ) {
            int x = 0;
            while( x < width && matches( x, rows ) ) {
                ++x;
            }
            if( x != width ) {
                break;
            }
            ++rows;
        }
        for( int y = 0; y < rows; ++y ) {
            for( int x = 0; x < width; ++x ) {
                const auto it = remaining.find( anchor.position + tripoint_rel_ms( x, y, 0 ) );
                scene.cells[it->second].stairs = stair_section{ rows, width, rows - y - 1, width - x - 1 };
                remaining.erase( it );
            }
        }
    }
    scene.stairs_connected = true;
}

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
// Distant overmap tiles have no generated map squares to inspect. Select the
// closest native wall material for each building family; the renderer takes
// its actual colour/texture from the player's active tileset.
std::string city_wall_material( const oter_t &terrain )
{
    const std::string id = terrain.get_type_id().str();
    const std::string land = terrain.get_land_use_code().str();
    const auto contains = [&]( const char *term ) {
        return id.find( term ) != std::string::npos;
    };
    if( contains( "brick" ) ) {
        return "t_brick_wall";
    }
    if( contains( "log_cabin" ) || contains( "log_house" ) ) {
        return "t_wall_log";
    }
    if( contains( "wood" ) || contains( "cabin" ) || contains( "bungalow" ) ||
        contains( "farmhouse" ) || contains( "farm_house" ) ) {
        return "t_wall_wood";
    }
    if( contains( "metal" ) || contains( "warehouse" ) || contains( "hangar" ) ||
        contains( "factory" ) || contains( "storage_unit" ) ) {
        return "t_wall_metal";
    }
    if( contains( "apartment" ) || contains( "office" ) || contains( "hospital" ) ||
        contains( "school" ) || contains( "hotel" ) || contains( "bank" ) ||
        contains( "police" ) || contains( "prison" ) || contains( "tower" ) ||
        land == "residential_high" || land == "residential_multi" ||
        land == "urban_public_institutional" ) {
        return "t_concrete_wall";
    }
    if( contains( "house" ) || land.compare( 0, 12, "residential_" ) == 0 ) {
        return "t_wall_wood";
    }
    if( contains( "store" ) || contains( "shop" ) || contains( "restaurant" ) ||
        contains( "s_gas" ) || land == "commercial" ) {
        return "t_brick_wall";
    }
    if( land == "industrial" ) {
        return "t_wall_metal";
    }
    return "t_wall";
}

float city_building_height( const oter_t &terrain, const tripoint_abs_omt &omt )
{
    const std::string land = terrain.get_land_use_code().str();
    const auto &id = terrain.get_type_id().str();
    if( terrain.has_flag( oter_flags::road ) || terrain.has_flag( oter_flags::highway ) ||
        terrain.has_flag( oter_flags::bridge ) || terrain.is_water() || terrain.is_wooded() ) {
        return 0;
    }
    // This is a cheap distant silhouette, not generated map geometry. Only
    // recognised urban footprints receive boxes; roads/forests stay clear.
    const bool building = land == "residential_multi" || land == "residential_high" ||
                          land == "industrial" || land == "urban_public_institutional" ||
                          land == "commercial" || land.compare( 0, 12, "residential_" ) == 0 ||
                          terrain.has_flag( oter_flags::has_sidewalk ) ||
                          id.find( "house" ) != std::string::npos ||
                          id.find( "apartment" ) != std::string::npos;
    if( !building ) {
        return 0;
    }

    // Overmap specials store each real upper storey at z=1,2,... and usually
    // place a separate roof over the last floor. The roof is not a storey.
    // Read existing overmap terrain only: no generation or unseen submaps.
    int storeys = 1;
    for( int z = 1; z <= OVERMAP_HEIGHT; ++z ) {
        const oter_id &upper = overmap_buffer.ter_existing( omt + tripoint_rel_omt( 0, 0, z ) );
        if( upper == oter_id() ) {
            break;
        }
        const oter_t &upper_terrain = upper.obj();
        const std::string upper_id = upper_terrain.get_type_id().str();
        if( upper_id == "open_air" || upper_id == "field" ||
            upper_id == "empty_rock" || upper_id == "solid_earth" ||
            upper_id.find( "roof" ) != std::string::npos ||
            upper_terrain.has_flag( oter_flags::road ) ||
            upper_terrain.has_flag( oter_flags::highway ) ||
            upper_terrain.has_flag( oter_flags::bridge ) ||
            upper_terrain.is_water() || upper_terrain.is_wooded() ) {
            break;
        }
        ++storeys;
    }
    return storeys * fp3d_camera::floor_height;
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
            // Keep legitimate CDDA terrain IDs: this game version has no
            // generic t_road terrain/atlas icon. Road overmaps are represented
            // by t_pavement and must be tiled, NOT stretched across 12m.
            const std::string ground = definition.is_water() || definition.is_river() || definition.is_lake() ?
                                       "t_water_dp" : id.find( "road" ) != std::string::npos ||
                                       id.find( "highway" ) != std::string::npos || id.find( "bridge" ) != std::string::npos ?
                                       "t_pavement" : wooded ? "t_dirt" : "t_grass";
            // An edge patch may be clipped into four pieces; count floors once.
            const float height = city_building_height( definition, omt );
            const std::string wall = height > 0 ? city_wall_material( definition ) : "t_wall";
            const auto append = [&]( int x0, int y0, int x1, int y1 ) {
                if( x1 > x0 && y1 > y0 ) {
                    scene.landscape.push_back( { tripoint_abs_ms( x0, y0, 0 ), ground,
                                                 wooded, x1 - x0, y1 - y0, height, true, wall } );
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
            const float height = city_building_height( terrain.obj(), omt );
            if( height > 0 ) {
                scene.landscape.push_back( { position, "t_pavement", false, SEEX * 2, SEEX * 2,
                                             height, false, city_wall_material( terrain.obj() ) } );
            }
        }
    }
}

float plant_height( const std::string &id )
{
    // UndeadPeople f_midden is a tangled triffid organic bush. Never create
    // a fake solid cylindrical machine from its vegetation-style sprite.
    if( id == "f_midden" || id == "f_alien_anemone" ||
        id == "f_sea_anemone" || id == "f_sea_sponge" ) {
        return .95f;
    }
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
    if( id == "t_ladder_up" || id == "t_ladder_down" || id == "t_ladder_up_down" ) {
        return terrain_kind::ladder;
    }
    if( id == "t_gutter_downspout" || id == "t_vitrified_downspout" ) {
        return terrain_kind::downspout;
    }
    if( id.compare( 0, 12, "t_chainfence" ) == 0 ||
        id.compare( 0, 19, "t_chickenwire_fence" ) == 0 ||
        id == "t_fence" || id == "t_privacy_fence" ||
        id == "t_fence_post" || id == "t_screened_porch_wall" ||
        id == "t_concrete_railing" || id == "t_railing" ||
        id == "t_metal_railing" || id == "t_guardrail" ||
        id == "t_guardrail_bg_dp" || id == "t_guardrail_hw_air" ) {
        return terrain_kind::fence;
    }
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

sprite describe_creature( const Creature &critter, const avatar &you, const map &here )
{
    sprite image;
    image.position = critter.pos_abs();
    image.mirrored = critter.facing == FacingDirection::LEFT;
    // Copy already-present native effect IDs once; never synthesize gameplay
    // statuses from damage or reveal unseen actor metadata.
    std::unordered_set<std::string> active_effects;
    for( const auto &active : critter.get_effects() ) {
        active_effects.insert( active.get().get_id().str() );
    }
    image.bleeding = active_effects.count( "bleed" ) != 0;
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
            // Monster overlays are effect markers, rendered as separate badges.
            // Their oversized transparent canvases must not resize the body.
            // Only mark genuinely aggressive monsters with current native LOS.
            image.alerted = mon->attitude_to( you ) == Creature::Attitude::HOSTILE &&
                            mon->sees( here, you ) && !you.has_trait( trait_id( "INATTENTIVE" ) );
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
    // Map every remaining UndeadPeople effect marker to its exact active
    // effect ID. The pack-only temp/morale_wet art stays dormant unless an
    // optional mod actually registers that effect. Urgent effects come first;
    // four displayed badges per creature avoid long ribbons across the world.
    static constexpr const char *extra_effects_by_priority[] = {
        "onfire",
        "attention",
        "stunned",
        "downed",
        "grabbed",
        "webbed",
        "poison",
        "badpoison",
        "paralyzepoison",
        "zapped",
        "shattered",
        "winded",
        "hypovolemia",
        "asthma",
        "dazed",
        "fetid_goop",
        "teargas",
        "boomered",
        "spores",
        "nausea",
        "flu",
        "common_cold",
        "foodpoison",
        "frostbite",
        "blisters",
        "cold",
        "hot",
        "glowing",
        "slimed",
        "laserlocked",
        "glare",
        "snow_glare",
        "deaf",
        "visuals",
        "drunk",
        "high",
        "sleep",
        "shrieking",
        "pacified",
        "docile",
        "pet",
        "evil",
        "happy",
        "sad",
        "earphones",
        "playing_instrument",
        "milked",
        "bouldering",
        "teleglow",
        "morale_wet",
        "temp"
    };
    const unsigned reserved = static_cast<unsigned>( image.alerted ) +
                              static_cast<unsigned>( image.bleeding );
    for( const char *id : extra_effects_by_priority ) {
        if( reserved + image.status_icons.size() >= 4 ) {
            break;
        }
        if( active_effects.count( id ) != 0 ) {
            image.status_icons.emplace_back( std::string( "overlay_effect_" ) + id );
        }
    }
    // Use the same native-size baseline as the avatar (medium = 1.8m).
    // The former extra 20% scale made zombies and NPCs dwarf the player.
    return image;
}

sprite describe_item( const item &object, const tripoint_abs_ms &position )
{
    sprite image;
    image.position = position;
    image.kind = sprite_kind::item;
    image.id = item_sprite_id( object );
    // Pulped, skinned and otherwise non-revivable corpses must lose the badge.
    image.revivable = object.can_revive();
    if( object.has_itype_variant() ) {
        image.variant = object.itype_variant().id;
    }
    image.height = .3f;
    return image;
}

// Native shadowcasting omits some upper facade cells beside visible exterior
// air. Also retain ray-visible facades behind a deep balcony;
// no furniture, items or creatures may use this presentation-only fallback.
bool exposed_surface( const avatar &you, const map &here, const tripoint_bub_ms &p,
                      const tripoint_bub_ms &player, int sight_range, bool beside_visible_air, bool blind )
{
    if( p.z() <= player.z() || blind || you.cant_see( p ) ||
        square_dist( player, p ) > sight_range ) {
        return false;
    }
    const terrain_kind kind = classify( here.ter( p ).id().str() );
    const bool facade = kind == terrain_kind::wall || kind == terrain_kind::window ||
                        kind == terrain_kind::glass || kind == terrain_kind::closed_door ||
                        kind == terrain_kind::open_door || kind == terrain_kind::damaged_door;
    // An unseen floor above the player is already represented by the room's
    // neutral ceiling. Only facade surfaces may bypass endpoint ray LOS.
    return facade ? ( beside_visible_air || here.sees( player, p, sight_range ) ) :
           ( beside_visible_air && here.sees( player, p, sight_range ) );
}

void capture_sprites( scene_snapshot &scene, const avatar &you, const map &here,
                      const tripoint_bub_ms &p, const Creature *critter )
{
    // Clear terrain visibility is necessary, but not sufficient for a creature.
    // Keep native concealment checks, but ignore lighting for 3D presentation.
    if( critter && !critter->is_avatar() && you.Creature::sees( here, *critter, true ) ) {
        sprite image = describe_creature( *critter, you, here );
        if( !image.id.empty() ) {
            scene.sprites.push_back( std::move( image ) );
        }
    }
    const auto tile = here.maptile_at( p );
    // Most visible cells contain no items. Avoid field/container queries there.
    if( tile.get_item_count() > 0 && tile.get_field().displayed_field_type()->display_items &&
        here.could_see_items( p, you ) ) {
        const item &top_item = tile.get_uppermost_item();
        sprite image = describe_item( top_item, here.get_abs( p ) );
        const int native_item_light = top_item.getlight_emit();
        if( native_item_light > 0 ) {
            scene.lights.push_back( { here.get_abs( p ), light_kind::portable,
                                      static_cast<float>( native_item_light ), .3f,
                                      top_item.typeId().str() } );
        }
        image.multiple_items = tile.get_item_count() > 1;
        image.height = .7f;
        if( const auto furniture = fp3d_furniture::describe( here.furn( p ).id().str() ) ) {
            image.elevation = fp3d_furniture::item_surface( furniture->shape );
            if( furniture->shape == fp3d_furniture::kind::udp_glass_cabinet ) {
                // Fit the cutout inside the upper glass compartment at every
                // camera pitch, including a fully camera-facing overhead quad.
                image.height = .48f;
            }
        }
        scene.sprites.push_back( std::move( image ) );
    }
}
} // namespace

terrain_kind terrain_shape( const std::string &id )
{
    return classify( id );
}

bool is_glass_roof( const std::string &id )
{
    // The native skylight (t_glass_roof) is walkable roof glass: TRANSPARENT
    // keeps sight through it and TRANSPARENT_FLOOR keeps the roof usable.
    // Decide from the loaded definition rather than one mapgen ID, so a mod
    // glass roof inherits the same translucent pane instead of an opaque
    // sheet. A solid walkable floor without glass in its identity stays
    // opaque; ramps and open-air transitions are excluded by the glass test.
    const ter_str_id terrain( id );
    if( !terrain.is_valid() ) {
        return id == "t_glass_roof";
    }
    const ter_t &definition = terrain.obj();
    return definition.movecost > 0 &&
           definition.has_flag( ter_furn_flag::TFLAG_TRANSPARENT_FLOOR ) &&
           definition.has_flag( ter_furn_flag::TFLAG_TRANSPARENT ) &&
           id.find( "glass" ) != std::string::npos;
}

std::vector<fp3d_furniture::part> fence_parts( const std::array<bool, 4> &neighbours,
        bool posts_only )
{
    std::vector<fp3d_furniture::part> result{ { 0, 0, .06f, .06f, 0, 2.0f } };
    if( posts_only ) {
        return result;
    }
    auto linked = neighbours;
    if( std::none_of( linked.begin(), linked.end(), []( bool value ) { return value; } ) ) {
        linked[1] = linked[3] = true;
    }
    for( int direction = 0; direction < 4; ++direction ) {
        if( !linked[direction] ) {
            continue;
        }
        const bool east_west = direction % 2 != 0;
        const float sign = direction < 2 ? 1.0f : -1.0f;
        result.push_back( { east_west ? sign * .25f : 0, east_west ? 0 : -sign * .25f,
                            east_west ? .5f : 0, east_west ? 0 : .5f, .06f, 1.94f } );
    }
    return result;
}

bool fence_connects( const std::string &a, const std::string &b )
{
    if( a == b ) {
        return true;
    }
    const auto starts = []( const std::string & id, const std::string & prefix ) {
        return id.compare( 0, prefix.size(), prefix ) == 0;
    };
    const auto chain = [&]( const std::string &id ) {
        return starts( id, "t_chainfence" ) ||
               starts( id, "t_chickenwire_fence" ) || is_chain_gate( id );
    };
    return ( chain( a ) && chain( b ) ) ||
           ( starts( a, "t_guardrail" ) && starts( b, "t_guardrail" ) );
}

bool is_chain_gate( const std::string &id )
{
    return id == "t_chaingate_c" || id == "t_chaingate_o" || id == "t_chaingate_l" ||
           id == "t_chickenwire_gate_c" || id == "t_chickenwire_gate_o";
}

bool chain_gate_along_x( const std::array<bool, 4> &neighbours )
{
    // Native neighbour order: north, east, south, west.
    return int( neighbours[1] ) + int( neighbours[3] ) >=
           int( neighbours[0] ) + int( neighbours[2] );
}

std::vector<fp3d_furniture::part> chain_gate_parts( const std::string &id, bool along_x )
{
    if( !is_chain_gate( id ) ) {
        return {};
    }
    using part = fp3d_furniture::part;
    constexpr float top = 2.02f;
    const bool opened = id == "t_chaingate_o" || id == "t_chickenwire_gate_o";
    // The two boundary posts remain fixed while the door leaf pivots around
    // its left hinge. No full-cell slab or invisible panel for open doors.
    std::vector<part> frame{
        { -.46f, 0, .075f, .075f, 0, top },
        { .46f, 0, .075f, .075f, 0, top },
        { -.46f, 0, .13f, .13f, top - .08f, top + .04f },
        { .46f, 0, .13f, .13f, top - .08f, top + .04f }
    };
    // The real fixed upper crossmember stays in place while the leaf opens.
    frame.push_back( { 0, 0, .93f, .055f, 1.97f, 2.03f } );
    if( opened ) {
        // Follow the V12 visible hinged leaf: the open wire panel swings
        // about x=-.425 and extends towards negative z, not a ghost
        // crosswise wall remaining in the closed position.
        frame.push_back( { -.425f, -.03f, .057f, .057f, .18f, 1.87f } );
        frame.push_back( { -.425f, -.74f, .057f, .057f, .18f, 1.87f } );
        for( const float y : { .20f, .985f, 1.85f } ) {
            frame.push_back( { -.425f, -.385f, .056f, .77f, y - .03f, y + .03f } );
        }
        frame.push_back( { -.425f, -.74f, .11f, .085f, .91f, 1.08f } );
    } else {
        // Rectangular hollow leaf, separate from the two exterior posts.
        frame.push_back( { -.36f, 0, .055f, .055f, .16f, 1.89f } );
        frame.push_back( { .36f, 0, .055f, .055f, .16f, 1.89f } );
        for( const float y : { .16f, .98f, 1.87f } ) {
            frame.push_back( { 0, 0, .76f, .052f, y - .026f, y + .026f } );
        }
        frame.push_back( { .365f, .035f, .095f, .085f, .91f, 1.07f } ); // latch
        if( id == "t_chaingate_l" ) {
            frame.push_back( { .365f, .095f, .095f, .07f, .84f, .94f } ); // padlock
        }
    }
    if( !along_x ) {
        for( part &p : frame ) {
            p = fp3d_furniture::rotated( p, 1 );
        }
    }
    return frame;
}

std::vector<fp3d_furniture::part> terrain_model_parts( const std::string &id,
        const std::array<bool, 4> &neighbours )
{
    using part = fp3d_furniture::part;
    if( id == "t_radio_tower" ) {
        // Thin targeting proxies follow the open 3D steel frame. Never make
        // the entire transparent tile an invisible solid ray-hit box.
        constexpr float half = .34f;
        const float height = fp3d_camera::floor_height;
        std::vector<part> result;
        result.reserve( 64 );
        for( const float x : { -half, half } ) {
            for( const float z : { -half, half } ) {
                result.push_back( { x, z, .095f, .095f, 0, height } );
            }
        }
        for( int level = 0; level <= 2; ++level ) {
            const float y = height * level / 2.f;
            for( const float side : { -half, half } ) {
                result.push_back( { 0, side, .74f, .075f,
                                    std::max( 0.f, y - .04f ), std::min( height, y + .04f ) } );
                result.push_back( { side, 0, .075f, .74f,
                                    std::max( 0.f, y - .04f ), std::min( height, y + .04f ) } );
            }
        }
        // Three short proxies per diagonal face brace, including the central
        // X intersections, approximate the actual tilted beams for picking.
        for( int level = 0; level < 2; ++level ) {
            for( int sample = 0; sample < 3; ++sample ) {
                const float t = ( sample + .5f ) / 3.f;
                const float y = height * ( level + t ) / 2.f;
                const float across = half * ( 2.f * t - 1.f );
                for( const float side : { -half, half } ) {
                    for( const float sign : { -1.f, 1.f } ) {
                        result.push_back( { across * sign, side, .27f, .09f,
                                            std::max( 0.f, y - .20f ), std::min( height, y + .20f ) } );
                        result.push_back( { side, across * sign, .09f, .27f,
                                            std::max( 0.f, y - .20f ), std::min( height, y + .20f ) } );
                    }
                }
            }
        }
        return result;
    }
    if( id == "t_column" || id == "t_pillar" ||
        id == "t_little_column" || id == "t_column_halfway" ) {
        const bool little = id == "t_little_column";
        const float shaft = little ? .36f : id == "t_pillar" ? .61f : .52f;
        const float height = id == "t_column_halfway" ? 1.49f : 3.0f;
        const float cap = shaft + .12f;
        return {
            { 0, 0, cap, cap, 0, .17f },            // stone plinth
            { 0, 0, shaft, shaft, .17f, height - .17f },
            { 0, 0, cap, cap, height - .17f, height },
            { 0, .5f * shaft + .012f, .18f, .025f, .32f, height - .32f }
        };
    }
    const bool picket = id == "t_fence";
    const bool privacy = id == "t_privacy_fence";
    const bool concrete = id == "t_concrete_railing";
    const bool wood = id == "t_railing";
    const bool steel = id == "t_metal_railing";
    const bool road = id.compare( 0, 11, "t_guardrail" ) == 0;
    if( !picket && !privacy && !concrete && !wood && !steel && !road ) {
        return {};
    }
    // The UndeadPeople t_fence is a simple low white picket fence, not a
    // wrought-iron railing.  Leave space between the individual uprights.
    const float height = privacy ? 2.16f : picket ? 1.16f :
                         concrete ? 1.06f : road ? .92f : 1.11f;
    const float thickness = concrete ? .14f : road ? .11f : picket ? .073f : .095f;
    std::vector<part> out;
    out.reserve( privacy || picket ? 36 : 14 );
    // Each tile contributes one central post and half-rails connecting to
    // neighbours. A standalone segment is a full E-W tile, not one point.
    out.push_back( { 0, 0, thickness + .09f, thickness + .09f, 0, height } );
    out.push_back( { 0, 0, thickness + .15f, thickness + .15f, height - .08f, height } );
    auto links = neighbours;
    if( std::none_of( links.begin(), links.end(), []( bool linked ) { return linked; } ) ) {
        links[1] = links[3] = true;
    }
    for( unsigned direction = 0; direction < links.size(); ++direction ) {
        if( !links[direction] ) {
            continue;
        }
        const bool east_west = direction % 2 == 1;
        const float sign = direction == 1 || direction == 2 ? 1.f : -1.f;
        const float x = east_west ? sign * .25f : 0.f;
        const float z = east_west ? 0.f : sign * .25f;
        const float along_x = east_west ? .50f : thickness;
        const float along_z = east_west ? thickness : .50f;
        if( privacy ) {
            // Full-height planks and their horizontal backing strips.
            for( int n = 0; n < 4; ++n ) {
                const float at = sign * ( .075f + n * .115f );
                out.push_back( { east_west ? at : 0.f, east_west ? 0.f : at,
                                 east_west ? .110f : .085f,
                                 east_west ? .085f : .110f,
                                 .04f, height - .03f } );
            }
            out.push_back( { x, z, along_x, along_z, .46f, .56f } );
            out.push_back( { x, z, along_x, along_z, 1.55f, 1.65f } );
        } else if( picket ) {
            // Two slender uprights per half-tile, painted cream-white as in
            // the UndeadPeople artwork.  No painted-on black shadow stripes.
            for( int n = 0; n < 2; ++n ) {
                const float at = sign * ( .17f + n * .20f );
                const float px = east_west ? at : 0.f;
                const float pz = east_west ? 0.f : at;
                out.push_back( { px, pz, .062f, .062f, .065f, height - .07f } );
                out.push_back( { px, pz, .048f, .048f, height - .07f, height } );
            }
            out.push_back( { x, z, along_x, along_z, .35f, .42f } );
            out.push_back( { x, z, along_x, along_z, .80f, .88f } );
        } else if( concrete ) {
            // Raised cement rail and a second lower brace; never a flat road
            // marking. Central posts occupy each original game tile.
            out.push_back( { x, z, along_x, along_z, .40f, .56f } );
            out.push_back( { x, z, along_x, along_z, .79f, .97f } );
        } else if( road ) {
            out.push_back( { x, z, along_x, along_z, .56f, .76f } );
        } else {
            // Wooden or wrought-metal railings are spaced open with upper
            // and lower bars instead of a solid billboard.
            out.push_back( { x, z, along_x, along_z, .31f, .40f } );
            out.push_back( { x, z, along_x, along_z, .81f, .91f } );
        }
    }
    return out;
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

prepare_timing prepare_view( avatar &you, map &here )
{
    prepare_timing timing;
    const auto p = you.pos_bub( here );
    if( !here.inbounds( p ) ) {
        return timing;
    }
    const auto milliseconds = []( const std::chrono::steady_clock::time_point &from,
                                  const std::chrono::steady_clock::time_point &to ) {
        return std::chrono::duration<double, std::milli>( to - from ).count();
    };
    const auto step_started = std::chrono::steady_clock::now();
    here.build_map_cache( p.z() );
    const auto player_layer_finished = std::chrono::steady_clock::now();
    timing.player_layer_ms = milliseconds( step_started, player_layer_finished );
    if( here.supports_zlevels() ) {
        // build_map_cache() already sweeps every loaded z-level's outside /
        // transparency / floor / vision / vehicle caches and its
        // build_seen_cache() fills the LOS of all levels at once, so calling it
        // again for a storey above the player only repeated that sweep, and
        // those storeys get no lightmap of their own either (they are only
        // needed for LOS, geometry and light-independent 3D presentation).
        // Only the storeys below the player still need a call, because
        // update_visibility_cache() and the native light rules read their
        // lightmaps.
        const int lowest = std::max( -OVERMAP_DEPTH, p.z() - below_for_storey( p.z() ) );
        for( int z = lowest; z < p.z(); ++z ) {
            here.build_map_cache( z );
            ++timing.extra_layer_calls;
        }
    }
    const auto extra_layers_finished = std::chrono::steady_clock::now();
    timing.extra_layers_ms = milliseconds( player_layer_finished, extra_layers_finished );
    // The 3D path skips the native 2D map draw, so nothing under it reads the
    // visibility cache of the storeys below the player - only the player's own
    // level is consumed (HUD/minimap, aiming and the snapshot freshness check).
    // Native callers keep the recursive default; a switch back to 2D recomputes
    // the lower storeys through handle_action()/map::draw().
    here.update_visibility_cache( p.z(), false );
    const auto visibility_finished = std::chrono::steady_clock::now();
    timing.visibility_ms = milliseconds( extra_layers_finished, visibility_finished );
    you.prepare_map_memory_region(
        here.get_abs( p - tripoint_rel_ms( radius, radius, 0 ) ),
        here.get_abs( p + tripoint_rel_ms( radius, radius, 0 ) ) );
    const auto memory_finished = std::chrono::steady_clock::now();
    timing.memory_ms = milliseconds( visibility_finished, memory_finished );
    timing.total_ms = milliseconds( step_started, memory_finished );
    fp3d_regression::prepare_costs( timing.total_ms, timing.player_layer_ms,
                                    timing.extra_layers_ms, timing.visibility_ms,
                                    timing.extra_layer_calls );
    return timing;
}

// The capture walks 121x121 cells per storey and stays inside one submap for
// 12 consecutive cells, so resolve the memorized submap once per submap rather
// than once per cell. The per-cell lookup (submap hash plus the timed-event
// check inside should_show_map_memory) was the dominant cost of the cell loop.
class memorized_reader
{
    public:
        explicit memorized_reader( const avatar &you ) : you_( you ) {}
        const memorized_tile &at( const tripoint_abs_ms &abs )
        {
            tripoint_abs_sm sm;
            point_sm_ms loc;
            std::tie( sm, loc ) = coords::project_remain<coords::sm>( abs );
            if( !resolved_ || sm != cached_sm_ ) {
                resolved_ = true;
                cached_sm_ = sm;
                cached_ = you_.get_memorized_submap( sm );
            }
            return cached_ ? cached_->get_tile( loc ) : mm_submap::default_tile;
        }
    private:
        const avatar &you_;
        tripoint_abs_sm cached_sm_;
        const mm_submap *cached_ = nullptr;
        bool resolved_ = false;
};

static scene_snapshot capture_impl( avatar &you, const map &here, int above, int below,
                                    bool cut_overhead, bool camera_geometry, scene_snapshot *previous = nullptr )
{    const auto started = std::chrono::steady_clock::now();
    // Equipment and body state cannot change during this synchronous capture.
    const bool blind = you.is_blind();
    camera_geometry = camera_geometry && !blind;
    scene_snapshot result;
    result.origin = you.pos_abs();
    result.player_crouching = you.is_crouching();
    // The player's discrete map cell is for CDDA collision/interactions,
    // not necessarily the centre of a rotating 3D passenger seat. Publish
    // the exact native mount transformed by the same rigid vehicle root
    // used for drawing; only an actually boarded player receives this pose.
    if( you.in_vehicle ) {
        const optional_vpart_position vp = here.veh_at( you.pos_bub( here ) );
        if( vp && vp->part_with_feature( VPFLAG_BOARDABLE, true ) ) {
            const vehicle &car = vp->vehicle();
            const point_rel_ms mount = vp->mount_pos();
            const float degrees = units::to_degrees( car.pivot_rotation[0] );
            const double radians = degrees * 3.14159265358979323846 / 180.0;
            const double cx = mount.x() - car.pivot_anchor[0].x();
            const double cy = mount.y() - car.pivot_anchor[0].y();
            const bool seated = bool( vp->part_with_feature( "SEAT", true ) );
            fp3d_camera::rider_pose rider;
            rider.world_x = car.pos_abs().x() + std::cos( radians ) * cx -
                            std::sin( radians ) * cy;
            rider.world_y = car.pos_abs().y() + std::sin( radians ) * cx +
                            std::cos( radians ) * cy;
            rider.eye = seated ? 1.42f : 1.55f;
            rider.vehicle_angle = degrees;
            rider.mount_x = mount.x();
            rider.mount_y = mount.y();
            rider.storey = car.pos_abs().z();
            rider.driver = car.player_in_control( here, you );
            result.rider = rider;
        }
    }
    result.hud = fp3d_hud::capture( you );
    result.season = season_of_year( calendar::turn );
    if( !blind ) {
        if( const auto wielded = you.get_wielded_item() ) {
            result.held_item = describe_item( *wielded, result.origin );
            result.held_item->kind = sprite_kind::held_item;
        }
    }    const tripoint_bub_ms player = you.pos_bub( here );
    if( !here.inbounds( player ) ) {
        return result;
    }
    result.sky = fp3d_sky::sample( calendar::turn,
                                   player.z() >= 0 && here.is_outside( player ) && !blind );
    const auto &weather = get_weather_const();
    if( !blind && player.z() >= 0 && weather.weather_id.is_valid() ) {
        result.weather = fp3d_weather::sample( *weather.weather_id, weather.windspeed,
                                              weather.winddirection, weather.lightning_active, here.is_outside( player ) );
    }
    const visibility_variables &variables = here.get_visibility_variables_cache();
    const bool fresh = variables.variables_set && !variables.visibility_cache_dirty &&
                       variables.last_pos == player;
    // Reuse the same native emission aggregate consumed by CDDA's lightmap.
    // Off phones/flashlights do not have LIGHT_* and produce no active_light().
    // A carried but non-wielded phone is included by Character::active_light().
    if( fresh && !blind ) {
        const float held_light = you.active_light();
        if( held_light >= 2.0f ) {
            const bool phone = you.has_active_item( itype_id( "smart_phone_flashlight" ) ) ||
                               you.has_active_item( itype_id( "smart_phone_locked_flashlight" ) ) ||
                               you.has_active_item( itype_id( "cell_phone_flashlight" ) );
            const bool flashlight = you.has_active_item( itype_id( "flashlight_on" ) );
            result.lights.push_back( { result.origin,
                                     phone || flashlight ? light_kind::beam : light_kind::portable,
                                     held_light, 1.35f,
                                     phone ? "native:phone_active_light" :
                                     flashlight ? "native:flashlight_active_light" :
                                                  "native:player_active_light" } );
        }
    }
    // The camera sees z-levels in world space, independent of 2D mode's
    // four-up/two-down legacy snapshot. Extend to the native 3D LOS horizon,
    // preserving the street below tall structures. Optional callers can
    // still narrow either direction for isolated tests/performance probes.
    const int below_budget = std::clamp( below, 0,
                                        std::min( below_for_storey( player.z() ), fov_3d_z_range ) );
    const int above_budget = std::clamp( above, 0,
                                        std::min( render_above, fov_3d_z_range ) );
    result.min_z = here.supports_zlevels() ?
                   std::max( -OVERMAP_DEPTH, player.z() - below_budget ) : player.z();
    result.max_z = here.supports_zlevels() ?
                   std::min( OVERMAP_HEIGHT, player.z() + above_budget ) : player.z();
    // Query native perception once per cell, including a one-cell halo for
    // exposed-surface neighbours. The raw 3D seen/camera cache can reject
    // obstructed cells cheaply; it is not the 2D viewport visibility mask.
    // Use the loaded map's stable boundary for camera geometry. A one-cell
    // sliding crop changed edge chunks every step and recompiled their lists
    // even on unchanged ground. Native perception retains its original range.
    const int side = camera_geometry ? here.getmapsize() * SEEX + 2 : radius * 2 + 3;
    constexpr unsigned char visible_bit = 1;
    constexpr unsigned char air_bit = 2;
    const int first_x = camera_geometry ? -1 : player.x() - radius - 1;
    const int first_y = camera_geometry ? -1 : player.y() - radius - 1;
    std::vector<unsigned char> perception( ( result.max_z - result.min_z + 1 ) * side * side );
    const auto index = [&]( const tripoint_bub_ms & p ) {
        return ( ( p.z() - result.min_z ) * side + p.y() - first_y ) * side + p.x() - first_x;
    };
    const auto perception_started = std::chrono::steady_clock::now();
    const int sight_range = you.unimpaired_range();
    const bool aiming = you.recoil < MAX_RECOIL;
    if( fresh && !blind ) {
        const int clairvoyance = you.clairvoyance();
        for( int z = result.min_z; z <= result.max_z; ++z ) {
            const auto &cache = here.access_cache( z );
            for( int y = first_y; y < first_y + side; ++y ) {
                for( int x = first_x; x < first_x + side; ++x ) {
                    const tripoint_bub_ms p( x, y, z );
                    if( !here.inbounds( p ) ) {
                        continue;
                    }
                    if( camera_geometry && ( std::abs( x - player.x() ) > radius ||
                                             std::abs( y - player.y() ) > radius ) ) {
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
                    // Match apparent_light_helper's obstruction and aiming
                    // gates without calculating unused light quadrants for
                    // every opaque tile. 3D presentation is light-independent.
                    const float visibility = distance > sight_range ? cache.camera_cache[x][y] :
                                             std::max( cache.seen_cache[x][y], cache.camera_cache[x][y] );
                    if( clairvoyant || ( visibility > LIGHT_TRANSPARENCY_SOLID + .1f &&
                                         ( !aiming || !you.cant_see( p ) ) ) ) {
                        perception[index( p )] = visible_bit | ( here.is_open_air( p ) ? air_bit : 0 );
                    }
                }
            }
        }
    }
    const auto perception_finished = std::chrono::steady_clock::now();
    // P3: the interior region the avatar actually stands in. This is the
    // walkable indoor cluster closed by its own walls, so the overhead view can
    // lift exactly that region's roof instead of cutting every building on
    // screen or deleting geometry by z alone. Only native indoor/passability
    // data at the avatar's own storey is read; nothing here widens perception.
    std::vector<unsigned char> avatar_region;
    const auto region_index = [&]( const int x, const int y ) {
        return ( player.z() - result.min_z ) * side * side + ( y - first_y ) * side + ( x - first_x );
    };
    if( ( fresh || camera_geometry ) && !blind && !here.is_outside( player ) ) {
        avatar_region.assign( perception.size(), 0 );
        std::vector<tripoint_bub_ms> queue;
        queue.reserve( 256 );
        const auto in_grid = [&]( const tripoint_bub_ms &q ) {
            return q.x() >= first_x && q.x() < first_x + side &&
                   q.y() >= first_y && q.y() < first_y + side;
        };
        avatar_region[region_index( player.x(), player.y() )] = 1;
        queue.push_back( player );
        for( size_t head = 0; head < queue.size(); ++head ) {
            const tripoint_bub_ms from = queue[head];
            for( const tripoint_rel_ms &dir : { tripoint_rel_ms::north, tripoint_rel_ms::east,
                     tripoint_rel_ms::south, tripoint_rel_ms::west } ) {
                const tripoint_bub_ms next = from + dir;
                if( !in_grid( next ) || !here.inbounds( next ) ) {
                    continue;
                }
                if( avatar_region[region_index( next.x(), next.y() )] ) {
                    continue;
                }
                // A separate room of the same building keeps its own roof:
                // walls, closed doors and impassable furniture stop the flood.
                if( here.is_outside( next ) || !here.passable( next ) ) {
                    continue;
                }
                avatar_region[region_index( next.x(), next.y() )] = 1;
                queue.push_back( next );
            }
        }
        // Include the enclosing walls so the removed roof does not leave a
        // floating ring of wall tops behind. One step only: never a second
        // flood that could reach a neighbouring room through a wall.
        for( const tripoint_bub_ms &interior : queue ) {
            for( int dy = -1; dy <= 1; ++dy ) {
                for( int dx = -1; dx <= 1; ++dx ) {
                    const tripoint_bub_ms next( interior.x() + dx, interior.y() + dy, interior.z() );
                    if( !in_grid( next ) || !here.inbounds( next ) ||
                        avatar_region[region_index( next.x(), next.y() )] ) {
                        continue;
                    }
                    if( !here.is_outside( next ) ) {
                        avatar_region[region_index( next.x(), next.y() )] = 1;
                    }
                }
            }
        }
    }
    const auto over_avatar_region = [&]( const tripoint_bub_ms &p ) {
        if( avatar_region.empty() || p.z() < player.z() || p.z() > result.max_z ||
            p.x() < first_x || p.x() >= first_x + side ||
            p.y() < first_y || p.y() >= first_y + side ) {
            return false;
        }
        return avatar_region[region_index( p.x(), p.y() )] != 0;
    };
    // The avatar's own tile sprite is always published so the overhead drawer
    // can place it on its real tile; the first-person drawer never draws it.
    // P0-V / P1-V: cover every map-loaded vehicle, not only the car being
    // driven.  VehicleList may include duplicates across submaps/z-levels;
    // de-duplicate by pointer ONLY within this capture, never store that
    // pointer or a vehicle_part reference in the published snapshot.
    // The get_vehicles() API is non-const in CDDA 0.I because it lazily
    // walks map caches. This call does not modify vehicle simulation state.
    if( ( fresh || camera_geometry ) && !blind ) {
        std::unordered_set<const vehicle *> visited;
        for( const wrapped_vehicle &entry : const_cast<map &>( here ).get_vehicles() ) {
            const vehicle *veh = entry.v;
            if( !veh || !visited.insert( veh ).second ) {
                continue;
            }
            ++result.enumerated_vehicles;
            fp3d_vehicle::vehicle_view view;
            // Keep candidates private until the powered grid is checked.
            std::vector<emitted_light> powered_fixtures;
            view.origin = veh->pos_abs();
            view.name = veh->name;
            view.angle_degrees = units::to_degrees( veh->pivot_rotation[0] );
            view.pivot_x = veh->pivot_anchor[0].x();
            view.pivot_y = veh->pivot_anchor[0].y();
            // A complete car must be assembled from a complete local
            // geometry snapshot. Visibility can reject the far-side
            // fender *because the car itself blocks the 2D LOS*, leaving
            // a scattered chair/glass scaffold on the 3D side. Gate at
            // the entity level instead. Native/reference capture requires a
            // visible mount; the camera path requires a mount in its loaded
            // grid. GL depth hides parts behind buildings and obstacles.
            bool any_part_visible = false;
            for( int index_part = 0; index_part < veh->part_count(); ++index_part ) {
                const vehicle_part &part = veh->part( index_part );
                if( part.removed || part.is_fake || part.hidden ) {
                    continue;
                }
                ++view.real_parts;
                // Native rounded occupancy is used for the vehicle's
                // visibility gate only. Mesh geometry stays local to the
                // unrounded mount, and includes *all* present components
                // once any native vehicle mount is visible. The GL depth
                // buffer occludes far-side panels behind real walls.
                const tripoint_bub_ms p = here.get_bub( veh->mount_to_tripoint_abs( part.mount ) );
                if( p.z() >= result.min_z && p.z() <= result.max_z &&
                    p.x() >= first_x && p.x() < first_x + side &&
                    p.y() >= first_y && p.y() < first_y + side &&
                    here.inbounds( p ) && ( camera_geometry || ( perception[index( p )] & visible_bit ) ) ) {
                    any_part_visible = true;
                }
                const vpart_info &info = part.info();
                // Native lightmap uses vehicle::lights(): enabled, unbroken,
                // available light parts only. Also require actual connected
                // battery energy below before publishing a powered fixture.
                const auto native_light_tile =
                    here.get_bub( veh->mount_to_tripoint_abs( part.mount ) );
                const bool light_visible = native_light_tile.z() >= result.min_z &&
                                           native_light_tile.z() <= result.max_z &&
                                           native_light_tile.x() >= first_x &&
                                           native_light_tile.x() < first_x + side &&
                                           native_light_tile.y() >= first_y &&
                                           native_light_tile.y() < first_y + side &&
                                           here.inbounds( native_light_tile ) &&
                                           ( camera_geometry || ( perception[index( native_light_tile )] & visible_bit ) );
                if( light_visible && part.enabled && part.is_available() &&
                    part.is_light() && info.bonus > 0 ) {
                    const bool fixture = info.has_flag( VPFLAG_CIRCLE_LIGHT ) ||
                                         info.has_flag( VPFLAG_HALF_CIRCLE_LIGHT );
                    const float height = fixture ? 2.2f : 1.0f;
                    powered_fixtures.push_back( { veh->mount_to_tripoint_abs( part.mount ),
                                                  light_kind::fixture,
                                                  static_cast<float>( info.bonus ), height,
                                                  info.id.str() } );
                }
                fp3d_vehicle::part_view item;
                item.index = index_part;
                item.mount_x = part.mount.x();
                item.mount_y = part.mount.y();
                item.native_position = veh->mount_to_tripoint_abs( part.mount );
                item.id = info.id.str();
                item.variant = part.variant;
                item.looks_like = info.looks_like;
                item.broken = part.is_broken();
                item.open = part.open;
                item.glazed_door = info.has_flag( VPFLAG_WINDOW );
                item.health = static_cast<float>( part.health_percent() );
                using role = fp3d_vehicle::role;
                const auto name_has = [&]( const char *needle ) {
                    return item.id.find( needle ) != std::string::npos;
                };
                // A CDDA car DOOR also has WINDOW and BOARDABLE. The old
                // priority built giant glass boxes instead of red car doors.
                // Likewise, a CARGO/BOARDABLE trunk is *not* a seat!
                if( info.has_flag( VPFLAG_WHEEL ) ) {
                    item.kind = role::wheel;
                } else if( info.has_flag( VPFLAG_ROOF ) ) {
                    item.kind = role::roof;
                } else if( info.has_flag( "DOOR" ) || info.has_flag( VPFLAG_OPENABLE ) ) {
                    // Opening tailgates and hatches are still movable doors.
                    item.kind = role::door;
                } else if( name_has( "trunk" ) || name_has( "halfboard" ) ||
                           ( name_has( "board" ) && !name_has( "dashboard" ) ) || name_has( "hatch" ) ||
                           name_has( "hood" ) || name_has( "fender" ) ||
                           name_has( "quarterpanel" ) || name_has( "bumper" ) ||
                           name_has( "ram_" ) ) {
                    item.kind = role::body;
                } else if( info.has_flag( VPFLAG_WINDOW ) ) {
                    item.kind = role::window;
                } else if( info.has_flag( VPFLAG_ARMOR ) ) {
                    item.kind = role::armor;
                } else if( info.has_flag( "SEAT" ) ) {
                    item.kind = role::seat;
                } else if( info.has_flag( VPFLAG_ENGINE ) ) {
                    item.kind = role::engine;
                } else if( info.has_flag( "CONE_LIGHT" ) ||
                           name_has( "headlight" ) || name_has( "taillight" ) ||
                           name_has( "brakelight" ) || name_has( "turn_signal" ) ) {
                    item.kind = role::light;
                } else if( info.has_flag( VPFLAG_CARGO ) ) {
                    item.kind = role::cargo;
                } else if( info.has_flag( "STRUCTURE" ) ||
                           info.location == "structure" ) {
                    item.kind = role::frame;
                } else {
                    item.kind = role::other;
                }
                view.parts.push_back( std::move( item ) );
            }
            if( any_part_visible && !view.parts.empty() ) {
                // CDDA's energized distribution network includes vehicles
                // connected through power cables. No charge = no glowing
                // fixture, even if a switch was left in its ON position.
                if( !powered_fixtures.empty() &&
                    veh->connected_battery_power_level( here ).first > 0 ) {
                    for( auto &light : powered_fixtures ) {
                        result.lights.push_back( std::move( light ) );
                    }
                }
                result.visible_vehicle_parts += view.parts.size();
                result.vehicles.push_back( std::move( view ) );
            }
        }
    }
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
    // Treat the structural floor under an outdoor railing as metal when it
    // actually touches a metal walkway. Determining this from real terrain
    // once, at snapshot time, avoids material changes as the upper-level
    // visibility cache flickers while the camera rotates.
    const auto metal_rail = [&]( const tripoint_bub_ms &p ) {
        if( !here.inbounds( p ) || here.ter( p ).id().str() != "t_railing" ) {
            return false;
        }
        for( const auto &dir : { tripoint_rel_ms::north, tripoint_rel_ms::south,
                tripoint_rel_ms::east, tripoint_rel_ms::west } ) {
            const auto n = p + dir;
            if( !here.inbounds( n ) ) {
                continue;
            }
            const std::string id = here.ter( n ).id().str();
            if( id == "t_metal_floor_no_roof" || id == "t_floor_metal_gangway" ) {
                return true;
            }
        }
        return false;
    };
    // Many cells share the same furniture definition (including f_null).
    // Resolve its model/looks_like chain once per capture, so edits, mod
    // reloads and newly connected appliances cannot leave a stale model.
    std::unordered_map<std::string, std::optional<fp3d_furniture::model>> furniture_models;
    const auto furniture_model = [&]( const std::string &id ) -> const std::optional<fp3d_furniture::model> & {
        const auto entry = furniture_models.try_emplace( id );
        if( entry.second ) { entry.first->second = fp3d_furniture::describe( id ); }
        return entry.first->second;
    };
    // The independent path contains several full storeys, rather than the
    // sparse native-visible cells. Avoid repeatedly moving the large cell
    // vector through doubling allocations on every step.
    bool retaining = camera_geometry && previous && previous->geometry_generation &&
                     previous->vehicles.empty() && result.vehicles.empty();
    size_t retained_cells = 0;
    if( !retaining ) { result.cells.reserve( camera_geometry ? ( side - 2 ) * ( side - 2 ) *
                          std::min( 4, result.max_z - result.min_z + 1 ) :
                          ( radius * 2 + 1 ) * ( radius * 2 + 1 ) ); }
    // Definition-derived presentation is identical at every square with this
    // terrain ID. Keep this cache capture-local so data reloads cannot make
    // it stale, and continue reading dynamic fields/furniture per square.
    struct terrain_presentation {
        terrain_kind kind;
        float vegetation_height;
    };
    std::unordered_map<std::string, terrain_presentation> terrain_presentations;
    std::unordered_map<std::string, float> plant_heights;
    const auto cached_plant_height = [&]( const std::string &id ) {
        const auto entry = plant_heights.try_emplace( id, 0.f );
        if( entry.second ) {
            entry.first->second = plant_height( id );
        }
        return entry.first->second;
    };
    memorized_reader memorized( you );
    // Definition templates live for one capture only. All map contents and
    // fields are still read this turn; no door/edit invalidation is required.
    std::unordered_map<std::uint64_t, cell> camera_templates;
    for( int z = result.min_z; z <= result.max_z; ++z ) {
        const auto &cache = here.access_cache( z );
        for( int y = first_y + 1; y < first_y + side - 1; ++y ) {
            const int dy = y - player.y();
            for( int x = first_x + 1; x < first_x + side - 1; ++x ) {
                const int dx = x - player.x();
                const tripoint_bub_ms p = player + tripoint_rel_ms( dx, dy, z - player.z() );
                if( !here.inbounds( p ) ) {
                    continue;
                }
                const bool region_column = over_avatar_region( p );
                // P3: the overhead camera looks down through the avatar's own
                // roof. Dropping that geometry IS the cut: this path never adds
                // a cell, never reads an unseen storey and never reveals an
                // interior that the avatar's own perception did not produce.
                if( cut_overhead && region_column && p.z() > player.z() ) {
                    continue;
                }
                // Native perception still gates actors and nearby actions.
                // Live 3D geometry instead uses the complete loaded grid;
                // camera culling and depth resolve its visible surfaces.
                const bool native_visible = perception[index( p )] & visible_bit;
                const bool visible = camera_geometry || native_visible;
                if( camera_geometry ) {
                    const ter_id terrain = here.ter( p );
                    const ter_t &definition = terrain.obj();
                    if( native_visible ) {
                        const auto creature = creatures.find( p );
                        capture_sprites( result, you, here, p,
                                         creature == creatures.end() ? nullptr : creature->second );
                        if( p.z() == player.z() && square_dist( p, player ) <= 1 ) {
                            const fp3d_targeting::target target{ you.pos_abs(), here.get_abs( p ),
                                fp3d_targeting::kind::terrain, terrain.id().str() };
                            const auto choices = fp3d_interaction::context_entries( target, you, here );
                            if( std::any_of( choices.begin(), choices.end(), []( const auto &choice ) {
                                return choice.visible && choice.enabled && ( choice.action != ACTION_NULL || choice.command );
                            } ) ) { result.interactable.push_back( target.position ); }
                        }
                    }
                    // Native is_open_air() is exactly this terrain flag. Test
                    // it before allocating cell strings for empty storeys.
                    if( definition.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) || terrain == ter_id() ) {
                        continue;
                    }
                    const furn_id furniture = here.furn( p );
                    const furn_t &furn_definition = furniture.obj();
                    const std::uint64_t key = static_cast<std::uint64_t>( terrain.to_i() ) << 32 |
                                              static_cast<unsigned>( furniture.to_i() );
                    auto entry = camera_templates.try_emplace( key );
                    if( entry.second ) {
                        cell &base = entry.first->second;
                        base.terrain = terrain.id().str();
                        base.kind = classify( base.terrain );
                        base.floor_present = !definition.has_flag( ter_furn_flag::TFLAG_NO_FLOOR_WATER ) &&
                                             !furn_definition.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) &&
                                             !furn_definition.has_flag( ter_furn_flag::TFLAG_NO_FLOOR_WATER );
                        base.goes_up = definition.has_flag( ter_furn_flag::TFLAG_GOES_UP );
                        base.goes_down = definition.has_flag( ter_furn_flag::TFLAG_GOES_DOWN );
                        base.is_ramp = definition.has_flag( ter_furn_flag::TFLAG_RAMP ) ||
                                       definition.has_flag( ter_furn_flag::TFLAG_RAMP_UP ) ||
                                       definition.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN );
                        base.ramp_delta = !base.is_ramp ? 0 :
                                          definition.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN ) ? -1 : 1;
                        if( base.kind == terrain_kind::window ) {
                            base.pane = !definition.has_flag( ter_furn_flag::TFLAG_TRANSPARENT ) ?
                                        window_pane::covered : definition.movecost > 0 ?
                                        window_pane::opening : window_pane::glazed;
                        }
                        base.vegetation_height = plant_height( base.terrain );
                        if( base.vegetation_height > 0 ) {
                            base.vegetation = base.terrain;
                            base.kind = terrain_kind::floor;
                        }
                        const std::string &furn = furniture.id().str();
                        if( const float height = plant_height( furn ); height > 0 ) {
                            base.vegetation = furn;
                            base.vegetation_height = height;
                        }
                        base.furniture = furniture_model( furn );
                    }
                    const cell &base = entry.first->second;
                    const auto absolute = here.get_abs( p );
                    const bool floor = !here.supports_zlevels() || z < -OVERMAP_DEPTH + 1 || base.floor_present;
                    const bool metal_railing = base.terrain == "t_railing" && floor && metal_rail( p );
                    const int native_lum = std::max( definition.light_emitted, furn_definition.light_emitted );
                    if( native_lum > 0 ) {
                        result.lights.push_back( { absolute, light_kind::fixture,
                            static_cast<float>( native_lum ), furn_definition.light_emitted > 0 ? 1.6f : .4f,
                            furn_definition.light_emitted > 0 ? furniture.id().str() : base.terrain } );
                    }
                    std::vector<ground_field> ground_fields;
                    for( const auto &field : here.field_at( p ) ) {
                        const int lum = field.second.get_intensity_level().light_emitted;
                        const std::string &id = field.first.id().str();
                        if( lum > 0 ) {
                            result.lights.push_back( { absolute, light_kind::flame,
                                static_cast<float>( lum ), .65f, id } );
                        }
                        const auto &type = field.first.obj();
                        if( type.display_field && field.second.is_field_alive() &&
                            ( type.phase == phase_id::LIQUID || id == "fd_web" || id == "fd_web_nostick" ||
                              id == "fd_void_web" || id == "fd_churned_earth" || id == "fd_rubble" ) ) {
                            ground_fields.push_back( { id, field.second.get_field_intensity() } );
                        }
                    }
                    std::stable_sort( ground_fields.begin(), ground_fields.end(),
                    []( const ground_field &left, const ground_field &right ) {
                        const auto &a = field_type_str_id( left.id ).obj();
                        const auto &b = field_type_str_id( right.id ).obj();
                        return a.is_dangerous() != b.is_dangerous() ? !a.is_dangerous() : a.priority < b.priority;
                    } );
                    if( z == player.z() && !result.sky.visible && player.z() >= 0 && here.is_outside( p ) ) {
                        result.sky = fp3d_sky::sample( calendar::turn, true );
                    }
                    if( result.weather.particles != fp3d_weather::precipitation::none && floor &&
                        base.kind == terrain_kind::floor && dx * dx + dy * dy <= 144 && here.is_outside( p ) ) {
                        result.weather.columns.push_back( absolute );
                    }
                    ++result.visible;
                    if( retaining ) {
                        const cell *old = retained_cells < previous->cells.size() ? &previous->cells[retained_cells] : nullptr;
                        const bool same = old && old->position == absolute && old->terrain == base.terrain &&
                            old->kind == base.kind && old->known == knowledge::visible && !old->exterior_only &&
                            old->vegetation == base.vegetation && old->vegetation_height == base.vegetation_height &&
                            old->pane == base.pane && old->floor_present == floor && !old->ceiling_present &&
                            !old->open_air && old->metal_deck_railing == metal_railing &&
                            old->over_avatar_region == region_column && old->appliance.empty() &&
                            old->goes_up == base.goes_up && old->goes_down == base.goes_down &&
                            old->is_ramp == base.is_ramp && old->ramp_delta == base.ramp_delta &&
                            bool( old->furniture ) == bool( base.furniture ) &&
                            ( !base.furniture || ( old->furniture->id == base.furniture->id &&
                                                  old->furniture->shape == base.furniture->shape ) ) &&
                            old->ground_fields.size() == ground_fields.size() &&
                            std::equal( ground_fields.begin(), ground_fields.end(), old->ground_fields.begin(),
                            []( const ground_field &a, const ground_field &b ) { return a.id == b.id && a.intensity == b.intensity; } );
                        if( same ) { ++retained_cells; continue; }
                        result.cells.assign( previous->cells.begin(), previous->cells.begin() + retained_cells );
                        retaining = false;
                    }
                    cell tile = base;
                    tile.position = absolute;
                    tile.floor_present = floor;
                    tile.over_avatar_region = region_column;
                    tile.metal_deck_railing = metal_railing;
                    tile.ground_fields = std::move( ground_fields );
                    if( !tile.furniture && cache.get_veh_exists_at( p ) ) {
                        const auto part = here.veh_at( p ).part_with_feature( VPFLAG_APPLIANCE, false );
                        if( part ) {
                            tile.furniture = furniture_model( part->info().looks_like );
                            if( tile.furniture ) { tile.appliance = part->info().id.str(); }
                        }
                    }
                    result.cells.push_back( std::move( tile ) );
                    continue;
                }
                if( visible && p.z() == player.z() && !result.sky.visible && player.z() >= 0 && !blind &&
                    here.is_outside( p ) ) {
                    // Seen outdoors through a window/door also needs a sky
                    // backdrop. Known room ceilings/walls still occlude it.
                    result.sky = fp3d_sky::sample( calendar::turn, true );
                }
                // Adjacent air is only used by unseen upper facade fallbacks.
                // Visible/current/lower cells need none of these neighbour reads.
                bool beside_visible_air = false;
                if( fresh && !visible && !blind && p.z() > player.z() ) {
                    beside_visible_air =
                        ( perception[index( p + tripoint_rel_ms::north )] & air_bit ) ||
                        ( perception[index( p + tripoint_rel_ms::south )] & air_bit ) ||
                        ( perception[index( p + tripoint_rel_ms::east )] & air_bit ) ||
                        ( perception[index( p + tripoint_rel_ms::west )] & air_bit );
                    if( !beside_visible_air ) {
                        for( int dz : { -1, 1 } ) {
                            const auto neighbour = p + tripoint_rel_ms( 0, 0, dz );
                            if( neighbour.z() >= result.min_z && neighbour.z() <= result.max_z &&
                                ( perception[index( neighbour )] & air_bit ) &&
                                here.sees( player, p, sight_range ) ) {
                                beside_visible_air = true;
                                break;
                            }
                        }
                    }
                }
                // Geometry in a dark room must still occlude the exterior.
                // Native geometric LOS gates surfaces; contents retain full perception checks.
                const bool room_surface = fresh && !visible && !blind &&
                                          cache.seen_cache[p.x()][p.y()] > LIGHT_TRANSPARENCY_SOLID + .1f &&
                                          !here.is_outside( p ) &&
                                          square_dist( player, p ) <= sight_range && here.sees( player, p, sight_range );
                // An outdoor radio tower is open steelwork, metal walkway
                // and railings across several z-levels. Native 3D endpoint
                // visibility often drops the upper terrain while the adjacent
                // exterior air is clearly visible from underneath. Keep the
                // REAL material/model for these narrowly identified exterior
                // surfaces, never upper floor contents or other indoor IDs.
                const bool tower_exterior_surface = fresh && !visible && !blind &&
                    p.z() > player.z() &&
                    // The underside of a first-level metal deck is visible
                    // from the ground even when that ground is a paved tile,
                    // not an open-air tile. Native-visible lower terrain is
                    // enough for these exterior-only structural components.
                    ( beside_visible_air ||
                      ( perception[index( p + tripoint_rel_ms::below )] & visible_bit ) ) &&
                    ( [&] {
                        const std::string id = here.ter( p ).id().str();
                        return id == "t_radio_tower" || id == "t_railing" ||
                               id == "t_metal_floor_no_roof" ||
                               id == "t_floor_metal_gangway" ||
                               id == "t_ladder_up_down" ||
                               id == "t_ladder_up" || id == "t_ladder_down";
                    } )() && square_dist( player, p ) <= sight_range && !you.cant_see( p );
                // Native 3D shadowcasting can see the air immediately
                // beneath an overhead platform while dropping the solid
                // platform endpoint. Show only that neutral underside:
                // never disclose the upstairs terrain ID, furniture,
                // contents or layout when the upper cell itself is unseen.
                // A completely enclosed upper room has no visible air below,
                // so this cannot turn its hidden floor into a visible room.
                const bool upper_floor_boundary = fresh && !visible && !blind &&
                    p.z() > player.z() &&
                    ( perception[index( p + tripoint_rel_ms::below )] & air_bit ) &&
                    !you.cant_see( p ) &&
                    square_dist( player, p ) <= sight_range &&
                    here.has_floor( p );
                // Do not turn unseen upper radio-tower metal, railings or
                // open lattice into a fake brown t_floor. Native shadowcast
                // may omit the endpoint while retaining the air below it.
                // Only this small whitelist of external structural geometry
                // is revealed; furniture, interior floors and contents stay
                // behind the normal visibility gate.
                const bool tower_upper_boundary = upper_floor_boundary && ( [&] {
                    const std::string id = here.ter( p ).id().str();
                    return id == "t_radio_tower" || id == "t_railing" ||
                           id == "t_metal_floor_no_roof" ||
                           id == "t_floor_metal_gangway" ||
                           id == "t_ladder_up_down" || id == "t_ladder_up" ||
                           id == "t_ladder_down";
                } )();
                // Outdoor roof and platform tiles should preserve the actual
                // known exterior surface, not a generic wooden "t_floor"
                // proxy. This is limited to the exposed upper boundary below
                // a natively visible air shaft; indoor floors stay private.
                const bool upper_exposed_outdoor = upper_floor_boundary &&
                    here.is_outside( p ) && !here.is_open_air( p );
                if( upper_floor_boundary && !tower_exterior_surface &&
                    !tower_upper_boundary && !upper_exposed_outdoor ) {
                    cell boundary;
                    boundary.position = here.get_abs( p );
                    boundary.terrain = "t_floor";
                    boundary.kind = terrain_kind::floor;
                    boundary.known = knowledge::visible;
                    boundary.exterior_only = true;
                    boundary.floor_present = true;
                    boundary.ceiling_present = false;
                    result.cells.push_back( std::move( boundary ) );
                    ++result.visible;
                    continue;
                }
                // The first solid boundary below visible shaft air is visible
                // geometry even when cross-level shadowcasting drops its endpoint.
                // This fallback never copies the room's furniture or contents.
                const bool lower_surface = fresh && !visible && !blind && p.z() < player.z() &&
                                           ( perception[index( p + tripoint_rel_ms::above )] & air_bit ) &&
                                           !you.cant_see( p ) && square_dist( player, p ) <= sight_range &&
                                           here.has_floor( p );
                const bool exterior_only = fresh && !visible && ( room_surface || lower_surface ||
                                         tower_exterior_surface || tower_upper_boundary ||
                                         upper_exposed_outdoor ||
                                         exposed_surface( you, here, p, player, sight_range, beside_visible_air, blind ) );
                // The grid walks (2*radius+1)^2 positions per storey and the vast
                // majority contribute nothing: no perception and no memory. Build
                // a cell only once it can actually be emitted; constructing and
                // destroying the strings of a discarded cell was most of
                // capture()'s cost.
                std::string remembered_terrain;
                if( !visible && !exterior_only ) {
                    // Never query live terrain, furniture, items or creatures here.
                    remembered_terrain = memorized.at( here.get_abs( p ) ).get_ter_id();
                    if( remembered_terrain.empty() || remembered_terrain == "t_null" ) {
                        const tripoint_abs_ms absolute = here.get_abs( p );
                        // Mask only the lower knowledge boundary. Extending
                        // anonymous volumes upwards would obscure the skyline.
                        if( p.z() > player.z() ) {
                            continue;
                        }
                        if( !result.unknown.empty() &&
                            result.unknown.back().position +
                            tripoint_rel_ms( result.unknown.back().width, 0, 0 ) == absolute ) {
                            ++result.unknown.back().width;
                        } else {
                            result.unknown.push_back( { absolute, 1 } );
                        }
                        continue;
                    }
                }
                cell tile;
                tile.position = here.get_abs( p );
                tile.exterior_only = exterior_only;
                tile.over_avatar_region = region_column;
                if( visible || exterior_only ) {
                    tile.terrain = here.ter( p ).id().str();
                    tile.known = knowledge::visible;
                    if( native_visible ) {
                        const auto creature = creatures.find( p );
                        capture_sprites( result, you, here, p,
                                         creature == creatures.end() ? nullptr : creature->second );
                        if( !tile.exterior_only && p.z() == player.z() && square_dist( p, player ) <= 1 ) {
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
                    // Independent camera geometry includes real upper floors,
                    // so open air needs no synthetic underside or fog volume.
                    if( camera_geometry && here.is_open_air( p ) ) {
                        continue;
                    }
                } else {
                    tile.terrain = std::move( remembered_terrain );
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
                if( ( visible || tile.exterior_only ) && tile.floor_present &&
                    tile.terrain == "t_railing" ) {
                    tile.metal_deck_railing = metal_rail( p );
                }
                const auto overhead = p + tripoint_rel_ms::above;
                const bool visible_air_above = p.z() < result.max_z &&
                                               ( perception[index( overhead )] & air_bit );
                if( !camera_geometry && ( visible || tile.exterior_only ) && !blind && here.supports_zlevels() ) {
                    // Copy only the visible room's enclosing boundary. Never
                    // copy the unseen upstairs terrain ID, contents or layout.
                    // Roof/ceiling proxies must be backed by an actual floor,
                    // or by a genuinely closed overhead boundary. Standing
                    // below an open radio-tower platform used to manufacture
                    // extra wooden panels whenever upper open air happened to
                    // be absent from the 3D LOS cache.
                    const bool actual_floor_above = here.inbounds( overhead ) &&
                                                   here.has_floor( overhead );
                    const std::string overhead_id = actual_floor_above ?
                                                    here.ter( overhead ).id().str() : "";
                    // t_radio_tower is an OPEN steel lattice, even though
                    // native has_floor() may treat its impassable terrain as
                    // supporting a floor. It must not create a wooden ceiling.
                    const bool overhead_lattice = overhead_id == "t_radio_tower";
                    tile.ceiling_present = !overhead_lattice &&
                        ( actual_floor_above ||
                          ( here.inbounds( overhead ) && !here.is_outside( p ) &&
                            !visible_air_above && !here.is_open_air( overhead ) ) );
                    if( actual_floor_above && !overhead_lattice ) {
                        // Retain the metal deck's appearance on the underside
                        // of a known upper floor without reading its contents.
                        if( overhead_id == "t_metal_floor_no_roof" ||
                            overhead_id == "t_floor_metal_gangway" ) {
                            tile.ceiling_surface = overhead_id;
                        } else if( metal_rail( overhead ) ) {
                            // A tower railing is built on the same metal
                            // platform as its neighbours. Both the rail tile
                            // and the ceiling proxy under it must resolve to
                            // the same material regardless of upper LOS.
                            tile.ceiling_surface = "t_metal_floor_no_roof";
                        }
                        // A skylight is already seen through from below, so
                        // keeping its translucent pane is not extra upstairs
                        // disclosure. It must not become an opaque ceiling.
                        if( tile.ceiling_present && is_glass_roof( overhead_id ) ) {
                            tile.glass_ceiling = true;
                        }
                    }
                }
                if( tile.known == knowledge::remembered && !visible_air_above &&
                    definition.has_flag( ter_furn_flag::TFLAG_INDOORS ) ) {
                    // Remembered indoor geometry must enclose the remembered room
                    // too; never inspect the live, unseen upstairs here.
                    tile.ceiling_present = true;
                    // Only the remembered upstairs tile may say whether that
                    // boundary was roof glass; the live unseen cell stays
                    // unread. Absence of a memory simply keeps the ceiling.
                    if( is_glass_roof( memorized.at( here.get_abs( overhead ) ).get_ter_id() ) ) {
                        tile.glass_ceiling = true;
                    }
                }
                if( tile.open_air && !tile.ceiling_present ) {
                    continue;
                }
                // The same cut removes only the overhead boundary of the
                // avatar's own room; the walls, floor and contents stay.
                if( cut_overhead && region_column ) {
                    tile.ceiling_present = false;
                    tile.glass_ceiling = false;
                    tile.ceiling_surface.clear();
                }
                tile.goes_up = definition.has_flag( ter_furn_flag::TFLAG_GOES_UP );
                tile.goes_down = definition.has_flag( ter_furn_flag::TFLAG_GOES_DOWN );
                tile.is_ramp = definition.has_flag( ter_furn_flag::TFLAG_RAMP ) ||
                               definition.has_flag( ter_furn_flag::TFLAG_RAMP_UP ) ||
                               definition.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN );
                tile.ramp_delta = !tile.is_ramp ? 0 :
                                  definition.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN ) ? -1 : 1;
                const auto presentation = terrain_presentations.try_emplace( tile.terrain );
                if( presentation.second ) {
                    presentation.first->second = { classify( tile.terrain ), cached_plant_height( tile.terrain ) };
                }
                tile.kind = presentation.first->second.kind;
                if( tile.kind == terrain_kind::window ) {
                    tile.pane = !definition.has_flag( ter_furn_flag::TFLAG_TRANSPARENT ) ?
                                window_pane::covered : definition.movecost > 0 ?
                                window_pane::opening : window_pane::glazed;
                }
                if( tile.exterior_only &&
                    ( ( tile.open_air && !tile.ceiling_present && tile.terrain != "t_radio_tower" ) ||
                      ( tile.kind == terrain_kind::obstacle && tile.terrain != "t_radio_tower" ) ||
                      presentation.first->second.vegetation_height > 0 ) ) {
                    continue;
                }
                tile.vegetation_height = presentation.first->second.vegetation_height;
                if( tile.vegetation_height > 0 ) {
                    tile.vegetation = tile.terrain;
                    tile.kind = terrain_kind::floor;
                }
                // Camera geometry reads loaded furniture along with terrain.
                // Native/reference capture keeps its live-versus-memory rule.
                const std::string furniture = tile.exterior_only ? "" : visible ?
                                              here.furn( p ).id().str() :
                                              memorized.at( tile.position ).get_dec_id();
                if( const float height = cached_plant_height( furniture ); height > 0 ) {
                    tile.vegetation = furniture;
                    tile.vegetation_height = height;
                }
                if( visible && !tile.exterior_only ) {
                    // CDDA itself already distinguishes static emissions
                    // from unpowered street-lamp furniture here. Only emit
                    // what the native definitions actually emit this turn.
                    const int native_terrain_lum = here.ter( p )->light_emitted;
                    const int native_furniture_lum = here.furn( p )->light_emitted;
                    const int native_lum = std::max( native_terrain_lum, native_furniture_lum );
                    if( native_lum > 0 ) {
                        result.lights.push_back( { tile.position, light_kind::fixture,
                                                 static_cast<float>( native_lum ),
                                                 native_furniture_lum > 0 ? 1.6f : .4f,
                                                 native_furniture_lum > 0 ? furniture : tile.terrain } );
                    }
                    // Fires and burning fields emit real native light, too.
                    for( const auto &emission : here.field_at( p ) ) {
                        const int field_lum = emission.second.get_intensity_level().light_emitted;
                        if( field_lum > 0 ) {
                            result.lights.push_back( { tile.position, light_kind::flame,
                                                     static_cast<float>( field_lum ),
                                                     .65f, emission.first.id().str() } );
                        }
                    }
                    tile.furniture = furniture_model( furniture );
                    if( !tile.furniture ) {
                        const auto part = here.veh_at( p ).part_with_feature( VPFLAG_APPLIANCE, false );
                        if( part ) {
                            tile.furniture = furniture_model( part->info().looks_like );
                            if( tile.furniture ) {
                                tile.appliance = part->info().id.str();
                            }
                        }
                    }
                    for( const auto &entry : here.field_at( p ) ) {
                        const std::string id = entry.first.id().str();
                        const auto &definition = entry.first.obj();
                        const int intensity = entry.second.get_field_intensity();
                        // Liquid fields and the native solid ground coverings.
                        // Gases, flames and invisible gameplay markers are not decals.
                        if( definition.display_field && entry.second.is_field_alive() &&
                            ( definition.phase == phase_id::LIQUID || id == "fd_web" ||
                              id == "fd_web_nostick" || id == "fd_void_web" ||
                              id == "fd_churned_earth" || id == "fd_rubble" ) ) {
                            tile.ground_fields.push_back( { id, intensity } );
                        }
                    }
                    // Keep native field priority; hazards remain above cosmetic stains.
                    std::stable_sort( tile.ground_fields.begin(), tile.ground_fields.end(),
                    []( const ground_field &left, const ground_field &right ) {
                        const auto &a = field_type_str_id( left.id ).obj();
                        const auto &b = field_type_str_id( right.id ).obj();
                        if( a.is_dangerous() != b.is_dangerous() ) {
                            return !a.is_dangerous();
                        }
                        return a.priority < b.priority;
                    } );
                }
                if( tile.known == knowledge::visible ) {
                    ++result.visible;
                } else {
                    ++result.remembered;
                }
                if( result.weather.particles != fp3d_weather::precipitation::none &&
                    tile.known == knowledge::visible && !tile.exterior_only && tile.floor_present &&
                    tile.kind == terrain_kind::floor && !tile.ceiling_present &&
                    dx * dx + dy * dy <= 144 && here.is_outside( p ) ) {
                    result.weather.columns.push_back( tile.position );
                }
                result.cells.push_back( std::move( tile ) );
            }
        }
    }
    if( retaining ) {
        if( retained_cells == previous->cells.size() ) {
            result.cells = std::move( previous->cells );
            result.geometry_generation = previous->geometry_generation;
            result.stairs_connected = true;
        } else {
            result.cells.assign( previous->cells.begin(), previous->cells.begin() + retained_cells );
        }
    }
    // An orbit camera needs the exterior roof skin even where ground LOS
    // cannot see its middle. Copy roof surfaces only, never upstairs contents.
    const auto roof_surface = []( const std::string &id ) {
        return id == "t_gutter" || id == "t_gutter_drop" ||
               ( id.find( "roof" ) != std::string::npos &&
                 id.find( "no_roof" ) == std::string::npos &&
                 id.find( "rooved" ) == std::string::npos &&
                 id.find( "open_air" ) == std::string::npos );
    };
    if( cut_overhead && fresh && !blind && !camera_geometry ) {
        std::unordered_set<tripoint_abs_ms> occupied;
        std::vector<tripoint_bub_ms> roofs;
        for( const auto &tile : result.cells ) {
            occupied.insert( tile.position );
            if( tile.known != knowledge::visible ) { continue; }
            const auto p = here.get_bub( tile.position );
            if( roof_surface( tile.terrain ) ) {
                roofs.push_back( p );
            } else if( tile.kind == terrain_kind::wall || tile.kind == terrain_kind::window ) {
                for( int z = p.z() + 1; z <= result.max_z; ++z ) {
                    const tripoint_bub_ms upper( p.xy(), z );
                    if( over_avatar_region( upper ) || !here.inbounds( upper ) ) { break; }
                    if( roof_surface( here.ter( upper ).id().str() ) ) {
                        roofs.push_back( upper );
                        break;
                    }
                    if( here.is_open_air( upper ) ) { break; }
                }
            }
        }
        std::unordered_set<tripoint_bub_ms> visited;
        for( size_t i = 0; i < roofs.size(); ++i ) {
            const auto p = roofs[i];
            if( !visited.insert( p ).second || !here.inbounds( p ) ||
                p.x() < first_x || p.x() >= first_x + side ||
                p.y() < first_y || p.y() >= first_y + side ||
                over_avatar_region( p ) || square_dist( p, player ) > sight_range ) { continue; }
            const auto id = here.ter( p ).id().str();
            if( !roof_surface( id ) || !here.has_floor( p ) ) { continue; }
            if( occupied.insert( here.get_abs( p ) ).second ) {
                cell roof;
                roof.position = here.get_abs( p );
                roof.terrain = id;
                roof.exterior_only = true;
                roof.floor_present = true;
                result.cells.push_back( std::move( roof ) );
                ++result.visible;
            }
            for( const auto &dir : { tripoint_rel_ms::north, tripoint_rel_ms::east,
                                    tripoint_rel_ms::south, tripoint_rel_ms::west } ) {
                roofs.push_back( p + dir );
            }
        }
    }
    // Native 2D gutter connections: south/east/west/north bits.
    for( auto &tile : result.cells ) {
        if( tile.terrain != "t_gutter" && tile.terrain != "t_gutter_drop" ) { continue; }
        const auto p = here.get_bub( tile.position );
        const std::array<tripoint_rel_ms, 4> dirs{ tripoint_rel_ms::south,
            tripoint_rel_ms::east, tripoint_rel_ms::west, tripoint_rel_ms::north };
        tile.roof_connections = 0;
        for( unsigned i = 0; i < dirs.size(); ++i ) {
            const auto q = p + dirs[i];
            std::string id;
            if( tile.known == knowledge::visible && here.inbounds( q ) ) {
                id = here.ter( q ).id().str();
            } else {
                id = memorized.at( here.get_abs( q ) ).get_ter_id();
            }
            if( id == "t_gutter" || id == "t_gutter_drop" || id == "t_railing" ) {
                tile.roof_connections |= 1 << i;
            }
        }
    }
    const auto detail_finished = std::chrono::steady_clock::now();
    if( !blind ) {
        // P5: the avatar's own tile sprite for the overhead camera. Same tileset
        // body and clothing overlays the 2D renderer uses, on the avatar's real
        // tile and storey. It is never a creature target and the first-person
        // drawer ignores it, so the avatar is never drawn twice.
        sprite avatar_image;
        avatar_image.kind = sprite_kind::character;
        avatar_image.is_avatar = true;
        avatar_image.position = result.origin;
        avatar_image.male = you.male;
        const auto overrides = you.get_functioning_mutations( true, false,
        []( const mutation_branch & mutation ) {
            return mutation.override_look.has_value();
        } );
        if( overrides.empty() ) {
            avatar_image.id = you.male ? "player_male" : "player_female";
            avatar_image.overlays = you.get_overlay_ids();
        } else {
            avatar_image.id = overrides.front()->override_look->id;
            avatar_image.overlays = you.get_overlay_ids_when_override_look();
        }
        // Movement signs use oversized transparent canvases in native tilesets.
        // Keep them off the body atlas so changing gait cannot rescale/blur it.
        const std::string movement = you.current_movement_mode().str();
        avatar_image.overlays.erase( std::remove_if( avatar_image.overlays.begin(),
        avatar_image.overlays.end(), [&]( const auto &overlay ) {
            if( overlay.first != movement ) {
                return false;
            }
            avatar_image.status_icons.emplace_back( "overlay_" + movement );
            return true;
        } ), avatar_image.overlays.end() );
        avatar_image.height = you.is_crouching() ? 1.2f : 1.8f;
        result.sprites.push_back( std::move( avatar_image ) );
    }
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
    if( !result.stairs_connected ) { connect_stairs( result ); }
    std::sort( result.weather.columns.begin(), result.weather.columns.end(), [&]( const auto &a, const auto &b ) {
        const auto left = a - result.origin;
        const auto right = b - result.origin;
        return left.x() * left.x() + left.y() * left.y() + left.z() * left.z() <
               right.x() * right.x() + right.y() * right.y() + right.z() * right.z();
    } );
    const auto tail_finished = std::chrono::steady_clock::now();
    result.capture_full_ms = std::chrono::duration<double, std::milli>( tail_finished - started ).count();
    fp3d_regression::snapshot_full_capture( result.capture_full_ms,
                                            std::chrono::duration<double, std::milli>(
                                                tail_finished - finished ).count() );
    // Explicit, bounded QA export. Rejected cells are included so a missing
    // surface can be distinguished from native unknown space. Live map data
    // here goes only to the diagnostic file, never into the published scene.
    static bool traced = false;
    const char *trace_path = std::getenv( "CDDA_FP3D_QA_TRACE_FILE" );
    const char *capture_frame = std::getenv( "CDDA_FP3D_CAPTURE_FRAME" );
    if( !traced && fresh && trace_path && capture_frame && std::string( capture_frame ) == "1" ) {
        traced = true;
        std::ofstream trace( trace_path );
        trace << "origin," << result.origin.x() << ',' << result.origin.y() << ',' << result.origin.z()
              << ",capture_ms," << result.capture_full_ms << '\n';
        trace << "x,y,z,terrain,seen,camera,native_los,perceived,memory,open_air,has_floor,"
              "snapshot,exterior_only,floor,ceiling,cut,skip_reason\n";
        std::unordered_map<tripoint_abs_ms, const cell *> published;
        for( const auto &tile : result.cells ) { published.emplace( tile.position, &tile ); }
        constexpr int trace_radius = 32;
        for( int z = result.min_z; z <= result.max_z; ++z ) {
            const auto &cache = here.access_cache( z );
            for( int dy = -trace_radius; dy <= trace_radius; ++dy ) {
                for( int dx = -trace_radius; dx <= trace_radius; ++dx ) {
                    const auto p = player + tripoint_rel_ms( dx, dy, z - player.z() );
                    if( !here.inbounds( p ) ) { continue; }
                    const auto absolute = here.get_abs( p );
                    const auto found = published.find( absolute );
                    const cell *tile = found == published.end() ? nullptr : found->second;
                    const bool perceived = perception[index( p )] & visible_bit;
                    const bool cut = cut_overhead && over_avatar_region( p ) && z > player.z();
                    const auto memory = memorized.at( absolute ).get_ter_id();
                    const char *reason = tile ? "present" : cut ? "overhead_cut" :
                                         !perceived && ( memory.empty() || memory == "t_null" ) ?
                                         "unknown_no_memory" : here.is_open_air( p ) ?
                                         "open_air_without_boundary" : "filtered";
                    trace << absolute.x() << ',' << absolute.y() << ',' << z << ','
                          << here.ter( p ).id().str() << ',' << cache.seen_cache[p.x()][p.y()] << ','
                          << cache.camera_cache[p.x()][p.y()] << ',' << here.sees( player, p, sight_range ) << ','
                          << perceived << ',' << memory << ',' << here.is_open_air( p ) << ','
                          << here.has_floor( p ) << ',' << ( tile != nullptr ) << ','
                          << ( tile && tile->exterior_only ) << ',' << ( tile && tile->floor_present ) << ','
                          << ( tile && tile->ceiling_present ) << ',' << cut << ',' << reason << '\n';
                }
            }
        }
    }
    return result;
}
scene_snapshot capture( avatar &you, const map &here, int above, int below, bool cut_overhead )
{
    return capture_impl( you, here, above, below, cut_overhead, false );
}

scene_snapshot capture_camera( avatar &you, const map &here, int above, int below,
                               bool cut_overhead )
{
    return capture_impl( you, here, above, below, cut_overhead, true );
}
scene_snapshot capture_camera_reusing( avatar &you, const map &here, scene_snapshot *previous,
                                       int above, int below, bool cut_overhead )
{
    return capture_impl( you, here, above, below, cut_overhead, true, previous );
}
} // namespace fp3d_snapshot
#endif
