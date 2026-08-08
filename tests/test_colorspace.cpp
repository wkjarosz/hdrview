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
