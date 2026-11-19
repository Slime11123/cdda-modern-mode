#pragma once

#if defined(TILES) && defined(ENABLE_FP3D)
#include <optional>

#include "coordinates.h"

namespace fp3d_compositor::aiming
{
void set_active( bool active );
bool active();
void set_target( std::optional<tripoint_abs_ms> position );
std::optional<tripoint_abs_ms> target();
std::optional<tripoint_abs_ms> pick( const point &mouse );
}
#endif
