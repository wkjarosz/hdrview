/** \file test_numeric_edge_cases.cpp
    \author Wojciech Jarosz

    Values outside [0,1], negatives from gamut conversion, and the NaN/Inf real EXRs contain.
*/

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"

#include <cmath>

TEST_CASE("Transfer functions mirror around the origin for negative input")
{
    // Gamut conversion produces negatives, and the convention (OpenColorIO's LIN_TO_PQ, colour-science's
    // signed power, our own linear_to_sRGB) is to mirror the curve around the origin so the sign survives.
    for (int t = TransferFunction::Unspecified; t < TransferFunction::Count; ++t)
    {
        TransferFunction tf{static_cast<TransferFunction::Type_>(t)};
        for (float x : {-0.5f, -10.f})
        {
            INFO("transfer function = ", transfer_function_name(tf), ", x = ", x);
            float encoded = from_linear(x, tf);
            REQUIRE(std::isfinite(encoded));

            // Log100 and its sqrt10 variant are specified on a bounded domain and clamp, losing the sign
            if (tf.type == TransferFunction::Log100 || tf.type == TransferFunction::Log100_Sqrt10)
                continue;

            CHECK(encoded <= 0.f);
            CHECK(to_linear(encoded, tf) == doctest::Approx(x).epsilon(1e-3));
        }
    }
}

TEST_CASE("Transfer functions round-trip above 1.0")
{
    for (int t = TransferFunction::Unspecified; t < TransferFunction::Count; ++t)
    {
        TransferFunction tf{static_cast<TransferFunction::Type_>(t)};
        for (float x : {2.f, 10.f, 100.f, 1000.f})
        {
            INFO("transfer function = ", transfer_function_name(tf), ", x = ", x);
            CHECK(to_linear(from_linear(x, tf), tf) == doctest::Approx(x).epsilon(1e-3));
        }
    }
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
            CHECK(axis_scale_inv(f, (AxisScale)s) == doctest::Approx(x).epsilon(1e-9));
        }
}
