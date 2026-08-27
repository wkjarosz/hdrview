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
#include "imageio/heif.h"
#include "imageio/image_loader.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/uhdr.h"

#if HDRVIEW_ENABLE_LIBHEIF
#include <libheif/heif.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{

//! A flat RGB image, so that any variation after applying a gain map came from the map.
ImagePtr make_flat_image(int2 size, float value)
{
    auto img = std::make_shared<Image>(size, 3);
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) img->channels[c](x, y) = value;
    return img;
}

//! A monochrome gain map holding one constant sRGB-encoded value.
GainmapImage make_flat_gainmap(int2 size, float encoded)
{
    GainmapImage gm;
    gm.size     = size;
    gm.channels = 1;
    gm.pixels.assign(size_t(size.x) * size.y, encoded);
    return gm;
}

int channel_index(const Image &img, const std::string &name)
{
    for (int i = 0; i < (int)img.channels.size(); ++i)
        if (img.channels[i].name == name)
            return i;
    return -1;
}

} // namespace

TEST_CASE("Apple gain-map strength follows the published piecewise fit")
{
    // The two branches of the fit are selected by whether the headroom field reaches 1.0, and within
    // each branch by whether the gain field is past the 0.01 knee.
    SUBCASE("low headroom, gain below the knee")
    {
        AppleGainmapParams p{0.5f, 0.f};
        CHECK(p.stops() == doctest::Approx(1.8f));
    }
    SUBCASE("low headroom, gain past the knee")
    {
        AppleGainmapParams p{0.5f, 1.f};
        CHECK(p.stops() == doctest::Approx(1.5f));
    }
    SUBCASE("high headroom, gain below the knee")
    {
        AppleGainmapParams p{1.5f, 0.f};
        CHECK(p.stops() == doctest::Approx(3.f));
    }
    SUBCASE("high headroom, gain past the knee")
    {
        AppleGainmapParams p{1.5f, 1.f};
        CHECK(p.stops() == doctest::Approx(2.f));
    }

    SUBCASE("the defaults are the weakest reconstruction Apple's software falls back to")
    {
        AppleGainmapParams p;
        CHECK(p.stops() == doctest::Approx(0.793f));
    }

    SUBCASE("what an iPhone 12 Pro actually writes")
    {
        // Maker note 0x21 = 0.8432090282, 0x30 = 0, which several captures in the test corpus share.
        AppleGainmapParams p{0.8432090282f, 0.f};
        CHECK(p.stops() == doctest::Approx(1.8f));
    }
}

TEST_CASE("Applying an Apple gain map scales the base image by the reconstructed headroom")
{
    const int2 size{8, 4};

    SUBCASE("a fully-on map brightens by the full headroom")
    {
        auto img = make_flat_image(size, 0.25f);
        // 1.0 encodes to 1.0 under any sane transfer function, so the gain here is exactly 1.
        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);

        // stops = 1.8, so headroom = 2^1.8, and gain 1 means the base is scaled by the whole of it.
        const float expected = 0.25f * std::exp2(1.8f);
        CHECK(img->channels[0](0, 0) == doctest::Approx(expected));
        CHECK(img->channels[2](7, 3) == doctest::Approx(expected));
    }

    SUBCASE("a fully-off map leaves the base image alone")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, make_flat_gainmap(size, 0.f), AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f));
    }

    SUBCASE("a target of zero stops leaves the base image alone, but still yields the map")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, 0.f);

        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f));
        REQUIRE(channel_index(*img, "gainmap.Y") >= 0);
    }

    SUBCASE("the target caps a map that asks for more")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, 1.f);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f * 2.f)); // capped at 1 stop
    }

    SUBCASE("the target does not raise a map that asks for less")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, 10.f);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f * std::exp2(1.8f)));
    }

    SUBCASE("alpha is not a color and is not scaled")
    {
        auto img = std::make_shared<Image>(size, 4);
        for (int c = 0; c < 4; ++c)
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) img->channels[c](x, y) = 0.5f;

        apply_apple_gainmap(*img, make_flat_gainmap(size, 1.f), AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);

        REQUIRE(img->channels[3].name == "A");
        CHECK(img->channels[3](0, 0) == doctest::Approx(0.5f));
        CHECK(img->channels[0](0, 0) > 0.5f);
    }
}

TEST_CASE("A gain map is appended as its own channel group, resized to the base image")
{
    const int2 size{16, 8};
    auto       img = make_flat_image(size, 0.5f);

    // Quarter resolution in each axis, as gain maps are usually stored.
    apply_apple_gainmap(*img, make_flat_gainmap(int2{4, 2}, 1.f), AppleGainmapParams{0.5f, 0.f}, 0.f);

    const int gm = channel_index(*img, "gainmap.Y");
    REQUIRE(gm >= 0);
    CHECK(img->channels[gm].size() == size);

    // The map was uniform, so every resampled value should still be the linearized 1.0.
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) CHECK(img->channels[gm](x, y) == doctest::Approx(1.f));
}

TEST_CASE("A malformed gain map is ignored rather than corrupting the base image")
{
    const int2 size{4, 4};

    SUBCASE("empty map")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_apple_gainmap(*img, GainmapImage{}, AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);
        CHECK(img->channels.size() == 3);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f));
    }

    SUBCASE("map claiming more pixels than it holds")
    {
        auto         img = make_flat_image(size, 0.25f);
        GainmapImage gm;
        gm.size     = int2{64, 64};
        gm.channels = 1;
        gm.pixels.assign(4, 1.f);
        apply_apple_gainmap(*img, gm, AppleGainmapParams{0.5f, 0.f}, k_full_gainmap_headroom);
        CHECK(img->channels.size() == 3);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f));
    }
}

#if HDRVIEW_ENABLE_LIBHEIF
TEST_CASE("An Apple HEIC's gain map is found and applied" * doctest::skip(false))
{
    // Apple gain maps need a real capture to exercise: the aux-image plumbing, the maker-note read,
    // and the map itself all have to line up, and none of that is reachable from a synthetic file
    // this suite could build.
    const char *path = std::getenv("HDRVIEW_TEST_APPLE_HEIC");
    if (!path)
    {
        MESSAGE("HDRVIEW_TEST_APPLE_HEIC is unset; skipping the real-capture gain-map test.");
        return;
    }

    std::ifstream is{path, std::ios_base::binary};
    REQUIRE_MESSAGE(is.good(), "cannot open HDRVIEW_TEST_APPLE_HEIC");

    // HEIC decoding needs an HEVC plugin, which some presets leave out on patent grounds
    // (HDRVIEW_ENABLE_HEIC=OFF). Nothing about the gain map is testable then.
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

    // Reloading with the map suppressed must give strictly darker (or equal) color pixels.
    is.clear();
    is.seekg(0);
    ImageLoadOptions base_opts;
    base_opts.gainmap_headroom = 0.f;
    auto base_images           = load_heif_image(is, path, base_opts);
    REQUIRE(!base_images.empty());

    auto &base = *base_images.front();
    REQUIRE(base.channels[0].size() == img.channels[0].size());

    // The gain is a scalar >= 1, so it scales every sample away from zero. Comparing magnitudes
    // rather than signed values matters: a wide-gamut capture carries some slightly negative
    // samples, and scaling those up makes them more negative, not larger.
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

    // One CHECK apiece rather than one per pixel: twelve million assertions take longer to report
    // than the decode takes to run.
    CHECK_MESSAGE(!shrank, "applying the gain map darkened part of the image");
    CHECK_MESSAGE(amplified, "applying the gain map changed nothing");

    // No pixel may gain more than the map asks for: the gain is 1 + (2^stops - 1) * g with g in
    // [0,1], so 2^stops is the ceiling however bright the scene is.
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
    // libuhdr hands the gain map back at the reduced resolution the file stores it at, so HDRView has
    // to expand it. These dimensions are chosen so the reduction rounds: 100x60 at a scale of 8 comes
    // back as 12x7, whose ratios to the base are not whole numbers. Expanding by an integer ratio
    // runs off the end of the decoded map, leaving the last rows and columns of the channel at zero
    // -- which reads as "this part of the image needs no brightening at all".
    const int2 size{100, 60};
    const int  scale = 8;

    auto img = std::make_shared<Image>(size, 3);
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            // A bright wedge on the left, so the map has something to encode everywhere down the frame.
            const float v = x < size.x / 2 ? 8.f : 0.25f;
            for (int c = 0; c < 3; ++c) img->channels[c](x, y) = v;
        }
    img->finalize(); // as_interleaved(), which the writer uses, reads img->groups

    std::stringstream ss;
    REQUIRE_NOTHROW(save_uhdr_image(*img, ss, "test.jpg", 1.f, 95, 95, false, scale));
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

    // Every row has to have been written, including the last. A partial expansion leaves the bottom
    // of the channel at zero, which reads as "this part of the image needs no brightening at all".
    int empty_rows = 0;
    for (int y = 0; y < size.y; ++y)
    {
        float row = 0.f;
        for (int x = 0; x < size.x; ++x) row += std::abs(out.channels[gm](x, y));
        if (row == 0.f)
            ++empty_rows;
    }
    CHECK_MESSAGE(empty_rows == 0, "the expanded gain map has ", empty_rows, " unwritten row(s) of ", size.y);

    // The wedge should still be visible after the expansion.
    float left = 0.f, right = 0.f;
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) (x < size.x / 2 ? left : right) += out.channels[gm](x, y);

    CHECK(left != doctest::Approx(right));
}
#endif

// ------------------------------------------------------------------------------------------------
// ISO 21496-1
// ------------------------------------------------------------------------------------------------

namespace
{

// The gain map's ISO 21496-1 APP2 block from Gain_Map_Sample_Photos/samples_jpeg/01.jpg: the
// multi-channel, per-field-denominator encoding, which is what Adobe's exporter writes.
constexpr char k_iso_multichannel[] =
    "\x00\x00\x00\x00\xc0\x00\x00\x00\x00\x00\x00\x00\x01\x00\x01\x45\x3e\x00\x00\x80\x00\xfc\x23\x05\x14\x40\x00\x00"
    "\x00\x00\x01\x1f\xe1\x00\x00\x80\x00\x10\x4b\x9f\x0a\x40\x00\x00\x00\x01\x00\x00\x00\x40\x00\x00\x00\x01\x00\x00"
    "\x00\x40\x00\x00\x00\xfd\xdb\x68\x04\x40\x00\x00\x00\x00\x01\x11\x68\x00\x00\x80\x00\x10\x28\xf9\x53\x40\x00\x00"
    "\x00\x01\x00\x00\x00\x40\x00\x00\x00\x01\x00\x00\x00\x40\x00\x00\x00\xf7\x16\x7b\x90\x40\x00\x00\x00\x00\x01\x0f"
    "\x9a\x00\x00\x80\x00\x12\x95\xa8\x3f\x40\x00\x00\x00\x01\x00\x00\x00\x40\x00\x00\x00\x01\x00\x00\x00\x40\x00\x00"
    "\x00";

// A three-channel hdrgm packet, from greg benz photography/DSC0529-Edit...benz8GainMap.jpg. Note the
// mixture of attributes for the scalars and rdf:Seq elements for the per-channel values.
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
    // sizeof - 1 drops the literal's terminating NUL, which is not part of the block.
    const auto p = parse_iso_gainmap((const uint8_t *)k_iso_multichannel, sizeof(k_iso_multichannel) - 1);

    CHECK(p.base_headroom == doctest::Approx(0.f));
    CHECK(p.alternate_headroom == doctest::Approx(2.54095459f));
    CHECK(p.use_base_color_space);

    // Three distinct channels, which is the point of this particular file.
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
}

TEST_CASE("The ISO metadata's other encodings parse too")
{
    // Every sample file to hand writes a denominator per field and forward-direction headrooms, so
    // these two blobs are built by hand. libultrahdr emits the shared-denominator form whenever the
    // denominators happen to agree, and the backward-direction flag whenever the base rendition is
    // the HDR one, so both are shapes a real file can arrive in.

    SUBCASE("one denominator shared by every field")
    {
        // flags 0xC8: multi-channel, shared denominator, gain applies in the base color space.
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
        // flags 0x4C: single channel, shared denominator, backward direction. The file stores the
        // headrooms and offsets the other way round, and the parser swaps them back.
        constexpr char blob[] = "\x00\x00\x00\x00\x4c\x00\x0f\x42\x40\x00\x26\x25\xa0\x00\x00\x00\x00\xff\xfe\x79\x60"
                                "\x00\x21\x91\xc0\x00\x03\xd0\x90\x00\x00\x7a\x12\x00\x00\x3d\x09";

        const auto p = parse_iso_gainmap((const uint8_t *)blob, sizeof(blob) - 1);

        // Stored as base 2.5 / alternate 0, so after the swap the base is the darker rendition.
        CHECK(p.base_headroom == doctest::Approx(0.f));
        CHECK(p.alternate_headroom == doctest::Approx(2.5f));

        // The offsets swap with them: stored base 0.03125 / alternate 0.015625.
        CHECK(p.base_offset[0] == doctest::Approx(0.015625f));
        CHECK(p.alternate_offset[0] == doctest::Approx(0.03125f));

        // A single-channel map drives all three the same way.
        CHECK(p.min[2] == doctest::Approx(p.min[0]));
        CHECK(p.max[2] == doctest::Approx(2.2f));
        CHECK(p.gamma[1] == doctest::Approx(0.25f));
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

    // Values that arrived as rdf:Seq elements rather than attributes.
    CHECK(p.min[0] == doctest::Approx(-0.356199f));
    CHECK(p.min[1] == doctest::Approx(-0.293477f));
    CHECK(p.min[2] == doctest::Approx(-0.161647f));
    CHECK(p.max[0] == doctest::Approx(2.520904f));
    CHECK(p.max[2] == doctest::Approx(1.965616f));
    CHECK(p.gamma[1] == doctest::Approx(0.322233f));

    // And ones that arrived as attributes.
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

TEST_CASE("Gain-map weight follows the target headroom, in both directions")
{
    SUBCASE("base is the SDR rendition, as in a JPEG")
    {
        IsoGainmapParams p;
        p.base_headroom      = 0.f;
        p.alternate_headroom = 2.5f;

        CHECK(p.weight(k_full_gainmap_headroom) == doctest::Approx(1.f)); // all the way to HDR
        CHECK(p.weight(2.5f) == doctest::Approx(1.f));
        CHECK(p.weight(1.25f) == doctest::Approx(0.5f));
        CHECK(p.weight(0.f) == doctest::Approx(0.f)); // the base image as stored
    }

    SUBCASE("base is the HDR rendition, as in a base-HDR JPEG XL")
    {
        // The map then derives a *darker* rendition, so an unbounded target must leave the base
        // alone rather than applying the map in full.
        IsoGainmapParams p;
        p.base_headroom      = 2.5f;
        p.alternate_headroom = 0.f;

        CHECK(p.weight(k_full_gainmap_headroom) == doctest::Approx(0.f));
        CHECK(p.weight(2.5f) == doctest::Approx(0.f));
        CHECK(p.weight(1.25f) == doctest::Approx(-0.5f));
        CHECK(p.weight(0.f) == doctest::Approx(-1.f)); // all the way down to SDR
    }

    SUBCASE("a map whose two renditions want the same headroom does nothing")
    {
        IsoGainmapParams p;
        p.base_headroom = p.alternate_headroom = 1.f;
        CHECK(p.weight(k_full_gainmap_headroom) == doctest::Approx(0.f));
    }
}

TEST_CASE("Applying an ISO gain map moves the base image towards the alternate rendition")
{
    const int2 size{8, 8};

    // A map that is 1.0 everywhere, with min 0 and max 2, encodes a uniform 2-stop brightening.
    IsoGainmapParams p;
    p.min                = float3{0.f};
    p.max                = float3{2.f};
    p.gamma              = float3{1.f};
    p.base_offset        = float3{0.f};
    p.alternate_offset   = float3{0.f};
    p.base_headroom      = 0.f;
    p.alternate_headroom = 2.f;

    SUBCASE("fully applied")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_iso_gainmap(*img, make_flat_gainmap(size, 1.f), p, k_full_gainmap_headroom);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f * 4.f)); // 2 stops
    }

    SUBCASE("half applied")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_iso_gainmap(*img, make_flat_gainmap(size, 1.f), p, 1.f);
        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f * 2.f)); // 1 stop
    }

    SUBCASE("not applied, but the map is still there to look at")
    {
        auto img = make_flat_image(size, 0.25f);
        apply_iso_gainmap(*img, make_flat_gainmap(size, 1.f), p, 0.f);

        CHECK(img->channels[0](0, 0) == doctest::Approx(0.25f));

        // The appended group holds log2 gains, so a map that decodes to "2 stops" reads as 2.
        const int gm = channel_index(*img, "gainmap.Y");
        REQUIRE(gm >= 0);
        CHECK(img->channels[gm](0, 0) == doctest::Approx(2.f));
    }

    SUBCASE("the offsets are applied around the gain, not after it")
    {
        IsoGainmapParams q = p;
        q.base_offset      = float3{0.125f};
        q.alternate_offset = float3{0.0625f};

        auto img = make_flat_image(size, 0.25f);
        apply_iso_gainmap(*img, make_flat_gainmap(size, 1.f), q, k_full_gainmap_headroom);
        CHECK(img->channels[0](0, 0) == doctest::Approx((0.25f + 0.125f) * 4.f - 0.0625f));
    }

    SUBCASE("alpha is left alone")
    {
        auto img = std::make_shared<Image>(size, 4);
        for (int c = 0; c < 4; ++c)
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) img->channels[c](x, y) = 0.5f;

        apply_iso_gainmap(*img, make_flat_gainmap(size, 1.f), p, k_full_gainmap_headroom);

        REQUIRE(img->channels[3].name == "A");
        CHECK(img->channels[3](0, 0) == doctest::Approx(0.5f));
        CHECK(img->channels[0](0, 0) > 0.5f);
    }
}

#if HDRVIEW_ENABLE_LIBJXL
TEST_CASE("A JPEG XL gain map is read out of the jhgm box and applied")
{
    // Point this at Adobe's Gain_Map_Sample_Photos, which carries the same scene encoded both ways:
    // samples_jxl_base_sdr stores the SDR rendition and brightens towards HDR, samples_jxl_base_hdr
    // stores the HDR one and darkens towards SDR. A viewer has to land on the HDR rendition in both.
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

        // The map cannot ask for more brightening than its own maximum, whatever the codec's
        // reconstruction of the map overshoots to.
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
    // libultrahdr applies the map inside its own decoder, so the target has to be handed to it
    // rather than applied afterwards. Without that, this control would silently do nothing for the
    // one JPEG flavor that does not come through HDRView's own gain-map path.
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

    const float full = peak_at(k_full_gainmap_headroom);

    // Each stop of target headroom doubles what the brightest pixel is allowed to reach, until the
    // map's own maximum takes over.
    CHECK(peak_at(0.f) == doctest::Approx(1.f).epsilon(0.01));
    CHECK(peak_at(1.f) == doctest::Approx(2.f).epsilon(0.01));
    CHECK(peak_at(2.f) == doctest::Approx(4.f).epsilon(0.01));

    MESSAGE("unbounded target reconstructs to a peak of ", full);
    CHECK(full > 4.f);
}
#endif

#if HDRVIEW_ENABLE_LIBJPEG
TEST_CASE("A gain map packed into a JPEG is found through the MPF index and applied")
{
    // Point this at a directory of gain-mapped JPEGs. Well-formed UltraHDR files are claimed by the
    // libultrahdr loader before this path sees them, so what this exercises is everything else that
    // packs a map the same way.
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
    // When the file is also a well-formed UltraHDR JPEG, libultrahdr will decode it too -- through
    // an entirely separate implementation of the same standard. Agreeing with it is a much stronger
    // statement than any self-consistency check this suite can make on its own.
    std::ifstream probe{path, std::ios_base::binary};
    if (is_uhdr_image(probe))
    {
        std::ifstream is{path, std::ios_base::binary};
        auto          via_uhdr = load_uhdr_image(is, path);
        REQUIRE(!via_uhdr.empty());

        auto &other = *via_uhdr.front();
        REQUIRE(other.channels[0].size() == size);

        // Means rather than peaks: the two resample the reduced-resolution map slightly differently,
        // so individual pixels near an edge can differ by more than the image as a whole does.
        double sum_hdr = 0., sum_other = 0.;
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                sum_hdr += hdr->channels[0](x, y);
                sum_other += other.channels[0](x, y);
            }

        const double ratio = sum_hdr / sum_other;
        MESSAGE("mean brightness vs. libultrahdr: ", ratio);
        CHECK(ratio == doctest::Approx(1.0).epsilon(0.02));
    }
#endif
}
#endif
