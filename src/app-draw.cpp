#include "app.h"

#include "imgui_internal.h"

#include "colormap.h"
#include "common.h"
#include "fonts.h"
#include "image.h"
#include "log_throttle.h"

#include <random>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

using namespace std;

void HDRViewApp::draw_pixel_grid() const
{
    if (!current_image())
        return;

    static const int s_grid_threshold = 10;

    if (!m_draw_grid || (s_grid_threshold == -1) || (m_zoom <= s_grid_threshold))
        return;

    float factor = clamp((m_zoom - s_grid_threshold) / (2 * s_grid_threshold), 0.f, 1.f);
    float alpha  = lerp(0.0f, 1.0f, smoothstep(0.0f, 1.0f, factor));

    if (alpha <= 0.0f)
        return;

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    ImColor col_fg(1.0f, 1.0f, 1.0f, alpha);
    ImColor col_bg(0.2f, 0.2f, 0.2f, alpha);

    auto bounds =
        Box2i{int2(pixel_at_vp_pos({0.f, 0.f})), int2(pixel_at_vp_pos(viewport_size()))}.make_valid().expand(1);

    // draw vertical lines
    for (int x = bounds.min.x; x <= bounds.max.x; ++x)
        draw_list->AddLine(app_pos_at_pixel(float2((float)x, (float)bounds.min.y)),
                           app_pos_at_pixel(float2((float)x, (float)bounds.max.y)), col_bg, 4.f);

    // draw horizontal lines
    for (int y = bounds.min.y; y <= bounds.max.y; ++y)
        draw_list->AddLine(app_pos_at_pixel(float2((float)bounds.min.x, (float)y)),
                           app_pos_at_pixel(float2((float)bounds.max.x, (float)y)), col_bg, 4.f);

    // and now again with the foreground color
    for (int x = bounds.min.x; x <= bounds.max.x; ++x)
        draw_list->AddLine(app_pos_at_pixel(float2((float)x, (float)bounds.min.y)),
                           app_pos_at_pixel(float2((float)x, (float)bounds.max.y)), col_fg, 2.f);
    for (int y = bounds.min.y; y <= bounds.max.y; ++y)
        draw_list->AddLine(app_pos_at_pixel(float2((float)bounds.min.x, (float)y)),
                           app_pos_at_pixel(float2((float)bounds.max.x, (float)y)), col_fg, 2.f);
}

void HDRViewApp::draw_pixel_info() const
{
    auto img = current_image();
    if (!img || !m_draw_pixel_info)
        return;

    // These numbers report the same pixel the status bar does, values and labels alike; see
    // m_status_pixel_target. The composite has no channel layout of its own, so it borrows the current
    // image's.
    const bool from_reference = m_status_pixel_target == 1;
    auto       src            = from_reference ? reference_image() : img;
    if (!src)
        return; //< the reference was chosen and has since been closed; nothing to report

    static constexpr float2 align = {0.5f, 0.5f};

    auto  &group = src->groups[src->active_group_index(from_reference ? Target_Secondary : Target_Primary)];
    string names[4];
    string longest_name;
    for (int c = 0; c < group.num_channels; ++c)
    {
        auto &channel = src->channels[group.channels[c]];
        names[c]      = Channel::tail(channel.name);
        if (names[c].length() > longest_name.length())
            longest_name = names[c];
    }

    ImGui::PushFont(m_mono_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);
    static float line_height = ImGui::CalcTextSize("").y;
    const float2 channel_threshold2 =
        float2{ImGui::CalcTextSize((longest_name + ": 31.00000").c_str()).x, group.num_channels * line_height};
    const float2 coord_threshold2  = channel_threshold2 + float2{0.f, 2.f * line_height};
    const float  channel_threshold = maxelem(channel_threshold2);
    const float  coord_threshold   = maxelem(coord_threshold2);
    ImGui::PopFont();

    if (m_zoom <= channel_threshold)
        return;

    // fade value for the channel values shown at sufficient zoom
    float factor = clamp((m_zoom - channel_threshold) / (1.25f * channel_threshold), 0.f, 1.f);
    float alpha  = smoothstep(0.0f, 1.0f, factor);

    if (alpha <= 0.0f)
        return;

    // fade value for the (x,y) coordinates shown at further zoom
    float factor2 = clamp((m_zoom - coord_threshold) / (1.25f * coord_threshold), 0.f, 1.f);
    float alpha2  = smoothstep(0.0f, 1.0f, factor2);

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    ImGui::PushFont(m_mono_bold, ImGui::GetStyle().FontSizeBase * 16.f / 14.f);

    auto bounds =
        Box2i{int2(pixel_at_vp_pos({0.f, 0.f})), int2(pixel_at_vp_pos(viewport_size()))}.make_valid().expand(1);

    for (int y = bounds.min.y; y < bounds.max.y; ++y)
    {
        for (int x = bounds.min.x; x < bounds.max.x; ++x)
        {
            auto   pos     = app_pos_at_pixel(float2(x + 0.5f, y + 0.5f));
            float4 r_pixel = pixel_value({x, y}, true, m_status_pixel_target);
            float4 t_pixel = linear_to_sRGB(pixel_value({x, y}, false, m_status_pixel_target));
            float4 pixel   = m_status_color_mode == 0 ? r_pixel : t_pixel;
            // Legibility is against what is on screen, which is the composite whatever the numbers show.
            float4 displayed  = m_status_pixel_target == 2 ? t_pixel : linear_to_sRGB(pixel_value({x, y}, false, 2));
            float3 text_color = contrasting_color(displayed.xyz());
            float3 shadow     = contrasting_color(text_color);
            if (alpha2 > 0.f)
            {
                float2 c_pos = pos + float2{0.f, (-1 - 0.5f * (group.num_channels - 1)) * line_height};
                auto   text  = fmt::format("({},{})", x, y);
                ImGui::AddTextAligned(draw_list, c_pos + 1.f, ImColor(float4{shadow, alpha2}), text, align);
                ImGui::AddTextAligned(draw_list, c_pos, ImColor(float4{text_color, alpha2}), text, align);
            }

            for (int c = 0; c < group.num_channels; ++c)
            {
                float2 c_pos = pos + float2{0.f, (c - 0.5f * (group.num_channels - 1)) * line_height};
                auto   text  = fmt::format("{:>2s}:{: > 9.5f}", names[c], pixel[c]);
                ImGui::AddTextAligned(draw_list, c_pos + 1.f, ImColor(float4{shadow, alpha2}), text, align);
                ImGui::AddTextAligned(draw_list, c_pos, ImColor{float4{text_color, alpha2}}, text, align);
            }
        }
    }
    ImGui::PopFont();
}

void HDRViewApp::draw_image_border() const
{
    auto draw_list = ImGui::GetBackgroundDrawList();

    auto cimg = current_image();
    auto rimg = reference_image();

    if (!cimg && !rimg)
        return;

    if (cimg && cimg->data_window.has_volume())
    {
        auto data_window =
            Box2f{app_pos_at_pixel(float2{cimg->data_window.min}), app_pos_at_pixel(float2{cimg->data_window.max})}
                .make_valid();
        auto display_window = Box2f{app_pos_at_pixel(float2{cimg->display_window.min}),
                                    app_pos_at_pixel(float2{cimg->display_window.max})}
                                  .make_valid();
        bool non_trivial = cimg->data_window != cimg->display_window || cimg->data_window.min != int2{0, 0};
        ImGui::PushRowColors(true, false);
        if (m_draw_data_window)
            ImGui::DrawLabeledRect(draw_list, data_window, ImGui::GetColorU32(ImGuiCol_HeaderActive), "Data window",
                                   {0.f, 0.f}, non_trivial);
        if (m_draw_display_window && non_trivial)
            ImGui::DrawLabeledRect(draw_list, display_window, ImGui::GetColorU32(ImGuiCol_Header), "Display window",
                                   {1.f, 1.f}, true);
        ImGui::PopStyleColor(3);
    }

    if (rimg && rimg->data_window.has_volume())
    {
        auto data_window =
            Box2f{app_pos_at_pixel(float2{rimg->data_window.min}), app_pos_at_pixel(float2{rimg->data_window.max})}
                .make_valid();
        auto display_window = Box2f{app_pos_at_pixel(float2{rimg->display_window.min}),
                                    app_pos_at_pixel(float2{rimg->display_window.max})}
                                  .make_valid();
        ImGui::PushRowColors(false, true, true);
        if (m_draw_data_window)
            ImGui::DrawLabeledRect(draw_list, data_window, ImGui::GetColorU32(ImGuiCol_HeaderActive),
                                   "Reference data window", {1.f, 0.f}, true);
        if (m_draw_display_window)
            ImGui::DrawLabeledRect(draw_list, display_window, ImGui::GetColorU32(ImGuiCol_Header),
                                   "Reference display window", {0.f, 1.f}, true);
        ImGui::PopStyleColor(3);
    }

    if (m_roi_live.has_volume())
    {
        Box2f crop_window{app_pos_at_pixel(float2{m_roi_live.min}), app_pos_at_pixel(float2{m_roi_live.max})};
        ImGui::DrawLabeledRect(draw_list, crop_window, ImGui::ColorConvertFloat4ToU32(float4{float3{0.5f}, 1.f}),
                               "Selection", {0.5f, 1.f}, true);
    }
}

VgTransform HDRViewApp::viewport_transform() const
{
    VgTransform xform;
    xform.to_screen = [this](float2 p) { return app_pos_at_pixel(p); };
    // Screen pixels per image pixel, read off the transform itself; abs() because a flip mirrors the
    // mapping but must not give a stroke a negative width.
    xform.scale        = std::abs(app_pos_at_pixel(float2{1.f, 0.f}).x - app_pos_at_pixel(float2{0.f, 0.f}).x);
    xform.default_font = font("sans regular");
    xform.font_for     = [this](const std::string &face) -> void *
    {
        // NanoVG face names, as tev's own overlay defaults use them, mapped onto the fonts HDRView loads.
        if (face == "sans-bold" || face == "bold")
            return font("sans bold");
        if (face == "mono" || face == "monospace")
            return font("mono regular");
        if (face == "mono-bold")
            return font("mono bold");
        return font("sans regular");
    };
    // By value: the transform is returned, so a reference to it would be gone by the time this is called.
    xform.measure_text = [font_for = xform.font_for, fallback = xform.default_font](const std::string &face, float size,
                                                                                    const std::string &text) -> float2
    {
        auto *f = (ImFont *)(font_for ? font_for(face) : nullptr);
        if (!f)
            f = (ImFont *)fallback;
        if (!f)
            return float2{0.f};

        // Measured at the size the glyphs are baked at, for the same reason they are drawn there: asking
        // for an arbitrary size here would bake one. Measuring is linear in the size, so scaling what comes
        // back gives the same answer.
        const float baked = text_baked_size(size);
        return float2{f->CalcTextSizeA(baked, FLT_MAX, 0.f, text.c_str())} * (std::max(1.f, size) / baked);
    };
    return xform;
}

void HDRViewApp::draw_vector_overlays() const
{
    // The current image and, when one is set, the reference: a renderer can annotate either, so they get
    // different default colors, matching tev's.
    const std::pair<ConstImagePtr, ImU32> targets[] = {{current_image(), IM_COL32(255, 255, 255, 200)},
                                                       {reference_image(), IM_COL32(255, 128, 0, 200)}};

    const VgTransform xform = viewport_transform();

    auto unsupported = [](const char *what)
    {
        // Throttled: an overlay is redrawn every frame, so an unsupported command in it would report
        // itself at the frame rate.
        if (static LogThrottle throttle{std::chrono::seconds(10)}; throttle)
            spdlog::warn("Vector overlay uses {}, which HDRView does not draw.", what);
    };

    for (const auto &[img, color] : targets)
    {
        if (!img)
            continue;

        if (img->vector_overlay_visible && !img->vector_overlay.empty())
            draw_vector_overlay(ImGui::GetBackgroundDrawList(), img->vector_overlay, xform, color, unsupported);

        // Flattened each frame: an annotation being dragged changes every frame anyway, and the command
        // list is small.
        if (m_draw_annotations && !img->annotations.empty())
            draw_vector_overlay(ImGui::GetBackgroundDrawList(), to_vg_commands(img->annotations, xform.scale), xform,
                                color, unsupported);
    }
}

void HDRViewApp::draw_text_editing() const
{
    const int active = active_annotation();
    if (active < 0 || !m_draw_annotations)
        return;

    const Annotation &a = current_image()->annotations[size_t(active)];
    if (a.shape != Annotation::Shape::Text)
        return;

    const auto xform = viewport_transform();

    // The same box the hit test uses, so what is boxed is what can be clicked.
    float2 lo, hi;
    if (!text_screen_box(a, xform, lo, hi))
        return;

    auto *draw_list = ImGui::GetBackgroundDrawList();

    // Boxed, so a selected string reads as selected even where its corners are not yet in reach.
    draw_list->AddRect(lo - 2.f, hi + 2.f, ImGui::GetColorU32(ImGuiCol_Border));

    // Dear ImGui draws the caret and the selection inside InputTextEx, which cannot be called from out
    // here; the state behind it can be read, so the same marks are drawn from it.
    ImGuiInputTextState *state = ImGui::GetInputTextState(m_annotation_rename_id);
    if (!state || m_annotation_renaming != active)
        return;

    // How far into the string a byte offset is, measured the way the box was. One line, as the overlay's
    // Text command is.
    const float line_h = hi.y - lo.y;
    auto        place  = [&](int offset)
    {
        const int n = std::clamp(offset, 0, int(a.text.size()));
        return xform.measure_text(a.font_face, a.font_size, a.text.substr(0, size_t(n))).x * xform.scale;
    };

    if (state->HasSelection())
    {
        const float from = place(std::min(state->GetSelectionStart(), state->GetSelectionEnd()));
        const float to   = place(std::max(state->GetSelectionStart(), state->GetSelectionEnd()));
        draw_list->AddRectFilled(lo + float2{from, 0.f}, lo + float2{to, line_h},
                                 ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
    }

    // Blinking as Dear ImGui's own does, so it reads as the same caret.
    if (std::fmod(state->CursorAnim, 1.20f) <= 0.80f)
    {
        const float x = place(state->GetCursorPos());
        draw_list->AddLine(lo + float2{x, 0.f}, lo + float2{x, line_h}, ImGui::GetColorU32(ImGuiCol_Text));
    }
}

void HDRViewApp::draw_tool_decorations() const
{
    if (!current_image())
        return;

    auto draw_list = ImGui::GetBackgroundDrawList();

    if (m_draw_watched_pixels)
    {
        ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase);
        for (int i = 0; i < (int)m_watched_pixels.size(); ++i)
            ImGui::DrawCrosshairs(draw_list, app_pos_at_pixel(m_watched_pixels[i].pixel + 0.5f),
                                  fmt::format(" {}", i + 1));
        ImGui::PopFont();
    }

    ImGui::PushFont(m_sans_bold, ImGui::GetStyle().FontSizeBase * 18.f / 14.f);

    // Before the handles, so the corners of a text annotation's box sit on top of the box itself.
    draw_text_editing();

    // The handles of the annotation being worked on, drawn over everything so they can be picked up even
    // where the shape runs under another. White on black reads over any image. Only under the annotate
    // tool, since that is the only place a click does anything with them.
    // Not while one is being drawn: the shape under the cursor already says what is happening, and a box
    // around a scribble is a shape the scribble is not.
    if (const int active = active_annotation(); active >= 0 && m_draw_annotations &&
                                                m_mouse_mode == MouseMode_Annotate &&
                                                m_annotation_drag != AnnotationDrag::Creating)
    {
        const auto  xform  = viewport_transform();
        const float radius = 0.3f * HelloImGui::EmSize();
        const auto &a      = current_image()->annotations[size_t(active)];

        // The one a press would take hold of, lit so the handle says so before it is pressed; during a
        // resize it is whichever the drag is already holding.
        const int hot = m_annotation_drag == AnnotationDrag::Resizing
                            ? m_annotation_drag_handle
                            : handle_at(a, ImGui::GetIO().MousePos, xform, radius);

        float2    handles[Annotation::MaxHandles];
        const int count = annotation_handles(a, handles, &xform);
        for (int i = 0; i < count; ++i)
        {
            const float2 at = xform.to_screen(handles[i]);
            const float  r  = i == hot ? radius * 1.35f : radius;
            draw_list->AddRectFilled(at - r, at + r,
                                     i == hot ? ImGui::GetColorU32(ImGuiCol_ButtonActive) : IM_COL32_WHITE);
            draw_list->AddRect(at - r, at + r, IM_COL32_BLACK);
        }
    }

    float2 pos = ImGui::GetIO().MousePos;
    if (m_mouse_mode == MouseMode_RectangularSelection)
    {
        // draw selection indicator
        ImGui::AddTextAligned(draw_list, pos + int2{18} + int2{1, 1}, IM_COL32_BLACK, ICON_MY_SELECT, {0.5f, 0.5f});
        ImGui::AddTextAligned(draw_list, pos + int2{18}, IM_COL32_WHITE, ICON_MY_SELECT, {0.5f, 0.5f});
    }
    else if (m_mouse_mode == MouseMode_ColorInspector)
    {
        // draw pixel watcher indicator
        ImGui::DrawCrosshairs(draw_list, pos + int2{18}, " +");
    }
    else if (m_mouse_mode == MouseMode_Annotate)
    {
        // draw annotate indicator
        ImGui::AddTextAligned(draw_list, pos + int2{18} + int2{1, 1}, IM_COL32_BLACK, ICON_MY_ANNOTATE, {0.5f, 0.5f});
        ImGui::AddTextAligned(draw_list, pos + int2{18}, IM_COL32_WHITE, ICON_MY_ANNOTATE, {0.5f, 0.5f});
    }

    ImGui::PopFont();
}

void HDRViewApp::draw_image() const
{
    auto set_color = [this](Target_ target, ConstImagePtr img)
    {
        float4x4 M_to_sRGB{la::identity};
        int      channels_type  = (int)ChannelGroup::Single_Channel;
        int      straight_alpha = 0;
        float3   yw             = sRGB_Yw();

        if (img)
        {
            int                 group_idx = img->active_group_index(target);
            const ChannelGroup &group     = img->groups[group_idx];

            // FIXME: tried to pass this as a 3x3 matrix, but the data was somehow not being passed properly to MSL.
            // resulted in rapid flickering. So, for now, just pad the 3x3 matrix into a 4x4 one.
            M_to_sRGB = float4x4{
                {img->M_to_sRGB[0], 0.f}, {img->M_to_sRGB[1], 0.f}, {img->M_to_sRGB[2], 0.f}, {0.f, 0.f, 0.f, 1.f}};
            channels_type  = (int)group.type;
            straight_alpha = (int)(img->transparency == TransparencyType_Straight);
            yw             = img->luminance_weights;
        }

        // Both targets report their alpha convention: choose_channel() runs after blend(), so undoing the
        // premultiply on an isolated channel is only safe when the reference qualifies too.
        const string prefix = (target == Target_Primary) ? "fsp.primary_" : "fsp.secondary_";
        m_shader->set_uniform(prefix + "M_to_sRGB", M_to_sRGB);
        m_shader->set_uniform(prefix + "channels_type", channels_type);
        m_shader->set_uniform(prefix + "yw", yw);
        m_shader->set_uniform(prefix + "straight_alpha", straight_alpha);
    };

    set_color(Target_Primary, current_image());
    set_color(Target_Secondary, reference_image());

    if (current_image() && !current_image()->data_window.is_empty())
    {
        static mt19937 rng(53);
        float2         randomness(generate_canonical<float, 10>(rng) * 255, generate_canonical<float, 10>(rng) * 255);

        const bool   has_reference   = (bool)reference_image();
        const float2 secondary_pos   = has_reference ? image_position(reference_image()) : float2{0.f};
        const float2 secondary_scale = has_reference ? image_scale(reference_image()) : float2{1.f};

        // fs_params/vs_params are GLSL uniform blocks (see image-shader.sglsl); booleans are `int` there
        // since GLSL uniform blocks cannot contain `bool` members.
        m_shader->set_uniform("fsp.time", (float)ImGui::GetTime());
        m_shader->set_uniform("fsp.clip_warnings", int2{m_clip_warnings});
        m_shader->set_uniform("fsp.clip_range", m_clip_range);
        m_shader->set_uniform("fsp.randomness", randomness);
        m_shader->set_uniform("fsp.gain", powf(2.0f, m_exposure_live));
        m_shader->set_uniform("fsp.offset", m_offset_live);
        m_shader->set_uniform("fsp.gamma", m_gamma_live);
        m_shader->set_uniform("fsp.tonemap_mode", (int)m_tonemap);
        m_shader->set_uniform("fsp.clamp_to_LDR", (int)m_clamp_to_LDR);
        m_shader->set_uniform("fsp.do_dither", (int)m_dither);
        m_shader->set_uniform("fsp.blend_mode", (int)m_blend_mode);
        m_shader->set_uniform("fsp.channel", (int)m_channel);
        m_shader->set_uniform("fsp.bg_mode", (int)m_bg_mode);
        m_shader->set_uniform("fsp.bg_color", m_bg_color);
        m_shader->set_uniform("fsp.reverse_colormap", (int)m_reverse_colormap);
        m_shader->set_uniform("fsp.has_reference", (int)has_reference);

        m_shader->set_uniform("vsp.primary_pos", image_position(current_image()));
        m_shader->set_uniform("vsp.primary_scale", image_scale(current_image()));
        m_shader->set_uniform("vsp.secondary_pos", secondary_pos);
        m_shader->set_uniform("vsp.secondary_scale", secondary_scale);

        m_shader->set_texture("colormap", Colormap::texture(m_colormaps[m_colormap_index]));

        m_shader->begin();
        m_shader->draw_array(Shader::PrimitiveType::Triangle, 0, 6, false);
        m_shader->end();
    }

    // ImGui::Begin("Texture window");
    // ImGui::Image((ImTextureID)(intptr_t)Colormap::texture(m_colormap)->texture_handle(),
    //              ImGui::GetContentRegionAvail());
    // ImGui::End();
}

void HDRViewApp::draw_background()
{
    using namespace literals;

    // Decide once, here, whether this frame needs color management, so the two halves of the colorpass,
    // which run at different points in the frame, cannot disagree. If it does, this frame's rendering goes
    // into an offscreen target that end_colorpass_frame() converts to the real framebuffer just before the
    // frame is presented. Both no-op otherwise.
    update_colorpass();
    begin_colorpass_frame();

    static auto prev_frame                   = chrono::steady_clock::now();
    static auto last_file_changes_check_time = chrono::steady_clock::now();
    auto        this_frame                   = chrono::steady_clock::now();

    if (m_play_forward || m_play_backward)
    {
        // Keep the period in floating-point seconds: whole milliseconds would quantize the achievable rates
        // to 1000/n, landing 23.976 and 24 fps both on 41ms. The clamp matches the slider's range and bounds
        // the catch-up loop below, since the settings file can carry any value at all.
        const auto period = chrono::duration_cast<chrono::steady_clock::duration>(
            chrono::duration<float>{1.f / std::clamp(m_playback_speed, 1.f / 20.f, 60.f)});
        const auto direction = m_play_forward ? Direction_Forward : Direction_Backward;

        // Past this, either playback has just started (prev_frame still dates from the first frame drawn)
        // or the app stalled; stepping through the whole backlog would be work whose result is discarded.
        const auto resync_after = std::max(period, chrono::steady_clock::duration{1s});

        bool advanced = false;
        if (this_frame - prev_frame > resync_after)
        {
            prev_frame = this_frame;
            set_current_image_index(next_visible_image_index(m_current, direction));
            advanced = true;
        }
        else
        {
            // Accumulating whole periods, instead of resetting to now, keeps the average rate right and
            // lets a render slower than the playback rate step several images per frame.
            while (this_frame - prev_frame >= period)
            {
                prev_frame += period;
                set_current_image_index(next_visible_image_index(m_current, direction));
                advanced = true;
            }
        }

        // Only the image left current is ever displayed, so upload textures once no matter how many steps.
        if (advanced)
            set_image_textures();
    }

    // process_shortcuts();

    // If watching files for changes, do so every 250ms
    if (m_watch_files_for_changes && this_frame - last_file_changes_check_time >= 250ms)
    {
        m_image_loader.load_new_and_modified_files();
        last_file_changes_check_time = this_frame;
    }

    try
    {
        auto &io = ImGui::GetIO();

        calculate_viewport();

        handle_mouse_interaction();

        float2 fbscale = io.DisplayFramebufferScale;
        // RenderPass expects things in framebuffer coordinates
        m_render_pass->resize(int2{float2{io.DisplaySize} * fbscale});
        m_render_pass->set_viewport(int2(m_viewport_min * fbscale), int2(m_viewport_size * fbscale));

        auto_fit_viewport();

        m_render_pass->begin();
        draw_image();
        m_render_pass->end();

        draw_pixel_info();
        draw_pixel_grid();
        draw_image_border();
        draw_vector_overlays();
        draw_tool_decorations();
    }
    catch (const exception &e)
    {
        if (static LogThrottle throttle{std::chrono::seconds(5)}; throttle)
            spdlog::error("Drawing failed:\n\t{}.", e.what());
    }
}

void HDRViewApp::set_image_textures()
{
    // Every binding below goes through the shader, which setup_rendering() creates once the graphics
    // backend is up. Images can arrive before that; the next frame binds them anyway.
    if (!m_shader)
        return;

    try
    {
        // bind the primary and secondary images, or a placehold black texture when we have no current or
        // reference image
        if (auto img = current_image())
            img->set_as_texture(Target_Primary);
        else
            Image::set_null_texture(Target_Primary);

        if (auto ref = reference_image())
            ref->set_as_texture(Target_Secondary);
        else
            Image::set_null_texture(Target_Secondary);
    }
    catch (const exception &e)
    {
        if (static LogThrottle throttle{std::chrono::seconds(5)}; throttle)
            spdlog::error("Could not upload texture to graphics backend: {}.", e.what());
    }
}
