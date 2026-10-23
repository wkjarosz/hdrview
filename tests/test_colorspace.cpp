//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "common.h" // for blend_mode_names()
#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

TEST_CASE("to_linear/from_linear round-trip for every TransferFunction")
{
    // The values outside [0,1] are the ones an HDR viewer reaches. Gamut conversion produces negatives, and
    // the convention (OpenColorIO's LIN_TO_PQ, colour-science's signed power, our own linear_to_sRGB) is to
    // mirror the transfer function around the origin so the sign survives.
    std::vector<TransferFunction> tfs;
    for (int t = TransferFunction::Unspecified; t < TransferFunction::Count; ++t)
        tfs.emplace_back(static_cast<TransferFunction::Type_>(t));
    for (float gamma : {1.0f, 1.8f, 2.6f}) tfs.emplace_back(TransferFunction::Gamma, gamma);

    for (const TransferFunction &tf : tfs)
    {
        for (float x : {0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 2.f, 10.f, 100.f, 1000.f, -0.5f, -10.f})
        {
            INFO("transfer function = ", transfer_function_name(tf), " (gamma ", tf.gamma, "), x = ", x);
            const float encoded = from_linear(x, tf);
            REQUIRE(std::isfinite(encoded));

            // Log100 and its sqrt10 variant are specified on a bounded domain and clamp, losing the sign
            if (x < 0.f && (tf.type == TransferFunction::Log100 || tf.type == TransferFunction::Log100_Sqrt10))
                continue;
            if (x < 0.f)
                CHECK(encoded <= 0.f);

            auto expected = doctest::Approx(x);
            if (std::abs(x) > 1.f)
                expected.epsilon(1e-3);
            CHECK(to_linear(encoded, tf) == expected);
        }

        // and as a color, whose channels differ: HLG encodes one as a whole, its OOTF depending on the
        // luminance, and every other transfer function channel by channel
        INFO("transfer function = ", transfer_function_name(tf), " (gamma ", tf.gamma, ")");
        const float3 rgb{0.2f, 0.7f, 0.05f};
        const float3 encoded = from_linear(rgb, tf);
        CHECK(approx_equal(to_linear(encoded, tf), rgb, 1e-4f));
        if (tf.type != TransferFunction::BT2100_HLG)
            CHECK(encoded == float3{from_linear(rgb.x, tf), from_linear(rgb.y, tf), from_linear(rgb.z, tf)});
        else
            CHECK_FALSE(approx_equal(
                encoded, float3{from_linear(rgb.x, tf), from_linear(rgb.y, tf), from_linear(rgb.z, tf)}, 1e-3f));
    }
}

// Narrow (studio) range packs luma into 16..235, a span of 219.
TEST_CASE("narrow-range dequantization maps the studio range to [0,1]")
{
    CHECK(dequantize_narrow<uint8_t>(16) == doctest::Approx(0.f));
    CHECK(dequantize_narrow<uint8_t>(235) == doctest::Approx(1.f));
    CHECK(dequantize_narrow<uint16_t>(16 * 256) == doctest::Approx(0.f));
    CHECK(dequantize_narrow<uint16_t>(235 * 256) == doctest::Approx(1.f));
}

TEST_CASE(
    "quantize_full/dequantize_full round-trip within quantization error, and non-finite input stays representable")
{
    for (float x : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f})
    {
        INFO("x = ", x);
        CHECK(dequantize_full(quantize_full<uint8_t>(x, 0, 0, false)) == doctest::Approx(x).epsilon(0.004));
        CHECK(dequantize_full(quantize_full<uint16_t>(x, 0, 0, false)) == doctest::Approx(x).epsilon(1e-4));
    }

    // Casting a NaN to an integer type is undefined behavior, and std::clamp doesn't filter it out: both of its
    // comparisons are false for NaN, so it returns the NaN unchanged.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(quantize_full<uint8_t>(nan, 0, 0, false) == 0);
    CHECK(quantize_full<uint16_t>(nan, 0, 0, false) == 0);
    CHECK(quantize_full<uint8_t>(inf, 0, 0, false) == 255);
    CHECK(quantize_full<uint8_t>(-inf, 0, 0, false) == 0);
}

TEST_CASE("color_u32_to_f128/color_f128_to_u32 round-trip exactly")
{
    for (uint32_t packed : {0x00000000u, 0xFFFFFFFFu, 0xFF0000FFu, 0x00FF00FFu, 0x0000FFFFu, 0x7F3C9AFFu})
    {
        INFO("packed = ", packed);
        CHECK(color_f128_to_u32(color_u32_to_f128(packed)) == packed);
    }
}

TEST_CASE("a gamut converts to itself as the identity, and the shader's sRGB luminance weights are the derived ones")
{
    // converting a gamut to itself is reported as unnecessary, so the colorpass keeps an identity matrix in the
    // common sRGB case
    for (int g = ColorGamut_FirstNamed; g <= ColorGamut_LastNamed; ++g)
    {
        INFO("gamut = ", color_gamut_name(ColorGamut_(g)));
        const Chromaticities chr = gamut_chromaticities(ColorGamut_(g));
        float3x3             M{float3{2.f}, float3{3.f}, float3{4.f}};
        CHECK_FALSE(color_conversion_matrix(M, chr, chr));
        CHECK(M == float3x3{la::identity});
    }

    // assets/shaders/image-shader.sglsl converts every image into sRGB/BT.709 primaries before choose_channel()
    // runs, so Channels_Y weighs with shader::sRGB_Yw, the middle row of the classic rounded sRGB matrix.
    // colorspace.h derives sRGB_Yw() from the BT.709 chromaticities instead; the two agree to about 3e-5 per weight.
    const float3 shader_Yw = shader::sRGB_Yw;
    INFO("colorspace.h says ", sRGB_Yw().x, ", ", sRGB_Yw().y, ", ", sRGB_Yw().z);
    CHECK(approx_equal(sRGB_Yw(), shader_Yw, 1e-4f));
}

TEST_CASE("every transfer function is odd where it mirrors, and its vector form is its scalar one per component")
{
    // The properties colour-science's values cannot show (test_colour_reference.cpp checks those): which transfer
    // functions HDRView mirrors around the origin, that a NaN survives them, and that the vec3 overloads the shaders
    // call apply the scalar function to each component in order. The round trip is checked above.
    struct TransferCase
    {
        const char *name;
        float (*f)(float);
        float3 (*v)(float3);
        bool odd; ///< mirrored around the origin, rather than extended linearly or clamped
    };
    const TransferCase cases[] = {
        {"linear_to_sRGB", shader::linear_to_sRGB, shader::linear_to_sRGB, true},
        {"sRGB_to_linear", shader::sRGB_to_linear, shader::sRGB_to_linear, true},
        {"OETF_ITU", shader::OETF_ITU, shader::OETF_ITU, false},
        {"inverse_OETF_ITU", shader::inverse_OETF_ITU, shader::inverse_OETF_ITU, false},
        {"EOTF_BT1886", shader::EOTF_BT1886, shader::EOTF_BT1886, true},
        {"inverse_EOTF_BT1886", shader::inverse_EOTF_BT1886, shader::inverse_EOTF_BT1886, true},
        {"OETF_IEC61966_2_4", shader::OETF_IEC61966_2_4, shader::OETF_IEC61966_2_4, true},
        {"EOTF_IEC61966_2_4", shader::EOTF_IEC61966_2_4, shader::EOTF_IEC61966_2_4, true},
        {"EOTF_BT2100_PQ", shader::EOTF_BT2100_PQ, shader::EOTF_BT2100_PQ, true},
        {"inverse_EOTF_BT2100_PQ", shader::inverse_EOTF_BT2100_PQ, shader::inverse_EOTF_BT2100_PQ, true},
        {"OETF_BT2100_HLG", shader::OETF_BT2100_HLG, shader::OETF_BT2100_HLG, true},
        {"OETF_ST240", shader::OETF_ST240, shader::OETF_ST240, false},
        {"EOTF_ST240", shader::EOTF_ST240, shader::EOTF_ST240, false},
        {"OETF_log100", shader::OETF_log100, shader::OETF_log100, false},
        {"EOTF_log100", shader::EOTF_log100, shader::EOTF_log100, false},
        {"OETF_log100_sqrt10", shader::OETF_log100_sqrt10, shader::OETF_log100_sqrt10, false},
        {"EOTF_log100_sqrt10", shader::EOTF_log100_sqrt10, shader::EOTF_log100_sqrt10, false},
        {"EOTF_DCI_P3", shader::EOTF_DCI_P3, shader::EOTF_DCI_P3, true},
        {"inverse_EOTF_DCI_P3", shader::inverse_EOTF_DCI_P3, shader::inverse_EOTF_DCI_P3, true},
    };

    // straddling each transfer function's breakpoint
    const float values[] = {1e-6f, 0.0031308f, 0.004f, 0.018f, 0.019f, 0.0228f, 0.04045f, 0.0913f, 0.18f, 0.5f, 1.f};
    for (const TransferCase &c : cases)
    {
        INFO("transfer function = ", std::string(c.name));
        for (float x : values)
        {
            INFO("x = ", x);
            if (c.odd)
                CHECK(c.f(-x) == -c.f(x));
            // the vector overload applies the transfer function per component, in order; bitwise, since 2x leaves
            // PQ's [0,1] domain, where both give NaN
            const float3 got = c.v(float3{x, 0.5f * x, 2.f * x}), want{c.f(x), c.f(0.5f * x), c.f(2.f * x)};
            CHECK(std::memcmp(&got, &want, sizeof(got)) == 0);
        }
        // a NaN pixel stays NaN on the CPU, rather than turning into a color
        if (c.odd)
            CHECK(std::isnan(c.f(std::numeric_limits<float>::quiet_NaN())));
    }

    // the sRGB overloads leave alpha alone
    const float4 c{-0.25f, 0.01f, 0.5f, 0.3f};
    CHECK(shader::linear_to_sRGB(c) == float4{shader::linear_to_sRGB(c.xyz()), c.w});
    CHECK(shader::sRGB_to_linear(c) == float4{shader::sRGB_to_linear(c.xyz()), c.w});

    // luminance-chroma undoes RGB_to_YC, for weights that are not symmetric in R and B
    for (const float3 &Yw : {sRGB_Yw(), computeYw(gamut_chromaticities(ColorGamut_BT2020_2100))})
        for (const float3 &rgb : {float3{0.2f, 0.7f, 0.4f}, float3{1.f, 0.f, 0.f}, float3{0.f, 0.f, 1.f}, float3{0.5f}})
        {
            INFO("rgb = ", rgb, ", Yw = ", Yw);
            CHECK(approx_equal(shader::YC_to_RGB(RGB_to_YC(rgb, Yw), Yw), rgb, 1e-5f));
        }
}

TEST_CASE("the image shader's per-pixel steps do what the viewer's controls say")
{
    // src/shared/pipeline.h, which the image shader runs and HDRViewApp reproduces the display with. Colors
    // are premultiplied, and every component differs, so a step that mixes up channels is caught.
    const float4 c{0.12f, 0.34f, 0.56f, 0.5f};
    const float3 straight = c.xyz() / c.w;

    for (int ch = 0; ch < Channels_COUNT; ++ch)
        for (bool straight_alpha : {false, true})
        {
            INFO("channel = ", ch, ", straight_alpha = ", straight_alpha);
            const float4 got = shader::choose_channel(c, Channels_(ch), straight_alpha);
            // isolating channels shows them opaque, undoing the premultiply only of a file that stored
            // straight alpha
            const float3 shown = straight_alpha ? straight : c.xyz();
            switch (Channels_(ch))
            {
            case Channels_RGBA: CHECK(got == c); break;
            case Channels_RGB: CHECK(approx_equal(got, float4{shown, 1.f}, 1e-6f)); break;
            case Channels_Red: CHECK(approx_equal(got, float4{float3{shown.x}, 1.f}, 1e-6f)); break;
            case Channels_Green: CHECK(approx_equal(got, float4{float3{shown.y}, 1.f}, 1e-6f)); break;
            case Channels_Blue: CHECK(approx_equal(got, float4{float3{shown.z}, 1.f}, 1e-6f)); break;
            case Channels_Alpha: CHECK(got == float4{c.w, c.w, c.w, 1.f}); break;
            // luminance keeps the image's alpha, and weighs as colorspace.h's derived sRGB weights do
            case Channels_Y: CHECK(approx_equal(got, float4{float3{dot(c.xyz(), sRGB_Yw())}, c.w}, 1e-4f)); break;
            default: FAIL("no expectation for channel mode ", ch);
            }
        }

    // a straight-alpha pixel with no alpha left still isolates to a finite color
    const float4 no_alpha = shader::choose_channel(float4{0.1f, 0.2f, 0.3f, 0.f}, Channels_RGB, true);
    CHECK((std::isfinite(no_alpha.x) && std::isfinite(no_alpha.y) && std::isfinite(no_alpha.z)));

    // exposure scales the color and not its coverage; the offset applies to the straight color
    const float4 exposed = shader::expose(c, 4.f, 0.1f);
    CHECK(exposed.w == c.w);
    CHECK(approx_equal(exposed.xyz() / exposed.w, straight * 4.f + float3{0.1f}, 1e-5f));

    // the colormap coordinate is the channel average, mapped so the positive/negative map centers zero, and
    // reversing it runs the colormap the other way
    CHECK(shader::colormap_coordinate(float3{0.f, 0.3f, 0.9f}, Tonemap_FalseColor, false) == doctest::Approx(0.4f));
    for (float v : {-1.f, 0.f, 0.25f, 1.f})
    {
        INFO("v = ", v);
        CHECK(shader::colormap_coordinate(float3{v}, Tonemap_FalseColor, false) == doctest::Approx(v));
        CHECK(shader::colormap_coordinate(float3{v}, Tonemap_PositiveNegative, false) ==
              doctest::Approx(0.5f * v + 0.5f));
        for (Tonemap_ mode : {Tonemap_FalseColor, Tonemap_PositiveNegative})
            CHECK(shader::colormap_coordinate(float3{v}, mode, true) ==
                  doctest::Approx(1.f - shader::colormap_coordinate(float3{v}, mode, false)));
    }

    // every tonemap mode: gamma encodes the color itself, and the colormap modes show the colormap's color,
    // premultiplied, whatever the input
    const float3 colormap_sRGB{0.9f, 0.4f, 0.1f};
    for (int m = 0; m < Tonemap_COUNT; ++m)
    {
        INFO("tonemap mode = ", m);
        const float4 got = shader::tonemap(c, Tonemap_(m), 2.2f, colormap_sRGB);
        CHECK(got.w == c.w);
        if (Tonemap_(m) == Tonemap_Gamma)
        {
            for (int i = 0; i < 3; ++i)
                CHECK(got[i] == doctest::Approx(std::pow(double(c[i]), 1.0 / 2.2)).epsilon(1e-5));
            CHECK(shader::tonemap(c, Tonemap_(m), 2.2f, float3{0.f}) == got);
        }
        else
        {
            CHECK(approx_equal(got.xyz(), sRGB_to_linear(colormap_sRGB) * c.w, 1e-6f));
            CHECK(shader::tonemap(float4{5.f, -1.f, 0.f, c.w}, Tonemap_(m), 2.2f, colormap_sRGB) == got);
        }
    }

    // The highlight rolloff's curve, across ceilings and peaks: below the knee it changes nothing, above it it
    // climbs monotonically toward the ceiling, leaving the knee at slope 1, and it never passes the ceiling; a known
    // peak above the ceiling lands exactly on it, and one at or below it, or none at all, needs no rolloff, only
    // holding anything brighter at the ceiling, so the curve does not jump as the peak crosses the ceiling.
    const float peak_offsets[] = {-1.f, 0.5f, 3.f, 12.f}; //< from the ceiling
    for (float ceiling : {0.f, 1.f, 2.5f})
        for (int p = -1; p < 4; ++p)
        {
            // without a peak, a value one would roll off is passed anyway, which the curve has to ignore
            const bool  has_peak = p >= 0;
            const float peak     = ceiling + (has_peak ? peak_offsets[p] : 3.f);
            INFO("ceiling = ", ceiling, ", peak = ", peak, ", has peak = ", has_peak);
            auto f = [&](float x) { return shader::rolloff_stops(x, ceiling, has_peak, peak); };

            const bool  identity = !has_peak || peak <= ceiling;
            const float knee     = shader::rolloff_knee(ceiling, has_peak, peak);
            // BT.2390's knee placement, half the gap below the ceiling
            if (!identity)
                CHECK(knee == doctest::Approx(ceiling - 0.5f * (peak - ceiling)));
            float prev = -INFINITY;
            for (float x = ceiling - 20.f; x <= ceiling + 20.f; x += 1.f / 64)
            {
                const float y = f(x);
                if (identity || x <= knee)
                    REQUIRE(y == std::min(x, ceiling));
                REQUIRE(y <= ceiling);
                REQUIRE(y >= prev - 1e-5f); //< float rounding where the Hermite flattens into the ceiling
                prev = y;
            }
            if (!identity)
            {
                // just above and just below the ceiling, a peak gives nearly the same curve
                for (float x : {ceiling - 1.f, ceiling + 0.5f, ceiling + 4.f})
                    CHECK(shader::rolloff_stops(x, ceiling, true, ceiling + 1e-3f) ==
                          doctest::Approx(shader::rolloff_stops(x, ceiling, true, ceiling - 1e-3f)).epsilon(1e-2));
                CHECK(f(peak) == doctest::Approx(ceiling).epsilon(1e-6));
                CHECK(f(peak + 5.f) == ceiling);
                const float h = 1e-3f;
                CHECK((f(knee + h) - f(knee)) / h == doctest::Approx(1.f).epsilon(0.01));
            }
        }

    // and of a color: at preserve_hue 1, on the brightest channel, which goes through the curve while the others
    // keep their ratios to it, so hues hold; at 0, on each channel, which goes through the curve on its own; and
    // between, the mix of the two. Always, no channel ends above the ceiling, coverage is untouched, the curve sees
    // the straight color, only the gamma tonemap is rolled off, and a gray, all of whose channels are the
    // brightest, comes out the same.
    const float ceiling = 1.f;
    const float peak    = 3.f; //< a knee at SDR white, with 6.0 inside the Hermite
    auto        curve   = [&](float v) { return std::exp2(shader::rolloff_stops(std::log2(v), ceiling, true, peak)); };
    for (float preserve_hue : {0.f, 0.5f, 1.f})
        for (float alpha : {1.f, 0.5f})
            for (int rotation = 0; rotation < 3; ++rotation)
            {
                INFO("preserve hue = ", preserve_hue, ", alpha = ", alpha, ", rotation = ", rotation);
                auto roll = [&](float4 c, float hue, Tonemap_ mode = Tonemap_Gamma)
                { return shader::roll_off(c, mode, hue, ceiling, true, peak); };

                // the brightest channel in each position in turn
                const float  v[3] = {6.f, 3.f, 1.5f};
                const float3 straight_hot{v[rotation], v[(rotation + 1) % 3], v[(rotation + 2) % 3]};
                const float4 hot{straight_hot * alpha, alpha};
                const float4 rolled = roll(hot, preserve_hue);
                CHECK(rolled.w == alpha);
                CHECK(la::maxelem(rolled.xyz()) / alpha == doctest::Approx(curve(6.f)));
                CHECK(la::maxelem(rolled.xyz()) / alpha <= std::exp2(ceiling));
                for (int i = 0; i < 3; ++i)
                {
                    const float each     = curve(straight_hot[i]);
                    const float together = straight_hot[i] * curve(6.f) / 6.f;
                    CHECK(rolled[i] / alpha == doctest::Approx(each + (together - each) * preserve_hue));
                }
                if (preserve_hue == 1.f)
                    for (int i = 0; i < 3; ++i)
                        CHECK(rolled[i] / rolled[(i + 1) % 3] == doctest::Approx(hot[i] / hot[(i + 1) % 3]));

                const float4 gray{float3{6.f * alpha}, alpha};
                CHECK(approx_equal(roll(gray, preserve_hue), roll(gray, 1.f - preserve_hue), 1e-6f));
                for (Tonemap_ mode : {Tonemap_FalseColor, Tonemap_PositiveNegative})
                    CHECK(roll(hot, preserve_hue, mode) == hot);
                // nothing at or below the knee, and nothing without light to judge, changes
                CHECK(roll(float4{0.5f * alpha, 0.f, 0.f, alpha}, preserve_hue) ==
                      float4{0.5f * alpha, 0.f, 0.f, alpha});
                CHECK(roll(float4{-1.f, 0.f, 0.f, alpha}, preserve_hue) == float4{-1.f, 0.f, 0.f, alpha});
            }
}

TEST_CASE("blend()'s overloads agree on every blend mode")
{
    // the three overloads are separate switches over the same enum, so a mode present in one and missing
    // from another blends as whatever its `default:` is. The RGBA one is the image shader's own
    // (src/shared/pipeline.h); its channels differ, so one that reads another's value is caught too.
    const float4 top{0.6f, 0.15f, 1.3f, 0.75f}, bottom{0.25f, 0.8f, 0.05f, 0.5f};

    for (int m = 0; m < BlendMode_COUNT; ++m)
    {
        auto mode = (BlendMode_)m;
        CAPTURE(blend_mode_names()[m]);

        float4 v4 = blend(top, bottom, mode);
        for (int c = 0; c < 3; ++c)
        {
            CAPTURE(c);
            float  s  = blend(top[c], bottom[c], mode);
            float2 v2 = blend(float2{top[c], top.w}, float2{bottom[c], bottom.w}, mode);

            // the two vector overloads carry the same alpha and must agree on both color and alpha
            CHECK(v2.x == doctest::Approx(v4[c]));
            CHECK(v2.y == doctest::Approx(v4.w));

            // the scalar overload has no alpha to composite with, so Normal keeps the top sample, as the
            // statistics pass wants; every other mode is per-channel arithmetic and must match
            if (mode != BlendMode_Normal)
                CHECK(v4[c] == doctest::Approx(s));

            // colorspace.h is a header, where only a qualified std::abs is sure to reach the floating-point
            // overloads: libstdc++ leaves only <stdlib.h>'s integer ::abs at global scope, which would truncate
            // these differences, of either sign and two of them below 1
            const float diff = std::fabs(top[c] - bottom[c]);
            if (mode == BlendMode_Difference)
                CHECK(s == doctest::Approx(diff));
            else if (mode == BlendMode_Relative_Difference)
                CHECK(s == doctest::Approx(diff / (bottom[c] + 0.01f)));
        }
    }
}

namespace
{

/// Colors spanning what the HSL conversions and the hue edits have to handle, including values outside [0,1].
const float3 k_hsl_colors[] = {
    {0.5f, 0.25f, 0.125f}, {1.f, 0.f, 0.f},    {0.f, 1.f, 0.f},       {0.f, 0.f, 1.f}, {0.2f, 0.7f, 0.9f},
    {0.f, 0.f, 0.f},       {1.f, 1.f, 1.f},    {0.35f, 0.35f, 0.35f}, // achromatic, where hue is undefined
    {4.f, 1.5f, 0.25f},    {-0.2f, 0.4f, 1.f},                        // beyond white and below black
};

/// Whether \p a is \p b to within \p rel of its length, or of one, whichever is larger.
template <typename V>
bool close(V a, V b, float rel = 1e-5f)
{ return la::length(a - b) <= rel * std::max(1.f, la::length(b)); }

} // namespace

TEST_CASE("a color survives the trip through HSL and back, whose lightness is the midpoint of the extremes")
{
    // the textbook version measures saturation as a fraction of the unit range, which has no meaning for a
    // value brighter than white
    for (const float3 &rgb : k_hsl_colors)
    {
        CAPTURE(rgb.x);
        CAPTURE(rgb.y);
        CAPTURE(rgb.z);

        const float3 hsl = RGB_to_HSL(rgb);
        const float3 out = HSL_to_RGB(hsl);
        CHECK(out.x == doctest::Approx(rgb.x).epsilon(1e-4));
        CHECK(out.y == doctest::Approx(rgb.y).epsilon(1e-4));
        CHECK(out.z == doctest::Approx(rgb.z).epsilon(1e-4));

        const float mn = std::min({rgb.x, rgb.y, rgb.z}), mx = std::max({rgb.x, rgb.y, rgb.z});
        CHECK(hsl.z == doctest::Approx(0.5f * (mn + mx)));
        CHECK(hsl.x >= 0.f);
        CHECK(hsl.x <= 1.f);

        // gray has no hue to rotate, which a rotation has to leave alone
        if (mx - mn < 1e-6f)
            CHECK(hsl.y == doctest::Approx(0.f));
        else
            CHECK(hsl.y > 0.f);
    }
}

TEST_CASE("turning the hue translates the hue wheel, and keeps a color's range at any brightness")
{
    auto wheel = [](float t) { return HSL_to_RGB(float3{t - std::floor(t), 1.f, 0.5f}); };

    // the wheel slides along by the turn, whichever way and however far
    for (float turns : {0.05f, 1.f / 6.f, 1.f / 3.f, 0.5f, -0.4f, 1.3f})
        for (int i = 0; i < 48; ++i)
        {
            const float t = float(i) / 48.f;
            CAPTURE(turns);
            CAPTURE(t);
            CHECK(close(rotate_hue(wheel(t), turns), wheel(t + turns)));
        }

    // colors in, above and below the unit range, and grays
    for (const float3 c : k_hsl_colors)
        for (float turns : {0.1f, 1.f / 3.f, -0.4f})
        {
            CAPTURE(turns);
            const float3 r = rotate_hue(c, turns);
            // only the middle component moves, so the range is kept
            CHECK(la::minelem(r) == doctest::Approx(la::minelem(c)));
            CHECK(la::maxelem(r) == doctest::Approx(la::maxelem(c)));
            // a positive scale passes through, as premultiplied alpha needs
            for (float k : {0.5f, 3.f}) CHECK(close(rotate_hue(k * c, turns), k * r));
            // turns compose, and a whole one comes back
            CHECK(close(rotate_hue(r, 0.25f), rotate_hue(c, turns + 0.25f)));
            CHECK(close(rotate_hue(c, turns + 1.f), r));
        }

    // no turn is exact, and a gray has no hue to turn
    for (const float3 c : k_hsl_colors) CHECK(rotate_hue(c, 0.f) == c);
    for (float k : {0.f, 0.3f, 5.f}) CHECK(rotate_hue(float3{k}, 0.3f) == float3{k});
}

TEST_CASE("the saturation blends toward the gray of the same luminance, at any brightness")
{
    const float3 weights[] = {sRGB_Yw(), computeYw(gamut_chromaticities(ColorGamut_BT2020_2100)), float3{1.f / 3.f}};

    for (const float3 Yw : weights)
        for (float saturation : {0.f, 0.5f, 1.f, 2.f})
        {
            CAPTURE(saturation);
            const float3x3 M = saturation_matrix(saturation, Yw);
            // each color moves toward or away from the gray of its own luminance, by the factor asked for, which
            // leaves grays and luminance where they were
            for (const float3 c : k_hsl_colors)
            {
                CHECK(close(mul(M, c), float3{dot(Yw, c)} + saturation * (c - float3{dot(Yw, c)}), 1e-4f));
                // asked for nothing, it is exact, so an edit that does nothing changes no sample
                if (saturation == 1.f)
                    CHECK(mul(M, c) == c);
            }
        }
}

TEST_CASE("the lightness mixes toward black, or toward white or a highlight's own level")
{
    auto lighten = [](float4 c, float l, bool premultiplied)
    {
        return shader::edit_color(shader::EDIT_HUE_SATURATION, c, float4{0.f, 0.f, l, 0.f},
                                  float4{premultiplied ? 1.f : 0.f, 0.f, 0.f, 0.f}, float3x3{la::identity},
                                  float3x3{la::identity});
    };
    const float4 sdr{0.5f, 0.25f, 0.125f, 1.f}, hdr{4.f, 2.f, 1.f, 1.f};
    CHECK(close(lighten(sdr, -1.f, false), float4{0.f, 0.f, 0.f, 1.f}));
    CHECK(close(lighten(hdr, -0.5f, false), float4{2.f, 1.f, 0.5f, 1.f}));
    CHECK(close(lighten(sdr, 1.f, false), float4{1.f, 1.f, 1.f, 1.f}));
    CHECK(close(lighten(sdr, 0.5f, false), float4{0.75f, 0.625f, 0.5625f, 1.f}));
    // a highlight goes toward its own brightest component, so it loses color without dimming
    CHECK(close(lighten(hdr, 1.f, false), float4{4.f, 4.f, 4.f, 1.f}));
    CHECK(close(lighten(hdr, 0.5f, false), float4{4.f, 3.f, 2.5f, 1.f}));
    // premultiplied, the level is the straight color's, scaled back by alpha
    const float4 half{0.25f, 0.125f, 0.0625f, 0.5f};
    CHECK(close(lighten(half, 1.f, true), float4{0.5f, 0.5f, 0.5f, 0.5f}));
    CHECK(close(lighten(float4{2.f, 1.f, 0.5f, 0.5f}, 1.f, true), float4{2.f, 2.f, 2.f, 0.5f}));
}

TEST_CASE("Schlick's rational gain keeps 0, 1/2 and 1, with finite slopes at its ends")
{
    // its midpoint slope is (1 - a) / a and its ends' the reciprocal, which is what lets the rational curve go on
    // past black and white
    const float d = 1e-4f; //< small beside the curvature of a steep gain
    for (float a : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f})
    {
        CAPTURE(a);
        CHECK(gain_Schlick(0.f, a) == doctest::Approx(0.f));
        CHECK(gain_Schlick(0.5f, a) == doctest::Approx(0.5f));
        CHECK(gain_Schlick(1.f, a) == doctest::Approx(1.f));
        CHECK((gain_Schlick(0.5f + d, a) - gain_Schlick(0.5f - d, a)) / (2.f * d) ==
              doctest::Approx((1.f - a) / a).epsilon(0.01));
        CHECK(gain_Schlick(d, a) / d == doctest::Approx(a / (1.f - a)).epsilon(0.01));
        CHECK((1.f - gain_Schlick(1.f - d, a)) / d == doctest::Approx(a / (1.f - a)).epsilon(0.01));
    }
    // and at a half, it is the identity
    for (int i = 0; i <= 20; ++i) CHECK(gain_Schlick(float(i) / 20.f, 0.5f) == doctest::Approx(float(i) / 20.f));
}

TEST_CASE("Schlick's bias has the properties it is chosen for")
{
    // a shape function, checked on the identities that make it usable, not on its formula
    for (float a : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f})
    {
        CAPTURE(a);

        // bias is defined by where it sends the midpoint, which is the parameter itself
        CHECK(bias_Schlick(0.f, a) == doctest::Approx(0.f));
        CHECK(bias_Schlick(0.5f, a) == doctest::Approx(a));
        CHECK(bias_Schlick(1.f, a) == doctest::Approx(1.f));

        // monotone, so it reorders nothing
        float previous = -1.f;
        for (int i = 0; i <= 20; ++i)
        {
            const float t = float(i) / 20.f;
            CHECK(bias_Schlick(t, a) > previous);
            previous = bias_Schlick(t, a);
        }
    }
}

TEST_CASE("Every brightness/contrast curve leaves a neutral setting alone and steepens with the contrast")
{
    // What the sliders mean: contrast is an angle, so its ends are a flat line and a vertical one, and
    // brightness slides the point the curve pivots about.
    auto slope_of    = [](float c) { return float(std::tan(lerp(0.0, M_PI_2, c / 2.0 + 0.5))); };
    auto midpoint_of = [](float b) { return (1.f - b) / 2.f; };
    auto bias_of     = [](float b) { return (b + 1.f) / 2.f; };

    // no curve moves anything when both controls are centered
    for (int i = 0; i <= 20; ++i)
    {
        const float v = float(i) / 20.f;
        CAPTURE(v);
        CHECK(brightness_contrast_linear(v, slope_of(0.f), midpoint_of(0.f)) == doctest::Approx(v));
        CHECK(brightness_contrast_rational(v, slope_of(0.f), bias_of(0.f)) == doctest::Approx(v));
    }

    // raising contrast pushes the ends apart about the midpoint
    for (float c : {0.3f, 0.6f})
    {
        CAPTURE(c);
        const float slope = slope_of(c);

        CHECK(brightness_contrast_linear(0.75f, slope, 0.5f) > 0.75f);
        CHECK(brightness_contrast_linear(0.25f, slope, 0.5f) < 0.25f);
        CHECK(brightness_contrast_rational(0.75f, slope, 0.5f) > 0.75f);
        CHECK(brightness_contrast_rational(0.25f, slope, 0.5f) < 0.25f);
    }

    // The rational curve rises everywhere, its slope is continuous through 0 and 1, and beyond them it carries on as
    // straight lines, so values past white stay apart. The samples reach -2 and 4, where the bare hyperbolas would
    // flatten or blow up.
    for (float c : {-0.8f, -0.3f, 0.f, 0.4f, 0.8f})
        for (float bias : {0.3f, 0.5f, 0.7f})
        {
            CAPTURE(c);
            CAPTURE(bias);
            const float d        = 1e-4f; //< small beside the curvature of a steep curve
            auto        f        = [&](float v) { return brightness_contrast_rational(v, slope_of(c), bias); };
            auto        slope_at = [&](float v, float side) { return side * (f(v + side * d) - f(v)) / d; };
            for (float at : {0.f, 1.f}) CHECK(slope_at(at, -1.f) == doctest::Approx(slope_at(at, 1.f)).epsilon(0.02));
            float previous = f(-2.f);
            for (int i = 1; i <= 80; ++i)
            {
                const float v = -2.f + 6.f * float(i) / 80.f, now = f(v);
                CHECK(std::isfinite(now));
                CHECK(now >= previous);
                previous = now;
            }
            CHECK(f(4.f) - f(3.f) == doctest::Approx(f(3.f) - f(2.f)).epsilon(1e-4));
            CHECK(f(4.f) - f(3.f) > 0.f);
        }

    // at any brightness, the two curves cross the middle at the same input with the same slope, which is the
    // contrast's
    for (float B : {-0.6f, -0.2f, 0.f, 0.3f, 0.7f})
        for (float c : {-0.5f, 0.f, 0.4f})
        {
            CAPTURE(B);
            CAPTURE(c);
            const float slope = slope_of(c), m = midpoint_of(B), bias = bias_of(B), d = 1e-4f;
            auto        f = [&](float v) { return brightness_contrast_rational(v, slope, bias); };
            CHECK(f(m) == doctest::Approx(0.5f).epsilon(1e-4));
            CHECK((f(m + d) - f(m - d)) / (2.f * d) == doctest::Approx(slope).epsilon(1e-3));
        }

    // the straight line runs out of [0,1], so an HDR sample keeps its relation to its neighbors
    const float steep = slope_of(0.8f);
    CHECK(brightness_contrast_linear(1.f, steep, 0.5f) > 1.f);
    CHECK(brightness_contrast_linear(0.f, steep, 0.5f) < 0.f);
}

TEST_CASE("brightness/contrast curves the RGB channels, L* alone, or a* and b* alone, as its mode says")
{
    // The command curves the three channels alike, or works in L*a*b* through the image's own primaries and white
    // (BrightnessContrast::point_edit() in src/edit/commands/color.cpp), where one mode curves L* and leaves a* and
    // b*, and the other does the reverse. Each shape's curve applies to the channels, or to normalized L*a*b*, and
    // clipped, that curve is held to [0, 1].
    struct Setting
    { float slope, midpoint, bias; };
    // a brighter, steeper curve and a darker, flatter one
    const Setting settings[] = {{1.376f, 0.3f, 0.7f}, {0.51f, 0.65f, 0.35f}};
    const int     shapes[]   = {shader::SHAPE_RATIONAL, shader::SHAPE_LINE, shader::SHAPE_RATIONAL + shader::SHAPE_CLIP,
                                shader::SHAPE_LINE + shader::SHAPE_CLIP};

    std::vector<float3> colors;
    for (float r : {0.05f, 0.3f, 0.6f, 0.9f})
        for (float g : {0.05f, 0.3f, 0.6f, 0.9f})
            for (float b : {0.05f, 0.3f, 0.6f, 0.9f}) colors.push_back({r, g, b});
    colors.push_back({2.5f, 1.2f, 0.4f}); // beyond white, where the unclipped curves carry on

    for (ColorGamut_ gamut : {ColorGamut_sRGB_BT709, ColorGamut_BT2020_2100, ColorGamut_ProPhotoRGB})
    {
        const Chromaticities chr = gamut_chromaticities(gamut);
        const float3x3       M = RGB_to_XYZ(chr, 1.f), M_inv = la::inverse(M);
        const float3         white  = XYZ_from_xy(chr.white);
        auto                 lab_of = [&](float3 rgb) { return XYZ_to_Lab(mul(M, rgb), white); };

        for (const Setting &set : settings)
            for (int shape : shapes)
            {
                INFO("gamut = ", color_gamut_name(gamut), ", slope = ", set.slope, ", shape = ", shape);
                auto curve = [&](float v)
                {
                    const float y = shape % shader::SHAPE_CLIP == shader::SHAPE_LINE
                                        ? brightness_contrast_linear(v, set.slope, set.midpoint)
                                        : brightness_contrast_rational(v, set.slope, set.bias);
                    return shape >= shader::SHAPE_CLIP ? std::clamp(y, 0.f, 1.f) : y;
                };
                const float4 p0{set.slope, set.midpoint, set.bias, float(shape)};

                float rgb_moves_lightness = 0.f, rgb_moves_color = 0.f;
                for (const float3 &rgb : colors)
                {
                    INFO("rgb = ", rgb);
                    const float4 c{rgb, 0.7f};
                    const float3 lab = lab_of(rgb), n = normalize_Lab(lab);
                    for (int mode : {shader::CURVE_RGB, shader::CURVE_LIGHTNESS, shader::CURVE_CHROMATICITY})
                    {
                        INFO("mode = ", mode);
                        const float4 p1 =
                            mode == shader::CURVE_RGB ? float4{0.f} : float4{float(mode), white.x, white.y, white.z};
                        const float4 got = shader::edit_color(shader::EDIT_BRIGHTNESS_CONTRAST, c, p0, p1, M, M_inv);
                        CHECK(got.w == c.w);
                        if (mode == shader::CURVE_RGB)
                        {
                            CHECK(got.xyz() == float3{curve(rgb.x), curve(rgb.y), curve(rgb.z)});
                            const float3 moved  = lab_of(got.xyz()) - lab;
                            rgb_moves_lightness = std::max(rgb_moves_lightness, std::fabs(moved.x));
                            rgb_moves_color     = std::max({rgb_moves_color, std::fabs(moved.y), std::fabs(moved.z)});
                            continue;
                        }
                        // the moved components follow the curve, and the others stay, to within the trip out to
                        // RGB and back
                        const float3 want =
                            unnormalize_Lab(mode == shader::CURVE_LIGHTNESS ? float3{curve(n.x), n.y, n.z}
                                                                            : float3{n.x, curve(n.y), curve(n.z)});
                        const float3 got_lab = lab_of(got.xyz());
                        INFO("L*a*b* = ", lab, " -> ", got_lab, ", want ", want);
                        CHECK(approx_equal(got_lab, want, 2e-3f));
                    }
                }
                // the three channels alike move L* and also a* and b*, which the L*a*b* modes keep apart
                CHECK(rgb_moves_lightness > 5.f);
                CHECK(rgb_moves_color > 5.f);
            }
    }
}

TEST_CASE("A color survives the trip through L*a*b* and back")
{
    const float3 white = Lab_reference_white();

    // across the linear segment near black, the cube root above it, and out past the white
    for (float x : {0.0f, 0.002f, 0.05f, 0.4f, 1.0f, 4.0f})
        for (float y : {0.0f, 0.002f, 0.05f, 0.4f, 1.0f, 4.0f})
            for (float z : {0.0f, 0.002f, 0.05f, 0.4f, 1.0f, 4.0f})
            {
                CAPTURE(x);
                CAPTURE(y);
                CAPTURE(z);

                const float3 xyz{x, y, z};
                const float3 back = Lab_to_XYZ(XYZ_to_Lab(xyz, white), white);

                CHECK(back.x == doctest::Approx(x).epsilon(1e-3));
                CHECK(back.y == doctest::Approx(y).epsilon(1e-3));
                CHECK(back.z == doctest::Approx(z).epsilon(1e-3));
            }

    // and through the normalized form the editing controls use
    for (float L : {0.f, 12.f, 50.f, 88.f, 100.f})
        for (float a : {-100.f, -20.f, 0.f, 35.f, 120.f})
        {
            const float3 lab{L, a, -a};
            const float3 back = unnormalize_Lab(normalize_Lab(lab));

            CAPTURE(L);
            CAPTURE(a);
            CHECK(back.x == doctest::Approx(L).epsilon(1e-4));
            CHECK(back.y == doctest::Approx(a).epsilon(1e-4));
            CHECK(back.z == doctest::Approx(-a).epsilon(1e-4));
        }

    // the normalized form puts black at zero, the white's lightness at one, and neutral chroma in the middle,
    // so a tone curve applies to it unchanged
    const float3 n = normalize_Lab(float3{100.f, 0.f, 0.f});
    CHECK(n.x == doctest::Approx(1.f).epsilon(1e-4));
    CHECK(n.y == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(n.z == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(normalize_Lab(float3{0.f, 0.f, 0.f}).x == doctest::Approx(0.f).epsilon(1e-4));
}

TEST_CASE("Saving wide-gamut content through an HDR transfer function stays finite")
{
    // BT.2020 green is outside Rec.709, so as_interleaved()'s conversion to sRGB primaries makes it
    // negative just before the transfer function is applied.
    Image img(int2{2, 1}, 3);
    img.chromaticities    = gamut_chromaticities(ColorGamut_BT2020_2100);
    img.channels[0](0, 0) = 0.0f;
    img.channels[1](0, 0) = 1.0f;
    img.channels[2](0, 0) = 0.0f;
    img.channels[0](1, 0) = 0.2f;
    img.channels[1](1, 0) = 0.2f;
    img.channels[2](1, 0) = 0.2f;
    img.finalize();

    int  w, h, n;
    auto px = img.as_interleaved<float>(&w, &h, &n, TransferFunction{TransferFunction::BT2100_PQ}, false, true, true);
    for (int i = 0; i < w * h * n; ++i)
    {
        INFO("sample ", i);
        CHECK(std::isfinite(px[i]));
    }
}

TEST_CASE("axis_scale_fwd/inv round-trip across the range the histogram spans")
{
    for (int s = 0; s < AxisScale_COUNT; ++s)
        for (double x : {0.0, 1e-12, 1e-6, 1e-4, 0.5, 1.0, 100.0, 1e6, 1e12, -1e-6, -1.0, -1e6})
        {
            INFO("scale = ", s, ", x = ", x);
            double f = axis_scale_fwd(x, (AxisScale)s);
            REQUIRE(std::isfinite(f));
            // the sRGB scale runs the shaders' curve, which is float; ample for placing bins on screen
            const double epsilon = s == AxisScale_SRGB ? 1e-6 : 1e-9;
            CHECK(axis_scale_inv(f, (AxisScale)s) == doctest::Approx(x).epsilon(epsilon));
        }
}
