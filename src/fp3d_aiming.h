#pragma once

#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>

#include "coordinates.h"

namespace fp3d_compositor::aiming
{
void set_active( bool active );
bool active();
// Creature under the aim cursor (the locked target).
void set_target( std::optional<tripoint_abs_ms> position );
std::optional<tripoint_abs_ms> target();
// Tile the aim cursor rests on, creature or not. Only used to place the modern
// HUD aiming markers; selection and hit resolution stay with the native code.
void set_aim_tile( std::optional<tripoint_abs_ms> position );
std::optional<tripoint_abs_ms> aim_tile();
std::optional<tripoint_abs_ms> pick( const point &mouse );
}
#endif
