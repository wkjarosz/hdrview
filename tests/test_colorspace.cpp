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
    // mirror the curve around the origin so the sign survives.
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

// The round trip above passes for any normalization constant, since to_linear and from_linear scale by it
// symmetrically, and colour-science checks the curves in absolute cd/m^2 (test_colour_reference.cpp). These pin
// HDRView's own convention: BT.2408's 203 cd/m^2 HDR reference white is 1.0.
TEST_CASE("PQ and HLG map BT.2408's reference white to 1.0")
{
    // 203 is spelled out here, not taken from HDR_REFERENCE_WHITE_NITS, so these pin the constant instead of
    // tracking it
    CHECK(to_linear(1.0f, TransferFunction::BT2100_PQ) == doctest::Approx(10000.f / 203.f));
    CHECK(to_linear(shader::inverse_EOTF_BT2100_PQ(203.f), TransferFunction::BT2100_PQ) == doctest::Approx(1.f));

    // from_linear() therefore takes 1.0 to the signal for 203 cd/m^2, and is not interchangeable with the
    // inverse EOTF's absolute input, which the colorpass feeds
    CHECK(from_linear(1.f, TransferFunction::BT2100_PQ) == doctest::Approx(shader::inverse_EOTF_BT2100_PQ(203.f)));
    CHECK(from_linear(1.f, TransferFunction::BT2100_PQ) != doctest::Approx(shader::inverse_EOTF_BT2100_PQ(1.f)));

    // BT.2408 places HLG reference white at 75% signal, which a 1000 cd/m^2 reference display renders at
    // about 203 cd/m^2
    CHECK(shader::EOTF_BT2100_HLG(0.75f) == doctest::Approx(203.f).epsilon(0.002));
    CHECK(to_linear(0.75f, TransferFunction::BT2100_HLG) == doctest::Approx(1.f).epsilon(0.002));
}

// Narrow (studio) range packs luma into 16..235, a span of 219, unrelated to the HDR reference white above.
TEST_CASE("narrow-range dequantization maps the studio range to [0,1]")
{
    CHECK(dequantize_narrow<uint8_t>(16) == doctest::Approx(0.f));
    CHECK(dequantize_narrow<uint8_t>(235) == doctest::Approx(1.f));
    CHECK(dequantize_narrow<uint16_t>(16 * 256) == doctest::Approx(0.f));
    CHECK(dequantize_narrow<uint16_t>(235 * 256) == doctest::Approx(1.f));
}

TEST_CASE("quantize_full/dequantize_full round-trip within quantization error, undithered")
{
    for (float x : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f})
    {
        INFO("x = ", x);
        CHECK(dequantize_full(quantize_full<uint8_t>(x, 0, 0, false)) == doctest::Approx(x).epsilon(0.004));
        CHECK(dequantize_full(quantize_full<uint16_t>(x, 0, 0, false)) == doctest::Approx(x).epsilon(1e-4));
    }
}

TEST_CASE("color_u32_to_f128/color_f128_to_u32 round-trip exactly")
{
    for (uint32_t packed : {0x00000000u, 0xFFFFFFFFu, 0xFF0000FFu, 0x00FF00FFu, 0x0000FFFFu, 0x7F3C9AFFu})
    {
        INFO("packed = ", packed);
        CHECK(color_f128_to_u32(color_u32_to_f128(packed)) == packed);
    }
}

TEST_CASE("color_conversion_matrix generalizes to the colorpass's other reachable display gamuts")
{
    // Every gamut query_display_colorspace() can produce has to survive that call. Display-P3 is the one most
    // likely to show up: Wayland reports it, and most laptop panels advertise it.
    float3x3 M;
    CHECK(color_conversion_matrix(M, gamut_chromaticities(ColorGamut_sRGB_BT709),
                                  gamut_chromaticities(ColorGamut_Display_P3_SMPTE432)));

    // white stays white: an equal-energy RGB triple is the same color in both gamuts, which share D65
    float3 white = mul(M, float3{1.f, 1.f, 1.f});
    INFO("M * (1,1,1) = ", white);
    CHECK(approx_equal(white, float3{1.f, 1.f, 1.f}, 1e-4f));

    // Rec.709 is a subset of Display-P3, so saturated primaries map inside the unit cube with no negative
    // lobes, unlike the reverse direction
    for (const float3 &primary : {float3{1, 0, 0}, float3{0, 1, 0}, float3{0, 0, 1}})
    {
        float3 converted = mul(M, primary);
        INFO("primary ", primary, " -> ", converted);
        CHECK(la::all(la::gequal(converted, float3{-1e-4f})));
    }

    // and converting a gamut to itself is reported as unnecessary, so the colorpass keeps an identity matrix
    // in the common sRGB case
    float3x3 I;
    CHECK_FALSE(color_conversion_matrix(I, gamut_chromaticities(ColorGamut_sRGB_BT709),
                                        gamut_chromaticities(ColorGamut_sRGB_BT709)));
}

TEST_CASE("the image shader isolates luminance in the primaries it has already converted to")
{
    // assets/shaders/image-shader.sglsl converts every image into sRGB/BT.709 primaries before
    // choose_channel() runs, so Channels_Y weighs with shader::sRGB_Yw, the middle row of the classic rounded
    // sRGB matrix. colorspace.h derives sRGB_Yw() from the BT.709 chromaticities instead; the two agree to
    // about 3e-5 per weight.
    const float3 shader_Yw = shader::sRGB_Yw;
    INFO("colorspace.h says ", sRGB_Yw().x, ", ", sRGB_Yw().y, ", ", sRGB_Yw().z);
    CHECK(approx_equal(sRGB_Yw(), shader_Yw, 1e-4f));
    CHECK(shader_Yw.x + shader_Yw.y + shader_Yw.z == doctest::Approx(1.f).epsilon(1e-5));

    // Luminance is a property of the color, not of the primaries it is written in, so converting into sRGB
    // and weighing with sRGB's weights recovers what the source gamut's own weights describe. Restricted to
    // gamuts sharing sRGB's D65 white, since a chromatic adaptation is free to move Y.
    for (ColorGamut_ gamut : {ColorGamut_Display_P3_SMPTE432, ColorGamut_BT2020_2100, ColorGamut_AdobeRGB})
    {
        const Chromaticities chr = gamut_chromaticities(gamut);
        INFO("gamut = ", color_gamut_name(gamut));
        REQUIRE(approx_equal(chr.white, Chromaticities{}.white, 1e-4f));

        float3x3 M;
        color_conversion_matrix(M, chr, Chromaticities{});

        const float3 native_Yw = computeYw(chr);
        for (const float3 &rgb :
             {float3{1, 1, 1}, float3{1, 0, 0}, float3{0, 1, 0}, float3{0, 0, 1}, float3{0.2f, 0.7f, 0.4f}})
        {
            INFO("rgb = ", rgb);
            CHECK(dot(mul(M, rgb), sRGB_Yw()) == doctest::Approx(dot(rgb, native_Yw)).epsilon(1e-4));
        }

        // and the weights differ, so the check above is not passing on a coincidence
        CHECK_FALSE(approx_equal(native_Yw, sRGB_Yw(), 1e-3f));
    }
}

TEST_CASE("every transfer function is odd where it mirrors, and its vector form is its scalar one per component")
{
    // The properties colour-science's values cannot show (test_colour_reference.cpp checks those): which curves
    // HDRView mirrors around the origin, that a NaN survives them, and that the vec3 overloads the shaders call
    // apply the scalar curve to each component in order. The round trip is checked above.
    struct Curve
    {
        const char *name;
        float (*f)(float);
        float3 (*v)(float3);
        bool odd; ///< mirrored around the origin, rather than extended linearly or clamped
    };
    const Curve curves[] = {
        {"linear_to_sRGB", shader::linear_to_sRGB, shader::linear_to_sRGB, true},
        {"sRGB_to_linear", shader::sRGB_to_linear, shader::sRGB_to_linear, true},
        {"OETF_ITU", shader::OETF_ITU, shader::OETF_ITU, false},
        {"inverse_OETF_ITU", shader::inverse_OETF_ITU, shader::inverse_OETF_ITU, false},
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

    // straddling each curve's breakpoint
    const float values[] = {1e-6f, 0.0031308f, 0.004f, 0.018f, 0.019f, 0.0228f, 0.04045f, 0.0913f, 0.18f, 0.5f, 1.f};
    for (const Curve &c : curves)
    {
        INFO("curve = ", std::string(c.name));
        for (float x : values)
        {
            INFO("x = ", x);
            if (c.odd)
                CHECK(c.f(-x) == -c.f(x));
            // the vector overload applies the curve per component, in order; bitwise, since 2x leaves PQ's
            // [0,1] domain, where both give NaN
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
    CHECK(shader::RGB_to_Y(float3{0.2f, 0.7f, 0.4f}, shader::sRGB_Yw) ==
          doctest::Approx(0.2f * 0.212671f + 0.7f * 0.715160f + 0.4f * 0.072169f));
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
        }
    }
}

TEST_CASE("blend() keeps fractional results in the difference modes")
{
    // colorspace.h is a header, where only a qualified std::abs is sure to reach the floating-point overloads:
    // libstdc++ leaves only <stdlib.h>'s integer ::abs at global scope. Every difference here is below 1, so an
    // integer abs would return zero.
    const float top = 0.6f, bottom = 0.25f;

    CHECK(blend(top, bottom, BlendMode_Difference) == doctest::Approx(0.35f));
    CHECK(blend(bottom, top, BlendMode_Difference) == doctest::Approx(0.35f));
    CHECK(blend(top, bottom, BlendMode_Relative_Difference) == doctest::Approx(0.35f / 0.26f));

    // also below 1 with the operands the other way round
    CHECK(blend(0.2f, 0.1f, BlendMode_Difference) == doctest::Approx(0.1f));
    CHECK(blend(0.1f, 0.2f, BlendMode_Difference) == doctest::Approx(0.1f));
}

namespace
{

/// Colors spanning what an HSL adjustment has to handle, including values outside [0,1].
const float3 k_hsl_colors[] = {
    {0.5f, 0.25f, 0.125f}, {1.f, 0.f, 0.f},    {0.f, 1.f, 0.f},       {0.f, 0.f, 1.f}, {0.2f, 0.7f, 0.9f},
    {0.f, 0.f, 0.f},       {1.f, 1.f, 1.f},    {0.35f, 0.35f, 0.35f}, // achromatic, where hue is undefined
    {4.f, 1.5f, 0.25f},    {-0.2f, 0.4f, 1.f},                        // beyond white and below black
};

float hsl_spread(float3 c) { return std::max({c.x, c.y, c.z}) - std::min({c.x, c.y, c.z}); }

} // namespace

TEST_CASE("a color survives the trip through HSL and back")
{
    // the textbook version measures saturation as a fraction of the unit range, which has no meaning for a
    // value brighter than white
    for (const float3 &rgb : k_hsl_colors)
    {
        CAPTURE(rgb.x);
        CAPTURE(rgb.y);
        CAPTURE(rgb.z);

        const float3 out = HSL_to_RGB(RGB_to_HSL(rgb));

        CHECK(out.x == doctest::Approx(rgb.x).epsilon(1e-4));
        CHECK(out.y == doctest::Approx(rgb.y).epsilon(1e-4));
        CHECK(out.z == doctest::Approx(rgb.z).epsilon(1e-4));
    }
}

TEST_CASE("HSL's lightness is the midpoint of the extremes, and gray has no hue")
{
    for (const float3 &rgb : k_hsl_colors)
    {
        CAPTURE(rgb.x);
        CAPTURE(rgb.y);
        CAPTURE(rgb.z);

        const float3 hsl = RGB_to_HSL(rgb);
        const float  mn = std::min({rgb.x, rgb.y, rgb.z}), mx = std::max({rgb.x, rgb.y, rgb.z});

        CHECK(hsl.z == doctest::Approx(0.5f * (mn + mx)));
        CHECK(hsl.x >= 0.f);
        CHECK(hsl.x <= 1.f);

        // no hue to rotate, which a rotation has to leave alone
        if (mx - mn < 1e-6f)
            CHECK(hsl.y == doctest::Approx(0.f));
        else
            CHECK(hsl.y > 0.f);
    }
}

TEST_CASE("an HSL adjustment that asks for nothing changes nothing")
{
    // exact, not approximate: this is what an opened dialog applies before any slider is touched
    for (const float3 &rgb : k_hsl_colors)
    {
        CAPTURE(rgb.x);
        CAPTURE(rgb.y);
        CAPTURE(rgb.z);

        for (float turns : {0.f, 1.f, -1.f})
        {
            CAPTURE(turns);
            const float3 out = adjust_HSL(rgb, turns, 1.f, 0.f);
            CHECK(out.x == doctest::Approx(rgb.x).epsilon(1e-3));
            CHECK(out.y == doctest::Approx(rgb.y).epsilon(1e-3));
            CHECK(out.z == doctest::Approx(rgb.z).epsilon(1e-3));
        }
    }
}

TEST_CASE("rotating the hue moves it around the wheel and leaves the lightness alone")
{
    for (const float3 &rgb : k_hsl_colors)
    {
        const float3 hsl = RGB_to_HSL(rgb);
        if (hsl.y <= 1e-6f)
            continue; // gray has no hue to rotate

        CAPTURE(rgb.x);
        CAPTURE(rgb.y);
        CAPTURE(rgb.z);

        for (float turn : {1.f / 3.f, -0.25f, 0.5f})
        {
            CAPTURE(turn);
            const float3 out = RGB_to_HSL(adjust_HSL(rgb, turn, 1.f, 0.f));

            float moved = out.x - hsl.x - turn;
            moved -= std::floor(moved + 0.5f); // the shortest way round, so the wrap does not count
            CHECK(std::abs(moved) < 0.01f);

            // a rotation is a move around the wheel and nothing else
            CHECK(out.z == doctest::Approx(hsl.z).epsilon(1e-3));
        }
    }
}

TEST_CASE("saturation spreads the components apart, and zero of it leaves gray")
{
    for (const float3 &rgb : k_hsl_colors)
    {
        CAPTURE(rgb.x);
        CAPTURE(rgb.y);
        CAPTURE(rgb.z);

        const float3 gray = adjust_HSL(rgb, 0.f, 0.f, 0.f);
        CHECK(gray.y == doctest::Approx(gray.x).epsilon(1e-3));
        CHECK(gray.z == doctest::Approx(gray.x).epsilon(1e-3));

        // at the lightness it had, so desaturating did not also darken it
        CHECK(gray.x == doctest::Approx(RGB_to_HSL(rgb).z).epsilon(1e-3));

        // a turn of something with no hue is a no-op, whichever path the adjustment takes
        const float3 turned = adjust_HSL(gray, 0.25f, 1.f, 0.f);
        CHECK(turned.x == doctest::Approx(gray.x).epsilon(1e-3));
        CHECK(turned.y == doctest::Approx(gray.y).epsilon(1e-3));
        CHECK(turned.z == doctest::Approx(gray.z).epsilon(1e-3));

        // monotonic either side of where it started, as a slider needs
        if (RGB_to_HSL(rgb).y > 1e-6f)
        {
            CHECK(hsl_spread(adjust_HSL(rgb, 0.f, 0.5f, 0.f)) < hsl_spread(rgb));
            CHECK(hsl_spread(adjust_HSL(rgb, 0.f, 1.5f, 0.f)) > hsl_spread(rgb));
        }
    }
}

TEST_CASE("the lightness control mixes toward black and white rather than washing the color out")
{
    // Photoshop's slider of that name; changing L directly desaturates on the way
    const float3 rgb{0.5f, 0.25f, 0.125f};

    const float3 black = adjust_HSL(rgb, 0.f, 1.f, -1.f);
    const float3 white = adjust_HSL(rgb, 0.f, 1.f, 1.f);
    const float3 half  = adjust_HSL(rgb, 0.f, 1.f, 0.5f);

    for (int i = 0; i < 3; ++i)
    {
        CAPTURE(i);
        CHECK(black[i] == doctest::Approx(0.f).epsilon(1e-4));
        CHECK(white[i] == doctest::Approx(1.f).epsilon(1e-4));
        CHECK(half[i] == doctest::Approx(0.5f * (rgb[i] + 1.f)).epsilon(1e-3));
    }
}

TEST_CASE("Perlin's gain and Schlick's bias have the properties they are chosen for")
{
    // shape functions, checked on the identities that make them usable, not on their formulas
    for (float P : {0.25f, 0.5f, 1.f, 2.f, 4.f})
    {
        CAPTURE(P);

        // pinned at both ends and at the middle, whatever the shape
        CHECK(gain_Perlin(0.f, P) == doctest::Approx(0.f));
        CHECK(gain_Perlin(0.5f, P) == doctest::Approx(0.5f));
        CHECK(gain_Perlin(1.f, P) == doctest::Approx(1.f));

        for (int i = 0; i <= 20; ++i)
        {
            const float t = float(i) / 20.f;
            CAPTURE(t);

            // an exponent of one is the identity, and the inverse exponent undoes it
            CHECK(gain_Perlin(t, 1.f) == doctest::Approx(t));
            CHECK(gain_Perlin(gain_Perlin(t, P), 1.f / P) == doctest::Approx(t).epsilon(1e-4));

            // it never leaves [0,1]
            CHECK(gain_Perlin(t, P) >= -1e-6f);
            CHECK(gain_Perlin(t, P) <= 1.f + 1e-6f);
        }
    }

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

TEST_CASE("Both brightness/contrast curves leave a neutral setting alone and steepen together")
{
    // What the sliders mean: contrast is an angle, so its ends are a flat line and a vertical one, and
    // brightness slides the point the curve pivots about.
    auto slope_of    = [](float c) { return float(std::tan(lerp(0.0, M_PI_2, c / 2.0 + 0.5))); };
    auto midpoint_of = [](float b) { return (1.f - b) / 2.f; };
    auto bias_of     = [](float b) { return (b + 1.f) / 2.f; };

    CHECK(slope_of(0.f) == doctest::Approx(1.f));  // 45 degrees: no change
    CHECK(slope_of(-1.f) == doctest::Approx(0.f)); // flat: everything to one level
    CHECK(slope_of(1.f) > 1e6f);                   // vertical: a hard threshold
    CHECK(midpoint_of(0.f) == doctest::Approx(0.5f));
    CHECK(bias_of(0.f) == doctest::Approx(0.5f));

    // neither curve moves anything when both controls are centered
    for (int i = 0; i <= 20; ++i)
    {
        const float v = float(i) / 20.f;
        CAPTURE(v);
        CHECK(brightness_contrast_linear(v, slope_of(0.f), midpoint_of(0.f)) == doctest::Approx(v));
        CHECK(brightness_contrast_nonlinear(v, slope_of(0.f), bias_of(0.f)) == doctest::Approx(v));
    }

    // raising contrast pushes the ends apart about the midpoint, and both curves still meet at it
    for (float c : {0.3f, 0.6f})
    {
        CAPTURE(c);
        const float slope = slope_of(c);

        CHECK(brightness_contrast_linear(0.5f, slope, 0.5f) == doctest::Approx(0.5f));
        CHECK(brightness_contrast_nonlinear(0.5f, slope, 0.5f) == doctest::Approx(0.5f));

        CHECK(brightness_contrast_linear(0.75f, slope, 0.5f) > 0.75f);
        CHECK(brightness_contrast_linear(0.25f, slope, 0.5f) < 0.25f);
        CHECK(brightness_contrast_nonlinear(0.75f, slope, 0.5f) > 0.75f);
        CHECK(brightness_contrast_nonlinear(0.25f, slope, 0.5f) < 0.25f);
    }

    // the straight line runs out of [0,1], so an HDR sample keeps its relation to its neighbors; the s-curve
    // approaches the ends without reaching them
    const float steep = slope_of(0.8f);
    CHECK(brightness_contrast_linear(1.f, steep, 0.5f) > 1.f);
    CHECK(brightness_contrast_linear(0.f, steep, 0.5f) < 0.f);
    for (int i = 0; i <= 20; ++i)
    {
        const float v = float(i) / 20.f;
        CAPTURE(v);
        CHECK(brightness_contrast_nonlinear(v, steep, 0.5f) >= -1e-6f);
        CHECK(brightness_contrast_nonlinear(v, steep, 0.5f) <= 1.f + 1e-6f);
    }

    // brightness moves which input lands on the middle, in opposite directions for the two curves' parameters
    // but to the same effect
    CHECK(brightness_contrast_linear(0.4f, 1.f, midpoint_of(0.2f)) > 0.4f);
    CHECK(brightness_contrast_nonlinear(0.4f, 1.f, bias_of(0.2f)) > 0.4f);
}

TEST_CASE("L*a*b* agrees with the values the standard defines it by")
{
    // checked against the definition, since a round trip passes just as well with both halves wrong the same way
    const float3 white = Lab_reference_white();

    // the reference white is the point L* is scaled to, and it is achromatic
    const float3 w = XYZ_to_Lab(white, white);
    CHECK(w.x == doctest::Approx(100.f).epsilon(1e-4));
    CHECK(w.y == doctest::Approx(0.f).epsilon(1e-4));
    CHECK(w.z == doctest::Approx(0.f).epsilon(1e-4));

    // black is the other end, and also achromatic
    const float3 k = XYZ_to_Lab(float3{0.f, 0.f, 0.f}, white);
    CHECK(k.x == doctest::Approx(0.f).epsilon(1e-4));
    CHECK(k.y == doctest::Approx(0.f).epsilon(1e-4));
    CHECK(k.z == doctest::Approx(0.f).epsilon(1e-4));

    // any neutral is achromatic whatever its level, since the three ratios to the white are equal
    for (float y : {0.02f, 0.18f, 0.5f, 0.9f})
    {
        CAPTURE(y);
        const float3 gray = XYZ_to_Lab(white * y, white);
        CHECK(gray.y == doctest::Approx(0.f).epsilon(1e-3));
        CHECK(gray.z == doctest::Approx(0.f).epsilon(1e-3));
    }

    // the published anchors of the lightness curve: mid gray at Y = 0.18 sits near L* = 49.5, and the linear
    // segment below Y = 216/24389 has slope kappa = 24389/27 in Y
    CHECK(XYZ_to_Lab(white * 0.184187f, white).x == doctest::Approx(50.f).epsilon(1e-3));

    const float y_knee = 216.f / 24389.f;
    CHECK(XYZ_to_Lab(white * y_knee, white).x == doctest::Approx(8.f).epsilon(1e-3));
    const float y_small = 0.5f * y_knee;
    CHECK(XYZ_to_Lab(white * y_small, white).x == doctest::Approx((24389.f / 27.f) * y_small).epsilon(1e-3));

    // L* rises with luminance and nothing else does
    float previous = -1.f;
    for (int i = 0; i <= 20; ++i)
    {
        const float3 lab = XYZ_to_Lab(white * (float(i) / 20.f), white);
        CHECK(lab.x > previous);
        previous = lab.x;
    }

    // a* is the green-red axis and b* the blue-yellow one: more X than the white asks for reads red, more Z
    // reads blue
    CHECK(XYZ_to_Lab(float3{white.x * 1.2f, white.y, white.z}, white).y > 0.f);
    CHECK(XYZ_to_Lab(float3{white.x * 0.8f, white.y, white.z}, white).y < 0.f);
    CHECK(XYZ_to_Lab(float3{white.x, white.y, white.z * 1.2f}, white).z < 0.f);
    CHECK(XYZ_to_Lab(float3{white.x, white.y, white.z * 0.8f}, white).z > 0.f);
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

TEST_CASE("sRGB primaries land where L*a*b* is documented to put them")
{
    // The path an edit takes, linear sRGB through XYZ into L*a*b*, against values published for the sRGB
    // primaries under D65. These catch a transposed matrix or a swapped axis; the achromatic checks cannot.
    const float3 white  = Lab_reference_white();
    auto         lab_of = [&](float3 rgb) { return XYZ_to_Lab(mul(sRGB_to_XYZ(), rgb), white); };

    const float3 red = lab_of(float3{1.f, 0.f, 0.f});
    CHECK(red.x == doctest::Approx(53.24f).epsilon(2e-3));
    CHECK(red.y == doctest::Approx(80.09f).epsilon(2e-3));
    CHECK(red.z == doctest::Approx(67.20f).epsilon(2e-3));

    const float3 green = lab_of(float3{0.f, 1.f, 0.f});
    CHECK(green.x == doctest::Approx(87.73f).epsilon(2e-3));
    CHECK(green.y == doctest::Approx(-86.18f).epsilon(2e-3));
    CHECK(green.z == doctest::Approx(83.18f).epsilon(2e-3));

    const float3 blue = lab_of(float3{0.f, 0.f, 1.f});
    CHECK(blue.x == doctest::Approx(32.30f).epsilon(2e-3));
    CHECK(blue.y == doctest::Approx(79.19f).epsilon(2e-3));
    CHECK(blue.z == doctest::Approx(-107.86f).epsilon(2e-3));

    // white through the same path, tying the matrix and the reference white together
    const float3 w = lab_of(float3{1.f, 1.f, 1.f});
    CHECK(w.x == doctest::Approx(100.f).epsilon(1e-3));
    CHECK(w.y == doctest::Approx(0.f).epsilon(2e-2));
    CHECK(w.z == doctest::Approx(0.f).epsilon(2e-2));
}

TEST_CASE("quantize_full maps non-finite input to a representable integer")
{
    // Casting a NaN to an integer type is undefined behavior, and std::clamp doesn't filter it out:
    // both of its comparisons are false for NaN, so it returns the NaN unchanged.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    CHECK(quantize_full<uint8_t>(nan, 0, 0, false) == 0);
    CHECK(quantize_full<uint16_t>(nan, 0, 0, false) == 0);
    CHECK(quantize_full<uint8_t>(inf, 0, 0, false) == 255);
    CHECK(quantize_full<uint8_t>(-inf, 0, 0, false) == 0);
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
    auto px =
        img.as_interleaved<float>(&w, &h, &n, 1.f, TransferFunction{TransferFunction::BT2100_PQ}, false, true, true);
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
