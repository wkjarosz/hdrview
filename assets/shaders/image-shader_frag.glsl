precision mediump float;

#define CHANNEL_RGB               0
#define CHANNEL_RED               1
#define CHANNEL_GREEN             2
#define CHANNEL_BLUE              3
#define CHANNEL_ALPHA             4
#define CHANNEL_LUMINANCE         5
#define CHANNEL_GRAY              6
#define CHANNEL_CIE_L             7
#define CHANNEL_CIE_a             8
#define CHANNEL_CIE_b             9
#define CHANNEL_CIE_CHROMATICITY  10
#define CHANNEL_FALSE_COLOR       11
#define CHANNEL_POSITIVE_NEGATIVE 12

#define NORMAL_BLEND              0
#define MULTIPLY_BLEND            1
#define DIVIDE_BLEND              2
#define ADD_BLEND                 3
#define AVERAGE_BLEND             4
#define SUBTRACT_BLEND            5
#define DIFFERENCE_BLEND          6
#define RELATIVE_DIFFERENCE_BLEND 7

#define BG_BLACK         0
#define BG_WHITE         1
#define BG_DARK_CHECKER  2
#define BG_LIGHT_CHECKER 3
#define BG_CUSTOM_COLOR  4

#define RGBA_Channels   0
#define RGB_Channels    1
#define XYZA_Channels   2
#define XYZ_Channels    3
#define YCA_Channels    4
#define YC_Channels     5
#define UVorXY_Channels 6
#define Z_Channel       7
#define Single_Channel  8

in vec2 primary_uv;
in vec2 secondary_uv;
// in highp in vec4 gl_FragCoord;

uniform bool  has_reference;
uniform bool  do_dither;
uniform vec2  randomness;
uniform int   blend_mode;
uniform int   channel;
uniform float gain;
uniform float gamma;
uniform bool  sRGB;
uniform bool  clamp_to_LDR;
uniform int   bg_mode;
uniform vec4  bg_color;

uniform sampler2D dither_texture;

uniform sampler2D primary_0_texture;
uniform sampler2D primary_1_texture;
uniform sampler2D primary_2_texture;
uniform sampler2D primary_3_texture;
uniform vec3      primary_yw;
uniform int       primary_channels_type;
uniform mat4      primary_M_to_Rec709;

uniform sampler2D secondary_0_texture;
uniform sampler2D secondary_1_texture;
uniform sampler2D secondary_2_texture;
uniform sampler2D secondary_3_texture;
uniform vec3      secondary_yw;
uniform int       secondary_channels_type;
uniform mat4      secondary_M_to_Rec709;

out vec4 frag_color;

vec4 tonemap(vec4 color)
{
    return vec4(sRGB ? linearToSRGB(color.rgb) : sign(color.rgb) * pow(abs(color.rgb), vec3(1.0 / gamma)), color.a);
}
vec4 inv_tonemap(vec4 color)
{
    return vec4(sRGB ? sRGBToLinear(color.rgb) : sign(color.rgb) * pow(abs(color.rgb), vec3(gamma)), color.a);
}

float rand_box(vec2 xy)
{
    // Result is in range (-0.5, 0.5)
    return (texture(dither_texture, xy / vec2(256, 256)).r + 0.5) / 65536.0 - 0.5;
}

float rand_tent(vec2 xy)
{
    float r = rand_box(xy);

    // Convert uniform distribution into triangle-shaped distribution
    // Result is in range (-0.5,0.5)
    float rp = sqrt(2.0 * r);             // positive triangle
    float rn = sqrt(2.0 * r + 1.0) - 1.0; // negative triangle
    return (r < 0.0) ? 0.5 * rn : 0.5 * rp;
}

vec4 choose_channel(vec4 rgba)
{
    switch (channel)
    {
    case CHANNEL_RGB: return rgba;
    case CHANNEL_RED: return vec4(rgba.rrr, 1.0);
    case CHANNEL_GREEN: return vec4(rgba.ggg, 1.0);
    case CHANNEL_BLUE: return vec4(rgba.bbb, 1.0);
    case CHANNEL_ALPHA: return vec4(rgba.aaa, 1.0);
    case CHANNEL_LUMINANCE: return vec4(RGBToLuminance(rgba.rgb), 1.0);
    case CHANNEL_GRAY: return vec4(RGBToGray(rgba.rgb), 1.0);
    case CHANNEL_CIE_L: return vec4(RGBToLab(rgba.rgb).xxx, 1.0);
    case CHANNEL_CIE_a: return vec4(RGBToLab(rgba.rgb).yyy, 1.0);
    case CHANNEL_CIE_b: return vec4(RGBToLab(rgba.rgb).zzz, 1.0);
    case CHANNEL_CIE_CHROMATICITY: return vec4(LabToRGB(vec3(0.5, RGBToLab(rgba.rgb).yz)), 1.0);
    // case CHANNEL_FALSE_COLOR:       return jetFalseColor(saturate(RGBToLuminance(col).r));
    case CHANNEL_FALSE_COLOR: return vec4(inferno(saturate(RGBToLuminance(rgba.rgb).r)), 1.0);
    case CHANNEL_POSITIVE_NEGATIVE: return vec4(positiveNegative(rgba.rgb), 1.0);
    }
    return rgba;
}

vec4 blend(vec4 top, vec4 bottom)
{
    vec3  diff  = top.rgb - bottom.rgb;
    float alpha = top.a + bottom.a * (1.0 - top.a);
    switch (blend_mode)
    {
    case NORMAL_BLEND: return vec4(top.rgb + bottom.rgb * (1.0 - top.a), alpha);
    case MULTIPLY_BLEND: return vec4(top.rgb * bottom.rgb, alpha);
    case DIVIDE_BLEND: return vec4(top.rgb / bottom.rgb, alpha);
    case ADD_BLEND: return vec4(top.rgb + bottom.rgb, alpha);
    case AVERAGE_BLEND: return 0.5 * (top + bottom);
    case SUBTRACT_BLEND: return vec4(diff, alpha);
    case DIFFERENCE_BLEND: return vec4(abs(diff), alpha);
    case RELATIVE_DIFFERENCE_BLEND: return vec4(abs(diff) / (bottom.rgb + vec3(0.01)), alpha);
    }
    return vec4(0.0);
}

vec4 dither(vec4 color)
{
    if (!do_dither)
        return color;

    return color + vec4(vec3(rand_tent(gl_FragCoord.xy + randomness) / 256.0), 0.0);
}

float sample_channel(sampler2D sampler, vec2 uv, bool within_image)
{
    return within_image ? texture(sampler, uv).r : 0.0;
}

void main()
{
    vec4 background = vec4(bg_color.rgb, 1.0);
    if (bg_mode == BG_BLACK)
        background.rgb = vec3(0.0);
    else if (bg_mode == BG_WHITE)
        background.rgb = vec3(1.0);
    else if (bg_mode == BG_DARK_CHECKER || bg_mode == BG_LIGHT_CHECKER)
    {
        float dark_gray  = (bg_mode == BG_DARK_CHECKER) ? 0.1 : 0.5;
        float light_gray = (bg_mode == BG_DARK_CHECKER) ? 0.2 : 0.55;
        float checkerboard =
            mod(floor(gl_FragCoord.x / 8.0) + floor(gl_FragCoord.y / 8.0), 2.0) == 0.0 ? dark_gray : light_gray;
        background.rgb = vec3(checkerboard);
    }
    // inverse tonemap the background color so that it appears correct when we blend and tonemap below
    background = inv_tonemap(background);

    bool in_img = primary_uv.x < 1.0 && primary_uv.y < 1.0 && primary_uv.x > 0.0 && primary_uv.y > 0.0;
    bool in_ref = secondary_uv.x < 1.0 && secondary_uv.y < 1.0 && secondary_uv.x > 0.0 && secondary_uv.y > 0.0;

    if (!in_img && !in_ref)
    {
        frag_color = background;
        return;
    }

    vec4 value = vec4(
        sample_channel(primary_0_texture, primary_uv, in_img), sample_channel(primary_1_texture, primary_uv, in_img),
        sample_channel(primary_2_texture, primary_uv, in_img), sample_channel(primary_3_texture, primary_uv, in_img));

    if (primary_channels_type == YCA_Channels || primary_channels_type == YC_Channels)
        value.xyz = YCToRGB(value.xyz, primary_yw);

    value = primary_M_to_Rec709 * value;

    if (has_reference)
    {
        vec4 reference_val = vec4(sample_channel(secondary_0_texture, secondary_uv, in_ref),
                                  sample_channel(secondary_1_texture, secondary_uv, in_ref),
                                  sample_channel(secondary_2_texture, secondary_uv, in_ref),
                                  sample_channel(secondary_3_texture, secondary_uv, in_ref));

        if (secondary_channels_type == YCA_Channels || secondary_channels_type == YC_Channels)
            reference_val.xyz = YCToRGB(reference_val.xyz, secondary_yw);

        reference_val = secondary_M_to_Rec709 * reference_val;

        value = blend(value, reference_val);
    }

    vec4 foreground = choose_channel(value) * vec4(vec3(gain), 1.0);
    vec4 blended    = dither(tonemap(foreground + background * (1.0 - foreground.a)));
    blended         = clamp(blended, clamp_to_LDR ? 0.0 : -64.0, clamp_to_LDR ? 1.0 : 64.0);
    frag_color      = vec4(blended.rgb, 1.0);
}