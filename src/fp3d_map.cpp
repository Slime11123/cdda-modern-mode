#include "fp3d_map.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <set>
#include <tuple>
#include <utility>
#include "calendar.h"
#include "overmap_ui.h"
#include "avatar.h"
#include "map_scale_constants.h"
#include "omdata.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
namespace fp3d_map
{
namespace
{
struct marker_palette_entry {
    const char *code;
    const char *name;
    marker_tint tint;
};
constexpr std::array<marker_palette_entry, marker_palette_size> colors = {{
        { "", "金色", { 255, 220, 80 } },
        { "r", "红色", { 244, 103, 92 } },
        { "g", "绿色", { 120, 220, 126 } },
        { "b", "蓝色", { 121, 177, 244 } },
        { "C", "青色", { 92, 221, 212 } },
        { "P", "粉色", { 246, 150, 205 } },
        { "W", "白色", { 245, 242, 223 } },
        { "br", "棕色", { 200, 145, 90 } }
    }
};
struct cached_marker_list {
    int layer = 9999;
    time_point last = calendar::turn_zero;
    bool dirty = true;
    std::vector<compass_marker> markers;
    std::optional<tripoint_abs_omt> nearest_to;
    std::vector<compass_marker> nearest;
};
cached_marker_list &marker_cache()
{
    static cached_marker_list cache;
    return cache;
}
}
const char *marker_color_name( int index )
{
    return colors[std::clamp( index, 0, marker_palette_size - 1 )].name;
}
int marker_color_index( std::string_view note )
{
    const nc_color native_color = std::get<1>( overmap_ui::get_note_display_info( note ) );
    for( int i = 1; i < marker_palette_size; ++i ) {
        if( get_note_color( colors[i].code ) == native_color ) {
            return i;
        }
    }
    // Imported native notes can use the darker variants of the same hues.
    for( const auto &alias : { std::pair<const char *, int>{ "R", 1 },
                               { "G", 2 }, { "B", 3 }, { "c", 4 }, { "m", 5 },
                               { "lg", 6 }, { "dg", 6 } } ) {
        if( get_note_color( alias.first ) == native_color ) {
            return alias.second;
        }
    }
    return 0;
}
marker_tint marker_color( std::string_view note )
{
    return colors[marker_color_index( note )].tint;
}
std::string marker_label( std::string_view note )
{
    const auto parsed = overmap_ui::get_note_display_info( note );
    const size_t start = std::min( note.size(), std::get<2>( parsed ) );
    return remove_color_tags( std::string( note.substr( start ) ) );
}
std::string format_marker( const std::string &label, int color_index )
{
    const int index = std::clamp( color_index, 0, marker_palette_size - 1 );
    const std::string code = colors[index].code;
    return ( code.empty() ? std::string() : code + ";" ) + "X:" + label;
}
std::vector<compass_marker> compass_markers( const tripoint_abs_omt &player )
{
    cached_marker_list &cache = marker_cache();
    if( cache.dirty || cache.layer != player.z() ||
        calendar::turn < cache.last || calendar::turn - cache.last > 5_minutes ) {
        cache.layer = player.z();
        cache.last = calendar::turn;
        cache.dirty = false;
        cache.markers.clear();
        cache.nearest_to.reset();
        // Native API only enumerates notes in overmaps already loaded by the game.
        // Cache the scan across HUD frames: large saves must not scan each render.
        for( const auto &note : overmap_buffer.get_all_notes( player.z() ) ) {
            cache.markers.push_back( { tripoint_abs_omt( note.first.x(), note.first.y(),
                                      player.z() ), marker_color( note.second ) } );
        }
    }
    if( cache.nearest_to && *cache.nearest_to == player ) {
        return cache.nearest;
    }
    cache.nearest_to = player;
    cache.nearest = cache.markers;
    const auto priority = [&]( const compass_marker &a, const compass_marker &b ) {
        return square_dist( a.position.xy(), player.xy() ) <
               square_dist( b.position.xy(), player.xy() );
    };
    const size_t visible = std::min( cache.nearest.size(), size_t{ 6 } );
    std::partial_sort( cache.nearest.begin(), cache.nearest.begin() + visible,
                       cache.nearest.end(), priority );
    cache.nearest.resize( visible );
    return cache.nearest;
}
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
                row.note = marker_label( row.note_text );
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
    marker_cache().dirty = true;
    return true;
}
}
#endif
