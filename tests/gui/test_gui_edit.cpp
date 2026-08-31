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

        menu_click(ctx, "Edit/Zap gremlins");

        IM_CHECK_EQ(ch(0, 0), 0.f);
        IM_CHECK_EQ(ch(1, 0), 0.f);
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
        hdrview()->roi() = roi;

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

        hdrview()->roi() = Box2i{};
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
