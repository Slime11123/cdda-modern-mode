#pragma once
#ifndef CATA_SRC_FP3D_HUD_H
#define CATA_SRC_FP3D_HUD_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>
#include <string>
#include <vector>
#include "color.h"
#include "fp3d_inventory.h"
#include "type_id.h"
class avatar;
class map;
namespace fp3d_targeting
{
struct target;
}
namespace fp3d_hud
{
struct data {
    struct message_entry {
        std::string text;
        nc_color color;
    };
    struct status_entry {
        std::string label;
        std::string value;
        nc_color color;
    };
    struct body_region {
        std::string label;
        int current = 0;
        int maximum = 0;
        nc_color color;
    };
    std::string status;
    std::string day_time;
    std::string temperature;
    std::string mission;
    int active_slot = 0;
    std::optional<float> navigation_heading;
    int navigation_distance = 0;
    int navigation_z = 0;
    std::vector<std::string> slots;
    std::vector<fp3d_inventory::slot_icon> hotbar_icons;
    std::vector<message_entry> messages;
    std::vector<status_entry> status_entries;
    struct effect_icon {
        std::string tile_id;
        std::string label;
    };
    std::vector<effect_icon> effect_icons;
    // Head, torso, left/right arm, left/right leg, in front-view anatomical order.
    std::vector<body_region> body_regions;
    std::vector<std::string> expanded;
};
data capture( avatar &you );
std::vector<data::message_entry> inspect( const avatar &you, const map &here,
        const fp3d_targeting::target &target );
std::string health_text( const avatar &you, const bodypart_id &part );
std::string stamina_text( const avatar &you );
bool navigation_enabled();
void set_navigation_enabled( bool enabled );
}
#endif
#endif
