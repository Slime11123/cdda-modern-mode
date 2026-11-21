#include "fp3d_inventory.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <vector>

#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "catacharset.h"
#include "sdl_font.h"
#include "sdl_geometry.h"
#include "sdl_wrappers.h"
#include "sdltiles.h"

TEST_CASE( "fp3d_font_keeps_complete_wide_and_tall_glyphs", "[fp3d][font]" )
{
    REQUIRE( TTF_Init() );
    on_out_of_scope quit_ttf( []() { TTF_Quit(); } );
    restore_on_out_of_scope restore_palette( windowsPalette );
    windowsPalette[15] = SDL_Color{ 255, 255, 255, 255 };
    const std::string typeface = std::filesystem::exists( "C:/Windows/Fonts/msyh.ttc" ) ?
                                 "C:/Windows/Fonts/msyh.ttc" : "data/font/unifont.ttf";
    TTF_Font_Ptr original_font( TTF_OpenFont( typeface.c_str(), 24 ) );
    REQUIRE( original_font );
    GeometryRenderer_Ptr geometry = std::make_unique<DefaultGeometryRenderer>();
    const bool blending = GENERATE( true, false );
    CAPTURE( blending );
    for( const point cell : { point( 12, 24 ), point( 8, 16 ), point( 24, 32 ) } ) {
        CachedTTFFont font( cell.x, cell.y, windowsPalette, typeface, 24, blending );
        for( const std::string glyph : { "W", "M", "N", "g", "温", "度", "完", "成", "地", "图", "重", "，" } ) {
            CAPTURE( cell.x, cell.y, glyph );
            const int width = cell.x * utf8_width( glyph );
            SDL_Surface_Ptr actual( CreateSurfaceWithFormat( 0, width, cell.y, 32,
                                    SDL_PIXELFORMAT_RGBA32 ) );
            SDL_Surface_Ptr expected( CreateSurfaceWithFormat( 0, width, cell.y, 32,
                                      SDL_PIXELFORMAT_RGBA32 ) );
            REQUIRE( actual );
            REQUIRE( expected );
            SDL_Renderer_Ptr renderer( SDL_CreateSoftwareRenderer( actual.get() ) );
            REQUIRE( renderer );
            REQUIRE( SDL_SetRenderDrawColor( renderer.get(), 0, 0, 0, 255 ) );
            REQUIRE( SDL_RenderClear( renderer.get() ) );
            font.OutputChar( renderer, geometry, glyph, point::zero, 15 );
            SDL_RenderPresent( renderer.get() );

            const auto render_glyph = blending ? TTF_RenderText_Blended : TTF_RenderText_Solid;
            SDL_Surface_Ptr source( render_glyph( original_font.get(), glyph.c_str(), glyph.size(),
                                    windowsPalette[15] ) );
            REQUIRE( source );
            source.reset( SDL_ConvertSurface( source.get(), SDL_PIXELFORMAT_RGBA32 ) );
            REQUIRE( source );
            SDL_Surface_Ptr fitted( CreateSurfaceWithFormat( 0, width, cell.y, 32,
                                    SDL_PIXELFORMAT_RGBA32 ) );
            REQUIRE( fitted );
            int top = source->h;
            int bottom = 0;
            for( int y = 0; y < source->h; ++y ) {
                for( int x = 0; x < source->w; ++x ) {
                    Uint8 red, green, blue, alpha;
                    const auto *row = reinterpret_cast<const Uint32 *>(
                                          static_cast<const Uint8 *>( source->pixels ) + y * source->pitch );
                    SDL_GetRGBA( row[x], SDL_GetPixelFormatDetails( source->format ), nullptr, &red, &green, &blue, &alpha );
                    if( alpha ) {
                        top = std::min( top, y );
                        bottom = std::max( bottom, y + 1 );
                    }
                }
            }
            REQUIRE( top < bottom );
            const float scale = std::min( { 1.0f, static_cast<float>( width ) / source->w,
                                            static_cast<float>( cell.y ) / ( bottom - top ) } );
            const int fitted_width = std::max( 1, static_cast<int>( source->w * scale ) );
            const int fitted_height = std::max( 1, static_cast<int>( source->h * scale ) );
            const int baseline_y = std::clamp( ( cell.y - fitted_height ) / 2,
                                               -static_cast<int>( top * scale ), cell.y - static_cast<int>( bottom * scale ) );
            SDL_Rect destination{ ( width - fitted_width ) / 2, baseline_y,
                                  fitted_width, fitted_height };
            REQUIRE( SDL_SetSurfaceBlendMode( source.get(), SDL_BLENDMODE_NONE ) );
            REQUIRE( SDL_BlitSurfaceScaled( source.get(), nullptr, fitted.get(), &destination, SDL_SCALEMODE_NEAREST ) );
            REQUIRE( SDL_FillSurfaceRect( expected.get(), nullptr,
                                  SDL_MapSurfaceRGBA( expected.get(), 0, 0, 0, 255 ) ) );
            REQUIRE( SDL_SetSurfaceBlendMode( fitted.get(), SDL_BLENDMODE_BLEND ) );
            REQUIRE( SDL_BlitSurface( fitted.get(), nullptr, expected.get(), nullptr ) );
            std::vector<Uint32> pixels( width * cell.y );
            REQUIRE( ReadRenderPixels( renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA32,
                                          pixels.data(), width * 4 ) == 0 );
            const auto *reference = static_cast<const Uint32 *>( expected->pixels );
            for( int y = 0; y < cell.y; ++y ) {
                const auto start = pixels.begin() + y * width;
                const auto *expected_row = reference + y * expected->pitch / 4;
                // SDL3's software texture renderer and surface blitter can
                // round alpha blending differently by one 8-bit channel step.
                CHECK( std::equal( start, start + width, expected_row, [blending]( Uint32 a, Uint32 b ) {
                    if( !blending ) {
                        return a == b;
                    }
                    for( int shift = 0; shift < 32; shift += 8 ) {
                        if( std::abs( int( a >> shift & 255 ) - int( b >> shift & 255 ) ) > 1 ) {
                            return false;
                        }
                    }
                    return true;
                } ) );
            }
            if( cell == point( 12, 24 ) && glyph == "温" ) {
                // The Chinese glyph must fill its advance instead of gaining
                // extra inter-character space from the font's transparent rows.
                int left = width;
                int right = 0;
                for( int y = 0; y < cell.y; ++y ) {
                    for( int x = 0; x < width; ++x ) {
                        if( pixels[y * width + x] & SDL_GetPixelFormatDetails( actual->format )->Rmask ) {
                            left = std::min( left, x );
                            right = std::max( right, x + 1 );
                        }
                    }
                }
                CHECK( right - left >= width - 2 );
            }
            if( const char *directory = std::getenv( "CDDA_FP3D_FONT_REPORT_DIR" ) ) {
                const auto file = std::filesystem::path( directory ) /
                                  ( std::string( blending ? "blended-" : "solid-" ) + std::to_string( cell.x ) + "-" +
                                    std::to_string( UTF8_getch( glyph ) ) + ".bmp" );
                REQUIRE( SDL_SaveBMP( actual.get(), file.string().c_str() ) );
                const auto reference_file = file.parent_path() / ( "reference-" + file.filename().string() );
                REQUIRE( SDL_SaveBMP( expected.get(), reference_file.string().c_str() ) );
            }
        }
    }
}
#endif
