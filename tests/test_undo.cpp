//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "edit/undo.h"
#include "image.h"

#include "test_support.h"

using namespace hdrview_test;

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{

// A distinct value per sample per channel, so a transposition, reflection or off-by-one in a restore shows
// up as a mismatch instead of coinciding with a neighbor. Not square, since a square image hides an axis swap.
constexpr int2 k_size{5, 3};

float expected(int c, int x, int y) { return float(c * 1000 + y * 10 + x); }

ImagePtr make_test_image(int num_channels = 3) { return test_image(k_size, num_channels, expected); }

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

/// The channel names, and the layers and groups built from them, as one comparable value.
std::vector<std::string> structure_of(const ImagePtr &img)
{
    std::vector<std::string> out;
    for (const auto &c : img->channels) out.push_back("channel " + c.name);
    for (const auto &g : img->groups)
    {
        std::string group = "group " + g.name + ":";
        for (int i = 0; i < g.num_channels; ++i) group += " " + std::to_string(g.channels[i]);
        out.push_back(group);
    }
    out.push_back("layers " + std::to_string(img->layers.size()));
    return out;
}

} // namespace

TEST_CASE("Each geometric operation moves every sample where it says, and its opposite brings it back")
{
    // Undoing a flip or quarter turn stores no pixels, which is sound only while the round trip is
    // bit-identical: no sample lost, none resampled. A round trip alone passes a no-op, though, or a pair
    // that is the wrong way round consistently, so each is held to its closed form first.
    struct Case
    {
        const char *name;
        void (Image::*forward)();
        void (Image::*backward)();
        int2 (*to)(int2 p, int2 size); ///< where the sample at \p p of a \p size image lands
    };
    const Case cases[] = {
        {"flip horizontally", &Image::flip_horizontal, &Image::flip_horizontal,
         [](int2 p, int2 s) { return int2{s.x - 1 - p.x, p.y}; }},
        {"flip vertically", &Image::flip_vertical, &Image::flip_vertical,
         [](int2 p, int2 s) { return int2{p.x, s.y - 1 - p.y}; }},
        {"transpose", &Image::transpose, &Image::transpose, [](int2 p, int2) { return int2{p.y, p.x}; }},
        // clockwise, the top-left sample ends at the top right, and counter-clockwise at the bottom left
        {"rotate 90 cw", &Image::rotate_90_cw, &Image::rotate_90_ccw,
         [](int2 p, int2 s) { return int2{s.y - 1 - p.y, p.x}; }},
        {"rotate 90 ccw", &Image::rotate_90_ccw, &Image::rotate_90_cw,
         [](int2 p, int2 s) { return int2{p.y, s.x - 1 - p.x}; }},
    };

    // off center both ways and not square, so a window left in place, or moved by the wrong formula, shows:
    // the shape of a raw CFA part, whose display window is the sensor's active area
    const Box2i display{int2{0, 1}, int2{3, 3}};

    for (const auto &c : cases)
    {
        CAPTURE(c.name);
        auto img            = make_test_image();
        img->display_window = display;

        const auto structure = structure_of(img);

        (img.get()->*c.forward)();

        // the size is the box two opposite corners land in: the image's, transposed or not
        const int2 size = la::max(c.to(int2{0}, k_size), c.to(k_size - 1, k_size)) + 1;
        REQUIRE(img->size() == size);
        CHECK(img->data_window == Box2i{int2{0}, size});

        for (int ch = 0; ch < 3; ++ch)
            for (int y = 0; y < k_size.y; ++y)
                for (int x = 0; x < k_size.x; ++x)
                {
                    CAPTURE(ch);
                    CAPTURE(x);
                    CAPTURE(y);
                    const int2 q = c.to(int2{x, y}, k_size);
                    CHECK(img->channels[size_t(ch)](q.x, q.y) == expected(ch, x, y));
                }

        // the display window goes where its samples did
        const int2 lo = c.to(display.min, k_size), hi = c.to(display.max - 1, k_size);
        CHECK(img->display_window == Box2i{la::min(lo, hi), la::max(lo, hi) + 1});

        // moving samples touches neither the channel set nor the layers built from it
        CHECK(structure_of(img) == structure);

        (img.get()->*c.backward)();
        CHECK(matches_original(img));
        CHECK(img->data_window == Box2i{int2{0}, k_size});
        CHECK(img->display_window == display);
        CHECK(structure_of(img) == structure);
    }
}

TEST_CASE("A rectangle entry swaps exactly the samples it covers, clipped to the image, in both directions")
{
    // channels 0 and 2 of three, so restoring the channel in between, or the wrong one, shows
    const std::vector<int> saved{0, 2};
    const struct
    {
        const char *what;
        Box2i       bounds;
    } cases[] = {
        {"inside", Box2i{{1, 1}, {3, 2}}},
        {"hanging off the bottom right", Box2i{{3, 2}, {99, 99}}},
        {"hanging off the top left", Box2i{{-2, -1}, {2, 2}}},
        {"the whole image", Box2i{{0, 0}, k_size}},
    };

    for (const auto &c : cases)
    {
        CAPTURE(c.what);
        auto            img = make_test_image();
        ChannelRectUndo entry{*img, saved, c.bounds, "Test"};

        // overwrite more than the entry covers, every sample of every channel, so undoing has to leave the
        // excess alone
        for (auto &ch : img->channels)
            for (int i = 0; i < ch.num_elements(); ++i) ch(i) = -1.f;

        auto check = [&](bool restored)
        {
            for (int ch = 0; ch < 3; ++ch)
                for (int y = 0; y < k_size.y; ++y)
                    for (int x = 0; x < k_size.x; ++x)
                    {
                        CAPTURE(ch);
                        CAPTURE(x);
                        CAPTURE(y);
                        const bool covered =
                            c.bounds.contains(int2{x, y}) && std::find(saved.begin(), saved.end(), ch) != saved.end();
                        CHECK(img->channels[size_t(ch)](x, y) == (restored && covered ? expected(ch, x, y) : -1.f));
                    }
        };

        entry.undo(*img);
        check(true);

        // the entry came out of undo() holding what it displaced, so redo is the same swap
        entry.redo(*img);
        check(false);

        entry.undo(*img);
        check(true);
    }
}

TEST_CASE("The history cursor tracks what can be undone and redone")
{
    auto           img = make_test_image(1);
    CommandHistory history;

    CHECK_FALSE(history.has_undo());
    CHECK_FALSE(history.has_redo());

    int  applied        = 0;
    auto counting_entry = [&applied](std::string name)
    {
        auto count = [&applied](Image &) { ++applied; };
        return std::make_unique<LambdaUndo>(std::move(name), count, count);
    };

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

TEST_CASE("The modified flag follows the distance from the last save in both directions")
{
    auto           img = make_test_image(1);
    CommandHistory history;

    auto noop = [](std::string name)
    { return std::make_unique<LambdaUndo>(std::move(name), [](Image &) {}, [](Image &) {}); };

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

TEST_CASE("A new edit discards what had been undone, save point included")
{
    auto           img = make_test_image(1);
    CommandHistory history;

    auto noop = [](std::string name)
    { return std::make_unique<LambdaUndo>(std::move(name), [](Image &) {}, [](Image &) {}); };

    history.add(noop("First"));
    history.add(noop("Second"));
    history.mark_saved();
    REQUIRE_FALSE(history.is_modified());

    // undoing and then editing drops the entry the save point sat on
    history.undo(*img);
    REQUIRE(history.has_redo());
    history.add(noop("Third"));
    CHECK_FALSE(history.has_redo());
    CHECK(history.size() == 2);
    CHECK(history.undo_name() == "Third");
    CHECK(history.is_modified());

    // so no amount of undoing gets back to a state that matches the file
    history.undo(*img);
    CHECK(history.is_modified());
    history.undo(*img);
    CHECK(history.is_modified());
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

TEST_CASE("Resizing the canvas puts the old samples where the anchor says, cutting or zero-filling the rest")
{
    // Even changes, so a centered anchor has an exact middle. Grown, shrunk, and grown one way while shrunk
    // the other, since an anchor that confuses its row with its column only shows when the two differ.
    const int2 changes[] = {{4, 2}, {-2, -2}, {2, -2}};

    for (const int2 change : changes)
        for (int anchor = 0; anchor < Image::Anchor_COUNT; ++anchor)
        {
            CAPTURE(change.x);
            CAPTURE(change.y);
            CAPTURE(anchor);

            // where the old top-left lands: nothing taken off the anchored edge, half off each side of a
            // centered one, all of it off the far side
            int2 offset{0};
            switch (Image::CanvasAnchor(anchor))
            {
            case Image::Anchor_TopLeft: offset = {0, 0}; break;
            case Image::Anchor_TopCenter: offset = {change.x / 2, 0}; break;
            case Image::Anchor_TopRight: offset = {change.x, 0}; break;
            case Image::Anchor_MiddleLeft: offset = {0, change.y / 2}; break;
            case Image::Anchor_MiddleCenter: offset = {change.x / 2, change.y / 2}; break;
            case Image::Anchor_MiddleRight: offset = {change.x, change.y / 2}; break;
            case Image::Anchor_BottomLeft: offset = {0, change.y}; break;
            case Image::Anchor_BottomCenter: offset = {change.x / 2, change.y}; break;
            case Image::Anchor_BottomRight: offset = {change.x, change.y}; break;
            default: FAIL("an anchor this test does not know");
            }

            auto img = make_test_image(2);
            img->resize_canvas(k_size + change, Image::CanvasAnchor(anchor));

            const int2 size = k_size + change;
            REQUIRE(img->size() == size);
            CHECK(img->data_window == Box2i{int2{0}, size});
            CHECK(img->display_window == img->data_window);

            for (int c = 0; c < 2; ++c)
            {
                CHECK(img->channels[size_t(c)].size() == size);
                for (int y = 0; y < size.y; ++y)
                    for (int x = 0; x < size.x; ++x)
                    {
                        CAPTURE(c);
                        CAPTURE(x);
                        CAPTURE(y);
                        const int2 from = int2{x, y} - offset;
                        CHECK(img->channels[size_t(c)](x, y) ==
                              (Box2i{int2{0}, k_size}.contains(from) ? expected(c, from.x, from.y) : 0.f));
                    }
            }
        }
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

TEST_CASE("Reducing keeps each channel's light, and resizes every channel and both windows together")
{
    // under a point-sampled reduction a lone bright sample lands between the output samples and vanishes;
    // filtering spreads it, so the total survives. A different sample in each channel, so a channel resampled
    // from another's samples, or left at its old size, shows.
    auto img               = std::make_shared<Image>(int2{16, 12}, 2);
    img->channels[0](7, 7) = 64.f;
    img->channels[1](2, 9) = 32.f;
    img->finalize();
    // a display window smaller than the data, which a resample replaces with the new whole image
    img->display_window = Box2i{int2{1, 1}, int2{9, 5}};

    img->resample(int2{4, 3});

    REQUIRE(img->size() == int2{4, 3});
    CHECK(img->data_window == Box2i{int2{0}, int2{4, 3}});
    CHECK(img->display_window == img->data_window);

    const float totals[] = {64.f, 32.f};
    for (int c = 0; c < 2; ++c)
    {
        CAPTURE(c);
        REQUIRE(img->channels[size_t(c)].size() == int2{4, 3});

        double total = 0.0;
        for (int i = 0; i < img->channels[size_t(c)].num_elements(); ++i) total += double(img->channels[size_t(c)](i));

        // each output sample now stands for sixteen input ones
        CHECK(total * 16.0 == doctest::Approx(double(totals[c])).epsilon(0.05));
    }
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

TEST_CASE("An edit of the color metadata undoes as one entry, however many frames it spans")
{
    // The Colorspace panel's edits reinterpret the pixels without touching them, a frame at a time while a widget is
    // dragged; the edit is one entry from where it began to where it ends, and taking it back restores every field
    auto img            = make_test_image();
    img->chromaticities = gamut_chromaticities(ColorGamut_sRGB_BT709);
    img->compute_color_transform();
    img->metadata["color profile"] = "as loaded";
    const auto original            = ColorMetadata::of(*img);
    const bool pixels_before       = matches_original(img);

    ColorMetadataEdit edit;
    // what one frame of the panel does to the image, while a widget is active or once it lets go
    const auto frame = [&](const std::function<void()> &change, bool editing)
    {
        edit.begin(*img);
        if (change)
            change();
        edit.end(*img, editing);
    };
    const auto move_white = [&](float2 white)
    {
        img->chromaticities->white = white;
        img->white_point           = WhitePoint_Custom;
        img->compute_color_transform();
        img->metadata["color profile"] = "edited";
    };

    frame({}, false);
    CHECK(img->history.size() == 0); //< nothing changed

    // a drag across three frames, released on the last
    frame([&] { move_white(float2{0.32f, 0.33f}); }, true);
    frame([&] { move_white(float2{0.33f, 0.34f}); }, true);
    CHECK(img->history.size() == 0); //< still being dragged
    frame([&] { move_white(float2{0.34f, 0.35f}); }, false);
    REQUIRE(img->history.size() == 1);
    const auto edited = ColorMetadata::of(*img);
    CHECK(edited != original);

    CHECK(img->history.undo(*img));
    CHECK(ColorMetadata::of(*img) == original);
    CHECK(img->history.redo(*img));
    CHECK(ColorMetadata::of(*img) == edited);
    CHECK(matches_original(img) == pixels_before); //< and no pixel moved

    // a drag that ends where it began is no edit
    frame([&] { move_white(float2{0.30f, 0.30f}); }, true);
    frame([&] { edited.apply_to(*img); }, false);
    CHECK(img->history.size() == 1);
}
