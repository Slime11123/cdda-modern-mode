// SDL's D3D12 storage views are raw. Strides match fp3d_gpu_terrain_compute.h,
// mesh_instance and SDL_GPUIndexedIndirectDrawCommand.
ByteAddressBuffer cells : register(t0, space0);
ByteAddressBuffer models : register(t1, space0);
ByteAddressBuffer blocks : register(t2, space0);
RWByteAddressBuffer instances : register(u0, space1);
RWByteAddressBuffer commands : register(u1, space1);

cbuffer Camera : register(b0, space2)
{
    float4 planes[6];
    uint cellCount;
    uint modelCount;
    uint blockCount;
    uint renderRevision;
};

bool intersects(float3 center, float3 halfExtent)
{
    bool visible = true;
    [unroll] for (int i = 0; i < 6; ++i) {
        const float4 plane = planes[i];
        const float edge = dot(plane.xyz, center) + plane.w + dot(abs(plane.xyz), halfExtent);
        visible = visible && !(edge < -0.001f);
    }
    return visible;
}

// One thread per block keeps in-block order and avoids global atomic appends.
[numthreads(32, 1, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x >= blockCount) {
        return;
    }

    const uint blockOffset = thread.x * 48;
    const uint4 range = blocks.Load4(blockOffset + 32);
    const uint capacity = 64;
    const uint firstInstance = thread.x * capacity;
    uint firstIndex = 0;
    uint indexCount = 0;
    uint instanceCount = 0;
    if (range.w == renderRevision && range.z < modelCount &&
        range.y <= capacity && range.x <= cellCount && range.y <= cellCount - range.x &&
        intersects(asfloat(blocks.Load3(blockOffset)), asfloat(blocks.Load3(blockOffset + 16)))) {
        const uint modelOffset = range.z * 64;
        const float3 center = asfloat(models.Load3(modelOffset));
        const float3 halfExtent = asfloat(models.Load3(modelOffset + 16));
        const uint4 uv = models.Load4(modelOffset + 32);
        const uint3 modelDraw = models.Load3(modelOffset + 48);
        firstIndex = modelDraw.y;
        indexCount = modelDraw.z;
        for (uint i = 0; i < range.y; ++i) {
            const uint cellOffset = (range.x + i) * 32;
            const float3 translation = asfloat(cells.Load3(cellOffset));
            if (intersects(translation + center, halfExtent)) {
                const uint writeOffset = (firstInstance + instanceCount) * 48;
                instances.Store4(writeOffset, uint4(asuint(translation), modelDraw.x));
                instances.Store4(writeOffset + 16, uv);
                instances.Store4(writeOffset + 32, cells.Load4(cellOffset + 16));
                ++instanceCount;
            }
        }
    }
    const uint commandOffset = thread.x * 20;
    commands.Store4(commandOffset, uint4(indexCount, instanceCount, firstIndex, 0));
    commands.Store(commandOffset + 16, firstInstance);
}
