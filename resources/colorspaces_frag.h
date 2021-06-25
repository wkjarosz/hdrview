
#define min_Lab     float3(0, -128, -128)
#define max_Lab     float3(100, 128, 128)
#define range_Lab   (max_Lab - min_Lab)
#define Lab_d65_wts float3(.95047, 1.000, 1.08883)

float linearToS(float a) { return a < 0.0031308 ? 12.92 * a : 1.055 * pow(a, 1.0 / 2.4) - 0.055; }

float3 linearToSRGB(float3 color) { return float3(linearToS(color.r), linearToS(color.g), linearToS(color.b)); }

float sToLinear(float a) { return a < 0.04045 ? (1.0 / 12.92) * a : pow((a + 0.055) * (1.0 / 1.055), 2.4); }

float3 sRGBToLinear(float3 color) { return float3(sToLinear(color.r), sToLinear(color.g), sToLinear(color.b)); }

// returns the luminance of a linear rgb color
float3 RGBToLuminance(float3 rgb)
{
    const float3 RGB2Y = float3(0.212671, 0.715160, 0.072169);
    return float3(dot(RGB2Y, rgb));
}

// returns the monochrome version of a linear rgb color
float3 RGBToGray(float3 rgb)
{
    const float3 RGB2Y = float3(1. / 3.);
    return float3(dot(RGB2Y, rgb));
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

float labf(float t)
{
    const float c1 = 0.008856451679; // pow(6.0/29.0, 3.0);
    const float c2 = 7.787037037;    // pow(29.0/6.0, 2.0)/3;
    const float c3 = 0.1379310345;   // 16.0/116.0
    return (t > c1) ? pow(t, 1.0 / 3.0) : (c2 * t) + c3;
}

float3 XYZToLab(float3 xyz)
{
    // normalize for D65 white point
    xyz /= Lab_d65_wts;

    float3 v = float3(labf(xyz.x), labf(xyz.y), labf(xyz.z));
    return float3((116.0 * v.y) - 16.0, 500.0 * (v.x - v.y), 200.0 * (v.y - v.z));
}

float3 LabToXYZ(float3 lab)
{
    const float eps   = 216.0 / 24389.0;
    const float kappa = 24389.0 / 27.0;
    float       yr    = (lab.x > kappa * eps) ? pow((lab.x + 16.0) / 116.0, 3.) : lab.x / kappa;
    float       fy    = (yr > eps) ? (lab.x + 16.0) / 116.0 : (kappa * yr + 16.0) / 116.0;
    float       fx    = lab.y / 500.0 + fy;
    float       fz    = fy - lab.z / 200.0;

    float fx3 = pow(fx, 3.);
    float fz3 = pow(fz, 3.);

    float3 xyz =
        float3((fx3 > eps) ? fx3 : (116.0 * fx - 16.0) / kappa, yr, (fz3 > eps) ? fz3 : (116.0 * fz - 16.0) / kappa);

    // unnormalize for D65 white point
    xyz *= Lab_d65_wts;
    return xyz;
}

float3 RGBToLab(float3 rgb)
{
    float3 lab = XYZToLab(RGBToXYZ(rgb));

    // renormalize
    return (lab - min_Lab) / range_Lab;
}

float3 LabToRGB(float3 lab)
{
    // unnormalize
    lab = lab * range_Lab + min_Lab;

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
    float x = dot(col, float3(1.0) / 3.0);
    float r = saturate(mix(0.0, 1.0, max(x, 0.0)));
    float g = 0.0;
    float b = saturate(mix(0.0, 1.0, -min(x, 0.0)));
    return float3(r, g, b);
}