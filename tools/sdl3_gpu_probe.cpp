// SDL3 GPU-only bring-up probe. Does not link OpenGL or change the game renderer.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_gpu.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void check(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr,"%s: %s\n",what,SDL_GetError()); std::exit(1); }
}
static SDL_GPUShader *shader(SDL_GPUDevice *dev,const char *src,bool vertex) {
    ID3DBlob *blob=nullptr,*err=nullptr;
    HRESULT hr=D3DCompile(src,std::strlen(src),nullptr,nullptr,nullptr,"main",
        vertex?"vs_5_1":"ps_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&err);
    if(FAILED(hr)) {
        if(err) std::fprintf(stderr,"%s\n",static_cast<const char*>(err->GetBufferPointer()));
        std::exit(2);
    }
    if(err) err->Release();
    SDL_GPUShaderCreateInfo info{};
    info.code=static_cast<const Uint8*>(blob->GetBufferPointer());
    info.code_size=blob->GetBufferSize();
    info.entrypoint="main";
    info.format=SDL_GPU_SHADERFORMAT_DXBC;
    info.stage=vertex?SDL_GPU_SHADERSTAGE_VERTEX:SDL_GPU_SHADERSTAGE_FRAGMENT;
    auto *s=SDL_CreateGPUShader(dev,&info);
    blob->Release(); check(s!=nullptr,"SDL_CreateGPUShader"); return s;
}
int main() {
    SDL_SetMainReady();
    check(SDL_Init(SDL_INIT_VIDEO),"SDL_Init");
    auto *dev=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC,true,"direct3d12");
    check(dev!=nullptr,"SDL_CreateGPUDevice");
    std::printf("SDL GPU driver = %s\n",SDL_GetGPUDeviceDriver(dev));
    auto *renderer=SDL_CreateGPURenderer(dev,nullptr);
    check(renderer!=nullptr,"SDL_CreateGPURenderer");
    check(SDL_GetGPURendererDevice(renderer)==dev,"shared renderer/device");
    const char *vs=R"(
struct Out { float4 pos:SV_Position; float4 color:COLOR0; };
Out main(uint v:SV_VertexID,uint i:SV_InstanceID) {
    float2 p[3]={float2(-0.33,-0.6),float2(0.33,-0.6),float2(0,0.6)};
    Out o; o.pos=float4(p[v]+float2(i==0?-0.5:0.5,0),0.4,1);
    o.color=i==0?float4(1,0,0,1):float4(0,1,0,1); return o;
})";
    const char *ps=R"(float4 main(float4 pos:SV_Position,float4 color:COLOR0):SV_Target0 {return color;})";
    auto *v=shader(dev,vs,true), *f=shader(dev,ps,false);
    SDL_GPUTextureCreateInfo tex{};
    tex.type=SDL_GPU_TEXTURETYPE_2D; tex.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tex.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex.width=128; tex.height=64; tex.layer_count_or_depth=1; tex.num_levels=1;
    auto *color=SDL_CreateGPUTexture(dev,&tex); check(color!=nullptr,"color texture");
    tex.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT; tex.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    auto *depth=SDL_CreateGPUTexture(dev,&tex); check(depth!=nullptr,"depth texture");
    SDL_GPUColorTargetDescription desc{}; desc.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader=v; pi.fragment_shader=f; pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pi.depth_stencil_state.enable_depth_test=true; pi.depth_stencil_state.enable_depth_write=true;
    pi.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS;
    pi.target_info.color_target_descriptions=&desc; pi.target_info.num_color_targets=1;
    pi.target_info.has_depth_stencil_target=true; pi.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    auto *pipeline=SDL_CreateGPUGraphicsPipeline(dev,&pi); check(pipeline!=nullptr,"graphics pipeline");
    SDL_ReleaseGPUShader(dev,v); SDL_ReleaseGPUShader(dev,f);
    SDL_GPUColorTargetInfo target{}; target.texture=color; target.load_op=SDL_GPU_LOADOP_CLEAR;
    target.store_op=SDL_GPU_STOREOP_STORE; target.clear_color={0,0,0,1};
    SDL_GPUDepthStencilTargetInfo dt{}; dt.texture=depth; dt.clear_depth=1;
    dt.load_op=SDL_GPU_LOADOP_CLEAR; dt.store_op=SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE; dt.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto *cmd=SDL_AcquireGPUCommandBuffer(dev); check(cmd!=nullptr,"command buffer");
    auto *pass=SDL_BeginGPURenderPass(cmd,&target,1,&dt); check(pass!=nullptr,"render pass");
    SDL_BindGPUGraphicsPipeline(pass,pipeline); SDL_DrawGPUPrimitives(pass,3,2,0,0);
    SDL_EndGPURenderPass(pass);
    auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(cmd); check(fence!=nullptr,"submit");
    check(SDL_WaitForGPUFences(dev,true,&fence,1),"fence wait (probe only)");
    SDL_ReleaseGPUFence(dev,fence);
    SDL_PropertiesID props=SDL_CreateProperties(); check(props!=0,"properties");
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_RGBA32);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,128);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,64);
    SDL_SetPointerProperty(props,SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER,color);
    auto *wrapped=SDL_CreateTextureWithProperties(renderer,props); SDL_DestroyProperties(props);
    check(wrapped!=nullptr,"GPU texture wrapping");
    auto *output=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,128,64);
    check(output!=nullptr,"2D output texture");
    check(SDL_SetRenderTarget(renderer,output),"2D render target");
    check(SDL_SetTextureBlendMode(wrapped,SDL_BLENDMODE_NONE),"blend state");
    check(SDL_RenderTexture(renderer,wrapped,nullptr,nullptr),"GPU image composite");
    auto *pixels=SDL_RenderReadPixels(renderer,nullptr); check(pixels!=nullptr,"readback (probe only)");
    Uint8 r=0,g=0,b=0,a=0;
    check(SDL_ReadSurfacePixel(pixels,32,32,&r,&g,&b,&a),"left pixel");
    std::printf("left RGBA=%u,%u,%u,%u\n",r,g,b,a);
    check(r>240&&g<10&&b<10,"left instance color");
    check(SDL_ReadSurfacePixel(pixels,96,32,&r,&g,&b,&a),"right pixel");
    std::printf("right RGBA=%u,%u,%u,%u\n",r,g,b,a);
    check(g>240&&r<10&&b<10,"right instance color");
    SDL_DestroySurface(pixels);
    check(SDL_RenderPresent(renderer),"offscreen present");
    check(SDL_SetRenderTarget(renderer,nullptr),"reset target");
    check(SDL_WaitForGPUIdle(dev),"final idle");
    SDL_DestroyTexture(output); SDL_DestroyTexture(wrapped);
    // Wrapped GPU textures are externally owned; release after wrapper.
    SDL_ReleaseGPUGraphicsPipeline(dev,pipeline); SDL_ReleaseGPUTexture(dev,depth);
    SDL_ReleaseGPUTexture(dev,color);
    SDL_DestroyRenderer(renderer); SDL_DestroyGPUDevice(dev); SDL_Quit();
    std::puts("PASS: GPU-only instanced draw, depth attachment, fence and SDL renderer composite.");
}
