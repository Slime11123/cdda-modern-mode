#include "fp3d_hud.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include "avatar.h"
#include "bodypart.h"
#include "catacharset.h"
#include "fp3d_inventory.h"
#include "item.h"
#include "item_location.h"
#include "calendar.h"
#include "display.h"
#include "debug.h"
#include "game.h"
#include "messages.h"
#include "mission.h"
#include "map.h"
#include "output.h"
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
    result.slots = fp3d_inventory::slot_names( you );
    result.hotbar_icons = fp3d_inventory::slot_icons( you );
    result.active_slot = fp3d_inventory::active_slot();
    for( const auto &message : Messages::recent_messages_with_color( 4 ) ) {
        std::string line = message.timestamp + "  " + remove_color_tags( message.text );
        std::replace( line.begin(), line.end(), '\n', ' ' );
        std::replace( line.begin(), line.end(), '\r', ' ' );
        result.messages.push_back( { utf8_truncate( line, 72 ), message.color } );
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
    return result;
}
}
#endif
