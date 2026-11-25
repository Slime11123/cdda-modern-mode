#include "fp3d_hud.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include "avatar.h"
#include "bodypart.h"
#include "catacharset.h"
#include "creature_tracker.h"
#include "fp3d_inventory.h"
#include "fp3d_map.h"
#include "fp3d_snapshot.h"
#include "fp3d_targeting.h"
#include "item.h"
#include "item_location.h"
#include "lightmap.h"
#include "calendar.h"
#include "display.h"
#include "debug.h"
#include "game.h"
#include "messages.h"
#include "mission.h"
#include "map.h"
#include "monster.h"
#include "npc.h"
#include "vehicle.h"
#include "veh_type.h"
#include "vpart_position.h"
#include "omdata.h"
#include "output.h"
#include "overmapbuffer.h"
#include "string_formatter.h"
#include "submap.h"
#include "timed_event.h"
#include "weather.h"
#include "translations.h"
namespace fp3d_hud
{
namespace
{
bool show_navigation = true;
const json_character_flag flag_pain_immune( "PAIN_IMMUNE" );
}
bool navigation_enabled()
{
    return show_navigation;
}
void set_navigation_enabled( bool enabled )
{
    show_navigation = enabled;
}

std::string health_text( const avatar &you, const bodypart_id &part )
{
    const int current = you.get_part_hp_cur( part );
    const int maximum = you.get_part_hp_max( part );
    if( maximum <= 0 ) {
        return "—";
    }
    if( you.is_limb_broken( part ) ) {
        return "==%==";
    }
    if( you.has_flag( flag_pain_immune ) ) {
        const float fraction = current / static_cast<float>( maximum );
        return fraction < .125f ? _( "Very Bad" ) : fraction < .375f ? _( "Bad" ) :
               fraction < .625f ? _( "So-so" ) : fraction < .875f ? _( "Okay" ) : _( "Good" );
    }
    if( debug_mode ) {
        return std::to_string( current ) + "/" + std::to_string( maximum );
    }
    const auto bar = get_hp_bar( current, maximum );
    return bar.first + std::string( std::max( 0, 5 - utf8_width( bar.first ) ), '.' );
}

std::string stamina_text( const avatar &you )
{
    if( debug_mode ) {
        return std::to_string( you.get_stamina() ) + "/" + std::to_string( you.get_stamina_max() );
    }
    const auto bar = get_stamina_bar( you.get_stamina(), you.get_stamina_max() );
    return bar.first + std::string( std::max( 0, 5 - utf8_width( bar.first ) ), '.' );
}

std::vector<data::message_entry> inspect( const avatar &you, const map &here,
        const fp3d_targeting::target &target )
{
    const auto position = here.get_bub( target.position );
    const auto &visibility = here.get_visibility_variables_cache();
    if( target.origin != you.pos_abs() || you.is_blind() || !here.inbounds( position ) ) {
        return {};
    }
    // The tile itself must be clearly visible for terrain/furniture/item text,
    // exactly as before. A creature gets its own rule further down.
    const bool clearly_visible =
        here.get_visibility( here.apparent_light_at( position, visibility ),
                             visibility ) == visibility_type::CLEAR;

    const auto area_position = coords::project_to<coords::omt>( target.position );
    std::string area = uppercase_first_letter( overmap_buffer.ter( area_position )->get_name(
                           overmap_buffer.seen( area_position ) ) );
    if( const timed_event *event = get_timed_events().get( timed_event_type::OVERRIDE_PLACE ) ) {
        area = event->string_id;
    }
    std::vector<data::message_entry> lines{ { area, c_yellow } };
    // A visible creature standing on the inspected tile is the primary subject,
    // whether the pick landed on the actor itself or on the tile under it. Its
    // name, wounds and attitude come from the native monster/NPC accessors
    // only, so nothing is invented and nothing unseen is revealed. The
    // visibility test is the same light-ignoring native check the 3D snapshot
    // uses to decide that this actor is actually drawn, so the panel can never
    // describe a creature the view does not show, and never hides one it does.
    const Creature *creature = get_creature_tracker().creature_at( position, true );
    bool described_creature = false;
    if( creature && !creature->is_avatar() && you.Creature::sees( here, *creature, true ) ) {
        described_creature = true;
        lines.push_back( { creature->disp_name(), c_white } );
        if( const monster *mon = creature->as_monster() ) {
            nc_color bar_color = c_white;
            std::string bar_text;
            mon->get_HP_Bar( bar_color, bar_text );
            if( !bar_text.empty() ) {
                lines.push_back( { bar_text, bar_color } );
            }
            const std::pair<std::string, nc_color> attitude = mon->get_attitude();
            if( !attitude.first.empty() ) {
                lines.push_back( { attitude.first, attitude.second } );
            }
        } else if( creature->as_character() ) {
            const auto bar = get_hp_bar( creature->get_hp(), creature->get_hp_max() );
            lines.push_back( { bar.first, bar.second } );
            if( const npc *guy = creature->as_npc() ) {
                lines.push_back( { npc_attitude_name( guy->get_attitude() ), c_light_gray } );
            }
        }
        // Items the creature is standing on stay readable as secondary
        // information instead of disappearing behind the actor.
        const auto floor = here.maptile_at( position );
        if( floor.get_item_count() > 0 && floor.get_field().displayed_field_type()->display_items &&
            here.could_see_items( position, you ) ) {
            std::string underfoot = floor.get_uppermost_item().tname();
            if( floor.get_item_count() > 1 ) {
                underfoot += " +" + std::to_string( floor.get_item_count() - 1 );
            }
            lines.push_back( { std::move( underfoot ), c_light_gray } );
        }
    } else if( clearly_visible && target.type == fp3d_targeting::kind::vehicle &&
               target.vehicle_origin && target.vehicle_part >= 0 ) {
        const auto vp = here.veh_at( position );
        if( vp && vp->vehicle().pos_abs() == *target.vehicle_origin &&
            target.vehicle_part < vp->vehicle().part_count() ) {
            const vehicle_part &part = vp->vehicle().part( target.vehicle_part );
            if( !part.removed && !part.is_fake && !part.hidden &&
                part.mount.x() == target.vehicle_mount_x &&
                part.mount.y() == target.vehicle_mount_y &&
                part.info().id.str() == target.id ) {
                lines.push_back( { vp->vehicle().name, c_yellow } );
                lines.push_back( { part.info().name(), part.is_broken() ? c_light_red : c_white } );
            }
        }
    } else if( clearly_visible && target.type == fp3d_targeting::kind::item &&
               here.could_see_items( position, you ) ) {
        const auto tile = here.maptile_at( position );
        if( tile.get_item_count() > 0 && tile.get_field().displayed_field_type()->display_items ) {
            const item &object = tile.get_uppermost_item();
            if( fp3d_snapshot::item_sprite_id( object ) == target.id ) {
                lines.push_back( { object.tname(), c_white } );
            }
        }
    }
    // Terrain, furniture and the tile details keep the original rule: the
    // avatar must be able to make out the tile itself. A creature the 3D view
    // really draws is already named above, even in a dark room.
    if( !clearly_visible ) {
        return described_creature ? lines : std::vector<data::message_entry>{};
    }
    lines.push_back( { uppercase_first_letter( here.tername( position ) ), c_white } );
    lines.push_back( { here.ter( position ).obj().description.translated(), c_light_gray } );
    if( here.has_furn( position ) ) {
        const auto &furniture = here.furn( position ).obj();
        lines.push_back( { uppercase_first_letter( furniture.name() ), c_white } );
        lines.push_back( { furniture.description.translated(), c_light_gray } );
    }
    lines.push_back( { string_format( _( "Concealment: %d%%" ), here.coverage( position ) ), c_light_gray } );
    if( std::string features = here.features( position ); !features.empty() ) {
        lines.push_back( { std::move( features ), c_light_gray } );
    }
    if( here.impassable( position ) ) {
        lines.push_back( { _( "Impassable" ), c_light_red } );
    } else {
        lines.push_back( { string_format( _( "Move cost: %d" ), here.move_cost( position ) * 50 ),
                           c_light_gray } );
    }
    const auto light = get_light_level( std::max( 1.0,
                                        LIGHT_AMBIENT_LIT - here.ambient_light_at( position ) + 1.0 ) );
    lines.push_back( { _( "Lighting: " ) + light.first, light.second } );
    return lines;
}

std::vector<data::message_entry> message_rows( const std::vector<data::message_entry> &messages,
        int columns, int rows )
{
    std::vector<data::message_entry> result;
    if( columns <= 0 || rows <= 0 ) {
        return result;
    }
    // Collect backwards so wrapping an old message never hides a newer one.
    for( auto message = messages.rbegin(); message != messages.rend() &&
         static_cast<int>( result.size() ) < rows; ++message ) {
        auto folded = foldstring( message->text, columns );
        for( auto line = folded.rbegin(); line != folded.rend() &&
             static_cast<int>( result.size() ) < rows; ++line ) {
            result.push_back( { *line, message->color } );
        }
    }
    std::reverse( result.begin(), result.end() );
    return result;
}

data capture( avatar &you )
{
    data result;
    result.status = "耐力 " + stamina_text( you ) + " | " +
                    ( you.is_running() ? "奔跑" : you.is_crouching() ? "蹲姿" : "行走" );
    result.day_time = "第 " + std::to_string( to_days<int>( calendar::turn - calendar::turn_zero ) + 1 ) +
                      " 天  " + display::time_string( you );
    result.temperature = display::get_temp( you );
    if( const mission *active = you.get_active_mission() ) {
        result.mission = active->name();
    } else {
        result.mission = "暂无活动任务";
    }
    if( const auto weapon = you.get_wielded_item(); weapon && weapon->is_gun() ) {
        result.status += " | 弹药 " + std::to_string( weapon->ammo_remaining() );
    }
    const auto &weariness = display::weariness_text_color( you );
    result.status_entries.push_back( { "疲惫", weariness.first, weariness.second } );
    const auto activity = display::activity_text_color( you );
    result.status_entries.push_back( { "活动", activity.first, activity.second } );
    const auto weariness_progress = you.weariness_transition_progress();
    const int weariness_percent = weariness_progress.second > 0 ? std::clamp( 100 -
                                  weariness_progress.first * 100 / weariness_progress.second, 0, 100 ) : 0;
    result.status_entries.push_back( { "疲惫进程", debug_mode ? std::to_string( weariness_percent ) + "%" :
                                      get_bar( weariness_percent, 100, 5, false ).first, c_yellow } );
    const auto fatigue_penalty = display::weary_malus_text_color( you );
    result.status_entries.push_back( { "疲惫惩罚", fatigue_penalty.first, fatigue_penalty.second } );
    const auto pain = display::pain_text_color( you );
    result.status_entries.push_back( { "疼痛", pain.first.empty() ? "无" : pain.first, pain.second } );
    const auto thirst = display::thirst_text_color( you );
    result.status_entries.push_back( { "口渴", thirst.first, thirst.second } );
    const auto sleepiness = display::sleepiness_text_color( you );
    result.status_entries.push_back( { "睡意", sleepiness.first.empty() ? "清醒" : sleepiness.first,
                                       sleepiness.second } );
    const auto hunger = display::hunger_text_color( you );
    result.status_entries.push_back( { "饥饿", hunger.first, hunger.second } );
    const auto warmth = display::temp_text_color( you );
    result.status_entries.push_back( { "保暖", warmth.first, warmth.second } );
    const auto weight = display::weight_text_color( you );
    result.status_entries.push_back( { "重量", weight.first, weight.second } );
    // Keep opposite limbs independent so damage to one side is never averaged away.
    for( const bodypart_str_id &id : { body_part_head, body_part_torso, body_part_arm_l,
                                     body_part_arm_r, body_part_leg_l, body_part_leg_r } ) {
        const bodypart_id part = id.id();
        const bool present = you.has_part( part, body_part_filter::equivalent );
        result.body_regions.push_back( { body_part_name( part ), present ? you.get_part_hp_cur( part ) : 0,
                                         present ? you.get_part_hp_max( part ) : 0,
                                         display::get_bodygraph_bp_color( you, part, bodygraph_var::hp ) } );
    }
    // The compact effect column sits immediately below the anatomical HP
    // figure. Match actual native effect IDs to tileset overlay art.
    for( const auto &reference : you.get_effects() ) {
        const effect &state = reference.get();
        const std::string id = state.get_id().str();
        if( id.empty() ) {
            continue;
        }
        const std::string tile = "overlay_effect_" + id;
        if( std::none_of( result.effect_icons.begin(), result.effect_icons.end(),
            [&]( const data::effect_icon & entry ) { return entry.tile_id == tile; } ) ) {
            result.effect_icons.push_back( { tile, state.disp_name() } );
        }
    }
    result.slots = fp3d_inventory::slot_names( you );
    result.hotbar_icons = fp3d_inventory::slot_icons( you );
    result.active_slot = fp3d_inventory::active_slot();
    for( const auto &message : Messages::recent_messages_with_color( 64 ) ) {
        std::string line = message.timestamp + "  " + remove_color_tags( message.text );
        std::replace( line.begin(), line.end(), '\n', ' ' );
        std::replace( line.begin(), line.end(), '\r', ' ' );
        result.messages.push_back( { std::move( line ), message.color } );
    }
    result.expanded.push_back( "力量 " + std::to_string( you.get_str() ) + " 敏捷 " +
                               std::to_string( you.get_dex() ) +
                               " 智力 " + std::to_string( you.get_int() ) + " 感知 " + std::to_string( you.get_per() ) );
    result.expanded.push_back( "饥饿 " + hunger.first + " 口渴 " + thirst.first );
    for( const auto &part : you.get_all_body_parts() ) {
        const int maximum = you.get_part_hp_max( part );
        if( maximum > 0 ) {
            result.expanded.push_back( body_part_name( part ) + " " + health_text( you, part ) );
        }
    }
    auto destination = you.get_active_mission_target();
    if( !you.omt_path.empty() ) {
        destination = you.omt_path.front();
    }
    if( !destination.is_invalid() ) {
        const auto offset = destination - you.pos_abs_omt();
        if( offset.x() != 0 || offset.y() != 0 || offset.z() != 0 ) {
            result.navigation_heading = std::atan2( static_cast<float>( offset.x() ),
                                                    static_cast<float>( -offset.y() ) ) * 180 / 3.14159265f;
            result.navigation_distance = square_dist( destination.xy(), you.pos_abs_omt().xy() );
            result.navigation_z = offset.z();
        }
    }
    if( show_navigation ) {
        const tripoint_abs_omt player = you.pos_abs_omt();
        for( const fp3d_map::compass_marker &marker : fp3d_map::compass_markers( player ) ) {
            const auto offset = marker.position - player;
            if( offset.x() == 0 && offset.y() == 0 ) {
                continue;
            }
            const float bearing = std::atan2( static_cast<float>( offset.x() ),
                                             static_cast<float>( -offset.y() ) ) * 180.0f / 3.14159265f;
            result.map_markers.push_back( { bearing,
                                            square_dist( marker.position.xy(), player.xy() ),
                                            marker.tint.red, marker.tint.green, marker.tint.blue } );
        }
    }
    return result;
}
}
#endif
