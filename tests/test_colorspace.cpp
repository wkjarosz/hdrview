//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "common.h" // for blend_mode_names()

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
