/** \file test_gui_dialogs.cpp
    \author Wojciech Jarosz

    Opens and closes the File-menu dialogs, runs every save format's options panel, and checks what each kind of
    save writes.
*/

#include "app.h"
#include "colorspace.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

#include <cmath>
#include <cstring>
#include <limits>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

// ImGui::CloseCurrentPopup() takes a couple of frames to take effect (~2 at ImGuiTestRunSpeed_Fast), so
// poll for the window to disappear
static void wait_for_window_closed(ImGuiTestContext *ctx, const char *window_name)
{
    wait_until(ctx, [&] { return ctx->WindowInfo(window_name, ImGuiTestOpFlags_NoError).Window == nullptr; });
}

void RegisterTests_Dialogs(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "dialogs", "image_loading_options_open_close");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        // the menu bar is a top-level BeginMainMenuBar() window named "##MainMenuBar", not part of
        // "MainDockSpace"'s ID stack
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("File/Image loading options...");

        // the popup is a top-level window, not a child of "##MainMenuBar": reset the ref to root, or
        // WindowInfo() searches relative to the still-active one
        ctx->SetRef("");
        IM_CHECK(ctx->WindowInfo("Image loading options...").Window != nullptr);

        ctx->SetRef("Image loading options...");
        ctx->ItemClick("OK");

        ctx->SetRef("");
        wait_for_window_closed(ctx, "Image loading options...");
        IM_CHECK(ctx->WindowInfo("Image loading options...", ImGuiTestOpFlags_NoError).Window == nullptr);
    };

    t           = IM_REGISTER_TEST(engine, "dialogs", "save_as_open_close");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        // two images, so a Down that reached the shortcuts behind the dialog would change the current one
        if (hdrview()->num_images() < 2)
        {
            hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2});
            wait_for_loads(ctx);
        }
        IM_CHECK(hdrview()->num_images() >= 2);
        const int current = hdrview()->current_image_index();

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("File/Save as...");

        ctx->SetRef("");
        IM_CHECK(ctx->WindowInfo("Save as...").Window != nullptr);

        // The dialog's keys are its own: keyboard navigation is on while it is open, Down moves within it rather than
        // through the images behind it, and a shortcut it has no use for, H for a horizontal flip, stands down.
        ctx->Yield(2);
        IM_CHECK(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
        const bool flipped = *hdrview()->action("Flip horizontally").p_selected;
        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->KeyPress(ImGuiKey_H);
        ctx->Yield(2);
        IM_CHECK_EQ(hdrview()->current_image_index(), current);
        IM_CHECK_EQ(*hdrview()->action("Flip horizontally").p_selected, flipped);

        ctx->SetRef("Save as...");
        ctx->ItemClick("Cancel");

        ctx->SetRef("");
        wait_for_window_closed(ctx, "Save as...");
        IM_CHECK(ctx->WindowInfo("Save as...", ImGuiTestOpFlags_NoError).Window == nullptr);
        // and once it closes, the arrows are the app's again
        ctx->Yield(2);
        IM_CHECK((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) == 0);
    };

    // Selecting each format in turn is what runs every *_parameters_gui(): in both dialogs, since a rendition
    // gives EXR's options one RGBA group to list and adds a target display to every HDR encoding.
    for (const char *dialog : {"Save as...", "Export rendition..."})
    {
        t           = IM_REGISTER_TEST(engine, "dialogs",
                                       dialog[0] == 'S' ? "save_as_every_format_options_panel"
                                                        : "export_rendition_every_format_options_panel");
        t->TestFunc = [dialog](ImGuiTestContext *ctx)
        {
            if (hdrview()->num_images() == 0)
            {
                hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
                wait_for_loads(ctx);
            }
            IM_CHECK(hdrview()->num_images() > 0);

            const std::string menu = std::string("File/") + dialog, window = std::string("//") + dialog;
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick(menu.c_str());
            ctx->SetRef("");
            IM_CHECK(ctx->WindowInfo(dialog).Window != nullptr);

            // the format list is a BeginListBox(), whose child window carries a runtime ID-hash suffix
            // ("<dialog>/<hex>"), so the entries have no written-out path: gather them and click by id
            ImGuiTestItemList items;
            ctx->GatherItems(&items, window.c_str(), -1);
            ImVector<ImGuiID> format_ids;
            const std::string child = std::string(dialog) + "/";
            for (auto &item : items)
                if (item.Depth == 1 && strstr(item.Window->Name, child.c_str()) != nullptr)
                    format_ids.push_back(item.ID);
            // eight entries are present whatever the build enables (BMP, HDR, JPEG and PNG via stb, OpenEXR,
            // PFM, QOI, TGA); the rest are gated on their HDRVIEW_ENABLE_* option
            ctx->LogInfo("format list holds %d entries", format_ids.Size);
            IM_CHECK_GE(format_ids.Size, 8);

            for (ImGuiID id : format_ids)
            {
                ctx->ItemClick(id);
                ctx->Yield(3); // let the newly selected format's options panel draw
            }

            ctx->SetRef(dialog);
            ctx->ItemClick("Cancel");
            ctx->SetRef("");
            wait_for_window_closed(ctx, dialog);
            IM_CHECK(ctx->WindowInfo(dialog, ImGuiTestOpFlags_NoError).Window == nullptr);
        };
    }

    // What each kind of save writes: the image itself, a copy of the selection, or the view, unchanged when its
    // highlights pass through and clipped at SDR white when they are clipped there, in the image's own primaries or
    // those chosen, and with alpha only when the image has it.
    t           = IM_REGISTER_TEST(engine, "dialogs", "image_to_save_writes_what_each_mode_says");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (hdrview()->num_images() == 0)
        {
            hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
            wait_for_loads(ctx);
        }
        auto img = hdrview()->current_image();
        IM_CHECK(img != nullptr);
        auto *app = hdrview();

        // the image's own data, whole
        IM_CHECK(app->image_to_save(HDRViewApp::SaveMode_Data, false, 0.f) == img);

        // A lit, opaque pixel, for the selection check below: the fixture's corners are transparent black, which
        // would match a copy from the wrong place.
        const int2 lit = [&]
        {
            for (int y = 0; y < img->size().y; y += 7)
                for (int x = 0; x < img->size().x; x += 7)
                {
                    const int2   p = int2{x, y} + img->data_window.min;
                    const float4 v = app->pixel_value(p, true, 2);
                    if (v.w == 1.f && v.x + v.y + v.z > 0.5f)
                        return p;
                }
            return img->data_window.min;
        }();
        IM_CHECK(lit != img->data_window.min);

        // and under a selection, which keeps its place in the frame
        const Box2i roi{lit, lit + int2{3, 2}};
        app->set_selection(roi);
        auto part = app->image_to_save(HDRViewApp::SaveMode_Data, true, 0.f);
        IM_CHECK(part != nullptr && part->size() == roi.size() && part->data_window == roi);
        const int2 at = roi.min - img->data_window.min;
        for (int c = 0; c < (int)img->channels.size(); ++c)
            IM_CHECK_EQ(part->channels[c](0, 0), img->channels[c](at.x, at.y));
        app->set_selection(Box2i{});

        // A rendition two stops up in plain gamma, with highlights passed through whatever the saved settings chose,
        // is the exposed value at any target. Clipped at SDR white instead, it holds the premultiplied colors of the
        // fixture's partly transparent pixels to their alpha.
        bool       &roll_off = *app->action("Roll off highlights").p_selected;
        const float exposure = app->exposure_live(), gamma = app->gamma_live();
        const auto  tonemap = app->tonemap();
        const bool  clamp = app->clamp_to_LDR(), rolling = roll_off;
        app->exposure_live() = 2.f;
        app->gamma_live()    = 1.f;
        app->tonemap()       = Tonemap_Gamma;
        app->clamp_to_LDR()  = false;
        roll_off             = false;
        ctx->Yield(2); //< for update_rolloff() to see it

        auto check = [&](float target_ceiling, const Chromaticities *primaries = nullptr)
        {
            auto view = app->image_to_save(HDRViewApp::SaveMode_Rendition, false, target_ceiling, primaries);
            IM_CHECK(view != nullptr && view->size() == img->size());
            // in the image's own primaries, or those chosen for it, so what they convert back to is what the view
            // computes
            const auto want = primaries ? *primaries : img->chromaticities.value_or(Chromaticities{});
            IM_CHECK(view->chromaticities && view->chromaticities->red == want.red &&
                     view->chromaticities->green == want.green && view->chromaticities->blue == want.blue &&
                     view->chromaticities->white == want.white);
            // the worst error over the image, checked once: the engine logs every check, passing ones too
            const float limit = app->clamp_to_LDR() ? 1.f : std::numeric_limits<float>::infinity();
            float       worst = 0.f;
            for (int y = 0; y < view->size().y; ++y)
                for (int x = 0; x < view->size().x; ++x)
                {
                    const float4 raw   = app->pixel_value(int2{x, y} + img->data_window.min, true, 2);
                    const float  alpha = raw.w > 0.f ? raw.w : 1.f;
                    const float3 shown = mul(view->M_to_sRGB, float3{view->channels[0](x, y), view->channels[1](x, y),
                                                                     view->channels[2](x, y)});
                    for (int c = 0; c < 3; ++c)
                        worst = std::max(worst, std::abs(shown[c] - std::min(raw[c] * 4.f, limit * alpha)));
                }
            IM_CHECK_LE(worst, 1e-4f);
        };
        check(0.f);
        check(1.f); //< the target sets the metadata, not a clip
        const auto bt2020 = gamut_chromaticities(ColorGamut_BT2020_2100);
        check(1.f, &bt2020); //< in the primaries chosen for it
        app->clamp_to_LDR() = true;
        ctx->Yield(2);
        check(1.f); //< clipped at SDR white, whatever the target
        app->clamp_to_LDR() = false;
        ctx->Yield(2);

        // a wide-gamut image keeps its gamut
        const auto chromaticities = img->chromaticities;
        img->chromaticities       = gamut_chromaticities(ColorGamut_Display_P3_SMPTE432);
        img->compute_color_transform();
        check(1.f);
        img->chromaticities = chromaticities;
        img->compute_color_transform();

        // alpha only when the image has it: the fixture's group does, and would not as plain RGB
        auto      &group = img->groups[img->selected_group];
        const auto type  = group.type;
        IM_CHECK_EQ(app->image_to_save(HDRViewApp::SaveMode_Rendition, false, 0.f)->channels.size(), 4u);
        group.type = ChannelGroup::RGB_Channels;
        IM_CHECK_EQ(app->image_to_save(HDRViewApp::SaveMode_Rendition, false, 0.f)->channels.size(), 3u);
        group.type = type;

        app->exposure_live() = exposure;
        app->gamma_live()    = gamma;
        app->tonemap()       = tonemap;
        app->clamp_to_LDR()  = clamp;
        roll_off             = rolling;
    };
}
