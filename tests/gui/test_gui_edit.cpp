/** \file test_gui_edit.cpp
    \author Wojciech Jarosz

    The Edit menu, driven the way a user drives it: click the item, then look at the pixels. The
    logic-level suite covers Image's operations and the undo history in isolation; what these add is the
    wiring between the menu and them, and the invalidation that runs after an edit.
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

/// The context the application builds for \p img, with \p subject standing in for the current one.
EditContext edit_context(const ImagePtr &img, const EditSubject &subject)
{
    auto ctx    = hdrview()->edit_context(img);
    ctx.subject = subject;
    return ctx;
}

/// Loads the single-layer fixture and leaves it as the current image.
bool load_fixture(ImGuiTestContext *ctx)
{
    reset_images(ctx);
    // edits are selection-only by default, so a selection left by an earlier test would narrow this one
    hdrview()->roi()          = Box2i{};
    hdrview()->edit_subject() = EditSubject{};
    IM_CHECK_SILENT_RETV(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE}) == 1, false);
    IM_CHECK_SILENT_RETV(hdrview()->current_image() != nullptr, false);
    return true;
}

/// Every sample of every channel, in channel order, so two states can be compared.
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

    t           = IM_REGISTER_TEST(engine, "edit", "an edit covers every selected image");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        reset_images(ctx);
        hdrview()->roi()          = Box2i{};
        hdrview()->edit_subject() = EditSubject{};
        IM_CHECK_SILENT(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2}) == 2);

        // selected here, not clicked: the chords that build a selection are navigation's business
        hdrview()->set_current_image_index(0);
        hdrview()->toggle_group_selected(1, hdrview()->image(1)->selected_group);
        IM_CHECK(hdrview()->image(0)->is_selected());
        IM_CHECK(hdrview()->image(1)->is_selected());
        IM_CHECK_EQ(hdrview()->current_image_index(), 0);

        // an edit that takes a subject reaches both, each as an entry in its own history
        menu_click(ctx, "Edit/Invert");
        for (int i = 0; i < 2; ++i)
        {
            IM_CHECK_EQ((int)hdrview()->image(i)->history.size(), 1);
            IM_CHECK_STR_EQ(hdrview()->image(i)->history.undo_name().c_str(), "Invert");
        }

        // undo and redo step every selected image, not only the one being looked at
        menu_click(ctx, "Edit/Undo");
        for (int i = 0; i < 2; ++i) IM_CHECK_EQ(hdrview()->image(i)->history.has_undo(), false);
        menu_click(ctx, "Edit/Redo");
        for (int i = 0; i < 2; ++i) IM_CHECK_EQ(hdrview()->image(i)->history.has_undo(), true);

        // an edit that reshapes an image reaches both too
        menu_click(ctx, "Edit/Rotate 90 degrees clockwise");
        for (int i = 0; i < 2; ++i) IM_CHECK_EQ((int)hdrview()->image(i)->history.size(), 2);

        // cutting and copying are the exception: there is one clipboard, so both act on the image being
        // looked at and leave the rest of the selection alone
        menu_click(ctx, "Edit/Select all");
        menu_click(ctx, "Edit/Cut");
        IM_CHECK_EQ((int)hdrview()->image(0)->history.size(), 3);
        IM_CHECK_EQ((int)hdrview()->image(1)->history.size(), 2);
        IM_CHECK(hdrview()->clipboard() != nullptr);
        IM_CHECK_EQ(hdrview()->clipboard()->size().x, hdrview()->image(0)->size().x);
        IM_CHECK_EQ(hdrview()->clipboard()->size().y, hdrview()->image(0)->size().y);

        // edited images left loaded would make the next test's close_all_images() prompt
        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a color edit that covers nothing says so");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // ungrouping leaves every channel standing alone, and a lone channel is not a color group, so the
        // scope now names groups a color operation has nothing to do with
        menu_click(ctx, "Edit/Ungroup channels");
        auto img = hdrview()->current_image();
        IM_CHECK_EQ((int)img->groups.size(), 4);

        const auto original = snapshot(img);
        const int  entries  = (int)img->history.size();

        LogWatcher  log;
        EditSubject all_channels;
        all_channels.scope = EditSubject::Scope_AllChannels;
        IM_CHECK_EQ(
            modify_colors(edit_context(img, all_channels), "Test", [](const float4 &c, int2, int) { return -c; }),
            false);

        IM_CHECK(snapshot(img) == original);
        IM_CHECK_EQ((int)img->history.size(), entries);
        IM_CHECK(log.warnings() > 0);

        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a filter over a selection reaches every image in turn");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        reset_images(ctx);
        hdrview()->roi()          = Box2i{};
        hdrview()->edit_subject() = EditSubject{};
        IM_CHECK_SILENT(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2}) == 2);

        hdrview()->set_current_image_index(0);
        hdrview()->toggle_group_selected(1, hdrview()->image(1)->selected_group);

        // only one filter runs at a time, there being one progress bar and one Cancel, so the second
        // image's blur waits for the first and starts as it lands
        menu_click(ctx, "Edit/Blur...");
        ctx->SetRef("Blur...");
        ctx->KeyPress(ImGuiKey_Enter);
        wait_until(ctx,
                   [] { return hdrview()->image(0)->history.has_undo() && hdrview()->image(1)->history.has_undo(); });

        for (int i = 0; i < 2; ++i) IM_CHECK_EQ((int)hdrview()->image(i)->history.size(), 1);

        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "flip from the menu changes the pixels");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();
        // a flip of a symmetric image is indistinguishable from doing nothing, and the fixture is an icon,
        // so establish it is asymmetric first
        const auto before = snapshot(img);

        menu_click(ctx, "Edit/Flip image horizontally");

        const auto after = snapshot(img);
        IM_CHECK_EQ(before.size(), after.size());
        IM_CHECK(before != after);

        // the view's own flip has the same two words in its name and is registered later, so it can replace
        // this action outright; it toggles a display flag and touches no samples
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

        // the channels, the data window and the texture all have to agree about the new shape
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

        // rebuilding the tree after every edit has to replace it, not append to it
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

        // back to what the file holds, so there is nothing left to warn about on close
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

        // the row's label is built from the image, so what it says is what the panel draws; reading the
        // rendered text back would test ImGui's truncation
        auto img = hdrview()->current_image();
        IM_CHECK_EQ(img->history.is_modified(), false);

        menu_click(ctx, "Edit/Flip image horizontally");
        ctx->Yield(2);
        IM_CHECK_EQ(img->history.is_modified(), true);

        // the Images panel has to have drawn at least once in the edited state without tripping over it
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

        // 1-(1-v) is not exact in float, so compare with a tolerance
        menu_click(ctx, "Edit/Invert");
        const auto twice = snapshot(img);
        IM_CHECK_EQ(twice.size(), original.size());
        for (size_t i = 0; i < twice.size(); ++i) IM_CHECK_LT(std::fabs(twice[i] - original[i]), 1e-6f);

        // undo puts back the samples it saved, so two undos land on the original bit-for-bit
        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == inverted);
        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "an edit restricted to the selection leaves the rest alone");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // a box well inside the image, in image coordinates, the same space the data window uses
        const Box2i roi{img->data_window.min + int2{2, 2}, img->data_window.min + int2{6, 5}};
        hdrview()->set_selection(roi);

        EditSubject subject;
        subject.selection_only = true;

        const auto before = snapshot(img);
        IM_CHECK(modify_pixels(edit_context(img, subject), "Invert", [](float v, int2, int) { return 1.f - v; }));

        const auto &ch = img->channels[img->groups[img->selected_group].channels[0]];
        const int2  o  = img->data_window.min;
        for (int y = 0; y < ch.size().y; ++y)
            for (int x = 0; x < ch.size().x; ++x)
            {
                const bool inside = roi.contains(int2{x, y} + o);
                // outside the selection nothing may have moved; inside, the samples came from 1-v
                if (!inside)
                    IM_CHECK_EQ(ch(x, y), before[size_t(img->groups[img->selected_group].channels[0]) *
                                                     size_t(ch.size().x * ch.size().y) +
                                                 size_t(y * ch.size().x + x)]);
            }

        // the entry that reverses it covers the selection, not the image
        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == before);

        hdrview()->set_selection(Box2i{});
    };

    t           = IM_REGISTER_TEST(engine, "edit", "the scope choice is only offered when it would change anything");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // the single-layer fixture has one group, so both scopes name the same channels
        auto img = hdrview()->current_image();
        IM_CHECK_EQ(HDRViewApp::scope_matters(img), img->groups.size() > 1);

        // whatever the scope says, a single-group image resolves to the same channels either way
        EditSubject group_scope, all_scope;
        all_scope.scope = EditSubject::Scope_AllChannels;
        if (!HDRViewApp::scope_matters(img))
            IM_CHECK(resolve_subject(img, group_scope, hdrview()->roi()).first ==
                     resolve_subject(img, all_scope, hdrview()->roi()).first);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a dialog changes nothing until it is confirmed");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);

        // applying on confirm keeps a dragged slider from filling the history, so cancelling has to leave
        // both the pixels and the history alone
        menu_click(ctx, "Edit/Exposure\\/gamma...");
        ctx->SetRef("Exposure\\/gamma...");
        ctx->ItemInputValue("Exposure", 2.0f);
        ctx->ItemClick("Cancel");
        ctx->Yield(2);

        IM_CHECK(snapshot(img) == original);
        IM_CHECK_EQ(img->history.has_undo(), false);
        IM_CHECK_EQ(img->history.is_modified(), false);

        // confirming applies it, as one entry
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

        // fill is the one edit whose value depends on which channel it writes, so a group's channels must
        // not all come out the same
        const float4 color{0.25f, 0.5f, 0.75f, 1.f};
        EditSubject  subject;
        IM_CHECK(modify_pixels(edit_context(img, subject), "Fill",
                               [color](float, int2, int slot) { return color[slot % 4]; }));

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
        // what was selected is the whole image now, so the selection has nothing left to say
        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);
        IM_CHECK_EQ(hdrview()->roi_live().has_volume(), false);

        // a structural entry puts back the samples, both windows, and the layer tree built from them
        menu_click(ctx, "Edit/Undo");
        IM_CHECK(img->size() == size);
        IM_CHECK(snapshot(img) == original);

        // with several images selected it crops all of them to the same rectangle: cropping consumes the
        // selection, so each invocation has to start from the same one
        reset_images(ctx);
        hdrview()->roi()          = Box2i{};
        hdrview()->edit_subject() = EditSubject{};
        IM_CHECK_SILENT(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2}) == 2);
        hdrview()->set_current_image_index(0);
        hdrview()->toggle_group_selected(1, hdrview()->image(1)->selected_group);

        hdrview()->set_selection(Box2i{int2{1, 1}, int2{5, 4}});
        ctx->Yield();
        menu_click(ctx, "Edit/Crop to selection");

        for (int i = 0; i < 2; ++i)
        {
            IM_CHECK((hdrview()->image(i)->size() == int2{4, 3}));
            IM_CHECK_EQ((int)hdrview()->image(i)->history.size(), 1);
        }
        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);

        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "cropping is only offered when it would do something");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto &crop = hdrview()->action("Crop to selection");

        // no selection at all
        hdrview()->set_selection(Box2i{});
        ctx->Yield();
        IM_CHECK_EQ(crop.enabled(), false);

        // a selection covering the whole image would crop it to itself
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

        IM_CHECK(modify_image(
            hdrview()->edit_context(img), "Canvas size", [](Image &i)
            { i.resize_canvas(i.size() + int2{4, 4}, Image::Anchor_MiddleCenter); }, structure_undo, Extent_Structure));

        IM_CHECK(img->size() == int2{img->channels[0].size()});
        // rebuilt from the new channels, not left describing the old ones
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
        // straight from the menu, with nothing configured: what a drawn selection makes the next edit do
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

        // selection-only is on by default, but an empty selection means "no selection", not "edit nothing"
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

        // escape cancels, whatever else has keyboard focus
        menu_click(ctx, "Edit/Blur...");
        ctx->SetRef("Blur...");
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        IM_CHECK(snapshot(img) == original);
        IM_CHECK_EQ(img->history.has_undo(), false);

        // Enter applies: plain Enter, not a chord, so a filter reached from the command palette can be
        // finished without the mouse
        menu_click(ctx, "Edit/Blur...");
        ctx->SetRef("Blur...");
        ctx->KeyPress(ImGuiKey_Enter);
        // the blur runs off the main thread, so wait for the result
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
        // nothing to clear, so deselect has nothing to offer
        IM_CHECK_EQ(hdrview()->action("Deselect").enabled(), false);

        menu_click(ctx, "Edit/Select all");
        IM_CHECK(hdrview()->roi() == img->data_window);
        IM_CHECK(hdrview()->roi_live() == img->data_window);
        IM_CHECK_EQ(hdrview()->action("Deselect").enabled(), true);

        menu_click(ctx, "Edit/Deselect");
        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);
        // the viewport draws the marquee from roi_live(), so clearing only roi() leaves the rectangle on
        // screen with nothing behind it
        IM_CHECK_EQ(hdrview()->roi_live().has_volume(), false);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "each blur mode is reachable and brings its own controls");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // the modes are told apart by what they ask for, which also catches a radio button wired to the
        // wrong one
        menu_click(ctx, "Edit/Blur...");
        ctx->SetRef("Blur...");

        ctx->ItemClick("Gaussian");
        IM_CHECK(ctx->ItemExists("Sigma"));
        IM_CHECK(!ctx->ItemExists("Half width"));
        // quality belongs to the approximation alone; the exact kernel has nothing to trade
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

        // the filters called directly, so the comparison is of the filters themselves and not of the plumbing
        auto blurred_by = [&](int which)
        {
            const Box2i all{int2{0}, img->channels[0].size()};
            Array2Df    out = which == 0   ? gaussian_blurred(img->channels[0], all, 3.f, 3.f)
                              : which == 1 ? fast_gaussian_blurred(img->channels[0], all, 3.f, 3.f, 6)
                                           : box_blurred(img->channels[0], all, 3, 3, 1);
            return out;
        };

        const Array2Df exact = blurred_by(0), fast = blurred_by(1), box = blurred_by(2);

        // the approximation is close to the exact one but not equal to it, and a single box is neither
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

        // the work happens on another thread and is applied by the frame loop when it finishes
        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_EQ(img->history.undo_name(), std::string("Median filter"));

        // one entry, not one per channel: the whole filter is a single undoable step
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

        // structural and computed off the main thread at once: the result is a different size than what it
        // was computed from, so there is no rectangle to write back
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
        // tiny: the convolution costs the two resolutions multiplied together, and the result is smooth
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

        // through the keyboard: the action itself is covered above, so a failure here is in the dispatch
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
        ctx->Yield(2);

        IM_CHECK_EQ(hdrview()->roi().has_volume(), false);
        IM_CHECK_EQ(hdrview()->roi_live().has_volume(), false);

        // and again once keyboard navigation is showing, the state the command palette and the dialogs
        // leave behind
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
        // loudly, not silently: a test that skips itself here would pass while fill was wrong
        IM_CHECK_EQ(int(img->alpha_type != AlphaType_None), 1);
        IM_CHECK_EQ(int(group_has_alpha(group.type)), 1);

        // half-transparent red: finalize() premultiplies a straight-alpha image, so what lands in the
        // channels is the color scaled by its own alpha
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
        // alpha itself is stored as given
        const auto &alpha = img->channels[group.channels[group.num_channels - 1]];
        IM_CHECK_LT(std::fabs(alpha(0, 0) - color.w), 1e-4f);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "converting the color space rewrites the samples and the tag");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // stated, so the conversion below is a real one whatever the fixture is tagged as when it loads
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

        IM_CHECK(modify_colors(
            hdrview()->edit_context(img), "Convert color space",
            [M](const float4 &c, int2, int) { return float4{la::mul(M, c.xyz()), c.w}; },
            [to](Image &image)
            {
                image.chromaticities = to;
                image.compute_color_transform();
                image.metadata["color profile"] = color_profile_name(ColorGamut_BT2020_2100, TransferFunction::Linear);
            }));
        ctx->Yield();

        // both halves landed: the samples moved, and so did what the Colorspace panel reads
        IM_CHECK(snapshot(img) != original);
        IM_CHECK_EQ(img->color_space, ColorGamut_BT2020_2100);
        IM_CHECK_STR_EQ(img->metadata.value<string>("color profile", "").c_str(),
                        color_profile_name(ColorGamut_BT2020_2100, TransferFunction::Linear).c_str());
        // derived from the chromaticities, not stored beside them, so this says compute_color_transform()
        // was rerun
        IM_CHECK(std::fabs(img->M_to_sRGB[0][0] - original_wide) > 1e-4f);

        // one step takes back both: a tag left behind would describe the image as something it is not
        IM_CHECK_EQ(hdrview()->undo(), true);
        ctx->Yield();
        IM_CHECK(snapshot(img) == original);
        IM_CHECK_EQ(img->color_space, ColorGamut_sRGB_BT709);
        IM_CHECK_STR_EQ(img->metadata.value<string>("color profile", "").c_str(), original_name.c_str());
        IM_CHECK_LT(std::fabs(img->M_to_sRGB[0][0] - original_wide), 1e-4f);

        // redo puts both back, which a composite that undoes in the wrong order would not
        IM_CHECK_EQ(hdrview()->redo(), true);
        ctx->Yield();
        IM_CHECK(snapshot(img) != original);
        IM_CHECK_EQ(img->color_space, ColorGamut_BT2020_2100);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "every new edit dialog is wired to its edit");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const auto original = snapshot(img);

        // the dialogs themselves, from the menu: everything above tests the operation, and an operation
        // wired to nothing passes all of it. Each waits for the entry, since a dialog may hand the work to
        // a worker rather than do it in the frame that confirmed it.
        menu_click(ctx, "Edit/Shift...");
        ctx->SetRef("Shift...");
        ctx->ItemInputValue("X, Y offset/$$0", 3.0f);
        ctx->ItemClick("Shift");
        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Shift");

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);

        menu_click(ctx, "Edit/Convert color space...");
        ctx->SetRef("Convert color space...");
        ctx->ComboClick("Primaries##to/ACES AP0");
        ctx->ItemClick("Convert");
        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Convert color space");
        IM_CHECK_EQ(img->color_space, ColorGamut_ACES_AP0);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);

        // both of the group-scoped ones, which reach the pixels through modify_colors() and so are wired
        // differently again
        menu_click(ctx, "Edit/Channel mixer...");
        ctx->SetRef("Channel mixer...");
        ctx->ItemClick("Monochrome");
        ctx->ItemClick("Mix");
        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Channel mixer");

        // monochrome means the three channels came out equal, which says the mix ran
        {
            const auto &group = img->groups[img->selected_group];
            if (group.num_channels >= 3)
            {
                const auto &r = img->channels[group.channels[0]];
                const auto &g = img->channels[group.channels[1]];
                const auto &b = img->channels[group.channels[2]];
                for (int i = 0; i < r.num_elements(); i += 37)
                {
                    IM_CHECK_LT(std::fabs(r(i) - g(i)), 1e-5f);
                    IM_CHECK_LT(std::fabs(r(i) - b(i)), 1e-5f);
                }
            }
        }

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);

        menu_click(ctx, "Edit/Hue\\/saturation...");
        ctx->SetRef("Hue\\/saturation...");
        ctx->ItemInputValue("Saturation", -100.0f);
        ctx->ItemClick("Apply");
        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Hue/saturation");

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);

        menu_click(ctx, "Edit/Flatten...");
        ctx->SetRef("Flatten...");
        ctx->ItemClick("Flatten");
        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Flatten");

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);

        menu_click(ctx, "Edit/Bump to normal map...");
        ctx->SetRef("Bump to normal map...");
        ctx->ItemClick("Convert");
        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK(snapshot(img) != original);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Bump to normal map");

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == original);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "brightness/contrast can move lightness and color separately");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // a color with all three channels different, so an edit touching one quality of it can be told
        // from one touching everything
        IM_CHECK(modify_pixels(hdrview()->edit_context(img), "Fill",
                               [](float, int2, int slot)
                               {
                                   const float v[4] = {0.6f, 0.3f, 0.15f, 1.f};
                                   return v[slot < 4 ? slot : 0];
                               }));
        ctx->Yield();

        const int4   ch    = img->groups[img->selected_group].channels;
        const float3 white = img->chromaticities ? XYZ_from_xy(img->chromaticities->white) : Lab_reference_white();

        // measured in L*a*b*, which is what the modes are stated in: one moves L* and leaves a* and b*, the
        // other does the reverse. In RGB this would say something else, since changing L* alone does move
        // the RGB ratios.
        auto sample_lab = [&]
        {
            const float3 rgb{img->channels[ch[0]](int2{4, 4}), img->channels[ch[1]](int2{4, 4}),
                             img->channels[ch[2]](int2{4, 4})};
            return XYZ_to_Lab(mul(img->M_RGB_to_XYZ, rgb), white);
        };

        const float3 before = sample_lab();

        struct Mode
        {
            const char *button;
            bool        moves_lightness;
            bool        moves_color;
        };
        const Mode modes[] = {
            {"RGB", true, true},          // the three channels alike, which shifts both
            {"Lightness", true, false},   // L* only
            {"Chromaticity", false, true} // a* and b* only
        };

        for (const Mode &mode : modes)
            for (bool linear : {true, false})
            {
                ctx->LogInfo("--- %s, %s curve ---", mode.button, linear ? "straight" : "s");

                menu_click(ctx, "Edit/Brightness\\/contrast...");

                // escaped, as the menu path is: the slash in the name is a path separator to the engine, so
                // an unescaped ref looks for "contrast..." inside a window called "Brightness"
                ctx->SetRef("Brightness\\/contrast...");

                // the plot is drawn above these and must not swallow them
                ctx->ItemInputValue("Brightness", 0.4f);
                ctx->ItemInputValue("Contrast", 0.2f);
                ctx->ItemClick(mode.button);

                if (linear)
                    ctx->ItemCheck("Linear");
                else
                    ctx->ItemUncheck("Linear");

                ctx->ItemClick("Apply");
                ctx->Yield(2);

                IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Brightness/contrast");

                const float3 after = sample_lab();

                const float d_lightness = std::fabs(after.x - before.x);
                const float d_color     = std::max(std::fabs(after.y - before.y), std::fabs(after.z - before.z));
                ctx->LogInfo("dL* %f  d(a*,b*) %f", d_lightness, d_color);

                // well above what the trip out to RGB and back costs, and well below what an edit does
                const float noise = 0.05f;

                if (mode.moves_lightness)
                    IM_CHECK_GT(d_lightness, noise);
                else
                    IM_CHECK_LT(d_lightness, noise);

                if (mode.moves_color)
                    IM_CHECK_GT(d_color, noise);
                else
                    IM_CHECK_LT(d_color, noise);

                menu_click(ctx, "Edit/Undo");
                ctx->Yield();
                IM_CHECK_LT(std::fabs(sample_lab().x - before.x), 1e-3f);
            }

        reset_images(ctx);
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

        // reading the sample beside it is what modify_pixels() cannot do: it is handed one sample and told
        // which slot it is, never the others
        IM_CHECK(modify_colors(hdrview()->edit_context(img), "Swap red and blue",
                               [](const float4 &c, int2, int) { return float4{c.z, c.y, c.x, c.w}; }));
        ctx->Yield();

        const auto &r = img->channels[group.channels[0]];
        const auto &b = img->channels[group.channels[2]];

        // swapped, so the two channels are each other's, and undoing restores both, not one
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

        // a depth channel beside the color, the ordinary shape of a render: a color matrix has no meaning
        // for it, so covering "all channels" must still not touch it. Added through modify_image() with a
        // structural extent, which rebuilds the layer tree and the visibility the Images panel walks.
        modify_image(
            hdrview()->edit_context(img), "Add Z",
            [](Image &i)
            {
                Channel z{"Z", i.channels[0].size()};
                for (int k = 0; k < z.num_elements(); ++k) z(k) = 0.25f * float(k % 7);
                i.channels.push_back(std::move(z));
            },
            structure_undo, Extent_Structure);
        ctx->Yield();

        const int     zi = int(img->channels.size()) - 1;
        vector<float> before;
        for (int i = 0; i < img->channels[zi].num_elements(); ++i) before.push_back(img->channels[zi](i));

        auto subject  = hdrview()->edit_subject();
        subject.scope = EditSubject::Scope_AllChannels;

        IM_CHECK(modify_colors(edit_context(img, subject), "Halve",
                               [](const float4 &c, int2, int) { return float4{0.5f * c.xyz(), c.w}; }));
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

        // starts hidden, like the log, so the window has to be asked for before it can be read
        ctx->SetRef("");
        if (ctx->WindowInfo("History", ImGuiTestOpFlags_NoError).Window == nullptr)
        {
            *hdrview()->action("Show History window").p_selected = true;
            ctx->Yield(2);
        }
        IM_CHECK(ctx->WindowInfo("History").Window != nullptr);

        // a row per state, not per entry: the image as opened, plus one for each edit
        IM_CHECK_EQ(img->history.size(), 2);
        IM_CHECK_EQ(img->history.current_state(), 2);

        // clicking the first row walks all the way back, where the Edit menu moves one step
        ctx->SetRef("History");
        ctx->ItemClick("**/" ICON_MY_OPEN_IMAGE " Opened");
        ctx->Yield(2);

        IM_CHECK_EQ(img->history.current_state(), 0);
        IM_CHECK(snapshot(img) == original);

        // and forward again, to a state in the middle: the entries ahead of the cursor are still there and
        // are what redo reapplies
        ctx->ItemClick("**/" ICON_MY_HISTORY " Flip image horizontally");
        ctx->Yield(2);

        IM_CHECK_EQ(img->history.current_state(), 1);
        IM_CHECK(snapshot(img) == flipped);

        ctx->ItemClick("**/" ICON_MY_HISTORY " Rotate 90 degrees clockwise");
        ctx->Yield(2);
        IM_CHECK(snapshot(img) == rotated);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "flattening composites over a background and leaves it opaque");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        const int g = img->active_group_index(Target_Primary);
        IM_CHECK(img->is_valid_group(g));
        if (img->groups[g].num_channels < 4)
            return; // nothing to flatten in a group with no alpha

        // a known state to composite: half-transparent mid gray, premultiplied the way the image model
        // holds every RGBA group
        const float4 fg{0.25f, 0.25f, 0.25f, 0.5f};
        IM_CHECK(
            modify_pixels(hdrview()->edit_context(img), "Fill", [fg](float, int2, int slot) { return fg[slot % 4]; }));
        ctx->Yield();

        const float4 bg{0.5f, 0.f, 0.f, 1.f};
        IM_CHECK(modify_colors(hdrview()->edit_context(img), "Flatten", [bg](const float4 &c, int2, int)
                               { return float4{c.xyz() + bg.xyz() * bg.w * (1.f - c.w), c.w + bg.w * (1.f - c.w)}; }));
        ctx->Yield();

        const auto &group = img->groups[img->active_group_index(Target_Primary)];
        const auto &r     = img->channels[group.channels[0]];
        const auto &gch   = img->channels[group.channels[1]];
        const auto &a     = img->channels[group.channels[group.num_channels - 1]];

        // opaque afterwards, and the background has shown through by the fraction that was missing
        IM_CHECK_LT(std::fabs(a(0, 0) - 1.f), 1e-5f);
        IM_CHECK_LT(std::fabs(r(0, 0) - (0.25f + 0.5f * 0.5f)), 1e-5f);
        IM_CHECK_LT(std::fabs(gch(0, 0) - 0.25f), 1e-5f);

        // an opaque background makes it idempotent: there is nothing left for a second pass to show
        // through, which a lerp written against straight alpha would get wrong
        const auto once = snapshot(img);
        IM_CHECK(modify_colors(hdrview()->edit_context(img), "Flatten", [bg](const float4 &c, int2, int)
                               { return float4{c.xyz() + bg.xyz() * bg.w * (1.f - c.w), c.w + bg.w * (1.f - c.w)}; }));
        ctx->Yield();
        IM_CHECK(snapshot(img) == once);

        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a normal map leans away from the slope it was given");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        const auto &group = img->groups[img->active_group_index(Target_Primary)];
        if (group.num_channels < 3)
            return;

        const int2 size = img->size();

        // a ramp rising to the right, so the answer is known: flat down the image, sloped across it
        IM_CHECK(modify_pixels(hdrview()->edit_context(img), "Ramp", [size](float, int2 p, int slot)
                               { return slot >= 3 ? 1.f : float(p.x) / float(size.x); }));
        ctx->Yield();

        // the heights come from a copy, as the command's own do
        const float2 fsize{float(size.x), float(size.y)};
        auto         src = img->duplicate();
        const auto   grp = img->groups;
        IM_CHECK(modify_colors(hdrview()->edit_context(img), "Bump to normal map",
                               [fsize, src, grp](const float4 &, int2 p, int gi)
                               {
                                   auto height = [&](int2 q)
                                   {
                                       const int4 channels = grp[size_t(gi)].channels;
                                       const int2 extent   = src->channels[size_t(channels[0])].size();
                                       const int  x =
                                           wrap_coord(q.x - src->data_window.min.x, extent.x, BorderMode_Edge);
                                       const int y =
                                           wrap_coord(q.y - src->data_window.min.y, extent.y, BorderMode_Edge);
                                       float sum = 0.f;
                                       for (int k = 0; k < 3; ++k) sum += src->channels[size_t(channels[k])](x, y);
                                       return sum / 3.f;
                                   };
                                   const float h00 = height(p);
                                   const float dx = height(p + int2{1, 0}) - h00, dy = height(p + int2{0, 1}) - h00;
                                   float3      n = la::normalize(float3{dx * fsize.x, dy * fsize.y, 1.f});
                                   return float4{n * 0.5f + 0.5f, 1.f};
                               }));
        ctx->Yield();

        const auto &r = img->channels[group.channels[0]];
        const auto &g = img->channels[group.channels[1]];
        const auto &b = img->channels[group.channels[2]];

        // away from the edges, where the border mode flattens the last column's forward difference
        const int2 mid{size.x / 2, size.y / 2};

        // rising to the right leans the normal that way, so red is above the 0.5 that means flat...
        IM_CHECK_GT(r(mid.x, mid.y), 0.55f);
        // ...green stays at flat, since nothing changes down the image...
        IM_CHECK_LT(std::fabs(g(mid.x, mid.y) - 0.5f), 1e-3f);
        // ...and z still points out of the surface, so blue stays in the upper half
        IM_CHECK_GT(b(mid.x, mid.y), 0.5f);

        // encoded, so every component is inside the range a normal map is stored in
        for (int i = 0; i < r.num_elements(); i += 53)
        {
            IM_CHECK(r(i) >= 0.f && r(i) <= 1.f);
            IM_CHECK(g(i) >= 0.f && g(i) <= 1.f);
            IM_CHECK(b(i) >= 0.f && b(i) <= 1.f);
        }

        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "duplicating an image copies its samples, not a reference");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       original = hdrview()->current_image();
        const auto before   = snapshot(original);
        const int  count    = hdrview()->num_images();
        const int  index    = hdrview()->current_image_index();

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("File/Duplicate image");
        ctx->Yield(2);

        IM_CHECK_EQ(hdrview()->num_images(), count + 1);

        // beside the one it was made from, and selected
        IM_CHECK_EQ(hdrview()->current_image_index(), index + 1);
        auto copy = hdrview()->current_image();
        IM_CHECK(copy != nullptr);
        IM_CHECK(copy != original);

        // the same picture...
        IM_CHECK(copy->size() == original->size());
        IM_CHECK(snapshot(copy) == before);

        // ...and its own copy of it: a shallow copy would pass everything above and fail here
        IM_CHECK(modify_pixels(hdrview()->edit_context(copy), "Invert", [](float v, int2, int) { return 1.f - v; }));
        ctx->Yield();
        IM_CHECK(snapshot(copy) != before);
        IM_CHECK(snapshot(original) == before);

        // histories are its own too: the copy has one edit to undo and the original has none
        IM_CHECK_EQ(copy->history.has_undo(), true);
        IM_CHECK_EQ(original->history.has_undo(), false);

        // nothing on disk holds the copy, so closing it has something to warn about
        IM_CHECK_EQ(original->history.is_modified(), false);

        // what the samples mean travels with them; a copy read in different primaries is a different picture
        IM_CHECK_EQ(copy->color_space, original->color_space);
        IM_CHECK_EQ(copy->alpha_type, original->alpha_type);
        IM_CHECK_EQ(copy->groups.size(), original->groups.size());

        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "duplicating with a selection lifts out just the selection");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       original = hdrview()->current_image();
        const int2 size     = original->size();

        const Box2i box{int2{size.x / 4, size.y / 4}, int2{size.x / 2, size.y / 2}};
        hdrview()->set_selection(box);
        ctx->Yield();

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("File/Duplicate image");
        ctx->Yield(2);

        auto copy = hdrview()->current_image();
        IM_CHECK(copy != original);

        // the size of what was selected, not of what it was selected from
        IM_CHECK(copy->size() == box.size());

        // and holding those samples: the corner of the copy is the corner of the selection
        const auto &co = copy->channels[0];
        const auto &og = original->channels[0];
        for (int i = 0; i < 5; ++i)
            IM_CHECK_EQ(co(i, i),
                        og(box.min.x + i - original->data_window.min.x, box.min.y + i - original->data_window.min.y));

        // the selection belonged to the image it was taken from, and the copy is all of itself
        hdrview()->set_selection(Box2i{});
        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "copy and paste carry samples from one place to another");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img  = hdrview()->current_image();
        const int2 size = img->size();

        // two known, different halves, so what lands where is unambiguous
        IM_CHECK(modify_pixels(hdrview()->edit_context(img), "Fill", [size](float, int2 p, int slot)
                               { return slot == 3 ? 1.f : (p.x < size.x / 2 ? 1.f : 0.f); }));
        ctx->Yield();

        const auto &ch = img->channels[img->groups[img->selected_group].channels[0]];
        IM_CHECK_EQ(ch(1, 1), 1.f);          // left half
        IM_CHECK_EQ(ch(size.x - 2, 1), 0.f); // right half

        // copy a piece of the left half...
        const Box2i src_box{int2{0, 0}, int2{size.x / 4, size.y / 4}};
        hdrview()->set_selection(src_box);
        ctx->Yield();
        menu_click(ctx, "Edit/Copy");

        IM_CHECK(hdrview()->clipboard() != nullptr);
        IM_CHECK(hdrview()->clipboard()->size() == src_box.size());

        // ...and paste it into the right half, which was zero
        const Box2i dst_box{int2{size.x / 2, size.y / 2}, int2{size.x / 2 + size.x / 4, size.y / 2 + size.y / 4}};
        hdrview()->set_selection(dst_box);
        ctx->Yield();

        const auto before = snapshot(img);
        menu_click(ctx, "Edit/Paste");

        IM_CHECK(snapshot(img) != before);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Paste");

        // what was pasted is what was copied, at its new place
        IM_CHECK_EQ(ch(dst_box.min.x + 1, dst_box.min.y + 1), 1.f);
        // and nothing outside the selection moved
        IM_CHECK_EQ(ch(size.x - 2, 1), 0.f);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == before);

        hdrview()->set_selection(Box2i{});
        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "cutting takes the samples away and undo brings them back");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img  = hdrview()->current_image();
        const int2 size = img->size();

        const Box2i box{int2{0, 0}, int2{size.x / 4, size.y / 4}};
        hdrview()->set_selection(box);
        ctx->Yield();

        const auto  before = snapshot(img);
        const auto &ch     = img->channels[img->groups[img->selected_group].channels[0]];
        const float kept   = ch(size.x - 2, size.y - 2); // outside the selection

        menu_click(ctx, "Edit/Cut");

        // on the clipboard...
        IM_CHECK(hdrview()->clipboard() != nullptr);
        IM_CHECK(hdrview()->clipboard()->size() == box.size());

        // ...gone from the image, everywhere inside the selection and nowhere outside it
        for (int y = 0; y < box.size().y; ++y)
            for (int x = 0; x < box.size().x; x += 7) IM_CHECK_EQ(ch(x, y), 0.f);
        IM_CHECK_EQ(ch(size.x - 2, size.y - 2), kept);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(snapshot(img) == before);

        hdrview()->set_selection(Box2i{});
        reset_images(ctx);
    };

    t = IM_REGISTER_TEST(engine, "edit", "paste waits for a clipboard, and copy does not need an editable image");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // nothing has been copied in this session yet, so there is nothing to paste
        hdrview()->set_clipboard(nullptr);
        ctx->Yield();
        IM_CHECK_EQ(hdrview()->action("Paste").enabled(), false);

        auto img = hdrview()->current_image();

        // an image a renderer owns refuses every edit, but reading one is not editing it
        img->is_live = true;
        ctx->Yield();

        IM_CHECK_EQ(hdrview()->action("Cut").enabled(), false);
        IM_CHECK_EQ(hdrview()->action("Copy").enabled(), true);

        menu_click(ctx, "Edit/Copy");
        IM_CHECK(hdrview()->clipboard() != nullptr);

        // pasting into it is still refused, since that would be editing it
        IM_CHECK_EQ(hdrview()->action("Paste").enabled(), false);

        img->is_live = false;
        hdrview()->set_clipboard(nullptr);
        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "the canvas size dialog means the same size either way round");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        const int2 original = img->size();

        // percent first, while the dialog is still in the absolute, in-pixels state it was constructed in,
        // so nothing has to be read back to find out which way it is set
        menu_click(ctx, "Edit/Canvas size...");
        ctx->SetRef("Canvas size...");
        ctx->ComboClick("Units/Pixels");
        ctx->Yield();
        IM_CHECK_EQ(ctx->ItemReadAsInt("##width"), original.x); // absolute, as a fresh dialog is

        // a negative change trims, and percent has its own path back to pixels, which has to clamp to what
        // a relative size may go down to and not to one sample
        ctx->ItemClick("Relative");
        ctx->ComboClick("Units/Percent");
        ctx->Yield();
        ctx->ItemInputValue("##width", -25.0f);
        ctx->ItemInputValue("##height", -25.0f);
        ctx->ItemClick("Resize");
        ctx->Yield(2);

        IM_CHECK_EQ(img->size().x, original.x - original.x / 4);
        IM_CHECK_EQ(img->size().y, original.y - original.y / 4);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(img->size() == original);

        menu_click(ctx, "Edit/Canvas size...");
        ctx->SetRef("Canvas size...");
        ctx->ComboClick("Units/Pixels");
        ctx->Yield();
        ctx->ItemClick("Relative"); // back to absolute, which the rest of this is about

        // an absolute size, wider and shorter than the image
        const int2 wanted{original.x + 100, original.y - 40};
        ctx->ItemInputValue("##width", wanted.x);
        ctx->ItemInputValue("##height", wanted.y);
        ctx->Yield();

        // switching to relative describes the same canvas, as a change instead of a size
        ctx->ItemClick("Relative");
        ctx->Yield(2);
        IM_CHECK_EQ(ctx->ItemReadAsInt("##width"), 100);
        IM_CHECK_EQ(ctx->ItemReadAsInt("##height"), -40);

        // ...including that it may be negative, which an absolute size may not
        ctx->ItemClick("Relative");
        ctx->Yield(2);
        IM_CHECK_EQ(ctx->ItemReadAsInt("##width"), wanted.x);
        IM_CHECK_EQ(ctx->ItemReadAsInt("##height"), wanted.y);

        // and what it produces is that size, whichever way it was expressed
        ctx->ItemClick("Relative");
        ctx->Yield();
        ctx->ItemClick("Resize");
        ctx->Yield(2);

        IM_CHECK(img->size() == wanted);

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(img->size() == original);

        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "a seamless paste leaves no step at the border");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        const int2 size = img->size();

        // small rectangles: the solve is iterative, and in a debug build a quarter of a megapixel is a long
        // wait for something a few thousand samples show as well
        const int2 patch{32, 32};

        // a flat background, so any step at the border is the paste's doing and not the picture's
        IM_CHECK(modify_pixels(hdrview()->edit_context(img), "Fill",
                               [](float, int2, int slot) { return slot == 3 ? 1.f : 0.25f; }));
        ctx->Yield();

        // copy a corner, which is 0.25 throughout...
        const Box2i src_box{int2{0, 0}, patch};
        hdrview()->set_selection(src_box);
        ctx->Yield();
        menu_click(ctx, "Edit/Copy");
        IM_CHECK(hdrview()->clipboard() != nullptr);

        // ...then make the background around the destination a different level: an ordinary paste would
        // leave a visible step where 0.25 meets 0.8, and a seamless one cannot
        hdrview()->set_selection(Box2i{});
        ctx->Yield();
        IM_CHECK(modify_pixels(hdrview()->edit_context(img), "Fill",
                               [](float, int2, int slot) { return slot == 3 ? 1.f : 0.8f; }));
        ctx->Yield();

        const Box2i dst_box{int2{size.x / 2, size.y / 2}, int2{size.x / 2, size.y / 2} + patch};
        hdrview()->set_selection(dst_box);
        ctx->Yield();

        const int steps_before = img->history.size();

        menu_click(ctx, "Edit/Seamless paste...");
        ctx->SetRef("Seamless paste...");
        ctx->ItemInputValue("Iterations", 200);
        ctx->ItemClick("Paste");

        // it runs off the main thread behind a progress dialog and lands once the main thread has drained
        // it, so wait for the history to grow: the fills above already made it non-empty
        for (int i = 0; i < 2000 && img->history.size() == steps_before; ++i) ctx->Yield();
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Seamless paste");

        const auto &ch = img->channels[img->groups[img->selected_group].channels[0]];

        // the whole border of the pasted region still holds the background it was pinned to, every edge of
        // it: a misplaced paste is seamless along whichever side it touches and steps along the other three
        float worst_border = 0.f;
        int2  worst_at{-1, -1};
        for (int x = dst_box.min.x; x < dst_box.max.x; ++x)
            for (int y : {dst_box.min.y, dst_box.max.y - 1})
                if (const float e = std::fabs(ch(x, y) - 0.8f); e > worst_border)
                {
                    worst_border = e;
                    worst_at     = int2{x, y};
                }
        for (int y = dst_box.min.y; y < dst_box.max.y; ++y)
            for (int x : {dst_box.min.x, dst_box.max.x - 1})
                if (const float e = std::fabs(ch(x, y) - 0.8f); e > worst_border)
                {
                    worst_border = e;
                    worst_at     = int2{x, y};
                }
        ctx->LogInfo("worst border error %f at %d,%d (box %d,%d..%d,%d)", worst_border, worst_at.x, worst_at.y,
                     dst_box.min.x, dst_box.min.y, dst_box.max.x, dst_box.max.y);
        IM_CHECK_LT(worst_border, 1e-3f);

        // ...and the interior was carried to that level instead of arriving at its own 0.25, where an
        // ordinary paste would have written 0.25 here
        float worst_interior = 0.f;
        for (int y = dst_box.min.y + 1; y < dst_box.max.y - 1; ++y)
            for (int x = dst_box.min.x + 1; x < dst_box.max.x - 1; ++x)
                worst_interior = std::max(worst_interior, std::fabs(ch(x, y) - 0.8f));
        ctx->LogInfo("worst interior error %f", worst_interior);
        IM_CHECK_LT(worst_interior, 0.05f);

        // nothing outside the selection moved
        IM_CHECK_LT(std::fabs(ch(2, 2) - 0.8f), 1e-4f);

        hdrview()->set_selection(Box2i{});
        hdrview()->set_clipboard(nullptr);
        reset_images(ctx);
    };

    t = IM_REGISTER_TEST(engine, "edit", "a seamless paste lands where it was aimed, over a background that varies");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img  = hdrview()->current_image();
        const int2 size = img->size();
        const int2 patch{32, 32};

        auto checker = [](int x, int y) { return ((x / 4 + y / 4) % 2) ? 1.f : 0.f; };

        // a flat background cannot tell a patch that landed correctly from one that did not, so the
        // background varies and the source does not. The answer inside the patch is then a smooth function
        // of its border alone, checkable everywhere.
        struct Config
        {
            const char *what;
            Box2i       selection;
            int2        expected; ///< Where the top-left of the patch should end up
        };
        const int2   at{size.x / 2, size.y / 2};
        const Config configs[] = {
            {"a selection the size of the clipboard", Box2i{at, at + patch}, at},
            // larger: the clipboard is placed at the selection's top-left, not centered or scaled
            {"a selection larger than the clipboard", Box2i{at, at + patch * 3}, at},
            // none at all: the whole image is the target, so the patch lands at its top-left
            {"no selection at all", Box2i{}, int2{0, 0}},
        };

        for (const Config &cfg : configs)
        {
            ctx->LogInfo("--- %s ---", cfg.what);

            // deselect first: these fills go through the current subject, and one left from the previous
            // pass would fill only that rectangle
            hdrview()->set_selection(Box2i{});
            ctx->Yield();

            // a constant, so the copy taken from it has no gradients of its own to impose
            IM_CHECK(modify_pixels(hdrview()->edit_context(img), "Fill",
                                   [](float, int2, int slot) { return slot == 3 ? 1.f : 0.5f; }));
            ctx->Yield();

            hdrview()->set_selection(Box2i{int2{0, 0}, patch});
            ctx->Yield();
            menu_click(ctx, "Edit/Copy");
            IM_CHECK(hdrview()->clipboard() != nullptr);

            hdrview()->set_selection(Box2i{});
            ctx->Yield();
            IM_CHECK(modify_pixels(hdrview()->edit_context(img), "Fill", [](float, int2 p, int slot)
                                   { return slot == 3 ? 1.f : (((p.x / 4 + p.y / 4) % 2) ? 1.f : 0.f); }));
            ctx->Yield();

            hdrview()->set_selection(cfg.selection);
            ctx->Yield();

            const int steps_before = img->history.size();
            menu_click(ctx, "Edit/Seamless paste...");
            ctx->SetRef("Seamless paste...");
            ctx->ItemInputValue("Iterations", 300);
            ctx->ItemClick("Paste");

            // it runs off the main thread behind a progress dialog and lands once the main thread has
            // drained it, so wait for the history to grow
            for (int i = 0; i < 4000 && img->history.size() == steps_before; ++i) ctx->Yield();
            IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Seamless paste");

            const auto &ch = img->channels[img->groups[img->selected_group].channels[0]];

            // whatever stopped being the checkerboard is what the paste touched, which says where it went
            // without trusting the command's own account of it
            Box2i touched{int2{size.x, size.y}, int2{0, 0}};
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x)
                    if (std::fabs(ch(x, y) - checker(x, y)) > 1e-4f)
                    {
                        touched.min = la::min(touched.min, int2{x, y});
                        touched.max = la::max(touched.max, int2{x + 1, y + 1});
                    }

            // the border ring keeps the background, so what moved is the interior: one sample in from the
            // patch on every side
            const int2 want_min = cfg.expected + int2{1, 1};
            const int2 want_max = cfg.expected + patch - int2{1, 1};
            IM_CHECK_EQ(touched.min, want_min);
            IM_CHECK_EQ(touched.max, want_max);

            // and with no gradients asked for, every sample inside is the average of its neighbors, smooth
            // right across the patch
            float worst_lap = 0.f;
            for (int y = touched.min.y + 1; y < touched.max.y - 1; ++y)
                for (int x = touched.min.x + 1; x < touched.max.x - 1; ++x)
                    worst_lap = std::max(worst_lap, std::fabs(ch(x - 1, y) + ch(x + 1, y) + ch(x, y - 1) +
                                                              ch(x, y + 1) + ch(x - 1, y - 1) + ch(x + 1, y + 1) +
                                                              ch(x + 1, y - 1) + ch(x - 1, y + 1) - 8.f * ch(x, y)));
            ctx->LogInfo("worst interior laplacian %f", worst_lap);
            IM_CHECK_LT(worst_lap, 1e-2f);

            menu_click(ctx, "Edit/Undo");
            ctx->Yield();
        }

        hdrview()->set_selection(Box2i{});
        hdrview()->set_clipboard(nullptr);
        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "painting straight after a resize reaches the new texture");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // a structural edit replaces every channel and its texture, and the next edit writes a tile into
        // whatever came back. Driven through the menu: an edit invoked from the test's own thread builds
        // its textures there, where there is no GL context, and gets back a texture handle of zero.
        menu_click(ctx, "Edit/Image size...");
        ctx->SetRef("Image size...");
        ctx->ComboClick("Units/Pixels");
        ctx->ItemInputValue("##width", 48);
        ctx->ItemClick("Resize");
        ctx->Yield(3);
        IM_CHECK_EQ(img->size().x, 48);

        menu_click(ctx, "Edit/Invert");
        ctx->Yield(2);
        IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Invert");
        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "an image a renderer owns refuses edits");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto img = hdrview()->current_image();

        // what arriving over IPC marks an image as: its pixels belong to the other process, so an edit
        // would be overwritten by the next tile and undoing one would restore samples already replaced
        img->is_live = true;
        ctx->Yield();

        IM_CHECK_EQ(hdrview()->action("Flip image horizontally").enabled(), false);
        IM_CHECK_EQ(hdrview()->action("Rotate 90 degrees clockwise").enabled(), false);
        IM_CHECK_EQ(hdrview()->action("Undo").enabled(), false);

        // not only grayed out in the menu: the edit itself has to decline, since the command palette and
        // the keyboard chord reach the same callback
        const auto                         before = snapshot(img);
        const std::function<void(Image &)> flip   = [](Image &i) { i.flip_horizontal(); };
        IM_CHECK_EQ(modify_image(hdrview()->edit_context(img), "Flip image horizontally", flip, reversible(flip, flip)),
                    false);
        IM_CHECK(snapshot(img) == before);
        IM_CHECK_EQ(img->history.has_undo(), false);

        img->is_live = false;
        reset_images(ctx);
    };
}
