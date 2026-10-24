//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "imageio/icc.h"
#include "imageio/image_loader.h"

#include "test_support.h"

#include "image.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace hdrview_test;

namespace
{

// The smallest byte sequence carrying an ICC `cicp` tag: a 128-byte header, a one-entry tag table, and the
// tag. LCMS refuses to open it as a profile, so the code points have to be readable from the bytes alone.
std::vector<uint8_t> icc_bytes_with_cicp(uint8_t cp, uint8_t tc, uint8_t mc, uint8_t fr)
{
    std::vector<uint8_t> v(128 + 4 + 12 + 12, 0);
    patch<uint32_t>(v, 0, (uint32_t)v.size(), Endian::Big); // profile size
    patch<uint32_t>(v, 128, 1, Endian::Big);                // one tag
    patch<uint32_t>(v, 132, 0x63696370u, Endian::Big);      // 'cicp'
    patch<uint32_t>(v, 136, 144, Endian::Big);              // tag offset
    patch<uint32_t>(v, 140, 12, Endian::Big);               // tag size
    patch<uint32_t>(v, 144, 0x63696370u, Endian::Big);      // tag type signature, repeated
    v[152] = cp;
    v[153] = tc;
    v[154] = mc;
    v[155] = fr;
    return v;
}

// 16-bit narrow ("video") range puts black at 16 and white at 235, scaled by the sample depth.
constexpr uint16_t k_narrow_black = 16 * 256;  // 4096
constexpr uint16_t k_narrow_white = 235 * 256; // 60160
constexpr uint16_t k_mid          = 128 * 256; // 32768

/// A 3x1, 16-bit, single-sample uncompressed TIFF holding `codes`, with `icc` embedded.
std::string gray16_tiff(const std::vector<uint16_t> &codes, const std::vector<uint8_t> &icc)
{
    std::vector<uint8_t> strip;
    for (uint16_t c : codes) put(strip, c);

    return tiff_bytes(Endian::Little,
                      {
                          {256, 4, 1, uint32_t(codes.size()), {}},  // ImageWidth
                          {257, 4, 1, 1, {}},                       // ImageLength
                          {258, 3, 1, 16, {}},                      // BitsPerSample
                          {259, 3, 1, 1, {}},                       // Compression: none
                          {262, 3, 1, 1, {}},                       // Photometric: BlackIsZero
                          {277, 3, 1, 1, {}},                       // SamplesPerPixel
                          {278, 4, 1, 1, {}},                       // RowsPerStrip
                          {339, 3, 1, 1, {}},                       // SampleFormat: unsigned int
                          {34675, 7, uint32_t(icc.size()), 0, icc}, // ICCProfile
                      },
                      strip);
}

} // namespace

// ICC.1:2022 lets a profile declare CICP code points directly. HDRView reads them from the profile bytes;
// LCMS only gained the tag in 2.16 and libjxl vendors 2.10.
TEST_CASE("An ICC cicp tag is read from the profile bytes, and decodes by its transfer function alone")
{
    struct Tag
    {
        int                     cp, tc;
        TransferFunction::Type_ tf;
        bool                    hdr;
        ColorGamut_             gamut;
    };
    const Tag tags[] = {
        {9, 16, TransferFunction::BT2100_PQ, true, ColorGamut_BT2020_2100},
        {9, 18, TransferFunction::BT2100_HLG, true, ColorGamut_BT2020_2100},
        {1, 13, TransferFunction::sRGB, false, ColorGamut_sRGB_BT709},
        {1, 8, TransferFunction::Linear, false, ColorGamut_sRGB_BT709},
    };
    constexpr int num = 33;

    for (const Tag &t : tags)
        for (int fr : {0, 1})
        {
            INFO("cicp ", t.cp, "-", t.tc, "-0-", fr);
            const auto       bytes = icc_bytes_with_cicp(uint8_t(t.cp), uint8_t(t.tc), 0, uint8_t(fr));
            const ICCProfile profile(bytes.data(), bytes.size());
            for (const CICPProfile &codes : {profile.cicp(), icc_cicp_tag(bytes.data(), bytes.size())})
            {
                REQUIRE(codes.valid());
                CHECK(codes.cp() == t.cp);
                CHECK(codes.tc() == t.tc);
                CHECK(codes.mc() == 0);
                CHECK(codes.fr() == fr);
                CHECK(codes.is_HDR() == t.hdr);
                CHECK(codes.transfer_function().type == t.tf);
            }

            // The tag is authoritative, and the only description of an HDR encoding a profile can hold: the ICC PCS
            // is normalized to media white, so transforming through the profile would clamp away everything above
            // diffuse white. The range is the loader's to apply when it dequantizes, so the ramp decodes the same
            // whichever range the tag declares.
            std::vector<float> pixels(num * 3);
            for (int i = 0; i < num * 3; ++i) pixels[i] = float(i / 3) / (num - 1);
            std::string    description;
            Chromaticities chr;
            REQUIRE(
                profile.linearize_pixels(pixels.data(), int3{num, 1, 3}, /*keep_primaries*/ true, &description, &chr));
            CHECK(description == profile.cicp().short_name());
            CHECK(approx_equal(chr, gamut_chromaticities(t.gamut)));
            for (int i = 0; i < num * 3; ++i)
            {
                const float want = to_linear(float(i / 3) / (num - 1), TransferFunction{t.tf});
                INFO("sample ", i, ": got ", pixels[i], ", want ", want);
                CHECK(pixels[i] == doctest::Approx(want).epsilon(1e-5));
            }
        }

    // and a profile without the tag reports no code points
    std::vector<uint8_t> v(132, 0);
    patch<uint32_t>(v, 0, (uint32_t)v.size(), Endian::Big);
    patch<uint32_t>(v, 128, 0, Endian::Big); // no tags
    CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    CHECK(!icc_cicp_tag(v.data(), v.size()).valid());
}

TEST_CASE("A CICP profile's name reads back as its codes, which its description names")
{
    // every code H.273 assigns each of the four fields, and some it leaves reserved: the label short_name() writes is
    // what a loader stores and the Colorspace panel parses back for its tooltip
    for (int cp = 0; cp <= 23; ++cp)
        for (int tc = 0; tc <= 19; ++tc)
            for (int mc = 0; mc <= 17; ++mc)
                for (int fr = 0; fr <= 1; ++fr)
                {
                    const CICPProfile written{std::array<int, 4>{{cp, tc, mc, fr}}};
                    const auto        read = CICPProfile::from_name(written.short_name());
                    REQUIRE(read);
                    if (read->cp() != cp || read->tc() != tc || read->mc() != mc || read->fr() != fr)
                        FAIL_CHECK(written.short_name(), " reads back as ", read->cp(), "-", read->tc(), "-",
                                   read->mc(), "-", read->fr());
                }

    const auto hlg = CICPProfile{std::array<int, 4>{{9, 18, 0, 1}}}.fields();
    CHECK(hlg[0].code == 9);
    CHECK(hlg[1].code == 18);
    CHECK(hlg[1].meaning == "HLG");
    CHECK(hlg[3].meaning == "Full");
    // unspecified primaries are named so, not as the BT.709 they stand in for
    CHECK(CICPProfile{std::array<int, 4>{{2, 13, 0, 1}}}.fields()[0].meaning == "Unspecified");
    CHECK_FALSE(CICPProfile::from_name("sRGB IEC61966-2.1"));
}

// The tag is parsed straight from untrusted bytes.
TEST_CASE("ICC cicp parsing rejects malformed profiles")
{
    auto good = icc_bytes_with_cicp(9, 16, 0, 1);

    SUBCASE("truncated before the tag table")
    {
        for (size_t n : {size_t(0), size_t(4), size_t(127), size_t(131)})
        {
            std::vector<uint8_t> v(good.begin(), good.begin() + (ptrdiff_t)n);
            CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
        }
    }

    SUBCASE("tag count larger than the buffer can hold")
    {
        auto v = good;
        patch<uint32_t>(v, 128, 0xFFFFFFFFu, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("tag offset and size point past the end")
    {
        auto v = good;
        patch<uint32_t>(v, 136, 0xFFFFFF00u, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());

        v = good;
        patch<uint32_t>(v, 140, 0xFFFFFFFFu, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("tag size too small to hold the code points")
    {
        auto v = good;
        patch<uint32_t>(v, 140, 11, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("tag type signature does not match the table entry")
    {
        auto v = good;
        patch<uint32_t>(v, 144, 0x64656164u, Endian::Big);
        CHECK(!ICCProfile(v.data(), v.size()).cicp().valid());
    }

    SUBCASE("a null buffer is handled")
    {
        CHECK(!ICCProfile(nullptr, 0).cicp().valid());
        CHECK(!icc_cicp_tag(nullptr, 0).valid());
    }
}

// The range flag lives only in the cicp tag; TIFF has no field of its own for it. Transfer characteristic 8
// is Linear, so these assertions see the dequantization alone.
TEST_CASE("A cicp tag declaring narrow video range is dequantized 16..235")
{
    const std::vector<uint16_t> codes{k_narrow_black, k_narrow_white, k_mid};

    SUBCASE("narrow range")
    {
        auto img = load_bytes(gray16_tiff(codes, icc_bytes_with_cicp(1, 8, 0, /*full_range*/ 0)), "narrow.tif");
        REQUIRE(img);
        const auto &ch = img->channels[0];
        REQUIRE(ch.size().x == 3);

        CHECK(ch(0, 0) == doctest::Approx(0.f).epsilon(1e-5));
        CHECK(ch(1, 0) == doctest::Approx(1.f).epsilon(1e-5));
        CHECK(ch(2, 0) == doctest::Approx((128.f - 16.f) / 219.f).epsilon(1e-5));
    }

    SUBCASE("full range, same samples")
    {
        auto img = load_bytes(gray16_tiff(codes, icc_bytes_with_cicp(1, 8, 0, /*full_range*/ 1)), "full.tif");
        REQUIRE(img);
        const auto &ch = img->channels[0];
        REQUIRE(ch.size().x == 3);

        // without the flag, black and white would sit at the ends of the full range
        CHECK(ch(0, 0) == doctest::Approx(k_narrow_black / 65535.f).epsilon(1e-5));
        CHECK(ch(1, 0) == doctest::Approx(k_narrow_white / 65535.f).epsilon(1e-5));
        CHECK(ch(2, 0) == doctest::Approx(k_mid / 65535.f).epsilon(1e-5));
    }

    SUBCASE("narrow range keeps excursions beyond black and white")
    {
        // codes outside 16..235 are legal and must survive as values outside [0,1]; HDR test patterns use
        // that headroom
        auto img = load_bytes(gray16_tiff({0, 65535}, icc_bytes_with_cicp(1, 8, 0, 0)), "excursions.tif");
        REQUIRE(img);
        const auto &ch = img->channels[0];
        CHECK(ch(0, 0) < 0.f);
        CHECK(ch(1, 0) > 1.f);
    }
}

TEST_CASE("A profile built for a transfer function reads back as that transfer function and those primaries")
{
    // Every transfer function, with gamma at an exponent no other one shares, as an RGB profile over BT.709 and a
    // wide gamut and as a gray one, read back through the path a loader takes. PQ and HLG read back through their
    // cicp tag, and through their tone curves as the SDR clip a reader that ignores the tag sees.
    std::vector<TransferFunction> tfs;
    for (int i = TransferFunction::Unspecified; i < TransferFunction::Count; ++i)
        if (i != TransferFunction::Gamma)
            tfs.push_back({(TransferFunction::Type_)i});
    tfs.push_back({TransferFunction::Gamma, 1.8f});

    const Chromaticities gamuts[] = {Chromaticities{}, gamut_chromaticities(ColorGamut_Display_P3_SMPTE432)};
    constexpr int        num      = 64;

    for (const auto &tf : tfs)
    {
        CAPTURE(transfer_function_name(tf));
        const bool hdr  = tf.type == TransferFunction::BT2100_PQ || tf.type == TransferFunction::BT2100_HLG;
        const bool lcms = ICCProfile::lcms_version() > 0;
        CHECK(ICCProfile::can_describe(tf) == (tf.type != TransferFunction::Unspecified && !hdr && lcms));
        CHECK(ICCProfile::can_label(tf) == (tf.type != TransferFunction::Unspecified && lcms));
        if (!ICCProfile::can_label(tf))
        {
            CHECK_FALSE(ICCProfile::RGB(Chromaticities{}, tf).valid());
            continue;
        }

        // a ramp of code values decoded by the transfer function, and that clipped to the white an ICC curve ends at
        std::vector<float> want(num), clipped(num);
        for (int i = 0; i < num; ++i)
        {
            want[i]    = to_linear(float(i) / (num - 1), tf);
            clipped[i] = std::clamp(want[i], 0.f, 1.f);
        }
        if (!hdr)
            want = clipped;
        const auto ramp = [&](int channels)
        {
            std::vector<float> pixels(num * channels);
            for (int i = 0; i < num * channels; ++i) pixels[i] = float(i / channels) / (num - 1);
            return pixels;
        };
        const auto worst_error = [&](const std::vector<float> &pixels, const std::vector<float> &expected, int channels)
        {
            float worst = 0.f;
            for (int i = 0; i < num * channels; ++i)
                worst = std::max(worst,
                                 std::abs(pixels[i] - expected[i / channels]) / std::max(1.f, expected[i / channels]));
            return worst;
        };

        // a built profile reports the code points it was tagged with, as its bytes read back do
        const auto same_codes = [](const CICPProfile &a, const CICPProfile &b)
        { return a.cp() == b.cp() && a.tc() == b.tc() && a.mc() == b.mc() && a.fr() == b.fr(); };

        for (const auto &chr : gamuts)
        {
            const ICCProfile built = ICCProfile::RGB(chr, tf);
            const auto       bytes = built.bytes();
            REQUIRE_FALSE(bytes.empty());
            ICCProfile read; // assigned, as a loader holding it for several frames does
            read = ICCProfile{bytes};
            REQUIRE(read.valid());
            CHECK(read.cicp().valid() == hdr);
            CHECK(same_codes(built.cicp(), read.cicp()));
            CHECK(read.description().find(transfer_function_name(tf)) != std::string::npos);

            Chromaticities got;
            REQUIRE(read.extract_chromaticities(&got));
            for (auto [g, w] : {std::pair{got.red, chr.red}, std::pair{got.green, chr.green},
                                std::pair{got.blue, chr.blue}, std::pair{got.white, chr.white}})
                CHECK(la::maxelem(la::abs(g - w)) <= 1e-3f);

            auto pixels = ramp(3);
            REQUIRE(read.linearize_pixels(pixels.data(), int3{num, 1, 3}, true));
            CHECK(worst_error(pixels, want, 3) <= 2e-3f);

            pixels = ramp(3);
            REQUIRE(ICCProfile::transform_pixels(pixels.data(), int3{num, 1, 3}, read, ICCProfile::linear_RGB(chr)));
            CHECK(worst_error(pixels, clipped, 3) <= 2e-3f);
        }

        const ICCProfile built_gray = ICCProfile::Gray(white_point(WhitePoint_D65), tf);
        ICCProfile       gray{built_gray.bytes()};
        REQUIRE(gray.is_Gray());
        CHECK(gray.cicp().valid() == hdr);
        CHECK(same_codes(built_gray.cicp(), gray.cicp()));
        auto pixels = ramp(1);
        REQUIRE(gray.linearize_pixels(pixels.data(), int3{num, 1, 1}, true));
        CHECK(worst_error(pixels, want, 1) <= 2e-3f);

        // and beside an alpha ramp, which stays as it was: a transform advancing one float per pixel would cover
        // only the first half of the buffer, reading alternating luminance and alpha as consecutive gray pixels
        std::vector<float> gray_alpha(num * 2), alpha(num);
        for (int i = 0; i < num; ++i)
        {
            alpha[i]              = 1.f - float(i) / (num - 1);
            gray_alpha[2 * i]     = float(i) / (num - 1);
            gray_alpha[2 * i + 1] = alpha[i];
        }
        REQUIRE(gray.linearize_pixels(gray_alpha.data(), int3{num, 1, 2}, /*keep_primaries*/ false));
        float worst_luminance = 0.f, worst_alpha = 0.f;
        for (int i = 0; i < num; ++i)
        {
            worst_luminance = std::max(worst_luminance, std::abs(gray_alpha[2 * i] - want[i]) / std::max(1.f, want[i]));
            worst_alpha     = std::max(worst_alpha, std::abs(gray_alpha[2 * i + 1] - alpha[i]));
        }
        CHECK(worst_luminance <= 2e-3f);
        CHECK(worst_alpha <= 1e-6f);
    }
}

#ifdef HDRVIEW_TEST_LIBJXL_DIR

// The Compact-ICC-Profiles set libjxl vendors writes each color space four ways, over ICC v2 and v4 and in
// the cut-down "micro" and "magic" forms, named <space>-<version>[-form].icc. The names are the oracle:
// whatever primaries one encoding yields, its siblings have to yield the same.
TEST_CASE("Profiles encoding the same color space agree on its primaries")
{
    namespace fs = std::filesystem;

    std::map<std::string, std::vector<std::pair<std::string, Chromaticities>>> by_space;
    int                                                                        read = 0;

    for (const auto &entry :
         fs::directory_iterator(std::string(HDRVIEW_TEST_LIBJXL_DIR) + "/external/Compact-ICC-Profiles/profiles"))
    {
        const auto path = entry.path();
        if (path.extension() != ".icc")
            continue;

        const std::string name = path.filename().string();
        CAPTURE(name);

        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        REQUIRE(bytes.size() > 128);
        ++read;

        // reading one must not throw, whatever it holds
        ICCProfile     profile{reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()};
        Chromaticities chr;
        if (!profile.valid() || !profile.extract_chromaticities(&chr))
            continue; // a gray or LUT-shaped profile carries no colorants to read

        by_space[name.substr(0, name.find("-v"))].emplace_back(name, chr);
    }
    CHECK(read >= 40);

    for (const auto &[space, profiles] : by_space)
    {
        CAPTURE(space);
        const auto &[first_name, want] = profiles.front();
        for (const auto &[name, got] : profiles)
        {
            CAPTURE(first_name);
            CAPTURE(name);
            CHECK(got.red.x == doctest::Approx(want.red.x).epsilon(1e-3));
            CHECK(got.red.y == doctest::Approx(want.red.y).epsilon(1e-3));
            CHECK(got.green.x == doctest::Approx(want.green.x).epsilon(1e-3));
            CHECK(got.green.y == doctest::Approx(want.green.y).epsilon(1e-3));
            CHECK(got.blue.x == doctest::Approx(want.blue.x).epsilon(1e-3));
            CHECK(got.blue.y == doctest::Approx(want.blue.y).epsilon(1e-3));
            CHECK(got.white.x == doctest::Approx(want.white.x).epsilon(1e-3));
            CHECK(got.white.y == doctest::Approx(want.white.y).epsilon(1e-3));
        }
    }

    // Agreement alone is self-consistent: read every colorant out of the wrong tag and the siblings still
    // match each other. sRGB's primaries are published, so one group is anchored to them.
    const auto srgb = by_space.find("sRGB");
    REQUIRE(srgb != by_space.end());
    for (const auto &[name, got] : srgb->second)
    {
        CAPTURE(name);
        CHECK(got.red.x == doctest::Approx(0.64f).epsilon(1e-3));
        CHECK(got.red.y == doctest::Approx(0.33f).epsilon(1e-3));
        CHECK(got.green.x == doctest::Approx(0.30f).epsilon(1e-3));
        CHECK(got.green.y == doctest::Approx(0.60f).epsilon(1e-3));
        CHECK(got.blue.x == doctest::Approx(0.15f).epsilon(1e-3));
        CHECK(got.blue.y == doctest::Approx(0.06f).epsilon(1e-3));
        CHECK(got.white.x == doctest::Approx(0.3127f).epsilon(1e-3));
        CHECK(got.white.y == doctest::Approx(0.3290f).epsilon(1e-3));
    }
}

#endif // HDRVIEW_TEST_LIBJXL_DIR

#ifdef HDRVIEW_TEST_LCMS_TESTBED_DIR

// lcms's own testbed profiles: eight real ones, RGB and CMYK over ICC v2 and v4, beside the three it keeps
// for being broken. Every profile HDRView parses came embedded in a file it did not write, and every loader
// spends it the same way, asking what color space it is and falling back when the answer is none, so a
// profile that cannot be used has to answer that way rather than crash or claim a space it does not have.
TEST_CASE("Profiles lcms cannot use are not mistaken for ones it can")
{
    namespace fs = std::filesystem;

    int usable = 0, cmyk = 0, broken = 0;
    for (const auto &entry : fs::directory_iterator(HDRVIEW_TEST_LCMS_TESTBED_DIR))
    {
        const auto path = entry.path();
        if (path.extension() != ".icc")
            continue;

        const std::string name = path.filename().string();
        CAPTURE(name);

        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

        // constructing from bytes must not throw, whatever they hold: the loaders do this inside a decode,
        // and one exception would lose an image whose pixels are perfectly good
        ICCProfile profile;
        REQUIRE_NOTHROW(profile = ICCProfile{reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()});

        // being two of these at once would send a loader down two paths
        const int spaces = int(profile.is_RGB()) + int(profile.is_Gray()) + int(profile.is_CMYK());
        CHECK(spaces <= 1);

        // lcms refuses two of its bad files outright and opens the third, whose color space is a malformed
        // signature; either way no loader can use it, and the name says which those are
        if (name.rfind("bad", 0) == 0 || name == "toosmall.icc")
        {
            ++broken;
            CHECK(spaces == 0);
        }

        if (spaces == 0)
        {
            // an unusable profile answers every accessor without reaching lcms, and leaves the pixels of a
            // caller that ignores the return value untouched rather than partly transformed
            Chromaticities chr;
            CHECK_FALSE(profile.extract_chromaticities(&chr));
            CHECK_FALSE(profile.linearized_profile().valid());

            const std::array<float, 8> before{0.1f, 0.2f, 0.3f, 1.f, 0.4f, 0.5f, 0.6f, 1.f};
            auto                       pixels = before;
            CHECK_FALSE(profile.linearize_pixels(pixels.data(), int3{2, 1, 4}));
            CHECK(pixels == before);
            continue;
        }

        ++usable;
        CHECK_FALSE(profile.description().empty());

        // CMYK has no colorants of its own to read; its conversion is the next test's
        if (profile.is_CMYK())
        {
            ++cmyk;
            continue;
        }

        // linearized_profile() is the keep_primaries path every loader takes: it rebuilds the profile with
        // linear curves and the same colorants, so reading those back has to return what it was handed
        Chromaticities want;
        REQUIRE(profile.extract_chromaticities(&want));
        auto linear = profile.linearized_profile();
        REQUIRE(linear.valid());

        Chromaticities got;
        REQUIRE(linear.extract_chromaticities(&got));
        CHECK(got.red.x == doctest::Approx(want.red.x).epsilon(1e-3));
        CHECK(got.red.y == doctest::Approx(want.red.y).epsilon(1e-3));
        CHECK(got.green.x == doctest::Approx(want.green.x).epsilon(1e-3));
        CHECK(got.green.y == doctest::Approx(want.green.y).epsilon(1e-3));
        CHECK(got.blue.x == doctest::Approx(want.blue.x).epsilon(1e-3));
        CHECK(got.blue.y == doctest::Approx(want.blue.y).epsilon(1e-3));
        CHECK(got.white.x == doctest::Approx(want.white.x).epsilon(1e-3));
        CHECK(got.white.y == doctest::Approx(want.white.y).epsilon(1e-3));
    }

    CHECK(broken == 3);
    CHECK(usable >= 8);
    CHECK(cmyk >= 1);
}

#endif // HDRVIEW_TEST_LCMS_TESTBED_DIR

#if HDRVIEW_ENABLE_LCMS2


#ifdef HDRVIEW_TEST_LCMS_TESTBED_DIR

TEST_CASE("a CMYK ICC profile converts ink to color, and only inverts it when told to")
{
    // lcms's own CMYK printer profile. It says of itself that it is not suitable for real use, which is
    // beside the point: its color space is CMYK, and that is the property the conversion turns on.
    std::ifstream in(std::string(HDRVIEW_TEST_LCMS_TESTBED_DIR) + "/test1.icc", std::ios::binary);
    REQUIRE(in.good());
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    ICCProfile        profile{reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()};
    REQUIRE(profile.valid());
    REQUIRE(profile.is_CMYK());

    // bare paper and a solid hit of black, as a file storing ink directly writes them
    const auto ink = [] { return std::vector<float>{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f}; };

    // A CMYK profile has no primaries of its own to keep, so the default has to convert anyway rather than
    // asking for colorants that are not there and giving up.
    for (bool keep_primaries : {true, false})
    {
        CAPTURE(keep_primaries);
        auto pixels = ink();
        REQUIRE(profile.linearize_pixels(pixels.data(), int3{2, 1, 4}, keep_primaries, nullptr, nullptr,
                                         /*cmyk_is_inverted*/ false));

        for (int c = 0; c < 3; ++c)
        {
            CAPTURE(c);
            CHECK(pixels[c] > 0.5f);     // paper
            CHECK(pixels[4 + c] < 0.2f); // solid black
        }
        CHECK(pixels[3] == doctest::Approx(1.f)); // the conversion writes an opaque alpha
    }

    // Adobe's JPEGs store the same ink inverted, and nothing in the profile says which convention a file
    // used, so the caller states it. Bare paper is what separates the two: read the wrong way up it is not
    // an absence of ink but a full hit of every one.
    auto inverted = ink();
    REQUIRE(profile.linearize_pixels(inverted.data(), int3{2, 1, 4}, /*keep_primaries*/ false, nullptr, nullptr,
                                     /*cmyk_is_inverted*/ true));
    for (int c = 0; c < 3; ++c)
    {
        CAPTURE(c);
        CHECK(inverted[c] < 0.2f);
    }
}

#endif // HDRVIEW_TEST_LCMS_TESTBED_DIR

#endif // HDRVIEW_ENABLE_LCMS2
