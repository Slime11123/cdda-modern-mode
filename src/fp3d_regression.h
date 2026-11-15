#pragma once
#ifndef CATA_SRC_FP3D_REGRESSION_H
#define CATA_SRC_FP3D_REGRESSION_H
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
}
#endif
