#pragma once
#ifndef CATA_SRC_FP3D_GPU_WORLD_QA_H
#define CATA_SRC_FP3D_GPU_WORLD_QA_H

// Migration evidence only. Draw selected terrain geometry from a real
// snapshot, using the genuine game's colour atlas. Never replace the actual
// world, enable incomplete gameplay, or run readbacks in normal rendering.
#include "fp3d_camera.h"
#include "fp3d_compositor.h"
#include "fp3d_base_light.h"
#include "fp3d_weather.h"
#include "fp3d_cell_index.h"
#include "fp3d_scene.h"
#include "fp3d_ramp.h"
#include "fp3d_snapshot.h"
#include "fp3d_regression.h"
#include "fp3d_materials.h"
#include "fp3d_gpu_textured.h"
#include "fp3d_gpu_sky.h"
#include "fp3d_gpu_halo.h"
#include "fp3d_gpu_photon.h"
#include "fp3d_gpu_pbr_atlas.h"
#include "fp3d_gpu_occlusion.h"
#include "fp3d_gpu_presentation.h"
#include "fp3d_mcshader.h"
#include "fp3d_geometry_curved.h"
#include "fp3d_model_mesh_cache.h"
#include "fp3d_gpu_instance_pages.h"
#include "fp3d_gpu_terrain_compute.h"
#include "fp3d_shore_cache.h"
#include "fp3d_structure_geometry.h"
#include "fp3d_vehicle_roof_visibility.h"
#include "fp3d_local_lights.h"
#include "fp3d_iris_frame.h"
#include "fp3d_iris_gpu_graph.h"
#include "fp3d_iris_gpu_images.h"
#include "fp3d_iris_layout.h"
#include "fp3d_iris_materials.h"
#include "fp3d_iris_block_light.h"
#include "fp3d_iris_scene_post.h"
#include "fp3d_gpu_base_shaders.inc"
#include "debug.h"
#include <chrono>
#include <iterator>
#include <map>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace fp3d_gpu_world_qa
{
using kind = fp3d_snapshot::terrain_kind;
using iris_caster = fp3d_shaderpacks::iris_shadow_caster;
enum class shape {
    floor, capped_wall, open_wall, ramp, door, window, curtain, stairs, pad,
    landscape_ground, landscape_facade, landscape_facade_tiled, plant, grass, furniture,
    furniture_glass, furniture_light_head, kiln_fire, water, ceiling, radio_tower,
    built_terrain, fence, chain_gate, glass_barrier, barrier_pane, climb, stair_stringers, obstacle
};
enum class reflective_surface { none, water_shallow, water_deep, asphalt, concrete };

struct model_key {
    shape type;
    float param_x = 0;
    float param_y = 0;
    float building_height = 0;
    kind door_kind = kind::floor;
    bool upper_floor = false;
    unsigned quarter_turns = 0;
    int stair_rows = 1;
    int stair_south = 0;
    std::string furniture_id{};
    fp3d_furniture::kind furniture_shape = fp3d_furniture::kind::table;
    reflective_surface surface = reflective_surface::none;
    bool water_sky = false;
    bool open_structure = false;
    std::string terrain_id{};
    unsigned fence_links = 0;
    unsigned bed_links = 0;
    bool glass_door = false;
    float door_swing = 0.f;
    bool blueprint = false;
    bool metal_deck = false;
    std::optional<int> iris_terrain_material{};
    bool iris_terrain_solid = false;
    std::optional<int> iris_water_material{};

    bool operator<( const model_key &other ) const {
        return std::tie( type, param_x, param_y, building_height, door_kind, upper_floor, quarter_turns,
                         stair_rows, stair_south, furniture_id, furniture_shape, surface, water_sky, open_structure,
                         terrain_id, fence_links, bed_links, glass_door, door_swing, blueprint, metal_deck,
                         iris_terrain_material, iris_terrain_solid, iris_water_material ) <
               std::tie( other.type, other.param_x, other.param_y, other.building_height,
                         other.door_kind, other.upper_floor,
                         other.quarter_turns, other.stair_rows, other.stair_south, other.furniture_id,
                         other.furniture_shape, other.surface, other.water_sky, other.open_structure,
                         other.terrain_id, other.fence_links, other.bed_links, other.glass_door, other.door_swing,
                         other.blueprint, other.metal_deck, other.iris_terrain_material,
                         other.iris_terrain_solid, other.iris_water_material );
    }
};

enum class pane_shape : unsigned { window, turned_window, full_glass, turned_glass, open_glass_in, open_glass_out,
                                   turned_open_glass_in, turned_open_glass_out, skylight };

struct sorted_pane {
    pane_shape type;
    float distance_squared;
    fp3d_gpu::mesh_instance instance;
};

struct batch {
    Uint32 first_index;
    Uint32 index_count;
    Uint32 first_instance;
    Uint32 instance_count;
    bool resident = false;
    bool paged_instances = false;
    bool iris_water = false;
    std::optional<int> iris_terrain_material{};
    bool iris_terrain_solid = false;
    iris_caster shadow_caster = iris_caster::unclassified;
    std::optional<int> iris_model_id{};
    int iris_item_id = -1;
    bool iris_basic = false;
};
struct reflective_batch {
    batch draws;
    std::array<float, 4> material;
    reflective_surface surface;
    std::vector<std::uint8_t> iris_lights{};
    std::optional<int> iris_water_material{};
};

inline constexpr Uint32 resident_instance_buffer_limit = 16 * 1024 * 1024;

struct prepared_world {
    fp3d_geometry::mesh geometry;
    std::shared_ptr<const fp3d_geometry::mesh> resident_geometry;
    std::array<float, 3> resident_to_scene{};
    std::vector<fp3d_gpu::mesh_instance> instances;
    std::vector<fp3d_gpu::mesh_instance> resident_instances;
    std::shared_ptr<const std::vector<fp3d_gpu::mesh_instance>> resident_pages;
    std::shared_ptr<const fp3d_gpu::instance_page_revision> resident_page_revision;
    size_t resident_instance_count = 0;
    // Indexed like the corresponding dynamic and resident GPU instance streams.
    std::array<std::vector<Uint32>, 2> iris_terrain_lights;
    std::vector<batch> batches;
    std::optional<batch> unknown_background;
    std::vector<reflective_batch> reflective_batches;
    std::vector<batch> open_structure_shadows;
    std::vector<unsigned> auxiliary_slots;
    unsigned material_revision = 0;
    unsigned floor_count = 0;
    unsigned wall_count = 0;
    unsigned uncapped_walls = 0;
    unsigned ramp_count = 0;
    unsigned door_count = 0;
    unsigned window_count = 0;
    unsigned plant_count = 0;
    unsigned stair_count = 0;
    unsigned furniture_count = 0;
    fp3d_vehicle::gpu_vehicle_counts vehicle_geometry;
    unsigned sprite_count = 0;
    unsigned missing = 0;
    Uint32 opaque_batches = 0;
    Uint32 badge_end = 0;
    Uint32 blend_end = 0;
    unsigned badge_count = 0;
    unsigned pane_count = 0;
    unsigned vehicle_pane_count = 0;
    unsigned ground_layer_count = 0;
    unsigned animated_field_count = 0;
    fp3d_gpu_camera_math::matrix4 camera;
    std::array<float, 3> iris_sun{ 0.f, 1.f, 0.f };
    std::array<double, 3> iris_shadow_anchor{};
    fp3d_gpu::directional_light base_light;
    fp3d_gpu::atmosphere_uniforms atmosphere;
    float lightning_flash = 0.f;
    fp3d_gpu::halo_uniforms halos;
    Uint32 halo_count = 0;
    std::optional<fp3d_gpu::photon_uniforms> photon;
    std::array<fp3d_gpu_camera_math::matrix4, 6> point_faces;
    fp3d_gpu_camera_math::matrix4 single_sun_clip_from_world;
    std::optional<fp3d_gpu::sky_uniforms> sky;
    Uint32 width = 512;
    Uint32 height = 512;
    bool overhead = true;
};

// These cells need only a floor or wall and its supporting slab.
inline bool can_cache_terrain( const fp3d_snapshot::cell &tile,
                               std::unordered_map<std::string_view, bool> &types )
{
    if( ( tile.kind != kind::floor && tile.kind != kind::wall ) ||
        ( tile.kind == kind::floor && !tile.floor_present ) || tile.open_air ||
        tile.goes_up || tile.goes_down || tile.is_ramp || tile.exterior_only ||
        tile.ceiling_present || tile.cutaway_ceiling || tile.glass_ceiling ||
        tile.open_structure_above || tile.metal_deck_railing || tile.furniture ||
        !tile.ground_fields.empty() || !tile.vegetation.empty() || fp3d_scene::water_surface_offset( tile ) < 0.f ) {
        return false;
    }
    const auto [entry, inserted] = types.try_emplace( tile.terrain, false );
    if( inserted ) {
        const std::string &id = tile.terrain;
        entry->second = !id.empty() && !fp3d_furniture::world_prop( id ) && id != "t_radio_tower" && id != "t_wood_stairs_up_half" &&
                        id != "t_metal_floor_no_roof" && id != "t_floor_metal_gangway" &&
                        id.compare( 0, 11, "t_guardrail" ) != 0 && id.compare( 0, 9, "t_treetop" ) != 0 &&
                        id.find( "door" ) == std::string::npos &&
                        !fp3d_snapshot::is_glass_roof( id ) && !fp3d_snapshot::is_chain_gate( id ) &&
                        !fp3d_snapshot::is_metal_gate( id ) &&
                        !fp3d_snapshot::is_picket_gate( id ) &&
                        id != "t_column" && id != "t_pillar" && id != "t_little_column" &&
                        id != "t_column_halfway";
    }
    return entry->second;
}

class terrain_recipes
{
    public:
        struct recipe {
            std::optional<fp3d_materials::uv_rect> uv;
            reflective_surface surface = reflective_surface::none;
        };

        const recipe &get( std::size_t slot, const fp3d_snapshot::cell &tile,
                           unsigned atlas_revision, std::size_t atlas_slots ) {
            const auto version = std::make_pair( atlas_revision, atlas_slots );
            if( atlas_layout_ != version ) {
                for( auto &item : entries_ ) {
                    item.valid = false;
                }
                atlas_layout_ = version;
            }
            auto &item = entries_[slot];
            if( !item.valid || item.terrain != tile.terrain ||
                item.ground_connections != tile.ground_connections ||
                item.roof_connections != tile.roof_connections ) {
                item.terrain = tile.terrain;
                item.ground_connections = tile.ground_connections;
                item.roof_connections = tile.roof_connections;
                item.value.uv = fp3d_materials::lookup_gpu( fp3d_materials::terrain_key( tile ) );
                const auto pavement = fp3d_scene::classify_pavement( tile.terrain );
                item.value.surface = pavement == fp3d_scene::pavement_surface::concrete ?
                                     reflective_surface::concrete :
                                     pavement == fp3d_scene::pavement_surface::asphalt ?
                                     reflective_surface::asphalt : reflective_surface::none;
                item.valid = true;
            }
            return item.value;
        }

    private:
        struct entry {
            recipe value;
            std::string terrain;
            int ground_connections = -1;
            int roof_connections = -1;
            bool valid = false;
        };
        std::array<entry, 64> entries_;
        std::pair<unsigned, std::size_t> atlas_layout_{};
};

// Retain spatial data while the snapshot reuses its geometry. Chunks admit both camera
// and shadow candidates; individual cells still use the exact frustum test.
class terrain_index
{
    public:
        explicit terrain_index( bool retain_recipes = true, bool resident = false,
                                bool split_terrain_instances = false ) :
            retain_models_on_gpu( resident ), split_terrain_instances( split_terrain_instances ),
            retain_recipes_( retain_recipes ) {}

        using shore_cache = fp3d_shore_cache<tripoint_abs_ms>;
        struct chunk {
            tripoint_abs_ms anchor;
            float radius;
            shore_cache::mask shore;
            terrain_recipes *recipes = nullptr;
        };

        void invalidate_vehicle_roofs() {
            roofs_current_ = false;
        }

        void ensure( const fp3d_snapshot::scene_snapshot &scene, bool landscape ) {
            if( !scene.geometry_generation || geometry_generation_ != scene.geometry_generation ||
                allocation_ != scene.cells.data() || cell_count_ != scene.cells.size() ||
                landscape_ != landscape ) {
                rebuild( scene, landscape );
                roofs_current_ = false;
            }
            if( !roofs_current_ ) {
                // Vehicle silhouettes may change without changing terrain.
                vehicle_roofs = landscape && !scene.vehicles.empty() ?
                                fp3d_scene::find_vehicle_roof_ghosts( scene,
                [&]( const tripoint_abs_ms &position ) { return cells.get( position ); } ) :
                                fp3d_scene::vehicle_roof_visibility{};
                roofs_current_ = true;
            }
        }

        fp3d_cell_index cells;
        const bool retain_models_on_gpu;
        const bool split_terrain_instances;
        static constexpr size_t split_page_budget = 96;
        fp3d_geometry::model_mesh_cache<model_key> model_meshes;
        fp3d_geometry::resident_model_meshes<model_key> resident_models;
        using page_key = std::pair<tripoint_abs_ms, model_key>;
        using instance_pages = fp3d_gpu::instance_pages<page_key, fp3d_gpu::mesh_instance>;
        instance_pages resident_pages;

        void begin_resident_pages( unsigned revision ) {
            if( page_revision_ != revision ) {
                resident_pages.clear();
                page_revision_ = revision;
            }
            resident_pages.begin_frame();
        }
        shore_cache shore;
        fp3d_scene::vehicle_roof_visibility vehicle_roofs;
        std::vector<chunk> chunks;
        std::vector<size_t> chunk_ids;
        std::vector<bool> plain_cells;
        static constexpr int chunk_width = shore_cache::width;
        static constexpr float chunk_half_width = ( chunk_width - 1 ) * .5f;

    private:
        void rebuild( const fp3d_snapshot::scene_snapshot &scene, bool landscape ) {
            geometry_generation_ = scene.geometry_generation;
            allocation_ = scene.cells.data();
            cell_count_ = scene.cells.size();
            landscape_ = landscape;
            cells.rebuild( scene.cells );
            chunks.clear();
            chunk_ids.clear();
            plain_cells.clear();
            shore_cache::blocks water;
            for( const auto &tile : scene.cells ) {
                if( fp3d_scene::water_surface_offset( tile ) < 0.f && !tile.exterior_only &&
                    cells.get( tile.position ) == &tile ) {
                    water[shore_cache::block_of( tile.position )] |= shore_cache::cell_bit( tile.position );
                }
            }
            shore.update( std::move( water ) );
            if( !landscape ) {
                return;
            }
            std::unordered_map<tripoint_abs_ms, size_t> buckets;
            chunk_ids.reserve( scene.cells.size() );
            if( retain_recipes_ ) {
                plain_cells.reserve( scene.cells.size() );
            }
            std::unordered_map<std::string_view, bool> types;
            for( const auto &tile : scene.cells ) {
                const auto key = shore_cache::block_of( tile.position );
                const auto [entry, inserted] = buckets.try_emplace( key, chunks.size() );
                if( inserted ) {
                    const tripoint_abs_ms anchor( key.x() * chunk_width, key.y() * chunk_width, key.z() );
                    chunks.push_back( { anchor, 0.f, shore.shore_in( key ) } );
                }
                // The farthest cell center is under five metres from the center.
                auto &bucket = chunks[entry->second];
                bucket.radius = std::max( bucket.radius,
                                         5.f + std::max( 3.5f, tile.vegetation_height + .5f ) );
                chunk_ids.push_back( entry->second );
                if( !retain_recipes_ ) {
                    continue;
                }
                const bool plain = can_cache_terrain( tile, types );
                plain_cells.push_back( plain );
                if( plain && !bucket.recipes ) {
                    bucket.recipes = &recipe_blocks_[key];
                }
            }
            for( auto it = recipe_blocks_.begin(); it != recipe_blocks_.end(); ) {
                const auto found = buckets.find( it->first );
                if( found == buckets.end() || !chunks[found->second].recipes ) {
                    it = recipe_blocks_.erase( it );
                } else {
                    ++it;
                }
            }
        }

        unsigned page_revision_ = 0;
        const bool retain_recipes_;
        std::unordered_map<tripoint_abs_ms, terrain_recipes> recipe_blocks_;
        unsigned geometry_generation_ = 0;
        const fp3d_snapshot::cell *allocation_ = nullptr;
        size_t cell_count_ = 0;
        bool landscape_ = false;
        bool roofs_current_ = false;
};

inline std::optional<std::array<float, 3>> solid_furniture_color( fp3d_furniture::kind shape,
        unsigned index )
{
    using kind = fp3d_furniture::kind;
    using rgb = std::array<float, 3>;
    constexpr rgb dark_steel{ .27f, .32f, .36f };
    constexpr rgb steel{ .56f, .61f, .64f };
    constexpr rgb silver{ .76f, .83f, .86f };
    constexpr rgb enamel{ .90f, .92f, .90f };
    constexpr rgb blue{ .10f, .31f, .76f };
    constexpr rgb dark_blue{ .10f, .18f, .35f };

    // These meshes use solid part colors because the sprites already have
    // painted holes, shadows and perspective that would double the 3D detail.
    switch( shape ) {
        case kind::udp_dish: {
            constexpr std::array<rgb, 5> parts{ silver, steel, enamel, steel, enamel };
            return parts[index];
        }
        case kind::udp_ct: {
            constexpr std::array<rgb, 6> parts{ steel, enamel, dark_steel, silver, steel, silver };
            return parts[index];
        }
        case kind::udp_autodoc: {
            constexpr rgb green{ .28f, .65f, .40f };
            constexpr std::array<rgb, 14> parts{
                steel, enamel, dark_steel, green, silver, green, green,
                dark_steel, steel, dark_steel, steel, dark_steel, steel, silver
            };
            return parts[index];
        }
        case kind::udp_heavy_lathe: {
            constexpr std::array<rgb, 16> parts{
                dark_steel, steel, steel, silver, silver, dark_steel, steel, silver,
                steel, dark_steel, silver, dark_steel, steel, silver, steel, dark_steel
            };
            return parts[index];
        }
        case kind::udp_arcade: {
            constexpr rgb screen{ .065f, .10f, .13f };
            constexpr rgb red{ .78f, .14f, .16f };
            constexpr rgb amber{ .93f, .66f, .16f };
            constexpr std::array<rgb, 14> parts{
                dark_blue, blue, blue, blue, blue, screen, dark_steel, steel,
                red, amber, red, dark_blue, blue, dark_steel
            };
            return parts[index];
        }
        case kind::udp_windmill_real:
            return index >= 14 && index < 18 ? silver : index >= 12 ? steel : dark_steel;
        case kind::mailbox: {
            constexpr rgb wood{ .43f, .32f, .23f };
            constexpr rgb red{ .64f, .15f, .12f };
            constexpr rgb flag{ .89f, .26f, .17f };
            constexpr std::array<rgb, 10> parts{
                wood, wood, red, red, red, red, wood, flag, flag, silver
            };
            return parts[index];
        }
        case kind::udp_coal_forge: {
            constexpr rgb brick{ .35f, .23f, .16f };
            constexpr rgb coal{ .27f, .14f, .095f };
            constexpr rgb ember{ .67f, .27f, .095f };
            constexpr std::array<rgb, 11> parts{
                brick, brick, brick, brick, brick, coal, coal, ember, coal, ember, coal
            };
            return parts[index];
        }
        case kind::udp_vertical_bandsaw: {
            constexpr rgb brass{ .79f, .59f, .18f };
            constexpr std::array<rgb, 10> parts{
                dark_steel, dark_steel, steel, dark_steel, steel, silver, steel, brass, dark_steel, brass
            };
            return parts[index];
        }
        case kind::udp_sandbag_low: {
            constexpr rgb canvas{ .53f, .45f, .28f };
            constexpr rgb lit_canvas{ .69f, .60f, .40f };
            return index < 2 ? canvas : lit_canvas;
        }
        case kind::udp_compressor: {
            constexpr rgb tank{ .25f, .48f, .60f };
            constexpr rgb motor{ .19f, .28f, .40f };
            constexpr rgb green{ .33f, .70f, .46f };
            constexpr std::array<rgb, 14> parts{
                tank, tank, tank, dark_steel, dark_steel, steel, motor, dark_blue, motor,
                steel, dark_steel, silver, dark_steel, green
            };
            return parts[index];
        }
        case kind::udp_stone_statue:
            return index < 3 ? rgb{ .48f, .46f, .40f } : rgb{ .67f, .66f, .59f };
        case kind::udp_diesel_unit: {
            constexpr rgb red{ .69f, .24f, .17f };
            constexpr rgb orange{ .83f, .34f, .23f };
            constexpr std::array<rgb, 16> parts{
                dark_steel, dark_steel, dark_steel, dark_steel, red, orange, steel, steel,
                steel, steel, steel, steel, dark_steel, red, steel, dark_steel
            };
            return parts[index];
        }
        case kind::udp_hammock:
            return index < 2 ? rgb{ .47f, .32f, .20f } : rgb{ .75f, .77f, .74f };
        case kind::udp_mixer: {
            constexpr rgb green{ .30f, .52f, .49f };
            constexpr rgb red{ .78f, .28f, .22f };
            constexpr std::array<rgb, 10> parts{
                dark_steel, green, green, green, silver, steel, steel, dark_steel, red, red
            };
            return parts[index];
        }
        case kind::udp_treadmill: {
            constexpr rgb belt{ .15f, .17f, .19f };
            constexpr std::array<rgb, 12> parts{
                dark_steel, belt, steel, steel, dark_steel, steel, steel, dark_steel,
                dark_steel, dark_steel, belt, dark_steel
            };
            return parts[index];
        }
        case kind::udp_ball_machine:
            return index < 2 ? rgb{ .44f, .46f, .40f } : index == 2 ? dark_steel : steel;
        case kind::udp_bellows: {
            constexpr rgb leather{ .41f, .24f, .13f };
            constexpr rgb timber{ .66f, .39f, .17f };
            constexpr std::array<rgb, 6> parts{ leather, timber, timber, steel, dark_steel, timber };
            return parts[index];
        }
        case kind::udp_boulder_large:
        case kind::udp_boulder_medium:
        case kind::udp_boulder_small:
            return index == 0 ? rgb{ .62f, .58f, .47f } : rgb{ .67f, .62f, .50f };
        case kind::udp_brazier:
            return index == 0 ? steel : dark_steel;
        default:
            return std::nullopt;
    }
}

inline void emit_glass_shards( fp3d_geometry::mesh &mesh, float x, float z, float y,
                               float width, bool turned )
{
    std::vector<fp3d_geometry::draw_range> ranges;
    fp3d_geometry::primitive_mesh_sink glass( mesh, ranges );
    glass.begin( fp3d_geometry::primitive::triangles );
    glass.color( .59f, .82f, .89f );
    for( float side : { -1.f, 1.f } ) {
        glass.normal( turned ? side : 0.f, 0.f, turned ? 0.f : side );
        for( const auto &shard : std::array<std::array<float, 4>, 3>{ {
                 { -.48f, -.28f, -.44f, .27f }, { -.20f, .04f, -.07f, .11f },
                 { .24f, .48f, .42f, .36f }
             } } ) {
            const auto point = [&]( float along, float up ) {
                const float d = width * along;
                glass.point( x + ( turned ? 0.f : d ), y + up, z + ( turned ? d : 0.f ) );
            };
            const bool forward = turned ? side < 0.f : side > 0.f;
            point( shard[forward ? 0 : 1], 0.f );
            point( shard[forward ? 1 : 0], 0.f );
            point( shard[2], shard[3] );
        }
    }
    glass.end();
}

inline void emit_model( const model_key &model, fp3d_geometry::mesh &mesh )
{
    using furniture_kind = fp3d_furniture::kind;
    const std::optional<fp3d_materials::uv_rect> unit_uv = fp3d_materials::uv_rect{ 0, 0, 1, 1 };
    fp3d_geometry::quad_mesh_sink sink( mesh );
    const size_t first_vertex = mesh.vertices.size();
    const auto box = [&]( const fp3d_furniture::part &part ) {
        fp3d_geometry::box_faces( sink, part.x, part.z, part.width, part.depth,
                                  part.bottom, part.top, unit_uv, false,
                                  static_cast<const fp3d_materials::furniture_uv *>( nullptr ), true );
    };
    if( model.type == shape::radio_tower ) {
        fp3d_geometry::radio_tower_beams( fp3d_camera::floor_height,
        [&]( float x0, float y0, float z0, float x1, float y1, float z1, float thickness, bool painted ) {
            if( painted ) {
                sink.color( .68f, .15f, .14f );
            } else {
                sink.color( .56f, .60f, .65f );
            }
            fp3d_geometry::structural_beam( sink, { x0, y0, z0 }, { x1, y1, z1 }, thickness );
        } );
        sink.color( 1, 1, 1 );
    } else if( model.type == shape::ceiling ) {
        fp3d_geometry::floor_face( sink, 0, 0, 0, *unit_uv, .5f, true );
    } else if( model.type == shape::water ) {
        fp3d_geometry::floor_face( sink, 0, 0, 0, *unit_uv, .5f );
    } else if( model.type == shape::floor ) {
        fp3d_geometry::box_faces( sink, 0, 0, 1, 1, model.building_height, 0, unit_uv, false,
                                  static_cast<const fp3d_materials::furniture_uv *>( nullptr ), true );
        if( !model.metal_deck ) {
            fp3d_geometry::floor_face( sink, 0, 0, model.building_height, *unit_uv, .5f, true );
        }
    } else if( model.type == shape::ramp ) {
        fp3d_geometry::ramp_face( sink, 0, 0, *unit_uv,
                                  fp3d_ramp::surface{ 0, model.param_x, model.param_y } );
    } else if( model.type == shape::climb ) {
        fp3d_snapshot::cell profile;
        profile.kind = model.door_kind;
        profile.goes_up = model.param_x > 0;
        std::vector<fp3d_geometry::draw_range> ranges;
        fp3d_geometry::primitive_mesh_sink climb( mesh, ranges );
        const fp3d_materials::furniture_uv material{ unit_uv, unit_uv, unit_uv };
        for( const auto &part : fp3d_snapshot::climb_parts( profile ) ) {
            if( part.round ) {
                fp3d_geometry::round_furniture_part( climb, part, material, 1.f );
            } else {
                climb.begin( fp3d_geometry::primitive::quads );
                fp3d_geometry::box_faces( climb, part.x, part.z, part.width, part.depth,
                                          part.bottom, part.top, unit_uv, false, &material, true );
                climb.end();
            }
        }
    } else if( model.type == shape::built_terrain || model.type == shape::fence ||
               model.type == shape::chain_gate || model.type == shape::glass_barrier ||
               model.type == shape::barrier_pane ) {
        std::array<bool, 4> links{};
        for( size_t i = 0; i < links.size(); ++i ) { links[i] = model.fence_links & ( 1u << i ); }
        if( model.type == shape::glass_barrier || model.type == shape::barrier_pane ) {
            for( const auto &part : fp3d_snapshot::glass_barrier_parts(
                     model.terrain_id == "t_glass_railing", links ) ) {
                if( part.glass && model.type == shape::barrier_pane ) {
                    const bool along_x = part.width > part.depth;
                    const float dx = along_x ? part.width * .5f : 0.f;
                    const float dz = along_x ? 0.f : part.depth * .5f;
                    sink.normal( along_x ? 0.f : 1.f, 0.f, along_x ? 1.f : 0.f );
                    sink.point( part.x - dx, part.bottom, part.z - dz );
                    sink.point( part.x + dx, part.bottom, part.z + dz );
                    sink.point( part.x + dx, part.top, part.z + dz );
                    sink.point( part.x - dx, part.top, part.z - dz );
                } else if( model.type == shape::glass_barrier && !part.glass ) {
                    box( part );
                } else if( model.type == shape::glass_barrier &&
                           fp3d_snapshot::broken_glass_wall( model.terrain_id ) ) {
                    emit_glass_shards( mesh, part.x, part.z, part.bottom,
                                       std::max( part.width, part.depth ), part.depth > part.width );
                }
            }
        } else if( model.type == shape::built_terrain ) {
            if( const auto *prop = fp3d_furniture::world_prop( model.terrain_id ) ) {
                model_key geometry{ shape::furniture };
                geometry.furniture_id = model.terrain_id;
                geometry.furniture_shape = prop->shape;
                emit_model( geometry, mesh );
            } else {
                for( const auto &part : fp3d_snapshot::terrain_model_parts( model.terrain_id, links, model.door_swing ) ) {
                    box( part );
                }
            }
        } else if( model.type == shape::fence ) {
            const bool posts_only = model.terrain_id == "t_chainfence_posts" ||
                                    model.terrain_id == "t_chickenwire_fence_post" ||
                                    model.terrain_id == "t_fence_post";
            const auto parts = fp3d_snapshot::fence_parts( links, posts_only );
            if( !parts.empty() ) {
                const auto &post = parts.front();
                box( post );
                box( { 0, 0, .077f, .077f, 1.96f, 2.02f } );
                for( size_t i = 1; i < parts.size(); ++i ) {
                    const auto &span = parts[i];
                    box( { span.x, span.z, std::max( span.width, .03f ), std::max( span.depth, .03f ),
                           1.86f, 1.915f } );
                    box( { span.x, span.z, std::max( span.width, .025f ), std::max( span.depth, .025f ),
                           .11f, .145f } );
                    sink.color( .68f, .70f, .69f );
                    fp3d_geometry::wire_lattice( sink, span.x - span.width / 2, span.z - span.depth / 2,
                                                 span.x + span.width / 2, span.z + span.depth / 2, .13f, 1.83f );
                    sink.color( 1, 1, 1 );
                }
            }
        } else {
            const bool along_x = fp3d_snapshot::chain_gate_along_x( links );
            for( const auto &part : fp3d_snapshot::chain_gate_parts( model.terrain_id, true, model.door_swing ) ) {
                box( along_x ? part : fp3d_furniture::rotated( part, 1 ) );
            }
            const bool open = model.terrain_id == "t_chaingate_o" || model.terrain_id == "t_chickenwire_gate_o";
            const auto orient = [&]( float u, float y ) -> fp3d_geometry::structure_point {
                const float x = open ? -.425f : -.425f + u;
                const float z = open ? model.door_swing * u : 0.f;
                return along_x ? fp3d_geometry::structure_point{ x, y, z } :
                       fp3d_geometry::structure_point{ -z, y, x };
            };
            const auto a = orient( .055f, .24f );
            const auto b = orient( .705f, 1.80f );
            sink.color( .68f, .70f, .69f );
            fp3d_geometry::wire_lattice( sink, a[0], a[2], b[0], b[2], .20f, 1.85f );
            sink.color( 1, 1, 1 );
        }
    } else if( model.type == shape::door ) {
        for( const auto &part : fp3d_snapshot::door_parts( model.door_kind ) ) {
            const auto uv = fp3d_geometry::damaged_door_uv( *unit_uv, part,
                            fp3d_camera::floor_height );
            const fp3d_materials::furniture_uv faces{ uv, uv, uv };
            fp3d_geometry::box_faces( sink, part.x, part.z, part.width, part.depth,
                                      part.bottom, part.top, unit_uv, false,
                                      model.door_kind == kind::damaged_door ? &faces : nullptr,
                                      fp3d_scene::door_part_owns_cap( part.top, model.upper_floor ) );
        }
        if( model.glass_door ) {
            const auto leaf_box = [&]( fp3d_furniture::part part ) {
                box( model.door_swing ? fp3d_snapshot::swing_door_part( part, model.door_swing ) : part );
            };
            if( model.door_swing ) {
                for( auto part : fp3d_snapshot::door_parts( kind::glass ) ) {
                    part.width *= .88f;
                    part.x *= .88f;
                    part.bottom = std::max( .045f, part.bottom );
                    part.top = std::min( 2.09f, part.top );
                    leaf_box( part );
                }
            }
            for( float side : { -1.f, 1.f } ) {
                leaf_box( { .30f, side * .105f, .055f, .055f, 1.f, 1.36f } );
                for( float y : { 1.015f, 1.315f } ) {
                    leaf_box( { .30f, side * .07f, .065f, .10f, y, y + .03f } );
                }
            }
        } else if( model.door_swing ) {
            box( fp3d_snapshot::swing_door_part( { 0, 0, .88f, .075f, .035f, 2.09f },
                                                 model.door_swing ) );
        } else if( fp3d_snapshot::broken_glass_door( model.terrain_id ) ) {
            emit_glass_shards( mesh, 0, 0, .025f, .84f, false );
        }
    } else if( model.type == shape::window ) {
        fp3d_geometry::window_frame<fp3d_materials::furniture_uv>( sink, unit_uv,
                fp3d_camera::floor_height, model.upper_floor );
        if( fp3d_snapshot::broken_window_glass( model.terrain_id ) ) {
            emit_glass_shards( mesh, 0, 0, .899f, .82f, false );
        }
    } else if( model.type == shape::curtain ) {
        std::vector<fp3d_geometry::draw_range> ranges;
        fp3d_geometry::primitive_mesh_sink curtain( mesh, ranges );
        fp3d_geometry::window_face( curtain, unit_uv, .42f, .9f, 2.1f );
    } else if( model.type == shape::stair_stringers ) {
        for( float x : { .04f, .46f } ) {
            fp3d_geometry::structural_beam( sink, { x, fp3d_camera::floor_height, -.5f },
                                           { x, 0.f, .5f }, .07f );
        }
    } else if( model.type == shape::stairs ) {
        fp3d_snapshot::cell profile;
        profile.stairs = fp3d_snapshot::stair_section{ model.stair_rows, 1, model.stair_south, 0 };
        for( const auto &part : fp3d_snapshot::stair_parts( profile ) ) {
            fp3d_geometry::box_faces( sink, part.x, part.z, part.width, part.depth,
                                      part.bottom, part.top, unit_uv, false,
                                      static_cast<const fp3d_materials::furniture_uv *>( nullptr ), true );
        }
    } else if( model.type == shape::pad ) {
        fp3d_geometry::floor_face( sink, 0, 0, .015f, *unit_uv, .5f );
    } else if( model.type == shape::landscape_ground ) {
        fp3d_geometry::floor_face( sink, 0, 0, 0, *unit_uv, .5f );
        for( size_t i = first_vertex; i < mesh.vertices.size(); ++i ) {
            auto &p = mesh.vertices[i].position;
            p[0] *= model.param_x;
            p[2] *= model.param_y;
        }
    } else if( model.type == shape::landscape_facade ||
               model.type == shape::landscape_facade_tiled ) {
        fp3d_geometry::box_faces( sink, 0, 0, model.param_x, model.param_y, 0,
                                  model.building_height, unit_uv,
                                  model.type == shape::landscape_facade_tiled,
                                  static_cast<const fp3d_materials::furniture_uv *>( nullptr ), false );
    } else if( model.type == shape::plant || model.type == shape::grass ) {
        std::vector<fp3d_geometry::draw_range> ranges;
        fp3d_geometry::primitive_mesh_sink foliage( mesh, ranges );
        foliage.textured( true );
        fp3d_geometry::crossed_plant( foliage, 0, 0, model.param_x,
                                     model.type == shape::grass, *unit_uv, 0.f, model.param_y );
    } else if( model.type == shape::kiln_fire ) {
        std::vector<fp3d_geometry::draw_range> ranges;
        fp3d_geometry::primitive_mesh_sink fire( mesh, ranges );
        fire.begin( fp3d_geometry::primitive::triangles );
        fp3d_geometry::kiln_fire_vertices( [&]( float x, float y, float z, float r, float g, float b, float a ) {
            fire.color( r, g, b, a );
            fire.point( x, y, z );
        } );
        fire.end();
    } else if( model.type == shape::furniture || model.type == shape::furniture_glass ||
               model.type == shape::furniture_light_head ) {
        std::vector<fp3d_geometry::draw_range> ranges;
        fp3d_geometry::primitive_mesh_sink furniture( mesh, ranges );
        unsigned part_index = 0;
        for( const auto &original : fp3d_furniture::parts( model.furniture_shape ) ) {
            const unsigned index = part_index++;
            const auto segment = model.bed_links ?
                                 fp3d_furniture::bed_segment_part( model.furniture_shape, index, model.bed_links ) :
                                 std::optional<fp3d_furniture::part>( original );
            if( !segment ) {
                continue;
            }
            const auto &part = *segment;
            if( part.glass != ( model.type == shape::furniture_glass ) ) {
                continue;
            }
            const bool lamp_head = model.furniture_shape == furniture_kind::street_light &&
                                   index >= 3;
            if( lamp_head != ( model.type == shape::furniture_light_head ) ||
                ( lamp_head && ( index >= 4 ) != ( model.param_x > 0 ) ) ) {
                continue;
            }
            if( model.furniture_shape == furniture_kind::udp_stop_sign ) {
                const bool post = index == 0;
                const bool red = index == 2;
                furniture.color( red ? .78f : post ? .62f : .94f,
                                 red ? .045f : post ? .65f : .94f,
                                 red ? .035f : post ? .68f : .90f );
            }
            if( model.furniture_shape == furniture_kind::udp_dish && index == 2 ) {
                fp3d_geometry::satellite_reflector( furniture, part );
                furniture.color( .60f, .62f, .65f );
                furniture.begin( fp3d_geometry::primitive::quads );
                    fp3d_geometry::structural_beam( furniture, { 0.f, 1.31f, .375f },
                                               { 0.f, 1.74f, .42f }, .043f );
                furniture.end();
                continue;
            }
            const auto *prop = fp3d_furniture::world_prop( model.furniture_id );
            const auto solid = prop ? std::nullopt : solid_furniture_color( model.furniture_shape, index );
            const auto faces = solid ? fp3d_materials::furniture_uv{} :
                               fp3d_materials::lookup_furniture_gpu( model.furniture_id, index );
            if( solid ) {
                furniture.color( ( *solid )[0], ( *solid )[1], ( *solid )[2] );
            }
            if( prop && prop->black_glass ) {
                furniture.color( .27f, .30f, .38f );
            } else if( !faces.side && prop ) {
                furniture.color( .55f, .57f, .58f );
            }
            if( !faces.side && model.furniture_shape == furniture_kind::udp_terminal ) {
                furniture.color( .19f, .20f, .21f );
            } else if( !faces.side && model.furniture_shape == furniture_kind::udp_air_conditioner ) {
                const float metal = index == 5 ? .22f : index == 1 ? .75f : .55f;
                furniture.color( metal, metal, metal );
            } else if( !faces.side && !part.glass &&
                       model.furniture_shape == furniture_kind::udp_water_purifier ) {
                const bool filter = index >= 5 && index < 20 && ( index - 5 ) % 5 == 0;
                const bool dial = index >= 5 && index < 20 && ( index - 5 ) % 5 == 4;
                furniture.color( dial ? .80f : filter ? .35f : .40f,
                                 dial ? .84f : filter ? .39f : .51f,
                                 dial ? .86f : filter ? .68f : .61f );
            }
            if( model.furniture_shape == furniture_kind::rack && !faces.side ) {
                if( index < 4 ) {
                    furniture.color( .37f, .38f, .40f );
                } else if( model.furniture_id == "f_rack_wood" ) {
                    furniture.color( .49f, .33f, .22f );
                } else {
                    furniture.color( .70f, .71f, .73f );
                }
            }
            if( part.form != fp3d_furniture::part_form::standard ) {
                if( faces.side ) {
                    furniture.texcoord( ( faces.side->left + faces.side->right ) * .5f,
                                        ( faces.side->top + faces.side->bottom ) * .5f );
                }
                fp3d_geometry::shaped_furniture_part( furniture, part );
                continue;
            }
            if( part.sphere ) {
                fp3d_geometry::furniture_ellipsoid( furniture, part, faces );
                continue;
            }
            if( model.furniture_shape == furniture_kind::floor_lamp && index == 3 ) {
                fp3d_geometry::round_furniture_part( furniture, part, faces, .39f );
                continue;
            }
            if( part.round ) {
                if( part.hollow ) {
                    fp3d_geometry::furniture_hollow_ring( furniture, part, faces );
                } else if( part.horizontal ) {
                    const size_t first = mesh.vertices.size();
                    fp3d_geometry::round_furniture_part( furniture,
                            fp3d_geometry::horizontal_furniture_cylinder( part ),
                            fp3d_geometry::horizontal_furniture_surfaces( faces ), 1.f );
                    const float center = .5f * ( part.bottom + part.top );
                    for( size_t i = first; i < mesh.vertices.size(); ++i ) {
                        auto &vertex = mesh.vertices[i];
                        const auto [x, y, z] = vertex.position;
                        const auto [nx, ny, nz] = vertex.normal;
                        vertex.position = { part.x + x, center - z, part.z + y };
                        vertex.normal = { nx, -nz, ny };
                    }
                } else {
                    fp3d_geometry::round_furniture_part( furniture, part, faces, 1.f );
                }
                continue;
            }
            const bool rolled = std::abs( part.roll ) > .01f;
            auto p = part;
            if( rolled ) {
                p.x = p.z = 0;
                p.bottom = -.5f * ( part.top - part.bottom );
                p.top = -p.bottom;
            }
            const size_t first = mesh.vertices.size();
            furniture.begin( fp3d_geometry::primitive::quads );
            const unsigned hidden_sides = p.width == 1.f && p.depth == 1.f ? model.bed_links : 0;
            fp3d_geometry::box_faces( furniture, p.x, p.z, p.width, p.depth, p.bottom, p.top,
                                      faces.side, false, &faces, true, hidden_sides );
            const auto uv = faces.side.value_or( *unit_uv );
            const float left = p.x - p.width / 2;
            const float right = p.x + p.width / 2;
            const float north = p.z - p.depth / 2;
            const float south = p.z + p.depth / 2;
            furniture.normal( 0, -1, 0 );
            furniture.texcoord( uv.left, uv.top );
            furniture.point( left, p.bottom, south );
            furniture.texcoord( uv.left, uv.bottom );
            furniture.point( left, p.bottom, north );
            furniture.texcoord( uv.right, uv.bottom );
            furniture.point( right, p.bottom, north );
            furniture.texcoord( uv.right, uv.top );
            furniture.point( right, p.bottom, south );
            furniture.end();
            if( rolled ) {
                constexpr float radians_per_degree = .017453292519943295769f;
                const float sine = std::sin( part.roll * radians_per_degree );
                const float cosine = std::cos( part.roll * radians_per_degree );
                const float center = .5f * ( part.bottom + part.top );
                for( size_t i = first; i < mesh.vertices.size(); ++i ) {
                    auto &vertex = mesh.vertices[i];
                    const auto [x, y, z] = vertex.position;
                    const auto [nx, ny, nz] = vertex.normal;
                    vertex.position = { part.x + cosine * x - sine * y,
                                        center + sine * x + cosine * y, part.z + z };
                    vertex.normal = { cosine * nx - sine * ny, sine * nx + cosine * ny, nz };
                }
            }
        }
        if( model.furniture_shape == furniture_kind::udp_stop_sign ) {
            furniture.color( .98f, .98f, .95f );
            furniture.begin( fp3d_geometry::primitive::quads );
            fp3d_geometry::stop_sign_letters( furniture );
            furniture.end();
        }
    } else {
        const float height = model.type == shape::obstacle ? 1.f :
                             fp3d_camera::floor_height + model.param_x;
        fp3d_geometry::box_faces( sink, 0, 0, fp3d_scene::wall_width, fp3d_scene::wall_width,
                                  0, height, unit_uv, true,
                                  static_cast<const fp3d_materials::furniture_uv *>( nullptr ),
                                  model.type == shape::capped_wall || model.type == shape::obstacle );
    }
    if( ( model.type == shape::floor || model.type == shape::built_terrain ||
          model.type == shape::fence ) && ( model.param_x != 0 || model.param_y != 0 ) ) {
        for( size_t i = first_vertex; i < mesh.vertices.size(); ++i ) {
            auto &vertex = mesh.vertices[i];
            vertex.position[1] += model.param_x * vertex.position[0] + model.param_y * vertex.position[2];
            const auto [x, y, z] = vertex.normal;
            const float nx = x - model.param_x * y;
            const float nz = z - model.param_y * y;
            const float length = std::sqrt( nx * nx + y * y + nz * nz );
            vertex.normal = { nx / length, y / length, nz / length };
        }
    }
    for( unsigned turn = 0; turn < model.quarter_turns; ++turn ) {
        for( size_t i = first_vertex; i < mesh.vertices.size(); ++i ) {
            auto &vertex = mesh.vertices[i];
            vertex.position = { vertex.position[2], vertex.position[1], -vertex.position[0] };
            vertex.normal = { vertex.normal[2], vertex.normal[1], -vertex.normal[0] };
        }
    }
}

inline std::optional<prepared_world> prepare( const fp3d_snapshot::scene_snapshot &scene,
        const fp3d_camera::pose &pose, bool overhead, Uint32 width = 512, Uint32 height = 512,
        bool include_landscape = false, bool enable_local_lights = false,
        bool enable_local_shadows = false, bool enable_sun_shadow = false,
        const fp3d_scene::blueprint *blueprint = nullptr,
        const fp3d_targeting::edge_outline *target_outline = nullptr, terrain_index *cache = nullptr,
        bool reuse_material_slots = false,
        const fp3d_compositor::pickup_highlight *pickup = nullptr,
        const tripoint_abs_ms *peek_window = nullptr, float open_structure_opacity = 1.f,
        const fp3d_iris::material_id_map *iris_materials = nullptr,
        const fp3d_iris::material_id_map *iris_entities = nullptr,
        const fp3d_iris::material_id_map *iris_items = nullptr,
        const fp3d_iris::material_id_map *iris_water_materials = nullptr,
        bool iris_basic = false )
{
    if( !width || !height || width > 4096 || height > 4096 ) {
        return std::nullopt;
    }
    fp3d_regression::turn_probe timing( "gpu_prepare_materials" );
    using pickup_kind = fp3d_compositor::pickup_highlight::kind;
    std::optional<fp3d_snapshot::sprite> pickup_item;
    if( pickup && pickup->type == pickup_kind::item ) {
        pickup_item.emplace();
        pickup_item->kind = fp3d_snapshot::sprite_kind::item;
        pickup_item->position = pickup->tile;
        pickup_item->id = pickup->id;
        pickup_item->variant = pickup->variant;
        pickup_item->height = .7f;
    }
    if( !reuse_material_slots ) {
        fp3d_materials::prepare( scene, blueprint && blueprint->terrain ? &*blueprint->terrain : nullptr,
                                 pickup_item ? &*pickup_item : nullptr );
    }
    timing.stage( "gpu_prepare_view" );
    prepared_world result;
    result.width = width;
    result.height = height;
    result.overhead = overhead;
    result.iris_sun = { scene.sky.sun_x, scene.sky.sun_y, scene.sky.sun_z };
    result.iris_shadow_anchor = { static_cast<double>( scene.origin.x() ),
                                  static_cast<double>( scene.origin.z() ) * fp3d_camera::floor_height,
                                  static_cast<double>( scene.origin.y() ) };
    result.material_revision = fp3d_materials::revision();
    fp3d_gpu_camera_math::camera view;
    view.yaw_degrees = pose.yaw;
    view.pitch_degrees = pose.pitch;
    view.eye_east = pose.eye.x;
    view.eye_height = pose.eye.height;
    view.eye_south = pose.eye.y;
    view.aspect = static_cast<float>( width ) / height;
    view.horizontal_fov_degrees = fp3d_camera::horizontal_fov_degrees;
    view.near_clip = fp3d_camera::near_clip;
    view.far_clip = fp3d_camera::far_clip;
    const auto camera = fp3d_gpu_camera_math::clip_from_world( view );
    if( !camera ) return std::nullopt;
    result.camera = *camera;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch() ).count();
    const float lightning = fp3d_weather::flash( scene.weather, now );
    result.lightning_flash = lightning;
    const float clouds = std::clamp( scene.weather.clouds, 0.f, 1.f );
    const float daylight = std::clamp( scene.sky.daylight, 0.f, 1.f );
    const float wetness = scene.weather.particles == fp3d_weather::precipitation::rain ?
                          .64f * std::clamp( scene.weather.intensity, 0.f, 1.f ) : 0.f;
    // Surface windows can reveal the sky even when the avatar is under a roof.
    if( include_landscape && scene.origin.z() >= 0 ) {
        auto &sky = result.sky.emplace();
        const auto &m = camera->elements;
        sky.right_daylight = { m[0], m[1], m[2], daylight };
        sky.up_twilight = { m[4], m[5], m[6], scene.sky.twilight };
        sky.forward_clouds = { m[12], m[13], m[14], clouds };
        sky.sun_moonlight = { scene.sky.sun_x, scene.sky.sun_y, scene.sky.sun_z, scene.sky.moonlight };
        sky.screen_flash_time = { 1.f / width, 1.f / height, lightning,
                                  ( now % 600000 ) / 1000.f };
        sky.star_cloud_mode[0] = daylight < .25f && clouds < 1 ? fp3d_gpu::sky_star_count : 0;
        sky.star_cloud_mode[1] = clouds > .05f ? fp3d_gpu::sky_cloud_count : 0;
    }
    const auto &direction = fp3d_base_light::direction;
    const float inv_length = 1.f / std::hypot( direction[0], direction[1], direction[2] );
    // Keep the original global and directional ambient contributions.
    result.base_light.direction_ambient = { direction[0] * inv_length, direction[1] * inv_length,
                                          direction[2] * inv_length,
                                          .2f + fp3d_base_light::ambient( daylight, clouds, lightning ) };
    result.base_light.direct_and_padding[0] = fp3d_base_light::direct;
    const float fog_scale = overhead ?
                            std::max( 1.f, pose.eye.height / fp3d_camera::eye_height ) : 1.f;
    const auto fog = fp3d_weather::fog_parameters( scene.weather, scene.sky.daylight, lightning, fog_scale );
    if( fog.end > fog.start ) {
        result.atmosphere.color_start = { fog.color[0], fog.color[1], fog.color[2], fog.start };
        result.atmosphere.inverse_range_and_padding[0] = 1.f / ( fog.end - fog.start );
    }
    constexpr float far_sun_radius = 96.f;
    if( enable_sun_shadow && ( scene.origin.z() >= 0 || scene.sky.visible ) &&
        scene.sky.sun_y >= .12f && daylight >= .1f ) {
        const std::array<float, 3> sun{ scene.sky.sun_x, scene.sky.sun_y, scene.sky.sun_z };
        const std::array<double, 3> anchor{ static_cast<double>( scene.origin.x() ),
                                            static_cast<double>( scene.origin.z() ) * fp3d_camera::floor_height,
                                            static_cast<double>( scene.origin.y() ) };
        auto &uniforms = result.atmosphere;
        uniforms.sun_clip_from_world = fp3d_gpu_camera_math::sun_shadow_clip_from_world(
                                          sun, anchor, 28.f, fp3d_gpu::sun_shadow_size );
        uniforms.far_sun_clip_from_world = fp3d_gpu_camera_math::sun_shadow_clip_from_world(
                                              sun, anchor, far_sun_radius,
                                              fp3d_gpu::far_sun_shadow_size );
        result.single_sun_clip_from_world = fp3d_gpu_camera_math::sun_shadow_clip_from_world(
                                          sun, anchor, 38.f, fp3d_gpu::sun_shadow_size );
        uniforms.sun_direction_strength = { sun[0], sun[1], sun[2],
                                            .68f * daylight * ( 1.f - .6f * clouds ) };
        uniforms.sun_texel_sizes = { 1.f / fp3d_gpu::sun_shadow_size,
                                     1.f / fp3d_gpu::far_sun_shadow_size, 0, 0 };
    }
    if( enable_local_lights ) {
        static_assert( fp3d_gpu::max_local_lights == fp3d_local_lights::max_active );
        const auto lights = fp3d_local_lights::select( scene, pose.eye );
        auto &uniforms = result.atmosphere;
        result.halos.inverse_extent = { 1.f / width, 1.f / height, 0, 0 };
        uniforms.eye_and_light_count = { pose.eye.x, pose.eye.height, pose.eye.y,
                                         static_cast<float>( lights.size() ) };
        // The projection's last row gives the distance along the camera's forward axis.
        uniforms.forward_and_daylight = { camera->elements[12], camera->elements[13],
                                          camera->elements[14], daylight };
        for( size_t i = 0; i < lights.size(); ++i ) {
            uniforms.local_position[i] = lights[i].position;
            uniforms.local_energy[i] = lights[i].color;
            uniforms.local_beam_flags[i / 4][i % 4] = lights[i].beam;
            if( const auto center = fp3d_gpu::project_halo( *camera, lights[i].position,
                                    view.near_clip, view.far_clip ) ) {
                result.halos.centers[result.halo_count] = *center;
                result.halos.colors[result.halo_count++] = lights[i].color;
            }
            if( enable_local_shadows && lights[i].held_beam && uniforms.beam_index_and_texel[0] == 0 ) {
                const auto &position = lights[i].position;
                fp3d_gpu_camera_math::camera beam_view = view;
                beam_view.eye_east = position[0];
                beam_view.eye_height = position[1];
                beam_view.eye_south = position[2];
                beam_view.aspect = 1;
                // Match the legacy frustum with a half-width of 1.108 times its near plane.
                beam_view.horizontal_fov_degrees = 2.f * std::atan( 1.108f ) * 57.2957795f;
                beam_view.near_clip = .05f;
                beam_view.far_clip = position[3];
                if( const auto beam_camera = fp3d_gpu_camera_math::clip_from_world( beam_view ) ) {
                    uniforms.beam_clip_from_world = *beam_camera;
                    uniforms.beam_index_and_texel = { static_cast<float>( i + 1 ),
                                                      1.f / fp3d_gpu::beam_shadow_size, 0, 0 };
                }
            }
            if( enable_local_shadows && lights[i].fixture && lights[i].position[3] > .1f &&
                uniforms.point_index_and_texel[0] == 0 ) {
                result.point_faces = fp3d_gpu_camera_math::point_shadow_faces( lights[i].position );
                uniforms.point_index_and_texel = { static_cast<float>( i + 1 ),
                                                   1.f / fp3d_gpu::point_shadow_face_size, 0, 0 };
            }
        }
    }
    using instance_group = std::vector<fp3d_gpu::mesh_instance>;
    std::map<model_key, instance_group> groups;
    // Ordinary floors vary only by thickness and reflective surface.
    constexpr size_t surface_count = static_cast<size_t>( reflective_surface::concrete ) + 1;
    std::array<instance_group *, surface_count * 2> floor_groups{};
    std::array<instance_group *, 2> wall_groups{};
    const auto group_for = [&]( instance_group *&entry, const model_key &model ) {
        if( !entry ) {
            entry = &groups[model];
        }
        return entry;
    };
    const auto floor_group = [&]( const model_key &model ) {
        const size_t surface = static_cast<size_t>( model.surface );
        return group_for( floor_groups[surface * 2 + ( model.building_height == -.26f )], model );
    };
    terrain_index temporary_index( false );
    auto &terrain = cache ? *cache : temporary_index;
    timing.stage( "gpu_prepare_spatial_index" );
    terrain.ensure( scene, include_landscape );
    timing.stage( "gpu_prepare_terrain_gather" );
    const auto known_cell = [&]( const tripoint_abs_ms &position ) {
        const auto *tile = terrain.cells.get( position );
        return tile && overhead && fp3d_snapshot::hidden_by_cutaway( scene, *tile ) ? nullptr : tile;
    };
    const auto monster_walls = overhead ? fp3d_scene::monster_cutaway_walls( scene, pose.eye, known_cell ) :
                               std::unordered_set<tripoint_abs_ms>{};
    const auto &vehicle_roofs = terrain.vehicle_roofs;
    const std::optional<fp3d_materials::uv_rect> unit_uv = fp3d_materials::uv_rect{ 0, 0, 1, 1 };
    using furniture_kind = fp3d_furniture::kind;
    std::vector<sorted_pane> panes;
    std::vector<batch> translucent_models;
    struct sorted_blend {
        float depth;
        batch draw;
    };
    std::vector<sorted_blend> sorted_blends;
    bool fading_open_panes = false;
    const auto blend_depth = [&]( const fp3d_gpu::mesh_instance &instance, float height ) {
        return result.camera.transform( instance.offset[0], instance.offset[1] + height,
                                        instance.offset[2] )[3];
    };
    std::vector<batch> blueprint_models;
    std::vector<fp3d_gpu::mesh_instance> ground_layers;
    const fp3d_camera::frustum world_view( pose.yaw, pose.pitch, view.aspect );
    const auto in_shadow_range = [&]( float x, float y, float z, float radius ) {
        const auto &lighting = result.atmosphere;
        if( lighting.sun_direction_strength[3] > 0 ) {
            const auto sun = lighting.far_sun_clip_from_world.transform( x, y, z );
            const float edge = 1.f + radius / far_sun_radius;
            if( std::abs( sun[0] ) <= edge && std::abs( sun[1] ) <= edge ) {
                return true;
            }
        }
        for( const float index : { lighting.beam_index_and_texel[0], lighting.point_index_and_texel[0] } ) {
            if( index > 0 ) {
                const auto &source = lighting.local_position[static_cast<size_t>( index ) - 1];
                const float dx = x - source[0];
                const float dy = y - source[1];
                const float dz = z - source[2];
                const float range = radius + source[3];
                if( dx * dx + dy * dy + dz * dz <= range * range ) {
                    return true;
                }
            }
        }
        return false;
    };
    const auto add_pane = [&]( pane_shape type, const tripoint_rel_ms &rel, float height,
                               const std::optional<fp3d_materials::uv_rect> &uv, float tint,
                               float opacity, const std::array<float, 4> *preview_color ) {
        fp3d_gpu::mesh_instance instance;
        instance.offset = { static_cast<float>( rel.x() ), height, static_cast<float>( rel.y() ),
                            uv ? fp3d_gpu::unlit_cutout( .01f ) : 2.f };
        if( uv ) {
            instance.uv_rect = { uv->left, uv->top, uv->right, uv->bottom };
        }
        instance.tint = type != pane_shape::window && type != pane_shape::turned_window ?
                        std::array<float, 4>{ tint * .66f, tint * .89f, tint, opacity } :
                        std::array<float, 4>{ tint, tint, tint, opacity };
        if( preview_color ) {
            instance.tint = *preview_color;
        }
        const float dx = rel.x() - pose.eye.x;
        const float dy = height - pose.eye.height + ( type == pane_shape::skylight ? 0.f : 1.5f );
        const float dz = rel.y() - pose.eye.y;
        panes.push_back( { type, dx * dx + dy * dy + dz * dz, instance } );
    };
    bool prepared_blueprint = false;
    const std::array<float, 4> preview_color = blueprint && blueprint->valid ?
            std::array<float, 4>{ .15f, .9f, .3f, .35f } :
            std::array<float, 4>{ .95f, .15f, .3f, .35f };
    const auto metal_deck = []( const std::string &id ) {
        return id == "t_metal_floor_no_roof" || id == "t_floor_metal_gangway";
    };
    const std::size_t atlas_slots = fp3d_materials::atlas_slot_count();
    const auto gather_tile = [&]( const fp3d_snapshot::cell &tile, bool preview,
                                  const terrain_index::chunk *chunk, bool plain_terrain ) {
        if( !preview && overhead && fp3d_snapshot::hidden_by_cutaway( scene, tile ) ) {
            return;
        }
        const auto rel = tile.position - scene.origin;
        if( !include_landscape && ( std::abs( rel.x() ) > 24 || std::abs( rel.y() ) > 24 ||
                                    tile.terrain == "t_radio_tower" ) ) {
            return;
        }
        const bool memory = tile.known == fp3d_snapshot::knowledge::remembered;
        const float tint = memory ? ( rel.z() < 0 ? .60f : .23f ) : .9f;
        const float storey = rel.z() * fp3d_camera::floor_height + ( preview ? .025f : 0.f );
        if( include_landscape ) {
            const float center = storey + fp3d_camera::floor_height * .5f;
            const float radius = std::max( 3.5f, tile.vegetation_height + .5f );
            if( !world_view.contains( rel.x() - pose.eye.x, center - pose.eye.height,
                                     rel.y() - pose.eye.y, radius ) &&
                !in_shadow_range( rel.x(), center, rel.y(), radius ) && !preview ) {
                return;
            }
        }
        const auto near_room_wall = [&]() {
            if( !preview && monster_walls.count( tile.position ) ) {
                return true;
            }
            if( preview || !overhead || !scene.pointer_room || !fp3d_snapshot::cutaway_column( scene, tile ) ||
                tile.position.z() != scene.origin.z() || tile.kind != kind::wall ) {
                return false;
            }
            int neighbours = 0;
            bool near = false;
            for( const auto &dir : { tripoint_rel_ms::north, tripoint_rel_ms::east,
                     tripoint_rel_ms::south, tripoint_rel_ms::west } ) {
                if( scene.pointer_room->interior.count( tile.position + dir ) ) {
                    ++neighbours;
                    near |= dir.x() * ( pose.eye.x - rel.x() ) +
                            dir.y() * ( pose.eye.y - rel.y() ) < 0;
                }
            }
            // Keep pillars and partitions inside the selected region full height.
            return neighbours == 1 && near;
        };
        const auto add_instance = [&]( model_key model, const std::optional<fp3d_materials::uv_rect> &uv,
                                       float height, std::optional<std::array<float, 4>> color = {},
                                       bool unlit = false, instance_group *destination = nullptr,
                                       float cutout = 0.f ) {
            fp3d_gpu::mesh_instance instance;
            instance.offset = { static_cast<float>( rel.x() ), height,
                                static_cast<float>( rel.y() ), unlit ? ( uv ? -2.f : 2.f ) :
                                ( uv ? -cutout : 1.f ) };
            if( uv ) {
                instance.uv_rect = { uv->left, uv->top, uv->right, uv->bottom };
            }
            instance.tint = color.value_or( std::array<float, 4>{ tint, tint, tint, 1 } );
            if( !preview && pickup && pickup->type == pickup_kind::furniture &&
                pickup->tile == tile.position && model.type == shape::furniture ) {
                instance.offset[3] = fp3d_gpu::white_model_material;
            }
            model.open_structure = overhead && tile.open_structure_above && !preview;
            if( preview ) {
                model.blueprint = true;
                model.surface = reflective_surface::none;
                instance.offset[3] = uv ? fp3d_gpu::unlit_cutout( .01f ) : 2.f;
                instance.tint = preview_color;
                prepared_blueprint = true;
            }
            if( iris_materials && !preview && ( model.type == shape::floor ||
                 model.type == shape::open_wall || model.type == shape::capped_wall ||
                 model.type == shape::ramp ) ) {
                const auto material = fp3d_iris::terrain_material( tile );
                if( material.surface == fp3d_iris::material_surface::solid ) {
                    model.iris_terrain_material = iris_materials->resolve( material ).value_or( -1 );
                    model.iris_terrain_solid = uv && instance.tint[3] == 1.f &&
                                               fp3d_materials::gpu_rect_is_opaque( *uv );
                    destination = nullptr;
                }
            }
            ( destination ? *destination : groups[model] ).push_back( instance );
        };
        if( cache && plain_terrain ) {
            const bool floor = tile.kind == kind::floor;
            const bool slab = tile.floor_present && !vehicle_roofs.floors.count( tile.position );
            if( floor && !slab ) {
                return;
            }
            const auto local = tile.position - chunk->anchor;
            const auto slot = local.y() * terrain_index::chunk_width + local.x();
            const auto &cached = chunk->recipes->get( slot, tile, result.material_revision, atlas_slots );
            model_key support{ shape::floor };
            support.building_height = ( chunk->shore & ( terrain_index::shore_cache::mask{1} << slot ) ) ?
                                      -.26f : -.08f;
            std::optional<std::array<float, 4>> fallback;
            if( !cached.uv ) {
                ++result.missing;
                if( floor ) {
                    const float dim = tint * .57f;
                    fallback = memory ? std::array<float, 4>{ dim, dim, dim, 1.f } :
                               std::array<float, 4>{ .32f, .36f, .28f, 1.f };
                } else if( !memory ) {
                    fallback = std::array<float, 4>{ .58f, .60f, .64f, 1.f };
                }
            }
            if( floor ) {
                if( !memory && wetness >= .02f && cached.surface != reflective_surface::none ) {
                    const auto *above = known_cell( tile.position + tripoint_rel_ms::above );
                    if( !above || !above->floor_present || above->open_air ) {
                        support.surface = cached.surface;
                    }
                }
                add_instance( support, cached.uv, storey, fallback, false, floor_group( support ) );
                ++result.floor_count;
            } else {
                if( slab ) {
                    add_instance( support, cached.uv, storey, {}, false, floor_group( support ) );
                }
                model_key wall{ shape::capped_wall };
                const auto *above = known_cell( tile.position + tripoint_rel_ms::above );
                if( fp3d_scene::upper_floor_owns_cap( above ) ) {
                    wall.type = shape::open_wall;
                    wall.param_x = fp3d_scene::water_surface_offset( *above );
                    ++result.uncapped_walls;
                }
                if( near_room_wall() ) {
                    wall.type = shape::capped_wall;
                    wall.param_x = fp3d_scene::cutaway_wall_height - fp3d_camera::floor_height;
                }
                // A wall under water can have a distinct top height.
                auto *destination = wall.param_x == 0.f ?
                                    group_for( wall_groups[wall.type == shape::open_wall], wall ) : nullptr;
                add_instance( wall, cached.uv, storey, fallback, false, destination );
                ++result.wall_count;
            }
            return;
        }
        const float water_height = fp3d_scene::water_surface_offset( tile );
        const auto tile_pane = [&]( pane_shape type, float height,
                                    const std::optional<fp3d_materials::uv_rect> &uv, float opacity = .32f ) {
            if( overhead && tile.open_structure_above && !preview ) {
                opacity *= open_structure_opacity;
                fading_open_panes |= open_structure_opacity < 1.f;
            }
            add_pane( type, rel, height, uv, tint, opacity, preview ? &preview_color : nullptr );
            prepared_blueprint |= preview;
        };
        if( tile.terrain == "t_radio_tower" ) {
            add_instance( { shape::radio_tower }, std::nullopt, storey );
            return;
        }
        if( !tile.vegetation.empty() ) {
            if( const auto uv = fp3d_materials::lookup_gpu( "$plant:" + tile.vegetation ) ) {
                const shape plant_type = tile.vegetation == "f_lilypad" ? shape::pad :
                                         tile.vegetation.compare( 0, 7, "t_grass" ) == 0 ?
                                         shape::grass : shape::plant;
                // The original tree sprite has translucent pixels joining its branches.
                const bool tree = tile.vegetation == tile.terrain &&
                                  tile.vegetation_height >= fp3d_camera::floor_height;
                add_instance( { plant_type, plant_type == shape::pad ? 0.f : tile.vegetation_height },
                              *uv, storey + water_height, {}, false, nullptr, tree ? .12f : 0.f );
                ++result.plant_count;
            } else {
                ++result.missing;
            }
        }
        if( tile.furniture ) {
            const auto &furniture = *tile.furniture;
            const auto &parts = fp3d_furniture::parts( furniture.shape );
            if( !parts.empty() ) {
                model_key furniture_model{ shape::furniture };
                furniture_model.furniture_id = furniture.id;
                furniture_model.furniture_shape = furniture.shape;
                furniture_model.bed_links = fp3d_scene::bed_links( tile, known_cell );
                furniture_model.quarter_turns = furniture_model.bed_links ? 0 :
                    ( 4 - fp3d_scene::furniture_facing( tile, known_cell ) ) % 4;
                const auto *prop = fp3d_furniture::world_prop( furniture.id );
                const bool per_face_material = prop || furniture.shape == furniture_kind::rack ||
                                               furniture.shape == furniture_kind::udp_terminal ||
                                               furniture.shape == furniture_kind::udp_air_conditioner ||
                                               furniture.shape == furniture_kind::udp_water_purifier;
                const bool textured = ( prop || !solid_furniture_color( furniture.shape, 0 ) ) &&
                                      ( per_face_material ?
                                      fp3d_materials::lookup_furniture_gpu( furniture.id, 0 ).side.has_value() :
                                      furniture.shape != furniture_kind::udp_stop_sign &&
                                      fp3d_materials::lookup_gpu( "$furniture:" + furniture.id ).has_value() );
                // The per-face furniture UVs are already mapped to the full atlas.
                add_instance( furniture_model, textured ? unit_uv : std::nullopt, storey );
                if( furniture.id == "f_planter_seedling" ) {
                    if( const auto shoots = fp3d_materials::lookup_gpu( "$plant:f_planter_seedling" ) ) {
                        add_instance( { shape::plant, .26f, .47f }, shoots, storey + parts[1].top );
                        ++result.plant_count;
                    }
                }
                if( std::any_of( parts.begin(), parts.end(), []( const fp3d_furniture::part &part ) {
                return part.glass;
                } ) ) {
                    furniture_model.type = shape::furniture_glass;
                    add_instance( furniture_model, std::nullopt, storey,
                                  std::array<float, 4>{ .57f, .83f, .94f, .27f }, true );
                }
                if( furniture.shape == furniture_kind::street_light ) {
                    furniture_model.type = shape::furniture_light_head;
                    add_instance( furniture_model, std::nullopt, storey,
                                  std::array<float, 4>{ .58f, .63f, .67f, 1.f } );
                    furniture_model.param_x = 1.f;
                    add_instance( furniture_model, std::nullopt, storey,
                                  std::array<float, 4>{ .95f, .92f, .76f, 1.f }, true );
                } else if( furniture.shape == furniture_kind::kiln ) {
                    furniture_model.type = shape::kiln_fire;
                    add_instance( furniture_model, std::nullopt, storey,
                                  std::array<float, 4>{ 1, 1, 1, 1 }, true );
                }
                ++result.furniture_count;
            }
        }
        const auto *above = known_cell( tile.position + tripoint_rel_ms::above );
        const auto *grade_support = include_landscape ? fp3d_ramp::support( tile, known_cell ) : nullptr;
        const bool sloped_railing = grade_support && grade_support != &tile;
        if( !preview && sloped_railing && above && fp3d_ramp::covered( *grade_support,
                known_cell( grade_support->position + tripoint_rel_ms::above ) ) ) {
            return;
        }
        const auto grade = sloped_railing ? fp3d_ramp::plane( *grade_support, known_cell ) :
                           fp3d_ramp::surface{};
        const float bounced = tint * ( .18f + .44f * std::clamp( scene.sky.daylight, 0.f, 1.f ) );
        const auto under_tint = std::array<float, 4>{ bounced, bounced, bounced, 1.f };
        // A presentation cut must not let daylight through the physical roof.
        const auto *physical_above = terrain.cells.get( tile.position + tripoint_rel_ms::above );
        const bool lifted_ceiling = overhead && scene.camera_cutaway &&
                                    tile.position.z() == scene.origin.z() && fp3d_snapshot::cutaway_column( scene, tile );
        const bool clear_overhead = !tile.ceiling_present && !tile.cutaway_ceiling &&
                                    !( physical_above && physical_above->floor_present && !physical_above->open_air );
        if( include_landscape && tile.ceiling_present && !lifted_ceiling && !tile.glass_ceiling &&
            !vehicle_roofs.ceilings.count( tile.position ) &&
            !fp3d_scene::upper_floor_owns_cap( above ) &&
            tile.terrain.compare( 0, 9, "t_treetop" ) != 0 ) {
            const auto uv = fp3d_materials::lookup_gpu( tile.ceiling_surface.empty() ?
                            "t_floor" : tile.ceiling_surface );
            const bool metal = metal_deck( tile.ceiling_surface );
            add_instance( { shape::ceiling }, uv, storey + fp3d_camera::floor_height - .08f,
                          metal ? std::optional( under_tint ) : std::nullopt, metal );
        }
        if( fp3d_snapshot::is_glass_roof( tile.terrain ) ) {
            tile_pane( pane_shape::skylight, storey, std::nullopt );
        }
        if( tile.ceiling_present && !lifted_ceiling && tile.glass_ceiling &&
            !( above && fp3d_snapshot::is_glass_roof( above->terrain ) ) ) {
            tile_pane( pane_shape::skylight, storey + fp3d_camera::floor_height - .08f, std::nullopt );
        }
        if( fp3d_scene::stair_flight_visible( tile, above ) ) {
            if( const auto uv = fp3d_materials::lookup_gpu( fp3d_materials::terrain_key( tile ) ) ) {
                const auto section = tile.stairs.value_or( fp3d_snapshot::stair_section{} );
                model_key stair_model{ shape::stairs };
                stair_model.stair_rows = section.rows;
                stair_model.stair_south = section.south;
                add_instance( stair_model, fp3d_geometry::stair_uv( *uv, section ), storey );
                ++result.stair_count;
            } else {
                ++result.missing;
            }
        }
        if( include_landscape && tile.terrain == "t_wood_stairs_up_half" ) {
            add_instance( { shape::stair_stringers },
                          fp3d_materials::lookup_gpu( fp3d_materials::terrain_key( tile ) ), storey );
        }
        model_key model{ shape::floor };
        const char *guardrail_ground = fp3d_snapshot::guardrail_ground( tile.terrain );
        const bool prison_barrier = fp3d_snapshot::is_prison_barrier( tile.terrain );
        const auto *world_prop = fp3d_furniture::world_prop( tile.terrain );
        const bool picket_barrier = tile.terrain == "t_fence" ||
                                    fp3d_snapshot::is_picket_gate( tile.terrain );
        const bool metal_barrier = tile.terrain == "t_metal_railing" || tile.terrain == "t_fence_metal" ||
                                   fp3d_snapshot::is_metal_gate( tile.terrain );
        const bool glass_door = tile.terrain.find( "door" ) != std::string::npos &&
                                tile.terrain.find( "glass" ) != std::string::npos;
        const auto wall_at = [&]( const tripoint_rel_ms &direction ) {
            return fp3d_scene::wall_neighbour( known_cell( tile.position + direction ) );
        };
        const auto orient_opening = [&] {
            model.upper_floor = !preview && fp3d_scene::upper_floor_owns_cap( above );
            model.quarter_turns = wall_at( tripoint_rel_ms::east ) + wall_at( tripoint_rel_ms::west ) <
                                 wall_at( tripoint_rel_ms::north ) + wall_at( tripoint_rel_ms::south );
        };
        float base_height = storey;
        if( include_landscape && ( tile.kind == kind::ladder || tile.kind == kind::downspout ) ) {
            model_key climb{ shape::climb };
            climb.door_kind = tile.kind;
            climb.param_x = tile.goes_up ? 1.f : 0.f;
            climb.quarter_turns = ( 4 - fp3d_furniture::facing( furniture_kind::cabinet,
            { wall_at( tripoint_rel_ms::north ), wall_at( tripoint_rel_ms::east ),
              wall_at( tripoint_rel_ms::south ), wall_at( tripoint_rel_ms::west ) } ) ) % 4;
            add_instance( climb, fp3d_materials::lookup_gpu( tile.terrain == "t_vitrified_downspout" ?
                          "t_vitrified_floor" : "t_metal_floor" ), storey );
        }
        const auto fence_neighbours = [&]() {
            std::array<bool, 4> linked{};
            const std::array<tripoint_rel_ms, 4> directions{ tripoint_rel_ms::north, tripoint_rel_ms::east,
                tripoint_rel_ms::south, tripoint_rel_ms::west
            };
            for( size_t i = 0; i < directions.size(); ++i ) {
                const auto *other = known_cell( tile.position + directions[i] );
                linked[i] = other && fp3d_snapshot::fence_connects( tile.terrain, other->terrain );
            }
            return linked;
        };
        if( tile.is_ramp ) {
            if( !preview && fp3d_ramp::covered( tile, above ) ) {
                return;
            }
            const auto plane = fp3d_ramp::plane( tile, known_cell );
            model.type = shape::ramp;
            model.param_x = plane.dx;
            model.param_y = plane.dy;
            base_height += plane.center;
        } else if( include_landscape && world_prop ) {
            model.type = shape::built_terrain;
            model.terrain_id = tile.terrain;
            model.quarter_turns = fp3d_scene::world_prop_turn( tile, *world_prop, known_cell );
        } else if( include_landscape && ( fp3d_scene::glass_wall( tile ) ||
                                          tile.terrain == "t_glass_railing" ) ) {
            model.type = shape::glass_barrier;
            model.terrain_id = tile.terrain;
            const auto links = fp3d_scene::glass_barrier_links( tile, known_cell );
            for( size_t i = 0; i < links.size(); ++i ) {
                model.fence_links |= static_cast<unsigned>( links[i] ) << i;
            }
            if( !fp3d_snapshot::broken_glass_wall( tile.terrain ) ) {
                auto pane = model;
                pane.type = shape::barrier_pane;
                const float opacity = tile.terrain.find( "frosted" ) != std::string::npos ? .60f : .30f;
                add_instance( pane, std::nullopt, storey,
                              std::array<float, 4>{ .56f * tint, .84f * tint, .94f * tint, opacity }, true );
            }
        } else if( include_landscape && ( tile.kind == kind::fence ||
                   fp3d_snapshot::is_chain_gate( tile.terrain ) ||
                   fp3d_snapshot::is_metal_gate( tile.terrain ) ||
                   fp3d_snapshot::is_picket_gate( tile.terrain ) ||
                   tile.terrain == "t_column" || tile.terrain == "t_pillar" ||
                   tile.terrain == "t_little_column" || tile.terrain == "t_column_halfway" ) ) {
            const auto links = prison_barrier ? fp3d_scene::prison_barrier_links( tile, known_cell ) :
                               fence_neighbours();
            model.terrain_id = tile.terrain;
            for( size_t i = 0; i < links.size(); ++i ) {
                model.fence_links |= static_cast<unsigned>( links[i] ) << i;
            }
            if( tile.terrain.size() >= 2 &&
                tile.terrain.compare( tile.terrain.size() - 2, 2, "_o" ) == 0 ) {
                model.door_swing = fp3d_scene::door_inward( tile,
                                   fp3d_snapshot::chain_gate_along_x( links ), false, known_cell );
            }
            model.type = fp3d_snapshot::is_chain_gate( tile.terrain ) ? shape::chain_gate :
                         fp3d_snapshot::terrain_model_parts( tile.terrain, links ).empty() ?
                         shape::fence : shape::built_terrain;
        } else if( tile.kind == kind::wall && tile.vegetation.empty() &&
            tile.terrain != "t_column" && tile.terrain != "t_pillar" &&
            tile.terrain != "t_little_column" && tile.terrain != "t_column_halfway" ) {
            model.type = shape::capped_wall;
            if( !preview && fp3d_scene::upper_floor_owns_cap( above ) ) {
                model.type = shape::open_wall;
                model.param_x = fp3d_scene::water_surface_offset( *above );
            }
            if( near_room_wall() ) {
                model.type = shape::capped_wall;
                model.param_x = fp3d_scene::cutaway_wall_height - fp3d_camera::floor_height;
            }
        } else if( tile.kind == kind::closed_door || tile.kind == kind::open_door ||
                   tile.kind == kind::damaged_door || ( include_landscape && tile.kind == kind::glass ) ) {
            if( !include_landscape && glass_door ) { return; }
            model.type = shape::door;
            model.door_kind = tile.kind;
            model.terrain_id = tile.terrain;
            const auto closed = tile.kind == kind::open_door ?
                                fp3d_snapshot::closed_door_id( tile.terrain ) : std::string{};
            model.glass_door = glass_door && !fp3d_snapshot::broken_glass_door( tile.terrain );
            orient_opening();
            if( !closed.empty() ) {
                model.door_swing = fp3d_scene::door_inward( tile, !model.quarter_turns, true, known_cell );
            }
            if( tile.kind == kind::glass || ( model.glass_door && model.door_swing ) ) {
                const pane_shape pane = !model.door_swing ?
                                        ( model.quarter_turns ? pane_shape::turned_glass : pane_shape::full_glass ) :
                                        model.quarter_turns ?
                                        ( model.door_swing > 0 ? pane_shape::turned_open_glass_in :
                                          pane_shape::turned_open_glass_out ) :
                                        ( model.door_swing > 0 ? pane_shape::open_glass_in :
                                          pane_shape::open_glass_out );
                tile_pane( pane, storey, std::nullopt,
                           tile.terrain.find( "frosted" ) != std::string::npos ? .60f : .32f );
            }
        } else if( tile.kind == kind::window ) {
            model.type = shape::window;
            model.terrain_id = tile.terrain;
            orient_opening();
            if( tile.pane == fp3d_snapshot::window_pane::glazed &&
                !fp3d_snapshot::broken_window_glass( tile.terrain ) ) {
                tile_pane( model.quarter_turns ? pane_shape::turned_window : pane_shape::window,
                           storey, fp3d_materials::lookup_gpu( "$window:" + tile.terrain ) );
            }
            if( tile.pane == fp3d_snapshot::window_pane::covered ) {
                if( peek_window && *peek_window == tile.position ) {
                    const auto fabric = fp3d_materials::lookup_gpu( "$window:" + tile.terrain );
                    tile_pane( model.quarter_turns ? pane_shape::turned_window : pane_shape::window,
                               storey, fabric, .22f );
                    if( fabric ) {
                        panes.back().instance.offset[3] = fp3d_gpu::soft_curtain_material;
                    }
                } else {
                    model_key curtain{ shape::curtain };
                    curtain.quarter_turns = model.quarter_turns;
                    add_instance( curtain, fp3d_materials::lookup_gpu( "$window:" + tile.terrain ),
                                  storey, {}, true );
                }
            }
        } else if( ( include_landscape || !memory ) && !tile.exterior_only &&
                   water_height < 0 ) {
            model.type = shape::water;
            if( !memory ) {
                const bool deep = tile.water ? tile.water->depth == fp3d_snapshot::water_depth::deep :
                                  tile.terrain.find( "_dp" ) != std::string::npos;
                model.surface = deep ?
                                reflective_surface::water_deep : reflective_surface::water_shallow;
                if( iris_water_materials && !preview ) {
                    model.iris_water_material = iris_water_materials->resolve( fp3d_iris::terrain_material( tile ) );
                }
            }
            model.water_sky = clear_overhead && tile.position.z() >= 0;
            base_height += water_height;
        } else if( include_landscape && tile.kind == kind::obstacle ) {
            model.type = shape::obstacle;
        } else if( ( preview || !vehicle_roofs.floors.count( tile.position ) ) &&
                   ( include_landscape || ( rel.z() == 0 && !memory ) ) &&
                   ( tile.kind == kind::floor || ( include_landscape &&
                     ( tile.kind == kind::ladder || tile.kind == kind::downspout ) ) ) && tile.floor_present &&
                   !tile.open_air && !tile.goes_up && !tile.goes_down &&
                   !tile.exterior_only && tile.terrain.compare( 0, 9, "t_treetop" ) != 0 &&
                   water_height == 0 &&
                   !fp3d_snapshot::is_glass_roof( tile.terrain ) ) {
            model.type = shape::floor;
            if( !memory && wetness >= .02f && clear_overhead ) {
                const auto pavement = fp3d_scene::classify_pavement( tile.terrain );
                if( pavement == fp3d_scene::pavement_surface::concrete ) {
                    model.surface = reflective_surface::concrete;
                } else if( pavement == fp3d_scene::pavement_surface::asphalt ) {
                    model.surface = reflective_surface::asphalt;
                }
            }
        } else {
            return;
        }

        const bool floor_metal = tile.metal_deck_railing || metal_deck( tile.terrain );
        const bool plant_ground = tile.terrain == tile.vegetation &&
                                  tile.terrain.compare( 0, 7, "t_grass" ) != 0;
        std::optional<std::string> connected_key;
        const auto tile_material_key = [&]() -> const std::string & {
            if( tile.ground_connections < 0 && tile.roof_connections < 0 ) {
                return tile.terrain;
            }
            if( !connected_key ) {
                connected_key = fp3d_materials::terrain_key( tile );
            }
            return *connected_key;
        };
        const auto floor_material_key = [&]() -> const std::string & {
            static const std::string metal = "t_metal_floor_no_roof";
            static const std::string dirt = "t_dirt";
            return tile.metal_deck_railing ? metal : plant_ground ? dirt : tile_material_key();
        };
        const auto floor_profile = [&]() {
            model_key floor{ shape::floor };
            floor.param_x = grade.dx;
            floor.param_y = grade.dy;
            floor.metal_deck = floor_metal;
            const auto mask = chunk ? chunk->shore : terrain.shore.shore_in(
                                  terrain_index::shore_cache::block_of( tile.position ) );
            floor.building_height = ( mask & terrain_index::shore_cache::cell_bit( tile.position ) ) ?
                                    -.26f : -.08f;
            return floor;
        };
        std::optional<fp3d_materials::uv_rect> uv;
        std::optional<std::array<float, 4>> fallback;
        if( model.door_kind == kind::glass || model.glass_door || model.type == shape::glass_barrier ) {
            fallback = std::array<float, 4>{ .46f * tint, .52f * tint, .56f * tint, 1.f };
        } else if( model.type == shape::fence || model.type == shape::chain_gate ) {
            const bool timber = tile.terrain.compare( 0, 13, "t_chickenwire" ) == 0 ||
                                tile.terrain == "t_screened_porch_wall" || tile.terrain == "t_fence_post";
            fallback = timber ? std::array<float, 4>{ .49f * tint, .34f * tint, .19f * tint, 1.f } :
                       std::array<float, 4>{ .54f * tint, .58f * tint, .60f * tint, 1.f };
        } else if( guardrail_ground ) {
            fallback = std::array<float, 4>{ .53f * tint, .57f * tint, .60f * tint, 1.f };
        } else if( picket_barrier ) {
            uv = fp3d_materials::picket_wood_gpu();
            if( !uv ) {
                fallback = std::array<float, 4>{ .76f * tint, .74f * tint, .68f * tint, 1.f };
            }
        } else if( prison_barrier ) {
            fallback = std::array<float, 4>{ .68f * tint, .69f * tint, .70f * tint, 1.f };
        } else if( metal_barrier ) {
            fallback = std::array<float, 4>{ .49f * tint, .54f * tint, .56f * tint, 1.f };
        } else if( world_prop && model.type == shape::built_terrain ) {
            uv = fp3d_materials::lookup_furniture_gpu( tile.terrain, 0 ).side ?
                 unit_uv : std::nullopt;
            if( !uv ) {
                fallback = std::array<float, 4>{ tint, tint, tint, 1.f };
            }
        } else if( model.type == shape::built_terrain ) {
            uv = fp3d_materials::lookup_gpu( tile.kind == kind::fence ? "$fence:" + tile.terrain : "t_concrete" );
        } else if( fp3d_snapshot::broken_glass_door( tile.terrain ) ||
                   fp3d_snapshot::broken_window_glass( tile.terrain ) ) {
            fallback = std::array<float, 4>{ .71f * tint, .69f * tint, .66f * tint, 1.f };
        } else if( model.type == shape::door && model.door_swing ) {
            uv = fp3d_materials::lookup_gpu( fp3d_snapshot::closed_door_id( tile.terrain ) );
        } else if( model.type == shape::window ) {
            uv = fp3d_materials::lookup_gpu( fp3d_scene::window_surround( tile, known_cell ) );
        } else {
            uv = fp3d_materials::lookup_gpu( model.type == shape::floor ? floor_material_key() :
                                             tile_material_key() );
        }
        if( include_landscape && tile.floor_present && !tile.open_air && !tile.goes_down &&
            ( preview || !vehicle_roofs.floors.count( tile.position ) ) &&
            tile.vegetation.empty() && !fp3d_snapshot::is_glass_roof( tile.terrain ) &&
            model.type != shape::floor && model.type != shape::ramp && model.type != shape::water ) {
            const auto floor = floor_profile();
            auto floor_uv = fp3d_materials::lookup_gpu(
                                      world_prop && model.type == shape::built_terrain ?
                                      ( tile.ceiling_present || tile.glass_ceiling || tile.cutaway_ceiling ?
                                        "t_floor" : "t_dirt" ) :
                                      guardrail_ground ? guardrail_ground :
                                      tile.metal_deck_railing ? floor_material_key() :
                                      fp3d_snapshot::broken_glass_door( tile.terrain ) ||
                                      fp3d_snapshot::broken_glass_wall( tile.terrain ) ? floor_material_key() :
                                      model.type == shape::door || model.type == shape::glass_barrier ||
                                      metal_barrier || prison_barrier ? "t_floor" :
                                      model.type == shape::fence || model.type == shape::chain_gate ?
                                      "t_dirt" : floor_material_key() );
            if( picket_barrier ) {
                floor_uv.reset();
                for( const auto &direction : { tripoint_rel_ms::north, tripoint_rel_ms::east,
                         tripoint_rel_ms::south, tripoint_rel_ms::west } ) {
                    const auto *ground = known_cell( tile.position + direction );
                    if( ground && ground->floor_present && ground->kind == kind::floor &&
                        ground->ground_connections >= 0 &&
                        fp3d_scene::water_surface_offset( *ground ) == 0.f ) {
                        floor_uv = fp3d_materials::lookup_gpu( fp3d_materials::terrain_key( *ground ) );
                        if( floor_uv ) { break; }
                    }
                }
                if( !floor_uv ) { floor_uv = fp3d_materials::lookup_gpu( "t_dirt" ); }
            }
            std::optional<std::array<float, 4>> floor_fallback;
            if( !floor_uv && guardrail_ground ) {
                floor_uv = fp3d_materials::lookup_gpu( "t_pavement" );
                if( !floor_uv ) {
                    floor_fallback = std::array<float, 4>{ .24f * tint, .23f * tint, .22f * tint, 1.f };
                }
            } else if( !floor_uv && world_prop && model.type == shape::built_terrain ) {
                floor_fallback = std::array<float, 4>{ .33f * tint, .31f * tint, .27f * tint, 1.f };
            } else if( !floor_uv && prison_barrier ) {
                floor_fallback = std::array<float, 4>{ .35f * tint, .38f * tint, .41f * tint, 1.f };
            } else if( !floor_uv && picket_barrier ) {
                floor_fallback = std::array<float, 4>{ .43f * tint, .35f * tint, .24f * tint, 1.f };
            }
            add_instance( floor, floor_uv, storey + grade.center, floor_fallback );
            if( floor_metal ) {
                add_instance( { shape::ceiling }, floor_uv, storey + grade.center + floor.building_height,
                              under_tint, true );
            }
        }
        if( !uv && !fallback ) {
            ++result.missing;
            if( !memory ) {
                if( model.type == shape::capped_wall || model.type == shape::open_wall ) {
                    fallback = std::array<float, 4>{ .58f, .60f, .64f, 1 };
                } else if( model.type == shape::door ) {
                    fallback = std::array<float, 4>{ .62f, .36f, .15f, 1 };
                } else if( model.type == shape::water ) {
                    fallback = std::array<float, 4>{ .12f, .29f, .43f, 1 };
                } else if( model.type != shape::window ) {
                    fallback = std::array<float, 4>{ .32f, .36f, .28f, 1 };
                }
            } else if( model.type == shape::ramp || model.type == shape::floor ) {
                fallback = std::array<float, 4>{ tint * .57f, tint * .57f, tint * .57f, 1 };
            }
        }
        if( model.type == shape::floor ) {
            const auto floor = floor_profile();
            model.building_height = floor.building_height;
            model.metal_deck = floor.metal_deck;
            if( floor_metal ) {
                add_instance( { shape::ceiling }, uv, base_height + floor.building_height, under_tint, true );
            }
        } else if( sloped_railing ) {
            model.param_x = grade.dx;
            model.param_y = grade.dy;
            base_height += grade.center;
        }
        if( model.type == shape::water && uv && tile.ground_connections >= 0 &&
            tile.ground_connections < 15 ) {
            // Fill the dry portion of the connected shore sprite beneath the water.
            // The slight depth separation avoids fighting the water's cutout.
            const auto soil = fp3d_materials::lookup_gpu( "t_dirt" );
            add_instance( { shape::landscape_ground, 1.f, 1.f }, soil, base_height - .006f,
                          soil ? std::array<float, 4>{ tint, tint, tint, 1.f } :
                          std::array<float, 4>{ .48f * tint, .39f * tint, .27f * tint, 1.f } );
        }
        add_instance( model, uv, base_height, fallback );
        if( preview ) {
            return;
        }
        if( model.type == shape::floor || model.type == shape::water ) {
            ++result.floor_count;
            float layer_height = base_height + fp3d_geometry::first_ground_layer_offset;
            for( const auto &field : tile.ground_fields ) {
                if( const auto surface = fp3d_materials::lookup_gpu(
                                             fp3d_materials::ground_field_key( field ) ) ) {
                    fp3d_gpu::mesh_instance layer;
                    layer.offset = { static_cast<float>( rel.x() ), layer_height,
                                     static_cast<float>( rel.y() ), fp3d_gpu::lit_texture_without_cutout };
                    layer.uv_rect = { surface->left, surface->top, surface->right, surface->bottom };
                    layer.tint = { tint, tint, tint, 1.f };
                    ground_layers.push_back( layer );
                    layer_height += fp3d_geometry::ground_layer_spacing;
                }
            }
        } else if( model.type == shape::ramp ) {
            ++result.ramp_count;
        } else if( model.type == shape::door ) {
            ++result.door_count;
        } else if( model.type == shape::window ) {
            ++result.window_count;
        } else {
            ++result.wall_count;
            result.uncapped_walls += model.type == shape::open_wall;
        }
    };
    std::vector<bool> chunk_visible;
    chunk_visible.reserve( terrain.chunks.size() );
    for( const auto &bucket : terrain.chunks ) {
        const auto rel = bucket.anchor - scene.origin;
        const float x = rel.x() + terrain_index::chunk_half_width;
        const float y = ( rel.z() + .5f ) * fp3d_camera::floor_height;
        const float z = rel.y() + terrain_index::chunk_half_width;
        chunk_visible.push_back( world_view.contains( x - pose.eye.x, y - pose.eye.height,
                                 z - pose.eye.y, bucket.radius ) ||
                                 in_shadow_range( x, y, z, bucket.radius ) );
    }
    for( size_t i = 0; i < scene.cells.size(); ++i ) {
        if( !include_landscape || chunk_visible[terrain.chunk_ids[i]] ) {
            gather_tile( scene.cells[i], false, include_landscape ?
                         &terrain.chunks[terrain.chunk_ids[i]] : nullptr,
                         !terrain.plain_cells.empty() && terrain.plain_cells[i] );
        }
    }
    if( blueprint && blueprint->terrain ) {
        gather_tile( *blueprint->terrain, true, nullptr, false );
    }

    timing.stage( "gpu_prepare_landscape_gather" );
    if( include_landscape ) {
        for( const auto &patch : scene.landscape ) {
            const auto rel = patch.position - scene.origin;
            const float x = rel.x() + ( patch.width - 1 ) / 2.f;
            const float z = rel.y() + ( patch.depth - 1 ) / 2.f;
            const float y = rel.z() * fp3d_camera::floor_height - .06f;
            const bool has_trees = patch.wooded && patch.width >= 8 && patch.depth >= 8;
            const float tree_height = has_trees ? fp3d_geometry::landscape_tree_height : 0.f;
            const float radius = std::hypot( patch.width / 2.f + tree_height / 2.f,
                                             patch.depth / 2.f + tree_height / 2.f,
                                             std::max( patch.building_height, tree_height ) );
            if( !world_view.contains( x - pose.eye.x, y - pose.eye.height, z - pose.eye.y, radius ) ) {
                if( result.atmosphere.sun_direction_strength[3] <= 0 ) {
                    continue;
                }
                // Off-screen landscape can cast into the visible scene.
                const auto light_position = result.atmosphere.far_sun_clip_from_world.transform( x, y, z );
                const float border = 1.f + radius / far_sun_radius;
                if( std::abs( light_position[0] ) > border || std::abs( light_position[1] ) > border ) {
                    continue;
                }
            }
            const auto append = [&]( const model_key &model,
                                     const std::optional<fp3d_materials::uv_rect> &uv,
                                     float east, float height, float south,
                                     const std::array<float, 4> &color, float textured_mode = 0.f ) {
                fp3d_gpu::mesh_instance instance;
                instance.offset = { east, height, south, uv ? textured_mode : 1.f };
                if( uv ) {
                    instance.uv_rect = { uv->left, uv->top, uv->right, uv->bottom };
                }
                instance.tint = color;
                groups[model].push_back( instance );
            };
            if( patch.ground_present ) {
                const auto uv = fp3d_materials::lookup_gpu( patch.terrain );
                const bool road = patch.terrain == "t_pavement";
                const float tint = road ? .9f : .65f;
                const std::array<float, 4> color{ tint, tint, tint, 1 };
                const bool tiled_road = road && std::abs( rel.x() ) < 104 &&
                                        std::abs( rel.y() ) < 104 && patch.width * patch.depth <= 576;
                if( tiled_road ) {
                    const model_key tile{ shape::landscape_ground, 1, 1 };
                    for( int dz = 0; dz < patch.depth; ++dz ) {
                        for( int dx = 0; dx < patch.width; ++dx ) {
                            append( tile, uv, rel.x() + dx, y, rel.y() + dz, color );
                        }
                    }
                } else {
                    append( { shape::landscape_ground, static_cast<float>( patch.width ),
                              static_cast<float>( patch.depth ) }, uv, x, y, z, color );
                }
            }
            const bool has_building = patch.building_height > 0 && patch.width >= 8 && patch.depth >= 8;
            if( has_building ) {
                const auto wall = fp3d_materials::lookup_gpu( patch.wall_material );
                std::array<float, 4> tint{ .77f, .74f, .70f, 1 };
                if( wall ) {
                    tint = { .98f, .98f, .98f, 1 };
                } else if( patch.wall_material == "t_brick_wall" ) {
                    tint = { .65f, .35f, .29f, 1 };
                } else if( patch.wall_material == "t_concrete_wall" ) {
                    tint = { .66f, .67f, .65f, 1 };
                } else if( patch.wall_material == "t_wall_metal" ) {
                    tint = { .48f, .57f, .62f, 1 };
                } else if( patch.wall_material == "t_wall_wood" || patch.wall_material == "t_wall_log" ) {
                    tint = { .59f, .45f, .32f, 1 };
                }
                const shape facade = patch.ground_present ? shape::landscape_facade_tiled :
                                     shape::landscape_facade;
                append( { facade, patch.width * .72f, patch.depth * .72f, patch.building_height },
                        wall, x, y, z, tint );
                append( { shape::landscape_ground, patch.width * .76f, patch.depth * .76f },
                        fp3d_materials::lookup_gpu( "t_flat_roof" ),
                        x, y + patch.building_height + .02f, z, { .96f, .96f, .96f, 1 } );
            }
            if( has_trees ) {
                if( const auto uv = fp3d_materials::lookup_gpu( "$plant:t_tree" ) ) {
                    const float tint = has_building ? .96f : patch.terrain == "t_pavement" ? .9f : .65f;
                    for( const auto &position : fp3d_geometry::landscape_tree_positions ) {
                        append( { shape::plant, tree_height }, uv, x + position[0], y, z + position[1],
                                { tint, tint, tint, 1.f }, fp3d_gpu::unlit_cutout( .5f ) );
                        ++result.plant_count;
                    }
                }
            }
        }
    }

    timing.stage( "gpu_prepare_model_groups" );
    if( cache ) {
        cache->model_meshes.set_atlas_layout( result.material_revision, atlas_slots );
        cache->resident_models.set_atlas_layout( result.material_revision, atlas_slots );
    }
    // Encode static instances against a stable map origin within each
    // anchor region. Keep game/view, picking and shader lighting scene-relative.
    constexpr int anchor_width = 128;
    const auto shift = []( int coordinate ) {
        const int remainder = coordinate % anchor_width;
        return -static_cast<float>( remainder < 0 ? remainder + anchor_width : remainder );
    };
    result.resident_to_scene = { shift( scene.origin.x() ), 0.f, shift( scene.origin.y() ) };
    if( cache && cache->retain_models_on_gpu ) {
        cache->begin_resident_pages( result.material_revision );
    }
    const auto encode_resident = [&]( fp3d_gpu::mesh_instance instance ) {
        instance.offset[0] -= result.resident_to_scene[0];
        instance.offset[2] -= result.resident_to_scene[2];
        return instance;
    };
    const auto draw_block = [&]( const fp3d_gpu::mesh_instance &instance ) {
        const auto &offset = instance.offset;
        const auto absolute = scene.origin + tripoint_rel_ms( static_cast<int>( std::floor( offset[0] ) ),
                              static_cast<int>( std::floor( offset[2] ) ),
                              static_cast<int>( std::lround( offset[1] / fp3d_camera::floor_height ) ) );
        return terrain_index::shore_cache::block_of( absolute );
    };
    bool has_resident = false;
    size_t split_pages = 0;
    for( const auto &[model, group] : groups ) {
        auto geometry_model = model;
        geometry_model.open_structure = false;
        geometry_model.iris_terrain_material.reset();
        geometry_model.iris_terrain_solid = false;
        geometry_model.iris_water_material.reset();
        const auto build = [&]( fp3d_geometry::mesh &mesh ) { emit_model( geometry_model, mesh ); };
        const bool fade = model.open_structure && open_structure_opacity < 1.f;
        // Static structure meshes remain resident across both passes.
        // Intrinsically transparent effects retain their dynamic geometry.
        constexpr size_t resident_limit = resident_instance_buffer_limit / sizeof( fp3d_gpu::mesh_instance ) -
                                          terrain_index::instance_pages::max_extent;
        const bool retain = cache && cache->retain_models_on_gpu && !model.blueprint &&
                            model.type != shape::furniture_glass && model.type != shape::barrier_pane &&
                            model.type != shape::kiln_fire &&
                            group.size() <= resident_limit - result.resident_instances.size();
        const auto resident = retain ? cache->resident_models.retain( geometry_model, build ) : std::nullopt;
        auto &instance_stream = resident ? result.resident_instances : result.instances;
        batch draw{ static_cast<Uint32>( result.geometry.indices.size() ), 0,
                    static_cast<Uint32>( instance_stream.size() ), static_cast<Uint32>( group.size() ) };
        if( resident ) {
            draw.first_index = resident->first;
            draw.index_count = resident->count;
            draw.resident = true;
            has_resident |= draw.index_count != 0;
        } else {
            if( cache ) {
                cache->model_meshes.append( geometry_model, result.geometry, build );
            } else {
                build( result.geometry );
            }
            draw.index_count = static_cast<Uint32>( result.geometry.indices.size() ) - draw.first_index;
        }
        if( !draw.index_count ) {
            continue;
        }
        const bool terrain_model = model.type == shape::floor || model.type == shape::open_wall ||
                                   model.type == shape::capped_wall;
        if( fade ) {
            if( draw.resident ) {
                result.resident_instance_count += group.size();
            }
            if( model.type != shape::barrier_pane && model.type != shape::furniture_glass &&
                model.type != shape::kiln_fire ) {
                result.open_structure_shadows.push_back( draw );
            }
            const bool slab = model.type == shape::floor || model.type == shape::ceiling;
            for( auto instance : group ) {
                draw.first_instance = static_cast<Uint32>( instance_stream.size() );
                draw.instance_count = 1;
                sorted_blends.push_back( {
                    blend_depth( instance, slab ? 0.f : fp3d_camera::floor_height * .5f ), draw
                } );
                instance.tint[3] *= open_structure_opacity;
                if( instance.offset[3] == 0.f || instance.offset[3] == 1.f ||
                    instance.offset[3] == fp3d_gpu::white_model_material ) {
                    // Cut out the texture's own holes before applying
                    // the instance opacity. Keep the original lighting.
                    instance.offset[3] = instance.offset[3] == 0.f ?
                                         fp3d_gpu::faded_texture_material : fp3d_gpu::faded_solid_material;
                }
                instance_stream.push_back( draw.resident ? encode_resident( instance ) : instance );
            }
            continue;
        }
        auto &queue = model.blueprint ? blueprint_models :
                      ( model.type == shape::furniture_glass || model.type == shape::barrier_pane ||
                        model.type == shape::kiln_fire ) ? translucent_models : result.batches;
        const auto emit_batch = [&]( const batch &part ) {
            queue.push_back( part );
            queue.back().iris_water = model.type == shape::water &&
                                      model.surface != reflective_surface::none && !model.blueprint;
            queue.back().iris_terrain_material = model.iris_terrain_material;
            queue.back().iris_terrain_solid = model.iris_terrain_solid;
            if( iris_basic && model.type == shape::furniture && !model.blueprint ) {
                queue.back().iris_basic = std::all_of( group.begin(), group.end(), []( const auto &instance ) {
                    return ( instance.offset[3] == 1.f || instance.offset[3] == 2.f ) && instance.tint[3] == 1.f;
                } );
            }
            if( model.type == shape::furniture || model.type == shape::furniture_glass ||
                model.type == shape::furniture_light_head || model.type == shape::kiln_fire ) {
                queue.back().shadow_caster = iris_caster::block;
                if( iris_materials && model.type == shape::furniture && !model.blueprint &&
                    std::all_of( group.begin(), group.end(), []( const auto &instance ) {
                    return instance.offset[3] <= 0.f && instance.offset[3] != fp3d_gpu::white_sprite_material;
                } ) ) {
                    fp3d_iris::virtual_material material;
                    material.native_id = "cdda:" + model.furniture_id;
                    queue.back().iris_model_id = iris_materials->resolve( material ).value_or( -1 );
                }
            }
            if( model.surface != reflective_surface::none ) {
                const bool water = model.type == shape::water;
                const float roughness = water ? .13f : model.surface == reflective_surface::concrete ? .69f : .48f;
                const float optical_depth = water ?
                                            ( model.surface == reflective_surface::water_deep ? .95f : .35f ) : 0.f;
                result.reflective_batches.push_back( { part,
                    { water ? 1.f : wetness, roughness, model.water_sky ? 1.f : 0.f, optical_depth },
                    model.surface, {}, model.iris_water_material
                } );
            }
        };
        if( draw.resident ) {
            result.resident_instance_count += group.size();
            if( terrain_model && !model.open_structure && cache->split_terrain_instances &&
                split_pages < terrain_index::split_page_budget &&
                group.size() > terrain_index::instance_pages::page_size ) {
                std::map<tripoint_abs_ms, instance_group> blocks;
                for( const auto &item : group ) {
                    blocks[draw_block( item )].push_back( item );
                }
                for( const auto &[block, items] : blocks ) {
                    const auto page = split_pages < terrain_index::split_page_budget ?
                                      cache->resident_pages.write( { block, model }, items, encode_resident ) :
                                      std::nullopt;
                    if( page ) {
                        batch part = draw;
                        part.first_instance = *page;
                        part.instance_count = static_cast<Uint32>( items.size() );
                        part.paged_instances = true;
                        emit_batch( part );
                        ++split_pages;
                    } else {
                        std::transform( items.begin(), items.end(), std::back_inserter( instance_stream ),
                                        encode_resident );
                    }
                }
                draw.instance_count = static_cast<Uint32>( instance_stream.size() ) - draw.first_instance;
                if( draw.instance_count ) {
                    emit_batch( draw );
                }
                continue;
            }
            const bool can_page = terrain_model && !model.open_structure && group.size() <= terrain_index::instance_pages::page_size;
            if( can_page ) {
                const auto block = draw_block( group.front() );
                if( std::all_of( group.begin() + 1, group.end(), [&]( const auto &item ) {
                return draw_block( item ) == block;
                } ) ) {
                    if( const auto page = cache->resident_pages.write( { block, model }, group, encode_resident ) ) {
                        draw.first_instance = *page;
                        draw.paged_instances = true;
                    }
                }
            }
        }
        emit_batch( draw );
        if( !draw.paged_instances ) {
            if( draw.resident ) {
                std::transform( group.begin(), group.end(), std::back_inserter( instance_stream ),
                                encode_resident );
            } else {
                instance_stream.insert( instance_stream.end(), group.begin(), group.end() );
            }
        }
    }
    if( has_resident ) {
        result.resident_geometry = cache->resident_models.geometry();
        if( !cache->resident_pages.data().empty() ) {
            result.resident_pages = cache->resident_pages.snapshot();
            result.resident_page_revision = cache->resident_pages.snapshot_revision();
            const auto prefix = static_cast<Uint32>( result.resident_pages->size() );
            const auto add_prefix = [prefix]( batch &draw ) {
                if( draw.resident && !draw.paged_instances ) {
                    draw.first_instance += prefix;
                }
            };
            for( auto &draw : result.batches ) {
                add_prefix( draw );
            }
            for( auto &draw : result.reflective_batches ) {
                add_prefix( draw.draws );
            }
            for( auto &draw : result.open_structure_shadows ) {
                add_prefix( draw );
            }
            for( auto &part : sorted_blends ) {
                add_prefix( part.draw );
            }
        }
    }
    timing.stage( "gpu_prepare_dynamic_geometry" );
    std::vector<fp3d_geometry::draw_range> vehicle_ranges;
    result.vehicle_geometry = fp3d_vehicle::append_gpu_vehicle_geometry( scene, pose, result.camera,
                              width, height, result.geometry, vehicle_ranges,
                              pickup && pickup->type == pickup_kind::vehicle ? &pickup->tile : nullptr );
    std::map<float, Uint32> vehicle_materials;
    for( const auto &range : vehicle_ranges ) {
        const float mode = range.highlighted ? fp3d_gpu::white_model_material :
                           range.textured ? -range.alpha_cutoff.value_or( .5f ) :
                           range.unlit ? 2.f : 1.f;
        const auto [instance, inserted] = vehicle_materials.emplace( mode, result.instances.size() );
        if( inserted ) {
            fp3d_gpu::mesh_instance material;
            material.offset[3] = mode;
            result.instances.push_back( material );
        }
        result.batches.push_back( { range.first_index, range.index_count, instance->second, 1 } );
        result.batches.back().shadow_caster = iris_caster::entity;
    }
    // Keep the snapshot's order for coincident depth-tested cutouts.
    const auto &sprite_view = world_view;
    const std::array<float, 3> right{ sprite_view.right_x(), 0.f, sprite_view.right_z() };
    const std::array<float, 3> up = overhead ?
        std::array<float, 3>{ sprite_view.up_x(), sprite_view.up_y(), sprite_view.up_z() } :
        std::array<float, 3>{ 0.f, 1.f, 0.f };
    const size_t first_sprite_batch = result.batches.size();
    std::map<std::tuple<bool, bool, float>, Uint32> sprite_meshes;
    const auto sprite_mesh = [&]( bool bedding, float height, bool badge = false ) {
        const float half = height * .5f;
        const auto [mesh, inserted] = sprite_meshes.emplace(
                                          std::make_tuple( bedding, badge, bedding ? 0.f : height ),
                                          result.geometry.indices.size() );
        if( inserted ) {
            using namespace fp3d_geometry;
            std::vector<draw_range> ranges;
            primitive_mesh_sink sprite( result.geometry, ranges );
            sprite.begin( primitive::quads );
            sprite.normal( 0, 1, 0 );
            if( bedding ) {
                const size_t first = result.geometry.vertices.size();
                floor_face( sprite, 0, 0, bedding_lift, *unit_uv, .5f );
                for( size_t i = first; i < result.geometry.vertices.size(); ++i ) {
                    auto &vertex = result.geometry.vertices[i];
                    vertex.position[0] *= 2.f * bedding_half_width;
                    vertex.position[2] *= 2.f * bedding_half_depth;
                }
            } else {
                const auto corner = [&]( float u, float v, float side, float rise ) {
                    const auto p = billboard_position( {}, right, up, side, rise );
                    sprite.texcoord( u, v );
                    sprite.point( p[0], p[1], p[2] );
                };
                const float top = badge ? height : height - billboard_lift;
                corner( 0, 1, -half, 0 );
                corner( 1, 1, half, 0 );
                corner( 1, 0, half, top );
                corner( 0, 0, -half, top );
            }
            sprite.end();
        }

        return mesh->second;
    };
    using sprite_draw = std::pair<Uint32, fp3d_gpu::mesh_instance>;
    std::vector<sprite_draw> badges;
    std::vector<sprite_draw> overlays;
    const auto append_sprite = [&]( Uint32 index, const fp3d_gpu::mesh_instance &instance,
                                    size_t first_batch, iris_caster caster = iris_caster::unclassified,
                                    std::optional<int> model_id = {}, int item_id = -1 ) {
        const auto first_instance = static_cast<Uint32>( result.instances.size() );
        result.instances.push_back( instance );
        if( result.batches.size() > first_batch && result.batches.back().first_index == index &&
            result.batches.back().first_instance + result.batches.back().instance_count == first_instance &&
            result.batches.back().shadow_caster == caster &&
            result.batches.back().iris_model_id == model_id && result.batches.back().iris_item_id == item_id ) {
            ++result.batches.back().instance_count;
        } else {
            result.batches.push_back( { index, 6, first_instance, 1 } );
            auto &draw = result.batches.back();
            draw.shadow_caster = caster;
            draw.iris_model_id = model_id;
            draw.iris_item_id = item_id;
        }
    };
    std::unordered_set<tripoint_abs_ms> storage_items;
    const auto draw_world_sprite = [&]( const fp3d_snapshot::sprite &image, bool highlighted ) {
        using sprite_kind = fp3d_snapshot::sprite_kind;
        if( image.kind == sprite_kind::field || image.kind == sprite_kind::held_item ||
            ( image.is_avatar && !overhead ) || image.height <= 0.f ) {
            return;
        }
        const auto rel = image.position - scene.origin;
        const auto *support = known_cell( image.position );
        const bool item = image.kind == sprite_kind::item;
        const bool fade = overhead && support && support->open_structure_above &&
                          open_structure_opacity < 1.f;
        if( item && support && support->furniture &&
            fp3d_furniture::shows_contents( support->furniture->shape ) ) {
            if( support->known != fp3d_snapshot::knowledge::visible || support->exterior_only ||
                !storage_items.insert( image.position ).second ) {
                return;
            }
        }
        const float base = rel.z() * fp3d_camera::floor_height + image.elevation;
        // The drawn height comes from the 3D actor-size setting. The quad is
        // anchored at the feet and centred on the tile, so shrinking keeps
        // ground contact and centre alignment; picking still uses the native
        // `image.height` so reach and hit rules never follow a display option.
        const float height = image.height * image.display_scale;
        const float half = height * .5f;
        if( !sprite_view.contains( rel.x() + up[0] * half - pose.eye.x,
                                   base + up[1] * half - pose.eye.height,
                                   rel.y() + up[2] * half - pose.eye.y, height * .74f ) ) {
            return;
        }
        const bool bedding = !overhead && item && support && support->furniture &&
                             ( support->furniture->shape == furniture_kind::bed ||
                               support->furniture->shape == furniture_kind::bed_frame );
        const Uint32 mesh = sprite_mesh( bedding, height );
        // The native idle animation of this actor, resolved to the frame the 2D
        // tileset would show at this instant. Every frame is already an atlas
        // slot, so switching costs one table lookup and no texture upload.
        const unsigned animation_phase =
            static_cast<unsigned>( std::hash<tripoint_abs_ms>{}( image.position ) );
        auto uv = fp3d_materials::sprite_frame_uv( image, now, animation_phase );
        const float flash = image.kind == sprite_kind::monster ?
                            fp3d_compositor::hit_flash( image.position ) : 0.f;
        fp3d_gpu::mesh_instance sprite;
        sprite.offset = { static_cast<float>( rel.x() ), base, static_cast<float>( rel.y() ), 1.f };
        sprite.tint = { .8f, .25f, .7f, 1.f };
        if( uv ) {
            if( image.mirrored ) {
                std::swap( uv->left, uv->right );
            }
            sprite.uv_rect = { uv->left, uv->top, uv->right, uv->bottom };
            sprite.offset[3] = flash > 0.f ? fp3d_gpu::unlit_cutout( .5f ) : 0.f;
            const float flash_tint = .9f - .72f * flash;
            sprite.tint = { flash > 0.f ? 1.f : .9f, flash_tint, flash_tint, 1.f };
        }
        if( highlighted ) {
            sprite.offset[3] = uv ? fp3d_gpu::white_sprite_material : fp3d_gpu::white_model_material;
        }
        const auto append_faded_sprite = [&]( Uint32 index, fp3d_gpu::mesh_instance instance ) {
            instance.tint[3] *= open_structure_opacity;
            const auto first = static_cast<Uint32>( result.instances.size() );
            result.instances.push_back( instance );
            sorted_blends.push_back( { blend_depth( instance, 0.f ), { index, 6, first, 1 } } );
        };
        if( image.is_avatar ) {
            overlays.emplace_back( mesh, sprite );
        } else if( fade ) {
            sprite.offset[3] = uv ? fp3d_gpu::faded_texture_material : fp3d_gpu::faded_solid_material;
            append_faded_sprite( mesh, sprite );
        } else {
            std::optional<int> model_id;
            int item_id = -1;
            if( iris_entities && uv && !highlighted ) {
                fp3d_iris::virtual_material material;
                if( image.kind == sprite_kind::item ) {
                    material.shader_id = "minecraft:item";
                    model_id = iris_entities->resolve( material ).value_or( -1 );
                    material.shader_id.clear();
                    material.native_id = "cdda:" + image.id;
                    if( iris_items ) {
                        item_id = iris_items->resolve( material ).value_or( -1 );
                    }
                } else {
                    material.native_id = "cdda:" + image.id;
                    model_id = iris_entities->resolve( material ).value_or( -1 );
                }
            }
            append_sprite( mesh, sprite, first_sprite_batch, iris_caster::entity, model_id, item_id );
        }
        ++result.sprite_count;
        if( bedding ) {
            return; // The legacy flat bedding path returns before drawing badges.
        }

        unsigned displayed = 0;
        const auto add_badge = [&]( const std::string &id ) {
            if( displayed >= 4 ) {
                return;
            }
            const auto icon = fp3d_materials::lookup_gpu( "$status:" + id );
            if( !icon ) {
                return;
            }
            constexpr float size = .30f;
            const float side = height * .33f + displayed * ( size + .02f );
            const float rise = height + .16f - size * .5f - fp3d_geometry::billboard_lift;
            fp3d_gpu::mesh_instance badge;
            badge.offset = { rel.x() + right[0] * side + up[0] * rise, base + up[1] * rise,
                             rel.y() + right[2] * side + up[2] * rise, fp3d_gpu::unlit_cutout( .5f ) };
            badge.uv_rect = { icon->left, icon->top, icon->right, icon->bottom };
            const auto index = sprite_mesh( false, size, true );
            if( fade ) {
                badge.offset[3] = -2.f;
                append_faded_sprite( index, badge );
            } else {
                ( image.is_avatar ? overlays : badges ).emplace_back( index, badge );
            }
            ++displayed;
        };
        if( image.revivable ) { add_badge( "zombie_revival_indicator" ); }
        if( image.alerted ) { add_badge( "overlay_hostile_sees_player" ); }
        if( image.bleeding ) { add_badge( "overlay_effect_bleed" ); }
        for( const auto &id : image.status_icons ) {
            add_badge( id );
        }
    };
    for( const auto &image : scene.sprites ) {
        if( const auto *support = terrain.cells.get( image.position );
            overhead && support && fp3d_snapshot::hidden_by_cutaway( scene, *support ) ) { continue; }
        if( pickup_item && image.kind == fp3d_snapshot::sprite_kind::item &&
            image.position == pickup_item->position ) {
            pickup_item->height = image.height;
            pickup_item->display_scale = image.display_scale;
            pickup_item->elevation = image.elevation;
            continue;
        }
        draw_world_sprite( image, false );
    }
    if( pickup_item ) {
        draw_world_sprite( *pickup_item, true );
    }
    result.opaque_batches = static_cast<Uint32>( result.batches.size() );
    if( include_landscape && !scene.unknown.empty() ) {
        using namespace fp3d_geometry;
        std::vector<draw_range> ranges;
        primitive_mesh_sink unknown( result.geometry, ranges );
        const Uint32 first = result.geometry.indices.size();
        unknown.begin( primitive::quads );
        for( const auto &span : scene.unknown ) {
            const auto offset = span.position - scene.origin;
            const float x = offset.x() + ( span.width - 1 ) * .5f;
            const float top = offset.z() * fp3d_camera::floor_height;
            const float bottom = top - fp3d_camera::floor_height;
            const float z = offset.y();
            const float radius = std::hypot( span.width * .5f, fp3d_camera::floor_height * .5f, .5f );
            if( !world_view.contains( x - pose.eye.x, ( top + bottom ) * .5f - pose.eye.height,
                                     z - pose.eye.y, radius ) ) {
                continue;
            }
            box_faces( unknown, x, z, span.width, 1, bottom, top, unit_uv, false,
                       static_cast<const fp3d_materials::furniture_uv *>( nullptr ), true );
            unknown.point( x - span.width * .5f, bottom, z - .5f );
            unknown.point( x + span.width * .5f, bottom, z - .5f );
            unknown.point( x + span.width * .5f, bottom, z + .5f );
            unknown.point( x - span.width * .5f, bottom, z + .5f );
        }
        unknown.end();
        const Uint32 count = static_cast<Uint32>( result.geometry.indices.size() ) - first;
        if( count ) {
            fp3d_gpu::mesh_instance material;
            const float shade = .035f + .20f * daylight;
            material.offset[3] = fp3d_gpu::unknown_background_material;
            material.tint = { shade, shade, shade, 1.f };
            result.unknown_background = batch{ first, count, static_cast<Uint32>( result.instances.size() ), 1 };
            result.instances.push_back( material );
        }
    }
    for( const auto &[index, instance] : badges ) {
        append_sprite( index, instance, result.opaque_batches );
    }
    result.badge_end = static_cast<Uint32>( result.batches.size() );
    result.badge_count = badges.size();
    result.pane_count = panes.size();
    result.ground_layer_count = ground_layers.size();
    if( !ground_layers.empty() ) {
        const auto first_index = static_cast<Uint32>( result.geometry.indices.size() );
        using namespace fp3d_geometry;
        std::vector<draw_range> ranges;
        primitive_mesh_sink decal_sink( result.geometry, ranges );
        decal_sink.begin( primitive::quads );
        floor_face( decal_sink, 0, 0, 0, *unit_uv, .5f );
        decal_sink.end();
        result.batches.push_back( { first_index, 6, static_cast<Uint32>( result.instances.size() ),
                                    static_cast<Uint32>( ground_layers.size() ) } );
        result.instances.insert( result.instances.end(), ground_layers.begin(), ground_layers.end() );
    }
    struct field_animation {
        std::vector<fp3d_materials::field_frame> frames;
        unsigned ticks = 0;
    };
    struct field_draw {
        float distance_squared;
        float height;
        fp3d_gpu::mesh_instance instance;
    };
    std::unordered_map<std::string, field_animation> field_animations;
    std::vector<field_draw> fields;
    for( const auto &image : scene.sprites ) {
        if( const auto *support = terrain.cells.get( image.position );
            overhead && support && fp3d_snapshot::hidden_by_cutaway( scene, *support ) ) { continue; }
        if( image.kind != fp3d_snapshot::sprite_kind::field || image.height <= 0.f ) {
            continue;
        }
        const auto rel = image.position - scene.origin;
        const auto *support = known_cell( image.position );
        const float ramp_height = support && support->is_ramp ?
                                  fp3d_ramp::plane( *support, known_cell ).center : 0.f;
        const float base = rel.z() * fp3d_camera::floor_height + image.elevation + ramp_height;
        const float half = image.height * .5f;
        if( !sprite_view.contains( rel.x() + up[0] * half - pose.eye.x,
                                   base + up[1] * half - pose.eye.height,
                                   rel.y() + up[2] * half - pose.eye.y, image.height * .74f ) ) {
            continue;
        }
        auto [entry, inserted] = field_animations.try_emplace( fp3d_materials::sprite_key( image ) );
        auto &animation = entry->second;
        if( inserted ) {
            animation.frames = fp3d_materials::field_animation_gpu( image );
            for( const auto &frame : animation.frames ) {
                animation.ticks += static_cast<unsigned>( std::max( 0, frame.ticks ) );
            }
        }
        if( !animation.ticks ) {
            continue;
        }
        const auto phase = std::hash<tripoint_abs_ms>{}( image.position );
        auto tick = ( now / 17 + phase ) % animation.ticks;
        std::optional<fp3d_materials::uv_rect> uv;
        for( const auto &frame : animation.frames ) {
            const auto duration = static_cast<unsigned>( std::max( 0, frame.ticks ) );
            if( tick < duration ) {
                uv = frame.uv;
                break;
            }
            tick -= duration;
        }
        if( !uv ) {
            continue;
        }
        if( image.mirrored ) {
            std::swap( uv->left, uv->right );
        }
        fp3d_gpu::mesh_instance instance;
        instance.offset = { static_cast<float>( rel.x() ), base, static_cast<float>( rel.y() ), -2.f };
        instance.uv_rect = { uv->left, uv->top, uv->right, uv->bottom };
        instance.tint[3] = image.id == "fd_smoke" ? .40f + .15f * image.field_intensity : 1.f;
        if( overhead && support && support->open_structure_above && open_structure_opacity < 1.f ) {
            instance.tint[3] *= open_structure_opacity;
            fading_open_panes = true;
        }
        const float dx = rel.x() - pose.eye.x;
        const float dy = base + half - pose.eye.height;
        const float dz = rel.y() - pose.eye.y;
        fields.push_back( { dx * dx + dy * dy + dz * dz, image.height, instance } );
    }
    std::stable_sort( fields.begin(), fields.end(), []( const field_draw &a, const field_draw &b ) {
        return a.distance_squared > b.distance_squared;
    } );
    const bool sort_blends = fading_open_panes || !sorted_blends.empty();
    const size_t first_field_batch = result.batches.size();
    for( const auto &field : fields ) {
        const auto index = sprite_mesh( false, field.height );
        if( sort_blends ) {
            const auto instance = static_cast<Uint32>( result.instances.size() );
            result.instances.push_back( field.instance );
            sorted_blends.push_back( { blend_depth( field.instance, field.height * .5f ),
                                       { index, 6, instance, 1 } } );
        } else {
            append_sprite( index, field.instance, first_field_batch );
        }
    }
    result.animated_field_count = fields.size();
    if( sort_blends ) {
        for( const auto &draw : translucent_models ) {
            for( Uint32 n = 0; n < draw.instance_count; ++n ) {
                const auto instance = draw.first_instance + n;
                const float depth = blend_depth( result.instances[instance], fp3d_camera::floor_height * .5f );
                auto part = draw;
                part.first_instance = instance;
                part.instance_count = 1;
                sorted_blends.push_back( { depth, part } );
            }
        }
    } else {
        result.batches.insert( result.batches.end(), translucent_models.begin(), translucent_models.end() );
    }

    if( include_landscape && !scene.weather.columns.empty() &&
        scene.weather.particles != fp3d_weather::precipitation::none ) {
        const bool rain = scene.weather.particles == fp3d_weather::precipitation::rain;
        const bool anomaly = scene.weather.particles == fp3d_weather::precipitation::anomaly;
        const float light = .3f + .65f * daylight;
        const Uint32 first = static_cast<Uint32>( result.geometry.indices.size() );
        std::vector<fp3d_geometry::draw_range> ranges;
        fp3d_geometry::primitive_mesh_sink weather_sink( result.geometry, ranges );
        weather_sink.begin( fp3d_geometry::primitive::quads );
        fp3d_weather::visit_particles( scene.weather, scene.origin, now,
        [&]( const fp3d_weather::particle &p ) {
            if( !sprite_view.contains( p.x - pose.eye.x, p.y - pose.eye.height,
                                       p.z - pose.eye.y, .6f ) ) {
                return;
            }
            const float depth = result.camera.transform( p.x, p.y, p.z )[3];
            const float size = rain ? std::max( .001f, depth / ( width * std::hypot(
                                                    camera->elements[0], camera->elements[2] ) ) ) :
                               anomaly ? .04f : .025f;
            const float dx = right[0] * size;
            const float dz = right[2] * size;
            const float end_x = rain ? p.end_x : p.x;
            const float end_y = rain ? p.end_y : p.y - size;
            const float end_z = rain ? p.end_z : p.z;
            const float top = p.y + ( rain ? 0.f : size );
            weather_sink.point( p.x - dx, top, p.z - dz );
            weather_sink.point( end_x - dx, end_y, end_z - dz );
            weather_sink.point( end_x + dx, end_y, end_z + dz );
            weather_sink.point( p.x + dx, top, p.z + dz );
        } );
        weather_sink.end();
        const Uint32 indices = static_cast<Uint32>( result.geometry.indices.size() ) - first;
        if( indices ) {
            fp3d_gpu::mesh_instance material;
            material.offset[3] = 2.f;
            material.tint = rain ? std::array<float, 4>{ light * .68f, light * .83f, light, .48f } :
                            anomaly ? std::array<float, 4>{ .65f, .24f, .93f, .65f } :
                            std::array<float, 4>{ light, light, light, .8f };
            result.batches.push_back( { first, indices, static_cast<Uint32>( result.instances.size() ), 1 } );
            result.instances.push_back( material );
        }
    }

    std::vector<fp3d_geometry::draw_range> vehicle_glass_ranges;
    result.vehicle_pane_count = fp3d_vehicle::append_gpu_vehicle_glass( scene, sprite_view, pose.eye,
                                result.geometry, vehicle_glass_ranges );
    std::map<bool, Uint32> glass_materials;
    for( const auto &range : vehicle_glass_ranges ) {
        const auto [instance, inserted] = glass_materials.emplace( range.textured, result.instances.size() );
        if( inserted ) {
            fp3d_gpu::mesh_instance material;
            material.offset[3] = range.textured ? -2.f : 2.f;
            result.instances.push_back( material );
        }
        if( sort_blends ) {
            // Vehicle glazing is emitted as six indices per quad in world
            // coordinates, independent of the shared material instance.
            for( Uint32 i = range.first_index; i < range.first_index + range.index_count; i += 6 ) {
                const auto &a = result.geometry.vertices[result.geometry.indices[i]].position;
                const auto &b = result.geometry.vertices[result.geometry.indices[i + 2]].position;
                const float depth = result.camera.transform( ( a[0] + b[0] ) * .5f,
                                    ( a[1] + b[1] ) * .5f, ( a[2] + b[2] ) * .5f )[3];
                sorted_blends.push_back( { depth, { i, 6, instance->second, 1 } } );
                sorted_blends.back().draw.shadow_caster = iris_caster::entity;
            }
        } else {
            result.batches.push_back( { range.first_index, range.index_count, instance->second, 1 } );
            result.batches.back().shadow_caster = iris_caster::entity;
        }
    }

    std::stable_sort( panes.begin(), panes.end(), []( const sorted_pane &a, const sorted_pane &b ) {
        const bool a_skylight = a.type == pane_shape::skylight;
        const bool b_skylight = b.type == pane_shape::skylight;
        return a_skylight != b_skylight ? !a_skylight : a.distance_squared > b.distance_squared;
    } );
    std::map<pane_shape, Uint32> pane_indices;
    for( const auto &pane : panes ) {
        const auto [index, inserted] = pane_indices.emplace( pane.type, result.geometry.indices.size() );
        if( inserted ) {
            const size_t first_vertex = result.geometry.vertices.size();
            using namespace fp3d_geometry;
            std::vector<draw_range> ranges;
            primitive_mesh_sink pane_sink( result.geometry, ranges );
            if( pane.type == pane_shape::skylight ) {
                pane_sink.begin( primitive::quads );
                floor_face( pane_sink, 0, 0, 0, *unit_uv, .5f );
                pane_sink.end();
            } else {
                const bool window = pane.type == pane_shape::window || pane.type == pane_shape::turned_window;
                const bool turned = pane.type == pane_shape::turned_window ||
                                    pane.type == pane_shape::turned_glass ||
                                    pane.type == pane_shape::turned_open_glass_in ||
                                    pane.type == pane_shape::turned_open_glass_out;
                const bool swing_in = pane.type == pane_shape::open_glass_in ||
                                      pane.type == pane_shape::turned_open_glass_in;
                const bool swing_out = pane.type == pane_shape::open_glass_out ||
                                       pane.type == pane_shape::turned_open_glass_out;
                window_face( pane_sink, unit_uv, window ? .42f : .44f, window ? .9f : .06f,
                             window ? 2.1f : fp3d_camera::floor_height - .08f );
                for( size_t i = first_vertex; i < result.geometry.vertices.size(); ++i ) {
                    auto &vertex = result.geometry.vertices[i];
                    if( swing_in || swing_out ) {
                        const float sign = swing_in ? 1.f : -1.f;
                        vertex.position = { -.45f, vertex.position[1],
                                            sign * ( .49f + vertex.position[0] * .88f ) };
                        vertex.normal = { -sign, 0.f, 0.f };
                    }
                    if( turned ) {
                        vertex.position = { vertex.position[2], vertex.position[1], -vertex.position[0] };
                        vertex.normal = { vertex.normal[2], vertex.normal[1], -vertex.normal[0] };
                    }
                }
            }
        }
        const auto instance = static_cast<Uint32>( result.instances.size() );
        result.instances.push_back( pane.instance );
        if( sort_blends ) {
            sorted_blends.push_back( { blend_depth( pane.instance,
                pane.type == pane_shape::skylight ? 0.f : 1.5f ), { index->second, 6, instance, 1 } } );
        } else if( result.batches.size() > result.badge_end && !result.batches.back().resident &&
                   result.batches.back().first_index == index->second ) {
            ++result.batches.back().instance_count;
        } else {
            result.batches.push_back( { index->second, 6, instance, 1 } );
        }
    }
    std::stable_sort( sorted_blends.begin(), sorted_blends.end(),
    []( const sorted_blend &a, const sorted_blend &b ) {
        return a.depth > b.depth;
    } );
    for( const auto &part : sorted_blends ) {
        result.batches.push_back( part.draw );
    }
    result.batches.insert( result.batches.end(), blueprint_models.begin(), blueprint_models.end() );
    if( ( blueprint && !prepared_blueprint ) || target_outline ) {
        using namespace fp3d_geometry;
        std::vector<draw_range> ranges;
        primitive_mesh_sink cue( result.geometry, ranges );
        const auto append_cue = [&]( Uint32 first, const std::array<float, 4> &tint ) {
            const auto count = static_cast<Uint32>( result.geometry.indices.size() ) - first;
            if( count ) {
                fp3d_gpu::mesh_instance instance;
                instance.offset[3] = 2.f;
                instance.tint = tint;
                result.batches.push_back( { first, count, static_cast<Uint32>( result.instances.size() ), 1 } );
                result.instances.push_back( instance );
            }
        };
        if( blueprint && !prepared_blueprint ) {
            const Uint32 first = result.geometry.indices.size();
            const auto position = blueprint->position - scene.origin;
            const float x = position.x();
            const float y = position.z() * fp3d_camera::floor_height;
            const float z = position.y();
            const auto box = [&]( float px, float pz, float w, float d, float bottom, float top ) {
                box_faces( cue, px, pz, w, d, bottom, top, unit_uv, false,
                           static_cast<const fp3d_materials::furniture_uv *>( nullptr ), true );
            };
            cue.begin( primitive::quads );
            if( blueprint->vertical_hint ) {
                for( float side : { -.45f, .45f } ) {
                    box( x + side, z, .04f, .94f, y + .03f, y + .06f );
                    box( x, z + side, .94f, .04f, y + .03f, y + .06f );
                }
                const bool down = blueprint->vertical_hint < 0;
                box( x, z, .09f, .09f, y + ( down ? .5f : .18f ), y + ( down ? 1.15f : .83f ) );
                cue.end();
                cue.begin( primitive::triangles );
                const float tip = y + ( down ? .18f : 1.15f );
                const float base = y + ( down ? .5f : .83f );
                const std::array<std::array<float, 2>, 4> corners{{
                    { -.24f, -.24f }, { .24f, -.24f }, { .24f, .24f }, { -.24f, .24f }
                }};
                for( size_t i = 0; i < corners.size(); ++i ) {
                    cue.point( x, tip, z );
                    cue.point( x + corners[i][0], base, z + corners[i][1] );
                    const auto &next = corners[( i + 1 ) % corners.size()];
                    cue.point( x + next[0], base, z + next[1] );
                }
            } else if( blueprint->terrain && blueprint->terrain->is_ramp ) {
                auto plane = fp3d_ramp::plane( *blueprint->terrain, known_cell );
                plane.center += y + .025f;
                ramp_face( cue, x, z, *unit_uv, plane );
            } else {
                box( x, z, .94f, .94f, y + .025f, y + blueprint->height );
            }
            cue.end();
            append_cue( first, { blueprint->valid ? .15f : .95f, blueprint->valid ? .9f : .15f,
                                 .3f, .35f } );
        }
        if( target_outline ) {
            const Uint32 first = result.geometry.indices.size();
            const auto segment = [&]( std::array<float, 3> a, std::array<float, 3> b ) {
                constexpr float shift = .006f;
                for( auto *p : { &a, &b } ) {
                    const float x = ( *p )[0] - pose.eye.x;
                    const float y = ( *p )[1] - pose.eye.height;
                    const float z = ( *p )[2] - pose.eye.y;
                    const float scale = shift / std::max( .05f, std::hypot( x, y, z ) );
                    ( *p )[0] -= x * scale;
                    ( *p )[1] -= y * scale;
                    ( *p )[2] -= z * scale;
                }
                const auto ca = camera->transform( a[0], a[1], a[2] );
                const auto cb = camera->transform( b[0], b[1], b[2] );
                if( ca[3] < view.near_clip || cb[3] < view.near_clip ) { return; }
                const float dx = ( cb[0] / cb[3] - ca[0] / ca[3] ) * width;
                const float dy = ( cb[1] / cb[3] - ca[1] / ca[3] ) * height;
                const float len = std::hypot( dx, dy );
                if( len <= .0001f ) { return; }
                const float scale = 2.f * fp3d_camera::horizontal_tangent() / ( width * len );
                const std::array<float, 3> side{
                    ( -dy * sprite_view.right_x() + dx * sprite_view.up_x() ) * scale,
                    dx * sprite_view.up_y() * scale,
                    ( -dy * sprite_view.right_z() + dx * sprite_view.up_z() ) * scale
                };
                const auto emit = [&]( const std::array<float, 3> &p, float depth ) {
                    cue.point( p[0] + side[0] * depth, p[1] + side[1] * depth, p[2] + side[2] * depth );
                };
                emit( a, ca[3] );
                emit( b, cb[3] );
                emit( b, -cb[3] );
                emit( a, -ca[3] );
            };
            cue.begin( primitive::quads );
            for( const auto &edge : target_outline->segments ) { segment( edge[0], edge[1] ); }
            for( size_t i = 0; target_outline->segments.empty() && i < target_outline->corners.size(); ++i ) {
                const auto &a = target_outline->corners[i];
                const auto &b = target_outline->corners[( i + 1 ) % target_outline->corners.size()];
                segment( a, b );
                if( target_outline->height > 0 ) {
                    auto top_a = a;
                    auto top_b = b;
                    top_a[1] += target_outline->height;
                    top_b[1] += target_outline->height;
                    segment( top_a, top_b );
                    segment( a, top_a );
                }
            }
            cue.end();
            append_cue( first, target_outline->color );
        }
    }
    result.blend_end = static_cast<Uint32>( result.batches.size() );
    for( const auto &[index, instance] : overlays ) {
        append_sprite( index, instance, result.blend_end );
    }
    if( result.instances.empty() && result.resident_instance_count == 0 ) {
        return std::nullopt;
    }
    return result;
}

using scene_pipelines = std::array<fp3d_gpu::textured_pipeline,
      static_cast<size_t>( fp3d_gpu::mesh_pass::count )>;

inline bool initialize_scene_pipelines( scene_pipelines &pipelines, SDL_GPUDevice *device,
                                        SDL_GPUShader *vs, SDL_GPUShader *fs,
                                        SDL_GPUTextureFormat format = fp3d_gpu::scene_target::color_format() )
{
    for( size_t i = 0; i < pipelines.size(); ++i ) {
        if( !pipelines[i].initialize( device, vs, fs, static_cast<fp3d_gpu::mesh_pass>( i ), format ) ) {
            return false;
        }
    }
    return true;
}

struct resources {
    SDL_GPUDevice *device;
    SDL_GPUShader *vs = nullptr, *fs = nullptr;
    SDL_GPUTransferBuffer *readback = nullptr;
    SDL_GPUFence *fence = nullptr;
    std::unique_ptr<fp3d_gpu::scene_target> target = std::make_unique<fp3d_gpu::scene_target>();
    fp3d_gpu::indexed_mesh mesh;
    fp3d_gpu::upload_buffer instances;
    fp3d_gpu::upload_buffer draw_commands;
    scene_pipelines pipelines;
    explicit resources( SDL_GPUDevice *d ) : device( d ) {}
    ~resources() {
        // Isolated one-shot QA only. Real frame slots must retire by fence.
        SDL_WaitForGPUIdle( device );
        for( auto &pipeline : pipelines ) {
            pipeline.reset();
        }
        mesh.reset();
        instances.reset();
        draw_commands.reset();
        target.reset();
        if( readback ) SDL_ReleaseGPUTransferBuffer( device, readback );
        if( fence ) SDL_ReleaseGPUFence( device, fence );
        if( vs ) SDL_ReleaseGPUShader( device, vs );
        if( fs ) SDL_ReleaseGPUShader( device, fs );
    }
};
inline SDL_GPUShader *shader( SDL_GPUDevice *device, const unsigned char *code,
                             std::size_t size, bool vertex, Uint32 uniforms, Uint32 samplers )
{
    SDL_GPUShaderCreateInfo info{};
    info.code = code;
    info.code_size = size;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXBC;
    info.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_uniform_buffers = uniforms;
    info.num_samplers = samplers;
    return SDL_CreateGPUShader( device, &info );
}

inline bool stage_instances( SDL_GPUDevice *device,
                             const std::vector<fp3d_gpu::mesh_instance> &stream,
                             fp3d_gpu::upload_buffer &buffer, bool retain )
{
    if( stream.empty() ) {
        return true;
    }
    if( stream.size() > SDL_MAX_UINT32 / sizeof( fp3d_gpu::mesh_instance ) ) {
        return SDL_SetError( "Invalid FP3D terrain instance count" );
    }
    const auto bytes = static_cast<Uint32>( stream.size() * sizeof( fp3d_gpu::mesh_instance ) );
    return buffer.reserve( device, bytes, SDL_GPU_BUFFERUSAGE_VERTEX ) &&
           ( retain ? buffer.stage_delta( stream.data(), bytes ) : buffer.stage( stream.data(), bytes ) );
}

inline bool stage_resident_instances( SDL_GPUDevice *device, const prepared_world &world,
                                      fp3d_gpu::upload_buffer &buffer )
{
    if( world.resident_instance_count == 0 ) {
        return true;
    }
    constexpr Uint32 instance_bytes = sizeof( fp3d_gpu::mesh_instance );
    constexpr Uint32 page_bytes = terrain_index::instance_pages::page_size * instance_bytes;
    const auto *pages = world.resident_pages.get();
    const auto head = static_cast<Uint32>( pages ? pages->size() * instance_bytes : 0 );
    const auto tail = static_cast<Uint32>( world.resident_instances.size() * instance_bytes );
    return buffer.reserve( device, head + tail, SDL_GPU_BUFFERUSAGE_VERTEX,
                           resident_instance_buffer_limit ) &&
           buffer.stage_delta_parts( pages ? pages->data() : nullptr,
                                     { head, page_bytes, world.resident_page_revision },
                                     world.resident_instances.data(), tail );
}

inline bool stage_draws( SDL_GPUDevice *device, const prepared_world &world,
                         fp3d_gpu::upload_buffer &draw_commands,
                         std::vector<SDL_GPUIndexedIndirectDrawCommand> *retained_commands = nullptr,
                         bool occlusion_input = false )
{
    if( world.batches.empty() ||
        world.batches.size() > SDL_MAX_UINT32 / sizeof( SDL_GPUIndexedIndirectDrawCommand ) ) {
        return SDL_SetError( "Invalid FP3D draw command count" );
    }
    std::vector<SDL_GPUIndexedIndirectDrawCommand> temporary_commands;
    auto &commands = retained_commands ? *retained_commands : temporary_commands;
    commands.clear();
    commands.reserve( world.batches.size() );
    for( const auto &group : world.batches ) {
        commands.push_back( { group.index_count, group.instance_count, group.first_index,
                              0, group.first_instance } );
    }
    const auto command_bytes = static_cast<Uint32>(
                                   commands.size() * sizeof( SDL_GPUIndexedIndirectDrawCommand ) );
    const auto usage = SDL_GPU_BUFFERUSAGE_INDIRECT |
                       ( occlusion_input ? SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ : 0u );
    if( !draw_commands.reserve( device, command_bytes, usage ) ||
        !( retained_commands ? draw_commands.stage_delta( commands.data(), command_bytes ) :
           draw_commands.stage( commands.data(), command_bytes ) ) ) {
        return false;
    }
    return true;
}

namespace terrain_compute = fp3d_gpu::terrain_compute;

struct terrain_compute_inputs {
    std::vector<terrain_compute::cell> cells;
    std::vector<terrain_compute::model> models;
    std::vector<terrain_compute::block> blocks;
    std::vector<Uint32> draw_indices;
};

inline terrain_compute_inputs prepare_terrain_compute( const prepared_world &world )
{
    terrain_compute_inputs result;
    if( !world.resident_pages || !world.resident_geometry ) {
        return result;
    }
    const auto &pages = *world.resident_pages;
    for( Uint32 i = 0; i < world.opaque_batches && result.blocks.size() < terrain_compute::max_blocks; ++i ) {
        const auto &draw = world.batches[i];
        if( !draw.resident || !draw.paged_instances || !draw.instance_count ||
            draw.instance_count > terrain_compute::cells_per_block || draw.first_instance >= pages.size() ||
            draw.instance_count > pages.size() - draw.first_instance ) {
            continue;
        }
        const auto &sample = pages[draw.first_instance];
        const auto first = pages.begin() + draw.first_instance;
        const auto end = first + draw.instance_count;
        if( !std::all_of( first, end, [&]( const fp3d_gpu::mesh_instance &item ) {
            return item.uv_rect == sample.uv_rect && item.offset[3] == sample.offset[3] &&
                   std::isfinite( item.offset[0] ) && std::isfinite( item.offset[1] ) &&
                   std::isfinite( item.offset[2] );
        } ) ) {
            continue;
        }
        const auto model = terrain_compute::make_model( *world.resident_geometry, draw.first_index,
                           draw.index_count, sample.uv_rect, sample.offset[3] );
        if( !model ) {
            continue;
        }
        const auto first_cell = static_cast<Uint32>( result.cells.size() );
        for( auto instance = first; instance != end; ++instance ) {
            const auto &p = instance->offset;
            result.cells.push_back( { { p[0], p[1], p[2] }, 0, instance->tint } );
        }
        result.blocks.push_back( *terrain_compute::make_block( result.cells, first_cell, draw.instance_count,
                                 *model, static_cast<Uint32>( result.models.size() ), world.material_revision ) );
        result.models.push_back( *model );
        result.draw_indices.push_back( i );
    }
    return result;
}

class terrain_compute_input_cache
{
    public:
        bool reuse_or_rebuild( const prepared_world &world ) {
            const auto end = world.batches.begin() + world.opaque_batches;
            const auto same_candidate = []( const batch &a, const batch &b ) {
                const bool eligible_a = a.resident && a.paged_instances;
                const bool eligible_b = b.resident && b.paged_instances;
                return eligible_a == eligible_b &&
                       ( !eligible_a ||
                         ( a.first_index == b.first_index && a.index_count == b.index_count &&
                           a.first_instance == b.first_instance && a.instance_count == b.instance_count ) );
            };
            if( world.resident_page_revision && world.resident_page_revision == pages_ &&
                world.resident_geometry == geometry_ && world.material_revision == material_revision_ &&
                std::equal( world.batches.begin(), end, opaque_.begin(), opaque_.end(), same_candidate ) ) {
                return true;
            }
            inputs_ = prepare_terrain_compute( world );
            pages_ = world.resident_page_revision;
            geometry_ = world.resident_geometry;
            material_revision_ = world.material_revision;
            opaque_.assign( world.batches.begin(), end );
            return false;
        }

        const terrain_compute_inputs &inputs() const {
            return inputs_;
        }

    private:
        std::shared_ptr<const fp3d_gpu::instance_page_revision> pages_;
        std::shared_ptr<const fp3d_geometry::mesh> geometry_;
        unsigned material_revision_ = 0;
        std::vector<batch> opaque_;
        terrain_compute_inputs inputs_;
};

inline std::array<terrain_compute::vec4, 6> terrain_compute_planes( const prepared_world &world )
{
    auto anchored = world.camera;
    for( size_t row = 0; row < 4; ++row ) {
        const auto start = row * 4;
        for( size_t axis = 0; axis < 3; ++axis ) {
            anchored.elements[start + 3] += world.camera.elements[start + axis] * world.resident_to_scene[axis];
        }
    }
    return terrain_compute::clip_planes( anchored );
}

struct compute_draws {
    SDL_GPUBuffer *instances;
    SDL_GPUBuffer *commands;
    const std::vector<Uint32> &indices;
};

struct world_meshes {
    const fp3d_gpu::indexed_mesh &dynamic;
    const fp3d_gpu::indexed_mesh *resident = nullptr;
    std::array<float, 3> resident_to_scene{};
    SDL_GPUBuffer *dynamic_instances = nullptr;
    SDL_GPUBuffer *resident_instances = nullptr;

    void bind_instances( SDL_GPURenderPass *pass, bool use_resident ) const {
        if( auto *buffer = use_resident ? resident_instances : dynamic_instances ) {
            const SDL_GPUBufferBinding binding{ buffer, 0 };
            SDL_BindGPUVertexBuffers( pass, 1, &binding, 1 );
        }
    }

    void vertex_constants( SDL_GPUCommandBuffer *cmd, const fp3d_gpu_camera_math::matrix4 &matrix,
                           const fp3d_gpu::directional_light &light, bool use_resident ) const {
        fp3d_gpu::vertex_uniforms uniforms{ matrix, light };
        if( use_resident ) {
            std::copy( resident_to_scene.begin(), resident_to_scene.end(),
                       uniforms.light.direct_and_padding.begin() + 1 );
        }
        SDL_PushGPUVertexUniformData( cmd, 0, &uniforms, sizeof( uniforms ) );
    }

    fp3d_gpu::indexed_draw indexed_draw_for( const batch &entry ) const {
        const auto *mesh = entry.resident ? resident : &dynamic;
        auto *instances = entry.resident ? resident_instances : dynamic_instances;
        return { mesh, entry.first_index, entry.index_count, entry.first_instance,
                 entry.instance_count, instances };
    }

    bool draw( SDL_GPURenderPass *pass, const batch &entry ) const {
        return indexed_draw_for( entry ).record( pass );
    }

    bool draw_indirect( SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, SDL_GPUBuffer *commands,
                        const std::vector<batch> &batches, Uint32 first, Uint32 count,
                        const fp3d_gpu_camera_math::matrix4 &matrix,
                        const fp3d_gpu::directional_light &light = {} ) const {
        const Uint32 end = first + count;
        bool anchored = false;
        while( first < end ) {
            const bool use_resident = batches[first].resident;
            Uint32 next = first + 1;
            while( next < end && batches[next].resident == use_resident ) {
                ++next;
            }
            if( anchored != use_resident ) {
                vertex_constants( cmd, matrix, light, use_resident );
                anchored = use_resident;
            }
            bind_instances( pass, use_resident );
            const auto *mesh = use_resident ? resident : &dynamic;
            if( !mesh || !mesh->draw_indirect( pass, commands, next - first,
                                               first * sizeof( SDL_GPUIndexedIndirectDrawCommand ) ) ) {
                return false;
            }
            first = next;
        }
        return true;
    }

    bool draw_opaque( SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, SDL_GPUBuffer *commands,
                      const prepared_world &world, const compute_draws &compute, Uint32 first,
                      Uint32 end ) const {
        size_t i = first == 0 ? 0 : std::lower_bound( compute.indices.begin(), compute.indices.end(),
                   first ) - compute.indices.begin();
        while( i < compute.indices.size() && compute.indices[i] < end ) {
            const Uint32 start = compute.indices[i];
            if( !draw_indirect( cmd, pass, commands, world.batches, first, start - first,
                               world.camera, world.base_light ) ) {
                return false;
            }
            const auto begin = i++;
            first = start + 1;
            while( first < end && i < compute.indices.size() && compute.indices[i] == first ) {
                ++i;
                ++first;
            }
            vertex_constants( cmd, world.camera, world.base_light, true );
            const SDL_GPUBufferBinding binding{ compute.instances, 0 };
            SDL_BindGPUVertexBuffers( pass, 1, &binding, 1 );
            if( !resident || !resident->draw_indirect( pass, compute.commands, static_cast<Uint32>( i - begin ),
                    begin * sizeof( SDL_GPUIndexedIndirectDrawCommand ) ) ) {
                return false;
            }
            vertex_constants( cmd, world.camera, world.base_light, false );
        }
        return draw_indirect( cmd, pass, commands, world.batches, first, end - first,
                              world.camera, world.base_light );
    }
};

struct water_geometry_draw {
    fp3d_gpu::indexed_draw draw;
    std::array<float, 3> anchor_to_scene;
    reflective_surface surface;
    std::optional<std::uint8_t> iris_light;
    std::optional<int> iris_water_material;
};

inline bool is_water( reflective_surface surface )
{
    return surface == reflective_surface::water_shallow || surface == reflective_surface::water_deep;
}

inline std::uint8_t iris_light_at( const std::array<float, 4> &point, const std::array<float, 3> &anchor,
                                   const tripoint_abs_ms &origin,
                                   const fp3d_iris::block_light_levels &block,
                                   const fp3d_iris::block_light_levels &sky )
{
    const auto position = origin + tripoint_rel_ms(
                              static_cast<int>( std::floor( point[0] + anchor[0] ) ),
                              static_cast<int>( std::floor( point[2] + anchor[2] ) ),
                              static_cast<int>( std::lround( ( point[1] + anchor[1] ) / fp3d_camera::floor_height ) ) );
    return static_cast<std::uint8_t>( fp3d_iris::light_level_at( block, position ) |
                                     ( fp3d_iris::light_level_at( sky, position ) << 4 ) );
}

inline void assign_iris_water_lights( prepared_world &world, const tripoint_abs_ms &origin,
                                      const fp3d_iris::block_light_levels &block,
                                      const fp3d_iris::block_light_levels &sky )
{
    for( auto &entry : world.reflective_batches ) {
        if( !is_water( entry.surface ) ) {
            continue;
        }
        const auto &draw = entry.draws;
        const auto &stream = draw.resident ? world.resident_instances : world.instances;
        const size_t prefix = draw.resident && world.resident_pages ? world.resident_pages->size() : 0;
        entry.iris_lights.clear();
        if( draw.paged_instances || draw.first_instance < prefix ) {
            continue;
        }
        const size_t begin = draw.first_instance - prefix;
        if( begin > stream.size() || draw.instance_count > stream.size() - begin ) {
            continue;
        }
        const auto anchor = draw.resident ? world.resident_to_scene : std::array<float, 3>{};
        entry.iris_lights.reserve( draw.instance_count );
        for( size_t index = begin; index < begin + draw.instance_count; ++index ) {
            entry.iris_lights.push_back( iris_light_at( stream[index].offset, anchor, origin, block, sky ) );
        }
    }
}

inline void assign_iris_terrain_lights( prepared_world &world, const tripoint_abs_ms &origin,
        const fp3d_iris::block_light_levels &block, const fp3d_iris::block_light_levels &sky )
{
    for( auto &stream : world.iris_terrain_lights ) {
        stream.clear();
    }
    for( size_t index = 0; index < world.opaque_batches; ++index ) {
        const auto &draw = world.batches[index];
        if( ( !draw.iris_terrain_material && !draw.iris_model_id && !draw.iris_basic ) ||
            ( draw.paged_instances && !world.resident_pages ) ) {
            continue;
        }
        const auto &instances = draw.paged_instances ? *world.resident_pages :
                                draw.resident ? world.resident_instances : world.instances;
        const size_t prefix = draw.resident && !draw.paged_instances && world.resident_pages ?
                              world.resident_pages->size() : 0;
        const size_t first = draw.first_instance;
        if( first < prefix || first - prefix > instances.size() ||
            draw.instance_count > instances.size() - ( first - prefix ) ) {
            continue;
        }
        const size_t end = first + draw.instance_count;
        auto &lights = world.iris_terrain_lights[draw.resident];
        lights.resize( std::max( lights.size(), end ) );
        const auto anchor = draw.resident ? world.resident_to_scene : std::array<float, 3>{};
        for( size_t i = first; i < end; ++i ) {
            lights[i] = iris_light_at( instances[i - prefix].offset, anchor, origin, block, sky );
        }
    }
}

inline bool stage_iris_terrain_lights( SDL_GPUDevice *device,
                                      const std::vector<Uint32> &lights, fp3d_gpu::upload_buffer &buffer )
{
    if( lights.empty() ) {
        return true;
    }
    if( lights.size() > SDL_MAX_UINT32 / sizeof( Uint32 ) ) {
        return SDL_SetError( "Iris terrain light stream exceeds the GPU buffer limit" );
    }
    const Uint32 bytes = lights.size() * sizeof( Uint32 );
    return buffer.reserve( device, bytes, SDL_GPU_BUFFERUSAGE_VERTEX ) &&
           buffer.stage_delta( lights.data(), bytes );
}

// Borrow only the visible native water batches. Call after the frame's instance
// offsets have been finalized and consume before the frame's meshes retire.
inline std::vector<water_geometry_draw> collect_water_geometry( const prepared_world &world,
        const world_meshes &meshes )
{
    std::vector<water_geometry_draw> result;
    for( const auto &entry : world.reflective_batches ) {
        if( !is_water( entry.surface ) ) {
            continue;
        }
        const auto anchor = entry.draws.resident ? meshes.resident_to_scene : std::array<float, 3>{};
        if( entry.iris_lights.empty() ) {
            result.push_back( { meshes.indexed_draw_for( entry.draws ), anchor, entry.surface, std::nullopt, entry.iris_water_material } );
            continue;
        }
        // Preserve native instance order and keep equal light levels in a single draw.
        for( size_t first = 0; first < entry.iris_lights.size(); ) {
            const auto light = entry.iris_lights[first];
            size_t end = first + 1;
            while( end < entry.iris_lights.size() && entry.iris_lights[end] == light ) {
                ++end;
            }
            auto draw = entry.draws;
            draw.first_instance += first;
            draw.instance_count = end - first;
            result.push_back( { meshes.indexed_draw_for( draw ), anchor, entry.surface, light, entry.iris_water_material } );
            first = end;
        }
    }
    return result;
}

// Use the pack's water material ID and preserve each native batch's anchor.
// The caller supplies lightmap coordinates and decides which missing declared
// frame constants are safe to leave at zero for the selected pack.
struct water_draw_binding {
    std::vector<std::string> missing;
    std::string error;
};

inline fp3d_iris::uniform_values iris_camera_uniforms(
    const fp3d_gpu_camera_math::matrix4 &clip_from_world, float near_plane, float far_plane )
{
    const auto &clip = clip_from_world.elements;
    const float x = std::hypot( clip[0], clip[1], clip[2] );
    const float y = std::hypot( clip[4], clip[5], clip[6] );
    if( x <= 0.f || y <= 0.f || near_plane <= 0.f || far_plane <= near_plane ) {
        return {};
    }
    // Shader packs use column-packed uniforms and an OpenGL-style clip range.
    // The GLSL compatibility wrapper converts clip depth back to SDL's range.
    std::vector<float> view( 16 ), inverse_view( 16 );
    view[15] = inverse_view[15] = 1.f;
    for( int col = 0; col < 4; ++col ) {
        view[col * 4] = clip[col] / x;
        view[col * 4 + 1] = clip[col + 4] / y;
        view[col * 4 + 2] = -clip[col + 12];
    }
    for( int row = 0; row < 3; ++row ) {
        for( int col = 0; col < 3; ++col ) {
            const float rotation = view[row * 4 + col];
            inverse_view[col * 4 + row] = rotation;
            inverse_view[12 + row] -= rotation * view[12 + col];
        }
    }
    const float c = -( far_plane + near_plane ) / ( far_plane - near_plane );
    const float d = -2.f * far_plane * near_plane / ( far_plane - near_plane );
    return {
        { "gbufferModelView", std::move( view ) },
        { "gbufferModelViewInverse", std::move( inverse_view ) },
        { "gbufferProjection", std::vector<float>{ x, 0, 0, 0, 0, y, 0, 0, 0, 0, c, -1, 0, 0, d, 0 } },
        { "gbufferProjectionInverse", std::vector<float>{ 1.f / x, 0, 0, 0, 0, 1.f / y, 0, 0,
                  0, 0, 0, 1.f / d, 0, 0, -1, c / d } },
        { "near", std::vector<float>{ near_plane } },
        { "far", std::vector<float>{ far_plane } }
    };
}

inline std::array<float, 3> iris_unit_sun( std::array<float, 3> sun )
{
    const float length = std::hypot( sun[0], sun[1], sun[2] );
    if( !std::isfinite( length ) || length <= 0.f ) {
        return { 0.f, 1.f, 0.f };
    }
    for( float &axis : sun ) {
        axis /= length;
    }
    return sun;
}

inline fp3d_iris::uniform_values iris_scene_uniforms( const fp3d_iris::frame_inputs &frame,
        float near_plane, float far_plane )
{
    auto values = native_frame_uniform_values( frame );
    values.emplace( "fogEnd", std::vector<float>{
        frame.fog_distance > frame.fog_start ? frame.fog_distance : far_plane
    } );
    auto current = iris_camera_uniforms( frame.current.clip_from_scene, near_plane, far_plane );
    const auto prior = frame.previous ?
                       iris_camera_uniforms( frame.previous->clip_from_scene, near_plane, far_plane ) : current;
    if( !prior.empty() ) {
        values.emplace( "gbufferPreviousModelView", prior.at( "gbufferModelView" ) );
        values.emplace( "gbufferPreviousProjection", prior.at( "gbufferProjection" ) );
    }
    if( !current.empty() ) {
        const auto &view = std::get<std::vector<float>>( current.at( "gbufferModelView" ) );
        const auto sun = iris_unit_sun( frame.sun_direction );
        std::vector<float> sun_position( 3 ), moon_position( 3 );
        for( size_t axis = 0; axis < 3; ++axis ) {
            // Directions use the rotational portion of the column-packed matrix.
            sun_position[axis] = 100.f * ( view[axis] * sun[0] + view[axis + 4] * sun[1] +
                                           view[axis + 8] * sun[2] );
            moon_position[axis] = -sun_position[axis];
        }
        values.emplace( "shadowLightPosition", sun[1] < 0.f ? moon_position : sun_position );
        values.emplace( "upPosition", std::vector<float>{ 100.f * view[4], 100.f * view[5], 100.f * view[6] } );
        values.emplace( "sunPosition", std::move( sun_position ) );
        values.emplace( "moonPosition", std::move( moon_position ) );
    }
    values.merge( current );
    return values;
}

inline fp3d_iris::uniform_values iris_shadow_uniforms(
    const fp3d_gpu_camera_math::matrix4 &clip_from_world )
{
    const auto &clip = clip_from_world.elements;
    const std::array<float, 3> scale{
        std::hypot( clip[0], clip[1], clip[2] ),
        std::hypot( clip[4], clip[5], clip[6] ),
        -2.f * std::hypot( clip[8], clip[9], clip[10] )
    };
    std::vector<float> view( 16 ), inverse_view( 16 ), projection( 16 ), inverse_projection( 16 );
    view[15] = inverse_view[15] = projection[15] = inverse_projection[15] = 1.f;
    for( size_t axis = 0; axis < 3; ++axis ) {
        if( scale[axis] == 0.f ) {
            return {};
        }
        for( size_t i = 0; i < 3; ++i ) {
            const float rotation = ( axis == 2 ? 2.f : 1.f ) * clip[axis * 4 + i] / scale[axis];
            view[i * 4 + axis] = inverse_view[axis * 4 + i] = rotation;
        }
        projection[axis * 5] = scale[axis];
        inverse_projection[axis * 5] = 1.f / scale[axis];
        projection[12 + axis] = axis == 2 ? 2.f * clip[11] - 1.f : clip[axis * 4 + 3];
        inverse_projection[12 + axis] = -projection[12 + axis] / scale[axis];
    }
    return {
        { "shadowModelView", std::move( view ) },
        { "shadowModelViewInverse", std::move( inverse_view ) },
        { "shadowProjection", std::move( projection ) },
        { "shadowProjectionInverse", std::move( inverse_projection ) }
    };
}

inline water_draw_binding bind_iris_shadow_draws( const prepared_world &world,
        const world_meshes &meshes, const fp3d_iris::shader_layout &vertex,
        fp3d_iris::uniform_values frame, fp3d_iris::graphics_stage &stage,
        const fp3d_iris::scene_post &post, std::string_view name, int water_material )
{
    const auto append = [&]( const batch &draw ) -> water_draw_binding {
        const auto anchor = draw.resident ? meshes.resident_to_scene : std::array<float, 3>{};
        frame["cata_iris_anchor"] = std::vector<float>( anchor.begin(), anchor.end() );
        frame["cata_iris_material"] = std::vector<float>{
            static_cast<float>( draw.iris_terrain_material.value_or( -1 ) )
        };
        frame["cata_iris_lightmap"] = std::vector<float>{ 0.f, 0.f };
        auto packed = fp3d_iris::pack_frame_uniforms( vertex, frame );
        if( !packed.error.empty() || !packed.missing.empty() ) {
            return { std::move( packed.missing ), std::move( packed.error ) };
        }
        stage.draws.push_back( meshes.indexed_draw_for( draw ) );
        stage.draw_vertex_constants.push_back( std::move( packed.bytes ) );
        return {};
    };
    const bool translucent = post.shadow_is_translucent( name );
    const size_t first = translucent ? world.badge_end : 0;
    const size_t end = translucent ? world.blend_end : world.opaque_batches;
    for( size_t i = first; i < end; ++i ) {
        const auto &draw = world.batches[i];
        const auto caster = draw.iris_terrain_material ? iris_caster::terrain : draw.shadow_caster;
        if( draw.iris_water || post.shadow_program( translucent, caster, draw.iris_terrain_solid ) != name ) {
            continue;
        }
        auto result = append( draw );
        if( !result.error.empty() || !result.missing.empty() ) {
            return result;
        }
    }
    if( translucent && post.shadow_program( true, iris_caster::water ) == name ) {
        for( const auto &entry : world.reflective_batches ) {
            if( is_water( entry.surface ) ) {
                auto water = entry.draws;
                water.iris_terrain_material = entry.iris_water_material.value_or( water_material );
                auto result = append( water );
                if( !result.error.empty() || !result.missing.empty() ) {
                    return result;
                }
            }
        }
    }
    return {};
}

inline water_draw_binding bind_iris_water_draws( const prepared_world &world,
        const world_meshes &meshes, const fp3d_iris::shader_layout &vertex,
        const fp3d_iris::material_id_map &materials, fp3d_iris::uniform_values frame,
        const std::array<float, 2> &lightmap, fp3d_iris::graphics_stage &stage )
{
    const auto water = collect_water_geometry( world, meshes );
    if( water.empty() ) {
        stage.draws.clear();
        stage.draw_vertex_constants.clear();
        return {};
    }
    if( !vertex.error.empty() || vertex.stage != "vert" ||
        !vertex.uniforms.count( "cata_iris_anchor" ) || !vertex.uniforms.count( "cata_iris_lightmap" ) ||
        !vertex.uniforms.count( "cata_iris_material" ) ) {
        return { {}, "Missing Iris native water vertex layout" };
    }
    fp3d_iris::virtual_material semantic_water;
    semantic_water.shader_id = "minecraft:water";
    semantic_water.surface = fp3d_iris::material_surface::water;
    const auto material = materials.resolve( semantic_water );
    if( !material ) {
        return { {}, "Iris shader pack does not map minecraft:water" };
    }
    std::vector<fp3d_gpu::indexed_draw> draws;
    std::vector<std::vector<std::uint8_t>> constants;
    draws.reserve( water.size() );
    constants.reserve( water.size() );
    water_draw_binding result;
    for( const auto &batch : water ) {
        frame["cata_iris_material"] = std::vector<float>{
            static_cast<float>( batch.iris_water_material.value_or( *material ) )
        };
        frame["cata_iris_lightmap"] = batch.iris_light ?
                                     std::vector<float>{ ( *batch.iris_light & 15 ) * 16.f,
                                         ( *batch.iris_light >> 4 ) * 16.f } :
                                     std::vector<float>( lightmap.begin(), lightmap.end() );
        frame["cata_iris_anchor"] = std::vector<float>( batch.anchor_to_scene.begin(), batch.anchor_to_scene.end() );
        auto packed = fp3d_iris::pack_frame_uniforms( vertex, frame );
        if( !packed.error.empty() ) {
            return { {}, std::move( packed.error ) };
        }
        result.missing = std::move( packed.missing );
        constants.push_back( std::move( packed.bytes ) );
        draws.push_back( batch.draw );
    }
    stage.draws = std::move( draws );
    stage.draw_vertex_constants = std::move( constants );
    return result;
}

inline std::string_view iris_opaque_program( const batch &draw, const fp3d_iris::scene_post &post )
{
    if( draw.iris_basic && post.uses_basic() ) {
        return "gbuffers_basic";
    }
    if( draw.iris_model_id ) {
        return post.model_program( draw.shadow_caster );
    }
    return draw.iris_terrain_material ? post.terrain_program( draw.iris_terrain_solid ) : std::string_view{};
}

inline water_draw_binding bind_iris_opaque_draws( const prepared_world &world,
        const world_meshes &meshes, const fp3d_iris::shader_layout &vertex,
        const fp3d_iris::shader_layout &fragment, fp3d_iris::uniform_values frame,
        fp3d_iris::graphics_stage &stage, const std::array<SDL_GPUBuffer *, 2> &lights, const fp3d_iris::scene_post &post,
        std::string_view name )
{
    const bool model = fp3d_shaderpacks::is_iris_model_program( name );
    for( size_t index = 0; index < world.opaque_batches; ++index ) {
        const auto &draw = world.batches[index];
        if( iris_opaque_program( draw, post ) != name ) {
            continue;
        }
        const auto anchor = draw.resident ? meshes.resident_to_scene : std::array<float, 3>{};
        const int material = model ? draw.iris_model_id.value_or( -1 ) : *draw.iris_terrain_material;
        frame["cata_iris_material"] = std::vector<float>{ static_cast<float>( material ) };
        if( model ) {
            if( name == "gbuffers_basic" ) {
                frame["renderStage"] = std::vector<std::int32_t>{ 0 };
            }
            frame["entityId"] = std::vector<std::int32_t>{ draw.shadow_caster == iris_caster::entity ? material : -1 };
            frame["blockEntityId"] = std::vector<std::int32_t>{ draw.shadow_caster == iris_caster::block ? material : -1 };
            frame["currentRenderedItemId"] = std::vector<std::int32_t>{ draw.iris_item_id };
            frame["entityColor"] = std::vector<float>{ 0.f, 0.f, 0.f, 0.f };
        }
        frame["cata_iris_anchor"] = std::vector<float>( anchor.begin(), anchor.end() );
        if( !lights[draw.resident] ) {
            return { {}, "Missing Iris opaque geometry instance light buffer" };
        }
        auto packed = fp3d_iris::pack_frame_uniforms( vertex, frame );
        if( !packed.error.empty() || !packed.missing.empty() ) {
            return { std::move( packed.missing ), std::move( packed.error ) };
        }
        auto gpu_draw = meshes.indexed_draw_for( draw );
        gpu_draw.instance_light = lights[draw.resident];
        stage.draws.push_back( gpu_draw );
        stage.draw_vertex_constants.push_back( std::move( packed.bytes ) );
        if( model ) {
            auto constants = fp3d_iris::pack_frame_uniforms( fragment, frame );
            if( !constants.error.empty() || !constants.missing.empty() ) {
                return { std::move( constants.missing ), std::move( constants.error ) };
            }
            if( !constants.bytes.empty() ) {
                stage.draw_fragment_constants.push_back( std::move( constants.bytes ) );
            }
        }
    }
    return {};
}

inline bool record_shadow( SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *depth_texture,
                           const fp3d_gpu_camera_math::matrix4 &clip_from_world,
                           const prepared_world &world, SDL_GPUTexture *atlas,
                           const fp3d_gpu::textured_pipeline &pipeline,
                           const world_meshes &meshes, SDL_GPUBuffer *instances,
                           SDL_GPUBuffer *commands, bool point_atlas = false )
{
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = depth_texture;
    depth.clear_depth = 1;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    auto *pass = SDL_BeginGPURenderPass( cmd, nullptr, 0, &depth );
    if( !pass ) {
        return false;
    }
    bool drawn = true;
    const size_t faces = point_atlas ? world.point_faces.size() : 1;
    for( size_t face = 0; face < faces && drawn; ++face ) {
        if( point_atlas ) {
            const float size = fp3d_gpu::point_shadow_face_size;
            const SDL_GPUViewport area{ ( face % 3 ) * size, ( face / 3 ) * size,
                                        size, size, 0.f, 1.f };
            SDL_SetGPUViewport( pass, &area );
        }
        const auto &matrix = point_atlas ? world.point_faces[face] : clip_from_world;
        drawn = pipeline.bind( cmd, pass, atlas, matrix, instances ) &&
                meshes.draw_indirect( cmd, pass, commands, world.batches, 0, world.opaque_batches, matrix );
        for( const auto &structure : world.open_structure_shadows ) {
            if( !drawn ) {
                break;
            }
            meshes.vertex_constants( cmd, matrix, {}, structure.resident );
            drawn = meshes.draw( pass, structure );
        }
    }
    SDL_EndGPURenderPass( pass );
    return drawn;
}

struct world_pass_targets {
    SDL_GPUTexture *color = nullptr;
    SDL_GPUTexture *depth = nullptr;
    SDL_GPUTexture *water_depth = nullptr;
    const fp3d_gpu::textured_pipeline *behind_water = nullptr;
};

inline bool initialize_behind_water( SDL_GPUDevice *device, fp3d_gpu::textured_pipeline &pipeline,
                                     SDL_GPUTextureFormat format = fp3d_gpu::scene_target::color_format() )
{
    if( pipeline.ready() ) {
        return true;
    }
    auto *vs = shader( device, fp3d_gpu_shader_bytes::base_vertex,
                       sizeof( fp3d_gpu_shader_bytes::base_vertex ), true, 1, 0 );
    auto *fs = shader( device, fp3d_gpu_shader_bytes::behind_water_fragment,
                       sizeof( fp3d_gpu_shader_bytes::behind_water_fragment ), false, 1, 7 );
    const bool initialized = pipeline.initialize( device, vs, fs, fp3d_gpu::mesh_pass::blend, format );
    for( auto *stage : { vs, fs } ) {
        if( stage ) {
            SDL_ReleaseGPUShader( device, stage );
        }
    }
    return initialized;
}

inline bool record_world( SDL_GPUCommandBuffer *cmd, fp3d_gpu::scene_target &target,
                          const prepared_world &world, SDL_GPUTexture *atlas,
                          const scene_pipelines &pipelines,
                          const world_meshes &meshes, SDL_GPUBuffer *instances,
                          SDL_GPUBuffer *commands, const fp3d_gpu::shadow_maps &maps = {},
                          const fp3d_gpu::sky_pipeline *sky = nullptr, bool retain_depth = false,
                          SDL_GPUTexture *pbr_atlas = nullptr,
                          const fp3d_gpu::textured_pipeline *background = nullptr,
                          const compute_draws *compute = nullptr,
                          fp3d_iris::borrowed_scene_images *iris_scene = nullptr,
                          bool opaque_only = false, world_pass_targets resume = {},
                          const fp3d_iris::scene_post *iris_terrain = nullptr )
{
    if( iris_scene && !target.sampleable_depth() ) {
        return SDL_SetError( "Iris world input requires sampleable scene depth" );
    }
    auto atmosphere = world.atmosphere;
    if( !pbr_atlas ) {
        atmosphere.pbr_eye_strength[3] = 0;
    }
    if( !maps.beam ) {
        atmosphere.beam_index_and_texel[0] = 0;
    }
    if( !maps.point ) {
        atmosphere.point_index_and_texel[0] = 0;
    }
    if( !maps.sun ) {
        atmosphere.sun_direction_strength[3] = 0;
    } else if( !maps.far_sun ) {
        atmosphere.sun_clip_from_world = world.single_sun_clip_from_world;
        atmosphere.sun_texel_sizes[1] = 0;
    }
    SDL_GPUColorTargetInfo color{};
    color.texture = resume.color ? resume.color : target.native_color();
    color.clear_color = sky ? SDL_FColor{ 0, 0, 0, 1 } : SDL_FColor{ .04f, .05f, .06f, 1 };
    color.load_op = resume.color ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = resume.depth ? resume.depth : target.depth();
    depth.clear_depth = 1;
    depth.load_op = resume.depth ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    depth.store_op = retain_depth || iris_scene || resume.depth ?
                     SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    auto *pass = SDL_BeginGPURenderPass( cmd, &color, 1, &depth );
    if( !pass ) { return false; }
    const std::array<Uint32, 5> boundaries{ 0, world.opaque_batches, world.badge_end,
        world.blend_end, static_cast<Uint32>( world.batches.size() )
    };
    bool drawn = true;
    constexpr size_t blend_pass = static_cast<size_t>( fp3d_gpu::mesh_pass::blend );
    const size_t first_pass = resume.behind_water ? blend_pass : ( resume.color ? 1 : 0 );
    const size_t end_pass = resume.behind_water ? blend_pass + 1 : ( opaque_only ? 1 : pipelines.size() );
    for( size_t i = first_pass; i < end_pass && drawn; ++i ) {
        const Uint32 first = boundaries[i];
        const Uint32 count = boundaries[i + 1] - first;
        if( count ) {
            const bool opaque = i == static_cast<size_t>( fp3d_gpu::mesh_pass::opaque );
            const auto draw_range = [&]( Uint32 begin, Uint32 end ) {
                return compute && opaque ?
                       meshes.draw_opaque( cmd, pass, commands, world, *compute, begin, end ) :
                       meshes.draw_indirect( cmd, pass, commands, world.batches, begin, end - begin,
                                             world.camera, world.base_light );
            };
            const auto &pipeline = resume.behind_water ? *resume.behind_water : pipelines[i];
            drawn = pipeline.bind( cmd, pass, atlas, world.camera, instances,
                                   world.base_light, atmosphere, maps, pbr_atlas, resume.water_depth );
            if( drawn && ( iris_scene || iris_terrain ) && opaque ) {
                const auto end = world.batches.begin() + boundaries[i + 1];
                auto begin = world.batches.begin() + first;
                const auto replaced = [&]( const batch &entry ) {
                    return ( iris_scene && entry.iris_water ) ||
                           ( iris_terrain && !iris_opaque_program( entry, *iris_terrain ).empty() );
                };
                while( drawn && begin != end ) {
                    begin = std::find_if_not( begin, end, replaced );
                    if( begin == end ) {
                        break;
                    }
                    const auto next = std::find_if( begin, end, replaced );
                    meshes.vertex_constants( cmd, world.camera, world.base_light, false );
                    drawn = draw_range( begin - world.batches.begin(), next - world.batches.begin() );
                    begin = next;
                }
            } else if( drawn ) {
                drawn = draw_range( first, boundaries[i + 1] );
            }
        }
        // Opaque depth rejects the sky. Translucent geometry still blends over it.
        if( drawn && i == static_cast<size_t>( fp3d_gpu::mesh_pass::opaque ) ) {
            if( sky && world.sky ) {
                sky->draw( cmd, pass, *world.sky );
            }
            if( background && world.unknown_background ) {
                const auto &draw = *world.unknown_background;
                const SDL_GPUViewport far_depth{ 0, 0, static_cast<float>( world.width ),
                                                 static_cast<float>( world.height ), 1, 1 };
                SDL_SetGPUViewport( pass, &far_depth );
                drawn = background->bind( cmd, pass, atlas, world.camera, instances ) &&
                        meshes.draw( pass, draw );
                SDL_GPUViewport normal_depth = far_depth;
                normal_depth.min_depth = 0;
                SDL_SetGPUViewport( pass, &normal_depth );
            }
        }
    }
    SDL_EndGPURenderPass( pass );
    if( drawn && iris_scene ) {
        *iris_scene = fp3d_iris::borrow_scene_images( target );
    }
    return drawn;
}

// The world capture writes opaque depth only. Both logical views initially
// borrow it; later Iris geometry writes a new version of depthtex0.
// Colortex and shadow inputs are supplied by the pack's own graph setup.
inline bool add_iris_world_images( SDL_Renderer *renderer, SDL_Texture *atlas,
                                  const fp3d_iris::borrowed_scene_images &scene,
                                  std::map<std::string, fp3d_iris::borrowed_graph_image> &images )
{
    if( !atlas || atlas->format != SDL_PIXELFORMAT_RGBA32 || atlas->w <= 0 || atlas->h <= 0 ) {
        return SDL_SetError( "Iris requires the native RGBA32 material atlas" );
    }
    auto *texture = fp3d_gpu::native_atlas( renderer, atlas );
    if( !texture || !scene.color.texture || !scene.depth.texture ) {
        return SDL_SetError( "Missing native Iris world textures" );
    }
    for( const char *name : { "worldColor:0", "depthtex0:0", "depthtex1:0", "worldAtlas:0" } ) {
        if( images.count( name ) ) {
            return SDL_SetError( "Conflicting Iris world texture: %s", name );
        }
    }
    images.emplace( "worldColor:0", scene.color );
    images.emplace( "depthtex0:0", scene.depth );
    images.emplace( "depthtex1:0", scene.depth );
    images.emplace( "worldAtlas:0", fp3d_iris::borrowed_graph_image{
        texture, { fp3d_gpu::scene_target::color_format(),
                   static_cast<Uint32>( atlas->w ), static_cast<Uint32>( atlas->h ) }
    } );
    return true;
}

// Prepared by the selected pack. GPU pipelines, samplers and imports remain
// live until the submit fence has retired.
struct iris_water_frame {
    const fp3d_iris::pack_fullscreen_graph &graph;
    const fp3d_iris::pack_properties &properties;
    const std::map<std::string, fp3d_iris::graph_image_spec> &output_specs;
    const std::map<std::string, fp3d_iris::borrowed_graph_image> &imports;
    const std::map<std::string, fp3d_iris::graphics_stage> &stages;
    const std::map<std::string, fp3d_iris::compute_stage> &compute;
    const fp3d_iris::shader_layout &water_vertex;
    const fp3d_iris::uniform_values &water_values;
    const fp3d_iris::material_id_map &materials;
    std::array<float, 2> water_lightmap;
};

template<typename Capture>
bool record_iris_water_frame( SDL_GPUCommandBuffer *cmd, const fp3d_gpu::scene_target &target,
                              SDL_Renderer *renderer, SDL_Texture *atlas, const prepared_world &world,
                              const world_meshes &meshes, const iris_water_frame &pack,
                              std::unique_ptr<fp3d_iris::pack_graph_images> &images, Capture capture )
{
    if( !fp3d_iris::has_pack_display_output( pack.graph ) ) {
        return false;
    }
    auto stages = pack.stages;
    const auto water = stages.find( "gbuffers_water" );
    const auto water_pass = std::find_if( pack.graph.passes.begin(), pack.graph.passes.end(),
    []( const fp3d_iris::graph_pass &pass ) {
        return pass.program == "gbuffers_water" && pass.kind == fp3d_iris::pass_kind::geometry;
    } );
    if( water == stages.end() || water_pass == pack.graph.passes.end() ) {
        return SDL_SetError( "Iris frame is missing gbuffers_water" );
    }
    const auto draws = bind_iris_water_draws( world, meshes, pack.water_vertex, pack.materials,
                       pack.water_values, pack.water_lightmap, water->second );
    if( !draws.error.empty() ) {
        return SDL_SetError( "%s", draws.error.c_str() );
    }
    if( !draws.missing.empty() ) {
        return SDL_SetError( "Unbound Iris water constant: %s", draws.missing.front().c_str() );
    }
    water->second.allow_empty_geometry = true;

    auto scene = fp3d_iris::borrow_scene_images( target );
    auto imports = pack.imports;
    if( !add_iris_world_images( renderer, atlas, scene, imports ) ) {
        return false;
    }
    images = fp3d_iris::prepare_pack_graph_images( target.device(), pack.graph, pack.properties,
             pack.output_specs, imports );
    if( !images || !capture( &scene ) ) {
        return false;
    }
    return fp3d_iris::record_gpu_graph( cmd, pack.graph.external, pack.graph.passes, pack.graph.output,
                                       images->textures.images(), stages, pack.compute, images->specs ) &&
           fp3d_iris::record_pack_display( cmd, pack.graph, *images, target );
}

class terrain_compute_pass
{
    public:
        explicit terrain_compute_pass( SDL_GPUDevice *device ) : device_( device ) {}
        ~terrain_compute_pass() {
            if( pipeline_ ) {
                SDL_ReleaseGPUComputePipeline( device_, pipeline_ );
            }
            for( auto *buffer : { instances_, commands_ } ) {
                if( buffer ) {
                    SDL_ReleaseGPUBuffer( device_, buffer );
                }
            }
        }

        bool stage( const prepared_world &world ) {
            if( !cache_.reuse_or_rebuild( world ) ) {
                inputs_committed_ = false;
                committed_planes_.reset();
            }
            const auto &inputs = cache_.inputs();
            if( inputs.blocks.empty() || !initialize() ) {
                return false;
            }
            // Only a submitted frame may make these buffers reusable.
            if( inputs_committed_ ) {
                return true;
            }
            const auto stage_input = [&]( auto &buffer, const auto &items, Uint32 limit ) {
                const auto bytes = static_cast<Uint32>( items.size() * sizeof( items.front() ) );
                return buffer.reserve( device_, bytes, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
                                       limit * sizeof( items.front() ) ) &&
                       buffer.stage_delta( items.data(), bytes );
            };
            return stage_input( storage_[0], inputs.cells,
                                terrain_compute::max_blocks * terrain_compute::cells_per_block ) &&
                   stage_input( storage_[1], inputs.models, terrain_compute::max_blocks ) &&
                   stage_input( storage_[2], inputs.blocks, terrain_compute::max_blocks );
        }

        bool record_uploads( SDL_GPUCopyPass *copy ) {
            return inputs_committed_ ||
                   ( storage_[0].record_delta( copy ) && storage_[1].record_delta( copy ) &&
                     storage_[2].record_delta( copy ) );
        }

        bool dispatch( SDL_GPUCommandBuffer *cmd, const prepared_world &world ) {
            pending_planes_ = terrain_compute_planes( world );
            reused_dispatch_ = committed_planes_ && *committed_planes_ == pending_planes_;
            if( reused_dispatch_ ) {
                return true;
            }
            const auto &inputs = cache_.inputs();
            const terrain_compute::camera view{ pending_planes_,
                static_cast<Uint32>( inputs.cells.size() ), static_cast<Uint32>( inputs.models.size() ),
                static_cast<Uint32>( inputs.blocks.size() ), world.material_revision
            };
            return terrain_compute::record( cmd, pipeline_, view,
            { storage_[0].get(), storage_[1].get(), storage_[2].get() }, instances_, commands_ );
        }

        compute_draws draws() const {
            return { instances_, commands_, cache_.inputs().draw_indices };
        }

        Uint32 upload_bytes() const {
            return inputs_committed_ ? 0 :
                   storage_[0].delta_bytes() + storage_[1].delta_bytes() + storage_[2].delta_bytes();
        }

        bool reuses_gpu_inputs() const {
            return inputs_committed_;
        }

        bool reuses_dispatch() const {
            return reused_dispatch_;
        }

        void commit_dispatch() {
            committed_planes_ = pending_planes_;
        }

        void commit_uploads() {
            if( inputs_committed_ ) {
                return;
            }
            for( auto &buffer : storage_ ) {
                buffer.commit_delta();
            }
            inputs_committed_ = true;
        }

    private:
        bool initialize() {
            if( !attempted_ ) {
                attempted_ = true;
                pipeline_ = terrain_compute::create_pipeline( device_, fp3d_gpu_shader_bytes::terrain_compute,
                            sizeof( fp3d_gpu_shader_bytes::terrain_compute ) );
                if( pipeline_ ) {
                    SDL_GPUBufferCreateInfo info{};
                    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_VERTEX;
                    info.size = terrain_compute::max_blocks * terrain_compute::cells_per_block *
                                sizeof( fp3d_gpu::mesh_instance );
                    instances_ = SDL_CreateGPUBuffer( device_, &info );
                    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_INDIRECT;
                    info.size = terrain_compute::max_blocks * sizeof( SDL_GPUIndexedIndirectDrawCommand );
                    commands_ = SDL_CreateGPUBuffer( device_, &info );
                }
                if( !pipeline_ || !instances_ || !commands_ ) {
                    DebugLog( D_WARNING, D_MAIN ) << "fp3d: optional terrain compute unavailable: " << SDL_GetError();
                }
            }
            return pipeline_ && instances_ && commands_;
        }

        SDL_GPUDevice *device_;
        SDL_GPUComputePipeline *pipeline_ = nullptr;
        SDL_GPUBuffer *instances_ = nullptr;
        SDL_GPUBuffer *commands_ = nullptr;
        bool attempted_ = false;
        bool inputs_committed_ = false;
        bool reused_dispatch_ = false;
        std::optional<std::array<terrain_compute::vec4, 6>> committed_planes_;
        std::array<terrain_compute::vec4, 6> pending_planes_{};
        std::array<fp3d_gpu::upload_buffer, 3> storage_;
        terrain_compute_input_cache cache_;
};

struct occlusion_inputs {
    std::vector<fp3d_gpu::occlusion::candidate> candidates;
    std::vector<Uint32> occluders;
    unsigned candidate_count = 0;
};

inline occlusion_inputs prepare_occlusion( const prepared_world &world )
{
    if( world.batches.empty() || world.batches.size() > fp3d_gpu::occlusion::max_draws ) {
        return {};
    }
    const auto terrain = prepare_terrain_compute( world );
    occlusion_inputs result;
    result.candidates.resize( world.batches.size() );
    const fp3d_gpu::occlusion::viewport view{ world.width, world.height,
        static_cast<Uint32>( world.batches.size() )
    };
    constexpr size_t occluder_limit = 16;
    for( size_t i = 0; i < terrain.blocks.size(); ++i ) {
        const auto &block = terrain.blocks[i];
        const std::array<float, 3> center{
            block.center[0] + world.resident_to_scene[0], block.center[1] + world.resident_to_scene[1],
            block.center[2] + world.resident_to_scene[2]
        };
        const auto projected = fp3d_gpu::occlusion::project( world.camera, center,
        { block.half_extent[0], block.half_extent[1], block.half_extent[2] }, view );
        if( !projected.eligible ) {
            continue;
        }
        const auto draw = terrain.draw_indices[i];
        // A paged floor has a single quad. Walls provide a small set of
        // occluders that also remain in the normal opaque pass.
        if( terrain.models[block.model_index].index_count > 6 && result.occluders.size() < occluder_limit ) {
            result.occluders.push_back( draw );
        } else {
            result.candidates[draw] = projected;
            ++result.candidate_count;
        }
    }
    return result;
}

class occlusion_pass
{
    public:
        explicit occlusion_pass( SDL_GPUDevice *device ) : device_( device ) {}
        ~occlusion_pass() {
            for( auto *pipeline : { reduce_, filter_ } ) {
                if( pipeline ) {
                    SDL_ReleaseGPUComputePipeline( device_, pipeline );
                }
            }
            for( auto *buffer : { tiles_, filtered_ } ) {
                if( buffer ) {
                    SDL_ReleaseGPUBuffer( device_, buffer );
                }
            }
            if( sampler_ ) {
                SDL_ReleaseGPUSampler( device_, sampler_ );
            }
        }

        bool stage( const prepared_world &world ) {
            inputs_ = prepare_occlusion( world );
            if( !inputs_.candidate_count || inputs_.occluders.empty() || !initialize() ) {
                return false;
            }
            constexpr auto stride = sizeof( fp3d_gpu::occlusion::candidate );
            const auto size = static_cast<Uint32>( inputs_.candidates.size() * stride );
            return candidates_.reserve( device_, size, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
                                        fp3d_gpu::occlusion::max_draws * stride ) &&
                   candidates_.stage( inputs_.candidates.data(), size );
        }

        bool record_upload( SDL_GPUCopyPass *copy ) {
            return candidates_.record( copy );
        }

        SDL_GPUBuffer *record( SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *depth, SDL_GPUTexture *atlas,
                               const prepared_world &world, const world_meshes &meshes,
                               const fp3d_gpu::textured_pipeline &depth_pipeline, SDL_GPUBuffer *original ) const {
            SDL_GPUDepthStencilTargetInfo target{};
            target.texture = depth;
            target.clear_depth = 1;
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            auto *pass = SDL_BeginGPURenderPass( cmd, nullptr, 0, &target );
            if( !pass ) {
                return nullptr;
            }
            bool drawn = meshes.resident && depth_pipeline.bind( cmd, pass, atlas, world.camera,
                         meshes.resident_instances, world.base_light );
            if( drawn ) {
                meshes.vertex_constants( cmd, world.camera, world.base_light, true );
                for( const Uint32 draw : inputs_.occluders ) {
                    drawn = meshes.resident->draw_indirect( pass, original, 1,
                            draw * sizeof( SDL_GPUIndexedIndirectDrawCommand ) );
                    if( !drawn ) {
                        break;
                    }
                }
            }
            SDL_EndGPURenderPass( pass );
            const fp3d_gpu::occlusion::viewport view{ world.width, world.height,
                static_cast<Uint32>( inputs_.candidates.size() )
            };
            return drawn && fp3d_gpu::occlusion::record( cmd, view, { reduce_, filter_, depth, sampler_,
                    tiles_, candidates_.get(), original, filtered_ } ) ? filtered_ : nullptr;
        }

        unsigned candidate_count() const {
            return inputs_.candidate_count;
        }

    private:
        bool initialize() {
            if( !attempted_ ) {
                attempted_ = true;
                const auto make_buffer = [&]( Uint32 size, SDL_GPUBufferUsageFlags usage ) {
                    SDL_GPUBufferCreateInfo info{};
                    info.size = size;
                    info.usage = usage;
                    return SDL_CreateGPUBuffer( device_, &info );
                };
                reduce_ = fp3d_gpu::occlusion::create_pipeline( device_, fp3d_gpu_shader_bytes::occlusion_reduce,
                          sizeof( fp3d_gpu_shader_bytes::occlusion_reduce ), true );
                filter_ = fp3d_gpu::occlusion::create_pipeline( device_, fp3d_gpu_shader_bytes::occlusion_filter,
                          sizeof( fp3d_gpu_shader_bytes::occlusion_filter ), false );
                if( reduce_ && filter_ ) {
                    constexpr auto side = fp3d_gpu::occlusion::max_extent / fp3d_gpu::occlusion::tile_size;
                    tiles_ = make_buffer( side * side * sizeof( Uint32 ), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                                          SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE );
                    filtered_ = make_buffer( fp3d_gpu::occlusion::max_draws * sizeof( SDL_GPUIndexedIndirectDrawCommand ),
                                             SDL_GPU_BUFFERUSAGE_INDIRECT | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE );
                    SDL_GPUSamplerCreateInfo info{};
                    info.min_filter = info.mag_filter = SDL_GPU_FILTER_NEAREST;
                    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
                    info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                    sampler_ = SDL_CreateGPUSampler( device_, &info );
                }
                if( !ready() ) {
                    DebugLog( D_WARNING, D_MAIN ) << "fp3d: optional depth occlusion unavailable: " << SDL_GetError();
                }
            }
            return ready();
        }

        bool ready() const {
            return reduce_ && filter_ && tiles_ && filtered_ && sampler_;
        }

        SDL_GPUDevice *device_;
        bool attempted_ = false;
        SDL_GPUComputePipeline *reduce_ = nullptr;
        SDL_GPUComputePipeline *filter_ = nullptr;
        SDL_GPUBuffer *tiles_ = nullptr;
        SDL_GPUBuffer *filtered_ = nullptr;
        SDL_GPUSampler *sampler_ = nullptr;
        fp3d_gpu::upload_buffer candidates_;
        occlusion_inputs inputs_;
};

// Both slots live on the SDL renderer's device. A native submission is
// queued after SDL presented the previous texture, so its fence also covers
// that previous SDL sample. Keep presenting the latest frame while it is busy.
class live_world
{
    public:
        explicit live_world( SDL_Renderer *renderer, bool request_compute = false,
                             bool request_occlusion = false ) :
            renderer_( renderer ), device_( SDL_GetGPURendererDevice( renderer ) ),
            auxiliary_readback_( device_ ), terrain_compute_( device_ ), occlusion_( device_ ) {
            const char *driver = SDL_GetGPUDeviceDriver( device_ );
            const bool d3d12 = driver && std::string_view( driver ) == "direct3d12";
            terrain_compute_enabled_ = request_compute && d3d12;
            occlusion_enabled_ = request_occlusion && d3d12 && !terrain_compute_enabled_;
        }

        ~live_world() {
            SDL_FlushRenderer( renderer_ );
            SDL_WaitForGPUIdle( device_ );
            if( fence_ ) {
                SDL_ReleaseGPUFence( device_, fence_ );
            }
            for( auto *texture : { shadow_maps_.beam, shadow_maps_.sun, shadow_maps_.far_sun,
                                   shadow_maps_.point } ) {
                if( texture ) {
                    SDL_ReleaseGPUTexture( device_, texture );
                }
            }
        }

        bool ready() const {
            return !failed_ && ( !fence_ || SDL_QueryGPUFence( device_, fence_ ) );
        }

        fp3d_gpu::preparation_mode preparation( bool materials_current ) {
            return failed_ ? fp3d_gpu::preparation_mode::defer :
                   preparation_.choose( ready(), materials_current );
        }

        void collect_auxiliary() {
            auxiliary_readback_.collect( fence_, fp3d_materials::revision(),
                                         fp3d_materials::cache_auxiliary_readback );
        }

        void hide() {
            visible_ = false;
            iris_history_.reset();
        }

        SDL_Texture *texture() const {
            if( !visible_ ) {
                return nullptr;
            }
            const auto &slot = slots_[current_];
            return slot.photon_displayed ? slot.photon_output.get() : slot.target.color();
        }

        // Called only after SDL_RenderPresent submitted SDL's atlas writes and
        // composition. Downloads recorded here are consumed on a later frame.
        struct iris_frame_source {
            const fp3d_snapshot::scene_snapshot &scene;
            const fp3d_camera::pose &camera;
            fp3d_iris::world_clock clock;
            std::array<int, 2> eye_brightness{};
        };

        fp3d_gpu::submission_result submit( const prepared_world &world, const iris_water_frame *iris = nullptr,
                                            const fp3d_shaderpacks::iris_compile_state *compiled = nullptr,
                                            const iris_frame_source *source = nullptr ) {
            using result = fp3d_gpu::submission_result;
            fp3d_regression::turn_probe timing( "gpu_submit_preflight" );
            if( !ready() || world.material_revision != fp3d_materials::revision() ) {
                return result::skipped;
            }
            auto *material_atlas = fp3d_materials::gpu_colour_atlas( renderer_ );
            auto *atlas = fp3d_gpu::native_atlas( renderer_, material_atlas );
            if( !atlas ) {
                return result::failed;
            }
            if( !initialize_pipelines() ) {
                failed_ = true;
                hide();
                return result::failed;
            }

            const unsigned next = 1 - current_;
            auto &slot = slots_[next];
            slot.iris_images.reset();
            if( slot.width != world.width || slot.height != world.height ) {
                slot.iris_post.images.reset();
                slot.target.reset();
                slot.photon_output.reset();
                slot.reflectivity_mask.reset();
                if( !slot.target.initialize( renderer_, world.width, world.height, true ) ) {
                    slot.width = slot.height = 0;
                    return result::failed;
                }
                slot.width = world.width;
                slot.height = world.height;
            }
            bool iris_post_ready = false;
            if( compiled && !iris ) {
                SDL_ClearError();
                iris_post_ready = iris_post_.update( device_, *compiled );
                if( !iris_post_ready && *SDL_GetError() ) {
                    SDL_LogWarn( SDL_LOG_CATEGORY_RENDER, "Iris native fallback: %s", SDL_GetError() );
                }
            }
            timing.stage( "gpu_submit_stage_mesh" );
            const bool has_dynamic = !world.geometry.indices.empty();
            const bool update_resident = world.resident_geometry && resident_source_ != world.resident_geometry;
            if( update_resident ) {
                resident_source_.reset();
                if( !resident_mesh_.stage( device_, *world.resident_geometry, true ) ) {
                    return result::failed;
                }
            }
            const bool dynamic_instances = !world.instances.empty();
            const bool static_instances = world.resident_instance_count != 0;
            if( ( has_dynamic && !slot.mesh.stage( device_, world.geometry, true ) ) ||
                !stage_instances( device_, world.instances, slot.instances, true ) ||
                !stage_resident_instances( device_, world, resident_instances_ ) ||
                !stage_draws( device_, world, slot.commands, &slot.retained_draws, occlusion_enabled_ ) ) {
                return result::failed;
            }
            const bool stage_terrain_lights = iris_post_ready && iris_post_.uses_opaque_geometry() &&
                    stage_iris_terrain_lights( device_, world.iris_terrain_lights[0], slot.terrain_lights[0] ) &&
                    stage_iris_terrain_lights( device_, world.iris_terrain_lights[1], slot.terrain_lights[1] );
            const bool pbr_staged = world.atmosphere.pbr_eye_strength[3] > 0 &&
                                    pbr_.stage( device_, world.material_revision, fp3d_materials::auxiliary_tiles() );
            const bool queue_auxiliary = auxiliary_readback_.prepare( world.auxiliary_slots );
            const bool compute_staged = terrain_compute_enabled_ && terrain_compute_.stage( world );
            const bool occlusion_staged = occlusion_enabled_ && slot.target.sampleable_depth() &&
                                          occlusion_.stage( world ) && initialize_shadow_caster();
            fp3d_gpu::shadow_maps maps;
            if( world.atmosphere.beam_index_and_texel[0] > 0 ) {
                maps.beam = shadow_texture( shadow_maps_.beam, beam_attempted_, fp3d_gpu::beam_shadow_size );
            }
            if( world.atmosphere.point_index_and_texel[0] > 0 ) {
                const Uint32 size = fp3d_gpu::point_shadow_face_size;
                maps.point = shadow_texture( shadow_maps_.point, point_attempted_, 3 * size, 2 * size );
            }
            if( world.atmosphere.sun_direction_strength[3] > 0 ) {
                maps.sun = shadow_texture( shadow_maps_.sun, sun_attempted_, fp3d_gpu::sun_shadow_size );
                if( maps.sun ) {
                    maps.far_sun = shadow_texture( shadow_maps_.far_sun, far_sun_attempted_,
                                                   fp3d_gpu::far_sun_shadow_size );
                }
            }
            timing.stage( "gpu_submit_record" );
            auto *cmd = SDL_AcquireGPUCommandBuffer( device_ );
            if( !cmd ) {
                return result::failed;
            }
            const bool pbr_ready = pbr_staged && pbr_.record_clear( cmd );
            auto *copy = SDL_BeginGPUCopyPass( cmd );
            if( !copy ) {
                SDL_CancelGPUCommandBuffer( cmd );
                return result::failed;
            }
            const bool uploaded = ( !update_resident || resident_mesh_.record( copy ) ) &&
                                  ( !has_dynamic || slot.mesh.record( copy ) ) &&
                                  ( !dynamic_instances || slot.instances.record_delta( copy ) ) &&
                                  ( !static_instances || resident_instances_.record_delta( copy ) ) &&
                                  slot.commands.record_delta( copy );
            std::array<SDL_GPUBuffer *, 2> terrain_lights{};
            if( uploaded && stage_terrain_lights ) {
                for( size_t i = 0; i < terrain_lights.size(); ++i ) {
                    if( !world.iris_terrain_lights[i].empty() && slot.terrain_lights[i].record_delta( copy ) ) {
                        terrain_lights[i] = slot.terrain_lights[i].get();
                    }
                }
            }
            if( uploaded && pbr_ready ) {
                pbr_.record( copy );
            }
            if( uploaded && queue_auxiliary ) {
                auxiliary_readback_.record( copy, atlas, world.auxiliary_slots );
            }
            const bool compute_uploaded = uploaded && compute_staged && terrain_compute_.record_uploads( copy );
            const bool occlusion_uploaded = uploaded && occlusion_staged && occlusion_.record_upload( copy );
            SDL_EndGPUCopyPass( copy );
            const bool compute_ready = compute_uploaded && terrain_compute_.dispatch( cmd, world );
            const auto compute = terrain_compute_.draws();
            const world_meshes meshes{ slot.mesh, world.resident_geometry ? &resident_mesh_ : nullptr,
                                       world.resident_to_scene, dynamic_instances ? slot.instances.get() : nullptr,
                                       static_instances ? resident_instances_.get() : nullptr };
            auto *initial_instances = dynamic_instances ? meshes.dynamic_instances : meshes.resident_instances;
            auto *filtered = occlusion_uploaded ? occlusion_.record( cmd, slot.target.depth(), atlas, world,
                             meshes, shadow_caster_, slot.commands.get() ) : nullptr;
            const auto record_map = [&]( SDL_GPUTexture *depth,
                                         const fp3d_gpu_camera_math::matrix4 &matrix, bool point = false ) {
                return !depth || record_shadow( cmd, depth, matrix, world, atlas, shadow_caster_,
                                                meshes, initial_instances, slot.commands.get(), point );
            };
            const auto &sun_matrix = maps.far_sun ? world.atmosphere.sun_clip_from_world :
                                     world.single_sun_clip_from_world;
            const bool draw_halos = world.halo_count && slot.target.sampleable_depth() &&
                                    initialize_halos();
            SDL_GPUTexture *photon_target = nullptr;
            if( !iris && !iris_post_ready && world.photon && initialize_photon() ) {
                if( !slot.photon_output ) {
                    slot.photon_output.reset( SDL_CreateTexture( renderer_, SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_TARGET, world.width, world.height ) );
                    if( slot.photon_output &&
                        !SDL_SetTextureBlendMode( slot.photon_output.get(), SDL_BLENDMODE_NONE ) ) {
                        slot.photon_output.reset();
                    }
                }
                if( slot.photon_output ) {
                    photon_target = fp3d_gpu::native_atlas( renderer_, slot.photon_output.get() );
                }
            }
            fp3d_gpu::photon_uniforms photon_settings{};
            if( photon_target ) {
                photon_settings = *world.photon;
                if( maps.sun ) {
                    photon_settings.sun_matrix = sun_matrix.elements;
                    photon_settings.far_sun_matrix = world.atmosphere.far_sun_clip_from_world.elements;
                }
            }
            const bool retain_depth = draw_halos || ( photon_target && slot.target.sampleable_depth() ) ||
                                      ( iris_post_ready && iris_post_.needs_depth() );
            SDL_GPUTexture *reflectivity_mask = nullptr;
            if( photon_target && photon_settings.depth_and_ao[3] == 0.f &&
                slot.target.sampleable_depth() && !world.reflective_batches.empty() &&
                initialize_reflectivity_mask() ) {
                if( !slot.reflectivity_mask ) {
                    slot.reflectivity_mask.reset( SDL_CreateTexture( renderer_, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_TARGET, world.width, world.height ) );
                }
                if( slot.reflectivity_mask ) {
                    reflectivity_mask = fp3d_gpu::native_atlas( renderer_, slot.reflectivity_mask.get() );
                }
            }
            if( !uploaded ||
                !record_map( maps.beam, world.atmosphere.beam_clip_from_world ) ||
                !record_map( maps.point, world.point_faces[0], true ) ||
                !record_map( maps.sun, sun_matrix ) ||
                !record_map( maps.far_sun, world.atmosphere.far_sun_clip_from_world ) ) {
                SDL_CancelGPUCommandBuffer( cmd );
                return result::failed;
            }
            const auto capture = [&]( fp3d_iris::borrowed_scene_images *scene, bool opaque_only = false,
                                      world_pass_targets resume = {}, const fp3d_iris::scene_post *terrain = nullptr,
                                      const scene_pipelines *pipelines = nullptr ) {
                return record_world( cmd, slot.target, world, atlas, pipelines ? *pipelines : pipelines_, meshes,
                                     initial_instances, filtered ? filtered : slot.commands.get(), maps, &sky_,
                                     retain_depth, pbr_ready ? pbr_.texture() : nullptr, &unknown_background_,
                                     compute_ready ? &compute : nullptr, scene, opaque_only, resume, terrain );
            };
            bool iris_drawn = false;
            if( iris ) {
                SDL_ClearError();
                iris_drawn = record_iris_water_frame( cmd, slot.target, renderer_, material_atlas, world,
                                                     meshes, *iris, slot.iris_images, capture );
                const std::string error = iris_drawn ? "" : SDL_GetError();
                if( iris_error_ != error && !error.empty() ) {
                    SDL_LogWarn( SDL_LOG_CATEGORY_RENDER, "Iris native fallback: %s", error.c_str() );
                }
                iris_error_ = error;
            } else {
                iris_error_.clear();
            }
            const auto report_iris_post = [&]( bool shown ) {
                const std::string error = shown ? "" : SDL_GetError();
                if( iris_post_error_ != error && !error.empty() ) {
                    SDL_LogWarn( SDL_LOG_CATEGORY_RENDER, "Iris native fallback: %s", error.c_str() );
                }
                iris_post_error_ = error;
                return shown;
            };
            const float shader_time = static_cast<float>( SDL_GetTicks() / 1000.0 );
            const auto iris_frame = iris_post_ready && source ?
                                    iris_history_.prepare( source->scene, source->camera, world.camera,
                                            world.width, world.height, world.overhead, source->clock,
                                            compiled->revision, source->eye_brightness, world.lightning_flash ) :
                                    std::nullopt;
            const auto *previous_iris = iris_frame ? &slots_[current_].iris_post : nullptr;
            const bool retain_render_history = iris_frame && iris_frame->previous.has_value();
            const auto scene_uniforms = [&]() {
                if( iris_frame ) {
                    auto values = iris_scene_uniforms( *iris_frame, fp3d_camera::near_clip, fp3d_camera::far_clip );
                    values.emplace( "heldItemId", std::vector<std::int32_t>{
                        iris_post_.held_item_id( source->scene.held_item_type )
                    } );
                    return values;
                }
                return iris_camera_uniforms( world.camera, fp3d_camera::near_clip, fp3d_camera::far_clip );
            };
            const bool iris_water = iris_post_ready && iris_post_.uses_water();
            const bool iris_terrain = iris_post_ready && iris_post_.uses_opaque_geometry();
            const bool iris_shadow = iris_post_ready && iris_post_.uses_shadow();
            if( iris_water || iris_terrain || iris_shadow ) {
                SDL_ClearError();
                const auto &rules = iris_post_.properties().materials;
                const auto block = rules.find( "block.properties" );
                const fp3d_iris::material_id_map materials( block == rules.end() ?
                        std::vector<fp3d_iris::material_rule>{} : block->second );
                const auto bind_water = [&]( const fp3d_iris::shader_layout &layout,
                const fp3d_iris::uniform_values &values, fp3d_iris::graphics_stage &stage ) {
                    const auto bound = bind_iris_water_draws( world, meshes, layout, materials, values,
                                       {}, stage );
                    if( !bound.error.empty() ) {
                        return SDL_SetError( "%s", bound.error.c_str() );
                    }
                    if( !bound.missing.empty() ) {
                        return SDL_SetError( "Unbound Iris water constant: %s", bound.missing.front().c_str() );
                    }
                    return true;
                };
                const auto bind_opaque = [&]( std::string_view name, const fp3d_iris::shader_layout &layout,
                const fp3d_iris::shader_layout &fragment, const fp3d_iris::uniform_values &values,
                fp3d_iris::graphics_stage &stage ) {
                    const auto bound = bind_iris_opaque_draws( world, meshes, layout, fragment, values, stage,
                                       terrain_lights, iris_post_, name );
                    if( !bound.error.empty() ) {
                        return SDL_SetError( "%s", bound.error.c_str() );
                    }
                    if( !bound.missing.empty() ) {
                        return SDL_SetError( "Unbound Iris opaque geometry constant: %s", bound.missing.front().c_str() );
                    }
                    return true;
                };
                const auto bind_shadow = [&]( std::string_view name, const fp3d_iris::shader_layout &layout,
                const fp3d_iris::uniform_values &values, fp3d_iris::graphics_stage &stage ) {
                    fp3d_iris::virtual_material water;
                    water.shader_id = "minecraft:water";
                    water.surface = fp3d_iris::material_surface::water;
                    const auto bound = bind_iris_shadow_draws( world, meshes, layout, values, stage, iris_post_, name,
                                       materials.resolve( water ).value_or( -1 ) );
                    if( !bound.error.empty() ) {
                        return SDL_SetError( "%s", bound.error.c_str() );
                    }
                    if( !bound.missing.empty() ) {
                        return SDL_SetError( "Unbound Iris shadow constant: %s", bound.missing.front().c_str() );
                    }
                    return true;
                };
                fp3d_iris::borrowed_scene_images scene;
                const fp3d_iris::borrowed_graph_image world_atlas{ atlas,
                    { slot.target.color_format(), static_cast<Uint32>( material_atlas->w ),
                      static_cast<Uint32>( material_atlas->h ) }
                };
                const auto overlays = [&]( SDL_GPUTexture *color, SDL_GPUTexture *depth,
                SDL_GPUTextureFormat format ) {
                    if( world.opaque_batches == world.batches.size() ) {
                        return true;
                    }
                    const auto *pipelines = iris_overlay_pipelines( format );
                    return pipelines && capture( nullptr, false, { color, depth }, nullptr, pipelines );
                };
                fp3d_iris::scene_post::water_background background;
                if( world.blend_end > world.badge_end ) {
                    background = [&]( SDL_GPUTexture *color, SDL_GPUTexture *opaque_depth,
                    SDL_GPUTexture *water_depth, SDL_GPUTextureFormat format ) {
                        auto &pipeline = behind_water_[format];
                        return initialize_behind_water( device_, pipeline, format ) &&
                               capture( nullptr, false, { color, opaque_depth, water_depth, &pipeline } );
                    };
                }
                auto values = scene_uniforms();
                if( iris_shadow ) {
                    auto sun = iris_unit_sun( world.iris_sun );
                    if( sun[1] < 0.f ) {
                        for( float &component : sun ) {
                            component = -component;
                        }
                    }
                    values.merge( iris_shadow_uniforms( fp3d_gpu_camera_math::sun_shadow_clip_from_world(
                                      sun, world.iris_shadow_anchor, 96.f, iris_post_.shadow_resolution() ) ) );
                }
                iris_drawn = capture( iris_water ? &scene : nullptr, iris_water || iris_terrain, {},
                                      iris_terrain ? &iris_post_ : nullptr ) &&
                             iris_post_.record( cmd, slot.target, slot.iris_post, shader_time, std::move( values ),
                                     world_atlas, bind_water, overlays, background, bind_opaque, bind_shadow,
                                     previous_iris, retain_render_history );
                report_iris_post( iris_drawn );
            }
            if( ( !iris_drawn && !capture( nullptr ) ) ||
                ( draw_halos && !halo_.draw( cmd, slot.target, world.halos, world.halo_count ) ) ||
                ( reflectivity_mask && !record_reflectivity_mask( cmd, reflectivity_mask, slot.target.depth(), world,
                                                     atlas, meshes, initial_instances ) ) ||
                ( photon_target && !photon_.draw( cmd, slot.target, photon_target, photon_settings,
                                                 reflectivity_mask, maps.sun, maps.far_sun ) ) ) {
                SDL_CancelGPUCommandBuffer( cmd );
                return result::failed;
            }
            if( iris_post_ready && !iris_water && !iris_terrain && !iris_shadow ) {
                SDL_ClearError();
                iris_drawn = iris_post_.record( cmd, slot.target, slot.iris_post, shader_time, scene_uniforms(),
                                               {}, {}, {}, {}, {}, {}, previous_iris, retain_render_history );
                report_iris_post( iris_drawn );
            } else if( !iris_post_ready ) {
                slot.iris_post.images.reset();
                iris_post_error_.clear();
            }
            timing.stage( "gpu_submit_driver" );
            auto *submitted = SDL_SubmitGPUCommandBufferAndAcquireFence( cmd );
            timing.stage( "gpu_submit_commit" );
            if( !submitted ) {
                failed_ = true;
                hide();
                return result::failed;
            }
            if( fence_ ) {
                SDL_ReleaseGPUFence( device_, fence_ );
            }
            fence_ = submitted;
            if( iris_drawn && iris_frame ) {
                iris_history_.presented( *iris_frame );
            } else {
                iris_history_.reset();
                slot.iris_post.uniform_time.reset();
            }
            if( queue_auxiliary ) {
                auxiliary_readback_.submitted( world.material_revision, world.auxiliary_slots );
            }
            if( fp3d_regression::sampling_gpu_buffers() ) {
                std::uint64_t slot_capacity = 0;
                for( const auto &frame : slots_ ) {
                    slot_capacity += frame.mesh.buffer_capacity() +
                                     std::uint64_t( frame.instances.capacity() ) + frame.commands.capacity();
                }
                fp3d_regression::gpu_buffer_frame metrics;
                metrics.resident_upload_bytes = update_resident ? resident_mesh_.delta_bytes() : 0;
                metrics.dynamic_upload_bytes = has_dynamic ? slot.mesh.delta_bytes() : 0;
                metrics.instance_upload_bytes = dynamic_instances ? slot.instances.delta_bytes() : 0;
                metrics.resident_instance_upload_bytes = static_instances ? resident_instances_.delta_bytes() : 0;
                if( static_instances ) {
                    metrics.resident_page_upload_bytes = resident_instances_.delta_prefix_upload_bytes();
                    metrics.resident_instance_merged_gap_bytes = resident_instances_.delta_merged_gap_bytes();
                    metrics.resident_packed_instances = static_cast<unsigned>( world.resident_instances.size() );
                }
                metrics.command_upload_bytes = slot.commands.delta_bytes();
                metrics.upload_copies = static_cast<unsigned>(
                                            ( update_resident ? resident_mesh_.delta_copy_count() : 0 ) +
                                            ( has_dynamic ? slot.mesh.delta_copy_count() : 0 ) +
                                            ( dynamic_instances ? slot.instances.delta_copy_count() : 0 ) +
                                            ( static_instances ? resident_instances_.delta_copy_count() : 0 ) +
                                            slot.commands.delta_copy_count() );
                metrics.resident_buffer_capacity = resident_mesh_.buffer_capacity();
                metrics.resident_instance_capacity = resident_instances_.capacity();
                metrics.frame_slot_buffer_capacity = slot_capacity;
                metrics.instances = static_cast<unsigned>( world.instances.size() + world.resident_instance_count );
                metrics.indirect_commands = static_cast<unsigned>( world.batches.size() );
                metrics.pbr_upload_bytes = pbr_ready ? pbr_.upload_bytes() : 0;
                metrics.compute_batches = compute_ready ? static_cast<unsigned>( compute.indices.size() ) : 0;
                metrics.compute_upload_bytes = compute_uploaded ? terrain_compute_.upload_bytes() : 0;
                metrics.compute_input_reused = compute_ready && terrain_compute_.reuses_gpu_inputs();
                metrics.compute_dispatch_reused = compute_ready && terrain_compute_.reuses_dispatch();
                metrics.occlusion_candidates = filtered ? occlusion_.candidate_count() : 0;
                fp3d_regression::sample_gpu_buffers( metrics );
            }
            if( has_dynamic ) {
                slot.mesh.commit();
            }
            if( update_resident ) {
                resident_mesh_.commit();
                resident_source_ = world.resident_geometry;
            }
            if( dynamic_instances ) {
                slot.instances.commit_delta();
            }
            if( static_instances ) {
                resident_instances_.commit_delta();
            }
            slot.commands.commit_delta();
            for( size_t i = 0; i < terrain_lights.size(); ++i ) {
                if( terrain_lights[i] ) {
                    slot.terrain_lights[i].commit_delta();
                }
            }
            if( pbr_ready ) { pbr_.submitted(); }
            if( compute_uploaded ) { terrain_compute_.commit_uploads(); }
            if( compute_ready ) { terrain_compute_.commit_dispatch(); }
            slot.photon_displayed = photon_target != nullptr;
            if( !visible_ ) {
                DebugLog( D_INFO, D_MAIN ) << "fp3d: native GPU world submitted, Photon="
                                           << slot.photon_displayed;
            }
            current_ = next;
            visible_ = true;
            preparation_.submitted();
            return result::submitted;
        }

    private:
        const scene_pipelines *iris_overlay_pipelines( SDL_GPUTextureFormat format ) {
            if( format == fp3d_gpu::scene_target::color_format() ) {
                return &pipelines_;
            }
            const auto found = iris_overlays_.find( format );
            if( found != iris_overlays_.end() ) {
                return found->second.get();
            }
            auto pipelines = std::make_unique<scene_pipelines>();
            auto *vs = shader( device_, fp3d_gpu_shader_bytes::base_vertex,
                               sizeof( fp3d_gpu_shader_bytes::base_vertex ), true, 1, 0 );
            auto *fs = shader( device_, fp3d_gpu_shader_bytes::base_fragment,
                               sizeof( fp3d_gpu_shader_bytes::base_fragment ), false, 1, 6 );
            const bool ready = initialize_scene_pipelines( *pipelines, device_, vs, fs, format );
            for( auto *stage : { vs, fs } ) {
                if( stage ) {
                    SDL_ReleaseGPUShader( device_, stage );
                }
            }
            if( !ready ) {
                return nullptr;
            }
            return iris_overlays_.emplace( format, std::move( pipelines ) ).first->second.get();
        }

        bool initialize_reflectivity_mask() {
            if( !reflectivity_mask_attempted_ ) {
                reflectivity_mask_attempted_ = true;
                auto *vs = shader( device_, fp3d_gpu_shader_bytes::base_vertex,
                                   sizeof( fp3d_gpu_shader_bytes::base_vertex ), true, 1, 0 );
                auto *fs = shader( device_, fp3d_gpu_shader_bytes::reflectivity_mask_fragment,
                                   sizeof( fp3d_gpu_shader_bytes::reflectivity_mask_fragment ), false, 1, 1 );
                reflectivity_mask_ready_ = reflectivity_mask_.initialize( device_, vs, fs,
                                    fp3d_gpu::mesh_pass::reflectivity_mask );
                for( auto *stage : { vs, fs } ) {
                    if( stage ) {
                        SDL_ReleaseGPUShader( device_, stage );
                    }
                }
            }
            return reflectivity_mask_ready_;
        }

        bool record_reflectivity_mask( SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *mask,
                                SDL_GPUTexture *depth_texture, const prepared_world &world,
                                SDL_GPUTexture *atlas, const world_meshes &meshes,
                                SDL_GPUBuffer *instances ) const {
            SDL_GPUColorTargetInfo color{};
            color.texture = mask;
            color.load_op = SDL_GPU_LOADOP_CLEAR;
            color.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture = depth_texture;
            depth.load_op = SDL_GPU_LOADOP_LOAD;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            auto *pass = SDL_BeginGPURenderPass( cmd, &color, 1, &depth );
            if( !pass ) {
                return false;
            }
            bool drawn = reflectivity_mask_.bind( cmd, pass, atlas, world.camera, instances );
            bool anchored = false;
            for( const auto &item : world.reflective_batches ) {
                if( !drawn ) {
                    break;
                }
                const auto &draw = item.draws;
                if( anchored != draw.resident ) {
                    meshes.vertex_constants( cmd, world.camera, {}, draw.resident );
                    anchored = draw.resident;
                }
                SDL_PushGPUFragmentUniformData( cmd, 0, item.material.data(), sizeof( item.material ) );
                drawn = meshes.draw( pass, draw );
            }
            SDL_EndGPURenderPass( pass );
            return drawn;
        }

        bool initialize_photon() {
            if( !photon_attempted_ ) {
                photon_attempted_ = true;
                auto *vs = shader( device_, fp3d_gpu_shader_bytes::photon_vertex,
                                   sizeof( fp3d_gpu_shader_bytes::photon_vertex ), true, 0, 0 );
                auto *fs = shader( device_, fp3d_gpu_shader_bytes::photon_fragment,
                                   sizeof( fp3d_gpu_shader_bytes::photon_fragment ), false, 1, 5 );
                photon_ready_ = photon_.initialize( device_, vs, fs );
                for( auto *stage : { vs, fs } ) {
                    if( stage ) {
                        SDL_ReleaseGPUShader( device_, stage );
                    }
                }
                if( !photon_ready_ ) {
                    DebugLog( D_WARNING, D_MAIN ) << "fp3d: SDL GPU Photon postprocess unavailable";
                }
            }
            return photon_ready_;
        }

        bool initialize_halos() {
            if( !halo_attempted_ ) {
                halo_attempted_ = true;
                auto *vs = shader( device_, fp3d_gpu_shader_bytes::halo_vertex,
                                   sizeof( fp3d_gpu_shader_bytes::halo_vertex ), true, 1, 0 );
                auto *fs = shader( device_, fp3d_gpu_shader_bytes::halo_fragment,
                                   sizeof( fp3d_gpu_shader_bytes::halo_fragment ), false, 0, 1 );
                halo_ready_ = halo_.initialize( device_, vs, fs );
                for( auto *stage : { vs, fs } ) {
                    if( stage ) {
                        SDL_ReleaseGPUShader( device_, stage );
                    }
                }
                if( !halo_ready_ ) {
                    DebugLog( D_WARNING, D_MAIN ) << "fp3d: SDL GPU lamp halo pipeline unavailable";
                }
            }
            return halo_ready_;
        }

        SDL_GPUTexture *shadow_texture( SDL_GPUTexture *&texture, bool &attempted, Uint32 width,
                                        Uint32 height = 0 ) {
            if( attempted ) {
                return texture;
            }
            attempted = true;
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = fp3d_gpu::scene_target::depth_format();
            info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width = width;
            info.height = height ? height : width;
            info.layer_count_or_depth = 1;
            info.num_levels = 1;
            if( SDL_GPUTextureSupportsFormat( device_, info.format, info.type, info.usage ) &&
                initialize_shadow_caster() ) {
                texture = SDL_CreateGPUTexture( device_, &info );
            }
            if( !texture ) {
                DebugLog( D_WARNING, D_MAIN ) << "fp3d: SDL GPU shadow map unavailable at "
                                             << info.width << "x" << info.height;
            }
            return texture;
        }

        bool initialize_shadow_caster() {
            if( shadow_caster_ready_ ) {
                return true;
            }
            auto *vs = shader( device_, fp3d_gpu_shader_bytes::base_vertex,
                               sizeof( fp3d_gpu_shader_bytes::base_vertex ), true, 1, 0 );
            auto *fs = shader( device_, fp3d_gpu_shader_bytes::shadow_caster_fragment,
                               sizeof( fp3d_gpu_shader_bytes::shadow_caster_fragment ), false, 0, 1 );
            shadow_caster_ready_ = shadow_caster_.initialize( device_, vs, fs,
                                   fp3d_gpu::mesh_pass::shadow_caster );
            if( vs ) {
                SDL_ReleaseGPUShader( device_, vs );
            }
            if( fs ) {
                SDL_ReleaseGPUShader( device_, fs );
            }
            return shadow_caster_ready_;
        }

        bool initialize_pipelines() {
            if( pipelines_ready_ ) {
                return true;
            }
            auto *vs = shader( device_, fp3d_gpu_shader_bytes::base_vertex,
                               sizeof( fp3d_gpu_shader_bytes::base_vertex ), true, 1, 0 );
            auto *fs = shader( device_, fp3d_gpu_shader_bytes::base_fragment,
                               sizeof( fp3d_gpu_shader_bytes::base_fragment ), false, 1, 6 );
            auto *sky_vs = shader( device_, fp3d_gpu_shader_bytes::sky_vertex,
                                   sizeof( fp3d_gpu_shader_bytes::sky_vertex ), true, 1, 0 );
            auto *sky_fs = shader( device_, fp3d_gpu_shader_bytes::sky_fragment,
                                   sizeof( fp3d_gpu_shader_bytes::sky_fragment ), false, 1, 0 );
            pipelines_ready_ = initialize_scene_pipelines( pipelines_, device_, vs, fs ) &&
                               sky_.initialize( device_, sky_vs, sky_fs ) &&
                               unknown_background_.initialize( device_, vs, fs,
                                       fp3d_gpu::mesh_pass::unknown_background );
            for( auto *stage : { vs, fs, sky_vs, sky_fs } ) {
                if( stage ) {
                    SDL_ReleaseGPUShader( device_, stage );
                }
            }
            return pipelines_ready_;
        }

        struct frame_slot {
            fp3d_gpu::scene_target target;
            std::unique_ptr<fp3d_iris::pack_graph_images> iris_images;
            fp3d_iris::scene_post::frame_slot iris_post;
            std::unique_ptr<SDL_Texture, decltype( &SDL_DestroyTexture )> photon_output{
                nullptr, SDL_DestroyTexture
            };
            std::unique_ptr<SDL_Texture, decltype( &SDL_DestroyTexture )> reflectivity_mask{
                nullptr, SDL_DestroyTexture
            };
            bool photon_displayed = false;
            fp3d_gpu::indexed_mesh mesh;
            fp3d_gpu::upload_buffer instances;
            fp3d_gpu::upload_buffer commands;
            std::array<fp3d_gpu::upload_buffer, 2> terrain_lights;
            std::vector<SDL_GPUIndexedIndirectDrawCommand> retained_draws;
            Uint32 width = 0;
            Uint32 height = 0;
        };

        SDL_Renderer *renderer_;
        SDL_GPUDevice *device_;
        std::string iris_error_;
        std::string iris_post_error_;
        fp3d_iris::scene_post iris_post_;
        fp3d_iris::frame_history iris_history_;
        std::array<frame_slot, 2> slots_;
        fp3d_gpu::indexed_mesh resident_mesh_;
        fp3d_gpu::upload_buffer resident_instances_;
        std::shared_ptr<const fp3d_geometry::mesh> resident_source_;
        scene_pipelines pipelines_;
        fp3d_gpu::sky_pipeline sky_;
        fp3d_gpu::textured_pipeline unknown_background_;
        std::map<SDL_GPUTextureFormat, fp3d_gpu::textured_pipeline> behind_water_;
        std::map<SDL_GPUTextureFormat, std::unique_ptr<scene_pipelines>> iris_overlays_;
        fp3d_gpu::halo_pipeline halo_;
        fp3d_gpu::photon_pipeline photon_;
        fp3d_gpu::pbr_atlas pbr_;
        fp3d_gpu::auxiliary_readback auxiliary_readback_;
        terrain_compute_pass terrain_compute_;
        occlusion_pass occlusion_;
        bool terrain_compute_enabled_ = false;
        bool occlusion_enabled_ = false;
        fp3d_gpu::textured_pipeline reflectivity_mask_;
        bool reflectivity_mask_attempted_ = false;
        bool reflectivity_mask_ready_ = false;
        bool photon_attempted_ = false;
        bool photon_ready_ = false;
        bool halo_attempted_ = false;
        bool halo_ready_ = false;
        fp3d_gpu::textured_pipeline shadow_caster_;
        fp3d_gpu::shadow_maps shadow_maps_;
        bool beam_attempted_ = false;
        bool sun_attempted_ = false;
        bool far_sun_attempted_ = false;
        bool point_attempted_ = false;
        bool shadow_caster_ready_ = false;
        SDL_GPUFence *fence_ = nullptr;
        fp3d_gpu::frame_preparation preparation_;
        unsigned current_ = 0;
        bool visible_ = false;
        bool pipelines_ready_ = false;
        bool failed_ = false;
};

// Called ONLY after the normal window SDL_RenderPresent has submitted the
// SDL atlas writes. SDL_FlushRenderer alone is insufficient on GPU backend.
inline bool render_after_present( SDL_Renderer *renderer, const prepared_world &world,
                                  const char *path,
                                  std::unique_ptr<fp3d_gpu::scene_target> *preview = nullptr )
{
    auto *device = SDL_GetGPURendererDevice( renderer );
    if( !device || !( SDL_GetGPUShaderFormats( device ) & SDL_GPU_SHADERFORMAT_DXBC ) ||
        world.material_revision != fp3d_materials::revision() ) {
        return SDL_SetError( "FP3D QA device or atlas revision mismatch" );
    }
    auto *atlas = fp3d_gpu::native_atlas( renderer, fp3d_materials::gpu_colour_atlas( renderer ) );
    if( !atlas ) return false;
    const Uint32 width = world.width;
    const Uint32 height = world.height;
    // D3D12 buffer-to-texture copies require a 256-byte row pitch.
    const Uint32 row_pixels = ( width + 63 ) & ~Uint32( 63 );
    resources r( device );
    if( !r.target->initialize( renderer, width, height ) ) return false;
    r.vs = shader( device, fp3d_gpu_shader_bytes::base_vertex,
                   sizeof( fp3d_gpu_shader_bytes::base_vertex ), true, 1, 0 );
    r.fs = shader( device, fp3d_gpu_shader_bytes::base_fragment,
                   sizeof( fp3d_gpu_shader_bytes::base_fragment ), false, 1, 6 );
    if( !initialize_scene_pipelines( r.pipelines, device, r.vs, r.fs ) ||
        !r.mesh.initialize( device, world.geometry ) ) return false;
    if( !stage_instances( device, world.instances, r.instances, false ) ||
        !stage_draws( device, world, r.draw_commands ) ) {
        return false;
    }
    const auto instance_bytes = world.instances.size() * sizeof( fp3d_gpu::mesh_instance );
    SDL_GPUTransferBufferCreateInfo staging{};
    staging.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    staging.size = row_pixels * height * 4;
    r.readback = SDL_CreateGPUTransferBuffer( device, &staging );
    if( !r.readback ) return false;
    auto *cmd = SDL_AcquireGPUCommandBuffer( device );
    if( !cmd ) return false;
    auto *copy = SDL_BeginGPUCopyPass( cmd );
    if( !copy ) { SDL_CancelGPUCommandBuffer( cmd ); return false; }
    const bool uploaded = r.mesh.record( copy ) && r.instances.record( copy ) &&
                          r.draw_commands.record( copy );
    SDL_EndGPUCopyPass( copy );
    if( !uploaded ) { SDL_CancelGPUCommandBuffer( cmd ); return false; }
    if( !record_world( cmd, *r.target, world, atlas, r.pipelines,
                       world_meshes{ r.mesh }, r.instances.get(), r.draw_commands.get() ) ) {
        SDL_CancelGPUCommandBuffer( cmd );
        return false;
    }
    const unsigned indirect_calls = ( world.opaque_batches > 0 ) +
                                    ( world.badge_end > world.opaque_batches ) +
                                    ( world.blend_end > world.badge_end ) +
                                    ( world.batches.size() > world.blend_end );
    copy = SDL_BeginGPUCopyPass( cmd );
    if( !copy ) { SDL_CancelGPUCommandBuffer( cmd ); return false; }
    SDL_GPUTextureRegion src{};
    src.texture = r.target->native_color();
    src.w = width;
    src.h = height;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = r.readback;
    dst.pixels_per_row = row_pixels;
    dst.rows_per_layer = height;
    SDL_DownloadFromGPUTexture( copy, &src, &dst );
    SDL_EndGPUCopyPass( copy );
    r.fence = SDL_SubmitGPUCommandBufferAndAcquireFence( cmd );
    if( !r.fence || !SDL_WaitForGPUFences( device, true, &r.fence, 1 ) ) return false;
    void *pixels = SDL_MapGPUTransferBuffer( device, r.readback, false );
    if( !pixels ) return false;
    // Explicit QA check: reject a blank clear-only frame before saving.
    const auto *rgba = static_cast<const Uint8 *>( pixels );
    unsigned changed = 0;
    for( Uint32 y = 0; y < height; ++y ) {
        for( Uint32 x = 0; x < width; ++x ) {
            const Uint32 i = 4 * ( y * row_pixels + x );
            if( rgba[i] != 10 || rgba[i + 1] != 13 || rgba[i + 2] != 15 ) {
                ++changed;
            }
        }
    }
    std::unique_ptr<SDL_Surface, decltype( &SDL_DestroySurface )> image(
        SDL_CreateSurfaceFrom( width, height, SDL_PIXELFORMAT_RGBA32, pixels, row_pixels * 4 ),
        SDL_DestroySurface );
    const bool saved = image && changed > 100 && changed < width * height &&
                       SDL_SaveBMP( image.get(), path );
    image.reset();
    SDL_UnmapGPUTransferBuffer( device, r.readback );
    DebugLog( D_INFO, D_MAIN ) << "fp3d: GPU_WORLD_QA saved=" << saved
                              << ", floors=" << world.floor_count
                              << ", walls=" << world.wall_count
                              << ", uncapped_walls=" << world.uncapped_walls
                              << ", batches=" << world.batches.size()
                              << ", indirect_calls=" << indirect_calls
                              << ", ramps=" << world.ramp_count
                              << ", doors=" << world.door_count
                              << ", windows=" << world.window_count
                              << ", plants=" << world.plant_count
                              << ", stairs=" << world.stair_count
                              << ", furniture=" << world.furniture_count
                              << ", wheels=" << world.vehicle_geometry.wheels
                              << ", modular_parts=" << world.vehicle_geometry.modular_parts
                              << ", connected_shells=" << world.vehicle_geometry.connected_shells
                              << ", sprites=" << world.sprite_count
                              << ", badges=" << world.badge_count
                              << ", panes=" << world.pane_count
                              << ", vehicle_panes=" << world.vehicle_pane_count
                              << ", ground_layers=" << world.ground_layer_count
                              << ", animated_fields=" << world.animated_field_count
                              << ", missing=" << world.missing
                              << ", vertices=" << world.geometry.vertices.size()
                              << ", indices=" << world.geometry.indices.size()
                              << ", instances=" << world.instances.size()
                              << ", instance_bytes=" << instance_bytes
                              << ", overhead=" << world.overhead
                              << ", nonclear_pixels=" << changed;
    if( saved && preview ) {
        *preview = std::move( r.target );
    }
    return saved;
}
} // namespace fp3d_gpu_world_qa
#endif
