#if defined(TILES) || defined(SDL_SOUND)

#include "sdl_wrappers.h"

#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cata_assert.h"
#include "debug.h"
#include "point.h"

#if defined(_MSC_VER) && defined(USE_VCPKG)
#   include <SDL3_image/SDL_image.h>
#else
#   include <SDL3_image/SDL_image.h>
#endif

#define dbg(x) DebugLog((x),D_SDL) << __FILE__ << ":" << __LINE__ << ": "

bool printErrorIf( const bool condition, const char *const message )
{
    if( !condition ) {
        return false;
    }
    dbg( D_ERROR ) << message << ": " << SDL_GetError();
    return true;
}

void throwErrorIf( const bool condition, const char *const message )
{
    if( !condition ) {
        return;
    }
    throw std::runtime_error( std::string( message ) + ": " + SDL_GetError() );
}

#if defined(TILES)

namespace
{
SDL_FRect float_rect( const SDL_Rect *rect )
{
    SDL_FRect result{};
    if( rect ) {
        SDL_RectToFRect( rect, &result );
    }
    return result;
}
}

int RenderTexturePixels( SDL_Renderer *renderer, SDL_Texture *texture,
                         const SDL_Rect *source, const SDL_Rect *destination )
{
    const SDL_FRect src = float_rect( source );
    const SDL_FRect dst = float_rect( destination );
    return SDL_RenderTexture( renderer, texture, source ? &src : nullptr,
                              destination ? &dst : nullptr ) ? 0 : -1;
}

int RenderTextureRotatedPixels( SDL_Renderer *renderer, SDL_Texture *texture,
                                const SDL_Rect *source, const SDL_Rect *destination,
                                double angle, const SDL_Point *center, SDL_FlipMode flip )
{
    const SDL_FRect src = float_rect( source );
    const SDL_FRect dst = float_rect( destination );
    const SDL_FPoint pivot = center ? SDL_FPoint{ float( center->x ), float( center->y ) } :
                            SDL_FPoint{};
    return SDL_RenderTextureRotated( renderer, texture, source ? &src : nullptr,
                                     destination ? &dst : nullptr, angle,
                                     center ? &pivot : nullptr, flip ) ? 0 : -1;
}

int RenderRectPixels( SDL_Renderer *renderer, const SDL_Rect *rect )
{
    const SDL_FRect dst = float_rect( rect );
    return SDL_RenderRect( renderer, rect ? &dst : nullptr ) ? 0 : -1;
}

int RenderFillRectPixels( SDL_Renderer *renderer, const SDL_Rect *rect )
{
    const SDL_FRect dst = float_rect( rect );
    return SDL_RenderFillRect( renderer, rect ? &dst : nullptr ) ? 0 : -1;
}

int RenderLinesPixels( SDL_Renderer *renderer, const SDL_Point *points, int count )
{
    std::vector<SDL_FPoint> vertices;
    vertices.reserve( count );
    for( int i = 0; i < count; ++i ) {
        vertices.push_back( { float( points[i].x ), float( points[i].y ) } );
    }
    return SDL_RenderLines( renderer, vertices.data(), count ) ? 0 : -1;
}

int ReadRenderPixels( SDL_Renderer *renderer, const SDL_Rect *rect, SDL_PixelFormat format,
                      void *pixels, int pitch )
{
    const SDL_Surface_Ptr surface( SDL_RenderReadPixels( renderer, rect ) );
    return surface && SDL_ConvertPixels( surface->w, surface->h, surface->format, surface->pixels,
                                         surface->pitch, format, pixels, pitch ) ? 0 : -1;
}

int QueryTexturePixels( SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h )
{
    if( !texture ) {
        SDL_SetError( "Missing texture" );
        return -1;
    }
    if( format ) { *format = texture->format; }
    if( access ) {
        *access = SDL_GetNumberProperty( SDL_GetTextureProperties( texture ),
                                         SDL_PROP_TEXTURE_ACCESS_NUMBER, 0 );
    }
    if( w ) { *w = texture->w; }
    if( h ) { *h = texture->h; }
    return 0;
}

void GetLogicalRenderSize( SDL_Renderer *renderer, int *w, int *h )
{
    SDL_GetRenderLogicalPresentation( renderer, w, h, nullptr );
}

int SetLogicalRenderSize( SDL_Renderer *renderer, int w, int h )
{
    const auto mode = w <= 0 || h <= 0 ? SDL_LOGICAL_PRESENTATION_DISABLED :
                      GetIntegerRenderScale( renderer ) ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE :
                      SDL_LOGICAL_PRESENTATION_LETTERBOX;
    return SDL_SetRenderLogicalPresentation( renderer, w, h, mode ) ? 0 : -1;
}

bool GetIntegerRenderScale( SDL_Renderer *renderer )
{
    return SDL_GetBooleanProperty( SDL_GetRendererProperties( renderer ),
                                   "cdda.render.integer_scale", false );
}

int SetIntegerRenderScale( SDL_Renderer *renderer, bool enabled )
{
    SDL_SetBooleanProperty( SDL_GetRendererProperties( renderer ), "cdda.render.integer_scale", enabled );
    int w = 0;
    int h = 0;
    GetLogicalRenderSize( renderer, &w, &h );
    return SetLogicalRenderSize( renderer, w, h );
}

SDL_Surface *CreateSurfaceWithFormat( Uint32, int w, int h, int, SDL_PixelFormat format )
{
    return SDL_CreateSurface( w, h, format );
}

SDL_Surface *CreateSurfaceFromPixels( void *pixels, int w, int h, int, int pitch,
                                      SDL_PixelFormat format )
{
    return SDL_CreateSurfaceFrom( w, h, format, pixels, pitch );
}

SDL_MouseButtonFlags MousePositionPixels( int *x, int *y )
{
    float sx, sy;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState( &sx, &sy );
    if( x ) { *x = static_cast<int>( sx ); }
    if( y ) { *y = static_cast<int>( sy ); }
    return buttons;
}

void RenderCopy( const SDL_Renderer_Ptr &renderer, const SDL_Texture_Ptr &texture,
                 const SDL_Rect *srcrect, const SDL_Rect *dstrect )
{
    if( !renderer ) {
        dbg( D_ERROR ) << "Tried to render to a null renderer";
        return;
    }
    if( !texture ) {
        dbg( D_ERROR ) << "Tried to render a null texture";
        return;
    }
    printErrorIf( RenderTexturePixels( renderer.get(), texture.get(), srcrect, dstrect ) != 0,
                  "RenderTexturePixels failed" );
}

SDL_Texture_Ptr CreateTexture( const SDL_Renderer_Ptr &renderer, Uint32 format, int access,
                               int w, int h )
{
    if( !renderer ) {
        dbg( D_ERROR ) << "Tried to create texture with a null renderer";
        return SDL_Texture_Ptr();
    }
    SDL_Texture_Ptr result( SDL_CreateTexture( renderer.get(), static_cast<SDL_PixelFormat>( format ),
                            static_cast<SDL_TextureAccess>( access ), w, h ) );
    printErrorIf( !result, "SDL_CreateTexture failed" );
    return result;
}

SDL_Texture_Ptr CreateTextureFromSurface( const SDL_Renderer_Ptr &renderer,
        const SDL_Surface_Ptr &surface )
{
    if( !renderer ) {
        dbg( D_ERROR ) << "Tried to create texture with a null renderer";
        return SDL_Texture_Ptr();
    }
    if( !surface ) {
        dbg( D_ERROR ) << "Tried to create texture from a null surface";
        return SDL_Texture_Ptr();
    }
    SDL_Texture_Ptr result( SDL_CreateTextureFromSurface( renderer.get(), surface.get() ) );
    printErrorIf( !result, "SDL_CreateTextureFromSurface failed" );
    return result;
}

void SetRenderDrawColor( const SDL_Renderer_Ptr &renderer, const Uint8 r, const Uint8 g,
                         const Uint8 b, const Uint8 a )
{
    if( !renderer ) {
        dbg( D_ERROR ) << "Tried to use a null renderer";
        return;
    }
    printErrorIf( !SDL_SetRenderDrawColor( renderer.get(), r, g, b, a ),
                  "SDL_SetRenderDrawColor failed" );
}

void RenderDrawPoint( const SDL_Renderer_Ptr &renderer, const point &p )
{
    printErrorIf( !SDL_RenderPoint( renderer.get(), p.x, p.y ), "SDL_RenderPoint failed" );
}

void RenderFillRect( const SDL_Renderer_Ptr &renderer, const SDL_Rect *const rect )
{
    if( !renderer ) {
        dbg( D_ERROR ) << "Tried to use a null renderer";
        return;
    }
    printErrorIf( RenderFillRectPixels( renderer.get(), rect ) != 0, "RenderFillRectPixels failed" );
}

void FillRect( const SDL_Surface_Ptr &surface, const SDL_Rect *const rect, Uint32 color )
{
    if( !surface ) {
        dbg( D_ERROR ) << "Tried to use a null surface";
        return;
    }
    printErrorIf( !SDL_FillSurfaceRect( surface.get(), rect, color ), "SDL_FillSurfaceRect failed" );
}

void SetTextureBlendMode( const SDL_Texture_Ptr &texture, SDL_BlendMode blendMode )
{
    if( !texture ) {
        dbg( D_ERROR ) << "Tried to use a null texture";
    }

    throwErrorIf( !SDL_SetTextureBlendMode( texture.get(), blendMode ),
                  "SDL_SetTextureBlendMode failed" );
}

bool SetTextureColorMod( const SDL_Texture_Ptr &texture, Uint32 r, Uint32 g, Uint32 b )
{
    if( !texture ) {
        dbg( D_ERROR ) << "Tried to use a null texture";
        return true;
    }
    return printErrorIf( !SDL_SetTextureColorMod( texture.get(), r, g, b ),
                         "SDL_SetTextureColorMod failed" );
}

void SetRenderDrawBlendMode( const SDL_Renderer_Ptr &renderer, const SDL_BlendMode blendMode )
{
    if( !renderer ) {
        dbg( D_ERROR ) << "Tried to use a null renderer";
        return;
    }
    printErrorIf( !SDL_SetRenderDrawBlendMode( renderer.get(), blendMode ),
                  "SDL_SetRenderDrawBlendMode failed" );
}

void GetRenderDrawBlendMode( const SDL_Renderer_Ptr &renderer, SDL_BlendMode &blend_mode )
{
    if( !renderer ) {
        dbg( D_ERROR ) << "Tried to use a null renderer";
        return;
    }
    printErrorIf( !SDL_GetRenderDrawBlendMode( renderer.get(), &blend_mode ),
                  "SDL_GetRenderDrawBlendMode failed" );
}

SDL_Surface_Ptr load_image( const char *const path )
{
    cata_assert( path );
    SDL_Surface_Ptr result( IMG_Load( path ) );
    if( !result ) {
        throw std::runtime_error( "Could not load image \"" + std::string( path ) + "\": " +
                                  SDL_GetError() );
    }
    return result;
}

void SetRenderTarget( const SDL_Renderer_Ptr &renderer, const SDL_Texture_Ptr &texture )
{
    if( !renderer ) {
        dbg( D_ERROR ) << "Tried to use a null renderer";
        return;
    }
    // a null texture is fine for SDL
    printErrorIf( !SDL_SetRenderTarget( renderer.get(), texture.get() ),
                  "SDL_SetRenderTarget failed" );
}

void RenderClear( const SDL_Renderer_Ptr &renderer )
{
    if( !renderer ) {
        dbg( D_ERROR ) << "Tried to use a null renderer";
        return;
    }
    printErrorIf( !SDL_RenderClear( renderer.get() ), "SDL_RenderClear failed" );
}

SDL_Surface_Ptr CreateRGBSurface( const Uint32, const int width, const int height,
                                  const int depth, const Uint32 Rmask, const Uint32 Gmask, const Uint32 Bmask, const Uint32 Amask )
{
    const auto format = SDL_GetPixelFormatForMasks( depth, Rmask, Gmask, Bmask, Amask );
    SDL_Surface_Ptr surface( SDL_CreateSurface( width, height, format ) );
    throwErrorIf( !surface, "Failed to create surface" );
    return surface;
}
#endif // defined(TILES)
#endif // defined(TILES) || defined(SDL_SOUND)
