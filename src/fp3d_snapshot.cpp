#include "fp3d_snapshot.h"
#include "fp3d_interaction.h"
#include "fp3d_regression.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <utility>
#include <algorithm>
#include <chrono>
#include <cmath>
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
            // Keep legitimate CDDA terrain IDs: this game version has no
            // generic t_road terrain/atlas icon. Road overmaps are represented
            // by t_pavement and must be tiled, NOT stretched across 12m.
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
            image.overlays = mon->get_overlay_ids();
            // Only mark genuinely aggressive monsters with current native LOS.
            image.alerted = mon->attitude( &you ) == MATT_ATTACK && mon->sees( here, you );
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
    // The native attention effect can also request the same alert artwork.
    image.alerted = image.alerted || active_effects.count( "attention" ) != 0;
    // Map every remaining UndeadPeople effect marker to its exact active
    // effect ID. The pack-only temp/morale_wet art stays dormant unless an
    // optional mod actually registers that effect. Urgent effects come first;
    // four displayed badges per creature avoid long ribbons across the world.
    static constexpr const char *extra_effects_by_priority[] = {
        "onfire",
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
    image.height *= 1.2f;
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
// air. Authorize only a surface touching native-visible open air;
// no furniture, items or creatures may use this presentation-only fallback.
bool exposed_surface( const avatar &you, const map &here, const tripoint_bub_ms &p,
                      bool beside_visible_air, bool blind )
{
    if( !beside_visible_air || p.z() <= you.posz() || blind || you.cant_see( p ) ||
        square_dist( you.pos_bub( here ), p ) > you.unimpaired_range() ) {
        return false;
    }
    const terrain_kind kind = classify( here.ter( p ).id().str() );
    const bool facade = kind == terrain_kind::wall || kind == terrain_kind::window ||
                        kind == terrain_kind::glass || kind == terrain_kind::closed_door ||
                        kind == terrain_kind::open_door || kind == terrain_kind::damaged_door;
    // An unseen floor above the player is already represented by the room's
    // neutral ceiling. Only facade surfaces may bypass endpoint ray LOS.
    return facade || here.sees( you.pos_bub( here ), p, you.unimpaired_range() );
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
        // Include every native-visible storey above the player, and retain
        // the surface even from z=4 and higher. Avoid building ten empty
        // subterranean cache levels when the player is outdoors.
        for( int z = std::max( -OVERMAP_DEPTH, p.z() - below_for_storey( p.z() ) );
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
    }
    const tripoint_bub_ms player = you.pos_bub( here );
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
    // P0-V / P1-V: cover every map-loaded vehicle, not only the car being
    // driven.  VehicleList may include duplicates across submaps/z-levels;
    // de-duplicate by pointer ONLY within this capture, never store that
    // pointer or a vehicle_part reference in the published snapshot.
    // The get_vehicles() API is non-const in CDDA 0.I because it lazily
    // walks map caches. This call does not modify vehicle simulation state.
    if( fresh && !blind ) {
        std::unordered_set<const vehicle *> visited;
        for( const wrapped_vehicle &entry : const_cast<map &>( here ).get_vehicles() ) {
            const vehicle *veh = entry.v;
            if( !veh || !visited.insert( veh ).second ) {
                continue;
            }
            ++result.enumerated_vehicles;
            fp3d_vehicle::vehicle_view view;
            view.origin = veh->pos_abs();
            view.name = veh->name;
            view.angle_degrees = units::to_degrees( veh->pivot_rotation[0] );
            view.pivot_x = veh->pivot_anchor[0].x();
            view.pivot_y = veh->pivot_anchor[0].y();
            // A complete car must be assembled from a complete local
            // geometry snapshot. Visibility can reject the far-side
            // fender *because the car itself blocks the 2D LOS*, leaving
            // a scattered chair/glass scaffold on the 3D side. Gate at
            // the entity level instead: render this car iff at least one
            // nonremoved mount is natively visible, then let terrain GL
            // depth testing hide all parts behind buildings/obstacles.
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
                    here.inbounds( p ) && ( perception[index( p )] & visible_bit ) ) {
                    any_part_visible = true;
                }
                const vpart_info &info = part.info();
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
    result.cells.reserve( ( radius * 2 + 1 ) * ( radius * 2 + 1 ) );
    for( int z = result.min_z; z <= result.max_z; ++z ) {
        const auto &cache = here.access_cache( z );
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
                                          here.sees( player, p, sight_range );
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
                    square_dist( player, p ) <= you.unimpaired_range() &&
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
                    tile.terrain = "t_floor";
                    tile.kind = terrain_kind::floor;
                    tile.known = knowledge::visible;
                    tile.exterior_only = true;
                    tile.floor_present = true;
                    tile.ceiling_present = false;
                    ++result.visible;
                    result.cells.push_back( std::move( tile ) );
                    continue;
                }
                // The first solid boundary below visible shaft air is visible
                // geometry even when cross-level shadowcasting drops its endpoint.
                // This fallback never copies the room's furniture or contents.
                const bool lower_surface = fresh && !visible && !blind && p.z() < player.z() &&
                                           ( perception[index( p + tripoint_rel_ms::above )] & air_bit ) &&
                                           !you.cant_see( p ) && square_dist( player, p ) <= you.unimpaired_range() &&
                                           here.has_floor( p );
                tile.exterior_only = fresh && !visible && ( room_surface || lower_surface ||
                                     tower_exterior_surface || tower_upper_boundary ||
                                     upper_exposed_outdoor ||
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
                if( ( visible || tile.exterior_only ) && tile.floor_present &&
                    tile.terrain == "t_railing" ) {
                    tile.metal_deck_railing = metal_rail( p );
                }
                const auto overhead = p + tripoint_rel_ms::above;
                const bool visible_air_above = p.z() < result.max_z &&
                                               ( perception[index( overhead )] & air_bit );
                if( ( visible || tile.exterior_only ) && !blind && here.supports_zlevels() ) {
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
                    }
                }
                if( tile.known == knowledge::remembered && !visible_air_above &&
                    definition.has_flag( ter_furn_flag::TFLAG_INDOORS ) ) {
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
                if( tile.exterior_only &&
                    ( ( tile.open_air && !tile.ceiling_present && tile.terrain != "t_radio_tower" ) ||
                      ( tile.kind == terrain_kind::obstacle && tile.terrain != "t_radio_tower" ) ||
                      plant_height( tile.terrain ) > 0 ) ) {
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
                        if( id.compare( 0, 8, "fd_blood" ) == 0 &&
                            entry.second.get_field_intensity() > tile.blood_intensity ) {
                            tile.blood = id;
                            tile.blood_intensity = entry.second.get_field_intensity();
                        }
                    }
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
    connect_stairs( result );
    std::sort( result.weather.columns.begin(), result.weather.columns.end(), [&]( const auto &a, const auto &b ) {
        const auto left = a - result.origin;
        const auto right = b - result.origin;
        return left.x() * left.x() + left.y() * left.y() + left.z() * left.z() <
               right.x() * right.x() + right.y() * right.y() + right.z() * right.z();
    } );
    return result;
}
} // namespace fp3d_snapshot
#endif
