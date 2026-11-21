// N3 QA: actual SDL-renderer atlas -> native GPU sampler, original mesh
// generator, game-projection matrix, alpha cutout, depth and UI composite.
// No OpenGL dependency. Blocking readback is confined to this test.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include "../src/fp3d_gpu_textured.h"

namespace {
void check(bool ok, const char *label) {
    if (!ok) {
        std::fprintf(stderr,"FAIL: %s: %s\n",label,SDL_GetError());
        throw std::runtime_error(label);
    }
}
template<typename T> T *ptr(T *p,const char *label) {
    check(p!=nullptr,label); return p;
}
struct blob_deleter {
    void operator()(ID3DBlob *p) const { if(p) p->Release(); }
};
SDL_GPUShader *compile(SDL_GPUDevice *d,const char *source,bool vertex) {
    ID3DBlob *code=nullptr,*errors=nullptr;
    HRESULT hr=D3DCompile(source,std::strlen(source),"fp3d_gpu_textured.h",nullptr,nullptr,
        "main",vertex?"vs_5_1":"ps_5_1",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    std::unique_ptr<ID3DBlob,blob_deleter> c(code),e(errors);
    if(FAILED(hr)) {
        if(e) std::fprintf(stderr,"%.*s\n",(int)e->GetBufferSize(),
                           (const char *)e->GetBufferPointer());
        throw std::runtime_error("shader compile failed");
    }
    SDL_GPUShaderCreateInfo info{};
    info.code=static_cast<const Uint8 *>(c->GetBufferPointer());
    info.code_size=c->GetBufferSize(); info.entrypoint="main";
    info.format=SDL_GPU_SHADERFORMAT_DXBC;
    info.stage=vertex?SDL_GPU_SHADERSTAGE_VERTEX:SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_uniform_buffers=1;
    info.num_samplers=vertex?0:6;
    return ptr(SDL_CreateGPUShader(d,&info),"shader");
}
struct assets {
    SDL_GPUDevice *device=nullptr;
    SDL_Renderer *renderer=nullptr;
    SDL_Texture *atlas=nullptr,*output=nullptr;
    SDL_GPUShader *vs=nullptr,*fs=nullptr;
    SDL_GPUFence *fence=nullptr;
    fp3d_gpu::scene_target scene;
    fp3d_gpu::indexed_mesh terrain;
    fp3d_gpu::upload_buffer instances;
    fp3d_gpu::textured_pipeline pipeline;
    fp3d_gpu::textured_pipeline glass;
    ~assets() {
        if(device) SDL_WaitForGPUIdle(device);
        glass.reset(); pipeline.reset(); terrain.reset(); instances.reset();
        scene.reset();
        if(fence) SDL_ReleaseGPUFence(device,fence);
        if(vs) SDL_ReleaseGPUShader(device,vs);
        if(fs) SDL_ReleaseGPUShader(device,fs);
        if(atlas) SDL_DestroyTexture(atlas);
        if(output) SDL_DestroyTexture(output);
        if(renderer) SDL_DestroyRenderer(renderer);
        if(device) SDL_DestroyGPUDevice(device);
    }
};
void fill(SDL_Renderer *r,SDL_FRect rect,Uint8 red,Uint8 green,Uint8 blue,Uint8 alpha) {
    check(SDL_SetRenderDrawColor(r,red,green,blue,alpha),"draw color");
    check(SDL_RenderFillRect(r,&rect),"atlas fill");
}
void pixel(SDL_Surface *s,int x,int y,Uint8 red,Uint8 green,Uint8 blue,const char *label, Uint8 alpha = 255) {
    Uint8 r=0,g=0,b=0,a=0;
    check(SDL_ReadSurfacePixel(s,x,y,&r,&g,&b,&a),"pixel");
    std::printf("%s @%d,%d = %u,%u,%u,%u\n",label,x,y,r,g,b,a);
    check(r==red && g==green && b==blue && a==alpha,label);
}
void run() {
    assets a;
    a.device=ptr(SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC|SDL_GPU_SHADERFORMAT_DXIL,
                 true,"direct3d12"),"GPU device");
    a.renderer=ptr(SDL_CreateGPURenderer(a.device,nullptr),"GPU renderer");
    std::printf("driver=%s renderer=%s\n",SDL_GetGPUDeviceDriver(a.device),
                SDL_GetRendererName(a.renderer));
    a.atlas=ptr(SDL_CreateTexture(a.renderer,SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_TARGET,8,4),"atlas");
    a.output=ptr(SDL_CreateTexture(a.renderer,SDL_PIXELFORMAT_RGBA32,
                                  SDL_TEXTUREACCESS_TARGET,128,128),"output");
    check(a.scene.initialize(a.renderer,128,128),"scene target");
    check(SDL_SetRenderTarget(a.renderer,a.atlas),"atlas target");
    check(SDL_SetRenderDrawBlendMode(a.renderer,SDL_BLENDMODE_NONE),"atlas blend");
    fill(a.renderer,{0,0,2,2},0,255,0,255);
    fill(a.renderer,{2,0,2,2},255,255,255,0);
    fill(a.renderer,{0,2,2,2},255,255,0,255);
    fill(a.renderer,{2,2,2,2},0,0,255,255);
    fill(a.renderer,{4,0,4,4},255,0,0,255);
    fill( a.renderer, { 3, 0, 1, 1 }, 0, 255, 255, 64 );
    check(SDL_FlushRenderer(a.renderer),"flush atlas writes");
    check(SDL_RenderPresent(a.renderer),"submit offscreen atlas writes");
    check(SDL_SetRenderTarget(a.renderer,a.output),"restore target");
    check(SDL_FlushRenderer(a.renderer),"flush output");
    auto *native=ptr(fp3d_gpu::native_atlas(a.renderer,a.atlas),"native atlas");
    a.vs=compile(a.device,fp3d_gpu::textured_vertex_hlsl,true);
    a.fs=compile(a.device,fp3d_gpu::textured_fragment_hlsl,false);
    check(a.pipeline.initialize(a.device,a.vs,a.fs),"pipeline");
    check( a.glass.initialize( a.device, a.vs, a.fs, fp3d_gpu::mesh_pass::blend ), "glass pipeline" );
    struct uv {float left,top,right,bottom;};
    fp3d_geometry::mesh mesh;
    fp3d_geometry::quad_mesh_sink sink(mesh);
    fp3d_geometry::floor_face(sink,0,0,0,uv{0,0,.5f,1},2.5f);
    const auto far_index=static_cast<Uint32>(mesh.indices.size());
    fp3d_geometry::floor_face(sink,0,0,-1,uv{0,0,0,0},4.0f);
    const auto ramp_index = static_cast<Uint32>( mesh.indices.size() );
    struct grade {
        float dx = .25f;
        float dy = -.5f;
        float height( float x, float z ) const {
            return dx * x + dy * z;
        }
    };
    fp3d_geometry::ramp_face( sink, 0, 0, uv{ 0, 0, 1, 1 }, grade{} );
    const auto decal_index = static_cast<Uint32>( mesh.indices.size() );
    fp3d_geometry::floor_face( sink, 0, 0, 0, uv{ 0, 0, 1, 1 }, .18f );
    const auto decal_count = static_cast<Uint32>( mesh.indices.size() ) - decal_index;
    check(a.terrain.initialize(a.device,mesh),"combined terrain mesh");
    std::array<fp3d_gpu::mesh_instance, 8> instances{};
    instances[1].uv_rect={.75f,.5f,.75f,.5f};
    instances[2].uv_rect = instances[1].uv_rect;
    instances[2].offset[1] = .5f;
    instances[2].tint[0] = .5f;
    instances[3].offset = { -.9f, .5f, 0.f, 1.f };
    instances[3].uv_rect = { .375f, .25f, .375f, .25f }; // transparent atlas texel
    instances[3].tint = { 1, 0, 1, 1 };
    instances[4].offset = { 1.18f, .55f, -1.35f, -.22f };
    instances[4].uv_rect = { .4375f, .125f, .4375f, .125f };
    instances[5].offset = { -1.18f, .55f, -1.35f, 2.f };
    instances[5].tint = { 1, 0, 0, .5f };
    instances[6].offset = { -1.18f, .35f, -1.35f, 2.f };
    instances[6].tint = { 0, 0, 1, .5f };
    instances[7].offset = { 1.18f, .55f, 1.35f, 0.f };
    instances[7].uv_rect = instances[1].uv_rect;
    check(a.instances.initialize(a.device,sizeof(instances),SDL_GPU_BUFFERUSAGE_VERTEX),"instances");
    check(a.instances.stage(instances.data(),sizeof(instances)),"instance data");
    fp3d_gpu_camera_math::camera camera;
    camera.pitch_degrees=90; camera.eye_height=2;
    auto matrix=fp3d_gpu_camera_math::clip_from_world(camera);
    check(bool(matrix),"camera matrix");
    auto *cmd=ptr(SDL_AcquireGPUCommandBuffer(a.device),"command");
    auto *copy=ptr(SDL_BeginGPUCopyPass(cmd),"copy pass");
    const bool uploaded=a.terrain.record(copy)&&a.instances.record(copy);
    SDL_EndGPUCopyPass(copy);
    if(!uploaded) {SDL_CancelGPUCommandBuffer(cmd);check(false,"upload");}
    SDL_GPUColorTargetInfo color{};
    color.texture=a.scene.native_color();color.clear_color={.25f,.25f,.25f,1};
    color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture=a.scene.depth();depth.clear_depth=1;
    depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto *pass=ptr(SDL_BeginGPURenderPass(cmd,&color,1,&depth),"render pass");
    fp3d_gpu::directional_light test_light;
    test_light.direction_ambient = { 0, 1, 0, .25f };
    test_light.direct_and_padding[0] = .25f;
    const bool rendered=a.pipeline.bind(cmd,pass,native,*matrix,a.instances.get())&&
                     a.terrain.draw_range(pass,0,far_index,1,0)&&
                     a.terrain.draw_range( pass, far_index, ramp_index - far_index, 1, 1 ) &&
                     a.terrain.draw_range( pass, ramp_index, decal_index - ramp_index, 2, 2 ) &&
                     a.terrain.draw_range( pass, decal_index, decal_count, 1, 4 ) &&
                     a.pipeline.bind( cmd, pass, native, *matrix, a.instances.get(), test_light ) &&
                     a.terrain.draw_range( pass, decal_index, decal_count, 1, 7 ) &&
                     a.glass.bind( cmd, pass, native, *matrix, a.instances.get() ) &&
                     a.terrain.draw_range( pass, decal_index, decal_count, 2, 5 );
    SDL_EndGPURenderPass(pass);
    if(!rendered) {SDL_CancelGPUCommandBuffer(cmd);check(false,"draw");}
    a.fence=ptr(SDL_SubmitGPUCommandBufferAndAcquireFence(cmd),"submit");
    check(SDL_WaitForGPUFences(a.device,true,&a.fence,1),"QA fence");
    check(SDL_RenderTexture(a.renderer,a.scene.color(),nullptr,nullptr),"composite");
    fill(a.renderer,{0,0,12,12},255,0,255,255);
    check(SDL_RenderPresent(a.renderer),"present");
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(
        ptr(SDL_RenderReadPixels(a.renderer,nullptr),"readback"),SDL_DestroySurface);
    pixel(pixels.get(),40,40,0,255,0,"top-left original UV");
    pixel(pixels.get(),88,40,255,0,0,"alpha-cutout exposes far mesh");
    pixel(pixels.get(),40,88,255,255,0,"bottom-left original UV");
    pixel(pixels.get(),88,88,0,0,255,"near depth occludes far mesh");
    pixel(pixels.get(),5,5,255,0,255,"SDL UI overlay");
    pixel( pixels.get(), 64, 64, 127, 0, 0, "shared ramp depth and tint" );
    pixel( pixels.get(), 42, 64, 255, 0, 255, "solid material ignores atlas alpha" );
    pixel( pixels.get(), 94, 30, 0, 255, 255, "wheel decal cutoff accepts alpha .25", 64 );
    pixel( pixels.get(), 94, 98, 127, 0, 0, "vertex normal ambient + directional light" );
    Uint8 r = 0, g = 0, b = 0, alpha = 0;
    check( SDL_ReadSurfacePixel( pixels.get(), 35, 31, &r, &g, &b, &alpha ), "glass readback" );
    std::printf( "overlapping translucent solids = %u,%u,%u,%u\n", r, g, b, alpha );
    check( std::abs( r - 64 ) <= 1 && std::abs( g - 64 ) <= 1 && std::abs( b - 128 ) <= 1,
           "translucent pass blends without writing depth" );
    std::puts("PASS: actual SDL atlas + GPU camera matrix + textured mesh ranges + base instance + UV orientation + alpha cutout + depth + UI");
}
}
int main() {
    SDL_SetMainReady();
    if(!SDL_Init(SDL_INIT_VIDEO)) return 1;
    int result=0;
    try {run();} catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());result=1;}
    SDL_Quit();return result;
}
