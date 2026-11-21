#include "fp3d_shaderpacks.h"
#include "fp3d_shaderpacks_internal.h"
#include "fp3d_shader_archive.h"
#include "fp3d_mcshader.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif
#include "sdl_wrappers.h"

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
        if( !saved.empty() && saved.size() < 256 &&
            saved.find_first_of( "\r\n/" ) == std::string::npos &&
            saved.find( '\\' ) == std::string::npos ) {
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
        if( builtin( id ) || ( !it->is_directory( entry_error ) &&
                               ( path.extension() != ".zip" || !it->is_regular_file( entry_error ) ) ) ) {
            continue;
        }
        const auto pack = inspect_pack( path );
        const bool compatible = pack.compatible();
        const std::string note = compatible ? pack.profile.label + " · SDL3 GPU 光影适配" :
                                 ( pack.native || pack.profile.direct_final ) ? "此包需要移植 GLSL 后处理" :
                                 "无可识别光影入口";
        result.push_back( { id, id, note, compatible } );
    }
    if( error ) {
        ( scan_error ? *scan_error : report ) = "扫描光影文件夹时发生错误：" + error.message();
    }
    return result;
}

static std::uint64_t gpu_profile_revision = 0;
static fp3d_mcshader::profile gpu_profile;

void request_reload()
{
    restore_choice();
    ++selection_revision;
    report = "已刷新光影包；返回 3D 世界后应用。";
    selected_gpu_profile();
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
    selected_gpu_profile();
    std::ofstream file( root_path() / ".selected", std::ios::binary | std::ios::trunc );
    if( file ) {
        file << id << '\n';
    } else {
        report = "光影已选择，但偏好无法写入文件。";
    }
    return true;
}

std::string status()
{
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
