#pragma once
#ifndef CATA_SRC_FP3D_GPU_PRESENTATION_H
#define CATA_SRC_FP3D_GPU_PRESENTATION_H

#include <optional>

namespace fp3d_gpu
{
enum class submission_result { submitted, skipped, failed };

enum class preparation_mode { defer, refresh_materials, reuse_materials };

// One speculative CPU preparation per native submission. When the GPU is
// busy, only a snapshot whose material slots are already valid may be built.
class frame_preparation
{
    public:
        preparation_mode choose( bool ready, bool materials_current ) {
            if( ready ) {
                return preparation_mode::refresh_materials;
            }
            if( !materials_current || attempted_ ) {
                return preparation_mode::defer;
            }
            attempted_ = true;
            return preparation_mode::reuse_materials;
        }
        void submitted() {
            attempted_ = false;
        }

    private:
        bool attempted_ = false;
};

// SDL composites the previous native submission before queuing the next one.
// Keep input on the last successful presentation; HUD drawing can use the
// frame actually copied into the SDL target for the impending presentation.
template<typename View>
class frame_presentation
{
    public:
        void submit( const View &view ) {
            submitted_ = view;
        }
        void begin_composition() {
            composing_ = true;
            composed_.reset();
        }
        void world_composed() {
            composed_ = submitted_;
        }
        void finish_present( bool succeeded ) {
            if( succeeded ) {
                presented_ = composed_;
            }
            composing_ = false;
            composed_.reset();
        }
        const std::optional<View> &target_view() const {
            return composing_ ? composed_ : presented_;
        }
        const std::optional<View> &submitted_view() const {
            return submitted_;
        }
        void clear() {
            submitted_.reset();
            composed_.reset();
            presented_.reset();
            composing_ = false;
        }

    private:
        std::optional<View> submitted_;
        std::optional<View> composed_;
        std::optional<View> presented_;
        bool composing_ = false;
};
} // namespace fp3d_gpu

#endif // CATA_SRC_FP3D_GPU_PRESENTATION_H
