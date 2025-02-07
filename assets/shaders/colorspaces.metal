#include <metal_stdlib>

using namespace metal;

#define min_Lab     float3(0, -128, -128)
#define max_Lab     float3(100, 128, 128)
#define range_Lab   (max_Lab - min_Lab)
#define Lab_d65_wts float3(.95047, 1.000, 1.08883)


// Luminance-chroma to RGB conversion
// assumes `input: {RY, Y, BY}`
float3 YCToRGB(float3 input, float3 Yw)
{
    if (input[0] == 0.0 && input[2] == 0.0)
        //
        // Special case -- both chroma channels are 0.  To avoid
        // rounding errors, we explicitly set the output R, G and B
        // channels equal to the input luminance.
        //
        return float3(input.g, input.g, input.g);
    
    float Y = input.g;
    float r = (input.r + 1.0) * input.g;
    float b = (input.b + 1.0) * input.g;
    float g = (Y - r * Yw.x - b * Yw.z) / Yw.y;

    return float3(r, g, b);
}

float linearToS(float a)
{
    float old_sign = sign(a);
    a = fabs(a);
    return a < 0.0031308f ? old_sign * 12.92f * a : old_sign * 1.055f * pow(a, 1.0f / 2.4f) - 0.055f;
}

float3 linearToSRGB(float3 color) { return float3(linearToS(color.r), linearToS(color.g), linearToS(color.b)); }

float sToLinear(float a)
{
    float old_sign = sign(a);
    a = fabs(a);
    return a < 0.04045f ? old_sign * (1.0f / 12.92f) * a : old_sign * pow((a + 0.055f) * (1.0f / 1.055f), 2.4f);
}

float3 sRGBToLinear(float3 color) { return float3(sToLinear(color.r), sToLinear(color.g), sToLinear(color.b)); }

// returns the luminance of a linear rgb color
float RGBToY(float3 rgb, float3 weights)
{
    return dot(weights, rgb);
}

// Converts a color from linear RGB to XYZ space
float3 RGBToXYZ(float3 rgb)
{
    const float3x3 RGB2XYZ =
        float3x3(0.412453, 0.212671, 0.019334, 0.357580, 0.715160, 0.119193, 0.180423, 0.072169, 0.950227);
    return RGB2XYZ * rgb;
}

// Converts a color from XYZ to linear RGB space
float3 XYZToRGB(float3 xyz)
{
    const float3x3 XYZ2RGB =
        float3x3(3.240479, -0.969256, 0.055648, -1.537150, 1.875992, -0.204043, -0.498535, 0.041556, 1.057311);
    return XYZ2RGB * xyz;
}

float3 jetFalseColor(float x)
{
    float r = saturate((x < 0.7) ? 4.0 * x - 1.5 : -4.0 * x + 4.5);
    float g = saturate((x < 0.5) ? 4.0 * x - 0.5 : -4.0 * x + 3.5);
    float b = saturate((x < 0.3) ? 4.0 * x + 0.5 : -4.0 * x + 2.5);
    return float3(r, g, b);
}

float3 positiveNegative(const float x)
{
    return float3(-min(x, 0.0), 0.0, max(x, 0.0));
}