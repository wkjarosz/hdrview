//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "common.h" // for blend_mode_names()

#include <algorithm>
#include <cmath>

TEST_CASE("sRGB encode/decode are inverses, including negative (out-of-gamut) values")
{
    for (float x : {0.0f, 0.001f, 0.0031308f, 0.02f, 0.18f, 0.5f, 0.8f, 1.0f, -0.18f, -0.8f})
    {
        INFO("x = ", x);
        CHECK(sRGB_to_linear(linear_to_sRGB(x)) == doctest::Approx(x));
        CHECK(linear_to_sRGB(sRGB_to_linear(x)) == doctest::Approx(x));
    }
}

TEST_CASE("linear_to_gamma round-trips when encoding with 1/gamma and decoding with gamma")
{
    for (float gamma : {1.0f, 1.8f, 2.2f, 2.6f})
        for (float x : {0.0f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f})
        {
            INFO("gamma = ", gamma, ", x = ", x);
            float encoded = linear_to_gamma(x, 1.f / gamma);
            CHECK(linear_to_gamma(encoded, gamma) == doctest::Approx(x));
        }
}

TEST_CASE("to_linear/from_linear round-trip for every TransferFunction")
{
    for (int t = TransferFunction::Unspecified; t < TransferFunction::Count; ++t)
    {
        TransferFunction tf{static_cast<TransferFunction::Type_>(t)};
        for (float x : {0.1f, 0.25f, 0.5f, 0.75f, 0.9f})
        {
            INFO("transfer function = ", transfer_function_name(tf), ", x = ", x);
            CHECK(to_linear(from_linear(x, tf), tf) == doctest::Approx(x));
        }
    }
}

// The round-trip test above passes for any choice of normalization constant, since to_linear and from_linear
// scale by it symmetrically. These cases pin the absolute mapping against the values BT.2100/BT.2408 specify.
TEST_CASE("PQ maps absolute luminance relative to BT.2408 reference white")
{
    // The PQ EOTF is defined over 0 to 10000 cd/m^2.
    CHECK(EOTF_BT2100_PQ(1.0f) == doctest::Approx(10000.f));
    CHECK(EOTF_BT2100_PQ(0.0f) == doctest::Approx(0.f));

    // to_linear additionally normalizes so that reference white lands at 1.0. These expectations spell out 203
    // rather than referring to HDR_REFERENCE_WHITE_NITS, so that they pin the constant instead of tracking it.
    CHECK(to_linear(1.0f, TransferFunction::BT2100_PQ) == doctest::Approx(10000.f / 203.f));

    const float reference_white_signal = inverse_EOTF_BT2100_PQ(203.f);
    CHECK(to_linear(reference_white_signal, TransferFunction::BT2100_PQ) == doctest::Approx(1.f));
}

TEST_CASE("HLG maps its 75% reference level to BT.2408 reference white")
{
    // BT.2408 places HLG reference white at 75% signal, which the 1000 cd/m^2 reference display renders at
    // approximately 203 cd/m^2.
    CHECK(EOTF_BT2100_HLG(0.75f) == doctest::Approx(203.f).epsilon(0.002));
    CHECK(to_linear(0.75f, TransferFunction::BT2100_HLG) == doctest::Approx(1.f).epsilon(0.002));
}

TEST_CASE("HLG system gamma follows display peak luminance")
{
    CHECK(HLG_system_gamma(1000.f) == doctest::Approx(1.2f));
    CHECK(HLG_system_gamma(2000.f) == doctest::Approx(1.2f + 0.42f * std::log10(2.f)));
    CHECK(HLG_system_gamma(500.f) == doctest::Approx(1.2f - 0.42f * std::log10(2.f)));
}

// Narrow (studio) range packs luma into 16..235, a span of 219. This 219 is unrelated to the HDR reference
// white above; the two must not be conflated.
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

TEST_CASE("RGB_to_XYZ/XYZ_to_RGB are inverses for the default (sRGB/BT.709) primaries")
{
    Chromaticities chroma; // defaults to sRGB/BT.709 primaries and D65 white point

    float3x3 rgb_to_xyz = RGB_to_XYZ(chroma, 1.f);
    float3x3 xyz_to_rgb = XYZ_to_RGB(chroma, 1.f);

    for (float3 rgb : {float3{1.f, 1.f, 1.f}, float3{1.f, 0.f, 0.f}, float3{0.2f, 0.5f, 0.8f}, float3{0.f, 0.f, 0.f}})
    {
        INFO("rgb = ", rgb);
        float3 round_tripped = mul(xyz_to_rgb, mul(rgb_to_xyz, rgb));
        CHECK(approx_equal(round_tripped, rgb, 1e-4f));
    }
}

TEST_CASE("color_conversion_matrix's Rec.709->BT.2020 matches the published ITU-R BT.2087 reference matrix")
{
    // The HDR colorpass shader needs a linear-light Rec.709 -> BT.2020 primaries conversion. Confirms
    // color_conversion_matrix() (computed from this codebase's own Chromaticities tables) agrees with the
    // commonly published ITU-R BT.2087 reference matrix, so the shader can use the former (computed in C++,
    // uploaded as a uniform) instead of hardcoding the latter.
    float3x3 M;
    bool     needs_conversion = color_conversion_matrix(M, gamut_chromaticities(ColorGamut_sRGB_BT709),
                                                        gamut_chromaticities(ColorGamut_BT2020_2100));
    CHECK(needs_conversion);

    float3x3 reference{float3{0.627403926658f, 0.069097233123f, 0.016391587664f},
                       float3{0.329282097415f, 0.919541035593f, 0.088013255546f},
                       float3{0.043313797587f, 0.011361189924f, 0.895595009604f}};

    INFO("computed M = ", M);
    INFO("reference  = ", reference);
    CHECK(approx_equal(M, reference, 1e-4f));
}

TEST_CASE("color_conversion_matrix generalizes to the colorpass's other reachable display gamuts")
{
    // The colorpass uploads color_conversion_matrix(Rec.709 -> display primaries) as a uniform, so every
    // gamut query_display_colorspace() can produce has to survive that call. Display-P3 is the one most
    // likely to actually show up (Wayland reports it, and it is what most laptop panels advertise).
    float3x3 M;
    CHECK(color_conversion_matrix(M, gamut_chromaticities(ColorGamut_sRGB_BT709),
                                  gamut_chromaticities(ColorGamut_Display_P3_SMPTE432)));

    // White must stay white: an equal-energy RGB triple is the same color in both gamuts, since they share
    // the D65 white point. This is the property the shader actually depends on.
    float3 white = mul(M, float3{1.f, 1.f, 1.f});
    INFO("M * (1,1,1) = ", white);
    CHECK(approx_equal(white, float3{1.f, 1.f, 1.f}, 1e-4f));

    // Rec.709 is a subset of Display-P3, so saturated primaries must map inside the unit cube (no negative
    // lobes), unlike the reverse direction.
    for (const float3 &primary : {float3{1, 0, 0}, float3{0, 1, 0}, float3{0, 0, 1}})
    {
        float3 converted = mul(M, primary);
        INFO("primary ", primary, " -> ", converted);
        CHECK(la::all(la::gequal(converted, float3{-1e-4f})));
    }

    // And converting a gamut to itself must be reported as unnecessary, which is what lets the colorpass
    // keep an identity matrix in the common sRGB case.
    float3x3 I;
    CHECK_FALSE(color_conversion_matrix(I, gamut_chromaticities(ColorGamut_sRGB_BT709),
                                        gamut_chromaticities(ColorGamut_sRGB_BT709)));
}

TEST_CASE("the image shader isolates luminance in the primaries it has already converted to")
{
    // assets/shaders/image-shader.sglsl converts every image into sRGB/BT.709 primaries before
    // choose_channel() runs, so the Channels_Y case weighs with sRGB's own luminance weights -- spelled out
    // in colorspaces.sglsl as sRGB_Yw, since GLSL can't include colorspace.h. If you edit either side, edit
    // both.
    // The shader spells them as the middle row of its own RGB2XYZ, the classic rounded sRGB matrix, while
    // colorspace.h derives them from the BT.709 chromaticities; the two agree to about 3e-5 per weight,
    // which is well under a code value at any bit depth HDRView displays.
    const float3 glsl_sRGB_Yw{0.212671f, 0.715160f, 0.072169f};
    INFO("colorspace.h says ", sRGB_Yw().x, ", ", sRGB_Yw().y, ", ", sRGB_Yw().z);
    CHECK(approx_equal(sRGB_Yw(), glsl_sRGB_Yw, 1e-4f));
    CHECK(glsl_sRGB_Yw.x + glsl_sRGB_Yw.y + glsl_sRGB_Yw.z == doctest::Approx(1.f).epsilon(1e-5));

    // What makes those the right weights for a value that arrived in some other gamut: luminance is a
    // property of the color, not of the primaries it is written in, so converting into sRGB and weighing
    // with sRGB's weights recovers the luminance the source gamut's own weights describe. Restricted to
    // gamuts that share sRGB's D65 white, since a chromatic adaptation is free to move Y.
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

        // And that the weights actually differ, so the check above is not passing on a coincidence: this
        // is the whole reason using the source gamut's weights after the conversion is wrong.
        CHECK_FALSE(approx_equal(native_Yw, sRGB_Yw(), 1e-3f));
    }
}

TEST_CASE("colorpass GLSL PQ constants match colorspace.h's inverse_EOTF_BT2100_PQ")
{
    // assets/shaders/colorspaces.sglsl's pq_encode() hand-duplicates the five ST.2084 constants as decimal
    // literals, because GLSL can't include colorspace.h. Nothing in the build enforces that they stay in
    // sync -- this test does. If you edit either side, edit both.
    //
    // First: the decimals in the shader are exactly the rationals in colorspace.h, not approximations. Every
    // one of these has a power-of-two denominator, so it is exactly representable and == is the right test.
    CHECK(0.1593017578125 == 2610.0 / 16384.0);
    CHECK(78.84375 == 2523.0 * 128.0 / 4096.0);
    CHECK(0.8359375 == 3424.0 / 4096.0);
    CHECK(18.8515625 == 2413.0 / 4096.0 * 32.0);
    CHECK(18.6875 == 2392.0 / 4096.0 * 32.0);

    // Second, and more important: the shader's *calling convention*. pq_encode() consumes absolute nits and
    // does its own /10000 normalization, matching inverse_EOTF_BT2100_PQ() one-for-one. This is deliberately
    // NOT the convention of colorspace.h's from_linear(), which pre-multiplies by HDR_REFERENCE_WHITE_NITS
    // (see below) -- feeding the shader a from_linear()-style value would silently shift everything on
    // screen by ~2.7 stops.
    auto glsl_pq_encode = [](float nits)
    {
        // Transcribed literally from pq_encode() in assets/shaders/colorspaces.sglsl.
        const float m1  = 0.1593017578125f;
        const float m2  = 78.84375f;
        const float c1  = 0.8359375f;
        const float c2  = 18.8515625f;
        const float c3  = 18.6875f;
        float       Y   = std::max(nits, 0.0f) / 10000.0f;
        float       Ym1 = std::pow(Y, m1);
        return std::pow((c1 + c2 * Ym1) / (1.0f + c3 * Ym1), m2);
    };

    for (float nits : {0.f, 0.005f, 1.f, 80.f, 203.f, 1000.f, 4000.f, 10000.f})
    {
        INFO("nits = ", nits);
        CHECK(glsl_pq_encode(nits) == doctest::Approx(inverse_EOTF_BT2100_PQ(nits)).epsilon(1e-5));
    }

    // PQ has no negative representation; both sides must clip rather than produce NaN from pow().
    CHECK(glsl_pq_encode(-100.f) == doctest::Approx(0.f));

    // Anchor points, so a botched edit that keeps the two implementations consistent but wrong still fails:
    // PQ is absolute, with 10000 nits at the top of the range and the SDR reference at ~0.58.
    CHECK(inverse_EOTF_BT2100_PQ(10000.f) == doctest::Approx(1.f));
    CHECK(inverse_EOTF_BT2100_PQ(0.f) == doctest::Approx(0.f));
    CHECK(inverse_EOTF_BT2100_PQ(203.f) == doctest::Approx(0.5806f).epsilon(1e-3));

    // Pin the reference-white divergence itself: from_linear() treats 1.0 as HDR_REFERENCE_WHITE_NITS (203
    // cd/m^2, the BT.2408 reference white), so it is NOT interchangeable with the shader's absolute-nits
    // input.
    CHECK(from_linear(1.f, TransferFunction::BT2100_PQ) ==
          doctest::Approx(inverse_EOTF_BT2100_PQ(HDR_REFERENCE_WHITE_NITS)));
    CHECK(from_linear(1.f, TransferFunction::BT2100_PQ) != doctest::Approx(inverse_EOTF_BT2100_PQ(1.f)));
}

TEST_CASE("blend()'s overloads agree on every blend mode")
{
    // The three overloads are separate switches over the same enum, so a mode present in one and absent
    // from another silently blends as whatever its `default:` is.
    const float top = 0.6f, bottom = 0.25f, top_a = 0.75f, bottom_a = 0.5f;

    for (int m = 0; m < BlendMode_COUNT; ++m)
    {
        auto mode = (BlendMode_)m;
        CAPTURE(blend_mode_names()[m]);

        float  s  = blend(top, bottom, mode);
        float2 v2 = blend(float2{top, top_a}, float2{bottom, bottom_a}, mode);
        float4 v4 = blend(float4{top, top, top, top_a}, float4{bottom, bottom, bottom, bottom_a}, mode);

        // The two vector overloads carry the same alpha and must agree on both color and alpha.
        CHECK(v2.x == doctest::Approx(v4.x));
        CHECK(v4.x == doctest::Approx(v4.y));
        CHECK(v4.x == doctest::Approx(v4.z));
        CHECK(v2.y == doctest::Approx(v4.w));

        // The scalar overload has no alpha to composite with, so for Normal it keeps the top sample rather
        // than compositing -- which is what the statistics pass wants of it. Every other mode is pure
        // per-channel arithmetic and must match.
        if (mode != BlendMode_Normal)
            CHECK(v4.x == doctest::Approx(s));
    }
}

TEST_CASE("blend() keeps fractional results in the difference modes")
{
    // colorspace.h is a header, where only a qualified std::abs is sure to reach the floating-point
    // overloads: libstdc++ leaves just <stdlib.h>'s integer ::abs at global scope, libc++ the float ones
    // too. Every difference here is below 1, so an integer abs would return exactly zero.
    const float top = 0.6f, bottom = 0.25f;

    CHECK(blend(top, bottom, BlendMode_Difference) == doctest::Approx(0.35f));
    CHECK(blend(bottom, top, BlendMode_Difference) == doctest::Approx(0.35f));
    CHECK(blend(top, bottom, BlendMode_Relative_Difference) == doctest::Approx(0.35f / 0.26f));

    // Also below 1 with the operands the other way round.
    CHECK(blend(0.2f, 0.1f, BlendMode_Difference) == doctest::Approx(0.1f));
    CHECK(blend(0.1f, 0.2f, BlendMode_Difference) == doctest::Approx(0.1f));
}

namespace
{

//! Colors spanning what an HSL adjustment has to handle, including the ones outside [0,1] that a textbook
//! HSL has no answer for.
const float3 k_hsl_colors[] = {
    {0.5f, 0.25f, 0.125f}, {1.f, 0.f, 0.f},    {0.f, 1.f, 0.f},       {0.f, 0.f, 1.f}, {0.2f, 0.7f, 0.9f},
    {0.f, 0.f, 0.f},       {1.f, 1.f, 1.f},    {0.35f, 0.35f, 0.35f}, // achromatic, where hue is undefined
    {4.f, 1.5f, 0.25f},    {-0.2f, 0.4f, 1.f},                        // beyond white and below black
};

float hsl_spread(float3 c) { return std::max({c.x, c.y, c.z}) - std::min({c.x, c.y, c.z}); }

} // namespace

TEST_CASE("a color survives the trip through HSL and back")
{
    // What makes these usable on an HDR image at all: the textbook version measures saturation as a
    // fraction of the unit range, which has no meaning for a value brighter than white, and loses it.
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

        // The case a hue rotation has to leave alone: there is no hue to rotate.
        if (mx - mn < 1e-6f)
            CHECK(hsl.y == doctest::Approx(0.f));
        else
            CHECK(hsl.y > 0.f);
    }
}

TEST_CASE("an HSL adjustment that asks for nothing changes nothing")
{
    // Exact rather than approximate: this is what an opened dialog applies before any slider is touched,
    // and a whole turn of the hue has to land back where it started for the same reason.
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

            // A rotation is a move around the wheel and nothing else.
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

        // At the lightness it had, which says desaturating did not also darken it.
        CHECK(gray.x == doctest::Approx(RGB_to_HSL(rgb).z).epsilon(1e-3));

        // A turn of something with no hue is a no-op, whichever path the adjustment takes through it.
        const float3 turned = adjust_HSL(gray, 0.25f, 1.f, 0.f);
        CHECK(turned.x == doctest::Approx(gray.x).epsilon(1e-3));
        CHECK(turned.y == doctest::Approx(gray.y).epsilon(1e-3));
        CHECK(turned.z == doctest::Approx(gray.z).epsilon(1e-3));

        // Monotonic either side of where it started, which is what a slider needs and what an
        // implementation that clamps somewhere in the middle would lose.
        if (RGB_to_HSL(rgb).y > 1e-6f)
        {
            CHECK(hsl_spread(adjust_HSL(rgb, 0.f, 0.5f, 0.f)) < hsl_spread(rgb));
            CHECK(hsl_spread(adjust_HSL(rgb, 0.f, 1.5f, 0.f)) > hsl_spread(rgb));
        }
    }
}

TEST_CASE("the lightness control mixes toward black and white rather than washing the color out")
{
    // Photoshop's slider of that name: changing L directly desaturates on the way, so at the ends this has
    // to land exactly on black and white, and halfway exactly halfway.
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
    // These are shape functions, and what makes them usable is a handful of exact identities rather than
    // their formulas -- so those are what is checked.
    for (float P : {0.25f, 0.5f, 1.f, 2.f, 4.f})
    {
        CAPTURE(P);

        // Pinned at both ends and at the middle, whatever the shape.
        CHECK(gain_Perlin(0.f, P) == doctest::Approx(0.f));
        CHECK(gain_Perlin(0.5f, P) == doctest::Approx(0.5f));
        CHECK(gain_Perlin(1.f, P) == doctest::Approx(1.f));

        for (int i = 0; i <= 20; ++i)
        {
            const float t = float(i) / 20.f;
            CAPTURE(t);

            // An exponent of one is the identity, and the inverse exponent undoes it.
            CHECK(gain_Perlin(t, 1.f) == doctest::Approx(t));
            CHECK(gain_Perlin(gain_Perlin(t, P), 1.f / P) == doctest::Approx(t).epsilon(1e-4));

            // It never leaves [0,1], which is the whole reason for having it beside the straight line.
            CHECK(gain_Perlin(t, P) >= -1e-6f);
            CHECK(gain_Perlin(t, P) <= 1.f + 1e-6f);
        }
    }

    for (float a : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f})
    {
        CAPTURE(a);

        // Bias is defined by where it sends the midpoint, which is the parameter itself.
        CHECK(bias_Schlick(0.f, a) == doctest::Approx(0.f));
        CHECK(bias_Schlick(0.5f, a) == doctest::Approx(a));
        CHECK(bias_Schlick(1.f, a) == doctest::Approx(1.f));

        // Monotone, so it reorders nothing.
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
    // What the two controls mean, taken from the sliders rather than from the functions: contrast is an
    // angle, so that its ends are a flat line and a vertical one, and brightness slides the point the
    // curve pivots about.
    auto slope_of    = [](float c) { return float(std::tan(lerp(0.0, M_PI_2, c / 2.0 + 0.5))); };
    auto midpoint_of = [](float b) { return (1.f - b) / 2.f; };
    auto bias_of     = [](float b) { return (b + 1.f) / 2.f; };

    CHECK(slope_of(0.f) == doctest::Approx(1.f));  // 45 degrees: no change
    CHECK(slope_of(-1.f) == doctest::Approx(0.f)); // flat: everything to one level
    CHECK(slope_of(1.f) > 1e6f);                   // vertical: a hard threshold
    CHECK(midpoint_of(0.f) == doctest::Approx(0.5f));
    CHECK(bias_of(0.f) == doctest::Approx(0.5f));

    // Neither curve moves anything when both controls are centered.
    for (int i = 0; i <= 20; ++i)
    {
        const float v = float(i) / 20.f;
        CAPTURE(v);
        CHECK(brightness_contrast_linear(v, slope_of(0.f), midpoint_of(0.f)) == doctest::Approx(v));
        CHECK(brightness_contrast_nonlinear(v, slope_of(0.f), bias_of(0.f)) == doctest::Approx(v));
    }

    // Raising contrast pushes the ends apart about the midpoint, and both curves still meet at it.
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

    // The difference between them: the straight line runs out of [0,1] and is meant to, so that an HDR
    // sample keeps its relation to its neighbors; the s-curve approaches the ends without reaching them.
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

    // Brightness moves which input lands on the middle, in opposite directions for the two curves'
    // parameters but to the same effect: a positive setting lifts the picture.
    CHECK(brightness_contrast_linear(0.4f, 1.f, midpoint_of(0.2f)) > 0.4f);
    CHECK(brightness_contrast_nonlinear(0.4f, 1.f, bias_of(0.2f)) > 0.4f);
}

TEST_CASE("L*a*b* agrees with the values the standard defines it by")
{
    // Checked against the definition rather than against itself: a round trip would pass just as well
    // with both halves wrong in the same way.
    const float3 white = Lab_reference_white();

    // The reference white is the point L* is scaled to, and it is achromatic.
    const float3 w = XYZ_to_Lab(white, white);
    CHECK(w.x == doctest::Approx(100.f).epsilon(1e-4));
    CHECK(w.y == doctest::Approx(0.f).epsilon(1e-4));
    CHECK(w.z == doctest::Approx(0.f).epsilon(1e-4));

    // Black is the other end, and also achromatic.
    const float3 k = XYZ_to_Lab(float3{0.f, 0.f, 0.f}, white);
    CHECK(k.x == doctest::Approx(0.f).epsilon(1e-4));
    CHECK(k.y == doctest::Approx(0.f).epsilon(1e-4));
    CHECK(k.z == doctest::Approx(0.f).epsilon(1e-4));

    // Any neutral is achromatic whatever its level, since the three ratios to the white are equal.
    for (float y : {0.02f, 0.18f, 0.5f, 0.9f})
    {
        CAPTURE(y);
        const float3 gray = XYZ_to_Lab(white * y, white);
        CHECK(gray.y == doctest::Approx(0.f).epsilon(1e-3));
        CHECK(gray.z == doctest::Approx(0.f).epsilon(1e-3));
    }

    // The two published anchors of the lightness curve: mid gray at Y = 0.18 sits near L* = 49.5, and the
    // linear segment below Y = 216/24389 has slope kappa = 24389/27 in Y.
    CHECK(XYZ_to_Lab(white * 0.184187f, white).x == doctest::Approx(50.f).epsilon(1e-3));

    const float y_knee = 216.f / 24389.f;
    CHECK(XYZ_to_Lab(white * y_knee, white).x == doctest::Approx(8.f).epsilon(1e-3));
    const float y_small = 0.5f * y_knee;
    CHECK(XYZ_to_Lab(white * y_small, white).x == doctest::Approx((24389.f / 27.f) * y_small).epsilon(1e-3));

    // L* rises with luminance and nothing else does.
    float previous = -1.f;
    for (int i = 0; i <= 20; ++i)
    {
        const float3 lab = XYZ_to_Lab(white * (float(i) / 20.f), white);
        CHECK(lab.x > previous);
        previous = lab.x;
    }

    // a* is the green-red axis and b* the blue-yellow one: more X than the white asks for reads red, more
    // Z reads blue.
    CHECK(XYZ_to_Lab(float3{white.x * 1.2f, white.y, white.z}, white).y > 0.f);
    CHECK(XYZ_to_Lab(float3{white.x * 0.8f, white.y, white.z}, white).y < 0.f);
    CHECK(XYZ_to_Lab(float3{white.x, white.y, white.z * 1.2f}, white).z < 0.f);
    CHECK(XYZ_to_Lab(float3{white.x, white.y, white.z * 0.8f}, white).z > 0.f);
}

TEST_CASE("A color survives the trip through L*a*b* and back")
{
    const float3 white = Lab_reference_white();

    // Across the linear segment near black and the cube root above it, and out past the white, since an
    // HDR sample is not bounded by it.
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

    // And through the normalized form the editing controls use.
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

    // The normalized form puts black at zero, the white's lightness at one, and neutral chroma in the
    // middle -- which is what lets a tone curve be applied to it unchanged.
    const float3 n = normalize_Lab(float3{100.f, 0.f, 0.f});
    CHECK(n.x == doctest::Approx(1.f).epsilon(1e-4));
    CHECK(n.y == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(n.z == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(normalize_Lab(float3{0.f, 0.f, 0.f}).x == doctest::Approx(0.f).epsilon(1e-4));
}

TEST_CASE("sRGB primaries land where L*a*b* is documented to put them")
{
    // The whole path an edit takes -- linear sRGB through XYZ into L*a*b* -- against values published for
    // the sRGB primaries under D65. These catch a transposed matrix or a swapped axis, which the
    // achromatic checks above cannot see.
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

    // White through the same path, which ties the matrix and the reference white together.
    const float3 w = lab_of(float3{1.f, 1.f, 1.f});
    CHECK(w.x == doctest::Approx(100.f).epsilon(1e-3));
    CHECK(w.y == doctest::Approx(0.f).epsilon(2e-2));
    CHECK(w.z == doctest::Approx(0.f).epsilon(2e-2));
}

TEST_CASE("The hue strip is piecewise linear, counting where the display's range bends it")
{
    // What the hue/saturation dialog draws its strip from, and the reason it can draw it as a handful of
    // shaded quads instead of a sample per pixel.
    //
    // Two kinds of bend. The hexcone's six corners, slid along by the hue rotation. And, because the strip
    // has to show a color the display can reach, wherever clamping catches a component: raising the
    // saturation of an already-saturated hue sends components past 0 and 1, and clamping is not affine.
    // Testing the unclamped sweep alone says the corners are enough, which is how a boost in saturation
    // came to look like no change at all.
    auto raw = [](float t, float h_deg, float s_pct, float l_pct)
    { return adjust_HSL(HSL_to_RGB(float3{t, 1.f, 0.5f}), h_deg / 360.f, (s_pct + 100.f) / 100.f, l_pct / 100.f); };
    auto shown = [&](float t, float h, float sp, float lp)
    {
        const float3 c = raw(t, h, sp, lp);
        return float3{std::clamp(c.x, 0.f, 1.f), std::clamp(c.y, 0.f, 1.f), std::clamp(c.z, 0.f, 1.f)};
    };

    struct Setting
    {
        const char *what;
        float       h, s, l;
    };
    const Setting settings[] = {{"neutral", 0.f, 0.f, 0.f},
                                {"hue rotated within a sixth", 20.f, 0.f, 0.f},
                                {"hue rotated backwards", -120.f, 0.f, 0.f},
                                {"desaturated", 0.f, -60.f, 0.f},
                                {"oversaturated", 0.f, 80.f, 0.f},
                                {"lifted toward white", 0.f, 0.f, 40.f},
                                {"pushed toward black", 0.f, 0.f, -70.f},
                                {"all three at once", 37.f, -60.f, 40.f},
                                {"everything at once, out of gamut", 37.f, 90.f, 40.f}};

    for (const Setting &cfg : settings)
    {
        CAPTURE(std::string(cfg.what));

        std::vector<float> corners{0.f, 1.f};
        for (int k = 0; k < 6; ++k)
        {
            const float t = float(k) / 6.f - cfg.h / 360.f;
            corners.push_back(t - std::floor(t));
        }
        std::sort(corners.begin(), corners.end());

        // The corners, plus wherever a component crosses 0 or 1 between two of them.
        std::vector<float> bends = corners;
        for (size_t i = 0; i + 1 < corners.size(); ++i)
        {
            const float  t0 = corners[i], t1 = corners[i + 1];
            const float3 a = raw(t0, cfg.h, cfg.s, cfg.l), b = raw(t1, cfg.h, cfg.s, cfg.l);

            for (int c = 0; c < 3; ++c)
                for (float level : {0.f, 1.f})
                {
                    const float v0 = a[c], v1 = b[c];
                    if ((v0 < level) == (v1 < level) || v1 == v0)
                        continue;
                    bends.push_back(t0 + (level - v0) / (v1 - v0) * (t1 - t0));
                }
        }
        std::sort(bends.begin(), bends.end());

        // Few enough to be worth drawing this way rather than sampling.
        CAPTURE(bends.size());
        CHECK(bends.size() <= 32);

        for (int i = 0; i <= 600; ++i)
        {
            const float t = float(i) / 600.f;

            size_t seg = 0;
            while (seg + 2 < bends.size() && bends[seg + 1] < t) ++seg;

            const float t0 = bends[seg], t1 = bends[seg + 1];
            const float u = t1 > t0 ? (t - t0) / (t1 - t0) : 0.f;

            const float3 a     = shown(t0, cfg.h, cfg.s, cfg.l);
            const float3 b     = shown(t1, cfg.h, cfg.s, cfg.l);
            const float3 exact = shown(t, cfg.h, cfg.s, cfg.l);

            CAPTURE(t);
            CHECK(a.x + u * (b.x - a.x) == doctest::Approx(exact.x).epsilon(1e-3));
            CHECK(a.y + u * (b.y - a.y) == doctest::Approx(exact.y).epsilon(1e-3));
            CHECK(a.z + u * (b.z - a.z) == doctest::Approx(exact.z).epsilon(1e-3));
        }
    }
}

TEST_CASE("Raising the saturation changes the hue strip rather than leaving it alone")
{
    // The strip is drawn from fully saturated hues, so a boost pushes them out of the display's range and
    // clamping brings the ends straight back -- the corners come out identical either way. What does
    // change is in between: the ramp between two corners steepens and flattens off at both ends, which is
    // what more saturation looks like. Drawing only from the corners misses exactly that.
    auto shown = [](float t, float s_pct)
    {
        const float3 c = adjust_HSL(HSL_to_RGB(float3{t, 1.f, 0.5f}), 0.f, (s_pct + 100.f) / 100.f, 0.f);
        return float3{std::clamp(c.x, 0.f, 1.f), std::clamp(c.y, 0.f, 1.f), std::clamp(c.z, 0.f, 1.f)};
    };

    // The corners agree, which is why this needs looking at in between them at all.
    for (int k = 0; k <= 6; ++k)
    {
        const float t = float(k) / 6.f;
        CAPTURE(t);
        CHECK(shown(t, 0.f).y == doctest::Approx(shown(t, 80.f).y).epsilon(1e-4));
    }

    // In between, the boosted strip has reached its extreme where the neutral one is still climbing.
    float largest = 0.f;
    for (int i = 0; i <= 600; ++i)
    {
        const float t = float(i) / 600.f;
        largest       = std::max(largest, std::fabs(shown(t, 80.f).y - shown(t, 0.f).y));
    }
    CAPTURE(largest);
    CHECK(largest > 0.2f);
}
