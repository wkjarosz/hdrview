//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"

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
