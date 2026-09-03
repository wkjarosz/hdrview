//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "fwd.h"

#include "app.h"
#include "image.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "implot.h"
#include "texture.h"
#include <hello_imgui/dpi_aware.h>

#include <string>

using namespace std;
using namespace HelloImGui;

void Image::draw_chromaticity_diagram(float width)
{
    static float2 vMin{-0.05f, -0.05f};
    static float2 vMax{0.75f, 0.9f};
    static float2 vSize  = vMax - vMin;
    static float  aspect = vSize.x / vSize.y;

    float const size = ImMax(width, EmSize(8.f));

    ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase);

    float4 plot_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); //{0.35f, 0.35f, 0.35f, 1.f};
    ImGui::PushStyleColor(ImGuiCol_WindowBg, plot_bg);
    if (ImPlot::BeginPlot("##Chromaticity diagram", ImVec2(size, size / aspect * 0.95f),
                          ImPlotFlags_Crosshairs | ImPlotFlags_Equal | ImPlotFlags_NoLegend | ImPlotFlags_NoTitle))
    {
        static constexpr float lambda_min   = 400.f;
        static constexpr float lambda_max   = 680.f;
        static constexpr int   sample_count = 200;

        auto text_color_f  = float4{0.f, 0.f, 0.f, 1.f};
        auto text_color_fc = contrasting_color(text_color_f);

        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImGui::GetColorU32(text_color_fc));

        ImPlot::GetInputMap().ZoomRate = 0.03f;
        ImPlot::SetupAxis(ImAxis_X1, "x", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_Foreground);
        ImPlot::SetupAxis(ImAxis_Y1, "y", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_Foreground);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
        ImPlot::SetupAxesLimits(vMin.x, vMax.x, vMin.y, vMax.y);
        ImPlot::SetupMouseText(ImPlotLocation_NorthEast, ImPlotMouseTextFlags_NoFormat);

        ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
        ImPlot::SetupFinish();
        ImGui::PopFont();

        ImPlot::PushPlotClipRect();

        //
        // plot background texture
        //
        ImPlot::PlotImage("##chromaticity_image", (ImTextureID)chromaticity_texture()->texture_handle(),
                          ImPlotPoint(0.0, 0.0), ImPlotPoint(.73f, .83f), {0.f, .83f}, {.73f, 0.f});

        auto normal_to_plot_tangent = [](const float2 &tangent, float pixel_length) -> float2
        {
            float2 p0            = ImPlot::PlotToPixels(0, 0);
            float2 tangent_px    = float2(ImPlot::PlotToPixels(tangent.x, tangent.y)) - p0;
            float2 normal_px     = pixel_length * normalize(float2{-tangent_px.y, tangent_px.x});
            auto   plot_tick_end = ImPlot::PixelsToPlot(p0 + normal_px);
            return float2((float)plot_tick_end.x, (float)plot_tick_end.y);
        };

        //
        // draw the spectral locus
        //
        float pixels_per_texel     = 1.f;
        float pixels_per_plot_unit = 1.f;
        float scale_factor         = 1.f;
        {
            float2 plot_size     = ImPlot::GetPlotSize();
            auto   plot_rect     = ImPlot::GetPlotLimits();
            pixels_per_plot_unit = length(plot_size / float2((float)plot_rect.X.Max - (float)plot_rect.X.Min,
                                                             (float)plot_rect.Y.Max - (float)plot_rect.Y.Min));
            // compute width in pixels of a chromaticity texture texel
            pixels_per_texel = 1.f / 256.f * pixels_per_plot_unit;
            scale_factor     = std::clamp(pixels_per_texel * 1.2f, 1.f, 4.f);

            // ImPlot::PlotLine draws ugly, unrounded, line segments, so we use AddPolyline ourselves
            ImVector<float2> poly;
            poly.resize(sample_count);
            // Iterate over all entries in poly and map them to pixel coordinates
            for (int i = 0; i < poly.Size; ++i)
            {
                float wavelength = lerp(lambda_min, lambda_max, ((float)i) / ((float)(sample_count - 1)));
                auto  pos        = wavelength_to_xy(wavelength);
                poly[i]          = ImPlot::PlotToPixels(pos.x, pos.y);
            }

            ImPlot::GetPlotDrawList()->AddPolyline((ImVec2 *)&poly[0], poly.Size, ImGui::GetColorU32(text_color_f),
                                                   std::max(1.f, 1.2f * pixels_per_texel), ImDrawFlags_Closed);
        }

        //
        // draw wavelength tick marks
        //
        {
            ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
            // Draw tick marks stored in tick_marks
            const float minor_tick_pixel_length = std::max(1.f, 2.f * pixels_per_texel);
            const float major_tick_pixel_length = std::max(1.f, 3.f * pixels_per_texel);

            static const float first_tick = (ImFloor(lambda_min / 10.f)) * 10.f;
            int                tick_count = (int)ImFloor((lambda_max - lambda_min) / 10.0f) + 1;
            for (int i = 0; i < tick_count; ++i)
            {
                // Compute wavelength for this tick
                float nm = first_tick + i * 10.0f;
                // Check if tick is at a 100 nm multiple
                bool is_major = (static_cast<int>(nm) % 100 == 0);

                float lambda = first_tick + i * 10.0f;

                // Compute chromaticity at this wavelength
                float2 pos     = wavelength_to_xy(lambda);
                float2 tangent = wavelength_to_xy(lambda + 1) - wavelength_to_xy(lambda - 1);
                float2 normal =
                    -normal_to_plot_tangent(tangent, is_major ? major_tick_pixel_length : minor_tick_pixel_length);

                // Tick mark parameters
                float2 tick[2] = {pos, pos + normal};

                ImPlotSpec tick_spec;
                tick_spec.LineColor  = ImVec4{0.f, 0.f, 0.f, 1.f};
                tick_spec.LineWeight = 0.5f * scale_factor;
                tick_spec.Stride     = sizeof(float2);
                ImPlot::PlotLine("##CCT_tick", &tick[0].x, &tick[0].y, 2, tick_spec);

                // Add text label for major ticks (100 nm multiples)
                if (is_major)
                {
                    static char label[8];
                    ImFormatString(label, sizeof(label), "%d nm", static_cast<int>(nm));

                    float4 bg = contrasting_color(contrasting_color(plot_bg));
                    bg.w      = 0.5f;

                    ImPlot::Annotation(tick[1].x, tick[1].y, bg, float2{1.f, -1.f} * round(normalize(normal)), false,
                                       "%s", label);
                }
            }
            ImGui::PopFont();
        }

        //
        // draw the locus of D (daylight) CCTs
        //
        {
            auto create_CCT_locus = []()
            {
                ImVector<float2> poly;
                poly.resize(sample_count);

                for (int i = 0; i < sample_count; ++i)
                {
                    float T = lerp(1668.f, 25000.f, ((float)i) / ((float)(sample_count - 1)));
                    poly[i] = Kelvin_to_xy(T);
                }

                return poly;
            };
            const static ImVector<float2> cct_locus = create_CCT_locus();

            // ImPlot::PlotLine draws ugly, unrounded, line segments, so we use AddPolyline ourselves
            ImVector<float2> poly = cct_locus;
            // Iterate over all entries in poly and map them to pixel coordinates
            for (int i = 0; i < poly.Size; ++i) poly[i] = ImPlot::PlotToPixels({poly[i].x, poly[i].y});

            ImPlot::GetPlotDrawList()->AddPolyline((ImVec2 *)&poly[0], poly.Size, ImGui::GetColorU32(text_color_f),
                                                   scale_factor, ImDrawFlags_None);

            const float label_font_scale = 1.0f;
            const float scale            = 1.f;
            ImGui::PushFont(hdrview()->font("sans regular"),
                            ImGui::GetStyle().FontSizeBase * label_font_scale * scale / 2.f);

            constexpr int temp_step = 1000;
            // Tracks the last tick actually drawn, so labels that would overlap it are skipped. Starts far
            // off-plot so the first tick always draws.
            float2 prev_tick_end = {-100000.f, -100000.f};
            for (int temp = 2000; temp <= 25000; temp += temp_step)
            {
                float2 xy = Kelvin_to_xy((float)temp);
                char   label[8];
                snprintf(label, sizeof(label), "%dK", temp);
                float2 text_size = ImGui::CalcTextSize(label);

                // Compute tangent and normal
                float2 tangent = normalize(Kelvin_to_xy((float)(temp - 1)) - Kelvin_to_xy((float)(temp + 1)));
                float2 normal  = normal_to_plot_tangent(tangent, scale_factor * 2.f);

                // Tick mark parameters
                float2 tick[2] = {xy, xy + normal};

                // Only draw this tick if it doesn't overlap with the previous tick
                const float min_dist = 5.0f; // minimum pixel distance between ticks

                float2 tick_end_px      = ImPlot::PlotToPixels(ImPlotPoint(tick[1].x, tick[1].y));
                float2 prev_tick_end_px = ImPlot::PlotToPixels(ImPlotPoint(prev_tick_end.x, prev_tick_end.y));
                bool   draw             = length(tick_end_px - prev_tick_end_px) > min_dist &&
                            (2.f * text_size.y < abs(tick_end_px.y - prev_tick_end_px.y) ||
                             1.5f * text_size.x < abs(tick_end_px.x - prev_tick_end_px.x));

                if (draw)
                {
                    ImPlotSpec tick_spec;
                    tick_spec.LineColor  = text_color_f;
                    tick_spec.LineWeight = 0.5f * scale_factor;
                    tick_spec.Stride     = sizeof(float2);
                    ImPlot::PlotLine("##CCT_tick", &tick[0].x, &tick[0].y, 2, tick_spec);
                    prev_tick_end = tick[1];

                    ImPlot::Annotation(tick[1].x, tick[1].y, ImVec4(1, 1, 1, 0.5), ImVec2(1, 1), false, "%s", label);
                }
            }

            ImGui::PopFont();
        }

        //
        // draw the primaries, gamut triangle, whitepoint, and text labels
        //
        {
            Chromaticities gamut_chr{chromaticities.value_or(Chromaticities{})};
            ImVec4         colors[] = {
                {0.8f, 0.f, 0.f, 1.f}, {0.f, 0.8f, 0.f, 1.f}, {0.f, 0.f, 0.8f, 1.f}, {0.5f, 0.5f, 0.5f, 1.f}};
            const char *names[]     = {"R", "G", "B", "W"};
            double2     primaries[] = {double2(gamut_chr.red), double2(gamut_chr.green), double2(gamut_chr.blue),
                                       double2(gamut_chr.red)};
            static bool clicked[4]  = {false, false, false, false};
            static bool hovered[4]  = {false, false, false, false};
            static bool held[4]     = {false, false, false, false};

            primaries[3] = double2(gamut_chr.red);

            ImPlotSpec triangle_spec;
            triangle_spec.LineColor  = text_color_fc;
            triangle_spec.LineWeight = scale_factor;
            triangle_spec.Stride     = sizeof(double2);
            ImPlot::PlotLine("##gamut_triangle", &primaries[0].x, &primaries[0].y, 4, triangle_spec);

            primaries[3] = double2(gamut_chr.white);

            // ImPlot::PlotScatter draws ugly, unrounded, circles, so we use AddPolyline ourselves
            // Iterate over all entries in poly and map them to pixel coordinates
            std::array<ImVec2, 4> poly = {ImPlot::PlotToPixels(ImPlotPoint(primaries[0].x, primaries[0].y)),
                                          ImPlot::PlotToPixels(ImPlotPoint(primaries[1].x, primaries[1].y)),
                                          ImPlot::PlotToPixels(ImPlotPoint(primaries[2].x, primaries[2].y)),
                                          ImPlot::PlotToPixels(ImPlotPoint(primaries[3].x, primaries[3].y))};
            for (int i = 0; i < 4; ++i)
                ImPlot::GetPlotDrawList()->AddCircleFilled(
                    poly[i], 2.5f * scale_factor,
                    ImGui::GetColorU32(clicked[i] || hovered[i] || held[i] ? text_color_f : text_color_fc), 0);

            ImGui::PushFont(hdrview()->font("sans bold"), ImGui::GetStyle().FontSizeBase);
            for (int i = 0; i < 4; ++i)
            {
                if (ImPlot::DragPoint(i, &primaries[i].x, &primaries[i].y, colors[i], 1.5f * scale_factor,
                                      ImPlotDragToolFlags_Delayed, &clicked[i], &hovered[i], &held[i]))
                {
                    gamut_chr.red   = float2((float)primaries[0].x, (float)primaries[0].y);
                    gamut_chr.green = float2((float)primaries[1].x, (float)primaries[1].y);
                    gamut_chr.blue  = float2((float)primaries[2].x, (float)primaries[2].y);
                    gamut_chr.white = float2((float)primaries[3].x, (float)primaries[3].y);
                    chromaticities  = gamut_chr;
                    compute_color_transform();
                }

                // draw text label shadow
                ImPlot::PushStyleColor(ImPlotCol_InlayText, ImGui::GetColorU32(text_color_f));
                float2 offset{4.f * scale_factor, -4.f * scale_factor};
                ImPlot::PlotText(names[i], primaries[i].x, primaries[i].y, offset);
                ImPlot::PopStyleColor();

                // draw text label
                ImPlot::PushStyleColor(ImPlotCol_InlayText, ImGui::GetColorU32(text_color_fc));
                offset -= 1.f;
                ImPlot::PlotText(names[i], primaries[i].x, primaries[i].y, offset);
                ImPlot::PopStyleColor();

                // ImPlot::PushStyleColor(ImPlotCol_InlayText, ImGui::GetColorU32(text_color_f));
                // ImVec2 offset{4.f * scale, -4.f * scale};
                // ImPlot::PlotText(names[i], primaries[i].x, primaries[i].y, offset);
                // ImPlot::PopStyleColor();
            }

            ImGui::PopFont();
        }

        //
        // draw the hovered pixel in the chromaticity diagram
        //
        {
            auto &io      = ImGui::GetIO();
            auto  rgb2xyz = mul(M_RGB_to_XYZ, inverse(M_to_sRGB));
            if (hdrview()->mouse_over_viewport())
            {
                auto   hovered_pixel = int2{hdrview()->pixel_at_app_pos(io.MousePos)};
                float4 color32       = hdrview()->pixel_value(hovered_pixel, false, 0);

                float3 XYZ = mul(rgb2xyz, color32.xyz());
                float2 xy  = XYZ.xy() / (XYZ.x + XYZ.y + XYZ.z);

                ImPlotSpec spec;
                spec.LineColor  = ImVec4{0.f, 0.f, 0.f, 1.0f};
                spec.MarkerSize = 2.f;
                ImPlot::PlotScatter("##HoveredPixel", &xy.x, &xy.y, 1, spec);
            }
        }
        ImPlot::PopPlotClipRect();

        ImPlot::PopStyleColor();

        ImGui::PushFont(hdrview()->font("sans regular"), ImGui::GetStyle().FontSizeBase * 10.f / 14.f);
        ImPlot::EndPlot();
        ImGui::PopFont();
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void Image::draw_colorspace()
{
    auto bold_font = hdrview()->font("sans bold");

    static const ImGuiTableFlags table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize;
    // The enclosing window scrolls this panel (see the ImGuiWindowFlags_AlwaysVerticalScrollbar the
    // Colorspace DockableWindow is created with), so the table itself neither scrolls nor sits in a
    // scrolling child.
    if (ImGui::PE::Begin("Colorspace", table_flags))
    {
        // The diagram gets its own full-width row when the value column alone is too narrow to render it legibly.
        const bool diagram_fits_in_value_column = ImGui::PE::ColumnWidth(1) > HelloImGui::EmSize(12.f);
        ImGui::Indent(ImGui::GetStyle().CellPadding.x);
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);

        ImGui::PE::WrappedText(
            "Profile name",
            metadata.value<string>("color profile", color_profile_name(ColorGamut_sRGB_BT709, TransferFunction::Linear))
                .c_str(),
            "The color profile (primaries and transfer function) applied at load time to make the values linear. This "
            "might come from various sources (ICC profiles, CICP tags, structured metadata provided by the image "
            "loading library). If no color profile is found, HDRView assumes BT.709/sRGB primaries with a D65 "
            "whitepoint, and an sRGB transfer function for SDR images.",
            bold_font, FLT_MAX);

        ImGui::PE::Entry("Color gamut",
                         [&]
                         {
                             bool modified = false;
                             auto csn      = color_gamut_names();
                             auto open_combo =
                                 ImGui::BeginCombo("##Color gamut", color_gamut_name((ColorGamut_)color_space),
                                                   ImGuiComboFlags_HeightLargest);
                             if (open_combo)
                             {
                                 for (ColorGamut n = ColorGamut_FirstNamed; n <= ColorGamut_LastNamed; ++n)
                                 {
                                     auto       cg          = (ColorGamut_)n;
                                     const bool is_selected = (color_space == n);
                                     if (ImGui::Selectable(csn[n], is_selected))
                                     {
                                         color_space = cg;
                                         spdlog::debug("Switching to color space {}.", n);
                                         chromaticities = ::gamut_chromaticities(cg);
                                         compute_color_transform();
                                         modified = true;
                                     }

                                     // Set the initial focus when opening the combo (scrolling + keyboard
                                     // navigation focus)
                                     if (is_selected)
                                         ImGui::SetItemDefaultFocus();
                                 }
                                 ImGui::EndCombo();
                             }
                             return modified;
                         });
        ImGui::Tooltip("Interpret the values stored in the file using the chromaticities of a common color profile.");

        ImGui::PE::Entry("White point",
                         [&]
                         {
                             bool modified = false;
                             auto wpn      = white_point_names();
                             ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x < EmSize(8.f)
                                                         ? EmSize(8.f)
                                                         : -FLT_MIN); // use the full width of the column
                             auto open_combo = ImGui::BeginCombo("##White point", white_point_name(white_point),
                                                                 ImGuiComboFlags_HeightLargest);
                             if (open_combo)
                             {
                                 for (WhitePoint n = WhitePoint_FirstNamed; n <= WhitePoint_LastNamed; ++n)
                                 {
                                     auto       wp          = (WhitePoint_)n;
                                     const bool is_selected = (white_point == n);
                                     if (ImGui::Selectable(wpn[n], is_selected))
                                     {
                                         white_point = wp;
                                         spdlog::debug("Switching to white point {}.", n);
                                         if (!chromaticities)
                                             chromaticities = Chromaticities{};
                                         chromaticities->white = ::white_point(wp);
                                         compute_color_transform();
                                     }

                                     // Set the initial focus when opening the combo (scrolling + keyboard
                                     // navigation focus)
                                     if (is_selected)
                                         ImGui::SetItemDefaultFocus();
                                 }
                                 ImGui::EndCombo();
                             }
                             return modified;
                         });

        const Chromaticities chr_orig{chromaticities.value_or(Chromaticities{})};
        Chromaticities       chr{chr_orig};
        bool                 edited = false;

        edited |= ImGui::PE::SliderFloat2("Red", &chr.red.x, 0.f, 1.f, "%.4f");
        edited |= ImGui::PE::SliderFloat2("Green", &chr.green.x, 0.f, 1.f, "%.4f");
        edited |= ImGui::PE::SliderFloat2("Blue", &chr.blue.x, 0.f, 1.f, "%.4f");

        if (chr_orig != chr || edited)
        {
            spdlog::debug("Setting chromaticities to ({}, {}), ({}, {}), ({}, {}), ({}, {})", chr.red.x, chr.red.y,
                          chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x, chr.white.y);
            chromaticities = chr;
            compute_color_transform();
        }

        chr       = chromaticities.value_or(Chromaticities{});
        float2 wp = chr.white;

        if (ImGui::PE::SliderFloat2("White point", &wp.x, 0.f, 1.f, "%.4f") || wp != chr.white)
        {
            chr.white = wp;
            spdlog::info("Setting chromaticities to ({}, {}), ({}, {}), ({}, {}), ({}, {})", chr.red.x, chr.red.y,
                         chr.green.x, chr.green.y, chr.blue.x, chr.blue.y, chr.white.x, chr.white.y);
            chromaticities = chr;
            compute_color_transform();
        }

        ImGui::PE::Entry(
            "Adopted neutral",
            [&]
            {
                bool has_an = adopted_neutral.has_value();
                if (ImGui::Checkbox("##hidden", &has_an))
                {
                    if (has_an)
                        adopted_neutral = wp;
                    else
                        adopted_neutral.reset();
                    compute_color_transform();
                }

                ImGui::SetNextItemWidth(-FLT_MIN);

                if (has_an && ImGui::SliderFloat2("##hidden", &adopted_neutral->x, 0.f, 1.f, "%.4f"))
                    compute_color_transform();

                return false;
            },
            "Specifies the CIE (x,y) coordinates that should be considered neutral during "
            "color rendering. Pixels in the image file whose (x,y) coordinates match the "
            "adoptedNeutral value should be mapped to neutral values on the display.");

        ImGui::PE::Entry(
            "Adaptation",
            [&]
            {
                bool modified   = false;
                auto open_combo = ImGui::BeginCombo("##Adaptation", adaptation_method_name(adaptation_method),
                                                    ImGuiComboFlags_HeightLargest);
                if (open_combo)
                {
                    for (AdaptationMethod_ n = 0; n < AdaptationMethod_Count; ++n)
                    {
                        auto       am          = (AdaptationMethod)n;
                        const bool is_selected = (adaptation_method == am);
                        if (ImGui::Selectable(adaptation_method_name(am), is_selected))
                        {
                            adaptation_method = am;
                            spdlog::debug("Switching to adaptation method {}.", n);
                            compute_color_transform();
                            modified = true;
                        }

                        // Set the initial focus when opening the combo (scrolling + keyboard navigation
                        // focus)
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                return modified;
            },
            "Method for chromatic adaptation transform.");

        ImGui::PE::InputFloat3("Yw", &luminance_weights.x, "%+8.2e",
                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly,
                               "Channel weights to compute the luminance (Y) of a pixel.");

        ImGui::PE::Entry(
            "Color matrix",
            [&]
            {
                bool modified = false;
                modified |= ImGui::InputFloat3("##M0", &M_to_sRGB[0][0], "%+8.2e",
                                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
                // ImGui::NewLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                modified |= ImGui::InputFloat3("##M1", &M_to_sRGB[1][0], "%+8.2e",
                                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
                // ImGui::NewLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                modified |= ImGui::InputFloat3("##M2", &M_to_sRGB[2][0], "%+8.2e",
                                               ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
                return modified;
            });

        if (diagram_fits_in_value_column)
            ImGui::PE::Entry("Diagram",
                             [this]()
                             {
                                 draw_chromaticity_diagram(ImGui::GetContentRegionAvail().x);
                                 return false;
                             });
        else
            ImGui::PE::FullWidthEntry("Diagram",
                                      [this](float width)
                                      {
                                          draw_chromaticity_diagram(width);
                                          return false;
                                      });

        ImGui::PopStyleVar();
        ImGui::Unindent(ImGui::GetStyle().CellPadding.x);
        ImGui::PE::End();
    }
}
