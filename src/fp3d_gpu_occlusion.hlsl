cbuffer Viewport : register(b0, space2)
{
    uint width;
    uint height;
    uint drawCount;
    uint padding;
};

static const uint tileSize = 8;

#ifdef FP3D_OCCLUSION_REDUCE
Texture2D<float> sourceDepth : register(t0, space0);
SamplerState nearestDepth : register(s0, space0);
RWByteAddressBuffer tileDepth : register(u0, space1);
groupshared float depths[64];

[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID, uint3 tile : SV_GroupID, uint index : SV_GroupIndex)
{
    float depth = 1;
    if (pixel.x < width && pixel.y < height) {
        const float2 uv = (float2(pixel.xy) + 0.5f) / float2(width, height);
        depth = sourceDepth.SampleLevel(nearestDepth, uv, 0);
    }
    depths[index] = depth >= 0 && depth <= 1 ? depth : 1;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint step = 32; step > 0; step >>= 1) {
        if (index < step) {
            depths[index] = max(depths[index], depths[index + step]);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (index == 0) {
        const uint columns = (width + tileSize - 1) / tileSize;
        tileDepth.Store((tile.y * columns + tile.x) * 4, asuint(depths[0]));
    }
}
#else
ByteAddressBuffer tileDepth : register(t0, space0);
ByteAddressBuffer candidates : register(t1, space0);
ByteAddressBuffer inputDraws : register(t2, space0);
RWByteAddressBuffer outputDraws : register(u0, space1);

bool occluded(uint4 bounds, float nearDepth, uint eligible)
{
    bool hidden = eligible == 1 && nearDepth > 0 && nearDepth <= 1 &&
                  bounds.x <= bounds.z && bounds.y <= bounds.w && bounds.z < width && bounds.w < height;
    if (hidden) {
        const uint4 tiles = bounds / tileSize;
        const uint count = (tiles.z - tiles.x + 1) * (tiles.w - tiles.y + 1);
        const uint columns = (width + tileSize - 1) / tileSize;
        hidden = count <= 256;
        for (uint y = tiles.y; y <= tiles.w && hidden; ++y) {
            for (uint x = tiles.x; x <= tiles.z && hidden; ++x) {
                const float depth = asfloat(tileDepth.Load((y * columns + x) * 4));
                hidden = depth + 0.0001f < nearDepth;
            }
        }
    }
    return hidden;
}

[numthreads(32, 1, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x >= drawCount) {
        return;
    }
    const uint candidate = thread.x * 32;
    const uint draw = thread.x * 20;
    const uint4 bounds = candidates.Load4(candidate);
    const uint2 visibility = candidates.Load2(candidate + 16);
    uint4 command = inputDraws.Load4(draw);
    if (command.y != 0 && occluded(bounds, asfloat(visibility.x), visibility.y)) {
        command.y = 0;
    }
    outputDraws.Store4(draw, command);
    outputDraws.Store(draw + 16, inputDraws.Load(draw + 16));
}
#endif
