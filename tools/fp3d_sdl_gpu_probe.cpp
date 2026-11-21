// Native SDL3 GPU backend bring-up. This is a standalone QA executable,
// not a replacement for the current game renderer yet. No OpenGL dependency.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_gpu.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include "../src/fp3d_gpu_target.h"
#include "../src/fp3d_gpu_buffer.h"
#include "../src/fp3d_gpu_mesh.h"
#include <cstddef>

namespace {
void require(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s: %s\n", message, SDL_GetError());
        throw std::runtime_error(message);
    }
}
template<typename T> T *require_ptr(T *p, const char *message) {
    require(p != nullptr, message);
    return p;
}
struct blob_release {
    void operator()(ID3DBlob *p) const { if (p) p->Release(); }
};
SDL_GPUShader *shader(SDL_GPUDevice *device, const char *source, const char *profile,
                      SDL_GPUShaderStage stage) {
    ID3DBlob *raw = nullptr, *errors = nullptr;
    const HRESULT result = D3DCompile(source, std::strlen(source), "fp3d_probe.hlsl",
        nullptr, nullptr, "main", profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &raw, &errors);
    std::unique_ptr<ID3DBlob, blob_release> bytes(raw), diagnostics(errors);
    if (FAILED(result)) {
        if (diagnostics) std::fprintf(stderr, "%.*s\n", (int)diagnostics->GetBufferSize(),
                                      (const char *)diagnostics->GetBufferPointer());
        throw std::runtime_error("D3DCompile SM5.1 failed");
    }
    SDL_GPUShaderCreateInfo info{};
    info.code = static_cast<const Uint8 *>(bytes->GetBufferPointer());
    info.code_size = bytes->GetBufferSize();
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXBC;
    info.stage = stage;
    return require_ptr(SDL_CreateGPUShader(device, &info), "create shader");
}
constexpr char vs[] = R"hlsl(
struct V { float4 pos : SV_Position; float4 color : COLOR0; };
V main(float3 position : TEXCOORD0, float z : TEXCOORD1, float4 color : TEXCOORD2) {
    V o;
    o.pos = float4(position.x, position.z, z, 1);
    o.color = color;
    return o;
}
)hlsl";
constexpr char ps[] = R"hlsl(
float4 main(float4 pos : SV_Position, float4 color : COLOR0) : SV_Target0 {
    return color;
}
)hlsl";
struct objects {
    SDL_GPUDevice *device = nullptr;
    SDL_Renderer *renderer = nullptr;
    fp3d_gpu::scene_target scene;
    fp3d_gpu::indexed_mesh mesh;
    fp3d_gpu::upload_buffer instances;
    SDL_Texture *output = nullptr;
    SDL_GPUShader *vertex = nullptr, *fragment = nullptr;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    SDL_GPUFence *fence = nullptr;
    ~objects() {
        if (device) {
            SDL_WaitForGPUIdle(device);
            if (fence) SDL_ReleaseGPUFence(device, fence);
            if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
            if (fragment) SDL_ReleaseGPUShader(device, fragment);
            if (vertex) SDL_ReleaseGPUShader(device, vertex);
        }
        if (output) SDL_DestroyTexture(output);
        mesh.reset();
        instances.reset();
        scene.reset();
        if (renderer) SDL_DestroyRenderer(renderer);
        if (device) SDL_DestroyGPUDevice(device);
    }
};
void pixel(SDL_Surface *s, int x, int y, Uint8 r, Uint8 g, Uint8 b, const char *label) {
    Uint8 ar=0, ag=0, ab=0, aa=0;
    require(SDL_ReadSurfacePixel(s,x,y,&ar,&ag,&ab,&aa), "QA pixel fetch");
    std::printf("%s: (%u,%u,%u,%u)\n", label, ar, ag, ab, aa);
    require(ar==r && ag==g && ab==b && aa==255, label);
}
void run() {
    objects o;
    o.device = require_ptr(SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC |
        SDL_GPU_SHADERFORMAT_DXIL, true, "direct3d12"), "D3D12 device");
    std::printf("driver=%s\n", SDL_GetGPUDeviceDriver(o.device));
    o.renderer = require_ptr(SDL_CreateGPURenderer(o.device, nullptr), "offscreen GPU renderer");
    require(SDL_GetGPURendererDevice(o.renderer)==o.device, "same GPU device");
    std::printf("renderer=%s\n", SDL_GetRendererName(o.renderer));
    constexpr int size=128;
    require(o.scene.initialize(o.renderer,size,size),"shared scene target");
    require(o.scene.device()==o.device,"target device identity");
    o.output = require_ptr(SDL_CreateTexture(o.renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_TARGET,size,size), "UI target");
    o.vertex=shader(o.device,vs,"vs_5_1",SDL_GPU_SHADERSTAGE_VERTEX);
    o.fragment=shader(o.device,ps,"ps_5_1",SDL_GPU_SHADERSTAGE_FRAGMENT);
    SDL_GPUColorTargetDescription color{};
    color.format=fp3d_gpu::scene_target::color_format();
    SDL_GPUGraphicsPipelineCreateInfo pipeline{};
    pipeline.vertex_shader=o.vertex; pipeline.fragment_shader=o.fragment;
    pipeline.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    const SDL_GPUVertexBufferDescription buffers[] = {
        {0, sizeof(fp3d_geometry::vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
        {1, 5*sizeof(float), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0}
    };
    const SDL_GPUVertexAttribute attributes[] = {
        {0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(fp3d_geometry::vertex,position)},
        {1,1,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,0},
        {2,1,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,sizeof(float)}
    };
    pipeline.vertex_input_state={buffers,2,attributes,3};
    pipeline.depth_stencil_state.enable_depth_test=true;
    pipeline.depth_stencil_state.enable_depth_write=true;
    pipeline.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS;
    pipeline.target_info.color_target_descriptions=&color;
    pipeline.target_info.num_color_targets=1;
    pipeline.target_info.has_depth_stencil_target=true;
    pipeline.target_info.depth_stencil_format=fp3d_gpu::scene_target::depth_format();
    o.pipeline=require_ptr(SDL_CreateGPUGraphicsPipeline(o.device,&pipeline),"graphics pipeline");
    require(SDL_SetRenderTarget(o.renderer,o.output),"set offscreen UI target");
    require(SDL_FlushRenderer(o.renderer),"flush before native GPU pass");
    fp3d_geometry::mesh geometry;
    fp3d_geometry::quad_mesh_sink sink(geometry);
    struct uv_rect {float left, top, right, bottom;};
    fp3d_geometry::floor_face(sink,0,0,0,uv_rect{0,0,1,1},.8f);
    require(geometry.vertices.size()==4 && geometry.indices.size()==6,"shared floor mesh");
    const float instances[][5]={{.2f,0,1,0,1},{.8f,1,0,0,1}};
    require(o.mesh.initialize(o.device,geometry),"shared indexed mesh upload");
    require(o.instances.initialize(o.device,sizeof(instances),SDL_GPU_BUFFERUSAGE_VERTEX),"instance buffer");
    require(o.instances.stage(instances,sizeof(instances)),"stage instances");
    auto *cmd=require_ptr(SDL_AcquireGPUCommandBuffer(o.device),"GPU command buffer");
    auto *copy=SDL_BeginGPUCopyPass(cmd);
    if (!copy) { SDL_CancelGPUCommandBuffer(cmd); require(false,"copy pass"); }
    const bool copied=o.mesh.record(copy) && o.instances.record(copy);
    SDL_EndGPUCopyPass(copy);
    if (!copied) { SDL_CancelGPUCommandBuffer(cmd); require(false,"buffer upload"); }
    SDL_GPUColorTargetInfo target{};
    target.texture=o.scene.native_color(); target.clear_color={0,0,1,1};
    target.load_op=SDL_GPU_LOADOP_CLEAR; target.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo dt{};
    dt.texture=o.scene.depth(); dt.clear_depth=1;
    dt.load_op=SDL_GPU_LOADOP_CLEAR; dt.store_op=SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto *pass=SDL_BeginGPURenderPass(cmd,&target,1,&dt);
    if (!pass) { SDL_CancelGPUCommandBuffer(cmd); require(false,"native render pass"); }
    SDL_BindGPUGraphicsPipeline(pass,o.pipeline);
    const SDL_GPUBufferBinding instances_binding{o.instances.get(),0};
    SDL_BindGPUVertexBuffers(pass,1,&instances_binding,1);
    const bool drawn=o.mesh.draw(pass,2);
    if (!drawn) { SDL_EndGPURenderPass(pass); SDL_CancelGPUCommandBuffer(cmd); require(false,"indexed draw"); }
    SDL_EndGPURenderPass(pass);
    o.fence=require_ptr(SDL_SubmitGPUCommandBufferAndAcquireFence(cmd),"GPU submit");
    // Only the QA probe waits immediately; game frames must use bounded in-flight slots.
    require(SDL_WaitForGPUFences(o.device,true,&o.fence,1),"GPU fence");
    require(SDL_SetRenderTarget(o.renderer,o.output),"set UI target");
    require(SDL_RenderTexture(o.renderer,o.scene.color(),nullptr,nullptr),"GPU scene composite");
    require(SDL_SetRenderDrawColor(o.renderer,255,255,0,255),"UI color");
    SDL_FRect ui{0,0,12,12};
    require(SDL_RenderFillRect(o.renderer,&ui),"2D UI overlay");
    require(SDL_RenderPresent(o.renderer),"GPU present");
    // Explicit QA readback, never on the gameplay render loop.
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(
        require_ptr(SDL_RenderReadPixels(o.renderer,nullptr),"QA GPU readback"),SDL_DestroySurface);
    pixel(pixels.get(),64,64,0,255,0,"near green instance survives far red (depth test)");
    pixel(pixels.get(),127,0,0,0,255,"blue clear");
    pixel(pixels.get(),5,5,255,255,0,"SDL 2D UI composited");
    std::puts("PASS: SDL3 GPU D3D12 shared floor mesh + indexed draw + depth + instancing + renderer texture + fence + UI");
}
}
int main() {
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr,"%s\n",SDL_GetError()); return 1; }
    int status=0;
    try { run(); } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); status=1; }
    SDL_Quit();
    return status;
}
