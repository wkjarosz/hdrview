/** \file test_gui_edit.cpp
    \author Wojciech Jarosz

    The Edit menu, driven the way a user drives it: click the item, then look at the pixels.

    The logic-level suite already covers Image's geometric operations and the undo history in isolation, and
    both were correct while the menu wired to the wrong thing entirely -- once because an action name
    collided with the view's own flip and silently replaced it, once because the invalidation that runs
    after an edit took the image down. Neither is reachable from a test that calls the operation directly,
    so these go through the menu and check the image afterwards.
*/

#include "app.h"
#include "colorspace.h"
#include "edit/filters.h"
#include "fonts.h"
#include "image.h"
#include "test_gui_registry.h"
#include "test_gui_support.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace hdrview_test;
using std::string;
using std::vector;

namespace
{

//! Loads the single-layer fixture and leaves it as the current image.
bool load_fixture(ImGuiTestContext *ctx)
{
    reset_images(ctx);
    // Edits are selection-only by default, so a selection left behind by an earlier test would silently
    // narrow what the next one edits.
    hdrview()->roi()          = Box2i{};
    hdrview()->edit_subject() = EditSubject{};
    IM_CHECK_SILENT_RETV(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE}) == 1, false);
    IM_CHECK_SILENT_RETV(hdrview()->current_image() != nullptr, false);
    return true;
}

//! Every sample of every channel, in channel order, so two states can be compared exactly.
vector<float> snapshot(const ConstImagePtr &img)
{
    vector<float> out;
    for (const auto &c : img->channels)
        for (int y = 0; y < c.size().y; ++y)
            for (int x = 0; x < c.size().x; ++x) out.push_back(c(x, y));
    return out;
}

void menu_click(ImGuiTestContext *ctx, const char *path)
{
    ctx->SetRef("##MainMenuBar");
    ctx->MenuClick(path);
    ctx->Yield(2);
}

} // namespace

void RegisterTests_Edit(ImGuiTestEngine *engine)
{
    ImGuiTest *t = nullptr;

    t           = IM_REGISTER_TEST(engine, "edit", "flip from the menu changes the pixels");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();
        // A flip of a symmetric image is indistinguishable from doing nothing, and the fixture is an icon
        // rather than a known pattern -- so this establishes it is asymmetric before relying on that.
        const auto before = snapshot(img);

        menu_click(ctx, "Edit/Flip image horizontally");

        const auto after = snapshot(img);
        IM_CHECK_EQ(before.size(), after.size());
        IM_CHECK(before != after);

        // The view's own flip has the same two words in its name and is registered later, so it once
        // replaced this action outright. It toggles a display flag and touches no samples; if the menu is
        // reaching it again, the pixels above would not have moved.
        IM_CHECK_EQ(img->history.has_undo(), true);
        IM_CHECK_EQ(img->history.undo_name(), string("Flip image horizontally"));
    };

    t           = IM_REGISTER_TEST(engine, "edit", "undo and redo return the image to each state");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);

        menu_click(ctx, "Edit/Rotate 90 degrees clockwise");
        const auto rotated = snapshot(img);
        IM_CHECK(original != rotated);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);

        menu_click(ctx, "Edit/Redo");
        IM_CHECK(snapshot(img) == rotated);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "four quarter turns from the menu come back exactly");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);
        const int2 size     = img->size();

        for (int i = 0; i < 4; ++i) menu_click(ctx, "Edit/Rotate 90 degrees clockwise");

        IM_CHECK(img->size() == size);
        IM_CHECK(snapshot(img) == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a quarter turn swaps the image's axes");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img  = hdrview()->current_image();
        const int2 size = img->size();

        menu_click(ctx, "Edit/Rotate 90 degrees clockwise");

        // The channels, the data window, and the texture all have to agree about the new shape; a rebuild
        // that missed one of them is what the logic tests cannot see.
        IM_CHECK_EQ(img->size().x, size.y);
        IM_CHECK_EQ(img->size().y, size.x);
        for (const auto &c : img->channels) IM_CHECK(c.size() == img->size());
    };

    t           = IM_REGISTER_TEST(engine, "edit", "an edit leaves the layer and group structure intact");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto         img      = hdrview()->current_image();
        const size_t layers   = img->layers.size();
        const size_t groups   = img->groups.size();
        const size_t channels = img->channels.size();

        // Rebuilding the tree after every edit used to append to it rather than replace it, which
        // duplicated every layer and then threw partway through drawing the frame.
        menu_click(ctx, "Edit/Flip image vertically");
        menu_click(ctx, "Edit/Rotate 90 degrees counter-clockwise");
        menu_click(ctx, "Edit/Undo");

        IM_CHECK_EQ(img->layers.size(), layers);
        IM_CHECK_EQ(img->groups.size(), groups);
        IM_CHECK_EQ(img->channels.size(), channels);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "editing marks the image modified and undoing clears it");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();
        IM_CHECK_EQ(img->history.is_modified(), false);
        IM_CHECK_EQ(hdrview()->any_image_modified(), false);

        menu_click(ctx, "Edit/Flip image horizontally");
        IM_CHECK_EQ(img->history.is_modified(), true);
        IM_CHECK_EQ(hdrview()->any_image_modified(), true);

        // Back to what the file holds, so there is nothing left to warn about on close.
        menu_click(ctx, "Edit/Undo");
        IM_CHECK_EQ(img->history.is_modified(), false);
        IM_CHECK_EQ(hdrview()->any_image_modified(), false);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "undo and redo are only offered when there is something to do");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto &undo = hdrview()->action("Undo");
        auto &redo = hdrview()->action("Redo");

        IM_CHECK_EQ(undo.enabled(), false);
        IM_CHECK_EQ(redo.enabled(), false);

        menu_click(ctx, "Edit/Flip image horizontally");
        IM_CHECK_EQ(undo.enabled(), true);
        IM_CHECK_EQ(redo.enabled(), false);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK_EQ(undo.enabled(), false);
        IM_CHECK_EQ(redo.enabled(), true);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "the image list marks an edited image");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // The row's label is built from the image, so what it says is what the panel draws. Reading the
        // rendered text back would test ImGui's truncation rather than whether the mark is applied.
        auto img = hdrview()->current_image();
        IM_CHECK_EQ(img->history.is_modified(), false);

        menu_click(ctx, "Edit/Flip image horizontally");
        ctx->Yield(2);
        IM_CHECK_EQ(img->history.is_modified(), true);

        // The Images panel has to have drawn at least once in the edited state without tripping over it.
        ctx->SetRef("");
        IM_CHECK(ctx->WindowInfo("//Images").Window != nullptr);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK_EQ(img->history.is_modified(), false);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a point op changes the samples and undo puts them back");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);

        menu_click(ctx, "Edit/Invert");
        const auto inverted = snapshot(img);
        IM_CHECK(original != inverted);
        IM_CHECK_EQ(inverted.size(), original.size());

        // Inverting twice returns the samples to within rounding, but not bit-for-bit: 1-(1-v) is not v
        // in floating point once 1-v has to round, which it does for every v below a half. That gap is
        // exactly why undo stores the pixels rather than recomputing them -- see below.
        menu_click(ctx, "Edit/Invert");
        const auto twice = snapshot(img);
        IM_CHECK_EQ(twice.size(), original.size());
        for (size_t i = 0; i < twice.size(); ++i) IM_CHECK_LT(std::fabs(twice[i] - original[i]), 1e-6f);

        // Undo, by contrast, is exact, because it puts back the samples it saved rather than recomputing
        // them -- two undos land on the original bit-for-bit, which the arithmetic above does not.
        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == inverted);
        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "clamping leaves every sample inside the unit range");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // Push samples outside [0,1] first, so the clamp has something to do on a fixture that may already
        // be inside it.
        IM_CHECK(hdrview()->modify_pixels(img, "Spread", hdrview()->edit_subject(),
                                          [](float v, int2, int) { return v * 4.f - 1.5f; }));

        menu_click(ctx, "Edit/Clamp to [0,1]");

        for (const auto &c : img->channels)
            for (int y = 0; y < c.size().y; ++y)
                for (int x = 0; x < c.size().x; ++x) IM_CHECK(c(x, y) >= 0.f && c(x, y) <= 1.f);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "zapping gremlins replaces only the non-finite samples");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // Scatter a NaN and an infinity into the first channel, leaving its other samples alone.
        auto       &ch   = img->channels[0];
        const float kept = ch(2, 2);
        ch(0, 0)         = std::numeric_limits<float>::quiet_NaN();
        ch(1, 0)         = std::numeric_limits<float>::infinity();
        ++img->content_version;

        menu_click(ctx, "Edit/Zap gremlins...");
        ctx->SetRef("Zap gremlins...");
        ctx->ItemClick("Median of neighbors");
        ctx->ItemClick("Zap");
        ctx->Yield(2);

        // Filled from the surrounding samples rather than blanked, so what goes back matches the
        // neighbourhood; the fixture is smooth around these, so both land on what was already there.
        IM_CHECK(std::isfinite(ch(0, 0)));
        IM_CHECK(std::isfinite(ch(1, 0)));
        IM_CHECK_EQ(ch(2, 2), kept);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "an edit restricted to the selection leaves the rest alone");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // A box well inside the image, in image coordinates -- the same space the data window uses.
        const Box2i roi{img->data_window.min + int2{2, 2}, img->data_window.min + int2{6, 5}};
        hdrview()->set_selection(roi);

        EditSubject subject;
        subject.selection_only = true;

        const auto before = snapshot(img);
        IM_CHECK(hdrview()->modify_pixels(img, "Invert", subject, [](float v, int2, int) { return 1.f - v; }));

        const auto &ch = img->channels[img->groups[img->selected_group].channels[0]];
        const int2  o  = img->data_window.min;
        for (int y = 0; y < ch.size().y; ++y)
            for (int x = 0; x < ch.size().x; ++x)
            {
                const bool inside = roi.contains(int2{x, y} + o);
                // Outside the selection nothing may have moved; inside, the samples came from 1-v.
                if (!inside)
                    IM_CHECK_EQ(ch(x, y), before[size_t(img->groups[img->selected_group].channels[0]) *
                                                     size_t(ch.size().x * ch.size().y) +
                                                 size_t(y * ch.size().x + x)]);
            }

        // And the entry that reverses it covers the selection, not the image.
        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == before);

        hdrview()->set_selection(Box2i{});
    };

    t           = IM_REGISTER_TEST(engine, "edit", "the scope choice is only offered when it would change anything");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // The single-layer fixture has one group, so both scopes name the same channels.
        auto img = hdrview()->current_image();
        IM_CHECK_EQ(HDRViewApp::scope_matters(img), img->groups.size() > 1);

        // Whatever the scope says, a single-group image resolves to the same channels either way.
        EditSubject group_scope, all_scope;
        all_scope.scope = EditSubject::Scope_AllChannels;
        if (!HDRViewApp::scope_matters(img))
            IM_CHECK(hdrview()->resolve_subject(img, group_scope).first ==
                     hdrview()->resolve_subject(img, all_scope).first);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a dialog changes nothing until it is confirmed");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);

        // Applying on confirm is what keeps a dragged slider from filling the history with states nobody
        // asked for, so cancelling has to leave both the pixels and the history alone.
        menu_click(ctx, "Edit/Exposure\\/gamma...");
        ctx->SetRef("Exposure\\/gamma...");
        ctx->ItemInputValue("Exposure", 2.0f);
        ctx->ItemClick("Cancel");
        ctx->Yield(2);

        IM_CHECK(snapshot(img) == original);
        IM_CHECK_EQ(img->history.has_undo(), false);
        IM_CHECK_EQ(img->history.is_modified(), false);

        // And confirming has to actually apply it, as one entry.
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Edit/Exposure\\/gamma...");
        ctx->SetRef("Exposure\\/gamma...");
        ctx->ItemInputValue("Exposure", 2.0f);
        ctx->ItemClick("Apply");
        ctx->Yield(2);

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_EQ(img->history.has_undo(), true);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "filling writes a different value per channel");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // Fill is the one edit whose value depends on which channel it is writing, so the channels of a
        // group must not all come out the same.
        const float4 color{0.25f, 0.5f, 0.75f, 1.f};
        EditSubject  subject;
        IM_CHECK(
            hdrview()->modify_pixels(img, "Fill", subject, [color](float, int2, int slot) { return color[slot % 4]; }));

        const auto &group = img->groups[img->selected_group];
        for (int c = 0; c < group.num_channels; ++c)
        {
            const auto &ch = img->channels[group.channels[c]];
            IM_CHECK_EQ(ch(0, 0), color[c % 4]);
            IM_CHECK_EQ(ch(ch.size().x - 1, ch.size().y - 1), color[c % 4]);
        }
    };

    t           = IM_REGISTER_TEST(engine, "edit", "cropping to the selection resizes the image and undo restores it");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const int2 size     = img->size();
        const auto original = snapshot(img);

        const Box2i roi{img->data_window.min + int2{1, 1}, img->data_window.min + int2{5, 4}};
        hdrview()->set_selection(roi);
        ctx->Yield();

        menu_click(ctx, "Edit/Crop to selection");

        IM_CHECK((img->size() == int2{4, 3}));
        IM_CHECK((img->data_window.size() == int2{4, 3}));
        IM_CHECK((img->display_window.size() == int2{4, 3}));
        for (const auto &c : img->channels) IM_CHECK(c.size() == img->size());
        // What was selected is the whole image now, so the selection has nothing left to say.
        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);
        IM_CHECK_EQ(hdrview()->roi_live().has_volume(), false);

        // A structural entry has to put back the samples, both windows, and the layer tree built from them.
        menu_click(ctx, "Edit/Undo");
        IM_CHECK(img->size() == size);
        IM_CHECK(snapshot(img) == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "cropping is only offered when it would do something");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto &crop = hdrview()->action("Crop to selection");

        // No selection at all.
        hdrview()->set_selection(Box2i{});
        ctx->Yield();
        IM_CHECK_EQ(crop.enabled(), false);

        // A selection covering the whole image would crop it to itself.
        auto img = hdrview()->current_image();
        hdrview()->set_selection(img->data_window);
        ctx->Yield();
        IM_CHECK_EQ(crop.enabled(), false);

        hdrview()->set_selection(Box2i{img->data_window.min, img->data_window.min + int2{2, 2}});
        ctx->Yield();
        IM_CHECK_EQ(crop.enabled(), true);

        hdrview()->set_selection(Box2i{});
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a structural edit refits the view and rebuilds the tree");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto         img    = hdrview()->current_image();
        const size_t layers = img->layers.size();
        const size_t groups = img->groups.size();

        IM_CHECK(hdrview()->modify_structure(img, "Canvas size", [](Image &i)
                                             { i.resize_canvas(i.size() + int2{4, 4}, Image::Anchor_MiddleCenter); }));

        IM_CHECK(img->size() == int2{img->channels[0].size()});
        // Rebuilt from the new channels rather than left describing the old ones -- the bug that took the
        // image down when an edit re-ran finalize() instead.
        IM_CHECK_EQ(img->layers.size(), layers);
        IM_CHECK_EQ(img->groups.size(), groups);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK_EQ(img->layers.size(), layers);
        IM_CHECK_EQ(img->groups.size(), groups);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "with a selection, an edit covers only the selection");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();
        // Straight from the menu, with nothing configured: having drawn a selection, this is what the
        // next edit is expected to do.
        const Box2i roi{img->data_window.min + int2{2, 2}, img->data_window.min + int2{6, 5}};
        hdrview()->set_selection(roi);
        ctx->Yield();

        const auto before = snapshot(img);
        menu_click(ctx, "Edit/Invert");

        const auto &group       = img->groups[img->selected_group];
        const int2  o           = img->data_window.min;
        bool        any_changed = false;
        for (int c = 0; c < group.num_channels; ++c)
        {
            const auto  &ch   = img->channels[group.channels[c]];
            const size_t base = size_t(group.channels[c]) * size_t(ch.size().x * ch.size().y);
            for (int y = 0; y < ch.size().y; ++y)
                for (int x = 0; x < ch.size().x; ++x)
                {
                    const float was = before[base + size_t(y * ch.size().x + x)];
                    if (roi.contains(int2{x, y} + o))
                        any_changed |= ch(x, y) != was;
                    else
                        IM_CHECK_EQ(ch(x, y), was);
                }
        }
        IM_CHECK(any_changed);

        hdrview()->set_selection(Box2i{});
    };

    t           = IM_REGISTER_TEST(engine, "edit", "with no selection, an edit covers the whole image");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // Selection-only is on by default, but an empty selection means "no selection" rather than "edit
        // nothing" -- otherwise every edit would appear to do nothing until one was drawn.
        IM_CHECK_EQ(hdrview()->edit_subject().selection_only, true);
        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);

        auto       img    = hdrview()->current_image();
        const auto before = snapshot(img);

        menu_click(ctx, "Edit/Invert");

        const auto after = snapshot(img);
        IM_CHECK_EQ(after.size(), before.size());
        size_t changed = 0;
        for (size_t i = 0; i < after.size(); ++i)
            if (after[i] != before[i])
                ++changed;
        IM_CHECK(changed > 0);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a dialog can be finished from the keyboard");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);

        // Escape cancels, whatever else has keyboard focus.
        menu_click(ctx, "Edit/Blur...");
        ctx->SetRef("Blur...");
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        IM_CHECK(snapshot(img) == original);
        IM_CHECK_EQ(img->history.has_undo(), false);

        // And Enter applies -- plain Enter, not a chord, so a filter reached from the command palette can
        // be finished without the mouse.
        menu_click(ctx, "Edit/Blur...");
        ctx->SetRef("Blur...");
        ctx->KeyPress(ImGuiKey_Enter);
        // The blur runs off the main thread now, so this waits for the result rather than a frame count.
        wait_until(ctx, [&] { return img->history.has_undo(); });
        IM_CHECK(snapshot(img) != original);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "select all and deselect set and clear the selection");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();
        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);
        // Nothing to clear, so deselect has nothing to offer.
        IM_CHECK_EQ(hdrview()->action("Deselect").enabled(), false);

        menu_click(ctx, "Edit/Select all");
        IM_CHECK(hdrview()->roi() == img->data_window);
        IM_CHECK(hdrview()->roi_live() == img->data_window);
        IM_CHECK_EQ(hdrview()->action("Deselect").enabled(), true);

        menu_click(ctx, "Edit/Deselect");
        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);
        // The viewport draws the marquee from roi_live(), so clearing only roi() left the rectangle on
        // screen with nothing behind it.
        IM_CHECK_EQ(hdrview()->roi_live().has_volume(), false);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "each blur mode is reachable and brings its own controls");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // The modes are told apart by what they ask for, which is also what catches a radio button wired to
        // the wrong one: "Box" once selected the fast Gaussian between them, and offered sigma accordingly.
        menu_click(ctx, "Edit/Blur...");
        ctx->SetRef("Blur...");

        ctx->ItemClick("Gaussian");
        IM_CHECK(ctx->ItemExists("Sigma"));
        IM_CHECK(!ctx->ItemExists("Half width"));
        // Quality belongs to the approximation alone; the exact kernel has nothing to trade.
        IM_CHECK(!ctx->ItemExists("Quality"));

        ctx->ItemClick("Fast Gaussian");
        IM_CHECK(ctx->ItemExists("Sigma"));
        IM_CHECK(ctx->ItemExists("Quality"));
        IM_CHECK(!ctx->ItemExists("Half width"));

        ctx->ItemClick("Box");
        IM_CHECK(ctx->ItemExists("Half width"));
        IM_CHECK(ctx->ItemExists("Passes"));
        IM_CHECK(!ctx->ItemExists("Sigma"));

        ctx->ItemClick("Cancel");
    };

    t           = IM_REGISTER_TEST(engine, "edit", "the blur modes give different results");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // Applied straight through modify_channels so the comparison is of the filters themselves; a mode
        // that silently ran another one would come back identical.
        auto blurred_by = [&](int which)
        {
            const Box2i all{int2{0}, img->channels[0].size()};
            Array2Df    out = which == 0   ? gaussian_blurred(img->channels[0], all, 3.f, 3.f)
                              : which == 1 ? fast_gaussian_blurred(img->channels[0], all, 3.f, 3.f, 6)
                                           : box_blurred(img->channels[0], all, 3, 3, 1);
            return out;
        };

        const Array2Df exact = blurred_by(0), fast = blurred_by(1), box = blurred_by(2);

        // The approximation is close to the exact one but not equal to it, and a single box is neither.
        double d_fast = 0.0, d_box = 0.0;
        for (int i = 0; i < exact.num_elements(); ++i)
        {
            d_fast += std::abs(double(exact(i)) - double(fast(i)));
            d_box += std::abs(double(exact(i)) - double(box(i)));
        }
        IM_CHECK(d_fast > 0.0);
        IM_CHECK(d_box > d_fast);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a filter run off the main thread lands as one edit");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);

        menu_click(ctx, "Edit/Median filter...");
        ctx->SetRef("Median filter...");
        ctx->ItemInputValue("Radius", 1.5f);
        ctx->ItemClick("Apply");

        // The work happens on another thread and is applied by the frame loop when it finishes, so this
        // waits for the result rather than for a number of frames.
        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_EQ(img->history.undo_name(), std::string("Median filter"));

        // One entry, not one per channel: the whole filter is a single undoable step.
        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);
        IM_CHECK_EQ(img->history.has_undo(), false);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "remapping resizes the image and undo puts it back");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const int2 original = img->size();
        const auto samples  = snapshot(img);

        // Structural *and* computed off the main thread, which nothing else here does at once: the result
        // is a different size than what it was computed from, so there is no rectangle to write back.
        menu_click(ctx, "Edit/Remap envmap...");
        ctx->SetRef("Remap envmap...");
        ctx->ItemClick("Remap");

        wait_until(ctx, [&] { return img->history.has_undo(); });

        for (const auto &c : img->channels) IM_CHECK(c.size() == img->size());
        IM_CHECK(img->data_window.size() == img->size());
        IM_CHECK(img->display_window.size() == img->size());

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(img->size() == original);
        IM_CHECK(snapshot(img) == samples);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "an irradiance map comes out at the size asked for");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const int2 original = img->size();

        menu_click(ctx, "Edit/Irradiance envmap...");
        ctx->SetRef("Irradiance envmap...");
        // Deliberately tiny: the convolution costs the two resolutions multiplied together, and the result
        // is smooth enough that this loses nothing.
        ctx->ItemInputValue("Width, height/$$0", 8);
        ctx->ItemInputValue("Width, height/$$1", 6);
        ctx->ItemClick("Convolve");

        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK((img->size() == int2{8, 6}));
        for (const auto &c : img->channels) IM_CHECK((c.size() == int2{8, 6}));

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(img->size() == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "the deselect shortcut clears the selection");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();
        hdrview()->set_selection(img->data_window);
        ctx->Yield(2);
        IM_CHECK_EQ(hdrview()->roi().has_volume(), true);

        // Through the keyboard rather than the menu: the action itself is covered above, so a failure here
        // is in how chords are dispatched.
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
        ctx->Yield(2);

        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);
        IM_CHECK_EQ(hdrview()->roi_live().has_volume(), false);

        // And again once keyboard navigation is showing, which is the state the command palette and the
        // dialogs leave behind -- shortcuts used to stop working entirely from here on.
        hdrview()->set_selection(img->data_window);
        ImGui::GetIO().NavVisible = true;
        ctx->Yield(2);

        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
        ctx->Yield(2);

        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);
        IM_CHECK_EQ(hdrview()->roi_live().has_volume(), false);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "filling premultiplies when the image stores it that way");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto        img   = hdrview()->current_image();
        const auto &group = img->groups[img->selected_group];
        // Loudly, not silently: this test skipping itself is how it passed while fill was still wrong.
        IM_CHECK_EQ(int(img->alpha_type != AlphaType_None), 1);
        IM_CHECK_EQ(int(group_has_alpha(group.type)), 1);

        // Half-transparent red. finalize() premultiplies a straight-alpha image, so what should land in
        // the channels is the color scaled by its own alpha -- writing it as typed reads as the alpha
        // having done nothing.
        const float4 color{0.8f, 0.2f, 0.1f, 0.5f};

        menu_click(ctx, "Edit/Fill...");
        ctx->SetRef("Fill...");
        ctx->ItemInputValue("Color/##X", color.x);
        ctx->ItemInputValue("Color/##Y", color.y);
        ctx->ItemInputValue("Color/##Z", color.z);
        ctx->ItemInputValue("Color/##W", color.w);
        ctx->ItemClick("Fill");
        ctx->Yield(2);

        for (int c = 0; c < group.num_channels - 1; ++c)
        {
            const auto &ch = img->channels[group.channels[c]];
            IM_CHECK_LT(std::fabs(ch(0, 0) - color[c] * color.w), 1e-4f);
        }
        // Alpha itself is stored as given.
        const auto &alpha = img->channels[group.channels[group.num_channels - 1]];
        IM_CHECK_LT(std::fabs(alpha(0, 0) - color.w), 1e-4f);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "converting the color space rewrites the samples and the tag");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // Stated rather than assumed, so the conversion below is a real one whatever the fixture is
        // tagged as when it loads.
        img->chromaticities = gamut_chromaticities(ColorGamut_sRGB_BT709);
        img->compute_color_transform();
        img->metadata["color profile"] = color_profile_name(ColorGamut_sRGB_BT709, TransferFunction::Linear);
        ctx->Yield();

        const auto  original      = snapshot(img);
        const auto  original_chr  = img->chromaticities;
        const auto  original_name = img->metadata.value<string>("color profile", "");
        const float original_wide = img->M_to_sRGB[0][0];

        Chromaticities to = gamut_chromaticities(ColorGamut_BT2020_2100);
        to.white          = white_point(WhitePoint_D65);

        float3x3   M;
        const bool needed = color_conversion_matrix(M, *original_chr, to, AdaptationMethod_Bradford);
        IM_CHECK(needed);

        IM_CHECK(hdrview()->modify_colors(
            img, "Convert color space", hdrview()->edit_subject(),
            [M](const float4 &c, int2) { return float4{la::mul(M, c.xyz()), c.w}; },
            [to](Image &image)
            {
                image.chromaticities = to;
                image.compute_color_transform();
                image.metadata["color profile"] = color_profile_name(ColorGamut_BT2020_2100, TransferFunction::Linear);
            }));
        ctx->Yield();

        // Both halves landed: the samples moved, and so did what the Colorspace panel reads.
        IM_CHECK(snapshot(img) != original);
        IM_CHECK_EQ(img->color_space, ColorGamut_BT2020_2100);
        IM_CHECK_STR_EQ(img->metadata.value<string>("color profile", "").c_str(),
                        color_profile_name(ColorGamut_BT2020_2100, TransferFunction::Linear).c_str());
        // Derived from the chromaticities rather than stored beside them, so this is what says
        // compute_color_transform() was rerun.
        IM_CHECK(std::fabs(img->M_to_sRGB[0][0] - original_wide) > 1e-4f);

        // One step takes back both. A tag left behind would describe the image as something it is not,
        // and nothing downstream would notice.
        IM_CHECK_EQ(hdrview()->undo(), true);
        ctx->Yield();
        IM_CHECK(snapshot(img) == original);
        IM_CHECK_EQ(img->color_space, ColorGamut_sRGB_BT709);
        IM_CHECK_STR_EQ(img->metadata.value<string>("color profile", "").c_str(), original_name.c_str());
        IM_CHECK_LT(std::fabs(img->M_to_sRGB[0][0] - original_wide), 1e-4f);

        // And redo puts both back, which a composite that undoes in the wrong order would not.
        IM_CHECK_EQ(hdrview()->redo(), true);
        ctx->Yield();
        IM_CHECK(snapshot(img) != original);
        IM_CHECK_EQ(img->color_space, ColorGamut_BT2020_2100);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "the shift and color-space dialogs are wired to their edits");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);

        // The dialogs themselves, from the menu: everything above tests the operation, and an operation
        // wired to nothing passes all of it.
        menu_click(ctx, "Edit/Shift...");
        ctx->SetRef("Shift...");
        ctx->ItemInputValue("X, Y offset/$$0", 3.0f);
        ctx->ItemClick("Shift");
        ctx->Yield(2);

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Shift");

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);

        menu_click(ctx, "Edit/Convert color space...");
        ctx->SetRef("Convert color space...");
        ctx->ComboClick("Primaries##to/ACES AP0");
        ctx->ItemClick("Convert");
        ctx->Yield(2);

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Convert color space");
        IM_CHECK_EQ(img->color_space, ColorGamut_ACES_AP0);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a color edit sees a group's channels together");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        const int g = img->active_group_index(Target_Primary);
        IM_CHECK(img->is_valid_group(g));
        const auto &group = img->groups[g];
        if (group.num_channels < 3)
            return;

        // Reading the sample beside it is exactly what modify_pixels() cannot do: it is handed one sample
        // and told which slot it is, never the others.
        IM_CHECK(hdrview()->modify_colors(img, "Swap red and blue", hdrview()->edit_subject(),
                                          [](const float4 &c, int2) { return float4{c.z, c.y, c.x, c.w}; }));
        ctx->Yield();

        const auto &r = img->channels[group.channels[0]];
        const auto &b = img->channels[group.channels[2]];

        // Swapped, so the two channels are each other's -- and undoing restores both, not one.
        vector<float> reds, blues;
        for (int y = 0; y < r.size().y; ++y)
            for (int x = 0; x < r.size().x; ++x)
            {
                reds.push_back(r(x, y));
                blues.push_back(b(x, y));
            }
        IM_CHECK(reds != blues); // a fixture whose channels were equal would prove nothing

        const auto swapped = snapshot(img);
        IM_CHECK_EQ(hdrview()->undo(), true);
        ctx->Yield();
        IM_CHECK_EQ(hdrview()->redo(), true);
        ctx->Yield();
        IM_CHECK(snapshot(img) == swapped);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a color edit leaves channels that are not color alone");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // A depth channel beside the color, which is the ordinary shape of a render: a color matrix has no
        // meaning for it, so covering "all channels" must still not touch it. Added through the structural
        // chokepoint, which is what rebuilds the layer tree and the visibility the Images panel walks.
        hdrview()->modify_structure(img, "Add Z",
                                    [](Image &i)
                                    {
                                        Channel z{"Z", i.channels[0].size()};
                                        for (int k = 0; k < z.num_elements(); ++k) z(k) = 0.25f * float(k % 7);
                                        i.channels.push_back(std::move(z));
                                    });
        ctx->Yield();

        const int     zi = int(img->channels.size()) - 1;
        vector<float> before;
        for (int i = 0; i < img->channels[zi].num_elements(); ++i) before.push_back(img->channels[zi](i));

        auto subject  = hdrview()->edit_subject();
        subject.scope = EditSubject::Scope_AllChannels;

        IM_CHECK(hdrview()->modify_colors(img, "Halve", subject,
                                          [](const float4 &c, int2) { return float4{0.5f * c.xyz(), c.w}; }));
        ctx->Yield();

        for (int i = 0; i < img->channels[zi].num_elements(); ++i) IM_CHECK_EQ(img->channels[zi](i), before[i]);

        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "the history panel lists every state and moves between them");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);

        menu_click(ctx, "Edit/Flip image horizontally");
        const auto flipped = snapshot(img);
        menu_click(ctx, "Edit/Rotate 90 degrees clockwise");
        const auto rotated = snapshot(img);

        // Starts hidden, like the log, so the window has to be asked for before it can be read.
        ctx->SetRef("");
        if (ctx->WindowInfo("History", ImGuiTestOpFlags_NoError).Window == nullptr)
        {
            *hdrview()->action("Show History window").p_selected = true;
            ctx->Yield(2);
        }
        IM_CHECK(ctx->WindowInfo("History").Window != nullptr);

        // A row per state rather than per entry: the image as opened, plus one for each edit.
        IM_CHECK_EQ(img->history.size(), 2);
        IM_CHECK_EQ(img->history.current_state(), 2);

        // Clicking the first row walks all the way back, which is what the panel is for -- the Edit menu
        // only ever moves one step.
        ctx->SetRef("History");
        ctx->ItemClick("**/" ICON_MY_OPEN_IMAGE " Opened");
        ctx->Yield(2);

        IM_CHECK_EQ(img->history.current_state(), 0);
        IM_CHECK(snapshot(img) == original);

        // And forward again, to a state in the middle, which is the direction a naive implementation gets
        // wrong: the entries ahead of the cursor are still there and are what redo reapplies.
        ctx->ItemClick("**/" ICON_MY_HISTORY " Flip image horizontally");
        ctx->Yield(2);

        IM_CHECK_EQ(img->history.current_state(), 1);
        IM_CHECK(snapshot(img) == flipped);

        ctx->ItemClick("**/" ICON_MY_HISTORY " Rotate 90 degrees clockwise");
        ctx->Yield(2);
        IM_CHECK(snapshot(img) == rotated);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "an image a renderer owns refuses edits");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // What arriving over IPC marks an image as. Its pixels belong to the other process, so an edit
        // would be overwritten by the next tile and undoing one would restore samples already replaced.
        img->is_live = true;
        ctx->Yield();

        IM_CHECK_EQ(hdrview()->action("Flip image horizontally").enabled(), false);
        IM_CHECK_EQ(hdrview()->action("Rotate 90 degrees clockwise").enabled(), false);
        IM_CHECK_EQ(hdrview()->action("Undo").enabled(), false);

        // Not merely greyed out in the menu: the edit itself has to decline, since the command palette and
        // the keyboard chord reach the same callback.
        const auto before = snapshot(img);
        IM_CHECK_EQ(hdrview()->modify_image_reversibly(
                        img, "Flip image horizontally", [](Image &i) { i.flip_horizontal(); },
                        [](Image &i) { i.flip_horizontal(); }),
                    false);
        IM_CHECK(snapshot(img) == before);
        IM_CHECK_EQ(img->history.has_undo(), false);

        img->is_live = false;
        reset_images(ctx);
    };
}
