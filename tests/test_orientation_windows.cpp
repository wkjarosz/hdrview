//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"

#include <string>
#include <vector>

namespace
{

// Marks the samples outside the display window, so whether the window followed the pixels can be asked
// of the pixels themselves.
constexpr float k_outside = 999.f;
constexpr float k_inside  = 1.f;

// A 6x4 image whose display window covers only the left 4 columns; the two beyond hold the sentinel. This
// is the shape of a raw CFA part: the whole sensor frame as data, what LibRaw decodes as the display window.
ImagePtr make_marked_image(int orientation)
{
    const int2 size{6, 4};
    auto       img = std::make_shared<Image>(size, 1);

    img->data_window    = Box2i{{0, 0}, size};
    img->display_window = Box2i{{0, 0}, {4, 4}};

    auto &ch = img->channels[0];
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) ch(x, y) = (x < 4) ? k_inside : k_outside;

    img->metadata["exif"]["IFD0"]["Orientation"] = {{"value", orientation}};
    return img;
}

// Every sample the display window covers, and every sample it does not.
void check_partition(const ImagePtr &img, int orientation)
{
    const auto &ch  = img->channels[0];
    const auto  dsw = img->display_window;

    CAPTURE(orientation);
    REQUIRE(ch.size().x * ch.size().y == 24);
    // The display window keeps its area through any rotation or reflection.
    REQUIRE(dsw.size().x * dsw.size().y == 16);

    size_t inside = 0, outside = 0;
    for (int y = 0; y < ch.size().y; ++y)
        for (int x = 0; x < ch.size().x; ++x)
        {
            const bool in_window = x >= dsw.min.x && x < dsw.max.x && y >= dsw.min.y && y < dsw.max.y;
            const bool is_marked = ch(x, y) == k_outside;
            CAPTURE(x);
            CAPTURE(y);
            // A marked sample inside the window, or an unmarked one outside it, means the window did not
            // move with the pixels.
            CHECK(in_window != is_marked);
            in_window ? ++inside : ++outside;
        }
    CHECK(inside == 16);
    CHECK(outside == 8);
}

} // namespace

TEST_CASE("EXIF orientation carries the display window with the pixels")
{
    // 1 is a no-op; 2-8 are the reflections and rotations, and 4, 7 and 8 are the ones a flip moves.
    for (int orientation : {1, 2, 3, 4, 5, 6, 7, 8})
    {
        auto img = make_marked_image(orientation);
        img->apply_exif_orientation();
        check_partition(img, orientation);
    }
}

TEST_CASE("EXIF orientation transposes both windows")
{
    for (int orientation : {5, 6, 7, 8})
    {
        auto img = make_marked_image(orientation);
        img->apply_exif_orientation();

        CAPTURE(orientation);
        CHECK(img->data_window.size().x == 4);
        CHECK(img->data_window.size().y == 6);
        CHECK(img->display_window.size().x == 4);
        CHECK(img->display_window.size().y == 4);
    }

    for (int orientation : {1, 2, 3, 4})
    {
        auto img = make_marked_image(orientation);
        img->apply_exif_orientation();

        CAPTURE(orientation);
        CHECK(img->data_window.size().x == 6);
        CHECK(img->data_window.size().y == 4);
        CHECK(img->display_window.size().x == 4);
        CHECK(img->display_window.size().y == 4);
    }
}
