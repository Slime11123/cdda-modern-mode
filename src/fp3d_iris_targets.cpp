#include "fp3d_iris_targets.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <charconv>
#include <utility>

namespace fp3d_iris
{
namespace
{
std::string_view trim( std::string_view text )
{
    constexpr std::string_view space = " \t\r\n";
    const auto first = text.find_first_not_of( space );
    if( first == text.npos ) {
        return {};
    }
    return text.substr( first, text.find_last_not_of( space ) - first + 1 );
}

std::string color_name( color_buffer_bank bank, unsigned slot )
{
    const char *prefix = bank == color_buffer_bank::scene ? "colortex" : "shadowcolor";
    return prefix + std::to_string( slot );
}

std::string versioned( const std::string &image, unsigned version )
{
    return image + ":" + std::to_string( version );
}

render_targets parse_targets( std::string_view text, bool legacy )
{
    render_targets result;
    do {
        text = trim( text );
        const auto separator = legacy ? 1 : text.find( ',' );
        const auto value = trim( text.substr( 0, separator ) );
        unsigned index = 0;
        if( value.empty() ) {
            return { {}, "Empty render target index" };
        }
        const auto end = value.data() + value.size();
        const auto conversion = std::from_chars( value.data(), end, index );
        if( conversion.ec != std::errc{} || conversion.ptr != end || index >= 32 ) {
            return { {}, "Invalid render target index" };
        }
        if( std::find( result.slots.begin(), result.slots.end(), index ) != result.slots.end() ) {
            return { {}, "Duplicate render target index" };
        }
        result.slots.push_back( index );
        if( result.slots.size() > 8 ) {
            return { {}, "Render target count exceeds eight color attachments" };
        }
        if( separator >= text.size() ) {
            break;
        }
        text.remove_prefix( separator + ( legacy ? 0 : 1 ) );
        if( !legacy && trim( text ).empty() ) {
            return { {}, "Empty render target index" };
        }
    } while( !text.empty() );
    return result;
}
} // namespace

render_targets read_render_targets( std::string_view fragment, target_source source )
{
    render_targets result;
    std::string_view active_value;
    bool active_legacy = false;
    bool has_active_directive = false;
    while( !fragment.empty() ) {
        const auto next = fragment.find_first_of( "/\"" );
        if( next == fragment.npos ) {
            break;
        }
        fragment.remove_prefix( next );
        if( fragment.front() == '"' ) {
            // Quoted file names in generated #line directives can contain
            // slashes. Never interpret them as a comment.
            fragment.remove_prefix( 1 );
            while( !fragment.empty() ) {
                const char ch = fragment.front();
                fragment.remove_prefix( 1 );
                if( ch == '"' ) {
                    break;
                }
                if( ch == '\\' && !fragment.empty() ) {
                    fragment.remove_prefix( 1 );
                }
            }
        } else if( fragment.substr( 0, 2 ) == "//" ) {
            const auto line = fragment.find( '\n' );
            if( line == fragment.npos ) {
                break;
            }
            fragment.remove_prefix( line + 1 );
        } else if( fragment.substr( 0, 2 ) == "/*" ) {
            const auto end = fragment.find( "*/", 2 );
            if( end == fragment.npos ) {
                return { {}, "Unterminated block comment in fragment source" };
            }
            const auto comment = trim( fragment.substr( 2, end - 2 ) );
            fragment.remove_prefix( end + 2 );
            const auto colon = comment.find( ':' );
            const auto key = trim( comment.substr( 0, colon ) );
            const bool legacy = key == "DRAWBUFFERS";
            if( colon == comment.npos || ( !legacy && key != "RENDERTARGETS" ) ) {
                continue;
            }
            if( source == target_source::preprocessed ) {
                // Iris applies the last active declaration across both formats.
                active_value = comment.substr( colon + 1 );
                active_legacy = legacy;
                has_active_directive = true;
                continue;
            }
            auto current = parse_targets( comment.substr( colon + 1 ), legacy );
            if( !current.error.empty() ) {
                return current;
            }
            if( !result.slots.empty() && result.slots != current.slots ) {
                return { {}, "Conflicting render target declarations require preprocessing" };
            }
            result = std::move( current );
        } else {
            fragment.remove_prefix( 1 );
        }
    }
    return has_active_directive ? parse_targets( active_value, active_legacy ) : result;
}

std::vector<std::string> graph_color_outputs( const render_targets &targets,
        color_buffer_bank bank, unsigned version )
{
    std::vector<std::string> outputs;
    if( !targets.error.empty() ) {
        return outputs;
    }
    for( const auto slot : targets.slots ) {
        outputs.push_back( versioned( color_name( bank, slot ), version ) );
    }
    return outputs;
}

pack_fullscreen_graph build_pack_fullscreen_graph(
    const std::vector<std::string> &initial_images,
    const std::vector<pack_fullscreen_program> &programs, const std::string &final_image,
    const pack_buffer_flips &flips, const std::vector<std::string> &initial_alternates,
    const std::map<std::string, graph_viewport> &viewports )
{
    struct buffer_versions {
        std::string current;
        std::string alternate;
        unsigned writes = 0;
    };
    pack_fullscreen_graph result;
    std::map<std::string, buffer_versions> versions;
    const pack_buffer_flips::mapped_type no_flips;
    for( const auto &image : initial_images ) {
        if( image.empty() || image.find( ':' ) != image.npos || versions.count( image ) ) {
            return { {}, {}, {}, "Invalid initial pack image: " + image };
        }
        auto &buffer = versions[image];
        buffer.current = versioned( image, 0 );
        result.external.push_back( buffer.current );
    }
    for( const auto &image : initial_alternates ) {
        const auto buffer = versions.find( image );
        if( buffer == versions.end() || !buffer->second.alternate.empty() ) {
            return { {}, {}, {}, "Invalid alternate pack image: " + image };
        }
        buffer->second.alternate = image + ":alt";
        result.external.push_back( buffer->second.alternate );
    }
    for( const auto &program : programs ) {
        if( program.kind == pass_kind::compute ||
            ( program.kind == pass_kind::geometry &&
              ( flips.count( program.name ) || viewports.count( program.name ) ) ) ) {
            return { {}, {}, {}, program.name + " has unsupported graphics stage options" };
        }
        auto targets = read_render_targets( program.preprocessed_fragment, target_source::preprocessed );
        if( !targets.error.empty() ) {
            return { {}, {}, {}, program.name + ": " + targets.error };
        }
        if( targets.slots.empty() ) {
            return { {}, {}, {}, program.name + " requires an explicit color target layout" };
        }
        graph_pass pass{ program.name, {}, {} };
        pass.kind = program.kind;
        if( const auto viewport = viewports.find( program.name ); viewport != viewports.end() ) {
            pass.viewport = viewport->second;
        }
        for( const auto &sampler : program.samplers ) {
            const auto image = versions.find( sampler );
            if( image == versions.end() || image->second.current.empty() ) {
                return { {}, {}, {}, program.name + " samples unavailable pack image: " + sampler };
            }
            pass.reads.push_back( image->second.current );
        }
        const auto overrides = flips.find( program.name );
        const auto &stage_flips = overrides == flips.end() ? no_flips : overrides->second;
        for( const auto slot : targets.slots ) {
            const auto image = color_name( program.bank, slot );
            auto &buffer = versions[image];
            auto write = versioned( image, ++buffer.writes );
            auto &target = program.kind == pass_kind::geometry ? buffer.current : buffer.alternate;
            if( !target.empty() ) {
                pass.color_loads.emplace( write, target );
            }
            target = std::move( write );
            pass.writes.push_back( target );
            const auto flip = stage_flips.find( image );
            if( program.kind == pass_kind::fullscreen && ( flip == stage_flips.end() || flip->second ) ) {
                std::swap( buffer.current, buffer.alternate );
            }
        }
        // Iris applies explicit true after the default swaps, including for
        // attachments not touched by this pass.
        for( const auto &[image, flip] : stage_flips ) {
            if( flip ) {
                auto &buffer = versions[image];
                std::swap( buffer.current, buffer.alternate );
            }
        }
        if( !program.depth_buffer.empty() ) {
            if( program.kind != pass_kind::geometry ||
                program.depth_buffer.find( ':' ) != std::string::npos ||
                program.depth_buffer.compare( 0, 8, "colortex" ) == 0 ||
                program.depth_buffer.compare( 0, 11, "shadowcolor" ) == 0 ) {
                return { {}, {}, {}, program.name + " has invalid depth buffer: " + program.depth_buffer };
            }
            auto &buffer = versions[program.depth_buffer];
            pass.depth_load = buffer.current;
            pass.depth_write = versioned( program.depth_buffer, ++buffer.writes );
            buffer.current = pass.depth_write;
        }
        result.passes.push_back( std::move( pass ) );
    }
    const auto output = versions.find( final_image );
    if( output == versions.end() || output->second.current.empty() ) {
        return { {}, {}, {}, "Unavailable final pack image: " + final_image };
    }
    result.output = output->second.current;
    const auto validation = validate_graph( result.external, result.passes, result.output );
    if( !validation.error.empty() ) {
        return { {}, {}, {}, validation.error };
    }
    return result;
}
} // namespace fp3d_iris
#endif
