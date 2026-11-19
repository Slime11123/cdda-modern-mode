#pragma once
#ifndef CATA_SRC_FP3D_HUD_H
#define CATA_SRC_FP3D_HUD_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
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
struct panel_setting {
    const char *option;
    const char *label;
};
inline constexpr std::array<panel_setting, 10> panel_settings = {{
        { "FP3D_HUD_MAP_SCALE", "小地图" },
        { "FP3D_HUD_BODY_SCALE", "身体血量" },
        { "FP3D_HUD_EFFECTS_SCALE", "状态效果" },
        { "FP3D_HUD_COMPASS_SCALE", "指南针" },
        { "FP3D_HUD_TIME_SCALE", "时间与温度" },
        { "FP3D_HUD_MISSION_SCALE", "任务目标" },
        { "FP3D_HUD_STATUS_SCALE", "生存状态" },
        { "FP3D_HUD_LOG_SCALE", "消息日志" },
        { "FP3D_HUD_HOTBAR_SCALE", "快捷栏" },
        { "FP3D_HUD_TARGET_SCALE", "观察信息" }
    }};
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
    struct map_marker {
        float heading = 0.0f;
        int distance = 0;
        unsigned char red = 255;
        unsigned char green = 220;
        unsigned char blue = 80;
    };
    std::vector<map_marker> map_markers;
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
std::vector<data::message_entry> message_rows( const std::vector<data::message_entry> &messages,
        int columns, int rows );
std::vector<data::message_entry> inspect( const avatar &you, const map &here,
        const fp3d_targeting::target &target );
std::string health_text( const avatar &you, const bodypart_id &part );
std::string stamina_text( const avatar &you );
bool navigation_enabled();
void set_navigation_enabled( bool enabled );
}
#endif
#endif
