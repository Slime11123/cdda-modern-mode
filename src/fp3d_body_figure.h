#pragma once
#ifndef CATA_SRC_FP3D_BODY_FIGURE_H
#define CATA_SRC_FP3D_BODY_FIGURE_H

#include <array>
#include <vector>

#include "point.h"

namespace fp3d_body_figure
{
inline constexpr int width = 100;
inline constexpr int height = 200;

struct region {
    const char *id;
    std::vector<point> outline;
    point hit_min;
    point hit_max;
};

// Front view: the character's left limbs are on the viewer's right.
inline const std::array<region, 6> regions = {{
    { "head", { { 43, 3 }, { 57, 3 }, { 62, 8 }, { 62, 19 }, { 57, 27 },
        { 43, 27 }, { 38, 19 }, { 38, 8 } }, { 36, 1 }, { 64, 28 } },
    { "torso", { { 42, 29 }, { 58, 29 }, { 59, 34 }, { 70, 39 }, { 67, 63 },
        { 61, 80 }, { 65, 99 }, { 60, 110 }, { 40, 110 }, { 35, 99 },
        { 39, 80 }, { 33, 63 }, { 30, 39 }, { 41, 34 } }, { 31, 28 }, { 69, 111 } },
    { "arm_l", { { 72, 39 }, { 78, 44 }, { 84, 64 }, { 89, 87 }, { 90, 109 },
        { 86, 115 }, { 80, 111 }, { 80, 90 }, { 75, 72 }, { 68, 51 } }, { 69, 37 }, { 92, 116 } },
    { "arm_r", { { 32, 51 }, { 25, 72 }, { 20, 90 }, { 20, 111 }, { 14, 115 },
        { 10, 109 }, { 11, 87 }, { 16, 64 }, { 22, 44 }, { 28, 39 } }, { 8, 37 }, { 31, 116 } },
    { "leg_l", { { 52, 113 }, { 64, 111 }, { 67, 132 }, { 65, 154 }, { 69, 180 },
        { 74, 191 }, { 70, 197 }, { 57, 196 }, { 55, 180 }, { 51, 153 },
        { 50, 131 } }, { 50, 111 }, { 76, 199 } },
    { "leg_r", { { 50, 131 }, { 49, 153 }, { 45, 180 }, { 43, 196 }, { 30, 197 },
        { 26, 191 }, { 31, 180 }, { 35, 154 }, { 33, 132 }, { 36, 111 },
        { 48, 113 } }, { 24, 111 }, { 50, 199 } }
}};
} // namespace fp3d_body_figure

#endif // CATA_SRC_FP3D_BODY_FIGURE_H
