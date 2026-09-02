//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/undo.h"
#include "image.h"

#include <memory>
#include <string>
#include <vector>

namespace
{

// A distinct value per sample per channel, so a transposition, reflection or off-by-one in a restore shows
// up as a mismatch instead of coinciding with a neighbor. Not square, since a square image hides an axis swap.
constexpr int2 k_size{5, 3};

float expected(int c, int x, int y) { return float(c * 1000 + y * 10 + x); }

ImagePtr make_test_image(int num_channels = 3)
{
    auto img = std::make_shared<Image>(k_size, num_channels);
    for (int c = 0; c < num_channels; ++c)
    {
        auto &ch = img->channels[size_t(c)];
        for (int y = 0; y < k_size.y; ++y)
            for (int x = 0; x < k_size.x; ++x) ch(x, y) = expected(c, x, y);
    }
    return img;
}

// Every sample of every channel, against the value make_test_image() put there.
bool matches_original(const ImagePtr &img)
{
    if (img->size() != k_size)
        return false;
    for (int c = 0; c < int(img->channels.size()); ++c)
        for (int y = 0; y < k_size.y; ++y)
            for (int x = 0; x < k_size.x; ++x)
                if (img->channels[size_t(c)](x, y) != expected(c, x, y))
                    return false;
    return true;
}

// Everything the geometric operations preserve or change, as one description.
struct Shape
{
    int2  size;
    Box2i data_window, display_window;
};

Shape shape_of(const ImagePtr &img) { return {img->size(), img->data_window, img->display_window}; }

bool operator==(const Shape &a, const Shape &b)
{
    return a.size == b.size && a.data_window == b.data_window && a.display_window == b.display_window;
}

} // namespace

TEST_CASE("Each geometric operation is undone exactly by its opposite")
{
    // Undoing a flip or quarter turn stores no pixels, which is sound only while the round trip is
    // bit-identical: no sample lost, none resampled.
    struct Case
    {
        const char *name;
        void (Image::*forward)();
        void (Image::*backward)();
    };
    const Case cases[] = {
        {"flip horizontally", &Image::flip_horizontal, &Image::flip_horizontal},
        {"flip vertically", &Image::flip_vertical, &Image::flip_vertical},
        {"transpose", &Image::transpose, &Image::transpose},
        {"rotate 90 cw", &Image::rotate_90_cw, &Image::rotate_90_ccw},
        {"rotate 90 ccw", &Image::rotate_90_ccw, &Image::rotate_90_cw},
    };

    for (const auto &c : cases)
    {
        CAPTURE(c.name);
        auto        img      = make_test_image();
        const Shape original = shape_of(img);

        (img.get()->*c.forward)();
        (img.get()->*c.backward)();

        CHECK(shape_of(img) == original);
        CHECK(matches_original(img));
    }
}

TEST_CASE("A quarter turn transposes the image and four of them return it")
{
    auto img = make_test_image();

    img->rotate_90_cw();
    // not square, so a turn that failed to swap the axes shows here
    CHECK(img->size() == int2{k_size.y, k_size.x});

    img->rotate_90_cw();
    CHECK(img->size() == k_size);

    img->rotate_90_cw();
    img->rotate_90_cw();
    CHECK(img->size() == k_size);
    CHECK(matches_original(img));
}

TEST_CASE("Turning one way then the other is the same as not turning at all")
{
    // the two directions have to be each other's inverse for LambdaUndo to be sound, and each is composed
    // out of a transpose and a flip
    auto cw_then_ccw = make_test_image();
    cw_then_ccw->rotate_90_cw();
    cw_then_ccw->rotate_90_ccw();
    CHECK(matches_original(cw_then_ccw));

    auto ccw_then_cw = make_test_image();
    ccw_then_cw->rotate_90_ccw();
    ccw_then_cw->rotate_90_cw();
    CHECK(matches_original(ccw_then_cw));
}

TEST_CASE("A flip carries the display window with the samples")
{
    // a display window narrower than the data window is the shape of a raw CFA part
    auto img            = make_test_image(1);
    img->data_window    = Box2i{{0, 0}, k_size};
    img->display_window = Box2i{{0, 0}, {2, k_size.y}};

    img->flip_horizontal();
    CHECK(img->display_window.min.x == k_size.x - 2);
    CHECK(img->display_window.max.x == k_size.x);

    img->flip_horizontal();
    CHECK(img->display_window.min.x == 0);
    CHECK(img->display_window.max.x == 2);
}

TEST_CASE("A rectangle entry restores the channels it saved and nothing else")
{
    auto img = make_test_image();

    const Box2i     bounds{{1, 1}, {3, 2}}; // half-open: x in [1,3), y in [1,2)
    const int       edited_channel = 1;
    ChannelRectUndo entry{*img, {edited_channel}, bounds, "Test"};

    // overwrite more than the entry covers, so undoing has to leave the excess alone
    auto &ch = img->channels[size_t(edited_channel)];
    for (int y = 0; y < k_size.y; ++y)
        for (int x = 0; x < k_size.x; ++x) ch(x, y) = -1.f;

    entry.undo(*img);

    for (int y = 0; y < k_size.y; ++y)
        for (int x = 0; x < k_size.x; ++x)
        {
            CAPTURE(x);
            CAPTURE(y);
            const bool covered = x >= bounds.min.x && x < bounds.max.x && y >= bounds.min.y && y < bounds.max.y;
            CHECK(ch(x, y) == (covered ? expected(edited_channel, x, y) : -1.f));
        }

    // untouched channels stay untouched
    CHECK(img->channels[0](2, 1) == expected(0, 2, 1));
    CHECK(img->channels[2](2, 1) == expected(2, 2, 1));
}

TEST_CASE("Undoing and redoing a rectangle entry cycles between the two states")
{
    auto  img = make_test_image(1);
    auto &ch  = img->channels[0];

    const Box2i     bounds{{0, 0}, k_size};
    ChannelRectUndo entry{*img, {0}, bounds, "Test"};

    ch(2, 1) = -5.f;

    entry.undo(*img);
    CHECK(ch(2, 1) == expected(0, 2, 1));

    // the entry came out of undo() holding what it displaced, so redo is the same swap
    entry.redo(*img);
    CHECK(ch(2, 1) == -5.f);

    entry.undo(*img);
    CHECK(ch(2, 1) == expected(0, 2, 1));
}

TEST_CASE("A rectangle entry clipped to the image saves only the part that lands")
{
    auto img = make_test_image(1);

    // hangs off the right and bottom edges
    ChannelRectUndo entry{*img, {0}, Box2i{{3, 2}, {99, 99}}, "Test"};

    auto &ch = img->channels[0];
    ch(4, 2) = -1.f;
    ch(0, 0) = -2.f;

    entry.undo(*img);

    CHECK(ch(4, 2) == expected(0, 4, 2)); // inside the clipped rectangle
    CHECK(ch(0, 0) == -2.f);              // outside it
}

TEST_CASE("The history cursor tracks what can be undone and redone")
{
    auto           img = make_test_image(1);
    CommandHistory history;

    CHECK_FALSE(history.has_undo());
    CHECK_FALSE(history.has_redo());

    int  applied        = 0;
    auto counting_entry = [&applied](std::string name)
    { return std::make_unique<LambdaUndo>(std::move(name), [&applied](Image &) { ++applied; }); };

    history.add(counting_entry("First"));
    history.add(counting_entry("Second"));
    CHECK(history.size() == 2);
    CHECK(history.has_undo());
    CHECK_FALSE(history.has_redo());
    CHECK(history.undo_name() == "Second");

    CHECK(history.undo(*img));
    CHECK(applied == 1);
    CHECK(history.has_redo());
    CHECK(history.undo_name() == "First");
    CHECK(history.redo_name() == "Second");

    CHECK(history.redo(*img));
    CHECK(applied == 2);
    CHECK_FALSE(history.has_redo());

    // walking off either end does nothing, and does not run an entry twice
    CHECK(history.undo(*img));
    CHECK(history.undo(*img));
    CHECK_FALSE(history.undo(*img));
    CHECK(applied == 4);
}

TEST_CASE("A new edit discards what had been undone")
{
    auto           img = make_test_image(1);
    CommandHistory history;

    auto noop = [](std::string name) { return std::make_unique<LambdaUndo>(std::move(name), [](Image &) {}); };

    history.add(noop("First"));
    history.add(noop("Second"));
    history.undo(*img);
    REQUIRE(history.has_redo());

    history.add(noop("Third"));
    CHECK_FALSE(history.has_redo());
    CHECK(history.size() == 2);
    CHECK(history.undo_name() == "Third");
}

TEST_CASE("The modified flag follows the distance from the last save in both directions")
{
    auto           img = make_test_image(1);
    CommandHistory history;

    auto noop = [](std::string name) { return std::make_unique<LambdaUndo>(std::move(name), [](Image &) {}); };

    CHECK_FALSE(history.is_modified());

    history.add(noop("First"));
    CHECK(history.is_modified());

    history.mark_saved();
    CHECK_FALSE(history.is_modified());

    // editing forward past a save
    history.add(noop("Second"));
    CHECK(history.is_modified());

    // ...and coming back to it
    history.undo(*img);
    CHECK_FALSE(history.is_modified());

    // undoing back past a save is as much a difference from the file as editing past it
    history.undo(*img);
    CHECK(history.is_modified());

    history.redo(*img);
    CHECK_FALSE(history.is_modified());
}

TEST_CASE("A save point that is discarded cannot be returned to")
{
    auto           img = make_test_image(1);
    CommandHistory history;

    auto noop = [](std::string name) { return std::make_unique<LambdaUndo>(std::move(name), [](Image &) {}); };

    history.add(noop("First"));
    history.add(noop("Second"));
    history.mark_saved();
    REQUIRE_FALSE(history.is_modified());

    // undoing and then editing drops the entry the save point sat on, so no amount of undoing gets back to a
    // state that matches the file
    history.undo(*img);
    history.add(noop("Third"));
    CHECK(history.is_modified());

    history.undo(*img);
    CHECK(history.is_modified());
    history.undo(*img);
    CHECK(history.is_modified());
}

TEST_CASE("An image starts unmodified and stays that way until something edits it")
{
    auto img = make_test_image();
    CHECK_FALSE(img->history.is_modified());
    CHECK_FALSE(img->history.has_undo());
    CHECK_FALSE(img->history.has_redo());
}

TEST_CASE("Rebuilding an image's layers replaces them rather than adding to them")
{
    // finalize() appends as it walks the channels, so anything left from an earlier build is duplicated: a
    // second leaf per node, and channel counts that no longer agree with the channels themselves
    auto img = make_test_image();
    img->finalize();

    const size_t layers = img->layers.size();
    const size_t groups = img->groups.size();
    REQUIRE(layers > 0);
    REQUIRE(groups > 0);

    // whatever an edit does, asking for the tree again describes the same channels
    img->finalize();

    CHECK(img->layers.size() == layers);
    CHECK(img->groups.size() == groups);
}

TEST_CASE("A geometric edit leaves the layer and group structure alone")
{
    // flips and quarter turns move samples without touching the channel set, so nothing about the layers
    // changes and nothing needs rebuilding
    auto img = make_test_image();
    img->finalize();

    const size_t layers   = img->layers.size();
    const size_t groups   = img->groups.size();
    const size_t channels = img->channels.size();

    img->rotate_90_cw();
    img->flip_vertical();

    CHECK(img->layers.size() == layers);
    CHECK(img->groups.size() == groups);
    CHECK(img->channels.size() == channels);
}

TEST_CASE("Cropping keeps the samples inside the box and makes them the whole image")
{
    auto img = make_test_image();
    img->finalize();

    const Box2i box{{1, 1}, {4, 3}}; // half-open: 3 wide, 2 tall
    img->crop(box);

    CHECK(img->size() == int2{3, 2});
    CHECK(img->data_window == box);
    // what is left is the whole image, not a crop sitting inside the old canvas
    CHECK(img->display_window == box);

    for (int c = 0; c < int(img->channels.size()); ++c)
    {
        CHECK(img->channels[size_t(c)].size() == int2{3, 2});
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
            {
                CAPTURE(x);
                CAPTURE(y);
                // the sample that was at box.min + (x,y) before
                CHECK(img->channels[size_t(c)](x, y) == expected(c, x + box.min.x, y + box.min.y));
            }
    }
}

TEST_CASE("Cropping to nothing leaves the image alone")
{
    auto img = make_test_image();
    img->finalize();

    // entirely outside the data window, so the intersection is empty
    img->crop(Box2i{{20, 20}, {30, 30}});

    CHECK(img->size() == k_size);
    CHECK(matches_original(img));
}

TEST_CASE("Growing the canvas keeps the samples and zero-fills the rest")
{
    auto img = make_test_image(1);
    img->finalize();

    // anchored top-left, so the old samples stay at the origin and the new space is added right and below
    img->resize_canvas(int2{k_size.x + 2, k_size.y + 1}, Image::Anchor_TopLeft);

    CHECK(img->size() == int2{k_size.x + 2, k_size.y + 1});

    const auto &ch = img->channels[0];
    for (int y = 0; y < ch.size().y; ++y)
        for (int x = 0; x < ch.size().x; ++x)
        {
            CAPTURE(x);
            CAPTURE(y);
            const bool inside = x < k_size.x && y < k_size.y;
            CHECK(ch(x, y) == (inside ? expected(0, x, y) : 0.f));
        }
}

TEST_CASE("The anchor decides which edges absorb the change")
{
    // grown by two columns anchored right, so the new space lands on the left and the old first column is
    // now the third
    auto img = make_test_image(1);
    img->finalize();
    img->resize_canvas(int2{k_size.x + 2, k_size.y}, Image::Anchor_MiddleRight);

    const auto &ch = img->channels[0];
    CHECK(ch(0, 0) == 0.f);
    CHECK(ch(1, 0) == 0.f);
    CHECK(ch(2, 0) == expected(0, 0, 0));
}

TEST_CASE("Shrinking the canvas discards what falls outside it")
{
    auto img = make_test_image(1);
    img->finalize();

    // anchored top-left, so the right and bottom edges are the ones cut
    img->resize_canvas(int2{k_size.x - 2, k_size.y - 1}, Image::Anchor_TopLeft);

    CHECK(img->size() == int2{k_size.x - 2, k_size.y - 1});
    const auto &ch = img->channels[0];
    for (int y = 0; y < ch.size().y; ++y)
        for (int x = 0; x < ch.size().x; ++x) CHECK(ch(x, y) == expected(0, x, y));
}

TEST_CASE("A structural entry restores the samples, the windows, and the layer tree")
{
    auto img = make_test_image();
    img->finalize();

    const size_t layers  = img->layers.size();
    const size_t groups  = img->groups.size();
    const Box2i  data    = img->data_window;
    const Box2i  display = img->display_window;

    StructureUndo entry{*img, "Crop"};
    img->crop(Box2i{{1, 1}, {3, 2}});
    img->rebuild_layers();
    REQUIRE(img->size() != k_size);

    entry.undo(*img);

    CHECK(img->size() == k_size);
    CHECK(img->data_window == data);
    CHECK(img->display_window == display);
    CHECK(matches_original(img));
    // rebuilt from the restored channels, not left describing the cropped ones
    CHECK(img->layers.size() == layers);
    CHECK(img->groups.size() == groups);

    // and redo returns to the cropped state, since the entry came out holding it
    entry.redo(*img);
    CHECK(img->size() == int2{2, 1});
}

TEST_CASE("Resampling to the same size changes nothing")
{
    auto img = make_test_image();
    img->finalize();
    img->resample(k_size);
    CHECK(matches_original(img));
}

TEST_CASE("Reducing mixes the samples rather than dropping them")
{
    // A row alternating 0 and 1: point-sampling a halving returns all of one or all of the other, and a
    // filter returns something in between. What exactly depends on the filter's width, so this asks only
    // that both outputs are mixed and that they average out.
    auto  img = std::make_shared<Image>(int2{4, 1}, 1);
    auto &ch  = img->channels[0];
    for (int x = 0; x < 4; ++x) ch(x, 0) = float(x % 2);
    img->finalize();

    img->resample(int2{2, 1});

    REQUIRE(img->size() == int2{2, 1});
    const float a0 = img->channels[0](0, 0), a1 = img->channels[0](1, 0);
    CHECK(a0 > 0.f);
    CHECK(a0 < 1.f);
    CHECK(a1 > 0.f);
    CHECK(a1 < 1.f);
    CHECK(0.5f * (a0 + a1) == doctest::Approx(0.5f).epsilon(0.02));
}

TEST_CASE("Reducing keeps the light rather than losing it")
{
    // under a point-sampled reduction a lone bright sample lands between the output samples and vanishes;
    // filtering spreads it, so the total survives
    auto  img = std::make_shared<Image>(int2{16, 16}, 1);
    auto &ch  = img->channels[0];
    ch(7, 7)  = 64.f;
    img->finalize();

    img->resample(int2{4, 4});

    double total = 0.0;
    for (int i = 0; i < img->channels[0].num_elements(); ++i) total += double(img->channels[0](i));

    // each output sample now stands for sixteen input ones
    CHECK(total * 16.0 == doctest::Approx(64.0).epsilon(0.05));
}

TEST_CASE("Enlarging interpolates between the samples it has")
{
    auto  img = std::make_shared<Image>(int2{2, 1}, 1);
    auto &ch  = img->channels[0];
    ch(0, 0)  = 0.f;
    ch(1, 0)  = 1.f;
    img->finalize();

    img->resample(int2{8, 1});

    REQUIRE(img->size() == int2{8, 1});
    const auto &out = img->channels[0];

    // Rising from one original to the other, but not bounded by them: a filter good enough to enlarge
    // without blurring overshoots slightly either side of a step.
    for (int x = 1; x < 8; ++x)
    {
        CAPTURE(x);
        CHECK(out(x, 0) >= out(x - 1, 0));
    }
    CHECK(out(0, 0) < 0.2f);
    CHECK(out(7, 0) > 0.8f);
}

TEST_CASE("Resampling resizes every channel and both windows together")
{
    auto img = make_test_image();
    img->finalize();

    img->resample(int2{10, 6});

    CHECK(img->size() == int2{10, 6});
    CHECK(img->data_window.size() == int2{10, 6});
    CHECK(img->display_window.size() == int2{10, 6});
    for (const auto &c : img->channels) CHECK(c.size() == int2{10, 6});
}
