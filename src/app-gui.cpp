#include "app.h"

#include "colormap.h"
#include "fonts.h"
#include "image.h"
#include "imageio/icc.h"
#include "imcmd_command_palette.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "texture.h"
#include "version.h"
#include <hello_imgui/dpi_aware.h>
#include <hello_imgui/hello_imgui.h>

#include <OpenEXRConfig.h>

#include "imageio/heif.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/png.h"
#include "imageio/raw.h"
#include "imageio/tiff.h"
#include "imageio/uhdr.h"
#include "imageio/webp.h"

#include "platform_utils.h"

// Macro to convert a boolean to a "yes" or "no" string
#define YESNO(x) ((x) ? "ON" : "OFF")

// Macro to format a feature flag with its name and value
// Usage: info += PRINT_FEATURE("{:32s} : {}", HDRVIEW_ENABLE_LIBJPEG);
// Output: "HDRVIEW_ENABLE_LIBJPEG        : ON"
#define PRINT_FEATURE(format_str, feature) fmt::format(format_str, #feature, YESNO(feature))

using namespace std;
using namespace HelloImGui;

void HDRViewApp::pixel_color_widget(const int2 &pixel, int &color_mode, int which_image, bool allow_copy,
                                    float width) const
{
    float4   color32         = pixel_value(pixel, true, which_image);
    float4   displayed_color = linear_to_sRGB(pixel_value(pixel, false, which_image));
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
            if (!current_image())
                return;
            img        = current_image();
            components = color_mode == 0 ? img->groups[img->selected_group].num_channels : 4;
            group      = img->groups[img->selected_group];
            inside[0]  = img->contains(pixel);
        }
        else if (which_image == 1)
        {
            if (!reference_image())
                return;
            img        = reference_image();
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
            ImGui::InputScalarN(fmt::format("##component {}", c).c_str(), ImGuiDataType_S32, &ldr_color[c], 1, nullptr,
                                nullptr, fmt::format("{}: %d", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 3)
    {
        ImGui::SetNextItemWidth(IM_TRUNC(w_full));
        ImGui::InputScalar("##hex color", ImGuiDataType_S32, &hex, nullptr, nullptr, "#%08X",
                           ImGuiInputTextFlags_ReadOnly);
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
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), EmToVec2(15.f, 0.f),
                           fmt::format("Loading {} image{}", num, num > 1 ? "s" : "").c_str());
        ImGui::SameLine();
        // ImGui::PopFont();
    }
    else if (m_remaining_download > 0)
    {
        ImGui::ScopedFont f{nullptr, 4.0f};
        ImGui::ProgressBar((100 - m_remaining_download) / 100.f, EmToVec2(15.f, 0.f), "Downloading image");
        ImGui::SameLine();
    }

    float x = ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x;

    auto sized_text = [&](float em_size, const string &text, float align = 1.f)
    {
        float item_width = EmSize(em_size);
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
        float drag_size = EmSize(5.f);
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
        pixel_color_widget(hovered_pixel, m_status_color_mode, 2, false, EmSize(25.f));
        ImGui::PopID();

        float real_zoom = m_zoom * pixel_ratio();
        int   numer     = (real_zoom < 1.0f) ? 1 : (int)round(real_zoom);
        int   denom     = (real_zoom < 1.0f) ? (int)round(1.0f / real_zoom) : 1;
        x               = ImGui::GetIO().DisplaySize.x - EmSize(11.f) - (m_show_FPS ? EmSize(14.f) : EmSize(0.f));
        sized_text(10.f, fmt::format("{:7.2f}% ({:d}:{:d})", real_zoom * 100, numer, denom));
    }

    if (m_show_FPS)
    {
        ImGui::SameLine(ImGui::GetIO().DisplaySize.x - 14.f * ImGui::GetFontSize());
        ImGui::Checkbox("Enable idling", &m_params.fpsIdling.enableIdling);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("FPS: %.1f%s", FrameRate(), m_params.fpsIdling.isIdling ? " (Idling)" : "");
    }

    ImGui::PopStyleVar();
}

void HDRViewApp::draw_menus()
{
    if (ImGui::BeginMenu("File"))
    {
        MenuItem(action("Open image..."));
        MenuItem(action("Image loading options..."));
#if defined(__EMSCRIPTEN__)
        MenuItem(action("Open URL..."));
#else

        // // ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));
        // ImGui::SetNextItemAllowOverlap();
        // ImGui::Selectable(ICON_MY_OPEN_IMAGE "##Open image selectable", false);
        // // ImGui::TextUnformatted(ICON_MY_OPEN_IMAGE);
        // ImGui::SameLine();
        // ImGui::TextUnformatted("Open image...");
        // ImGui::SameLine(ImGui::GetContentRegionAvail().x - 2.f * ImGui::GetTextLineHeight());
        // // ImGui::TextUnformatted("Clip warnings");
        // // ImGui::SameLine();
        // ImGui::SmallButton(ICON_MY_ZEBRA_STRIPES "##Open options");
        // // ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        // // ImGui::BeginDisabled(!m_draw_clip_warnings);
        // // ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x);
        // // ImGui::DragFloatRange2("##Clip warnings", &m_clip_range.x, &m_clip_range.y, 0.01f, 0.f, 0.f, "min: %.01f",
        // //    "max: %.01f");
        // // ImGui::EndDisabled();
        // // ImGui::PopStyleVar();

        MenuItem(action("Open folder..."));

        ImGui::BeginDisabled(m_image_loader.recent_files().empty());
        if (ImGui::BeginMenuEx("Open recent", ICON_MY_OPEN_IMAGE))
        {
            auto recents = m_image_loader.recent_files_short(47, 50);
            int  i       = 0;
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

        ImGui::Separator();
        MenuItem(action(reveal_in_file_manager_text()));
#endif

        ImGui::Separator();

        MenuItem(action("Save as..."));

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
        ImGui::MenuItem(action("Locate settings file"));

        ImGui::EndMenu();
    }

    auto  a      = action("Show Log window");
    float text_w = ImGui::CalcTextSize(a.icon.c_str()).x;

    auto pos_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 2.f * text_w -
                 3.5f * ImGui::GetStyle().ItemSpacing.x + 0.5f * ImGui::GetStyle().WindowPadding.x - 2.f;
    if (pos_x > ImGui::GetCursorPosX())
        ImGui::SetCursorPosX(pos_x);

    ImGui::MenuItem(a, false);
    ImGui::MenuItem(action("Show help"), false);
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
    ImGui::SetNextItemWidth(EmSize(8));
    ImGui::SliderFloat("##ExposureSlider", &m_exposure_live, -9.f, 9.f, "Exposure: %+5.2f");
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_exposure = m_exposure_live;
    ImGui::EndGroup();
    ImGui::Tooltip("Increasing (Shift+E) or decreasing (e) the exposure. The displayed brightness of "
                   "the image is multiplied by 2^exposure.");

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
    ImGui::TextUnformatted(ICON_MY_OFFSET);
    ImGui::PopFont();
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(EmSize(6));
    ImGui::SliderFloat("##OffsetSlider", &m_offset_live, -1.f, 1.f, "Offset: %+1.2f");
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_offset = m_offset_live;
    ImGui::EndGroup();
    ImGui::Tooltip("Increase/decrease the blackpoint offset. The offset is added to the pixel value after "
                   "exposure is applied.");

    ImGui::SameLine();

    IconButton(action("Normalize exposure"));

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

    IconButton(action("Reset tonemapping"));

    ImGui::SameLine();

    ImGui::SetNextItemWidth(EmSize(4));
    static const char *items[] = {ICON_MY_TONEMAPPING ": γ", ICON_MY_TONEMAPPING ": +", ICON_MY_TONEMAPPING ": ±"};
    if (ImGui::BeginCombo("##Tonemapping", items[m_tonemap]))
    {
        static const char *items2[] = {"Gamma", "Colormap [0,1]", "Colormap [-1,1]"};
        for (int n = 0; n < IM_ARRAYSIZE(items2); n++)
        {
            const bool is_selected = (m_tonemap == n);
            if (ImGui::Selectable(items2[n], is_selected))
                m_tonemap = (Tonemap_)n;

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::Tooltip(
        "Set the tonemapping mode, which is applied to the pixel values after exposure and blackpoint offset.\n\n"
        "Gamma: Raise the pixel values to this exponent before display.\n"
        "Colormap [0,1]: Falsecolor with colormap range set to [0,1].\n"
        "Colormap [-1,1]: Falsecolor with colormap range set to [-1,+1] (choosing a diverging "
        "colormap like IceFire can help visualize positive/negative values).");

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

    const auto tonemap_width = EmSize(7);
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
        const float cmap_size = (float)Colormap::values(colormap).size();
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

void HDRViewApp::draw_command_palette(bool &open)
{
    if (open)
        ImGui::OpenPopup("Command palette...");

    static int last_used = 0;

    auto viewport = ImGui::GetMainViewport();

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x / 2, viewport->Pos.y + 5.f * EmSize()),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2{EmSize(30), 0}, ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Command palette...", nullptr,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoDocking))
    {
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
            ImCmd::SetStyleColor(ImCmdTextType_Highlight, ImGui::GetColorU32(ImGuiCol_CheckMark));

            for (auto &a : m_actions)
            {
                if (a.second.enabled())
                    ImCmd::AddCommand({
                        a.second.names,
                        a.second.p_selected ? [&a](){
                            *a.second.p_selected = !*a.second.p_selected;
                            a.second.callback();
                        } : a.second.callback,
                        nullptr,
                        nullptr,
                        nullptr,
                        [&a](){ a.second.last_used = ++last_used; },
                        a.second.icon,
                        ImGui::GetKeyChordNameTranslated(a.second.chord),
                        a.second.p_selected,
                        a.second.last_used
                    });
            }

#if !defined(__EMSCRIPTEN__)
            // add a two-step command to list and open recent files
            if (!m_image_loader.recent_files().empty())
            {
                static int open_recent_last_used = 0;
                ImCmd::AddCommand({{"Open recent"},
                                   [this]()
                                   {
                                       ImCmd::Prompt(m_image_loader.recent_files_short());
                                       ImCmd::SetNextCommandPaletteSearchBoxFocused();
                                   },
                                   [this](int selected_option) { m_image_loader.load_recent_file(selected_option); },
                                   nullptr,
                                   nullptr,
                                   []() { open_recent_last_used = ++last_used; },
                                   ICON_MY_OPEN_IMAGE,
                                   "",
                                   nullptr,
                                   open_recent_last_used});
            }

#endif
            // set logging verbosity. This is a two-step command
            static int set_logging_last_used = 0;
            ImCmd::AddCommand({{"Set logging verbosity"},
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
                               nullptr,
                               nullptr,
                               []() { set_logging_last_used = ++last_used; },
                               ICON_MY_LOG_LEVEL,
                               "",
                               nullptr,
                               set_logging_last_used});

            // set background color. This is a two-step command, or three-step if custom color is chosen
            static int  set_background_last_used = 0;
            static bool first_frame_bg           = true;
            ImCmd::AddCommand(
                {{"Set background color", "background", "bg color", "change background"},
                 []()
                 {
                     ImCmd::Prompt(
                         vector<string>{"0: black", "1: white", "2: dark checker", "3: light checker", "4: custom..."});
                     ImCmd::SetNextCommandPaletteSearchBoxFocused();
                 },
                 [this](int selected_option)
                 {
                     if (selected_option == 4)
                     {
                         // Custom color - show color picker widget
                         first_frame_bg = true;
                         // Save current color for cancel
                         static float4 previous_bg_color;

                         ImCmd::PromptWidget(
                             [this]() -> bool
                             {
                                 ImGui::Text("Select a custom background color:");
                                 ImGui::Spacing();

                                 // ColorPicker4 with HDR support - allows keyboard entry
                                 // Focus the Red input field on first frame (need to call SetKeyboardFocusHere before
                                 // the picker)
                                 if (first_frame_bg)
                                 {
                                     // Save current color for cancel
                                     previous_bg_color = m_bg_color;
                                     // The picker has: SV square, Hue bar, then R,G,B inputs
                                     // Skip to the R input (typically offset 0 or 2 depending on whether square/hue are
                                     // focusable)
                                     ImGui::SetKeyboardFocusHere(2);
                                     first_frame_bg = false;
                                 }

                                 // Calculate width to make the picker fill available space
                                 // ColorPicker4 renders: sv_picker + spacing + hue_bar + spacing +
                                 // side_preview(square_sz*3) The internal calculation is: sv_picker = width - (hue_bar
                                 // + spacing) Total width = width + spacing + (square_sz * 3) So to fill
                                 // available_width: width = available_width - spacing - (square_sz * 3)
                                 float available    = ImGui::GetContentRegionAvail().x;
                                 float square_sz    = ImGui::GetFrameHeight();
                                 float spacing      = ImGui::GetStyle().ItemInnerSpacing.x;
                                 float picker_width = available - spacing - (square_sz * 3);

                                 ImGui::SetNextItemWidth(picker_width);
                                 ImGui::ColorPicker4("##Custom background color", (float *)&m_bg_color,
                                                     ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float |
                                                         ImGuiColorEditFlags_NoAlpha,
                                                     (float *)&previous_bg_color);

                                 //  ImGui::Spacing();
                                 bool applied = false;

                                 // Also allow Enter key to apply
                                 if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsItemActive())
                                     applied = true;

                                 // Cancel restores previous color
                                 if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                                 {
                                     m_bg_color = previous_bg_color;
                                     applied    = true; // Close the widget
                                 }

                                 return applied;
                             },
                             "Use the color picker or type RGB values. Press Enter to apply or Escape to cancel.");
                     }
                     else
                     {
                         // Set predefined background mode
                         m_bg_mode = (BackgroundMode_)clamp(selected_option, (int)BGMode_Black, (int)BGMode_COUNT - 1);
                     }
                 },
                 nullptr,
                 [this]()
                 {
                     // Only called when custom color is accepted
                     m_bg_mode = BGMode_Custom_Color;
                     spdlog::info("Background color set to: ({:.3f}, {:.3f}, {:.3f})", m_bg_color.x, m_bg_color.y,
                                  m_bg_color.z);
                 },
                 []() { set_background_last_used = ++last_used; },
                 ICON_MY_BLANK,
                 "",
                 nullptr,
                 set_background_last_used});

            // add two-step theme selection command
            static int set_theme_last_used = 0;
            ImCmd::AddCommand({{"Set theme", "Set style", "Set appearance", "Change colors"},
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
                               nullptr,
                               nullptr,
                               []() { set_theme_last_used = ++last_used; },
                               ICON_MY_THEME,
                               "",
                               nullptr,
                               set_theme_last_used});

            // // Example: Free-form text input
            // static int text_input_last_used = 0;
            // ImCmd::AddCommand({"Enter custom text",
            //                    []()
            //                    {
            //                        ImCmd::PromptText(
            //                            "Type some text...",
            //                            [](const char *input) -> std::string
            //                            {
            //                                if (strlen(input) == 0)
            //                                    return "Enter any text you'd like";
            //                                return fmt::format("You typed: '{}'", input);
            //                            },
            //                            ImCmd::ValidateNotEmpty());
            //                    },
            //                    nullptr, [](const std::string &text) { spdlog::info("User entered text: '{}'", text);
            //                    }, nullptr, []() { text_input_last_used = ++last_used; }, ICON_MY_TEXT_WRAP_ON, "",
            //                    nullptr, text_input_last_used});

            // // Example: Integer input with range validation
            // static int int_input_last_used = 0;
            // ImCmd::AddCommand(
            //     {"Set image quality (1-100)",
            //      []()
            //      {
            //          ImCmd::PromptInt(
            //              "Enter quality (1-100)...",
            //              [](const char *input) -> std::string
            //              {
            //                  if (strlen(input) == 0)
            //                      return "Enter a quality value between 1 and 100";
            //                  return fmt::format("Quality will be set to: {}", input);
            //              },
            //              ImCmd::ValidateIntRange(1, 100));
            //      },
            //      nullptr, [](const std::string &text) { spdlog::info("Quality set to: {}", std::stoi(text)); },
            //      nullptr,
            //      []() { int_input_last_used = ++last_used; }, ICON_MY_SETTINGS_WINDOW, "", nullptr,
            //      int_input_last_used});

            // // Example: Float input with range validation
            // static int float_input_last_used = 0;
            // ImCmd::AddCommand({"Set custom exposure",
            //                    []()
            //                    {
            //                        ImCmd::PromptFloat(
            //                            "Enter exposure value...",
            //                            [](const char *input) -> std::string
            //                            {
            //                                if (strlen(input) == 0)
            //                                    return "Enter an exposure value between -9.0 and 9.0";
            //                                return fmt::format("Exposure will be set to: {}", input);
            //                            },
            //                            ImCmd::ValidateFloatRange(-9.0f, 9.0f));
            //                    },
            //                    nullptr,
            //                    [](const std::string &text)
            //                    {
            //                        float val = std::stof(text);
            //                        spdlog::info("Would set exposure to: {}", val);
            //                    },
            //                    nullptr, []() { float_input_last_used = ++last_used; }, ICON_MY_EXPOSURE, "", nullptr,
            //                    float_input_last_used});

            // // Example: Complex validation with combined validators
            // static int filename_input_last_used = 0;
            // ImCmd::AddCommand(
            //     {"Save with custom filename",
            //      []()
            //      {
            //          auto validate =
            //              ImCmd::CombineValidators({ImCmd::ValidateNotEmpty(), [](const char *input) -> std::string
            //                                        {
            //                                            std::string s(input);
            //                                            // Check for invalid filename characters
            //                                            if (s.find_first_of("/\\:*?\"<>|") != std::string::npos)
            //                                                return "Filename contains invalid characters";
            //                                            return "";
            //                                        }});

            //          ImCmd::PromptText(
            //              "Enter filename...",
            //              [](const char *input) -> std::string
            //              {
            //                  if (strlen(input) == 0)
            //                      return "Enter a filename without extension";
            //                  return fmt::format("Will save as: {}.png", input);
            //              },
            //              validate);
            //      },
            //      nullptr, [](const std::string &text) { spdlog::info("Would save as: {}.png", text); }, nullptr,
            //      []() { filename_input_last_used = ++last_used; }, ICON_MY_SAVE_AS, "", nullptr,
            //      filename_input_last_used});

            ImCmd::SetNextCommandPaletteSearchBoxFocused();
            ImCmd::SetNextCommandPaletteSearch("");
        }

        bool prev_clicked = false, next_clicked = false, use_clicked = false, esc_clicked = false;

        if (ImGui::BeginTable("PaletteHelp", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ContextMenuInBody))
        {
            ImGui::TableNextColumn();
            prev_clicked = ImGui::Button(ICON_MY_ARROW_UP);
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            next_clicked = ImGui::Button(ICON_MY_ARROW_DOWN);
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::Text("to navigate");

            ImGui::TableNextColumn();
            ImGui::AlignCursor(HelloImGui::EmSize(5.f), 0.5f);
            use_clicked = ImGui::Button(ICON_MY_KEY_RETURN " to use", HelloImGui::EmToVec2(5.f, 0.f));

            ImGui::TableNextColumn();
            ImGui::AlignCursor(HelloImGui::EmSize(7.f), 1.f);
            esc_clicked = ImGui::Button(ICON_MY_KEY_ESC " to dismiss ", HelloImGui::EmToVec2(7.f, 0.f));

            ImGui::EndTable();
        }

        ImCmd::CommandPalette("Command palette", "Filter commands...");

        if (!ImCmd::IsAnyItemSelected())
        {
            if (ImGui::Shortcut(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat) || prev_clicked)
                ImCmd::FocusPreviousItem();
            else if (ImGui::Shortcut(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat) || next_clicked)
                ImCmd::FocusNextItem();
            else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || use_clicked)
                ImCmd::Submit();
        }

        ImCmd::EndCommandPalette();

        // Close window when we select an item, hit escape
        if (ImCmd::IsAnyItemSelected() || esc_clicked ||
            ImGui::GlobalShortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteOverActive) ||
            ImGui::GlobalShortcut(ImGuiMod_Ctrl | ImGuiKey_Period, ImGuiInputFlags_RouteOverActive))
        {
            ImGui::CloseCurrentPopup();
            open = false;
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::draw_about_dialog(bool &open)
{
    // work around HelloImGui rendering a couple frames to figure out sizes
    if (open && ImGui::GetFrameCount() > 2)
        ImGui::OpenPopup("About");

    auto &io = ImGui::GetIO();

    // Center window horizontally, align near top vertically
    auto pos = ImVec2(io.DisplaySize.x / 2, min(5.f * EmSize(), 0.1f * io.DisplaySize.y));
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowFocus();
    constexpr float icon_size            = 128.f;
    float           icon_col_width       = icon_size + EmSize();
    constexpr float min_window_width     = 2.f * icon_size;
    const float     desired_window_width = icon_size + 33 * EmSize();

    // Choose a width for the About popup: prefer desired, but clamp to the
    // application's display width (down to min_window_width). Also set
    // window size constraints so ImGui lays out text properly and wrapping
    // will occur at the window content width.
    {
        ImGuiStyle &style             = ImGui::GetStyle();
        float       padding           = style.WindowPadding.x;
        float       max_window_width  = io.DisplaySize.x - 2.f * padding;
        float       max_window_height = io.DisplaySize.y - 2.f * padding - pos.y;
        float       target_width      = clamp(desired_window_width, min_window_width, max_window_width);

        ImGui::SetNextWindowSizeConstraints(ImVec2(min_window_width, 0.f), ImVec2(max_window_width, max_window_height));
        ImGui::SetNextWindowSize(ImVec2(target_width, 0.f), ImGuiCond_Always);
    }

    if (ImGui::BeginPopup("About", ImGuiWindowFlags_NoSavedSettings))
    {
        open = false;
        ImGui::Spacing();

        auto platform_backend = [](PlatformBackendType type)
        {
            using T = PlatformBackendType;
            switch (type)
            {
            case T::FirstAvailable: return "FirstAvailable";
            case T::Glfw: return "GLFW 3";
            case T::Sdl: return "SDL 2";
            case T::Null: return "Null";
            default: return "Unknown";
            }
        }(m_params.platformBackendType);

        auto renderer_backend = [](RendererBackendType type)
        {
            using T = RendererBackendType;
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
            ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed, icon_col_width);
            ImGui::TableSetupColumn("description", ImGuiTableColumnFlags_WidthStretch, 1.f);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // right align the image
            ImGui::AlignCursor(icon_size + 0.5f * EmSize(), 1.f);
            ImageFromAsset("app_settings/icon-256.png", {icon_size, icon_size}); // show the app icon

            ImGui::TableNextColumn();

            ImGui::PushTextWrapPos(0.f);
            {
                ImGui::ScopedFont sf{m_sans_bold, 30};
                ImGui::HyperlinkText("HDRView", "https://github.com/wkjarosz/hdrview");
            }

            ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 18.f / 14.f);
            ImGui::TextUnformatted(version());
            ImGui::PopFont();
            ImGui::PushFont(m_sans_regular, ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
            ImGui::TextFmt("Built on {} for {} using the {} backend with {}.", build_timestamp(), architecture(),
                           platform_backend, renderer_backend);
            ImGui::PopFont();
            ImGui::PopTextWrapPos();

            ImGui::Spacing();

            {
                ImGui::PushTextWrapPos(0.f);
                ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
                ImGui::TextUnformatted(
                    "HDRView is a simple research-oriented tool for examining, comparing, manipulating, and "
                    "converting high-dynamic range images.");
                ImGui::PopFont();

                ImGui::PopTextWrapPos();
            }

            ImGui::Spacing();

            ImGui::EndTable();
        }

        ImGui::Spacing();

        {
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(
                "It is developed by Wojciech Jarosz, and is available under a 3-clause BSD license.");
            ImGui::PopTextWrapPos();
        }

        ImGui::Spacing();

        if (ImGui::BeginTabBar("AboutTabBar"))
        {
            if (ImGui::BeginTabItem("Keybindings", nullptr))
            {
                ImVec2 child_size = ImVec2(0, EmSize(14.f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
                ImGui::BeginChild(ImGui::GetID("cfg_infos"), child_size, ImGuiChildFlags_AlwaysUseWindowPadding);

                ImGui::PushTextWrapPos(0.f);

                ImGui::Spacing();
                ImGui::TextAligned2(0.5f, -FLT_MIN, "The main keyboard shortcut to remember is:");

                ImGui::PushFont(font("mono regular"), ImGui::GetStyle().FontSizeBase * 30.f / 14.f);
                ImGui::TextAligned2(0.5f, -FLT_MIN,
                                    ImGui::GetKeyChordNameTranslated(action("Command palette...").chord));
                ImGui::PopFont();

                ImGui::TextUnformatted("This opens the command palette, which lists every available HDRView command "
                                       "along with its keyboard shortcuts (if any).");
                ImGui::Dummy(EmToVec2(1.f, 1.f));

                ImGui::TextUnformatted("Many commands and their keyboard shortcuts are also listed in the menu bar.");

                ImGui::TextUnformatted(
                    "Additonally, left-click+drag will pan the image view, and scrolling the mouse/pinching "
                    "will zoom in and out.");

                ImGui::Spacing();
                ImGui::PopTextWrapPos();

                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Credits"))
            {
                ImVec2 child_size = ImVec2(0, EmSize(18.f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
                ImGui::BeginChild(ImGui::GetID("cfg_infos"), child_size, ImGuiChildFlags_AlwaysUseWindowPadding);

                ImGui::PushTextWrapPos(0.f);

                ImGui::Spacing();
                ImGui::TextUnformatted(
                    "HDRView additionally makes use of the following external libraries and techniques (in "
                    "alphabetical order):\n\n");

                // calculate left column based on the longest library name
                ImGui::PushFont(hdrview()->font("sans bold"), ImGui::GetStyle().FontSizeBase);
#ifndef __EMSCRIPTEN__
                float col_width = ImGui::CalcTextSize("portable-file-dialogs").x;
#else
                float col_width = ImGui::CalcTextSize("emscripten-browser-file").x;
#endif
                ImGui::PopFont();

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

                if (ImGui::PE::Begin("about_table2", 0))
                {
                    ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, col_width);

                    ImGui::PE::Hyperlink("Dear ImGui", "Omar Cornut's immediate-mode graphical user interface for C++.",
                                         "https://github.com/ocornut/imgui");
#ifdef __EMSCRIPTEN__
                    ImGui::PE::Hyperlink("emscripten", "An MIT-licensed LLVM-to-WebAssembly compiler.",
                                         "https://github.com/emscripten-core/emscripten");
                    ImGui::PE::Hyperlink("emscripten-browser-file",
                                         "Armchair Software's MIT-licensed header-only C++ library "
                                         "to open and save files in the browser.",
                                         "https://github.com/Armchair-Software/emscripten-browser-file");
#endif
                    ImGui::PE::Hyperlink("{fmt}", "A modern formatting library.", "https://github.com/fmtlib/fmt");
                    ImGui::PE::Hyperlink("Hello ImGui", "Pascal Thomet's cross-platform starter-kit for Dear ImGui.",
                                         "https://github.com/pthom/hello_imgui");
                    if (ICCProfile::lcms_version() != 0)
                        ImGui::PE::Hyperlink("lcms2", "LittleCMS color management engine.",
                                             "https://github.com/mm2/Little-CMS");
                    if (HDRVIEW_ENABLE_AVIF)
                        ImGui::PE::Hyperlink("aom", "For encoding and decoding AV1/AVIF/AVIFS-compressed HEIF images.",
                                             "https://aomedia.googlesource.com/aom");
                    if (HDRVIEW_ENABLE_HEIC)
                        ImGui::PE::Hyperlink("libde265", "For decoding HEIC/HEVC/H265-compressed HEIF images.",
                                             "https://github.com/strukturag/libde265");
                    if (HDRVIEW_ENABLE_LIBHEIF)
                        ImGui::PE::Hyperlink("libheif", "For loading HEIF images.",
                                             "https://github.com/strukturag/libheif");
                    if (HDRVIEW_ENABLE_LIBJXL)
                        ImGui::PE::Hyperlink("libjxl", "For loading & saving JPEG-XL images.",
                                             "https://github.com/libjxl/libjxl");
                    if (HDRVIEW_ENABLE_LIBJPEG)
                        ImGui::PE::Hyperlink("libjpeg (turbo)", "For loading & saving JPEG images.",
                                             "https://github.com/libjpeg-turbo/libjpeg-turbo");
                    if (HDRVIEW_ENABLE_LIBPNG)
                        ImGui::PE::Hyperlink("libpng", "For loading & saving PNG images.",
                                             "https://github.com/pnggroup/libpng");
                    if (HDRVIEW_ENABLE_LIBRAW)
                        ImGui::PE::Hyperlink("libraw", "For loading RAW images.", "https://github.com/LibRaw/LibRaw");

                    if (HDRVIEW_ENABLE_LIBUHDR)
                        ImGui::PE::Hyperlink("libuhdr", "For loading Ultra HDR JPEG images.",
                                             "https://github.com/google/libultrahdr");
                    if (HDRVIEW_ENABLE_LIBWEBP)
                        ImGui::PE::Hyperlink("libwebp", "For loading & saving WebP images.",
                                             "https://github.com/webmproject/libwebp");
                    ImGui::PE::Hyperlink(
                        "linalg", "Sterling Orsten's public domain, single header short vector math library for C++.",
                        "https://github.com/sgorsten/linalg");
                    ImGui::PE::Hyperlink("NanoGUI", "Bits of code from Wenzel Jakob's BSD-licensed NanoGUI library.",
                                         "https://github.com/mitsuba-renderer/nanogui");
                    ImGui::PE::Hyperlink("OpenEXR", "High Dynamic-Range (HDR) image file format.",
                                         "https://github.com/AcademySoftwareFoundation/openexr");
                    if (HDRVIEW_ENABLE_AVCI)
                        ImGui::PE::Hyperlink("OpenH264", "For decoding AVC/AVCI/AVCS/H264 files.",
                                             "https://github.com/cisco/openh264");
                    if (HDRVIEW_ENABLE_J2K)
                        ImGui::PE::Hyperlink("OpenJPEG", "For encoding/decoding J2K- and HTJ2K-compressed HEIF images.",
                                             "https://github.com/uclouvain/openjpeg");
                    if (HDRVIEW_ENABLE_HTJ2K)
                        ImGui::PE::Hyperlink("OpenJPH", "For encoding HTJ2K-compressed HEIF images.",
                                             "https://github.com/aous72/OpenJPH");
#ifndef __EMSCRIPTEN__
                    ImGui::PE::Hyperlink("portable-file-dialogs",
                                         "Sam Hocevar's WTFPL portable GUI dialogs library, C++11, single-header.",
                                         "https://github.com/samhocevar/portable-file-dialogs");
#endif
                    ImGui::PE::Hyperlink("smalldds", "Single-header library for loading DDS images.",
                                         "https://github.com/wkjarosz/smalldds");
                    ImGui::PE::Hyperlink("stb_image/write",
                                         "Single-header libraries for loading/saving various image formats.",
                                         "https://github.com/nothings/stb");
                    ImGui::PE::Hyperlink("tev", "Some code is adapted from Thomas Müller's tev.",
                                         "https://github.com/Tom94/tev");
                    ImGui::PE::Hyperlink("TinyXML2", "For parsing XMP metadata.",
                                         "https://github.com/leethomason/tinyxml2");
                    if (HDRVIEW_ENABLE_HEIC)
                        ImGui::PE::Hyperlink("x265", "For encoding HEIC/HEVC/H265 files.",
                                             "https://www.videolan.org/developers/x265.html");

                    ImGui::PE::End();
                }

                ImGui::PopStyleVar();
                ImGui::PopTextWrapPos();
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Build info"))
            {
                // Build the info text string for display and clipboard
                auto build_info_text = [&]()
                {
                    auto heif_info = get_heif_info();
                    auto jpg_info  = get_jpg_info();
                    auto png_info  = get_png_info();
                    auto jxl_info  = get_jxl_info();
                    auto raw_info  = get_raw_info();
                    auto uhdr_info = get_uhdr_info();
                    auto webp_info = get_webp_info();
                    auto tiff_info = get_tiff_info();

                    string info;
                    info += fmt::format("{:<16} : {}\n", "HDRView version", version());
                    info += fmt::format("{:<16} : {}\n", "Build timestamp", build_timestamp());
                    info += fmt::format("{:<16} : {}\n", "Architecture", architecture());
                    info += fmt::format("{:<16} : {}\n", "Build type", HDRVIEW_BUILD_TYPE);
                    info += fmt::format("{:<16} : {}\n", "Platform backend", platform_backend);
                    info += fmt::format("{:<16} : {}\n", "Renderer backend", renderer_backend);

                    info += fmt::format("{:<16} : {}\n", "ImGui version", ImGui::GetVersion());
                    info += fmt::format("{:<16} : {}\n", "EDR support", hasEdrSupport() ? "yes" : "no");

                    info += fmt::format("{:<16} : {}\n", "__EMSCRIPTEN__",
#ifdef __EMSCRIPTEN__
                                        "yes"
#else
                                        "(not defined)"
#endif
                    );
                    info += fmt::format("{:<16} : {}\n", "ASSETS_LOCATION",
#ifdef ASSETS_LOCATION
                                        ASSETS_LOCATION
#else
                                        "(not defined)"
#endif
                    );
                    info += fmt::format("{:<16} : {}\n", "HDRVIEW_ICONSET",
#ifdef HDRVIEW_ICONSET_FA6
                                        "Font Awesome 6"
#elif defined(HDRVIEW_ICONSET_LC)
                                        "Lucide Icons"
#elif defined(HDRVIEW_ICONSET_MS)
                                        "Material Symbols"
#elif defined(HDRVIEW_ICONSET_MD)
                                        "Material Design"
#elif defined(HDRVIEW_ICONSET_MDI)
                                        "Material Design Icons"
#endif
                    );

                    info += "\nBuild flag\n";
                    info += fmt::format("{:=<28}\n", "");

                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_LIBPNG);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_LIBJPEG);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_LIBUHDR);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_LIBJXL);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_LIBHEIF);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_J2K);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_HTJ2K);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_AVIF);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_HEIC);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_AVCI);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_LIBWEBP);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_LIBTIFF);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_LIBRAW);
                    info += PRINT_FEATURE("{:22} : {}\n", HDRVIEW_ENABLE_X3F);
                    info += "\n";

                    info += fmt::format("{:<8} {:<15}\n", "Library", "Version");
                    info += fmt::format("{:=<8} {:=<15}\n", "", "");
                    info += fmt::format(
                        "{:<8} {:<15}\n", "OpenEXR",
                        fmt::format("{}.{}.{}", OPENEXR_VERSION_MAJOR, OPENEXR_VERSION_MINOR, OPENEXR_VERSION_PATCH));

                    info +=
                        fmt::format("{:<8} {:<15}\n", "lcms2",
                                    ICCProfile::lcms_version() ? to_string(ICCProfile::lcms_version()) : "not found");

                    info += fmt::format("{:<8} {:<15}\n", "libheif", heif_info.value("version", "not enabled"));
                    info += fmt::format("{:<8} {:<15}\n", "libjpeg", jpg_info.value("version", "not enabled"));
                    info += fmt::format("{:<8} {:<15}\n", "libjxl", jxl_info.value("version", "not enabled"));
                    info += fmt::format("{:<8} {:<15}\n", "libpng", png_info.value("version", "not enabled"));
                    info += fmt::format("{:<8} {:<15}\n", "libraw", raw_info.value("version", "not enabled"));
                    info += fmt::format("{:<8} {:<15}\n", "libtiff", tiff_info.value("version", "not enabled"));
                    info += fmt::format("{:<8} {:<15}\n", "libuhdr", uhdr_info.value("version", "not enabled"));
                    info += fmt::format("{:<8} {:<15}\n", "libwebp", webp_info.value("version", "not enabled"));

                    if (png_info.value("enabled", false))
                    {
                        info += "\nlibpng features:\n";
                        info += fmt::format("{:=<23}\n", "");
                        for (auto &it : png_info["features"].items())
                        {
                            auto key = it.key();
                            bool val = it.value().get<bool>();
                            info += fmt::format("{:<16} : {}\n", key, val ? "yes" : "no");
                        }
                    }

                    if (heif_info.value("enabled", false))
                    {
                        info += "\nlibheif compression support:\n";
                        info += fmt::format("{:<13} {:<8} {:<8}\n", "Format", "decoding", "encoding");
                        info += fmt::format("{:=<13} {:=<8} {:=<8}\n", "", "", "");
                        if (heif_info.contains("features") && heif_info["features"].contains("compression"))
                        {
                            for (auto &it : heif_info["features"]["compression"].items())
                            {
                                auto codec = it.key();
                                auto val   = it.value();
                                bool dec   = val.value("decoder", false);
                                bool enc   = val.value("encoder", false);
                                info +=
                                    fmt::format("{:<13} {:<8} {:<8}\n", codec, dec ? "yes" : "no", enc ? "yes" : "no");
                            }
                        }
                    }
                    return info;
                };

                string build_info = build_info_text();

                ImVec2 child_size = ImVec2(0, EmSize(18.f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
                ImGui::BeginChild(ImGui::GetID("cfg_infos"), child_size, ImGuiChildFlags_AlwaysUseWindowPadding,
                                  ImGuiWindowFlags_HorizontalScrollbar);

                // ImGui::PushTextWrapPos(0.f);

                ImGui::PushFont(m_mono_regular, 0.f);
                ImGui::TextUnformatted(build_info.c_str());
                ImGui::PopFont();
                ImGui::Tooltip("Click to copy to clipboard.");
                if (ImGui::IsItemClicked())
                    ImGui::SetClipboardText(build_info.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                // ImGui::PopTextWrapPos();
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (ImGui::Button("Dismiss", EmToVec2(8.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Escape) ||
            ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_Space) ||
            ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_Slash))
            ImGui::CloseCurrentPopup();

        ImGui::ScrollWhenDraggingOnVoid(ImVec2(0.0f, -ImGui::GetIO().MouseDelta.y), ImGuiMouseButton_Left);
        ImGui::EndPopup();
    }
}
