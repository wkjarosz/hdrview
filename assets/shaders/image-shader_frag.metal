#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

// These need to stay in sync with the various enums in fwd.h
#define Channels_RGBA   0
#define Channels_RGB    1
#define Channels_Red    2
#define Channels_Green  3
#define Channels_Blue   4
#define Channels_Alpha  5
#define Channels_Y      6

#define Tonemap_Gamma            0
#define Tonemap_FalseColor       1
#define Tonemap_PositiveNegative 2

#define BlendMode_Normal 0
#define BlendMode_Multiply 1
#define BlendMode_Divide 2
#define BlendMode_Add 3
#define BlendMode_Average 4
#define BlendMode_Subtract 5
#define BlendMode_Relative_Subtract 6
#define BlendMode_Difference 7
#define BlendMode_Relative_Difference 8

#define BGMode_Black 0
#define BGMode_White 1
#define BGMode_Dark_Checker 2
#define BGMode_Light_Checker 3
#define BGMode_Custom_Color 4

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

float4 tonemap(const float4 color, const int mode, const float gamma, texture2d<float, access::sample> colormap, sampler colormap_sampler, const bool reverse_colormap)
{
    switch (mode)
    {
        default:
        case Tonemap_Gamma: return float4(sign(color.rgb) * pow(abs(color.rgb), float3(1.0 / gamma)), color.a);
        case Tonemap_FalseColor: 
        {
            int cmap_size = colormap.get_width(0);
            float t = mix(0.5/cmap_size, (cmap_size-0.5)/cmap_size, dot(color.rgb, float3(1.0/3.0)));
            if (reverse_colormap)
                t = 1.0 - t;
            return float4(sRGBToLinear(colormap.sample(colormap_sampler, float2(t, 0.5)).rgb) * color.a, color.a);
        }
        case Tonemap_PositiveNegative: 
        {
            int cmap_size = colormap.get_width(0);
            float t = mix(0.5/cmap_size, (cmap_size-0.5)/cmap_size, 0.5 * dot(color.rgb, float3(1.0/3.0)) + 0.5);
            if (reverse_colormap)
                t = 1.0 - t;
            return float4(sRGBToLinear(colormap.sample(colormap_sampler, float2(t, 0.5)).rgb) * color.a, color.a);
        }
    }
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

float4 choose_channel(float4 rgba, int channel, const float3 yw)
{
    switch (channel)
    {
        case Channels_RGBA:  return rgba;
        case Channels_RGB:   return float4(rgba.rgb, 1.0);
        case Channels_Red:   return float4(rgba.rrr, 1.0);
        case Channels_Green: return float4(rgba.ggg, 1.0);
        case Channels_Blue:  return float4(rgba.bbb, 1.0);
        case Channels_Alpha: return float4(rgba.aaa, 1.0);
        case Channels_Y:     return float4(float3(dot(rgba.rgb, yw)), rgba.a);
    }
    return rgba;
}

float4 blend(float4 top, float4 bottom, int blend_mode)
{
    float3 diff = top.rgb - bottom.rgb;
    float alpha = top.a + bottom.a*(1-top.a);
    switch (blend_mode)
    {
        case BlendMode_Normal:              return float4(top.rgb + bottom.rgb*(1-top.a), alpha);
        case BlendMode_Multiply:            return float4(top.rgb * bottom.rgb, alpha);
        case BlendMode_Divide:              return float4(top.rgb / bottom.rgb, alpha);
        case BlendMode_Add:                 return float4(top.rgb + bottom.rgb, alpha);
        case BlendMode_Average:             return 0.5*(top + bottom);
        case BlendMode_Subtract:            return float4(diff, alpha);
        case BlendMode_Relative_Subtract:   return float4(diff / (bottom.rgb + float3(0.01)), alpha);
        case BlendMode_Difference:          return float4(abs(diff), alpha);
        case BlendMode_Relative_Difference: return float4(abs(diff) / (bottom.rgb + float3(0.01)), alpha);
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
                              const constant float &time,
                              const constant bool &draw_clip_warnings,
                              const constant float2 &clip_range,
                              const constant float2 &randomness,
                              const constant int &blend_mode,
                              const constant int &channel,
                              const constant float &gain,
                              const constant float &offset,
                              const constant int &tonemap_mode,
                              const constant float &gamma,
                              const constant bool &clamp_to_LDR,
                              const constant int &bg_mode,
                              const constant float4 &bg_color,
                              texture2d<float, access::sample> dither_texture,
                              sampler dither_sampler,
                              texture2d<float, access::sample> colormap,
                              sampler colormap_sampler,
                              const constant bool &reverse_colormap,
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
                              const constant float4x4 &primary_M_to_sRGB,
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
                              const constant float4x4 &secondary_M_to_sRGB
                            )
{
    float4 background(bg_color.rgb, 1.0);
    if (bg_mode == BGMode_Black)
        background.rgb = float3(0.0);
    else if (bg_mode == BGMode_White)
        background.rgb = float3(1.0);
    else if (bg_mode == BGMode_Dark_Checker || bg_mode == BGMode_Light_Checker)
    {
        float dark_gray = (bg_mode == BGMode_Dark_Checker) ? 0.1 : 0.5;
        float light_gray = (bg_mode == BGMode_Dark_Checker) ? 0.2 : 0.55;
        float checkerboard = (fmod(float(int(floor(vert.position.x / 8.0) + floor(vert.position.y / 8.0))), 2.0) == 0.0) ? dark_gray : light_gray;
        background.rgb = sRGBToLinear(float3(checkerboard));
    }

    float zebra1 = (fmod(float(int(floor((vert.position.x + vert.position.y - 30.0*time) / 8.0))), 2.0) == 0.0) ? 0.0 : 1.0;
    float zebra2 = (fmod(float(int(floor((vert.position.x - vert.position.y - 30.0*time) / 8.0))), 2.0) == 0.0) ? 0.0 : 1.0;

    bool in_img = all(vert.primary_uv <= 1.0) and all(vert.primary_uv >= 0.0);
    bool in_ref = all(vert.secondary_uv <= 1.0) and all(vert.secondary_uv >= 0.0);

    float4 value = float4(sample_channel(primary_0_texture, primary_0_sampler, vert.primary_uv, in_img),
                          sample_channel(primary_1_texture, primary_1_sampler, vert.primary_uv, in_img),
                          sample_channel(primary_2_texture, primary_2_sampler, vert.primary_uv, in_img),
                          sample_channel(primary_3_texture, primary_3_sampler, vert.primary_uv, in_img));

    if (primary_channels_type == YCA_Channels || primary_channels_type == YC_Channels)
        value.rgb = YC_to_RGB(value.xyz, primary_yw);

    value = primary_M_to_sRGB * value;

    if (has_reference)
    {
        float4 reference_val = float4(sample_channel(secondary_0_texture, secondary_0_sampler, vert.secondary_uv, in_ref),
                                      sample_channel(secondary_1_texture, secondary_1_sampler, vert.secondary_uv, in_ref),
                                      sample_channel(secondary_2_texture, secondary_2_sampler, vert.secondary_uv, in_ref),
                                      sample_channel(secondary_3_texture, secondary_3_sampler, vert.secondary_uv, in_ref));

        if (secondary_channels_type == YCA_Channels || secondary_channels_type == YC_Channels)
            reference_val.rgb = YC_to_RGB(reference_val.xyz, secondary_yw);

        reference_val = secondary_M_to_sRGB * reference_val;

        value = blend(value, reference_val, blend_mode);
    }

    float4 foreground = choose_channel(value, channel, primary_yw) * float4(float3(gain), 1.0) + float4(float3(offset * value.a), 0.0);
    float4 tonemapped = tonemap(foreground, tonemap_mode, gamma, colormap, colormap_sampler, reverse_colormap) + background*(1-foreground.a);
    bool3 clipped = draw_clip_warnings and foreground.rgb > clip_range.y;
    bool3 crushed = draw_clip_warnings and foreground.rgb < clip_range.x;
    float4 blended = linearToSRGB(tonemapped);
    float4 dithered = dither(blended, vert.position.xy, randomness, do_dither, dither_texture, dither_sampler);
    dithered = clamp(dithered, clamp_to_LDR ? 0.0 : -64.0, clamp_to_LDR ? 1.0 : 64.0);
    dithered.rgb = mix(dithered.rgb, float3(zebra1), float3(clipped));
    dithered.rgb = mix(dithered.rgb, float3(zebra2), float3(crushed));
    return float4(dithered.rgb, 1.0);
}