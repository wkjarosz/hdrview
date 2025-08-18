precision mediump float;

// These need to stay in sync with the various enums in fwd.h
#define CHANNEL_RGB   0
#define CHANNEL_RED   1
#define CHANNEL_GREEN 2
#define CHANNEL_BLUE  3
#define CHANNEL_ALPHA 4
#define CHANNEL_Y     5

#define Tonemap_Gamma            0
#define Tonemap_FalseColor       1
#define Tonemap_PositiveNegative 2

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
#define YA_Channels     6
#define UVorXY_Channels 7
#define Z_Channel       8
#define Single_Channel  9

in vec2 primary_uv;
in vec2 secondary_uv;
// in highp in vec4 gl_FragCoord;

uniform bool  has_reference;
uniform bool  do_dither;
uniform vec2  randomness;
uniform int   blend_mode;
uniform int   channel;
uniform float gain;
uniform float offset;
uniform float gamma;
uniform int   tonemap_mode;
uniform bool  clamp_to_LDR;
uniform int   bg_mode;
uniform vec4  bg_color;

uniform sampler2D colormap;
uniform bool      reverse_colormap;

uniform sampler2D dither_texture;

uniform sampler2D primary_0_texture;
uniform sampler2D primary_1_texture;
uniform sampler2D primary_2_texture;
uniform sampler2D primary_3_texture;
uniform vec3      primary_yw;
uniform int       primary_channels_type;
uniform mat4      primary_M_to_sRGB;

uniform sampler2D secondary_0_texture;
uniform sampler2D secondary_1_texture;
uniform sampler2D secondary_2_texture;
uniform sampler2D secondary_3_texture;
uniform vec3      secondary_yw;
uniform int       secondary_channels_type;
uniform mat4      secondary_M_to_sRGB;

uniform float time;
uniform bool  draw_clip_warnings;
uniform vec2  clip_range;

out vec4 frag_color;

vec4 tonemap(vec4 color)
{
    switch (tonemap_mode)
    {
    default:
    case Tonemap_Gamma: return vec4(sign(color.rgb) * pow(abs(color.rgb), vec3(1.0 / gamma)), color.a);
    case Tonemap_FalseColor:
    {
        float cmap_size = float(textureSize(colormap, 0).x);
        float t         = mix(0.5 / cmap_size, (cmap_size - 0.5) / cmap_size, dot(color.rgb, vec3(1.0 / 3.0)));
        if (reverse_colormap)
            t = 1.f - t;
        return vec4(sRGBToLinear(texture(colormap, vec2(t, 0.5)).rgb) * color.a, color.a);
    }
    case Tonemap_PositiveNegative:
    {
        float cmap_size = float(textureSize(colormap, 0).x);
        float t = mix(0.5 / cmap_size, (cmap_size - 0.5) / cmap_size, 0.5 * dot(color.rgb, vec3(1.0 / 3.0)) + 0.5);
        if (reverse_colormap)
            t = 1.f - t;
        return vec4(sRGBToLinear(texture(colormap, vec2(t, 0.5)).rgb) * color.a, color.a);
    }
    }
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
    case CHANNEL_ALPHA: return rgba;
    case CHANNEL_Y: return vec4(vec3(dot(rgba.rgb, primary_yw)), rgba.a);
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
    vec2 pixel      = vec2(gl_FragCoord.x, -gl_FragCoord.y);
    vec4 background = vec4(bg_color.rgb, 1.0);
    if (bg_mode == BG_BLACK)
        background.rgb = vec3(0.0);
    else if (bg_mode == BG_WHITE)
        background.rgb = vec3(1.0);
    else if (bg_mode == BG_DARK_CHECKER || bg_mode == BG_LIGHT_CHECKER)
    {
        float dark_gray    = (bg_mode == BG_DARK_CHECKER) ? 0.1 : 0.5;
        float light_gray   = (bg_mode == BG_DARK_CHECKER) ? 0.2 : 0.55;
        float checkerboard = mod(floor(pixel.x / 8.0) + floor(pixel.y / 8.0), 2.0) == 0.0 ? dark_gray : light_gray;
        background.rgb     = sRGBToLinear(vec3(checkerboard));
    }

    float zebra1 = (mod(float(int(floor((pixel.x + pixel.y - 30.0 * time) / 8.0))), 2.0) == 0.0) ? 0.0 : 1.0;
    float zebra2 = (mod(float(int(floor((pixel.x - pixel.y - 30.0 * time) / 8.0))), 2.0) == 0.0) ? 0.0 : 1.0;

    bool in_img = primary_uv.x < 1.0 && primary_uv.y < 1.0 && primary_uv.x > 0.0 && primary_uv.y > 0.0;
    bool in_ref = secondary_uv.x < 1.0 && secondary_uv.y < 1.0 && secondary_uv.x > 0.0 && secondary_uv.y > 0.0;

    vec4 value = vec4(
        sample_channel(primary_0_texture, primary_uv, in_img), sample_channel(primary_1_texture, primary_uv, in_img),
        sample_channel(primary_2_texture, primary_uv, in_img), sample_channel(primary_3_texture, primary_uv, in_img));

    if (primary_channels_type == YCA_Channels || primary_channels_type == YC_Channels)
        value.xyz = YC_to_RGB(value.xyz, primary_yw);

    value = primary_M_to_sRGB * value;
    if (channel == CHANNEL_ALPHA)
        value = vec4(value.aaa, 1.0);

    if (has_reference)
    {
        vec4 reference_val = vec4(sample_channel(secondary_0_texture, secondary_uv, in_ref),
                                  sample_channel(secondary_1_texture, secondary_uv, in_ref),
                                  sample_channel(secondary_2_texture, secondary_uv, in_ref),
                                  sample_channel(secondary_3_texture, secondary_uv, in_ref));

        if (secondary_channels_type == YCA_Channels || secondary_channels_type == YC_Channels)
            reference_val.xyz = YC_to_RGB(reference_val.xyz, secondary_yw);

        reference_val = secondary_M_to_sRGB * reference_val;
        if (channel == CHANNEL_ALPHA)
            reference_val = vec4(reference_val.aaa, 1.0);

        value = blend(value, reference_val);
    }

    vec4  foreground = choose_channel(value) * vec4(vec3(gain), 1.0) + vec4(vec3(offset), 0.0);
    vec4  tonemapped = tonemap(foreground) + background * (1.0 - foreground.a);
    bvec3 clipped    = greaterThan(foreground.rgb, clip_range.yyy);
    bvec3 crushed    = lessThan(foreground.rgb, clip_range.xxx);
    vec4  blended    = linearToSRGB(tonemapped);
    vec4  dithered   = dither(blended);
    dithered         = clamp(dithered, clamp_to_LDR ? 0.0 : -64.0, clamp_to_LDR ? 1.0 : 64.0);
    if (draw_clip_warnings)
    {
        dithered.rgb = mix(dithered.rgb, vec3(zebra1), clipped);
        dithered.rgb = mix(dithered.rgb, vec3(zebra2), crushed);
    }
    frag_color = vec4(dithered.rgb, 1.0);
}