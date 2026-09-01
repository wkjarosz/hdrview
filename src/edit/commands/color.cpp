//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file color.cpp
    \author Wojciech Jarosz

    The edits that need to operate on a group's channels together, instead of independently.
*/

#include "edit/commands.h"

#include "colorspace.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"

#include <hello_imgui/hello_imgui.h>
#include <implot.h>

#include <cmath>

namespace
{

class ConvertColorSpace final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Convert color space...", "Change primaries", "Change white point", "Gamut conversion"},
                ICON_MY_COLORSPACE,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Convert",
                27.f};
    }

    //! The file's own tag, so a conversion starts from what the pixels actually are.
    void on_open(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        m_src_gamut = img->color_space;
        m_src_white = img->white_point;
        if (m_src_gamut == ColorGamut_Unspecified || m_src_gamut == ColorGamut_Custom)
            m_src_gamut = ColorGamut_sRGB_BT709;
        if (m_src_white == WhitePoint_Unspecified)
            m_src_white = WhitePoint_D65;
        m_method = img->adaptation_method;
    }

    void draw(EditContext &) override
    {
        auto gamut_combo = [](const char *label, int *value)
        {
            if (ImGui::BeginCombo(label, color_gamut_name(ColorGamut_(*value)), ImGuiComboFlags_HeightLargest))
            {
                for (int n = ColorGamut_FirstNamed; n <= ColorGamut_LastNamed; ++n)
                    if (ImGui::Selectable(color_gamut_name(ColorGamut_(n)), *value == n))
                        *value = n;
                ImGui::EndCombo();
            }
        };
        auto white_combo = [](const char *label, int *value)
        {
            if (ImGui::BeginCombo(label, white_point_name(WhitePoint_(*value)), ImGuiComboFlags_HeightLargest))
            {
                for (int n = WhitePoint_FirstNamed; n <= WhitePoint_LastNamed; ++n)
                    if (ImGui::Selectable(white_point_name(WhitePoint_(n)), *value == n))
                        *value = n;
                ImGui::EndCombo();
            }
        };

        ImGui::SeparatorText("From");
        gamut_combo("Primaries##from", &m_src_gamut);
        white_combo("White point##from", &m_src_white);
        ImGui::Tooltip("What the samples already are, taken from the image's own tag. Correcting it here "
                       "changes how they are read, not what they are.");

        ImGui::SeparatorText("To");
        gamut_combo("Primaries##to", &m_dst_gamut);
        white_combo("White point##to", &m_dst_white);

        if (ImGui::BeginCombo("Adaptation", adaptation_method_name(AdaptationMethod(m_method))))
        {
            for (int n = 0; n < AdaptationMethod_Count; ++n)
                if (ImGui::Selectable(adaptation_method_name(AdaptationMethod(n)), m_method == n))
                    m_method = n;
            ImGui::EndCombo();
        }
        ImGui::Tooltip("How a change of white point is accounted for. Bradford is the usual choice; the "
                       "identity transform leaves the primaries to do it alone, which shifts neutrals.");

        float3x3 M;
        if (!color_conversion_matrix(M, from(), to(), AdaptationMethod(m_method)))
            ImGui::TextWrapped("These describe the same color space, so this would leave the samples as they are.");
    }

    void apply(EditContext &ctx) override
    {
        float3x3 M;
        color_conversion_matrix(M, from(), to(), AdaptationMethod(m_method));

        const int  g = m_dst_gamut, w = m_dst_white, m = m_method;
        const auto tagged = to();

        ctx.modify_colors(
            "Convert color space",
            // Premultiplied alpha is not in the way here: the conversion is a matrix, and scaling every
            // component by alpha commutes with it.
            [M](const float4 &c, int2) { return float4{la::mul(M, c.xyz()), c.w}; },
            [g, w, m, tagged](Image &image)
            {
                image.chromaticities    = tagged;
                image.adopted_neutral   = std::nullopt;
                image.adaptation_method = AdaptationMethod(m);
                image.compute_color_transform();

                // compute_color_transform() names the space from the chromaticities, which is the
                // authority; these only matter when the destination is one it cannot recognize.
                image.color_space = ColorGamut_(g);
                image.white_point = WhitePoint_(w);

                // The samples are linear light in the new primaries, and the Colorspace panel reads this
                // as its "Profile name".
                image.metadata["color profile"] = color_profile_name(ColorGamut_(g), TransferFunction::Linear);
            });
    }

private:
    Chromaticities from() const
    {
        Chromaticities c = gamut_chromaticities(ColorGamut_(m_src_gamut));
        c.white          = white_point(WhitePoint_(m_src_white));
        return c;
    }
    Chromaticities to() const
    {
        Chromaticities c = gamut_chromaticities(ColorGamut_(m_dst_gamut));
        c.white          = white_point(WhitePoint_(m_dst_white));
        return c;
    }

    int m_src_gamut = ColorGamut_sRGB_BT709, m_dst_gamut = ColorGamut_sRGB_BT709;
    int m_src_white = WhitePoint_D65, m_dst_white = WhitePoint_D65;
    int m_method = AdaptationMethod_Bradford;
};

class ChannelMixer final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Channel mixer...", "Mix channels", "Monochrome"},
                ICON_MY_CHANNEL_MIXER,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Mix",
                27.f};
    }

    void draw(EditContext &) override
    {
        // Radio-height text, so the label sits on the same line as the buttons beside it rather than
        // riding above them.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Output channel");

        // Each named in its own color, and gray last: the output is one of
        // four rather than three plus a mode, so nothing has to say what happens when both are set.
        const char  *names[]  = {"Red", "Green", "Blue", "Monochrome"};
        const ImVec4 colors[] = {ImVec4(0.90f, 0.35f, 0.35f, 1.f), ImVec4(0.35f, 0.85f, 0.35f, 1.f),
                                 ImVec4(0.45f, 0.55f, 1.00f, 1.f), ImVec4(0.80f, 0.80f, 0.80f, 1.f)};

        for (int i = 0; i < Out_COUNT; ++i)
        {
            // Only between them: SameLine() before the first would put it back on the line above.
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, colors[i]);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, colors[i]);
            ImGui::RadioButton(names[i], &m_output, i);
            ImGui::PopStyleColor(2);
        }
        ImGui::Tooltip("Which channel the weights below are written to. Monochrome writes one gray value "
                       "to all three.");

        float3 &w = m_rows[m_output];

        ImGui::DragFloat("Red##weight", &w.x, 0.5f, -200.f, 200.f, "%.1f%%");
        ImGui::DragFloat("Green##weight", &w.y, 0.5f, -200.f, 200.f, "%.1f%%");
        ImGui::DragFloat("Blue##weight", &w.z, 0.5f, -200.f, 200.f, "%.1f%%");

        // The checkbox first, so that the total -- which changes width as it changes -- has nothing after
        // it to push around.
        ImGui::Checkbox("Normalize", &m_normalize);
        ImGui::Tooltip("Divides each row by its own total, so the mix neither brightens nor darkens. Off, a "
                       "total above 100% lightens the result and one below darkens it.");
        ImGui::SameLine();
        ImGui::TextDisabled("Total: %.1f%%", w.x + w.y + w.z);
    }

    void apply(EditContext &ctx) override
    {
        auto row = [norm = m_normalize](const float3 &r)
        {
            const float3 v   = r / 100.f;
            const float  sum = v.x + v.y + v.z;
            // A row summing to zero is a legitimate difference of channels; leaving it alone is the only
            // thing to do rather than dividing by nothing.
            return norm && std::abs(sum) > 1e-6f ? v / sum : v;
        };

        if (m_output == Out_Monochrome)
        {
            const float3 g = row(m_rows[Out_Monochrome]);
            ctx.modify_colors("Channel mixer",
                              [g](const float4 &c, int2)
                              {
                                  const float y = la::dot(g, c.xyz());
                                  return float4{y, y, y, c.w};
                              });
        }
        else
        {
            const float3 r = row(m_rows[Out_Red]), g = row(m_rows[Out_Green]), b = row(m_rows[Out_Blue]);
            ctx.modify_colors("Channel mixer", [r, g, b](const float4 &c, int2)
                              { return float4{la::dot(r, c.xyz()), la::dot(g, c.xyz()), la::dot(b, c.xyz()), c.w}; });
        }
    }

private:
    //! Which channel the weights are written to, gray among them rather than beside them.
    enum Output : int
    {
        Out_Red = 0,
        Out_Green,
        Out_Blue,
        Out_Monochrome,

        Out_COUNT
    };

    // A row of source weights per output channel, plus the one that makes a single gray from all three.
    // Kept as percentages the way Photoshop presents them, since that is how the numbers are read:
    // "40% of the red channel", not "0.4".
    //
    // Thirds rather than 33.3 each: a monochrome default that announces a total of 99.9% reads as an error
    // in the dialog rather than as the rounding it is.
    float3 m_rows[Out_COUNT] = {
        {100.f, 0.f, 0.f}, {0.f, 100.f, 0.f}, {0.f, 0.f, 100.f}, {100.f / 3.f, 100.f / 3.f, 100.f / 3.f}};
    int  m_output    = Out_Red;
    bool m_normalize = false;
};

class HueSaturation final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Hue/saturation...", "Colorize", "Desaturate"},
                ICON_MY_HUE_SATURATION,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Apply",
                27.f};
    }

    void draw(EditContext &) override
    {
        // The ranges Photoshop uses: hue in degrees around the wheel, the other two as a percentage away
        // from where they are.
        ImGui::SliderFloat("Hue", &m_hue, -180.f, 180.f, "%+.0f deg");
        ImGui::SliderFloat("Saturation", &m_saturation, -100.f, 100.f, "%+.0f%%");
        ImGui::SliderFloat("Lightness", &m_lightness, -100.f, 100.f, "%+.0f%%");
        ImGui::Tooltip("Lightness mixes toward black or white rather than changing the lightness itself, "
                       "which would wash the color out on the way.");

        // The wheel as it is and as the settings would leave it, which is easier to judge than the numbers.
        auto strip = [](const char *id, float h, float s, float l)
        {
            const float  w    = ImGui::GetContentRegionAvail().x;
            const float  step = std::max(1.f, w / 64.f);
            const ImVec2 p    = ImGui::GetCursorScreenPos();
            auto        *dl   = ImGui::GetWindowDrawList();

            for (float x = 0.f; x < w; x += step)
            {
                const float3 rgb =
                    adjust_HSL(HSL_to_RGB(float3{x / w, 1.f, 0.5f}), h / 360.f, (s + 100.f) / 100.f, l / 100.f);
                dl->AddRectFilled(ImVec2(p.x + x, p.y), ImVec2(p.x + x + step, p.y + ImGui::GetFrameHeight()),
                                  ImGui::ColorConvertFloat4ToU32(ImVec4(rgb.x, rgb.y, rgb.z, 1.f)));
            }
            ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
            ImGui::SetItemTooltip("%s", id);
        };
        strip("The hue wheel as it is", 0.f, 0.f, 0.f);
        strip("The hue wheel as these settings would leave it", m_hue, m_saturation, m_lightness);
    }

    void apply(EditContext &ctx) override
    {
        const float h = m_hue / 360.f, s = (m_saturation + 100.f) / 100.f, l = m_lightness / 100.f;
        ctx.modify_colors("Hue/saturation",
                          [h, s, l](const float4 &c, int2) { return float4{adjust_HSL(c.xyz(), h, s, l), c.w}; });
    }

private:
    float m_hue = 0.f, m_saturation = 0.f, m_lightness = 0.f;
};

class BrightnessContrast final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Brightness/contrast...", "Levels", "Tone curve"},
                ICON_MY_BRIGHTNESS_CONTRAST,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Apply",
                27.f};
    }

    void draw(EditContext &) override
    {
        // The curve first, since it is what the two sliders are for and reading it is quicker than
        // reading the numbers. Both are drawn whichever is in force, the inactive one dimmed, so the
        // difference between them is visible before it is chosen.
        draw_curve();

        ImGui::SliderFloat("Brightness", &m_brightness, -1.f, 1.f, "%+.3f");
        ImGui::Tooltip("Shift the 50% gray midpoint.\n\nAbove zero this lifts a previously darker value to "
                       "50%; below zero it dims a previously brighter one to 50%.");

        ImGui::SliderFloat("Contrast", &m_contrast, -1.f, 1.f, "%+.3f");
        ImGui::Tooltip("Change the slope at the new 50% midpoint. At -1 everything collapses to one level; "
                       "at +1 the curve is vertical and only black and white are left.");

        ImGui::Checkbox("Linear", &m_linear);
        ImGui::Tooltip("A straight line through the midpoint, which runs past black and white rather than "
                       "stopping at them -- so an HDR sample keeps its relation to its neighbors. Unticked, "
                       "an s-curve that approaches them without ever arriving.");

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Apply to");
        const char *names[] = {"RGB", "Lightness", "Chromaticity"};
        for (int i = 0; i < Channel_COUNT; ++i)
        {
            ImGui::SameLine();
            ImGui::RadioButton(names[i], &m_channel, i);
        }
        ImGui::Tooltip("RGB moves the three channels alike, which shifts saturation along with everything "
                       "else. The other two work in L*a*b*, where lightness and color are separate: one "
                       "changes how light the image is and leaves its colors, the other does the reverse.");
    }

    void apply(EditContext &ctx) override
    {
        auto img = ctx.image();
        if (!img)
            return;

        const float slope    = slope_of(m_contrast);
        const float midpoint = (1.f - m_brightness) / 2.f;
        const float bias     = (m_brightness + 1.f) / 2.f;
        const bool  linear   = m_linear;

        auto curve = [slope, midpoint, bias, linear](float v) {
            return linear ? brightness_contrast_linear(v, slope, midpoint)
                          : brightness_contrast_nonlinear(v, slope, bias);
        };

        if (m_channel == Channel_RGB)
        {
            ctx.modify_colors("Brightness/contrast", [curve](const float4 &c, int2)
                              { return float4{curve(c.x), curve(c.y), curve(c.z), c.w}; });
            return;
        }

        // Through the image's own primaries rather than sRGB's: L*a*b* is defined from XYZ, and what the
        // samples mean in XYZ is what the image says they do.
        const float3x3 to_XYZ   = img->M_RGB_to_XYZ;
        const float3x3 from_XYZ = img->M_XYZ_to_RGB;
        const float3   white    = img->chromaticities ? XYZ_from_xy(img->chromaticities->white) : Lab_reference_white();
        const bool     lightness = m_channel == Channel_Lightness;

        ctx.modify_colors("Brightness/contrast",
                          [curve, to_XYZ, from_XYZ, white, lightness](const float4 &c, int2)
                          {
                              float3 lab = normalize_Lab(XYZ_to_Lab(mul(to_XYZ, c.xyz()), white));

                              if (lightness)
                                  lab.x = curve(lab.x);
                              else
                              {
                                  lab.y = curve(lab.y);
                                  lab.z = curve(lab.z);
                              }

                              return float4{mul(from_XYZ, Lab_to_XYZ(unnormalize_Lab(lab), white)), c.w};
                          });
    }

private:
    //! The slope the contrast slider asks for, as the tangent of an angle.
    /*!
        An angle rather than a multiplier, so that the two ends of the slider are the two extremes there
        are: -1 is a horizontal line and no contrast at all, 0 is the 45-degree diagonal that changes
        nothing, and +1 is vertical, which leaves only black and white.
    */
    static float slope_of(float contrast) { return float(std::tan(lerp(0.0, M_PI_2, contrast / 2.0 + 0.5))); }

    //! Both curves over [0,1], the one in force drawn solid and the other left faint behind it.
    void draw_curve()
    {
        const float slope    = slope_of(m_contrast);
        const float midpoint = (1.f - m_brightness) / 2.f;
        const float bias     = (m_brightness + 1.f) / 2.f;

        constexpr int N = 129;
        float         xs[N], linear[N], curved[N];
        for (int i = 0; i < N; ++i)
        {
            xs[i]     = float(i) / float(N - 1);
            linear[i] = brightness_contrast_linear(xs[i], slope, midpoint);
            curved[i] = brightness_contrast_nonlinear(xs[i], slope, bias);
        }

        // As wide as the sliders beneath it. The widget also holds the tick labels, which are wider down
        // the left side than they are tall along the bottom, so a square widget leaves a plot area that is
        // not square -- the height carries a correction measured from the last frame, which settles after
        // one and then stays put.
        const float width = ImGui::CalcItemWidth();

        if (ImPlot::BeginPlot("##Curve", ImVec2(width, width + m_plot_extra_height),
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
                                  ImPlotFlags_NoBoxSelect))
        {
            const ImPlotAxisFlags axes = ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_Lock;
            ImPlot::SetupAxes(nullptr, nullptr, axes, axes);

            // The same ticks both ways, since the two axes carry the same quantity: a level in and the
            // level it maps to.
            ImPlot::SetupAxisTicks(ImAxis_X1, 0.0, 1.0, 5);
            ImPlot::SetupAxisTicks(ImAxis_Y1, 0.0, 1.0, 5);

            // Fixed, so that the curve moves against the frame instead of the frame following the curve.
            // The straight line leaves the top and bottom at any real contrast, and seeing it leave is the
            // point of drawing it.
            ImPlot::SetupAxesLimits(0.0, 1.0, 0.0, 1.0, ImPlotCond_Always);

            const ImVec4 active{1.f, 1.f, 1.f, 0.85f};
            const ImVec4 faint{1.f, 1.f, 1.f, 0.18f};

            ImPlotSpec spec;
            spec.LineWeight = 2.f;

            spec.LineColor = m_linear ? faint : active;
            ImPlot::PlotLine("s-curve", xs, curved, N, spec);

            spec.LineColor = m_linear ? active : faint;
            ImPlot::PlotLine("line", xs, linear, N, spec);

            // Where the midpoint sits, which is what brightness moves.
            const float mid_x[2] = {midpoint, midpoint};
            const float mid_y[2] = {0.f, 1.f};
            spec.LineColor       = ImVec4(1.f, 1.f, 1.f, 0.25f);
            spec.LineWeight      = 1.f;
            ImPlot::PlotLine("midpoint", mid_x, mid_y, 2, spec);

            // Dragging sets both at once, which finds a look faster than two sliders do: across is the
            // midpoint the curve pivots about, and up is how steep it is there.
            //
            // In the plot's own coordinates rather than the widget's. The widget rectangle includes the
            // frame and the tick labels around the plot, so a fraction taken across it is not the
            // fraction across the axes -- the midpoint line lands beside the cursor and drifts further
            // the nearer the edge it gets.
            if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                m_dragging = true;

            if (m_dragging)
            {
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    m_dragging = false;
                else
                {
                    // Kept live once the drag has started, even past the edge of the plot, so a slide to
                    // an extreme does not stop short of it.
                    const ImPlotPoint m = ImPlot::GetPlotMousePos();
                    m_brightness        = std::clamp(float(lerp(1.0, -1.0, m.x)), -1.f, 1.f);
                    m_contrast          = std::clamp(float(lerp(-1.0, 1.0, m.y)), -1.f, 1.f);
                }
            }

            const ImVec2 area = ImPlot::GetPlotSize();
            if (area.x > 0.f && area.y > 0.f)
                m_plot_extra_height = std::clamp(m_plot_extra_height + (area.x - area.y), 0.f, width);

            ImPlot::EndPlot();
        }
    }

    //! Which of the image's qualities the curve is applied to.
    enum Channel : int
    {
        Channel_RGB = 0,      //!< The three channels alike, saturation moving with everything else
        Channel_Lightness,    //!< L* alone, so the colors stay where they are
        Channel_Chromaticity, //!< a* and b*, so how light the image is does not change

        Channel_COUNT
    };

    float m_brightness = 0.f, m_contrast = 0.f;
    bool  m_linear  = false;
    int   m_channel = Channel_RGB;
    //! A drag that began inside the plot, and goes on following the mouse after it leaves.
    bool m_dragging = false;
    //! What the widget needs above its width for the plot *area* to come out square; see draw_curve().
    float m_plot_extra_height = 0.f;
};

class Flatten final : public EditCommand
{
public:
    Info info() const override
    {
        return {{"Flatten...", "Composite over a background", "Remove transparency"},
                ICON_MY_FLATTEN,
                ImGuiKey_None,
                ImGuiInputFlags_None,
                "Flatten"};
    }

    void draw(EditContext &ctx) override
    {
        ImGui::TextWrapped("Composites the image over a background color, so what was transparent becomes "
                           "that color and the result is opaque.");
        ImGui::Spacing();

        ImGui::ColorEdit4("Background", &m_bg.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar);

        // The color the viewport is already showing behind the image, which is usually the one being
        // matched.
        if (ImGui::Button("Use viewport background"))
            m_bg = ctx.background_color();
        ImGui::Tooltip("Takes the custom background color from the View menu.");
    }

    void apply(EditContext &ctx) override
    {
        const float4 b = m_bg;
        ctx.modify_colors("Flatten",
                          [b](const float4 &c, int2)
                          {
                              // Samples are held premultiplied, so "over" is an addition rather than the
                              // textbook's lerp; the background is given straight and is premultiplied here.
                              return float4{c.xyz() + b.xyz() * b.w * (1.f - c.w), c.w + b.w * (1.f - c.w)};
                          });
    }

private:
    float4 m_bg{0.f, 0.f, 0.f, 1.f};
};

} // namespace

void add_color_commands(std::vector<EditCommandPtr> &out)
{
    out.push_back(std::make_unique<BrightnessContrast>());
    out.push_back(std::make_unique<ConvertColorSpace>());
    out.push_back(std::make_unique<ChannelMixer>());
    out.push_back(std::make_unique<HueSaturation>());
    out.push_back(std::make_unique<Flatten>());
}
