#include <metal_stdlib>
#include <simd/simd.h>

#define CHANNEL_RED 1
#define CHANNEL_GREEN 2
#define CHANNEL_BLUE 3
#define CHANNEL_RGB 0
#define CHANNEL_LUMINANCE 4
#define CHANNEL_CIE_L 5
#define CHANNEL_CIE_a 6
#define CHANNEL_CIE_b 7
#define CHANNEL_CIE_CHROMATICITY 8
#define CHANNEL_FALSE_COLOR 9
#define CHANNEL_POSITIVE_NEGATIVE 10

#define NORMAL_BLEND 0
#define MULTIPLY_BLEND 1
#define DIVIDE_BLEND 2
#define ADD_BLEND 3
#define AVERAGE_BLEND 4
#define SUBTRACT_BLEND 5
#define DIFFERENCE_BLEND 6
#define RELATIVE_DIFFERENCE_BLEND 7

using namespace metal;

struct VertexOut
{
    float4 position [[position]];
    float2 primary_uv;
    float2 secondary_uv;
};

#define min_Lab float3(0, -128, -128)
#define max_Lab float3(100, 128, 128)
#define range_Lab (max_Lab-min_Lab)
#define Lab_d65_wts float3(.95047, 1.000, 1.08883)


float linearToS(float a)
{
    return a < 0.0031308 ? 12.92 * a : 1.055 * pow(a, 1.0/2.4) - 0.055;
}

float3 linearToSRGB(float3 color)
{
    return float3(linearToS(color.r), linearToS(color.g), linearToS(color.b));
}


float sToLinear(float a)
{
    return a < 0.04045 ? (1.0 / 12.92) * a : pow((a + 0.055) * (1.0 / 1.055), 2.4);
}

float3 sRGBToLinear(float3 color)
{
    return float3(sToLinear(color.r), sToLinear(color.g), sToLinear(color.b));
}

// returns the luminance of a linear rgb color
float3 RGBToLuminance(float3 rgb)
{
    const float3 RGB2Y = float3(0.212671, 0.715160, 0.072169);
    return float3(dot(RGB2Y, rgb));
}

// Converts a color from linear RGB to XYZ space
float3 RGBToXYZ(float3 rgb)
{
    const float3x3 RGB2XYZ = float3x3(
        0.412453, 0.212671, 0.019334,
        0.357580, 0.715160, 0.119193,
        0.180423, 0.072169, 0.950227);
    return RGB2XYZ * rgb;
}

// Converts a color from XYZ to linear RGB space
float3 XYZToRGB(float3 xyz)
{
    const float3x3 XYZ2RGB = float3x3(
            3.240479, -0.969256,  0.055648,
        -1.537150,  1.875992, -0.204043,
        -0.498535,  0.041556,  1.057311);
    return XYZ2RGB * xyz;
}

float labf(float t)
{
    const float c1 = 0.008856451679;    // pow(6.0/29.0, 3.0);
    const float c2 = 7.787037037;       // pow(29.0/6.0, 2.0)/3;
    const float c3 = 0.1379310345;      // 16.0/116.0
    return (t > c1) ? pow(t, 1.0/3.0) : (c2*t) + c3;
}

float3 XYZToLab(float3 xyz)
{
    // normalize for D65 white point
    xyz /= Lab_d65_wts;

    float3 v = float3(labf(xyz.x), labf(xyz.y), labf(xyz.z));
    return float3((116.0 * v.y) - 16.0,
                500.0 * (v.x - v.y),
                200.0 * (v.y - v.z));
}

float3 LabToXYZ(float3 lab)
{
    const float eps = 216.0 / 24389.0;
    const float kappa = 24389.0 / 27.0;
    float yr = (lab.x > kappa*eps) ? pow((lab.x + 16.0) / 116.0, 3.) : lab.x / kappa;
    float fy = (yr > eps) ? (lab.x + 16.0) / 116.0 : (kappa*yr + 16.0) / 116.0;
    float fx = lab.y / 500.0 + fy;
    float fz = fy - lab.z / 200.0;

    float fx3 = pow(fx, 3.);
    float fz3 = pow(fz, 3.);

    float3 xyz = float3((fx3 > eps) ? fx3 : (116.0 * fx - 16.0) / kappa,
                    yr,
                    (fz3 > eps) ? fz3 : (116.0 * fz - 16.0) / kappa);

    // unnormalize for D65 white point
    xyz *= Lab_d65_wts;
    return xyz;
}

float3 RGBToLab(float3 rgb)
{
    float3 lab = XYZToLab(RGBToXYZ(rgb));

    // renormalize
    return (lab-min_Lab)/range_Lab;
}

float3 LabToRGB(float3 lab)
{
    // unnormalize
    lab = lab*range_Lab + min_Lab;

    return XYZToRGB(LabToXYZ(lab));
}

float3 jetFalseColor(float x)
{
    float r = saturate((x < 0.7) ? 4.0 * x - 1.5 : -4.0 * x + 4.5);
    float g = saturate((x < 0.5) ? 4.0 * x - 0.5 : -4.0 * x + 3.5);
    float b = saturate((x < 0.3) ? 4.0 * x + 0.5 : -4.0 * x + 2.5);
    return float3(r, g, b);
}

float3 positiveNegative(float3 col)
{
    float x = dot(col, float3(1.0)/3.0);
    float r = saturate(mix(0.0, 1.0, max(x, 0.0)));
    float g = 0.0;
    float b = saturate(mix(0.0, 1.0, -min(x, 0.0)));
    return float3(r, g, b);
}

float3 inferno(const float t)
{
    const float3 c0 = float3(0.0002189403691192265, 0.001651004631001012, -0.01948089843709184);
    const float3 c1 = float3(0.1065134194856116, 0.5639564367884091, 3.932712388889277);
    const float3 c2 = float3(11.60249308247187, -3.972853965665698, -15.9423941062914);
    const float3 c3 = float3(-41.70399613139459, 17.43639888205313, 44.35414519872813);
    const float3 c4 = float3(77.162935699427, -33.40235894210092, -81.80730925738993);
    const float3 c5 = float3(-71.31942824499214, 32.62606426397723, 73.20951985803202);
    const float3 c6 = float3(25.13112622477341, -12.24266895238567, -23.07032500287172);

    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));
}

float3 tonemap(const float3 color, const float gamma, const bool sRGB)
{
    return sRGB ? linearToSRGB(color) : pow(color, float3(1.0 / gamma));
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

float3 choose_channel(float3 col, int channel)
{
    switch (channel)
    {
        case CHANNEL_RED:               return col.rrr;
        case CHANNEL_GREEN:             return col.ggg;
        case CHANNEL_BLUE:              return col.bbb;
        case CHANNEL_LUMINANCE:         return RGBToLuminance(col);
        case CHANNEL_CIE_L:             return RGBToLab(col).xxx;
        case CHANNEL_CIE_a:             return RGBToLab(col).yyy;
        case CHANNEL_CIE_b:             return RGBToLab(col).zzz;
        case CHANNEL_CIE_CHROMATICITY:  return LabToRGB(float3(0.5, RGBToLab(col).yz));
        // case CHANNEL_FALSE_COLOR:       return jetFalseColor(saturate(RGBToLuminance(col).r));
        case CHANNEL_FALSE_COLOR:       return inferno(saturate(RGBToLuminance(col).r));
        case CHANNEL_POSITIVE_NEGATIVE: return positiveNegative(col);
    }
    return col;
}

float4 blend(float4 top, float4 bottom, int blend_mode)
{
    float3 diff = top.rgb - bottom.rgb;
    float alpha = top.a + bottom.a*(1-top.a);
    switch (blend_mode)
    {
        case NORMAL_BLEND:              return float4(top.rgb*top.a + bottom.rgb*bottom.a*(1-top.a), alpha);
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
                              constant uint &sRGB)
{
    float dark_gray = 0.1;
    float light_gray = 0.2;
    float checkerboard = (fmod(float(int(floor(vert.position.x / 8.0) + floor(vert.position.y / 8.0))), 2.0) == 0.0) ? dark_gray : light_gray;
    float4 background = float4(float3(checkerboard), 1.0);

    float4 value = primary_texture.sample(primary_sampler, vert.primary_uv);
    if (vert.primary_uv.x > 1.0 || vert.primary_uv.y > 1.0 || vert.primary_uv.x < 0.0 || vert.primary_uv.y < 0.0)
        value = float4(0.0);

    if (has_reference != 0u)
    {
        float4 reference_val = secondary_texture.sample(secondary_sampler, vert.secondary_uv);
        if (vert.secondary_uv.x > 1.0 || vert.secondary_uv.y > 1.0 || vert.secondary_uv.x < 0.0 || vert.secondary_uv.y < 0.0)
            reference_val = float4(0.0);
    }

    float3 blended = mix(background.rgb, dither(tonemap(choose_channel(value.rgb * gain, channel), gamma, sRGB), vert.position.xy, randomness, do_dither, dither_texture, dither_sampler), float3(value.w));
    return float4(blended, 1.0);
}

