#include "fp3d_local_lights.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace fp3d_local_lights
{
namespace
{
struct ranked_light {
    float score = 0.0f;
    selected value;
};

bool contains( const std::string &id, const char *word )
{
    return id.find( word ) != std::string::npos;
}

} // namespace

std::vector<selected> select( const fp3d_snapshot::scene_snapshot &scene,
                              fp3d_camera::offset camera )
{
    std::vector<ranked_light> ranked;
    ranked.reserve( scene.lights.size() );
    for( const auto &source : scene.lights ) {
        if( source.intensity <= 0.0f ) {
            continue;
        }
        const auto rel = source.position - scene.origin;
        // This is a player-held flashlight/phone, not a static world lamp.
        // The game snapshot is turn-based but camera movement is smooth;
        // attach the emitter to the *current* camera every rendered frame.
        // Otherwise the cone pivots around the old tile during movement.
        const bool held_beam = source.kind == fp3d_snapshot::light_kind::beam &&
                               ( contains( source.source, "native:flashlight_active_light" ) ||
                                 contains( source.source, "native:phone_active_light" ) );
        const float x = held_beam ? camera.x : static_cast<float>( rel.x() );
        const float z = held_beam ? camera.y : static_cast<float>( rel.y() );
        const float y = held_beam ? camera.height - 0.18f :
                        static_cast<float>( rel.z() ) * fp3d_camera::floor_height +
                        source.elevation;
        const float dx = x - camera.x;
        const float dz = z - camera.y;
        const float dy = y - camera.height;
        const float distance = std::sqrt( dx*dx + dy*dy + dz*dz );
        float radius = std::clamp( 5.0f + std::sqrt( source.intensity ) * 1.4f,
                                   6.0f, 21.0f );
        if( source.kind == fp3d_snapshot::light_kind::beam ) {
            radius = std::clamp( 9.0f + std::sqrt( source.intensity ), 11.0f, 24.0f );
        }
        if( distance > radius + 5.0f ) {
            continue;
        }
        selected light;
        light.position = { x, y, z, radius };
        const bool cold = contains( source.source, "phone" ) ||
                          contains( source.source, "flashlight" ) ||
                          contains( source.source, "led" ) ||
                          contains( source.source, "fluor" ) ||
                          contains( source.source, "arc" );
        const bool fire = source.kind == fp3d_snapshot::light_kind::flame ||
                          contains( source.source, "fire" ) ||
                          contains( source.source, "torch" );
        const float strength = std::clamp( .20f + std::sqrt( source.intensity ) *
                                           .13f, .28f, 1.45f );
        light.color = fire ? std::array<float,4>{ 1.0f,.56f,.28f,strength } :
                      cold ? std::array<float,4>{ .77f,.87f,1.0f,strength } :
                             std::array<float,4>{ 1.0f,.81f,.57f,strength };
        light.beam = source.kind == fp3d_snapshot::light_kind::beam ? 1.0f : 0.0f;
        light.held_beam = held_beam;
        light.fixture = source.kind == fp3d_snapshot::light_kind::fixture;
        ranked.push_back( { strength * radius / ( 3.0f + distance ), light } );
    }
    std::sort( ranked.begin(), ranked.end(), []( const ranked_light &a,
    const ranked_light &b ) { return a.score > b.score; } );
    std::vector<selected> chosen;
    const int n = std::min( static_cast<int>( ranked.size() ), max_active );
    chosen.reserve( n );
    for( int i = 0; i < n; ++i ) {
        chosen.push_back( ranked[i].value );
    }
    return chosen;
}

} // namespace fp3d_local_lights
#endif
