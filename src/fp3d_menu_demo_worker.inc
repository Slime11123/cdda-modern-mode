// Dedicated, isolated menu-demo process. The main menu never invokes
// map::generate, game::start_game or do_turn in the user's game process.
// All of these are native CDDA routines operating inside --userdir's
// private menu-demo-sandbox, with AUTOSAVE disabled and no gameplay save.
#if defined(TILES) && defined(ENABLE_FP3D)
#include "calendar.h"
#include "creature_tracker.h"
#include "cursesport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <sstream>

namespace
{
// One scene keeps running long enough to read as continuous footage, but
// switches before the action degenerates into a static pile-up.
constexpr int menu_demo_scene_turns = 96;
// Native CDDA turn cadence for the demonstration: one vanilla game turn per
// step, paced at roughly the original real-time turn duration.
constexpr int menu_demo_turn_ms = 150;
constexpr int menu_demo_stat_every = 12;

std::string menu_demo_vehicle_report( map &here, const tripoint_bub_ms &anchor )
{
    std::ostringstream os;
    bool first = true;
    for( const wrapped_vehicle &wrapped : here.get_vehicles() ) {
        vehicle *veh = wrapped.v;
        if( veh == nullptr ) {
            continue;
        }
        const tripoint_bub_ms vpos = veh->pos_bub( here );
        if( rl_dist( vpos, anchor ) > 80 ) {
            continue;
        }
        if( !first ) {
            os << "|";
        }
        first = false;
        os << vpos.x() << "," << vpos.y() << ",v" << veh->velocity;
    }
    return first ? std::string( "none" ) : os.str();
}
} // namespace

int run_fp3d_native_menu_worker( const std::string &frame_output, const std::string &tileset,
                                 int canvas_width, int canvas_height )
{
    const std::filesystem::path sandbox( PATH_INFO::user_dir() );
    // Refuse to start the worker unless explicitly given a private world
    // directory; accidental invocation must NEVER open a user's normal world.
    if( sandbox.generic_string().find( "menu-demo-sandbox" ) == std::string::npos ) {
        std::cerr << "Refusing to start demo outside isolated userdir\n";
        return 74;
    }
    try {
        auto trace = [&]( const std::string &label ) {
            std::ofstream stream( sandbox / "menu_worker_trace.txt", std::ios::app );
            stream << label << '\n';
        };
        trace( "worker_initialize" );
        std::filesystem::create_directories( frame_output );
        if( !tileset.empty() ) {
            get_options().get_option( "TILES" ).setValue( tileset );
        }
        get_options().get_option( "AUTOSAVE" ).setValue( "false" );
        // Native CDDA real-time turns, no user input or scripted sprite movement.
        get_options().get_option( "TURN_DURATION" ).setValue( "0.12" );
        // The demonstration must never be interrupted by safety prompts.
        get_options().get_option( "SAFEMODE" ).setValue( "false" );
        get_options().get_option( "AUTOSAFEMODE" ).setValue( "false" );
        get_options().get_option( "SOUND_ENABLED" ).setValue( "false" );
        world_generator->init();
        std::vector<mod_id> mods = { mod_id( "dda" ) };
        WORLD *demo_world = world_generator->make_new_world( "NativeMenuDemo", mods );
        if( !demo_world ) {
            std::cerr << "Menu-demo world creation failed\n";
            return 75;
        }
        world_generator->set_active_world( demo_world );
        g->setup();
        trace( "game_setup_finished" );
        if( !tileset.empty() ) {
            get_options().get_option( "TILES" ).setValue( tileset );
        }
        load_tileset();
        avatar &actor = get_avatar();
        actor = avatar();
        if( !actor.create( character_type::NOW ) || !main_menu::start_native_demo_game( *g ) ) {
            std::cerr << "Menu-demo character/world generation failed\n";
            return 76;
        }
        // The title camera is not a real survivor. Keep it safe so game-over
        // prompts cannot interrupt the isolated automated demonstration.
        actor.set_mutation( trait_id( "DEBUG_NODMG" ) );
        // The required avatar is only a spectator/camera anchor. It cannot
        // attract zombies or interfere with NPC combat, and the tile renderer
        // skips drawing it entirely (see cata_tiles::draw_critter_at).
        actor.set_mutation( trait_id( "DEBUG_CLOAK" ) );
        // The demonstration must never show unseen squares. The strongest
        // vanilla debug clairvoyance sees through walls and floor for 40 tiles
        // (Character::sees returns true inside that range), which covers the
        // whole camera track and the loaded reality bubble.
        actor.set_mutation( trait_id( "DEBUG_CLAIRVOYANCE" ) );
        set_native_menu_worker_output( frame_output );
        // Target pixel canvas of the title screen. The terrain viewport is
        // enlarged past this size and the publisher centre-crops to it, so the
        // streamed frames are 1:1 with what the title screen will blit.
        set_native_menu_canvas( canvas_width, canvas_height );
        menu_clear_worker_frames( frame_output );
        trace( "started_world" );

        // Enlarge the hidden demo window so its native terrain viewport is at
        // least the requested canvas. The extra margin absorbs the sidebar and
        // the message log, and the centre crop removes them again.
        {
            const int want_w = canvas_width > 0 ? canvas_width : 1280;
            const int want_h = canvas_height > 0 ? canvas_height : 720;
            const int cell_w = std::max( 1, fontwidth );
            const int cell_h = std::max( 1, fontheight );
            const int want_cells_w = want_w / cell_w + 64;
            const int want_cells_h = want_h / cell_h + 4;
            resize_term( want_cells_w, want_cells_h );
            std::ostringstream os;
            os << "canvas_request=" << want_w << "x" << want_h
               << " cells=" << want_cells_w << "x" << want_cells_h
               << " cell_px=" << cell_w << "x" << cell_h
               << " terrain_term=" << TERRAIN_WINDOW_TERM_WIDTH << "x" << TERRAIN_WINDOW_TERM_HEIGHT
               << " terrain_px=" << TERRAIN_WINDOW_TERM_WIDTH * cell_w << "x"
               << TERRAIN_WINDOW_TERM_HEIGHT * cell_h;
            trace( os.str() );
        }

        const shared_ptr_fast<ui_adaptor> ui = g->create_or_get_main_ui_adaptor();
        g->mark_main_ui_adaptor_resize();
        ui_manager::screen_resized();
        {
            std::ostringstream os;
            os << "canvas_after_resize terrain_term=" << TERRAIN_WINDOW_TERM_WIDTH << "x"
               << TERRAIN_WINDOW_TERM_HEIGHT << " terrain_px="
               << TERRAIN_WINDOW_TERM_WIDTH * std::max( 1, fontwidth ) << "x"
               << TERRAIN_WINDOW_TERM_HEIGHT * std::max( 1, fontheight );
            trace( os.str() );
        }

        // Each scene is a different part of the sandbox's *real* overmap.
        // Spawn via vanilla vehicle, NPC and monster APIs, then advance turns
        // with the normal do_turn() logic. No invented paper-doll or car model.
        const mtype_id zombie( "mon_zombie" );
        // Fixed reference point: every scene is placed relative to this, never to
        // the avatar (whose position is what the scenes keep changing), so the
        // offsets cannot accumulate or drift back onto a previous scene.
        const tripoint_abs_omt scene_base = actor.pos_abs_omt();
        for( int scene = 0; ; ++scene ) {
            // QA-only: a dedicated skylight scene for visual verification.
            // Never active outside an explicit isolated capture request.
            static const bool qa_skylight_scene = []() {
                const char *v = std::getenv( "CDDA_FP3D_QA_SKYLIGHT_SCENE" );
                return v && std::strcmp( v, "1" ) == 0;
            }();
            const int kind = qa_skylight_scene ? 7 : scene % 7;
            trace( "scene_" + std::to_string( scene ) + "_begin_kind_" + std::to_string( kind ) );
            // Golden-ratio style walk over a +-60 omt square: every scene lands in
            // a different part of the overmap, so its map chunks are generated fresh
            // instead of showing the previous scene again.
            const tripoint_abs_omt origin(
                scene_base.x() + ( ( scene * 47 ) % 121 ) - 60,
                scene_base.y() + ( ( scene * 89 ) % 121 ) - 60, 0 );
            trace( "scene_" + std::to_string( scene ) + "_at_" +
                   std::to_string( origin.x() ) + "_" + std::to_string( origin.y() ) );
            // Mi-go territory teleports to the real camp when one is nearby; every
            // other scene sits on a road.
            bool placed_on_camp = false;
            // Set when the town scene managed to land on a street.
            bool placed_on_town = false;
            if( kind == 5 ) {
                const tripoint_abs_omt camp = overmap_buffer.find_closest(
                                                 origin, "mi-go_camp2", 60, false );
                if( !camp.is_invalid() ) {
                    g->place_player_overmap( camp );
                    placed_on_camp = true;
                    trace( "scene_migo_camp" );
                }
            }
            if( !placed_on_camp && kind == 6 ) {
                // Town showcase: find the nearest town and drop onto one of its streets,
                // so the scene shows real buildings and pavements instead of open road.
                const city_reference town = overmap_buffer.closest_city(
                                                project_to<coords::sm>( origin ) );
                tripoint_abs_omt search_from = origin;
                if( town ) {
                    search_from = project_to<coords::omt>( town.abs_sm_pos );
                    trace( "scene_town_size_" +
                           std::to_string( town.city != nullptr ? town.city->size : -1 ) +
                           "_pop_" +
                           std::to_string( town.city != nullptr ? town.city->population : 0 ) );
                }
                // Land in the middle of town rather than on the nearest road: the closest
                // road to a town is usually the highway at its edge, which shows nothing
                // but tarmac and grass. The centre is inside the built-up area.
                if( town ) {
                    g->place_player_overmap( search_from );
                    placed_on_town = true;
                    trace( "scene_town_center" );
                } else {
                    const tripoint_abs_omt street = overmap_buffer.find_closest(
                                                        origin, "road", 16, false );
                    if( !street.is_invalid() ) {
                        g->place_player_overmap( street );
                        placed_on_town = true;
                        trace( "scene_town_road" );
                    }
                }
            }
            if( !placed_on_camp ) {
                const tripoint_abs_omt road = overmap_buffer.find_closest(
                                                 origin, "road", 16, false );
                g->place_player_overmap( road.is_invalid() ? origin : road );
            }
            map &here = get_map();
            // Paved only: TFLAG_ROAD also marks dirt trails and gravel, and a car
            // cruising through a forest is not the "steady street drive" this scene
            // is supposed to show.
            auto is_paved = [&]( const tripoint_bub_ms &p ) -> bool {
                const std::string tid = here.ter( p ).id().str();
                return tid.rfind( "t_pavement", 0 ) == 0 || tid.rfind( "t_sidewalk", 0 ) == 0;
            };
            // Nothing from the previous scene may leak into this one: whatever
            // monsters and NPC spawns are left inside the bubble are dropped here,
            // before this scene creates its own cast.
            {
                int leftover = 0;
                for( monster &mon : g->all_monsters() ) {
                    static_cast<void>( mon );
                    ++leftover;
                }
                // Deliberately NOT clearing monsters: this area was just generated,
                // so its own horde is part of the new scene, not a leftover.
                std::vector<character_id> strays;
                for( npc &who : g->all_npcs() ) {
                    strays.push_back( who.getID() );
                }
                for( const character_id &stray : strays ) {
                    g->remove_npc( stray );
                }
                trace( "scene_" + std::to_string( scene ) + "_fresh_cleared_" +
                       std::to_string( leftover ) + "_npcs_" +
                       std::to_string( strays.size() ) );
            }
            // Each scene owns the time of day it wants: noon by default, and the
            // night scene below switches to 22:00 so the street lamps do the work.
            {
                const time_duration past_midnight = time_past_midnight( calendar::turn );
                const bool want_night = ( kind == 4 );
                calendar::turn = calendar::turn - past_midnight +
                                 ( want_night ? 22_hours : 12_hours );
                set_native_menu_night( want_night );
            }
            // Action anchor. The spectator avatar stands here (it is never
            // drawn); every scene prop is spawned around this point so the
            // camera has a stable stage to point at.
            const tripoint_bub_ms stage = actor.pos_bub();
            vehicle *focus_vehicle = nullptr;
            character_id driver_id;
            // Direction index the driving scene keeps heading in (0=+x 1=-x 2=+y 3=-y).
            int car_dir_cursor = scene % 4;
            // The two ends of the verified-clear stretch the car patrols between.
            tripoint_abs_ms car_wp_a;
            tripoint_abs_ms car_wp_b;
            tripoint_abs_ms car_target;
            std::vector<tripoint_bub_ms> fight_hint;

            // Place the demonstration car on a stretch of road that is actually
            // clear and point it down that stretch. Dropping it wherever the player
            // happened to stand produced a car boxed in by wrecks that could only
            // turn around on the spot.
            // Find the longest clear straight run anywhere in the neighbourhood and
            // put the car at its start, facing along it. Searching only around the
            // spectator was unreliable: place_player_overmap sometimes drops them
            // inside a wreck or a doorway, and the car could never get going.
            auto place_demo_car = [&]( const tripoint_bub_ms &origin,
                                       int &dir_cursor_out, tripoint_abs_ms &wp_a,
                                       tripoint_abs_ms &wp_b,
                                       bool lane_along_y = false,
                                       int reach = 12, int min_run = 20 ) -> vehicle * {
                const std::array<point, 4> dirs = { point( 1, 0 ), point( -1, 0 ),
                                                    point( 0, 1 ), point( 0, -1 )
                                                  };
                int best_run = 0;
                int best_dir = -1;
                tripoint_bub_ms best_start;
                for( int dy = -reach; dy <= reach; dy += 3 ) {
                    for( int dx = -reach; dx <= reach; dx += 3 ) {
                        const tripoint_bub_ms base_probe = origin + point( dx, dy );
                        if( !here.inbounds( base_probe ) || !here.passable( base_probe ) ||
                            here.veh_at( base_probe ) ||
                            get_creature_tracker().creature_at( base_probe, true ) ) {
                            continue;
                        }
                        // Wheels need real road: on other terrain vehicle::act_on_map
                        // sees no traction and stops the car on the spot.
                        if( !is_paved( base_probe ) ) {
                            continue;
                        }
                        for( int di = 0; di < 4; ++di ) {
                            // Only drive along the road, never across it: a wide road
                            // also offers a long paved run sideways, and picking that one
                            // put the car broadside to the street it should follow.
                            if( ( di < 2 ) == lane_along_y ) {
                                continue;
                            }
                            const point dir = dirs[di];
                            int run = 0;
                            for( int i = 1; i <= 28; ++i ) {
                                const tripoint_bub_ms probe( base_probe + dir * i );
                                if( !here.inbounds( probe ) || !here.passable( probe ) ||
                                    here.veh_at( probe ) ||
                                    get_creature_tracker().creature_at( probe, true ) ) {
                                    break;
                                }
                                // The whole lane has to be paved, not just its start:
                                // a run that leaves the road ends up in a field.
                                if( !is_paved( probe ) ) {
                                    break;
                                }
                                // The car is three tiles wide: a one-tile line is
                                // not enough, it would scrape the kerb and stall.
                                const point side( dir.x != 0 ? 0 : 1, dir.x != 0 ? 1 : 0 );
                                bool blocked_side = false;
                                for( int w = -1; w <= 1 && !blocked_side; ++w ) {
                                    if( w == 0 ) {
                                        continue;
                                    }
                                    const tripoint_bub_ms flank( probe + side * w );
                                    if( !here.inbounds( flank ) || !here.passable( flank ) ||
                                        here.veh_at( flank ) ) {
                                        blocked_side = true;
                                    }
                                }
                                if( blocked_side ) {
                                    break;
                                }
                                ++run;
                            }
                            if( run > best_run ) {
                                best_run = run;
                                best_dir = di;
                                best_start = base_probe;
                            }
                        }
                    }
                }
                if( best_run < min_run || best_dir < 0 ) {
                    return nullptr;
                }
                dir_cursor_out = best_dir;
                dir_cursor_out = best_dir;
                // Clear the wreck that gave the corridor its name: a road full of
                // parked hulks is exactly what makes the autopilot brake forever.
                {
                    const point corridor_dir = dirs[best_dir];
                    // Clear a couple of squares *behind* the start too: the hull extends
                    // past the pivot, and a parked car tucked in there wedges it so that
                    // it holds speed but never actually moves.
                    for( int i = -2; i <= best_run + 2; ++i ) {
                        const tripoint_bub_ms probe( best_start + corridor_dir * i );
                        for( int w = -1; w <= 1; ++w ) {
                            const tripoint_bub_ms flank( probe + point( corridor_dir.x != 0 ? 0 : w,
                                                        corridor_dir.x != 0 ? w : 0 ) );
                            const optional_vpart_position other = here.veh_at( flank );
                            if( other && &other->vehicle() != nullptr ) {
                                here.destroy_vehicle( &other->vehicle() );
                            }
                        }
                    }
                }
                const point dir = dirs[best_dir];
                const units::angle facing = ( dir.x != 0 ) ? 0_degrees : 90_degrees;
                trace( "scene_dir_car_run_" + std::to_string( best_run ) );
                // Aim at both ends of the stretch; the caller alternates them.
                wp_a = here.get_abs( best_start + dir * 3 );
                wp_b = here.get_abs( best_start + dir * std::max( 6, best_run - 2 ) );
                return here.add_vehicle( vproto_id( "car" ), best_start, facing, 80, 0 );
            };

            // Seat a survivor in a free boardable tile of this vehicle. Returns
            // the boarded NPC's id, or an invalid id when it could not be done.
            auto seat_demo_driver = [&]( vehicle *veh ) -> character_id {
                if( veh == nullptr ) {
                    return character_id();
                }
                // A seat tile of this very vehicle.
                const tripoint_bub_ms car_pos = veh->pos_bub( here );
                tripoint_bub_ms seat_pos;
                bool have_seat = false;
                for( int dx = -2; dx <= 2 && !have_seat; ++dx ) {
                    for( int dy = -2; dy <= 2 && !have_seat; ++dy ) {
                        const tripoint_bub_ms probe = car_pos + point( dx, dy );
                        const optional_vpart_position vp = here.veh_at( probe );
                        if( vp && vp->part_with_feature( VPFLAG_BOARDABLE, true ) ) {
                            seat_pos = probe;
                            have_seat = true;
                        }
                    }
                }
                if( !have_seat ) {
                    return character_id();
                }
                // Spawn on a free neighbour tile: spawning straight onto a vehicle
                // tile pops a debug dialog, which would block the worker.
                tripoint_bub_ms spawn_at;
                bool have_spawn = false;
                for( int dx = -3; dx <= 3 && !have_spawn; ++dx ) {
                    for( int dy = -3; dy <= 3 && !have_spawn; ++dy ) {
                        const tripoint_bub_ms probe = seat_pos + point( dx, dy );
                        if( !here.passable( probe ) || here.veh_at( probe ) ) {
                            continue;
                        }
                        if( get_creature_tracker().creature_at( probe, true ) ) {
                            continue;
                        }
                        spawn_at = probe;
                        have_spawn = true;
                    }
                }
                if( !have_spawn ) {
                    return character_id();
                }
                std::string unique_name;
                std::vector<trait_id> traits;
                if( !g->spawn_npc( spawn_at, npc_template_id( "survivor_camper" ),
                                   unique_name, traits, std::nullopt ) ) {
                    return character_id();
                }
                for( npc &guy : g->all_npcs() ) {
                    if( guy.pos_bub() == spawn_at ) {
                        // Impacts otherwise hurl the driver out of the seat.
                        guy.set_mutation( trait_id( "DEBUG_NODMG" ) );
                        here.board_vehicle( seat_pos, &guy );
                        return guy.getID();
                    }
                }
                return character_id();
            };

            if( kind == 7 ) {
                // QA-only skylight demonstration: a real room whose roof is
                // native t_glass_roof (the walkable skylight), so the
                // translucent pane can be checked inside the actual game.
                for( int dy = -5; dy <= 5; ++dy ) {
                    for( int dx = -5; dx <= 5; ++dx ) {
                        const tripoint_bub_ms p = stage + point( dx, dy );
                        const bool edge = std::abs( dx ) == 5 || std::abs( dy ) == 5;
                        const tripoint_bub_ms above( p.xy(), p.z() + 1 );
                        here.ter_set( above, ter_str_id( "t_flat_roof" ) );
                        if( edge ) {
                            here.ter_set( p, ter_str_id( "t_wall" ) );
                            here.ter_set( above, ter_str_id( "t_wall" ) );
                        } else {
                            here.ter_set( p, ter_str_id( "t_floor" ) );
                        }
                    }
                }
                // The skylight itself covers the middle of the room.
                for( int dy = -2; dy <= 2; ++dy ) {
                    for( int dx = -2; dx <= 2; ++dx ) {
                        here.ter_set( tripoint_bub_ms( ( stage + point( dx, dy ) ).xy(),
                                                       stage.z() + 1 ), ter_str_id( "t_glass_roof" ) );
                    }
                }
                actor.setpos( here, stage + point( 0, 4 ) );
                trace( "scene_skylight_built" );
            } else if( kind == 0 ) {
                // Parked, vanilla-template cars along the street.
                here.add_vehicle( vproto_id( "car" ), stage + point( 7, 2 ),
                                  0_degrees, -1, 1 );
                here.add_vehicle( vproto_id( "car" ), stage + point( -8, -4 ),
                                  90_degrees, -1, 1 );
                for( int n = 0; n < 5; ++n ) {
                    g->place_critter_around( zombie, stage + point( 6 + n, -6 ), 2 );
                }
            } else if( kind == 1 ) {
                // NPC melee against a zombie group: original AI and combat.
                std::string unique_name;
                std::vector<trait_id> traits;
                g->spawn_npc( stage + point( -2, -2 ), npc_template_id( "survivor_camper" ),
                              unique_name, traits, std::nullopt );
                g->spawn_npc( stage + point( 2, -2 ), npc_template_id( "survivor_camper" ),
                              unique_name, traits, std::nullopt );
                for( int n = 0; n < 3; ++n ) {
                    g->place_critter_around( zombie, stage + point( 5, 3 + n ), 2 );
                    fight_hint.push_back( stage + point( 5, 3 + n ) );
                }
            } else if( kind == 2 ) {
                // A survivor drives the car. The NPC is seated through the
                // original boarding path and the vehicle is steered by the
                // original patrol autopilot (a driverless car loses its speed:
                // vehicle::gain_moves only applies thrust under player control,
                // following, or patrolling).
                // Which way does this street actually run? Measure the paved extent in
                // each of the four directions (the local area may be an intersection,
                // where every direction looks short) and follow the longest one.
                bool lane_along_y = false;
                {
                    int ext[4] = { 0, 0, 0, 0 };  // +x, -x, +y, -y
                    const point dirs4[4] = { point( 1, 0 ), point( -1, 0 ),
                                             point( 0, 1 ), point( 0, -1 ) };
                    for( int d = 0; d < 4; ++d ) {
                        for( int i = 1; i <= 60; ++i ) {
                            const tripoint_bub_ms probe = stage + dirs4[d] * i;
                            if( !here.inbounds( probe ) ||
                                !is_paved( probe ) ) {
                                break;
                            }
                            ext[d] = i;
                        }
                    }
                    const int best_x = std::max( ext[0], ext[1] );
                    const int best_y = std::max( ext[2], ext[3] );
                    lane_along_y = best_y > best_x;
                    trace( "scene_lane_axis_" + std::string( lane_along_y ? "y" : "x" ) +
                           "_x" + std::to_string( best_x ) + "_y" + std::to_string( best_y ) );
                }
                focus_vehicle = place_demo_car( stage, car_dir_cursor, car_wp_a, car_wp_b,
                                                lane_along_y, 12, 20 );
                if( focus_vehicle == nullptr ) {
                    // Some road tiles have no long straight stretch nearby; widen the
                    // search and accept a shorter lane rather than showing an empty road.
                    focus_vehicle = place_demo_car( stage, car_dir_cursor, car_wp_a, car_wp_b,
                                                    lane_along_y, 26, 14 );
                    trace( "scene_" + std::to_string( scene ) + "_car_wide_search" );
                }
                if( focus_vehicle ) {
                    focus_vehicle->engine_on = true;
                    focus_vehicle->autopilot_on = true;
                    focus_vehicle->is_patrolling = true;
                    focus_vehicle->cruise_velocity = 400;
                    focus_vehicle->velocity = 320;
                    driver_id = seat_demo_driver( focus_vehicle );
                    // Head for the far end of the verified stretch on turn one.
                    car_target = car_wp_b;
                    focus_vehicle->set_autopilot_waypoint( car_target );
                    trace( "scene_" + std::to_string( scene ) + "_driver_seated_" +
                           ( driver_id.is_valid() ? "yes" : "no" ) );
                }
                // No zombies are spawned on this scene: the point of it is the
                // drive itself, and the autopilot brakes for anything standing in
                // its lane (the surrounding city already supplies background life).
            } else if( kind == 3 ) {
                // NPC shooting at approaching zombies; visible firearms.
                std::string unique_name;
                std::vector<trait_id> traits;
                g->spawn_npc( stage + point( -3, -3 ), npc_template_id( "NPC_gun_store_survivor" ),
                              unique_name, traits, std::nullopt );
                g->spawn_npc( stage + point( 3, -3 ), npc_template_id( "NPC_gun_store_survivor" ),
                              unique_name, traits, std::nullopt );
                for( int n = 0; n < 4; ++n ) {
                    g->place_critter_around( zombie, stage + point( 0, 6 + n ), 2 );
                    fight_hint.push_back( stage + point( 0, 6 + n ) );
                }
            } else if( kind == 4 ) {
                // Night in town: the street lamps are the only light and the dead
                // drift along the road. No NPCs, so it reads as an empty street.
                for( int n = 0; n < 12; ++n ) {
                    const point offset( -11 + ( n % 6 ) * 4, -3 + ( n / 6 ) * 6 );
                    g->place_critter_around( zombie, stage + offset, 3 );
                }
                for( int n = 0; n < 4; ++n ) {
                    g->place_critter_around( zombie, stage + point( -6 + n * 4, 8 ), 2 );
                }
            } else if( kind == 5 ) {
                // Mi-go territory: their camp, their guards, their slaves. When no
                // camp could be found nearby we still stage a group of mi-go so the
                // scene reads as their turf.
                const mtype_id migo( "mon_mi_go" );
                const mtype_id migo_guard( "mon_mi_go_guard" );
                // The slaver casts a beam that can freeze a headless worker waiting
                // on something that only a real client would dismiss.
                if( !placed_on_camp ) {
                    g->place_critter_around( migo_guard, stage + point( 4, -3 ), 2 );
                    g->place_critter_around( migo_guard, stage + point( -4, -3 ), 2 );
                }
                for( int n = 0; n < 5; ++n ) {
                    g->place_critter_around( migo, stage + point( -8 + n * 4, 4 ), 3 );
                }
                // (no slaver: see note above)
                for( int n = 0; n < 3; ++n ) {
                    g->place_critter_around( zombie, stage + point( -6 + n * 6, 9 ), 3 );
                }
            } else {
                // Town: streets, buildings and people. Monsters are cleared so the town
                // reads as a surviving settlement instead of a battlefield.
                g->clear_zombies();
                const std::array<point, 10> walkers = {
                    point( -6, 0 ), point( -3, 2 ), point( 0, -2 ), point( 3, 3 ),
                    point( 6, -1 ), point( -8, -4 ), point( 8, 4 ), point( 2, -6 ),
                    point( -2, 6 ), point( 10, 0 )
                };
                const std::array<std::string, 4> town_folk = {
                    "NPC_gun_store_survivor", "NPC_park_survivor",
                    "NPC_homeless_survivor", "NPC_evac_shelter_survivor"
                };
                for( int n = 0; n < 10; ++n ) {
                    std::string folk_name;
                    std::vector<trait_id> folk_traits;
                    g->spawn_npc( stage + walkers[n],
                                  npc_template_id( town_folk[n % 4] ),
                                  folk_name, folk_traits, std::nullopt );
                }
                trace( "scene_" + std::to_string( scene ) + "_town_spawned_" +
                       ( placed_on_town ? "street" : "road" ) );

            {
                std::ostringstream geo;
                geo << "scene_" << scene << "_geo map=" << MAPSIZE_X << "x" << MAPSIZE_Y
                    << " avatar_local=" << actor.pos_bub( here ).x() << "_"
                    << actor.pos_bub( here ).y();
                trace( geo.str() );
            }
            }


            trace( "scene_" + std::to_string( scene ) + "_enter_turn_loop" );
            int car_stuck_turns = 0;
            int step = 0;
            // The driving scene is short on purpose: get the car rolling, watch it
            // cruise for a dozen seconds, then move on before it runs out of road.
            const int scene_turns = ( kind == 2 ) ? 80 : menu_demo_scene_turns;
            bool scene_done = false;
            const tripoint_bub_ms base = actor.pos_bub();
            Uint32 last_turn_tick = SDL_GetTicks();

            // ---- camera -------------------------------------------------------------
            // The camera is a function of the wall clock, evaluated once per published
            // frame. It is registered as the frame hook, so it also advances while a
            // vanilla animation loop (projectiles, hits) redraws the screen on its own
            // instead of returning to this while. That is what used to freeze the
            // backdrop for the whole duration of a bullet in flight.
            float cam_x = static_cast<float>( base.x() );
            float cam_y = static_cast<float>( base.y() );
            bool cam_along_x = true;
            const float cam_phase0 = static_cast<float>( scene ) * 1.7f;
            const Uint32 cam_t0 = last_turn_tick;
            Uint32 last_cam_hook = cam_t0;
            float cam_pan_x = 0.0f;
            float cam_pan_y = 0.0f;
            {
                int clear_x = 0;
                int clear_y = 0;
                for( int i = 1; i <= 20; ++i ) {
                    if( here.passable( base + point( i, 0 ) ) &&
                        here.passable( base + point( -i, 0 ) ) ) {
                        clear_x = i;
                    } else {
                        break;
                    }
                }
                for( int i = 1; i <= 20; ++i ) {
                    if( here.passable( base + point( 0, i ) ) &&
                        here.passable( base + point( 0, -i ) ) ) {
                        clear_y = i;
                    } else {
                        break;
                    }
                }
                cam_along_x = clear_x >= clear_y;
                trace( "scene_" + std::to_string( scene ) + "_cam_axis_" +
                       ( cam_along_x ? "x" : "y" ) + "_clear_" +
                       std::to_string( std::max( clear_x, clear_y ) ) );
            }
            auto cam_tick = [&]() {
                const Uint32 now = SDL_GetTicks();
                const float dt = std::clamp(
                                     static_cast<float>( now - last_cam_hook ) / 1000.0f, 0.0f, 0.25f );
                last_cam_hook = now;
                if( kind == 2 && focus_vehicle != nullptr ) {
                    // The driving scene follows the car, never the player.
                    const tripoint_bub_ms car_pos = focus_vehicle->pos_bub( here );
                    const float follow = std::min( 1.0f, dt * 2.2f );
                    cam_x += ( static_cast<float>( car_pos.x() ) - cam_x ) * follow;
                    cam_y += ( static_cast<float>( car_pos.y() ) - cam_y ) * follow;
                } else {
                    // Bounded glide along the street: a sinusoid on the wall clock, so
                    // the speed is constant no matter how slow a world turn is.
                    const float phase = cam_phase0 +
                                        static_cast<float>( now - cam_t0 ) * 0.001f * 0.22f;
                    const float along = std::sin( phase ) * 9.0f;
                    const float across = std::sin( phase * 0.6f ) * 0.8f;
                    cam_x = static_cast<float>( base.x() ) + ( cam_along_x ? along : across );
                    cam_y = static_cast<float>( base.y() ) + ( cam_along_x ? across : along );
                }
                const float limit = 22.0f;
                // Clamp around where the spectator currently is, not where it started:
                // the driving scene walks the spectator along with the car.
                const tripoint_bub_ms pivot = actor.pos_bub();
                cam_x = std::clamp( cam_x, static_cast<float>( pivot.x() ) - limit,
                                    static_cast<float>( pivot.x() ) + limit );
                cam_y = std::clamp( cam_y, static_cast<float>( pivot.y() ) - limit,
                                    static_cast<float>( pivot.y() ) + limit );
                const int cam_tx = static_cast<int>( std::lround( cam_x ) );
                const int cam_ty = static_cast<int>( std::lround( cam_y ) );
                actor.view_offset = tripoint_rel_ms( cam_tx - pivot.x(), cam_ty - pivot.y(), 0 );
                cam_pan_x = cam_x - static_cast<float>( cam_tx );
                cam_pan_y = cam_y - static_cast<float>( cam_ty );
            };
            set_native_menu_frame_hook( cam_tick );

            while( step < scene_turns && !scene_done ) {
                const Uint32 now = SDL_GetTicks();
                // Advance the camera before drawing so the tiles use this offset;
                // the frame hook repeats it for frames drawn by animation loops.
                cam_tick();
                // Snapshot the sub-tile pan of the frame we are about to draw. The
                // hook keeps advancing cam_pan_x/y afterwards; if the crop read the
                // live value it would shift by a square at every boundary crossing.
                set_native_menu_capture_pan( cam_pan_x, cam_pan_y );

                // ---- draw + publish -------------------------------------------------------
                const auto prof_draw_start = std::chrono::steady_clock::now();
                ui_manager::invalidate_all_ui_adaptors();
                ui_manager::redraw();
                const auto prof_render_end = std::chrono::steady_clock::now();
                refresh_display();
                const auto prof_publish_end = std::chrono::steady_clock::now();

                // ---- world turn on its own cadence ---------------------------------------
                if( now - last_turn_tick >= static_cast<Uint32>( menu_demo_turn_ms ) ) {
                    last_turn_tick = now;
                    ++step;

                    // Keep a scripted scene populated so a wiped-out group does not turn
                    // the footage static. Ordinary spawns, then the vanilla AI takes over.
                    // The survivor camp must stay zombie-free even if the world
                    // decides to wander a horde in while the scene runs.
                    if( kind == 6 && ( step % 8 ) == 0 ) {
                        g->clear_zombies();
                    }

                    if( ( kind == 1 || kind == 3 || kind == 4 ) && ( step % 24 ) == 20 ) {
                        int living_npcs = 0;
                        for( npc &guy : g->all_npcs() ) {
                            ( void )guy;
                            ++living_npcs;
                        }
                        if( living_npcs == 0 && kind != 4 ) {
                            std::string unique_name;
                            std::vector<trait_id> traits;
                            const npc_template_id role( kind == 1 ? "survivor_camper" :
                                                        "NPC_gun_store_survivor" );
                            g->spawn_npc( stage + point( -2, -2 ), role, unique_name, traits,
                                          std::nullopt );
                            g->spawn_npc( stage + point( 2, -2 ), role, unique_name, traits,
                                          std::nullopt );
                        }
                        for( int n = 0; n < 2; ++n ) {
                            g->place_critter_around( zombie, stage + point( 5, 4 + n ), 3 );
                        }
                    }

                    if( kind == 2 && focus_vehicle != nullptr ) {
                        // Vanilla autopilot drives; we only keep the hull on a 90-degree
                        // axis. CDDA cannot draw a vehicle heading 15/30/45 degrees - it
                        // renders the parts sheared and half-turned, which is exactly the
                        // "it turned but still slides the old way" look on screen.
                        if( !focus_vehicle->is_patrolling ) {
                            focus_vehicle->is_patrolling = true;
                        }
                        // Rolling waypoint: keep aiming a fixed distance ahead of the
                        // car instead of at one fixed target. A fixed target made the
                        // autopilot stop as soon as it arrived, which is why the car
                        // halted a few seconds into the shot.
                        {
                            const int lane_dx2 = ( car_wp_b.x() > car_wp_a.x() ) -
                                                 ( car_wp_b.x() < car_wp_a.x() );
                            const int lane_dy2 = ( car_wp_b.y() > car_wp_a.y() ) -
                                                 ( car_wp_b.y() < car_wp_a.y() );
                            if( ( lane_dx2 != 0 || lane_dy2 != 0 ) &&
                                rl_dist( focus_vehicle->pos_abs(), car_wp_a ) <= 30 ) {
                                car_target = here.get_abs( focus_vehicle->pos_bub( here ) +
                                                           point( lane_dx2 * 12, lane_dy2 * 12 ) );
                            } else if( car_target == tripoint_abs_ms::zero ) {
                                car_target = car_wp_b;
                            }
                        }
                        focus_vehicle->set_autopilot_waypoint( car_target );
                        {
                            const int deg_now = static_cast<int>(
                                                  std::lround( to_degrees( focus_vehicle->move.dir() ) ) );
                            const int deg_snap = ( ( ( deg_now % 360 ) + 45 ) / 90 ) * 90 % 360;
                            const units::angle snapped = deg_snap * 1_degrees;
                            if( focus_vehicle->move.dir() != snapped ) {
                                focus_vehicle->move.init( snapped );
                                focus_vehicle->face.init( snapped );
                                trace( "scene_" + std::to_string( scene ) + "_axis_snap_" +
                                       std::to_string( deg_snap ) );
                            }
                        }
                        focus_vehicle->turn_dir = focus_vehicle->face.dir();
                        focus_vehicle->last_turn = 0_degrees;
                        if( focus_vehicle->skidding ) {
                            focus_vehicle->possibly_recover_from_skid();
                        }
                        focus_vehicle->skidding = false;
                        {
                            const int spd = ( focus_vehicle->velocity < 0 ) ?
                                            -focus_vehicle->velocity : focus_vehicle->velocity;
                            if( spd < 300 ) {
                                focus_vehicle->velocity = ( focus_vehicle->velocity < 0 ) ? -300 : 300;
                            }
                        }
                        {
                            const tripoint_bub_ms car_now = focus_vehicle->pos_bub( here );
                            if( rl_dist( car_now, actor.pos_bub() ) > 8 ) {
                                actor.setpos( here, car_now + point( 0, 3 ) );
                            }
                        }
                        {
                            const tripoint_bub_ms lane_pos = focus_vehicle->pos_bub( here );
                            std::vector<const monster *> roadkill;
                            for( monster &mon : g->all_monsters() ) {
                                if( rl_dist( mon.pos_bub(), lane_pos ) <= 6 ) {
                                    roadkill.push_back( &mon );
                                }
                            }
                            for( const monster *mon : roadkill ) {
                                get_creature_tracker().remove( *mon );
                            }
                        }
                        // Safety net: if the car cannot move at all (wedged into a
                        // wall, boxed in by parked wrecks) cut the shot instead of
                        // showing a parked car for the rest of the scene.
                        if( focus_vehicle->velocity == 0 ) {
                            ++car_stuck_turns;
                        } else {
                            car_stuck_turns = 0;
                        }
                        if( car_stuck_turns >= 8 ) {
                            trace( "scene_" + std::to_string( scene ) + "_car_wedged" );
                            scene_done = true;
                        }
                    }

                    // ---- driver stays in the seat -----------------------------------------
                    // Hard impacts hurl a passenger out of the seat (vehicle_move.cpp
                    // calls unboard_vehicle on throw_from_seat), which used to leave the
                    // survivor standing in the road being run over.
                    if( kind == 2 && focus_vehicle != nullptr && step > 1 ) {
                        npc *driver = nullptr;
                        if( driver_id.is_valid() ) {
                            for( npc &guy : g->all_npcs() ) {
                                if( guy.getID() == driver_id ) {
                                    driver = &guy;
                                    break;
                                }
                            }
                        }
                        if( driver == nullptr ) {
                        } else if( !driver->in_vehicle ) {
                            const tripoint_bub_ms car_pos = focus_vehicle->pos_bub( here );
                            tripoint_bub_ms seat_pos;
                            bool have_seat = false;
                            for( int dx = -2; dx <= 2 && !have_seat; ++dx ) {
                                for( int dy = -2; dy <= 2 && !have_seat; ++dy ) {
                                    const tripoint_bub_ms probe = car_pos + point( dx, dy );
                                    const std::optional<vpart_reference> sp =
                                        here.veh_at( probe ).part_with_feature( VPFLAG_BOARDABLE, true );
                                    if( sp && sp->part().is_seat() ) {
                                        seat_pos = probe;
                                        have_seat = true;
                                    }
                                }
                            }
                            if( have_seat ) {
                                here.board_vehicle( seat_pos, driver );
                                trace( "scene_" + std::to_string( scene ) + "_driver_reseated" );
                            }
                        }
                    }

                    // The spectator never acts; only the world turn advances.
                    actor.set_moves( 0 );
                    const auto prof_turn_start = std::chrono::steady_clock::now();
                    const bool quit_now = do_turn();
                    const auto prof_turn_end = std::chrono::steady_clock::now();
                    if( ( step % 20 ) == 0 ) {
                        std::ostringstream prof;
                        prof << "[PROF] render="
                             << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 prof_render_end - prof_draw_start ).count()
                             << "ms publish="
                             << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 prof_publish_end - prof_render_end ).count()
                             << "ms turn="
                             << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 prof_turn_end - prof_turn_start ).count()
                             << "ms";
                        std::cerr << prof.str() << std::endl;
                    }
                    if( quit_now ) {
                        trace( "scene_" + std::to_string( scene ) + "_game_turn_end" );
                        scene_done = true;
                    }

                    if( ( step % menu_demo_stat_every ) == 0 ||
                        step == scene_turns ) {
                        int mons = 0;
                        int npcs = 0;
                        int monhp = 0;
                        int mind = -1;
                        for( npc &guy : g->all_npcs() ) {
                            ( void )guy;
                            ++npcs;
                        }
                        for( monster &mon : g->all_monsters() ) {
                            ++mons;
                            monhp += mon.get_hp();
                            for( npc &guy : g->all_npcs() ) {
                                const int d = rl_dist( mon.pos_bub(), guy.pos_bub() );
                                if( mind < 0 || d < mind ) {
                                    mind = d;
                                }
                            }
                        }
                        std::ostringstream os;
                        os << "[STAT] scene=" << scene << " kind=" << kind << " step=" << step
                           << " turn=" << to_turn<int>( calendar::turn )
                           << " mons=" << mons << " monhp=" << monhp
                           << " npcs=" << npcs << " mind=" << mind
                           << " veh=" << menu_demo_vehicle_report( here, stage );
                        if( focus_vehicle != nullptr ) {
                            // Facing vs actual travel direction: these must agree, or
                            // the car looks like it turns while still sliding along.
                            os << " face=" << std::lround( to_degrees( focus_vehicle->face.dir() ) )
                               << " move=" << std::lround( to_degrees( focus_vehicle->move.dir() ) )
                               << " skid=" << ( focus_vehicle->skidding ? 1 : 0 );
                        }
                        std::cerr << os.str() << std::endl;
                    }
                }

                // Stay tight: publishing a frame is what makes the camera glide, so
                // never sleep for a whole turn here.
                SDL_Delay( 4 );
            }
            set_native_menu_frame_hook( nullptr );
            trace( "scene_" + std::to_string( scene ) + "_finished" );
        }
        trace( "all_scenes_finished" );
        return 0;
    } catch( const std::exception &e ) {
        std::cerr << "Native menu worker error: " << e.what() << '\n';
        // Keep the reason in the trace file too: the parent spawns this process
        // without a console, so stderr is usually lost and a crash would look
        // like an unexplained restart.
        {
            std::ofstream stream( sandbox / "menu_worker_trace.txt", std::ios::app );
            stream << "worker_exception: " << e.what() << '\n';
        }
        return 77;
    }
}
#endif
