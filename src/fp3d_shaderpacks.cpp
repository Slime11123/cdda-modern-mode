#include "fp3d_shaderpacks.h"
#include "fp3d_shaderpacks_internal.h"
#include "fp3d_shader_archive.h"
#include "fp3d_mcshader.h"
#include "fp3d_iris_pack.h"
#include "fp3d_iris_layout.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <system_error>
#include <SDL3/SDL_process.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif
#include "sdl_wrappers.h"
#include "path_info.h"
#include "json_loader.h"
#include "options.h"

namespace fp3d_shaderpacks
{
namespace fs = std::filesystem;
namespace detail
{
std::string choice;
std::string report;
std::uint64_t selection_revision = 1;
int quality_level = 2;
int mc_intensity = 145;
bool wet_ssr = true;
bool shadow_volume = true;
bool native_local_lights = true;
} // namespace detail
using namespace detail;

namespace
{
bool choice_loaded = false;
bool native_shadows_enabled = true;
bool native_local_shadows = true;
bool experimental_pbr = false;
int pbr_strength = 100;

bool builtin( const std::string &id )
{
    return id == "off" || id == "cinematic" || id == "warm";
}

bool valid_pack_id( const std::string &id )
{
    return !id.empty() && id.size() < 256 && id != "." && id != ".." &&
           id.find_first_of( "\r\n/\\:" ) == std::string::npos && id.find( '\0' ) == std::string::npos;
}

struct pack_info {
    bool native = false;
    fp3d_mcshader::profile profile;

    bool compatible() const {
        return !native && profile.recognized && !profile.direct_final;
    }
};

pack_info inspect_pack( const fs::path &path )
{
    std::error_code error;
    bool native = false;
    if( fs::is_directory( path, error ) ) {
        native = fs::is_regular_file( path / "post.frag", error ) ||
                 fs::is_regular_file( path / "shaders" / "post.frag", error );
    } else if( path.extension() == ".zip" && fs::is_regular_file( path, error ) ) {
        std::string source;
        native = fp3d_shader_archive::read_post( path, source );
    }
    return { native, native ? fp3d_mcshader::profile{} : fp3d_mcshader::inspect( path ) };
}
} // namespace

fs::path detail::root_path()
{
    const char *base = SDL_GetBasePath();
    if( base ) {
        const fs::path root = fs::u8path( base ) / "shaderpacks";
        return root;
    }
    std::error_code error;
    return fs::current_path( error ) / "shaderpacks";
}

void detail::restore_choice()
{
    if( choice_loaded ) {
        return;
    }
    choice_loaded = true;
    choice = "off";
    std::ifstream quality_file( root_path() / ".quality", std::ios::binary );
    int saved_quality = 2;
    if( quality_file >> saved_quality && saved_quality >= 0 && saved_quality <= 3 ) {
        quality_level = saved_quality;
    }
    int stored_intensity = 145;
    std::ifstream strength_file( root_path() / ".intensity", std::ios::binary );
    if( strength_file >> stored_intensity && stored_intensity >= 50 &&
        stored_intensity <= 200 ) {
        mc_intensity = stored_intensity;
    }
    int stored_wet = 1;
    std::ifstream wet_file( root_path() / ".wet_ssr", std::ios::binary );
    if( wet_file >> stored_wet && ( stored_wet == 0 || stored_wet == 1 ) ) {
        wet_ssr = stored_wet != 0;
    }
    int stored_local_lights = 1;
    std::ifstream local_file( root_path() / ".local_lights", std::ios::binary );
    if( local_file >> stored_local_lights &&
        ( stored_local_lights == 0 || stored_local_lights == 1 ) ) {
        native_local_lights = stored_local_lights != 0;
    }
    int stored_local_shadows = 1;
    std::ifstream local_shadow_file( root_path() / ".local_shadows", std::ios::binary );
    if( local_shadow_file >> stored_local_shadows &&
        ( stored_local_shadows == 0 || stored_local_shadows == 1 ) ) {
        native_local_shadows = stored_local_shadows != 0;
    }
    int stored_volume = 1;
    std::ifstream volume_file( root_path() / ".shadow_volume", std::ios::binary );
    if( volume_file >> stored_volume && ( stored_volume == 0 || stored_volume == 1 ) ) {
        shadow_volume = stored_volume != 0;
    }
    int stored_pbr = 0;
    std::ifstream pbr_file( root_path() / ".pbr", std::ios::binary );
    if( pbr_file >> stored_pbr && ( stored_pbr == 0 || stored_pbr == 1 ) ) {
        experimental_pbr = stored_pbr != 0;
    }
    int stored_pbr_strength = 100;
    std::ifstream pbr_strength_file( root_path() / ".pbr_strength", std::ios::binary );
    if( pbr_strength_file >> stored_pbr_strength &&
        stored_pbr_strength >= 0 && stored_pbr_strength <= 200 ) {
        pbr_strength = stored_pbr_strength;
    }
    int stored_shadow = 1;
    std::ifstream shadow_file( root_path() / ".shadows", std::ios::binary );
    if( shadow_file >> stored_shadow && ( stored_shadow == 0 || stored_shadow == 1 ) ) {
        native_shadows_enabled = stored_shadow != 0;
    }
    std::ifstream file( root_path() / ".selected", std::ios::binary );
    std::string saved;
    if( file && std::getline( file, saved ) ) {
        // Windows Notepad and Python's text mode commonly write CRLF.
        // getline() removes LF only; reject embedded control characters,
        // but accept a single trailing CR from the platform line ending.
        if( !saved.empty() && saved.back() == '\r' ) {
            saved.pop_back();
        }
        if( valid_pack_id( saved ) ) {
            choice = saved;
        }
    }
}

std::string directory()
{
    return root_path().u8string();
}

bool open_folder()
{
    std::error_code error;
    fs::create_directories( root_path(), error );
    if( error ) {
        report = "无法打开光影包文件夹：" + error.message();
        return false;
    }
#if defined(_WIN32)
    const std::wstring folder = root_path().wstring();
    const auto launched = reinterpret_cast<std::intptr_t>(
                              ShellExecuteW( nullptr, L"open", folder.c_str(),
                                             nullptr, nullptr, SW_SHOWNORMAL ) );
    if( launched <= 32 ) {
        report = "Windows 无法打开光影包文件夹；请复制文件夹路径手动打开。";
        return false;
    }
    report = "已在 Windows 资源管理器中打开光影包文件夹。";
    return true;
#else
    report = "当前系统暂不支持直接打开目录；请复制路径。";
    return false;
#endif
}

std::vector<entry> scan( std::string *scan_error )
{
    if( scan_error ) {
        scan_error->clear();
    }
    restore_choice();
    std::vector<entry> result{
        { "off", "关闭光影", "使用原版 3D 画面", true },
        { "cinematic", "内置 · 柔光", "柔和 Bloom 与色调映射", true },
        { "warm", "内置 · 暖色电影", "Bloom、暖色调与色调映射", true }
    };
    std::error_code error;
    fs::create_directories( root_path(), error );
    if( error ) {
        ( scan_error ? *scan_error : report ) = "无法创建 shaderpacks 文件夹：" + error.message();
        return result;
    }
    for( fs::directory_iterator it( root_path(), error ),
         end; !error && it != end; it.increment( error ) ) {
        const auto &path = it->path();
        std::error_code entry_error;
        const auto id = path.filename().u8string();
        if( !valid_pack_id( id ) || builtin( id ) || ( !it->is_directory( entry_error ) &&
                               ( path.extension() != ".zip" || !it->is_regular_file( entry_error ) ) ) ) {
            continue;
        }
        const auto pack = inspect_pack( path );
        const bool compatible = pack.compatible();
        const auto iris = fp3d_iris::inspect_pack( path );
        const bool iris_candidate = iris.error.empty() && !iris.programs.empty();
        const std::string note = compatible ? pack.profile.label + " · SDL3 GPU 光影适配" :
                                 iris_candidate ? "可选用于 Cataclysm Iris，等待管线验证" :
                                 ( pack.native || pack.profile.direct_final ) ? "此包需要移植 GLSL 后处理" :
                                 "无可识别光影入口";
        result.push_back( { id, id, note, compatible || iris_candidate } );
    }
    if( error ) {
        ( scan_error ? *scan_error : report ) = "扫描光影文件夹时发生错误：" + error.message();
    }
    return result;
}

static std::uint64_t gpu_profile_revision = 0;
static fp3d_mcshader::profile gpu_profile;

iris_selection selected_iris_pack()
{
    restore_choice();
    static iris_selection current{};
    if( current.revision != selection_revision ) {
        current.revision = selection_revision;
        current.source.reset();
        if( !builtin( choice ) ) {
            current.source = std::make_shared<const fp3d_iris::pack_source>(
                                 fp3d_iris::prepare_pack_source( root_path() / fs::u8path( choice ) ) );
        }
    }
    return current;
}

namespace
{
iris_compile_state compile_state;
std::future<iris_compile_state> compile_future;
std::shared_ptr<std::atomic<bool>> compile_stop;

bool load_iris_texture_metadata( const fp3d_iris::pack_source &pack, const std::string &path,
                                 iris_sampled_texture &texture, iris_compile_state &result )
{
    std::string data;
    const std::string metadata = path + ".mcmeta";
    if( std::binary_search( pack.inventory.files.begin(), pack.inventory.files.end(), metadata ) ) {
        if( !fp3d_iris::read_pack_asset( pack, metadata, data ) || data.size() > 64 * 1024 ) {
            result.diagnostics.push_back( "Cannot read Iris texture metadata: " + metadata );
            return false;
        }
        try {
            const auto json = json_loader::from_string( std::move( data ) );
            const auto root = json.get_object();
            root.allow_omitted_members();
            if( root.has_member( "texture" ) ) {
                const auto sampler = root.get_object( "texture" );
                sampler.allow_omitted_members();
                texture.sampling.linear = sampler.get_bool( "blur", texture.sampling.linear );
                texture.sampling.repeat = !sampler.get_bool( "clamp", !texture.sampling.repeat );
            }
        } catch( const std::exception &error ) {
            result.diagnostics.push_back( "Invalid Iris texture metadata: " + metadata + ": " + error.what() );
            return false;
        }
    }
    return true;
}

bool load_iris_png( const fp3d_iris::pack_source &pack, const std::string &path,
                    iris_sampled_texture &texture, iris_compile_state &result )
{
    std::string data;
    if( fs::u8path( path ).extension() != ".png" || !fp3d_iris::read_pack_asset( pack, path, data ) ||
        data.size() < 24 || data.compare( 0, 8, "\x89PNG\r\n\x1a\n" ) != 0 ||
        data.compare( 12, 4, "IHDR" ) != 0 ) {
        result.diagnostics.push_back( "Cannot read Iris texture PNG: " + path );
        return false;
    }
    const auto dimension = [&]( size_t start ) {
        unsigned value = 0;
        for( size_t i = start; i < start + 4; ++i ) {
            value = value << 8 | static_cast<unsigned char>( data[i] );
        }
        return value;
    };
    texture.width = dimension( 16 );
    texture.height = dimension( 20 );
    if( !texture.width || !texture.height || texture.width > 2048 || texture.height > 2048 ) {
        result.diagnostics.push_back( "Iris texture PNG size is unsupported: " + path );
        return false;
    }
    const std::unique_ptr<SDL_IOStream, decltype( &SDL_CloseIO )> io(
        SDL_IOFromConstMem( data.data(), data.size() ), SDL_CloseIO );
    const SDL_Surface_Ptr decoded( io ? IMG_LoadPNG_IO( io.get() ) : nullptr );
    const SDL_Surface_Ptr rgba( decoded ? SDL_ConvertSurface( decoded.get(), SDL_PIXELFORMAT_RGBA32 ) : nullptr );
    if( !rgba || rgba->w != static_cast<int>( texture.width ) ||
        rgba->h != static_cast<int>( texture.height ) ) {
        result.diagnostics.push_back( "Cannot decode Iris texture PNG: " + path );
        return false;
    }
    const size_t row_bytes = texture.width * 4;
    texture.bytes.resize( row_bytes * texture.height );
    for( unsigned y = 0; y < texture.height; ++y ) {
        std::memcpy( texture.bytes.data() + y * row_bytes,
                     static_cast<const std::uint8_t *>( rgba->pixels ) + static_cast<size_t>( y ) * rgba->pitch,
                     row_bytes );
    }
    return true;
}

bool load_iris_raw_texture( const fp3d_iris::pack_source &pack, const std::vector<std::string> &parts,
                           iris_sampled_texture &texture, iris_compile_state &result )
{
    texture.volume = parts.size() == 8 && parts[1] == "TEXTURE_3D";
    if( !texture.volume && !( parts.size() == 7 && parts[1] == "TEXTURE_2D" ) ) {
        result.diagnostics.push_back( "Unsupported Iris raw texture declaration: " + parts.front() );
        return false;
    }
    const auto dimension = []( const std::string &text, unsigned &value ) {
        const auto end = text.data() + text.size();
        const auto parsed = std::from_chars( text.data(), end, value );
        return parsed.ec == std::errc{} && parsed.ptr == end && value > 0 && value <= 2048;
    };
    if( !dimension( parts[3], texture.width ) || !dimension( parts[4], texture.height ) ||
        ( texture.volume && !dimension( parts[5], texture.depth ) ) ) {
        result.diagnostics.push_back( "Invalid Iris raw texture dimensions: " + parts.front() );
        return false;
    }
    const auto &channels = parts[parts.size() - 2];
    const auto &encoding = parts.back();
    const bool r8 = parts[2] == "R8" && channels == "RED" && encoding == "UNSIGNED_BYTE";
    const bool rgb16f = parts[2] == "RGB16F" && channels == "RGB" && encoding == "HALF_FLOAT";
    if( !r8 && !rgb16f ) {
        result.diagnostics.push_back( "Unsupported Iris raw texture format: " + parts.front() );
        return false;
    }
    const auto texels = static_cast<std::uint64_t>( texture.width ) * texture.height * texture.depth;
    if( texels * ( r8 ? 1 : 8 ) > 16 * 1024 * 1024 ) {
        result.diagnostics.push_back( "Iris raw texture exceeds the decoded size limit: " + parts.front() );
        return false;
    }
    std::string data;
    if( !fp3d_iris::read_pack_asset( pack, parts.front(), data ) ||
        data.size() != texels * ( r8 ? 1 : 6 ) ) {
        result.diagnostics.push_back( "Iris raw texture size does not match its declaration: " + parts.front() );
        return false;
    }
    texture.format = r8 ? iris_texture_format::r8 : iris_texture_format::rgba16f;
    texture.sampling = { true, false };
    if( r8 ) {
        texture.bytes.assign( data.begin(), data.end() );
    } else {
        texture.bytes.resize( texels * 8 );
        for( size_t i = 0; i < texels; ++i ) {
            auto *pixel = texture.bytes.data() + i * 8;
            std::memcpy( pixel, data.data() + i * 6, 6 );
            // SDL GPU exposes RGBA16F. Extend each RGB texel with binary16 1.0.
            pixel[6] = 0;
            pixel[7] = 0x3c;
        }
    }
    return true;
}

std::string iris_texture_family( const std::string &program )
{
    if( program == "final" ) {
        return "composite";
    }
    if( program == "shadow" || program.compare( 0, 7, "shadow_" ) == 0 ) {
        return "gbuffers";
    }
    const auto end = program.find_first_of( "0123456789_" );
    return program.substr( 0, end );
}

void load_iris_custom_textures( const fp3d_iris::pack_source &pack, iris_compile_state &result )
{
    const auto &declarations = result.properties.custom_textures;
    std::set<std::string> failed;
    for( const auto &stage : result.stages ) {
        const auto family = iris_texture_family( stage.program );
        for( const auto &sampler : stage.layout.samplers ) {
            auto declaration = declarations.find( { family, sampler.name, sampler.type } );
            if( declaration == declarations.end() ) {
                declaration = declarations.find( { "", sampler.name, sampler.type } );
                if( declaration == declarations.end() ) {
                    continue;
                }
            }
            const auto &key = declaration->first;
            const auto &parts = declaration->second;
            const auto image = "packTexture/" + key[0] + "/" + key[1] + "/" + key[2];
            if( failed.count( image ) ) {
                continue;
            }
            if( result.textures.count( image ) == 0 ) {
                iris_sampled_texture texture;
                const bool loaded = parts.size() == 1 ? load_iris_png( pack, parts.front(), texture, result ) :
                                    load_iris_raw_texture( pack, parts, texture, result );
                if( !loaded || !load_iris_texture_metadata( pack, parts.front(), texture, result ) ) {
                    failed.insert( image );
                    continue;
                }
                result.textures.emplace( image, std::move( texture ) );
            }
            const auto [bound, inserted] = result.texture_bindings[stage.program].emplace( sampler.name, image );
            if( !inserted && bound->second != image ) {
                result.diagnostics.push_back( "Conflicting Iris texture types for " + sampler.name + " in " +
                                              stage.program );
            }
        }
    }
}

void load_iris_noise( const fp3d_iris::pack_source &pack, iris_compile_state &result )
{
    std::optional<unsigned> resolution;
    bool used = false;
    for( const auto &stage : result.stages ) {
        used |= std::any_of( stage.layout.samplers.begin(), stage.layout.samplers.end(), []( const auto &sampler ) {
            return sampler.name == "noisetex";
        } );
        if( stage.layout.noise_resolution ) {
            if( resolution && resolution != stage.layout.noise_resolution ) {
                result.diagnostics.emplace_back( "Conflicting Iris noise texture resolutions" );
                return;
            }
            resolution = stage.layout.noise_resolution;
        }
    }
    if( !used ) {
        return;
    }

    iris_sampled_texture noise;
    const auto &path = result.properties.noise_texture;
    if( path.empty() ) {
        noise.width = noise.height = resolution.value_or( 256 );
        noise.bytes.resize( static_cast<size_t>( noise.width ) * noise.height * 4, 255 );
        // Stable white-noise RGB, independent of the game's random stream.
        std::uint32_t random = 0x811c9dc5u;
        for( size_t i = 0; i < noise.bytes.size(); ++i ) {
            if( i % 4 != 3 ) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                noise.bytes[i] = static_cast<std::uint8_t>( random >> 24 );
            }
        }
    } else if( !load_iris_png( pack, path, noise, result ) ||
               !load_iris_texture_metadata( pack, path, noise, result ) ) {
        return;
    }
    result.textures.emplace( "noisetex", std::move( noise ) );
}

iris_compile_state compile_iris_sources( iris_selection selection, const fs::path &cache,
        const std::string &python, const std::string &script, const std::string &dxc,
        const std::atomic<bool> &stop )
{
    using namespace fp3d_iris;
    iris_compile_state result;
    result.revision = selection.revision;
    auto plan = prepare_pack_compilation( *selection.source, {} );
    if( !plan.error.empty() ) {
        result.diagnostics.push_back( plan.error );
        return result;
    }
    result.properties = std::move( plan.properties );
    std::error_code ec;
    fs::create_directories( cache, ec );
    if( ec ) {
        result.diagnostics.push_back( "Cannot create Iris cache: " + ec.message() );
        return result;
    }
    for( size_t index = 0; index < plan.programs.size() && !stop; ++index ) {
        const auto &job = plan.programs[index];
        // CDDA has no enchanted armor overlay or Minecraft beacon geometry.
        // Keep the omitted programs visible in compatibility reporting.
        if( job.name == "gbuffers_armor_glint" || job.name == "gbuffers_beaconbeam" ) {
            result.unused_minecraft_programs.push_back( job.name );
            continue;
        }
        const bool water = job.name == "gbuffers_water";
        auto &program_diagnostics = water ? result.water_diagnostics : result.diagnostics;
        const size_t first_stage = result.stages.size();
        const bool shadow = is_iris_shadow_program( job.name );
        const bool terrain = is_iris_terrain_program( job.name ) || is_iris_model_program( job.name );
        const bool native_geometry = shadow || terrain || water;
        const bool geometry = job.name.compare( 0, 8, "gbuffers" ) == 0 || job.name == "shadow" ||
                              job.name.compare( 0, 7, "shadow_" ) == 0;
        if( !job.error.empty() || ( geometry && !native_geometry ) || job.stages.size() != 2 ||
            !job.stages.count( shader_stage::vertex ) || !job.stages.count( shader_stage::fragment ) ) {
            program_diagnostics.push_back( job.error.empty() ? "Unsupported Iris program: " + job.name : job.error );
            continue;
        }
        fs::path vertex_layout;
        for( const auto &[kind, name] : job.stages ) {
            if( stop ) {
                break;
            }
            if( kind == shader_stage::fragment && vertex_layout.empty() ) {
                program_diagnostics.push_back( name + ": Iris requires a successfully compiled vertex interface" );
                continue;
            }
            const auto expanded = expand_includes( selection.source->text, name );
            if( !expanded.error.empty() ) {
                program_diagnostics.push_back( expanded.error );
                continue;
            }
            const std::string stage = kind == shader_stage::vertex ? "vert" : "frag";
            const std::string stem = std::to_string( index ) + "." + stage;
            const auto input = cache / ( stem + "." + std::to_string( SDL_GetPerformanceCounter() ) + ".glsl" );
            const auto output = cache / ( stem + ".dxil" );
            std::ofstream source( input, std::ios::binary | std::ios::trunc );
            source << expanded.source;
            source.close();
            if( !source ) {
                program_diagnostics.push_back( "Cannot write Iris source: " + input.u8string() );
                continue;
            }
            std::vector<std::string> args{ python, script, input.u8string(), output.u8string(), "--stage",
                    stage, "--auto-pack-bindings", "--format", "dxil", "--reflect", "--cache",
                    "--define=MC_HAND_DEPTH=0.125", "--define=MC_SHADOW_QUALITY=1.0",
                    "--define=MC_RENDER_QUALITY=1.0" };
            if( stage == "vert" ) {
                args.push_back( terrain ? "--native-terrain-vertices" :
                                shadow ? "--native-shadow-vertices" :
                                native_geometry ? "--native-world-vertices" : "--fullscreen-vertices" );
            } else {
                args.insert( args.end(), { "--link-vertex-layout", vertex_layout.u8string() } );
            }
            if( !dxc.empty() ) {
                args.insert( args.end(), { "--dxc", dxc } );
            }
            if( selection.source->path.extension() == ".zip" ) {
                args.insert( args.end(), { "--pack-archive", selection.source->path.u8string() } );
            }
            std::vector<const char *> argv;
            for( const auto &arg : args ) {
                argv.push_back( arg.c_str() );
            }
            argv.push_back( nullptr );
            const SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetPointerProperty( props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data() );
            SDL_SetNumberProperty( props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP );
            SDL_SetBooleanProperty( props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true );
            std::unique_ptr<SDL_Process, decltype( &SDL_DestroyProcess )> process(
                SDL_CreateProcessWithProperties( props ), SDL_DestroyProcess );
            SDL_DestroyProperties( props );
            if( !process ) {
                result.diagnostics.push_back( name + ": " + SDL_GetError() );
                fs::remove( input, ec );
                return result;
            }
            int code = -1;
            size_t size = 0;
            std::unique_ptr<void, decltype( &SDL_free )> log( SDL_ReadProcess( process.get(), &size, &code ), SDL_free );
            fs::remove( input, ec );
            if( code != 0 ) {
                program_diagnostics.push_back( name + ": " +
                                              ( log ? std::string( static_cast<const char *>( log.get() ),
                                                      std::min<size_t>( size, 2048 ) ) : SDL_GetError() ) );
                continue;
            }
            auto artifact = load_compiled_shader( output );
            if( !artifact.error.empty() || artifact.layout.cache_key.empty() ) {
                program_diagnostics.push_back( name + ": " + ( artifact.error.empty() ?
                                              "Compiler did not produce an Iris cache key" : artifact.error ) );
                continue;
            }
            if( kind == shader_stage::vertex ) {
                vertex_layout = output;
                vertex_layout += ".json";
            }
            result.stages.push_back( { job.name, stage, output, artifact.layout.cache_key,
                                       std::move( artifact.layout ) } );
        }
        if( water && !program_diagnostics.empty() ) {
            // A missing or failed water stage invalidates the pair. Its
            // surviving interface must not be used to prepare pack textures.
            result.stages.resize( first_stage );
        }
    }
    if( !stop ) {
        load_iris_noise( *selection.source, result );
        load_iris_custom_textures( *selection.source, result );
        result.needs_eye_brightness = std::any_of( result.stages.begin(), result.stages.end(),
        []( const auto &stage ) {
            return stage.layout.uniforms.count( "eyeBrightness" ) != 0;
        } ) || std::any_of( result.properties.uniform_expressions.begin(),
        result.properties.uniform_expressions.end(), []( const auto &expression ) {
            return expression.second.expression.find( "eyeBrightness" ) != std::string::npos;
        } );
    }
    return result;
}
} // namespace

const iris_compile_state &poll_iris_compilation()
{
    const auto selection = selected_iris_pack();
    if( compile_state.revision != selection.revision ) {
        if( compile_stop ) {
            *compile_stop = true;
        }
        compile_state = {};
        compile_state.revision = selection.revision;
        const char *script = SDL_getenv( "CATA_IRIS_COMPILER" );
        compile_state.running = selection.source && selection.source->error.empty() &&
                                script && *script;
    }
    if( compile_future.valid() && compile_future.wait_for( std::chrono::seconds( 0 ) ) ==
        std::future_status::ready ) {
        auto finished = compile_future.get();
        if( finished.revision == selection.revision ) {
            compile_state = std::move( finished );
        }
        compile_stop.reset();
    }
    if( compile_state.running && !compile_future.valid() ) {
        compile_stop = std::make_shared<std::atomic<bool>>( false );
        const auto script = std::string( SDL_getenv( "CATA_IRIS_COMPILER" ) );
        const char *python = SDL_getenv( "CATA_IRIS_PYTHON" );
        const char *dxc = SDL_getenv( "CATA_IRIS_DXC" );
        const auto cache = fs::u8path( PATH_INFO::user_dir() ) / "cache" / "iris" /
                           selection.source->path.filename();
        try {
            compile_future = std::async( std::launch::async, [selection, cache, script, stop = compile_stop,
        python = std::string( python ? python : "python" ), dxc = std::string( dxc ? dxc : "" )] {
            try {
                return compile_iris_sources( selection, cache, python, script, dxc, *stop );
            } catch( const std::exception &error ) {
                return iris_compile_state{ selection.revision, false, {}, { error.what() } };
            }
            } );
        } catch( const std::system_error &error ) {
            compile_state.running = false;
            compile_state.diagnostics.push_back( error.what() );
        }
    }
    return compile_state;
}

void stop_iris_compilation()
{
    if( compile_stop ) {
        *compile_stop = true;
    }
    if( compile_future.valid() ) {
        compile_future.wait();
        compile_future = {};
    }
    compile_stop.reset();
    compile_state = {};
}

static void refresh_selected_pack()
{
    const auto iris = selected_iris_pack();
    poll_iris_compilation();
    const auto *legacy = selected_gpu_profile();
    if( iris.source && !legacy ) {
        if( !iris.source->error.empty() ) {
            report = "Cataclysm Iris 无法解析当前光影包：" + iris.source->error;
        } else {
            report = "Cataclysm Iris 已读取当前光影包，等待管线验证。当前使用原生 3D 画面。";
        }
    }
}

void request_reload()
{
    restore_choice();
    ++selection_revision;
    report = "已刷新光影包；返回 3D 世界后应用。";
    refresh_selected_pack();
}

std::string selected()
{
    restore_choice();
    return choice;
}

const fp3d_mcshader::profile *selected_gpu_profile()
{
    restore_choice();
    if( gpu_profile_revision != selection_revision ) {
        selected_iris_pack();
        gpu_profile_revision = selection_revision;
        gpu_profile = {};
        if( !builtin( choice ) ) {
            const auto path = root_path() / fs::u8path( choice );
            const auto pack = inspect_pack( path );
            if( pack.compatible() ) {
                gpu_profile = pack.profile;
            } else {
                std::error_code error;
                const bool exists = fs::exists( path, error );
                report = exists || error ? "当前光影包无法在 SDL3 GPU 加载，已使用关闭光影的画面。" :
                         "当前光影包文件缺失，已使用关闭光影的画面。";
            }
        }
    }
    return gpu_profile.recognized && !gpu_profile.direct_final ?
           &gpu_profile : nullptr;
}

bool choose( const std::string &id )
{
    const auto entries = scan();
    const auto found = std::find_if( entries.begin(), entries.end(), [&]( const entry &item ) {
        return item.id == id && item.compatible;
    } );
    if( found == entries.end() ) {
        report = "该光影包格式暂不支持。";
        return false;
    }
    choice = id;
    ++selection_revision;
    report = id == "off" ? "光影已关闭，返回游戏后恢复原版 3D。" :
             "已选择光影；返回 3D 世界后应用。";
    refresh_selected_pack();
    std::ofstream file( root_path() / ".selected", std::ios::binary | std::ios::trunc );
    if( file ) {
        file << id << '\n';
    } else {
        report = "光影已选择，但偏好无法写入文件。";
    }
    return true;
}

float screen_brightness()
{
    return get_option<float>( "FP3D_IRIS_BRIGHTNESS" );
}

std::string status()
{
    const auto &iris = poll_iris_compilation();
    if( iris.running ) {
        return "Cataclysm Iris 正在后台编译。";
    }
    if( !iris.stages.empty() || !iris.diagnostics.empty() || !iris.water_diagnostics.empty() ||
        !iris.unused_minecraft_programs.empty() ) {
        std::string summary = "Cataclysm Iris 已编译 " + std::to_string( iris.stages.size() ) +
                              " 个阶段，诊断 " +
                              std::to_string( iris.diagnostics.size() + iris.water_diagnostics.size() ) + " 条。";
        if( !iris.diagnostics.empty() ) {
            summary += "\n" + iris.diagnostics.front().substr( 0, 260 );
        }
        if( !iris.water_diagnostics.empty() ) {
            summary += "\n水面程序编译失败，使用原生水面。\n" + iris.water_diagnostics.front().substr( 0, 260 );
        }
        for( const auto &program : iris.unused_minecraft_programs ) {
            summary += "\nCDDA 无对应绘制内容，未执行 " + program;
        }
        return summary;
    }
    return report;
}

bool shadows_enabled()
{
    restore_choice();
    return native_shadows_enabled;
}

void set_shadows_enabled( bool enabled )
{
    restore_choice();
    native_shadows_enabled = enabled;
    std::ofstream file( root_path() / ".shadows", std::ios::trunc );
    if( file ) { file << static_cast<int>( enabled ) << '\n'; }
    else { report = "太阳阴影选项已改变，但无法保存到游戏目录。"; }
}

bool wet_ssr_enabled()
{
    restore_choice();
    return wet_ssr;
}

void set_wet_ssr_enabled( bool enabled )
{
    restore_choice();
    wet_ssr = enabled;
    std::ofstream file( root_path() / ".wet_ssr", std::ios::trunc );
    if( file ) { file << static_cast<int>( enabled ) << '\n'; }
    else { report = "湿地反射已更改，但无法保存选项。"; }
}

bool local_lights_enabled()
{
    restore_choice();
    return native_local_lights;
}

void set_local_lights_enabled( bool enabled )
{
    restore_choice();
    native_local_lights = enabled;
    std::ofstream file( root_path() / ".local_lights", std::ios::trunc );
    if( file ) { file << static_cast<int>( enabled ) << '\n'; }
    else { report = "动态灯光设置未能写入光影目录。"; }
}

bool local_shadows_enabled()
{
    restore_choice();
    return native_local_shadows;
}

void set_local_shadows_enabled( bool enabled )
{
    restore_choice();
    native_local_shadows = enabled;
    std::ofstream file( root_path() / ".local_shadows", std::ios::trunc );
    if( file ) { file << static_cast<int>( enabled ) << '\n'; }
    else { report = "局部光源阴影设置未能写入光影目录。"; }
}

bool shadow_volume_enabled()
{
    restore_choice();
    return shadow_volume;
}

void set_shadow_volume_enabled( bool enabled )
{
    restore_choice();
    shadow_volume = enabled;
    std::ofstream file( root_path() / ".shadow_volume", std::ios::trunc );
    if( file ) { file << static_cast<int>( enabled ) << '\n'; }
    else { report = "体积太阳光已更改，但无法保存选项。"; }
}

bool pbr_enabled()
{
    restore_choice();
    return experimental_pbr;
}

void set_pbr_enabled( bool enabled )
{
    restore_choice();
    experimental_pbr = enabled;
    std::ofstream file( root_path() / ".pbr", std::ios::trunc );
    if( file ) { file << static_cast<int>( enabled ) << '\n'; }
    else { report = "PBR 法线选项已修改，但无法保存到游戏目录。"; }
}

int pbr_strength_percent()
{
    restore_choice();
    return pbr_strength;
}

void set_pbr_strength_percent( int percent )
{
    restore_choice();
    pbr_strength = std::clamp( percent, 0, 200 );
    std::ofstream file( root_path() / ".pbr_strength", std::ios::trunc );
    if( file ) { file << pbr_strength << '\n'; }
    else { report = "PBR 细节强度已更改，但无法保存到游戏目录。"; }
}

int mc_intensity_percent()
{
    restore_choice();
    return mc_intensity;
}

void set_mc_intensity_percent( int percent )
{
    restore_choice();
    mc_intensity = std::clamp( percent, 50, 200 );
    std::ofstream file( root_path() / ".intensity", std::ios::trunc );
    if( file ) {
        file << mc_intensity << '\n';
    } else {
        report = "光影视觉强度已更新，但无法保存设置。";
    }
}

int quality()
{
    restore_choice();
    return quality_level;
}

void set_quality( int level )
{
    restore_choice();
    quality_level = std::clamp( level, 0, 3 );
    std::ofstream file( root_path() / ".quality", std::ios::trunc );
    if( file ) { file << quality_level << '\n'; }
    else { report = "质量已改变，但无法保存到游戏目录。"; }
}

} // namespace fp3d_shaderpacks
#endif
