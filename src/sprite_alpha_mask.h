#pragma once
#ifndef CATA_SRC_SPRITE_ALPHA_MASK_H
#define CATA_SRC_SPRITE_ALPHA_MASK_H
#include <vector>

// CPU alpha only: populated when art is loaded, never by a hover-time GPU read.
struct sprite_alpha_mask {
    int width = 0;
    int height = 0;
    std::vector<bool> pixels;
    bool opaque( int x, int y ) const {
        return x >= 0 && y >= 0 && x < width && y < height && pixels[y * width + x];
    }
    template<typename F>
    void edges( int left, int top, int w, int h, const F &emit ) const {
        const auto solid = [&]( int x, int y ) {
            return x >= 0 && y >= 0 && x < w && y < h && opaque( left + x, top + y );
        };
        for( int y = 0; y < h; ++y ) {
            for( int x = 0; x < w; ++x ) {
                if( !solid( x, y ) ) { continue; }
                if( !solid( x, y - 1 ) ) { emit( x, y, x + 1, y ); }
                if( !solid( x, y + 1 ) ) { emit( x, y + 1, x + 1, y + 1 ); }
                if( !solid( x - 1, y ) ) { emit( x, y, x, y + 1 ); }
                if( !solid( x + 1, y ) ) { emit( x + 1, y, x + 1, y + 1 ); }
            }
        }
    }
};
#endif
