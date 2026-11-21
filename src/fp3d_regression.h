#pragma once
#ifndef CATA_SRC_FP3D_REGRESSION_H
#define CATA_SRC_FP3D_REGRESSION_H
#include <cstdint>
struct SDL_Renderer;
namespace fp3d_regression
{
// Default-off QA timing; observes native stages without driving the world.
class turn_probe
{
    public:
        explicit turn_probe( const char *initial_stage = "setup" );
        ~turn_probe();
        void stage( const char *name );
    private:
        unsigned long long started = 0;
        const char *current = "setup";
};
// Explicitly enabled only for an isolated interactive QA run.
void tick();
// Loading redraws are not a playable action boundary.
void action_boundary();
// Native dispatch ID, independent of calendar::turn (several actions may share it).
void action_started();
// After native world/body/weather updates, before the next action redraw.
void native_turn_finished();
// Opt-in QA sampling of actual presents and native snapshot costs.
void presented( SDL_Renderer *renderer );
void snapshot_captured( double milliseconds );
void snapshot_costs( double detail_ms, double distant_ms, double perception_ms, double cells_ms );
// Full capture cost including connect_stairs() and the weather column sort, and
// that tail alone. snapshot_costs() stays the inner breakdown.
void snapshot_full_capture( double full_ms, double tail_ms );
// Whole prepare_view(): its phases, and how many extra storeys it prepared.
void prepare_costs( double total_ms, double player_layer_ms, double extra_layers_ms,
                    double visibility_ms, int extra_layer_calls );
// One published snapshot generation, and the per-generation materials pass.
void publish_called();
void material_prepare( double milliseconds );
// One scene::draw() pass: its CPU time and the incremental work it had to do.
void scene_draw( double cpu_ms, int indexed_cells, int chunks, int rebuilt, int landscape_chunks,
                 int landscape_rebuilt );
// Building-geometry detail of one scene::draw(): the static index/regroup block,
// the chunk signature and display-list pass, and why chunk lists were discarded
// (forced by a new geometry generation versus a changed chunk signature).
void scene_geometry( double index_ms, double chunk_pass_ms, int forced_rebuilds,
                     int changed_rebuilds, int visible_vehicles );
// One named slice of an overlay pass (a HUD panel, the entry overlay), reported
// as stage_<name> so a per-frame overlay cost can be attributed. Sampled only
// while FRAME_SELFTEST is on.
void stage_sample( const char *name, double milliseconds );
// CPU-side upload and capacity counters for accepted native submissions.
struct gpu_buffer_frame {
    std::uint64_t resident_upload_bytes = 0;
    std::uint64_t dynamic_upload_bytes = 0;
    unsigned instance_upload_bytes = 0;
    unsigned resident_instance_upload_bytes = 0;
    unsigned command_upload_bytes = 0;
    unsigned upload_copies = 0;
    std::uint64_t resident_buffer_capacity = 0;
    unsigned resident_instance_capacity = 0;
    std::uint64_t frame_slot_buffer_capacity = 0;
    unsigned instances = 0;
    unsigned indirect_commands = 0;
    unsigned pbr_upload_bytes = 0;
    unsigned compute_batches = 0;
    unsigned compute_upload_bytes = 0;
    bool compute_input_reused = false;
    bool compute_dispatch_reused = false;
    unsigned occlusion_candidates = 0;
};
bool sampling_gpu_buffers();
void sample_gpu_buffers( const gpu_buffer_frame &frame );
// Default-off timestamp queries. Results are polled on later frames, never waited on.
class gpu_probe
{
    public:
        explicit gpu_probe( const char *name );
        ~gpu_probe();
        gpu_probe( const gpu_probe & ) = delete;
        gpu_probe &operator=( const gpu_probe & ) = delete;
        void finish();
    private:
        int slot = -1;
};
void gpu_collect();
// Call before destroying the current compositor GL context.
void gpu_release();
}
#endif
