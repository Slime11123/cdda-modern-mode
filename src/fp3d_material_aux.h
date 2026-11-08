#pragma once
#ifndef CATA_SRC_FP3D_MATERIAL_AUX_H
#define CATA_SRC_FP3D_MATERIAL_AUX_H

#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdint>
#include <string>
#include <vector>

namespace fp3d_material_aux
{
enum class category : std::uint8_t {
    unknown, stone, road, soil, vegetation, wood, metal, glass, water, fabric
};

struct surface {
    category type = category::unknown;
    float roughness = 0.85f;
    float metallic = 0.0f;
    // A deliberately weak height-derived normal, not a hand-authored PBR map.
    float normal_strength = 0.0f;
};

struct maps {
    int width = 0;
    int height = 0;
    surface material;
    // Normal is RGBA8 tangent-space; neutral is (128,128,255).
    std::vector<std::uint8_t> normal_rgba;
    // Packed (AO=255, roughness, metallic, source alpha).
    std::vector<std::uint8_t> orm_rgba;
};

// Pure, read-only helpers.  Do not sample hidden game entities or gameplay RNG.
surface classify( const std::string &material_key );
maps generate( const std::string &material_key, const std::uint8_t *rgba,
               int width, int height, int pitch );
} // namespace fp3d_material_aux
#endif
#endif // CATA_SRC_FP3D_MATERIAL_AUX_H
