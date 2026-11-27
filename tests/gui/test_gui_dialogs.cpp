/** \file test_gui_dialogs.cpp
    \author Wojciech Jarosz

    Opens and closes the File-menu dialogs, checks that the save and edit dialogs leave the viewport navigable,
    runs every save format's options panel, checks what each kind of save writes, and that every request to close
    or quit asks before discarding unsaved edits.
*/

#include "app.h"
#include "colorspace.h"
#include "image.h"
#include "imageio/gainmap.h"
#include "imageio/image_loader.h"
#include "imageio/jxl.h"
#include "imageio/uhdr.h"
#include "test_gui_registry.h"
#include <GLFW/glfw3.h>
#include <hello_imgui/dpi_aware.h>
#include <hello_imgui/hello_imgui.h>

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

namespace
{

/// Shows the view two stops up in plain gamma, highlights passed through, and puts it back however the test ends.
struct TwoStopsUp
{
    HDRViewApp *app      = hdrview();
    bool       &roll_off = *app->action("Roll off highlights").p_selected;
    float       exposure = app->exposure_live(), gamma = app->gamma_live();
    Tonemap_    tonemap = app->tonemap();
    bool        clamp = app->clamp_to_LDR(), rolling = roll_off;

    explicit TwoStopsUp(ImGuiTestContext *ctx)
    {
        app->exposure_live() = 2.f;
        app->gamma_live()    = 1.f;
        app->tonemap()       = Tonemap_Gamma;
        app->clamp_to_LDR()  = false;
        roll_off             = false;
        ctx->Yield(2); //< for update_rolloff() to see it
    }
    ~TwoStopsUp()
    {
        app->exposure_live() = exposure;
        app->gamma_live()    = gamma;
        app->tonemap()       = tonemap;
        app->clamp_to_LDR()  = clamp;
        roll_off             = rolling;
    }
};

} // namespace

void RegisterTests_Dialogs(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "dialogs", "image_loading_options_open_close");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("File/Image loading options...");
        ctx->SetRef("");
        IM_CHECK(ctx->WindowInfo("Image loading options...").Window != nullptr);
        ctx->ItemClick("//Image loading options.../OK");
        IM_CHECK(window_closed(ctx, "Image loading options..."));
    };

    // The save and edit dialogs are modal, but leave the viewport to be panned and zoomed around them, to look at
    // the image while choosing: the wheel zooms over the viewport and not over the dialog, and a drag begun in the
    // viewport pans it. Their keys are their own, and all of them close from their title bar. Export rendition is
    // the same dialog as Save as.
    struct Navigable
    {
        std::string menu;
        const char *dialog, *test;
        bool        edit; ///< An edit dialog, with an edit's footer
    };
    for (const Navigable &n :
         {Navigable{"File/Save as...", "Save as...", "save_as_lets_the_viewport_be_navigated", false},
          Navigable{edit_menu_item("Channel mixer..."), "Channel mixer...",
                    "edit_dialog_lets_the_viewport_be_navigated", true}})
    {
        t           = IM_REGISTER_TEST(engine, "dialogs", n.test);
        t->TestFunc = [n](ImGuiTestContext *ctx)
        {
            const char *dialog = n.dialog;
            // two images, so a Down that reached the shortcuts behind the dialog would change the current one
            if (!load_fixture(ctx, 2))
                return;
            auto             *app = hdrview();
            const std::string at  = std::string("//") + dialog + "/";
            // the view as it was, pan and all, since the drags below move it
            const KeepView view;
            OnExit         deselect{[app] { app->set_selection(Box2i{}); }};
            app->set_selection(Box2i{});

            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick(n.menu.c_str());
            ctx->SetRef("");
            ImGuiWindow *window = ctx->WindowInfo(dialog).Window;
            IM_CHECK(window != nullptr);

            // Keyboard navigation is on while it is open, Down moves within it rather than through the images
            // behind it, and a shortcut it has no use for, H to mirror the view, stands down.
            const int current = app->current_image_index();
            ctx->Yield(2);
            IM_CHECK(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
            const bool flipped = *app->action("Mirror view horizontally").p_selected;
            ctx->KeyPress(ImGuiKey_DownArrow);
            ctx->KeyPress(ImGuiKey_H);
            ctx->Yield(2);
            IM_CHECK_EQ(app->current_image_index(), current);
            IM_CHECK_EQ(*app->action("Mirror view horizontally").p_selected, flipped);

            // a corner of the viewport, which the centered dialog leaves clear
            const float2 corner = app->app_pos_at_vp_pos(float2{HelloImGui::EmSize(2.f)});
            IM_CHECK(!window->Rect().Contains(ImVec2{corner.x, corner.y}));

            // An edit dialog's footer: "Selection only" only while there is a selection, and on one row with
            // Preview and the buttons.
            if (n.edit)
            {
                IM_CHECK(!ctx->ItemExists((at + "Selection only").c_str()));
                app->set_selection(Box2i{int2{0}, int2{4}});
                ctx->Yield(2);
                const float row = ctx->ItemInfo((at + "Cancel").c_str()).RectFull.GetCenter().y;
                for (const char *item : {"Selection only", "Preview"})
                    IM_CHECK_EQ(ctx->ItemInfo((at + item).c_str()).RectFull.GetCenter().y, row);
                app->set_selection(Box2i{});
                ctx->Yield(2);
            }

            const float zoom = app->zoom();
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
            drag(ctx, corner, float2{HelloImGui::EmSize(4.f), HelloImGui::EmSize(3.f)});
            IM_CHECK(la::length(app->pixel_at_app_pos(corner) - before) > 0.f);

            // and a drag begun on the dialog's own widgets is theirs, even carried out over the viewport: a press on
            // Cancel, released away from it, which leaves it unclicked
            const float2 held = app->pixel_at_app_pos(corner);
            ctx->MouseMoveToPos(ctx->ItemInfo((at + "Cancel").c_str()).RectFull.GetCenter());
            ctx->MouseDown(ImGuiMouseButton_Left);
            ctx->MouseMoveToPos(ImVec2{corner.x, corner.y});
            ctx->MouseMoveToPos(ImVec2{corner.x + HelloImGui::EmSize(4.f), corner.y + HelloImGui::EmSize(3.f)});
            ctx->MouseUp(ImGuiMouseButton_Left);
            ctx->Yield(2);
            IM_CHECK_EQ(la::length(app->pixel_at_app_pos(corner) - held), 0.f);

            // its title bar's arrow folds it to the title bar, out of the way and as wide as it was, and back
            const float full = window->Size.y, width = window->Size.x;
            ctx->ItemClick((at + "#FOLD").c_str());
            ctx->Yield(3);
            IM_CHECK_EQ(window->Size.x, width);
            // the title bar, plus the sliver Dear ImGui keeps below one for the rounding
            IM_CHECK_LE(window->Size.y,
                        window->TitleBarHeight + std::max(0.f, ImGui::GetStyle().WindowRounding - 1.f) + 1.f);
            const float folded_zoom = app->zoom();
            ctx->MouseMoveToPos(ImVec2{corner.x, corner.y});
            ctx->MouseWheelY(1.f);
            ctx->Yield(2);
            IM_CHECK(app->zoom() != folded_zoom); //< still navigable folded
            ctx->ItemClick((at + "#FOLD").c_str());
            ctx->Yield(3);
            IM_CHECK_EQ(window->Size.y, full);

            // the arrow sits beside the close button, which closes the dialog as Cancel does
            IM_CHECK(!ctx->ItemInfo((at + "#FOLD").c_str())
                          .RectFull.Overlaps(ctx->ItemInfo((at + "#CLOSE").c_str()).RectFull));
            ctx->ItemClick((at + "#CLOSE").c_str());
            IM_CHECK(window_closed(ctx, dialog));
            // and once it closes, the arrows are the app's again
            ctx->Yield(2);
            IM_CHECK((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) == 0);
        };
    }

    // Selecting each format in turn is what runs every *_parameters_gui(): in both dialogs, since a rendition
    // gives EXR's options one RGBA group to list and adds a target display to every HDR encoding.
    for (const char *dialog : {"Save as...", "Export rendition..."})
    {
        t           = IM_REGISTER_TEST(engine, "dialogs",
                                       dialog[0] == 'S' ? "save_as_every_format_options_panel"
                                                        : "export_rendition_every_format_options_panel");
        t->TestFunc = [dialog](ImGuiTestContext *ctx)
        {
            if (!load_fixture(ctx))
                return;

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
            const std::string child                = std::string(dialog) + "/";
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
                ctx->Yield(); // the newly selected format's options panel draws
            }

            ctx->SetRef(dialog);
            ctx->ItemClick("Cancel");
            ctx->SetRef("");
            IM_CHECK(window_closed(ctx, dialog));
        };
    }

    // What each kind of save writes: the image itself, a copy of the selection, or the view, unchanged when its
    // highlights pass through and clipped at SDR white when they are clipped there, in the image's own primaries or
    // those chosen, and with alpha only when the image has it.
    t           = IM_REGISTER_TEST(engine, "dialogs", "image_to_save_writes_what_each_mode_says");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;
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
        const TwoStopsUp view{ctx};

        // Each check compares with the image's own stored samples: two stops up is four times them, in the image's
        // own primaries, or converted by \p M into those chosen for it; clipped, each channel stops at the pixel's
        // alpha, the premultiplied form of SDR white.
        auto check = [&](float target_ceiling, const Chromaticities *primaries = nullptr,
                         const float3x3 &M = float3x3{la::identity})
        {
            auto view = app->image_to_save(HDRViewApp::SaveMode_Rendition, false, target_ceiling, primaries);
            IM_CHECK(view != nullptr && view->size() == img->size());
            const auto want = primaries ? *primaries : img->chromaticities.value_or(Chromaticities{});
            IM_CHECK(view->chromaticities && view->chromaticities->red == want.red &&
                     view->chromaticities->green == want.green && view->chromaticities->blue == want.blue &&
                     view->chromaticities->white == want.white);
            // the worst error over the image, checked once: the engine logs every check, passing ones too
            const bool clipped = app->clamp_to_LDR();
            float      worst   = 0.f;
            for (int y = 0; y < view->size().y; ++y)
                for (int x = 0; x < view->size().x; ++x)
                {
                    const float alpha = img->channels[3](x, y);
                    float3      v;
                    for (int c = 0; c < 3; ++c)
                    {
                        v[c] = 4.f * img->channels[size_t(c)](x, y);
                        if (clipped)
                            v[c] = std::min(v[c], alpha);
                    }
                    v = mul(M, v);
                    for (int c = 0; c < 3; ++c)
                        worst = std::max(worst, std::abs(view->channels[size_t(c)](x, y) - v[c]));
                }
            IM_CHECK_LE(worst, 1e-4f);
        };
        check(0.f);
        check(1.f); //< the target sets the metadata, not a clip
        // in the primaries chosen for it, by the BT.709-to-BT.2020 matrix BT.2087 publishes
        const auto     bt2020 = gamut_chromaticities(ColorGamut_BT2020_2100);
        const float3x3 bt709_to_bt2020{{0.6274040f, 0.0690970f, 0.0163916f},
                                       {0.3292820f, 0.9195400f, 0.0880132f},
                                       {0.0433136f, 0.0113612f, 0.8955950f}};
        check(1.f, &bt2020, bt709_to_bt2020);
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
    };

    // What Export rendition writes for a gain-map format, through the dialog the way a user saves: the file reads
    // back as the view clipped at SDR white with no headroom, and as the view itself in full. The view is two stops
    // up with highlights passing through, so the two renditions differ wherever the fixture is brighter than a
    // quarter of white. One format suffices for the dialog building the SDR base beside the HDR rendition;
    // test_gainmap.cpp reads back every gain-map writer, either way round.
    struct GainmapCase
    {
        const char *format, *extension;
        const char *gain_map; ///< the JPEG XL option to pick, or none for a format that always writes one
    };
    std::vector<GainmapCase> cases;
#if HDRVIEW_ENABLE_LIBUHDR
    cases.push_back({"JPEG (UltraHDR)", ".jpg", nullptr});
#elif HDRVIEW_ENABLE_LIBJXL && !defined(HDRVIEW_TEST_ASAN) // see test_asan.h
    cases.push_back({"JPEG-XL", ".jxl", "SDR base"});
#endif
    for (const auto &gc : cases)
    {
        const std::string name = std::string("export_rendition_writes_a_gain_map_") + gc.extension + "_sdr_base";
        t                      = IM_REGISTER_TEST(engine, "dialogs", strdup(name.c_str()));
        t->TestFunc            = [gc](ImGuiTestContext *ctx)
        {
            if (!load_fixture(ctx))
                return;
            auto *app = hdrview();
            auto  img = app->current_image();
            IM_CHECK(img != nullptr);

            const TwoStopsUp view{ctx};
            // the test hooks, put back however the test ends
            OnExit restore{[app]
                           {
                               app->test_save_path().clear();
#if HDRVIEW_ENABLE_LIBJXL
                               jxl_gain_map_option() = 0;
#endif
                           }};

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
            IM_CHECK(window_closed(ctx, dialog));
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

            // The full decode against the view two stops up, four times the stored samples, and the no-headroom one
            // against that clipped at SDR white, in the image's primaries, which the file was written in and an SDR
            // base clips in, at the fixture's opaque pixels: in stops where the view is bright and in sRGB code values
            // where it is dim. The 99th percentile, since a lossy codec always leaves some outliers (JPEG's 4:2:0
            // chroma most, on saturated colors); a missing or one-sided map is off by the two stops the view was raised
            // across all the highlights.
            std::vector<float> stops, codes;
            for (int y = 0; y < img->size().y; ++y)
                for (int x = 0; x < img->size().x; ++x)
                {
                    if (img->channels.size() > 3 && img->channels[3](x, y) < 1.f)
                        continue;
                    for (int c = 0; c < 3; ++c)
                    {
                        const float want = 4.f * img->channels[size_t(c)](x, y), hi = full->channels[c](x, y),
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
            // about twice what the codec's loss leaves, so an exposure half a stop off fails too
            IM_CHECK_LE(quantile(stops, 0.99f), 0.25f);
            IM_CHECK_LE(quantile(codes, 0.99f), 0.05f);

            std::filesystem::remove(path);
        };
    }

    // Each of the reset dialog's groups returns its own settings to their defaults and leaves every other setting
    // alone. Every setting is changed first, which also holds apply_user_settings() to reading back each one that
    // is saved; the rolloff's enums, saved as strings, are read back in test_settings_json.cpp instead.
    t           = IM_REGISTER_TEST(engine, "dialogs", "reset_settings_restores_the_chosen_groups");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const char *const groups[] = {"Viewing preferences", "Theme", "Window layout", "Recently opened files"};
        // the index in groups[] of the one holding \p key, as the dialog's tooltips describe them
        const auto group_of = [](const std::string &key)
        {
            if (key == "/theme" || key == "/font size")
                return 1;
            if (key == "/status bar" || key.find("tool palette") != std::string::npos)
                return 2;
            return key == "/recent files" ? 3 : 0;
        };
        const char *const dialog = "//Reset settings";
        const auto        reset  = [&](unsigned chosen)
        {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("Windows/Reset settings...");
            ctx->SetRef(dialog);
            for (int g = 0; g < 4; ++g)
                chosen & (1u << g) ? ctx->ItemCheck(groups[g]) : ctx->ItemUncheck(groups[g]);
            ctx->ItemClick("Reset");
            ctx->SetRef("");
            IM_CHECK(window_closed(ctx, dialog));
        };
        auto      &status_bar = HelloImGui::GetRunnerParams()->imGuiWindowParams.showStatusBar;
        // the numbers and toggles by JSON pointer, and the theme, font size, recent files and status bar
        const auto settings = [&]
        {
            const json j = hdrview()->user_settings(), flat = j.flatten();
            json       kept = json::object();
            for (auto it = flat.begin(); it != flat.end(); ++it)
                if (it->is_number() || it->is_boolean())
                    kept[it.key()] = *it;
            kept["/theme"]        = j["theme"];
            kept["/font size"]    = ImGui::GetStyle().FontSizeBase;
            kept["/recent files"] = j["recent files"];
            kept["/status bar"]   = status_bar;
            return kept;
        };

        reset(~0u);
        const json defaults = settings();

        json j = defaults;
        for (const char *by_hand : {"/theme", "/font size", "/recent files", "/status bar"}) j.erase(by_hand);
        for (auto &v : j)
            if (v.is_boolean())
                v = !v.get<bool>();
            else if (v.is_number_integer())
                v = v.get<int>() == 0 ? 1 : v.get<int>() - 1; //< within the range of anything with two choices
            else
                v = v.get<double>() * 0.5 + 0.25;
        j                 = j.unflatten();
        j["recent files"] = json::array({"reset_settings_test.exr"});
        const auto change = [&]
        {
            hdrview()->apply_user_settings(j);
            status_bar = !defaults["/status bar"].get<bool>();
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("Windows/Theme/HDRView light");
            ctx->SetRef("");
            // as the theme tweak window's font size does; a size set directly is overwritten within the frame
            ImGui::GetStyle()._NextFrameFontSizeBase = defaults["/font size"].get<float>() + 3.f;
            ctx->Yield(2);
        };

        change();
        const json changed = settings();
        for (auto it = defaults.begin(); it != defaults.end(); ++it)
            if (changed[it.key()] == *it)
                IM_ERRORF("%s kept its default", it.key().c_str());

        for (int g = 0; g < 4; ++g)
        {
            change();
            reset(1u << g);
            const json now = settings();
            for (auto it = defaults.begin(); it != defaults.end(); ++it)
                if (now[it.key()] != (group_of(it.key()) == g ? *it : changed[it.key()]))
                    IM_ERRORF("resetting %s left %s at %s", groups[g], it.key().c_str(), now[it.key()].dump().c_str());
        }
        reset(~0u); //< later tests start from the defaults
    };

    // Every request that would discard unsaved edits asks first, and cancelling it leaves everything as it was:
    // closing the image, closing them all, the Quit command, and the window's close button, which GLFW reports to
    // Hello ImGui's ConfirmExit, as it does Cmd-Q and Alt-F4.
    t           = IM_REGISTER_TEST(engine, "dialogs", "unsaved_edits_prompt_before_closing_or_quitting");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        if (!load_fixture(ctx))
            return;

        // with nothing unsaved, a request to quit goes ahead without a prompt; asking the callback, rather than
        // requesting a close, keeps the app running to report it
        const char *const prompt = "//Discard unsaved changes?";
        IM_CHECK(!hdrview()->any_image_modified());
        IM_CHECK(HelloImGui::GetRunnerParams()->callbacks.ConfirmExit());
        ctx->Yield(2);
        IM_CHECK(ctx->WindowInfo(prompt, ImGuiTestOpFlags_NoError).Window == nullptr);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick(edit_menu_item("Flip image horizontally").c_str());
        ctx->SetRef("");
        IM_CHECK(wait_until(ctx, [] { return hdrview()->any_image_modified(); }));

        auto *const window  = (GLFWwindow *)HelloImGui::GetRunnerParams()->backendPointers.glfwWindow;
        const auto  exiting = [] { return HelloImGui::GetRunnerParams()->appShallExit; };

        const std::pair<const char *, std::function<void()>> requests[] = {
            {"Close", [] { hdrview()->action("Close").callback(); }},
            {"Close all", [] { hdrview()->action("Close all").callback(); }},
            {"Quit", [] { hdrview()->action("Quit").callback(); }},
            // glfwSetWindowShouldClose() may be called from any thread, which the window's own button reaches too
            {"the window's close button", [window] { glfwSetWindowShouldClose(window, GLFW_TRUE); }},
        };
        for (const auto &[name, request] : requests)
        {
            ctx->LogInfo("Requesting %s", name);
            request();
            IM_CHECK(
                wait_until(ctx, [&] { return ctx->WindowInfo(prompt, ImGuiTestOpFlags_NoError).Window != nullptr; }));
            IM_CHECK(!exiting());
            IM_CHECK_EQ(hdrview()->num_images(), 1);

            ctx->SetRef(prompt);
            ctx->ItemClick("Cancel");
            ctx->SetRef("");
            IM_CHECK(window_closed(ctx, prompt));
            ctx->Yield(2);
            IM_CHECK(!exiting());
            IM_CHECK(!glfwWindowShouldClose(window));
            IM_CHECK_EQ(hdrview()->num_images(), 1);
            IM_CHECK(hdrview()->any_image_modified());
        }

        reset_images(ctx);
    };
}
