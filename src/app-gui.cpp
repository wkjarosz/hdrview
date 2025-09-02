#include "app.h"

#include "colormap.h"
#include "fonts.h"
#include "hello_imgui/hello_imgui.h"
#include "image.h"
#include "imcmd_command_palette.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "texture.h"
#include "version.h"

#ifdef HDRVIEW_ENABLE_LIBJPEG
#include <jpeglib.h>
#endif

#ifdef HDRVIEW_ENABLE_HEIF
#include <libheif/heif.h>
#endif

#ifdef HDRVIEW_ENABLE_JPEGXL
#include <jxl/version.h>
#endif

#ifdef HDRVIEW_ENABLE_LIBPNG
#include <png.h>
#endif

#ifdef HDRVIEW_ENABLE_UHDR
#include <ultrahdr_api.h>
#endif

#include "platform_utils.h"

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

void HDRViewApp::draw_color_picker(bool &open)
{
    if (open)
        ImGui::OpenPopup("Choose custom background color");

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, 5.f * EmSize()), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.0f));
    if (ImGui::BeginPopup("Choose custom background color", ImGuiWindowFlags_AlwaysAutoResize |
                                                                ImGuiWindowFlags_NoSavedSettings |
                                                                ImGuiWindowFlags_NoDocking))
    {
        open                            = false;
        static float4 previous_bg_color = m_bg_color;
        if (ImGui::IsWindowAppearing())
            previous_bg_color = m_bg_color;
        ImGui::ColorPicker4("##Custom background color", (float *)&m_bg_color,
                            ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoAlpha,
                            (float *)&previous_bg_color);

        ImGui::Dummy(EmToVec2(1.f, 0.5f));
        if (ImGui::Button("OK", EmToVec2(5.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Enter))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", EmToVec2(5.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Escape))
        {
            m_bg_color = previous_bg_color;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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

        ImGui::Separator();
        MenuItem(action(reveal_in_file_manager_text()));
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
        ImGui::MenuItem(action("Locate settings file"));

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
    ImGui::WrappedTooltip("Increasing (Shift+E) or decreasing (e) the exposure. The displayed brightness of "
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
    ImGui::WrappedTooltip("Increase/decrease the blackpoint offset. The offset is added to the pixel value after "
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
        static const char *items[] = {"Gamma", "Colormap [0,1]", "Colormap [-1,1]"};
        for (int n = 0; n < IM_ARRAYSIZE(items); n++)
        {
            const bool is_selected = (m_tonemap == n);
            if (ImGui::Selectable(items[n], is_selected))
                m_tonemap = (Tonemap_)n;

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

void HDRViewApp::draw_command_palette(bool &open)
{
    if (open)
        ImGui::OpenPopup("Command palette...");

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2, 5.f * EmSize()), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2{EmSize(29), 0}, ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Command palette...", nullptr,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        open = false;
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
                                   m_bg_mode = (BackgroundMode_)clamp(selected_option, (int)BGMode_Black,
                                                                      (int)BGMode_COUNT - 1);
                                   if (m_bg_mode == BGMode_Custom_Color)
                                       m_dialogs["Custom background color picker"]->open = true;
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

        if (ImGui::BeginTable("PaletteHelp", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ContextMenuInBody))
        {
            // ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

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

            // ImGui::PopStyleColor();

            ImGui::EndTable();
        }

        ImCmd::CommandPalette("Command palette", "Filter commands...");

        // Close window when we select an item, hit escape, or unfocus the command palette window (click elsewhere)
        if (ImCmd::IsAnyItemSelected() || ImGui::GlobalShortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteOverActive) ||
            ImGui::GlobalShortcut(ImGuiMod_Ctrl | ImGuiKey_Period, ImGuiInputFlags_RouteOverActive))
            ImGui::CloseCurrentPopup();

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
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, 5.f * EmSize()), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowFocus();
    constexpr float icon_size     = 128.f;
    float2          col_width     = {icon_size + EmSize(), 32 * EmSize()};
    float           content_width = col_width[0] + col_width[1] + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextWindowContentSize(float2{content_width, 0.f});
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{2 * icon_size, icon_size},
        float2{content_width + 2.f * ImGui::GetStyle().WindowPadding.x + ImGui::GetStyle().ScrollbarSize,
               io.DisplaySize.y - 7.f * EmSize()});

    if (ImGui::BeginPopup("About", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_AlwaysAutoResize))
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
            ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed, col_width[0]);
            ImGui::TableSetupColumn("description", ImGuiTableColumnFlags_WidthFixed, col_width[1]);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // right align the image
            ImGui::AlignCursor(icon_size + 0.5f * EmSize(), 1.f);
            ImageFromAsset("app_settings/icon-256.png", {icon_size, icon_size}); // show the app icon

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

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[1] - EmSize());
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
                ImVec2 child_size = ImVec2(0, EmSize(18.f));
                ImGui::BeginChild(ImGui::GetID("cfg_infos"), child_size, ImGuiChildFlags_FrameStyle);

                ImGui::Text("ImGui version: %s", ImGui::GetVersion());

                ImGui::Text("EDR support: %s", hasEdrSupport() ? "yes" : "no");
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
                ImGui::Text("\tlibuhdr: %s", UHDR_LIB_VERSION_STR);
#else
                ImGui::Text("\tlibuhdr: no");
#endif
#ifdef HDRVIEW_ENABLE_LIBJPEG
#ifdef LIBJPEG_TURBO_VERSION
#define STR_HELPER(x) #x
#define MY_STR(x)     STR_HELPER(x)
                ImGui::Text("\tlibjpeg-turbo: %d (%s)", LIBJPEG_TURBO_VERSION_NUMBER, MY_STR(LIBJPEG_TURBO_VERSION));
#else
                ImGui::Text("\tlibjpeg: %d", JPEG_LIB_VERSION);
#endif
#else
                ImGui::Text("\tlibjpeg:  no");
#endif

#ifdef HDRVIEW_ENABLE_JPEGXL
                ImGui::Text("\tlibjxl: %d.%d.%d", JPEGXL_MAJOR_VERSION, JPEGXL_MINOR_VERSION, JPEGXL_PATCH_VERSION);
#else
                ImGui::Text("\tlibjxl:  no");
#endif
#ifdef HDRVIEW_ENABLE_HEIF
                ImGui::Text("\tlibheif: %s", heif_get_version());
#else
                ImGui::Text("\tlibheif: no");
#endif
#ifdef HDRVIEW_ENABLE_LIBPNG
                ImGui::Text("\tlibpng: %s", PNG_LIBPNG_VER_STRING);
#ifdef PNG_TEXT_SUPPORTED
                ImGui::Text("\t\tPNG_TEXT_SUPPORTED: yes");
#else
                ImGui::Text("\t\tPNG_TEXT_SUPPORTED: no");
#endif
#ifdef PNG_eXIf_SUPPORTED
                ImGui::Text("\t\tPNG_eXIf_SUPPORTED: yes");
#else
                ImGui::Text("\t\tPNG_eXIf_SUPPORTED: no");
#endif
#ifdef PNG_EASY_ACCESS_SUPPORTED
                ImGui::Text("\t\tPNG_EASY_ACCESS_SUPPORTED: yes");
#else
                ImGui::Text("\t\tPNG_EASY_ACCESS_SUPPORTED: no");
#endif
                ImGui::Text("\t\tPNG_READ_ALPHA_MODE_SUPPORTED: %s",
#ifdef PNG_READ_ALPHA_MODE_SUPPORTED
                            "yes"
#else
                            "no"
#endif
                );
                ImGui::Text("\t\tPNG_GAMMA_SUPPORTED: %s",
#ifdef PNG_GAMMA_SUPPORTED
                            "yes"
#else
                            "no"
#endif
                );
                ImGui::Text("\t\tPNG_USER_CHUNKS_SUPPORTED: %s",
#ifdef PNG_USER_CHUNKS_SUPPORTED
                            "yes"
#else
                            "no"
#endif
                );
                ImGui::Text("\t\tPNG_APNG_SUPPORTED: %s",
#ifdef PNG_APNG_SUPPORTED
                            "yes"
#else
                            "no"
#endif
                );
                ImGui::Text("\t\tPNG_PROGRESSIVE_READ_SUPPORTED: %s",
#ifdef PNG_PROGRESSIVE_READ_SUPPORTED
                            "yes"
#else
                            "no"
#endif
                );
#endif
                ImGui::EndChild();
                ImGui::PopFont();
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
