#include <metal_stdlib>
#include <simd/simd.h>

// #include "colorspaces.metal"
// #include "colormaps.metal"

using namespace metal;

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

#define RGBA_Channels 0
#define RGB_Channels 1
#define XYZA_Channels 2
#define XYZ_Channels 3
#define YCA_Channels 4
#define YC_Channels 5
#define YA_Channels     6
#define UVorXY_Channels 7
#define Z_Channel 8
#define Single_Channel 9
    


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

float4 inv_tonemap(const float4 color, const float gamma, const bool sRGB)
{
    return float4(sRGB ? sRGBToLinear(color.rgb) : sign(color.rgb) * pow(abs(color.rgb), float3(gamma)), color.a);
}

float rand_box(float2 xy, texture2d<float, access::sample> dither_texture, sampler dither_sampler)
{
    // Result is in range (-0.5, 0.5)
    return (dither_texture.sample(dither_sampler, xy/float2(256,256)).r+0.5)/65536 - 0.5;
}

float rand_tent(float2 xy, texture2d<float, access::sample> dither_texture, sampler dither_sampler)
{
    float r = rand_box(xy, dither_texture, dither_sampler);

    // Convert uniform distribution into triangle-shaped distribution
    // Result is in range (-0.5,0.5)
    float rp = sqrt(2*r);       // positive triangle
    float rn = sqrt(2*r+1)-1;   // negative triangle
    return 0.5 * ((r < 0) ? rn : rp);
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

    return color + float4(float3(rand_tent(xy + randomness, dither_texture, dither_sampler)/256.0), 0.0);
}

float sample_channel(texture2d<float, access::sample> texture, sampler the_sampler, float2 uv, bool within_image)
{
    return (within_image) ? texture.sample(the_sampler, uv).r : 0.0;
}

fragment float4 fragment_main(VertexOut vert [[stage_in]],
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
                              const constant float4 &bg_color,
                              texture2d<float, access::sample> dither_texture,
                              sampler dither_sampler,
                              texture2d<float, access::sample> primary_0_texture,
                              texture2d<float, access::sample> primary_1_texture,
                              texture2d<float, access::sample> primary_2_texture,
                              texture2d<float, access::sample> primary_3_texture,
                              sampler primary_0_sampler,
                              sampler primary_1_sampler,
                              sampler primary_2_sampler,
                              sampler primary_3_sampler,
                              const constant float3 &primary_yw,
                              const constant int &primary_channels_type,
                              const constant float4x4 &primary_M_to_Rec709,
                              texture2d<float, access::sample> secondary_0_texture,
                              texture2d<float, access::sample> secondary_1_texture,
                              texture2d<float, access::sample> secondary_2_texture,
                              texture2d<float, access::sample> secondary_3_texture,
                              sampler secondary_0_sampler,
                              sampler secondary_1_sampler,
                              sampler secondary_2_sampler,
                              sampler secondary_3_sampler,
                              const constant float3 &secondary_yw,
                              const constant int &secondary_channels_type,
                              const constant float4x4 &secondary_M_to_Rec709
                            )
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
    // inverse tonemap the background color so that it appears correct when we blend and tonemap below
    background = inv_tonemap(background, gamma, sRGB);

    bool in_img = all(vert.primary_uv < 1.0) and all(vert.primary_uv > 0.0);
    bool in_ref = all(vert.secondary_uv < 1.0) and all(vert.secondary_uv > 0.0);

    if (!in_img and !in_ref)
        return background;

    float4 value = float4(sample_channel(primary_0_texture, primary_0_sampler, vert.primary_uv, in_img),
                          sample_channel(primary_1_texture, primary_1_sampler, vert.primary_uv, in_img),
                          sample_channel(primary_2_texture, primary_2_sampler, vert.primary_uv, in_img),
                          sample_channel(primary_3_texture, primary_3_sampler, vert.primary_uv, in_img));

    if (primary_channels_type == YCA_Channels || primary_channels_type == YC_Channels)
        value.xyz = YCToRGB(value.xyz, primary_yw);
    else if (primary_channels_type == YA_Channels)
        value.xyz = YCToRGB(value.xyz, primary_yw);

    value = primary_M_to_Rec709 * value;

    if (has_reference)
    {
        float4 reference_val = float4(sample_channel(secondary_0_texture, secondary_0_sampler, vert.secondary_uv, in_ref),
                                      sample_channel(secondary_1_texture, secondary_1_sampler, vert.secondary_uv, in_ref),
                                      sample_channel(secondary_2_texture, secondary_2_sampler, vert.secondary_uv, in_ref),
                                      sample_channel(secondary_3_texture, secondary_3_sampler, vert.secondary_uv, in_ref));

        if (secondary_channels_type == YCA_Channels || secondary_channels_type == YC_Channels)
            reference_val.xyz = YCToRGB(reference_val.xyz, secondary_yw);

        reference_val = secondary_M_to_Rec709 * reference_val;

        value = blend(value, reference_val, blend_mode);
    }

    float4 foreground = choose_channel(value, channel) * float4(float3(gain), 1.0);
    float4 blended = dither(tonemap(foreground + background*(1-foreground.a), gamma, sRGB), vert.position.xy, randomness, do_dither, dither_texture, dither_sampler);
    blended = clamp(blended, clamp_to_LDR ? 0.0 : -64.0, clamp_to_LDR ? 1.0 : 64.0);
    return float4(blended.rgb, 1.0);
}