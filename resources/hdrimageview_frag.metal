#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

#include "colormaps_frag.h"
#include "colorspaces_frag.h"

#define CHANNEL_RGB 0
#define CHANNEL_RED 1
#define CHANNEL_GREEN 2
#define CHANNEL_BLUE 3
#define CHANNEL_ALPHA 4
#define CHANNEL_LUMINANCE 5
#define CHANNEL_GRAY 6
#define CHANNEL_CIE_L 7
#define CHANNEL_CIE_a 8
#define CHANNEL_CIE_b 9
#define CHANNEL_CIE_CHROMATICITY 10
#define CHANNEL_FALSE_COLOR 11
#define CHANNEL_POSITIVE_NEGATIVE 12

#define NORMAL_BLEND 0
#define MULTIPLY_BLEND 1
#define DIVIDE_BLEND 2
#define ADD_BLEND 3
#define AVERAGE_BLEND 4
#define SUBTRACT_BLEND 5
#define DIFFERENCE_BLEND 6
#define RELATIVE_DIFFERENCE_BLEND 7


struct VertexOut
{
    float4 position [[position]];
    float2 primary_uv;
    float2 secondary_uv;
};

float3 tonemap(const float3 color, const float gamma, const bool sRGB)
{
    return sRGB ? linearToSRGB(color) : sign(color) * pow(abs(color), float3(1.0 / gamma));
}

// note: uniformly distributed, normalized rand, [0;1[
float nrand(float2 n)
{
    return fract(sin(dot(n.xy, float2(12.9898, 78.233)))* 43758.5453);
}

float rand_box(float2 xy, texture2d<float, access::sample> dither_texture, sampler dither_sampler)
{
    // Result is in range [-0.5, 0.5]
    return dither_texture.sample(dither_sampler, xy/float2(256,256)).r/65536 - 0.5;
}

float rand_tent(float2 xy, texture2d<float, access::sample> dither_texture, sampler dither_sampler)
{
    float r = rand_box(xy, dither_texture, dither_sampler);

    // Convert uniform distribution into triangle-shaped distribution
    // Result is in range [-1.0,1.0]
    float rp = sqrt(2*r);       // positive triangle
    float rn = sqrt(2*r+1)-1;   // negative triangle
    return (r < 0) ? rn : rp;
}

float3 choose_channel(float4 rgba, int channel)
{
    float3 rgb = rgba.rgb;
    switch (channel)
    {
        case CHANNEL_RED:               return rgba.rrr;
        case CHANNEL_GREEN:             return rgba.ggg;
        case CHANNEL_BLUE:              return rgba.bbb;
        case CHANNEL_ALPHA:             return rgba.aaa;
        case CHANNEL_LUMINANCE:         return RGBToLuminance(rgb);
        case CHANNEL_GRAY:              return RGBToGray(rgb);
        case CHANNEL_CIE_L:             return RGBToLab(rgb).xxx;
        case CHANNEL_CIE_a:             return RGBToLab(rgb).yyy;
        case CHANNEL_CIE_b:             return RGBToLab(rgb).zzz;
        case CHANNEL_CIE_CHROMATICITY:  return LabToRGB(float3(0.5, RGBToLab(rgb).yz));
        // case CHANNEL_FALSE_COLOR:       return jetFalseColor(saturate(RGBToLuminance(col).r));
        case CHANNEL_FALSE_COLOR:       return inferno(saturate(RGBToLuminance(rgb).r));
        case CHANNEL_POSITIVE_NEGATIVE: return positiveNegative(rgb);
    }
    return rgb;
}

float4 blend(float4 top, float4 bottom, int blend_mode)
{
    float3 diff = top.rgb - bottom.rgb;
    float alpha = top.a + bottom.a*(1-top.a);
    switch (blend_mode)
    {
        case NORMAL_BLEND:              return float4(top.rgb + bottom.rgb*(1-top.a), alpha);
        case MULTIPLY_BLEND:            return float4(top.rgb * bottom.rgb, alpha);
        case DIVIDE_BLEND:              return float4(top.rgb / bottom.rgb, alpha);
        case ADD_BLEND:                 return float4(top.rgb + bottom.rgb, alpha);
        case AVERAGE_BLEND:             return 0.5*(top + bottom);
        case SUBTRACT_BLEND:            return float4(diff, alpha);
        case DIFFERENCE_BLEND:          return float4(abs(diff), alpha);
        case RELATIVE_DIFFERENCE_BLEND: return float4(abs(diff) / (bottom.rgb + float3(0.01)), alpha);
    }
    return float4(0.0);
}

float3 dither(float3 color, float2 xy, const float2 randomness, const bool do_dither, texture2d<float, access::sample> dither_texture, sampler dither_sampler)
{
    if (!do_dither)
		return color;

    return color + float3(rand_tent(xy + randomness, dither_texture, dither_sampler)/255.0);
}

float4 sample(texture2d<float, access::sample> texture, sampler the_sampler, float2 uv)
{
    if (uv.x > 1.0 || uv.y > 1.0 || uv.x < 0.0 || uv.y < 0.0)
        return float4(0.0);

    return texture.sample(the_sampler, uv);
}

fragment float4 fragment_main(VertexOut vert [[stage_in]],
                              texture2d<float, access::sample> primary_texture,
                              texture2d<float, access::sample> secondary_texture,
                              texture2d<float, access::sample> dither_texture,
                              sampler primary_sampler,
                              sampler secondary_sampler,
                              sampler dither_sampler,
                              constant uint &has_reference,
                              constant uint &do_dither,
                              constant float2 &randomness,
                              constant int &blend_mode,
                              constant int &channel,
                              constant float &gain,
                              constant float &gamma,
                              constant bool &sRGB,
                              constant bool &LDR)
{
    float dark_gray = 0.1;
    float light_gray = 0.2;
    float checkerboard = (fmod(float(int(floor(vert.position.x / 8.0) + floor(vert.position.y / 8.0))), 2.0) == 0.0) ? dark_gray : light_gray;
    float4 background = float4(float3(checkerboard), 1.0);

    float4 value = sample(primary_texture, primary_sampler, vert.primary_uv);

    if (has_reference != 0u)
    {
        float4 reference_val = sample(secondary_texture, secondary_sampler, vert.secondary_uv);
        value = blend(value, reference_val, blend_mode);
    }

    float3 blended = dither(tonemap(choose_channel(value * gain, channel), gamma, sRGB), vert.position.xy, randomness, do_dither, dither_texture, dither_sampler) + background.rgb*(1-value.a);
    blended = clamp(blended, LDR ? 0.0f : -64.0f, LDR ? 1.0f : 64.0f);
    return float4(blended, 1.0);
}

