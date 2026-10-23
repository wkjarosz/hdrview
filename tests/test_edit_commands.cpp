//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file test_edit_commands.cpp
    \author Wojciech Jarosz

    The edit commands and the functions they change images through, run with no window, GL context or
    frame loop; everything but the dialogs.
*/

#include <doctest/doctest.h>

#include "colorspace.h"
#include "edit/commands.h"
#include "edit/edit_ops.h"
#include "edit/subject.h"
#include "image.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <vector>

using namespace hdrview_test;

namespace
{

/// What the application hands a command, over a bare Image: no textures, worker thread or window.
/**
    The subject and target rules and the edit functions are the application's own; the async pair runs
    inline.
*/
struct TestEditContext : EditContext
{
    ImagePtr              clipboard_image; ///< what cut and copy write to
    std::vector<ImagePtr> added;           ///< the images a command put beside the one it was given

    explicit TestEditContext(ImagePtr img)
    {
        image     = std::move(img);
        clipboard = &clipboard_image;
        retarget();

        add_image = [this](ImagePtr made, std::string partname)
        {
            if (!made)
                return;
            made->partname = std::move(partname);
            added.push_back(std::move(made));
        };
        set_selection = [this](const Box2i &box) { roi = box; };

        modify_channels_async = [this](const std::string &name, const ChannelFilter &filter)
        {
            // Named apart: C++17 cannot capture a structured binding in a lambda.
            auto  resolved = resolve_subject(image, subject, roi);
            auto &channels = resolved.first;
            auto &bounds   = resolved.second;
            if (channels.empty() || !bounds.has_volume())
                return;

            const int2  offset = bounds.min - image->data_window.min;
            const Box2i local{offset, offset + bounds.size()};

            std::vector<Array2Df> results;
            for (size_t i = 0; i < channels.size(); ++i)
                results.push_back(filter(image->channels[size_t(channels[i])], local, int(i), AtomicProgress{}));

            modify_image(
                *this, name,
                [&](Image &img)
                {
                    for (size_t i = 0; i < channels.size(); ++i)
                        img.channels[size_t(channels[i])].upload_tile(local, results[i].data());
                },
                [&](const Image &img, const std::string &n) -> UndoPtr
                { return std::make_unique<ChannelRectUndo>(img, channels, bounds, n); });
        };

        resample_image_async = [this](const std::string &name, int2 size, const ChannelResampler &op, EditExtent extent)
        {
            if (!image || size.x <= 0 || size.y <= 0)
                return;

            std::vector<Array2Df> results;
            for (const auto &channel : image->channels) results.push_back(op(channel, AtomicProgress{}));

            modify_image(
                *this, name,
                [&](Image &img)
                {
                    for (size_t i = 0; i < img.channels.size(); ++i)
                    {
                        img.channels[i].resize(size);
                        std::copy(results[i].data(), results[i].data() + results[i].num_elements(),
                                  img.channels[i].data());
                    }
                    img.data_window    = Box2i{img.data_window.min, img.data_window.min + size};
                    img.display_window = img.data_window;
                },
                structure_undo, extent);
        };
    }

    /// Re-read which groups an edit acts on, as the application does each time it builds a context.
    /**
        \p pointed_at names a group the way right-clicking one in the Images panel does.
    */
    void retarget(int pointed_at = -1) { target_groups = ::target_groups(image, pointed_at); }
};

constexpr int2 k_size{7, 5};

/// A selection well inside a k_size image, so there is untouched ground on every side of it.
const Box2i k_roi{int2{2, 1}, int2{5, 4}};

/// An image whose every sample is distinct, so any misplacement shows as a mismatch.
ImagePtr make_image(int num_channels = 4)
{
    auto img = std::make_shared<Image>(k_size, num_channels);

    static const char *names[] = {"R", "G", "B", "A"};
    for (int c = 0; c < num_channels && c < 4; ++c) img->channels[size_t(c)].name = names[c];

    for (int c = 0; c < num_channels; ++c)
        for (int y = 0; y < k_size.y; ++y)
            for (int x = 0; x < k_size.x; ++x)
                // inside [0,1] and away from the ends, so the edits that clamp still change something
                img->channels[size_t(c)](x, y) = 0.15f + 0.1f * float(c) + 0.01f * float(y * k_size.x + x);

    img->rebuild_layers();
    return img;
}

/// An image with three channel groups: two color groups and a depth channel.
/**
    So "the group on screen", "the selected groups" and "every channel" are three different sets, and the
    color-only filter has one to drop.
*/
ImagePtr make_layered_image()
{
    auto img = std::make_shared<Image>(
        k_size, std::vector<std::string>{"R", "G", "B", "A", "Z", "normal.R", "normal.G", "normal.B"});

    for (size_t c = 0; c < img->channels.size(); ++c)
        for (int y = 0; y < k_size.y; ++y)
            for (int x = 0; x < k_size.x; ++x)
                img->channels[c](x, y) = 0.15f + 0.05f * float(c) + 0.01f * float(y * k_size.x + x);

    img->rebuild_layers();
    return img;
}

/// The index of the group holding the channel named \p name, or -1.
int group_of_channel(const ImagePtr &img, const std::string &name)
{
    for (size_t g = 0; g < img->groups.size(); ++g)
        for (int i = 0; i < img->groups[g].num_channels; ++i)
            if (img->channels[size_t(img->groups[g].channels[i])].name == name)
                return int(g);
    return -1;
}

/// The index of the channel named \p name, or -1.
int channel_index(const ImagePtr &img, const std::string &name)
{
    for (size_t c = 0; c < img->channels.size(); ++c)
        if (img->channels[c].name == name)
            return int(c);
    return -1;
}

/// Every sample of \p a, as one comparable value.
std::vector<float> values(const Array2Df &a) { return std::vector<float>(a.data(), a.data() + a.num_elements()); }

/// The names of group \p g's channels, or none when \p g is not a group of \p img.
std::vector<std::string> names_of(const ImagePtr &img, int g)
{
    std::vector<std::string> out;
    if (img->is_valid_group(g))
        for (int i = 0; i < img->groups[size_t(g)].num_channels; ++i)
            out.push_back(img->channels[size_t(img->groups[size_t(g)].channels[i])].name);
    return out;
}

/// Every selected group, each by the names of its channels; the indices are renumbered by a rebuild.
std::vector<std::vector<std::string>> selected_names(const ImagePtr &img)
{
    std::vector<std::vector<std::string>> out;
    for (int g : img->selected_groups()) out.push_back(names_of(img, g));
    return out;
}

/// Everything an edit can change about an image, with the samples compared bit for bit, so a NaN equals itself.
struct Snapshot
{
    std::vector<uint32_t>    bits;
    Box2i                    data_window, display_window;
    std::vector<std::string> structure; ///< the channels and their flags, and the groups, by name
    std::vector<std::string> selection; ///< the selected channels, and the group shown, by name
    ColorMetadata            color;

    /// This, but for which groups are selected and shown.
    Snapshot content() const
    {
        Snapshot s  = *this;
        s.selection = {};
        return s;
    }

    bool operator==(const Snapshot &o) const
    {
        return bits == o.bits && data_window == o.data_window && display_window == o.display_window &&
               structure == o.structure && selection == o.selection && color == o.color;
    }
    bool operator!=(const Snapshot &o) const { return !(*this == o); }
};

Snapshot snapshot(const ImagePtr &img)
{
    Snapshot s;
    for (const auto &ch : img->channels)
    {
        for (int i = 0; i < ch.num_elements(); ++i)
        {
            uint32_t b;
            std::memcpy(&b, &ch(i), sizeof b);
            s.bits.push_back(b);
        }
        s.structure.push_back("channel " + ch.name + (ch.ungrouped ? " ungrouped" : ""));
        if (ch.selected)
            s.selection.push_back("selected " + ch.name);
    }
    for (size_t g = 0; g < img->groups.size(); ++g)
    {
        std::string group = "group";
        for (const auto &n : names_of(img, int(g))) group += " " + n;
        s.structure.push_back(group);
    }
    std::string shown = "shown";
    for (const auto &n : names_of(img, img->selected_group)) shown += " " + n;
    s.selection.push_back(shown);

    s.data_window    = img->data_window;
    s.display_window = img->display_window;
    s.color          = ColorMetadata::of(*img);
    return s;
}

} // namespace

namespace doctest
{

/// A snapshot as a failed check prints it: its structure and selection, which say what differs.
template <>
struct StringMaker<Snapshot>
{
    static String convert(const Snapshot &s)
    {
        std::string out = fmt::format("{} samples; ", s.bits.size());
        for (const auto &line : s.structure) out += line + "; ";
        for (const auto &line : s.selection) out += line + "; ";
        return out.c_str();
    }
};

} // namespace doctest

namespace
{

std::string first_name(const EditCommandPtr &cmd) { return cmd->info().names.front(); }

/// The registered command whose first name is \p name.
EditCommandPtr find_command(const std::string &name)
{
    for (auto &cmd : all_edit_commands())
        if (first_name(cmd) == name)
            return std::move(cmd);
    return nullptr;
}

} // namespace

TEST_CASE("Every edit command is addressable and describes itself")
{
    const auto commands = all_edit_commands();
    REQUIRE(commands.size() > 10); // the sweep is worthless if the registry did not build

    std::vector<std::string> names;
    for (const auto &cmd : commands)
    {
        const auto info = cmd->info();

        CAPTURE(info.names.front());

        // the first name is the key the action registry, the menu and the tests all address it by
        REQUIRE_FALSE(info.names.empty());
        CHECK_FALSE(info.names.front().empty());
        CHECK_FALSE(info.icon.empty());
        CHECK(info.width_em > 0.f);

        names.push_back(info.names.front());
    }

    // addressed by name, so two commands sharing one would make the second unreachable
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
}

TEST_CASE("Every edit that applies changes the image as one undo entry, and one it refuses changes nothing")
{
    // The commands that are the identity at the settings their dialog opens with, which still record an entry,
    // so all a sweep at those settings can check is that it undoes and redoes exactly.
    const std::set<std::string> identity = {
        "Exposure/gamma...",      // no exposure, no offset, and a gamma of one
        "Brightness/contrast...", // no brightness and no contrast
        "Channel mixer...",       // each output row starts as its own channel
        "Hue/saturation...",      // no turn, and no change of saturation or lightness
        "Shift...",               // no offset
        "Image size...",          // opens on the image's own size
        "Canvas size...",         // likewise
    };
    // The commands writing somewhere other than the image they are handed, which record nothing on it.
    const std::set<std::string> elsewhere = {
        "Copy",                // the clipboard
        "Generate mipmaps...", // new images beside it
    };

    // A clipboard unlike the image, so pasting it changes what it lands on, seamlessly or not.
    auto clipboard = make_image();
    for (int c = 0; c < 4; ++c)
        for (int y = 0; y < k_size.y; ++y)
            for (int x = 0; x < k_size.x; ++x)
                clipboard->channels[size_t(c)](x, y) = 0.5f + 0.3f * std::sin(1.3f * float(x) + 0.7f * float(y + c));

    // Across the whole image; within a selection, where cropping is offered; and on an image a renderer owns,
    // which refuses every edit, since its next tile would overwrite it and undoing would restore samples
    // already replaced.
    enum Pass
    {
        Pass_Whole,
        Pass_Selection,
        Pass_Live
    };
    for (int pass : {Pass_Whole, Pass_Selection, Pass_Live})
        for (auto &cmd : all_edit_commands())
        {
            const std::string name = first_name(cmd);
            CAPTURE(name);
            CAPTURE(pass);

            // Inside the selection, a NaN and an infinity for zapping to replace, and samples either side of
            // [0,1] for clamping to bring in. The gremlins are in the alpha channel, which a seamless paste
            // leaves alone, since one in a channel it solves for keeps the solve from converging.
            auto img               = make_image();
            img->channels[3](3, 2) = std::numeric_limits<float>::quiet_NaN();
            img->channels[3](2, 1) = std::numeric_limits<float>::infinity();
            img->channels[2](4, 3) = 1.75f;
            img->channels[0](3, 1) = -0.25f;
            img->is_live           = pass == Pass_Live;

            TestEditContext ctx{img};
            ctx.clipboard_image = clipboard;
            if (pass == Pass_Selection)
                ctx.roi = k_roi;

            const Snapshot before = snapshot(img);
            const int      steps  = img->history.size();

            // as the dialog does; several commands read their starting settings off the image here
            cmd->on_open(ctx);
            const bool enabled = cmd->enabled(ctx);
            // applied whether or not it is enabled, since the command palette and the chord reach it either way
            cmd->apply(ctx);
            cmd->on_close(ctx);

            const int      added = img->history.size() - steps;
            const Snapshot after = snapshot(img);

            if (pass == Pass_Live || !enabled || elsewhere.count(name))
            {
                CHECK(added == 0);
                CHECK(after == before);

                // and it did write where it writes
                if (enabled && pass != Pass_Live && name == "Copy")
                    CHECK(ctx.clipboard_image != clipboard);
                if (enabled && pass != Pass_Live && name == "Generate mipmaps...")
                    CHECK_FALSE(ctx.added.empty());
                continue;
            }

            // one entry: two would take two undos to reverse, and none could not be reversed at all
            REQUIRE(added == 1);

            // named for the history panel; the name need not be the command's own, since one command can record
            // different edits (a blur names the kind of blur it did)
            CHECK_FALSE(img->history.undo_name().empty());

            // it changed something, unless it is one of the identities, which are held to changing nothing so the
            // list stays true
            if (!identity.count(name))
                CHECK(after != before);
            else
                CHECK(after == before);

            // the entry restores everything, structure and color metadata included, in both directions
            REQUIRE(img->history.undo(*img));
            CHECK(snapshot(img) == before);
            REQUIRE(img->history.redo(*img));
            CHECK(snapshot(img) == after);
        }

    // not only the commands: the edit functions themselves decline, which is what every command goes through
    auto img     = make_image();
    img->is_live = true;
    TestEditContext ctx{img};
    ctx.subject.selection_only = false;

    const Snapshot                     before = snapshot(img);
    const std::function<void(Image &)> flip   = [](Image &i) { i.flip_horizontal(); };
    CHECK_FALSE(modify_image(ctx, "Flip image horizontally", flip, reversible(flip, flip)));
    CHECK_FALSE(modify_pixels(ctx, "Raise", [](float v, int2, int) { return v + 1.f; }));
    CHECK_FALSE(modify_colors(ctx, "Raise", [](const float4 &c, int2, int) { return c + 1.f; }));
    CHECK(snapshot(img) == before);
    CHECK_FALSE(img->history.has_undo());
}

TEST_CASE("An edit covers the subject it was given and nothing else")
{
    // An edit ignoring the subject writes outside the rectangle, and one misreading it writes to a channel
    // the scope never named; both show up as a sample changing where none should have. Run on a three-group
    // image, or every scope names the same channels and only the rectangle is under test.
    for (int scope = 0; scope < EditSubject::Scope_COUNT; ++scope)
        for (auto &cmd : all_edit_commands())
        {
            const std::string name = first_name(cmd);

            CAPTURE(name);
            CAPTURE(scope);

            auto img = make_layered_image();

            // the color group on screen and the two color groups selected, so "selected" is neither the
            // current group nor every channel
            img->selected_group = group_of_channel(img, "R");
            img->select_group(img->selected_group);
            img->select_group(group_of_channel(img, "normal.R"));

            TestEditContext ctx{img};
            ctx.clipboard_image = make_image();

            ctx.roi                    = k_roi;
            ctx.subject.selection_only = true;
            ctx.subject.scope          = EditSubject::Scope(scope);

            if (!cmd->enabled(ctx))
                continue;

            // worked out from the scope here, not asked of subject_channels(), so the resolver is checked
            // against an independent reading of it
            std::vector<int> covered;
            if (scope == EditSubject::Scope_AllChannels)
            {
                covered.resize(img->channels.size());
                std::iota(covered.begin(), covered.end(), 0);
            }
            else
            {
                const std::vector<int> groups = scope == EditSubject::Scope_SelectedGroups
                                                    ? img->selected_groups()
                                                    : std::vector<int>{img->selected_group};
                for (int g : groups)
                    for (int i = 0; i < img->groups[size_t(g)].num_channels; ++i)
                        covered.push_back(img->groups[size_t(g)].channels[i]);
            }

            const std::vector<float> before      = samples(img);
            const int                steps       = img->history.size();
            const ConstImagePtr      clip_before = ctx.clipboard_image;

            cmd->apply(ctx);

            // there is one clipboard, so a command that fills it cannot also run once per selected image
            if (ctx.clipboard_image != clip_before)
                CHECK_FALSE(cmd->info().fans_out);

            // A command offering the "Apply to" controls owes this: what it wrote lies inside the channels
            // and the rectangle they name. The others move every sample by definition, a flip does, or have
            // no rectangle to stay in, and say so by declining the controls.
            if (img->history.size() == steps || img->size() != k_size || !cmd->info().draws_subject_selector)
                continue;

            for (size_t c = 0; c < img->channels.size(); ++c)
            {
                const bool named = std::find(covered.begin(), covered.end(), int(c)) != covered.end();
                for (int y = 0; y < k_size.y; ++y)
                    for (int x = 0; x < k_size.x; ++x)
                    {
                        if (named && in_box(k_roi, int2{x, y}))
                            continue;

                        CAPTURE(c);
                        CAPTURE(x);
                        CAPTURE(y);
                        CHECK(img->channels[c](x, y) ==
                              before[c * size_t(k_size.x * k_size.y) + size_t(y * k_size.x + x)]);
                    }
            }
        }
}

TEST_CASE("An edit reaches every channel its scope names, each as the slot it holds there")
{
    // The other half of the sweep above, which catches only an edit writing where it should not: a scope
    // resolving to too few channels leaves part of the subject untouched and passes there. The ops are
    // driven directly. The pixel op adds one more than the slot it is handed, so each channel rises by an
    // amount that says which slot it was, and a selection skipping the depth channel puts the slots and the
    // channel indices out of step. The color op turns R, G and B around, which only an op handed a group's
    // channels together can do. An image of lone channels has no color group, so the color op covers nothing
    // and has to say so.
    for (bool lone : {false, true})
        for (int scope = 0; scope < EditSubject::Scope_COUNT; ++scope)
        {
            CAPTURE(lone);
            CAPTURE(scope);

            auto img = make_layered_image(); // R,G,B,A | Z | normal.R,G,B
            if (lone)
            {
                for (auto &c : img->channels) c.ungrouped = true;
                img->rebuild_layers();
            }

            // the group holding R on screen, and that plus the one holding normal.R selected
            img->selected_group = group_of_channel(img, "R");
            img->select_group(img->selected_group);
            img->select_group(group_of_channel(img, "normal.R"));

            TestEditContext ctx{img};
            ctx.subject.scope          = EditSubject::Scope(scope);
            ctx.subject.selection_only = false;

            // the groups the scope names, read off the image rather than asked of the resolver
            std::vector<int> groups;
            if (scope == EditSubject::Scope_AllChannels)
                for (size_t g = 0; g < img->groups.size(); ++g) groups.push_back(int(g));
            else if (scope == EditSubject::Scope_SelectedGroups)
                groups = img->selected_groups();
            else
                groups.push_back(img->selected_group);

            // the channels in slot order, which is channel order; "every channel" names them all, whether or
            // not they belong to a group
            std::vector<int>              covered;
            std::vector<std::vector<int>> colors;
            for (int g : groups)
            {
                const auto      &group = img->groups[size_t(g)];
                std::vector<int> channels;
                for (int i = 0; i < group.num_channels; ++i) channels.push_back(group.channels[i]);
                covered.insert(covered.end(), channels.begin(), channels.end());
                if (group.type == ChannelGroup::RGB_Channels || group.type == ChannelGroup::RGBA_Channels)
                    colors.push_back(channels);
            }
            if (scope == EditSubject::Scope_AllChannels)
            {
                covered.resize(img->channels.size());
                std::iota(covered.begin(), covered.end(), 0);
            }
            std::sort(covered.begin(), covered.end());

            const size_t             per_channel = size_t(k_size.x * k_size.y);
            const std::vector<float> before      = samples(img);
            auto                     was         = [&](int c, size_t i) { return before[size_t(c) * per_channel + i]; };

            REQUIRE(modify_pixels(ctx, "Raise", [](float v, int2, int slot) { return v + 1.f + float(slot); }));
            for (size_t c = 0; c < img->channels.size(); ++c)
            {
                CAPTURE(c);
                const auto slot = std::find(covered.begin(), covered.end(), int(c));
                for (size_t i = 0; i < per_channel; ++i)
                    CHECK(img->channels[c](int(i)) == (slot == covered.end()
                                                           ? was(int(c), i)
                                                           : was(int(c), i) + 1.f + float(slot - covered.begin())));
            }
            REQUIRE(img->history.undo(*img));
            CHECK(samples(img) == before);

            // R takes G's samples, G takes B's and B takes R's; alpha and every other channel keep their own
            const bool edited =
                modify_colors(ctx, "Turn", [](const float4 &c, int2, int) { return float4{c.y, c.z, c.x, c.w}; });
            CHECK(edited == !colors.empty());
            CHECK(img->history.has_undo() == edited); // the pixel op was undone, so only this can be undone now

            std::vector<int> from(img->channels.size());
            std::iota(from.begin(), from.end(), 0);
            for (const auto &group : colors)
                for (int k = 0; k < 3; ++k) from[size_t(group[size_t(k)])] = group[size_t((k + 1) % 3)];

            for (size_t c = 0; c < img->channels.size(); ++c)
            {
                CAPTURE(c);
                for (size_t i = 0; i < per_channel; ++i) CHECK(img->channels[c](int(i)) == was(from[c], i));
            }

            if (edited)
            {
                REQUIRE(img->history.undo(*img));
                CHECK(samples(img) == before);
            }
        }
}

TEST_CASE("A bump map becomes the normal map its slopes ask for")
{
    // Two color groups sloping different ways, so a normal derived from the wrong group's heights shows up
    // as one group wearing the other's normal. The heights are planes, so the interior normals are known in
    // closed form; the last row and column are left out, where the border mode decides the slope.
    constexpr float dh_dx = 0.02f, dh_dy = 0.05f;

    auto img = std::make_shared<Image>(k_size, std::vector<std::string>{"diffuse.R", "diffuse.G", "diffuse.B",
                                                                        "specular.R", "specular.G", "specular.B"});
    for (int y = 0; y < k_size.y; ++y)
        for (int x = 0; x < k_size.x; ++x)
            for (int c = 0; c < 3; ++c)
            {
                img->channels[size_t(c)](x, y)     = dh_dx * float(x);
                img->channels[size_t(c + 3)](x, y) = dh_dy * float(y);
            }
    img->rebuild_layers();
    REQUIRE(img->groups.size() == 2);

    TestEditContext ctx{img};
    ctx.subject.scope          = EditSubject::Scope_AllChannels;
    ctx.subject.selection_only = false;

    auto cmd = find_command("Bump to normal map...");
    REQUIRE(cmd);
    cmd->apply(ctx);

    // The command writes the normal as (dh/dx, dh/dy, 1), its slopes taken per unit of the whole image and
    // scaled by the default of one, normalized and stored in [0,1] where flat is (0.5, 0.5, 1).
    const float2 size{float(k_size.x), float(k_size.y)};
    const float3 diffuse  = la::normalize(float3{dh_dx * size.x, 0.f, 1.f}) * 0.5f + 0.5f;
    const float3 specular = la::normalize(float3{0.f, dh_dy * size.y, 1.f}) * 0.5f + 0.5f;

    const int diffuse_group  = group_of_channel(img, "diffuse.R");
    const int specular_group = group_of_channel(img, "specular.R");

    for (int y = 0; y < k_size.y - 1; ++y)
        for (int x = 0; x < k_size.x - 1; ++x)
            for (int k = 0; k < 3; ++k)
            {
                CAPTURE(x);
                CAPTURE(y);
                CAPTURE(k);
                CHECK(img->channels[size_t(img->groups[size_t(diffuse_group)].channels[k])](x, y) ==
                      doctest::Approx(diffuse[k]).epsilon(1e-5));
                CHECK(img->channels[size_t(img->groups[size_t(specular_group)].channels[k])](x, y) ==
                      doctest::Approx(specular[k]).epsilon(1e-5));
            }

    // the two are different maps, which is what says each group read its own heights
    CHECK(diffuse.x != doctest::Approx(specular.x));
    CHECK(diffuse.y != doctest::Approx(specular.y));
}

TEST_CASE("Hue/saturation works on straight color in every group it covers where alpha means transparency")
{
    // the group on screen has no alpha while another covered group does, so a decision made from the group on
    // screen alone reads that other group's premultiplied samples as straight
    auto img            = make_layered_image();
    img->selected_group = group_of_channel(img, "normal.R");

    TestEditContext ctx{img};
    ctx.subject.scope = EditSubject::Scope_AllChannels;

    auto cmd = find_command("Hue/saturation...");
    REQUIRE(cmd);
    for (TransparencyType_ t : {TransparencyType_None, TransparencyType_PremultipliedLinear})
    {
        CAPTURE(t);
        img->set_transparency(t, std::nullopt);
        const auto edit = cmd->point_edit(ctx);
        REQUIRE(edit);
        CHECK((edit->p1.x != 0.f) == (t != TransparencyType_None));
    }
}

TEST_CASE("Converting the color space moves the samples and the tag in one step")
{
    auto img            = make_image();
    img->chromaticities = gamut_chromaticities(ColorGamut_sRGB_BT709);
    img->compute_color_transform();

    const auto  before      = samples(img);
    const auto  before_chr  = img->chromaticities;
    const float before_wide = img->M_to_sRGB[0][0];

    Chromaticities to = gamut_chromaticities(ColorGamut_BT2020_2100);
    to.white          = white_point(WhitePoint_D65);

    float3x3 M;
    REQUIRE(color_conversion_matrix(M, *before_chr, to, AdaptationMethod_Bradford));

    TestEditContext ctx{img};
    ctx.subject.selection_only = false;

    REQUIRE(modify_colors(
        ctx, "Convert color space", [M](const float4 &c, int2, int) { return float4{la::mul(M, c.xyz()), c.w}; },
        [to](Image &image)
        {
            image.chromaticities = to;
            image.compute_color_transform();
        }));

    // both halves landed: the samples moved, and so did what is derived from the tag
    CHECK(samples(img) != before);
    CHECK(img->color_space == ColorGamut_BT2020_2100);
    CHECK(std::fabs(img->M_to_sRGB[0][0] - before_wide) > 1e-4f);

    // one step takes back both: a tag left behind would describe the image as something it is not
    const auto converted = samples(img);
    REQUIRE(img->history.undo(*img));
    CHECK(samples(img) == before);
    CHECK(img->color_space == ColorGamut_sRGB_BT709);
    CHECK(std::fabs(img->M_to_sRGB[0][0] - before_wide) < 1e-4f);

    // and redo puts both back, which a composite undoing in the wrong order would not
    REQUIRE(img->history.redo(*img));
    CHECK(samples(img) == converted);
    CHECK(img->color_space == ColorGamut_BT2020_2100);
}

TEST_CASE("Flattening makes the image opaque and leaves the composite where it is")
{
    // Samples are held premultiplied, so compositing over a background is an addition and not the
    // textbook's lerp. The command's background is its own dialog state, so what a test reaches is the
    // default opaque black: the colors stay put and only the coverage changes.
    auto img = make_image();
    for (int i = 0; i < img->channels[3].num_elements(); ++i) img->channels[3](i) = 0.5f;

    TestEditContext ctx{img};
    ctx.subject.selection_only = false;

    const auto before = samples(img);
    auto       cmd    = find_command("Flatten...");
    REQUIRE(cmd);
    REQUIRE(cmd->enabled(ctx));
    cmd->apply(ctx);

    const size_t per_channel = size_t(k_size.x * k_size.y);
    for (int i = 0; i < img->channels[3].num_elements(); ++i)
    {
        CAPTURE(i);
        CHECK(img->channels[3](i) == doctest::Approx(1.f));
        for (int c = 0; c < 3; ++c)
            CHECK(img->channels[size_t(c)](i) == doctest::Approx(before[size_t(c) * per_channel + size_t(i)]));
    }

    // an opaque result has nothing left to show through, which a lerp written against straight alpha would
    // get wrong
    const auto once = samples(img);
    cmd->apply(ctx);
    CHECK(samples(img) == once);

    REQUIRE(img->history.undo(*img));
    REQUIRE(img->history.undo(*img));
    CHECK(samples(img) == before);
}

TEST_CASE("A command with no image to work on does nothing rather than crashing")
{
    // the menu can be reached with no image loaded, and the palette hands a command whatever the context has
    for (auto &cmd : all_edit_commands())
    {
        CAPTURE(first_name(cmd));

        TestEditContext ctx{nullptr};
        CHECK_NOTHROW((void)cmd->enabled(ctx));
        CHECK_NOTHROW(cmd->apply(ctx));
        CHECK_NOTHROW(cmd->on_open(ctx));
        CHECK_NOTHROW(cmd->on_close(ctx));
    }
}

TEST_CASE("Clamping leaves every sample inside the unit range")
{
    auto            img = make_image();
    TestEditContext ctx{img};
    ctx.subject.scope = EditSubject::Scope_AllChannels;

    // pushed well outside [0,1] first, so the clamp has something to do at both ends
    for (auto &ch : img->channels)
        for (int i = 0; i < ch.num_elements(); ++i) ch(i) = ch(i) * 4.f - 1.5f;

    auto cmd = find_command("Clamp to [0,1]");
    REQUIRE(cmd);
    cmd->apply(ctx);

    for (const auto &ch : img->channels)
        for (int i = 0; i < ch.num_elements(); ++i)
        {
            CHECK(ch(i) >= 0.f);
            CHECK(ch(i) <= 1.f);
        }
}

TEST_CASE("Zapping gremlins replaces the non-finite samples and leaves the rest")
{
    auto            img = make_image();
    TestEditContext ctx{img};
    ctx.subject.scope = EditSubject::Scope_AllChannels;

    auto       &ch   = img->channels[0];
    const float kept = ch(2, 2);
    ch(0, 0)         = std::numeric_limits<float>::quiet_NaN();
    ch(1, 0)         = std::numeric_limits<float>::infinity();
    ch(3, 1)         = -std::numeric_limits<float>::infinity();

    auto cmd = find_command("Zap gremlins...");
    REQUIRE(cmd);
    cmd->apply(ctx);

    // every gremlin gone...
    for (const auto &c : img->channels)
        for (int i = 0; i < c.num_elements(); ++i) CHECK(std::isfinite(c(i)));

    // ...and the samples that were already fine untouched
    CHECK(ch(2, 2) == kept);
}

TEST_CASE("Generating mipmaps halves the image down to the number of levels asked for")
{
    // separate images, since an Image's channels share one data window and a chain of different sizes has
    // nowhere to live in it
    auto            img = make_image();
    TestEditContext ctx{img};

    auto cmd = find_command("Generate mipmaps...");
    REQUIRE(cmd);
    REQUIRE(cmd->enabled(ctx));

    cmd->on_open(ctx);
    cmd->apply(ctx);

    const auto &made = ctx.added;
    REQUIRE_FALSE(made.empty());

    // each one half the size of the one before, never below a single sample
    int2 expected = k_size;
    for (size_t i = 0; i < made.size(); ++i)
    {
        expected = int2{std::max(1, expected.x / 2), std::max(1, expected.y / 2)};

        CAPTURE(i);
        CHECK(made[i]->size() == expected);
        CHECK(made[i]->channels.size() == img->channels.size());
        CHECK(made[i]->partname == fmt::format("mip {}", i + 1));
    }

    // down to one sample, where a pyramid stops
    CHECK(made.back()->size() == int2{1, 1});

    // and the image it came from is untouched: this makes images, it does not edit one
    CHECK(img->size() == k_size);
    CHECK(img->history.size() == 0);
}

TEST_CASE("A mip level is averaged from its samples rather than dropping three of every four")
{
    // a checkerboard alternating every sample averages to its mean; picking one of each four would keep it
    // at one extreme or the other
    auto img = std::make_shared<Image>(int2{16, 16}, 1);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) img->channels[0](x, y) = ((x + y) % 2) ? 1.f : 0.f;
    img->rebuild_layers();

    TestEditContext ctx{img};
    auto            cmd = find_command("Generate mipmaps...");
    REQUIRE(cmd);
    cmd->on_open(ctx);
    cmd->apply(ctx);

    REQUIRE_FALSE(ctx.added.empty());
    const auto &half = ctx.added.front();

    for (int i = 0; i < half->channels[0].num_elements(); ++i)
    {
        CAPTURE(i);
        CHECK(half->channels[0](i) == doctest::Approx(0.5f).epsilon(0.2));
    }
}

TEST_CASE("Regrouping reaches the whole layer from any one of its exploded channels")
{
    // two layers, each with a color of its own, so scoping can be told from clearing everything
    auto img = std::make_shared<Image>(k_size, 6);

    static const char *names[] = {"diffuse.R", "diffuse.G", "diffuse.B", "specular.R", "specular.G", "specular.B"};
    for (int c = 0; c < 6; ++c) img->channels[size_t(c)].name = names[c];
    img->rebuild_layers();

    REQUIRE(img->groups.size() == 2); // one per layer

    TestEditContext ctx{img};
    auto            explode = find_command("Ungroup channels");
    auto            regroup = find_command("Regroup channels");
    REQUIRE(explode);
    REQUIRE(regroup);

    // explode both layers, one at a time, since one group is all that can be selected
    for (int pass = 0; pass < 2; ++pass)
    {
        for (size_t g = 0; g < img->groups.size(); ++g)
            if (img->groups[g].num_channels > 1)
            {
                img->selected_group = int(g);
                break;
            }
        ctx.retarget();
        explode->apply(ctx);
    }

    CHECK(img->groups.size() == 6); // every channel on its own

    // regroup from a single channel of one layer, and only that layer comes back
    for (size_t g = 0; g < img->groups.size(); ++g)
        if (Channel::head(img->channels[size_t(img->groups[g].channels[0])].name) == "diffuse.")
        {
            img->selected_group = int(g);
            break;
        }

    ctx.retarget();
    REQUIRE(regroup->enabled(ctx));
    regroup->apply(ctx);

    // diffuse is a color again; specular is still three channels
    CHECK(img->groups.size() == 4);

    int diffuse_grouped = 0, specular_single = 0;
    for (const auto &group : img->groups)
    {
        const std::string layer = Channel::head(img->channels[size_t(group.channels[0])].name);
        if (layer == "diffuse." && group.num_channels == 3)
            ++diffuse_grouped;
        if (layer == "specular." && group.num_channels == 1)
            ++specular_single;
    }
    CHECK(diffuse_grouped == 1);
    CHECK(specular_single == 3);

    // and the other layer is reachable in its turn
    for (size_t g = 0; g < img->groups.size(); ++g)
        if (Channel::head(img->channels[size_t(img->groups[g].channels[0])].name) == "specular.")
        {
            img->selected_group = int(g);
            break;
        }

    ctx.retarget();
    REQUIRE(regroup->enabled(ctx));
    regroup->apply(ctx);
    CHECK(img->groups.size() == 2);
}

TEST_CASE("Regrouping from a selection puts back exactly the channels it names")
{
    // Explode an RGBA group, leave the alpha out of the selection, and ask for the rest back. Grouping is
    // still derived from the names: holding the alpha out makes R,G,B,A come up short so R,G,B matches.
    auto img = std::make_shared<Image>(k_size, std::vector<std::string>{"R", "G", "B", "A", "Z"});
    img->rebuild_layers();
    REQUIRE(img->groups.size() == 2); // the color, and the depth on its own

    TestEditContext ctx{img};
    auto            regroup = find_command("Regroup channels");
    REQUIRE(regroup);

    img->selected_group = group_of_channel(img, "R");
    ctx.retarget();
    find_command("Ungroup channels")->apply(ctx);
    REQUIRE(img->groups.size() == 5); // four singletons, plus the depth

    // R, G and B, plus the depth channel, which was never exploded and carries no mark; a regroup clearing
    // a flag it never set would record setting one on the way back
    img->deselect_all();
    for (const char *name : {"R", "G", "B", "Z"}) img->select_group(group_of_channel(img, name));
    img->selected_group = group_of_channel(img, "R");

    ctx.retarget();
    REQUIRE(regroup->enabled(ctx));
    regroup->apply(ctx);

    // R,G,B are a color again; the alpha stands alone, still marked; the depth is as it always was
    CHECK(img->groups.size() == 3);
    const int rgb = group_of_channel(img, "R");
    CHECK(img->groups[size_t(rgb)].num_channels == 3);
    CHECK(img->groups[size_t(group_of_channel(img, "A"))].num_channels == 1);
    CHECK(img->channels[size_t(channel_index(img, "A"))].ungrouped);
    CHECK_FALSE(img->channels[size_t(channel_index(img, "Z"))].ungrouped);

    // the viewport follows the group it was showing into whatever swallowed it
    CHECK(img->selected_group == rgb);

    // the regrouped channels are still selected, since the flags ride on the channels and not on the group
    // indices the rebuild renumbered
    CHECK(img->is_group_selected(rgb));

    // undo marks back what was cleared, and nothing else
    REQUIRE(img->history.undo(*img));
    CHECK(img->groups.size() == 5);
    for (const char *name : {"R", "G", "B", "A"}) CHECK(img->channels[size_t(channel_index(img, name))].ungrouped);
    CHECK_FALSE(img->channels[size_t(channel_index(img, "Z"))].ungrouped);
}

TEST_CASE("A group command covers every group it is given, as one entry, and nothing else")
{
    // All three are addressed by target_groups(). What they do differs, but "every target group changes and
    // nothing outside them does" is a property all three owe, and so is an entry restoring all of it.
    for (const std::string name : {"Ungroup channels", "Regroup channels", "Delete channel group"})
    {
        CAPTURE(name);

        auto img = make_layered_image(); // R,G,B,A | Z | normal.R,G,B
        REQUIRE(img->groups.size() == 3);

        // two of the three groups selected, so a command reaching only the one on screen and one reaching
        // every group are both caught
        img->selected_group = group_of_channel(img, "R");
        img->select_group(group_of_channel(img, "R"));
        img->select_group(group_of_channel(img, "normal.R"));

        TestEditContext ctx{img};

        // regrouping has nothing to put back until something has been taken apart; the selection rides on
        // the channels, so it survives the rebuild that renumbers the groups
        if (name == "Regroup channels")
        {
            find_command("Ungroup channels")->apply(ctx);
            REQUIRE(img->history.size() == 1);
            ctx.retarget();
            REQUIRE(ctx.target_groups.size() == 7);
        }
        else if (name == "Delete channel group")
            CHECK(delete_channels_label(img, ctx.target_groups) == "Delete channel groups");

        auto cmd = find_command(name);
        REQUIRE(cmd);
        REQUIRE(cmd->enabled(ctx));

        const Snapshot           before = snapshot(img);
        const std::vector<float> depth  = values(img->channels[size_t(channel_index(img, "Z"))]);
        const auto               shown  = names_of(img, img->selected_group);
        const int                steps  = img->history.size();

        cmd->apply(ctx);
        CHECK(img->history.size() == steps + 1); // one entry over the lot, not one per group

        // whatever it did, it left the group nobody selected alone
        const int z = channel_index(img, "Z");
        REQUIRE(z >= 0);
        CHECK_FALSE(img->channels[size_t(z)].ungrouped);
        CHECK(values(img->channels[size_t(z)]) == depth);

        // and it reached both of the ones that were
        if (name == "Delete channel group")
            CHECK(img->channels.size() == 1);
        else
        {
            const bool apart = name == "Ungroup channels";
            for (const char *n : {"R", "G", "B", "A", "normal.R", "normal.G", "normal.B"})
            {
                CAPTURE(n);
                CHECK(img->channels[size_t(channel_index(img, n))].ungrouped == apart);
            }

            // the viewport follows what it was showing into whichever group now holds it
            const auto now = names_of(img, img->selected_group);
            CHECK(std::find(now.begin(), now.end(), shown.front()) != now.end());
        }

        // undoing puts back the channels, their flags and the groups built from them, and redoing returns; see
        // the test below for the selection
        const Snapshot after = snapshot(img);
        REQUIRE(img->history.undo(*img));
        CHECK(snapshot(img).content() == before.content());
        REQUIRE(img->history.redo(*img));
        CHECK(snapshot(img).content() == after.content());
    }
}

namespace
{

/// Shows and selects the layered image's second color group, and points \p ctx at a group outside it.
/**
    Ungrouping is pointed at the first color group, R,G,B,A. The others are pointed at its G once it has been
    taken apart, which regrouping reaches back from to the whole layer, and deleting takes alone. That layer
    also holds the depth channel, standing alone without ever having been taken apart.
*/
int point_outside_selection(TestEditContext &ctx, const std::string &command)
{
    const auto &img     = ctx.image;
    img->selected_group = group_of_channel(img, "normal.R");
    img->select_group(img->selected_group);

    int pointed = group_of_channel(img, "R");
    if (command != "Ungroup channels")
    {
        ctx.retarget(pointed);
        find_command("Ungroup channels")->apply(ctx);
        pointed = group_of_channel(img, "G");
    }
    ctx.retarget(pointed);
    return pointed;
}

} // namespace

TEST_CASE("A group command pointed at a group outside the selection acts on it alone")
{
    // Pointing at a group is not selecting it: right-clicking one in the Images panel acts on that group and
    // leaves the selection where it was. A lone channel the command does not reach, taken apart or never
    // grouped, has to keep its flag through the command and its undo both.
    for (const std::string name : {"Ungroup channels", "Regroup channels", "Delete channel group"})
    {
        CAPTURE(name);

        auto            img = make_layered_image(); // R,G,B,A | Z | normal.R,G,B
        TestEditContext ctx{img};
        const int       pointed = point_outside_selection(ctx, name);
        REQUIRE_FALSE(img->is_group_selected(pointed));
        REQUIRE(img->groups[size_t(pointed)].num_channels == (name == "Ungroup channels" ? 4 : 1));

        const std::vector<std::string> reached = name == "Delete channel group"
                                                     ? std::vector<std::string>{"G"}
                                                     : std::vector<std::string>{"R", "G", "B", "A"};

        // pointing inside the selection covers the selection, as a click inside one keeps it; outside, only the
        // group pointed at
        ctx.retarget(img->selected_groups().front());
        CHECK(ctx.target_groups == img->selected_groups());
        ctx.retarget(pointed);
        CHECK(ctx.target_groups == std::vector<int>{pointed});

        // and the label says what it is about to take
        CHECK(delete_channels_label(img, ctx.target_groups) ==
              (img->groups[size_t(pointed)].num_channels == 1 ? "Delete channel" : "Delete channel group"));

        auto cmd = find_command(name);
        REQUIRE(cmd);
        REQUIRE(cmd->enabled(ctx));

        struct Was
        {
            std::string        name;
            bool               ungrouped;
            std::vector<float> samples;
        };
        std::vector<Was> was;
        for (const auto &c : img->channels) was.push_back({c.name, c.ungrouped, values(c)});

        const Snapshot before   = snapshot(img);
        const auto     selected = selected_names(img);

        cmd->apply(ctx);

        // every channel the command does not reach is as it was: present, flagged and filled the same
        for (const auto &c : was)
        {
            if (std::find(reached.begin(), reached.end(), c.name) != reached.end())
                continue;
            CAPTURE(c.name);
            const int now = channel_index(img, c.name);
            REQUIRE(now >= 0);
            CHECK(img->channels[size_t(now)].ungrouped == c.ungrouped);
            CHECK(values(img->channels[size_t(now)]) == c.samples);
        }

        // what was pointed at changed as the command says
        for (const auto &n : reached)
        {
            CAPTURE(n);
            const int now = channel_index(img, n);
            if (name == "Delete channel group")
                CHECK(now < 0);
            else
            {
                REQUIRE(now >= 0);
                CHECK(img->channels[size_t(now)].ungrouped == (name == "Ungroup channels"));
            }
        }

        // the selection stays where it was, though the rebuild renumbered the groups
        CHECK(selected_names(img) == selected);

        REQUIRE(img->history.undo(*img));
        CHECK(snapshot(img).content() == before.content());
    }
}

TEST_CASE("A group command keeps the group on screen through its rebuild, and undoing it keeps the selection" *
          doctest::should_fail())
{
    // Fails as the commands stand. The rebuild renumbers the groups and only a changed group's channel is
    // followed into its new index, so acting on a group ahead of the one on screen leaves the viewport's index
    // naming another group; and a structural entry restores the channels without their selected flags.
    for (const std::string name : {"Ungroup channels", "Regroup channels", "Delete channel group"})
    {
        CAPTURE(name);

        auto            img = make_layered_image();
        TestEditContext ctx{img};
        point_outside_selection(ctx, name);

        const Snapshot before = snapshot(img);
        const auto     shown  = names_of(img, img->selected_group);

        auto cmd = find_command(name);
        REQUIRE(cmd);
        cmd->apply(ctx);
        CHECK(names_of(img, img->selected_group) == shown);

        REQUIRE(img->history.undo(*img));
        CHECK(snapshot(img) == before);
    }
}

TEST_CASE("A seamless paste lands at the selection's top-left, or the image's without one, and nowhere else")
{
    // The clipboard is constant and the background a checkerboard, so whatever stopped being the checkerboard
    // is what the paste touched, which says where it went without trusting the command's own account of it.
    // The border ring keeps the background, so what moves is the interior, one sample in from the patch on
    // every side; alpha is the mask and is left alone. The solve itself is test_poisson.cpp's business.
    const int2 size{24, 20}, patch{8, 8};
    auto       checker = [](int2 p) { return ((p.x / 4 + p.y / 4) % 2) ? 1.f : 0.f; };

    auto clip = std::make_shared<Image>(patch, 4);
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < clip->channels[size_t(c)].num_elements(); ++i)
            clip->channels[size_t(c)](i) = c < 3 ? 0.5f : 1.f;
    clip->rebuild_layers();

    const int2 at{12, 8};
    const struct
    {
        const char *what;
        Box2i       selection;
        int2        lands; ///< where the patch's top-left should end up
    } cases[] = {
        {"a selection the size of the clipboard", Box2i{at, at + patch}, at},
        // larger: the clipboard is placed at the selection's top-left, not centered or scaled
        {"a selection larger than the clipboard", Box2i{at, size}, at},
        // none at all: the whole image is the target, so the patch lands at its top-left
        {"no selection", Box2i{}, int2{0}},
    };

    for (const auto &c : cases)
    {
        CAPTURE(c.what);

        auto img = std::make_shared<Image>(size, 4);
        for (int ch = 0; ch < 4; ++ch)
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) img->channels[size_t(ch)](x, y) = ch < 3 ? checker(int2{x, y}) : 1.f;
        img->rebuild_layers();

        TestEditContext ctx{img};
        ctx.clipboard_image = clip;
        ctx.roi             = c.selection;

        auto cmd = find_command("Seamless paste...");
        REQUIRE(cmd);
        REQUIRE(cmd->enabled(ctx));
        cmd->apply(ctx);
        CHECK(img->history.undo_name() == "Seamless paste");

        const Box2i interior{c.lands + 1, c.lands + patch - 1};
        for (int ch = 0; ch < 4; ++ch)
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                {
                    CAPTURE(ch);
                    CAPTURE(x);
                    CAPTURE(y);
                    const float was   = ch < 3 ? checker(int2{x, y}) : 1.f;
                    const bool  moved = std::fabs(img->channels[size_t(ch)](x, y) - was) > 1e-4f;
                    CHECK(moved == (ch < 3 && in_box(interior, int2{x, y})));
                }
    }
}

TEST_CASE("Copy, cut and paste carry samples from where they were taken to where they are put")
{
    auto            img = make_image(); // RGBA, every sample distinct, partly transparent throughout
    TestEditContext ctx{img};

    auto copy = find_command("Copy"), cut = find_command("Cut"), paste = find_command("Paste");
    REQUIRE(copy);
    REQUIRE(cut);
    REQUIRE(paste);

    const std::vector<float> original    = samples(img);
    const size_t             per_channel = size_t(k_size.x * k_size.y);
    auto was = [&](int c, int2 p) { return original[size_t(c) * per_channel + size_t(p.y * k_size.x + p.x)]; };

    // the clipboard holds what the selection covered, at its size, and copying edits nothing
    const Box2i from{{1, 1}, {4, 3}};
    ctx.roi = from;
    copy->apply(ctx);
    REQUIRE(ctx.clipboard_image);
    const auto clip = ctx.clipboard_image;
    REQUIRE(clip->size() == from.size());
    for (int c = 0; c < 4; ++c)
        for (int y = 0; y < from.size().y; ++y)
            for (int x = 0; x < from.size().x; ++x)
                CHECK(clip->channels[size_t(c)](x, y) == was(c, from.min + int2{x, y}));
    CHECK(img->history.size() == 0);

    // Pasted into a selection larger than it: at the selection's top-left, source-over what is there on the
    // premultiplied samples, the same expression for color and alpha, and nothing past the clipboard's extent.
    const Box2i to{{3, 2}, {7, 5}};
    ctx.roi = to;
    paste->apply(ctx);
    CHECK(img->history.undo_name() == "Paste");
    for (int c = 0; c < 4; ++c)
        for (int y = 0; y < k_size.y; ++y)
            for (int x = 0; x < k_size.x; ++x)
            {
                CAPTURE(c);
                CAPTURE(x);
                CAPTURE(y);
                const int2 q = int2{x, y} - to.min;
                if (in_box(to, int2{x, y}) && in_box(Box2i{int2{0}, clip->size()}, q))
                    CHECK(img->channels[size_t(c)](x, y) ==
                          doctest::Approx(clip->channels[size_t(c)](q.x, q.y) +
                                          was(c, int2{x, y}) * (1.f - clip->channels[3](q.x, q.y))));
                else
                    CHECK(img->channels[size_t(c)](x, y) == was(c, int2{x, y}));
            }
    REQUIRE(img->history.undo(*img));
    REQUIRE(samples(img) == original);

    // Cutting takes the samples into the clipboard and leaves zero behind: these are held premultiplied, where
    // a color with no alpha is additive and not invisible.
    const Box2i taken{{4, 1}, {7, 4}};
    ctx.roi = taken;
    cut->apply(ctx);
    REQUIRE(ctx.clipboard_image);
    REQUIRE(ctx.clipboard_image->size() == taken.size());
    for (int c = 0; c < 4; ++c)
        for (int y = 0; y < k_size.y; ++y)
            for (int x = 0; x < k_size.x; ++x)
            {
                CAPTURE(c);
                CAPTURE(x);
                CAPTURE(y);
                const bool inside = in_box(taken, int2{x, y});
                CHECK(img->channels[size_t(c)](x, y) == (inside ? 0.f : was(c, int2{x, y})));
                if (inside)
                    CHECK(ctx.clipboard_image->channels[size_t(c)](x - taken.min.x, y - taken.min.y) ==
                          was(c, int2{x, y}));
            }
}

TEST_CASE("Cropping is offered only for a selection that would change the image, and crops to what lands")
{
    const struct
    {
        const char *what;
        Box2i       selection;
        bool        offered;
    } cases[] = {
        {"no selection", Box2i{}, false},
        {"the whole image, which would crop to itself", Box2i{int2{0}, k_size}, false},
        {"around all of it", Box2i{{-3, -2}, {20, 20}}, false},
        {"missing it entirely", Box2i{{10, 10}, {12, 12}}, false},
        {"hanging off a corner", Box2i{{5, 3}, {9, 9}}, true},
        {"inside", Box2i{{1, 1}, {5, 4}}, true},
    };

    for (const auto &c : cases)
    {
        CAPTURE(c.what);

        auto            img = make_image();
        TestEditContext ctx{img};
        ctx.roi = c.selection;

        auto crop = find_command("Crop to selection");
        REQUIRE(crop);
        CHECK(crop->enabled(ctx) == c.offered);

        const Snapshot           before   = snapshot(img);
        const std::vector<float> original = samples(img);
        crop->apply(ctx);

        if (!c.offered)
        {
            // reached anyway, from the palette, it does nothing
            CHECK(snapshot(img) == before);
            CHECK(img->history.size() == 0);
            continue;
        }

        // the part of the selection that lands on the image, which is the whole image now
        const int2 lo = la::max(c.selection.min, int2{0}), hi = la::min(c.selection.max, k_size);
        REQUIRE(img->size() == hi - lo);
        CHECK(img->display_window.size() == hi - lo);
        for (int ch = 0; ch < 4; ++ch)
        {
            REQUIRE(img->channels[size_t(ch)].size() == hi - lo);
            for (int y = 0; y < hi.y - lo.y; ++y)
                for (int x = 0; x < hi.x - lo.x; ++x)
                    CHECK(img->channels[size_t(ch)](x, y) ==
                          original[size_t(ch * k_size.x * k_size.y + (lo.y + y) * k_size.x + lo.x + x)]);
        }

        // and what was selected is all there is, so the selection has nothing left to say
        CHECK_FALSE(ctx.roi.has_volume());
    }
}
