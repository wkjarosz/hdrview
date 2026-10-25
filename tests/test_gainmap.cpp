//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// Gain-map tests. The synthetic cases here always run; the ones against real captures are opt-in,
// since the files are far too large to vendor. Point these at a checkout of HDRView's test-image
// corpus to run them:
//
//   HDRVIEW_TEST_APPLE_HEIC       OpenImageIO-images-main/heif/greyhounds-looking-for-a-table.heic
//   HDRVIEW_TEST_JXL_GAINMAP_DIR  Gain_Map_Sample_Photos
//   HDRVIEW_TEST_GAINMAP_JPEG     Gain_Map_Sample_Photos/samples_jpeg/01.jpg          (ISO binary)
//                                 greg benz photography/DSC0529-...benz8GainMap.jpg   (3-channel XMP)
//                                 Ultra_HDR_Samples-main/Originals/..._01.jpg         (also UltraHDR)
//                                 iphone-gainmap-jpeg/IMG_0825.jpeg                   (Apple in JPEG)

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/gainmap.h"
#include "imageio/hdr_metadata.h"
#include "imageio/heif.h"
#include "imageio/image_loader.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/save_options.h"
#include "imageio/uhdr.h"

#include "test_apple.h"
#include "test_support.h"

using namespace hdrview_test;

#if HDRVIEW_ENABLE_LIBHEIF
#include <libheif/heif.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

namespace
{

int channel_index(const Image &img, const std::string &name)
{
    for (int i = 0; i < (int)img.channels.size(); ++i)
        if (img.channels[i].name == name)
            return i;
    return -1;
}

/// An RGBA image whose four channels each ramp their own way, so a step that mixes them up or skips one shows.
ImagePtr make_ramp_image(int2 size)
{
    auto img = std::make_shared<Image>(size, 4);
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            img->channels[0](x, y) = 0.1f + 0.02f * x;
            img->channels[1](x, y) = 0.2f + 0.03f * y;
            img->channels[2](x, y) = 0.05f + 0.01f * (x + y);
            img->channels[3](x, y) = float(x + y) / float(size.x + size.y);
        }
    return img;
}

/// The value the gain maps below hold at sample (i, j), linear in both.
float map_ramp(float2 p) { return 0.2f + 0.15f * p.x + 0.2f * p.y; }

/// A monochrome map of \p size whose sample at (i, j) is \p encode of map_ramp(i, j).
GainmapImage make_ramp_gainmap(int2 size, const std::function<float(float)> &encode)
{
    GainmapImage gm;
    gm.size     = size;
    gm.channels = 1;
    for (int j = 0; j < size.y; ++j)
        for (int i = 0; i < size.x; ++i) gm.pixels.push_back(encode(map_ramp(float2{float(i), float(j)})));
    return gm;
}

/// Whether \p a holds exactly \p b's samples.
bool same_data(const Array2Df &a, const Array2Df &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.num_elements(); ++i)
        if (a(i) != b(i))
            return false;
    return true;
}

/// Whether \p a has exactly \p b's channels, by name and sample.
bool same_channels(const Image &a, const Image &b)
{
    if (a.channels.size() != b.channels.size())
        return false;
    for (size_t c = 0; c < a.channels.size(); ++c)
        if (a.channels[c].name != b.channels[c].name || !same_data(a.channels[c], b.channels[c]))
            return false;
    return true;
}

/// The ISO maps below: log2 gains from -0.5 to 2.5, under a gamma, with offsets on both sides.
IsoGainmapParams iso_params(float base_headroom, float alternate_headroom)
{
    IsoGainmapParams p;
    p.min                = float3{-0.5f};
    p.max                = float3{2.5f};
    p.gamma              = float3{0.5f};
    p.base_offset        = float3{0.125f};
    p.alternate_offset   = float3{0.0625f};
    p.base_headroom      = base_headroom;
    p.alternate_headroom = alternate_headroom;
    return p;
}

} // namespace

TEST_CASE("Apple gain-map strength follows the published piecewise fit")
{
    // The branch is chosen by whether the headroom field reaches 1.0, and within it by whether the gain field is past
    // the 0.01 knee. Pinned at the intercepts, the fit's two lines meet at the knee, and on each side of headroom 1
    // more gain asks for less reconstruction, as the fit's slopes say.
    CHECK(AppleGainmapParams{0.5f, 0.f}.stops() == doctest::Approx(1.8f));
    CHECK(AppleGainmapParams{1.5f, 0.f}.stops() == doctest::Approx(3.f));

    for (float headroom : {0.f, 0.5f, 0.999f, 1.f, 1.5f, 4.f})
    {
        CAPTURE(headroom);
        auto stops = [&](float gain) { return AppleGainmapParams{headroom, gain}.stops(); };

        const float knee = 0.01f, h = 1e-5f;
        CHECK(stops(knee - h) == doctest::Approx(stops(knee + h)).epsilon(1e-3));

        float previous = stops(0.f);
        for (float gain = 0.f; gain <= 8.f; gain += gain < 0.05f ? 1e-3f : 0.05f)
        {
            CAPTURE(gain);
            CHECK(stops(gain) <= previous + 1e-6f);
            previous = stops(gain);
        }
    }

    // and without a maker note, the weakest reconstruction Apple's software falls back to
    CHECK(AppleGainmapParams{}.stops() == doctest::Approx(0.793f));
}

TEST_CASE("Every gain map keeps what the file stores beside what it builds, and leaves alone what it cannot use")
{
    // An Apple map, and ISO maps brightening the base and darkening it, each stored at a quarter of the base's
    // resolution as a ramp linear in both directions, which center-aligned bilinear resampling reproduces wherever it
    // has samples either side. Apple's map is sRGB-encoded and an ISO map gamma-encoded log2 gain, both undone before
    // the resize, so the appended group holds the linear gain (Apple) or log2 gain (ISO) of the ramp.
    struct Format
    {
        const char                 *name;
        float                       base_headroom, alternate_headroom; ///< the targets that show each rendition
        GainmapImage                map;
        std::function<float(float)> gain_at; ///< of map_ramp()'s value
        std::function<void(Image &, const GainmapImage &, float target, bool keep)> apply;
    };
    const IsoGainmapParams up = iso_params(0.f, 2.f), down = iso_params(2.5f, 0.f);
    auto                   iso_gain   = [&](float r) { return up.min.x + (up.max.x - up.min.x) * r; };
    auto                   iso_encode = [&](float r) { return std::pow(r, up.gamma.x); };
    const int2             map_size{4, 2};
    const Format           formats[] = {
        {"Apple", 0.f, k_full_gainmap_headroom,
         make_ramp_gainmap(map_size, [](float r) { return float(linear_to_sRGB(r)); }), [](float r) { return r; },
         [](Image &img, const GainmapImage &gm, float target, bool keep)
         { apply_apple_gainmap(img, gm, AppleGainmapParams{0.5f, 0.f}, target, keep); }},
        {"ISO, base SDR", up.base_headroom, up.alternate_headroom, make_ramp_gainmap(map_size, iso_encode), iso_gain,
         [&](Image &img, const GainmapImage &gm, float target, bool keep)
         { apply_iso_gainmap(img, gm, up, target, keep); }},
        {"ISO, base HDR", down.base_headroom, down.alternate_headroom, make_ramp_gainmap(map_size, iso_encode),
         iso_gain, [&](Image &img, const GainmapImage &gm, float target, bool keep)
         { apply_iso_gainmap(img, gm, down, target, keep); }},
    };

    const int2 size{16, 8};
    const auto original = make_ramp_image(size);
    for (const Format &f : formats)
    {
        CAPTURE(std::string(f.name));

        // a malformed map, empty or claiming more samples than it holds, leaves the image as it was
        GainmapImage short_map;
        short_map.size     = int2{64, 64};
        short_map.channels = 1;
        short_map.pixels.assign(4, 1.f);
        for (const GainmapImage &bad : {GainmapImage{}, short_map})
        {
            auto img = make_ramp_image(size);
            f.apply(*img, bad, k_full_gainmap_headroom, true);
            CHECK(same_channels(*img, *original));
        }

        // at the base's own headroom nothing changes, and the map is there to look at, at the image's size
        auto  at_base_ptr = make_ramp_image(size);
        auto &at_base     = *at_base_ptr;
        f.apply(at_base, f.map, f.base_headroom, true);
        for (int c = 0; c < 4; ++c) CHECK(same_data(at_base.channels[c], original->channels[c]));
        const int gm = channel_index(at_base, "gainmap.Y");
        REQUIRE(gm >= 0);
        CHECK(at_base.channels[gm].size() == size);
        float worst = 0.f;
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                const float2 p{(x + 0.5f) * map_size.x / size.x - 0.5f, (y + 0.5f) * map_size.y / size.y - 0.5f};
                if (p.x >= 0.f && p.y >= 0.f && p.x <= map_size.x - 1.f && p.y <= map_size.y - 1.f)
                    worst = std::max(worst, std::abs(at_base.channels[gm](x, y) - f.gain_at(map_ramp(p))));
            }
        CHECK(worst < 1e-5f);

        // at the other rendition's, the base group holds the rendition as stored, color only, and with keep off only
        // the reconstruction remains, the same either way; alpha is never touched
        auto  kept_ptr = make_ramp_image(size), alone_ptr = make_ramp_image(size);
        auto &kept = *kept_ptr, &alone = *alone_ptr;
        f.apply(kept, f.map, f.alternate_headroom, true);
        f.apply(alone, f.map, f.alternate_headroom, false);
        CHECK(channel_index(kept, "base.A") < 0);
        REQUIRE(alone.channels.size() == original->channels.size());
        for (int c = 0; c < 4; ++c)
        {
            CAPTURE(c);
            const std::string name = original->channels[c].name;
            CHECK(alone.channels[c].name == name);
            CHECK(same_data(alone.channels[c], kept.channels[c]));
            if (name == "A")
                CHECK(same_data(kept.channels[c], original->channels[c]));
            else
            {
                const int b = channel_index(kept, "base." + name);
                REQUIRE(b >= 0);
                CHECK(same_data(kept.channels[b], original->channels[c]));
                CHECK_FALSE(same_data(kept.channels[c], original->channels[c]));
            }
        }
    }
}

TEST_CASE("An Apple gain map scales the base by the headroom it asks for, capped at the target")
{
    // the gain is 1 + (2^stops - 1) g for the map's linear value g, with stops held to the target's
    const int2 size{16, 8};
    const auto               original = make_ramp_image(size);
    const auto               map      = make_ramp_gainmap(int2{4, 2}, [](float r) { return float(linear_to_sRGB(r)); });
    const AppleGainmapParams params{0.5f, 0.f};
    REQUIRE(params.stops() == doctest::Approx(1.8f));

    for (float target : {0.f, 0.5f, 1.f, 1.8f, 3.f, k_full_gainmap_headroom})
    {
        CAPTURE(target);
        auto  img_ptr = make_ramp_image(size);
        auto &img     = *img_ptr;
        apply_apple_gainmap(img, map, params, target, true);
        const int g = channel_index(img, "gainmap.Y");
        REQUIRE(g >= 0);
        const float headroom = std::exp2(std::min(1.8f, target));
        float       worst    = 0.f;
        for (int c = 0; c < 4; ++c)
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                {
                    const float gain = c == 3 ? 1.f : 1.f + (headroom - 1.f) * img.channels[g](x, y);
                    worst =
                        std::max(worst, std::abs(img.channels[c](x, y) / (original->channels[c](x, y) * gain) - 1.f));
                }
        CHECK(worst < 1e-5f);
    }
}

TEST_CASE("An ISO gain map applies its log2 gain in proportion to how far the target is toward the alternate")
{
    // (base + base offset) 2^(gain w) - alternate offset, where w runs linearly from 0 at the base's headroom to 1 at
    // the alternate's, held there past it, and is negative when the alternate is the darker one. A weight of 0 shows
    // the base as stored, without the offsets, and a map whose two renditions want the same headroom does nothing.
    struct Case
    {
        IsoGainmapParams params;
        float            target, weight;
    };
    const IsoGainmapParams up = iso_params(0.f, 2.f), down = iso_params(2.5f, 0.f), same = iso_params(1.f, 1.f);
    const Case             cases[] = {
        {up, 0.f, 0.f},
        {up, 0.5f, 0.25f},
        {up, 1.f, 0.5f},
        {up, 2.f, 1.f},
        {up, 3.f, 1.f},
        {up, k_full_gainmap_headroom, 1.f},
        {down, k_full_gainmap_headroom, 0.f},
        {down, 2.5f, 0.f},
        {down, 1.25f, -0.5f},
        {down, 0.5f, -0.8f},
        {down, 0.f, -1.f},
        {same, k_full_gainmap_headroom, 0.f},
        {same, 0.f, 0.f},
    };

    const int2 size{16, 8};
    const auto original = make_ramp_image(size);
    const auto map      = make_ramp_gainmap(int2{4, 2}, [](float r) { return std::pow(r, 0.5f); });
    for (const Case &k : cases)
    {
        CAPTURE(k.params.base_headroom);
        CAPTURE(k.params.alternate_headroom);
        CAPTURE(k.target);
        CHECK(k.params.weight(k.target) == doctest::Approx(k.weight));

        auto  img_ptr = make_ramp_image(size);
        auto &img     = *img_ptr;
        apply_iso_gainmap(img, map, k.params, k.target, true);
        const int g = channel_index(img, "gainmap.Y");
        REQUIRE(g >= 0);
        float worst = 0.f;
        for (int c = 0; c < 4; ++c)
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                {
                    const float base = original->channels[c](x, y);
                    const float want = c == 3 || k.weight == 0.f ? base
                                                                 : (base + k.params.base_offset[c]) *
                                                                           std::exp2(img.channels[g](x, y) * k.weight) -
                                                                       k.params.alternate_offset[c];
                    worst = std::max(worst, std::abs(img.channels[c](x, y) - want) / std::max(1.f, std::abs(want)));
                }
        CHECK(worst < 1e-5f);
    }
}

#if HDRVIEW_ENABLE_LIBHEIF
TEST_CASE("An Apple HEIC's gain map is found and applied" * doctest::skip(false))
{
    // The aux-image plumbing, the maker-note read and the map itself all have to line up, which no
    // synthetic file this suite could build reaches.
    const char *path = std::getenv("HDRVIEW_TEST_APPLE_HEIC");
    if (!path)
    {
        MESSAGE("HDRVIEW_TEST_APPLE_HEIC is unset; skipping the real-capture gain-map test.");
        return;
    }

    std::ifstream is{path, std::ios_base::binary};
    REQUIRE_MESSAGE(is.good(), "cannot open HDRVIEW_TEST_APPLE_HEIC");

    // HEIC decoding needs an HEVC plugin, which some presets leave out on patent grounds
    // (HDRVIEW_ENABLE_HEIC=OFF)
    if (!heif_have_decoder_for_format(heif_compression_HEVC))
    {
        MESSAGE("this build has no HEVC decoder; skipping the real-capture gain-map test.");
        return;
    }

    ImageLoadOptions opts;
    opts.gainmap_headroom = k_full_gainmap_headroom;
    auto images           = load_heif_image(is, path, opts);
    REQUIRE(!images.empty());

    auto &img = *images.front();

    REQUIRE_MESSAGE(channel_index(img, "gainmap.Y") >= 0, "no gain map was extracted from the file");
    CHECK(img.metadata["header"].contains("Gain map"));
    CHECK(img.channels[channel_index(img, "gainmap.Y")].size() == img.channels[0].size());

    // reloading with the map suppressed gives darker or equal color pixels
    is.clear();
    is.seekg(0);
    ImageLoadOptions base_opts;
    base_opts.gainmap_headroom = 0.f;
    auto base_images           = load_heif_image(is, path, base_opts);
    REQUIRE(!base_images.empty());

    auto &base = *base_images.front();
    REQUIRE(base.channels[0].size() == img.channels[0].size());

    // The gain is a scalar >= 1, so it scales every sample away from zero. Compared as magnitudes: a
    // wide-gamut capture carries slightly negative samples, and scaling those up makes them more negative.
    const int2 size      = img.channels[0].size();
    bool       amplified = false;
    bool       shrank    = false;
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            const float with = std::abs(img.channels[0](x, y)), without = std::abs(base.channels[0](x, y));
            if (with < without * 0.999f)
                shrank = true;
            if (with > without * 1.01f)
                amplified = true;
        }

    // one CHECK apiece: twelve million assertions take longer to report than the decode takes to run
    CHECK_MESSAGE(!shrank, "applying the gain map darkened part of the image");
    CHECK_MESSAGE(amplified, "applying the gain map changed nothing");

    // the gain is 1 + (2^stops - 1) * g with g in [0,1], so 2^stops is the ceiling however bright the scene
    const float stops = img.metadata["header"]["Gain map headroom"]["value"].get<float>();
    float       peak  = 0.f;
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            const float without = std::abs(base.channels[0](x, y));
            if (without > 1e-4f)
                peak = std::max(peak, std::abs(img.channels[0](x, y)) / without);
        }

    MESSAGE("gain map asks for ", stops, " stops; brightest pixel gained ", peak, "x");
    CHECK(peak <= doctest::Approx(std::exp2(stops)).epsilon(0.01));
    CHECK(peak > 1.f);
}
#endif

#if HDRVIEW_ENABLE_LIBUHDR
TEST_CASE("An UltraHDR gain map is extracted at the base image's resolution")
{
    // libuhdr hands the gain map back at the reduced resolution the file stores it at. These dimensions make
    // the reduction round: 100x60 at a scale of 8 comes back as 12x7, whose ratios to the base are not whole
    // numbers, so expanding by an integer ratio runs off the end of the decoded map.
    const int2 size{100, 60};
    const int  scale = 8;

    auto img = std::make_shared<Image>(size, 3);
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            // a bright wedge on the left, so the map has something to encode everywhere down the frame
            const float v = x < size.x / 2 ? 8.f : 0.25f;
            for (int c = 0; c < 3; ++c) img->channels[c](x, y) = v;
        }
    img->finalize(); // as_interleaved(), which the writer uses, reads img->groups

    std::stringstream ss;
    REQUIRE_NOTHROW(save_uhdr_image(*img, ss, "test.jpg", 95, 95, false, scale));
    REQUIRE(ss.tellp() > 0);

    ss.clear();
    ss.seekg(0);
    auto loaded = load_uhdr_image(ss, "test.jpg");
    REQUIRE(loaded.size() == 1);

    auto     &out = *loaded.front();
    const int gm =
        channel_index(out, "gainmap.Y") >= 0 ? channel_index(out, "gainmap.Y") : channel_index(out, "gainmap.R");
    REQUIRE_MESSAGE(gm >= 0, "no gain map channel group was extracted");

    CHECK(out.channels[gm].size() == out.channels[0].size());

    // every row has to have been written; a partial expansion leaves the bottom of the channel at zero,
    // which reads as "this part of the image needs no brightening"
    int empty_rows = 0;
    for (int y = 0; y < size.y; ++y)
    {
        float row = 0.f;
        for (int x = 0; x < size.x; ++x) row += std::abs(out.channels[gm](x, y));
        if (row == 0.f)
            ++empty_rows;
    }
    CHECK_MESSAGE(empty_rows == 0, "the expanded gain map has ", empty_rows, " unwritten row(s) of ", size.y);

    // the wedge is still visible after the expansion
    float left = 0.f, right = 0.f;
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) (x < size.x / 2 ? left : right) += out.channels[gm](x, y);

    CHECK(left != doctest::Approx(right));
}

#endif

#if HDRVIEW_ENABLE_LIBUHDR || HDRVIEW_ENABLE_LIBJXL
TEST_CASE("Every gain-map writer's file reads back as the renditions it was written from, at any headroom")
{
    // A gain map takes the file's image to the other rendition: decoded with no headroom the file is the SDR
    // rendition, decoded in full the HDR one, and halfway the geometric mean of the two, which only the headroom the
    // map was written for puts there. Each channel ramps across the frame at its own level, from deep shadow to a
    // quarter stop below the peak, well past SDR white, where the SDR rendition reads back clipped: it is handed over
    // unclipped, as image_to_save() builds it with highlights passing through, and clips where an SDR file or display
    // does. In BT.709 and in Display P3.
    struct Writer
    {
        const char *name;
        bool        sdr_given; ///< whether the file's SDR rendition is the one handed to the writer
        Caveat      caveat;    ///< what the writer's options warn of
        std::function<void(const Image &hdr, const Image &sdr, std::ostream &)> save;
        std::function<ImagePtr(std::istream &, const ImageLoadOptions &)>       load;
    };
    const std::vector<Writer> writers = {
#if HDRVIEW_ENABLE_LIBUHDR
        {"Ultra HDR, SDR rendition given",
         true,
         {},
         [](const Image &hdr, const Image &sdr, std::ostream &os)
         { save_uhdr_image(hdr, os, "a.jpg", 95, 95, true, 1, 1.f, &sdr); },
         [](std::istream &is, const ImageLoadOptions &opts) { return load_uhdr_image(is, "a.jpg", opts).front(); }},
        {"Ultra HDR, libultrahdr's tone map",
         false,
         {},
         [](const Image &hdr, const Image &, std::ostream &os)
         { save_uhdr_image(hdr, os, "a.jpg", 95, 95, true, 1, 1.f); },
         [](std::istream &is, const ImageLoadOptions &opts) { return load_uhdr_image(is, "a.jpg", opts).front(); }},
#endif
#if HDRVIEW_ENABLE_LIBJXL && !defined(HDRVIEW_TEST_ASAN) // see test_asan.h
        {"JPEG XL, SDR base", true, jxl_gainmap_caveat(false), [](const Image &hdr, const Image &sdr, std::ostream &os)
         { save_jxl_image(hdr, os, "a.jxl", false, 95.f, TransferFunction::sRGB, 2 /* uint8 */, &sdr, false); },
         [](std::istream &is, const ImageLoadOptions &opts) { return load_jxl_image(is, "a.jxl", opts).front(); }},
        {"JPEG XL, HDR base", true, jxl_gainmap_caveat(true), [](const Image &hdr, const Image &sdr, std::ostream &os)
         { save_jxl_image(hdr, os, "a.jxl", false, 95.f, TransferFunction::BT2100_PQ, 3 /* uint16 */, &sdr, true); },
         [](std::istream &is, const ImageLoadOptions &opts) { return load_jxl_image(is, "a.jxl", opts).front(); }},
#endif
    };

    const int2  size{64, 16};
    const float peak_nits = 1000.f;
    const float headroom  = std::log2(peak_nits / HDR_REFERENCE_WHITE_NITS);

    for (const auto &writer : writers)
        for (auto gamut : {ColorGamut_sRGB_BT709, ColorGamut_Display_P3_SMPTE432})
        {
            CAPTURE(std::string(writer.name));
            CAPTURE(std::string(color_gamut_name(gamut)));
            const Chromaticities chr = gamut_chromaticities(gamut);

            // With alpha, which the rendition of an image that has it carries: the top rows are fully transparent, and
            // the comparisons start at the first 8-row block clear of them, since the ringing a lossy codec leaves at
            // their edge reaches through the block that holds it.
            const auto rgba = [&]
            {
                auto img = std::make_shared<Image>();
                for (auto name : {"R", "G", "B", "A"}) img->channels.emplace_back(name, size);
                img->display_window = img->data_window = Box2i{int2{0}, size};
                return img;
            };
            constexpr int transparent_rows = 2, first_row = (transparent_rows + 7) / 8 * 8;
            auto          hdr = rgba(), sdr = rgba();
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                {
                    for (int c = 0; c < 3; ++c)
                    {
                        const float stops      = -6.f + (headroom - 0.25f + 6.f) * x / (size.x - 1) - 0.5f * c;
                        hdr->channels[c](x, y) = sdr->channels[c](x, y) = std::exp2(stops);
                    }
                    hdr->channels[3](x, y) = sdr->channels[3](x, y) = y < transparent_rows ? 0.f : 1.f;
                }
            for (auto &img : {hdr, sdr})
            {
                img->transparency   = TransparencyType_Straight; //< so finalize() premultiplies, as loaders do
                img->chromaticities = chr;
                img->compute_color_transform();
                img->finalize();
            }
            add_mastering_display(hdr->metadata["header"], chr, k_mastering_display_min_nits, peak_nits, "test");

            std::stringstream ss;
            REQUIRE_NOTHROW(writer.save(*hdr, *sdr, ss));

            const auto decode = [&](float gainmap_headroom)
            {
                std::istringstream in(ss.str(), std::ios::binary);
                ImageLoadOptions   opts;
                opts.gainmap_headroom = gainmap_headroom;
                return writer.load(in, opts);
            };
            const auto none = decode(0.f), half = decode(headroom / 2), full = decode(k_full_gainmap_headroom);

            // the file records the primaries it was written in
            REQUIRE(full->chromaticities);
            for (auto [got, want] :
                 {std::pair{full->chromaticities->red, chr.red}, std::pair{full->chromaticities->green, chr.green},
                  std::pair{full->chromaticities->blue, chr.blue}, std::pair{full->chromaticities->white, chr.white}})
                CHECK(la::maxelem(la::abs(got - want)) <= 1e-3f);

            // Each decode against what it should be: within a tenth of a stop from two stops below SDR white up, where
            // a wrong headroom shows as a fraction of a stop or more, and within 0.03 in sRGB code values up to SDR
            // white, where 8 bits set the precision. Halfway is the geometric mean of the two ends, offset as the map's
            // gains are (ISO 21496-1's 1/64). The SDR end has a known answer only when the writer was handed it. The
            // error is in units of those bounds, so 1 is as far off as a right decode may be.
            struct Error
            {
                float stops = 0.f, code = 0.f;
                float worst() const { return std::max(stops / 0.1f, code / 0.03f); }
                void  add(float got, float want)
                {
                    if (want >= 0.25f)
                        stops = std::max(stops, std::abs(std::log2(std::max(got, 1e-6f) / want)));
                    if (want <= 1.f)
                        code = std::max(code, std::abs(linear_to_sRGB(got) - linear_to_sRGB(want)));
                }
            } error;
            const auto compare = [&](float got, float want) { error.add(got, want); };
            for (int y = first_row; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                    for (int c = 0; c < 3; ++c)
                    {
                        constexpr float offset = 1.f / 64;
                        const float     lo = none->channels[c](x, y), hi = full->channels[c](x, y);
                        compare(hi, hdr->channels[c](x, y));
                        if (writer.sdr_given)
                            compare(lo, std::min(sdr->channels[c](x, y), 1.f));
                        compare(half->channels[c](x, y),
                                std::sqrt((std::max(lo, 0.f) + offset) * (std::max(hi, 0.f) + offset)) - offset);
                    }
            CHECK(error.worst() <= 1.f);

#if defined(__APPLE__)
            // And as macOS reads it, HDR and SDR, in extended linear sRGB, which adds a conversion at 8 bits or so of
            // its own: within one and a half times those bounds where the writer's options do not warn that macOS
            // misreads the file, and outside them where they do, or the warning has gone stale. The misreads warned of
            // are off by ten times that. The caveats name the macOS versions they were measured on, 26 and 27, so the
            // check runs from 26 on: macOS 15 decodes these Ultra HDR files' HDR rendition otherwise, though its SDR
            // one matches.
            constexpr int k_caveats_measured_on = 26;
            const int     os                    = apple_os_major();
            if (os < k_caveats_measured_on)
                MESSAGE("macOS ", os, " predates the macOS ", k_caveats_measured_on,
                        " and later the gain-map caveats were measured on; skipping its decode");
            const auto apple_hdr =
                           os >= k_caveats_measured_on ? apple_decode(ss.str(), AppleDecode::HDR) : std::nullopt,
                       apple_sdr =
                           os >= k_caveats_measured_on ? apple_decode(ss.str(), AppleDecode::SDR) : std::nullopt;
            if (apple_hdr && apple_sdr)
            {
                // the SDR rendition clips in the file's primaries, and both are compared in sRGB's, which ImageIO
                // decodes into; HDRView's own SDR decode stands in for a rendition the writer made itself
                Error      apple_hdr_error, apple_sdr_error;
                const auto rgb = [&](const Image &img, int x, int y)
                { return float3{img.channels[0](x, y), img.channels[1](x, y), img.channels[2](x, y)}; };
                for (int y = first_row; y < size.y; ++y)
                    for (int x = 0; x < size.x; ++x)
                    {
                        const float3 want_hdr = mul(hdr->M_to_sRGB, rgb(*hdr, x, y)),
                                     want_sdr =
                                         mul(hdr->M_to_sRGB, writer.sdr_given ? la::min(rgb(*sdr, x, y), float3{1.f})
                                                                              : rgb(*none, x, y));
                        for (int c = 0; c < 3; ++c)
                        {
                            apple_hdr_error.add((*apple_hdr)(x, y)[c], want_hdr[c]);
                            apple_sdr_error.add((*apple_sdr)(x, y)[c], want_sdr[c]);
                        }
                    }
                const float worst = std::max(apple_hdr_error.worst(), apple_sdr_error.worst());
                MESSAGE(std::string(writer.name), ", ", std::string(color_gamut_name(gamut)), ": macOS off by ",
                        apple_hdr_error.worst(), " (HDR) and ", apple_sdr_error.worst(), " (SDR) of the bounds");
                if (writer.caveat.readers & Misreader_macOS)
                    CHECK_MESSAGE(worst > 1.5f, "the options warn that macOS misreads this file, which it reads "
                                                "right: the warning is stale");
                else
                    CHECK_MESSAGE(worst <= 1.5f, "macOS reads this file as other values, and its options do not "
                                                 "warn of it");
            }
#endif
        }
}
#endif

// ------------------------------------------------------------------------------------------------
// ISO 21496-1
// ------------------------------------------------------------------------------------------------

namespace
{

// The gain map's ISO 21496-1 APP2 block from Gain_Map_Sample_Photos/samples_jpeg/01.jpg: the multi-channel,
// per-field-denominator encoding Adobe's exporter writes.
constexpr char k_iso_multichannel[] =
    "\x00\x00\x00\x00\xc0\x00\x00\x00\x00\x00\x00\x00\x01\x00\x01\x45\x3e\x00\x00\x80\x00\xfc\x23\x05\x14\x40\x00\x00"
    "\x00\x00\x01\x1f\xe1\x00\x00\x80\x00\x10\x4b\x9f\x0a\x40\x00\x00\x00\x01\x00\x00\x00\x40\x00\x00\x00\x01\x00\x00"
    "\x00\x40\x00\x00\x00\xfd\xdb\x68\x04\x40\x00\x00\x00\x00\x01\x11\x68\x00\x00\x80\x00\x10\x28\xf9\x53\x40\x00\x00"
    "\x00\x01\x00\x00\x00\x40\x00\x00\x00\x01\x00\x00\x00\x40\x00\x00\x00\xf7\x16\x7b\x90\x40\x00\x00\x00\x00\x01\x0f"
    "\x9a\x00\x00\x80\x00\x12\x95\xa8\x3f\x40\x00\x00\x00\x01\x00\x00\x00\x40\x00\x00\x00\x01\x00\x00\x00\x40\x00\x00"
    "\x00";

// A three-channel hdrgm packet, from greg benz photography/DSC0529-Edit...benz8GainMap.jpg, mixing
// attributes for the scalars with rdf:Seq elements for the per-channel values.
constexpr char k_hdrgm_xmp[] = R"(<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about=""
    xmlns:hdrgm="http://ns.adobe.com/hdr-gain-map/1.0/"
   hdrgm:Version="1.0"
   hdrgm:BaseRenditionIsHDR="False"
   hdrgm:OffsetSDR="0.015625"
   hdrgm:OffsetHDR="0.015625"
   hdrgm:HDRCapacityMin="0"
   hdrgm:HDRCapacityMax="2.6">
   <hdrgm:GainMapMin><rdf:Seq>
     <rdf:li>-0.356199</rdf:li><rdf:li>-0.293477</rdf:li><rdf:li>-0.161647</rdf:li>
   </rdf:Seq></hdrgm:GainMapMin>
   <hdrgm:GainMapMax><rdf:Seq>
     <rdf:li>2.520904</rdf:li><rdf:li>2.197984</rdf:li><rdf:li>1.965616</rdf:li>
   </rdf:Seq></hdrgm:GainMapMax>
   <hdrgm:Gamma><rdf:Seq>
     <rdf:li>0.318981</rdf:li><rdf:li>0.322233</rdf:li><rdf:li>0.268956</rdf:li>
   </rdf:Seq></hdrgm:Gamma>
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>)";

} // namespace

TEST_CASE("The ISO 21496-1 binary metadata parses to the values the file declares")
{
    // sizeof - 1 drops the literal's terminating NUL, which is not part of the block
    const auto p = parse_iso_gainmap((const uint8_t *)k_iso_multichannel, sizeof(k_iso_multichannel) - 1);

    CHECK(p.base_headroom == doctest::Approx(0.f));
    CHECK(p.alternate_headroom == doctest::Approx(2.54095459f));
    CHECK(p.use_base_color_space);

    // three distinct channels, which is what this file is here for
    CHECK(p.min[0] == doctest::Approx(-0.06036256f));
    CHECK(p.min[1] == doctest::Approx(-0.03348350f));
    CHECK(p.min[2] == doctest::Approx(-0.13925277f));

    CHECK(p.max[0] == doctest::Approx(2.24905396f));
    CHECK(p.max[1] == doctest::Approx(2.13598633f));
    CHECK(p.max[2] == doctest::Approx(2.12188721f));

    CHECK(p.gamma[0] == doctest::Approx(0.25461555f));
    CHECK(p.gamma[1] == doctest::Approx(0.25250085f));
    CHECK(p.gamma[2] == doctest::Approx(0.29038435f));

    for (int c = 0; c < 3; ++c)
    {
        CHECK(p.base_offset[c] == doctest::Approx(1.f / 64.f));
        CHECK(p.alternate_offset[c] == doctest::Approx(1.f / 64.f));
    }

    // and what HDRView writes reads back as what it wrote, in this direction and the other, where the base is the
    // HDR rendition
    IsoGainmapParams down = p;
    std::swap(down.base_headroom, down.alternate_headroom);
    down.base_offset[1] = 1.f / 32.f;
    for (const auto &written : {p, down})
    {
        const auto bytes = serialize_iso_gainmap(written);
        const auto read  = parse_iso_gainmap(bytes.data(), bytes.size());
        CHECK(read.base_headroom == doctest::Approx(written.base_headroom).epsilon(1e-5));
        CHECK(read.alternate_headroom == doctest::Approx(written.alternate_headroom).epsilon(1e-5));
        CHECK(read.use_base_color_space == written.use_base_color_space);
        for (int c = 0; c < 3; ++c)
            for (auto [r, w] :
                 {std::pair{read.min[c], written.min[c]}, std::pair{read.max[c], written.max[c]},
                  std::pair{read.gamma[c], written.gamma[c]}, std::pair{read.base_offset[c], written.base_offset[c]},
                  std::pair{read.alternate_offset[c], written.alternate_offset[c]}})
                CHECK(r == doctest::Approx(w).epsilon(1e-5));
    }
}

TEST_CASE("The ISO metadata's other encodings parse too")
{
    // Every sample file to hand writes a denominator per field and forward-direction headrooms, so these two
    // blobs are built by hand. libultrahdr emits the shared-denominator form whenever the denominators agree,
    // and the backward-direction flag whenever the base rendition is the HDR one.

    SUBCASE("one denominator shared by every field")
    {
        // flags 0xC8: multi-channel, shared denominator, gain applies in the base color space
        constexpr char blob[] =
            "\x00\x00\x00\x00\xc8\x00\x0f\x42\x40\x00\x00\x00\x00\x00\x26\x25\xa0\xff\xfe\x79\x60\x00\x21\x91\xc0\x00"
            "\x03\xd0\x90\x00\x00\x3d\x09\x00\x00\x3d\x09\xff\xff\x3c\xb0\x00\x20\x0b\x20\x00\x03\xf7\xa0\x00\x00\x3d"
            "\x09\x00\x00\x3d\x09\xff\xfc\xf2\xc0\x00\x1e\x84\x80\x00\x04\x1e\xb0\x00\x00\x3d\x09\x00\x00\x3d\x09";

        const auto p = parse_iso_gainmap((const uint8_t *)blob, sizeof(blob) - 1);

        CHECK(p.base_headroom == doctest::Approx(0.f));
        CHECK(p.alternate_headroom == doctest::Approx(2.5f));
        CHECK(p.use_base_color_space);

        CHECK(p.min[0] == doctest::Approx(-0.1f));
        CHECK(p.min[1] == doctest::Approx(-0.05f));
        CHECK(p.min[2] == doctest::Approx(-0.2f));
        CHECK(p.max[0] == doctest::Approx(2.2f));
        CHECK(p.max[2] == doctest::Approx(2.0f));
        CHECK(p.gamma[1] == doctest::Approx(0.26f));
        CHECK(p.base_offset[0] == doctest::Approx(0.015625f));
    }

    SUBCASE("backward direction, where the base rendition is the HDR one")
    {
        // flags 0x4C: single channel, shared denominator, backward direction; the file stores the headrooms
        // and offsets the other way round and the parser swaps them back
        constexpr char blob[] = "\x00\x00\x00\x00\x4c\x00\x0f\x42\x40\x00\x26\x25\xa0\x00\x00\x00\x00\xff\xfe\x79\x60"
                                "\x00\x21\x91\xc0\x00\x03\xd0\x90\x00\x00\x7a\x12\x00\x00\x3d\x09";

        const auto p = parse_iso_gainmap((const uint8_t *)blob, sizeof(blob) - 1);

        // stored as base 2.5 / alternate 0, so after the swap the base is the darker rendition
        CHECK(p.base_headroom == doctest::Approx(0.f));
        CHECK(p.alternate_headroom == doctest::Approx(2.5f));

        // the offsets swap with them: stored base 0.03125 / alternate 0.015625
        CHECK(p.base_offset[0] == doctest::Approx(0.015625f));
        CHECK(p.alternate_offset[0] == doctest::Approx(0.03125f));

        // a single-channel map drives all three the same way
        CHECK(p.min[2] == doctest::Approx(p.min[0]));
        CHECK(p.max[2] == doctest::Approx(2.2f));
        CHECK(p.gamma[1] == doctest::Approx(0.25f));
    }
}

TEST_CASE("A HEIF tmap item's leading version byte is the caller's to strip, not this parser's")
{
    // ISO/IEC 23008-12:2024 defines a tmap item's payload as ToneMapImage: a one-byte version, then the ISO
    // 21496-1 metadata. The 62 bytes below are the tmap item of an iPhone capture from ISO's Adaptive HDR
    // test set; 62 is also a legal length for metadata alone, so a parser guessing by length corrupts one form.
    constexpr char tmap[] =
        "\x00\x00\x00\x00\x00\x40\x00\x00\x00\x00\x00\x00\x00\x01\x00\xb2\x6b\xb5\x00\x40\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x01\x00\xb2\x6b\xb5\x00\x40\x00\x00\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\xd1\xb7\x20\x00"
        "\x00\x00\x00\x00\xd1\xb7\x20\x00\x00\x00";

    const auto *bytes = (const uint8_t *)tmap;
    REQUIRE(sizeof(tmap) - 1 == 62);
    CHECK(bytes[0] == 0); // ToneMapImage version

    SUBCASE("the metadata parses once the container's version byte is past")
    {
        const auto p = parse_iso_gainmap(bytes + 1, 61);

        CHECK(p.use_base_color_space);
        CHECK(p.base_headroom == doctest::Approx(0.f));
        CHECK(p.alternate_headroom == doctest::Approx(2.78782f));
        CHECK(p.min[0] == doctest::Approx(0.f));
        CHECK(p.max[0] == doctest::Approx(2.78782f));
        CHECK(p.gamma[0] == doctest::Approx(1.f));
        CHECK(p.base_offset[0] == doctest::Approx(1e-4f).epsilon(0.001));
    }

    SUBCASE("handing the whole item over instead is rejected, not silently misread")
    {
        // reading the version byte as the first half of minimum_version shifts every field and lands the
        // denominators on zero
        CHECK_THROWS_AS(parse_iso_gainmap(bytes, 62), std::invalid_argument);
    }
}

TEST_CASE("Truncated or unreadable ISO metadata is rejected rather than guessed at")
{
    SUBCASE("truncated mid-field")
    {
        CHECK_THROWS_AS(parse_iso_gainmap((const uint8_t *)k_iso_multichannel, 20), std::invalid_argument);
    }
    SUBCASE("empty")
    {
        CHECK_THROWS_AS(parse_iso_gainmap((const uint8_t *)k_iso_multichannel, 0), std::invalid_argument);
    }
    SUBCASE("a version this build does not know")
    {
        const uint8_t future[] = {0x00, 0x09, 0x00, 0x00, 0x00};
        CHECK_THROWS_AS(parse_iso_gainmap(future, sizeof(future)), std::invalid_argument);
    }
}

TEST_CASE("Adobe's hdrgm XMP parses to the same struct as the binary form")
{
    const auto parsed = parse_hdrgm_xmp(k_hdrgm_xmp, sizeof(k_hdrgm_xmp) - 1);
    REQUIRE(parsed.has_value());
    const auto &p = *parsed;

    CHECK(p.base_headroom == doctest::Approx(0.f));
    CHECK(p.alternate_headroom == doctest::Approx(2.6f));

    // values that arrived as rdf:Seq elements
    CHECK(p.min[0] == doctest::Approx(-0.356199f));
    CHECK(p.min[1] == doctest::Approx(-0.293477f));
    CHECK(p.min[2] == doctest::Approx(-0.161647f));
    CHECK(p.max[0] == doctest::Approx(2.520904f));
    CHECK(p.max[2] == doctest::Approx(1.965616f));
    CHECK(p.gamma[1] == doctest::Approx(0.322233f));

    // and ones that arrived as attributes
    CHECK(p.base_offset[0] == doctest::Approx(0.015625f));
    CHECK(p.alternate_offset[0] == doctest::Approx(0.015625f));

    CHECK(p.version.find("1.0") != std::string::npos);
}

TEST_CASE("A packet with no hdrgm properties is reported as absent, not as defaults")
{
    constexpr char plain[] = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF
        xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description rdf:about=""
        xmlns:tiff="http://ns.adobe.com/tiff/1.0/" tiff:Orientation="1"/></rdf:RDF></x:xmpmeta>)";

    CHECK_FALSE(parse_hdrgm_xmp(plain, sizeof(plain) - 1).has_value());
    CHECK_FALSE(parse_hdrgm_xmp(nullptr, 0).has_value());
    CHECK_FALSE(parse_hdrgm_xmp("not xml at all", 14).has_value());
}

#if HDRVIEW_ENABLE_LIBJXL
TEST_CASE("A JPEG XL gain map is read out of the jhgm box and applied")
{
    // Point this at Adobe's Gain_Map_Sample_Photos, which carries the same scene encoded both ways:
    // samples_jxl_base_sdr stores the SDR rendition and brightens towards HDR, samples_jxl_base_hdr stores
    // the HDR one and darkens towards SDR. A viewer lands on the HDR rendition in both.
    const char *dir = std::getenv("HDRVIEW_TEST_JXL_GAINMAP_DIR");
    if (!dir)
    {
        MESSAGE("HDRVIEW_TEST_JXL_GAINMAP_DIR is unset; skipping the real-file gain-map tests.");
        return;
    }

    const auto load = [&](const char *subdir, const char *name, float headroom)
    {
        const auto    path = std::string{dir} + "/" + subdir + "/" + name;
        std::ifstream is{path, std::ios_base::binary};
        REQUIRE_MESSAGE(is.good(), "cannot open ", path);

        ImageLoadOptions opts;
        opts.gainmap_headroom = headroom;
        auto images           = load_jxl_image(is, name, opts);
        REQUIRE(!images.empty());
        return images.front();
    };

    const auto peak = [](const Image &img)
    {
        const int2 size = img.channels[0].size();
        float      p    = 0.f;
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) p = std::max(p, img.channels[0](x, y));
        return p;
    };

    SUBCASE("a base-SDR file brightens towards its HDR rendition")
    {
        auto sdr = load("samples_jxl_base_sdr", "01_base_sdr.jxl", 0.f);                     // as stored
        auto hdr = load("samples_jxl_base_sdr", "01_base_sdr.jxl", k_full_gainmap_headroom); // reconstructed

        REQUIRE_MESSAGE(channel_index(*hdr, "gainmap.R") >= 0, "no gain map was extracted from the jhgm box");
        REQUIRE(sdr->channels[0].size() == hdr->channels[0].size());

        const float peak_sdr = peak(*sdr), peak_hdr = peak(*hdr);
        MESSAGE("base-SDR peak ", peak_sdr, " -> reconstructed ", peak_hdr);
        CHECK(peak_hdr > peak_sdr * 1.5f);

        // the map cannot ask for more brightening than its own maximum, whatever the codec's reconstruction
        // of it overshoots to
        const float ceiling = std::exp2(hdr->metadata["header"]["Gain map headroom"]["value"].get<float>());
        CHECK(peak_hdr <= doctest::Approx(peak_sdr * ceiling).epsilon(0.01));
    }

    SUBCASE("a base-HDR file is left alone at full headroom, and darkened at zero")
    {
        auto full = load("samples_jxl_base_hdr", "01_base_hdr.jxl", k_full_gainmap_headroom); // already HDR
        auto none = load("samples_jxl_base_hdr", "01_base_hdr.jxl", 0.f);                     // driven to SDR

        REQUIRE(full->channels[0].size() == none->channels[0].size());

        const float peak_full = peak(*full), peak_none = peak(*none);
        MESSAGE("base-HDR peak ", peak_full, " -> driven to SDR ", peak_none);
        CHECK(peak_none < peak_full * 0.75f);
    }
}
#endif

#if HDRVIEW_ENABLE_LIBUHDR
TEST_CASE("The target headroom reaches an UltraHDR JPEG, which libultrahdr reconstructs itself")
{
    // libultrahdr applies the map inside its own decoder, so the target has to be handed to it and not
    // applied afterwards
    const char *path = std::getenv("HDRVIEW_TEST_GAINMAP_JPEG");
    if (!path)
    {
        MESSAGE("HDRVIEW_TEST_GAINMAP_JPEG is unset; skipping the UltraHDR headroom test.");
        return;
    }

    std::ifstream probe{path, std::ios_base::binary};
    if (!is_uhdr_image(probe))
    {
        MESSAGE("HDRVIEW_TEST_GAINMAP_JPEG is not an UltraHDR file; skipping.");
        return;
    }

    const auto peak_at = [&](float headroom)
    {
        std::ifstream is{path, std::ios_base::binary};
        REQUIRE(is.good());

        ImageLoadOptions opts;
        opts.gainmap_headroom = headroom;
        auto images           = load_uhdr_image(is, path, opts);
        REQUIRE(!images.empty());

        const int2 size = images.front()->channels[0].size();
        float      p    = 0.f;
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) p = std::max(p, images.front()->channels[0](x, y));
        return p;
    };

    const float base = peak_at(0.f);
    const float full = peak_at(k_full_gainmap_headroom);

    // A target of N stops caps the reconstruction at 2^N. The brightest pixel lands on the cap only where
    // the map saturates, which a per-channel map need not do at the channel being measured, so what is
    // checked is the ceiling plus the fact that raising the target never darkens anything.
    float previous = base;
    for (float stops : {0.5f, 1.f, 1.5f, 2.f, 3.f})
    {
        const float peak = peak_at(stops);
        MESSAGE("  ", stops, " stops -> peak ", peak, " (base x ", peak / base, ", ceiling x ", std::exp2(stops), ")");

        CHECK(peak <= doctest::Approx(base * std::exp2(stops)).epsilon(0.01));
        CHECK(peak >= doctest::Approx(previous).epsilon(0.01));
        previous = peak;
    }

    MESSAGE("base peak ", base, "; unbounded target reconstructs to ", full);
    CHECK(full > base * 2.f);
}
#endif

#if HDRVIEW_ENABLE_LIBJPEG
TEST_CASE("A gain map packed into a JPEG is found through the MPF index and applied")
{
    // Point this at a directory of gain-mapped JPEGs. Well-formed UltraHDR files are claimed by the
    // libultrahdr loader first, so this exercises everything else that packs a map the same way.
    const char *path = std::getenv("HDRVIEW_TEST_GAINMAP_JPEG");
    if (!path)
    {
        MESSAGE("HDRVIEW_TEST_GAINMAP_JPEG is unset; skipping the real-file JPEG gain-map test.");
        return;
    }

    const auto load = [&](float headroom)
    {
        std::ifstream is{path, std::ios_base::binary};
        REQUIRE_MESSAGE(is.good(), "cannot open ", path);

        ImageLoadOptions opts;
        opts.gainmap_headroom = headroom;
        auto images           = load_jpg_image(is, path, opts);
        REQUIRE(!images.empty());
        return images.front();
    };

    auto base = load(0.f);
    auto hdr  = load(k_full_gainmap_headroom);

    const bool has_map = channel_index(*hdr, "gainmap.R") >= 0 || channel_index(*hdr, "gainmap.Y") >= 0;
    REQUIRE_MESSAGE(has_map, "no gain map was extracted from the file");
    REQUIRE(base->channels[0].size() == hdr->channels[0].size());

    const int2 size      = base->channels[0].size();
    float      peak_base = 0.f, peak_hdr = 0.f;
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            peak_base = std::max(peak_base, base->channels[0](x, y));
            peak_hdr  = std::max(peak_hdr, hdr->channels[0](x, y));
        }

    MESSAGE("base peak ", peak_base, " -> reconstructed ", peak_hdr);
    CHECK(peak_hdr > peak_base * 1.2f);

#if HDRVIEW_ENABLE_LIBUHDR
    // when the file is also a well-formed UltraHDR JPEG, libultrahdr decodes it too, through a separate
    // implementation of the same standard
    std::ifstream probe{path, std::ios_base::binary};
    if (is_uhdr_image(probe))
    {
        const auto sum_via_uhdr = [&](float headroom)
        {
            std::ifstream    is{path, std::ios_base::binary};
            ImageLoadOptions opts;
            opts.gainmap_headroom = headroom;
            auto images           = load_uhdr_image(is, path, opts);
            REQUIRE(!images.empty());
            REQUIRE(images.front()->channels[0].size() == size);

            double sum = 0.;
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) sum += images.front()->channels[0](x, y);
            return sum;
        };

        double sum_base = 0., sum_hdr = 0.;
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                sum_base += base->channels[0](x, y);
                sum_hdr += hdr->channels[0](x, y);
            }

        // Compares what the map does, not the pixels it lands on. The two loaders linearize a wide-gamut base
        // differently (libultrahdr labels Display P3 with the canonical primaries where this path derives them
        // from the embedded ICC matrix), which shifts the channels by a fixed matrix whether or not a map is
        // involved; dividing each loader's reconstruction by its own base cancels that. Means, not peaks: the
        // two resample the reduced-resolution map slightly differently, so edge pixels differ more.
        const double mine   = sum_hdr / sum_base;
        const double theirs = sum_via_uhdr(k_full_gainmap_headroom) / sum_via_uhdr(0.f);

        MESSAGE("gain applied, mean: ", mine, " here vs ", theirs, " in libultrahdr");
        CHECK(mine == doctest::Approx(theirs).epsilon(0.02));
    }
#endif
}
#endif
