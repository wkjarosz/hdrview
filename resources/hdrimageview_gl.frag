#version 330

in vec2 image_uv;
in vec2 reference_uv;
in vec4 gl_FragCoord;

uniform bool has_reference;

uniform sampler2D image;
uniform sampler2D reference;
uniform sampler2D ditherImg;
uniform bool do_dither;
uniform vec2 randomness;

uniform int blend_mode;
uniform int channel;

uniform float gain;
uniform float gamma;
uniform bool sRGB;

out vec4 frag_color;

vec3 tonemap(vec3 color)
{
    return sRGB ? linearToSRGB(color) : pow(color, vec3(1.0/gamma));
}

// note: uniformly distributed, normalized rand, [0;1[
float nrand(vec2 n)
{
    return fract(sin(dot(n.xy, vec2(12.9898, 78.233)))* 43758.5453);
}

float randZeroMeanUniform(vec2 xy)
{
    // Result is in range [-0.5, 0.5]
    return texture(ditherImg, xy/vec2(256,256)).r/65536 - 0.5;
}

float randZeroMeanTriangle(vec2 xy)
{
    float r = randZeroMeanUniform(xy);

    // Convert uniform distribution into triangle-shaped distribution
    // Result is in range [-1.0,1.0]
    float rp = sqrt(2*r);       // positive triangle
    float rn = sqrt(2*r+1)-1;   // negative triangle
    return (r < 0) ? rn : rp;
}

vec3 chooseChannel(vec3 col)
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
        case CHANNEL_CIE_CHROMATICITY:  return LabToRGB(vec3(0.5, RGBToLab(col).yz));
        // case CHANNEL_FALSE_COLOR:       return jetFalseColor(saturate(RGBToLuminance(col).r));
        case CHANNEL_FALSE_COLOR:       return inferno(saturate(RGBToLuminance(col).r));
        case CHANNEL_POSITIVE_NEGATIVE: return positiveNegative(col);
    }
    return col;
}

vec4 blend(vec4 imageVal, vec4 referenceVal)
{
    vec3 diff = imageVal.rgb - referenceVal.rgb;
    float alpha = imageVal.a + referenceVal.a*(1-imageVal.a);
    switch (blend_mode)
    {
        case NORMAL_BLEND:              return vec4(imageVal.rgb*imageVal.a + referenceVal.rgb*referenceVal.a*(1-imageVal.a), alpha);
        case MULTIPLY_BLEND:            return vec4(imageVal.rgb * referenceVal.rgb, alpha);
        case DIVIDE_BLEND:              return vec4(imageVal.rgb / referenceVal.rgb, alpha);
        case ADD_BLEND:                 return vec4(imageVal.rgb + referenceVal.rgb, alpha);
        case AVERAGE_BLEND:             return 0.5*(imageVal + referenceVal);
        case SUBTRACT_BLEND:            return vec4(diff, alpha);
        case DIFFERENCE_BLEND:          return vec4(abs(diff), alpha);
        case RELATIVE_DIFFERENCE_BLEND: return vec4(abs(diff) / (referenceVal.rgb + vec3(0.01)), alpha);
    }
    return vec4(0.0);
}

vec3 dither(vec3 color)
{
    if (!do_dither)
		return color;

    return color + vec3(randZeroMeanTriangle(gl_FragCoord.xy + randomness)/255.0);
}

void main() {
    float darkGray = 0.1;
    float lightGray = 0.2;

    float checkerboard = mod(int(floor(gl_FragCoord.x / 8) + floor(gl_FragCoord.y / 8)), 2) == 0 ? darkGray : lightGray;

    vec4 background = vec4(vec3(checkerboard), 1.0);

    vec4 value = texture(image, image_uv);
    if (image_uv.x > 1.0 || image_uv.y > 1.0 || image_uv.x < 0.0 || image_uv.y < 0.0)
        value = vec4(0.0);
    
    if (has_reference)
    {
        vec4 reference_val = texture(reference, reference_uv);
        if (reference_uv.x > 1.0 || reference_uv.y > 1.0 || reference_uv.x < 0.0 || reference_uv.y < 0.0)
            reference_val = vec4(0.0);

        value = blend(value, reference_val);
    }

	frag_color.a = 1.0;
    frag_color.rgb = mix(background.rgb, dither(tonemap(chooseChannel(gain * value.rgb))), value.a);
}