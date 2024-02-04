/** \file SampleViewer.h
    \author Wojciech Jarosz
*/

#pragma once

#include "common.h"
#include "image.h"

#include "hello_imgui/hello_imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "renderpass.h"
#include "shader.h"
#include "texture.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

using std::map;
using std::ofstream;
using std::string;
using std::string_view;
using std::vector;

class SampleViewer
{
public:
    SampleViewer();
    virtual ~SampleViewer();

    void draw_background();
    void run();

private:
    void save_as(const string &filename) const;
    void open_image();
    void load_image(std::istream &is, const string &filename);
    void close_image();
    void close_all_images();

    int           num_images() const { return int(m_images.size()); }
    int           current_image_index() const { return m_current; }
    int           reference_image_index() const { return m_reference; }
    bool          is_valid(int index) const { return index >= 0 && index < num_images(); }
    ConstImagePtr current_image() const { return image(m_current); }
    ImagePtr      current_image() { return image(m_current); }
    ConstImagePtr reference_image() const { return image(m_reference); }
    ImagePtr      reference_image() { return image(m_reference); }
    ConstImagePtr image(int index) const { return is_valid(index) ? m_images[index] : nullptr; }
    ImagePtr      image(int index) { return is_valid(index) ? m_images[index] : nullptr; }

    void set_current_image_index(int index) { m_current = is_valid(index) ? index : m_current; }

    /// Calculates the image pixel coordinates of the given position on the widget.
    float2 pixel_at_position(float2 position) const;
    /// Calculates the position inside the widget for the given image pixel coordinate.
    float2 position_at_pixel(float2 pixel) const;
    /// Calculates the position inside the screen for the given image pixel coordinate.
    float2 screen_position_at_pixel(float2 pixel) const;
    /**
     * Modifies the internal state of the image viewer widget so that the provided
     * position on the widget has the specified image pixel coordinate. Also clamps the values of offset
     * to the sides of the widget.
     */
    void set_pixel_at_position(float2 position, float2 pixel);

    float  pixel_ratio() const;
    float2 size_f() const;
    float2 center_offset(ConstImagePtr img = nullptr) const;
    Box2f  scaled_display_window(ConstImagePtr img = nullptr) const;
    Box2f  scaled_data_window(ConstImagePtr img = nullptr) const;
    float2 image_position(ConstImagePtr img = nullptr) const;
    float2 image_scale(ConstImagePtr img = nullptr) const;
    /// Centers the image without affecting the scaling factor.
    void center();
    /// Centers and scales the image so that it fits inside the widget.
    void fit();
    /**
     * Changes the scale factor by the provided amount modified by the zoom sensitivity member variable.
     * The scaling occurs such that the image pixel coordinate under the focused position remains in
     * the same screen position before and after the scaling.
     */
    void zoom_by(float amount, float2 focusPosition);
    /// Zoom in to the next power of two
    void zoom_in();
    /// Zoom out to the previous power of two
    void  zoom_out();
    float zoom_level() const;
    void  set_zoom_level(float l);

    void   draw_about_dialog();
    void   draw_pixel_info() const;
    void   draw_pixel_grid() const;
    void   draw_contents() const;
    void   draw_top_toolbar();
    void   draw_image_border() const;
    void   draw_file_window();
    void   draw_channel_window();
    void   process_hotkeys();
    float4 image_pixel(int2 p) const
    {
        auto img = current_image();
        if (!img || !img->contains(p))
            return float4{0.f};

        float4 ret{0.f};
        for (int c = 0; c < img->groups[img->selected_group].num_channels; ++c)
            ret[c] = img->channels[img->groups[img->selected_group].channels[c]](p - img->data_window.min);

        return ret;
    }

    RenderPass      *m_render_pass = nullptr;
    Shader          *m_shader      = nullptr;
    vector<ImagePtr> m_images;
    int              m_current = -1, m_reference = -1;

    float m_exposure = 0.f, m_gamma = 2.2f;
    bool  m_sRGB = false, m_hdr = true, m_dither = true, m_draw_grid = true, m_draw_pixel_info = true;

    // Image display parameters.
    float      m_zoom_sensitivity = 1.0717734625f;
    float      m_zoom             = 1.f;                      ///< The scale/zoom of the image
    float2     m_offset           = {0.f, 0.f};               ///< The panning offset of the
    EChannel   m_channel          = EChannel::RGB;            ///< Which channel to display
    EBlendMode m_blend_mode       = EBlendMode::NORMAL_BLEND; ///< How to blend the current and reference images
    EBGMode    m_bg_mode          = EBGMode::BG_DARK_CHECKER; ///< How the background around the image should be
    // rendered
    float4 m_bg_color{0.3, 0.3, 0.3, 1.0}; ///< The background color if m_bg_mode == BG_CUSTOM_COLOR

    float2 m_viewport_offset, m_viewport_size;

    HelloImGui::RunnerParams m_params;

    vector<string> m_recent_files;

    // sans and mono fonts in both regular and bold weights at various sizes
    map<int, ImFont *> m_sans_regular, m_sans_bold, m_mono_regular, m_mono_bold;
};
