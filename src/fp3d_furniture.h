#pragma once
#ifndef CATA_SRC_FP3D_FURNITURE_H
#define CATA_SRC_FP3D_FURNITURE_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace fp3d_furniture
{
enum class kind {
    table, chair, bed, bed_frame, crate, open_crate, cupboard, cabinet, rack, locker,
    counter, desk, sofa, sink, oven, fridge, mini_fridge, dresser, bathtub, toilet,
    bench, park_bench, armchair, fireplace, washer, dishwasher, wood_stove, stool, chest_freezer, coffee_table,
    display_case, file_cabinet, vending_machine, workbench, piano,
    pool_table, anvil, kiln, table_saw, planter, metal_kiln,
    street_light, traffic_light, utility_pole, mailbox, floor_lamp, battery_unit,
    camp_chair, tourist_table, signpost,
    udp_alien, udp_analyzer, udp_antenna, udp_arcade, udp_autodoc, udp_bandsaw, udp_barricade,
    udp_cabinet, udp_chair, udp_coffin, udp_compressor, udp_crate, udp_ct, udp_decor,
    udp_dish, udp_drill, udp_dumpster, udp_fire, udp_firebarrel, udp_forge, udp_gate,
    udp_generator, udp_hammock, udp_iv, udp_ladder, udp_lathe, udp_machine, udp_mat,
    udp_mattress, udp_medical_bed, udp_mixer, udp_mri, udp_pile, udp_pole, udp_press,
    udp_prop, udp_rack, udp_robotic, udp_safe, udp_shower, udp_sign, udp_small_vessel,
    udp_solar, udp_stack, udp_stack_mid, udp_stack_short, udp_stack_tall, udp_table, udp_tank,
    udp_target, udp_trash, udp_treadmill, udp_vent, udp_wall, udp_windmill,
    udp_boat, udp_capacitor, udp_curtain, udp_diving_platform,
    udp_glass_cabinet, udp_lamp, udp_mirror, udp_mooring,
    udp_air_conditioner, udp_air_filter, udp_alien_pod, udp_alien_scar, udp_arc_furnace,
    udp_ball_machine, udp_bellows, udp_birdbath, udp_boulder_large, udp_boulder_medium,
    udp_boulder_small, udp_brazier, udp_brick_mid, udp_brick_short, udp_brick_tall,
    udp_bridge_flat, udp_butter_churn, udp_cell_booster, udp_climbing_rope, udp_closed_coffin,
    udp_clothesline, udp_coat_rack, udp_distiller, udp_egg_cluster, udp_electrical_conduit,
    udp_ergometer, udp_fire_ring, udp_flagpole, udp_fuel_tank, udp_gazing_ball,
    udp_grave, udp_ground_cable, udp_gunsafe, udp_hobo_stove, udp_home_furnace,
    udp_impaled, udp_keg, udp_loaded_pallet, udp_logs_mid, udp_logs_short,
    udp_logs_tall, udp_mannequin, udp_metal_table, udp_mound_mid, udp_mound_short,
    udp_mound_tall, udp_obelisk, udp_open_coffin, udp_open_gunsafe, udp_open_safe,
    udp_pillow_fort, udp_pinball, udp_planks_mid, udp_planks_short, udp_planks_tall,
    udp_roulette, udp_sand_castle, udp_scrap_heap, udp_shackle, udp_speaker,
    udp_spike, udp_stop_sign, udp_styling_station, udp_tall_antenna, udp_tubes_mid,
    udp_tubes_short, udp_tubes_tall, udp_tv_aerial, udp_twig_bundle, udp_twigs_mid,
    udp_twigs_short, udp_twigs_tall, udp_water_heater, udp_weight_bench, udp_wind_vane,
    udp_woodchips, udp_wreath,
    udp_cleat,
    udp_anesthetic, udp_archery_bale, udp_archery_box, udp_ash_decal, udp_bollard, udp_chimney, udp_dialysis, udp_hay_bale, udp_leaf_litter, udp_midden, udp_punching_bag, udp_rubble, udp_target_board, udp_turbine_vent, udp_vent_pipe, udp_ventilator, udp_water_purifier, udp_waterwheel, udp_windmill_real, udp_heavy_lathe,
    udp_coal_forge, udp_vertical_bandsaw, udp_sandbag_low, udp_stone_statue, udp_diesel_unit,
    udp_terminal,
    prop_card, prop_elev, prop_intercom, prop_switch,
    prop_winch, prop_lever, prop_fuel, prop_fuel_broken,
    prop_crossbuck, prop_crossing, prop_hatch, prop_halfwall,
    prop_ruinedwall, prop_overheadbars, prop_slide, prop_manual_pump,
    prop_bottle_cooler, prop_terminal, prop_kiosk, prop_recycler,
    prop_pipe, prop_motor, prop_pulley, prop_pedestal,
    prop_boom, prop_buffer, prop_forge, prop_rack,
    prop_cage, prop_cartilage, prop_column_machine, prop_scrap,
    prop_basin, prop_hive, prop_barrier, prop_memorial,
    prop_log, prop_dam, prop_cushion, prop_bike,
    prop_medical_chair, prop_antenna, prop_lantern, prop_anemone,
    prop_sponge, prop_bucket, prop_press, prop_mound_short,
    prop_mound_mid, prop_mound_tall, prop_vent_grate, prop_glassgate,
    prop_wire, prop_barbed, prop_ropefence, prop_railfence,
    prop_portcullis, prop_shelter, prop_mortar,     prop_detector,
    prop_lean_to, prop_chainmesh,
};
struct model {
    std::string id;
    kind shape;
};
struct world_prop_definition {
    kind shape;
    const char *art;
    std::array<int, 4> shell;
    std::array<int, 4> panel;
    int panel_part;
    bool linear;
    bool wall_fixture;
    bool black_glass;
};
const world_prop_definition *world_prop( const std::string &id );
std::array<int, 4> world_prop_shell( const world_prop_definition &prop, unsigned part );

enum class part_form { standard, arch, sling, rock, basin, sandbag, bellows, bellows_board };
struct part {
    float x, z, width, depth, bottom, top;
    bool round = false;
    bool glass = false; // A thin translucent display pane, not a solid block.
    bool horizontal = false; // Round part axis lies along local Z instead of Y.
    bool hollow = false; // Empty ring, not a filled cylinder.
    float inner = .60f; // Hollow radius fraction.
    float roll = 0.f; // Box-plane rotation around local Z, degrees.
    bool sphere = false; // Ellipsoid, not a capped cylinder.
    part_form form = part_form::standard;
};
// Owned, read-only presentation shared by built furniture, ghosts and picking.
std::optional<model> describe( const std::string &id );
const std::vector<part> &parts( kind shape );
bool connects_as_bed( const model &furniture );
// Links are north, east, south and west in bits 0–3.
std::optional<part> bed_segment_part( kind shape, unsigned index, unsigned links );
float item_surface( kind shape );
// Only already-captured accessible contents receive the storage display pass.
bool shows_contents( kind shape );
// Backing walls in native north/east/south/west order; no live-map queries.
unsigned facing( kind shape, const std::array<bool, 4> &walls,
                 const std::array<bool, 4> &tables = {} );
bool chair_table( const std::optional<model> &furniture );
// Same quarter-turn transform as the rendered model, for part-level picking.
part rotated( part value, unsigned quarter_turns );
}
#endif
#endif
