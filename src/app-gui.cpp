#include "app.h"

#include "colormap.h"
#include "fonts.h"
#include "hello_imgui/hello_imgui.h"
#include "imcmd_command_palette.h"
#include "imgui_internal.h"
#include "implot.h"
#include "texture.h"
#include "version.h"

#include "platform_utils.h"

using namespace std;

void HDRViewApp::run()
{
    ImPlot::CreateContext();
    HelloImGui::Run(m_params);
    ImPlot::DestroyContext();
}

static void pixel_color_widget(const int2 &pixel, int &color_mode, int which_image, bool allow_copy = false,
                               float width = 0.f)
{
    float4   color32         = hdrview()->pixel_value(pixel, true, which_image);
    float4   displayed_color = linear_to_sRGB(hdrview()->pixel_value(pixel, false, which_image));
    uint32_t hex             = color_f128_to_u32(color_u32_to_f128(color_f128_to_u32(displayed_color)));
    int4     ldr_color       = int4{float4{color_u32_to_f128(hex)} * 255.f};
    bool3    inside          = {false, false, false};

    float start_x = ImGui::GetCursorPosX();

    int    components       = 4;
    string channel_names[4] = {"R", "G", "B", "A"};
    if (which_image != 2)
    {
        ConstImagePtr img;
        ChannelGroup  group;
        if (which_image == 0)
        {
            if (!hdrview()->current_image())
                return;
            img        = hdrview()->current_image();
            components = color_mode == 0 ? img->groups[img->selected_group].num_channels : 4;
            group      = img->groups[img->selected_group];
            inside[0]  = img->contains(pixel);
        }
        else if (which_image == 1)
        {
            if (!hdrview()->reference_image())
                return;
            img        = hdrview()->reference_image();
            components = color_mode == 0 ? img->groups[img->reference_group].num_channels : 4;
            group      = img->groups[img->reference_group];
            inside[1]  = img->contains(pixel);
        }

        if (color_mode == 0)
            for (int c = 0; c < components; ++c)
                channel_names[c] = Channel::tail(img->channels[group.channels[c]].name);
    }
    inside[2] = inside[0] || inside[1];

    ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf;
    if (ImGui::ColorButton("colorbutton", displayed_color, color_flags))
        ImGui::OpenPopup("dropdown");
    ImGui::SetItemTooltip("Click to change value format%s", allow_copy ? " or copy to clipboard." : ".");

    // ImGui::PushFont(sans_font);
    if (ImGui::BeginPopup("dropdown"))
    {
        if (allow_copy && ImGui::Selectable("Copy to clipboard"))
        {
            string buf;
            if (color_mode == 0)
            {
                if (components == 4)
                    buf = fmt::format("({:g}, {:g}, {:g}, {:g})", color32.x, color32.y, color32.z, color32.w);
                else if (components == 3)
                    buf = fmt::format("({:g}, {:g}, {:g})", color32.x, color32.y, color32.z);
                else if (components == 2)
                    buf = fmt::format("({:g}, {:g})", color32.x, color32.y);
                else
                    buf = fmt::format("{:g}", color32.x);
            }
            else if (color_mode == 1)
                buf = fmt::format("({:g}, {:g}, {:g}, {:g})", displayed_color.x, displayed_color.y, displayed_color.z,
                                  displayed_color.w);
            else if (color_mode == 2)
                buf = fmt::format("({:d}, {:d}, {:d}, {:d})", ldr_color.x, ldr_color.y, ldr_color.z, ldr_color.w);
            else if (color_mode == 3)
                buf = fmt::format("#{:02X}{:02X}{:02X}{:02X}", ldr_color.x, ldr_color.y, ldr_color.z, ldr_color.w);
            ImGui::SetClipboardText(buf.c_str());
        }
        ImGui::SeparatorText("Display as:");
        if (ImGui::Selectable("Raw values", color_mode == 0))
            color_mode = 0;
        if (ImGui::Selectable("Displayed color (32-bit)", color_mode == 1))
            color_mode = 1;
        if (ImGui::Selectable("Displayed color (8-bit)", color_mode == 2))
            color_mode = 2;
        if (ImGui::Selectable("Displayed color (hex)", color_mode == 3))
            color_mode = 3;

        ImGui::EndPopup();
    }

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

    float w_full = (width == 0.f) ? ImGui::GetContentRegionAvail().x : width - (ImGui::GetCursorPosX() - start_x);
    // width available to all items (without spacing)
    float w_items    = w_full - ImGui::GetStyle().ItemInnerSpacing.x * (components - 1);
    float prev_split = w_items;
    // distributes the available width without jitter during resize
    auto set_item_width = [&prev_split, w_items, components](int c)
    {
        float next_split = ImMax(IM_TRUNC(w_items * (components - 1 - c) / components), 1.f);
        ImGui::SetNextItemWidth(ImMax(prev_split - next_split, 1.0f));
        prev_split = next_split;
    };

    ImGui::BeginDisabled(!inside[which_image]);
    ImGui::BeginGroup();
    if (color_mode == 0)
    {
        for (int c = 0; c < components; ++c)
        {
            if (c > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

            set_item_width(c);
            ImGui::InputFloat(fmt::format("##component {}", c).c_str(), &color32[c], 0.f, 0.f,
                              fmt::format("{}: %g", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 1)
    {
        for (int c = 0; c < components; ++c)
        {
            if (c > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

            set_item_width(c);
            ImGui::InputFloat(fmt::format("##component {}", c).c_str(), &displayed_color[c], 0.f, 0.f,
                              fmt::format("{}: %g", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 2)
    {
        for (int c = 0; c < components; ++c)
        {
            if (c > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

            set_item_width(c);
            ImGui::InputScalarN(fmt::format("##component {}", c).c_str(), ImGuiDataType_S32, &ldr_color[c], 1, NULL,
                                NULL, fmt::format("{}: %d", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 3)
    {
        ImGui::SetNextItemWidth(IM_TRUNC(w_full));
        ImGui::InputScalar("##hex color", ImGuiDataType_S32, &hex, NULL, NULL, "#%08X", ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::EndGroup();
    ImGui::EndDisabled();
}

void HDRViewApp::draw_status_bar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));
    if (auto num = m_image_loader.num_pending_images())
    {
        // ImGui::PushFont(m_sans_regular, 8.f);
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), HelloImGui::EmToVec2(15.f, 0.f),
                           fmt::format("Loading {} image{}", num, num > 1 ? "s" : "").c_str());
        ImGui::SameLine();
        // ImGui::PopFont();
    }
    else if (m_remaining_download > 0)
    {
        ImGui::ScopedFont{nullptr, 4.0f};
        ImGui::ProgressBar((100 - m_remaining_download) / 100.f, HelloImGui::EmToVec2(15.f, 0.f), "Downloading image");
        ImGui::SameLine();
    }

    float x = ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x;

    auto sized_text = [&](float em_size, const string &text, float align = 1.f)
    {
        float item_width = HelloImGui::EmSize(em_size);
        float text_width = ImGui::CalcTextSize(text.c_str()).x;
        float spacing    = ImGui::GetStyle().ItemInnerSpacing.x;

        ImGui::SameLine(x + align * (item_width - text_width));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        x += item_width + spacing;
    };

    if (auto img = current_image())
    {
        auto &io          = ImGui::GetIO();
        auto  ref         = reference_image();
        bool  in_viewport = vp_pos_in_viewport(vp_pos_at_app_pos(io.MousePos));

        auto hovered_pixel = int2{pixel_at_app_pos(io.MousePos)};

        float4 top   = img->raw_pixel(hovered_pixel);
        float4 pixel = top;
        if (ref && ref->data_window.contains(hovered_pixel))
        {
            float4 bottom = ref->raw_pixel(hovered_pixel);
            // blend with reference image if available
            pixel = float4{blend(top.x, bottom.x, m_blend_mode), blend(top.y, bottom.y, m_blend_mode),
                           blend(top.z, bottom.z, m_blend_mode), blend(top.w, bottom.w, m_blend_mode)};
        }

        ImGui::BeginDisabled(!in_viewport);
        ImGui::SameLine();
        auto  fpy       = ImGui::GetStyle().FramePadding.y;
        float drag_size = HelloImGui::EmSize(5.f);
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
        auto y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##pixel x coordinates", &hovered_pixel.x, 1.f, 0, 0, "X: %d", ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetCursorPosY(y + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##pixel y coordinates", &hovered_pixel.y, 1.f, 0, 0, "Y: %d", ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleVar();
        ImGui::EndDisabled();

        x += 2.f * drag_size + 2.f * ImGui::GetStyle().ItemInnerSpacing.x;

        sized_text(0.5f, "=", 0.5f);

        ImGui::PushID("Current");
        ImGui::SameLine(x);
        pixel_color_widget(hovered_pixel, m_status_color_mode, 2, false, HelloImGui::EmSize(25.f));
        ImGui::PopID();

        float real_zoom = m_zoom * pixel_ratio();
        int   numer     = (real_zoom < 1.0f) ? 1 : (int)round(real_zoom);
        int   denom     = (real_zoom < 1.0f) ? (int)round(1.0f / real_zoom) : 1;
        x               = ImGui::GetIO().DisplaySize.x - HelloImGui::EmSize(11.f) -
            (m_show_FPS ? HelloImGui::EmSize(14.f) : HelloImGui::EmSize(0.f));
        sized_text(10.f, fmt::format("{:7.2f}% ({:d}:{:d})", real_zoom * 100, numer, denom));
    }

    if (m_show_FPS)
    {
        ImGui::SameLine(ImGui::GetIO().DisplaySize.x - 14.f * ImGui::GetFontSize());
        ImGui::Checkbox("Enable idling", &m_params.fpsIdling.enableIdling);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("FPS: %.1f%s", HelloImGui::FrameRate(), m_params.fpsIdling.isIdling ? " (Idling)" : "");
    }

    ImGui::PopStyleVar();
}

void HDRViewApp::draw_color_picker()
{
    if (!m_show_bg_color_picker)
        return;

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, 5.f * HelloImGui::EmSize()),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
    if (ImGui::Begin("Choose custom background color", &m_show_bg_color_picker,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
    {
        static float4 previous_bg_color = m_bg_color;
        if (ImGui::IsWindowAppearing())
            previous_bg_color = m_bg_color;
        ImGui::ColorPicker4("##Custom background color", (float *)&m_bg_color,
                            ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoAlpha,
                            (float *)&previous_bg_color);

        ImGui::Dummy(HelloImGui::EmToVec2(1.f, 0.5f));
        if (ImGui::Button("OK", HelloImGui::EmToVec2(5.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Enter))
            m_show_bg_color_picker = false;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", HelloImGui::EmToVec2(5.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Escape))
        {
            m_bg_color             = previous_bg_color;
            m_show_bg_color_picker = false;
        }
    }
    ImGui::End();
}

void HDRViewApp::draw_tweak_window()
{
    if (!m_show_tweak_window)
        return;

    // auto &tweakedTheme = HelloImGui::GetRunnerParams()->imGuiWindowParams.tweakedTheme;
    ImGui::SetNextWindowSize(HelloImGui::EmToVec2(20.f, 46.f), ImGuiCond_FirstUseEver);
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

void HDRViewApp::draw_develop_windows()
{
    if (m_show_demo_window)
    {
        ImGui::ShowDemoWindow(&m_show_demo_window);
        ImPlot::ShowMetricsWindow(&m_show_demo_window);
        ImPlot::ShowDemoWindow(&m_show_demo_window);
    }

    if (m_show_debug_window)
    {
        ImGui::SetNextWindowSize(HelloImGui::EmToVec2(20.f, 46.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug", &m_show_debug_window))
        {
            if (ImGui::BeginTabBar("Debug tabs", ImGuiTabBarFlags_None))
            {
                if (ImGui::BeginTabItem("Transfer functions"))
                {
                    static float            gamma = 2.2f;
                    static TransferFunction tf    = TransferFunction_Linear;
                    ImGui::DragFloat("Gamma", &gamma, 0.01f, 0.f);
                    if (ImGui::BeginCombo("##transfer function", transfer_function_name(tf, 1.f / gamma).c_str(),
                                          ImGuiComboFlags_HeightLargest))
                    {
                        for (TransferFunction_ n = TransferFunction_Linear; n < TransferFunction_Count; ++n)
                        {
                            const bool is_selected = (tf == n);
                            if (ImGui::Selectable(transfer_function_name((TransferFunction)n, 1.f / gamma).c_str(),
                                                  is_selected))
                                tf = (TransferFunction)n;

                            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    if (ImPlot::BeginPlot("Transfer functions"))
                    {
                        ImPlot::SetupAxes("input", "encoded", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

                        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.f);
                        ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 2.f);

                        auto f = [](float x) { return to_linear(x, tf, 1.f / gamma); };
                        auto g = [](float y) { return from_linear(y, tf, 1.f / gamma); };

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

                        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                        ImPlot::PlotLine("to_linear", xs1, ys1, N);
                        ImPlot::SetNextMarkerStyle(ImPlotMarker_Square);
                        ImPlot::PlotLine("from_linear", xs2, ys2, N);

                        ImPlot::PopStyleVar(2);
                        ImPlot::EndPlot();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Illuminant spectra"))
                {
                    if (ImPlot::BeginPlot("Illuminant spectra"))
                    {
                        ImPlot::SetupAxes("Wavelength", "Intensity", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

                        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.f);
                        ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 2.f);
                        ImPlot::PushStyleVar(ImPlotStyleVar_Marker, ImPlotMarker_Circle);

                        for (WhitePoint_ n = WhitePoint_FirstNamed; n <= WhitePoint_LastNamed; ++n)
                        {
                            WhitePoint wp{n};
                            auto       spectrum = white_point_spectrum(wp);
                            if (spectrum.values.empty())
                                continue;
                            string name{white_point_name(wp)};
                            ImPlot::PlotLine(name.c_str(), spectrum.values.data(), spectrum.values.size(),
                                             (spectrum.max_wavelength - spectrum.min_wavelength) /
                                                 (spectrum.values.size() - 1),
                                             spectrum.min_wavelength);
                        }
                        ImPlot::PopStyleVar(3);
                        ImPlot::EndPlot();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("CIE 1931 XYZ"))
                {
                    if (ImPlot::BeginPlot("CIE 1931 XYZ color matching functions"))
                    {
                        ImPlot::SetupAxes("Wavelength", "Intensity", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

                        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.f);
                        ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 2.f);
                        ImPlot::PushStyleVar(ImPlotStyleVar_Marker, ImPlotMarker_Circle);

                        auto &xyz       = CIE_XYZ_spectra();
                        auto  increment = (xyz.max_wavelength - xyz.min_wavelength) / xyz.values.size();
                        ImPlot::PlotLine("X", (const float *)&xyz.values[0].x, xyz.values.size(), increment,
                                         xyz.min_wavelength, ImPlotLineFlags_None, 0, sizeof(float3));
                        ImPlot::PlotLine("Y", (const float *)&xyz.values[0].y, xyz.values.size(), increment,
                                         xyz.min_wavelength, ImPlotLineFlags_None, 0, sizeof(float3));
                        ImPlot::PlotLine("Z", (const float *)&xyz.values[0].z, xyz.values.size(), increment,
                                         xyz.min_wavelength, ImPlotLineFlags_None, 0, sizeof(float3));

                        ImPlot::PopStyleVar(3);
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

void HDRViewApp::draw_menus()
{
    if (ImGui::BeginMenu("File"))
    {
        MenuItem(action("Open image..."));
#if defined(__EMSCRIPTEN__)
        MenuItem(action("Open URL..."));
#else
        MenuItem(action("Open folder..."));

        ImGui::BeginDisabled(m_image_loader.recent_files().empty());
        if (ImGui::BeginMenuEx("Open recent", ICON_MY_OPEN_IMAGE))
        {
            auto   recents = m_image_loader.recent_files_short(47, 50);
            size_t i       = 0;
            for (auto f = recents.begin(); f != recents.end(); ++f, ++i)
            {
                if (ImGui::MenuItem(fmt::format("{}##File{}", *f, i).c_str()))
                {
                    m_image_loader.load_recent_file(i);
                    break;
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Clear recently opened"))
                m_image_loader.clear_recent_files();
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();

        MenuItem(action("Add watched folder..."));

        ImGui::Separator();

        MenuItem(action("Reload image"));
        MenuItem(action("Reload all images"));
        MenuItem(action("Watch for changes"));
#endif

        ImGui::Separator();

        MenuItem(action("Save as..."));
        MenuItem(action("Export image as..."));

        ImGui::Separator();

        MenuItem(action("Close"));
        MenuItem(action("Close all"));

        ImGui::Separator();

        MenuItem(action("Quit"));

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        MenuItem(action("Zoom in"));
        MenuItem(action("Zoom out"));
        MenuItem(action("Center"));
        MenuItem(action("100%"));
        MenuItem(action("Fit display window"));
        MenuItem(action("Auto fit display window"));
        MenuItem(action("Fit data window"));
        MenuItem(action("Auto fit data window"));
        MenuItem(action("Fit selection"));
        MenuItem(action("Auto fit selection"));
        MenuItem(action("Flip horizontally"));
        MenuItem(action("Flip vertically"));

        ImGui::Separator();

        MenuItem(action("Draw pixel grid"));
        MenuItem(action("Draw pixel values"));
        MenuItem(action("Draw data window"));
        MenuItem(action("Draw display window"));

        ImGui::Separator();

        MenuItem(action("Increase exposure"));
        MenuItem(action("Decrease exposure"));
        MenuItem(action("Normalize exposure"));

        ImGui::Separator();

        MenuItem(action("Increase gamma/Next colormap"));
        MenuItem(action("Decrease gamma/Previous colormap"));

        ImGui::Separator();

        MenuItem(action("Reset tonemapping"));
        if (m_params.rendererBackendOptions.requestFloatBuffer)
            MenuItem(action("Clamp to LDR"));
        MenuItem(action("Dither"));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));
        ImGui::TextUnformatted(ICON_MY_ZEBRA_STRIPES);
        ImGui::SameLine();
        ImGui::TextUnformatted("Clip warnings");
        ImGui::SameLine();
        ImGui::Checkbox("##Draw clip warnings", &m_draw_clip_warnings);
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::BeginDisabled(!m_draw_clip_warnings);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::DragFloatRange2("##Clip warnings", &m_clip_range.x, &m_clip_range.y, 0.01f, 0.f, 0.f, "min: %.01f",
                               "max: %.01f");
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        MenuItem(action("Pan and zoom"));
        MenuItem(action("Rectangular select"));
        MenuItem(action("Pixel/color inspector"));

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Windows"))
    {
        MenuItem(action("Command palette..."));

        ImGui::Separator();

        MenuItem(action("Restore default layout"));

        ImGui::Separator();

        MenuItem(action("Show entire GUI"));
        MenuItem(action("Hide entire GUI"));

        MenuItem(action("Show all windows"));
        MenuItem(action("Hide all windows"));

        ImGui::Separator();

        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
        {
            if (!dockableWindow.includeInViewMenu)
                continue;

            MenuItem(action(fmt::format("Show {} window", dockableWindow.label)));
        }

        ImGui::Separator();

        MenuItem(action("Show top toolbar"));
        MenuItem(action("Show status bar"));
        MenuItem(action("Show FPS in status bar"));

        if (ImGui::BeginMenuEx("Theme", ICON_MY_THEME))
        {
            if (ImGui::MenuItemEx("Theme tweak window", ICON_MY_TWEAK_THEME, nullptr, m_show_tweak_window))
                m_show_tweak_window = !m_show_tweak_window;

            ImGui::Separator();

            int start = m_theme == Theme::CUSTOM_THEME ? Theme::CUSTOM_THEME : Theme::LIGHT_THEME;
            for (int t = start; t < ImGuiTheme::ImGuiTheme_Count; ++t)
                if (ImGui::MenuItem(Theme::name(t), nullptr, t == m_theme))
                    m_theme.set(t);

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    if (m_show_developer_menu && ImGui::BeginMenu("Developer"))
    {
        ImGui::MenuItem(action("Show Dear ImGui demo window"));
        ImGui::MenuItem(action("Show debug window"));
        ImGui::MenuItem(action("Show developer menu"));

        ImGui::EndMenu();
    }

    auto  a      = action("Show Log window");
    float text_w = ImGui::CalcTextSize(a.icon.c_str(), NULL).x;

    auto pos_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 2.f * text_w -
                 3.5f * ImGui::GetStyle().ItemSpacing.x + 0.5f * ImGui::GetStyle().WindowPadding.x - 2.f;
    if (pos_x > ImGui::GetCursorPosX())
        ImGui::SetCursorPosX(pos_x);

    ImGui::MenuItem(a, false);
    ImGui::MenuItem(action("Show help"), false);
}

void HDRViewApp::draw_pixel_inspector_window()
{
    if (!current_image())
        return;

    auto PixelHeader = [](const string &title, int2 &pixel, bool *p_visible = nullptr)
    {
        bool open = ImGui::CollapsingHeader(title.c_str(), p_visible, ImGuiTreeNodeFlags_DefaultOpen);

        ImGuiInputTextFlags_ flags = p_visible ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
        ImGui::BeginDisabled(p_visible == nullptr);
        // slightly convoluted process to show the coordinates as drag elements within the header
        ImGui::SameLine();
        float drag_size =
            0.5f * (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x - ImGui::GetFrameHeight());
        if (drag_size > HelloImGui::EmSize(1.f))
        {
            auto fpy = ImGui::GetStyle().FramePadding.y;
            ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
            auto y = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(y + fpy);
            ImGui::SetNextItemWidth(drag_size);
            ImGui::DragInt("##pixel x coordinates", &pixel.x, 1.f, 0, 0, "X: %d", flags);
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::SetCursorPosY(y + fpy);
            ImGui::SetNextItemWidth(drag_size);
            ImGui::DragInt("##pixel y coordinates", &pixel.y, 1.f, 0, 0, "Y: %d", flags);
            ImGui::PopStyleVar();
        }
        else
            ImGui::NewLine();

        ImGui::EndDisabled();

        return open;
    };

    auto &io = ImGui::GetIO();

    ImGui::SeparatorText("Selection:");
    // float sz = ImGui::GetContentRegionAvail().x * 0.65f + ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize(" Min,Max ").x);
    ImGui::DragInt4("Min,Max", &m_roi_live.min.x);
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_roi = m_roi_live;
    ImGui::SetItemTooltip("W x H: (%d x %d)", m_roi_live.size().x, m_roi_live.size().y);

    ImGui::SeparatorText("Watched pixels:");

    auto hovered_pixel = int2{pixel_at_app_pos(io.MousePos)};
    if (PixelHeader(ICON_MY_CURSOR_ARROW "##hovered pixel", hovered_pixel))
    {
        static int3 color_mode = {0, 0, 0};
        ImGui::PushID("Current");
        pixel_color_widget(hovered_pixel, color_mode.x, 0);
        ImGui::SetItemTooltip("Hovered pixel values in current channel.");
        ImGui::PopID();

        ImGui::PushID("Reference");
        pixel_color_widget(hovered_pixel, color_mode.y, 1);
        ImGui::SetItemTooltip("Hovered pixel values in reference channel.");
        ImGui::PopID();

        ImGui::PushID("Composite");
        pixel_color_widget(hovered_pixel, color_mode.z, 2);
        ImGui::SetItemTooltip("Hovered pixel values in composite.");
        ImGui::PopID();

        ImGui::Spacing();
    }

    ImGui::Checkbox("Show " ICON_MY_WATCHED_PIXEL "s in viewport", &m_draw_watched_pixels);

    int delete_idx = -1;
    for (int i = 0; i < (int)m_watched_pixels.size(); ++i)
    {
        auto &wp = m_watched_pixels[i];

        ImGui::PushID(i);
        bool visible = true;
        if (PixelHeader(fmt::format("{}{}", ICON_MY_WATCHED_PIXEL, i + 1), wp.pixel, &visible))
        {
            ImGui::PushID("Current");
            pixel_color_widget(wp.pixel, wp.color_mode.x, 0, true);
            ImGui::SetItemTooltip("Pixel %s%d values in current channel.", ICON_MY_WATCHED_PIXEL, i + 1);
            ImGui::PopID();

            ImGui::PushID("Reference");
            pixel_color_widget(wp.pixel, wp.color_mode.y, 1, true);
            ImGui::SetItemTooltip("Pixel %s%d values in reference channel.", ICON_MY_WATCHED_PIXEL, i + 1);
            ImGui::PopID();

            ImGui::PushID("Composite");
            pixel_color_widget(wp.pixel, wp.color_mode.z, 2, true);
            ImGui::SetItemTooltip("Pixel %s%d values in composite.", ICON_MY_WATCHED_PIXEL, i + 1);
            ImGui::PopID();

            ImGui::Spacing();
        }
        ImGui::PopID();

        if (!visible)
            delete_idx = i;
    }
    if (delete_idx >= 0)
        m_watched_pixels.erase(m_watched_pixels.begin() + delete_idx);
}

void HDRViewApp::update_visibility()
{
    // compute image:channel visibility and update selection indices
    vector<string> visible_image_names;
    for (auto img : m_images)
    {
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
            visible_image_names.emplace_back(img->file_and_partname());

        img->root.calculate_visibility(img.get());

        // if the selected group is hidden, select the next visible group
        if (img->is_valid_group(img->selected_group) && !img->groups[img->selected_group].visible)
        {
            auto old = img->selected_group;
            if ((img->selected_group = img->next_visible_group_index(img->selected_group, Forward)) == old)
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
        if ((m_current = next_visible_image_index(m_current, Forward)) == old)
            m_current = -1; // no visible images left
    }

    // if the reference is hidden, clear it
    // TODO: keep it, but don't display it
    if (is_valid(m_reference) && !m_images[m_reference]->visible)
        m_reference = -1;

    //
    // compute short (i.e. unique) names for visible images

    // determine common vs. unique parts of visible filenames
    auto [begin_short_offset, end_short_offset] = find_common_prefix_suffix(visible_image_names);
    // we'll add ellipses, so don't shorten if we don't save much space
    if (begin_short_offset <= 4)
        begin_short_offset = 0;
    if (end_short_offset <= 4)
        end_short_offset = 0;
    for (auto img : m_images)
    {
        if (!img->visible)
            continue;

        auto   long_name   = img->file_and_partname();
        size_t short_begin = begin_short_offset;
        size_t short_end   = std::max(long_name.size() - (size_t)end_short_offset, short_begin);

        // Extend beginning and ending of short region to entire word/number
        if (isalnum(long_name[short_begin]))
            while (short_begin > 0 && isalnum(long_name[short_begin - 1])) --short_begin;
        if (isalnum(long_name[short_end - 1]))
            while (short_end < long_name.size() && isalnum(long_name[short_end])) ++short_end;

        // just use the filename if all file paths are identical
        if (short_begin == short_end)
            img->short_name = get_filename(img->file_and_partname());
        else
            img->short_name = long_name.substr(short_begin, short_end - short_begin);

        // add ellipses to indicate where we shortened
        if (short_begin != 0)
            img->short_name = "..." + img->short_name;
        if (short_end != long_name.size())
            img->short_name = img->short_name + "...";
    }

    set_image_textures();
}

void HDRViewApp::draw_file_window()
{
    if (ImGui::BeginCombo("Mode", blend_mode_names()[m_blend_mode].c_str(), ImGuiComboFlags_HeightLargest))
    {
        for (int n = 0; n < NUM_BLEND_MODES; ++n)
        {
            const bool is_selected = (m_blend_mode == n);
            if (ImGui::Selectable(blend_mode_names()[n].c_str(), is_selected))
            {
                m_blend_mode = (EBlendMode)n;
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
        for (int n = 0; n < NUM_CHANNELS; ++n)
        {
            const bool is_selected = (m_channel == n);
            if (ImGui::Selectable(channel_names()[n].c_str(), is_selected))
            {
                m_channel = (EChannel)n;
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
    ImGui::WrappedTooltip(
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
    ImGui::WrappedTooltip(m_short_names ? "Click to show full filenames."
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
    ImGui::WrappedTooltip("Choose how the images and layers are listed below");

    static constexpr ImGuiTreeNodeFlags base_node_flags =
        ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DrawLinesFull;

    static constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate |
                                                   ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit |
                                                   ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg |
                                                   ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("ImageList", 2, table_flags,
                          ImVec2(0.f, ImGui::GetContentRegionAvail().y - ImGui::IconButtonSize().y -
                                          ImGui::GetStyle().ItemSpacing.y)))
    {
        const float icon_width = ImGui::IconSize().x;

        ImGui::TableSetupColumn(ICON_MY_LIST_OL, ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed,
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
                    spdlog::info("Sorting {}", (int)direction);
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

        int id                 = 0;
        int visible_img_number = 0;
        int hidden_images      = 0;
        int hidden_groups      = 0;
        int image_to_close     = -1;

        for (int i = 0; i < num_images(); ++i)
        {
            auto &img          = m_images[i];
            bool  is_current   = m_current == i;
            bool  is_reference = m_reference == i;

            if (!img->visible)
            {
                ++hidden_images;
                continue;
            }

            ++visible_img_number;

            ImGuiTreeNodeFlags node_flags = base_node_flags;

            ImGui::PushFont(m_file_list_mode == 0 ? m_sans_regular : m_sans_bold, ImGui::GetStyle().FontSizeBase);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushRowColors(is_current, is_reference, ImGui::GetIO().KeyShift);
            ImGui::TextAligned(fmt::format("{}", visible_img_number), 1.0f);

            ImGui::TableNextColumn();

            if (is_current || is_reference)
                node_flags |= ImGuiTreeNodeFlags_Selected;
            if (m_file_list_mode == 0)
            {
                node_flags |= ImGuiTreeNodeFlags_Leaf;
                ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
            }

            auto &selected_group =
                img->groups[(is_reference && !is_current) ? img->reference_group : img->selected_group];
            string group_name =
                selected_group.num_channels == 1 ? selected_group.name : "(" + selected_group.name + ")";
            auto  &channel    = img->channels[selected_group.channels[0]];
            string layer_path = Channel::head(channel.name);
            string filename   = (m_short_names ? img->short_name : img->file_and_partname()) +
                              (m_file_list_mode ? "" : img->delimiter() + layer_path + group_name);

            string the_text = ImGui::TruncatedText(filename, img->groups.size() > 1 ? ICON_MY_IMAGES : ICON_MY_IMAGE);

            // auto item = m_images[i];
            bool open = ImGui::TreeNodeEx((void *)(intptr_t)i, node_flags, "%s", the_text.c_str());

            ImGui::PopStyleColor(3);

            // Add right-click context menu
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Copy path to clipboard"))
                    ImGui::SetClipboardText(img->filename.c_str());

#if !defined(__EMSCRIPTEN__)
                std::string menu_label = fmt::format("Reveal in {}", file_manager_name());
                if (ImGui::MenuItem(menu_label.c_str()))
                    show_in_file_manager(img->filename.c_str());
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
                if (ImGui::BeginTable("ImageList", 2, table_flags))
                {
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 1.25f * icon_width);
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextAligned(fmt::format("{}", visible_img_number), 1.0f);
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
                    visible_groups = img->draw_channel_rows(i, id, is_current, is_reference, m_scroll_to_next_frame);
                    MY_ASSERT(visible_groups == img->root.visible_groups,
                              "Unexpected number of visible groups; {} != {}", visible_groups,
                              img->root.visible_groups);
                }
                else
                {
                    visible_groups = img->draw_channel_tree(i, id, is_current, is_reference, m_scroll_to_next_frame);
                    MY_ASSERT(visible_groups == img->root.visible_groups,
                              "Unexpected number of visible groups; {} != {}", visible_groups,
                              img->root.visible_groups);
                }

                hidden_groups += (int)img->groups.size() - visible_groups;

                ImGui::PopFont();

                if (open)
                    ImGui::TreePop();
            }

            ImGui::PopFont();
        }

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

        ImGui::PopStyleVar(2);

        ImGui::EndTable();
    }

    {
        IconButton(action("Play backward"));

        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

        IconButton(action("Stop playback"));

        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

        IconButton(action("Play forward"));

        ImGui::SameLine();

        ImGui::SetNextItemWidth(std::max(HelloImGui::EmSize(1.f), ImGui::GetContentRegionAvail().x));
        if (ImGui::SliderFloat("##Playback speed", &m_playback_speed, 0.1f, 60.f, "%.1f fps",
                               ImGuiInputTextFlags_EnterReturnsTrue))
            m_playback_speed = clamp(m_playback_speed, 1.f / 20.f, 60.f);
    }
    // ImGui::EndDisabled();
}

void HDRViewApp::draw_top_toolbar()
{
    auto img = current_image();

    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
    ImGui::TextUnformatted(ICON_MY_EXPOSURE);
    ImGui::PopFont();
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(HelloImGui::EmSize(8));
    ImGui::SliderFloat("##ExposureSlider", &m_exposure_live, -9.f, 9.f, "Exposure: %+5.2f");
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_exposure = m_exposure_live;
    ImGui::EndGroup();
    ImGui::WrappedTooltip("Increasing (Shift+E) or decreasing (e) the exposure. The displayed brightness of "
                          "the image is multiplied by 2^exposure.");

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
    ImGui::TextUnformatted(ICON_MY_OFFSET);
    ImGui::PopFont();
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(HelloImGui::EmSize(6));
    ImGui::SliderFloat("##OffsetSlider", &m_offset_live, -1.f, 1.f, "Offset: %+1.2f");
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_offset = m_offset_live;
    ImGui::EndGroup();
    ImGui::WrappedTooltip("Increase/decrease the blackpoint offset. The offset is added to the pixel value after "
                          "exposure is applied.");

    ImGui::SameLine();

    IconButton(action("Normalize exposure"));

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

    IconButton(action("Reset tonemapping"));

    ImGui::SameLine();

    ImGui::SetNextItemWidth(HelloImGui::EmSize(4));
    static const char *items[] = {ICON_MY_TONEMAPPING ": γ", ICON_MY_TONEMAPPING ": +", ICON_MY_TONEMAPPING ": ±"};
    if (ImGui::BeginCombo("##Tonemapping", items[m_tonemap]))
    {
        static const char *items[] = {"Gamma", "Colormap [0,1]", "Colormap [-1,1]"};
        for (int n = 0; n < IM_ARRAYSIZE(items); n++)
        {
            const bool is_selected = (m_tonemap == n);
            if (ImGui::Selectable(items[n], is_selected))
                m_tonemap = (Tonemap)n;

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::WrappedTooltip(
        "Set the tonemapping mode, which is applied to the pixel values after exposure and blackpoint offset.\n\n"
        "Gamma: Raise the pixel values to this exponent before display.\n"
        "Colormap [0,1]: Falsecolor with colormap range set to [0,1].\n"
        "Colormap [-1,1]: Falsecolor with colormap range set to [-1,+1] (choosing a diverging "
        "colormap like IceFire can help visualize positive/negative values).");

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

    const auto tonemap_width = HelloImGui::EmSize(7);
    switch (m_tonemap)
    {
    default: [[fallthrough]];
    case Tonemap_Gamma:
    {
        ImGui::SetNextItemWidth(tonemap_width);
        ImGui::SliderFloat("##GammaSlider", &m_gamma_live, 0.02f, 9.f, "Gamma: %5.3f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            m_gamma = m_gamma_live;
        ImGui::SetItemTooltip("Set the exponent for gamma correction.");
    }
    break;
    case Tonemap_FalseColor: [[fallthrough]];
    case Tonemap_PositiveNegative:
    {
        ImGui::SetNextItemWidth(tonemap_width - ImGui::IconButtonSize().x - ImGui::GetStyle().ItemInnerSpacing.x);
        ImGuiComboFlags combo_flags = ImGuiComboFlags_HeightLargest | ImGuiComboFlags_NoArrowButton;

        auto colormap = m_colormaps[m_colormap_index];
        auto ret      = ImGui::BeginCombo("##Colormap", "", combo_flags);
        ImGui::SetItemTooltip("Click to choose a colormap.");
        if (ret)
        {
            for (int n = 0; n < (int)std::size(m_colormaps); n++)
            {
                const bool is_selected = (m_colormap_index == n);
                if (ImGui::Selectable((string("##") + Colormap::name(m_colormaps[n])).c_str(), is_selected, 0,
                                      ImVec2(0, ImGui::GetFrameHeight())))
                    m_colormap_index = n;
                ImGui::SameLine(0.f, 0.f);

                ImPlot::ColormapButton(
                    Colormap::name(m_colormaps[n]),
                    ImVec2(tonemap_width - ImGui::IconButtonSize().x - ImGui::GetStyle().ItemInnerSpacing.x,
                           ImGui::GetFrameHeight()),
                    m_colormaps[n]);

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const auto bb_min = float2{ImGui::GetItemRectMin()} + ImGui::GetStyle().FrameRounding;
        const auto bb_max = float2{ImGui::GetItemRectMax()} - float2(combo_flags & ImGuiComboFlags_NoArrowButton
                                                                         ? ImGui::GetStyle().FrameRounding
                                                                         : ImGui::GetFrameHeight(),
                                                                     ImGui::GetStyle().FrameRounding);
        const float cmap_size = Colormap::values(colormap).size();
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)Colormap::texture(colormap)->texture_handle(),
                                             bb_min, bb_max, ImVec2(0.5f / cmap_size, 0.5f),
                                             ImVec2((cmap_size - 0.5f) / cmap_size, 0.5f));

        const auto text_c = contrasting_color(Colormap::sample(colormap, 0.5f));
        ImGui::AddTextAligned(ImGui::GetWindowDrawList(), (bb_min + bb_max) / 2.f, ImColor(text_c),
                              Colormap::name(m_colormaps[m_colormap_index]), ImVec2{0.5f, 0.5f});

        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

        IconButton(action("Reverse colormap"));
    }
    break;
    }

    ImGui::SameLine();

    if (m_params.rendererBackendOptions.requestFloatBuffer)
    {
        IconButton(action("Clamp to LDR"));
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    }

    IconButton(action("Draw pixel grid"));
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

    IconButton(action("Draw pixel values"));
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
}

void HDRViewApp::draw_command_palette()
{
    if (m_open_command_palette)
        ImGui::OpenPopup("Command palette...");

    auto &io = ImGui::GetIO();

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, 5.f * HelloImGui::EmSize()), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowSize(ImVec2{400, 0}, ImGuiCond_Always);

    float remaining_height            = ImGui::GetMainViewport()->Size.y - ImGui::GetCursorScreenPos().y;
    float search_result_window_height = remaining_height - 2.f * HelloImGui::EmSize();

    // Set constraints to allow horizontal resizing based on content
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0, 0), ImVec2(io.DisplaySize.x - 2.f * HelloImGui::EmSize(), search_result_window_height));

    if (ImGui::BeginPopup("Command palette...", ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        m_open_command_palette = false;
        if (ImGui::IsWindowAppearing())
        {
            spdlog::trace("Creating ImCmd context");
            if (ImCmd::GetCurrentContext())
            {
                ImCmd::RemoveAllCaches();
                ImCmd::DestroyContext();
            }
            ImCmd::CreateContext();
            ImCmd::SetStyleFont(ImCmdTextType_Regular, m_sans_regular);
            ImCmd::SetStyleFont(ImCmdTextType_Highlight, m_sans_bold);
            ImCmd::SetStyleFlag(ImCmdTextType_Highlight, ImCmdTextFlag_Underline, true);
            auto highlight_font_color = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            ImCmd::SetStyleColor(ImCmdTextType_Highlight, ImGui::ColorConvertFloat4ToU32(highlight_font_color));

            for (auto &a : m_actions)
            {
                if (a.second.enabled())
                    ImCmd::AddCommand({a.second.name, a.second.p_selected ? [&a](){
                *a.second.p_selected = !*a.second.p_selected;a.second.callback();} : a.second.callback, nullptr, nullptr, a.second.icon, ImGui::GetKeyChordNameTranslated(a.second.chord), a.second.p_selected});
            }

#if !defined(__EMSCRIPTEN__)
            // add a two-step command to list and open recent files
            if (!m_image_loader.recent_files().empty())
                ImCmd::AddCommand({"Open recent",
                                   [this]()
                                   {
                                       ImCmd::Prompt(m_image_loader.recent_files_short());
                                       ImCmd::SetNextCommandPaletteSearchBoxFocused();
                                   },
                                   [this](int selected_option) { m_image_loader.load_recent_file(selected_option); },
                                   nullptr, ICON_MY_OPEN_IMAGE});

#endif

            // set logging verbosity. This is a two-step command
            ImCmd::AddCommand({"Set logging verbosity",
                               []()
                               {
                                   ImCmd::Prompt(vector<string>{"0: trace", "1: debug", "2: info", "3: warn", "4: err",
                                                                "5: critical", "6: off"});
                                   ImCmd::SetNextCommandPaletteSearchBoxFocused();
                               },
                               [](int selected_option)
                               {
                                   ImGui::GlobalSpdLogWindow().sink()->set_level(
                                       spdlog::level::level_enum(selected_option));
                                   spdlog::info("Setting verbosity threshold to level {:d}.", selected_option);
                               },
                               nullptr, ICON_MY_LOG_LEVEL});

            // set background color. This is a two-step command
            ImCmd::AddCommand({"Set background color",
                               []()
                               {
                                   ImCmd::Prompt(vector<string>{"0: black", "1: white", "2: dark checker",
                                                                "3: light checker", "4: custom..."});
                                   ImCmd::SetNextCommandPaletteSearchBoxFocused();
                               },
                               [this](int selected_option)
                               {
                                   m_bg_mode = (EBGMode)clamp(selected_option, (int)BG_BLACK, (int)NUM_BG_MODES - 1);
                                   if (m_bg_mode == BG_CUSTOM_COLOR)
                                       m_show_bg_color_picker = true;
                               },
                               nullptr, ICON_MY_BLANK});

            // add two-step theme selection command
            ImCmd::AddCommand({"Set theme",
                               []()
                               {
                                   vector<string> theme_names;
                                   theme_names.push_back(Theme::name(Theme::LIGHT_THEME));
                                   theme_names.push_back(Theme::name(Theme::DARK_THEME));
                                   for (int i = 0; i < ImGuiTheme::ImGuiTheme_Count; ++i)
                                       theme_names.push_back(ImGuiTheme::ImGuiTheme_Name((ImGuiTheme::ImGuiTheme_)(i)));

                                   ImCmd::Prompt(theme_names);
                                   ImCmd::SetNextCommandPaletteSearchBoxFocused();
                               },
                               [this](int selected_option) { m_theme.set(Theme::LIGHT_THEME + selected_option); },
                               nullptr, ICON_MY_THEME});

            ImCmd::SetNextCommandPaletteSearchBoxFocused();
            ImCmd::SetNextCommandPaletteSearch("");
        }

        // ImCmd::SetNextCommandPaletteSearchBoxFocused(); // always focus the search box
        ImCmd::CommandPalette("Command palette", "Filter commands...");

        if (ImCmd::IsAnyItemSelected() || ImGui::GlobalShortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteOverActive) ||
            ImGui::GlobalShortcut(ImGuiMod_Ctrl | ImGuiKey_Period, ImGuiInputFlags_RouteOverActive))
        {
            // Close window when user selects an item, hits escape, or unfocuses the command palette window
            // (clicking elsewhere)
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::BeginTable("PaletteHelp", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ContextMenuInBody))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::ScopedFont sf{m_sans_bold, 10};

            string txt;
            txt = "Navigate (" ICON_MY_ARROW_UP ICON_MY_ARROW_DOWN ")";
            ImGui::TableNextColumn();
            ImGui::TextAligned(txt, 0.f);

            ImGui::TableNextColumn();
            txt = "Use (" ICON_MY_KEY_RETURN ")";
            ImGui::TextAligned(txt, 0.5f);

            ImGui::TableNextColumn();
            txt = "Dismiss (" ICON_MY_KEY_ESC ")";
            ImGui::TextAligned(txt, 1.f);

            ImGui::PopStyleColor();

            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_about_dialog()
{
    // work around HelloImGui rendering a couple frames to figure out sizes
    if (m_open_help && ImGui::GetFrameCount() > 1)
        ImGui::OpenPopup("About");

    auto &io = ImGui::GetIO();

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, 5.f * HelloImGui::EmSize()), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowFocus();
    constexpr float icon_size     = 128.f;
    float2          col_width     = {icon_size + HelloImGui::EmSize(), 32 * HelloImGui::EmSize()};
    float           content_width = col_width[0] + col_width[1] + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextWindowContentSize(float2{content_width, 0.f});
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{2 * icon_size, icon_size},
        float2{content_width + 2.f * ImGui::GetStyle().WindowPadding.x + ImGui::GetStyle().ScrollbarSize,
               io.DisplaySize.y - 7.f * HelloImGui::EmSize()});

    if (ImGui::BeginPopup("About", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_AlwaysAutoResize))
    {
        m_open_help = false;
        ImGui::Spacing();

        auto platform_backend = [](HelloImGui::PlatformBackendType type)
        {
            using T = HelloImGui::PlatformBackendType;
            switch (type)
            {
            case T::FirstAvailable: return "FirstAvailable";
            case T::Glfw: return "GLFW 3";
            case T::Sdl: return "SDL 2";
            case T::Null: return "Null";
            default: return "Unknown";
            }
        }(m_params.platformBackendType);

        auto renderer_backend = [](HelloImGui::RendererBackendType type)
        {
            using T = HelloImGui::RendererBackendType;
            switch (type)
            {
            case T::FirstAvailable: return "FirstAvailable";
            case T::OpenGL3: return "OpenGL3";
            case T::Metal: return "Metal";
            case T::Vulkan: return "Vulkan";
            case T::DirectX11: return "DirectX11";
            case T::DirectX12: return "DirectX12";
            case T::Null: return "Null";
            default: return "Unknown";
            }
        }(m_params.rendererBackendType);

        if (ImGui::BeginTable("about_table1", 2))
        {
            ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed, col_width[0]);
            ImGui::TableSetupColumn("description", ImGuiTableColumnFlags_WidthFixed, col_width[1]);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // right align the image
            ImGui::AlignCursor(icon_size + 0.5f * HelloImGui::EmSize(), 1.f);
            HelloImGui::ImageFromAsset("app_settings/icon-256.png", {icon_size, icon_size}); // show the app icon

            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[1]);

            {
                ImGui::ScopedFont sf{m_sans_bold, 30};
                ImGui::HyperlinkText("HDRView", "https://github.com/wkjarosz/hdrview");
            }

            ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 18.f / 14.f);
            ImGui::TextUnformatted(version());
            ImGui::PopFont();
            ImGui::PushFont(m_sans_regular, ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
            ImGui::TextFmt("Built on {} using the {} backend with {}.", build_timestamp(), platform_backend,
                           renderer_backend);
            ImGui::PopFont();

            ImGui::Spacing();

            ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
            ImGui::TextUnformatted(
                "HDRView is a simple research-oriented tool for examining, comparing, manipulating, and "
                "converting high-dynamic range images.");
            ImGui::PopFont();

            ImGui::Spacing();

            ImGui::TextUnformatted(
                "It is developed by Wojciech Jarosz, and is available under a 3-clause BSD license.");

            ImGui::PopTextWrapPos();
            ImGui::EndTable();
        }

        auto item_and_description = [this, col_width](const char *name, const char *desc, const char *url = nullptr)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::AlignCursor(name, 1.f);
            ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase);
            ImGui::HyperlinkText(name, url);
            ImGui::PopFont();
            ImGui::TableNextColumn();

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[1] - HelloImGui::EmSize());
            ImGui::PushFont(m_sans_regular, ImGui::GetStyle().FontSizeBase);
            ImGui::TextUnformatted(desc);
            ImGui::PopFont();
        };

        if (ImGui::BeginTabBar("AboutTabBar"))
        {
            if (ImGui::BeginTabItem("Keybindings", nullptr))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[0] + col_width[1]);

                ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase);
                ImGui::TextAligned("The main keyboard shortcut to remember is:", 0.5f);
                ImGui::PopFont();

                ImGui::PushFont(font("mono regular"), ImGui::GetStyle().FontSizeBase * 30.f / 14.f);
                ImGui::TextAligned(ImGui::GetKeyChordNameTranslated(action("Command palette...").chord), 0.5f);
                ImGui::PopFont();

                ImGui::TextUnformatted("This opens the command palette, which lists every available HDRView command "
                                       "along with its keyboard shortcuts (if any).");
                ImGui::Spacing();

                ImGui::TextUnformatted("Many commands and their keyboard shortcuts are also listed in the menu bar.");

                ImGui::TextUnformatted(
                    "Additonally, left-click+drag will pan the image view, and scrolling the mouse/pinching "
                    "will zoom in and out.");

                ImGui::Spacing();
                ImGui::PopTextWrapPos();

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Credits"))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[0] + col_width[1]);
                ImGui::TextUnformatted(
                    "HDRView additionally makes use of the following external libraries and techniques (in "
                    "alphabetical order):\n\n");
                ImGui::PopTextWrapPos();

                if (ImGui::BeginTable("about_table2", 2))
                {
                    ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, col_width[0]);
                    ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthFixed, col_width[1]);

                    item_and_description("Dear ImGui", "Omar Cornut's immediate-mode graphical user interface for C++.",
                                         "https://github.com/ocornut/imgui");
#ifdef __EMSCRIPTEN__
                    item_and_description("emscripten", "An MIT-licensed LLVM-to-WebAssembly compiler.",
                                         "https://github.com/emscripten-core/emscripten");
                    item_and_description("emscripten-browser-file",
                                         "Armchair Software's MIT-licensed header-only C++ library "
                                         "to open and save files in the browser.",
                                         "https://github.com/Armchair-Software/emscripten-browser-file");
#endif
                    item_and_description("{fmt}", "A modern formatting library.", "https://github.com/fmtlib/fmt");
                    item_and_description("Hello ImGui", "Pascal Thomet's cross-platform starter-kit for Dear ImGui.",
                                         "https://github.com/pthom/hello_imgui");
#ifdef HDRVIEW_ENABLE_LCMS2
                    item_and_description("lcms2", "LittleCMS color management engine.",
                                         "https://github.com/mm2/Little-CMS");
#endif
#ifdef HDRVIEW_ENABLE_HEIF
                    item_and_description("libheif", "For loading HEIF, HEIC, AVIF, and AVIFS files.",
                                         "https://github.com/strukturag/libheif");
#endif
#ifdef HDRVIEW_ENABLE_JPEGXL
                    item_and_description("libjxl", "For loading JPEG-XL files.", "https://github.com/libjxl/libjxl");
#endif
#ifdef HDRVIEW_ENABLE_LIBPNG
                    item_and_description("libpng", "For loading PNG files.", "https://github.com/pnggroup/libpng");
#endif
#ifdef HDRVIEW_ENABLE_UHDR
                    item_and_description("libuhdr", "For loading Ultra HDR JPEG files.",
                                         "https://github.com/google/libultrahdr");
#endif
                    item_and_description(
                        "linalg", "Sterling Orsten's public domain, single header short vector math library for C++.",
                        "https://github.com/sgorsten/linalg");
                    item_and_description("NanoGUI", "Bits of code from Wenzel Jakob's BSD-licensed NanoGUI library.",
                                         "https://github.com/mitsuba-renderer/nanogui");
                    item_and_description("nvgui", "GUI components (property editor) from nvpro_core2",
                                         "https://github.com/nvpro-samples/nvpro_core2");
                    item_and_description("OpenEXR", "High Dynamic-Range (HDR) image file format.",
                                         "https://github.com/AcademySoftwareFoundation/openexr");
#ifndef __EMSCRIPTEN__
                    item_and_description("portable-file-dialogs",
                                         "Sam Hocevar's WTFPL portable GUI dialogs library, C++11, single-header.",
                                         "https://github.com/samhocevar/portable-file-dialogs");
#endif
                    item_and_description("smalldds", "Single-header library for loading DDS images.",
                                         "https://github.com/wkjarosz/smalldds");
                    item_and_description("stb_image/write", "Single-header libraries for loading/writing images.",
                                         "https://github.com/nothings/stb");
                    item_and_description("tev", "Some code is adapted from Thomas Müller's tev.",
                                         "https://github.com/Tom94/tev");
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Build info"))
            {
                ImGui::PushFont(m_mono_regular, 0.f);
                ImVec2 child_size = ImVec2(0, HelloImGui::EmSize(12.f));
                ImGui::BeginChild(ImGui::GetID("cfg_infos"), child_size, ImGuiChildFlags_FrameStyle);

                ImGui::Text("ImGui version: %s", ImGui::GetVersion());

                ImGui::Text("EDR support: %s", HelloImGui::hasEdrSupport() ? "yes" : "no");
#ifdef __EMSCRIPTEN__
                ImGui::Text("define: __EMSCRIPTEN__");
#endif
#ifdef ASSETS_LOCATION
                ImGui::Text("ASSETS_LOCATION: %s", ASSETS_LOCATION);
#endif
#ifdef HDRVIEW_ICONSET_FA6
                ImGui::Text("HDRVIEW_ICONSET: Font Awesome 6");
#elif defined(HDRVIEW_ICONSET_LC)
                ImGui::Text("HDRVIEW_ICONSET: Lucide Icons");
#elif defined(HDRVIEW_ICONSET_MS)
                ImGui::Text("HDRVIEW_ICONSET: Material Symbols");
#elif defined(HDRVIEW_ICONSET_MD)
                ImGui::Text("HDRVIEW_ICONSET: Material Design");
#elif defined(HDRVIEW_ICONSET_MDI)
                ImGui::Text("HDRVIEW_ICONSET: Material Design Icons");
#endif

                ImGui::Text("Image IO libraries:");
#ifdef HDRVIEW_ENABLE_UHDR
                ImGui::Text("\tlibuhdr: yes");
#else
                ImGui::Text("\tlibuhdr: no");
#endif
#ifdef HDRVIEW_ENABLE_JPEGXL
                ImGui::Text("\tlibjxl:  yes");
#else
                ImGui::Text("\tlibjxl:  no");
#endif
#ifdef HDRVIEW_ENABLE_HEIF
                ImGui::Text("\tlibheif: yes");
#else
                ImGui::Text("\tlibheif: no");
#endif
#ifdef HDRVIEW_ENABLE_LIBPNG
                ImGui::Text("\tlibpng:  yes");
#else
                ImGui::Text("\tlibpng:  no");
#endif
                ImGui::EndChild();
                ImGui::PopFont();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (ImGui::Button("Dismiss", HelloImGui::EmToVec2(8.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Escape) ||
            ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_Space) ||
            ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_Slash))
            ImGui::CloseCurrentPopup();

        ImGui::ScrollWhenDraggingOnVoid(ImVec2(0.0f, -ImGui::GetIO().MouseDelta.y), ImGuiMouseButton_Left);
        ImGui::EndPopup();
    }
}
