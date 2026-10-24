//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/heif.h"

#include "test_support.h"

using namespace hdrview_test;

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#if HDRVIEW_ENABLE_LIBHEIF

#include <libheif/heif.h>

namespace
{

ImagePtr make_rgb(int2 size = int2{16, 12})
{
    return test_image(size, 3, [](int c, int, int) { return 0.2f + 0.2f * float(c); });
}

/// The major brand out of the file's ftyp box: 4 bytes of size, 'ftyp', then the brand.
std::string major_brand(const std::string &bytes)
{
    REQUIRE(bytes.size() >= 12);
    CHECK(bytes.compare(4, 4, "ftyp") == 0);
    return bytes.substr(8, 4);
}

/// What a saved file's primary image says about itself, read with libheif rather than HDRView's loader.
struct Written
{
    int bit_depth = 0;  ///< the luma's bits per sample
    int transfer  = -1; ///< the nclx transfer characteristics, or -1 when there is no nclx
    int primaries = -1; ///< the nclx color primaries, or -1 when there is no nclx
};

Written written(const std::string &bytes)
{
    Written                                                 w;
    std::unique_ptr<heif_context, void (*)(heif_context *)> ctx(heif_context_alloc(), heif_context_free);
    REQUIRE(heif_context_read_from_memory_without_copy(ctx.get(), bytes.data(), bytes.size(), nullptr).code ==
            heif_error_Ok);
    heif_image_handle *raw = nullptr;
    REQUIRE(heif_context_get_primary_image_handle(ctx.get(), &raw).code == heif_error_Ok);
    std::unique_ptr<heif_image_handle, void (*)(const heif_image_handle *)> handle(raw, heif_image_handle_release);
    w.bit_depth                   = heif_image_handle_get_luma_bits_per_pixel(handle.get());
    heif_color_profile_nclx *nclx = nullptr;
    if (heif_image_handle_get_nclx_color_profile(handle.get(), &nclx).code == heif_error_Ok && nclx)
    {
        w.transfer  = nclx->transfer_characteristics;
        w.primaries = nclx->color_primaries;
        heif_nclx_color_profile_free(nclx);
    }
    return w;
}

} // namespace

TEST_CASE("The HEIF and AVIF save entries write files branded and labeled as what they hold")
{
    // libheif brands the file after the primary item's codec, so an AV1 item makes an 'avif' file. The nclx
    // reaches the file only through the encoding options, and the bit depth only through the image's planes.
    auto img = make_rgb();

    for (auto codec : {HEIFCodec::HEIF, HEIFCodec::AV1})
        for (auto tf : {TransferFunction::sRGB, TransferFunction::BT2100_PQ})
        {
            const bool avif = codec == HEIFCodec::AV1;
            INFO(std::string(avif ? "AVIF" : "HEIF"), ", ", transfer_function_name(tf));
            std::ostringstream out(std::ios::binary);
            try
            {
                save_heif_image(*img, out, avif ? "a.avif" : "a.heif", 90, false, true, codec, tf);
            }
            catch (const std::exception &e)
            {
                // which codecs encode at all depends on the plugins this libheif build found
                MESSAGE("nothing written here: ", e.what());
                continue;
            }
            const std::string brand = major_brand(out.str());
            CHECK((brand == "avif") == avif);

            const Written w = written(out.str());
            CHECK(w.transfer == (tf == TransferFunction::sRGB ? heif_transfer_characteristic_IEC_61966_2_1
                                                              : heif_transfer_characteristic_ITU_R_BT_2100_0_PQ));
            CHECK(w.primaries == heif_color_primaries_ITU_R_BT_709_5); // the image records none, so sRGB's
            // the automatic bit depth is 10 for PQ; a .heif may hold JPEG, which has only 8, so only HEVC is held to it
            if (avif || brand.rfind("hei", 0) == 0)
                CHECK(w.bit_depth == (tf == TransferFunction::sRGB ? 8 : 10));

            // and the loader reads it on its own grid, whatever the codec did to the values
            if (w.bit_depth == 8)
            {
                const auto back = load_bytes(out.str(), avif ? "a.avif" : "a.heif");
                REQUIRE(back);
                float off_grid = 0.f;
                for (float v : samples(back))
                {
                    const float code = 255.f * from_linear(v, tf);
                    off_grid         = std::max(off_grid, std::abs(code - std::round(code)));
                }
                CHECK(off_grid < 0.01f);
            }
        }
}

#endif
