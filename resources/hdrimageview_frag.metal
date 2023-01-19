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

#define BG_BLACK 0
#define BG_WHITE 1
#define BG_DARK_CHECKER 2
#define BG_LIGHT_CHECKER 3
#define BG_CUSTOM_COLOR 4


struct VertexOut
{
    float4 position [[position]];
    float2 primary_uv;
    float2 secondary_uv;
};

float4 tonemap(const float4 color, const float gamma, const bool sRGB)
{
    return float4(sRGB ? linearToSRGB(color.rgb) : sign(color.rgb) * pow(abs(color.rgb), float3(1.0 / gamma)), color.a);
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

float4 choose_channel(float4 rgba, int channel)
{
    switch (channel)
    {
        case CHANNEL_RGB:               return rgba;
        case CHANNEL_RED:               return float4(rgba.rrr, 1.0);
        case CHANNEL_GREEN:             return float4(rgba.ggg, 1.0);
        case CHANNEL_BLUE:              return float4(rgba.bbb, 1.0);
        case CHANNEL_ALPHA:             return float4(rgba.aaa, 1.0);
        case CHANNEL_LUMINANCE:         return float4(RGBToLuminance(rgba.rgb), 1.0);
        case CHANNEL_GRAY:              return float4(RGBToGray(rgba.rgb), 1.0);
        case CHANNEL_CIE_L:             return float4(RGBToLab(rgba.rgb).xxx, 1.0);
        case CHANNEL_CIE_a:             return float4(RGBToLab(rgba.rgb).yyy, 1.0);
        case CHANNEL_CIE_b:             return float4(RGBToLab(rgba.rgb).zzz, 1.0);
        case CHANNEL_CIE_CHROMATICITY:  return float4(LabToRGB(float3(0.5, RGBToLab(rgba.rgb).yz)), 1.0);
        // case CHANNEL_FALSE_COLOR:       return jetFalseColor(saturate(RGBToLuminance(col).r));
        case CHANNEL_FALSE_COLOR:       return float4(inferno(saturate(RGBToLuminance(rgba.rgb).r)), 1.0);
        case CHANNEL_POSITIVE_NEGATIVE: return float4(positiveNegative(rgba.rgb), 1.0);
    }
    return rgba;
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

float4 dither(float4 color, float2 xy, const float2 randomness, const bool do_dither, texture2d<float, access::sample> dither_texture, sampler dither_sampler)
{
    if (!do_dither)
		return color;

    return color + float4(float3(rand_tent(xy + randomness, dither_texture, dither_sampler)/255.0), 0.0);
}

float4 sample(texture2d<float, access::sample> texture, sampler the_sampler, float2 uv, bool within_image)
{
    return (within_image) ? texture.sample(the_sampler, uv) : float4(0.0);
}

fragment float4 fragment_main(VertexOut vert [[stage_in]],
                              texture2d<float, access::sample> primary_texture,
                              texture2d<float, access::sample> secondary_texture,
                              texture2d<float, access::sample> dither_texture,
                              sampler primary_sampler,
                              sampler secondary_sampler,
                              sampler dither_sampler,
                              const constant bool& has_reference,
                              const constant bool& do_dither,
                              const constant float2 &randomness,
                              const constant int &blend_mode,
                              const constant int &channel,
                              const constant float &gain,
                              const constant float &gamma,
                              const constant bool &sRGB,
                              const constant bool &clamp_to_LDR,
                              const constant int &bg_mode,
                              const constant float4 &bg_color)
{
    float4 background(bg_color.rgb, 1.0);
    if (bg_mode == BG_BLACK)
        background.rgb = float3(0.0);
    else if (bg_mode == BG_WHITE)
        background.rgb = float3(1.0);
    else if (bg_mode == BG_DARK_CHECKER || bg_mode == BG_LIGHT_CHECKER)
    {
        float dark_gray = (bg_mode == BG_DARK_CHECKER) ? 0.1 : 0.5;
        float light_gray = (bg_mode == BG_DARK_CHECKER) ? 0.2 : 0.55;
        float checkerboard = (fmod(float(int(floor(vert.position.x / 8.0) + floor(vert.position.y / 8.0))), 2.0) == 0.0) ? dark_gray : light_gray;
        background.rgb = float3(checkerboard);
    }

    bool in_img = all(vert.primary_uv < 1.0) and all(vert.primary_uv > 0.0);
    bool in_ref = all(vert.secondary_uv < 1.0) and all(vert.secondary_uv > 0.0);

    if (!in_img and !in_ref)
        return background;

    float4 value = sample(primary_texture, primary_sampler, vert.primary_uv, in_img);

    if (has_reference)
    {
        float4 reference_val = sample(secondary_texture, secondary_sampler, vert.secondary_uv, in_ref);
        value = blend(value, reference_val, blend_mode);
    }

    float4 foreground = dither(tonemap(choose_channel(value, channel) * float4(float3(gain), 1.0), gamma, sRGB), vert.position.xy, randomness, do_dither, dither_texture, dither_sampler);
    float4 blended = foreground + background*(1-foreground.a);
    blended = clamp(blended, clamp_to_LDR ? 0.0f : -64.0f, clamp_to_LDR ? 1.0f : 64.0f);
    return float4(blended.rgb, 1.0);
}

