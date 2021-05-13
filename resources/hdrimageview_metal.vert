#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct ImageBlock
{
    float2 image_scale;
    float2 image_pos;
    float2 reference_scale;
    float2 reference_pos;
};

struct VertexOut
{
    float2 image_uv [[user(locn0)]];
    float2 reference_uv [[user(locn1)]];
    float4 position [[position]];
};

struct VertexIn
{
    float2 position [[attribute(0)]];
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]], constant ImageBlock& ubo [[buffer(0)]])
{
    VertexOut vert = {};
    vert.image_uv = (((in.position / float2(2.0)) - ubo.image_pos) + float2(0.5)) / ubo.image_scale;
    vert.reference_uv = (((in.position / float2(2.0)) - ubo.reference_pos) + float2(0.5)) / ubo.reference_scale;
    vert.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
    return vert;
}










