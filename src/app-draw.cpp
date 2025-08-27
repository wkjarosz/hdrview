/** \file app.cpp
    \author Wojciech Jarosz
*/

#include "app.h"

#include "colormap.h"
#include "fonts.h"
#include "texture.h"

#include <spdlog/mdc.h>

#include <random>
#include <utility>

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

    auto ref = reference_image();

    static constexpr float2 align = {0.5f, 0.5f};

    auto  &group = img->groups[img->selected_group];
    string names[4];
    string longest_name;
    for (int c = 0; c < group.num_channels; ++c)
    {
        auto &channel = img->channels[group.channels[c]];
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
            auto   pos        = app_pos_at_pixel(float2(x + 0.5f, y + 0.5f));
            float4 r_pixel    = pixel_value({x, y}, true, 2);
            float4 t_pixel    = linear_to_sRGB(pixel_value({x, y}, false, 2));
            float4 pixel      = m_status_color_mode == 0 ? r_pixel : t_pixel;
            float3 text_color = contrasting_color(t_pixel.xyz());
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

    ImGui::PopFont();
}

void HDRViewApp::draw_image() const
{
    auto set_color = [this](Target target, ConstImagePtr img)
    {
        auto t = target_name(target);
        if (img)
        {
            int                 group_idx = target == Target_Primary ? img->selected_group : img->reference_group;
            const ChannelGroup &group     = img->groups[group_idx];

            // FIXME: tried to pass this as a 3x3 matrix, but the data was somehow not being passed properly to MSL.
            // resulted in rapid flickering. So, for now, just pad the 3x3 matrix into a 4x4 one.
            m_shader->set_uniform(fmt::format("{}_M_to_sRGB", t), float4x4{{img->M_to_sRGB[0], 0.f},
                                                                           {img->M_to_sRGB[1], 0.f},
                                                                           {img->M_to_sRGB[2], 0.f},
                                                                           {0.f, 0.f, 0.f, 1.f}});
            m_shader->set_uniform(fmt::format("{}_channels_type", t), (int)group.type);
            m_shader->set_uniform(fmt::format("{}_yw", t), img->luminance_weights);
        }
        else
        {
            m_shader->set_uniform(fmt::format("{}_M_to_sRGB", t), float4x4{la::identity});
            m_shader->set_uniform(fmt::format("{}_channels_type", t), (int)ChannelGroup::Single_Channel);
            m_shader->set_uniform(fmt::format("{}_yw", t), sRGB_Yw());
        }
    };

    set_color(Target_Primary, current_image());
    set_color(Target_Secondary, reference_image());

    if (current_image() && !current_image()->data_window.is_empty())
    {
        static mt19937 rng(53);
        float2         randomness(generate_canonical<float, 10>(rng) * 255, generate_canonical<float, 10>(rng) * 255);

        m_shader->set_uniform("time", (float)ImGui::GetTime());
        m_shader->set_uniform("draw_clip_warnings", m_draw_clip_warnings);
        m_shader->set_uniform("clip_range", m_clip_range);
        m_shader->set_uniform("randomness", randomness);
        m_shader->set_uniform("gain", powf(2.0f, m_exposure_live));
        m_shader->set_uniform("offset", m_offset_live);
        m_shader->set_uniform("gamma", m_gamma_live);
        m_shader->set_uniform("tonemap_mode", (int)m_tonemap);
        m_shader->set_uniform("clamp_to_LDR", m_clamp_to_LDR);
        m_shader->set_uniform("do_dither", m_dither);

        m_shader->set_uniform("primary_pos", image_position(current_image()));
        m_shader->set_uniform("primary_scale", image_scale(current_image()));

        m_shader->set_uniform("blend_mode", (int)m_blend_mode);
        m_shader->set_uniform("channel", (int)m_channel);
        m_shader->set_uniform("bg_mode", (int)m_bg_mode);
        m_shader->set_uniform("bg_color", m_bg_color);

        m_shader->set_texture("colormap", Colormap::texture(m_colormaps[m_colormap_index]));
        m_shader->set_uniform("reverse_colormap", m_reverse_colormap);

        if (reference_image())
        {
            m_shader->set_uniform("has_reference", true);
            m_shader->set_uniform("secondary_pos", image_position(reference_image()));
            m_shader->set_uniform("secondary_scale", image_scale(reference_image()));
        }
        else
        {
            m_shader->set_uniform("has_reference", false);
            m_shader->set_uniform("secondary_pos", float2{0.f});
            m_shader->set_uniform("secondary_scale", float2{1.f});
        }

        m_shader->begin();
        m_shader->draw_array(Shader::PrimitiveType::Triangle, 0, 6, false);
        m_shader->end();
    }

    // ImGui::Begin("Texture window");
    // ImGui::Image((ImTextureID)(intptr_t)Colormap::texture(m_colormap)->texture_handle(),
    //              ImGui::GetContentRegionAvail());
    // ImGui::End();
}

void HDRViewApp::process_shortcuts()
{
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        spdlog::trace("Not processing shortcuts because ImGui wants to capture the keyboard");
        return;
    }

    // spdlog::trace("Processing shortcuts (frame: {})", ImGui::GetFrameCount());

    for (auto &a : m_actions)
        if (a.second.chord)
            if (a.second.enabled() && ImGui::GlobalShortcut(a.second.chord, a.second.flags))
            {
                spdlog::trace("Processing shortcut for action '{}' (frame: {})", a.second.name, ImGui::GetFrameCount());
                if (a.second.p_selected)
                    *a.second.p_selected = !*a.second.p_selected;
                a.second.callback();
#ifdef __EMSCRIPTEN__
                ImGui::GetIO().ClearInputKeys(); // FIXME: somehow needed in emscripten, otherwise the key (without
                                                 // modifiers) needs to be pressed before this chord is detected again
#endif
                break;
            }

    set_image_textures();
}

void HDRViewApp::draw_background()
{
    using namespace literals;
    spdlog::mdc::put(" f", to_string(ImGui::GetFrameCount()));

    static auto prev_frame                   = chrono::steady_clock::now();
    static auto last_file_changes_check_time = chrono::steady_clock::now();
    auto        this_frame                   = chrono::steady_clock::now();

    if ((m_play_forward || m_play_backward) &&
        this_frame - prev_frame >= chrono::milliseconds(int(1000 / m_playback_speed)))
    {
        set_current_image_index(next_visible_image_index(m_current, m_play_forward ? Forward : Backward));
        set_image_textures();
        prev_frame = this_frame;
    }

    process_shortcuts();

    // If watching files for changes, do so every 250ms
    if (m_watch_files_for_changes && this_frame - last_file_changes_check_time >= 250ms)
    {
        spdlog::trace("Checking for file changes...");
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
        draw_tool_decorations();
    }
    catch (const exception &e)
    {
        spdlog::error("Drawing failed:\n\t{}.", e.what());
    }
}

void HDRViewApp::set_image_textures()
{
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
        spdlog::error("Could not upload texture to graphics backend: {}.", e.what());
    }
}

void HDRViewApp::setup_rendering()
{
    try
    {
        m_render_pass = new RenderPass(false, true);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);
        m_render_pass->set_depth_test(RenderPass::DepthTest::Always, false);
        m_render_pass->set_clear_color(float4(0.15f, 0.15f, 0.15f, 1.f));

        m_shader = new Shader(
            m_render_pass,
            /* An identifying name */
            "ImageView", Shader::from_asset("shaders/image-shader_vert"),
            Shader::prepend_includes(Shader::from_asset("shaders/image-shader_frag"), {"shaders/colorspaces"}),
            Shader::BlendMode::AlphaBlend);

        const float positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, 1.f};

        m_shader->set_buffer("position", VariableType::Float32, {6, 2}, positions);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);

        Image::make_default_textures();
        Colormap::initialize();

        m_shader->set_texture("dither_texture", Image::dither_texture());
        set_image_textures();
        spdlog::info("Successfully initialized graphics API!");
    }
    catch (const exception &e)
    {
        spdlog::error("Shader initialization failed!:\n\t{}.", e.what());
    }
}
