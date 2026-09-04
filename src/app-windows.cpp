#include "app.h"

#include "colorspace.h"
#include "fonts.h"
#include "image.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "implot.h"
#include <hello_imgui/hello_imgui.h>

#include "platform_utils.h"

#ifdef HDRVIEW_ENABLE_GUI_TEST_ENGINE
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include <cstdio>
#include <cstdlib>
#if defined(HELLOIMGUI_HAS_OPENGL)
#include <hello_imgui/hello_imgui_include_opengl.h>
#include <hello_imgui/internal/backend_impls/opengl_setup_helper/opengl_screenshot.h>
#endif
#endif

using namespace std;
using namespace HelloImGui;

void HDRViewApp::run()
{
    ImPlot::CreateContext();
    Run(m_params);
    ImPlot::DestroyContext();
}

#ifdef HDRVIEW_ENABLE_GUI_TEST_ENGINE

#if defined(HELLOIMGUI_HAS_OPENGL)
/// Screen capture that reads the colorpass's offscreen target instead of the window's framebuffer.
/**
    Hello ImGui's default capture reads the window, which is display-referred: whenever the colorpass runs,
    that holds whatever transfer function the display asked for (linear light, PQ, ...), and a PNG of linear
    light is read back as sRGB and comes out markedly too dark.

    The pass's own target holds HDRView's extended sRGB, already sRGB-encoded with 1.0 at SDR white (see the
    tail of assets/shaders/image-shader.sglsl). Reading it as fixed-point clamps to [0, 1] and quantizes,
    which is the SDR rendition a screenshot wants, while the app goes on rendering in HDR.

    Everything else -- the y-flip, the framebuffer scale -- is Hello ImGui's.
*/
static bool capture_colorpass_framebuffer(ImGuiID viewport_id, int x, int y, int w, int h, unsigned int *pixels,
                                          void *user_data)
{
    // Null whenever the frame went straight to the window, in which case its framebuffer is already the
    // sRGB one to read.
    const RenderPass *pass = ((const HDRViewApp *)user_data)->capture_source();
    const uint32_t    fbo  = pass ? pass->framebuffer_handle() : 0;

    GLint previous = 0;
    if (fbo)
    {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    }

    const bool ok = HelloImGui::ImGuiApp_ImplGL_CaptureFramebuffer(viewport_id, x, y, w, h, pixels, nullptr);

    if (fbo)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous);

    return ok;
}
#endif

void HDRViewApp::enable_gui_test_engine(void (*register_tests)(ImGuiTestEngine *))
{
    // The test binary drives a real HDRViewApp, which would otherwise read and rewrite the settings file the
    // installed HDRView uses. iniDisable makes IniSettingsLocation() return nothing, which both the ImGui
    // layout and HelloImGui's LoadUserPref/SaveUserPref treat as "don't", so tests start from the built-in
    // defaults every run.
    m_params.iniDisable = true;

    // The suite is a long sequence of "yield until this becomes true", so what it costs is frames, and both
    // idling and vsync make a frame wait: idling throttles the rate whenever nothing is animating, which
    // during a test run is nearly always, and vsync blocks every remaining frame until the monitor is ready.
    // rememberEnableIdling is off so a run cannot inherit the user's own setting.
    m_params.fpsIdling.enableIdling         = false;
    m_params.fpsIdling.rememberEnableIdling = false;
    m_params.fpsIdling.vsyncToMonitor       = false;

    // A screenshot run wants a window of a stated size: the pictures sit next to each other in the README,
    // and a layout that reflows with the window would make every one a different composition.
    if (const char *size = getenv("HDRVIEW_SCREENSHOT_SIZE"))
    {
        int w = 0, h = 0;
        if (sscanf(size, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
            m_params.appWindowParams.windowGeometry.size = {w, h};
        else
            spdlog::warn("Ignoring HDRVIEW_SCREENSHOT_SIZE='{}'; expected e.g. '1400x880'.", size);
    }

    // The size above is in 96-PPI units, which this factor turns into pixels, scaling fonts and widget
    // paddings with them: a factor of 2 gives a 2x-density picture, not the same interface stretched.
    if (const char *scale = getenv("HDRVIEW_SCREENSHOT_SCALE"))
    {
        const float f = strtof(scale, nullptr);
        if (f > 0.f)
            m_params.dpiAwareParams.dpiWindowSizeFactor = f;
        else
            spdlog::warn("Ignoring HDRVIEW_SCREENSHOT_SCALE='{}'; expected a positive number.", scale);
    }

    m_params.useImGuiTestEngine = true;
    m_params.callbacks.RegisterTests =
        [
#if defined(HELLOIMGUI_HAS_OPENGL)
            this, // the screen-capture override below is the only thing that needs the app pointer
#endif
            register_tests]()
    {
        ImGuiTestEngine   *engine = GetImGuiTestEngine();
        ImGuiTestEngineIO &io     = ImGuiTestEngine_GetIO(engine);
#if defined(HELLOIMGUI_HAS_OPENGL)
        // Hello ImGui's Setup() has already pointed this at the window's framebuffer; redirect it to the
        // buffer that still holds sRGB. This callback runs after that Setup(), so the override sticks.
        io.ScreenCaptureFunc     = capture_colorpass_framebuffer;
        io.ScreenCaptureUserData = this;
#endif
        // Defaults to off, since it's meant for the interactive Test Engine UI; the terminal is this
        // binary's only way to report why a test failed under CI.
        io.ConfigLogToTTY            = true;
        io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
        // Hello ImGui's own test-engine Setup() (called before this callback runs) hardcodes ConfigRunSpeed
        // = Normal ("slowest mode in this demo"), which animates every simulated mouse move over many real
        // frames (see MouseMoveToPos() in imgui_te_context.cpp); Fast teleports it in 2 frames.
        io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
        register_tests(engine);
        ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, nullptr, ImGuiTestRunFlags_RunFromCommandLine);
    };
    // HelloImGui's own PostSwap hook steps the running test forward each frame; this watches for the queue
    // draining and asks the app to exit via m_params.appShallExit, as a window-close request does.
    m_params.callbacks.AfterSwap = [this]()
    {
        ImGuiTestEngine *engine = GetImGuiTestEngine();
        if (engine && m_params.callbacks.registerTestsCalled && ImGuiTestEngine_IsTestQueueEmpty(engine))
        {
            ImGuiTestEngineResultSummary summary;
            ImGuiTestEngine_GetResultSummary(engine, &summary);
            m_test_engine_tested    = summary.CountTested;
            m_test_engine_succeeded = summary.CountSuccess;
            m_params.appShallExit   = true;
        }
    };
}
#endif

void HDRViewApp::draw_tool_palette()
{
    if (!m_show_tool_palette)
        return;

    // Anchor to a corner of the central dockspace node, not the whole window, so the palette floats over the
    // image and never over the docked panels. calculate_viewport() has already run this frame, from the
    // CustomBackground callback, so the rect below is current.
    const float2 pivot{(m_tool_palette_corner & 1) ? 1.f : 0.f, (m_tool_palette_corner & 2) ? 1.f : 0.f};
    const float2 margin{EmSize(0.5f)};
    const float2 anchor = m_viewport_min + margin + pivot * max(m_viewport_size - 2.f * margin, float2{0.f});

    // While the palette is being dragged ImGui owns its position; we take it back on release and snap it to
    // whichever corner it landed nearest.
    if (!m_tool_palette_dragging)
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, pivot);

    // Whole pixels: ImGui truncates the layout cursor to integers as it stacks items, so fractional padding
    // drifts out of step with the auto-fit size and leaves the bottom edge tighter than the top.
    const float gap = ImTrunc(ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImTrunc(EmSize(0.3f)), ImTrunc(EmSize(0.3f))));

    // Opaque: the palette sits over image content of arbitrary brightness.
    ImGui::SetNextWindowBgAlpha(1.f);

    // ImGui owns the collapsed state from here on; seed it from ours once, and read it back below.
    ImGui::SetNextWindowCollapsed(m_tool_palette_collapsed, ImGuiCond_Once);

    // ImGui sizes the title bar's collapse arrow from the current font, which has no style var of its own,
    // so shrink the font to shrink the arrow; padding makes up the difference, keeping the bar a standard
    // frame tall and the arrow centered within it.
    const float bar_height = ImGui::GetFrameHeight();
    ImGui::PushFont(nullptr, 0.75f * ImGui::GetStyle().FontSizeBase);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ImGui::GetStyle().FramePadding.x, 0.5f * (bar_height - ImGui::GetFontSize())));

    // The stock title bar supplies the drag handle and the collapse arrow, and a "##" name suppresses its
    // text. AlwaysAutoResize keeps this a compact box hugging its buttons; NoDocking is required, since the
    // app runs in ProvideFullScreenDockSpace mode and the dockspace would otherwise swallow the window;
    // NoSavedSettings keeps a stale position in imgui.ini from fighting the anchoring above.
    bool open = ImGui::Begin("##ToolPalette", nullptr,
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus |
                                 ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    m_tool_palette_collapsed = ImGui::IsWindowCollapsed();
    if (open)
    {
        // The same gap between buttons whichever way they run; ItemSpacing supplies it down a column.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        for (int i = 0; i < MouseMode_COUNT; ++i)
        {
            if (!m_tool_palette_vertical && i)
                ImGui::SameLine(0.f, gap);
            ImGui::IconButton(action(mouse_mode_action_name(i)));
        }
        ImGui::PopStyleVar();

        if (ImGui::BeginPopupContextWindow())
        {
            if (ImGui::MenuItem("Vertical", nullptr, m_tool_palette_vertical))
                m_tool_palette_vertical = true;
            if (ImGui::MenuItem("Horizontal", nullptr, !m_tool_palette_vertical))
                m_tool_palette_vertical = false;
            ImGui::EndPopup();
        }
    }

    // Dragging the title bar has ImGui move the window itself, so ask it which window that is: the title bar
    // is an item of its own and defeats the usual "pressed the background" tests. Outside the open block, so
    // the drop still registers on a frame the window is collapsed or clipped away.
    if (ImGuiContext *g = ImGui::GetCurrentContext(); g->MovingWindow == ImGui::GetCurrentWindow())
        m_tool_palette_dragging = true;
    else if (m_tool_palette_dragging)
    {
        m_tool_palette_dragging = false;

        float2 center         = float2{ImGui::GetWindowPos()} + 0.5f * float2{ImGui::GetWindowSize()};
        float2 rel            = center - m_viewport_min;
        m_tool_palette_corner = (rel.x > 0.5f * m_viewport_size.x ? 1 : 0) | (rel.y > 0.5f * m_viewport_size.y ? 2 : 0);
    }
    ImGui::End();
}

void HDRViewApp::draw_tweak_window()
{
    if (!m_show_tweak_window)
        return;

    // auto &tweakedTheme = HelloImGui::GetRunnerParams()->imGuiWindowParams.tweakedTheme;
    ImGui::SetNextWindowSize(EmToVec2(20.f, 46.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Theme Tweaks", &m_show_tweak_window))
    {
        ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.50f);
        if (ImGui::BeginCombo("Theme", m_theme.name(), ImGuiComboFlags_HeightLargest))
        {
            for (int t = Theme::LIGHT_THEME; t < ImGuiTheme::ImGuiTheme_Count; ++t)
            {
                const bool is_selected = t == m_theme;
                if (ImGui::Selectable(Theme::name(t), is_selected))
                    m_theme.set(t);

                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGuiStyle previous = ImGui::GetStyle();

        ImGui::ShowStyleEditor(nullptr);

        bool theme_changed = memcmp(&previous, &ImGui::GetStyle(), sizeof(ImGuiStyle) - 2 * sizeof(float)) != 0;

        if (theme_changed)
            m_theme.set(Theme::CUSTOM_THEME);
    }
    ImGui::End();
}

void HDRViewApp::draw_developer_windows()
{
    if (m_show_demo_window)
    {
        ImGui::ShowDemoWindow(&m_show_demo_window);
        ImPlot::ShowMetricsWindow(&m_show_demo_window);
        ImPlot::ShowDemoWindow(&m_show_demo_window);
    }

    if (m_show_debug_window)
    {
        ImGui::SetNextWindowSize(EmToVec2(20.f, 46.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug", &m_show_debug_window))
        {
            if (ImGui::BeginTabBar("Debug tabs", ImGuiTabBarFlags_None))
            {
                if (ImGui::BeginTabItem("Transfer functions"))
                {
                    static TransferFunction tf{TransferFunction::Linear, 2.2f};
                    ImGui::DragFloat("Gamma", &tf.gamma, 0.01f, 0.f);
                    if (ImGui::BeginCombo("##transfer function", transfer_function_name(tf).c_str(),
                                          ImGuiComboFlags_HeightLargest))
                    {
                        for (TransferFunction::Type n = TransferFunction::Linear; n < TransferFunction::Count; ++n)
                        {
                            const bool is_selected = (tf.type == n);
                            if (ImGui::Selectable(
                                    transfer_function_name({(TransferFunction::Type_)n, tf.gamma}).c_str(),
                                    is_selected))
                                tf.type = (TransferFunction::Type_)n;

                            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    if (ImPlot::BeginPlot("Transfer functions"))
                    {
                        ImPlot::SetupAxes("input", "encoded", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

                        auto f = [](float x) { return to_linear(x, tf); };
                        auto g = [](float y) { return from_linear(y, tf); };

                        const int    N = 101;
                        static float xs1[N], ys1[N];
                        for (int i = 0; i < N; ++i)
                        {
                            xs1[i] = i / float(N - 1);
                            ys1[i] = f(xs1[i]);
                        }
                        static float xs2[N], ys2[N];
                        for (int i = 0; i < N; ++i)
                        {
                            ys2[i] = lerp(0.0f, ys1[N - 1], i / float(N - 1));
                            xs2[i] = g(ys2[i]);
                        }

                        ImPlotSpec spec;
                        spec.LineWeight = 2.f;
                        spec.MarkerSize = 2.f;

                        spec.Marker = ImPlotMarker_Circle;
                        ImPlot::PlotLine("to_linear", xs1, ys1, N, spec);
                        spec.Marker = ImPlotMarker_Square;
                        ImPlot::PlotLine("from_linear", xs2, ys2, N, spec);

                        ImPlot::EndPlot();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Illuminant spectra"))
                {
                    if (ImPlot::BeginPlot("Illuminant spectra"))
                    {
                        ImPlot::SetupAxes("Wavelength", "Intensity", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

                        ImPlotSpec spec;
                        spec.LineWeight = 2.f;
                        spec.Marker     = ImPlotMarker_Circle;
                        spec.MarkerSize = 2.f;

                        for (WhitePoint n = WhitePoint_FirstNamed; n <= WhitePoint_LastNamed; ++n)
                        {
                            WhitePoint_ wp{n};
                            auto        spectrum = white_point_spectrum(wp);
                            if (spectrum.values.empty())
                                continue;
                            string name{white_point_name(wp)};
                            ImPlot::PlotLine(name.c_str(), spectrum.values.data(), (int)spectrum.values.size(),
                                             (spectrum.max_wavelength - spectrum.min_wavelength) /
                                                 (spectrum.values.size() - 1),
                                             spectrum.min_wavelength, spec);
                        }
                        ImPlot::EndPlot();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("CIE 1931 XYZ"))
                {
                    if (ImPlot::BeginPlot("CIE 1931 XYZ color matching functions"))
                    {
                        ImPlot::SetupAxes("Wavelength", "Intensity", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

                        ImPlotSpec spec;
                        spec.LineWeight = 2.f;
                        spec.Marker     = ImPlotMarker_Circle;
                        spec.MarkerSize = 2.f;
                        // the three curves are interleaved in one float3 array
                        spec.Stride = sizeof(float3);

                        auto &xyz       = CIE_XYZ_spectra();
                        auto  increment = (xyz.max_wavelength - xyz.min_wavelength) / xyz.values.size();
                        ImPlot::PlotLine("X", (const float *)&xyz.values[0].x, (int)xyz.values.size(), increment,
                                         xyz.min_wavelength, spec);
                        ImPlot::PlotLine("Y", (const float *)&xyz.values[0].y, (int)xyz.values.size(), increment,
                                         xyz.min_wavelength, spec);
                        ImPlot::PlotLine("Z", (const float *)&xyz.values[0].z, (int)xyz.values.size(), increment,
                                         xyz.min_wavelength, spec);

                        ImPlot::EndPlot();
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }

        ImGui::End();
    }
}

void HDRViewApp::draw_history_window()
{
    auto img = current_image();
    if (!img)
    {
        ImGui::TextDisabled("No image loaded.");
        return;
    }

    auto &history = img->history;

    ImGui::IconButton(action("Undo"));
    ImGui::SameLine();
    ImGui::IconButton(action("Redo"));
    ImGui::SameLine();

    // Only worth saying once there is one: most steps are a pair of lambdas and hold nothing.
    if (const size_t held = history.memory_usage(); held > 0)
        ImGui::TextDisabled("%d step%s, %s", history.size(), history.size() == 1 ? "" : "s",
                            fmt::format("{:.1H}", human_readible{held}).c_str());
    else
        ImGui::TextDisabled("%d step%s", history.size(), history.size() == 1 ? "" : "s");

    ImGui::Separator();

    // The state to move to once the list has been drawn: stepping mid-list would renumber the rows still
    // to be drawn.
    int target = -1;

    if (ImGui::BeginChild("##History list", ImVec2(0, 0), ImGuiChildFlags_None))
    {
        // One row per state, not per entry: state 0 is the image as it was opened, and state k is the image
        // after entries 0 through k-1. The cursor is numbered the same way.
        for (int state = 0; state <= history.size(); ++state)
        {
            ImGui::PushID(state);

            const bool current = state == history.current_state();
            const bool undone  = state > history.current_state();

            // Everything past the cursor is what redo would reapply; faded, since it is still reachable.
            if (undone)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            const string name = state == 0 ? "Opened" : history.entry_name(state - 1);
            const string icon = state == 0 ? ICON_MY_OPEN_IMAGE : ICON_MY_HISTORY;

            if (ImGui::Selectable(fmt::format("{} {}", icon, name).c_str(), current, ImGuiSelectableFlags_AllowOverlap))
                target = state;

            // Which state the file on disk holds, so it is clear how far back a save can be undone to.
            if (state == history.saved_state())
            {
                ImGui::SameLine();
                ImGui::TextDisabled(ICON_MY_SAVE_AS);
                ImGui::Tooltip("This is the state the image was last saved in.");
            }

            // What this step costs to be able to go back past, right-aligned so the names stay readable.
            // Blank, not "0 B", for a step that stores no pixels, as a flip undone by flipping back does.
            if (const size_t held = state > 0 ? history.entry_memory_usage(state - 1) : 0; held > 0)
            {
                const string text = fmt::format("{:.1H}", human_readible{held});
                const float  w    = ImGui::CalcTextSize(text.c_str()).x;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - w - ImGui::GetStyle().ItemSpacing.x);
                ImGui::TextDisabled("%s", text.c_str());
            }

            if (undone)
                ImGui::PopStyleColor();

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // Clicking a row walks there one entry at a time: each entry knows how to reverse the one edit it
    // describes and nothing knows how to skip. The cursor read here is the current image's, whose history
    // this window shows, while undo()/redo() step every selected image.
    if (target >= 0)
    {
        while (history.current_state() > target && undo()) {}
        while (history.current_state() < target && redo()) {}
    }
}

void HDRViewApp::draw_statistics_window()
{
    if (!current_image())
    {
        ImGui::TextDisabled("No image loaded.");
        return;
    }

    current_image()->draw_histogram();

    // Label column of the Statistics and Watched pixels tables below: bold, to read as a header against the
    // values beside it.
    auto bold_font = font("sans bold");

    // ImGui::SeparatorText("Selection");
    if (ImGui::PE::Begin("SelectionPE", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("Selection").x + ImGui::GetStyle().CellPadding.x);
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);

        ImGui::PE::Entry("Selection",
                         [&]
                         {
                             // Same width budget as ChannelValuesRow (N boxes + a trailing swatch-sized
                             // slot), but these boxes are editable DragInts and the trailing slot holds a
                             // "clear the selection" close button.
                             float col_w   = ImGui::PE::ColumnWidth(1);
                             float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                             float sz      = ImGui::GetFontSize();
                             float box_w   = ImMax((col_w - 4.f * spacing - sz) / 4.f, 1.f);

                             int *comps[4]  = {&m_roi_live.min.x, &m_roi_live.min.y, &m_roi_live.max.x,
                                               &m_roi_live.max.y};
                             bool committed = false;
                             for (int c = 0; c < 4; ++c)
                             {
                                 if (c > 0)
                                     ImGui::SameLine(0.f, spacing);
                                 ImGui::SetNextItemWidth(box_w);
                                 ImGui::DragInt(fmt::format("##roi{}", c).c_str(), comps[c]);
                                 if (ImGui::IsItemDeactivatedAfterEdit())
                                     committed = true;
                             }
                             // A cleared selection is the inverted box, whose corners are INT_MAX and
                             // INT_MIN, and size() is max minus min, which overflows on it.
                             const int2 extent = m_roi_live.has_volume() ? m_roi_live.size() : int2{0};
                             ImGui::SetItemTooltip("W x H: (%d x %d)", extent.x, extent.y);
                             if (committed)
                                 m_roi = m_roi_live;

                             ImGui::SameLine(0.f, spacing);
                             ImGui::BeginDisabled(!m_roi_live.has_volume());
                             if (ImGui::CloseButton(ImGui::GetID("##deselect"), ImGui::GetCursorScreenPos()))
                                 m_roi = m_roi_live = Box2i{int2{0}};
                             ImGui::EndDisabled();
                             ImGui::SetItemTooltip("Clear the selection.");

                             return committed;
                         });

        ImGui::PopStyleVar();
        ImGui::PE::End();
    }

    ImGui::SeparatorText("Statistics");

    // Draws one PE::TreeNode row: the value column holds X/Y coordinate boxes (draggable for watched pixels,
    // disabled for the hovered pixel, since ReadOnly blocks only keyboard entry) plus, for watched pixels, a
    // trailing delete button. Children, when open, are Current/Reference/Composite ChannelValuesRow entries.
    // Returns true if the delete button was clicked.
    auto PixelTreeNodePE = [&](const string &icon_title, int2 &pixel, int3 &color_mode, bool editable, bool show_delete)
    {
        bool deleted = false;
        // SpanAllColumns extends the tree node's click rect across the whole row (see TreeNodeBehavior() in
        // imgui_widgets.cpp), claiming it before the X/Y/delete controls below are drawn; AllowOverlap defers
        // that claim so a later overlapping widget can take the click, as CollapsingHeader's own close button
        // does.
        bool open = ImGui::PE::TreeNode(icon_title.c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth |
                                            ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::TableNextColumn();
        // Row's own top-left, before anything else is drawn in this column: the reference point for the close
        // button below, matching CollapsingHeader's own (g.LastItemData.Rect.Min.y + FramePadding.y).
        ImVec2 row_screen_pos = ImGui::GetCursorScreenPos();

        float col_w   = ImGui::PE::ColumnWidth(1);
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        // GetFontSize(), not GetFrameHeight(): the children rows are drawn compact (FramePadding.y == 0),
        // so their ChannelValuesRow swatch slot is FontSize wide, while this row still has the ambient
        // padding.
        float sz = ImGui::GetFontSize();
        // Two inter-item gaps (X-to-Y, Y-to-close-slot), and always the swatch-sized slot ChannelValuesRow
        // reserves, so the X/Y boxes end at the same place whether or not show_delete.
        float drag_size = ImMax((col_w - 2.f * spacing - sz) * 0.5f, 1.f);

        ImGuiInputTextFlags_ flags = editable ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
        ImGui::BeginDisabled(!editable);
        auto fpy = ImGui::GetStyle().FramePadding.y;
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
        auto y0 = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y0 + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##x", &pixel.x, 1.f, 0, 0, "X: %d", flags);
        ImGui::SameLine(0.f, spacing);
        ImGui::SetCursorPosY(y0 + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##y", &pixel.y, 1.f, 0, 0, "Y: %d", flags);
        ImGui::PopStyleVar();
        ImGui::EndDisabled();

        if (show_delete)
        {
            ImGui::SameLine(0.f, spacing);
            ImVec2 pos{ImGui::GetCursorScreenPos().x, row_screen_pos.y + ImGui::GetStyle().FramePadding.y};
            if (ImGui::CloseButton(ImGui::GetID("##delete"), pos))
                deleted = true;
            ImGui::SetItemTooltip("Remove this watched pixel.");
        }

        if (open)
        {
            // Compact only for the value rows (the tree node/X/Y/delete row above stays full height).
            ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);

            ImGui::PE::Entry("Current",
                             [&]
                             {
                                 pixel_color_widget(pixel, color_mode.x, 0, editable, ImGui::PE::ColumnWidth(1));
                                 return false;
                             });

            // Dims the "Reference" label itself too, not just its (already self-dimming) value row.
            ImGui::BeginDisabled(!reference_image());
            ImGui::PE::Entry("Reference",
                             [&]
                             {
                                 pixel_color_widget(pixel, color_mode.y, 1, editable, ImGui::PE::ColumnWidth(1));
                                 return false;
                             });
            ImGui::EndDisabled();

            ImGui::PE::Entry("Composite",
                             [&]
                             {
                                 pixel_color_widget(pixel, color_mode.z, 2, editable, ImGui::PE::ColumnWidth(1));
                                 return false;
                             });

            ImGui::PopStyleVar();
            ImGui::PE::TreePop();
        }
        return deleted;
    };

    if (ImGui::PE::Begin("StatisticsPE", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
    {
        // Initial (still user-resizable) label-column width, fitting the widest label ("Maximum"), measured
        // in the bold label font the rows themselves use.
        ImGui::PushFont(bold_font, 0.f);
        float label_col_w = ImGui::CalcTextSize("Maximum").x + ImGui::GetStyle().CellPadding.x;
        ImGui::PopFont();
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, label_col_w);
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
        ImGui::PE::PushLabelFont(bold_font);
        if (auto img = current_image())
            img->draw_channel_stats();
        ImGui::PE::PopLabelFont();
        ImGui::PopStyleVar();

        ImGui::PE::End();
    }

    ImGui::SeparatorText("Watched pixels");

    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
    ImGui::Checkbox("Show " ICON_MY_WATCHED_PIXEL "s in viewport", &m_draw_watched_pixels);
    ImGui::PopStyleVar();
    if (ImGui::PE::Begin("WatchedPixelsPE", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
    {
        // Same initial-width pattern as the Statistics table above, sized to the widest child-row label
        // ("Composite"), not the shorter top-level tree node labels.
        ImGui::PushFont(bold_font, 0.f);
        float label_col_w = ImGui::CalcTextSize("Composite").x + ImGui::GetStyle().CellPadding.x;
        ImGui::PopFont();
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, label_col_w);

        ImGui::PE::PushLabelFont(bold_font);

        if (auto hp = last_hovered_pixel())
        {
            auto        hovered_pixel = *hp;
            static int3 hover_color_mode{0, 0, 0};
            // PE::TreeNode only keeps its own PushID(icon_title) alive while open, so a caller-owned PushID
            // keeps the X/Y/delete controls' IDs stable across collapsed rows too.
            ImGui::PushID("Mouse");
            PixelTreeNodePE(ICON_MY_CURSOR_ARROW " Mouse", hovered_pixel, hover_color_mode, false, false);
            ImGui::PopID();
        }

        int pe_delete_idx = -1;
        for (int i = 0; i < (int)m_watched_pixels.size(); ++i)
        {
            auto &wp = m_watched_pixels[i];
            ImGui::PushID(i);
            if (PixelTreeNodePE(fmt::format("{}{}", ICON_MY_WATCHED_PIXEL, i + 1), wp.pixel, wp.color_mode, true, true))
                pe_delete_idx = i;
            ImGui::PopID();
        }
        if (pe_delete_idx >= 0)
            m_watched_pixels.erase(m_watched_pixels.begin() + pe_delete_idx);

        ImGui::PE::PopLabelFont();
        ImGui::PE::End();
    }
}

/// A color swatch of a stated size, opening a picker when clicked.
/**
    ColorEdit4's own swatch is always one frame tall, which is too big to pair two of them into a control
    that still lines up with the widgets beside it.
*/
static bool color_swatch(const char *id, float4 &color, float size)
{
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::ColorButton("##swatch", ImVec4(color.x, color.y, color.z, color.w), ImGuiColorEditFlags_AlphaPreviewHalf,
                           ImVec2(size, size)))
        ImGui::OpenPopup("##picker");
    if (ImGui::BeginPopup("##picker"))
    {
        changed = ImGui::ColorPicker4("##picker4", &color.x, ImGuiColorEditFlags_AlphaBar);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

/// Stroke and fill as one control, the way an illustration tool shows them: the fill in front, the stroke
/// behind it and drawn as a ring. Which is which is what the overlap says, so neither needs a label.
static bool stroke_fill_swatches(const char *id, float4 &stroke, float4 &fill)
{
    // Two thirds of a frame each, overlapping by half of that, so the pair is exactly one frame tall and
    // sits on the same line as the widgets beside it.
    const float  h    = ImGui::GetFrameHeight();
    const float  sw   = h * 2.f / 3.f;
    const float  off  = sw * 0.5f;
    const ImVec2 base = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);

    // Behind, so it is submitted first and allows the fill to take the hover where the two meet.
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetCursorScreenPos(ImVec2(base.x + off, base.y + off));
    bool changed = color_swatch("stroke", stroke, sw);
    ImGui::SetItemTooltip("Stroke: the outline of a shape, and the color of a text annotation.");

    // Its middle punched out before the fill goes over it, so it reads as an outline, not a second fill.
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    const float  t = (hi.x - lo.x) * 0.3f;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(lo.x + t, lo.y + t), ImVec2(hi.x - t, hi.y - t),
                                              ImGui::GetColorU32(ImGuiCol_WindowBg));

    ImGui::SetCursorScreenPos(base);
    changed |= color_swatch("fill", fill, sw);
    ImGui::SetItemTooltip("Fill: the inside of a closed shape. Fully transparent leaves it unfilled.");

    ImGui::PopID();

    // Both were placed by hand, so the layout has to be told how much room they took between them.
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2(sw + off, sw + off));
    return changed;
}

/// What a row calls an annotation, cut with an ellipsis at \p width.
/**
    A text annotation is named by what it says, which can be a paragraph, so the row shows as much of it as
    it has room for.
*/
static std::string row_name(const Annotation &a, float width)
{
    std::string name =
        a.shape == Annotation::Shape::Text && a.label.empty() && !a.text.empty() ? a.text : a.display_label();

    // Newlines would run the row into the ones below it.
    for (auto &c : name)
        if (c == '\n' || c == '\r')
            c = ' ';

    if (ImGui::CalcTextSize(name.c_str()).x <= width)
        return name;

    const float ellipsis = ImGui::CalcTextSize("...").x;
    while (!name.empty() && ImGui::CalcTextSize(name.c_str()).x + ellipsis > width) name.pop_back();
    return name + "...";
}

/// The icon the panel and the shape picker show for \p shape.
static const char *annotation_shape_icon(Annotation::Shape shape)
{
    switch (shape)
    {
    case Annotation::Shape::Rect: return ICON_MY_SHAPE_RECT;
    case Annotation::Shape::Ellipse: return ICON_MY_SHAPE_ELLIPSE;
    case Annotation::Shape::Line: return ICON_MY_SHAPE_LINE;
    case Annotation::Shape::Arrow: return ICON_MY_SHAPE_ARROW;
    case Annotation::Shape::Text: return ICON_MY_SHAPE_TEXT;
    case Annotation::Shape::Freehand: return ICON_MY_SHAPE_FREEHAND;
    default: return ICON_MY_ANNOTATE;
    }
}

void HDRViewApp::draw_annotations_window()
{
    auto img = current_image();
    if (!img)
    {
        ImGui::TextDisabled("No image loaded.");
        return;
    }

    auto     &list   = img->annotations;
    const int active = active_annotation();

    // Taken before anything below flattens it, for the popup that wants ordinary widgets.
    const ImVec2 frame_padding = ImGui::GetStyle().FramePadding;

    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
    ImGui::Checkbox("Show annotations in viewport", &m_draw_annotations);
    ImGui::PopStyleVar();

    // One row of controls, editing whichever annotation is in hand, or the look the next one will be drawn
    // with when none is. The same widgets either way, so there is nothing to learn twice.
    Annotation &edited = active >= 0 ? list[size_t(active)] : m_annotation_style;
    draw_annotation_controls(edited);

    // A text annotation is placed with a click and says nothing yet, so the row it landed in opens for
    // typing rather than leaving an empty one to be found and double-clicked.
    if (m_annotation_place_text)
    {
        m_annotation_place_text = false;
        if (active >= 0)
        {
            m_annotation_renaming  = active;
            m_annotation_rename[0] = '\0';
            m_annotation_rename_was.clear();
        }
    }

    // The viewport asks for this when a text annotation is double-clicked on the image.
    if (m_annotation_edit_text && active >= 0)
    {
        m_annotation_renaming   = active;
        m_annotation_rename_was = list[size_t(active)].text;
        snprintf(m_annotation_rename, sizeof(m_annotation_rename), "%s", m_annotation_rename_was.c_str());
    }

    // Applied after the list is drawn: removing a row mid-list renumbers the ones still to come.
    int erase = -1;

    // Where the rows are, so a drag can say which one the cursor is over.
    float first_row_top = 0.f, row_height = 0.f;

    // The same table the Images panel's list is: an outer border, striped rows, and rows that reach the
    // full width rather than sitting inside a child's padding.
    static constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit |
                                                   ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg |
                                                   ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("AnnotationList", 1, table_flags))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

        // A row is as tall as its text, like the Images panel's, and its buttons sit against each other:
        // each already carries its own highlight, which is all the separation they need. No frame padding
        // either, which would otherwise be subtracted from a square button and push its glyph off center.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleVarX(ImGuiStyleVar_ItemSpacing, 0.f);

        // Square, and big enough for the widest glyph any of these buttons draws: an icon is wider than the
        // font size it is drawn at, so a square of that would clip it.
        float side = ImGui::GetTextLineHeight();
        for (const char *icon : {ICON_MY_VISIBILITY, ICON_MY_VISIBILITY_OFF, ICON_MY_LOCK, ICON_MY_LOCK_OPEN,
                                 ICON_MY_SMOOTH, ICON_MY_POLYLINE, ICON_MY_FONT, ICON_MY_TRASH_CAN})
            side = std::max(side, ImGui::CalcTextSize(icon).x);
        const ImVec2 icon_sz{side, side};

        auto flat_toggle = [icon_sz](const char *on, const char *off, bool &value, const char *tooltip)
        {
            const bool clicked = ImGui::FlatButton(value ? on : off, false, icon_sz);
            if (clicked)
                value = !value;
            ImGui::SetItemTooltip("%s", tooltip);
            ImGui::SameLine();
            return clicked;
        };

        // The renderer's own overlay, listed but never edited: it belongs to the process sending it, and
        // this is the one place overlays are switched on and off.
        if (!img->vector_overlay.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            flat_toggle(ICON_MY_VISIBILITY, ICON_MY_VISIBILITY_OFF, img->vector_overlay_visible,
                        "Draw the renderer's overlay.");
            ImGui::TextDisabled(ICON_MY_LIST_VIEW " Renderer overlay");
            ImGui::Tooltip("Drawing commands streamed in by a renderer. They can be hidden here, but only "
                           "the process sending them can change them.");
        }

        for (int i = 0; i < int(list.size()); ++i)
        {
            auto &a = list[size_t(i)];
            ImGui::PushID(i);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            const float row_x = ImGui::GetCursorPosX();
            const float row_w = ImGui::GetContentRegionAvail().x;

            // The highlight goes down first and spans the row, with everything else drawn back over it, so
            // selecting reads across the whole row rather than just the label.
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::Selectable("##row", i == active,
                                  ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                                  ImVec2(0.f, icon_sz.y)))
                set_active_annotation(i);

            // Which row a press took hold of. Tracked here rather than read back from ImGui's active item,
            // whose id is this row's index: reordering moves annotations between indices, so the active
            // item would stay on the row number and the list would flicker between two orders.
            if (ImGui::IsItemActivated())
                m_annotation_row_drag = i;

            // Taken while the selectable is the item under the cursor, and acted on below, once the name
            // it applies to has been drawn.
            const bool renamed_here = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            // Where the rows sit, for the reorder below. The pitch is measured between the first two rather
            // than assumed, since a table decides row heights for itself.
            if (i == 0)
            {
                first_row_top = ImGui::GetItemRectMin().y;
                row_height    = ImGui::GetItemRectSize().y;
            }
            else if (i == 1)
                row_height = ImGui::GetItemRectMin().y - first_row_top;

            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorPosX(row_x);

            flat_toggle(ICON_MY_VISIBILITY, ICON_MY_VISIBILITY_OFF, a.visible, "Draw this annotation.");
            flat_toggle(ICON_MY_LOCK, ICON_MY_LOCK_OPEN, a.locked,
                        "A locked annotation cannot be picked up in the viewport.");

            // One column, whatever the shape has to put in it: a scribble runs a curve through its points,
            // a text annotation picks its face and size, and the rest hold the place so the shape icons
            // beside them still line up.
            if (a.shape == Annotation::Shape::Freehand)
            {
                if (flat_toggle(ICON_MY_SMOOTH, ICON_MY_POLYLINE, a.smooth,
                                "Draw this scribble as a curve through its points."))
                    m_annotation_style.smooth = a.smooth;
            }
            else if (a.shape == Annotation::Shape::Text)
            {
                if (ImGui::FlatButton(ICON_MY_FONT, false, icon_sz))
                    ImGui::OpenPopup("##font");
                ImGui::SetItemTooltip("The face and size this text is drawn in.");
                ImGui::SameLine();
                draw_font_popup(a, frame_padding);
            }
            else
            {
                ImGui::Dummy(icon_sz);
                ImGui::SameLine();
            }

            if (m_annotation_renaming == i)
            {
                // Only the name becomes a field; the icons stay where they are, so a row does not
                // rearrange itself the moment it is renamed.
                ImGui::TextUnformatted(annotation_shape_icon(a.shape));
                ImGui::SameLine();

                // Focused the frame it appears, so the name can be typed straight away; once it holds the
                // focus it is the active item, so this stops asking for it. Enter or clicking away keeps
                // the name, and Escape drops it, InputText restoring its own buffer.
                if (!ImGui::IsAnyItemActive())
                    ImGui::SetKeyboardFocusHere();

                const float field_w = row_x + row_w - icon_sz.x - ImGui::GetCursorPosX();

                bool edited = false;
                if (a.shape == Annotation::Shape::Text)
                {
                    // What a text annotation says can run to several lines, so its row grows a box that
                    // takes them, and Enter starts a line rather than finishing the edit.
                    const int lines = 1 + int(std::count(a.text.begin(), a.text.end(), '\n'));
                    edited          = ImGui::InputTextMultiline(
                        "##rename", m_annotation_rename, sizeof(m_annotation_rename),
                        ImVec2(field_w, ImGui::GetTextLineHeight() * float(std::min(lines + 1, 8))));
                }
                else
                {
                    ImGui::SetNextItemWidth(field_w);
                    edited = ImGui::InputTextWithHint("##rename", a.display_label().c_str(), m_annotation_rename,
                                                      sizeof(m_annotation_rename));
                }

                // So the viewport can find this field's caret and selection and show them on the image.
                m_annotation_rename_id = ImGui::GetItemID();

                // Focusing a field from code selects all of it; an edit opened from the image continues
                // what is already there, so the caret goes to the end instead.
                if (m_annotation_edit_text)
                    if (auto *state = ImGui::GetInputTextState(m_annotation_rename_id))
                    {
                        state->ReloadUserBufAndMoveToEnd();
                        m_annotation_edit_text = false;
                    }

                // For a text annotation the row's name is what it says, so this is how the text is typed --
                // as it is typed, so the image shows it rather than waiting for the field to be left.
                auto &named = a.shape == Annotation::Shape::Text ? a.text : a.label;
                if (edited)
                    named = m_annotation_rename;

                if (ImGui::IsItemDeactivated())
                {
                    // Escape asks for what was there before, and every keystroke has already been applied.
                    if (!ImGui::IsItemDeactivatedAfterEdit())
                        named = m_annotation_rename_was;
                    m_annotation_renaming  = -1;
                    m_annotation_edit_text = false;

                    // A text annotation with nothing to say is not kept: placing one and thinking better
                    // of it leaves nothing behind, as a click that never became a drag does.
                    if (a.shape == Annotation::Shape::Text && a.text.empty())
                        erase = i;
                }
            }
            else
            {
                const float name_w = row_x + row_w - icon_sz.x - ImGui::GetCursorPosX() -
                                     ImGui::CalcTextSize(annotation_shape_icon(a.shape)).x - ImGui::CalcTextSize(" ").x;
                ImGui::TextUnformatted(
                    fmt::format("{} {}", annotation_shape_icon(a.shape), row_name(a, name_w)).c_str());

                // Double-clicking the row renames it in place, which is where the name is read.
                if (renamed_here)
                {
                    m_annotation_renaming   = i;
                    m_annotation_rename_was = a.shape == Annotation::Shape::Text ? a.text : a.label;
                    snprintf(m_annotation_rename, sizeof(m_annotation_rename), "%s", m_annotation_rename_was.c_str());
                }
            }

            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorPosX(row_x + row_w - icon_sz.x);
            if (ImGui::FlatButton(ICON_MY_TRASH_CAN, false, icon_sz))
                erase = i;
            ImGui::SetItemTooltip("Delete this annotation.");

            ImGui::PopID();
        }

        ImGui::PopStyleVar(2);
        ImGui::EndTable();
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        m_annotation_row_drag = -1;

    // The row the cursor is over now, which a fast drag can be several away from. The list is drawn in
    // order, so moving a row is what puts one annotation in front of another.
    if (erase < 0 && m_annotation_row_drag >= 0 && m_annotation_row_drag < int(list.size()) && row_height > 0.f)
    {
        const int to =
            std::clamp(int((ImGui::GetIO().MousePos.y - first_row_top) / row_height), 0, int(list.size()) - 1);
        if (to != m_annotation_row_drag)
        {
            m_annotation_renaming = -1;

            Annotation moved = list[size_t(m_annotation_row_drag)];
            list.erase(list.begin() + m_annotation_row_drag);
            list.insert(list.begin() + to, moved);

            // What is in hand is the annotation, not the row it was in, so the selection follows it.
            if (active == m_annotation_row_drag)
                set_active_annotation(to);
            else if (active > m_annotation_row_drag && active <= to)
                set_active_annotation(active - 1);
            else if (active < m_annotation_row_drag && active >= to)
                set_active_annotation(active + 1);

            m_annotation_row_drag = to;
        }
    }

    if (erase >= 0)
    {
        // The row being renamed is about to be a different annotation, or none.
        m_annotation_renaming = -1;

        list.erase(list.begin() + erase);
        // The rows after it have shifted down, so what was in hand has to shift with them.
        if (active == erase)
            set_active_annotation(-1);
        else if (active > erase)
            set_active_annotation(active - 1);
    }
}

/// The "Add:" label and the picker that says which shape the tool draws next, as one table cell.
void HDRViewApp::draw_shape_picker(bool named)
{
    ImGui::TableNextColumn();
    const auto &style = ImGui::GetStyle();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Add:");
    ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string preview = named ? fmt::format("{} {}", annotation_shape_icon(m_annotation_shape),
                                                    annotation_shape_name(m_annotation_shape))
                                      : annotation_shape_icon(m_annotation_shape);
    if (ImGui::BeginCombo("##shape", preview.c_str()))
    {
        for (int n = 0; n < int(Annotation::Shape::COUNT); ++n)
        {
            const auto shape = Annotation::Shape(n);
            // The names are always spelled out in the list, whatever the closed control has room for.
            const bool is_selected = shape == m_annotation_shape;
            if (ImGui::Selectable(
                    fmt::format("{} {}", annotation_shape_icon(shape), annotation_shape_name(shape)).c_str(),
                    is_selected))
                m_annotation_shape = shape;
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Which shape the annotate tool draws next.");
}

void HDRViewApp::draw_font_popup(Annotation &a, const ImVec2 &frame_padding)
{
    if (!ImGui::BeginPopup("##font"))
        return;

    // The rows flatten their padding to stay one line tall; a popup off one of them is an ordinary window
    // and wants ordinary widgets.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, frame_padding);

    const auto &faces = annotation_font_faces();
    const char *shown = faces.front().label;
    for (const auto &f : faces)
        if (a.font_face == f.name)
            shown = f.label;

    ImGui::SetNextItemWidth(EmSize(8));
    if (ImGui::BeginCombo("##face", shown))
    {
        for (const auto &f : faces)
        {
            const bool is_selected = a.font_face == f.name;
            if (ImGui::Selectable(f.label, is_selected))
            {
                a.font_face                  = f.name;
                m_annotation_style.font_face = a.font_face;
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(EmSize(6));
    if (ImGui::DragFloat("##size", &a.font_size, 0.25f, Annotation::MinFontSize, Annotation::MaxFontSize, "%.0f px"))
        m_annotation_style.font_size = a.font_size;
    ImGui::SetItemTooltip("Screen pixels, so the text stays the same size however far the image is zoomed.");

    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

void HDRViewApp::draw_annotation_controls(Annotation &a)
{
    const auto &style = ImGui::GetStyle();

    // Icon alone, or icon and name once the row can spare the difference. It snaps between the two rather
    // than growing with the panel, so the control does not change size as the panel is resized. Both widths
    // leave room for the drop-down arrow, which is a frame wide at the right-hand end.
    const float arrow_w   = ImGui::GetFrameHeight();
    const float combo_pad = 2.f * style.FramePadding.x + arrow_w;

    float names_w = 0.f;
    for (int n = 0; n < int(Annotation::Shape::COUNT); ++n)
        names_w = std::max(names_w, ImGui::CalcTextSize(annotation_shape_name(Annotation::Shape(n))).x);

    const float combo_narrow = ImGui::IconSize().x + combo_pad;
    const float combo_wide   = ImGui::IconSize().x + style.ItemInnerSpacing.x + names_w + combo_pad;

    // "Add:" and the picker share a column, separated by their own spacing: in columns of their own the
    // cell padding between them would set the label further from what it labels than from the drag beside.
    const float add_text_w = ImGui::CalcTextSize("Add:").x + style.ItemInnerSpacing.x;
    const float colors_w   = ImGui::GetFrameHeight();
    const float width_min  = EmSize(3.5f);

    // Spare width all goes to the width drag, until there is enough of it for the picker to spell its
    // names out as well.
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool  named = avail >= colors_w + width_min + add_text_w + combo_wide + 3.f * style.CellPadding.x;
    const float add_w = add_text_w + (named ? combo_wide : combo_narrow);

    if (ImGui::BeginTable("##AnnotationControls", 3,
                          ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX |
                              ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("add", ImGuiTableColumnFlags_WidthFixed, add_w);
        ImGui::TableSetupColumn("colors", ImGuiTableColumnFlags_WidthFixed, colors_w);
        ImGui::TableSetupColumn("width", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableNextRow();

        draw_shape_picker(named);

        ImGui::TableNextColumn();
        bool restyled = stroke_fill_swatches("Colors", a.stroke_color, a.fill_color);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        restyled |= ImGui::DragFloat("##width", &a.stroke_width, 0.05f, 0.5f, 32.f, "%.1f px");
        ImGui::SetItemTooltip("Stroke width in screen pixels, so it does not change with zoom.");

        // Restyling the annotation in hand also sets what the next one will look like, so a color or a
        // width chosen once carries forward instead of being forgotten when the selection is dropped.
        if (restyled && &a != &m_annotation_style)
        {
            m_annotation_style.stroke_color = a.stroke_color;
            m_annotation_style.fill_color   = a.fill_color;
            m_annotation_style.stroke_width = a.stroke_width;
        }

        ImGui::EndTable();
    }
}

std::vector<std::pair<int, int>> HDRViewApp::selected_targets() const
{
    std::vector<std::pair<int, int>> out;
    for (size_t i : m_visible_images)
    {
        const auto &img = m_images[i];
        for (int g : img->selected_groups())
            if (img->groups[size_t(g)].visible)
                out.emplace_back(int(i), g);
    }
    return out;
}

std::vector<ImagePtr> HDRViewApp::selected_images()
{
    std::vector<ImagePtr> out;
    for (size_t i : m_visible_images)
        if (m_images[i]->is_selected())
            out.push_back(m_images[i]);

    // Visible only, and the current image is always both visible and selected, so an empty result means the
    // panel has never had a say.
    if (out.empty())
        if (auto img = current_image())
            out.push_back(img);

    return out;
}

void HDRViewApp::set_current_image_index(int index, bool force)
{
    if (!(force || is_valid(index)))
        return;

    if (!is_valid(index))
    {
        m_current = index;
        return;
    }

    set_current_group(index, m_images[size_t(index)]->selected_group);
}

void HDRViewApp::set_current_group(int index, int group)
{
    m_current = index;

    auto img = image(index);
    if (!img)
        return;

    if (img->is_valid_group(group))
        img->selected_group = group;

    // Nothing to reconcile against while a channel filter leaves the image with no group to show.
    if (!img->is_valid_group(img->selected_group))
        return;

    // If the target wasn't already selected, deselect the others: a click outside the selection starts a
    // new one, while one inside it only moves current.
    if (!img->is_group_selected(img->selected_group))
    {
        for (auto &i : m_images) i->deselect_all();
        img->select_group(img->selected_group);
    }
}

void HDRViewApp::toggle_group_selected(int index, int group)
{
    // logic:
    // if the target is not selected, then select it
    // if the target is already selected, then deselect it (but only if some other target is selected)
    //   if it was also the current target, then need to find a different current one from the selected

    auto img = image(index);
    if (!img || !img->is_valid_group(group))
        return;

    if (!img->is_group_selected(group))
    {
        img->select_group(group);
        return;
    }

    auto selected = selected_targets();
    if (selected.size() < 2)
        return;

    img->select_group(group, false);

    if (index == m_current && group == img->selected_group)
        for (const auto &[i, g] : selected)
            if (i != index || g != group)
            {
                set_current_group(i, g);
                break;
            }
}

void HDRViewApp::select_image_range_to(int index)
{
    auto pos = [this](int i)
    {
        auto it = std::find(m_visible_images.begin(), m_visible_images.end(), size_t(i));
        return it == m_visible_images.end() ? -1 : int(it - m_visible_images.begin());
    };

    const int to = pos(index);
    if (to < 0)
        return;

    const int current = pos(m_current);
    const int from    = current < 0 ? to : current;
    for (int v = std::min(from, to); v <= std::max(from, to); ++v)
    {
        auto &img = m_images[m_visible_images[size_t(v)]];
        img->select_group(img->selected_group);
    }

    // The far end is selected by the loop above, so set_current_image_index() moves current into the range
    // without collapsing the selection onto it.
    set_current_image_index(index);
}

void HDRViewApp::select_group_range_to(int index, int group)
{
    // Every visible target, in the order the panel lists them: images in list order, and within each, its
    // groups in the order build_layers_and_groups() created them, which is layer order.
    std::vector<std::pair<int, int>> targets;
    for (size_t i : m_visible_images)
        for (int g = 0; g < (int)m_images[i]->groups.size(); ++g)
            if (m_images[i]->groups[size_t(g)].visible)
                targets.emplace_back(int(i), g);

    auto pos = [&targets](int i, int g)
    {
        auto it = std::find(targets.begin(), targets.end(), std::pair{i, g});
        return it == targets.end() ? -1 : int(it - targets.begin());
    };

    const int to = pos(index, group);
    if (to < 0)
        return;

    auto      cur     = current_image();
    const int current = cur ? pos(m_current, cur->selected_group) : -1;
    const int from    = current < 0 ? to : current;

    for (int t = std::min(from, to); t <= std::max(from, to); ++t)
        m_images[size_t(targets[size_t(t)].first)]->select_group(targets[size_t(t)].second);

    set_current_group(index, group);
}

void HDRViewApp::update_visibility()
{
    // compute image:channel visibility and update selection indices
    static vector<string> visible_image_names;
    visible_image_names.resize(0);
    m_visible_images.resize(0);
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        auto        &img    = m_images[i];
        const string prefix = img->partname + (img->partname.empty() ? "" : ".");

        // compute visibility of all groups
        img->any_groups_visible = false;
        for (auto &g : img->groups)
        {
            // check if any of the contained channels in the group pass the channel filter
            g.visible = false;
            for (int c = 0; c < g.num_channels && !g.visible; ++c)
                g.visible |= m_channel_filter.PassFilter((prefix + img->channels[g.channels[c]].name).c_str());
            img->any_groups_visible |= g.visible;
        }

        // an image is visible if its filename passes the file filter and it has at least one visible group
        img->visible = m_file_filter.PassFilter(img->filename.c_str()) && img->any_groups_visible;

        if (img->visible)
        {
            visible_image_names.emplace_back(img->file_and_partname());
            m_visible_images.push_back(i);
        }

        img->root.calculate_visibility(img.get());

        // if the selected group is hidden, select the next visible group
        if (img->is_valid_group(img->selected_group) && !img->groups[img->selected_group].visible)
        {
            auto old = img->selected_group;
            if ((img->selected_group = img->next_visible_group_index(img->selected_group, Direction_Forward)) == old)
                img->selected_group = -1; // no visible groups left
        }

        // if the reference group is hidden, clear it
        // TODO: keep it, but don't display it
        if (img->is_valid_group(img->reference_group) && !img->groups[img->reference_group].visible)
            img->reference_group = -1;
    }

    // go to the next visible image if the current one is hidden
    if (!is_valid(m_current) || !m_images[m_current]->visible)
    {
        auto old = m_current;
        if ((m_current = next_visible_image_index(m_current, Direction_Forward)) == old)
            m_current = -1; // no visible images left
    }

    // if the reference is hidden, clear it
    // TODO: keep it, but don't display it
    if (is_valid(m_reference) && !m_images[m_reference]->visible)
        m_reference = -1;

    //
    // compute short (i.e. unique) names for visible images

    // m_visible_images and visible_image_names were appended to together above, so they index each other.
    auto short_names = shorten_names(visible_image_names);
    for (size_t n = 0; n < m_visible_images.size(); ++n) m_images[m_visible_images[n]]->short_name = short_names[n];

    // Filtering moves current and the group it shows by assignment above, not through set_current_group(),
    // so the rule that the current target is selected is restored here.
    if (auto img = current_image())
        set_current_group(m_current, img->selected_group);

    set_image_textures();
}

void HDRViewApp::draw_file_window()
{
    if (ImGui::BeginCombo("Mode", blend_mode_names()[m_blend_mode].c_str(), ImGuiComboFlags_HeightLargest))
    {
        for (BlendMode n = 0; n < BlendMode_COUNT; ++n)
        {
            const bool is_selected = (m_blend_mode == n);
            if (ImGui::Selectable(blend_mode_names()[n].c_str(), is_selected))
            {
                m_blend_mode = (BlendMode_)n;
                spdlog::debug("Switching to blend mode {}.", n);
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Channel", channel_names()[m_channel].c_str(), ImGuiComboFlags_HeightLargest))
    {
        for (Channels n = 0; n < Channels_COUNT; ++n)
        {
            const bool is_selected = (m_channel == n);
            if (ImGui::Selectable(channel_names()[n].c_str(), is_selected))
            {
                m_channel = (Channels_)n;
                spdlog::debug("Switching to channel {}.", n);
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // if (!num_images())
    //     return;

    static char filter_buffer[256] = {0};

    const ImVec2 button_size = ImGui::IconButtonSize();

    bool show_button = m_file_filter.IsActive() || m_channel_filter.IsActive(); // save here to avoid flicker
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2.f * (button_size.x + ImGui::GetStyle().ItemSpacing.x));
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InputTextWithHint("##file filter", ICON_MY_FILTER " Filter 'file pattern:channel pattern'",
                                 filter_buffer, IM_ARRAYSIZE(filter_buffer)))
    {
        // copy everything before first ':' into m_file_filter.InputBuf, and everything after into
        // m_channel_filter.InputBuf
        if (auto colon = strchr(filter_buffer, ':'))
        {
            int file_filter_length    = int(colon - filter_buffer + 1);
            int channel_filter_length = IM_ARRAYSIZE(filter_buffer) - file_filter_length;
            ImStrncpy(m_file_filter.InputBuf, filter_buffer, file_filter_length);
            ImStrncpy(m_channel_filter.InputBuf, colon + 1, channel_filter_length);
        }
        else
        {
            ImStrncpy(m_file_filter.InputBuf, filter_buffer, IM_ARRAYSIZE(m_file_filter.InputBuf));
            m_channel_filter.InputBuf[0] = 0; // Clear channel filter if no colon is found
        }

        m_file_filter.Build();
        m_channel_filter.Build();

        update_visibility();
    }
    ImGui::Tooltip(
        "Filter visible images and channel groups.\n\nOnly images with filenames matching the file pattern and "
        "channels matching the channel pattern will be shown. A pattern is a comma-separated list of strings "
        "that must be included or excluded (if prefixed with a '-').");
    if (show_button)
    {
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
        if (ImGui::IconButton(ICON_MY_DELETE))
        {
            m_file_filter.Clear();
            m_channel_filter.Clear();
            filter_buffer[0] = 0;
            update_visibility();
        }
    }

    ImGui::SameLine();
    if (ImGui::IconButton(m_short_names ? ICON_MY_SHORT_NAMES "##short names button"
                                        : ICON_MY_FULL_NAMES "##short names button"))
        m_short_names = !m_short_names;
    ImGui::Tooltip(m_short_names ? "Click to show full filenames."
                                 : "Click to show only the unique portion of each file name.");

    static const string s_view_mode_icons[] = {ICON_MY_NO_CHANNEL_GROUP, ICON_MY_LIST_VIEW, ICON_MY_TREE_VIEW};

    ImGui::SameLine();
    if (ImGui::BeginComboButton("##channel list mode", s_view_mode_icons[m_file_list_mode].data()))
    {
        auto old_mode = m_file_list_mode;
        if (ImGui::Selectable((s_view_mode_icons[0] + " Only images (do not list channel groups)").c_str(),
                              m_file_list_mode == 0))
            m_file_list_mode = 0;
        if (ImGui::Selectable((s_view_mode_icons[1] + " Flat list of layers and channels").c_str(),
                              m_file_list_mode == 1))
            m_file_list_mode = 1;
        if (ImGui::Selectable((s_view_mode_icons[2] + " Tree view of layers and channels").c_str(),
                              m_file_list_mode == 2))
            m_file_list_mode = 2;

        if (old_mode != m_file_list_mode)
            m_scroll_to_next_frame = 0.5f;

        ImGui::EndCombo();
    }
    ImGui::Tooltip("Choose how the images and layers are listed below");

    static constexpr ImGuiTreeNodeFlags base_node_flags =
        ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DrawLinesFull;

    static constexpr ImGuiTableFlags table_flags =
        ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("ImageList", 2, table_flags,
                          ImVec2(0.f, ImGui::GetContentRegionAvail().y - ImGui::IconButtonSize().y -
                                          ImGui::GetStyle().ItemSpacing.y)))
    {
        const float icon_width = ImGui::IconSize().x;

        ImGui::TableSetupColumn(ICON_MY_LIST_OL,
                                ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_IndentDisable,
                                ImGui::GetTreeNodeToLabelSpacing());
        ImGui::TableSetupColumn(m_file_list_mode ? "File:part or channel group" : "File:part.layer.channel group",
                                ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
        ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
        ImGui::TableHeadersRow();

        ImGuiSortDirection direction = ImGuiSortDirection_None;
        if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs())
            if (sort_specs->SpecsCount)
            {
                direction = sort_specs->Specs[0].SortDirection;
                if (sort_specs->SpecsDirty || m_request_sort)
                {
                    spdlog::debug("Sorting {}", (int)direction);
                    auto old_current   = current_image();
                    auto old_reference = reference_image();
                    sort(m_images.begin(), m_images.end(),
                         [direction](const ImagePtr &a, const ImagePtr &b)
                         {
                             return (direction == ImGuiSortDirection_Ascending)
                                        ? a->file_and_partname() < b->file_and_partname()
                                        : a->file_and_partname() > b->file_and_partname();
                         });

                    // restore selection
                    if (old_current)
                        m_current = int(find(m_images.begin(), m_images.end(), old_current) - m_images.begin());
                    if (old_reference)
                        m_reference = int(find(m_images.begin(), m_images.end(), old_reference) - m_images.begin());
                }

                sort_specs->SpecsDirty = m_request_sort = false;
            }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, icon_width);
        ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0.f);

        int id             = 0;
        int hidden_groups  = 0;
        int image_to_close = -1;

        // The image rows' font depends only on the list mode, so push it once around the whole list, before
        // the clipper is set up, so the row height it measures is the height rows are drawn at.
        ImGui::PushFont(m_file_list_mode == 0 ? m_sans_regular : m_sans_bold, ImGui::GetStyle().FontSizeBase);

        // currently we only support the clipper when each image is one row
        bool             use_clipper = m_file_list_mode == 0;
        ImGuiListClipper clipper;
        if (use_clipper)
        {
            clipper.Begin((int)m_visible_images.size());

            // A pending scroll-to-selection request is consumed inside the per-row loop below, which the
            // clipper only enters for rows it decided to draw. Without this, a request made while the target
            // is scrolled out of view would never reach its SetScrollHereY() call and would stay pending.
            if (m_scroll_to_next_frame >= -0.5f && is_valid(m_current))
            {
                auto it = find(m_visible_images.begin(), m_visible_images.end(), (size_t)m_current);
                if (it != m_visible_images.end())
                {
                    int vi = int(it - m_visible_images.begin());
                    clipper.IncludeItemsByIndex(vi, vi + 1);
                }
            }
        }
        // the loop conditions here are to execute this outer loop once if we are not using the clipper, and execute it
        // as long as clipper.Step() returns true otherwise
        for (int iter = 0; (!use_clipper && iter < 1) || (use_clipper && clipper.Step()); ++iter)
        {
            int start = use_clipper ? clipper.DisplayStart : 0;
            int end   = use_clipper ? clipper.DisplayEnd : (int)m_visible_images.size();
            for (int vi = start; vi < end; ++vi)
            {
                int   i            = (int)m_visible_images[vi];
                auto &img          = m_images[i];
                bool  is_current   = m_current == i;
                bool  is_reference = m_reference == i;
                bool  is_selected  = img->is_selected();

                ImGuiTreeNodeFlags node_flags = base_node_flags;

                if (is_current || is_reference || is_selected)
                    node_flags |= ImGuiTreeNodeFlags_Selected;
                if (m_file_list_mode == 0)
                    node_flags |= ImGuiTreeNodeFlags_Leaf;

                auto  &selected_group = img->groups[img->active_group_index(
                    is_reference && !is_current ? Target_Secondary : Target_Primary)];
                string group_name =
                    selected_group.num_channels == 1 ? selected_group.name : "(" + selected_group.name + ")";
                auto  &channel    = img->channels[selected_group.channels[0]];
                string layer_path = Channel::head(channel.name);
                string filename   = (m_short_names ? img->short_name : img->file_and_partname()) +
                                  (m_file_list_mode ? "" : img->delimiter() + layer_path + group_name);

                // Marks edits that exist only in memory. Appended, not prefixed, because the name below is
                // truncated from the front.
                if (img->history.is_modified())
                    filename += " *";

                // Drawn with an empty label -- SpanAllColumns still makes this the row's click target -- so
                // the icon and front-truncated filename below can be laid out by hand afterward.
                bool open = ImGui::TreeRow((void *)(intptr_t)i, node_flags, "", [&]
                                           { ImGui::TextAligned2(1.0f, -FLT_MIN, fmt::format("{}", vi + 1).c_str()); },
                                           [&]
                                           {
                                               if (m_file_list_mode == 0)
                                                   ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                                               ImGui::PushRowColors(is_current, is_reference, ImGui::GetIO().KeyShift,
                                                                    is_selected);
                                           });
                auto icon = img->groups.size() > 1 ? ICON_MY_IMAGES : ICON_MY_IMAGE;
                ImGui::SameLine(0.f, 0.f);
                string the_text = ImGui::TruncatedText(filename, icon);

                // Add right-click context menu
                ImGui::PushFont(m_sans_regular, 0.f);
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Copy path to clipboard"))
                        ImGui::SetClipboardText(img->filename.c_str());

#if !defined(__EMSCRIPTEN__)
                    std::string menu_label = fmt::format(reveal_in_file_manager_text(), file_manager_name());
                    if (ImGui::MenuItem(menu_label.c_str()))
                    {
                        string fn, entry_fn;
                        split_zip_entry(img->filename, fn, entry_fn);
                        show_in_file_manager(fn.c_str());
                    }
#endif
                    // Select as current image
                    ImGui::BeginDisabled(is_current);
                    if (ImGui::MenuItem("Select as current image"))
                    {
                        set_current_image_index(i);
                        set_image_textures();
                    }
                    ImGui::EndDisabled();

                    // Select as reference image
                    if (ImGui::MenuItem(fmt::format("{} as reference image", is_reference ? "Unselect" : "Select")))
                    {
                        m_reference = is_reference ? -1 : i;
                        set_image_textures();
                    }

                    if (ImGui::MenuItem("Close image"))
                        image_to_close = i;

                    ImGui::EndPopup();
                }
                ImGui::PopFont();

                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                {
                    // Shift is the reference modifier, so the selection chords are ctrl/cmd and
                    // ctrl/cmd+shift. An image row stands for the group the image is showing.
                    auto &io = ImGui::GetIO();
                    if (io.KeyCtrl && io.KeyShift)
                        select_image_range_to(i);
                    else if (io.KeyCtrl)
                        toggle_group_selected(i, img->selected_group);
                    else if (io.KeyShift)
                        m_reference = is_reference ? -1 : i;
                    else
                        set_current_image_index(i);
                    set_image_textures();
                    spdlog::trace("Clicked image {}; current is {}, reference is {}", i, m_current, m_reference);
                }

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    // Set payload to carry the index of our item
                    ImGui::SetDragDropPayload("DND_IMAGE", &i, sizeof(int));

                    // Display preview
                    ImGui::TextUnformatted("Move here");
                    if (ImGui::BeginTable("MoveList", 2, table_flags))
                    {
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 1.25f * icon_width);
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextAligned2(1.0f, -FLT_MIN, fmt::format("{}", vi + 1).c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text(the_text);
                        ImGui::EndTable();
                    }
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_IMAGE"))
                    {
                        IM_ASSERT(payload->DataSize == sizeof(int));
                        int payload_i = *(const int *)payload->Data;

                        // move image at payload_i to i, and shift all images in between
                        if (payload_i < i)
                            for (int j = payload_i; j < i; ++j) swap(m_images[j], m_images[j + 1]);
                        else
                            for (int j = payload_i; j > i; --j) swap(m_images[j], m_images[j - 1]);

                        // maintain the current and reference images
                        if (m_current == payload_i)
                            m_current = i;
                        if (m_reference == payload_i)
                            m_reference = i;

                        ImGui::TableSetColumnSortDirection(0, ImGuiSortDirection_None, false);
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::TextUnformatted(icon);
                ImGui::SameLine(0.f, 0.f);
                ImGui::TextAligned2(1.0f, -FLT_MIN, the_text.c_str());

                if (open)
                {
                    ImGui::PushFont(m_sans_regular, 0.f);
                    int visible_groups = 1;
                    if (m_file_list_mode == 0)
                    {
                        ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                        if (is_current && m_scroll_to_next_frame >= -0.5f)
                        {
                            if (!ImGui::IsItemVisible())
                                ImGui::SetScrollHereY(m_scroll_to_next_frame);
                            m_scroll_to_next_frame = -1.f;
                        }
                    }
                    else if (m_file_list_mode == 1)
                    {
                        visible_groups =
                            img->draw_channel_rows(i, id, is_current, is_reference, m_scroll_to_next_frame);
                        MY_ASSERT(visible_groups == img->root.visible_groups,
                                  "Unexpected number of visible groups; {} != {}", visible_groups,
                                  img->root.visible_groups);
                    }
                    else
                    {
                        visible_groups =
                            img->draw_channel_tree(i, id, is_current, is_reference, m_scroll_to_next_frame);
                        MY_ASSERT(visible_groups == img->root.visible_groups,
                                  "Unexpected number of visible groups; {} != {}", visible_groups,
                                  img->root.visible_groups);
                    }

                    hidden_groups += (int)img->groups.size() - visible_groups;

                    ImGui::PopFont();

                    ImGui::TreePop();
                }
            }
        }
        ImGui::PopFont();

        int hidden_images = num_images() - num_visible_images();
        if (hidden_images || hidden_groups)
        {
            ImGui::BeginDisabled();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // ImGui::TextUnformatted(ICON_MY_VISIBILITY_OFF);
            ImGui::TableNextColumn();
            auto images_str = hidden_images > 1 ? "s" : "";
            auto groups_str = hidden_groups > 1 ? "s" : "";
            if (hidden_groups)
            {
                if (hidden_images)
                    ImGui::TextFmt("{} {} image{} and {} channel group{} hidden", ICON_MY_VISIBILITY_OFF, hidden_images,
                                   images_str, hidden_groups, groups_str);
                else
                    ImGui::TextFmt("{} {} channel group{} hidden", ICON_MY_VISIBILITY_OFF, hidden_groups, groups_str);
            }
            else
                ImGui::TextFmt("{} {} image{} hidden", ICON_MY_VISIBILITY_OFF, hidden_images, images_str);
            ImGui::EndDisabled();
        }

        if (image_to_close >= 0)
            close_image(image_to_close);

        ImGui::PopStyleVar(3);

        ImGui::EndTable();
    }

    {
        IconButton(action("Play backward"));

        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

        IconButton(action("Stop playback"));

        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

        IconButton(action("Play forward"));

        ImGui::SameLine();

        ImGui::SetNextItemWidth(std::max(EmSize(1.f), ImGui::GetContentRegionAvail().x));
        if (ImGui::SliderFloat("##Playback speed", &m_playback_speed, 0.1f, 60.f, "%.3f fps",
                               ImGuiInputTextFlags_EnterReturnsTrue))
            m_playback_speed = clamp(m_playback_speed, 1.f / 20.f, 60.f);
    }
    // ImGui::EndDisabled();
}
