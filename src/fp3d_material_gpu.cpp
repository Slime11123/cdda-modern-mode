#include "fp3d_material_gpu.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace fp3d_material_gpu
{
namespace
{
constexpr int atlas_size = 2048;
constexpr int slot_size = 64;
constexpr unsigned slots_per_row = atlas_size / slot_size;
constexpr unsigned total_slots = slots_per_row * slots_per_row;

status live;
std::array<bool, total_slots> transferred = {};
bool made_attempt = false;

std::vector<std::uint8_t> neutral( bool normal )
{
    std::vector<std::uint8_t> pixels( atlas_size * atlas_size * 4 );
    for( std::size_t i = 0; i < pixels.size(); i += 4 ) {
        pixels[i] = normal ? 128 : 255;
        pixels[i + 1] = normal ? 128 : 217;
        pixels[i + 2] = normal ? 255 : 0;
        pixels[i + 3] = 255;
    }
    return pixels;
}

bool create( GLuint &texture, bool normal )
{
    glGenTextures( 1, &texture );
    if( !texture ) {
        return false;
    }
    glBindTexture( GL_TEXTURE_2D, texture );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    // Fully initialize even slots which are not cached yet. Uninitialized
    // texels must never become undefined random normals on a future PBR pass.
    const auto pixels = neutral( normal );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, atlas_size, atlas_size, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
    return glGetError() == GL_NO_ERROR;
}

struct texture_state {
    GLint prior_binding = 0;
    std::array<GLint, 8> previous{};
    constexpr static std::array<GLenum, 8> names = {
        GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS, GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
        GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS
    };
    texture_state() {
        glGetIntegerv( GL_TEXTURE_BINDING_2D, &prior_binding );
        for( std::size_t i = 0; i < names.size(); ++i ) {
            glGetIntegerv( names[i], &previous[i] );
            glPixelStorei( names[i], ( names[i] == GL_PACK_ALIGNMENT ||
                                       names[i] == GL_UNPACK_ALIGNMENT ) ? 1 : 0 );
        }
    }
    ~texture_state() {
        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( prior_binding ) );
        for( std::size_t i = 0; i < names.size(); ++i ) {
            glPixelStorei( names[i], previous[i] );
        }
    }
};
} // namespace

status current()
{
    return live;
}

void release()
{
    if( live.normal ) {
        glDeleteTextures( 1, &live.normal );
    }
    if( live.orm ) {
        glDeleteTextures( 1, &live.orm );
    }
    live = {};
    transferred.fill( false );
    made_attempt = false;
}

bool sync( unsigned revision, const std::vector<fp3d_materials::auxiliary_tile> &tiles )
{
    if( live.revision != revision ) {
        release();
        live.revision = revision;
    }
    if( live.failed ) {
        return false;
    }
    if( tiles.empty() ) {
        return live.normal && live.orm;
    }
    GLint max_size = 0;
    glGetIntegerv( GL_MAX_TEXTURE_SIZE, &max_size );
    if( max_size < atlas_size ) {
        live.failed = true;
        return false;
    }
    texture_state previous;
    if( !made_attempt ) {
        made_attempt = true;
        if( !create( live.normal, true ) || !create( live.orm, false ) ) {
            if( live.normal ) glDeleteTextures( 1, &live.normal );
            if( live.orm ) glDeleteTextures( 1, &live.orm );
            live.normal = live.orm = 0;
            live.failed = true;
            return false;
        }
    }
    for( const auto &tile : tiles ) {
        if( tile.slot >= total_slots || transferred[tile.slot] || !tile.data ||
            tile.data->width != slot_size || tile.data->height != slot_size ||
            tile.data->normal_rgba.size() != slot_size * slot_size * 4 ||
            tile.data->orm_rgba.size() != slot_size * slot_size * 4 ) {
            continue;
        }
        const int x = static_cast<int>( tile.slot % slots_per_row ) * slot_size;
        const int y = static_cast<int>( tile.slot / slots_per_row ) * slot_size;
        glBindTexture( GL_TEXTURE_2D, live.normal );
        glTexSubImage2D( GL_TEXTURE_2D, 0, x, y, slot_size, slot_size,
                         GL_RGBA, GL_UNSIGNED_BYTE, tile.data->normal_rgba.data() );
        glBindTexture( GL_TEXTURE_2D, live.orm );
        glTexSubImage2D( GL_TEXTURE_2D, 0, x, y, slot_size, slot_size,
                         GL_RGBA, GL_UNSIGNED_BYTE, tile.data->orm_rgba.data() );
        ++live.attempted;
        if( glGetError() != GL_NO_ERROR ) {
            // Optional material enhancement must never poison the world pass.
            // The old Photon colours, shadows and SSR remain available.
            glDeleteTextures( 1, &live.normal );
            glDeleteTextures( 1, &live.orm );
            live.normal = live.orm = 0;
            live.failed = true;
            return false;
        }
        transferred[tile.slot] = true;
        ++live.uploaded;
    }
    return live.normal && live.orm;
}

bool draw_qa_preview( const std::vector<fp3d_materials::auxiliary_tile> &tiles )
{
    if( !live.normal || !live.orm ) {
        return false;
    }
    const fp3d_materials::auxiliary_tile *sample = nullptr;
    for( const auto &tile : tiles ) {
        if( tile.slot < total_slots && transferred[tile.slot] && tile.data &&
            tile.data->material.normal_strength > 0.0f ) {
            sample = &tile;
            break;
        }
    }
    if( !sample ) {
        return false;
    }
    const float u0 = ( sample->slot % slots_per_row * slot_size + 3.0f ) / atlas_size;
    const float v0 = ( sample->slot / slots_per_row * slot_size + 3.0f ) / atlas_size;
    const float u1 = ( sample->slot % slots_per_row * slot_size + slot_size - 3.0f ) / atlas_size;
    const float v1 = ( sample->slot / slots_per_row * slot_size + slot_size - 3.0f ) / atlas_size;

    glPushAttrib( GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT |
                  GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_VIEWPORT_BIT );
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_BLEND );
    glDisable( GL_SCISSOR_TEST );
    glDisable( GL_FOG );
    glDisable( GL_LIGHTING );
    glDisable( GL_CULL_FACE );
    glDisable( GL_ALPHA_TEST );
    glEnable( GL_TEXTURE_2D );
    glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
    glColor4f( 1, 1, 1, 1 );
    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadIdentity();
    // Two top-left chips: normals (left) and packed ORM (right).
    // The world image is altered ONLY for explicit QA screenshot captures.
    for( int i = 0; i < 2; ++i ) {
        glBindTexture( GL_TEXTURE_2D, i == 0 ? live.normal : live.orm );
        const float left = -0.94f + i * 0.32f;
        const float right = left + 0.28f;
        const float top = 0.92f;
        const float bottom = 0.42f;
        glBegin( GL_QUADS );
        glTexCoord2f( u0, v1 ); glVertex2f( left, top );
        glTexCoord2f( u1, v1 ); glVertex2f( right, top );
        glTexCoord2f( u1, v0 ); glVertex2f( right, bottom );
        glTexCoord2f( u0, v0 ); glVertex2f( left, bottom );
        glEnd();
    }
    glPopMatrix();
    glMatrixMode( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode( GL_MODELVIEW );
    glPopAttrib();
    return glGetError() == GL_NO_ERROR;
}

bool verify_texels( const std::vector<fp3d_materials::auxiliary_tile> &tiles )
{
    if( !live.normal || !live.orm || live.uploaded == 0 ) {
        return false;
    }
    texture_state previous;
    std::vector<std::uint8_t> pixels( atlas_size * atlas_size * 4 );
    for( int channel = 0; channel < 2; ++channel ) {
        glBindTexture( GL_TEXTURE_2D, channel == 0 ? live.normal : live.orm );
        glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
        if( glGetError() != GL_NO_ERROR ) {
            return false;
        }
        bool matched_one = false;
        for( const auto &tile : tiles ) {
            if( tile.slot >= total_slots || !transferred[tile.slot] || !tile.data ) {
                continue;
            }
            const auto &src = channel == 0 ? tile.data->normal_rgba : tile.data->orm_rgba;
            if( src.size() != slot_size * slot_size * 4 ) {
                return false;
            }
            const int sx = static_cast<int>( tile.slot % slots_per_row ) * slot_size;
            const int sy = static_cast<int>( tile.slot / slots_per_row ) * slot_size;
            for( int iy : { 0, 15, 31, 63 } ) {
                for( int ix : { 0, 11, 32, 63 } ) {
                    const std::size_t gpu = ( static_cast<std::size_t>( sy + iy ) *
                                              atlas_size + sx + ix ) * 4;
                    const std::size_t cpu = ( iy * slot_size + ix ) * 4;
                    for( int c = 0; c < 4; ++c ) {
                        if( pixels[gpu + c] != src[cpu + c] ) {
                            return false;
                        }
                    }
                }
            }
            matched_one = true;
            break;
        }
        if( !matched_one ) {
            return false;
        }
    }
    return true;
}
} // namespace fp3d_material_gpu
#endif
