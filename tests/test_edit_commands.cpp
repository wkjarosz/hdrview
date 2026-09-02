//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file test_edit_commands.cpp
    \author Wojciech Jarosz

    The edit commands driven through EditContext, so everything but the dialog runs with no window, GL
    context or frame loop.
*/

#include <doctest/doctest.h>

#include "edit/commands.h"
#include "edit/subject.h"
#include "image.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace
{

//! EditContext over a bare Image: same undo/subject rules as HDRViewApp, no textures or worker thread.
//! The async ops run inline.
class TestEditContext : public EditContext
{
public:
    explicit TestEditContext(ImagePtr img) : m_image(std::move(img)) {}

    ImagePtr           image() const override { return m_image; }
    const EditSubject &subject() const override { return m_subject; }

    //! Mirrors HDRViewApp::target_groups(): a group pointed at from outside the selection names itself alone,
    //! one inside it covers the selection, and an image the panel never saw falls back to the group on screen.
    std::vector<int> target_groups() const override
    {
        if (!m_image)
            return {};

        if (m_target_group >= 0 && !m_image->is_group_selected(m_target_group))
            return {m_target_group};

        std::vector<int> groups = m_image->selected_groups();
        const int        shown  = m_image->active_group_index(Target_Primary);
        if (groups.empty() && m_image->is_valid_group(shown))
            groups.push_back(shown);
        return groups;
    }

    //! Point at a group without selecting it, the way the Images panel's context menu does.
    void          set_target_group(int group) { m_target_group = group; }
    Box2i         selection() const override { return m_selection; }
    void          set_selection(const Box2i &box) override { m_selection = box; }
    float4        background_color() const override { return m_background; }
    ConstImagePtr clipboard() const override { return m_clipboard; }
    void          set_clipboard(ImagePtr img) override { m_clipboard = std::move(img); }
    void          draw_subject_selector() override {}

    //! Collected, so a command that produces images can be checked for what it made.
    void add_image(ImagePtr img, const std::string &partname) override
    {
        if (!img)
            return;
        img->partname = partname;
        m_added.push_back(std::move(img));
    }

    const std::vector<ImagePtr> &added_images() const { return m_added; }

    EditSubject &mutable_subject() { return m_subject; }
    void         set_background(float4 c) { m_background = c; }

    //! Whether the last edit went through a chokepoint that takes the subject at all. Not the same question as
    //! Info::draws_subject_selector, which says only whether the dialog draws the "Apply to" controls.
    bool last_edit_used_subject() const { return m_used_subject; }

    bool modify_pixels(const std::string &name, const std::function<float(float, int2, int)> &op) override
    {
        auto [channels, bounds] = resolve();
        if (channels.empty() || !bounds.has_volume())
            return false;

        return edit(name, channels, bounds,
                    [&](Image &img)
                    {
                        for (size_t i = 0; i < channels.size(); ++i)
                            for (int y = bounds.min.y; y < bounds.max.y; ++y)
                                for (int x = bounds.min.x; x < bounds.max.x; ++x)
                                {
                                    auto &ch = img.channels[size_t(channels[i])];
                                    ch(x, y) = op(ch(x, y), int2{x, y}, int(i));
                                }
                    });
    }

    bool modify_colors(const std::string &name, const std::function<float4(const float4 &, int2)> &op,
                       const std::function<void(Image &)> &retag = {}) override
    {
        auto [channels, bounds] = resolve();
        if (channels.empty() || !bounds.has_volume())
            return false;

        const bool ok = edit(name, channels, bounds,
                             [&](Image &img)
                             {
                                 const int n = int(channels.size());
                                 for (int y = bounds.min.y; y < bounds.max.y; ++y)
                                     for (int x = bounds.min.x; x < bounds.max.x; ++x)
                                     {
                                         float4 c{0.f, 0.f, 0.f, 1.f};
                                         for (int i = 0; i < std::min(4, n); ++i)
                                             c[i] = img.channels[size_t(channels[size_t(i)])](x, y);

                                         const float4 out = op(c, int2{x, y});
                                         for (int i = 0; i < std::min(4, n); ++i)
                                             img.channels[size_t(channels[size_t(i)])](x, y) = out[i];
                                     }
                             });
        if (ok && retag)
            retag(*m_image);
        return ok;
    }

    bool modify_neighborhood(const std::string                                                      &name,
                             const std::function<float4(const std::function<float4(int2)> &, int2)> &op, int,
                             int) override
    {
        auto [channels, bounds] = resolve();
        if (channels.empty() || !bounds.has_volume())
            return false;

        // the reader sees the image as it was, so an op cannot read what it has already written
        const Image before = snapshot();

        return edit(name, channels, bounds,
                    [&](Image &img)
                    {
                        const int n    = int(channels.size());
                        auto      read = [&](int2 p)
                        {
                            float4     c{0.f, 0.f, 0.f, 1.f};
                            const int2 q{std::clamp(p.x, 0, img.size().x - 1), std::clamp(p.y, 0, img.size().y - 1)};
                            for (int i = 0; i < std::min(4, n); ++i)
                                c[i] = before.channels[size_t(channels[size_t(i)])](q.x, q.y);
                            return c;
                        };

                        for (int y = bounds.min.y; y < bounds.max.y; ++y)
                            for (int x = bounds.min.x; x < bounds.max.x; ++x)
                            {
                                const float4 out = op(read, int2{x, y});
                                for (int i = 0; i < std::min(4, n); ++i)
                                    img.channels[size_t(channels[size_t(i)])](x, y) = out[i];
                            }
                    });
    }

    bool modify_channels(const std::string                                              &name,
                         const std::function<Array2Df(const Array2Df &, const Box2i &)> &filter) override
    {
        auto [channels, bounds] = resolve();
        if (channels.empty() || !bounds.has_volume())
            return false;

        return edit(name, channels, bounds,
                    [&](Image &img)
                    {
                        for (int c : channels)
                        {
                            Channel       &ch  = img.channels[size_t(c)];
                            const Array2Df out = filter(ch, bounds);
                            for (int y = 0; y < bounds.size().y; ++y)
                                for (int x = 0; x < bounds.size().x; ++x)
                                    ch(bounds.min.x + x, bounds.min.y + y) = out(x, y);
                        }
                    });
    }

    void modify_channels_async(
        const std::string                                                                   &name,
        const std::function<Array2Df(const Array2Df &, const Box2i &, int, AtomicProgress)> &f) override
    {
        auto [channels, bounds] = resolve();
        if (channels.empty() || !bounds.has_volume())
            return;

        edit(name, channels, bounds,
             [&](Image &img)
             {
                 std::vector<Array2Df> results;
                 results.reserve(channels.size());
                 for (size_t i = 0; i < channels.size(); ++i)
                     results.push_back(f(img.channels[size_t(channels[i])], bounds, int(i), AtomicProgress{}));

                 for (size_t i = 0; i < channels.size(); ++i)
                 {
                     Channel &ch = img.channels[size_t(channels[i])];
                     for (int y = 0; y < bounds.size().y; ++y)
                         for (int x = 0; x < bounds.size().x; ++x)
                             ch(bounds.min.x + x, bounds.min.y + y) = results[i](x, y);
                 }
             });
    }

    void modify_image_async(const std::string &name, int2 size,
                            const std::function<Array2Df(const Array2Df &, AtomicProgress)> &op) override
    {
        if (!m_image || size.x <= 0 || size.y <= 0)
            return;

        auto entry = std::make_unique<StructureUndo>(*m_image, name);

        std::vector<Array2Df> results;
        results.reserve(m_image->channels.size());
        for (auto &ch : m_image->channels) results.push_back(op(ch, AtomicProgress{}));

        for (size_t i = 0; i < m_image->channels.size(); ++i)
        {
            m_image->channels[i].resize(size);
            std::copy(results[i].data(), results[i].data() + results[i].num_elements(), m_image->channels[i].data());
        }
        m_image->data_window    = Box2i{m_image->data_window.min, m_image->data_window.min + size};
        m_image->display_window = m_image->data_window;

        m_image->history.add(std::move(entry));
        ++m_image->content_version;
    }

    bool modify_structure(const std::string &name, const std::function<void(Image &)> &op) override
    {
        if (!m_image)
            return false;

        auto entry = std::make_unique<StructureUndo>(*m_image, name);
        op(*m_image);
        m_image->history.add(std::move(entry));
        ++m_image->content_version;
        return true;
    }

    bool modify_reversibly(const std::string &name, const std::function<void(Image &)> &forward,
                           const std::function<void(Image &)> &backward) override
    {
        if (!m_image)
            return false;

        forward(*m_image);
        m_image->history.add(std::make_unique<LambdaUndo>(name, backward, forward));
        ++m_image->content_version;
        return true;
    }

private:
    //! Which channels and which rectangle the subject names; mirrors HDRViewApp::resolve_subject().
    std::pair<std::vector<int>, Box2i> resolve() const
    {
        std::vector<int> channels;
        if (!m_image)
            return {channels, Box2i{}};

        if (m_subject.scope == EditSubject::Scope_AllChannels)
        {
            channels.resize(m_image->channels.size());
            std::iota(channels.begin(), channels.end(), 0);
        }
        else if (int g = m_image->active_group_index(Target_Primary); m_image->is_valid_group(g))
        {
            const auto &group = m_image->groups[size_t(g)];
            for (int c = 0; c < group.num_channels; ++c) channels.push_back(group.channels[c]);
        }

        Box2i bounds = m_image->data_window;
        if (m_subject.selection_only && m_selection.has_volume())
            bounds.intersect(m_selection);

        return {channels, bounds};
    }

    //! The undo entry is built before the edit, so it records what the edit is about to displace.
    bool edit(const std::string &name, const std::vector<int> &channels, const Box2i &bounds,
              const std::function<void(Image &)> &op)
    {
        m_used_subject = true;
        auto entry     = std::make_unique<ChannelRectUndo>(*m_image, channels, bounds, name);
        op(*m_image);
        m_image->history.add(std::move(entry));
        ++m_image->content_version;
        return true;
    }

    //! A copy of the samples alone, for the ops that must read the image as it was.
    Image snapshot() const
    {
        Image copy{m_image->size(), int(m_image->channels.size())};
        for (size_t c = 0; c < m_image->channels.size(); ++c)
            std::copy(m_image->channels[c].data(), m_image->channels[c].data() + m_image->channels[c].num_elements(),
                      copy.channels[c].data());
        return copy;
    }

    bool                  m_used_subject = false;
    int                   m_target_group = -1;
    std::vector<ImagePtr> m_added;
    ImagePtr              m_image;
    ImagePtr              m_clipboard;
    EditSubject           m_subject;
    Box2i                 m_selection;
    float4                m_background{0.f, 0.f, 0.f, 1.f};
};

constexpr int2 k_size{7, 5};

//! An image whose every sample is distinct, so any misplacement shows as a mismatch.
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

//! An image with three channel groups: two color groups and a depth channel, so "the group on screen", "the
//! selected groups" and "every channel" are three different sets and the color-only filter has one to drop.
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

//! The index of the group holding the channel named \p name, or -1.
int group_of_channel(const ImagePtr &img, const std::string &name)
{
    for (size_t g = 0; g < img->groups.size(); ++g)
        for (int i = 0; i < img->groups[g].num_channels; ++i)
            if (img->channels[size_t(img->groups[g].channels[i])].name == name)
                return int(g);
    return -1;
}

//! The index of the channel named \p name, or -1.
int channel_index(const ImagePtr &img, const std::string &name)
{
    for (size_t c = 0; c < img->channels.size(); ++c)
        if (img->channels[c].name == name)
            return int(c);
    return -1;
}

//! Every sample of every channel, as one comparable value.
std::vector<float> samples(const ImagePtr &img)
{
    std::vector<float> out;
    for (const auto &ch : img->channels)
        for (int i = 0; i < ch.num_elements(); ++i) out.push_back(ch(i));
    return out;
}

std::string first_name(const EditCommandPtr &cmd) { return cmd->info().names.front(); }

//! The registered command whose first name is \p name.
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

        // "..." means a dialog everywhere in the interface, so a command with no draw() must not claim one
        const std::string &n        = info.names.front();
        const bool         ellipsis = n.size() >= 3 && n.compare(n.size() - 3, 3, "...") == 0;
        CHECK(cmd->has_dialog() == ellipsis);

        names.push_back(info.names.front());
    }

    // addressed by name, so two commands sharing one would make the second unreachable
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
}

TEST_CASE("Every edit that applies leaves exactly one undo entry, and undoing restores the image")
{
    // a command recording two entries, or none, or one restoring only part of what it changed fails here
    for (auto &cmd : all_edit_commands())
    {
        const std::string name = first_name(cmd);
        CAPTURE(name);

        auto            img = make_image();
        TestEditContext ctx{img};

        // something on the clipboard, so the paste commands are exercised and not skipped
        ctx.set_clipboard(make_image());

        if (!cmd->enabled(ctx))
            continue;

        const std::vector<float> before       = samples(img);
        const int2               size_before  = img->size();
        const int                steps_before = img->history.size();

        cmd->apply(ctx);

        const int added = img->history.size() - steps_before;
        CHECK(added <= 1); // never more than one step per invocation

        if (added == 0)
        {
            // a command that declined to do anything must not have changed the image either
            CHECK(samples(img) == before);
            CHECK(img->size() == size_before);
            continue;
        }

        // named for the history panel; the name need not be the command's own, since one command can record
        // different edits (a blur names the kind of blur it did)
        CHECK_FALSE(img->history.undo_name().empty());

        const std::vector<float> after      = samples(img);
        const int2               size_after = img->size();

        REQUIRE(img->history.undo(*img));
        CHECK(img->size() == size_before);
        CHECK(samples(img) == before);

        // and redo returns to what the edit produced, so the entry is good in both directions
        REQUIRE(img->history.redo(*img));
        CHECK(img->size() == size_after);
        CHECK(samples(img) == after);
    }
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

            auto            img = make_layered_image();
            TestEditContext ctx{img};
            ctx.set_clipboard(make_image());

            // the color group on screen and the two color groups selected, so "selected" is neither the
            // current group nor every channel
            img->selected_group = group_of_channel(img, "R");
            img->select_group(img->selected_group);
            img->select_group(group_of_channel(img, "normal.R"));

            // a selection well inside the image, so there is untouched ground on every side of it
            const Box2i roi{int2{2, 1}, int2{5, 4}};
            ctx.set_selection(roi);
            ctx.mutable_subject().selection_only = true;
            ctx.mutable_subject().scope          = EditSubject::Scope(scope);

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
            const ConstImagePtr      clip_before = ctx.clipboard();

            cmd->apply(ctx);

            // there is one clipboard, so a command that fills it cannot also run once per selected image
            if (ctx.clipboard() != clip_before)
                CHECK_FALSE(cmd->info().fans_out);

            // an edit that changed the image without going through a chokepoint taking the subject is not
            // narrowed by it, and must not offer controls that would change nothing
            if (img->history.size() != steps && !ctx.last_edit_used_subject())
                CHECK_FALSE(cmd->info().draws_subject_selector);

            // a flip moves every sample by definition, and a resize has no rectangle to stay in
            if (img->history.size() == steps || img->size() != k_size || !ctx.last_edit_used_subject())
                continue;

            for (size_t c = 0; c < img->channels.size(); ++c)
            {
                const bool named = std::find(covered.begin(), covered.end(), int(c)) != covered.end();
                for (int y = 0; y < k_size.y; ++y)
                    for (int x = 0; x < k_size.x; ++x)
                    {
                        if (named && roi.contains(int2{x, y}))
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

TEST_CASE("Each scope names the channels it says it does")
{
    auto img = make_layered_image();

    const int color = group_of_channel(img, "R");
    const int depth = group_of_channel(img, "Z");
    const int other = group_of_channel(img, "normal.R");
    REQUIRE(img->groups.size() == 3);
    REQUIRE(color >= 0);
    REQUIRE(depth >= 0);
    REQUIRE(other >= 0);

    // the group on screen is the color one and the selection is that plus the second color group, not the
    // depth channel, so the three scopes cover 4, 7 and 8 of the 8 channels
    img->selected_group = color;
    img->select_group(color);
    img->select_group(other);

    EditSubject subject;
    subject.selection_only = false;

    auto channels_under = [&](EditSubject::Scope s)
    {
        subject.scope = s;
        return subject_channels(*img, subject);
    };
    auto color_groups_under = [&](EditSubject::Scope s)
    {
        subject.scope = s;
        return subject_color_groups(*img, subject);
    };

    const auto current  = channels_under(EditSubject::Scope_CurrentGroup);
    const auto selected = channels_under(EditSubject::Scope_SelectedGroups);
    const auto all      = channels_under(EditSubject::Scope_AllChannels);

    CHECK(current.size() == 4);  // R,G,B,A
    CHECK(selected.size() == 7); // and normal.R,G,B, but not Z
    CHECK(all.size() == img->channels.size());

    // narrower to wider: each scope names everything the one before it did
    for (int c : current) CHECK(std::find(selected.begin(), selected.end(), c) != selected.end());
    for (int c : selected) CHECK(std::find(all.begin(), all.end(), c) != all.end());

    // the one channel the selection leaves out is the one whose group was not selected
    CHECK(std::find(selected.begin(), selected.end(), channel_index(img, "Z")) == selected.end());

    // selecting every group makes "selected" and "all channels" the same set, since every channel of this
    // image belongs to a group
    img->select_group(depth);
    CHECK(channels_under(EditSubject::Scope_SelectedGroups).size() == img->channels.size());
    img->select_group(depth, false);

    // the color resolver keeps only the RGB/RGBA groups, whatever the scope, and its channel list is the
    // union of the groups it kept
    for (int s = 0; s < EditSubject::Scope_COUNT; ++s)
    {
        CAPTURE(s);
        auto [groups, channels] = color_groups_under(EditSubject::Scope(s));

        size_t expected = 0;
        for (int g : groups)
        {
            const auto t = img->groups[size_t(g)].type;
            CHECK((t == ChannelGroup::RGB_Channels || t == ChannelGroup::RGBA_Channels));
            expected += size_t(img->groups[size_t(g)].num_channels);
        }
        CHECK(channels.size() == expected);
        CHECK(std::find(channels.begin(), channels.end(), channel_index(img, "Z")) == channels.end());
    }

    // 1, 2 and 2 groups: the depth channel is all "all channels" adds over the selection, and it is not a color
    CHECK(color_groups_under(EditSubject::Scope_CurrentGroup).first.size() == 1);
    CHECK(color_groups_under(EditSubject::Scope_SelectedGroups).first.size() == 2);
    CHECK(color_groups_under(EditSubject::Scope_AllChannels).first.size() == 2);
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

TEST_CASE("An edit covers the whole image when there is no selection")
{
    // with the box cleared, every sample the scope names has to move, not just the ones a stale rectangle
    // covered
    for (auto &cmd : all_edit_commands())
    {
        const std::string name = first_name(cmd);
        CAPTURE(name);

        auto            img = make_image();
        TestEditContext ctx{img};
        ctx.set_clipboard(make_image());
        ctx.mutable_subject().scope = EditSubject::Scope_AllChannels;

        if (!cmd->enabled(ctx))
            continue;

        const std::vector<float> before = samples(img);
        const int                steps  = img->history.size();

        cmd->apply(ctx);

        if (img->history.size() == steps || !ctx.last_edit_used_subject() || img->size() != k_size)
            continue;

        // it was allowed to write anywhere, so the undo entry has to cover the whole image
        REQUIRE(img->history.undo(*img));
        CHECK(samples(img) == before);
    }
}

TEST_CASE("Clamping leaves every sample inside the unit range")
{
    auto            img = make_image();
    TestEditContext ctx{img};
    ctx.mutable_subject().scope = EditSubject::Scope_AllChannels;

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
    ctx.mutable_subject().scope = EditSubject::Scope_AllChannels;

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

TEST_CASE("Exploding a group takes its channels out of it, and regrouping puts them back")
{
    auto            img = make_image();
    TestEditContext ctx{img};

    // what the names alone give: one group over all four channels, or over three with alpha beside them
    const size_t grouped = img->groups.size();
    REQUIRE(grouped < img->channels.size());

    auto explode = find_command("Ungroup channels");
    REQUIRE(explode);
    REQUIRE(explode->enabled(ctx));

    explode->apply(ctx);

    // every channel of the group it was showing now stands on its own
    CHECK(img->groups.size() > grouped);
    for (const auto &group : img->groups) CHECK(group.num_channels == 1);

    // and nothing else about the image moved: this says how to look at it, not what it is
    CHECK(img->channels.size() == 4);
    CHECK(img->size() == k_size);

    // Only one group can be selected at a time, so regrouping works from whichever single exploded channel is
    // showing, which is why it is scoped to the layer. The explosion leaves the selection on one it made.
    REQUIRE(img->is_valid_group(img->selected_group));
    REQUIRE(img->groups[size_t(img->selected_group)].num_channels == 1);

    auto regroup = find_command("Regroup channels");
    REQUIRE(regroup);
    REQUIRE(regroup->enabled(ctx));

    regroup->apply(ctx);
    CHECK(img->groups.size() == grouped);

    // undoing the regroup explodes it again and undoing that puts it back; the flags ride the history
    // without storing a sample
    REQUIRE(img->history.undo(*img));
    CHECK(img->groups.size() > grouped);
    REQUIRE(img->history.undo(*img));
    CHECK(img->groups.size() == grouped);
}

TEST_CASE("Marking one channel leaves the others grouped")
{
    // the flag is per channel, so taking the alpha out of an RGBA image leaves a color behind and not four
    // separate channels
    auto img = make_image();

    const size_t grouped = img->groups.size();

    img->channels[3].ungrouped = true;
    img->rebuild_layers();

    // alpha on its own, and R, G, B still one group between them
    CHECK(img->groups.size() == grouped + 1);

    int multi = 0, single = 0;
    for (const auto &group : img->groups) (group.num_channels > 1 ? multi : single) += 1;
    CHECK(multi == 1);
    CHECK(single >= 1);

    for (const auto &group : img->groups)
        if (group.num_channels > 1)
            CHECK(group.num_channels == 3); // R, G and B, without the alpha

    img->channels[3].ungrouped = false;
    img->rebuild_layers();
    CHECK(img->groups.size() == grouped);
}

TEST_CASE("Deleting a channel group removes its channels, and undo brings them back")
{
    auto            img = make_image();
    TestEditContext ctx{img};

    // explode first, so there is more than one group and deleting one leaves an image behind
    find_command("Ungroup channels")->apply(ctx);

    const size_t channels_before = img->channels.size();
    const size_t groups_before   = img->groups.size();
    REQUIRE(groups_before > 1);

    auto del = find_command("Delete channel group");
    REQUIRE(del);
    REQUIRE(del->enabled(ctx));

    const std::string gone = img->channels[size_t(img->groups[size_t(img->selected_group)].channels[0])].name;

    del->apply(ctx);

    // gone, unlike exploding: this is what would be written on save
    CHECK(img->channels.size() == channels_before - 1);
    CHECK(img->groups.size() == groups_before - 1);
    for (const auto &c : img->channels) CHECK(c.name != gone);

    REQUIRE(img->history.undo(*img));
    CHECK(img->channels.size() == channels_before);
    CHECK(img->groups.size() == groups_before);

    bool found = false;
    for (const auto &c : img->channels) found = found || c.name == gone;
    CHECK(found);
}

TEST_CASE("Deleting is refused when it would leave nothing behind")
{
    // an image with no channels is not an image, so the last group has to stay
    auto            img = make_image();
    TestEditContext ctx{img};

    auto del = find_command("Delete channel group");
    REQUIRE(del);

    // one group over every channel, so deleting it would empty the image
    if (img->groups.size() == 1)
        CHECK_FALSE(del->enabled(ctx));

    // and applying it anyway changes nothing, since a command may be reached from the palette
    const size_t before = img->channels.size();
    if (!del->enabled(ctx))
    {
        del->apply(ctx);
        CHECK(img->channels.size() == before);
    }
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

    const auto &made = ctx.added_images();
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

    REQUIRE_FALSE(ctx.added_images().empty());
    const auto &half = ctx.added_images().front();

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

    REQUIRE(regroup->enabled(ctx));
    regroup->apply(ctx);
    CHECK(img->groups.size() == 2);
}

TEST_CASE("Regrouping touches only the channels an explosion marked, not every lone channel")
{
    // A layer can hold channels standing alone because their names never grouped, a depth channel beside a
    // color say. Marking those on the way back would be a change nobody asked for.
    auto img = std::make_shared<Image>(k_size, 4);

    static const char *names[] = {"beauty.R", "beauty.G", "beauty.B", "beauty.Z"};
    for (int c = 0; c < 4; ++c) img->channels[size_t(c)].name = names[c];
    img->rebuild_layers();

    // the color as one group, the depth on its own without anyone having said so
    REQUIRE(img->groups.size() == 2);
    for (const auto &c : img->channels) REQUIRE_FALSE(c.ungrouped);

    TestEditContext ctx{img};

    // select the color and explode it
    for (size_t g = 0; g < img->groups.size(); ++g)
        if (img->groups[g].num_channels > 1)
        {
            img->selected_group = int(g);
            break;
        }
    find_command("Ungroup channels")->apply(ctx);

    // three marked, and the depth channel still unmarked though it too stands alone
    CHECK(img->channels[0].ungrouped);
    CHECK(img->channels[1].ungrouped);
    CHECK(img->channels[2].ungrouped);
    CHECK_FALSE(img->channels[3].ungrouped);

    find_command("Regroup channels")->apply(ctx);
    CHECK(img->groups.size() == 2);
    for (const auto &c : img->channels) CHECK_FALSE(c.ungrouped);

    // undoing the regroup marks back what it cleared; one that had swept up every lone channel would mark
    // the depth channel, which was never exploded
    REQUIRE(img->history.undo(*img));
    CHECK(img->channels[0].ungrouped);
    CHECK_FALSE(img->channels[3].ungrouped);
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
    find_command("Ungroup channels")->apply(ctx);
    REQUIRE(img->groups.size() == 5); // four singletons, plus the depth

    // R, G and B, plus the depth channel, which was never exploded and carries no mark; a regroup clearing
    // a flag it never set would record setting one on the way back
    img->deselect_all();
    for (const char *name : {"R", "G", "B", "Z"}) img->select_group(group_of_channel(img, name));
    img->selected_group = group_of_channel(img, "R");

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

TEST_CASE("A group command covers every group it is given, and nothing else")
{
    // All three are addressed by target_groups(). What they do differs, but "every target group changes and
    // nothing outside them does" is a property all three owe.
    for (const char *name : {"Ungroup channels", "Regroup channels", "Delete channel group"})
    {
        CAPTURE(name);

        auto img = make_layered_image(); // R,G,B,A -- Z -- normal.R,G,B
        REQUIRE(img->groups.size() == 3);

        TestEditContext ctx{img};

        // two of the three groups selected, so a command reaching only the one on screen and one reaching
        // every group are both caught
        img->selected_group = group_of_channel(img, "R");
        img->select_group(group_of_channel(img, "R"));
        img->select_group(group_of_channel(img, "normal.R"));

        // regrouping has nothing to put back until something has been taken apart; the selection rides on
        // the channels, so it survives the rebuild that renumbers the groups
        if (std::string(name) == "Regroup channels")
        {
            find_command("Ungroup channels")->apply(ctx);
            REQUIRE(img->history.size() == 1);
            REQUIRE(ctx.target_groups().size() == 7);
        }

        auto cmd = find_command(name);
        REQUIRE(cmd);
        REQUIRE(cmd->enabled(ctx));

        const int before = img->history.size();
        cmd->apply(ctx);
        CHECK(img->history.size() == before + 1); // one entry over the lot, not one per group

        // whatever it did, it left the group nobody selected alone
        REQUIRE(channel_index(img, "Z") >= 0);
        CHECK_FALSE(img->channels[size_t(channel_index(img, "Z"))].ungrouped);

        // and it reached both of the ones that were
        if (std::string(name) == "Ungroup channels")
            for (const char *n : {"R", "G", "B", "A", "normal.R", "normal.G", "normal.B"})
                CHECK(img->channels[size_t(channel_index(img, n))].ungrouped);
        else if (std::string(name) == "Regroup channels")
            for (const char *n : {"R", "G", "B", "A", "normal.R", "normal.G", "normal.B"})
                CHECK_FALSE(img->channels[size_t(channel_index(img, n))].ungrouped);
        else
        {
            REQUIRE(img->channels.size() == 1);
            CHECK(img->channels[0].name == "Z");
        }

        // undoing puts back what was taken, and still nothing more
        REQUIRE(img->history.undo(*img));
        CHECK_FALSE(img->channels[size_t(channel_index(img, "Z"))].ungrouped);
    }
}

TEST_CASE("The delete command says whether it is about to take one channel or a group")
{
    // the label follows the group on screen while the action's name stays put, since the name is what the
    // registry, the palette and these tests address it by
    auto img = make_image();
    REQUIRE(delete_channels_label(img, {img->selected_group}) == "Delete channel group");

    TestEditContext ctx{img};
    find_command("Ungroup channels")->apply(ctx);

    // now every group is a lone channel, and deleting one is not deleting a group
    REQUIRE(img->groups[size_t(img->selected_group)].num_channels == 1);
    CHECK(delete_channels_label(img, {img->selected_group}) == "Delete channel");

    // the command is still registered under the one name throughout
    CHECK(find_command("Delete channel group") != nullptr);
    CHECK(find_command("Delete channel") == nullptr);
}

TEST_CASE("A group can be acted on without becoming the one on screen")
{
    // pointing at a group is not selecting it: right-clicking a lone depth channel to delete it leaves the
    // color that was being shown both selected and intact
    auto img = std::make_shared<Image>(k_size, 4);

    static const char *names[] = {"R", "G", "B", "Z"};
    for (int c = 0; c < 4; ++c) img->channels[size_t(c)].name = names[c];
    img->rebuild_layers();

    REQUIRE(img->groups.size() == 2); // the color, and the depth on its own

    // show the color
    int color = -1, depth = -1;
    for (size_t g = 0; g < img->groups.size(); ++g) (img->groups[g].num_channels > 1 ? color : depth) = int(g);
    REQUIRE(color >= 0);
    REQUIRE(depth >= 0);

    // shown and selected, so what follows says the pointed-at group wins over the selection and not merely
    // over what the viewport shows
    img->selected_group = color;
    img->select_group(color);

    TestEditContext ctx{img};

    // what the label says it is about to do differs between the two
    CHECK(delete_channels_label(img, {color}) == "Delete channel group");
    CHECK(delete_channels_label(img, {depth}) == "Delete channel");

    // point at the depth channel without selecting it
    ctx.set_target_group(depth);
    CHECK(ctx.target_groups() == std::vector<int>{depth});
    CHECK(delete_channels_label(img, ctx.target_groups()) == "Delete channel");

    auto del = find_command("Delete channel group");
    REQUIRE(del);
    REQUIRE(del->enabled(ctx));
    del->apply(ctx);

    // the depth channel is gone and the color is untouched, still shown and still selected
    CHECK(img->channels.size() == 3);
    for (const auto &c : img->channels) CHECK(c.name != "Z");

    REQUIRE(img->is_valid_group(img->selected_group));
    CHECK(img->groups[size_t(img->selected_group)].num_channels == 3);
    CHECK(img->is_group_selected(img->selected_group));

    // pointing at a group that is selected covers the selection, as a click inside one keeps it
    ctx.set_target_group(img->selected_group);
    CHECK(ctx.target_groups() == img->selected_groups());
}

TEST_CASE("Ungrouping a group that is not on screen leaves the viewport where it was")
{
    auto img = std::make_shared<Image>(k_size, 6);

    static const char *names[] = {"diffuse.R", "diffuse.G", "diffuse.B", "specular.R", "specular.G", "specular.B"};
    for (int c = 0; c < 6; ++c) img->channels[size_t(c)].name = names[c];
    img->rebuild_layers();
    REQUIRE(img->groups.size() == 2);

    int shown = -1, other = -1;
    for (size_t g = 0; g < img->groups.size(); ++g)
        (Channel::head(img->channels[size_t(img->groups[g].channels[0])].name) == "diffuse." ? shown : other) = int(g);

    img->selected_group         = shown;
    const std::string on_screen = img->channels[size_t(img->groups[size_t(shown)].channels[0])].name;

    TestEditContext ctx{img};
    ctx.set_target_group(other);

    find_command("Ungroup channels")->apply(ctx);

    // specular came apart, diffuse did not, and diffuse is still what is being shown
    CHECK(img->groups.size() == 4);
    REQUIRE(img->is_valid_group(img->selected_group));
    CHECK(img->groups[size_t(img->selected_group)].num_channels == 3);
    CHECK(img->channels[size_t(img->groups[size_t(img->selected_group)].channels[0])].name == on_screen);
}
