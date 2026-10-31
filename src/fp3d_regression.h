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
        turn_probe();
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
}
#endif
