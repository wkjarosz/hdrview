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
#include <vector>

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
    struct TransferCase
    {
        const char   *name;
        const Sample *samples;
        size_t        count;
        float (*f)(float);
        float rel;
        float abs = 1e-9f; ///< where a transfer function flattens toward zero, as the log ones do at their floor
    };
#define TRANSFER_CASE(table, fn, ...)                                                                                  \
    {                                                                                                                  \
        #table, table, std::size(table), fn, __VA_ARGS__                                                               \
    }
    // float against double: a few ulps through pow/log/exp, more where a transfer function is steep
    const TransferCase cases[] = {
        TRANSFER_CASE(sRGB_encode, shader::linear_to_sRGB, 1e-6f),
        TRANSFER_CASE(sRGB_decode, shader::sRGB_to_linear, 1e-6f),
        // H.273's sYCC: sRGB extended to negative values by odd symmetry, which is HDRView's convention
        TRANSFER_CASE(sYCC_encode, shader::linear_to_sRGB, 1e-6f),
        TRANSFER_CASE(sYCC_decode, shader::sRGB_to_linear, 1e-6f),
        TRANSFER_CASE(ITU_encode, shader::OETF_ITU, 1e-6f),
        TRANSFER_CASE(ITU_decode, shader::inverse_OETF_ITU, 1e-5f),
        TRANSFER_CASE(PQ_encode, shader::inverse_EOTF_BT2100_PQ, 1e-5f),
        TRANSFER_CASE(PQ_decode, shader::EOTF_BT2100_PQ, 1e-4f),
        TRANSFER_CASE(HLG_OETF, shader::OETF_BT2100_HLG, 1e-6f),
        TRANSFER_CASE(HLG_inverse_OETF, shader::inverse_OETF_BT2100_HLG, 1e-5f),
        TRANSFER_CASE(HLG_system_gamma, shader::HLG_system_gamma, 1e-6f),
        TRANSFER_CASE(ST240_encode, shader::OETF_ST240, 1e-6f),
        TRANSFER_CASE(ST240_decode, shader::EOTF_ST240, 1e-5f),
        TRANSFER_CASE(log100_encode, shader::OETF_log100, 1e-6f, 1e-6f),
        TRANSFER_CASE(log100_decode, shader::EOTF_log100, 1e-5f),
        TRANSFER_CASE(log100_sqrt10_encode, shader::OETF_log100_sqrt10, 1e-6f, 1e-6f),
        TRANSFER_CASE(log100_sqrt10_decode, shader::EOTF_log100_sqrt10, 1e-5f),
        TRANSFER_CASE(DCI_P3_decode, shader::EOTF_DCI_P3, 1e-5f),
        TRANSFER_CASE(DCI_P3_encode, shader::inverse_EOTF_DCI_P3, 1e-6f),
    };
#undef TRANSFER_CASE

    for (const TransferCase &c : cases)
    {
        INFO("transfer function = ", std::string(c.name));
        for (size_t i = 0; i < c.count; ++i)
        {
            const Sample &s = c.samples[i];
            INFO("in = ", s.in, ", want = ", s.out, ", got = ", c.f(s.in));
            CHECK(close(c.f(s.in), s.out, c.rel, c.abs));
        }
    }
}

TEST_CASE("to_linear and from_linear reach each TransferFunction's own function, with reference white at 1.0")
{
    // Each type against colour-science's values for the function it names, so a type routed to the wrong function,
    // or scaled by the wrong constant, is caught where a round trip through the pair passes. The ones in absolute
    // luminance put reference white at 1.0: BT.2408's 203 cd/m^2 for PQ and HLG, and ST 428-1's 48 cd/m^2 for
    // DCI-P3, spelled out here rather than taken from the constants, so these pin them. The three with no table
    // have closed forms: BT.709 decodes as BT.1886's gamma 2.4, as other viewers decode it, rather than through the
    // inverse of the camera's OETF.
    using namespace colour_reference;
    struct Case
    {
        std::vector<Sample> decode, encode; ///< in the table's own units, which `white` relates to HDRView's
        float               white = 1.f;
        float               rel = 1e-5f, abs = 1e-9f;
    };
    auto table       = [](const auto &t) { return std::vector<Sample>(std::begin(t), std::end(t)); };
    auto closed_form = [](auto f)
    {
        std::vector<Sample> out;
        for (const Sample &s : sRGB_decode) out.push_back({s.in, f(s.in)});
        return out;
    };
    auto inverted = [](std::vector<Sample> v)
    {
        for (Sample &s : v) std::swap(s.in, s.out);
        return v;
    };
    auto case_for = [&](TransferFunction tf) -> Case
    {
        switch (tf.type)
        {
        case TransferFunction::Unspecified:
        case TransferFunction::sRGB: return {table(sYCC_decode), table(sYCC_encode)};
        case TransferFunction::Linear:
        {
            const auto identity = closed_form([](float x) { return x; });
            return {identity, identity};
        }
        case TransferFunction::Gamma:
        {
            const auto decode = closed_form([g = tf.gamma](float x) { return std::pow(x, g); });
            return {decode, inverted(decode)};
        }
        case TransferFunction::ITU:
        {
            const auto decode = closed_form([](float x) { return std::pow(x, 2.4f); });
            return {decode, inverted(decode)};
        }
        case TransferFunction::BT2100_PQ: return {table(PQ_decode), table(PQ_encode), 203.f, 1e-4f};
        case TransferFunction::BT2100_HLG:
        {
            // the EOTF of a nominal 1000 cd/m^2 display, on grays, where the scalar form applies
            Case c{{}, {}, 203.f};
            for (const auto &s : HLG_EOTF)
                if (s.L_B == 0.f && s.L_W == 1000.f && s.in.x == s.in.y && s.in.y == s.in.z)
                    c.decode.push_back({s.in.x, s.out.x});
            c.encode = inverted(c.decode);
            return c;
        }
        case TransferFunction::ST240: return {table(ST240_decode), table(ST240_encode)};
        case TransferFunction::Log100: return {table(log100_decode), table(log100_encode), 1.f, 1e-5f, 1e-6f};
        case TransferFunction::Log100_Sqrt10:
            return {table(log100_sqrt10_decode), table(log100_sqrt10_encode), 1.f, 1e-5f, 1e-6f};
        // xvYCC: BT.709's OETF, extended to negative values by odd symmetry
        case TransferFunction::IEC61966_2_4: return {table(ITU_decode), table(ITU_encode)};
        case TransferFunction::DCI_P3: return {table(DCI_P3_decode), table(DCI_P3_encode), 48.f};
        default: FAIL("no reference for transfer function ", tf.type); return {};
        }
    };

    std::vector<TransferFunction> tfs;
    for (int t = TransferFunction::Unspecified; t < TransferFunction::Count; ++t)
        tfs.emplace_back(static_cast<TransferFunction::Type_>(t));
    for (float gamma : {1.8f, 2.6f}) tfs.emplace_back(TransferFunction::Gamma, gamma);

    for (const TransferFunction &tf : tfs)
    {
        INFO("transfer function = ", transfer_function_name(tf), " (gamma ", tf.gamma, ")");
        const Case c = case_for(tf);
        REQUIRE(!c.decode.empty());
        REQUIRE(!c.encode.empty());
        for (const Sample &s : c.decode)
        {
            INFO("decoding ", s.in, ": want ", s.out, ", got ", to_linear(s.in, tf) * c.white);
            CHECK(close(to_linear(s.in, tf) * c.white, s.out, c.rel, c.abs));
        }
        for (const Sample &s : c.encode)
        {
            INFO("encoding ", s.in, ": want ", s.out, ", got ", from_linear(s.in / c.white, tf));
            CHECK(close(from_linear(s.in / c.white, tf), s.out, c.rel, c.abs));
        }
    }

    // and on colors, which HLG decodes as a whole, its OOTF depending on the luminance
    for (const auto &s : HLG_EOTF)
        if (s.L_B == 0.f && s.L_W == 1000.f)
        {
            INFO("HLG color ", s.in);
            CHECK(close(to_linear(s.in, TransferFunction::BT2100_HLG) * 203.f, s.out, 1e-5f, 1e-6f));
            CHECK(close(from_linear(s.out / 203.f, TransferFunction::BT2100_HLG), s.in, 1e-5f, 1e-6f));
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
    // the white the editing controls' L*a*b* is relative to when an image names none is D65's
    for (const auto &s : colour_reference::white_points)
        if (s.white_point == WhitePoint_D65)
            CHECK(close(Lab_reference_white(), XYZ_from_xy(s.xy), 2e-4f));
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
                // sRGB's is also the one sRGB_to_XYZ() holds, from HDRView's own chromaticities
                if (s.gamut == ColorGamut_sRGB_BT709)
                    CHECK(close(element(sRGB_to_XYZ(), r, c), s.RGB_to_XYZ[3 * r + c], 2e-4f, 1e-6f));
            }

        // and default-constructed chromaticities are sRGB's
        if (s.gamut == ColorGamut_sRGB_BT709)
            CHECK(approx_equal(Chromaticities{}, Chromaticities{s.red, s.green, s.blue, s.white}, 2e-4f));
    }

    for (const auto &s : colour_reference::white_points)
    {
        INFO("white point = ", std::string(white_point_name(s.white_point)),
             fmt::format(": got {}, want {}", white_point(s.white_point), s.xy));
        // published to four decimals
        CHECK(approx_equal(white_point(s.white_point), s.xy, 1e-4f));
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
