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
void HDRViewApp::enable_gui_test_engine(void (*register_tests)(ImGuiTestEngine *))
{
    // The test binary drives a real HDRViewApp, which otherwise reads and rewrites the very settings file
    // the installed HDRView uses: a run would leave its fixture images in the recent-file list and persist
    // whatever exposure, gamma and window layout the tests happened to leave behind. iniDisable makes
    // IniSettingsLocation() return nothing, which both the ImGui layout and HelloImGui's LoadUserPref/
    // SaveUserPref (the "UserSettings" JSON in setup_persistence_callbacks) treat as "don't". Tests then
    // start from the built-in defaults every run, which is what they should be asserting against anyway.
    m_params.iniDisable = true;

    m_params.useImGuiTestEngine      = true;
    m_params.callbacks.RegisterTests = [register_tests]()
    {
        ImGuiTestEngine   *engine = GetImGuiTestEngine();
        ImGuiTestEngineIO &io     = ImGuiTestEngine_GetIO(engine);
        // Defaults to off, since it's meant for the interactive Test Engine UI; this binary has no other way
        // to report *why* a test failed when run in headless/CI (-nogui) mode.
        io.ConfigLogToTTY            = true;
        io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
        // Hello ImGui's own test-engine Setup() (called before this callback runs) hardcodes
        // ConfigRunSpeed = Normal ("slowest mode in this demo" - it's tuned for a watchable interactive
        // demo, not headless test runs). Normal/Cinematic speed animates every simulated mouse move over
        // many real frames (see MouseMoveToPos() in imgui_te_context.cpp); Fast teleports it in 2 frames.
        // Override back to Fast, since nothing else resets it after this point.
        io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
        register_tests(engine);
        ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, nullptr, ImGuiTestRunFlags_RunFromCommandLine);
    };
    // HelloImGui's own PostSwap hook steps the running test forward each frame; here we just watch for the
    // queue draining and ask the app to exit once every queued test has finished, the same way a window-close
    // request does (via m_params.appShallExit) rather than forcing an abrupt process exit.
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

    // Anchor to a corner of the central dockspace node rather than the whole window, so the palette floats
    // over the image and never over the docked panels. calculate_viewport() has already run this frame,
    // from the CustomBackground callback, so the rect below is current.
    const float2 pivot{(m_tool_palette_corner & 1) ? 1.f : 0.f, (m_tool_palette_corner & 2) ? 1.f : 0.f};
    const float2 margin{EmSize(0.5f)};
    const float2 anchor = m_viewport_min + margin + pivot * max(m_viewport_size - 2.f * margin, float2{0.f});

    // While the user is dragging the palette, ImGui owns its position; we take it back on release and snap
    // it to whichever corner it landed nearest.
    if (!m_tool_palette_dragging)
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, pivot);

    // Whole pixels: ImGui truncates the layout cursor to integers as it stacks items, so fractional padding
    // or spacing drifts out of step with the auto-fit size and leaves the bottom edge tighter than the top.
    const float gap = ImTrunc(ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImTrunc(EmSize(0.3f)), ImTrunc(EmSize(0.3f))));

    // Opaque: the palette sits over image content of arbitrary brightness, which at a high exposure would
    // wash a translucent background right out.
    ImGui::SetNextWindowBgAlpha(1.f);

    // ImGui owns the collapsed state from here on; seed it from ours once, and read it back below.
    ImGui::SetNextWindowCollapsed(m_tool_palette_collapsed, ImGuiCond_Once);

    // ImGui renders the title bar inside Begin() and sizes its collapse arrow from the current font, which
    // has no style var of its own, so shrink the font to shrink the arrow. Padding makes up the difference,
    // keeping the bar a standard frame tall and - since ImGui insets the arrow by that same padding - the
    // arrow centred within it.
    const float bar_height = ImGui::GetFrameHeight();
    ImGui::PushFont(nullptr, 0.75f * ImGui::GetStyle().FontSizeBase);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ImGui::GetStyle().FramePadding.x, 0.5f * (bar_height - ImGui::GetFontSize())));

    // The stock title bar supplies the drag handle and the collapse arrow, and a "##" name suppresses its
    // text. No close button: it would widen the palette past its buttons, and the Windows menu already
    // hides it. AlwaysAutoResize keeps this a compact box hugging its buttons rather than a full-width bar;
    // NoDocking is required, since the app runs in ProvideFullScreenDockSpace mode and the dockspace would
    // otherwise swallow the window; NoSavedSettings keeps a stale position in imgui.ini from fighting the
    // anchoring above.
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

    // Dragging the title bar has ImGui move the window itself; asking it which window that is beats
    // inferring it from mouse state, since the title bar is an item of its own and so defeats the usual
    // "pressed the background" tests. Outside the open block, so the drop still registers on a frame the
    // window is collapsed or clipped away.
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

void HDRViewApp::draw_statistics_window()
{
    if (!current_image())
    {
        ImGui::TextDisabled("No image loaded.");
        return;
    }

    current_image()->draw_histogram();

    // Label column of the Statistics and Watched pixels tables below: bold, to read as a header against the
    // values beside it (as the channel-name row drawn by ChannelValuesRowHeader() already does).
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
                             // Same width-budget shape as ChannelValuesRow (N boxes + a trailing swatch-
                             // sized slot), but these boxes are genuinely editable (DragInt, not read-only
                             // text) and the trailing slot holds a "clear the selection" close button
                             // instead of a color swatch.
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
                             ImGui::SetItemTooltip("W x H: (%d x %d)", m_roi_live.size().x, m_roi_live.size().y);
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
    // disabled for the hovered pixel -- ReadOnly alone doesn't block DragInt's drag gesture, only keyboard
    // entry) plus, for watched pixels, a trailing delete button. Children (open only) are Current/Reference/
    // Composite ChannelValuesRow entries. Returns true if the delete button was clicked.
    auto PixelTreeNodePE = [&](const string &icon_title, int2 &pixel, int3 &color_mode, bool editable, bool show_delete)
    {
        bool deleted = false;
        // SpanAllColumns extends the tree node's own click rect across the whole row (see TreeNodeBehavior()
        // in imgui_widgets.cpp), processed before the X/Y/delete controls below are drawn -- on its own, a
        // click meant for them would toggle the tree node instead. AllowOverlap defers that first claim,
        // letting a later, geometrically overlapping widget take the click instead: the same fix
        // CollapsingHeader(label, p_visible, ...) applies whenever it's given a close button.
        bool open = ImGui::PE::TreeNode(icon_title.c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth |
                                            ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::TableNextColumn();
        // Row's own top-left, before anything else is drawn in this column -- the reference point for the
        // close button below, matching CollapsingHeader's own (g.LastItemData.Rect.Min.y + FramePadding.y).
        ImVec2 row_screen_pos = ImGui::GetCursorScreenPos();

        float col_w   = ImGui::PE::ColumnWidth(1);
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        // GetFontSize(), not GetFrameHeight(): the children rows below (Current/Reference/Composite) are
        // drawn compact (FramePadding.y == 0, where FrameHeight == FontSize), so their ChannelValuesRow
        // swatch slot is FontSize wide. This row's own FramePadding is still the ambient (normal) one at
        // this point -- using GetFrameHeight() here would reserve a *larger* slot than the children's,
        // shifting the close button left of where their swatches actually sit.
        float sz = ImGui::GetFontSize();
        // Two inter-item gaps (X-to-Y, Y-to-close-slot), not one -- always reserve the same swatch-sized
        // slot ChannelValuesRow reserves for its color swatch (real close button here, or nothing for the
        // Mouse row), so the X/Y boxes end at the same place regardless of show_delete, matching the
        // channel value boxes' own column width below them.
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
        // Initial (still user-resizable) label-column width: fits the widest label ("Maximum"), matching
        // the pattern draw_info() uses for its own "Property" column. Measured in the bold label font the
        // rows themselves use, so the labels fit without truncation.
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
        // ("Composite") rather than the (typically shorter) top-level tree node labels.
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
            // is needed to keep the X/Y/delete controls' IDs (drawn unconditionally below) stable across
            // collapsed rows too.
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

    // m_visible_images and visible_image_names were appended to together above, so they index each other:
    // one shortened name per visible image, in the same order.
    auto short_names = shorten_names(visible_image_names);
    for (size_t n = 0; n < m_visible_images.size(); ++n)
        m_images[m_visible_images[n]]->short_name = short_names[n];

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

        // The image rows' font depends only on the list mode, so it's pushed once around the whole list
        // rather than per row. Pushed before the clipper is set up so that the row height it measures is
        // the height rows are actually drawn at.
        ImGui::PushFont(m_file_list_mode == 0 ? m_sans_regular : m_sans_bold, ImGui::GetStyle().FontSizeBase);

        // currently we only support the clipper when each image is one row
        bool             use_clipper = m_file_list_mode == 0;
        ImGuiListClipper clipper;
        if (use_clipper)
        {
            clipper.Begin((int)m_visible_images.size());

            // A pending scroll-to-selection request is consumed inside the per-row loop below, which the
            // clipper only enters for rows it decided to draw. Without this, a request made while the
            // target is scrolled out of view (e.g. a next/prev-image shortcut) would never reach its
            // SetScrollHereY() call, and -- since the clipper computes the same range again next frame --
            // would stay pending forever, until the user manually scrolled the target back into view.
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

                ImGuiTreeNodeFlags node_flags = base_node_flags;

                if (is_current || is_reference)
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

                // Drawn with an empty label -- SpanAllColumns still makes this the row's click target -- so
                // the icon and front-truncated filename below can be laid out and drawn by hand afterward.
                bool open = ImGui::TreeRow((void *)(intptr_t)i, node_flags, "", [&]
                                           { ImGui::TextAligned2(1.0f, -FLT_MIN, fmt::format("{}", vi + 1).c_str()); },
                                           [&]
                                           {
                                               if (m_file_list_mode == 0)
                                                   ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                                               ImGui::PushRowColors(is_current, is_reference, ImGui::GetIO().KeyShift);
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
                        m_current = i;
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
                    if (ImGui::GetIO().KeyShift)
                        m_reference = is_reference ? -1 : i;
                    else
                        m_current = i;
                    set_image_textures();
                    spdlog::trace("Setting image {} to the {} image", i, is_reference ? "reference" : "current");
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
        if (ImGui::SliderFloat("##Playback speed", &m_playback_speed, 0.1f, 60.f, "%.1f fps",
                               ImGuiInputTextFlags_EnterReturnsTrue))
            m_playback_speed = clamp(m_playback_speed, 1.f / 20.f, 60.f);
    }
    // ImGui::EndDisabled();
}
