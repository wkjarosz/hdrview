precision mediump float;

#ifndef saturate
#define saturate(v) clamp(v, 0.0, 1.0)
#endif

// Luminance-chroma to RGB conversion
// assumes `input: {RY, Y, BY}`
vec3 YC_to_RGB(vec3 c, vec3 Yw)
{
    if (c[0] == 0.0 && c[2] == 0.0)
        //
        // Special case -- both chroma channels are 0.  To avoid
        // rounding errors, we explicitly set the output R, G and B
        // channels equal to the input luminance.
        //
        return vec3(c.g, c.g, c.g);

    float Y = c.g;
    float r = (c.r + 1.0) * c.g;
    float b = (c.b + 1.0) * c.g;
    float g = (Y - r * Yw.x - b * Yw.z) / Yw.y;

    return vec3(r, g, b);
}

float linearToS(float a)
{
    float old_sign = sign(a);
    a              = abs(a);
    return a < 0.0031308 ? old_sign * 12.92 * a : old_sign * 1.055 * pow(a, 1.0 / 2.4) - 0.055;
}

vec3 linearToSRGB(vec3 color) { return vec3(linearToS(color.r), linearToS(color.g), linearToS(color.b)); }
vec4 linearToSRGB(vec4 color) { return vec4(linearToSRGB(color.rgb), color.a); }

float sToLinear(float a)
{
    float old_sign = sign(a);
    a              = abs(a);
    return a < 0.04045 ? old_sign * (1.0 / 12.92) * a : old_sign * pow((a + 0.055) * (1.0 / 1.055), 2.4);
}

vec3 sRGBToLinear(vec3 color) { return vec3(sToLinear(color.r), sToLinear(color.g), sToLinear(color.b)); }
vec4 sRGBToLinear(vec4 color) { return vec4(sRGBToLinear(color.rgb), color.a); }

// returns the luminance of a linear rgb color
float RGBToY(vec3 rgb, vec3 weights) { return dot(weights, rgb); }

// Converts a color from linear RGB to XYZ space
vec3 RGBToXYZ(vec3 rgb)
{
    const mat3 RGB2XYZ = mat3(0.412453, 0.212671, 0.019334, 0.357580, 0.715160, 0.119193, 0.180423, 0.072169, 0.950227);
    return RGB2XYZ * rgb;
}

// Converts a color from XYZ to linear RGB space
vec3 XYZToRGB(vec3 xyz)
{
    const mat3 XYZ2RGB =
        mat3(3.240479, -0.969256, 0.055648, -1.537150, 1.875992, -0.204043, -0.498535, 0.041556, 1.057311);
    return XYZ2RGB * xyz;
}
