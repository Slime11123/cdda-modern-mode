#include "fp3d_iris_pack.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>

#include "fp3d_shader_archive.h"

namespace fp3d_iris
{
namespace
{
namespace fs = std::filesystem;

std::optional<shader_stage> stage_for( std::string_view extension )
{
    constexpr std::pair<std::string_view, shader_stage> stages[] = {
        { ".vsh", shader_stage::vertex }, { ".fsh", shader_stage::fragment },
        { ".gsh", shader_stage::geometry }, { ".tcs", shader_stage::tess_control },
        { ".tes", shader_stage::tess_evaluation }, { ".csh", shader_stage::compute }
    };
    for( const auto &[suffix, stage] : stages ) {
        if( extension == suffix ) {
            return stage;
        }
    }
    return std::nullopt;
}

bool program_name( std::string_view name )
{
    if( name == "shadow" || name == "final" || name.substr( 0, 8 ) == "gbuffers" ||
        name.substr( 0, 7 ) == "shadow_" ) {
        return true;
    }
    for( const std::string_view prefix : { "setup", "begin", "shadowcomp", "prepare", "deferred",
                                          "composite" } ) {
        if( name.substr( 0, prefix.size() ) == prefix ) {
            const auto suffix = name.substr( prefix.size() );
            if( suffix.empty() || ( suffix[0] >= '0' && suffix[0] <= '9' ) ||
                suffix[0] == '_' ) {
                return true;
            }
        }
    }
    return false;
}

bool directory_entries( const fs::path &path, std::vector<std::string> &entries )
{
    std::error_code error;
    fs::recursive_directory_iterator it( path, error );
    for( ; !error && it != fs::recursive_directory_iterator(); it.increment( error ) ) {
        const auto status = it->symlink_status( error );
        if( error ) {
            return false;
        }
        if( it.depth() >= 12 ) {
            it.disable_recursion_pending();
        }
        if( fs::is_regular_file( status ) ) {
            if( entries.size() >= 8192 ) {
                return false;
            }
            entries.push_back( it->path().lexically_relative( path ).generic_u8string() );
        }
    }
    return !error;
}

bool text_file( std::string_view name )
{
    const auto dot = name.rfind( '.' );
    if( dot == std::string_view::npos ) {
        return false;
    }
    const auto extension = name.substr( dot );
    return stage_for( extension ).has_value() || extension == ".glsl" || extension == ".inc" ||
           extension == ".properties" || extension == ".lang";
}

bool within_root( const fs::path &path )
{
    return !path.empty() && !path.has_root_path() && *path.begin() != "..";
}

void strip_space( std::string_view &text )
{
    const auto start = text.find_first_not_of( " \t\r" );
    text.remove_prefix( start == std::string_view::npos ? text.size() : start );
}

std::string directive_line( std::string line, bool &comment )
{
    bool quoted = false;
    for( size_t i = 0; i < line.size(); ++i ) {
        if( comment ) {
            const bool end = line.compare( i, 2, "*/" ) == 0;
            line[i] = ' ';
            if( end ) {
                line[++i] = ' ';
                comment = false;
            }
        } else if( line[i] == '"' ) {
            quoted = !quoted;
        } else if( !quoted && line.compare( i, 2, "//" ) == 0 ) {
            line.resize( i );
            break;
        } else if( !quoted && line.compare( i, 2, "/*" ) == 0 ) {
            line[i] = line[i + 1] = ' ';
            ++i;
            comment = true;
        }
    }
    return line;
}
} // namespace

pack_inventory inspect_pack( const std::filesystem::path &path )
{
    std::error_code error;
    std::vector<std::string> entries;
    const bool directory = fs::is_directory( path, error );
    if( error || !( directory ? directory_entries( path, entries ) :
                    fp3d_shader_archive::list_entries( path, entries ) ) ) {
        return { "Cannot read shader pack: " + path.u8string(), {}, {}, {} };
    }
    std::set<std::string> roots;
    for( const auto &entry : entries ) {
        const auto slash = entry.find( '/' );
        if( slash == std::string::npos ) {
            continue;
        }
        if( entry.compare( 0, 8, "shaders/" ) == 0 ) {
            roots.emplace( "shaders/" );
        } else if( entry.compare( slash + 1, 8, "shaders/" ) == 0 ) {
            roots.emplace( entry.substr( 0, slash + 9 ) );
        }
    }
    if( roots.size() != 1 ) {
        return { roots.empty() ? "No shaders folder found" : "Multiple shaders folders found", {}, {}, {} };
    }
    pack_inventory pack;
    pack.shader_root = *roots.begin();
    for( const auto &entry : entries ) {
        if( entry.compare( 0, pack.shader_root.size(), pack.shader_root ) != 0 ) {
            continue;
        }
        const std::string file = entry.substr( pack.shader_root.size() );
        pack.files.push_back( file );
        const auto dot = file.rfind( '.' );
        const auto slash = file.rfind( '/' );
        if( dot == std::string::npos ||
            ( slash != std::string::npos && ( dot < slash || file.find( '/' ) != slash ) ) ) {
            continue;
        }
        const size_t start = slash == std::string::npos ? 0 : slash + 1;
        const std::string_view name( file.data() + start, dot - start );
        if( const auto stage = stage_for( std::string_view( file ).substr( dot ) );
            stage && program_name( name ) ) {
            pack.programs[file.substr( 0, dot )].emplace( *stage, file );
        }
    }
    std::sort( pack.files.begin(), pack.files.end() );
    if( std::adjacent_find( pack.files.begin(), pack.files.end() ) != pack.files.end() ) {
        return { "Duplicate shader pack file", {}, {}, {} };
    }
    if( pack.programs.empty() ) {
        pack.error = "No shader programs found";
    }
    return pack;
}

pack_text load_pack_text( const fs::path &path, const pack_inventory &inventory )
{
    if( !inventory.error.empty() ) {
        return { inventory.error, {} };
    }
    pack_text result;
    std::map<std::string, std::string> entries;
    for( const auto &file : inventory.files ) {
        if( text_file( file ) ) {
            entries.emplace( inventory.shader_root + file, std::string{} );
        }
    }
    std::error_code error;
    if( fs::is_directory( path, error ) ) {
        const auto package_root = fs::canonical( path, error );
        if( error ) {
            return { "Cannot resolve shader pack directory", {} };
        }
        const auto root = fs::canonical( path / fs::u8path( inventory.shader_root ), error );
        if( error || !within_root( root.lexically_relative( package_root ) ) ) {
            return { "Cannot resolve shaders directory", {} };
        }
        size_t remaining = 16 * 1024 * 1024;
        for( auto &[name, contents] : entries ) {
            const auto file = fs::canonical( path / fs::u8path( name ), error );
            if( error || !within_root( file.lexically_relative( root ) ) ) {
                return { "Shader text escapes source directory: " + name, {} };
            }
            const auto size = fs::file_size( file, error );
            if( error || size > 256 * 1024 || size > remaining ) {
                return { "Cannot read shader text: " + name, {} };
            }
            contents.resize( size );
            std::ifstream input( file, std::ios::binary );
            if( !input.read( contents.data(), contents.size() ) || contents.find( '\0' ) != std::string::npos ) {
                return { "Cannot read shader text: " + name, {} };
            }
            remaining -= size;
        }
    } else {
        std::string failed;
        if( error || !fp3d_shader_archive::read_text_entries( path, entries, failed ) ) {
            return { failed.empty() ? "Cannot read shader archive" : "Cannot read shader text: " + failed, {} };
        }
    }
    for( auto &[name, contents] : entries ) {
        if( contents.compare( 0, 3, "\xef\xbb\xbf" ) == 0 ) {
            contents.erase( 0, 3 );
        }
        result.files.emplace( name.substr( inventory.shader_root.size() ), std::move( contents ) );
    }
    return result;
}

bool read_pack_asset( const pack_source &pack, const std::string &name, std::string &data )
{
    if( !std::binary_search( pack.inventory.files.begin(), pack.inventory.files.end(), name ) ) {
        return false;
    }
    std::error_code error;
    if( !fs::is_directory( pack.path, error ) ) {
        return !error && fp3d_shader_archive::read_binary_entry( pack.path,
                pack.inventory.shader_root + name, data );
    }
    const auto package = fs::canonical( pack.path, error );
    if( error ) {
        return false;
    }
    const auto root = fs::canonical( package / fs::u8path( pack.inventory.shader_root ), error );
    if( error || !within_root( root.lexically_relative( package ) ) ) {
        return false;
    }
    const auto file = fs::canonical( root / fs::u8path( name ), error );
    if( error || !within_root( file.lexically_relative( root ) ) ) {
        return false;
    }
    const auto size = fs::file_size( file, error );
    if( error || !size || size > 16 * 1024 * 1024 ) {
        return false;
    }
    data.resize( size );
    std::ifstream input( file, std::ios::binary );
    return static_cast<bool>( input.read( data.data(), data.size() ) );
}

expanded_shader expand_includes( const pack_text &pack, const std::string &entry )
{
    if( !pack.error.empty() ) {
        return { pack.error, {}, {} };
    }
    expanded_shader result;
    std::set<std::string> active;
    std::map<std::string, size_t> ids;
    const auto expand = [&]( const auto &self, const std::string &name ) -> bool {
        const auto source = pack.files.find( name );
        if( source == pack.files.end() || active.count( name ) || active.size() >= 64 ) {
            result.error = source == pack.files.end() ? "Missing shader source: " + name :
                           active.count( name ) ? "Cyclic shader include: " + name :
                           "Shader include depth exceeds limit at: " + name;
            return false;
        }
        active.insert( name );
        const auto [id, inserted] = ids.try_emplace( name, ids.size() );
        if( inserted ) {
            result.source_paths.push_back( name );
        }
        if( active.size() > 1 ) {
            result.source += "#line 1 " + std::to_string( id->second ) + "\n";
        }
        std::istringstream stream( source->second );
        std::string line;
        bool comment = false;
        size_t number = 0;
        while( std::getline( stream, line ) ) {
            ++number;
            const auto visible = directive_line( line, comment );
            std::string_view directive( visible );
            strip_space( directive );
            if( !directive.empty() && directive.front() == '#' ) {
                directive.remove_prefix( 1 );
                strip_space( directive );
            } else {
                directive = {};
            }
            if( directive.substr( 0, 7 ) == "include" && ( directive.size() == 7 ||
                directive[7] == '"' || directive[7] == ' ' || directive[7] == '\t' ) ) {
                directive.remove_prefix( 7 );
                strip_space( directive );
                const auto closing = directive.find( '"', 1 );
                const auto location = name + ":" + std::to_string( number );
                if( directive.empty() || directive.front() != '"' || closing == std::string_view::npos ||
                    closing == 1 || comment || directive.find_first_not_of( " \t\r", closing + 1 ) !=
                    std::string_view::npos ) {
                    result.error = location + ": invalid shader include";
                    return false;
                }
                const auto include = directive.substr( 1, closing - 1 );
                const bool rooted = include.front() == '/';
                const auto target = fs::u8path( rooted ? include.substr( 1 ) : include );
                const auto relative = ( rooted ? target : fs::u8path( name ).parent_path() / target ).lexically_normal();
                if( include.find_first_of( "\\:" ) != std::string_view::npos || target.has_root_path() ||
                    !within_root( relative ) ) {
                    result.error = location + ": shader include escapes source directory";
                    return false;
                }
                // Preserve a leading comment terminator before replacing the directive.
                result.source.append( line, 0, visible.find( '#' ) );
                result.source += '\n';
                if( !self( self, relative.generic_u8string() ) ) {
                    result.error = location + ": " + result.error;
                    return false;
                }
                result.source += "#line " + std::to_string( number + 1 ) + " " +
                                 std::to_string( id->second ) + "\n";
            } else {
                result.source += line + '\n';
            }
            if( result.source.size() > 4 * 1024 * 1024 ) {
                result.error = "Expanded shader exceeds size limit: " + name;
                return false;
            }
        }
        active.erase( name );
        return true;
    };
    if( !expand( expand, entry ) ) {
        result.source.clear();
        result.source_paths.clear();
    }
    return result;
}
} // namespace fp3d_iris
#endif
