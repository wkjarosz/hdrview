/** \file app.h
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
using std::pair;
using std::string;
using std::string_view;
using std::vector;

class HDRViewApp
{
public:
    HDRViewApp();
    ~HDRViewApp();

    void run();

    RenderPass *renderpass() { return m_render_pass; }
    Shader     *shader() { return m_shader; }

    //-----------------------------------------------------------------------------
    // loading, saving, and closing images
    //-----------------------------------------------------------------------------
    void open_image();
    void load_image(std::istream &is, const string &filename);
    void save_as(const string &filename) const;
    void close_image();
    void close_all_images();
    //-----------------------------------------------------------------------------

    //-----------------------------------------------------------------------------
    // access to images
    //-----------------------------------------------------------------------------
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
    void          set_current_image_index(int index) { m_current = is_valid(index) ? index : m_current; }
    //-----------------------------------------------------------------------------

    //-----------------------------------------------------------------------------
    // These function allow converting between our various coordinate systems:
    //   1) app position (app_pos): coordinates within the entire native app window.
    //      Same coordinate system as ImGui::GetIO().MousePos.
    //   2) viewport position (vp_pos): coordinates within the portion of the main app window that displays the image
    //      The image is displayed in the central node of dear imgui's dock system
    //   3) pixel (pixel): coordinates within the displayed image (origin: top-left of image)
    //-----------------------------------------------------------------------------
    /// Calculates the image pixel coordinates of the given position in the viewport
    float2 pixel_at_vp_pos(float2 vp_pos) const { return (vp_pos - (m_offset + center_offset())) / m_zoom; }
    /// Calculates the position inside the viewport for the given image pixel coordinate.
    float2 vp_pos_at_pixel(float2 pixel) const { return m_zoom * pixel + (m_offset + center_offset()); }
    /// Calculates the app position at the given image pixel coordinate.
    float2 app_pos_at_pixel(float2 pixel) const { return app_pos_at_vp_pos(vp_pos_at_pixel(pixel)); }
    /// Calculates the image pixel coordinates at the given app position.
    float2 pixel_at_app_pos(float2 app_pos) const { return pixel_at_vp_pos(vp_pos_at_app_pos(app_pos)); }
    /// Convert from vp_pos to app_pos (just a translation); inverse of vp_pos_at_app_pos()
    float2 app_pos_at_vp_pos(float2 vp_pos) const { return vp_pos + m_viewport_min; }
    /// Convert from vp_pos to app_pos (just a translation); inverse of app_pos_at_vp_pos()
    float2 vp_pos_at_app_pos(float2 app_pos) const { return app_pos - m_viewport_min; }

    /// Reposition the image so that the specified image pixel coordinate lies under the provided viewport position
    void reposition_pixel_to_vp_pos(float2 vp_pos, float2 pixel);

    float  pixel_ratio() const;
    float2 viewport_size() const { return m_viewport_size; }
    bool   vp_pos_in_viewport(float2 vp_pos) const
    {
        return all(gequal(vp_pos, 0.f)) && all(less(vp_pos, m_viewport_size));
    }
    bool app_pos_in_viewport(float2 app_pos) const { return vp_pos_in_viewport(vp_pos_at_app_pos(app_pos)); }
    bool pixel_in_viewport(float2 pixel) const { return vp_pos_in_viewport(vp_pos_at_pixel(pixel)); }
    //-----------------------------------------------------------------------------

    //-----------------------------------------------------------------------------
    // Higher-level functions that modify the placement and zooming of the image
    //-----------------------------------------------------------------------------
    /// Centers the image without affecting the scaling factor.
    void center();
    /// Centers and scales the image so that it fits inside the widget.
    void fit();
    /**
        Changes the zoom factor by the provided amount modified by the zoom sensitivity member variable.
        The scaling occurs such that the image pixel coordinate under focus_vp_pos remains in
        the same place before and after the zoom.
    */
    void zoom_at_vp_pos(float amount, float2 focus_vp_pos);
    /// Zoom in to the next power of two
    void zoom_in();
    /// Zoom out to the previous power of two
    void  zoom_out();
    float zoom_level() const;
    void  set_zoom_level(float l);
    //-----------------------------------------------------------------------------

    float4 image_pixel(int2 pixel) const
    {
        auto img = current_image();
        if (!img || !img->contains(pixel))
            return float4{0.f};

        float4 ret{0.f};
        for (int c = 0; c < img->groups[img->selected_group].num_channels; ++c)
            ret[c] = img->channels[img->groups[img->selected_group].channels[c]](pixel - img->data_window.min);

        return ret;
    }

    // load font with the specified name at the specified size (if size > 0) or at the current font size (size <= 0)
    ImFont *font(const string &name, int size = -1) const;
    ImFont *load_font(const string &name, int size, bool merge_fa6 = false);
    ImFont *deferred_load_font(const string &name, int size, bool merge_fa6 = false);

private:
    void load_fonts();

    float2 center_offset(ConstImagePtr img = nullptr) const;
    Box2f  scaled_display_window(ConstImagePtr img = nullptr) const;
    Box2f  scaled_data_window(ConstImagePtr img = nullptr) const;
    float2 image_position(ConstImagePtr img = nullptr) const;
    float2 image_scale(ConstImagePtr img = nullptr) const;

    void draw_background();
    void draw_histogram_window();
    void draw_info_window();
    void draw_about_dialog();
    void draw_pixel_info() const;
    void draw_pixel_grid() const;
    void draw_image() const;
    void draw_top_toolbar();
    void draw_image_border() const;
    void draw_file_window();
    void draw_channel_window();
    void process_hotkeys();

private:
    //-----------------------------------------------------------------------------
    // Private data members
    //-----------------------------------------------------------------------------

    RenderPass      *m_render_pass = nullptr;
    Shader          *m_shader      = nullptr;
    vector<ImagePtr> m_images;
    int              m_current = -1, m_reference = -1;

    float m_exposure = 0.f, m_gamma = 2.2f;
    bool  m_sRGB = false, m_hdr = true, m_dither = true, m_draw_grid = true, m_draw_pixel_info = true;

    // Image display parameters.
    float m_zoom_sensitivity = 1.0717734625f;

    float      m_zoom       = 1.f;                      ///< The zoom factor (image pixel size / logical pixel size)
    float2     m_offset     = {0.f, 0.f};               ///< The panning offset of the image
    EChannel   m_channel    = EChannel::RGB;            ///< Which channel to display
    EBlendMode m_blend_mode = EBlendMode::NORMAL_BLEND; ///< How to blend the current and reference images
    EBGMode    m_bg_mode    = EBGMode::BG_DARK_CHECKER; ///< How the background around the image should be rendered
    float4     m_bg_color   = {0.3, 0.3, 0.3, 1.0};     ///< The background color if m_bg_mode == BG_CUSTOM_COLOR

    float2 m_viewport_min, m_viewport_size;

    HelloImGui::RunnerParams m_params;

    vector<string> m_recent_files;

    map<pair<string, int>, ImFont *> m_fonts;
    struct FontParams
    {
        string name;
        int    size;
        bool   merge_fa6;
    };
    vector<FontParams> m_deferred_fonts;
};

/// Return a pointer to the global singleton HDRViewApp instance
HDRViewApp *g_app();
