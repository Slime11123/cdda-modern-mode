#pragma once
#ifndef CATA_SRC_MODERN_2D_CAMERA_H
#define CATA_SRC_MODERN_2D_CAMERA_H

#include <algorithm>
#include <cmath>
#include "point.h"

// Screen-space look-ahead only. Never changes the avatar or its perception.
class modern_2d_camera
{
    public:
        point pixels() const {
            return point( std::lround( x ), std::lround( y ) );
        }

        bool update( bool enabled, const point &mouse, const point &size, float seconds ) {
            const point before = pixels();
            if( !enabled || size.x <= 1 || size.y <= 1 ) {
                x = y = 0;
                return before != pixels();
            }
            // Outside the map (including the sidebar), smoothly return home.
            const bool inside = mouse.x >= 0 && mouse.y >= 0 && mouse.x < size.x && mouse.y < size.y;
            const float dx = mouse.x - ( size.x - 1 ) * .5f;
            const float dy = mouse.y - ( size.y - 1 ) * .5f;
            const float distance = std::hypot( dx, dy );
            // Use pixel distance on both axes. Aspect-scaled axes or separate
            // edge thresholds create an ellipse/rectangle rather than a circle.
            const float radius = ( std::min( size.x, size.y ) - 1 ) * .5f;
            const float strength = inside ? std::clamp( ( distance / radius - .55f ) / .45f,
                                   0.0f, 1.0f ) : 0;
            const float reach = std::min( size.x, size.y ) * .45f;
            const float factor = distance > 0 ? strength * reach / distance : 0;
            const float tx = dx * factor;
            const float ty = dy * factor;
            const float blend = 1 - std::exp( -6 * std::clamp( seconds, 0.0f, .05f ) );
            x += ( tx - x ) * blend;
            y += ( ty - y ) * blend;
            if( std::abs( tx - x ) < .1f ) {
                x = tx;
            }
            if( std::abs( ty - y ) < .1f ) {
                y = ty;
            }
            return before != pixels();
        }

    private:
        float x = 0;
        float y = 0;
};

#endif
