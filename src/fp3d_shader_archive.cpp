#include "fp3d_shader_archive.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>
#include <zlib.h>

namespace fp3d_shader_archive
{
namespace
{
using byte = unsigned char;
constexpr size_t max_zip_size = 64 * 1024 * 1024;
constexpr size_t max_source_size = 256 * 1024;
constexpr size_t max_compressed_size = 1024 * 1024;

uint16_t u16( const std::vector<byte> &data, size_t i )
{
    return static_cast<uint16_t>( data[i] | ( static_cast<uint16_t>( data[i + 1] ) << 8 ) );
}
uint32_t u32( const std::vector<byte> &data, size_t i )
{
    return static_cast<uint32_t>( u16( data, i ) |
                                  ( static_cast<uint32_t>( u16( data, i + 2 ) ) << 16 ) );
}
bool range( size_t off, size_t length, size_t total )
{
    return off <= total && length <= total - off;
}
bool safe_name( const std::string &name )
{
    return !name.empty() && name.size() <= 256 &&
           name[0] != '/' && name.find( "\\" ) == std::string::npos &&
           name.find( "../" ) == std::string::npos &&
           name.find( "/.." ) == std::string::npos &&
           name.find( "//" ) == std::string::npos &&
           name.find( ':' ) == std::string::npos &&
           name.find( '\0' ) == std::string::npos;
}
bool matches( const std::string &actual, const std::string &relative, bool mc )
{
    if( !safe_name( actual ) ) { return false; }
    if( actual == relative ) { return true; }
    // Source snapshots downloaded from GitHub have one enclosing project root.
    // Native CDDA post.frag intentionally remains root-relative only.
    if( !mc ) { return false; }
    const size_t slash = actual.find( '/' );
    return slash != std::string::npos && slash > 0 &&
           actual.substr( slash + 1 ) == relative;
}
bool unpack( const std::vector<byte> &zip, size_t offset, uint16_t method,
             uint32_t packed, uint32_t unpacked, uint32_t checksum,
             std::string &source )
{
    if( !range( offset, packed, zip.size() ) || !unpacked ||
        unpacked > max_source_size || packed > max_compressed_size ) {
        return false;
    }
    std::vector<byte> result( unpacked );
    if( method == 0 ) {
        if( packed != unpacked ) { return false; }
        std::copy_n( zip.data() + offset, unpacked, result.data() );
    } else if( method == 8 ) {
        z_stream stream{};
        stream.next_in = const_cast<Bytef *>( zip.data() + offset );
        stream.avail_in = packed;
        stream.next_out = result.data();
        stream.avail_out = unpacked;
        if( inflateInit2( &stream, -MAX_WBITS ) != Z_OK ) { return false; }
        const int status = inflate( &stream, Z_FINISH );
        const bool success = status == Z_STREAM_END &&
                             stream.total_out == unpacked &&
                             stream.total_in == packed;
        inflateEnd( &stream );
        if( !success ) { return false; }
    } else {
        return false;
    }
    if( crc32( 0L, result.data(), static_cast<uInt>( result.size() ) ) != checksum ) {
        return false;
    }
    source.assign( reinterpret_cast<const char *>( result.data() ), result.size() );
    return source.find( '\0' ) == std::string::npos;
}

bool read_entry( const std::filesystem::path &file, const std::string &relative,
                 std::string &source, bool mc )
{
    source.clear();
    if( !safe_name( relative ) ) { return false; }
    std::error_code error;
    const auto length = std::filesystem::file_size( file, error );
    if( error || length < 22 || length > max_zip_size ) { return false; }
    std::ifstream stream( file, std::ios::binary );
    if( !stream ) { return false; }
    const std::vector<byte> zip( ( std::istreambuf_iterator<char>( stream ) ),
                                 std::istreambuf_iterator<char>() );
    if( zip.size() != length ) { return false; }
    const size_t earliest = zip.size() > 65557 ? zip.size() - 65557 : 0;
    size_t eocd = zip.size();
    for( size_t i = zip.size() - 22;; --i ) {
        if( u32( zip, i ) == 0x06054b50 &&
            range( i, 22 + u16( zip, i + 20 ), zip.size() ) ) {
            eocd = i;
            break;
        }
        if( i == earliest ) { break; }
    }
    if( eocd == zip.size() || u16( zip, eocd + 4 ) != 0 ||
        u16( zip, eocd + 6 ) != 0 ) {
        return false;
    }
    const uint16_t count = u16( zip, eocd + 10 );
    const size_t directory_offset = u32( zip, eocd + 16 );
    const size_t directory_size = u32( zip, eocd + 12 );
    if( !count || count > 2048 || !range( directory_offset, directory_size, eocd ) ) {
        return false;
    }
    size_t cursor = directory_offset;
    for( size_t entry = 0; entry < count; ++entry ) {
        if( !range( cursor, 46, zip.size() ) || u32( zip, cursor ) != 0x02014b50 ) {
            return false;
        }
        const auto flags = u16( zip, cursor + 8 );
        const auto method = u16( zip, cursor + 10 );
        const auto checksum = u32( zip, cursor + 16 );
        const auto packed = u32( zip, cursor + 20 );
        const auto unpacked = u32( zip, cursor + 24 );
        const auto name_len = u16( zip, cursor + 28 );
        const auto extra_len = u16( zip, cursor + 30 );
        const auto comment_len = u16( zip, cursor + 32 );
        const auto local_offset = u32( zip, cursor + 42 );
        if( !range( cursor + 46, static_cast<size_t>( name_len ) + extra_len + comment_len,
                    directory_offset + directory_size ) ) {
            return false;
        }
        const std::string name( reinterpret_cast<const char *>( zip.data() + cursor + 46 ),
                                name_len );
        cursor += 46 + name_len + extra_len + comment_len;
        if( !matches( name, relative, mc ) ) { continue; }
        if( ( flags & 1 ) || !range( local_offset, 30, zip.size() ) ||
            u32( zip, local_offset ) != 0x04034b50 ||
            u16( zip, local_offset + 8 ) != method ||
            u16( zip, local_offset + 6 ) != flags ) {
            return false;
        }
        const size_t data_offset = local_offset + 30 +
                                   u16( zip, local_offset + 26 ) + u16( zip, local_offset + 28 );
        return unpack( zip, data_offset, method, packed, unpacked, checksum, source );
    }
    return false;
}
} // namespace

bool read_post( const std::filesystem::path &file, std::string &source )
{
    return read_entry( file, "post.frag", source, false ) ||
           read_entry( file, "shaders/post.frag", source, false );
}

bool read_mc_entry( const std::filesystem::path &file,
                    const std::string &entry, std::string &source )
{
    if( entry.find( "shaders/" ) != 0 || !safe_name( entry ) ) { return false; }
    return read_entry( file, entry, source, true );
}
} // namespace fp3d_shader_archive
#endif
