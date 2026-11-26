#include "fp3d_iris_properties.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>

#include "fp3d_iris_pack.h"

namespace fp3d_iris
{
namespace
{
constexpr std::string_view whitespace = " \t\f\r";

std::string_view left_trim( std::string_view text )
{
    const auto start = text.find_first_not_of( whitespace );
    return start == text.npos ? std::string_view{} : text.substr( start );
}

std::string unescape( std::string_view text )
{
    std::string output;
    for( size_t i = 0; i < text.size(); ++i ) {
        if( text[i] != '\\' || i + 1 == text.size() ) {
            output += text[i];
            continue;
        }
        switch( const char next = text[++i]; next ) {
            case 't': output += '\t'; break;
            case 'n': output += '\n'; break;
            case 'r': output += '\r'; break;
            case 'f': output += '\f'; break;
            // Preserve Unicode escape text for later decoding.
            case 'u': output += "\\u"; break;
            default: output += next; break;
        }
    }
    return output;
}

std::vector<std::string> words( const std::string &value )
{
    std::istringstream input( value );
    std::vector<std::string> result;
    for( std::string word; input >> word; ) {
        result.push_back( std::move( word ) );
    }
    return result;
}

std::string color_buffer_name( std::string_view name )
{
    constexpr std::array<std::string_view, 8> aliases{
        "gcolor", "gdepth", "gnormal", "composite", "gaux1", "gaux2", "gaux3", "gaux4"
    };
    const auto alias = std::find( aliases.begin(), aliases.end(), name );
    if( alias != aliases.end() ) {
        return "colortex" + std::to_string( alias - aliases.begin() );
    }
    constexpr std::string_view prefix = "colortex";
    if( name.substr( 0, prefix.size() ) != prefix || name.size() <= prefix.size() ) {
        return {};
    }
    unsigned slot = 0;
    const auto end = name.data() + name.size();
    const auto index = std::from_chars( name.data() + prefix.size(), end, slot );
    return index.ec == std::errc{} && index.ptr == end && slot < 32 ?
           "colortex" + std::to_string( slot ) : std::string{};
}

std::optional<buffer_dimension> parse_dimension( const std::string &token )
{
    const char *start = token.data() + ( !token.empty() && token.front() == '+' );
    const char *end = token.data() + token.size();
    if( token.find( '.' ) != token.npos ) {
        float value = 0;
        const auto number = std::from_chars( start, end, value );
        if( number.ec == std::errc{} && number.ptr == end && std::isfinite( value ) && value > 0 ) {
            return buffer_dimension{ value, true };
        }
    } else {
        std::int32_t value = 0;
        const auto number = std::from_chars( start, end, value );
        if( number.ec == std::errc{} && number.ptr == end && value > 0 ) {
            return buffer_dimension{ static_cast<double>( value ), false };
        }
    }
    return std::nullopt;
}

std::optional<graph_viewport> parse_viewport( const std::string &value )
{
    const auto tokens = words( value );
    if( tokens.empty() || tokens.size() > 3 ) {
        return std::nullopt;
    }
    graph_viewport viewport{ 1, 0, 0 };
    const std::array<float *, 3> fields{ &viewport.scale, &viewport.x, &viewport.y };
    for( size_t i = 0; i < tokens.size(); ++i ) {
        const auto &token = tokens[i];
        const char *start = token.data() + ( token.front() == '+' );
        const char *end = token.data() + token.size();
        const auto number = std::from_chars( start, end, *fields[i] );
        if( number.ec != std::errc{} || number.ptr != end || !std::isfinite( *fields[i] ) ) {
            return std::nullopt;
        }
    }
    return viewport;
}

class property_expression
{
    public:
        property_expression( std::string_view text, const property_macros &macros ) :
            rest_( text ), macros_( macros ) {}

        std::optional<bool> evaluate() {
            const auto value = logical_or();
            return valid_ && left_trim( rest_ ).empty() ? std::optional<bool>( value != 0 ) : std::nullopt;
        }

    private:
        std::string_view rest_;
        const property_macros &macros_;
        bool valid_ = true;
        unsigned depth_ = 0;

        bool take( std::string_view token ) {
            rest_ = left_trim( rest_ );
            if( rest_.substr( 0, token.size() ) != token ) {
                return false;
            }
            rest_.remove_prefix( token.size() );
            return true;
        }

        std::string_view name() {
            rest_ = left_trim( rest_ );
            constexpr std::string_view letters = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
            if( rest_.empty() || letters.find( rest_.front() ) == letters.npos ) {
                return {};
            }
            const auto end = rest_.find_first_not_of(
                                 "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" );
            const auto result = rest_.substr( 0, end );
            rest_.remove_prefix( result.size() );
            return result;
        }

        long long atom() {
            if( ++depth_ > 64 ) {
                valid_ = false;
                --depth_;
                return 0;
            }
            const auto value = unary();
            --depth_;
            return value;
        }

        long long unary() {
            if( take( "!" ) ) {
                return !atom();
            }
            if( take( "(" ) ) {
                const auto result = logical_or();
                if( !take( ")" ) ) {
                    valid_ = false;
                }
                return result;
            }
            const auto id = name();
            if( id == "true" || id == "false" ) {
                return id == "true";
            }
            if( id == "defined" ) {
                const bool brackets = take( "(" );
                const auto symbol = name();
                if( symbol.empty() || ( brackets && !take( ")" ) ) ) {
                    valid_ = false;
                }
                return macros_.count( std::string( symbol ) );
            }
            if( !id.empty() ) {
                const auto macro = macros_.find( std::string( id ) );
                if( macro == macros_.end() ) {
                    return 0;
                }
                auto text = left_trim( macro->second );
                if( text.empty() ) {
                    return 1;
                }
                long long result = 0;
                const auto end = text.data() + text.size();
                const auto number = std::from_chars( text.data(), end, result );
                if( number.ec != std::errc{} ||
                    !left_trim( std::string_view( number.ptr, end - number.ptr ) ).empty() ) {
                    valid_ = false;
                }
                return result;
            }
            long long result = 0;
            if( rest_.empty() ) {
                valid_ = false;
                return result;
            }
            const auto number = std::from_chars( rest_.data(), rest_.data() + rest_.size(), result );
            if( number.ec != std::errc{} ) {
                valid_ = false;
                return 0;
            }
            rest_.remove_prefix( number.ptr - rest_.data() );
            return result;
        }

        long long comparison() {
            const auto left = atom();
            if( take( ">=" ) ) {
                return left >= atom();
            }
            if( take( "<=" ) ) {
                return left <= atom();
            }
            if( take( "==" ) ) {
                return left == atom();
            }
            if( take( "!=" ) ) {
                return left != atom();
            }
            if( take( ">" ) ) {
                return left > atom();
            }
            if( take( "<" ) ) {
                return left < atom();
            }
            return left;
        }

        long long logical_and() {
            auto value = comparison();
            while( take( "&&" ) ) {
                const auto right = comparison();
                value = value && right;
            }
            return value;
        }

        long long logical_or() {
            auto value = logical_and();
            while( take( "||" ) ) {
                const auto right = logical_and();
                value = value || right;
            }
            return value;
        }
};

std::optional<size_t> select_properties( std::vector<property_record> &records, property_macros macros )
{
    struct branch {
        bool parent;
        bool matched;
        bool else_seen;
    };
    std::vector<branch> scopes;
    bool active = true;
    for( auto &record : records ) {
        if( !record.directive ) {
            record.conditional = !active;
            continue;
        }
        const auto &key = record.key;
        auto value = std::string_view( record.value );
        value = value.substr( 0, value.find( "//" ) );
        const auto symbol = value.substr( 0, value.find_first_of( whitespace ) );
        if( key == "if" || key == "ifdef" || key == "ifndef" ) {
            std::optional<bool> condition = false;
            if( active ) {
                condition = key == "if" ? property_expression( value, macros ).evaluate() :
                            std::optional<bool>( ( macros.count( std::string( symbol ) ) != 0 ) ==
                                                 ( key == "ifdef" ) );
            }
            if( !condition || scopes.size() >= 64 ||
                ( key != "if" && ( symbol.empty() || !left_trim( value.substr( symbol.size() ) ).empty() ) ) ) {
                return record.line;
            }
            scopes.push_back( { active, *condition, false } );
            active = active && *condition;
        } else if( key == "else" || key == "elif" || key == "endif" ) {
            if( scopes.empty() ) {
                return record.line;
            }
            auto &scope = scopes.back();
            if( key == "endif" ) {
                active = scope.parent;
                scopes.pop_back();
                continue;
            }
            if( scope.else_seen ) {
                return record.line;
            }
            auto condition = std::optional<bool>( key == "else" );
            if( key == "elif" && scope.parent && !scope.matched ) {
                condition = property_expression( value, macros ).evaluate();
            }
            if( !condition ) {
                return record.line;
            }
            active = scope.parent && !scope.matched && *condition;
            scope.matched |= active;
            scope.else_seen = key == "else";
        } else if( active ) {
            if( key == "define" && !symbol.empty() && symbol.find( '(' ) == symbol.npos ) {
                macros[std::string( symbol )] = left_trim( value.substr( symbol.size() ) );
            } else if( key == "undef" && !symbol.empty() ) {
                macros.erase( std::string( symbol ) );
            } else {
                return record.line;
            }
        }
    }
    return scopes.empty() ? std::nullopt : std::optional<size_t>( records.back().line );
}
} // namespace

std::vector<property_record> parse_properties( std::string_view source )
{
    std::vector<property_record> records;
    std::istringstream input{ std::string( source ) };
    std::string logical;
    size_t conditional_depth = 0;
    size_t number = 0;
    size_t start = 0;
    bool continued = false;
    for( std::string line; std::getline( input, line ); ) {
        ++number;
        if( !line.empty() && line.back() == '\r' ) {
            line.pop_back();
        }
        if( !continued ) {
            start = number;
            logical.clear();
        }
        const auto content = left_trim( line );
        if( !continued && !content.empty() && ( content.front() == '#' || content.front() == '!' ) ) {
            if( content.front() == '#' ) {
                auto body = left_trim( content.substr( 1 ) );
                const auto command = body.substr( 0, body.find_first_of( whitespace ) );
                if( command == "if" || command == "ifdef" || command == "ifndef" ) {
                    ++conditional_depth;
                }
                if( command == "if" || command == "ifdef" || command == "ifndef" ||
                    command == "elif" || command == "else" || command == "endif" ||
                    command == "define" || command == "undef" || command == "include" ) {
                    records.push_back( { std::string( command ),
                                         std::string( left_trim( body.substr( command.size() ) ) ),
                                         number, true, conditional_depth != 0 } );
                }
                if( command == "endif" && conditional_depth ) {
                    --conditional_depth;
                }
            }
            continue;
        }
        auto last = content.find_last_not_of( '\\' );
        const auto backslashes = last == content.npos ? content.size() : content.size() - last - 1;
        continued = backslashes % 2 != 0 && input.peek() != std::char_traits<char>::eof();
        logical += content.substr( 0, content.size() - ( continued ? 1 : 0 ) );
        if( continued || logical.empty() ) {
            continue;
        }
        size_t split = 0;
        for( ; split < logical.size(); ++split ) {
            const char ch = logical[split];
            if( ch == '\\' && split + 1 < logical.size() ) {
                ++split;
            } else if( ch == '=' || ch == ':' || whitespace.find( ch ) != whitespace.npos ) {
                break;
            }
        }
        const std::string_view text( logical );
        auto value = left_trim( text.substr( split ) );
        if( !value.empty() && ( value.front() == '=' || value.front() == ':' ) ) {
            value = left_trim( value.substr( 1 ) );
        }
        records.push_back( { unescape( text.substr( 0, split ) ), unescape( value ), start, false,
                             conditional_depth != 0 } );
    }
    return records;
}

pack_properties inspect_properties( const pack_text &pack, const property_macros *macros )
{
    pack_properties result;
    result.error = pack.error;
    if( !result.error.empty() ) {
        return result;
    }
    for( const auto &[file, source] : pack.files ) {
        const auto dot = file.rfind( '.' );
        if( dot == std::string::npos ) {
            continue;
        }
        const auto extension = std::string_view( file ).substr( dot );
        const bool language = file.compare( 0, 5, "lang/" ) == 0 && extension == ".lang";
        if( !language && extension != ".properties" ) {
            continue;
        }
        auto &document = result.documents.emplace( file, parse_properties( source ) ).first->second;
        auto selected = macros ? document : std::vector<property_record>{};
        if( macros ) {
            if( const auto line = select_properties( selected, *macros ) ) {
                result.error = file + ":" + std::to_string( *line ) + ": unsupported or invalid property directive";
                return result;
            }
        }
        const bool material = file == "block.properties" || file == "entity.properties" ||
                              file == "item.properties";
        for( const auto &record : macros ? selected : document ) {
            if( record.directive ) {
                continue;
            }
            if( macros && record.conditional ) {
                continue;
            }
            const auto &key = record.key;
            if( material ) {
                const auto separator = key.find( '.' );
                const auto type = key.substr( 0, separator );
                if( type == "block" || type == "entity" || type == "item" ) {
                    if( separator != key.npos ) {
                        int id = 0;
                        const auto end = key.data() + key.size();
                        const auto parsed = std::from_chars( key.data() + separator + 1, end, id );
                        if( parsed.ec == std::errc{} && parsed.ptr == end && id >= 0 ) {
                            result.materials[file].push_back(
                            { id, words( record.value ), record.line, record.conditional } );
                        }
                    }
                }
            }
            if( record.conditional ) {
                continue;
            }
            if( language ) {
                result.translations[file.substr( 5, dot - 5 )][key] = record.value;
            } else if( file == "dimension.properties" && key.compare( 0, 10, "dimension." ) == 0 ) {
                result.dimensions[key.substr( 10 )] = words( record.value );
            } else if( file == "shaders.properties" ) {
                constexpr std::string_view program_prefix = "program.";
                constexpr std::string_view enabled_suffix = ".enabled";
                if( key.compare( 0, 5, "flip." ) == 0 ) {
                    const auto separator = key.rfind( '.' );
                    const auto buffer = color_buffer_name( std::string_view( key ).substr( separator + 1 ) );
                    if( separator <= 5 || buffer.empty() ||
                        ( record.value != "true" && record.value != "false" ) ) {
                        result.error = file + ":" + std::to_string( record.line ) +
                                       ": invalid buffer flip " + key;
                        return result;
                    }
                    result.buffer_flips[key.substr( 5, separator - 5 )][buffer] = record.value == "true";
                } else if( key.compare( 0, 6, "scale." ) == 0 ) {
                    const auto viewport = parse_viewport( record.value );
                    if( key.size() == 6 || !viewport ) {
                        result.error = file + ":" + std::to_string( record.line ) +
                                       ": invalid program viewport " + key;
                        return result;
                    }
                    result.viewports[key.substr( 6 )] = *viewport;
                } else if( key.compare( 0, 12, "size.buffer." ) == 0 ) {
                    const auto buffer = color_buffer_name( std::string_view( key ).substr( 12 ) );
                    const auto dimensions = words( record.value );
                    const auto width = dimensions.size() == 2 ? parse_dimension( dimensions[0] ) : std::nullopt;
                    const auto height = dimensions.size() == 2 ? parse_dimension( dimensions[1] ) : std::nullopt;
                    if( buffer.empty() || !width || !height ) {
                        result.error = file + ":" + std::to_string( record.line ) +
                                       ": invalid buffer size " + key;
                        return result;
                    }
                    result.buffer_sizes[buffer] = { *width, *height };
                } else if( macros && key.size() > program_prefix.size() + enabled_suffix.size() &&
                    key.compare( 0, program_prefix.size(), program_prefix ) == 0 &&
                    key.compare( key.size() - enabled_suffix.size(), enabled_suffix.size(), enabled_suffix ) == 0 ) {
                    const auto value = property_expression( record.value, *macros ).evaluate();
                    if( !value ) {
                        result.error = file + ":" + std::to_string( record.line ) +
                                       ": unsupported program condition for " + key;
                        return result;
                    }
                    const auto name = key.substr( program_prefix.size(),
                                                 key.size() - program_prefix.size() - enabled_suffix.size() );
                    result.program_enabled[name] = *value;
                } else if( key == "sliders" ) {
                    result.sliders = words( record.value );
                } else if( key.compare( 0, 8, "profile." ) == 0 ) {
                    result.profiles[key.substr( 8 )] = words( record.value );
                } else if( key == "screen" || ( key.compare( 0, 7, "screen." ) == 0 &&
                            key != "screen.columns" && key.rfind( ".columns" ) != key.size() - 8 ) ) {
                    result.screens[key == "screen" ? "" : key.substr( 7 )] = words( record.value );
                }
            }
        }
    }
    return result;
}
} // namespace fp3d_iris
#endif
