//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file test_edit_commands.cpp
    \author Wojciech Jarosz

    The edit commands driven through EditContext rather than through the application.

    Which is what that interface is for: a command is written against it and never against HDRViewApp, so
    everything but the dialog can be exercised without a window, a GL context or a frame loop. These run in
    milliseconds; the same coverage through hdrview_gui_tests costs seconds per case.

    Most of what is here is one sweep over the whole registry rather than a case per command. The
    properties every edit owes -- that it leaves exactly one undo entry, that undoing restores the image
    sample for sample, that it honors the subject it was given -- are the ones worth checking, and checking
    them for all of them at once covers the commands nobody has broken yet.
*/

#include <doctest/doctest.h>

#include "edit/commands.h"
#include "edit/subject.h"
#include "image.h"

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace
{

//! An EditContext over a bare Image, with none of the application behind it.
/*!
    Mirrors what HDRViewApp's chokepoints guarantee -- the undo entry is built before the edit so it sees
    the samples it is about to displace, and the subject decides which channels and which rectangle -- and
    leaves out everything that needs a window: texture binding, the layer tree's visibility counts, the
    worker thread. The async pair runs inline, which is what the web build does anyway.
*/
class TestEditContext : public EditContext
{
public:
    explicit TestEditContext(ImagePtr img) : m_image(std::move(img)) {}

    ImagePtr           image() const override { return m_image; }
    const EditSubject &subject() const override { return m_subject; }
    Box2i              selection() const override { return m_selection; }
    void               set_selection(const Box2i &box) override { m_selection = box; }
    float4             background_color() const override { return m_background; }
    ConstImagePtr      clipboard() const override { return m_clipboard; }
    void               set_clipboard(ImagePtr img) override { m_clipboard = std::move(img); }
    void               draw_subject_selector() override {}

    EditSubject &mutable_subject() { return m_subject; }
    void         set_background(float4 c) { m_background = c; }

    //! Whether the last edit went through a chokepoint that takes the subject at all.
    /*!
        Info::has_subject says only whether the dialog draws the "Apply to" controls. A flip has no dialog
        and no subject either way, and asking it to respect a selection would be asking for something it
        does not offer.
    */
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

        // The reader sees the image as it was, so an op cannot read what it has already written.
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

    bool        m_used_subject = false;
    ImagePtr    m_image;
    ImagePtr    m_clipboard;
    EditSubject m_subject;
    Box2i       m_selection;
    float4      m_background{0.f, 0.f, 0.f, 1.f};
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
                // Kept inside [0,1] and away from the ends, so the edits that clamp still change something.
                img->channels[size_t(c)](x, y) = 0.15f + 0.1f * float(c) + 0.01f * float(y * k_size.x + x);

    img->rebuild_layers();
    return img;
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

        // The first name is the key the action registry, the menu and the tests all address it by.
        REQUIRE_FALSE(info.names.empty());
        CHECK_FALSE(info.names.front().empty());
        CHECK_FALSE(info.icon.empty());
        CHECK(info.width_em > 0.f);

        // "..." means a dialog, everywhere in the interface. A command with no draw() must not claim one,
        // and one that draws must say so in its name.
        const std::string &n        = info.names.front();
        const bool         ellipsis = n.size() >= 3 && n.compare(n.size() - 3, 3, "...") == 0;
        CHECK(cmd->has_dialog() == ellipsis);

        names.push_back(info.names.front());
    }

    // Addressed by name, so two commands sharing one would make the second unreachable.
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
}

TEST_CASE("Every edit that applies leaves exactly one undo entry, and undoing restores the image")
{
    // The property every command owes, checked for all of them at once: whatever an edit does, the history
    // must be able to take it back sample for sample. A command that records two entries, or none, or one
    // that restores only part of what it changed, fails here whichever command it is.
    for (auto &cmd : all_edit_commands())
    {
        const std::string name = first_name(cmd);
        CAPTURE(name);

        auto            img = make_image();
        TestEditContext ctx{img};

        // Something on the clipboard, so the paste commands are exercised rather than skipped.
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
            // A command that declined to do anything must not have changed the image either.
            CHECK(samples(img) == before);
            CHECK(img->size() == size_before);
            continue;
        }

        // Named for the history panel; the name need not be the command's own, since one command can
        // record different edits (a blur names the kind of blur it did).
        CHECK_FALSE(img->history.undo_name().empty());

        const std::vector<float> after      = samples(img);
        const int2               size_after = img->size();

        REQUIRE(img->history.undo(*img));
        CHECK(img->size() == size_before);
        CHECK(samples(img) == before);

        // And redo returns to exactly what the edit produced, so the entry is good in both directions.
        REQUIRE(img->history.redo(*img));
        CHECK(img->size() == size_after);
        CHECK(samples(img) == after);
    }
}

TEST_CASE("An edit covers the subject it was given and nothing else")
{
    // Two scopes and a selection, swept over every command rather than checked for one: an edit that
    // ignores the subject writes outside the rectangle, and an edit that misreads it writes to the wrong
    // channels. Both show up as a sample changing where none should have.
    for (auto &cmd : all_edit_commands())
    {
        const std::string name = first_name(cmd);

        CAPTURE(name);

        auto            img = make_image();
        TestEditContext ctx{img};
        ctx.set_clipboard(make_image());

        // A selection well inside the image, so there is untouched ground on every side of it.
        const Box2i roi{int2{2, 1}, int2{5, 4}};
        ctx.set_selection(roi);
        ctx.mutable_subject().selection_only = true;
        ctx.mutable_subject().scope          = EditSubject::Scope_CurrentGroup;

        if (!cmd->enabled(ctx))
            continue;

        const std::vector<float> before = samples(img);
        const int                steps  = img->history.size();

        cmd->apply(ctx);

        // Nothing to check unless the edit both happened and went through a chokepoint that takes the
        // subject: a flip moves every sample by definition, and a resize has no rectangle to stay inside.
        if (img->history.size() == steps || img->size() != k_size || !ctx.last_edit_used_subject())
            continue;

        for (size_t c = 0; c < img->channels.size(); ++c)
            for (int y = 0; y < k_size.y; ++y)
                for (int x = 0; x < k_size.x; ++x)
                {
                    if (roi.contains(int2{x, y}))
                        continue;

                    CAPTURE(c);
                    CAPTURE(x);
                    CAPTURE(y);
                    CHECK(img->channels[c](x, y) == before[c * size_t(k_size.x * k_size.y) + size_t(y * k_size.x + x)]);
                }
    }
}

TEST_CASE("A command with no image to work on does nothing rather than crashing")
{
    // Every command must tolerate a null image: the menu can be reached with none loaded, and a command
    // invoked from the palette gets whatever the context has.
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
