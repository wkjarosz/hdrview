#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct VertexOut
{
    float4 position [[position]];
    float2 image_uv;
    float2 reference_uv;
};

float3 tonemap(thread const float3& color, const float gamma)
{
    return pow(color, float3(1.0 / gamma));
}

fragment float4 fragment_main(VertexOut vert [[stage_in]],
                              texture2d<float, access::sample> image,
                              texture2d<float, access::sample> reference,
                              texture2d<float, access::sample> dither_img,
                              sampler image_sampler,
                              sampler reference_sampler,
                              constant uint &has_reference,
                              constant uint &do_dither,
                              constant float2 &randomness,
                              constant int &blend_mode,
                              constant int &channel,
                              constant float &gain,
                              constant float &gamma,
                              constant uint &sRGB)
{
    float dark_gray = 0.1;
    float light_gray = 0.2;
    float checkerboard = (fmod(float(int(floor(vert.position.x / 8.0) + floor(vert.position.y / 8.0))), 2.0) == 0.0) ? dark_gray : light_gray;
    float4 background = float4(float3(checkerboard), 1.0);

    float4 value = image.sample(image_sampler, vert.image_uv);
    if (vert.image_uv.x > 1.0 || vert.image_uv.y > 1.0 || vert.image_uv.x < 0.0 || vert.image_uv.y < 0.0)
        value = float4(0.0);

    if (has_reference != 0u)
    {
        float4 reference_val = reference.sample(reference_sampler, vert.reference_uv);
        if (vert.reference_uv.x > 1.0 || vert.reference_uv.y > 1.0 || vert.reference_uv.x < 0.0 || vert.reference_uv.y < 0.0)
            reference_val = float4(0.0);
    }

    float3 param = value.xyz * gain;
    float3 blended = mix(background.xyz, tonemap(param, gamma), float3(value.w));
    return float4(blended.x, blended.y, blended.z, 1.0);
}

