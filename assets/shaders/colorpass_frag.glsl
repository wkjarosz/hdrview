precision mediump float;

in vec2 uv;

uniform sampler2D color_texture;
uniform int       transfer;  // wp_color_manager_v1 transfer_function code point (2=gamma22, 5=ext_linear, 11=PQ)
uniform int       primaries; // wp_color_manager_v1 primaries code point (1=sRGB/BT.709, 6=BT.2020)
uniform float     sdr_white_level;
uniform float     min_luminance;
uniform float     max_luminance;

out vec4 frag_color;

// scRGB's fixed convention: 1.0 (linear) == 80 nits.
const float SCRGB_REFERENCE_WHITE_NITS = 80.0;

// wp_color_manager_v1 code points this shader knows how to produce -- the reachable subset across
// Windows/Linux (macOS EDR consumes HDRView's extended-sRGB shader output directly and never reaches this
// shader; see app.cpp). Everything else in the protocol (HLG, BT.1886, ST.240, log100/316, xvYCC, gamma
// 2.8, Display-P3/DCI-P3/Adobe RGB primaries) is unreachable through any GLFW backend HDRView links against.
const int TRANSFER_GAMMA22    = 2;
const int TRANSFER_EXT_LINEAR = 5;
const int TRANSFER_PQ         = 11;
const int PRIMARIES_SRGB      = 1;
const int PRIMARIES_BT2020    = 6;

// Recommendation ITU-R BT.2100-2 PQ inverse EOTF (absolute nits -> PQ code value), ported from
// inverse_EOTF_BT2100_PQ() in src/colorspace.h -- keep the constants in sync with that source of truth.
// PQ has no negative representation, so clip below zero first.
float pq_encode(float nits)
{
    const float m1  = 0.1593017578125;
    const float m2  = 78.84375;
    const float c1  = 0.8359375;
    const float c2  = 18.8515625;
    const float c3  = 18.6875;
    float       Y   = max(nits, 0.0) / 10000.0;
    float       Ym1 = pow(Y, m1);
    return pow((c1 + c2 * Ym1) / (1.0 + c3 * Ym1), m2);
}
vec3 pq_encode(vec3 nits) { return vec3(pq_encode(nits.r), pq_encode(nits.g), pq_encode(nits.b)); }

// wp_color_manager_v1's gamma22 transfer is a literal power curve (not HDRView's own piecewise sRGB OETF),
// sign-extended the same way as HDRView's other curves so out-of-gamut (negative) values survive the pass.
vec3 gamma22_encode(vec3 nits) { return sign(nits) * pow(abs(nits) / SCRGB_REFERENCE_WHITE_NITS, vec3(1.0 / 2.2)); }

// Rec.709 (HDRView's/sRGB's working primaries) -> BT.2020, linear light. Standard ITU-R BT.2087 matrix.
const mat3 REC709_TO_BT2020 =
    mat3(0.627403926658, 0.069097233123, 0.016391587664, 0.329282097415, 0.919541035593, 0.088013255546,
        0.043313797587, 0.011361189924, 0.895595009604);

void main()
{
    vec4 color = texture(color_texture, uv);

    if (transfer == TRANSFER_GAMMA22 && primaries == PRIMARIES_SRGB && sdr_white_level == SCRGB_REFERENCE_WHITE_NITS)
    {
        // Identity: the display wants exactly HDRView's own extended-sRGB convention. Reached via
        // X11/no-color-management; app.cpp already keeps the colorpass inactive there, but this is cheap.
        frag_color = color;
        return;
    }

    // Decode HDRView's extended-sRGB convention (1.0 == sdr_white_level nits) to absolute nits, sign
    // preserved (out-of-gamut colors can be negative).
    vec3 nits = sdr_white_level * sRGBToLinear(color.rgb);

    if (primaries == PRIMARIES_BT2020)
        nits = REC709_TO_BT2020 * nits;

    // Some displays perform unwanted tonemapping on out-of-range values; hard-clip to the display's own
    // reported luminance limits instead, matching nanogui-1's ColorPass. 0.0 means "unknown", not "no
    // limit" -- only clamp when a limit is actually reported.
    if (max_luminance > 0.0)
        nits = clamp(nits, vec3(min_luminance), vec3(max_luminance));

    vec3 encoded;
    if (transfer == TRANSFER_PQ)
        encoded = pq_encode(nits);
    else if (transfer == TRANSFER_GAMMA22)
        // Reached when sdr_white_level != 80 (see identity check above) -- needs the gamma curve re-applied
        // after the reference-white-relative scaling, not a linear pass-through.
        encoded = gamma22_encode(nits);
    else // TRANSFER_EXT_LINEAR, or an unrecognized value -- scRGB-style linear is the safest fallback
        encoded = nits / SCRGB_REFERENCE_WHITE_NITS;

    frag_color = vec4(encoded, color.a);
}
