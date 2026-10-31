#pragma once
#ifndef CATA_SRC_FP3D_SKY_H
#define CATA_SRC_FP3D_SKY_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include "calendar.h"

namespace fp3d_sky
{
struct state {
    bool visible = false;
    float daylight = 0;
    float twilight = 0;
    float sun_x = 0;
    float sun_y = -1;
    float sun_z = 0;
    float moonlight = 0;
};

// Sample native astronomy on the game thread; rendering owns only these values.
state sample( const time_point &time, bool visible );
// Draw a camera-centred background using the scene's rotation/projection.
void draw( const state &sky );
}
#endif
#endif
