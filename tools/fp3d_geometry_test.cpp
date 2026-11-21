// Standalone repeatable tests for model geometry extracted from the legacy
// renderer. This program has no SDL or OpenGL dependency.
#include "../src/fp3d_geometry.h"
#include "../src/fp3d_geometry_vehicle.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {
struct uv {
    float left, top, right, bottom;
};
struct face_uv {
    std::optional<uv> top, front, side, back;
};
bool close(float a,float b) {
    return std::abs(a-b)<1e-5f;
}
void check_vertices(const fp3d_geometry::mesh &m) {
    assert(m.vertices.size()%4==0);
    assert(m.indices.size()==m.vertices.size()/4*6);
    for(std::size_t q=0;q<m.vertices.size()/4;++q) {
        const std::array<std::uint32_t,6> order{0,1,2,0,2,3};
        for(std::size_t k=0;k<6;++k) assert(m.indices[q*6+k]==q*4+order[k]);
    }
}
void floor_test() {
    fp3d_geometry::mesh m;
    fp3d_geometry::quad_mesh_sink sink(m);
    const uv rect{.25f,.125f,.5f,.375f};
    sink.color(.9f,.8f,.7f,.6f);
    fp3d_geometry::floor_face(sink,1,2,3,rect,.5f);
    fp3d_geometry::floor_face(sink,1,2,4,rect,.5f,true);
    assert(m.vertices.size()==8);
    const std::array<std::array<float,3>,8> positions{{
        {{.5f,3,1.5f}},{{.5f,3,2.5f}},{{1.5f,3,2.5f}},{{1.5f,3,1.5f}},
        {{.5f,4,2.5f}},{{.5f,4,1.5f}},{{1.5f,4,1.5f}},{{1.5f,4,2.5f}}
    }};
    const std::array<std::array<float,2>,4> tex{{
        {{.25f,.125f}},{{.25f,.375f}},{{.5f,.375f}},{{.5f,.125f}}
    }};
    for(std::size_t i=0;i<8;++i) {
        assert(m.vertices[i].position==positions[i]);
        assert(m.vertices[i].uv==tex[i%4]);
        assert((m.vertices[i].normal==std::array<float,3>{0,i<4?1.f:-1.f,0}));
        assert((m.vertices[i].color==std::array<float,4>{.9f,.8f,.7f,.6f}));
    }
    check_vertices(m);
}
void ramp_test()
{
    struct plane {
        float dx = .75f;
        float dy = -1.25f;
        float height( float x, float z ) const {
            return .5f + dx * x + dy * z;
        }
    };
    fp3d_geometry::mesh m;
    fp3d_geometry::quad_mesh_sink sink( m );
    fp3d_geometry::ramp_face( sink, 3, 7, uv{ .25f, .5f, .5f, .75f }, plane{} );
    assert( m.vertices.size() == 4 );
    assert( ( m.indices == std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 } ) );
    assert( ( m.vertices[0].position == std::array<float, 3>{ 2.5f, .75f, 6.5f } ) );
    assert( ( m.vertices[1].position == std::array<float, 3>{ 2.5f, -.5f, 7.5f } ) );
    assert( ( m.vertices[2].position == std::array<float, 3>{ 3.5f, .25f, 7.5f } ) );
    assert( ( m.vertices[3].position == std::array<float, 3>{ 3.5f, 1.5f, 6.5f } ) );
    assert( ( m.vertices[2].uv == std::array<float, 2>{ .5f, .75f } ) );
    const auto &normal = m.vertices[0].normal;
    assert( close( normal[0] / normal[1], -.75f ) );
    assert( close( normal[2] / normal[1], 1.25f ) );
    check_vertices( m );
    std::puts( "PASS: ramp heights, normal, UV and triangle winding" );
}

void opening_test()
{
    struct door_part {
        float x = -.25f;
        float width = .49f;
        float bottom = .55f;
        float top = 1.65f;
    };
    const uv material{ .25f, .125f, .75f, .625f };
    const auto crop = fp3d_geometry::damaged_door_uv( material, door_part{}, 2.2f );
    assert( close( crop.left, .25f + .5f * ( .5f - .495f / .98f ) ) );
    assert( close( crop.right, .25f + .5f * ( .5f - .005f / .98f ) ) );
    assert( close( crop.top, .25f ) && close( crop.bottom, .5f ) );

    struct stair_section {
        int width = 4;
        int east = 1;
    };
    const auto stair = fp3d_geometry::stair_uv( material, stair_section{} );
    assert( close( stair.left, .5f ) && close( stair.right, .625f ) );
    assert( stair.top == material.top && stair.bottom == material.bottom );

    fp3d_geometry::mesh standalone;
    fp3d_geometry::mesh under_floor;
    fp3d_geometry::quad_mesh_sink full( standalone );
    fp3d_geometry::quad_mesh_sink capped( under_floor );
    fp3d_geometry::window_frame<face_uv>( full, std::optional<uv>{ material }, 2.2f, false );
    fp3d_geometry::window_frame<face_uv>( capped, std::optional<uv>{ material }, 2.2f, true );
    assert( standalone.vertices.size() == under_floor.vertices.size() + 4 );
    assert( standalone.indices.size() == under_floor.indices.size() + 6 );
    assert( ( under_floor.vertices.back().position == std::array<float, 3>{ .5f, 2.1f, .09f } ) );
    assert( ( under_floor.vertices.back().normal == std::array<float, 3>{ 0, -1, 0 } ) );
    assert( ( under_floor.vertices.back().uv == std::array<float, 2>{ material.right, material.top } ) );
    check_vertices( standalone );
    check_vertices( under_floor );
    std::puts( "PASS: damaged door atlas crop, window lintel and upper-floor seam" );
}

void box_test() {
    const std::optional<uv> rect=uv{0,0,1,1};
    fp3d_geometry::mesh m;
    fp3d_geometry::quad_mesh_sink sink(m);
    fp3d_geometry::box_faces(sink,0,0,1,1,0,1,rect,false,
                             static_cast<const face_uv *>(nullptr),true);
    assert(m.vertices.size()==20);
    const std::array<std::array<float,3>,5> normals{{
        {{0,1,0}},{{0,0,-1}},{{0,0,1}},{{-1,0,0}},{{1,0,0}}
    }};
    for(std::size_t i=0;i<20;++i) assert(m.vertices[i].normal==normals[i/4]);
    check_vertices(m);
    fp3d_geometry::mesh trimmed;
    fp3d_geometry::quad_mesh_sink other(trimmed);
    fp3d_geometry::box_faces(other,0,0,.5f,1,.25f,2.25f,rect,true,
                             static_cast<const face_uv *>(nullptr),false);
    assert(trimmed.vertices.size()==48); // four sides, three rows, no top
    assert(close(trimmed.vertices[0].position[1],.25f));
    assert(close(trimmed.vertices[0].uv[1],.75f));
    assert(close(trimmed.vertices[1].uv[0],.5f));
    assert(close(trimmed.vertices[10].position[1],2.25f));
    assert(close(trimmed.vertices[10].uv[1],.75f));
    check_vertices(trimmed);

    fp3d_geometry::mesh furniture;
    fp3d_geometry::quad_mesh_sink fs(furniture);
    const face_uv faces{uv{.1f,0,.2f,1},uv{.3f,0,.4f,1},uv{.5f,0,.6f,1},uv{.7f,0,.8f,1}};
    fp3d_geometry::box_faces(fs,0,0,1,1,0,1,rect,false,&faces,true);
    assert(close(furniture.vertices[0].uv[0],.1f));
    assert(close(furniture.vertices[4].uv[0],.7f));
    assert(close(furniture.vertices[8].uv[0],.3f));
    assert(close(furniture.vertices[12].uv[0],.5f));
    assert(close(furniture.vertices[16].uv[0],.5f));
    check_vertices(furniture);
}

void wheel_test()
{
    using namespace fp3d_geometry;
    mesh geometry;
    std::vector<draw_range> ranges;
    primitive_mesh_sink sink( geometry, ranges );
    const std::optional<uv> material = uv{ .25f, .5f, .5f, .75f };
    vehicle_wheel( sink, 1, 2, true, true, material );
    assert( geometry.vertices.size() == 142 && geometry.indices.size() == 384 );
    const std::array<std::uint32_t, 6> first_quad{ 0, 1, 3, 0, 3, 2 };
    assert( std::equal( first_quad.begin(), first_quad.end(), geometry.indices.begin() ) );
    assert( close( geometry.vertices[0].position[0], 1.28f ) );
    assert( close( geometry.vertices[0].position[1], .292f ) );
    assert( close( geometry.vertices[0].position[2], 1.895f ) );
    assert( close( geometry.vertices[1].position[2], 2.105f ) );
    assert( ranges.size() == 5 );
    assert( !ranges.front().textured && !ranges.front().alpha_cutoff );
    assert( ranges[1].textured && ranges[1].alpha_cutoff == .22f && ranges[1].index_count == 48 );
    assert( ranges[3].textured && ranges[3].alpha_cutoff == .22f && ranges[3].index_count == 48 );
    assert( !ranges.back().textured && !ranges.back().alpha_cutoff );
    std::puts( "PASS: damaged wheel tread, hubs, side decal UV and alpha ranges" );
}

void curved_test() {
    using namespace fp3d_geometry;
    struct part {float x,z,width,depth,bottom,top,inner;};
    const part p{0,0,1,1,0,2,.6f};
    const uv rect{0,0,1,1};
    const face_uv faces{rect,rect,std::nullopt,rect};
    mesh m;
    std::vector<draw_range> ranges;
    primitive_mesh_sink sink(m,ranges);
    crossed_plant(sink,0,0,2,false,rect);
    assert(m.vertices.size()==8 && m.indices.size()==12);
    assert(close(m.vertices[0].position[0],-.7f));
    assert(close(m.vertices[0].position[2],.7f));
    assert(close(m.vertices[2].position[1],2.015f));
    sink.textured(true);
    crossed_plant(sink,0,0,1,true,rect);
    assert(m.vertices.size()==50 && m.indices.size()==54);
    assert(ranges.size()==2 && ranges[1].textured && ranges[1].first_index==12);
    window_face(sink,std::optional<uv>{rect},.42f,.9f,2.1f);
    assert(m.vertices.size()==54 && m.indices.size()==60);
    assert(ranges.size()==2 && ranges[1].index_count==48);
    auto v=m.vertices.size(), i=m.indices.size();
    round_furniture_part(sink,p,faces,.5f);
    assert(m.vertices.size()-v==100 && m.indices.size()-i==192);
    assert(m.indices[i]==v && m.indices[i+1]==v+1 && m.indices[i+2]==v+2);
    assert(close(m.vertices[v+1].position[0],.25f));
    assert(close(m.vertices[v+18+1].position[0],.5f));
    assert(ranges.back().textured==false);
    v=m.vertices.size(); i=m.indices.size();
    furniture_ellipsoid(sink,p,faces);
    assert(m.vertices.size()-v==960 && m.indices.size()-i==1440);
    assert(ranges.back().textured);
    v=m.vertices.size(); i=m.indices.size();
    furniture_hollow_ring(sink,p,faces);
    assert(m.vertices.size()-v==512 && m.indices.size()-i==768);
    assert(close(m.vertices[v+3].position[0],.3f));
    std::size_t offset=0;
    for(const auto &range:ranges) {
        assert(range.first_index==offset && range.index_count%3==0);
        offset+=range.index_count;
    }
    assert(offset==m.indices.size());
    for(auto index:m.indices) assert(index<m.vertices.size());
    std::puts("PASS: foliage, window, tapered cylinder/fan caps, ellipsoid, hollow ring, texture ranges");
}

}
int main() {
    floor_test();
    box_test();
    ramp_test();
    opening_test();
    curved_test();
    wheel_test();
    std::puts("PASS: shared floor/underside, exact normals/UV/color, box faces, wall UV cropping, furniture face UV, index winding");
}
