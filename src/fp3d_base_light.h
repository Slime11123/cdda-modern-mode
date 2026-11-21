#pragma once
#ifndef CATA_SRC_FP3D_BASE_LIGHT_H
#define CATA_SRC_FP3D_BASE_LIGHT_H

#include <array>

namespace fp3d_base_light
{
// Keep the existing Off lighting parameters available to both renderers.
inline constexpr float direct = .75f;
inline constexpr std::array<float, 4> direction{ -.4f, .8f, .5f, 0.f };
inline float ambient( float daylight, float clouds, float lightning )
{
    return .35f * ( 1 - clouds * daylight * .4f ) + lightning * .5f;
}
} // namespace fp3d_base_light
#endif // CATA_SRC_FP3D_BASE_LIGHT_H
