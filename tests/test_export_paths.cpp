//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/exr.h"
#include "imageio/heif.h"
#include "imageio/image_loader.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/pfm.h"
#include "imageio/tiff.h"

#include <array>
#include <iterator>
#include <sstream>

#if HDRVIEW_ENABLE_LIBJXL
#include <jxl/types.h> // for the JXL_TYPE_* pixel formats save_jxl_image() takes
#endif

namespace
{

// An image whose channel names make finalize() build the intended group type.
ImagePtr make_named(std::initializer_list<const char *> names, int2 size = int2{4, 4})
{
    auto img = std::make_shared<Image>();
    for (auto n : names) img->channels.emplace_back(n, size);
    img->display_window = img->data_window = Box2i{int2{0}, size};
    float v                                = 0.13f;
    for (auto &ch : img->channels)
    {
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) ch(x, y) = v + 0.01f * float(x + size.x * y);
        v += 0.17f;
    }
    img->finalize();
    return img;
}

} // namespace

TEST_CASE("A luminance-chroma group is converted to RGB on export, as the viewport shows it")
{
    // OpenEXR writes RY,Y,BY for luminance-chroma images; the viewport converts them with YC_to_RGB()
    // (see Image::rgba_pixel), and an exported file has to agree or it shows different colours.
    for (auto names : {std::initializer_list<const char *>{"RY", "Y", "BY"},
                       std::initializer_list<const char *>{"RY", "Y", "BY", "A"}})
    {
        auto img = make_named(names);
        REQUIRE((img->groups[img->selected_group].type == ChannelGroup::YC_Channels ||
                 img->groups[img->selected_group].type == ChannelGroup::YCA_Channels));

        int  w = 0, h = 0, n = 0;
        auto px = img->as_interleaved<float>(&w, &h, &n, 1.f, TransferFunction::Linear, /*dither*/ false,
                                             /*unpremultiply*/ false, /*convert_to_sRGB*/ true);

        // the viewport's own value for the same pixel, which does not unpremultiply either
        float4 expected = img->rgba_pixel(int2{1, 0}, Target_Primary);
        for (int c = 0; c < 3; ++c) CHECK(px[n * 1 + c] == doctest::Approx(expected[c]).epsilon(1e-5));

        // and the raw stored triple is *not* what gets written
        CHECK(px[n * 1 + 0] != doctest::Approx(img->channels[0](1, 0)).epsilon(1e-5));
    }
}

TEST_CASE("save_exr_image() with default options writes every group, not an empty channel list")
{
    // The GUI sizes EXRSaveOptions::group_enabled per image; without it the static default enables
    // nothing, and OpenEXR refuses a header whose channel list is empty.
    auto               img = make_named({"R", "G", "B"});
    std::ostringstream out(std::ios::binary);
    REQUIRE_NOTHROW(save_exr_image(*img, out, "test.exr"));

    std::istringstream in(out.str(), std::ios::binary);
    auto               reloaded = load_exr_image(in, "test.exr");
    REQUIRE(reloaded.size() == 1);
    REQUIRE(reloaded[0]->channels.size() == 3);
    reloaded[0]->finalize(); // the per-format loaders leave this to load_image()
    // compared through the group, since OpenEXR stores channels in alphabetical order, and to half
    // precision, which is what EXRSaveOptions defaults to
    float4 want = img->rgba_pixel(int2{1, 0}, Target_Primary);
    float4 got  = reloaded[0]->rgba_pixel(int2{1, 0}, Target_Primary);
    for (int c = 0; c < 3; ++c) CHECK(got[c] == doctest::Approx(want[c]).epsilon(1e-3));
}

#if HDRVIEW_ENABLE_LIBJPEG
TEST_CASE("Saving a gray+alpha group as JPEG writes a grayscale file instead of terminating")
{
    // JPEG stores one or three components. A Y,A group is two, and the colour space was picked from
    // the image's total channel count rather than the group's, so libjpeg was handed a combination it
    // rejects -- through a default error handler that exits the process rather than reporting.
    auto img = make_named({"Y", "A"});
    REQUIRE(img->groups[img->selected_group].type == ChannelGroup::YA_Channels);

    std::ostringstream out(std::ios::binary);
    REQUIRE_NOTHROW(save_jpg_image(*img, out, "test.jpg", /*gain*/ 1.f, /*sRGB*/ true, /*dither*/ false,
                                   /*quality*/ 100, /*progressive*/ false));
    CHECK(out.str().size() > 0);

    std::istringstream in(out.str(), std::ios::binary);
    auto               reloaded = load_jpg_image(in, "test.jpg");
    REQUIRE(reloaded.size() == 1);
    CHECK(reloaded[0]->channels.size() == 1);
}

#endif

// AddressSanitizer reports a stack-use-after-scope inside libjxl's own JxlEncoderSetBasicInfo, in
// SizeHeader::Set -> FixedAspectRatios (lib/jxl/headers.cc). The read is kRatios[ratio - 1] with
// 0 < ratio < 8, in bounds of a 56-byte array, and ASan names the address as a *global* while
// classifying the shadow as stack -- the signature of use-after-scope instrumentation applied to a
// function-local constexpr array the optimizer emitted into .rodata. It reproduces on CI's clang and
// not on Apple clang, it depends only on the image's dimensions, and no HDRView state reaches it. The
// tests below are the first in the suite to write a JXL at all, which is why it only surfaced now.
#if defined(__SANITIZE_ADDRESS__)
#define HDRVIEW_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HDRVIEW_TEST_ASAN 1
#endif
#endif

#if HDRVIEW_ENABLE_LIBJXL && !defined(HDRVIEW_TEST_ASAN)
TEST_CASE("JPEG-XL lossless encoding succeeds and round-trips its samples exactly")
{
    // libjxl refuses lossless on an XYB-encoded frame, so the encoder has to keep the original profile.
    auto               img = make_named({"R", "G", "B"});
    std::ostringstream out(std::ios::binary);
    REQUIRE_NOTHROW(save_jxl_image(*img, out, "test.jxl", /*gain*/ 1.f, /*lossless*/ true, /*quality*/ 100.f,
                                   TransferFunction::Linear, JXL_TYPE_UINT16));

    std::istringstream in(out.str(), std::ios::binary);
    auto               reloaded = load_jxl_image(in, "test.jxl");
    REQUIRE(reloaded.size() == 1);
    for (int c = 0; c < 3; ++c)
        CHECK(reloaded[0]->channels[c](1, 0) == doctest::Approx(img->channels[c](1, 0)).epsilon(1e-4));
}

TEST_CASE("JPEG-XL writes grayscale and gray+alpha groups")
{
    // num_color_channels and the colour encoding both have to say gray; declaring RGB for a one- or
    // two-channel buffer makes libjxl reject the encode outright.
    for (auto names : {std::initializer_list<const char *>{"Y"}, std::initializer_list<const char *>{"Y", "A"}})
    {
        auto               img = make_named(names);
        std::ostringstream out(std::ios::binary);
        REQUIRE_NOTHROW(
            save_jxl_image(*img, out, "test.jxl", 1.f, false, 100.f, TransferFunction::sRGB, JXL_TYPE_UINT16));

        std::istringstream in(out.str(), std::ios::binary);
        auto               reloaded = load_jxl_image(in, "test.jxl");
        REQUIRE(reloaded.size() == 1);
        reloaded[0]->finalize(); // the per-format loaders leave this to load_image()
        // via rgba_pixel so both sides are premultiplied alike, whatever the group's alpha
        float4 want = img->rgba_pixel(int2{1, 0}, Target_Primary);
        float4 got  = reloaded[0]->rgba_pixel(int2{1, 0}, Target_Primary);
        for (int c = 0; c < 4; ++c) CHECK(got[c] == doctest::Approx(want[c]).epsilon(2e-3));
    }
}
#endif

TEST_CASE("save_pfm_image()'s explicit-parameter overload applies its transfer function")
{
    // The declaration and the definition had drifted apart: the header advertised a TransferFunction
    // where the definition still took (Type_, gamma), so this overload did not link, and the stray
    // gamma landed in as_interleaved()'s dither flag.
    auto               img = make_named({"R", "G", "B"});
    std::ostringstream lin(std::ios::binary), srgb(std::ios::binary);
    REQUIRE_NOTHROW(save_pfm_image(*img, lin, "test.pfm", 1.f, TransferFunction::Linear));
    REQUIRE_NOTHROW(save_pfm_image(*img, srgb, "test.pfm", 1.f, TransferFunction::sRGB));
    CHECK(lin.str() != srgb.str());

    std::istringstream in(lin.str(), std::ios::binary);
    auto               reloaded = load_pfm_image(in, "test.pfm");
    REQUIRE(reloaded.size() == 1);
    CHECK(reloaded[0]->channels[0](1, 0) == doctest::Approx(img->channels[0](1, 0)).epsilon(1e-5));
}

#if HDRVIEW_ENABLE_LIBHEIF
TEST_CASE("save_heif_image()'s explicit-parameter overload reports a missing encoder instead of crashing")
{
    // The encoder table is filled lazily by heif_parameters_gui(). A non-GUI caller found it empty,
    // and clamping an index against a size of zero yields SIZE_MAX.
    auto               img = make_named({"R", "G", "B"});
    std::ostringstream out(std::ios::binary);
    // Either it encodes, or it says there is no encoder -- but it must not fault.
    try
    {
        save_heif_image(*img, out, "test.heif", 1.f, 90, false, true, 0, TransferFunction::sRGB);
        CHECK(out.str().size() > 0);
    }
    catch (const std::exception &e)
    {
        CHECK(std::string(e.what()).find("encoder") != std::string::npos);
    }
}
#endif

#if HDRVIEW_ENABLE_LIBTIFF

namespace
{

// A 4x4 8-bit RGB baseline TIFF carrying a TransferFunction tag (tag 301). TIFF sizes that curve at
// 2^BitsPerSample entries -- 256 here -- so a reader that indexes it over the full 16-bit range runs
// far off the end of it. The curve written is the identity, which makes a correct decode recover the
// stored samples exactly.
std::string tiff_with_transfer_function(std::array<uint8_t, 3> pixel_1_0)
{
    std::string out;
    auto        u16 = [&out](uint16_t v)
    {
        out.push_back(char(v & 0xff));
        out.push_back(char(v >> 8));
    };
    auto u32 = [&out](uint32_t v)
    {
        for (int i = 0; i < 4; ++i) out.push_back(char((v >> (8 * i)) & 0xff));
    };

    out += "II"; // little-endian
    u16(42);     // magic
    const size_t ifd_offset_pos = out.size();
    u32(0); // patched below

    const uint32_t pixels_off = (uint32_t)out.size();
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 3; ++c) out.push_back(char((x == 1 && y == 0) ? pixel_1_0[c] : uint8_t(0)));

    const uint32_t bps_off = (uint32_t)out.size();
    for (int i = 0; i < 3; ++i) u16(8);

    const uint32_t lut_off = (uint32_t)out.size();
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) u16(uint16_t(i * 257)); // identity, 8-bit index to 16-bit value

    const uint32_t ifd_off = (uint32_t)out.size();
    struct Entry
    {
        uint16_t tag, type;
        uint32_t count, value;
    };
    const Entry entries[] = {
        {256, 3, 1, 4},          // ImageWidth
        {257, 3, 1, 4},          // ImageLength
        {258, 3, 3, bps_off},    // BitsPerSample
        {259, 3, 1, 1},          // Compression: none
        {262, 3, 1, 2},          // Photometric: RGB
        {273, 4, 1, pixels_off}, // StripOffsets
        {277, 3, 1, 3},          // SamplesPerPixel
        {278, 3, 1, 4},          // RowsPerStrip
        {279, 4, 1, 48},         // StripByteCounts
        {301, 3, 768, lut_off},  // TransferFunction
    };
    u16((uint16_t)std::size(entries));
    for (auto &e : entries)
    {
        u16(e.tag);
        u16(e.type);
        u32(e.count);
        // a SHORT value with count 1 sits in the low half of the value field
        if (e.type == 3 && e.count == 1)
        {
            u16((uint16_t)e.value);
            u16(0);
        }
        else
            u32(e.value);
    }
    u32(0); // no next IFD

    for (int i = 0; i < 4; ++i) out[ifd_offset_pos + i] = char((ifd_off >> (8 * i)) & 0xff);
    return out;
}

} // namespace

TEST_CASE("An 8-bit TIFF's TransferFunction curve is indexed over its own length, not the 16-bit range")
{
    const std::array<uint8_t, 3> stored{24, 72, 136};
    auto                         bytes = tiff_with_transfer_function(stored);

    std::istringstream in(bytes, std::ios::binary);
    auto               imgs = load_tiff_image(in, "tf.tif");
    REQUIRE(imgs.size() == 1);
    REQUIRE(imgs[0]->channels.size() == 3);

    // the identity curve has to give the stored samples back
    for (int c = 0; c < 3; ++c) CHECK(imgs[0]->channels[c](1, 0) == doctest::Approx(stored[c] / 255.f).epsilon(1e-4));
}
TEST_CASE("A wide-gamut image saved as TIFF records the primaries its samples are in")
{
    // The TIFF writer asks as_interleaved() not to convert to sRGB, so it keeps the image's own
    // primaries -- and every other writer either converts or records the gamut. Writing neither made
    // the file read back as sRGB: a Rec.2020 image round-tripped to visibly different colours.
    Chromaticities bt2020{{0.708f, 0.292f}, {0.170f, 0.797f}, {0.131f, 0.046f}, {0.3127f, 0.3290f}};

    auto img            = make_named({"R", "G", "B"});
    img->chromaticities = bt2020;
    color_conversion_matrix(img->M_to_sRGB, bt2020, gamut_chromaticities(ColorGamut_sRGB_BT709));
    float4 want = img->rgba_pixel(int2{1, 0}, Target_Primary);

    // 32-bit float, so the transfer function plays no part in what comes back
    std::ostringstream out(std::ios::binary);
    REQUIRE_NOTHROW(save_tiff_image(*img, out, "wide.tif", /*gain*/ 1.f, TransferFunction::Linear,
                                    /*compression*/ 1, /*data_type*/ 2));

    std::istringstream in(out.str(), std::ios::binary);
    auto               reloaded = load_tiff_image(in, "wide.tif");
    REQUIRE(reloaded.size() == 1);
    reloaded[0]->finalize(); // the per-format loaders leave this to load_image()

    REQUIRE(reloaded[0]->chromaticities.has_value());
    CHECK(reloaded[0]->chromaticities->red.x == doctest::Approx(bt2020.red.x).epsilon(1e-4));
    CHECK(reloaded[0]->chromaticities->green.y == doctest::Approx(bt2020.green.y).epsilon(1e-4));
    CHECK(reloaded[0]->chromaticities->white.x == doctest::Approx(bt2020.white.x).epsilon(1e-4));

    float4 got = reloaded[0]->rgba_pixel(int2{1, 0}, Target_Primary);
    for (int c = 0; c < 3; ++c) CHECK(got[c] == doctest::Approx(want[c]).epsilon(1e-3));
}
#endif

#if HDRVIEW_ENABLE_LIBHEIF
// Builds a gray image with a chosen alpha, kept high enough that unpremultiplying on the way out does
// not clamp the luma.
static ImagePtr make_gray(float luma, float alpha, bool with_alpha)
{
    const int2 size{16, 16};
    auto       img = std::make_shared<Image>();
    img->channels.emplace_back("Y", size);
    if (with_alpha)
        img->channels.emplace_back("A", size);
    img->display_window = img->data_window = Box2i{int2{0}, size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            img->channels[0](x, y) = luma;
            if (with_alpha)
                img->channels[1](x, y) = alpha;
        }
    img->finalize();
    return img;
}

TEST_CASE("HEIF stores a grayscale group as monochrome rather than three equal colour planes")
{
    // libheif has a monochrome colourspace, every encoder here accepts it, and load_heif_image() already
    // decodes one -- the writer was the only part still fixed at RGB, so gray could not be saved at all.
    auto               img = make_gray(0.30f, 1.f, /*with_alpha*/ false);
    std::ostringstream mono(std::ios::binary);
    REQUIRE_NOTHROW(save_heif_image(*img, mono, "gray.heif", 1.f, 100, true, true, 0, TransferFunction::sRGB));

    std::istringstream in(mono.str(), std::ios::binary);
    auto               reloaded = load_heif_image(in, "gray.heif");
    REQUIRE(reloaded.size() == 1);
    CHECK(reloaded[0]->channels.size() == 1); // one channel, not three
    CHECK(reloaded[0]->channels[0](3, 2) == doctest::Approx(0.30f).epsilon(5e-3));
}

TEST_CASE("A monochrome HEIF's alpha plane is stored and read back without a transfer function")
{
    // Alpha is not a colour. The interleaved paths hand it to linearize_pixels(), which leaves the last
    // channel alone, but a monochrome image keeps alpha in its own plane -- and the per-plane loop ran
    // the full colour linearization over it, so 0.55 came back as sRGB-decoded 0.26.
    auto               img  = make_gray(0.30f, 0.90f, /*with_alpha*/ true);
    float4             want = img->rgba_pixel(int2{3, 2}, Target_Primary);
    std::ostringstream out(std::ios::binary);
    REQUIRE_NOTHROW(save_heif_image(*img, out, "graya.heif", 1.f, 100, true, true, 0, TransferFunction::sRGB));

    std::istringstream in(out.str(), std::ios::binary);
    auto               reloaded = load_heif_image(in, "graya.heif");
    REQUIRE(reloaded.size() == 1);
    REQUIRE(reloaded[0]->channels.size() == 2);
    reloaded[0]->finalize(); // the per-format loaders leave this to load_image()

    float4 got = reloaded[0]->rgba_pixel(int2{3, 2}, Target_Primary);
    CHECK(got[3] == doctest::Approx(want[3]).epsilon(5e-3));
    CHECK(got[0] == doctest::Approx(want[0]).epsilon(5e-3));
}
#endif
