#include "fp3d_iris_layout.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

#include "json.h"
#include "json_loader.h"
#include "fp3d_camera.h"
#include "fp3d_iris_frame.h"
#include "fp3d_iris_properties.h"
#include "fp3d_iris_targets.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace fp3d_iris
{
namespace
{
std::string read_artifact( const std::filesystem::path &path, std::streamoff limit )
{
    std::ifstream file( path, std::ios::binary | std::ios::ate );
    const auto size = file.tellg();
    if( size <= 0 || size > limit ) {
        throw std::runtime_error( "Missing, empty or oversized Iris artifact: " + path.u8string() );
    }
    std::string data( static_cast<size_t>( size ), '\0' );
    if( !file.seekg( 0 ) || !file.read( data.data(), size ) || file.peek() != EOF ) {
        throw std::runtime_error( "Iris artifact changed or could not be read: " + path.u8string() );
    }
    return data;
}

std::string artifact_sha256( std::string &bytes )
{
#ifdef _WIN32
    // Resolve from System32 on demand to keep the base game independent of
    // the Windows version required by the D3D12 shader artifact loader.
    struct hash_provider {
        HMODULE library = LoadLibraryExW( L"bcrypt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
        decltype( &BCryptHash ) hash = nullptr;
        hash_provider() {
            if( library ) {
                const auto proc = GetProcAddress( library, "BCryptHash" );
                static_assert( sizeof( proc ) == sizeof( hash ) );
                std::memcpy( &hash, &proc, sizeof( hash ) );
            }
        }
        ~hash_provider() {
            if( library ) {
                FreeLibrary( library );
            }
        }
    };
    static const hash_provider provider;
    std::array<unsigned char, 32> digest{};
    if( !provider.hash || provider.hash( BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                        reinterpret_cast<PUCHAR>( bytes.data() ),
                                        static_cast<ULONG>( bytes.size() ), digest.data(), digest.size() ) < 0 ) {
        throw std::runtime_error( "Windows SHA-256 provider unavailable" );
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve( digest.size() * 2 );
    for( const auto byte : digest ) {
        result.push_back( hex[byte >> 4] );
        result.push_back( hex[byte & 15] );
    }
    return result;
#else
    static_cast<void>( bytes );
    throw std::runtime_error( "Verified Iris DXIL/DXBC loading requires Windows" );
#endif
}

uniform_member read_uniform( const JsonObject &field )
{
    uniform_member result{ uniform_scalar::floating, 0, 1, 1, 0 };
    const int offset = field.get_int( "offset" );
    if( offset < 0 || offset % 4 != 0 || field.has_member( "array" ) ||
        field.get_bool( "row_major", false ) ) {
        throw std::runtime_error( "Unsupported shader uniform storage" );
    }
    result.offset = static_cast<unsigned>( offset );
    std::string type = field.get_string( "type" );
    if( type == "int" || type.compare( 0, 4, "ivec" ) == 0 ) {
        result.scalar = uniform_scalar::signed_integer;
    } else if( type == "uint" || type.compare( 0, 4, "uvec" ) == 0 ) {
        result.scalar = uniform_scalar::unsigned_integer;
    }
    if( result.scalar != uniform_scalar::floating && type.size() == 5 ) {
        type.erase( 0, 1 );
    }
    if( type.size() == 4 && type.back() >= '2' && type.back() <= '4' ) {
        result.rows = static_cast<unsigned>( type.back() - '0' );
        if( type.compare( 0, 3, "vec" ) == 0 ) {
            return result;
        }
        if( type.compare( 0, 3, "mat" ) == 0 && field.get_int( "matrix_stride" ) == 16 ) {
            result.columns = result.rows;
            result.matrix_stride = 16;
            return result;
        }
    }
    if( type != "float" && type != "int" && type != "uint" ) {
        throw std::runtime_error( "Unsupported shader uniform type: " + type );
    }
    return result;
}

bool supported_sampler( std::string type )
{
    const bool integer = type.compare( 0, 8, "isampler" ) == 0 ||
                         type.compare( 0, 8, "usampler" ) == 0;
    if( integer ) {
        type.erase( 0, 1 );
    }
    return type == "sampler2D" || type == "sampler2DArray" || type == "sampler3D" ||
           type == "samplerCube" || ( !integer &&
                   ( type == "sampler2DShadow" || type == "sampler2DArrayShadow" ||
                     type == "samplerCubeShadow" ) );
}
} // namespace

shader_layout parse_shader_layout( std::string json )
{
    try {
        const auto root = json_loader::from_string( std::move( json ) );
        const JsonObject object = root.get_object();
        object.allow_omitted_members();
        shader_layout result;
        result.stage = object.get_string( "stage" );
        result.format = object.get_string( "format" );
        result.bytecode_sha256 = object.get_string( "bytecode_sha256" );
        const auto valid_digest = []( std::string_view digest ) {
            return digest.size() == 64 && digest.find_first_not_of( "0123456789abcdef" ) == digest.npos;
        };
        if( object.get_int( "version" ) != 1 ||
            ( result.stage != "vert" && result.stage != "frag" ) ||
            ( result.format != "dxbc" && result.format != "dxil" ) ||
            !valid_digest( result.bytecode_sha256 ) ) {
            throw std::runtime_error( "Unsupported Iris shader layout header" );
        }
        if( object.has_member( "cache_key" ) ) {
            result.cache_key = object.get_string( "cache_key" );
            if( !valid_digest( result.cache_key ) ) {
                throw std::runtime_error( "Invalid Iris shader cache key" );
            }
        }
        for( const JsonObject slot : object.get_array( "samplers" ) ) {
            sampler_slot sampler{ slot.get_string( "name" ), slot.get_string( "type" ),
                                 slot.get_bool( "readonly_texture", false ) };
            if( slot.get_int( "slot" ) != static_cast<int>( result.samplers.size() ) ||
                sampler.name.empty() || !supported_sampler( sampler.type ) ||
                ( sampler.readonly_texture && ( result.stage != "frag" || sampler.type != "sampler2D" ) ) ||
                ( !sampler.readonly_texture && !result.samplers.empty() && result.samplers.back().readonly_texture ) ||
                std::any_of( result.samplers.begin(), result.samplers.end(), [&]( const auto &entry ) {
                    return entry.name == sampler.name;
                } ) ) {
                throw std::runtime_error( "Invalid Iris sampler layout" );
            }
            result.samplers.push_back( std::move( sampler ) );
        }
        const auto locations = [&]( const char *name, std::vector<unsigned> &values ) {
            if( !object.has_member( name ) ) {
                return;
            }
            for( const JsonObject entry : object.get_array( name ) ) {
                entry.allow_omitted_members();
                const int index = entry.get_int( "location" );
                if( index < 0 || index >= 32 || ( !values.empty() && static_cast<unsigned>( index ) <= values.back() ) ) {
                    throw std::runtime_error( "Invalid Iris stage locations: " + std::string( name ) );
                }
                values.push_back( static_cast<unsigned>( index ) );
            }
        };
        locations( "inputs", result.input_locations );
        locations( "outputs", result.output_locations );
        if( object.has_member( "target_directive" ) ) {
            result.target_directive = object.get_string( "target_directive" );
            if( result.stage != "frag" || result.target_directive->size() > 512 ) {
                throw std::runtime_error( "Invalid Iris target directive metadata" );
            }
        }
        if( object.has_member( "shadow_resolution" ) ) {
            const int resolution = object.get_int( "shadow_resolution" );
            if( resolution <= 0 || resolution > 16384 ) {
                throw std::runtime_error( "Invalid Iris shadow map resolution" );
            }
            result.shadow_resolution = static_cast<unsigned>( resolution );
        }
        if( object.has_member( "noise_resolution" ) ) {
            const int resolution = object.get_int( "noise_resolution" );
            if( resolution <= 0 || resolution > 2048 ) {
                throw std::runtime_error( "Invalid Iris noise texture resolution" );
            }
            result.noise_resolution = static_cast<unsigned>( resolution );
        }
        const auto read_half_life = [&]( const char *key ) -> std::optional<float> {
            if( !object.has_member( key ) ) {
                return std::nullopt;
            }
            const double value = object.get_float( key );
            if( !std::isfinite( value ) || value < 0 || value > std::numeric_limits<float>::max() ) {
                throw std::runtime_error( std::string( "Invalid Iris half-life: " ) + key );
            }
            return static_cast<float>( value );
        };
        result.wetness_half_life = read_half_life( "wetness_half_life" );
        result.dryness_half_life = read_half_life( "dryness_half_life" );
        if( object.has_member( "buffer_formats" ) ) {
            for( const JsonObject format : object.get_array( "buffer_formats" ) ) {
                const auto name = format.get_string( "image" );
                const auto value = format.get_string( "format" );
                if( name.empty() || value.empty() || !result.buffer_formats.emplace( name, value ).second ) {
                    throw std::runtime_error( "Invalid or duplicate Iris buffer format declaration" );
                }
            }
        }
        if( object.has_member( "buffer_clears" ) ) {
            for( const JsonObject entry : object.get_array( "buffer_clears" ) ) {
                const auto name = entry.get_string( "image" );
                if( name.empty() || !result.buffer_clears.emplace( name, entry.get_bool( "enabled" ) ).second ) {
                    throw std::runtime_error( "Invalid or duplicate Iris buffer clear declaration" );
                }
            }
        }
        if( object.has_member( "buffer_clear_colors" ) ) {
            for( const JsonObject entry : object.get_array( "buffer_clear_colors" ) ) {
                const auto name = entry.get_string( "image" );
                const auto components = entry.get_array( "color" );
                if( name.empty() || components.size() != 4 ) {
                    throw std::runtime_error( "Invalid Iris buffer clear color" );
                }
                std::array<float, 4> color{};
                for( size_t axis = 0; axis < color.size(); ++axis ) {
                    color[axis] = components.get_float( axis );
                    if( !std::isfinite( color[axis] ) ) {
                        throw std::runtime_error( "Invalid Iris buffer clear component" );
                    }
                }
                if( !result.buffer_clear_colors.emplace( name, color ).second ) {
                    throw std::runtime_error( "Duplicate Iris buffer clear color" );
                }
            }
        }
        if( object.has_null( "frame" ) ) {
            return result;
        }
        const JsonObject frame = object.get_object( "frame" );
        const int size = frame.get_int( "size" );
        if( size < 0 || size % 16 != 0 || size > 65536 ) {
            throw std::runtime_error( "Invalid Iris frame block size" );
        }
        result.frame_size = static_cast<unsigned>( size );
        std::vector<bool> occupied( result.frame_size / 4 );
        for( const JsonObject field : frame.get_array( "members" ) ) {
            field.allow_omitted_members();
            const std::string name = field.get_string( "name" );
            auto member = read_uniform( field );
            if( field.has_member( "default" ) ) {
                if( member.rows != 1 || member.columns != 1 ) {
                    throw std::runtime_error( "Nonscalar Iris uniform default: " + name );
                }
                if( member.scalar == uniform_scalar::floating ) {
                    const double value = field.get_float( "default" );
                    if( !std::isfinite( value ) || std::abs( value ) > std::numeric_limits<float>::max() ) {
                        throw std::runtime_error( "Invalid floating-point Iris uniform default: " + name );
                    }
                    member.default_value = std::vector<float>{ static_cast<float>( value ) };
                } else if( member.scalar == uniform_scalar::signed_integer ) {
                    member.default_value = std::vector<std::int32_t>{ field.get_int( "default" ) };
                } else {
                    const double value = field.get_float( "default" );
                    if( !std::isfinite( value ) || value != std::floor( value ) ||
                        value < 0 || value > std::numeric_limits<std::uint32_t>::max() ) {
                        throw std::runtime_error( "Invalid unsigned Iris uniform default: " + name );
                    }
                    member.default_value = std::vector<std::uint32_t>{ static_cast<std::uint32_t>( value ) };
                }
            }
            const unsigned stride = member.columns == 1 ? member.rows * 4 : member.matrix_stride;
            const unsigned size = member.columns * stride;
            if( name.empty() || member.offset > result.frame_size ||
                size > result.frame_size - member.offset ||
                !result.uniforms.emplace( name, member ).second ) {
                throw std::runtime_error( "Invalid Iris frame member: " + name );
            }
            const auto first = occupied.begin() + member.offset / 4;
            const auto last = first + size / 4;
            if( std::find( first, last, true ) != last ) {
                throw std::runtime_error( "Overlapping Iris frame member: " + name );
            }
            std::fill( first, last, true );
        }
        return result;
    } catch( const std::exception &error ) {
        shader_layout result;
        result.error = error.what();
        return result;
    }
}

compiled_shader load_compiled_shader( const std::filesystem::path &bytecode_path,
                                      std::string_view expected_cache_key )
{
    try {
        auto sidecar = bytecode_path;
        sidecar += ".json";
        auto layout = parse_shader_layout( read_artifact( sidecar, 1024 * 1024 ) );
        if( !layout.error.empty() ) {
            return { {}, {}, sidecar.u8string() + ": " + layout.error };
        }
        if( !expected_cache_key.empty() && layout.cache_key != expected_cache_key ) {
            return { {}, {}, "Iris cache key mismatch: " + sidecar.u8string() };
        }
        auto bytecode = read_artifact( bytecode_path, 16 * 1024 * 1024 );
        if( artifact_sha256( bytecode ) != layout.bytecode_sha256 ) {
            return { {}, {}, "Iris bytecode SHA-256 mismatch: " + bytecode_path.u8string() };
        }
        return { std::move( layout ), std::move( bytecode ), {} };
    } catch( const std::exception &error ) {
        return { {}, {}, error.what() };
    }
}

std::string bind_compiled_program( pack_fullscreen_program &program,
                                  const shader_layout &vertex, const shader_layout &fragment,
                                  const std::map<std::string, std::string> &resources )
{
    if( !fragment.target_directive ) {
        return "Missing Iris target directive metadata for " + program.name;
    }
    const auto targets = read_render_targets( *fragment.target_directive, target_source::preprocessed );
    if( !targets.error.empty() ) {
        return program.name + ": " + targets.error;
    }
    const size_t expected = targets.slots.empty() && program.name == "final" ? 1 : targets.slots.size();
    if( expected != fragment.output_locations.size() ) {
        return "Iris render targets disagree with compiled output locations for " + program.name;
    }
    for( size_t index = 0; index < expected; ++index ) {
        if( fragment.output_locations[index] != index ) {
            return "Iris fragment outputs are not contiguous for " + program.name;
        }
    }
    auto error = bind_program_sampler_inputs( program, vertex, fragment, resources );
    if( error.empty() ) {
        program.preprocessed_fragment = *fragment.target_directive;
    }
    return error;
}

sampler_inputs resolve_sampler_inputs( const shader_layout &layout,
                                       const std::map<std::string, std::string> &resources )
{
    if( !layout.error.empty() ) {
        return { {}, layout.error };
    }
    sampler_inputs result;
    for( const auto &slot : layout.samplers ) {
        const auto resource = resources.find( slot.name );
        if( resource == resources.end() || resource->second.empty() ) {
            return { {}, "Missing Iris sampler resource: " + slot.name };
        }
        result.images.push_back( resource->second );
    }
    return result;
}

std::string bind_program_sampler_inputs( pack_fullscreen_program &program,
        const shader_layout &vertex, const shader_layout &fragment,
        const std::map<std::string, std::string> &resources )
{
    if( vertex.stage != "vert" || fragment.stage != "frag" || vertex.format != fragment.format ) {
        return "Invalid Iris graphics shader pair: " + program.name;
    }
    auto vertex_inputs = resolve_sampler_inputs( vertex, resources );
    auto fragment_inputs = resolve_sampler_inputs( fragment, resources );
    if( !vertex_inputs.error.empty() ) {
        return vertex_inputs.error;
    }
    if( !fragment_inputs.error.empty() ) {
        return fragment_inputs.error;
    }
    program.vertex_samplers = std::move( vertex_inputs.images );
    program.samplers = std::move( fragment_inputs.images );
    return {};
}

uniform_values native_frame_uniform_values( const frame_inputs &frame )
{
    const auto width = static_cast<float>( frame.current.width );
    const auto height = static_cast<float>( frame.current.height );
    uniform_values values{
        { "isEyeInWater", std::vector<std::int32_t>{ frame.current.eye_in_water } },
        { "rainStrength", std::vector<float>{ frame.rain_strength } },
        { "eyeBrightness", std::vector<std::int32_t>{ frame.eye_brightness[0], frame.eye_brightness[1] } },
        { "blindness", std::vector<float>{ frame.blind ? 1.f : 0.f } },
        { "darknessFactor", std::vector<float>{ frame.darkness ? 1.f : 0.f } },
        { "nightVision", std::vector<float>{ frame.night_vision } },
        { "heldBlockLightValue", std::vector<std::int32_t>{ frame.held_light } },
        { "heldBlockLightValue2", std::vector<std::int32_t>{ 0 } },
        { "frameCounter", std::vector<std::int32_t>{ frame.frame_counter } },
        { "worldTime", std::vector<std::int32_t>{ frame.clock.world_time } },
        { "worldDay", std::vector<std::int32_t>{ frame.clock.world_day } },
        { "sunAngle", std::vector<float>{ frame.clock.sun_angle } },
        { "shadowAngle", std::vector<float>{ std::fmod( frame.clock.sun_angle, 0.5f ) } },
        { "moonPhase", std::vector<std::int32_t>{ frame.clock.moon_phase } },
        { "biome", std::vector<std::int32_t>{ frame.biome.id } },
        { "biome_category", std::vector<std::int32_t>{ frame.biome.category } },
        { "biome_precipitation", std::vector<std::int32_t>{ frame.biome.precipitation } },
        { "rainfall", std::vector<float>{ frame.biome.rainfall } },
        { "temperature", std::vector<float>{ frame.biome.temperature } },
        { "skyColor", std::vector<float>( frame.sky_color.begin(), frame.sky_color.end() ) },
        { "fogColor", std::vector<float>( frame.fog_color.begin(), frame.fog_color.end() ) },
        { "fogStart", std::vector<float>{ frame.fog_distance > frame.fog_start ? frame.fog_start : 0.f } }
    };
    const auto add_camera_position = [&]( const frame_view &view, const char *position_name,
    const char *integer_name, const char *fraction_name ) {
        // World geometry is X east, Y up, Z south, in renderer units.
        const std::array<double, 3> position{
            view.map_origin[0] + static_cast<double>( view.eye_in_scene[0] ),
            view.map_origin[2] * static_cast<double>( fp3d_camera::floor_height ) + view.eye_in_scene[1],
            view.map_origin[1] + static_cast<double>( view.eye_in_scene[2] )
        };
        values.emplace( position_name, std::vector<float>( position.begin(), position.end() ) );
        std::vector<std::int32_t> whole;
        std::vector<float> fraction;
        for( double axis : position ) {
            const double tile = std::floor( axis );
            if( !std::isfinite( tile ) || tile < std::numeric_limits<std::int32_t>::min() ||
                tile > std::numeric_limits<std::int32_t>::max() ) {
                return;
            }
            whole.push_back( static_cast<std::int32_t>( tile ) );
            fraction.push_back( static_cast<float>( axis - tile ) );
        }
        values.emplace( integer_name, std::move( whole ) );
        values.emplace( fraction_name, std::move( fraction ) );
    };
    add_camera_position( frame.current, "cameraPosition", "cameraPositionInt", "cameraPositionFract" );
    add_camera_position( frame.previous ? *frame.previous : frame.current, "previousCameraPosition",
                         "previousCameraPositionInt", "previousCameraPositionFract" );
    values.emplace( "eyeAltitude", std::vector<float>{
        std::get<std::vector<float>>( values.at( "cameraPosition" ) )[1]
    } );
    if( width > 0 && height > 0 ) {
        values.emplace( "aspectRatio", std::vector<float>{ width / height } );
        values.emplace( "viewWidth", std::vector<float>{ width } );
        values.emplace( "viewHeight", std::vector<float>{ height } );
        values.emplace( "view_res", std::vector<float>{ width, height } );
        values.emplace( "view_pixel_size", std::vector<float>{ 1.f / width, 1.f / height } );
    }
    return values;
}

namespace
{
using expression_value = std::vector<double>;
using expression_lookup = std::function<expression_value( std::string_view )>;
using smooth_expression = std::function<double( size_t, double, double, double )>;

double scalar( const expression_value &value )
{
    if( value.size() != 1 ) {
        throw std::runtime_error( "expected a scalar argument" );
    }
    return value.front();
}

class uniform_expression
{
    public:
        uniform_expression( std::string_view text, const expression_lookup &lookup,
                            const smooth_expression &smooth ) :
            rest_( text ), lookup_( lookup ), smooth_( smooth ) {}

        expression_value evaluate() {
            auto value = parse( 0, 0 );
            if( !remaining().empty() ) {
                throw std::runtime_error( "unexpected token " + std::string( remaining() ) );
            }
            return value;
        }

    private:
        std::string_view remaining() {
            const auto start = rest_.find_first_not_of( " \t\r\n" );
            rest_.remove_prefix( start == rest_.npos ? rest_.size() : start );
            return rest_;
        }

        bool take( std::string_view token ) {
            if( remaining().substr( 0, token.size() ) != token ) {
                return false;
            }
            rest_.remove_prefix( token.size() );
            return true;
        }

        void expect( std::string_view token ) {
            if( !take( token ) ) {
                throw std::runtime_error( "expected " + std::string( token ) );
            }
        }

        expression_value atom( unsigned depth ) {
            if( take( "-" ) ) {
                return { -scalar( parse( 7, depth ) ) };
            }
            if( take( "+" ) ) {
                return { scalar( parse( 7, depth ) ) };
            }
            if( take( "!" ) ) {
                return { static_cast<double>( scalar( parse( 7, depth ) ) == 0 ) };
            }
            if( take( "(" ) ) {
                auto value = parse( 0, depth );
                expect( ")" );
                return value;
            }
            const auto text = remaining();
            if( text.empty() ) {
                throw std::runtime_error( "expected a value" );
            }
            if( text.front() == '.' || ( text.front() >= '0' && text.front() <= '9' ) ) {
                double number = 0;
                const auto parsed = std::from_chars( text.data(), text.data() + text.size(), number );
                if( parsed.ec != std::errc{} ) {
                    throw std::runtime_error( "invalid number" );
                }
                rest_.remove_prefix( parsed.ptr - text.data() );
                return { number };
            }
            const auto end = text.find_first_not_of(
                                 "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789." );
            const auto name = text.substr( 0, end );
            if( name.empty() ) {
                throw std::runtime_error( "expected an identifier" );
            }
            rest_.remove_prefix( name.size() );
            if( !take( "(" ) ) {
                return lookup_( name );
            }
            const auto call_site = rest_.size();
            std::vector<expression_value> arguments;
            if( !take( ")" ) ) {
                do {
                    arguments.emplace_back( parse( 0, depth ) );
                } while( take( "," ) );
                expect( ")" );
            }
            if( name == "smooth" && !arguments.empty() && arguments.size() <= 4 ) {
                // Iris ignores the legacy ID in the four-argument overload.
                const size_t first = arguments.size() == 4 ? 1 : 0;
                const auto target = scalar( arguments[first] );
                const auto up = arguments.size() > 1 ? scalar( arguments[first + 1] ) : 1.0;
                const auto down = arguments.size() > 2 ? scalar( arguments[first + 2] ) : up;
                return { smooth_( call_site, target, up, down ) };
            }
            return call( name, arguments );
        }

        static expression_value call( std::string_view name, const std::vector<expression_value> &args ) {
            if( name == "vec2" || name == "vec3" || name == "vec4" ) {
                expression_value result;
                const size_t size = name.back() - '0';
                for( const auto &arg : args ) {
                    result.insert( result.end(), arg.begin(), arg.end() );
                }
                if( result.size() == size ) {
                    return result;
                }
            }
            if( name == "if" && args.size() >= 3 && args.size() % 2 == 1 ) {
                for( size_t i = 0; i + 1 < args.size(); i += 2 ) {
                    if( scalar( args[i] ) != 0 ) {
                        return args[i + 1];
                    }
                }
                return args.back();
            }
            const auto arg = [&]( size_t index ) { return scalar( args[index] ); };
            if( args.size() == 1 ) {
                if( name == "abs" ) { return { std::abs( arg( 0 ) ) }; }
                if( name == "sqrt" ) { return { std::sqrt( arg( 0 ) ) }; }
                if( name == "exp" ) { return { std::exp( arg( 0 ) ) }; }
                if( name == "floor" ) { return { std::floor( arg( 0 ) ) }; }
                if( name == "ceil" ) { return { std::ceil( arg( 0 ) ) }; }
                if( name == "frac" ) { return { arg( 0 ) - std::floor( arg( 0 ) ) }; }
                if( name == "sin" ) { return { std::sin( arg( 0 ) ) }; }
                if( name == "cos" ) { return { std::cos( arg( 0 ) ) }; }
            }
            if( args.size() == 2 ) {
                if( name == "min" ) { return { std::min( arg( 0 ), arg( 1 ) ) }; }
                if( name == "max" ) { return { std::max( arg( 0 ), arg( 1 ) ) }; }
                if( name == "pow" ) { return { std::pow( arg( 0 ), arg( 1 ) ) }; }
            }
            if( args.size() == 3 ) {
                if( name == "clamp" && arg( 1 ) <= arg( 2 ) ) {
                    return { std::clamp( arg( 0 ), arg( 1 ), arg( 2 ) ) };
                }
                if( name == "equals" ) {
                    return { static_cast<double>( std::abs( arg( 0 ) - arg( 1 ) ) <= arg( 2 ) ) };
                }
                if( name == "between" ) {
                    return { static_cast<double>( arg( 1 ) <= arg( 0 ) && arg( 0 ) <= arg( 2 ) ) };
                }
            }
            if( name == "in" && args.size() >= 2 ) {
                const double value = arg( 0 );
                return { static_cast<double>( std::any_of( args.begin() + 1, args.end(), [&]( const auto &item ) {
                        return scalar( item ) == value;
                    } ) ) };
            }
            throw std::runtime_error( "unsupported function or arity: " + std::string( name ) );
        }

        expression_value parse( unsigned precedence, unsigned depth ) {
            if( depth >= 64 ) {
                throw std::runtime_error( "expression nesting exceeds 64 levels" );
            }
            auto lhs = atom( depth + 1 );
            struct operation {
                std::string_view token;
                unsigned precedence;
                double ( *apply )( double, double );
            };
            static const operation operations[] = {
                { "||", 1, []( double a, double b ) -> double { return a != 0 || b != 0; } },
                { "&&", 2, []( double a, double b ) -> double { return a != 0 && b != 0; } },
                { "==", 3, []( double a, double b ) -> double { return a == b; } },
                { "!=", 3, []( double a, double b ) -> double { return a != b; } },
                { "<=", 4, []( double a, double b ) -> double { return a <= b; } },
                { ">=", 4, []( double a, double b ) -> double { return a >= b; } },
                { "<", 4, []( double a, double b ) -> double { return a < b; } },
                { ">", 4, []( double a, double b ) -> double { return a > b; } },
                { "+", 5, []( double a, double b ) { return a + b; } },
                { "-", 5, []( double a, double b ) { return a - b; } },
                { "*", 6, []( double a, double b ) { return a * b; } },
                { "/", 6, []( double a, double b ) { return a / b; } },
                { "%", 6, []( double a, double b ) { return std::fmod( a, b ); } }
            };
            while( true ) {
                const auto text = remaining();
                const auto op = std::find_if( std::begin( operations ), std::end( operations ),
                [&]( const operation &candidate ) {
                    return text.substr( 0, candidate.token.size() ) == candidate.token;
                } );
                if( op == std::end( operations ) || op->precedence < precedence ) {
                    return lhs;
                }
                rest_.remove_prefix( op->token.size() );
                const auto rhs = parse( op->precedence + 1, depth + 1 );
                lhs = { op->apply( scalar( lhs ), scalar( rhs ) ) };
            }
        }

        std::string_view rest_;
        const expression_lookup &lookup_;
        const smooth_expression &smooth_;
};
} // namespace

double smooth_uniform_value( double previous, double target, double half_life, double elapsed_seconds )
{
    if( half_life == 0 ) {
        return target;
    }
    // Iris half-life directives are measured in tenths of a second.
    const double residual = std::exp2( -elapsed_seconds / ( half_life * 0.1 ) );
    return residual * previous + ( 1 - residual ) * target;
}

std::string apply_custom_uniforms( const pack_properties &properties,
                                  const std::set<std::string> &required, uniform_values &values,
                                  uniform_smoothing *smoothing, double elapsed_seconds )
{
    std::map<std::string, expression_value> evaluated;
    std::set<std::string> active;
    uniform_smoothing updates;
    const auto resolve = [&]( const std::string &name, const auto &self ) -> expression_value {
        if( name == "pi" ) { return { 3.141592653589793 }; }
        if( name == "true" ) { return { 1 }; }
        if( name == "false" ) { return { 0 }; }
        if( const auto constant = biome_constant( name ) ) {
            return { static_cast<double>( *constant ) };
        }
        if( const auto known = evaluated.find( name ); known != evaluated.end() ) {
            return known->second;
        }
        const auto def = properties.uniform_expressions.find( name );
        if( def == properties.uniform_expressions.end() ) {
            if( const auto native = values.find( name ); native != values.end() ) {
                return std::visit( []( const auto &input ) {
                    return expression_value( input.begin(), input.end() );
                }, native->second );
            }
            throw std::runtime_error( "missing input " + name );
        }
        if( active.size() >= 64 || !active.insert( name ).second ) {
            throw std::runtime_error( "cyclic or deeply nested dependency " + name );
        }
        const expression_lookup lookup = [&]( std::string_view reference ) {
            const auto dot = reference.find( '.' );
            auto value = self( std::string( reference.substr( 0, dot ) ), self );
            if( dot == reference.npos ) {
                return value;
            }
            const auto selector = reference.substr( dot + 1 );
            size_t index = 0;
            if( selector.size() == 1 ) {
                constexpr std::string_view components = "xyzwrgbastpq0123";
                const auto pos = components.find( selector.front() );
                if( pos == components.npos || value.size() > 4 ) {
                    throw std::runtime_error( "invalid component " + std::string( reference ) );
                }
                index = pos % 4;
            } else if( selector.size() == 3 && selector[1] == '.' &&
                       selector[0] >= '0' && selector[0] <= '3' &&
                       selector[2] >= '0' && selector[2] <= '3' && value.size() == 16 ) {
                index = ( selector[2] - '0' ) * 4 + selector[0] - '0';
            } else {
                throw std::runtime_error( "invalid component " + std::string( reference ) );
            }
            if( index >= value.size() ) {
                throw std::runtime_error( "component out of bounds " + std::string( reference ) );
            }
            return expression_value{ value[index] };
        };
        const smooth_expression smooth = [&]( size_t site, double target, double up, double down ) {
            if( !smoothing || !std::isfinite( target ) || !std::isfinite( up ) || !std::isfinite( down ) ||
                up < 0 || down < 0 || !std::isfinite( elapsed_seconds ) || elapsed_seconds < 0 ) {
                throw std::runtime_error( "invalid smooth state or arguments" );
            }
            const auto key = std::make_pair( name, site );
            const auto previous = smoothing->find( key );
            double value = target;
            if( previous != smoothing->end() ) {
                const double half_life = target > previous->second ? up : down;
                value = smooth_uniform_value( previous->second, target, half_life, elapsed_seconds );
            }
            updates.emplace( key, value );
            return value;
        };
        try {
            auto result = uniform_expression( def->second.expression, lookup, smooth ).evaluate();
            const std::string &type = def->second.type;
            const size_t count = type == "float" || type == "bool" || type == "int" ? 1 :
                                 type == "vec2" || type == "vec3" || type == "vec4" ? type.back() - '0' : 0;
            if( result.empty() || result.size() != count ||
                !std::all_of( result.begin(), result.end(), []( double component ) {
                    return std::isfinite( component ) && std::abs( component ) <= std::numeric_limits<float>::max();
                } ) ) {
                throw std::runtime_error( "invalid result for type " + type );
            }
            if( type == "bool" ) {
                result.front() = result.front() != 0;
            } else if( type == "int" ) {
                result.front() = std::trunc( result.front() );
                if( result.front() < std::numeric_limits<std::int32_t>::min() ||
                    result.front() > std::numeric_limits<std::int32_t>::max() ) {
                    throw std::runtime_error( "integer result out of range" );
                }
            }
            active.erase( name );
            return evaluated.emplace( name, std::move( result ) ).first->second;
        } catch( const std::runtime_error &error ) {
            throw std::runtime_error( "shaders.properties:" + std::to_string( def->second.line ) + " " +
                                      name + ": " + error.what() );
        }
    };
    uniform_values custom;
    try {
        for( const auto &name : required ) {
            const auto definition = properties.uniform_expressions.find( name );
            if( definition == properties.uniform_expressions.end() || definition->second.variable ) {
                continue;
            }
            const auto result = resolve( name, resolve );
            if( definition->second.type == "bool" ) {
                custom.emplace( name, std::vector<std::uint32_t>{ result.front() != 0 } );
            } else if( definition->second.type == "int" ) {
                custom.emplace( name, std::vector<std::int32_t>{ static_cast<std::int32_t>( result.front() ) } );
            } else {
                custom.emplace( name, std::vector<float>( result.begin(), result.end() ) );
            }
        }
    } catch( const std::runtime_error &error ) {
        return error.what();
    }
    for( auto &[name, value] : custom ) {
        values.insert_or_assign( name, std::move( value ) );
    }
    for( const auto &[key, value] : updates ) {
        smoothing->insert_or_assign( key, value );
    }
    return {};
}

packed_uniforms pack_frame_uniforms( const shader_layout &layout,
                                    const uniform_values &values )
{
    if( !layout.error.empty() ) {
        return { {}, {}, layout.error };
    }
    packed_uniforms result;
    result.bytes.resize( layout.frame_size );
    for( const auto &[name, member] : layout.uniforms ) {
        const auto value = values.find( name );
        const auto *input = value != values.end() ? &value->second :
                            member.default_value ? &*member.default_value : nullptr;
        if( !input ) {
            result.missing.push_back( name );
            continue;
        }
        const auto *floats = std::get_if<std::vector<float>>( input );
        const auto *ints = std::get_if<std::vector<std::int32_t>>( input );
        const auto *uints = std::get_if<std::vector<std::uint32_t>>( input );
        const void *data = nullptr;
        const unsigned count = member.rows * member.columns;
        if( member.scalar == uniform_scalar::floating && floats && floats->size() == count ) {
            data = floats->data();
        } else if( member.scalar == uniform_scalar::signed_integer && ints && ints->size() == count ) {
            data = ints->data();
        } else if( member.scalar == uniform_scalar::unsigned_integer && uints && uints->size() == count ) {
            data = uints->data();
        }
        if( !data ) {
            return { {}, {}, "Invalid Iris uniform value: " + name };
        }
        for( unsigned column = 0; column < member.columns; ++column ) {
            std::memcpy( result.bytes.data() + member.offset + column * member.matrix_stride,
                         static_cast<const std::uint8_t *>( data ) + column * member.rows * 4,
                         member.rows * 4 );
        }
    }
    return result;
}
} // namespace fp3d_iris
#endif
