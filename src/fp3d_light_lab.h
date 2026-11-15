#pragma once
#ifndef CATA_SRC_FP3D_LIGHT_LAB_H
#define CATA_SRC_FP3D_LIGHT_LAB_H
#if defined( ENABLE_FP3D )
class avatar;
class map;
namespace fp3d_light_lab
{
struct status {
    bool ready = false;
    bool powered_fixture = false;
    bool inactive_fixture = false;
    bool isolated_fixture = false;
    int charged_kj = 0;
    int grid_kj = 0;
    int flashlight_charges = 0;
    int phone_charges = 0;
};
status provision( map &here, avatar &player );
}
#endif
#endif
