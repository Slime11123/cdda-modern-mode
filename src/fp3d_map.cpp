#include "fp3d_map.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <set>
#include <utility>
#include "avatar.h"
#include "map_scale_constants.h"
#include "omdata.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
namespace fp3d_map
{
model capture( const avatar &you, const tripoint_abs_omt &center, int radius )
{
    model result;
    result.player = you.pos_abs_omt();
    result.center = tripoint_abs_omt( center.x(), center.y(),
                                      std::clamp( center.z(), -OVERMAP_DEPTH, OVERMAP_HEIGHT ) );
    result.radius = std::clamp( radius, 4, 18 );
    auto destination = you.get_active_mission_target();
    if( !you.omt_path.empty() ) {
        destination = you.omt_path.front();
    }
    if( !destination.is_invalid() ) {
        result.objective = destination;
    }
    const std::set<tripoint_abs_omt> route( you.omt_path.begin(), you.omt_path.end() );
    for( int dy = -result.radius; dy <= result.radius; ++dy ) {
        for( int dx = -result.radius; dx <= result.radius; ++dx ) {
            cell row;
            row.position = result.center + tripoint_rel_omt( dx, dy, 0 );
            const auto vision = overmap_buffer.seen( row.position );
            if( vision != om_vision_level::unseen ) {
                const auto terrain = overmap_buffer.ter_existing( row.position );
                if( terrain != oter_id() ) {
                    row.known = true;
                    row.name = remove_color_tags( terrain->get_name( vision ) );
                    row.symbol = terrain->get_symbol( vision );
                    row.tileset_id = terrain->get_tileset_id( vision );
                    row.color = terrain->get_color( vision );
                }
            }
            row.editable = static_cast<bool>( overmap_buffer.get_existing_om_global( row.position ) );
            if( overmap_buffer.has_note( row.position ) ) {
                row.note_text = overmap_buffer.note( row.position );
                row.note = remove_color_tags( row.note_text );
            }
            row.route = route.count( row.position ) != 0;
            row.objective = result.objective && row.position == *result.objective;
            result.cells.push_back( std::move( row ) );
        }
    }
    return result;
}
bool write_note( const tripoint_abs_omt &position, const std::string &text, std::string &error )
{
    error.clear();
    if( position.z() < -OVERMAP_DEPTH || position.z() > OVERMAP_HEIGHT ) {
        error = "此楼层不能添加标记。";
        return false;
    }
    const auto existing = overmap_buffer.get_existing_om_global( position );
    if( !existing ) {
        error = "此区域尚未载入，不能在此添加标记。";
        return false;
    }
    if( text.empty() ) {
        if( existing.om->has_note( existing.local ) ) {
            existing.om->delete_note( existing.local );
        }
    } else {
        existing.om->add_note( existing.local, text );
    }
    return true;
}
}
#endif
