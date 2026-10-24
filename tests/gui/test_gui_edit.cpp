/** \file test_gui_edit.cpp
    \author Wojciech Jarosz

    The Edit menu, driven the way a user drives it: click the item, then look at the pixels. The
    logic-level suite covers what each command does to the samples; what these add is the wiring between
    the menu, the dialogs and the commands, and the invalidation that runs after an edit.
*/

#include "app.h"
#include "colorspace.h"
#include "edit/commands.h"
#include "edit/envmap.h"
#include "edit/preview.h"
#include "fonts.h"
#include "image.h"
#include "shared/edits.h"
#include "test_gui_registry.h"
#include "test_gui_support.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace hdrview_test;
using std::string;
using std::vector;

namespace
{

/// Loads the small single-layer fixture, alone, and leaves it as the current image with nothing selected.
bool load_fixture(ImGuiTestContext *ctx)
{
    reset_images(ctx);
    // edits are selection-only by default, so a selection left by an earlier test would narrow this one
    hdrview()->roi()          = Box2i{};
    hdrview()->edit_subject() = EditSubject{};
    hdrview()->set_clipboard(nullptr);
    IM_CHECK_SILENT_RETV(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE_2}) == 1, false);
    IM_CHECK_SILENT_RETV(hdrview()->current_image() != nullptr, false);
    return true;
}

/// Loads the fixture twice and selects both, with the first current.
bool load_selected_pair(ImGuiTestContext *ctx)
{
    reset_images(ctx);
    hdrview()->roi()          = Box2i{};
    hdrview()->edit_subject() = EditSubject{};
    IM_CHECK_SILENT_RETV(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE_2, HDRVIEW_GUI_TEST_IMAGE_2}) == 2, false);

    // selected here, not clicked: the chords that build a selection are navigation's business
    hdrview()->set_current_image_index(0);
    hdrview()->toggle_group_selected(1, hdrview()->image(1)->selected_group);
    IM_CHECK_SILENT_RETV(hdrview()->image(0)->is_selected() && hdrview()->image(1)->is_selected(), false);
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

/// Whether two snapshots hold the same bits, NaNs included, which compare unequal to themselves.
bool same(const vector<float> &a, const vector<float> &b)
{ return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0; }

/// How many samples outside \p roi differ from \p before, a snapshot of the same channels.
int changed_outside(const ConstImagePtr &img, const vector<float> &before, const Box2i &roi)
{
    int    changed = 0;
    size_t i       = 0;
    for (const auto &c : img->channels)
        for (int y = 0; y < c.size().y; ++y)
            for (int x = 0; x < c.size().x; ++x, ++i)
            {
                // half-open, so an edit reaching the selection's last row or column counts as outside it
                const int2 p = int2{x, y} + img->data_window.min;
                if (la::all(la::gequal(p, roi.min)) && la::all(la::less(p, roi.max)))
                    continue;
                const float v = c(x, y);
                changed += i >= before.size() || std::memcmp(&v, &before[i], sizeof(float)) != 0;
            }
    return changed;
}

/// \p name with each slash escaped, which a menu path or a window ref would otherwise read as a separator.
string escaped(const string &name)
{
    string out;
    for (char c : name)
    {
        if (c == '/')
            out += '\\';
        out += c;
    }
    return out;
}

void menu_click(ImGuiTestContext *ctx, const string &path)
{
    ctx->SetRef("##MainMenuBar");
    ctx->MenuClick(path.c_str());
    ctx->Yield(2);
}

/// Waits for the edit dialog \p name to close, and returns whether it did.
bool dialog_closed(ImGuiTestContext *ctx, const string &name)
{
    ctx->SetRef("");
    const string ref = "//" + escaped(name);
    return wait_until(
        ctx, [&] { return ctx->WindowInfo(ref.c_str(), ImGuiTestOpFlags_NoError).Window == nullptr; },
        std::chrono::seconds(2));
}

/// The remap dialog's settings for a small lat-long, point sampled once a pixel, so a preview frame stays cheap.
/**
    The preview evaluates the filter at every pixel of the viewport each frame, which on CI's software OpenGL makes a
    frame of EWA take long enough to trip the engine's watchdog.
*/
void small_remap_settings(ImGuiTestContext *ctx)
{
    ctx->ComboClick("Target/Longitude-latitude");
    ctx->ItemInputValue("Size/$$1", 48);
    ctx->ItemClick("Point");
    ctx->ItemInputValue("Samples per axis", 1);
}

} // namespace

void RegisterTests_Edit(ImGuiTestEngine *engine)
{
    ImGuiTest *t = nullptr;

    t           = IM_REGISTER_TEST(engine, "edit", "every edit without a dialog runs from its menu item and undoes");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // The category test for the menu: every command the Edit menu offers without a dialog, invoked the way a
        // user invokes it, over a selection well inside the image. What it does to the pixels belongs to the
        // logic-level suite; what this adds is that the item is there, reaches the command, keeps to the selection
        // where its subject says to, and leaves the image and the history in a state one Undo takes back.
        auto &undo      = hdrview()->action("Undo");
        auto &redo      = hdrview()->action("Redo");
        int   expected  = 0;
        int   exercised = 0;
        for (const auto &cmd : all_edit_commands())
        {
            const auto &info = cmd->info();
            if (info.has_dialog)
                continue;
            ++expected;

            const string name = info.names.front();
            ctx->LogInfo("%s", name.c_str());

            if (!load_fixture(ctx))
                return;
            auto       img  = hdrview()->current_image();
            const int2 size = img->size();
            const int2 o    = img->data_window.min;
            IM_CHECK_EQ(undo.enabled(), false);
            IM_CHECK_EQ(redo.enabled(), false);

            // what a command needs before it has anything to act on
            if (name == "Paste")
            {
                // copied from the middle, which differs from where it lands
                hdrview()->set_selection(Box2i{o + size / 2 - 16, o + size / 2 + 16});
                ctx->Yield();
                menu_click(ctx, "Edit/Copy");
                IM_CHECK(hdrview()->clipboard() != nullptr);
            }
            else if (name == "Regroup channels" || name == "Delete channel group")
            {
                // The fixture has one group, which neither may act on. Ungrouping leaves each of its channels
                // selected, and deleting every channel is not offered, so one is left selected.
                menu_click(ctx, "Edit/Ungroup channels");
                for (int g = 0; g < (int)img->groups.size(); ++g)
                    if (g != img->selected_group && img->is_group_selected(g))
                        hdrview()->toggle_group_selected(hdrview()->current_image_index(), g);
            }
            else if (name == "Clamp to [0,1]")
                // the fixture is an 8-bit PNG, already in [0, 1]
                img->channels[0](size / 2) = 2.f;

            const Box2i roi{o + size / 4, o + size - size / 4};
            hdrview()->set_selection(roi);
            ctx->Yield();

            // a disabled item does nothing when clicked, and would pass everything below
            IM_CHECK(hdrview()->action(name).enabled());

            const int    entries  = img->history.current_state();
            const bool   modified = img->history.is_modified();
            const auto   before   = snapshot(img);
            const size_t groups   = img->groups.size();
            const size_t channels = img->channels.size();
            IM_CHECK_EQ(undo.enabled(), entries > 0);
            IM_CHECK_EQ(redo.enabled(), false);

            // the menu labels this one by what it would delete
            const string label =
                name == "Delete channel group" ? delete_channels_label(img, hdrview()->target_groups(img)) : name;
            menu_click(ctx, "Edit/" + escaped(label));
            ++exercised;

            // A command that does not need an editable image cannot be editing one, and Copy is the only such
            // command: it reads the selection and leaves the image alone.
            if (!info.needs_editable)
            {
                ctx->Yield(4);
                IM_CHECK_EQ(img->history.current_state(), entries);
                IM_CHECK(same(snapshot(img), before));
                IM_CHECK(hdrview()->clipboard() != nullptr && hdrview()->clipboard()->size() == roi.size());
                continue;
            }

            // every other one owes one undo entry over the lot, not one per group or channel
            IM_CHECK(wait_until(ctx, [&] { return img->history.current_state() > entries; }, std::chrono::seconds(5)));
            IM_CHECK_EQ(img->history.current_state(), entries + 1);
            IM_CHECK_EQ(img->history.size(), entries + 1);
            IM_CHECK_STR_EQ(img->history.undo_name().c_str(), name.c_str());
            IM_CHECK(img->history.is_modified());
            IM_CHECK(hdrview()->any_image_modified());
            IM_CHECK_EQ(undo.enabled(), true);
            IM_CHECK_EQ(redo.enabled(), false);

            // it did something: moved samples, or regrouped or removed channels; and where it takes a subject, only
            // under the selection
            const auto edited = snapshot(img);
            IM_CHECK(!same(edited, before) || img->groups.size() != groups || img->channels.size() != channels);
            if (info.draws_subject_selector)
                IM_CHECK_EQ(changed_outside(img, before, roi), 0);

            menu_click(ctx, "Edit/Undo");
            IM_CHECK(same(snapshot(img), before));
            // the tree is rebuilt after an edit, not appended to
            IM_CHECK_EQ(img->groups.size(), groups);
            IM_CHECK_EQ(img->channels.size(), channels);
            IM_CHECK_EQ(img->history.current_state(), entries);
            IM_CHECK_EQ(img->history.is_modified(), modified);
            IM_CHECK_EQ(hdrview()->any_image_modified(), modified);
            IM_CHECK_EQ(undo.enabled(), entries > 0);
            IM_CHECK_EQ(redo.enabled(), true);

            // and redo puts it back where undo found it
            menu_click(ctx, "Edit/Redo");
            IM_CHECK(same(snapshot(img), edited));
            IM_CHECK_EQ(undo.enabled(), true);
            IM_CHECK_EQ(redo.enabled(), false);
            menu_click(ctx, "Edit/Undo");
            IM_CHECK(same(snapshot(img), before));
        }
        IM_CHECK_EQ(exercised, expected);

        hdrview()->set_selection(Box2i{});
        hdrview()->set_clipboard(nullptr);
        reset_images(ctx);
    };

    // In two parts, each every other command, which keeps either well inside the engine's watchdog under CI's
    // software rasterizer.
    for (int part = 0; part < 2; ++part)
    {
        t = IM_REGISTER_TEST(engine, "edit",
                             part == 0 ? "every edit dialog cancels, confirms as one edit, and undoes (1 of 2)"
                                       : "every edit dialog cancels, confirms as one edit, and undoes (2 of 2)");
        t->TestFunc = [part](ImGuiTestContext *ctx)
        {
            /*
                The dialog counterpart of the sweep above, over the whole image: each opens from its menu item, Escape
                leaves the image and its history alone, and confirming lands one entry that changes the samples or the
                size, which one Undo takes back. A reshaped image is then inverted, since the next edit writes into the
                textures the reshaping left. A command whose defaults change nothing is given a setting first, through
                its own controls, since the command keeps its settings between openings.
            */
            struct Case
            {
                /// Run before the dialog opens, on the image it will edit.
                std::function<void(ImGuiTestContext *, const ImagePtr &)> before_open;
                /// Run in the dialog, with the ref set to it, each time it opens.
                std::function<void(ImGuiTestContext *)> settings;
                /// Checks particular to the command, once its edit has landed, given the samples it started from.
                std::function<void(ImGuiTestContext *, const ConstImagePtr &, const vector<float> &)> check;
                bool enter       = false; ///< Confirmed with Enter, rather than with the confirm button
                int  adds_images = 0;     ///< Images it puts in the list, in place of an edit to the current one
            };

            auto mixed_to_gray = [](ImGuiTestContext *, const ConstImagePtr &img, const vector<float> &)
            {
                // monochrome means the three channels came out equal, which says the mix ran
                const auto &group = img->groups[img->selected_group];
                IM_CHECK_SILENT(group.num_channels >= 3);
                const auto &r     = img->channels[group.channels[0]];
                const auto &g     = img->channels[group.channels[1]];
                const auto &b     = img->channels[group.channels[2]];
                float       worst = 0.f;
                for (int i = 0; i < r.num_elements(); ++i)
                    worst = std::max({worst, std::fabs(r(i) - g(i)), std::fabs(r(i) - b(i))});
                IM_CHECK_LT(worst, 1e-5f);
            };
            auto turned_hue = [](ImGuiTestContext *, const ConstImagePtr &img, const vector<float> &before)
            {
                // a turn, with saturation and lightness at zero, keeps each pixel's smallest and largest component and
                // moves the one between
                const int4   ch             = img->groups[img->selected_group].channels;
                const size_t n              = size_t(img->channels[ch[0]].num_elements());
                int          ranges_changed = 0;
                float        moved          = 0.f;
                for (size_t i = 0; i < n; ++i)
                {
                    const float3 a{before[ch[0] * n + i], before[ch[1] * n + i], before[ch[2] * n + i]};
                    const float3 b{img->channels[ch[0]](int(i)), img->channels[ch[1]](int(i)),
                                   img->channels[ch[2]](int(i))};
                    ranges_changed += std::fabs(la::minelem(a) - la::minelem(b)) > 1e-4f ||
                                      std::fabs(la::maxelem(a) - la::maxelem(b)) > 1e-4f;
                    moved = std::max(moved, la::maxelem(la::abs(a - b)));
                }
                IM_CHECK_EQ(ranges_changed, 0);
                IM_CHECK_GT(moved, 1e-3f); //< a turn, not rounding
            };

            const std::map<string, Case> cases{
                {"Seamless paste...",
                 {[](ImGuiTestContext *ctx, const ImagePtr &img)
                  {
                      // a patch of the middle, which lands at the corner
                      const int2 c = img->data_window.min + img->size() / 2;
                      hdrview()->set_selection(Box2i{c - 16, c + 16});
                      ctx->Yield();
                      menu_click(ctx, "Edit/Copy");
                      hdrview()->set_selection(Box2i{});
                      ctx->Yield();
                  }}},
                {"Exposure/gamma...", {nullptr, [](ImGuiTestContext *ctx) { ctx->ItemInputValue("Exposure", 1.f); }}},
                {"Brightness/contrast...",
                 {nullptr, [](ImGuiTestContext *ctx) { ctx->ItemInputValue("Brightness", 0.4f); }}},
                {"Convert color space...",
                 {nullptr, [](ImGuiTestContext *ctx) { ctx->ComboClick("Primaries##to/ACES AP0"); },
                  [](ImGuiTestContext *, const ConstImagePtr &img, const vector<float> &)
                  { IM_CHECK_EQ(img->color_space, ColorGamut_ACES_AP0); }}},
                {"Channel mixer...",
                 {nullptr, [](ImGuiTestContext *ctx) { ctx->ItemCheck("Monochrome"); }, mixed_to_gray}},
                {"Hue/saturation...",
                 {nullptr,
                  [](ImGuiTestContext *ctx)
                  {
                      ctx->ItemInputValue("Saturation", 0.f);
                      ctx->ItemInputValue("Lightness", 0.f);
                      // the lower strip turns the hue as it is dragged
                      ctx->ItemDragWithDelta("The hue wheel as these settings would leave it; drag it to turn the hue",
                                             ImVec2(HelloImGui::EmSize(3.f), 0.f));
                  },
                  turned_hue}},
                {"Image size...",
                 {nullptr,
                  [](ImGuiTestContext *ctx)
                  {
                      ctx->ComboClick("Units/Pixels");
                      ctx->ItemInputValue("##width", 48);
                  },
                  [](ImGuiTestContext *, const ConstImagePtr &img, const vector<float> &)
                  { IM_CHECK_EQ(img->size().x, 48); }}},
                // a width of 40, or 40 more, whichever way the dialog was left
                {"Canvas size...",
                 {nullptr,
                  [](ImGuiTestContext *ctx)
                  {
                      ctx->ComboClick("Units/Pixels");
                      ctx->ItemInputValue("##width", 40);
                  }}},
                {"Generate mipmaps...",
                 {nullptr, [](ImGuiTestContext *ctx) { ctx->ItemInputValue("Levels", 2); }, nullptr, false, 2}},
                {"Blur...", {nullptr, nullptr, nullptr, true}},
                {"Unsharp mask...", {nullptr, nullptr, nullptr, true}},
                {"Median filter...", {nullptr, [](ImGuiTestContext *ctx) { ctx->ItemInputValue("Radius", 1.5f); }}},
                {"Shift...", {nullptr, [](ImGuiTestContext *ctx) { ctx->ItemInputValue("X, Y offset/$$0", 3.f); }}},
                {"Zap gremlins...",
                 {[](ImGuiTestContext *, const ImagePtr &img)
                  { img->channels[0](img->size() / 2) = std::numeric_limits<float>::quiet_NaN(); }, nullptr,
                  [](ImGuiTestContext *, const ConstImagePtr &img, const vector<float> &)
                  { IM_CHECK(std::isfinite(img->channels[0](img->size() / 2))); }}},
                {"Bump to normal map...", {nullptr, nullptr, nullptr, true}},
                {"Remap envmap...", {nullptr, small_remap_settings}},
                // tiny: the convolution costs the two resolutions multiplied together, and the result is smooth
                {"Irradiance envmap...",
                 {nullptr,
                  [](ImGuiTestContext *ctx)
                  {
                      ctx->ItemInputValue("Size/$$0", 8);
                      ctx->ItemInputValue("Size/$$1", 6);
                  },
                  [](ImGuiTestContext *, const ConstImagePtr &img, const vector<float> &)
                  {
                      IM_CHECK((img->size() == int2{8, 6}));
                      for (const auto &c : img->channels) IM_CHECK((c.size() == int2{8, 6}));
                  }}},
            };

            // this part's commands: every other one with a dialog
            auto                            *app      = hdrview();
            const auto                       commands = all_edit_commands();
            std::vector<const EditCommand *> ours;
            int                              dialogs = 0;
            for (const auto &cmd : commands)
                if (cmd->info().has_dialog && dialogs++ % 2 == part)
                    ours.push_back(cmd.get());
            // a case for a name no command has any more is a setting silently lost
            for (const auto &[name, c] : cases)
            {
                bool found = false;
                for (const auto &cmd : all_edit_commands()) found |= cmd->info().names.front() == name;
                IM_CHECK_NO_RET(found);
                if (!found)
                    ctx->LogError("no command is called %s", name.c_str());
            }

            for (const EditCommand *cmd : ours)
            {
                const auto &info = cmd->info();

                const string name = info.names.front();
                ctx->LogInfo("%s", name.c_str());
                const auto  found = cases.find(name);
                const Case &c     = found != cases.end() ? found->second : Case{};

                if (!load_fixture(ctx))
                    return;
                auto img = app->current_image();
                if (c.before_open)
                    c.before_open(ctx, img);
                const int2 size   = img->size();
                const auto before = snapshot(img);

                auto open = [&]
                {
                    menu_click(ctx, "Edit/" + escaped(name));
                    ctx->SetRef(escaped(name).c_str());
                    if (c.settings)
                        c.settings(ctx);
                    ctx->Yield();
                };

                // applying on confirm keeps a dragged slider from filling the history, so cancelling has to leave both
                // the pixels and the history alone, whatever the controls were set to
                open();
                ctx->KeyPress(ImGuiKey_Escape);
                IM_CHECK(dialog_closed(ctx, name));
                IM_CHECK(app->edit_preview() == nullptr);
                IM_CHECK(same(snapshot(img), before));
                IM_CHECK_EQ(img->history.size(), 0);
                IM_CHECK_EQ(app->num_images(), 1);

                // A point-wise edit lands in the frame it was confirmed, and takes its preview down with it, or the
                // next frame would draw it applied twice; the rest are handed to a worker and land later.
                const bool in_frame = cmd->point_edit(app->edit_context(img)).has_value();
                open();
                if (c.enter)
                    ctx->KeyPress(ImGuiKey_Enter);
                else
                    ctx->ItemClick(info.confirm.c_str());
                if (in_frame)
                {
                    IM_CHECK_EQ(img->history.size(), 1);
                    IM_CHECK(app->edit_preview() == nullptr);
                }

                IM_CHECK(wait_until(ctx, [&] { return img->history.has_undo() || app->num_images() > 1; }));
                IM_CHECK(dialog_closed(ctx, name));
                IM_CHECK(app->edit_preview() == nullptr);

                if (c.adds_images)
                {
                    // beside the image they were made from, which they leave alone
                    IM_CHECK_EQ(app->num_images(), 1 + c.adds_images);
                    IM_CHECK_EQ(img->history.size(), 0);
                    IM_CHECK(same(snapshot(img), before));
                    continue;
                }

                IM_CHECK_EQ(app->num_images(), 1);
                IM_CHECK_EQ(img->history.size(), 1);
                IM_CHECK(!img->history.undo_name().empty());
                IM_CHECK(img->history.is_modified());
                const bool reshaped = img->size() != size;
                IM_CHECK(reshaped || !same(snapshot(img), before));
                if (c.check)
                    c.check(ctx, img, before);

                // A reshaping replaces every channel and its texture, and the next edit writes a tile into whatever
                // came back. Driven through the menu: an edit invoked from the test's own thread builds its textures
                // there, where there is no GL context, and gets back a texture handle of zero.
                if (reshaped)
                {
                    const auto reshaped_samples = snapshot(img);
                    menu_click(ctx, "Edit/Invert");
                    IM_CHECK_EQ(img->history.size(), 2);
                    IM_CHECK_STR_EQ(img->history.undo_name().c_str(), "Invert");
                    IM_CHECK(!same(snapshot(img), reshaped_samples));
                    menu_click(ctx, "Edit/Undo");
                }

                menu_click(ctx, "Edit/Undo");
                IM_CHECK(img->size() == size);
                IM_CHECK(same(snapshot(img), before));
                IM_CHECK(!img->history.has_undo());
            }

            hdrview()->set_clipboard(nullptr);
            reset_view_controls(ctx);
            reset_images(ctx);
        };
    }

    t           = IM_REGISTER_TEST(engine, "edit", "an edit covers every selected image, but cut only the current one");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_selected_pair(ctx))
            return;
        auto *app     = hdrview();
        auto  entries = [app](int i) { return (int)app->image(i)->history.size(); };

        // an edit that takes a subject reaches both, each as an entry in its own history
        menu_click(ctx, "Edit/Invert");
        for (int i = 0; i < 2; ++i)
        {
            IM_CHECK_EQ(entries(i), 1);
            IM_CHECK_STR_EQ(app->image(i)->history.undo_name().c_str(), "Invert");
        }

        // undo and redo step every selected image, not only the one being looked at
        menu_click(ctx, "Edit/Undo");
        for (int i = 0; i < 2; ++i) IM_CHECK_EQ(app->image(i)->history.has_undo(), false);
        menu_click(ctx, "Edit/Redo");
        for (int i = 0; i < 2; ++i) IM_CHECK_EQ(app->image(i)->history.has_undo(), true);

        // an edit that reshapes an image reaches both too
        menu_click(ctx, "Edit/Rotate 90 degrees clockwise");
        for (int i = 0; i < 2; ++i) IM_CHECK_EQ(entries(i), 2);

        // and crops both to the same rectangle: cropping consumes the selection, so each invocation has to start
        // from the same one
        app->set_selection(Box2i{int2{1, 1}, int2{5, 4}});
        ctx->Yield();
        menu_click(ctx, "Edit/Crop to selection");
        for (int i = 0; i < 2; ++i)
        {
            IM_CHECK((app->image(i)->size() == int2{4, 3}));
            IM_CHECK_EQ(entries(i), 3);
        }
        IM_CHECK_EQ(app->roi().has_volume(), false);
        IM_CHECK_EQ(app->roi_live().has_volume(), false);

        // only one filter runs at a time, there being one progress bar and one Cancel, so the second image's blur
        // waits for the first and starts as it lands
        menu_click(ctx, "Edit/Blur...");
        ctx->SetRef("Blur...");
        ctx->KeyPress(ImGuiKey_Enter);
        IM_CHECK(wait_until(ctx, [&] { return entries(0) == 4 && entries(1) == 4; }));

        // cutting and copying are the exception: there is one clipboard, so both act on the image being looked at
        // and leave the rest of the selection alone
        menu_click(ctx, "Edit/Select all");
        menu_click(ctx, "Edit/Cut");
        IM_CHECK_EQ(entries(0), 5);
        IM_CHECK_EQ(entries(1), 4);
        IM_CHECK(app->clipboard() != nullptr);
        IM_CHECK(app->clipboard()->size() == app->image(0)->size());

        // edited images left loaded would make the next test's close_all_images() prompt
        app->set_selection(Box2i{});
        app->set_clipboard(nullptr);
        reset_images(ctx);
    };

    t           = IM_REGISTER_TEST(engine, "edit", "an image a renderer owns refuses edits, but can be copied");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;
        auto *app = hdrview();
        auto  img = app->current_image();

        // nothing has been copied in this session yet, so there is nothing to paste
        ctx->Yield();
        IM_CHECK_EQ(app->action("Paste").enabled(), false);

        // something to undo, which a live image must not offer either
        menu_click(ctx, "Edit/Invert");
        IM_CHECK_EQ(app->action("Undo").enabled(), true);

        // What arriving over IPC marks an image as: its pixels belong to the other process, so an edit would be
        // overwritten by the next tile and undoing one would restore samples already replaced. Every command that
        // edits is withdrawn, the command palette and the keyboard chords reaching the same actions, but reading
        // the image is not editing it.
        img->is_live = true;
        OnExit restore{[img] { img->is_live = false; }};
        ctx->Yield();
        for (const auto &cmd : all_edit_commands())
        {
            const string name = cmd->info().names.front();
            ctx->LogInfo("%s", name.c_str());
            IM_CHECK_EQ(app->action(name).enabled(), !cmd->info().needs_editable);
        }
        IM_CHECK_EQ(app->action("Undo").enabled(), false);
        IM_CHECK_EQ(app->action("Redo").enabled(), false);

        menu_click(ctx, "Edit/Copy");
        IM_CHECK(app->clipboard() != nullptr);

        // pasting into it is still refused, since that would be editing it
        IM_CHECK_EQ(app->action("Paste").enabled(), false);

        app->set_clipboard(nullptr);
        reset_images(ctx);
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
        // the viewport draws the marquee from roi_live(), so clearing only roi() leaves the rectangle on screen with
        // nothing behind it
        IM_CHECK_EQ(hdrview()->roi_live().has_volume(), false);

        // and through the keyboard, both plainly and once keyboard navigation is showing, the state the command
        // palette and the dialogs leave behind
        for (bool nav_visible : {false, true})
        {
            hdrview()->set_selection(img->data_window);
            if (nav_visible)
                ImGui::GetIO().NavVisible = true;
            ctx->Yield(2);
            IM_CHECK_EQ(hdrview()->roi().has_volume(), true);

            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
            ctx->Yield(2);
            IM_CHECK_EQ(hdrview()->roi().has_volume(), false);
            IM_CHECK_EQ(hdrview()->roi_live().has_volume(), false);
        }
    };

    t           = IM_REGISTER_TEST(engine, "edit", "duplicating copies the samples, or with a selection just those");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       original = hdrview()->current_image();
        const auto before   = snapshot(original);
        const int  index    = hdrview()->current_image_index();

        menu_click(ctx, "File/Duplicate image");
        IM_CHECK_EQ(hdrview()->num_images(), 2);

        // beside the one it was made from, and selected
        IM_CHECK_EQ(hdrview()->current_image_index(), index + 1);
        auto copy = hdrview()->current_image();
        IM_CHECK(copy != nullptr && copy != original);

        // the same picture, read the same way: a copy read in different primaries is a different picture
        IM_CHECK(copy->size() == original->size());
        IM_CHECK(same(snapshot(copy), before));
        IM_CHECK_EQ(copy->color_space, original->color_space);
        IM_CHECK_EQ(copy->transparency, original->transparency);
        IM_CHECK_EQ(copy->groups.size(), original->groups.size());

        // ...and its own copy of it, with its own history: a shallow copy would pass everything above and fail here
        menu_click(ctx, "Edit/Invert");
        IM_CHECK(!same(snapshot(copy), before));
        IM_CHECK(same(snapshot(original), before));
        IM_CHECK_EQ(copy->history.has_undo(), true);
        IM_CHECK_EQ(original->history.has_undo(), false);
        IM_CHECK_EQ(original->history.is_modified(), false);

        // with a selection, the size of what was selected, holding those samples
        hdrview()->set_current_image_index(index);
        const int2  size = original->size();
        const Box2i box{original->data_window.min + size / 4, original->data_window.min + size / 2};
        hdrview()->set_selection(box);
        ctx->Yield();
        menu_click(ctx, "File/Duplicate image");

        auto part = hdrview()->current_image();
        IM_CHECK(part != original && part != copy);
        IM_CHECK(part->size() == box.size());
        const int2 at = box.min - original->data_window.min;
        for (size_t c = 0; c < original->channels.size(); ++c)
            for (int i = 0; i < 5; ++i) IM_CHECK_EQ(part->channels[c](i, i), original->channels[c](at.x + i, at.y + i));

        hdrview()->set_selection(Box2i{});
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

        reset_images(ctx);
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

    t           = IM_REGISTER_TEST(engine, "edit", "each brightness/contrast control reaches the edit it previews");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // read off the preview, which is the point-wise edit applying it would make; what each mode does to the
        // samples is the logic-level suite's
        auto shown = []
        {
            const EditPreview *p = hdrview()->edit_preview();
            return p ? p->edit : PointEdit{};
        };

        menu_click(ctx, "Edit/Brightness\\/contrast...");
        // escaped, as the menu path is: the slash in the name is a path separator to the engine, so an unescaped
        // ref looks for "contrast..." inside a window called "Brightness"
        ctx->SetRef("Brightness\\/contrast...");
        ctx->ItemCheck("Preview");

        const std::pair<const char *, int> modes[] = {{"RGB", shader::CURVE_RGB},
                                                      {"Lightness", shader::CURVE_LIGHTNESS},
                                                      {"Chromaticity", shader::CURVE_CHROMATICITY}};
        for (const auto &[button, curve] : modes)
        {
            ctx->ItemClick(button);
            ctx->Yield(2);
            IM_CHECK_EQ(shown().op, shader::EDIT_BRIGHTNESS_CONTRAST);
            IM_CHECK_EQ(int(shown().p1.x), curve);
        }

        // the plot is drawn above the sliders and must not swallow them; the pivot is where brightness puts it
        ctx->ItemInputValue("Brightness", 0.4f);
        ctx->Yield(2);
        IM_CHECK_EQ(shown().p0.y, (1.f - 0.4f) / 2.f);

        // the curve's shape, and the clip after it
        ctx->ItemUncheck("Linear");
        ctx->ItemUncheck("Clip to SDR");
        ctx->Yield(2);
        IM_CHECK_EQ(int(shown().p0.w), shader::SHAPE_RATIONAL);
        ctx->ItemCheck("Linear");
        ctx->Yield(2);
        IM_CHECK_EQ(int(shown().p0.w), shader::SHAPE_LINE);
        ctx->ItemCheck("Clip to SDR");
        ctx->Yield(2);
        IM_CHECK_EQ(int(shown().p0.w), shader::SHAPE_LINE + shader::SHAPE_CLIP);

        ctx->ItemUncheck("Clip to SDR");
        ctx->ItemClick("Cancel");
        IM_CHECK(dialog_closed(ctx, "Brightness/contrast..."));
        IM_CHECK(!hdrview()->current_image()->history.has_undo());
    };

    t = IM_REGISTER_TEST(engine, "edit", "the shift preview follows its offset, and a drag carries the image");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto  *app = hdrview();
        auto   img = app->current_image();
        OnExit restore{[&, zoom = app->zoom()] { app->set_zoom(zoom); }};

        // previewed as the shift asked for, over the channels the view shows: the shown group's, all of them the
        // subject's
        menu_click(ctx, "Edit/Shift...");
        ctx->SetRef("Shift...");
        ctx->ItemInputValue("X, Y offset/$$0", 3.0f);
        ctx->ItemInputValue("X, Y offset/$$1", 0.0f);
        ctx->Yield(2);
        const EditPreview *preview = app->edit_preview();
        IM_CHECK(preview && preview->shift);
        IM_CHECK(preview->shift->offset == float2(3.f, 0.f));
        int4 slots{-1};
        for (int k = 0; k < std::min(4, img->groups[img->selected_group].num_channels); ++k) slots[k] = k;
        IM_CHECK(preview->slots == slots);

        // A drag begun on the image, clear of the dialog, carries the image with it by as far as the cursor went,
        // fractions of a pixel included: whole units of the cursor, which the engine moves in, at a zoom of 2.5.
        app->set_zoom(2.5f);
        ctx->Yield(2);
        const auto on = preview_point(ctx, "//Shift...");
        IM_CHECK(on.has_value());
        drag(ctx, *on, float2{19.f, 8.f});
        preview = app->edit_preview();
        IM_CHECK(preview && preview->shift);
        IM_CHECK(la::length(preview->shift->offset - float2{3.f + 19.f / 2.5f, 8.f / 2.5f}) < 1e-3f);

        ctx->ItemClick("Cancel");
        ctx->Yield(2);
        IM_CHECK(!img->history.has_undo());
    };

    // what the view shows of a remap's preview: the remap, and the corners of the result's window, in the viewport
    auto shown_remap = []
    {
        const EditPreview *p = hdrview()->edit_preview();
        return p && p->remap ? std::optional<EnvmapRemap>{*p->remap} : std::nullopt;
    };
    auto shown_corners = []
    {
        const EditPreview *p = hdrview()->edit_preview();
        if (!p || !p->window)
            return std::pair{float2{NAN}, float2{NAN}};
        const Box2f w{*p->window};
        return std::pair{hdrview()->vp_pos_at_pixel(w.min, w), hdrview()->vp_pos_at_pixel(w.max, w)};
    };
    auto open_remap = [](ImGuiTestContext *ctx)
    {
        menu_click(ctx, "Edit/Remap envmap...");
        ctx->SetRef("Remap envmap...");
        small_remap_settings(ctx);
        ctx->Yield(2);
    };

    t = IM_REGISTER_TEST(engine, "edit", "the remap preview frames the output, turns with its sliders and drags");
    t->TestFunc = [shown_remap, shown_corners, open_remap](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // The status bar shows what is under the cursor, here the viewport's middle, until a preview hides it; and
        // zoomed to 2 first, so the preview's fit to the view shows as a change.
        auto      *app        = hdrview();
        bool      &status_bar = *app->action("Show status bar").p_selected;
        const bool had_bar    = status_bar;
        OnExit     restore{[&, zoom = app->zoom(), at = app->vp_pos_at_pixel(float2{0.f})]
                           {
                           status_bar = had_bar;
                           app->set_zoom(zoom);
                           app->reposition_pixel_to_vp_pos(at, float2{0.f});
                           }};
        status_bar = true;
        app->set_zoom(2.f);
        ctx->MouseMoveToPos(ImVec2(app->app_pos_at_vp_pos(0.5f * app->viewport_size())));
        ctx->Yield(2);
        IM_CHECK(ctx->ItemExists("//StatusBar/##pixel x coordinates"));
        const float  before_zoom = app->zoom();
        const float2 before_at   = app->vp_pos_at_pixel(float2{0.f});

        // Previewed as it will come out: a lat-long twice as wide as tall, drawn at the view's zoom, with the pixel
        // readout hidden, since the pixel under the cursor no longer holds what is shown.
        open_remap(ctx);
        const auto shown = shown_remap();
        IM_CHECK(shown.has_value());
        IM_CHECK_EQ(shown->dst, int(EnvMapping_LatLong));
        IM_CHECK_EQ(shown->size.x, 2 * shown->size.y);
        const auto [lo, hi] = shown_corners();
        IM_CHECK(la::length(la::abs(hi - lo) - app->zoom() * float2(shown->size)) < 1e-3f);
        IM_CHECK(!ctx->ItemExists("//StatusBar/##pixel x coordinates"));
        // fitting the view to it
        IM_CHECK_EQ(app->zoom(), std::min(app->viewport_size().x / float(shown->size.x),
                                          app->viewport_size().y / float(shown->size.y)));

        // unticked, nothing is previewed, and the readout comes back
        ctx->ItemUncheck("Preview");
        ctx->Yield(2);
        IM_CHECK(app->edit_preview() == nullptr);
        IM_CHECK(ctx->ItemExists("//StatusBar/##pixel x coordinates"));
        ctx->ItemCheck("Preview");
        ctx->Yield(2);

        // NaN without a preview, so a comparison with it fails rather than reading nothing
        auto rotation = [&]
        {
            const auto r = shown_remap();
            return r ? r->rotation : float3x3{float3{NAN}, float3{NAN}, float3{NAN}};
        };

        // The sliders turn it as they say, roll included where a quarter-turn pitch locks it to the yaw, and Reset
        // rotation undoes them.
        ctx->ItemInputValue("Pitch", -90.f);
        ctx->ItemInputValue("Roll", 30.f);
        ctx->ItemInputValue("Roll", 60.f);
        ctx->Yield(2);
        {
            const float3x3 want = envmap_rotation(0.f, -shader::ENVMAP_PI / 2.f, shader::ENVMAP_PI / 3.f);
            for (int c = 0; c < 3; ++c) IM_CHECK(la::length(rotation()[c] - want[c]) < 1e-4f);
        }
        ctx->ItemClick("Reset rotation");
        ctx->Yield(2);
        IM_CHECK(rotation() == float3x3{la::identity});

        // A plain drag begun on the preview turns the sphere, so what was under the cursor where the drag began is
        // under it where the drag ended, and leaves the view where it was; with Alt held, it pans the view by the drag
        // instead and leaves the sphere alone.
        const auto on = preview_point(ctx, "//Remap envmap...");
        IM_CHECK(on.has_value());
        const float2 by{HelloImGui::EmSize(3.f), HelloImGui::EmSize(2.f)};
        // what the source shows at a point of the preview, under the remap's rotation
        auto seen = [&](float2 app_pos, const float3x3 &R)
        {
            const auto uv = app->preview_uv(app->vp_pos_at_app_pos(app_pos));
            return mul(R, envmap_uv_to_xyz(EnvMapping_LatLong, uv ? *uv : float2{0.f}));
        };
        const auto [lo0, hi0] = shown_corners();
        const float3 grabbed  = seen(*on, rotation());
        drag(ctx, *on, by);
        IM_CHECK(la::length(seen(*on + by, rotation()) - grabbed) < 2e-2f);
        const auto [lo1, hi1] = shown_corners();
        IM_CHECK(la::length(lo1 - lo0) < 1e-3f);

        const float3x3 turned = rotation();
        drag(ctx, *on, by, ImGuiMod_Alt);
        IM_CHECK(rotation() == turned);
        const auto [lo2, hi2] = shown_corners();
        IM_CHECK(la::length(lo2 - lo1 - by) < 1e-2f);

        // and canceled, the view the preview took over is given back
        IM_CHECK_NE(app->zoom(), before_zoom);
        ctx->ItemClick("Cancel");
        ctx->Yield(2);
        IM_CHECK(app->edit_preview() == nullptr);
        IM_CHECK_EQ(app->zoom(), before_zoom);
        IM_CHECK(la::length(app->vp_pos_at_pixel(float2{0.f}) - before_at) < 1e-3f);
    };

    t = IM_REGISTER_TEST(engine, "edit", "remapping resizes the image where its preview was, and undo puts it back");
    t->TestFunc = [shown_remap, shown_corners, open_remap](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        auto       img      = hdrview()->current_image();
        auto      *app      = hdrview();
        const int2 original = img->size();
        const auto samples  = snapshot(img);
        OnExit     restore{[&, zoom = app->zoom()] { app->set_zoom(zoom); }};

        // panned while it is previewed, so the result is seen to land where its preview was, not where a fit would
        // put it
        open_remap(ctx);
        const auto shown = shown_remap();
        IM_CHECK(shown.has_value());
        const auto on = preview_point(ctx, "//Remap envmap...");
        IM_CHECK(on.has_value());
        drag(ctx, *on, float2{HelloImGui::EmSize(3.f), HelloImGui::EmSize(2.f)}, ImGuiMod_Alt);
        const auto [lo, hi] = shown_corners();

        // structural and computed off the main thread at once: the result is a different size than what it was
        // computed from, so there is no rectangle to write back
        ctx->ItemClick("Remap");
        wait_until(ctx, [&] { return img->history.has_undo(); });

        IM_CHECK(img->size() == shown->size);
        IM_CHECK(la::length(app->vp_pos_at_pixel(float2(img->data_window.min)) - lo) < 1e-3f);
        IM_CHECK(la::length(app->vp_pos_at_pixel(float2(img->data_window.max)) - hi) < 1e-3f);
        for (const auto &c : img->channels) IM_CHECK(c.size() == img->size());
        IM_CHECK(img->data_window.size() == img->size());
        IM_CHECK(img->display_window.size() == img->size());

        menu_click(ctx, "Edit/Undo");
        IM_CHECK(img->size() == original);
        IM_CHECK(snapshot(img) == samples);
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
}
