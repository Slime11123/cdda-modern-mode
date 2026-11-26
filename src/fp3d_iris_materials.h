#pragma once
#ifndef CATA_SRC_FP3D_IRIS_MATERIALS_H
#define CATA_SRC_FP3D_IRIS_MATERIALS_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fp3d_snapshot
{
struct cell;
}

namespace fp3d_iris
{
struct material_rule;

enum class material_surface { solid, transparent, water };

struct virtual_material {
    std::string native_id;
    std::string shader_id;
    std::map<std::string, std::string> states;
    material_surface surface = material_surface::solid;
    bool deep_water = false;
    bool flowing_water = false;
    bool salt_water = false;
    bool sewage = false;
    bool submerged_water = false;
};

virtual_material terrain_material( const fp3d_snapshot::cell &tile );

// Build one map per property document, after any conditional rules have been
// resolved. Unevaluated rules and tags do not match.
class material_id_map
{
    public:
        explicit material_id_map( const std::vector<material_rule> &rules );
        std::optional<int> resolve( const virtual_material &material ) const;
        size_t unsupported_selectors() const;

    private:
        struct selector {
            int id;
            std::map<std::string, std::vector<std::string>> states;
        };
        std::map<std::string, std::vector<selector>> selectors_;
        size_t unsupported_ = 0;
};
} // namespace fp3d_iris
#endif
#endif
