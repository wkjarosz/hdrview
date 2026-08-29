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
