#pragma once
#ifndef CATA_SRC_FP3D_HUD_H
#define CATA_SRC_FP3D_HUD_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>
#include <string>
#include <vector>
class avatar;
namespace fp3d_hud
{
struct data {
    std::string status;
    std::optional<float> navigation_heading;
    int navigation_distance = 0;
    int navigation_z = 0;
    std::vector<std::string> slots;
    std::vector<std::string> expanded;
};
data capture( const avatar &you );
bool navigation_enabled();
void set_navigation_enabled( bool enabled );
}
#endif
#endif
