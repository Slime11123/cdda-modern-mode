#include "fp3d_hud.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cmath>
#include "avatar.h"
#include "bodypart.h"
#include "catacharset.h"
#include "fp3d_inventory.h"
#include "item.h"
#include "item_location.h"
#include "output.h"
namespace fp3d_hud
{
namespace
{
bool show_navigation = true;
}
bool navigation_enabled()
{
    return show_navigation;
}
void set_navigation_enabled( bool enabled )
{
    show_navigation = enabled;
}

data capture( const avatar &you )
{
    data result;
    result.status = "生命 " + std::to_string( you.get_hp() ) + " | 耐力 " + std::to_string(
                        you.get_stamina() ) +
                    "/" + std::to_string( you.get_stamina_max() ) + " | " +
                    ( you.is_running() ? "奔跑" : you.is_crouching() ? "蹲姿" : "行走" );
    if( const auto weapon = you.get_wielded_item(); weapon && weapon->is_gun() ) {
        result.status += " | 弹药 " + std::to_string( weapon->ammo_remaining() );
    }
    const auto names = fp3d_inventory::slot_names();
    for( int row = 0; row < 2; ++row ) {
        std::string text;
        for( int n = row * 4; n < row * 4 + 4; ++n ) {
            text += ( n == fp3d_inventory::active_slot() ? "[" : " " ) + std::to_string( n + 1 ) + ":" +
                    utf8_truncate( names[n], 8 ) + ( n == fp3d_inventory::active_slot() ? "] " : "  " );
        }
        result.slots.push_back( std::move( text ) );
    }
    result.expanded.push_back( "力量 " + std::to_string( you.get_str() ) + " 敏捷 " +
                               std::to_string( you.get_dex() ) +
                               " 智力 " + std::to_string( you.get_int() ) + " 感知 " + std::to_string( you.get_per() ) );
    result.expanded.push_back( "饥饿 " + std::to_string( you.get_hunger() ) + " 口渴 " +
                               std::to_string( you.get_thirst() ) );
    for( const auto &part : you.get_all_body_parts() ) {
        const int maximum = you.get_part_hp_max( part );
        if( maximum > 0 ) {
            result.expanded.push_back( body_part_name( part ) + " " + std::to_string( you.get_part_hp_cur(
                                           part ) ) +
                                       "/" + std::to_string( maximum ) );
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
