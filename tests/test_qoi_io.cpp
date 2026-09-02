//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/qoi.h"

#include <qoi.h>

#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// A single-pixel-per-value RGB QOI file holding `values` verbatim, tagged QOI_SRGB or QOI_LINEAR.
// qoi_encode() is used directly so the save path's gain/dither/transfer handling doesn't alter the bytes.
std::string encode_qoi_rgb(const std::vector<unsigned char> &values, unsigned char colorspace)
{
    const int      w = (int)values.size();
    const qoi_desc desc{(unsigned int)w, 1u, 3, colorspace};

    std::vector<unsigned char> interleaved;
    interleaved.reserve(values.size() * 3);
    for (auto v : values) interleaved.insert(interleaved.end(), {v, v, v});

    int                                          size = 0;
    std::unique_ptr<void, decltype(std::free) *> encoded{qoi_encode(interleaved.data(), &desc, &size), std::free};
    REQUIRE(encoded != nullptr);
    return std::string((const char *)encoded.get(), (size_t)size);
}

} // namespace

// Mid-gray 128 separates the two: 0.216 linearized, 0.502 left encoded.
TEST_CASE("QOI tagged sRGB is linearized on load")
{
    const std::vector<unsigned char> values{0, 64, 128, 192, 255};

    auto               bytes = encode_qoi_rgb(values, QOI_SRGB);
    std::istringstream in(bytes, std::ios::binary);

    auto loaded = load_qoi_image(in, "srgb.qoi");
    REQUIRE(loaded.size() == 1);
    const auto &img = loaded.front();
    REQUIRE(img->channels.size() == 3);

    for (int c = 0; c < 3; ++c)
        for (int x = 0; x < (int)values.size(); ++x)
        {
            const float encoded  = values[x] / 255.f;
            const float expected = sRGB_to_linear(encoded);
            CHECK(img->channels[c](x, 0) == doctest::Approx(expected).epsilon(1e-5));
        }

    CHECK(img->channels[0](2, 0) == doctest::Approx(0.21586f).epsilon(1e-4));
    CHECK(img->channels[0](2, 0) != doctest::Approx(128.f / 255.f).epsilon(1e-4));
}

TEST_CASE("QOI tagged linear is not linearized on load")
{
    const std::vector<unsigned char> values{0, 64, 128, 192, 255};

    auto               bytes = encode_qoi_rgb(values, QOI_LINEAR);
    std::istringstream in(bytes, std::ios::binary);

    auto loaded = load_qoi_image(in, "linear.qoi");
    REQUIRE(loaded.size() == 1);
    const auto &img = loaded.front();
    REQUIRE(img->channels.size() == 3);

    for (int c = 0; c < 3; ++c)
        for (int x = 0; x < (int)values.size(); ++x)
            CHECK(img->channels[c](x, 0) == doctest::Approx(values[x] / 255.f).epsilon(1e-5));
}
