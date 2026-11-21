#include "fp3d_mcshader.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include "fp3d_shader_archive.h"

namespace fp3d_mcshader
{
namespace
{
namespace fs = std::filesystem;
bool read_file( const fs::path &file, std::string &source )
{
    std::error_code error;
    if( !fs::is_regular_file( file, error ) || error ||
        fs::file_size( file, error ) > 256 * 1024 || error ) {
        return false;
    }
    std::ifstream stream( file, std::ios::binary );
    if( !stream ) { return false; }
    source.assign( ( std::istreambuf_iterator<char>( stream ) ),
                    std::istreambuf_iterator<char>() );
    return !source.empty() && source.find( '\0' ) == std::string::npos;
}
bool read( const fs::path &file, const std::string &name, std::string &source )
{
    if( file.extension() == ".zip" ) {
        return fp3d_shader_archive::read_mc_entry( file, name, source );
    }
    return read_file( file / fs::u8path( name ), source );
}
bool defined( const std::string &source, const std::string &name, bool fallback )
{
    if( source.empty() ) { return fallback; }
    std::istringstream stream( source );
    std::string line;
    const std::string name_prefix = "#define " + name;
    bool found = false;
    while( std::getline( stream, line ) ) {
        const auto first = line.find_first_not_of( " \t" );
        if( first == std::string::npos ) { continue; }
        const std::string clean = line.substr( first );
        if( clean.compare( 0, name_prefix.size(), name_prefix ) == 0 &&
            ( clean.size() == name_prefix.size() ||
              std::isspace( static_cast<unsigned char>( clean[name_prefix.size()] ) ) ) ) {
            found = true;
        }
    }
    return found ? true : fallback;
}
float setting_number( const std::string &settings, const char *name, float fallback,
                      float minimum, float maximum )
{
    std::istringstream lines( settings );
    std::string line, directive, key;
    float value;
    while( std::getline( lines, line ) ) {
        std::istringstream tokens( line );
        if( tokens >> directive >> key >> value && directive == "#define" && key == name ) {
            return std::clamp( value, minimum, maximum );
        }
    }
    return fallback;
}

bool has_photon_wave_functions( const fs::path &file )
{
    std::string waves;
    if( !read( file, "shaders/include/surface/water_normal.glsl", waves ) ) {
        return false;
    }
    const auto begin = waves.find( "float gerstner_wave(" );
    const auto end = waves.find( "vec3 get_water_normal(" );
    return begin != std::string::npos && end != std::string::npos && end > begin;
}
bool self_contained_final( const std::string &source )
{
    if( source.empty() || source.size() > 64 * 1024 ||
        source.find( "#include" ) != std::string::npos ||
        source.find( "void main(" ) == std::string::npos ||
        source.find( "#version 120" ) == std::string::npos ) {
        return false;
    }
    // Recognize standalone passes so the pack menu can flag them for conversion.
    for( const char *forbidden : { "colortex1", "colortex2", "colortex3", "colortex4",
                                    "colortex5", "colortex6", "colortex7",
                                    "noisetex", "shadowcolor", "gbufferProjection",
                                    "sampler3D", "sampler2DArray",
                                    "gl_FragData[1]", "viewInverse", "previousCameraPosition" } ) {
        if( source.find( forbidden ) != std::string::npos ) {
            return false;
        }
    }
    return true;
}
} // namespace

profile inspect( const fs::path &file )
{
    profile p;
    std::string properties, settings, final;
    const bool props = read( file, "shaders/shaders.properties", properties );
    const bool config = read( file, "shaders/settings.glsl", settings );
    const bool final_source = read( file, "shaders/world0/final.fsh", final ) ||
                              read( file, "shaders/final.fsh", final );
    if( !props && !config && !final_source ) {
        return p;
    }
    p.recognized = true;
    std::string id = file.filename().u8string();
    std::transform( id.begin(), id.end(), id.begin(), []( unsigned char c ) {
        return static_cast<char>( std::tolower( c ) );
    } );
    p.photon = id.find( "photon" ) != std::string::npos ||
               properties.find( "PHOTON" ) != std::string::npos;
    p.label = p.photon ? "Photon" : "Minecraft/OptiFine";
    p.direct_final = final_source && self_contained_final( final );
    p.shadows = defined( settings, "SHADOW", true );
    p.bloom = defined( settings, "BLOOM", true );
    p.volumetric = defined( settings, "VOLUMETRIC_FOG", true ) ||
                   defined( settings, "VOLUMETRIC_LIGHT", false );
    p.clouds = defined( settings, "CLOUDS", true );
    p.ao = defined( settings, "SHADER_AO", true );
    p.reflections = defined( settings, "SSR", false );
    p.saturation = p.photon ? 1.22f : 1.13f;
    p.warmness = p.photon ? 0.16f : 0.07f;
    p.glow = p.bloom ? ( p.photon ? 1.05f : .82f ) : 0.0f;
    if( p.photon ) {
        auto &waves = p.water_waves;
        waves.iterations = static_cast<int>( setting_number( settings, "WATER_WAVE_ITERATIONS", 3, 1, 8 ) );
        waves.frequency = setting_number( settings, "WATER_WAVE_FREQUENCY", 1, 0, 1.9f );
        waves.persistence = setting_number( settings, "WATER_WAVE_PERSISTENCE", 1, 0, 1.9f );
        waves.lacunarity = setting_number( settings, "WATER_WAVE_LACUNARITY", 1, 0, 1.9f );
        waves.speed_still = setting_number( settings, "WATER_WAVE_SPEED_STILL", 1, 0, 1.9f );
        waves.speed_flowing = setting_number( settings, "WATER_WAVE_SPEED_FLOWING", 1, 0, 1.9f );
        waves.height_variation = defined( settings, "WATER_WAVES_HEIGHT_VARIATION", false );
        p.has_water_waves = has_photon_wave_functions( file );
        p.water_wave_strength = defined( settings, "WATER_WAVES", false ) ?
                               setting_number( settings, "WATER_WAVE_STRENGTH", 1, 0, 2 ) : 0;
        p.water_texture = settings.find( "#define WATER_TEXTURE WATER_TEXTURE_VANILLA" ) !=
                          std::string::npos ? 1.0f : 0.0f;
        p.water_absorption[0] = setting_number( settings, "WATER_ABSORPTION_R", .39f, 0, 1 );
        p.water_absorption[1] = setting_number( settings, "WATER_ABSORPTION_G", .14f, 0, 1 );
        p.water_absorption[2] = setting_number( settings, "WATER_ABSORPTION_B", .07f, 0, 1 );
    }
    return p;
}

} // namespace fp3d_mcshader
#endif
