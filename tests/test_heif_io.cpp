//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/heif.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#if HDRVIEW_ENABLE_LIBHEIF

namespace
{

ImagePtr make_rgb(int2 size = int2{16, 12})
{
    auto img = std::make_shared<Image>(size, 3);
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) img->channels[c](x, y) = 0.2f + 0.2f * c;
    img->finalize();
    return img;
}

/// The major brand out of the file's ftyp box: 4 bytes of size, 'ftyp', then the brand.
std::string major_brand(const std::string &bytes)
{
    REQUIRE(bytes.size() >= 12);
    CHECK(bytes.compare(4, 4, "ftyp") == 0);
    return bytes.substr(8, 4);
}

} // namespace

TEST_CASE("The HEIF and AVIF save entries write files branded as what their extension claims")
{
    // libheif brands the file after the primary item's codec, so an AV1 item makes an 'avif' file
    auto img = make_rgb();

    SUBCASE("HEIF")
    {
        std::ostringstream out(std::ios::binary);
        try
        {
            save_heif_image(*img, out, "a.heif", 1.f, 90, false, true, HEIFCodec::HEIF, TransferFunction::sRGB);
        }
        catch (const std::exception &e)
        {
            // which codecs encode at all depends on the plugins this libheif build found
            MESSAGE("no HEIF written here: ", e.what());
            return;
        }
        CHECK(major_brand(out.str()) != "avif");
    }

    SUBCASE("AVIF")
    {
        std::ostringstream out(std::ios::binary);
        try
        {
            save_heif_image(*img, out, "a.avif", 1.f, 90, false, true, HEIFCodec::AV1, TransferFunction::sRGB);
        }
        catch (const std::exception &e)
        {
            MESSAGE("no AVIF written here: ", e.what());
            return;
        }
        CHECK(major_brand(out.str()) == "avif");
    }
}

#endif

TEST_CASE("The HEIF and AVIF entries offer disjoint encoders")
{
    // aom offered under HEIF would write a file libheif brands 'avif' under a .heif name
    const auto heif = heif_encoder_names(HEIFCodec::HEIF);
    const auto avif = heif_encoder_names(HEIFCodec::AV1);

    // a build with no encoder at all has nothing to say here
    if (heif.empty() && avif.empty())
        return;

    for (const auto &a : avif)
        CHECK_MESSAGE(std::find(heif.begin(), heif.end(), a) == heif.end(), "AV1 encoder also offered under HEIF: ", a);
}
