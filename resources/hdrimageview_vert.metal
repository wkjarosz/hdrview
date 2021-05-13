#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct VertexOut
{
    float4 position [[position]];
    float2 image_uv;
    float2 reference_uv;
};

vertex VertexOut vertex_main(device const float2 * position,
                             constant float2 &image_scale,
                             constant float2 &image_pos,
                             constant float2 &reference_scale,
                             constant float2 &reference_pos,
                             uint id [[vertex_id]])
{
    VertexOut vert = {};
    vert.image_uv = (((position[id] / float2(2.0)) - image_pos) + float2(0.5)) / image_scale;
    vert.reference_uv = (((position[id] / float2(2.0)) - reference_pos) + float2(0.5)) / reference_scale;
    vert.position = float4(position[id].x, -position[id].y, 0.0, 1.0);
    return vert;
}










