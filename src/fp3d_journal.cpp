#include "fp3d_journal.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "calendar.h"
#include "creature_tracker.h"
#include "game.h"
#include "map.h"
#include "messages.h"
#include "mission.h"
#include "npc.h"
#include "output.h"
namespace fp3d_journal
{
model capture( const avatar &you, map &here )
{
    model result;
    const auto append = [&]( const std::vector<mission *> &missions, int state ) {
        for( const mission *entry : missions ) {
            if( !entry ) {
                continue;
            }
            mission_entry row;
            row.id = entry->get_id();
            row.state = state;
            row.tracked = entry == you.get_active_mission();
            row.name = remove_color_tags( entry->name() );
            row.description = remove_color_tags( entry->get_description() );
            if( entry->has_target() ) {
                const auto target = entry->get_target();
                row.destination = "目的地距离 " + std::to_string( square_dist( target.xy(),
                                  you.pos_abs_omt().xy() ) ) +
                                  " 个大地图格 / 楼层差 " + std::to_string( target.z() - you.posz() );
            } else {
                row.destination = "目的地尚未确定";
            }
            if( entry->has_deadline() ) {
                row.deadline = entry->get_deadline() > calendar::turn ?
                               "剩余时间：" + to_string_clipped( entry->get_deadline() - calendar::turn ) :
                               "已到截止时间";
            }
            result.missions.push_back( std::move( row ) );
        }
    };
    append( you.get_active_missions(), 0 );
    append( you.get_completed_missions(), 1 );
    append( you.get_failed_missions(), 2 );
    for( const npc *person : g->get_npcs_if( [&]( const npc & candidate ) {
    return !candidate.is_dead_state() && candidate.posz() == you.posz() && you.sees( here, candidate );
    } ) ) {
        result.people.push_back( { person->pos_abs(), person->get_name(),
                                   Creature::get_attitude_ui_data( person->attitude_to( you ) ).first.translated(),
                                   square_dist( person->pos_bub( here ), you.pos_bub( here ) ),
                                   person->is_player_ally(), person->is_following(), person->getID().get_value() } );
    }
    std::sort( result.people.begin(), result.people.end(), []( const auto & a, const auto & b ) {
        return a.distance < b.distance || ( a.distance == b.distance && a.name < b.name );
    } );
    return result;
}
std::optional<tripoint_bub_ms> dialogue_target( const avatar &you, const map &here, int npc_id,
        const tripoint_abs_ms &expected_position )
{
    const auto position = here.get_bub( expected_position );
    if( npc_id <= 0 || you.is_dead_state() || you.is_blind() || !here.inbounds( position ) ||
        position.z() != you.posz() || square_dist( position, you.pos_bub( here ) ) > 1 ) {
        return std::nullopt;
    }
    const npc *person = get_creature_tracker().creature_at<npc>( position );
    if( !person || person->getID().get_value() != npc_id || person->is_dead_state() ||
        !you.sees( here, *person ) ) {
        return std::nullopt;
    }
    return position;
}
bool track( avatar &you, int mission_id )
{
    if( you.is_dead_state() ) {
        return false;
    }
    for( mission *entry : you.get_active_missions() ) {
        if( entry && entry->get_id() == mission_id ) {
            you.set_active_mission( *entry );
            return true;
        }
    }
    add_msg( m_info, "此任务已变化或不再进行中，请重新打开任务页。" );
    return false;
}
}
#endif
