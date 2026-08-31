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
