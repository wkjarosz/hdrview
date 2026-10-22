//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// The highlight rolloff: where its ceiling and the content's peak come from, and the GUI choosing it. The curve
// itself is shader::roll_off(), shared with the image shader.

#include "app.h"

#include "colorspace.h"
#include "fonts.h"
#include "image.h"
#include "imageio/hdr_metadata.h"
#include "imgui_ext.h"
#include "shared/pipeline.h"

#include <hello_imgui/hello_imgui.h>
#include <implot.h>
#include <implot_internal.h> // AddTextVertical()

#include <cmath>
#include <optional>

using namespace std;
using namespace HelloImGui;

namespace
{

/// The fraction of samples above the percentile that judges whether the measured maximum is an outlier.
constexpr float k_measured_tail = 1e-3f;

/// How far, in stops, metadata may stray outside the measured [99.9th percentile, maximum] and still be believed.
constexpr float k_metadata_tolerance = 1.f;

/// How far above the 99.9th percentile the measured maximum may lie, as a ratio, before it counts as an outlier.
constexpr float k_outlier_ratio = 2.f;

/// Where a rolloff's ceiling sits, in stops above SDR white, for a display whose ceiling is \p display_ceiling.
float ceiling_of(const HDRViewApp::RolloffParams &p, float display_ceiling)
{
    switch (p.ceiling)
    {
    case HDRViewApp::RolloffCeiling_SDRWhite: return 0.f;
    case HDRViewApp::RolloffCeiling_Custom: return std::max(0.f, p.custom_ceiling);
    default:
    case HDRViewApp::RolloffCeiling_Display: return display_ceiling;
    }
}

/// A line of disabled text set in a little from the widgets above it, with an optional tooltip.
void info_text(const string &text, const string &tooltip = {})
{
    const float indent = EmSize(0.75f);
    ImGui::Indent(indent);
    ImGui::BeginDisabled();
    ImGui::TextUnformatted(text.c_str());
    ImGui::EndDisabled();
    ImGui::Unindent(indent);
    if (!tooltip.empty())
        ImGui::Tooltip(tooltip.c_str());
}

/// The highlight modes, in the order highlight_mode() numbers them.
struct HighlightMode
{ const char *name, *tooltip; };
const HighlightMode k_highlight_modes[] = {
    {"Pass through", "Show highlights as bright as the display allows; anything brighter clips."},
    {"Clip at SDR white", "Show the image as an SDR display would."},
    {"Roll off to SDR white", "Compress highlights smoothly to fit under SDR white, instead of clipping them."},
    {"Roll off to display ceiling",
     "Compress highlights smoothly to fit under the display's headroom, instead of clipping them. "
#if defined(__APPLE__)
     "The headroom follows the display's brightness setting."
#else
     "The headroom is what the display reports."
#endif
    },
    {"Custom rolloff", "Roll off with your own settings. Changing any setting below switches to this."}};

/// A level given in stops relative to SDR white, as the settings show it: "4.0x above SDR white".
string relative_to_white(float stops)
{
    if (std::abs(stops) < 0.05f)
        return "at SDR white";
    return stops > 0.f ? fmt::format("{:.1f}x above SDR white", std::exp2(stops))
                       : fmt::format("{:.2g}x SDR white", std::exp2(stops));
}

/// A peak in the image's own units, before exposure, with what it was taken from.
struct ImagePeak
{
    float  value;
    string note;   ///< Where it came from, briefly
    string detail; ///< The figures behind it
};

/// The peak a file's light-level metadata states, in the image's own units, if it states one.
/**
    MaxCLL is preferred, the mastering display's peak only bounding the content from above. Both are in cd/m²,
    so they are divided by what 1.0 stands for: the file's own white luminance where it gives one, and
    otherwise HDRView's PQ/HLG reference white, since these chunks and boxes accompany PQ and HLG content.
*/
optional<ImagePeak> metadata_peak(const Image &img)
{
    if (!img.metadata.contains("header"))
        return nullopt;
    const json &header = img.metadata["header"];

    float  nits = 0.f;
    string what;
    if (header.contains(k_content_light_level_key))
    {
        nits = header[k_content_light_level_key]["value"].value("MaxCLL", 0.f);
        what = "MaxCLL";
    }
    if (nits <= 0.f && header.contains(k_mastering_display_key))
    {
        nits = header[k_mastering_display_key]["value"].value("max luminance", 0.f);
        what = "mastering peak";
    }
    if (nits <= 0.f)
        return nullopt;

    float  white      = HDR_REFERENCE_WHITE_NITS;
    string white_note = fmt::format("taking 1.0 as {:g} cd/m²", white);
    if (header.contains(k_white_luminance_key))
        if (float w = header[k_white_luminance_key].value("value", 0.f); w > 0.f)
        {
            white      = w;
            white_note = fmt::format("with 1.0 at the file's {:g} cd/m²", w);
        }
    return ImagePeak{nits / white, "from " + what, fmt::format("{} {:g} cd/m², {}", what, nits, white_note)};
}

/// The index of the first channel of \p group shown as color, and how many there are.
int2 color_channels(const ChannelGroup &group, Channels_ shown)
{
    int count;
    switch (group.type)
    {
    case ChannelGroup::RGBA_Channels:
    case ChannelGroup::RGB_Channels:
    case ChannelGroup::XYZA_Channels:
    case ChannelGroup::XYZ_Channels: count = 3; break;
    case ChannelGroup::UVorXY_Channels: count = 2; break;
    // the luminance alone, of the luminance/chroma groups: chroma is not a brightness
    default: count = 1; break;
    }
    count = std::min(count, group.num_channels);

    switch (shown)
    {
    case Channels_Red: return int2{0, std::min(count, 1)};
    case Channels_Green: return count > 1 ? int2{1, 1} : int2{0, 0};
    case Channels_Blue: return count > 2 ? int2{2, 1} : int2{0, 0};
    case Channels_Alpha: return int2{0, 0};
    default: return int2{0, count};
    }
}

} // namespace

namespace
{

// Stable identifiers for the settings' enums in settings and session files, apart from the GUI's labels.
const char *const k_preset_ids[]  = {"sdr_white", "display", "custom"};
const char *const k_ceiling_ids[] = {"sdr_white", "display", "custom"};
const char *const k_peak_ids[]    = {"auto", "metadata", "measured", "custom"};
static_assert(std::size(k_preset_ids) == HDRViewApp::RolloffPreset_COUNT);
static_assert(std::size(k_ceiling_ids) == HDRViewApp::RolloffCeiling_COUNT);
static_assert(std::size(k_peak_ids) == HDRViewApp::PeakSource_COUNT);

template <typename Enum, size_t N>
Enum enum_from_id(const json &j, const char *key, const char *const (&ids)[N], Enum v)
{
    if (j.contains(key) && j[key].is_string())
        for (size_t i = 0; i < N; ++i)
            if (j[key].get<string>() == ids[i])
                return Enum(i);
    return v;
}

} // namespace

json HDRViewApp::RolloffSettings::to_json() const
{
    return {{"enabled", enabled},
            {"preset", k_preset_ids[preset]},
            {"custom",
             {{"ceiling", k_ceiling_ids[custom.ceiling]},
              {"custom_ceiling", custom.custom_ceiling},
              {"peak_source", k_peak_ids[custom.peak_source]},
              {"custom_peak", custom.custom_peak},
              {"preserve_hue", custom.preserve_hue}}}};
}

void HDRViewApp::RolloffSettings::from_json(const json &j)
{
    if (!j.is_object())
        return;
    // a value of the wrong type is ignored like a missing one, rather than throwing
    auto number = [](const json &o, const char *key, float v)
    { return o.contains(key) && o[key].is_number() ? o[key].get<float>() : v; };
    if (j.contains("enabled") && j["enabled"].is_boolean())
        enabled = j["enabled"].get<bool>();
    preset = enum_from_id(j, "preset", k_preset_ids, preset);
    if (!j.contains("custom") || !j["custom"].is_object())
        return;
    const json &c         = j["custom"];
    custom.ceiling        = enum_from_id(c, "ceiling", k_ceiling_ids, custom.ceiling);
    custom.custom_ceiling = std::max(0.f, number(c, "custom_ceiling", custom.custom_ceiling));
    custom.peak_source    = enum_from_id(c, "peak_source", k_peak_ids, custom.peak_source);
    custom.custom_peak    = std::max(0.f, number(c, "custom_peak", custom.custom_peak));
    custom.preserve_hue   = std::clamp(number(c, "preserve_hue", custom.preserve_hue), 0.f, 1.f);
}

HDRViewApp::RenditionHighlights HDRViewApp::view_rendition_highlights() const
{
    if (m_clamp_to_LDR)
        return RenditionHighlights_ClipAtSDRWhite;
    if (!m_rolloff_settings.enabled)
        return RenditionHighlights_PassThrough;
    switch (m_rolloff_settings.preset)
    {
    case RolloffPreset_SDRWhite: return RenditionHighlights_RollOffToSDRWhite;
    case RolloffPreset_Display: return RenditionHighlights_RollOffToTarget;
    default: return RenditionHighlights_Custom;
    }
}

HDRViewApp::Highlights HDRViewApp::highlights_for(RenditionHighlights mode, float target_ceiling) const
{
    Highlights h;
    h.clip = mode == RenditionHighlights_ClipAtSDRWhite ? 1.f
             : mode == RenditionHighlights_ClipAtTarget ? std::exp2(target_ceiling)
                                                        : std::numeric_limits<float>::max();

    // the rolloff modes are the presets, with the target display in place of this one
    RolloffSettings s = m_rolloff_settings;
    s.preset          = mode == RenditionHighlights_RollOffToSDRWhite ? RolloffPreset_SDRWhite
                        : mode == RenditionHighlights_RollOffToTarget ? RolloffPreset_Display
                                                                      : RolloffPreset_Custom;
    const auto p      = s.params();
    Rolloff   &r      = h.rolloff;
    r                 = m_rolloff;
    r.enabled         = mode >= RenditionHighlights_RollOffToSDRWhite;
    r.preserve_hue    = std::clamp(p.preserve_hue, 0.f, 1.f);
    r.display_ceiling = target_ceiling;
    r.ceiling         = ceiling_of(p, target_ceiling);
    // as update_rolloff() keeps it: a custom peak is never below the ceiling
    if (p.peak_source == PeakSource_Custom)
    {
        r.has_peak = true;
        r.peak     = std::max(p.custom_peak, r.ceiling);
    }
    else
    {
        const bool custom = s.preset == RolloffPreset_Custom;
        r.has_peak        = custom ? r.has_custom_peak : r.has_auto_peak;
        r.peak            = custom ? r.custom_peak : r.auto_peak;
    }
    return h;
}

int HDRViewApp::highlight_mode() const
{ return m_clamp_to_LDR ? 1 : !m_rolloff_settings.enabled ? 0 : 2 + int(m_rolloff_settings.preset); }

const char *HDRViewApp::highlight_mode_name() const { return k_highlight_modes[highlight_mode()].name; }

void HDRViewApp::update_rolloff()
{
    const auto p = m_rolloff_settings.params();
    Rolloff    r;
    // clipping takes precedence over a rolloff, which stays chosen underneath for Clamp to LDR to return to
    r.enabled      = m_rolloff_settings.enabled && !m_clamp_to_LDR;
    r.preserve_hue = std::clamp(p.preserve_hue, 0.f, 1.f);

    const float headroom = display_headroom();
    r.display_ceiling    = headroom > 0.f ? std::log2(std::max(1.f, headroom)) : 0.f;
    r.ceiling            = ceiling_of(p, r.display_ceiling);
    if (p.ceiling == RolloffCeiling_Display && !(headroom > 0.f))
        r.ceiling_note = "the display reports no headroom";

    // Stops of display light above SDR white, from a value in the image's own units: the shader's exposure and
    // gamma steps, which the rolloff follows.
    auto display_stops = [this](float v)
    {
        float d = shader::linear_to_gamma(v * std::exp2(m_exposure_live) + m_offset_live, 1.f / m_gamma_live);
        return d > 0.f ? std::log2(d) : -1000.f;
    };

    // the peak is worked out while the settings are on screen too, so they can show what the rolloff would do
    auto img = current_image();
    if ((r.enabled || m_highlight_settings_shown) && img)
    {
        // The measured 99.9th percentile and maximum over the channels shown, once their statistics are in.
        // Only asked for when needed, since it can start computing them.
        auto measured = [&]() -> optional<float2>
        {
            auto      &group = img->groups[img->selected_group];
            const int2 range = color_channels(group, m_channel);
            if (range.y == 0)
                return nullopt;
            float2 peak{-INFINITY, -INFINITY};
            for (int c = range.x; c < range.x + range.y; ++c)
            {
                auto &channel = img->channels[group.channels[c]];
                channel.update_stats(c, img, reference_image());
                const PixelStats *stats = channel.get_stats();
                if (!stats || !stats->computed || !(stats->summary.valid_pixels > 0))
                    return nullopt;
                peak = la::max(peak, float2{stats->value_exceeded_by(k_measured_tail), stats->summary.maximum});
            }
            return peak;
        };

        // BT.2390 flattens everything above the peak onto the ceiling, so the measured peak is the true maximum,
        // unless that lies so far above the 99.9th percentile that a few stray pixels must have set it; it is then
        // a stop above the percentile, which leaves the strays to clip.
        auto measured_peak   = [](float2 m) { return std::min(m.y, k_outlier_ratio * m.x); };
        auto measured_detail = [](float2 m)
        {
            return m.y <= k_outlier_ratio * m.x
                       ? string()
                       : string("The maximum is an outlier, so the peak is a stop above the 99.9th percentile.");
        };

        // light-level metadata describes one file, not a comparison against another
        const bool comparing = reference_image() != nullptr;
        const auto meta      = comparing ? nullopt : metadata_peak(*img);

        struct Found
        {
            bool   has  = false;
            float  peak = 0.f; ///< In stops above SDR white
            string note, detail;
        };
        auto found = [&](float value, string note, string detail = {})
        { return Found{true, display_stops(value), std::move(note), std::move(detail)}; };
        // the peak a source finds in this image; Custom finds none of its own
        auto find = [&](PeakSource source) -> Found
        {
            switch (source)
            {
            case PeakSource_Metadata:
                if (meta)
                    return found(meta->value, meta->note, meta->detail);
                return Found{false, 0.f, comparing ? "none: no metadata for a comparison" : "none: no metadata"};
            case PeakSource_Measured:
                if (auto m = measured())
                    return found(measured_peak(*m), "measured", measured_detail(*m));
                return Found{false, 0.f, "measuring..."};
            case PeakSource_Auto:
            {
                // Metadata states a maximum, or near one, so it is believed anywhere from a stop below the
                // measured 99.9th percentile to a stop above the measured maximum; outside that, it is wrong
                // about this file.
                auto m = measured();
                if (meta && !m)
                    return found(meta->value, meta->note + ", unchecked", meta->detail);
                if (!m)
                    return Found{false, 0.f, "measuring..."};
                if (!meta)
                    return found(measured_peak(*m), "measured", measured_detail(*m));
                const float below = std::log2(m->x / meta->value), above = std::log2(meta->value / m->y);
                if (!(below > k_metadata_tolerance) && !(above > k_metadata_tolerance))
                    return found(meta->value, meta->note, meta->detail);
                return found(measured_peak(*m), "measured; metadata ignored",
                             fmt::format("{}: {:.1f} stops {} the pixels. {}", meta->detail,
                                         below > 0.f ? below : above, below > 0.f ? "below" : "above",
                                         measured_detail(*m)));
            }
            default: return Found{};
            }
        };

        // The fixed presets take Auto's peak and Custom its own source's, both worked out whichever is chosen, for
        // the settings' plot and an export's highlights.
        const Found automatic = find(PeakSource_Auto);
        r.has_auto_peak       = automatic.has;
        r.auto_peak           = automatic.peak;

        // A custom peak below the ceiling would ask for no rolloff, only a clip; the settings keep it at or above
        // the ceiling, and this keeps it there when the display's headroom moves the ceiling up past it.
        const auto &c      = m_rolloff_settings.custom;
        const Found custom = c.peak_source == PeakSource_Custom
                                 ? Found{true, std::max(c.custom_peak, ceiling_of(c, r.display_ceiling)), "custom"}
                             : c.peak_source == PeakSource_Auto ? automatic
                                                                : find(c.peak_source);
        r.has_custom_peak  = custom.has;
        r.custom_peak      = custom.peak;

        const Found &chosen = m_rolloff_settings.preset == RolloffPreset_Custom ? custom : automatic;
        r.has_peak          = chosen.has;
        r.peak              = chosen.peak;
        r.peak_note         = chosen.note;
        r.peak_detail       = chosen.detail;
    }

    m_rolloff = std::move(r);
}

void HDRViewApp::draw_highlight_settings(bool in_panel)
{
    auto &s = m_rolloff_settings;
    // Read live rather than copied: a change made in this panel is worked out at once with update_rolloff(), so
    // the plot drawn below it the same frame already shows it. The peak is worked out while this is shown.
    const auto &r              = m_rolloff;
    m_highlight_settings_shown = true;

    // The popup's width is fixed, so notes cannot stretch it, and the panel's is its own; the widgets leave room
    // on their right for the longest label and no more.
    const float width     = in_panel ? std::max(EmSize(18), ImGui::GetContentRegionAvail().x) : EmSize(24);
    const float inner     = ImGui::GetStyle().ItemInnerSpacing.x;
    const float row_width = width - ImGui::CalcTextSize("Preserve hue").x - inner;
    ImGui::PushTextWrapPos(width);

    // one choice over the clamp and the rolloff's presets
    // outside the gamma tonemap, which the toolbar's button keeps to, the panel stays but has nothing to do
    if (in_panel && m_tonemap != Tonemap_Gamma)
        info_text("Highlights apply only to the gamma tonemap.");
    ImGui::BeginDisabled(in_panel && m_tonemap != Tonemap_Gamma);

    // In the popup, a button in the top-right corner that turns it into the panel, open until closed: flat and
    // text-line high, like a window's title-bar buttons. Square, and wide enough for the glyph, which is wider
    // than the font size it is drawn at, as the Annotations panel sizes its row buttons.
    if (!in_panel && m_highlights_window)
    {
        const float side = std::max(ImGui::GetTextLineHeight(), ImGui::CalcTextSize(ICON_MY_POP_OUT).x);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - side);
        // no frame padding, which a square this small has no room for
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
        const bool pop_out = ImGui::FlatButton(ICON_MY_POP_OUT "##Pop out", false, ImVec2(side, side));
        ImGui::PopStyleVar();
        if (pop_out)
        {
            m_highlights_window->isVisible              = true;
            m_highlights_window->focusWindowAtNextFrame = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Tooltip("Open these settings as a panel, which can be docked. Closing the panel brings back the "
                       "popup.");
    }

    const int mode = highlight_mode();
    ImGui::SetNextItemWidth(row_width);
    if (ImGui::BeginCombo("Highlights", k_highlight_modes[mode].name))
    {
        for (int i = 0; i < IM_ARRAYSIZE(k_highlight_modes); ++i)
        {
            ImGui::BeginDisabled(i == 1 && !supports_hdr());
            // Clipping leaves the rolloff as it was, so Clamp to LDR, which toggles only the clip, returns to it
            if (ImGui::Selectable(k_highlight_modes[i].name, mode == i))
            {
                m_clamp_to_LDR = i == 1;
                if (i == 0)
                    s.enabled = false;
                else if (i >= 2)
                {
                    s.enabled = true;
                    s.preset  = RolloffPreset(i - 2);
                }
                update_rolloff();
            }
            ImGui::EndDisabled();
            ImGui::Tooltip(k_highlight_modes[i].tooltip);
            if (mode == i)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    const bool rolling = !m_clamp_to_LDR && s.enabled;

    // The settings below show the preset in force, and editing them while a fixed preset is chosen carries its
    // values over into Custom with the change applied.
    auto p      = s.params();
    bool edited = false;

    // The ceiling and the peak each get a source and a slider that always shows the value in force, so nothing
    // comes or goes as the choices change; moving a slider makes its value a custom one.
    const float source_width = EmSize(7.5f), value_width = row_width - source_width - inner;
    auto value_row = [&](const char *label, const char *const sources[], int num_sources, int &source, int custom,
                         float shown, float lo, float hi, float &custom_value, const char *tooltip)
    {
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(source_width);
        if (ImGui::Combo("##source", &source, sources, num_sources))
            edited = true;
        ImGui::Tooltip(tooltip);
        ImGui::SameLine(0.f, inner);
        ImGui::SetNextItemWidth(value_width);
        float v = shown;
        if (ImGui::SliderFloat(label, &v, lo, hi, "%.1f stops", ImGuiSliderFlags_AlwaysClamp))
        {
            source       = custom;
            custom_value = v;
            edited       = true;
        }
        ImGui::Tooltip("Above SDR white. Dragging makes it a custom value.");
        ImGui::PopID();
    };

    ImGui::BeginDisabled(!rolling);
    ImGui::SeparatorText("Rolloff");

    // The range in stops the sliders and the plot share. It depends on the display and the automatic peak, not on
    // the choices, so modes can be compared and nothing rescales under the mouse.
    const float top          = std::max({8.f, r.display_ceiling + 2.f, r.has_auto_peak ? r.auto_peak + 2.f : 0.f});
    const float auto_knee_lo = shader::rolloff_knee(0.f, r.has_auto_peak, r.auto_peak);
    const float bottom       = std::min(-2.f, auto_knee_lo < 1e30f ? auto_knee_lo : 0.f) - 1.f;

    static const char *ceilings[] = {"SDR white", "Display", "Custom"};
    int                ceiling    = p.ceiling;
    value_row("Ceiling", ceilings, IM_ARRAYSIZE(ceilings), ceiling, RolloffCeiling_Custom, r.ceiling, 0.f, top,
              p.custom_ceiling, "How bright highlights may get. Display: the display's headroom.");
    p.ceiling = (RolloffCeiling)ceiling;
    info_text(relative_to_white(r.ceiling) + (r.ceiling_note.empty() ? "" : ", " + r.ceiling_note));

    // where the settings as edited so far put the ceiling, below which a custom peak may not be dragged
    auto edited_ceiling = [&](const RolloffParams &q) { return ceiling_of(q, r.display_ceiling); };

    // A custom peak set from a knee: the two are tied through the ceiling, the peak sitting twice as far above it as
    // the knee sits below it, so either one fixes the curve.
    auto set_custom_peak = [&](float peak)
    {
        p.peak_source = PeakSource_Custom;
        p.custom_peak = std::clamp(peak, edited_ceiling(p), top);
        edited        = true;
    };
    auto peak_from_knee = [&](float knee)
    {
        const float c = edited_ceiling(p);
        return 3.f * c - 2.f * std::min(knee, c);
    };

    // The peak's source, then the knee and the peak in one pair, since moving either moves the other. With nothing
    // to roll off, both sit at the ceiling, from where dragging the knee down starts a rolloff.
    static const char *peaks[] = {"Auto", "Metadata", "Measured", "Custom"};
    int                peak    = p.peak_source;
    ImGui::PushID("Peak");
    ImGui::SetNextItemWidth(source_width);
    if (ImGui::Combo("##source", &peak, peaks, IM_ARRAYSIZE(peaks)))
    {
        p.peak_source = (PeakSource)peak;
        edited        = true;
    }
    ImGui::Tooltip("Where the peak comes from.\n\n"
                   "Auto: the file's metadata if it matches the pixels, else measured.\n"
                   "Metadata: MaxCLL, else the mastering display's peak.\n"
                   "Measured: the brightest pixels, ignoring outliers.");
    ImGui::SameLine(0.f, inner);
    const bool  rolls       = r.has_peak && r.peak > r.ceiling;
    const float knee        = shader::rolloff_knee(r.ceiling, r.has_peak, r.peak);
    float       knee_peak[] = {rolls ? knee : r.ceiling, r.has_peak ? std::max(r.peak, r.ceiling) : r.ceiling};
    const float shown_knee  = knee_peak[0];
    ImGui::SetNextItemWidth(value_width);
    if (ImGui::SliderFloat2("Knee, peak", knee_peak, bottom, top, "%.1f stops", ImGuiSliderFlags_AlwaysClamp))
        set_custom_peak(knee_peak[0] != shown_knee ? peak_from_knee(knee_peak[0]) : knee_peak[1]);
    ImGui::Tooltip("Above SDR white. Knee: where compression starts. Peak: what lands on the ceiling; anything "
                   "brighter clips. Moving either makes the peak a custom one.");
    ImGui::PopID();
    info_text(!r.has_peak ? r.peak_note
              : rolls ? fmt::format("knee {:.2g}x, peak {:.3g}x SDR white; {}", std::exp2(knee), std::exp2(r.peak),
                                    r.peak_note)
                      : fmt::format("peak {:.3g}x SDR white, under the ceiling; {}", std::exp2(r.peak), r.peak_note),
              r.peak_detail);

    float percent = 100.f * p.preserve_hue;
    ImGui::SetNextItemWidth(row_width);
    if (ImGui::SliderFloat("Preserve hue", &percent, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
    {
        p.preserve_hue = percent / 100.f;
        edited         = true;
    }
    ImGui::Tooltip("How much compressed highlights keep their hue and saturation. At 0%, each channel is "
                   "compressed separately, so bright colors shift toward white.");

    ImGui::EndDisabled();

    // Takes edits into Custom and works them out at once. A custom peak below the ceiling is kept as set, and
    // update_rolloff() lifts it to the ceiling, so dragging the ceiling up and back down leaves it where it was.
    auto commit = [&]
    {
        if (!edited)
            return;
        s.custom = p;
        s.preset = RolloffPreset_Custom;
        update_rolloff();
        edited = false;
    };
    commit();

    // What the chosen mode does to values above SDR white, as a curve from stops in to stops shown, display
    // clipping included: it stays live in every mode, and for a rolloff mode not chosen it shows what that mode
    // would do.
    const float limit = m_clamp_to_LDR ? 0.f : r.display_ceiling; //< where the display, or the clamp, cuts off
    auto        shown = [&](float x)
    { return std::min(rolling ? shader::rolloff_stops(x, r.ceiling, r.has_peak, r.peak) : x, limit); };

    const float   x_min = bottom, x_max = top;
    constexpr int n = 256;
    float         xs[n], ys[n];
    for (int i = 0; i < n; ++i)
    {
        xs[i] = x_min + (x_max - x_min) * i / (n - 1);
        ys[i] = shown(xs[i]);
    }

    // square: the axes' labels take about as much room below the plot area as beside it, so its area is square too
    const float  plot_width = std::min(width, EmSize(30));
    const ImVec2 plot_size{plot_width, plot_width};
    if (ImPlot::BeginPlot("##Highlight curve", plot_size, ImPlotFlags_NoLegend))
    {
        ImPlot::SetupAxes("stops in", "stops shown");
        ImPlot::SetupAxesLimits(x_min, x_max, x_min, x_max, ImPlotCond_Always);

        ImPlotSpec identity;
        identity.LineColor      = ImVec4(0.5f, 0.5f, 0.5f, 0.6f);
        const float diagonal[2] = {x_min, x_max};
        ImPlot::PlotLine("identity", diagonal, diagonal, 2, identity);

        const ImVec4 ceiling_color{0.9f, 0.3f, 0.3f, 0.8f}, peak_color{1.f, 0.8f, 0.2f, 0.8f},
            knee_color{0.5f, 0.7f, 1.f, 0.8f};
        // In a rolloff mode the ceiling, the knee and the peak are handles, which do what their sliders do;
        // otherwise the ceiling and peak only mark where the mode clips and where the content ends.
        ImPlotSpec ceiling_spec, peak_spec;
        ceiling_spec.LineColor = ceiling_color;
        peak_spec.LineColor    = peak_color;
        if (rolling)
        {
            double ceiling_y = r.ceiling;
            // drawn a frame late, so it shows where the clamp leaves it rather than where the mouse went
            if (ImPlot::DragLineY(0, &ceiling_y, ceiling_color, 2.f, ImPlotDragToolFlags_Delayed))
            {
                ceiling_y        = std::clamp(ceiling_y, 0.0, double(x_max));
                p.ceiling        = RolloffCeiling_Custom;
                p.custom_ceiling = float(ceiling_y);
                edited           = true;
            }
            double knee_x = shown_knee;
            if (ImPlot::DragLineX(2, &knee_x, knee_color, 2.f, ImPlotDragToolFlags_Delayed))
            {
                knee_x = std::clamp(knee_x, double(x_min), double(edited_ceiling(p)));
                set_custom_peak(peak_from_knee(float(knee_x)));
            }
            double peak_x = r.has_peak ? r.peak : r.ceiling;
            if (ImPlot::DragLineX(1, &peak_x, peak_color, 2.f, ImPlotDragToolFlags_Delayed))
            {
                peak_x = std::clamp(peak_x, double(edited_ceiling(p)), double(x_max));
                set_custom_peak(float(peak_x));
            }
        }
        else
        {
            ceiling_spec.Flags = ImPlotInfLinesFlags_Horizontal;
            ImPlot::PlotInfLines("ceiling", &limit, 1, ceiling_spec);
            if (r.has_peak)
                ImPlot::PlotInfLines("peak", &r.peak, 1, peak_spec);
        }

        ImPlotSpec curve;
        curve.LineWeight = 2.f;
        curve.LineColor  = ImVec4(1.f, 1.f, 1.f, 1.f);
        ImPlot::PlotLine("shown", xs, ys, n, curve);

        // Names along the ceiling and peak lines, starting just past the thicker ends their handles draw: the
        // ceiling's above its line at the left, or below where the line runs too close to the top; the peak's
        // rotated along its line at the bottom, to its right, or to its left where the line runs too close to the
        // right edge.
        {
            ImDrawList  *draw_list = ImPlot::GetPlotDrawList();
            const ImVec2 area_min = ImPlot::GetPlotPos(), area_size = ImPlot::GetPlotSize();
            const ImVec2 area_max{area_min.x + area_size.x, area_min.y + area_size.y};
            const float  pad    = EmSize(0.25f);
            auto         opaque = [](ImVec4 c) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 1.f)); };
            ImPlot::PushPlotClipRect();

            const float  ceiling_px = ImPlot::PlotToPixels(0.0, rolling ? r.ceiling : limit).y;
            const ImVec2 ceiling_sz = ImGui::CalcTextSize("ceiling");
            float        ceiling_y  = ceiling_px - pad - ceiling_sz.y;
            if (ceiling_y < area_min.y)
                ceiling_y = ceiling_px + pad;
            const ImVec2 handle = ImPlot::GetStyle().MajorTickLen;
            draw_list->AddText(ImVec2(area_min.x + handle.y + pad, ceiling_y), opaque(ceiling_color), "ceiling");

            // the knee's, likewise along its line but to its left, away from the peak's, unless too close to the
            // left edge
            if (rolling)
            {
                const float knee_px = ImPlot::PlotToPixels(shown_knee, 0.0).x;
                const float height  = ImGui::GetFontSize();
                float       knee_x  = knee_px - pad - height;
                if (knee_x < area_min.x)
                    knee_x = knee_px + pad;
                ImPlot::AddTextVertical(draw_list, ImVec2(knee_x, area_max.y - handle.x - pad), opaque(knee_color),
                                        "knee");
            }
            if (r.has_peak || rolling)
            {
                const float peak_px = ImPlot::PlotToPixels(r.has_peak ? r.peak : r.ceiling, 0.0).x;
                const float height  = ImGui::GetFontSize();
                float       peak_x  = peak_px + pad;
                if (peak_x + height > area_max.x)
                    peak_x = peak_px - pad - height;
                ImPlot::AddTextVertical(draw_list, ImVec2(peak_x, area_max.y - handle.x - pad), opaque(peak_color),
                                        "peak");
            }
            ImPlot::PopPlotClipRect();
        }
        ImPlot::EndPlot();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndDisabled();

    commit();
}
