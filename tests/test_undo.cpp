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

// A distinct value per sample per channel, so that any transposition, reflection, or off-by-one in a
// restore shows up as a mismatch rather than coinciding with its neighbor. Deliberately not square, since
// a square image hides an axis swap.
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

// Everything the geometric operations are expected to preserve or change, as one description.
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
    // The whole basis for storing no pixels when undoing a flip or a quarter turn: if any of these lost or
    // resampled a sample, the round trip would not come back bit-identical.
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
    // Not square, so a turn that failed to swap the axes would be visible here.
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
    // The two directions have to be each other's inverse for LambdaUndo to be sound, and composing them
    // out of a transpose and a flip makes that easy to get backwards.
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
    // A display window narrower than the data window is what a raw CFA part looks like; a flip that moved
    // the samples but left the window behind would put it on the wrong edge.
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

    // Overwrite more than the entry covers, so that undoing has to leave the excess alone rather than
    // restoring the whole channel.
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

    // Untouched channels stay untouched.
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

    // The entry came out of undo() holding what it displaced, which is what makes redo the same swap.
    entry.redo(*img);
    CHECK(ch(2, 1) == -5.f);

    entry.undo(*img);
    CHECK(ch(2, 1) == expected(0, 2, 1));
}

TEST_CASE("A rectangle entry clipped to the image saves only the part that lands")
{
    auto img = make_test_image(1);

    // Hangs off the right and bottom edges.
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

    // Walking off either end does nothing rather than running an entry twice.
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

    // Editing forward past a save.
    history.add(noop("Second"));
    CHECK(history.is_modified());

    // ...and coming back to it.
    history.undo(*img);
    CHECK_FALSE(history.is_modified());

    // Undoing back past a save is just as much a difference from the file as editing past it.
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

    // Undoing and then editing drops the entry the save point sat on, so no amount of undoing gets back to
    // a state that matches the file.
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
    // finalize() appends as it walks the channels, so anything left over from an earlier build would be
    // duplicated -- the layer tree gaining a second leaf per node, and the channel counts no longer
    // agreeing with the channels themselves, which finalize() then rejects.
    auto img = make_test_image();
    img->finalize();

    const size_t layers = img->layers.size();
    const size_t groups = img->groups.size();
    REQUIRE(layers > 0);
    REQUIRE(groups > 0);

    // Whatever an edit does, asking for the tree again must describe the same channels.
    img->finalize();

    CHECK(img->layers.size() == layers);
    CHECK(img->groups.size() == groups);
}

TEST_CASE("A geometric edit leaves the layer and group structure alone")
{
    // Flips and quarter turns move samples without touching the channel set, so nothing about the layers
    // should change -- and nothing should need rebuilding for them.
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
