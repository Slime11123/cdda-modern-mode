#pragma once
#ifndef CATA_SRC_FP3D_IRIS_RENDER_GRAPH_H
#define CATA_SRC_FP3D_IRIS_RENDER_GRAPH_H

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fp3d_iris
{
enum class pass_kind { fullscreen, compute, geometry };

struct graph_viewport {
    float scale;
    float x;
    float y;
};

struct graph_pass {
    std::string program;
    std::vector<std::string> reads;
    // Each write names a new logical version. A pass can sample the previous
    // version of an attachment while writing its next version.
    std::vector<std::string> writes;
    std::string depth_write{};
    pass_kind kind = pass_kind::fullscreen;
    // Destination color version -> previous contents to load before drawing.
    std::map<std::string, std::string> color_loads{};
    std::optional<graph_viewport> viewport{};
    // Inherit an earlier depth image before writing this stage's depth version.
    std::string depth_load{};
    std::vector<std::string> vertex_reads{};
    // Color version retained for native draws immediately around this pass.
    std::string native_color{};
    // Logical sampler aliases which borrow a snapshot of a different buffer.
    std::map<std::string, std::string> sampler_aliases{};
};

inline std::vector<std::string> graph_outputs( const graph_pass &pass )
{
    auto result = pass.writes;
    if( !pass.depth_write.empty() ) {
        result.push_back( pass.depth_write );
    }
    return result;
}

struct graph_validation {
    std::string error;
    // The last pass that uses each image. Frame outputs remain live through
    // presentation, which follows the last pass.
    std::map<std::string, size_t> last_use;
};

inline graph_validation validate_graph( const std::vector<std::string> &external,
                                        const std::vector<graph_pass> &passes,
                                        const std::string &frame_output )
{
    std::set<std::string> images;
    std::set<std::string> programs;
    graph_validation result;
    for( const auto &image : external ) {
        if( image.empty() || !images.insert( image ).second ) {
            return { "Invalid external image: " + image, {} };
        }
    }
    for( size_t index = 0; index < passes.size(); ++index ) {
        const auto &pass = passes[index];
        if( pass.program.empty() || !programs.insert( pass.program ).second ) {
            return { "Invalid program: " + pass.program, {} };
        }
        if( pass.kind == pass_kind::compute && !pass.vertex_reads.empty() ) {
            return { pass.program + " has vertex inputs on a compute stage", {} };
        }
        for( const auto *reads : { &pass.reads, &pass.vertex_reads } ) {
            for( const auto &image : *reads ) {
                if( images.count( image ) == 0 ) {
                    return { pass.program + " reads unavailable image: " + image, {} };
                }
                result.last_use[image] = index;
            }
        }
        if( !pass.native_color.empty() ) {
            if( images.count( pass.native_color ) == 0 ) {
                return { pass.program + " needs unavailable native color: " + pass.native_color, {} };
            }
            result.last_use[pass.native_color] = index;
        }
        for( const auto &[destination, source] : pass.color_loads ) {
            if( pass.kind == pass_kind::compute ||
                std::find( pass.writes.begin(), pass.writes.end(), destination ) == pass.writes.end() ||
                images.count( source ) == 0 ) {
                return { pass.program + " has invalid color load: " + destination + " from " + source, {} };
            }
            result.last_use[source] = index;
        }
        if( !pass.depth_load.empty() ) {
            if( pass.kind == pass_kind::compute || pass.depth_write.empty() ||
                !images.count( pass.depth_load ) ) {
                return { pass.program + " has invalid depth load: " + pass.depth_load, {} };
            }
            result.last_use[pass.depth_load] = index;
        }
        for( const auto &image : graph_outputs( pass ) ) {
            if( image.empty() || !images.insert( image ).second ) {
                return { pass.program + " overwrites an existing image version: " + image, {} };
            }
            result.last_use[image] = index;
        }
    }
    if( images.count( frame_output ) == 0 ) {
        return { "Unavailable frame output: " + frame_output, {} };
    }
    result.last_use[frame_output] = passes.size();
    return result;
}
} // namespace fp3d_iris

#endif
