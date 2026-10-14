//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// HDRView's color code against colour-science, an independent implementation of the same standards. The values
// are generated offline into colour_reference.h by tests/reference/generate_colour_reference.py, so these tests
// need no Python.

#include <doctest/doctest.h>

#include "colorspace.h"
#include "colour_reference.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace
{

/// Whether \p got is within \p rel of \p want relative to their magnitude, so tiny values are held as tightly.
bool close(float got, float want, float rel, float abs = 1e-9f)
{ return std::abs(got - want) <= rel * std::max(std::abs(got), std::abs(want)) + abs; }

bool close(float3 got, float3 want, float rel, float abs = 1e-9f)
{ return close(got.x, want.x, rel, abs) && close(got.y, want.y, rel, abs) && close(got.z, want.z, rel, abs); }

/// Element (row, col) of a linalg matrix, which stores columns.
float element(const float3x3 &M, int row, int col) { return M[col][row]; }

} // namespace

TEST_CASE("every transfer function agrees with colour-science")
{
    using namespace colour_reference;
    struct Curve
    {
        const char   *name;
        const Sample *samples;
        size_t        count;
        float (*f)(float);
        float rel;
        float abs = 1e-9f; ///< where a curve flattens toward zero, as the log curves do at their floor
    };
#define CURVE(table, fn, ...)                                                                                          \
    {                                                                                                                  \
        #table, table, std::size(table), fn, __VA_ARGS__                                                               \
    }
    // float against double: a few ulps through pow/log/exp, more where a curve is steep
    const Curve curves[] = {
        CURVE(sRGB_encode, shader::linear_to_sRGB, 1e-6f),
        CURVE(sRGB_decode, shader::sRGB_to_linear, 1e-6f),
        // H.273's sYCC: sRGB extended to negative values by odd symmetry, which is HDRView's convention
        CURVE(sYCC_encode, shader::linear_to_sRGB, 1e-6f),
        CURVE(sYCC_decode, shader::sRGB_to_linear, 1e-6f),
        CURVE(ITU_encode, shader::OETF_ITU, 1e-6f),
        CURVE(ITU_decode, shader::inverse_OETF_ITU, 1e-5f),
        CURVE(PQ_encode, shader::inverse_EOTF_BT2100_PQ, 1e-5f),
        CURVE(PQ_decode, shader::EOTF_BT2100_PQ, 1e-4f),
        CURVE(HLG_OETF, shader::OETF_BT2100_HLG, 1e-6f),
        CURVE(HLG_inverse_OETF, shader::inverse_OETF_BT2100_HLG, 1e-5f),
        CURVE(HLG_system_gamma, shader::HLG_system_gamma, 1e-6f),
        CURVE(ST240_encode, shader::OETF_ST240, 1e-6f),
        CURVE(ST240_decode, shader::EOTF_ST240, 1e-5f),
        CURVE(log100_encode, shader::OETF_log100, 1e-6f, 1e-6f),
        CURVE(log100_decode, shader::EOTF_log100, 1e-5f),
        CURVE(log100_sqrt10_encode, shader::OETF_log100_sqrt10, 1e-6f, 1e-6f),
        CURVE(log100_sqrt10_decode, shader::EOTF_log100_sqrt10, 1e-5f),
        CURVE(DCI_P3_decode, shader::EOTF_DCI_P3, 1e-5f),
        CURVE(DCI_P3_encode, shader::inverse_EOTF_DCI_P3, 1e-6f),
    };
#undef CURVE

    for (const Curve &c : curves)
    {
        INFO("curve = ", std::string(c.name));
        for (size_t i = 0; i < c.count; ++i)
        {
            const Sample &s = c.samples[i];
            INFO("in = ", s.in, ", want = ", s.out, ", got = ", c.f(s.in));
            CHECK(close(c.f(s.in), s.out, c.rel, c.abs));
        }
    }
}

TEST_CASE("the HLG EOTF agrees with colour-science on colors, and on grays in its scalar form")
{
    for (const auto &s : colour_reference::HLG_EOTF)
    {
        INFO("L_B = ", s.L_B, ", L_W = ", s.L_W, ", in = ", s.in);
        CHECK(close(shader::EOTF_BT2100_HLG(s.in, s.L_B, s.L_W), s.out, 1e-5f, 1e-6f));
        // the scalar form takes the channel as the luminance, which a gray color's luminance is
        if (s.in.x == s.in.y && s.in.y == s.in.z)
            CHECK(close(shader::EOTF_BT2100_HLG(s.in.x, s.L_B, s.L_W), s.out.x, 1e-5f, 1e-6f));
    }
}

TEST_CASE("L*a*b* and HSL agree with colour-science")
{
    for (const auto &s : colour_reference::Lab)
    {
        INFO("white = ", s.white_xy, ", XYZ = ", s.XYZ);
        CHECK(close(XYZ_to_Lab(s.XYZ, XYZ_from_xy(s.white_xy)), s.Lab, 1e-5f, 1e-4f));
    }
    for (const auto &s : colour_reference::HSL)
    {
        INFO("RGB = ", s.in);
        CHECK(close(RGB_to_HSL(s.in), s.out, 1e-5f, 1e-6f));
    }
}

TEST_CASE("the named gamuts and white points, and the matrices they define, agree with colour-science")
{
    for (const auto &s : colour_reference::gamuts)
    {
        const Chromaticities chr = gamut_chromaticities(s.gamut);
        INFO("gamut = ", std::string(color_gamut_name(s.gamut)),
             fmt::format(": got {} {} {} {}, want {} {} {} {}", chr.red, chr.green, chr.blue, chr.white, s.red, s.green,
                         s.blue, s.white));
        // published to three to five decimals: H.273 gives illuminant C as (0.310, 0.316), where HDRView
        // has the CIE's (0.31006, 0.31616)
        CHECK(approx_equal(chr.red, s.red, 2e-4f));
        CHECK(approx_equal(chr.green, s.green, 2e-4f));
        CHECK(approx_equal(chr.blue, s.blue, 2e-4f));
        CHECK(approx_equal(chr.white, s.white, 2e-4f));

        // from colour-science's own chromaticities, so the matrix is checked apart from the constants
        const float3x3 M = RGB_to_XYZ(Chromaticities{s.red, s.green, s.blue, s.white}, 1.f);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
            {
                INFO("element (", r, ", ", c, ")");
                CHECK(close(element(M, r, c), s.RGB_to_XYZ[3 * r + c], 1e-5f, 1e-6f));
            }
    }

    for (const auto &s : colour_reference::white_points)
    {
        INFO("white point = ", std::string(white_point_name(s.white_point)),
             fmt::format(": got {}, want {}", white_point(s.white_point), s.xy));
        // HDRView's fluorescent illuminants come from a five-decimal table, colour-science's from CIE 15's
        // four-decimal one, and the two differ by up to 2.3e-4
        const bool fluorescent = s.white_point >= WhitePoint_F1 && s.white_point <= WhitePoint_F12;
        CHECK(approx_equal(white_point(s.white_point), s.xy, fluorescent ? 2.5e-4f : 1e-4f));
    }
}

TEST_CASE("color_conversion_matrix agrees with colour-science for every adaptation method")
{
    auto chromaticities = [](ColorGamut_ gamut)
    {
        for (const auto &s : colour_reference::gamuts)
            if (s.gamut == gamut)
                return Chromaticities{s.red, s.green, s.blue, s.white};
        FAIL("no reference chromaticities for gamut ", gamut);
        return Chromaticities{};
    };

    for (const auto &s : colour_reference::conversions)
    {
        INFO("from ", std::string(color_gamut_name(s.src)), " to ", std::string(color_gamut_name(s.dst)), " with ",
             std::string(adaptation_method_name(s.method)));
        float3x3 M;
        color_conversion_matrix(M, chromaticities(s.src), chromaticities(s.dst), s.method);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
            {
                INFO("element (", r, ", ", c, "): got ", element(M, r, c), ", want ", s.M[3 * r + c]);
                CHECK(close(element(M, r, c), s.M[3 * r + c], 1e-4f, 1e-5f));
            }
    }
}

TEST_CASE("color temperatures map to the chromaticities colour-science gives them")
{
    for (const auto &s : colour_reference::Kang2002)
    {
        INFO("CCT = ", s.CCT);
        CHECK(approx_equal(Kelvin_to_xy(s.CCT), s.xy, 2e-6f));
    }
    for (const auto &s : colour_reference::CIE_daylight)
    {
        INFO("CCT = ", s.CCT, fmt::format(": got {}, want {}", daylight_to_xy(s.CCT), s.xy));
        CHECK(approx_equal(daylight_to_xy(s.CCT), s.xy, 2e-6f));
    }
}
