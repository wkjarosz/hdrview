#include "app.h"

#include "fonts.h"
#include "hello_imgui/hello_imgui.h"
#include "image.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "implot.h"

#include "platform_utils.h"

using namespace std;
using namespace HelloImGui;

void HDRViewApp::run()
{
    ImPlot::CreateContext();
    Run(m_params);
    ImPlot::DestroyContext();
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
        ImGui::SetNextWindowSize(EmToVec2(20.f, 46.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug", &m_show_debug_window))
        {
            if (ImGui::BeginTabBar("Debug tabs", ImGuiTabBarFlags_None))
            {
                if (ImGui::BeginTabItem("Transfer functions"))
                {
                    static float             gamma = 2.2f;
                    static TransferFunction_ tf    = TransferFunction_Linear;
                    ImGui::DragFloat("Gamma", &gamma, 0.01f, 0.f);
                    if (ImGui::BeginCombo("##transfer function", transfer_function_name(tf, 1.f / gamma).c_str(),
                                          ImGuiComboFlags_HeightLargest))
                    {
                        for (TransferFunction n = TransferFunction_Linear; n < TransferFunction_Count; ++n)
                        {
                            const bool is_selected = (tf == n);
                            if (ImGui::Selectable(transfer_function_name((TransferFunction_)n, 1.f / gamma).c_str(),
                                                  is_selected))
                                tf = (TransferFunction_)n;

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

                        for (WhitePoint n = WhitePoint_FirstNamed; n <= WhitePoint_LastNamed; ++n)
                        {
                            WhitePoint_ wp{n};
                            auto        spectrum = white_point_spectrum(wp);
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
        if (drag_size > EmSize(1.f))
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

    static constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate |
                                                   ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit |
                                                   ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg |
                                                   ImGuiTableFlags_ScrollY;
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

        int id             = 0;
        int hidden_groups  = 0;
        int image_to_close = -1;

        // currently we only support the clipper when each image is one row
        bool             use_clipper = m_file_list_mode == 0;
        ImGuiListClipper clipper;
        if (use_clipper)
            clipper.Begin(m_visible_images.size());
        // the loop conditions here are to execute this outer loop once if we are not using the clipper, and execute it
        // as long as clipper.Step() returns true otherwise
        for (int iter = 0; (!use_clipper && iter < 1) || (use_clipper && clipper.Step()); ++iter)
        {
            int start = use_clipper ? clipper.DisplayStart : 0;
            int end   = use_clipper ? clipper.DisplayEnd : m_visible_images.size();
            for (int vi = start; vi < end; ++vi)
            {
                int   i            = m_visible_images[vi];
                auto &img          = m_images[i];
                bool  is_current   = m_current == i;
                bool  is_reference = m_reference == i;

                ImGuiTreeNodeFlags node_flags = base_node_flags;

                ImGui::PushFont(m_file_list_mode == 0 ? m_sans_regular : m_sans_bold, ImGui::GetStyle().FontSizeBase);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushRowColors(is_current, is_reference, ImGui::GetIO().KeyShift);
                ImGui::TextAligned2(1.0f, -FLT_MIN, fmt::format("{}", vi + 1).c_str());

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

                bool open = ImGui::TreeNodeEx((void *)(intptr_t)i, node_flags, "");
                auto icon = img->groups.size() > 1 ? ICON_MY_IMAGES : ICON_MY_IMAGE;
                ImGui::SameLine(0.f, 0.f);
                string the_text = ImGui::TruncatedText(filename, icon);

                ImGui::PopStyleColor(3);

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
                        string filename, entry_fn;
                        split_zip_entry(img->filename, filename, entry_fn);
                        show_in_file_manager(filename.c_str());
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

                    if (open)
                        ImGui::TreePop();
                }

                ImGui::PopFont();
            }
        }

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

        ImGui::SetNextItemWidth(std::max(EmSize(1.f), ImGui::GetContentRegionAvail().x));
        if (ImGui::SliderFloat("##Playback speed", &m_playback_speed, 0.1f, 60.f, "%.1f fps",
                               ImGuiInputTextFlags_EnterReturnsTrue))
            m_playback_speed = clamp(m_playback_speed, 1.f / 20.f, 60.f);
    }
    // ImGui::EndDisabled();
}
