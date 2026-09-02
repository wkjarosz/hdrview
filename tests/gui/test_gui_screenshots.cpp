/** \file test_gui_screenshots.cpp
    \author Wojciech Jarosz

    Authoring tool, not a regression test: drives the app into each state the README illustrates and saves
    a PNG of the frame. Registers nothing without HDRVIEW_SCREENSHOT_DIR, so an ordinary ctest run never
    sees it; resources/regenerate-screenshots.sh is the entry point. Capture goes through hello_imgui's
    test-engine ScreenCaptureFunc, which it installs only under `#ifdef HELLOIMGUI_HAS_OPENGL`, so this
    works on the OpenGL backends (Linux, Windows) and not on macOS's Metal one.

    The subjects come from HDRVIEW_SCREENSHOT_IMAGES (a `:`-separated list of files and directories, first
    entry the one the shots are of) and HDRVIEW_SCREENSHOT_DIFF_IMAGE (the multi-view file the comparison
    shot differences), falling back to what is in the tree.
*/

#include "app.h"
#include "image.h"
#include "imgui_ext.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace hdrview_test;

namespace fs = std::filesystem;

namespace
{

fs::path output_dir() { return fs::path(getenv("HDRVIEW_SCREENSHOT_DIR")); }

//! Everything one entry of a `:`-separated path list refers to: a file as itself, a directory as its
//! loadable contents in filename order.
void expand_into(const fs::path &p, std::vector<std::string> &out)
{
    std::error_code ec;
    if (fs::is_directory(p, ec))
    {
        std::vector<std::string> found;
        for (const auto &entry : fs::directory_iterator(p, ec))
            if (entry.is_regular_file(ec) && Image::loadable(entry.path().extension().string()))
                found.push_back(entry.path().string());
        std::sort(found.begin(), found.end());
        out.insert(out.end(), found.begin(), found.end());
    }
    else if (fs::is_regular_file(p, ec))
        out.push_back(p.string());
}

//! Splits a `:`-separated list of paths, in the order given.
std::vector<std::string> path_list(const char *value)
{
    std::vector<std::string> v;
    for (std::string rest = value ? value : ""; !rest.empty();)
    {
        const auto  sep  = rest.find(':');
        std::string head = rest.substr(0, sep);
        rest             = sep == std::string::npos ? "" : rest.substr(sep + 1);
        if (!head.empty())
            expand_into(fs::path(head), v);
    }
    return v;
}

//! Every image the shots load, hero subject first.
/*!
    HDRVIEW_SCREENSHOT_IMAGES is a `:`-separated list of files and directories in the order given: the first
    entry is what most shots show in the viewport, and the rest fill the Images panel. Failing that, what is
    in the tree: the multi-part Beachball if this build fetched OpenEXR's test images, since eight parts
    fill the panel, then the gamma chart and the app icon, which are always present.
*/
const std::vector<std::string> &subjects()
{
    static const std::vector<std::string> paths = []
    {
        auto v = path_list(getenv("HDRVIEW_SCREENSHOT_IMAGES"));
        if (v.empty())
        {
#if defined(HDRVIEW_TEST_OPENEXR_DIR)
            v.push_back((fs::path(HDRVIEW_TEST_OPENEXR_DIR) / "multipart.0001.exr").string());
#endif
            v.push_back((fs::path(HDRVIEW_SCREENSHOT_FIXTURE_DIR) / "gamma-grid.exr").string());
            v.push_back(HDRVIEW_GUI_TEST_IMAGE);
        }
        return v;
    }();
    return paths;
}

//! The file whose two views the comparison shot differences, or empty to look among the subjects.
std::string diff_subject()
{
    auto v = path_list(getenv("HDRVIEW_SCREENSHOT_DIFF_IMAGE"));
    return v.empty() ? std::string{} : v.front();
}

//! The image the command-palette shot sits over, or empty to use the same subject as everything else.
std::string palette_subject()
{
    auto v = path_list(getenv("HDRVIEW_SCREENSHOT_PALETTE_IMAGE"));
    return v.empty() ? std::string{} : v.front();
}

//! Saves the whole frame to `<HDRVIEW_SCREENSHOT_DIR>/<name>.png`.
//! Adds no capture windows: with none, the capture tool takes the main viewport's rect as-is, where naming
//! windows would hide every window not named and shuffle the rest together.
void capture(ImGuiTestContext *ctx, const char *name)
{
    // the statistics panel is in every one of these pictures, and its numbers are computed off the main
    // thread: photographed mid-recompute it shows the inf/-inf it holds before any pixel has been counted
    if (auto img = hdrview()->current_image())
    {
        auto &group = img->groups[img->selected_group];
        for (int c = 0; c < group.num_channels; ++c)
            wait_for_stats(ctx, img->channels[group.channels[c]],
                           [](const PixelStats *s) { return s->settings.roi == hdrview()->roi(); });
    }

    // let the frame settle before the framebuffer is read back: a texture uploaded this frame, a panel that
    // resized, a popup still opening
    ctx->Yield(4);

    const std::string file = (output_dir() / (std::string(name) + ".png")).string();
    ctx->CaptureReset();
    // CaptureSetFilename() is compiled only for the Python bindings, so the path goes into the args
    ImStrncpy(ctx->CaptureArgs->InOutputFile, file.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
    IM_CHECK(ctx->CaptureScreenshot(ImGuiCaptureFlags_HideMouseCursor));
}

//! Starts routing log messages to the Log window, which one of the shots is of.
//! hdrview.cpp's main() normally installs this sink and the GUI suite has its own main(), so without it the
//! panel photographs empty. Installed this late because the tests ahead of these shots open damaged and
//! missing files, and their errors are not what a screenshot of the Log window should show.
void install_log_sink()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    spdlog::set_level(spdlog::level::info);
    spdlog::default_logger()->set_level(spdlog::level::info);
    spdlog::set_pattern("%^[%T | %l %&]: %$%v");
    spdlog::default_logger()->sinks().push_back(ImGui::GlobalSpdLogWindow().sink());
    ImGui::GlobalSpdLogWindow().set_pattern("%^%*[%T | %l %&]: %$%v");
}

//! Loads every subject, including the other shots' own, once for the whole run.
//! Reloading per shot is the expensive thing this harness does: a photograph large enough to be worth
//! photographing takes a second or so to decode, and its statistics longer.
void load_subjects(ImGuiTestContext *ctx)
{
    install_log_sink();

    auto paths = subjects();
    for (const auto &extra : {diff_subject(), palette_subject()})
        if (!extra.empty())
            paths.push_back(extra);

    // a multi-part or multi-view file becomes several images, so remember the count loading these paths
    // produced, not how many paths there were
    static std::vector<std::string> loaded;
    static int                      loaded_count = 0;
    if (loaded == paths && hdrview()->num_images() == loaded_count)
        return;

    reset_images(ctx);
    IM_CHECK(load_and_wait(ctx, paths) > 0);
    loaded       = paths;
    loaded_count = hdrview()->num_images();
}

//! Selects the loaded image whose filename contains `needle`; returns whether one was found.
//! By name, since load_images() makes no promise about the order background loads land in.
bool select_image_containing(const std::string &needle)
{
    for (int i = 0; i < hdrview()->num_images(); ++i)
        if (hdrview()->image(i)->filename.find(needle) != std::string::npos)
        {
            hdrview()->set_current_image_index(i);
            return true;
        }
    return false;
}

//! The pixel the zoomed-in shot centers on, when HDRVIEW_SCREENSHOT_ZOOM_PIXEL names one as "x,y".
std::optional<int2> chosen_zoom_pixel()
{
    const char *value = getenv("HDRVIEW_SCREENSHOT_ZOOM_PIXEL");
    int         x = 0, y = 0;
    if (value && sscanf(value, "%d,%d", &x, &y) == 2)
        return int2{x, y};
    return std::nullopt;
}

//! The busiest place in the image: the strongest edge inside the tile with the largest mean gradient.
/*!
    A zoomed-in shot has to land on an edge or a texture, or it is a picture of a flat color with numbers
    written on it. Two stages, since either alone picks badly: the tile survey finds a region that is
    textured and not a lone noisy pixel, and the search within it puts the edge through the middle of the
    frame. Measured, so that pointing the harness at a different photograph still produces a picture worth
    looking at; HDRVIEW_SCREENSHOT_ZOOM_PIXEL overrides it.
*/
int2 busiest_pixel(const ConstImagePtr &img)
{
    const auto    &group = img->groups[img->selected_group];
    const Channel &ch    = img->channels[group.channels[0]];
    const int2     size  = img->size();
    constexpr int  tiles = 16;
    const int2     tile  = la::max(size / tiles, int2{1});

    int2  best{size / 2};
    float best_score = -1.f;
    for (int ty = 0; ty + tile.y <= size.y; ty += tile.y)
        for (int tx = 0; tx + tile.x <= size.x; tx += tile.x)
        {
            // sparse forward differences: enough to rank tiles, and cheap over a 45-megapixel photograph
            constexpr int step  = 4;
            float         sum   = 0.f;
            int           count = 0;
            for (int y = ty; y + step < ty + tile.y; y += step)
                for (int x = tx; x + step < tx + tile.x; x += step)
                {
                    const float c = ch(x, y);
                    sum += std::abs(ch(x + step, y) - c) + std::abs(ch(x, y + step) - c);
                    ++count;
                }
            const float score = count ? sum / count : 0.f;
            if (score > best_score)
            {
                best_score = score;
                best       = int2{tx, ty};
            }
        }

    // second stage: the strongest single edge inside that tile, so the shot is centered on the edge itself
    int2  edge       = best + tile / 2;
    float edge_score = -1.f;
    for (int y = best.y; y + 1 < std::min(best.y + tile.y, size.y); ++y)
        for (int x = best.x; x + 1 < std::min(best.x + tile.x, size.x); ++x)
        {
            const float c     = ch(x, y);
            const float score = std::abs(ch(x + 1, y) - c) + std::abs(ch(x, y + 1) - c);
            if (score > edge_score)
            {
                edge_score = score;
                edge       = int2{x, y};
            }
        }
    return img->data_window.min + edge;
}

//! Puts the app back into the state every shot starts from.
/*!
    Mostly undoing whatever the previous shots and the rest of the suite left set: they run in one process
    against one app, and a leftover selection or tonemap is how a screenshot ends up showing statistics over
    an empty region. Three are not resets: short names, because the full ones are absolute paths that say
    more about the machine than about HDRView; raising the statistics tab, the most legible thing the
    right-hand dock can show; and parking the mouse over the image, so the readouts have something to report.
*/
void reset_view(ImGuiTestContext *ctx)
{
    hdrview()->blend_mode()  = BlendMode_Normal;
    hdrview()->tonemap()     = Tonemap_Gamma;
    hdrview()->short_names() = true;
    // forced, because set_reference_image_index() validates the index unless told not to, and -1, the
    // "no reference" value, is the one it rejects
    hdrview()->set_reference_image_index(-1, true);
    // above the default: the histogram's content is a picture, and at its normal height it reads as a
    // smear in a screenshot viewed scaled down
    hdrview()->histogram_height() = 1.6f * HDRViewApp::default_histogram_height;
    // the degenerate box at the origin, how the statistics panel's clear button spells "no selection"; a
    // default-constructed Box2i is the inverted one, whose INT_MAX corners get displayed
    hdrview()->roi() = hdrview()->roi_live() = Box2i{int2{0}};
    hdrview()->set_mouse_mode(MouseMode_PanZoom);

    // the first subject is the one the shots are of; which image ends up current after a load is otherwise
    // down to the order the background loads landed in
    if (!subjects().empty())
        select_image_containing(fs::path(subjects().front()).stem().string());

    ctx->SetRef("##MainMenuBar");
    ctx->MenuClick("View/Reset tonemapping");
    if (*hdrview()->action("Draw pixel grid").p_selected)
        ctx->MenuClick("View/Draw pixel grid");
    if (*hdrview()->action("Draw pixel values").p_selected)
        ctx->MenuClick("View/Draw pixel values");
    if (!*hdrview()->action("Show status bar").p_selected)
        ctx->MenuClick("Windows/Show status bar");
    // closed unless a shot asks for it: it docks under the viewport and takes height from the image
    *hdrview()->action("Show Log window").p_selected = false;
    ctx->MenuClick("View/Fit display window");
    ctx->SetRef("");

    ImGui::GlobalSpdLogWindow().mark_log_seen();

    ctx->WindowFocus("//Pixel statistics");
    ctx->MouseMoveToPos(hdrview()->app_pos_at_vp_pos(hdrview()->viewport_size() / 2.f));
}

//! Centers the view on `pixel` at the given zoom.
void zoom_to_pixel(float zoom, int2 pixel)
{
    hdrview()->set_zoom(zoom);
    hdrview()->reposition_pixel_to_vp_pos(hdrview()->viewport_size() / 2.f, float2(pixel));
}

} // namespace

void RegisterTests_Screenshots(ImGuiTestEngine *engine)
{
    if (!getenv("HDRVIEW_SCREENSHOT_DIR"))
        return; // an ordinary test run: register nothing

    // the whole app in its default layout, showing a file with enough structure to fill the Images panel,
    // and the Log window open under the viewport
    ImGuiTest *t = IM_REGISTER_TEST(engine, "screenshots", "overview");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        load_subjects(ctx);
        reset_view(ctx);
        *hdrview()->action("Show Log window").p_selected = true;
        capture(ctx, "screenshot-overview");
    };

    // the two halves of inspecting a pixel: the grid and per-pixel readouts over the image, and the
    // Colorspace panel saying which color space those numbers are in
    t           = IM_REGISTER_TEST(engine, "screenshots", "inspect");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_subjects(ctx);
        reset_view(ctx);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Draw pixel grid");
        ctx->MenuClick("View/Draw pixel values");
        ctx->SetRef("");
        // three image pixels top to bottom, framed by the viewport: how many readouts fit and whether they
        // are drawn at all both scale with the window, since draw_pixel_info() fades them in over a
        // threshold set by the width of "A: 31.00000" at the current font size
        zoom_to_pixel(hdrview()->viewport_size().y / 3.f,
                      chosen_zoom_pixel().value_or(busiest_pixel(hdrview()->current_image())));
        ctx->WindowFocus("//Colorspace");

        capture(ctx, "screenshot-inspect");
    };

    // the command palette over the image, with a query typed far enough to have filtered the list
    t           = IM_REGISTER_TEST(engine, "screenshots", "command_palette");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        load_subjects(ctx);
        reset_view(ctx);
        if (const auto subject = palette_subject(); !subject.empty())
        {
            select_image_containing(fs::path(subject).stem().string());
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Fit display window");
            ctx->SetRef("");
        }

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Windows/Command palette...");
        ctx->SetRef("");
        ctx->KeyChars("colo");

        capture(ctx, "screenshot-command-palette");

        ctx->KeyPress(ImGuiKey_Escape);
        wait_until(ctx, [] { return !*hdrview()->action("Command palette...").p_selected; });
    };

    // the difference between two views of one scene. A multi-view EXR's views arrive as channel groups of a
    // single image (Fog.exr holds 'Y' and 'right.Y'), so this sets the reference group alongside the
    // reference image. IceFire diverges from black, so sign reads as hue and magnitude as brightness.
    t           = IM_REGISTER_TEST(engine, "screenshots", "compare");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const std::string diff = diff_subject();
        load_subjects(ctx);
        reset_view(ctx);

        // whichever image has two or more groups to difference: the named diff subject if one was given,
        // otherwise the first that qualifies among the subjects
        int index = -1;
        for (int i = 0; i < hdrview()->num_images() && index < 0; ++i)
            if (hdrview()->image(i)->groups.size() >= 2 &&
                (diff.empty() ||
                 hdrview()->image(i)->filename.find(fs::path(diff).stem().string()) != std::string::npos))
                index = i;
        if (index < 0)
        {
            ctx->LogWarning("no subject with two channel groups to compare; skipping the comparison shot");
            return;
        }

        hdrview()->set_current_image_index(index);
        hdrview()->set_reference_image_index(index);
        auto img             = hdrview()->image(index);
        img->selected_group  = 0;
        img->reference_group = 1;
        // Subtract, not Difference: the latter is an absolute value, and half of a diverging colormap would
        // never be reached
        hdrview()->blend_mode() = BlendMode_Subtract;

        hdrview()->tonemap()          = Tonemap_PositiveNegative;
        hdrview()->reverse_colormap() = false;
        for (int i = 0; i < 20 && hdrview()->colormap() != Colormap_IceFire; ++i)
            hdrview()->action("Increase gamma/Next colormap").callback();
        IM_CHECK_EQ((int)hdrview()->colormap(), (int)Colormap_IceFire);

        // the difference is mostly a fraction of a percent, so at exposure 0 the picture is black with a few
        // bright fringes; the toolbar shows the exposure it was opened up to
        hdrview()->exposure() = hdrview()->exposure_live() = 4.f;

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Fit display window");
        ctx->SetRef("");

        capture(ctx, "screenshot-compare");

        hdrview()->tonemap()    = Tonemap_Gamma;
        hdrview()->blend_mode() = BlendMode_Normal;
        hdrview()->set_reference_image_index(-1, true);
    };
}
