// SDL3 world props and material patches from the UndeadPeople source tiles.
// A front patch is optional. All other faces use the plain material swatch,
// so the sprite silhouette and its painted shadow never get wrapped on the mesh.
const world_prop_definition *world_prop( const std::string &id )
{
    static const std::unordered_map<std::string, world_prop_definition> definitions{
        { "f_LIXA_machinery_electronic", { kind::prop_rack, "t_machinery_electronic", { 15, 16, 17, 18 }, { 8, 9, 23, 21 }, 1, false, false, false } },
        { "f_LIXA_unfold_barrier", { kind::prop_barrier, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "f_alien_anemone", { kind::prop_anemone, "f_alien_anemone", { 16, 24, 18, 26 }, { 16, 24, 18, 26 }, -1, false, false, false } },
        { "f_atm", { kind::prop_kiosk, "t_atm", { 14, 26, 16, 28 }, { 9, 10, 22, 19 }, 1, false, false, false } },
        { "f_atm_off", { kind::prop_kiosk, "t_atm", { 14, 26, 16, 28 }, { 9, 10, 22, 19 }, 1, false, false, false } },
        { "f_balthazar_server", { kind::prop_rack, "t_machinery_electronic", { 15, 16, 17, 18 }, { 8, 9, 23, 21 }, 1, false, false, false } },
        { "f_battlefield_cross", { kind::prop_memorial, "f_statue", { 14, 19, 15, 20 }, { 14, 19, 15, 20 }, -1, false, false, false } },
        { "f_beach_log", { kind::prop_log, "t_trunk", { 16, 14, 18, 16 }, { 16, 14, 18, 16 }, -1, false, false, false } },
        { "f_beanbag", { kind::prop_cushion, "f_armchair", { 15, 21, 17, 23 }, { 15, 21, 17, 23 }, -1, false, false, false } },
        { "f_beaver_dam", { kind::prop_dam, "t_trunk", { 16, 14, 18, 16 }, { 16, 14, 18, 16 }, -1, false, false, false } },
        { "f_bike_rack", { kind::prop_bike, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "f_bullettrailer_toilet", { kind::toilet, "f_toilet", { 16, 18, 17, 19 }, { 16, 18, 17, 19 }, -1, false, false, false } },
        { "f_chest_minifreezer", { kind::chest_freezer, "f_glass_fridge", { 13, 23, 15, 25 }, { 13, 23, 15, 25 }, -1, false, false, false } },
        { "f_console_broken_table", { kind::prop_terminal, "t_console_broken", { 18, 19, 20, 21 }, { 7, 3, 24, 19 }, 1, false, false, false } },
        { "f_dirtmound_pile", { kind::prop_mound_mid, "t_dirtmound", { 16, 24, 18, 26 }, { 16, 24, 18, 26 }, -1, false, false, false } },
        { "f_dirtmound_shallow", { kind::prop_mound_short, "t_dirtmound", { 16, 24, 18, 26 }, { 16, 24, 18, 26 }, -1, false, false, false } },
        { "f_dirtmound_tall", { kind::prop_mound_tall, "t_dirtmound", { 16, 24, 18, 26 }, { 16, 24, 18, 26 }, -1, false, false, false } },
        { "f_exam_chair", { kind::prop_medical_chair, "f_autodoc", { 9, 7, 11, 9 }, { 9, 7, 11, 9 }, -1, false, false, false } },
        { "f_exodii_locator_antenna", { kind::prop_antenna, "f_TV_antenna", { 15, 20, 17, 22 }, { 15, 20, 17, 22 }, -1, false, false, false } },
        { "f_firefly_terrarium", { kind::udp_glass_cabinet, "f_glass_cabinet", { 16, 4, 18, 6 }, { 16, 4, 18, 6 }, -1, false, false, false } },
        { "f_hd_compressor_unit", { kind::prop_motor, "t_sewage_pump", { 15, 10, 17, 12 }, { 15, 10, 17, 12 }, -1, false, false, false } },
        { "f_hoist_chain", { kind::prop_pulley, "t_palisade_pulley", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "f_hoist_pulley", { kind::prop_pulley, "t_palisade_pulley", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "f_hoist_rope", { kind::prop_pulley, "t_palisade_pulley", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "f_lighthouse_light", { kind::prop_lantern, "f_floor_lamp", { 10, 7, 12, 9 }, { 10, 7, 12, 9 }, -1, false, false, false } },
        { "f_m224_mortar", { kind::prop_mortar, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "f_makeshift_brazier", { kind::prop_bucket, "bucket", { 12, 12, 14, 14 }, { 12, 12, 14, 14 }, -1, false, false, false } },
        { "f_metal_detector", { kind::prop_detector, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "f_minifreezer", { kind::mini_fridge, "f_fridge", { 12, 28, 13, 29 }, { 12, 28, 13, 29 }, -1, false, false, false } },
        { "f_nuclear_fuel_cell", { kind::prop_column_machine, "t_machinery_heavy", { 16, 13, 18, 15 }, { 16, 13, 18, 15 }, -1, false, false, false } },
        { "f_oxygen_concentrator", { kind::prop_motor, "t_sewage_pump", { 15, 10, 17, 12 }, { 15, 10, 17, 12 }, -1, false, false, false } },
        { "f_parkour_bars", { kind::prop_overheadbars, "t_monkey_bars", { 15, 16, 17, 18 }, { 15, 16, 17, 18 }, -1, false, false, false } },
        { "f_screw_press", { kind::prop_press, "f_hydraulic_press", { 15, 16, 17, 18 }, { 15, 16, 17, 18 }, -1, false, false, false } },
        { "f_sea_anemone", { kind::prop_anemone, "f_sea_anemone", { 15, 22, 17, 24 }, { 15, 22, 17, 24 }, -1, false, false, false } },
        { "f_sea_sponge", { kind::prop_sponge, "f_sea_sponge", { 18, 23, 20, 25 }, { 18, 23, 20, 25 }, -1, false, false, false } },
        { "f_stack_plank", { kind::udp_planks_short, "f_coffee_table", { 21, 14, 23, 16 }, { 21, 14, 23, 16 }, -1, false, false, false } },
        { "f_torah_ark", { kind::cabinet, "f_wardrobe", { 16, 3, 18, 5 }, { 16, 3, 18, 5 }, -1, false, false, false } },
        { "f_training_dummy_heavy", { kind::udp_stone_statue, "f_statue", { 14, 19, 15, 20 }, { 14, 19, 15, 20 }, -1, false, false, false } },
        { "f_training_dummy_light", { kind::udp_stone_statue, "f_statue", { 14, 19, 15, 20 }, { 14, 19, 15, 20 }, -1, false, false, false } },
        { "f_vent_cold_air2_blast", { kind::prop_vent_grate, "f_vent_cold_air2_blast", { 16, 11, 18, 13 }, { 16, 11, 18, 13 }, -1, false, false, false } },
        { "f_vent_cold_air2_stream", { kind::prop_vent_grate, "f_vent_cold_air2_stream", { 24, 16, 26, 18 }, { 24, 16, 26, 18 }, -1, false, false, false } },
        { "f_vent_hot_air2_blast", { kind::prop_vent_grate, "f_vent_hot_air2_blast", { 16, 11, 18, 13 }, { 16, 11, 18, 13 }, -1, false, false, false } },
        { "f_vent_hot_air2_stream", { kind::prop_vent_grate, "f_vent_hot_air2_stream", { 16, 20, 18, 22 }, { 16, 20, 18, 22 }, -1, false, false, false } },
        { "f_vent_rad_leak", { kind::prop_vent_grate, "f_vent_rad_leak", { 25, 6, 27, 8 }, { 25, 6, 27, 8 }, -1, false, false, false } },
        { "f_vent_toxic_belch", { kind::prop_vent_grate, "f_vent_toxic_belch", { 15, 17, 17, 19 }, { 15, 17, 17, 19 }, -1, false, false, false } },
        { "f_vent_toxic_leak", { kind::prop_vent_grate, "f_vent_toxic_leak", { 16, 17, 18, 19 }, { 16, 17, 18, 19 }, -1, false, false, false } },
        { "f_vitrified_armchair", { kind::armchair, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_bathtub", { kind::bathtub, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_bed", { kind::bed, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_bench", { kind::bench, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_birdbath", { kind::udp_birdbath, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_bookcase", { kind::rack, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_bookcase_talker", { kind::rack, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_box", { kind::crate, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_chair", { kind::chair, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_counter", { kind::counter, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_cupboard", { kind::cupboard, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_desk", { kind::desk, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_dishwasher", { kind::dishwasher, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_doll", { kind::udp_impaled, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_dresser", { kind::dresser, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_dryer", { kind::washer, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_fireplace", { kind::fireplace, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_firering", { kind::udp_fire_ring, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_floor_lamp", { kind::floor_lamp, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_fridge", { kind::fridge, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_locker", { kind::locker, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_oven", { kind::oven, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_planter", { kind::planter, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_rack", { kind::rack, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_shower", { kind::udp_shower, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_sink", { kind::sink, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_sofa", { kind::sofa, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_table", { kind::table, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_toilet", { kind::toilet, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_washer", { kind::washer, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_vitrified_water_heater", { kind::udp_water_heater, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, true } },
        { "f_web_up", { kind::prop_pulley, "t_palisade_pulley", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "t_avgas_pump", { kind::prop_fuel, "t_diesel_pump", { 17, 27, 19, 29 }, { 8, 8, 21, 16 }, 5, false, false, false } },
        { "t_avgas_pump_smashed", { kind::prop_fuel_broken, "t_diesel_pump_smashed", { 12, 27, 14, 29 }, { 20, 7, 22, 9 }, -1, false, false, false } },
        { "t_bark_wall_broken", { kind::prop_ruinedwall, "t_bark_wall_broken", { 13, 12, 15, 14 }, { 13, 12, 15, 14 }, -1, true, false, false } },
        { "t_barndoor", { kind::prop_pulley, "t_barndoor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "t_beehive_natural", { kind::prop_hive, "f_hay", { 7, 12, 9, 14 }, { 7, 12, 9, 14 }, -1, false, false, false } },
        { "t_boom_barrier_lowered", { kind::prop_boom, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, true, false, false } },
        { "t_brick_oven", { kind::prop_forge, "t_brick_wall", { 15, 23, 17, 25 }, { 15, 23, 17, 25 }, -1, false, false, false } },
        { "t_brick_oven_struct", { kind::prop_forge, "t_brick_wall", { 15, 23, 17, 25 }, { 15, 23, 17, 25 }, -1, false, false, false } },
        { "t_buffer_stop", { kind::prop_buffer, "t_buffer_stop", { 16, 12, 18, 14 }, { 16, 12, 18, 14 }, -1, true, false, false } },
        { "t_card_LIXA", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_LIXA_2", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_LIXA_3", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_LIXA_mil", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_artisans", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_fp", { kind::prop_card, "t_card_fp", { 16, 13, 18, 15 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_industrial", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_military", { kind::prop_card, "t_card_military", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_reader_broken", { kind::prop_card, "t_card_reader_broken", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_robofac", { kind::prop_card, "t_card_robofac", { 14, 13, 16, 15 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science", { kind::prop_card, "t_card_science", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_maintenance_blue", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_maintenance_green", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_maintenance_yellow", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_medical_red", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_mu", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_mutagen_cyan", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_mutagen_green", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_mutagen_pink", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_security_black", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_security_magenta", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_security_yellow", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_transport_1", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_card_science_visitor", { kind::prop_card, "t_card_industrial", { 13, 25, 15, 27 }, { 9, 10, 22, 22 }, 1, false, true, false } },
        { "t_cattle_feeder", { kind::prop_basin, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "t_crossbuck_metal", { kind::prop_crossbuck, "t_crossbuck_metal", { 16, 10, 18, 12 }, { 16, 10, 18, 12 }, -1, false, false, false } },
        { "t_crossbuck_wood", { kind::prop_crossbuck, "t_crossbuck_wood", { 15, 6, 17, 8 }, { 15, 6, 17, 8 }, -1, false, false, false } },
        { "t_cvdmachine", { kind::prop_rack, "t_cvdmachine", { 18, 3, 20, 5 }, { 18, 3, 20, 5 }, -1, false, false, false } },
        { "t_diesel_pump", { kind::prop_fuel, "t_diesel_pump", { 17, 27, 19, 29 }, { 8, 8, 21, 16 }, 5, false, false, false } },
        { "t_diesel_pump_a", { kind::prop_fuel, "t_diesel_pump_a", { 17, 27, 19, 29 }, { 8, 8, 21, 16 }, 5, false, false, false } },
        { "t_diesel_pump_smashed", { kind::prop_fuel_broken, "t_diesel_pump_smashed", { 12, 27, 14, 29 }, { 20, 7, 22, 9 }, -1, false, false, false } },
        { "t_drill_string", { kind::prop_pipe, "t_sewage_pipe", { 15, 16, 17, 18 }, { 15, 16, 17, 18 }, -1, false, false, false } },
        { "t_drystone_wall_half", { kind::prop_halfwall, "t_drystone_wall_half", { 13, 10, 15, 12 }, { 13, 10, 15, 12 }, -1, true, false, false } },
        { "t_elevator_control", { kind::prop_elev, "t_elevator_control", { 15, 18, 17, 20 }, { 12, 6, 19, 25 }, 1, false, true, false } },
        { "t_elevator_control_no_roof", { kind::prop_elev, "t_elevator_control", { 15, 18, 17, 20 }, { 12, 6, 19, 25 }, 1, false, true, false } },
        { "t_elevator_control_off", { kind::prop_elev, "t_elevator_control_off", { 15, 16, 17, 18 }, { 12, 6, 19, 25 }, 1, false, true, false } },
        { "t_elevator_control_off_no_roof", { kind::prop_elev, "t_elevator_control", { 15, 18, 17, 20 }, { 12, 6, 19, 25 }, 1, false, true, false } },
        { "t_elevator_control_robofac_down", { kind::prop_elev, "t_elevator_control_robofac_down", { 14, 13, 16, 15 }, { 12, 6, 19, 25 }, 1, false, true, false } },
        { "t_elevator_control_robofac_up", { kind::prop_elev, "t_elevator_control_robofac_up", { 14, 13, 16, 15 }, { 12, 6, 19, 25 }, 1, false, true, false } },
        { "t_fence_barbed", { kind::prop_barbed, "t_fence_barbed", { 5, 15, 7, 17 }, { 5, 15, 7, 17 }, -1, true, false, false } },
        { "t_fence_rope", { kind::prop_ropefence, "t_fence_rope", { 17, 15, 19, 17 }, { 17, 15, 19, 17 }, -1, true, false, false } },
        { "t_fence_wire", { kind::prop_wire, "t_fence_wire", { 14, 6, 16, 8 }, { 14, 6, 16, 8 }, -1, true, false, false } },
        { "t_gas_pump", { kind::prop_fuel, "t_gas_pump", { 17, 27, 19, 29 }, { 8, 8, 21, 16 }, 5, false, false, false } },
        { "t_gas_pump_a", { kind::prop_fuel, "t_gas_pump_a", { 17, 27, 19, 29 }, { 8, 8, 21, 16 }, 5, false, false, false } },
        { "t_gas_pump_smashed", { kind::prop_fuel_broken, "t_gas_pump_smashed", { 12, 27, 14, 29 }, { 20, 7, 22, 9 }, -1, false, false, false } },
        { "t_gates_boom_barrier", { kind::prop_lever, "t_gates_control_metal", { 10, 4, 12, 6 }, { 10, 4, 12, 6 }, -1, false, true, false } },
        { "t_gates_control_brick", { kind::prop_winch, "t_gates_control_brick", { 19, 23, 21, 25 }, { 19, 23, 21, 25 }, -1, false, true, false } },
        { "t_gates_control_brick_lab", { kind::prop_winch, "t_gates_control_brick_lab", { 16, 24, 18, 26 }, { 16, 24, 18, 26 }, -1, false, true, false } },
        { "t_gates_control_concrete", { kind::prop_lever, "t_gates_control_concrete", { 19, 3, 21, 5 }, { 19, 3, 21, 5 }, -1, false, true, false } },
        { "t_gates_control_concrete_lab", { kind::prop_lever, "t_gates_control_concrete_lab", { 15, 4, 17, 6 }, { 15, 4, 17, 6 }, -1, false, true, false } },
        { "t_gates_control_metal", { kind::prop_lever, "t_gates_control_metal", { 10, 4, 12, 6 }, { 10, 4, 12, 6 }, -1, false, true, false } },
        { "t_gates_control_metal_lab", { kind::prop_lever, "t_gates_control_metal_lab", { 10, 4, 12, 6 }, { 10, 4, 12, 6 }, -1, false, true, false } },
        { "t_gates_mech_control", { kind::prop_winch, "t_gates_mech_control", { 19, 23, 21, 25 }, { 19, 23, 21, 25 }, -1, false, true, false } },
        { "t_gates_mech_control_lab", { kind::prop_winch, "t_gates_mech_control_lab", { 17, 25, 19, 27 }, { 17, 25, 19, 27 }, -1, false, true, false } },
        { "t_improvised_shelter", { kind::prop_shelter, "t_improvised_shelter", { 14, 9, 16, 11 }, { 14, 9, 16, 11 }, -1, false, false, false } },
        { "t_intercom", { kind::prop_intercom, "t_intercom", { 12, 25, 14, 27 }, { 9, 7, 22, 24 }, 1, false, true, false } },
        { "t_intercom_balthazar", { kind::prop_intercom, "t_intercom", { 12, 25, 14, 27 }, { 9, 7, 22, 24 }, 1, false, true, false } },
        { "t_jp8_pump", { kind::prop_fuel, "t_jp8_pump", { 17, 27, 19, 29 }, { 8, 8, 21, 16 }, 5, false, false, false } },
        { "t_jp8_pump_smashed", { kind::prop_fuel_broken, "t_jp8_pump_smashed", { 12, 27, 14, 29 }, { 20, 7, 22, 9 }, -1, false, false, false } },
        { "t_leanto", { kind::prop_lean_to, "t_leanto", { 15, 19, 16, 20 }, { 15, 19, 16, 20 }, -1, false, false, false } },
        { "t_missile", { kind::prop_column_machine, "t_missile", { 18, 15, 20, 17 }, { 18, 15, 20, 17 }, -1, false, false, false } },
        { "t_missile_exploded", { kind::prop_scrap, "t_missile_exploded", { 16, 13, 18, 15 }, { 16, 13, 18, 15 }, -1, false, false, false } },
        { "t_monkey_bars", { kind::prop_overheadbars, "t_monkey_bars", { 15, 16, 17, 18 }, { 15, 16, 17, 18 }, -1, false, false, false } },
        { "t_nanofab_body", { kind::prop_rack, "t_nanofab_body", { 18, 24, 20, 26 }, { 8, 9, 23, 21 }, 1, false, false, false } },
        { "t_nm_cartilage", { kind::prop_cartilage, "t_nm_cartilage", { 9, 17, 11, 19 }, { 9, 17, 11, 19 }, -1, false, false, false } },
        { "t_nuclear_reactor", { kind::prop_column_machine, "t_machinery_heavy", { 16, 13, 18, 15 }, { 16, 13, 18, 15 }, -1, false, false, false } },
        { "t_palisade_pulley", { kind::prop_pulley, "t_palisade_pulley", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "t_palisade_pulley_wood", { kind::prop_pulley, "t_palisade_pulley", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, false, false, false } },
        { "t_pedestal_temple", { kind::prop_pedestal, "t_pedestal_temple", { 15, 5, 18, 6 }, { 9, 11, 23, 22 }, 3, false, false, false } },
        { "t_pedestal_wyrm", { kind::prop_pedestal, "t_pedestal_wyrm", { 15, 5, 18, 6 }, { 9, 11, 23, 22 }, 3, false, false, false } },
        { "t_ponywall", { kind::prop_halfwall, "t_ponywall", { 13, 24, 15, 26 }, { 13, 24, 15, 26 }, -1, true, false, false } },
        { "t_portcullis", { kind::prop_portcullis, "t_portcullis", { 15, 13, 17, 15 }, { 15, 13, 17, 15 }, -1, true, false, false } },
        { "t_quietfarm_border_fence", { kind::prop_chainmesh, "t_bars", { 7, 13, 9, 15 }, { 7, 13, 9, 15 }, -1, true, false, false } },
        { "t_radio_controls", { kind::prop_terminal, "t_radio_controls", { 18, 19, 20, 21 }, { 7, 3, 24, 19 }, 1, false, false, false } },
        { "t_railroad_crossing_signal", { kind::prop_crossing, "t_railroad_crossing_signal", { 16, 10, 18, 12 }, { 16, 10, 18, 12 }, -1, false, false, false } },
        { "t_reb_cage", { kind::prop_cage, "t_reb_cage", { 18, 16, 20, 18 }, { 18, 16, 20, 18 }, -1, false, false, false } },
        { "t_recycler", { kind::prop_recycler, "t_recycler", { 22, 4, 24, 6 }, { 7, 10, 24, 24 }, 1, false, false, false } },
        { "t_retractable_gate_l", { kind::prop_chainmesh, "t_retractable_gate_l", { 10, 15, 12, 17 }, { 10, 15, 12, 17 }, -1, true, false, false } },
        { "t_rootcellar", { kind::prop_hatch, "t_rootcellar", { 16, 10, 18, 12 }, { 4, 4, 27, 27 }, 1, false, false, false } },
        { "t_sewage_pipe", { kind::prop_pipe, "t_sewage_pipe", { 15, 16, 17, 18 }, { 15, 16, 17, 18 }, -1, false, false, false } },
        { "t_sewage_pump", { kind::prop_motor, "t_sewage_pump", { 15, 10, 17, 12 }, { 15, 10, 17, 12 }, -1, false, false, false } },
        { "t_slide", { kind::prop_slide, "t_slide", { 13, 3, 15, 5 }, { 13, 3, 15, 5 }, -1, false, false, false } },
        { "t_slot_machine", { kind::prop_kiosk, "t_slot_machine", { 17, 19, 19, 21 }, { 9, 10, 22, 19 }, 1, false, false, false } },
        { "t_splitrail_fence", { kind::prop_railfence, "t_splitrail_fence", { 20, 17, 22, 19 }, { 20, 17, 22, 19 }, -1, true, false, false } },
        { "t_switch_even", { kind::prop_switch, "t_switch_even", { 16, 9, 18, 11 }, { 12, 11, 19, 21 }, 1, false, true, false } },
        { "t_switch_gb", { kind::prop_switch, "t_switch_gb", { 17, 9, 19, 11 }, { 12, 11, 19, 21 }, 1, false, true, false } },
        { "t_switch_rb", { kind::prop_switch, "t_switch_rb", { 16, 9, 18, 11 }, { 12, 11, 19, 21 }, 1, false, true, false } },
        { "t_switch_rg", { kind::prop_switch, "t_switch_rg", { 16, 9, 18, 11 }, { 12, 11, 19, 21 }, 1, false, true, false } },
        { "t_tarptent", { kind::prop_lean_to, "t_tarptent", { 14, 15, 16, 17 }, { 14, 15, 16, 17 }, -1, false, false, false } },
        { "t_ticket_machine", { kind::prop_kiosk, "t_ticket_machine", { 15, 23, 17, 25 }, { 9, 10, 22, 19 }, 1, false, false, false } },
        { "t_ticket_vendor", { kind::prop_kiosk, "t_ticket_vendor", { 15, 9, 17, 11 }, { 9, 10, 22, 19 }, 1, false, false, false } },
        { "t_truss_pillar", { kind::prop_cage, "t_reb_cage", { 18, 16, 20, 18 }, { 18, 16, 20, 18 }, -1, false, false, false } },
        { "t_underwater_drill_string", { kind::prop_pipe, "t_sewage_pipe", { 15, 16, 17, 18 }, { 15, 16, 17, 18 }, -1, false, false, false } },
        { "t_vitrified_bars", { kind::prop_portcullis, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, true, false, true } },
        { "t_vitrified_chickenwire_fence", { kind::prop_chainmesh, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, true, false, true } },
        { "t_vitrified_door", { kind::prop_glassgate, "t_metal_floor", { 15, 3, 17, 5 }, { 15, 3, 17, 5 }, -1, true, false, true } },
        { "t_wall_burnt", { kind::prop_ruinedwall, "t_wall_log_broken", { 25, 9, 27, 11 }, { 25, 9, 27, 11 }, -1, true, false, false } },
        { "t_wall_log_broken", { kind::prop_ruinedwall, "t_wall_log_broken", { 25, 9, 27, 11 }, { 25, 9, 27, 11 }, -1, true, false, false } },
        { "t_wall_wattle_broken", { kind::prop_ruinedwall, "t_wall_wattle_broken", { 16, 25, 18, 27 }, { 16, 25, 18, 27 }, -1, true, false, false } },
        { "t_wall_wood_broken", { kind::prop_ruinedwall, "t_wall_wood_broken", { 12, 4, 14, 6 }, { 12, 4, 14, 6 }, -1, true, false, false } },
        { "t_water_dispenser", { kind::prop_bottle_cooler, "t_water_dispenser", { 8, 26, 10, 28 }, { 9, 18, 22, 25 }, 1, false, false, false } },
        { "t_water_pump", { kind::prop_manual_pump, "t_water_pump", { 15, 15, 17, 17 }, { 15, 15, 17, 17 }, -1, false, false, false } },
        { "t_wattle_fence", { kind::prop_railfence, "t_splitrail_fence", { 20, 17, 22, 19 }, { 20, 17, 22, 19 }, -1, true, false, false } },
    };
    const auto it = definitions.find( id );
    return it == definitions.end() ? nullptr : &it->second;
}

std::array<int, 4> world_prop_shell( const world_prop_definition &prop, unsigned part )
{
    switch( prop.shape ) {
        case kind::prop_fuel:
            return part == 2 ? std::array<int, 4>{ 16, 4, 18, 6 } : prop.shell;
        case kind::prop_fuel_broken:
            return part == 2 ? std::array<int, 4>{ 20, 7, 22, 9 } : prop.shell;
        case kind::prop_bottle_cooler:
            return part >= 2 ? std::array<int, 4>{ 19, 15, 21, 17 } : prop.shell;
        case kind::prop_crossbuck:
        case kind::prop_crossing:
            // The original sign strokes are one pixel wide. Only use the white paint.
            return part == 1 || part == 2 ? std::array<int, 4>{ 10, 1, 10, 1 } : prop.shell;
        default:
            return prop.shell;
    }
}

namespace
{
const std::vector<part> *world_prop_parts( kind shape )
{
    switch( shape ) {
        case kind::prop_card: {
            static const std::vector<part> geometry{
                { 0, 0, 0.3f, 0.12f, 0.91f, 1.44f },
                { 0, 0.07f, 0.26f, 0.025f, 0.97f, 1.38f },
                { 0.1f, 0.087f, 0.025f, 0.012f, 1.03f, 1.27f },
            };
            return &geometry;
        }
        case kind::prop_elev: {
            static const std::vector<part> geometry{
                { 0, 0, 0.26f, 0.12f, 0.82f, 1.53f },
                { 0, 0.069f, 0.17f, 0.018f, 0.87f, 1.48f },
                { 0, 0.082f, 0.044f, 0.018f, 1.21f, 1.26f },
                { 0, 0.082f, 0.044f, 0.018f, 1.06f, 1.11f },
            };
            return &geometry;
        }
        case kind::prop_intercom: {
            static const std::vector<part> geometry{
                { 0, 0, 0.34f, 0.13f, 0.97f, 1.54f },
                { 0, 0.072f, 0.28f, 0.02f, 1.04f, 1.48f },
            };
            return &geometry;
        }
        case kind::prop_switch: {
            static const std::vector<part> geometry{
                { 0, 0, 0.14f, 0.062f, 0.93f, 1.29f },
                { 0, 0.039f, 0.096f, 0.019f, 1, 1.22f },
            };
            return &geometry;
        }
        case kind::prop_winch: {
            static const std::vector<part> geometry{
                { 0, 0, 0.46f, 0.19f, 0.55f, 1.17f },
                { 0, 0.14f, 0.39f, 0.19f, 0.65f, 1.05f, true, false, true },
                { 0.24f, 0.23f, 0.38f, 0.08f, 0.79f, 0.84f },
                { 0.41f, 0.26f, 0.075f, 0.14f, 0.83f, 0.99f },
            };
            return &geometry;
        }
        case kind::prop_lever: {
            static const std::vector<part> geometry{
                { 0, 0, 0.34f, 0.21f, 0.23f, 0.75f },
                { 0, 0.117f, 0.25f, 0.025f, 0.3f, 0.68f },
                { 0.1f, 0.144f, 0.06f, 0.08f, 0.49f, 0.99f, false, false, false, false, 0.6f, -32 },
                { 0.22f, 0.145f, 0.085f, 0.09f, 0.91f, 1, true, false, true },
            };
            return &geometry;
        }
        case kind::prop_fuel: {
            static const std::vector<part> geometry{
                { 0, 0, 0.49f, 0.44f, 0, 0.18f },
                { 0, 0, 0.43f, 0.34f, 0.19f, 1.17f },
                { 0, 0, 0.49f, 0.37f, 1.16f, 1.34f },
                { 0.255f, 0.015f, 0.068f, 0.055f, 0.31f, 0.93f },
                { 0.18f, 0.2f, 0.083f, 0.041f, 0.62f, 0.88f },
                { 0, 0.185f, 0.35f, 0.038f, 0.72f, 1.07f },
            };
            return &geometry;
        }
        case kind::prop_fuel_broken: {
            static const std::vector<part> geometry{
                { 0, 0, 0.49f, 0.44f, 0, 0.18f },
                { 0, 0, 0.44f, 0.34f, 0.19f, 0.81f, false, false, false, false, 0.6f, -12 },
                { -0.06f, 0, 0.48f, 0.35f, 0.79f, 1.07f, false, false, false, false, 0.6f, -16 },
                { 0.24f, 0.04f, 0.068f, 0.05f, 0.17f, 0.57f, false, false, false, false, 0.6f, 12 },
            };
            return &geometry;
        }
        case kind::prop_crossbuck: {
            static const std::vector<part> geometry{
                { 0, 0, 0.095f, 0.095f, 0, 1.86f },
                { 0, 0.052f, 0.09f, 0.051f, 1.27f, 2.13f, false, false, false, false, 0.6f, 45 },
                { 0, 0.053f, 0.09f, 0.051f, 1.27f, 2.13f, false, false, false, false, 0.6f, -45 },
            };
            return &geometry;
        }
        case kind::prop_crossing: {
            static const std::vector<part> geometry{
                { 0, 0, 0.105f, 0.105f, 0, 2.04f },
                { 0, 0.055f, 0.08f, 0.047f, 1.54f, 2.32f, false, false, false, false, 0.6f, 45 },
                { 0, 0.057f, 0.08f, 0.047f, 1.54f, 2.32f, false, false, false, false, 0.6f, -45 },
                { 0, 0.026f, 0.58f, 0.08f, 1.44f, 1.52f },
                { -0.21f, 0.08f, 0.19f, 0.11f, 1.38f, 1.57f, true, false, true },
                { 0.21f, 0.08f, 0.19f, 0.11f, 1.38f, 1.57f, true, false, true },
            };
            return &geometry;
        }
        case kind::prop_hatch: {
            static const std::vector<part> geometry{
                { 0, 0, 0.86f, 0.75f, 0, 0.065f },
                { 0, 0, 0.79f, 0.68f, 0.066f, 0.092f },
                { 0.17f, 0.24f, 0.17f, 0.045f, 0.093f, 0.126f },
            };
            return &geometry;
        }
        case kind::prop_halfwall: {
            static const std::vector<part> geometry{
                { 0, 0, 1, 0.43f, 0, 0.16f },
                { 0, 0, 1, 0.35f, 0.16f, 0.69f },
                { 0, 0, 1, 0.41f, 0.69f, 0.77f },
            };
            return &geometry;
        }
        case kind::prop_ruinedwall: {
            static const std::vector<part> geometry{
                { 0, 0, 1, 0.35f, 0, 0.24f },
                { -0.33f, 0, 0.29f, 0.25f, 0.22f, 0.76f, false, false, false, false, 0.6f, 12 },
                { 0.19f, 0.025f, 0.23f, 0.25f, 0.2f, 0.51f, false, false, false, false, 0.6f, -19 },
                { 0.37f, -0.021f, 0.1f, 0.17f, 0.18f, 0.52f, false, false, false, false, 0.6f, 25 },
                { -0.06f, -0.08f, 0.16f, 0.12f, 0.12f, 0.48f, false, false, false, false, 0.6f, 56 },
            };
            return &geometry;
        }
        case kind::prop_overheadbars: {
            static const std::vector<part> geometry{
                { -0.37f, -0.38f, 0.062f, 0.065f, 0, 1.9f },
                { -0.37f, 0.38f, 0.062f, 0.065f, 0, 1.9f },
                { 0.37f, -0.38f, 0.062f, 0.065f, 0, 1.9f },
                { 0.37f, 0.38f, 0.062f, 0.065f, 0, 1.9f },
                { -0.37f, 0, 0.061f, 0.81f, 1.82f, 1.89f },
                { 0.37f, 0, 0.061f, 0.81f, 1.82f, 1.89f },
                { 0, -0.37f, 0.76f, 0.047f, 1.82f, 1.89f },
                { 0, -0.19f, 0.76f, 0.047f, 1.82f, 1.89f },
                { 0, 0, 0.76f, 0.047f, 1.82f, 1.89f },
                { 0, 0.19f, 0.76f, 0.047f, 1.82f, 1.89f },
                { 0, 0.37f, 0.76f, 0.047f, 1.82f, 1.89f },
            };
            return &geometry;
        }
        case kind::prop_slide: {
            static const std::vector<part> geometry{
                { -0.36f, -0.205f, 0.048f, 0.062f, 0, 1.25f },
                { -0.36f, 0.205f, 0.048f, 0.062f, 0, 1.25f },
                { -0.35f, 0, 0.24f, 0.45f, 1.15f, 1.22f },
                { 0.02f, 0, 1.19f, 0.43f, 0.64f, 0.71f, false, false, false, false, 0.6f, -53 },
                { 0.02f, -0.205f, 1.2f, 0.038f, 0.657f, 0.77f, false, false, false, false, 0.6f, -53 },
                { 0.02f, 0.205f, 1.2f, 0.038f, 0.657f, 0.77f, false, false, false, false, 0.6f, -53 },
                { -0.42f, 0, 0.068f, 0.4f, 0.19f, 0.235f },
                { -0.42f, 0, 0.068f, 0.4f, 0.42f, 0.465f },
                { -0.42f, 0, 0.068f, 0.4f, 0.65f, 0.695f },
                { -0.42f, 0, 0.068f, 0.4f, 0.88f, 0.925f },
                { -0.42f, 0, 0.068f, 0.4f, 1.1f, 1.145f },
            };
            return &geometry;
        }
        case kind::prop_manual_pump: {
            static const std::vector<part> geometry{
                { 0, 0, 0.33f, 0.27f, 0, 0.11f },
                { 0, 0, 0.142f, 0.14f, 0.11f, 1.06f, true },
                { 0, 0.135f, 0.1f, 0.24f, 0.82f, 0.91f },
                { -0.14f, 0.015f, 0.35f, 0.08f, 0.91f, 0.97f, false, false, false, false, 0.6f, 20 },
                { -0.32f, 0.015f, 0.125f, 0.078f, 0.94f, 1 },
            };
            return &geometry;
        }
        case kind::prop_bottle_cooler: {
            static const std::vector<part> geometry{
                { 0, -0.015f, 0.42f, 0.38f, 0, 0.8f },
                { 0, 0.188f, 0.3f, 0.035f, 0.49f, 0.71f },
                { 0, -0.017f, 0.34f, 0.34f, 0.77f, 1.15f, true },
                { 0, -0.017f, 0.32f, 0.32f, 1.1f, 1.24f, false, false, false, false, 0.6f, 0, true },
            };
            return &geometry;
        }
        case kind::prop_terminal: {
            static const std::vector<part> geometry{
                { 0, 0, 0.65f, 0.47f, 0, 0.75f },
                { 0, 0.081f, 0.6f, 0.18f, 0.79f, 1.28f },
                { 0, 0.075f, 0.65f, 0.4f, 0.71f, 0.83f },
                { 0, -0.13f, 0.58f, 0.12f, 0.75f, 1.32f },
                { 0, 0.209f, 0.47f, 0.14f, 0.828f, 0.855f },
            };
            return &geometry;
        }
        case kind::prop_kiosk: {
            static const std::vector<part> geometry{
                { 0, -0.02f, 0.58f, 0.46f, 0, 1.48f },
                { 0, 0.224f, 0.43f, 0.045f, 0.8f, 1.22f },
                { 0, 0.225f, 0.49f, 0.06f, 0.46f, 0.52f },
                { 0, 0.229f, 0.32f, 0.032f, 0.34f, 0.4f },
                { 0, 0.019f, 0.62f, 0.49f, 1.47f, 1.56f },
            };
            return &geometry;
        }
        case kind::prop_recycler: {
            static const std::vector<part> geometry{
                { 0, 0, 0.85f, 0.74f, 0, 1.18f },
                { 0, 0.38f, 0.58f, 0.035f, 0.33f, 0.85f },
                { 0, 0.017f, 0.88f, 0.75f, 1.04f, 1.22f },
                { 0, 0.414f, 0.46f, 0.048f, 0.83f, 0.99f },
            };
            return &geometry;
        }
        case kind::prop_pipe: {
            static const std::vector<part> geometry{
                { 0, 0, 0.46f, 0.46f, 0, 1.33f, true },
                { 0, 0, 0.54f, 0.53f, 0.06f, 0.18f, true },
                { 0, 0, 0.55f, 0.54f, 1.09f, 1.21f, true },
                { 0, 0, 0.5f, 0.49f, 1.31f, 1.4f, true },
            };
            return &geometry;
        }
        case kind::prop_motor: {
            static const std::vector<part> geometry{
                { 0, 0, 0.71f, 0.62f, 0, 0.14f },
                { 0, -0.07f, 0.49f, 0.51f, 0.12f, 0.73f, true, false, true },
                { 0, 0.23f, 0.3f, 0.15f, 0.24f, 0.67f, true, false, true },
                { 0, -0.03f, 0.37f, 0.18f, 0.69f, 0.83f },
                { 0.25f, -0.13f, 0.1f, 0.12f, 0.25f, 0.8f, true },
            };
            return &geometry;
        }
        case kind::prop_pulley: {
            static const std::vector<part> geometry{
                { 0, 0, 0.28f, 0.17f, 2.04f, 2.19f },
                { 0, 0.025f, 0.2f, 0.12f, 1.86f, 2.065f, true, false, true },
                { -0.073f, 0.063f, 0.028f, 0.027f, 0.61f, 1.99f, true },
                { 0.074f, 0.063f, 0.028f, 0.027f, 0.95f, 1.99f, true },
                { -0.067f, 0.063f, 0.1f, 0.085f, 0.56f, 0.7f, true, false, true },
            };
            return &geometry;
        }
        case kind::prop_pedestal: {
            static const std::vector<part> geometry{
                { 0, 0, 0.65f, 0.63f, 0, 0.16f, true },
                { 0, 0, 0.38f, 0.36f, 0.16f, 0.65f, true },
                { 0, 0, 0.63f, 0.6f, 0.65f, 0.79f, true },
                { 0, 0, 0.44f, 0.43f, 0.78f, 0.863f, true },
            };
            return &geometry;
        }
        case kind::prop_boom: {
            static const std::vector<part> geometry{
                { -0.42f, 0, 0.23f, 0.25f, 0, 1.1f },
                { -0.04f, 0, 0.96f, 0.076f, 0.85f, 0.975f },
                { 0.38f, 0, 0.07f, 0.071f, 0.04f, 0.89f },
            };
            return &geometry;
        }
        case kind::prop_buffer: {
            static const std::vector<part> geometry{
                { 0, 0, 0.88f, 0.67f, 0, 0.1f },
                { 0, 0.11f, 0.85f, 0.13f, 0.66f, 0.8f },
                { -0.3f, -0.045f, 0.12f, 0.15f, 0.1f, 0.73f, false, false, false, false, 0.6f, -15 },
                { 0.3f, -0.045f, 0.12f, 0.15f, 0.1f, 0.73f, false, false, false, false, 0.6f, 15 },
                { 0, 0.11f, 0.8f, 0.1f, 0.28f, 0.39f },
            };
            return &geometry;
        }
        case kind::prop_forge: {
            static const std::vector<part> geometry{
                { 0, 0, 0.93f, 0.83f, 0, 0.21f },
                { -0.39f, -0.06f, 0.16f, 0.69f, 0.21f, 1.18f },
                { 0.39f, -0.06f, 0.16f, 0.69f, 0.21f, 1.18f },
                { 0, -0.34f, 0.7f, 0.12f, 0.21f, 1.04f },
                { 0, -0.06f, 0.92f, 0.69f, 0.99f, 1.19f },
                { 0, -0.18f, 0.41f, 0.35f, 1.18f, 1.42f },
            };
            return &geometry;
        }
        case kind::prop_rack: {
            static const std::vector<part> geometry{
                { 0, 0, 0.77f, 0.64f, 0, 1.53f },
                { 0, 0.33f, 0.59f, 0.048f, 0.47f, 1.3f },
                { 0, 0.333f, 0.6f, 0.048f, 0.28f, 0.34f },
                { 0, 0, 0.82f, 0.68f, 1.48f, 1.57f },
                { -0.345f, 0.33f, 0.065f, 0.057f, 0.15f, 1.47f },
                { 0.345f, 0.33f, 0.065f, 0.057f, 0.15f, 1.47f },
            };
            return &geometry;
        }
        case kind::prop_cage: {
            static const std::vector<part> geometry{
                { -0.32f, -0.32f, 0.07f, 0.07f, 0, 2.12f },
                { -0.32f, 0.32f, 0.07f, 0.07f, 0, 2.12f },
                { 0.32f, -0.32f, 0.07f, 0.07f, 0, 2.12f },
                { 0.32f, 0.32f, 0.07f, 0.07f, 0, 2.12f },
                { -0.32f, 0, 0.052f, 0.69f, 0.25f, 0.31f },
                { 0.32f, 0, 0.052f, 0.69f, 0.25f, 0.31f },
                { 0, -0.32f, 0.69f, 0.052f, 0.25f, 0.31f },
                { 0, 0.32f, 0.69f, 0.052f, 0.25f, 0.31f },
                { -0.32f, 0, 0.052f, 0.69f, 0.84f, 0.9f },
                { 0.32f, 0, 0.052f, 0.69f, 0.84f, 0.9f },
                { 0, -0.32f, 0.69f, 0.052f, 0.84f, 0.9f },
                { 0, 0.32f, 0.69f, 0.052f, 0.84f, 0.9f },
                { -0.32f, 0, 0.052f, 0.69f, 1.46f, 1.52f },
                { 0.32f, 0, 0.052f, 0.69f, 1.46f, 1.52f },
                { 0, -0.32f, 0.69f, 0.052f, 1.46f, 1.52f },
                { 0, 0.32f, 0.69f, 0.052f, 1.46f, 1.52f },
                { -0.32f, 0, 0.052f, 0.69f, 2.02f, 2.08f },
                { 0.32f, 0, 0.052f, 0.69f, 2.02f, 2.08f },
                { 0, -0.32f, 0.69f, 0.052f, 2.02f, 2.08f },
                { 0, 0.32f, 0.69f, 0.052f, 2.02f, 2.08f },
            };
            return &geometry;
        }
        case kind::prop_cartilage: {
            static const std::vector<part> geometry{
                { 0, 0, 0.88f, 0.8f, 0, 0.41f, false, false, false, false, 0.6f, 0, true },
                { -0.18f, 0, 0.46f, 0.52f, 0.26f, 0.71f, false, false, false, false, 0.6f, 0, true },
                { 0.2f, -0.14f, 0.54f, 0.51f, 0.07f, 0.48f, false, false, false, false, 0.6f, 0, true },
            };
            return &geometry;
        }
        case kind::prop_column_machine: {
            static const std::vector<part> geometry{
                { 0, 0, 0.73f, 0.64f, 0, 0.19f },
                { -0.19f, -0.15f, 0.27f, 0.26f, 0.18f, 1.39f, true },
                { 0.19f, -0.15f, 0.27f, 0.26f, 0.18f, 1.39f, true },
                { -0.19f, 0.15f, 0.27f, 0.26f, 0.18f, 1.39f, true },
                { 0.19f, 0.15f, 0.27f, 0.26f, 0.18f, 1.39f, true },
                { 0, 0, 0.7f, 0.62f, 1.38f, 1.49f },
            };
            return &geometry;
        }
        case kind::prop_scrap: {
            static const std::vector<part> geometry{
                { 0, 0, 0.64f, 0.58f, 0, 0.19f, false, false, false, false, 0.6f, 13 },
                { -0.18f, 0.12f, 0.43f, 0.34f, 0.125f, 0.38f, false, false, false, false, 0.6f, -17 },
                { 0.2f, -0.17f, 0.29f, 0.31f, 0.09f, 0.33f, false, false, false, false, 0.6f, 23 },
            };
            return &geometry;
        }
        case kind::prop_basin: {
            static const std::vector<part> geometry{
                { 0, 0, 0.85f, 0.5f, 0, 0.25f },
                { 0, 0, 0.83f, 0.48f, 0.25f, 0.65f, false, false, false, false, 0.6f, 0, false, part_form::basin },
            };
            return &geometry;
        }
        case kind::prop_hive: {
            static const std::vector<part> geometry{
                { 0, 0, 0.55f, 0.52f, 0, 0.2f, false, false, false, false, 0.6f, 0, true },
                { 0, 0, 0.62f, 0.6f, 0.02f, 0.8f, false, false, false, false, 0.6f, 0, true },
                { 0, 0.025f, 0.4f, 0.42f, 0.55f, 0.98f, false, false, false, false, 0.6f, 0, true },
            };
            return &geometry;
        }
        case kind::prop_barrier: {
            static const std::vector<part> geometry{
                { 0, 0, 0.9f, 0.065f, 0, 2.18f },
                { -0.442f, 0, 0.07f, 0.1f, 0, 2.2f },
                { 0.442f, 0, 0.07f, 0.1f, 0, 2.2f },
            };
            return &geometry;
        }
        case kind::prop_memorial: {
            static const std::vector<part> geometry{
                { 0, 0, 0.4f, 0.32f, 0, 0.12f },
                { 0, 0, 0.08f, 0.08f, 0.12f, 0.94f },
                { 0, 0, 0.53f, 0.08f, 0.55f, 0.63f },
                { 0, 0, 0.34f, 0.34f, 0.91f, 1.18f, false, false, false, false, 0.6f, 0, true },
            };
            return &geometry;
        }
        case kind::prop_log: {
            static const std::vector<part> geometry{
                { 0, 0, 0.39f, 0.89f, 0.02f, 0.43f, true, false, true },
                { 0.12f, 0.17f, 0.12f, 0.17f, 0.18f, 0.42f, false, false, false, false, 0.6f, -43 },
            };
            return &geometry;
        }
        case kind::prop_dam: {
            static const std::vector<part> geometry{
                { -0.2f, 0.04f, 0.39f, 0.89f, 0, 0.45f, true, false, true },
                { 0.18f, 0.01f, 0.39f, 0.8f, 0, 0.47f, true, false, true },
                { 0, -0.09f, 0.49f, 0.66f, 0.25f, 0.8f, false, false, false, false, 0.6f, 0, true },
            };
            return &geometry;
        }
        case kind::prop_cushion: {
            static const std::vector<part> geometry{
                { 0, 0, 0.81f, 0.81f, 0, 0.45f, false, false, false, false, 0.6f, 0, true },
                { 0, -0.14f, 0.67f, 0.55f, 0.13f, 0.69f, false, false, false, false, 0.6f, 0, true },
            };
            return &geometry;
        }
        case kind::prop_bike: {
            static const std::vector<part> geometry{
                { -0.34f, 0, 0.07f, 0.074f, 0, 0.72f },
                { 0.34f, 0, 0.07f, 0.074f, 0, 0.72f },
                { 0, 0, 0.73f, 0.075f, 0.68f, 0.76f },
            };
            return &geometry;
        }
        case kind::prop_medical_chair: {
            static const std::vector<part> geometry{
                { 0, 0, 0.5f, 0.45f, 0, 0.12f },
                { 0, 0, 0.18f, 0.19f, 0.12f, 0.49f, true },
                { 0, 0.11f, 0.51f, 0.54f, 0.49f, 0.61f },
                { 0, -0.205f, 0.48f, 0.14f, 0.58f, 1.13f },
                { 0.245f, -0.15f, 0.055f, 0.056f, 0.1f, 1.47f },
                { 0.13f, -0.15f, 0.31f, 0.06f, 1.43f, 1.49f },
                { 0.015f, -0.15f, 0.22f, 0.2f, 1.35f, 1.49f, true },
            };
            return &geometry;
        }
        case kind::prop_antenna: {
            static const std::vector<part> geometry{
                { 0, 0, 0.43f, 0.4f, 0, 0.16f },
                { 0, 0, 0.11f, 0.11f, 0.16f, 2.16f, true },
                { 0, 0, 0.56f, 0.07f, 1.79f, 1.87f },
                { 0, 0, 0.36f, 0.065f, 2.04f, 2.1f },
                { 0.22f, 0, 0.045f, 0.048f, 1.65f, 2.01f },
                { -0.22f, 0, 0.045f, 0.048f, 1.65f, 2.01f },
            };
            return &geometry;
        }
        case kind::prop_lantern: {
            static const std::vector<part> geometry{
                { 0, 0, 0.51f, 0.48f, 0, 0.18f, true },
                { 0, 0, 0.46f, 0.46f, 0.17f, 0.58f, true },
                { 0, 0.128f, 0.34f, 0.24f, 0.54f, 0.91f, true, false, true },
                { 0, -0.015f, 0.58f, 0.57f, 0.08f, 0.114f, true },
                { 0, -0.015f, 0.58f, 0.57f, 0.15f, 0.184f, true },
                { 0, -0.015f, 0.58f, 0.57f, 0.22f, 0.254f, true },
                { 0, -0.015f, 0.58f, 0.57f, 0.29f, 0.324f, true },
                { 0, -0.015f, 0.58f, 0.57f, 0.36f, 0.394f, true },
                { 0, -0.015f, 0.58f, 0.57f, 0.43f, 0.464f, true },
            };
            return &geometry;
        }
        case kind::prop_anemone: {
            static const std::vector<part> geometry{
                { 0, 0, 0.62f, 0.58f, 0, 0.28f, false, false, false, false, 0.6f, 0, true },
                { 0, 0, 0.32f, 0.3f, 0.18f, 0.47f, true },
                { 0.22f, 0, 0.064f, 0.069f, 0.3f, 0.64f, false, false, false, false, 0.6f, -20 },
                { 0.1685f, 0.135f, 0.064f, 0.069f, 0.3f, 0.68f, false, false, false, false, 0.6f, -15 },
                { 0.0382f, 0.2068f, 0.064f, 0.069f, 0.3f, 0.72f, false, false, false, false, 0.6f, -3 },
                { -0.11f, 0.1819f, 0.064f, 0.069f, 0.3f, 0.64f, false, false, false, false, 0.6f, 10 },
                { -0.2067f, 0.0718f, 0.064f, 0.069f, 0.3f, 0.68f, false, false, false, false, 0.6f, 19 },
                { -0.2067f, -0.0718f, 0.064f, 0.069f, 0.3f, 0.72f, false, false, false, false, 0.6f, 19 },
                { -0.11f, -0.1819f, 0.064f, 0.069f, 0.3f, 0.64f, false, false, false, false, 0.6f, 10 },
                { 0.0382f, -0.2068f, 0.064f, 0.069f, 0.3f, 0.68f, false, false, false, false, 0.6f, -3 },
                { 0.1685f, -0.135f, 0.064f, 0.069f, 0.3f, 0.72f, false, false, false, false, 0.6f, -15 },
            };
            return &geometry;
        }
        case kind::prop_sponge: {
            static const std::vector<part> geometry{
                { -0.18f, -0.025f, 0.18f, 0.15f, 0, 0.56f, false, false, false, false, 0.6f, 0, false, part_form::basin },
                { 0.04f, -0.08f, 0.24f, 0.2f, 0, 0.75f, false, false, false, false, 0.6f, 0, false, part_form::basin },
                { 0.17f, 0.085f, 0.18f, 0.19f, 0, 0.49f, false, false, false, false, 0.6f, 0, false, part_form::basin },
                { -0.07f, 0.15f, 0.16f, 0.17f, 0, 0.4f, false, false, false, false, 0.6f, 0, false, part_form::basin },
            };
            return &geometry;
        }
        case kind::prop_bucket: {
            static const std::vector<part> geometry{
                { 0, 0, 0.47f, 0.46f, 0, 0.47f, false, false, false, false, 0.6f, 0, false, part_form::basin },
                { 0, 0, 0.19f, 0.18f, 0.01f, 0.048f, true },
            };
            return &geometry;
        }
        case kind::prop_press: {
            static const std::vector<part> geometry{
                { 0, 0, 0.69f, 0.62f, 0, 0.17f },
                { -0.29f, 0, 0.095f, 0.14f, 0.17f, 1.66f },
                { 0.29f, 0, 0.095f, 0.14f, 0.17f, 1.66f },
                { 0, 0, 0.67f, 0.45f, 1.53f, 1.66f },
                { 0, 0, 0.11f, 0.1f, 0.65f, 1.63f, true },
                { 0, 0, 0.57f, 0.07f, 1.18f, 1.24f },
                { 0, 0, 0.47f, 0.44f, 0.25f, 0.58f, false, false, false, false, 0.6f, 0, false, part_form::basin },
            };
            return &geometry;
        }
        case kind::prop_mound_short: {
            static const std::vector<part> geometry{
                { 0, 0, 0.94f, 0.84f, 0, 0.26f, false, false, false, false, 0.6f, 0, false, part_form::rock },
            };
            return &geometry;
        }
        case kind::prop_mound_mid: {
            static const std::vector<part> geometry{
                { 0, 0, 0.95f, 0.91f, 0, 0.61f, false, false, false, false, 0.6f, 0, false, part_form::rock },
            };
            return &geometry;
        }
        case kind::prop_mound_tall: {
            static const std::vector<part> geometry{
                { 0, 0, 0.96f, 0.93f, 0, 1.1f, false, false, false, false, 0.6f, 0, false, part_form::rock },
            };
            return &geometry;
        }
        case kind::prop_vent_grate: {
            static const std::vector<part> geometry{
                { -0.34f, 0, 0.07f, 0.64f, 0, 0.043f },
                { 0.34f, 0, 0.07f, 0.64f, 0, 0.043f },
                { 0, -0.29f, 0.63f, 0.056f, 0, 0.043f },
                { 0, 0.29f, 0.63f, 0.056f, 0, 0.043f },
                { -0.24f, 0, 0.032f, 0.54f, 0.013f, 0.043f },
                { -0.12f, 0, 0.032f, 0.54f, 0.013f, 0.043f },
                { 0, 0, 0.032f, 0.54f, 0.013f, 0.043f },
                { 0.12f, 0, 0.032f, 0.54f, 0.013f, 0.043f },
                { 0.24f, 0, 0.032f, 0.54f, 0.013f, 0.043f },
            };
            return &geometry;
        }
        case kind::prop_glassgate: {
            static const std::vector<part> geometry{
                { -0.45f, 0, 0.075f, 0.085f, 0, 2.16f },
                { 0.45f, 0, 0.075f, 0.085f, 0, 2.16f },
                { 0, 0, 0.83f, 0.086f, 0, 0.085f },
                { 0, 0, 0.83f, 0.086f, 2.08f, 2.16f },
                { 0, 0, 0.82f, 0.043f, 0.08f, 2.08f },
            };
            return &geometry;
        }
        case kind::prop_wire: {
            static const std::vector<part> geometry{
                { -0.455f, 0, 0.076f, 0.08f, 0, 1.12f },
                { 0.455f, 0, 0.076f, 0.08f, 0, 1.12f },
                { 0, 0, 1, 0.027f, 0.32f, 0.347f },
                { 0, 0, 1, 0.027f, 0.59f, 0.617f },
                { 0, 0, 1, 0.027f, 0.89f, 0.917f },
            };
            return &geometry;
        }
        case kind::prop_barbed: {
            static const std::vector<part> geometry{
                { -0.455f, 0, 0.076f, 0.08f, 0, 1.12f },
                { 0.455f, 0, 0.076f, 0.08f, 0, 1.12f },
                { 0, 0, 1, 0.027f, 0.32f, 0.347f },
                { 0, 0, 1, 0.027f, 0.59f, 0.617f },
                { 0, 0, 1, 0.027f, 0.89f, 0.917f },
                { -0.35f, 0, 0.027f, 0.04f, 0.286f, 0.375f, false, false, false, false, 0.6f, -27 },
                { -0.35f, 0, 0.027f, 0.04f, 0.556f, 0.645f, false, false, false, false, 0.6f, -27 },
                { -0.35f, 0, 0.027f, 0.04f, 0.856f, 0.945f, false, false, false, false, 0.6f, -27 },
                { -0.15f, 0, 0.027f, 0.04f, 0.286f, 0.375f, false, false, false, false, 0.6f, -27 },
                { -0.15f, 0, 0.027f, 0.04f, 0.556f, 0.645f, false, false, false, false, 0.6f, -27 },
                { -0.15f, 0, 0.027f, 0.04f, 0.856f, 0.945f, false, false, false, false, 0.6f, -27 },
                { 0.05f, 0, 0.027f, 0.04f, 0.286f, 0.375f, false, false, false, false, 0.6f, -27 },
                { 0.05f, 0, 0.027f, 0.04f, 0.556f, 0.645f, false, false, false, false, 0.6f, -27 },
                { 0.05f, 0, 0.027f, 0.04f, 0.856f, 0.945f, false, false, false, false, 0.6f, -27 },
                { 0.25f, 0, 0.027f, 0.04f, 0.286f, 0.375f, false, false, false, false, 0.6f, -27 },
                { 0.25f, 0, 0.027f, 0.04f, 0.556f, 0.645f, false, false, false, false, 0.6f, -27 },
                { 0.25f, 0, 0.027f, 0.04f, 0.856f, 0.945f, false, false, false, false, 0.6f, -27 },
                { 0.42f, 0, 0.027f, 0.04f, 0.286f, 0.375f, false, false, false, false, 0.6f, -27 },
                { 0.42f, 0, 0.027f, 0.04f, 0.556f, 0.645f, false, false, false, false, 0.6f, -27 },
                { 0.42f, 0, 0.027f, 0.04f, 0.856f, 0.945f, false, false, false, false, 0.6f, -27 },
            };
            return &geometry;
        }
        case kind::prop_ropefence: {
            static const std::vector<part> geometry{
                { -0.455f, 0, 0.085f, 0.08f, 0, 0.92f },
                { 0.455f, 0, 0.085f, 0.08f, 0, 0.92f },
                { 0, 0, 1, 0.05f, 0.61f, 0.66f },
            };
            return &geometry;
        }
        case kind::prop_railfence: {
            static const std::vector<part> geometry{
                { -0.455f, 0, 0.115f, 0.08f, 0, 1.03f },
                { 0.455f, 0, 0.115f, 0.08f, 0, 1.03f },
                { 0, 0, 1, 0.105f, 0.31f, 0.415f },
                { 0, 0, 1, 0.105f, 0.77f, 0.875f },
            };
            return &geometry;
        }
        case kind::prop_portcullis: {
            static const std::vector<part> geometry{
                { -0.455f, 0, 0.076f, 0.08f, 0, 2.15f },
                { 0.455f, 0, 0.076f, 0.08f, 0, 2.15f },
                { 0, 0, 1, 0.052f, 0.57f, 0.622f },
                { 0, 0, 1, 0.052f, 1.25f, 1.302f },
                { 0, 0, 1, 0.052f, 1.96f, 2.012f },
                { -0.31f, 0, 0.053f, 0.055f, 0.11f, 2.13f },
                { -0.155f, 0, 0.053f, 0.055f, 0.11f, 2.13f },
                { 0, 0, 0.053f, 0.055f, 0.11f, 2.13f },
                { 0.155f, 0, 0.053f, 0.055f, 0.11f, 2.13f },
                { 0.31f, 0, 0.053f, 0.055f, 0.11f, 2.13f },
            };
            return &geometry;
        }
        case kind::prop_shelter: {
            static const std::vector<part> geometry{
                { -0.25f, 0, 0.99f, 0.85f, 0.73f, 0.78f, false, false, false, false, 0.6f, 57 },
                { 0.25f, 0, 0.99f, 0.85f, 0.73f, 0.78f, false, false, false, false, 0.6f, -57 },
                { 0, 0, 0.07f, 0.93f, 1.12f, 1.18f },
                { -0.43f, -0.39f, 0.055f, 0.065f, 0, 0.21f },
                { 0.43f, -0.39f, 0.055f, 0.065f, 0, 0.21f },
            };
            return &geometry;
        }
        case kind::prop_mortar: {
            static const std::vector<part> geometry{
                { 0, 0, 0.46f, 0.42f, 0, 0.068f },
                { 0, -0.042f, 0.114f, 0.12f, 0.06f, 0.7f, false, false, false, false, 0.6f, -23 },
                { -0.2f, -0.06f, 0.042f, 0.046f, 0.03f, 0.35f, false, false, false, false, 0.6f, -28 },
                { 0.21f, -0.06f, 0.042f, 0.046f, 0.03f, 0.35f, false, false, false, false, 0.6f, 29 },
            };
            return &geometry;
        }
        case kind::prop_lean_to: {
            // The source side view shows a single sloping tarp with an open front.
            static const std::vector<part> geometry{
                { 0, 0, 1.16f, .90f, .60f, .65f, false, false, false, false, .6f, 43 },
                { .405f, -.38f, .055f, .055f, 0, 1.00f },
                { .405f, .38f, .055f, .055f, 0, 1.00f },
                { -.405f, -.38f, .055f, .055f, 0, .29f },
                { -.405f, .38f, .055f, .055f, 0, .29f }
            };
            return &geometry;
        }
        case kind::prop_chainmesh: {
            static const std::vector<part> geometry = [] {
                std::vector<part> pieces{
                    { -.46f, 0, .07f, .07f, 0, 1.97f },
                    { .46f, 0, .07f, .07f, 0, 1.97f },
                    { 0, 0, 1.f, .053f, 1.91f, 1.97f },
                    { 0, 0, 1.f, .053f, .09f, .145f }
                };
                for( int i = -1; i <= 12; ++i ) {
                    const float offset = i * .20f;
                    const float left = std::max( -.46f, .12f - offset );
                    const float right = std::min( .46f, 1.93f - offset );
                    if( left >= right ) {
                        continue;
                    }
                    const float x = ( left + right ) * .5f;
                    const float y = x + offset;
                    const float half = ( right - left ) * .70710678f;
                    for( const float direction : { -1.f, 1.f } ) {
                        pieces.push_back( { direction * x, 0, .015f, .022f, y - half, y + half,
                                           false, false, false, false, .6f, -45.f * direction } );
                    }
                }
                return pieces;
            }();
            return &geometry;
        }
        case kind::prop_detector: {
            static const std::vector<part> geometry{
                { -0.37f, 0, 0.14f, 0.25f, 0, 1.97f },
                { 0.37f, 0, 0.14f, 0.25f, 0, 1.97f },
                { 0, 0, 0.88f, 0.26f, 1.83f, 2.04f },
                { 0.371f, 0.136f, 0.1f, 0.04f, 1.35f, 1.69f },
            };
            return &geometry;
        }
        default:
            return nullptr;
    }
}
} // namespace
