//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// The HDR light-level metadata loaders record: a content light level (MaxCLL, MaxFALL) and the mastering
// display's color volume, read back from files that carry them.

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/hdr_metadata.h"
#include "imageio/image_loader.h"
#include "imageio/png.h"
#include "test_support.h"

#include <png.h>
#if HDRVIEW_ENABLE_LIBHEIF
#include <libheif/heif.h>
#endif

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

using namespace hdrview_test;

namespace
{

// What each file is made to carry, on every format's quantization grid: chromaticities in steps of 0.00002,
// luminances in steps of 0.0001 cd/m^2, and whole cd/m^2 for the content light level
const float          k_max_cll = 1000.f, k_max_fall = 400.f;
const float          k_min_nits = 0.005f, k_max_nits = 1000.f;
const Chromaticities k_display{{0.68f, 0.32f}, {0.265f, 0.69f}, {0.15f, 0.06f}, {0.3127f, 0.329f}}; // P3 at D65

uint32_t crc32(const std::string &data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : data)
    {
        crc ^= c;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/// A PNG chunk: its length, type, data and the CRC over type and data.
std::string png_chunk(const char type[4], const std::vector<uint8_t> &data)
{
    std::string type_and_data(type, 4);
    type_and_data.append(data.begin(), data.end());
    std::string out;
    put<uint32_t>(out, (uint32_t)data.size(), Endian::Big);
    out += type_and_data;
    put<uint32_t>(out, crc32(type_and_data), Endian::Big);
    return out;
}

uint16_t xy_units(float v) { return (uint16_t)std::lround(v / 0.00002f); }
uint32_t nit_units(float v) { return (uint32_t)std::lround(double(v) * 10000.0); }

/// A PNG of HDRView's own making, with cLLI and mDCV chunks spliced in after its IHDR.
std::string png_with_light_levels()
{
    auto img = std::make_shared<Image>(int2{4, 4}, 3);
    img->finalize();
    std::ostringstream out;
    save_png_image(*img, out, "a.png");
    std::string bytes = out.str();

    std::vector<uint8_t> cLLI, mDCV;
    put<uint32_t>(cLLI, nit_units(k_max_cll), Endian::Big);
    put<uint32_t>(cLLI, nit_units(k_max_fall), Endian::Big);
    for (float2 xy : {k_display.red, k_display.green, k_display.blue, k_display.white})
    {
        put<uint16_t>(mDCV, xy_units(xy.x), Endian::Big);
        put<uint16_t>(mDCV, xy_units(xy.y), Endian::Big);
    }
    put<uint32_t>(mDCV, nit_units(k_max_nits), Endian::Big);
    put<uint32_t>(mDCV, nit_units(k_min_nits), Endian::Big);

    // the signature, then IHDR's length, type, 13 bytes of data and CRC
    const size_t after_IHDR = 8 + 4 + 4 + 13 + 4;
    return bytes.substr(0, after_IHDR) + png_chunk("cLLI", cLLI) + png_chunk("mDCV", mDCV) + bytes.substr(after_IHDR);
}

#if HDRVIEW_ENABLE_LIBHEIF
/// An AVIF whose clli and mdcv boxes say the same as png_with_light_levels(), or "" without an AV1 encoder.
std::string avif_with_light_levels()
{
    std::unique_ptr<heif_context, decltype(&heif_context_free)> ctx(heif_context_alloc(), heif_context_free);
    heif_encoder                                               *enc_raw = nullptr;
    if (heif_context_get_encoder_for_format(ctx.get(), heif_compression_AV1, &enc_raw).code != heif_error_Ok)
        return {};
    std::unique_ptr<heif_encoder, decltype(&heif_encoder_release)> enc(enc_raw, heif_encoder_release);

    heif_image *img_raw = nullptr;
    REQUIRE(heif_image_create(16, 16, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img_raw).code ==
            heif_error_Ok);
    std::unique_ptr<heif_image, decltype(&heif_image_release)> img(img_raw, heif_image_release);
    REQUIRE(heif_image_add_plane(img.get(), heif_channel_interleaved, 16, 16, 8).code == heif_error_Ok);
    int      stride = 0;
    uint8_t *plane  = heif_image_get_plane(img.get(), heif_channel_interleaved, &stride);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16 * 3; ++x) plane[y * stride + x] = 128;

    heif_content_light_level clli{(uint16_t)k_max_cll, (uint16_t)k_max_fall};
    heif_image_set_content_light_level(img.get(), &clli);

    // in the order H.265 suggests, green, blue, red, which the loader has to sort back out
    heif_mastering_display_colour_volume mdcv{};
    const float2                         order[3] = {k_display.green, k_display.blue, k_display.red};
    for (int i = 0; i < 3; ++i)
    {
        mdcv.display_primaries_x[i] = xy_units(order[i].x);
        mdcv.display_primaries_y[i] = xy_units(order[i].y);
    }
    mdcv.white_point_x                   = xy_units(k_display.white.x);
    mdcv.white_point_y                   = xy_units(k_display.white.y);
    mdcv.max_display_mastering_luminance = nit_units(k_max_nits);
    mdcv.min_display_mastering_luminance = nit_units(k_min_nits);
    heif_image_set_mastering_display_colour_volume(img.get(), &mdcv);

    heif_image_handle *handle = nullptr;
    REQUIRE(heif_context_encode_image(ctx.get(), img.get(), enc.get(), nullptr, &handle).code == heif_error_Ok);
    heif_image_handle_release(handle);

    std::string bytes;
    heif_writer writer{1, [](heif_context *, const void *data, size_t size, void *userdata) -> heif_error
                       {
                           static_cast<std::string *>(userdata)->append(static_cast<const char *>(data), size);
                           return heif_error_success;
                       }};
    REQUIRE(heif_context_write(ctx.get(), &writer, &bytes).code == heif_error_Ok);
    return bytes;
}
#endif

} // namespace

TEST_CASE("Every format that carries HDR light levels reports them the same way")
{
    struct Case
    {
        const char *name, *filename;
        std::string bytes;
    };
    std::vector<Case> cases;
#if defined(PNG_cLLI_SUPPORTED) && defined(PNG_mDCV_SUPPORTED)
    cases.push_back({"PNG", "a.png", png_with_light_levels()});
#endif
#if HDRVIEW_ENABLE_LIBHEIF
    if (auto avif = avif_with_light_levels(); !avif.empty())
        cases.push_back({"AVIF", "a.avif", avif});
#endif
    if (cases.empty())
        return; // this build reads light levels from no format

    json first_cll, first_md; // the first format's entries, which every other has to match
    for (const Case &c : cases)
    {
        INFO("format = ", std::string(c.name));
        auto img = load_bytes(c.bytes, c.filename);
        REQUIRE(img);
        const json &header = img->metadata["header"];
        REQUIRE(header.contains(k_content_light_level_key));
        REQUIRE(header.contains(k_mastering_display_key));

        const json &cll = header[k_content_light_level_key]["value"];
        CHECK(cll["MaxCLL"].get<float>() == doctest::Approx(k_max_cll));
        CHECK(cll["MaxFALL"].get<float>() == doctest::Approx(k_max_fall));

        const json &md = header[k_mastering_display_key]["value"];
        CHECK(approx_equal(md["red"].get<float2>(), k_display.red, 1e-5f));
        CHECK(approx_equal(md["green"].get<float2>(), k_display.green, 1e-5f));
        CHECK(approx_equal(md["blue"].get<float2>(), k_display.blue, 1e-5f));
        CHECK(approx_equal(md["white"].get<float2>(), k_display.white, 1e-5f));
        CHECK(md["min luminance"].get<float>() == doctest::Approx(k_min_nits).epsilon(1e-4));
        CHECK(md["max luminance"].get<float>() == doctest::Approx(k_max_nits).epsilon(1e-6));

        // and every format writes the same entries, which is what lets a reader look them up by key alone
        if (first_cll.is_null())
        {
            first_cll = cll;
            first_md  = md;
        }
        else
        {
            CHECK(first_cll == cll);
            CHECK(first_md == md);
        }
    }

    // a file that carries neither reports neither, rather than zeros
    auto plain = std::make_shared<Image>(int2{4, 4}, 3);
    plain->finalize();
    std::ostringstream out;
    save_png_image(*plain, out, "a.png");
    auto reloaded = load_bytes(out.str(), "a.png");
    REQUIRE(reloaded);
    CHECK_FALSE(reloaded->metadata["header"].contains(k_content_light_level_key));
    CHECK_FALSE(reloaded->metadata["header"].contains(k_mastering_display_key));
}
