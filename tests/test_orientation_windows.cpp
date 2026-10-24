//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"

#include "test_support.h"

using namespace hdrview_test;

namespace
{

/// Where the EXIF spec puts the stored image's 0th row and 0th column once it is displayed.
enum Side
{
    Top,
    Bottom,
    Left,
    Right
};

struct Orientation
{
    int  value;
    Side row; ///< the visual side the 0th row lies along
    Side col; ///< the visual side the 0th column lies along
};

// The tag's eight values, as the spec words them ("the 0th row is at the visual top of the image, and the
// 0th column is the visual left-hand side", and so on).
constexpr Orientation k_orientations[] = {
    {1, Top, Left}, {2, Top, Right}, {3, Bottom, Right}, {4, Bottom, Left},
    {5, Left, Top}, {6, Right, Top}, {7, Right, Bottom}, {8, Left, Bottom},
};

/// Where stored sample \p p of a \p size image lands on screen under \p o.
int2 displayed(const Orientation &o, int2 p, int2 size)
{
    // the row index runs away from the side the 0th row lies along, and the column index from the 0th column's
    if (o.row == Top || o.row == Bottom)
        return int2{o.col == Left ? p.x : size.x - 1 - p.x, o.row == Top ? p.y : size.y - 1 - p.y};
    return int2{o.row == Left ? p.y : size.y - 1 - p.y, o.col == Top ? p.x : size.x - 1 - p.x};
}

} // namespace

TEST_CASE("EXIF orientation turns the samples as the spec says, and carries the display window with them")
{
    // Every sample distinct, so a swapped pair of orientations shows. The display window is not square, off
    // the origin and off center both ways, so a window that stays put, or is reflected or transposed when
    // the samples were not, shows too: the shape of a raw CFA part, whose data window is the whole sensor
    // frame and whose display window is its active area.
    const int2  size{7, 5};
    const Box2i display{int2{2, 1}, int2{6, 3}};
    auto        value = [](int2 p) { return 1.f + float(p.x) + 10.f * float(p.y); };

    for (const auto &o : k_orientations)
    {
        CAPTURE(o.value);

        auto img            = std::make_shared<Image>(size, 1);
        img->data_window    = Box2i{int2{0}, size};
        img->display_window = display;
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) img->channels[0](x, y) = value(int2{x, y});
        img->metadata["exif"]["IFD0"]["Orientation"] = {{"value", o.value}};

        img->apply_exif_orientation();

        const auto &ch         = img->channels[0];
        const bool  transposed = o.row == Left || o.row == Right;
        const int2  turned     = transposed ? int2{size.y, size.x} : size;
        const int2  window     = transposed ? int2{display.size().y, display.size().x} : display.size();

        REQUIRE(ch.size() == turned);
        CHECK(img->data_window.size() == ch.size());
        CHECK(img->display_window.size() == window);

        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                const int2 p{x, y};
                const int2 q = displayed(o, p, size);
                CAPTURE(x);
                CAPTURE(y);

                CHECK(ch(q.x, q.y) == value(p));
                // a sample inside the display window before is inside it after, and one outside stays outside
                CHECK(img->display_window.contains(img->data_window.min + q) == display.contains(p));
            }
    }
}
