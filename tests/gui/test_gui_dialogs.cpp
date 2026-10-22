/** \file test_gui_dialogs.cpp
    \author Wojciech Jarosz

    Opens and closes the File-menu dialogs, runs every save format's options panel, and checks what each kind of
    save writes.
*/

#include "app.h"
#include "colorspace.h"
#include "image.h"
#include "imageio/gainmap.h"
#include "imageio/image_loader.h"
#include "imageio/jxl.h"
#include "imageio/uhdr.h"
#include "test_gui_registry.h"
#include <hello_imgui/dpi_aware.h>

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "../test_asan.h"
#include "test_gui_support.h"

using namespace hdrview_test;

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
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

    // The save and edit dialogs are modal, but leave the viewport to be panned and zoomed around them, to look at
    // the image while choosing: the wheel zooms over the viewport and not over the dialog, and a drag begun in the
    // viewport pans it. The save dialogs also close from their title bar.
    struct Navigable
    {
        const char *menu, *dialog, *test;
        bool        closable;
    };
    for (const Navigable n :
         {Navigable{"File/Save as...", "Save as...", "save_as_lets_the_viewport_be_navigated", true},
          Navigable{"File/Export rendition...", "Export rendition...",
                    "export_rendition_lets_the_viewport_be_navigated", true},
          Navigable{"Edit/Channel mixer...", "Channel mixer...", "edit_dialog_lets_the_viewport_be_navigated", false}})
    {
        t           = IM_REGISTER_TEST(engine, "dialogs", n.test);
        t->TestFunc = [n](ImGuiTestContext *ctx)
        {
            const char *dialog = n.dialog;
            if (hdrview()->num_images() == 0)
            {
                hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
                wait_for_loads(ctx);
            }
            auto      *app  = hdrview();
            const auto zoom = app->zoom();

            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick(n.menu);
            ctx->SetRef("");
            ImGuiWindow *window = ctx->WindowInfo(dialog).Window;
            IM_CHECK(window != nullptr);

            // a corner of the viewport, which the centered dialog leaves clear
            const float2 corner = app->app_pos_at_vp_pos(float2{HelloImGui::EmSize(2.f)});
            IM_CHECK(!window->Rect().Contains(ImVec2{corner.x, corner.y}));

            ctx->MouseMoveToPos(ImVec2{corner.x, corner.y});
            ctx->MouseWheelY(1.f);
            ctx->Yield(2);
            IM_CHECK(app->zoom() != zoom);

            const float over_viewport = app->zoom();
            ctx->MouseMoveToPos(window->Rect().GetCenter());
            ctx->MouseWheelY(1.f);
            ctx->Yield(2);
            IM_CHECK_EQ(app->zoom(), over_viewport);

            // a drag from the corner moves the pixel under it along
            const float2 before = app->pixel_at_app_pos(corner);
            ctx->MouseMoveToPos(ImVec2{corner.x, corner.y});
            ctx->MouseDown(ImGuiMouseButton_Left);
            ctx->MouseMoveToPos(ImVec2{corner.x + HelloImGui::EmSize(4.f), corner.y + HelloImGui::EmSize(3.f)});
            ctx->MouseUp(ImGuiMouseButton_Left);
            ctx->Yield(2);
            IM_CHECK(la::length(app->pixel_at_app_pos(corner) - before) > 0.f);

            // and a drag begun on the dialog's own widgets is theirs, even carried out over the viewport: a press on
            // Cancel, released away from it, which leaves it unclicked
            const float2 held = app->pixel_at_app_pos(corner);
            ctx->MouseMoveToPos(ctx->ItemInfo((std::string("//") + dialog + "/Cancel").c_str()).RectFull.GetCenter());
            ctx->MouseDown(ImGuiMouseButton_Left);
            ctx->MouseMoveToPos(ImVec2{corner.x, corner.y});
            ctx->MouseMoveToPos(ImVec2{corner.x + HelloImGui::EmSize(4.f), corner.y + HelloImGui::EmSize(3.f)});
            ctx->MouseUp(ImGuiMouseButton_Left);
            ctx->Yield(2);
            IM_CHECK_EQ(la::length(app->pixel_at_app_pos(corner) - held), 0.f);

            // its title bar's arrow folds it to the title bar, out of the way, and back
            const float full = window->Size.y;
            ctx->ItemClick((std::string("//") + dialog + "/#FOLD").c_str());
            ctx->Yield(3);
            // and the sliver Dear ImGui keeps below a title bar for the rounding
            IM_CHECK_LE(window->Size.y,
                        window->TitleBarHeight + std::max(0.f, ImGui::GetStyle().WindowRounding - 1.f) + 1.f);
            const float folded_zoom = app->zoom();
            ctx->MouseMoveToPos(ImVec2{corner.x, corner.y});
            ctx->MouseWheelY(1.f);
            ctx->Yield(2);
            IM_CHECK(app->zoom() != folded_zoom); //< still navigable folded
            ctx->ItemClick((std::string("//") + dialog + "/#FOLD").c_str());
            ctx->Yield(3);
            IM_CHECK_EQ(window->Size.y, full);

            // the arrow sits beside the close button, which closes the dialog as Cancel does
            const std::string bar = std::string("//") + dialog + "/";
            if (n.closable)
            {
                IM_CHECK(!ctx->ItemInfo((bar + "#FOLD").c_str())
                              .RectFull.Overlaps(ctx->ItemInfo((bar + "#CLOSE").c_str()).RectFull));
                ctx->ItemClick((bar + "#CLOSE").c_str());
            }
            else
                ctx->ItemClick((bar + "Cancel").c_str());
            wait_for_window_closed(ctx, dialog);
            IM_CHECK(ctx->WindowInfo(dialog, ImGuiTestOpFlags_NoError).Window == nullptr);
            app->set_zoom(zoom);
        };
    }

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
            bool              lists_gainmap_format = false;
            for (auto &item : items)
                if (item.Depth == 1 && strstr(item.Window->Name, child.c_str()) != nullptr)
                {
                    format_ids.push_back(item.ID);
                    lists_gainmap_format |= strcmp(item.DebugLabel, "JPEG (UltraHDR)") == 0;
                }
#if HDRVIEW_ENABLE_LIBUHDR
            // a gain map is two renditions of the view, so only Export rendition offers one
            IM_CHECK(lists_gainmap_format == (dialog[0] == 'E'));
#endif
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

    // What Export rendition writes for a gain-map format, through the dialog the way a user saves: the file reads
    // back as the view clipped at SDR white with no headroom, and as the view itself in full. The view is two stops
    // up with highlights passing through, so the two renditions differ wherever the fixture is brighter than a
    // quarter of white.
    struct GainmapCase
    {
        const char *format, *extension;
        const char *gain_map; ///< the JPEG XL option to pick, or none for a format that always writes one
    };
    std::vector<GainmapCase> cases;
#if HDRVIEW_ENABLE_LIBUHDR
    cases.push_back({"JPEG (UltraHDR)", ".jpg", nullptr});
#endif
#if HDRVIEW_ENABLE_LIBJXL && !defined(HDRVIEW_TEST_ASAN) // see test_asan.h
    cases.push_back({"JPEG-XL", ".jxl", "SDR base"});
    cases.push_back({"JPEG-XL", ".jxl", "HDR base"});
#endif
    for (const auto &gc : cases)
    {
        const std::string name = std::string("export_rendition_writes_a_gain_map_") + gc.extension + "_" +
                                 (gc.gain_map ? (gc.gain_map[0] == 'S' ? "sdr_base" : "hdr_base") : "sdr_base");
        t                      = IM_REGISTER_TEST(engine, "dialogs", strdup(name.c_str()));
        t->TestFunc            = [gc](ImGuiTestContext *ctx)
        {
            if (hdrview()->num_images() == 0)
            {
                hdrview()->load_images({HDRVIEW_GUI_TEST_IMAGE});
                wait_for_loads(ctx);
            }
            auto *app = hdrview();
            auto  img = app->current_image();
            IM_CHECK(img != nullptr);

            bool       &roll_off = *app->action("Roll off highlights").p_selected;
            const float exposure = app->exposure_live(), gamma = app->gamma_live();
            const auto  tonemap = app->tonemap();
            const bool  clamp = app->clamp_to_LDR(), rolling = roll_off;
            // a failed check returns early, so the view and the test hooks are put back however the test ends
            struct Restore
            {
                std::function<void()> undo;
                ~Restore() { undo(); }
            } restore{[&, exposure, gamma, tonemap, clamp, rolling]
                      {
                          app->test_save_path().clear();
#if HDRVIEW_ENABLE_LIBJXL
                          jxl_gain_map_option() = 0;
#endif
                          app->exposure_live() = exposure;
                          app->gamma_live()    = gamma;
                          app->tonemap()       = tonemap;
                          app->clamp_to_LDR()  = clamp;
                          roll_off             = rolling;
                      }};
            app->exposure_live() = 2.f;
            app->gamma_live()    = 1.f;
            app->tonemap()       = Tonemap_Gamma;
            app->clamp_to_LDR()  = false;
            roll_off             = false;
            ctx->Yield(2); //< for update_rolloff() to see it

            const auto path =
                std::filesystem::temp_directory_path() / (std::string("hdrview_gui_gainmap") + gc.extension);
            std::filesystem::remove(path);
            app->test_save_path() = path.u8string();

            const char *dialog = "Export rendition...";
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("File/Export rendition...");
            ctx->SetRef("");
            IM_CHECK(ctx->WindowInfo(dialog).Window != nullptr);

            // the format, by its entry in the list, whose child window has a runtime-hashed name
            const auto find = [&](const char *label, const char *window_part) -> ImGuiID
            {
                ImGuiTestItemList items;
                ctx->GatherItems(&items, "//Export rendition...", -1);
                for (auto &item : items)
                    if (strcmp(item.DebugLabel, label) == 0 && strstr(item.Window->Name, window_part) != nullptr)
                        return item.ID;
                return 0;
            };
            const ImGuiID format = find(gc.format, "Export rendition.../");
            IM_CHECK(format != 0);
            ctx->ItemClick(format);
            ctx->Yield(3);
            if (gc.gain_map)
            {
                // set directly: the test engine does not register the widgets of the options' property rows
                jxl_gain_map_option() = gc.gain_map[0] == 'S' ? 1 : 2;
                ctx->Yield(3);
            }

            ctx->SetRef(dialog);
            ctx->ItemClick((std::string("Save as ") + gc.format + "...").c_str());
            ctx->SetRef("");
            wait_for_window_closed(ctx, dialog);
            app->test_save_path().clear();
            IM_CHECK(std::filesystem::exists(path));

            const auto decode = [&](float headroom) -> ImagePtr
            {
                std::ifstream    is{path, std::ios::binary};
                ImageLoadOptions opts;
                opts.gainmap_headroom = headroom;
                auto images           = gc.extension[2] == 'x' ? load_jxl_image(is, path.u8string(), opts)
                                                               : load_uhdr_image(is, path.u8string(), opts);
                return images.empty() ? nullptr : images.front();
            };
            const auto none = decode(0.f), full = decode(k_full_gainmap_headroom);
            IM_CHECK(none && full && full->size() == img->size());

            // The full decode against the view's own rendition, and the no-headroom one against it clipped at SDR
            // white, in the image's primaries, which the file was written in and an SDR base clips in, at the
            // fixture's opaque pixels: in stops where the view is bright and in sRGB code values where it is dim. The
            // 99th percentile, since a lossy codec always leaves some outliers (JPEG's 4:2:0 chroma most, on saturated
            // colors); a missing or one-sided map is off by the two stops the view was raised across all the
            // highlights.
            const auto         view = app->image_to_save(HDRViewApp::SaveMode_Rendition, false, 0.f);
            std::vector<float> stops, codes;
            for (int y = 0; y < img->size().y; ++y)
                for (int x = 0; x < img->size().x; ++x)
                {
                    if (view->channels.size() > 3 && view->channels[3](x, y) < 1.f)
                        continue;
                    for (int c = 0; c < 3; ++c)
                    {
                        const float want = view->channels[c](x, y), hi = full->channels[c](x, y),
                                    lo = none->channels[c](x, y);
                        if (want >= 0.25f)
                            stops.push_back(std::abs(std::log2(std::max(hi, 1e-6f) / want)));
                        else
                            codes.push_back(std::abs(linear_to_sRGB(hi) - linear_to_sRGB(want)));
                        codes.push_back(std::abs(linear_to_sRGB(lo) - linear_to_sRGB(std::min(want, 1.f))));
                    }
                }
            const auto quantile = [](std::vector<float> v, float q)
            {
                if (v.empty())
                    return 0.f;
                std::nth_element(v.begin(), v.begin() + size_t(q * (v.size() - 1)), v.end());
                return v[size_t(q * (v.size() - 1))];
            };
            ctx->LogInfo("%s %s: stops 99%% %.3f worst %.3f; code values 99%% %.4f worst %.4f", gc.format,
                         gc.gain_map ? gc.gain_map : "", quantile(stops, 0.99f), quantile(stops, 1.f),
                         quantile(codes, 0.99f), quantile(codes, 1.f));
            IM_CHECK_LE(quantile(stops, 0.99f), 0.5f);
            IM_CHECK_LE(quantile(codes, 0.99f), 0.1f);

            std::filesystem::remove(path);
        };
    }
}
