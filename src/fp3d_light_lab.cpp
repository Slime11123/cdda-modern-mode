#include "fp3d_light_lab.h"

#if defined( ENABLE_FP3D )
#include <algorithm>
#include <array>
#include "avatar.h"
#include "calendar.h"
#include "coordinates.h"
#include "item.h"
#include "map.h"
#include "mapdata.h"
#include "veh_appliance.h"
#include "veh_type.h" // vpart_info is needed by vehicle_part::info().
#include "vehicle.h"
#include "vpart_position.h"

namespace fp3d_light_lab
{
status provision( map &here, avatar &player )
{
    status out;
    // A copy of ShaderDaylightLab, never another world: choose a clear
    // 17x13 patch in the current reality bubble, then place the avatar there.
    const tripoint_bub_ms original = player.pos_bub();
    const std::array<point, 9> offsets = {{
            point( 0, 0 ), point( 20, 0 ), point( -20, 0 ),
            point( 0, 20 ), point( 0, -20 ), point( 22, 20 ),
            point( -22, -20 ), point( 22, -20 ), point( -22, 20 )
        }};
    tripoint_bub_ms center = original;
    bool room_found = false;
    for( const point &offset : offsets ) {
        const tripoint_bub_ms candidate = original + offset;
        bool available = true;
        for( int dx = -8; dx <= 8 && available; ++dx ) {
            for( int dy = -6; dy <= 6; ++dy ) {
                const tripoint_bub_ms tile = candidate + point( dx, dy );
                if( !here.inbounds( tile ) || here.veh_at( tile ) ) {
                    available = false;
                    break;
                }
            }
        }
        if( available ) {
            center = candidate;
            room_found = true;
            break;
        }
    }
    if( !room_found ) {
        return out;
    }
    for( int dx = -8; dx <= 8; ++dx ) {
        for( int dy = -6; dy <= 6; ++dy ) {
            const tripoint_bub_ms tile = center + point( dx, dy );
            const bool border = dx == -8 || dx == 8 || dy == -6 || dy == 6;
            here.ter_set( tile, ter_str_id( border ? "t_wall" : "t_floor" ) );
            here.furn_set( tile, furn_str_id( "f_null" ) );
        }
    }
    for( int y = -4; y <= -1; ++y ) {
        // Leave the energized lamps visible on the right.  The unpowered
        // comparison fixture stays behind the dividing wall on the left.
        here.ter_set( center + point( -2, y ), ter_str_id( "t_wall" ) );
    }
    // This is a real CDDA power network: battery appliance adjacent to
    // standing-lamp appliance. place_appliance() performs native grid merging
    // and cable connection. The second connected lamp starts switched OFF.
    const tripoint_bub_ms battery_pos = center + point( 4, -2 );
    const tripoint_bub_ms lit_pos = center + point( 3, -2 );
    const tripoint_bub_ms switched_off_pos = center + point( 4, -3 );
    const tripoint_bub_ms disconnected_pos = center + point( -4, -2 );

    const vpart_id battery_part( "ap_battery" );
    const vpart_id fixture_part( "ap_standing_lamp" );
    if( !place_appliance( here, battery_pos, battery_part, player ) ) {
        return out;
    }
    const auto battery = here.veh_at( battery_pos );
    if( battery ) {
        for( int i = 0; i < battery->vehicle().part_count(); ++i ) {
            vehicle_part &part = battery->vehicle().part( i );
            const int capacity = part.ammo_capacity( ammotype( "battery" ) );
            if( capacity > 0 ) {
                out.charged_kj = std::min( capacity, 100000 );
                part.ammo_set( itype_id( "battery" ), out.charged_kj );
                break;
            }
        }
    }
    out.powered_fixture = place_appliance( here, lit_pos, fixture_part, player );
    out.inactive_fixture = place_appliance( here, switched_off_pos, fixture_part, player );
    out.isolated_fixture = place_appliance( here, disconnected_pos, fixture_part, player );

    // Merged grids install a power_cord as the first visible part at a
    // location. The lamp is NOT necessarily veh_at(pos)->part_index().
    // Identify the real lamp by its original absolute mounting tile instead.
    const auto set_lamp_state = [&]( const tripoint_bub_ms &position,
                                    bool on ) {
        const auto vp = here.veh_at( position );
        if( !vp ) {
            return false;
        }
        vehicle &grid = vp->vehicle();
        for( int part_i = 0; part_i < grid.part_count(); ++part_i ) {
            vehicle_part &part = grid.part( part_i );
            if( !part.removed && !part.is_fake &&
                part.info().id.str() == "ap_standing_lamp" &&
                grid.mount_to_tripoint_abs( part.mount ) == here.get_abs( position ) ) {
                part.enabled = on;
                return part.is_light() && part.is_available() &&
                       part.enabled == on;
            }
        }
        return false;
    };
    out.powered_fixture = out.powered_fixture && set_lamp_state( lit_pos, true );
    out.inactive_fixture = out.inactive_fixture && set_lamp_state( switched_off_pos, false );
    out.isolated_fixture = out.isolated_fixture && set_lamp_state( disconnected_pos, true );
    if( const auto lit = here.veh_at( lit_pos ) ) {
        out.grid_kj = lit->vehicle().connected_battery_power_level( here ).first;
    }
    // Supply real chargeable, initially OFF items. The player can pick them
    // up, switch them on, point, compare and switch them back off.
    const tripoint_bub_ms item_pos = center + point( -2, 2 );
    item torch( itype_id( "flashlight" ), calendar::turn );
    item torch_cell( itype_id( "medium_battery_cell" ), calendar::turn );
    torch_cell.ammo_set( itype_id( "battery" ), 56 );
    torch.put_in( torch_cell, pocket_type::MAGAZINE_WELL );
    out.flashlight_charges = torch.ammo_remaining();
    here.add_item_or_charges( item_pos, std::move( torch ) );
    item phone( itype_id( "smart_phone" ), calendar::turn );
    phone.ammo_set( itype_id( "battery" ), 50 );
    out.phone_charges = phone.ammo_remaining();
    here.add_item_or_charges( item_pos, std::move( phone ) );
    item spare_cell( itype_id( "medium_battery_cell" ), calendar::turn );
    spare_cell.ammo_set( itype_id( "battery" ), 56 );
    here.add_item_or_charges( item_pos, std::move( spare_cell ) );
    here.add_item_or_charges( item_pos, item( itype_id( "battery" ), calendar::turn ) );

    player.setpos( here, center );
    here.invalidate_map_cache( center.z() );
    here.build_map_cache( center.z() );
    out.ready = out.charged_kj > 0 && out.grid_kj > 0 &&
                out.flashlight_charges > 0 && out.phone_charges > 0 &&
                out.powered_fixture && out.inactive_fixture &&
                out.isolated_fixture;
    return out;
}
} // namespace fp3d_light_lab
#endif
