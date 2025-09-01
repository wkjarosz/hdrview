/** \file app.h
    \author Wojciech Jarosz
*/

#pragma once

#include "box.h"
#include "colormap.h"
#include "hello_imgui/runner_callbacks.h"
#include "hello_imgui/runner_params.h"
#include "imageio/image_loader.h"
#include "imgui_ext.h"
#include "renderpass.h"
#include "shader.h"
#include "theme.h"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

using std::map;
using std::optional;
using std::set;
using std::string;
using std::string_view;
using std::vector;

class HDRViewApp
{
public:
    HDRViewApp(optional<float> exposure, optional<float> gamma, optional<bool> dither, optional<bool> force_sdr,
               optional<bool> force_apple_keys, vector<string> in_files = {});

    void run();

    RenderPass *renderpass() { return m_render_pass; }
    Shader     *shader() { return m_shader; }

    //-----------------------------------------------------------------------------
    // loading, saving, and closing images
    //-----------------------------------------------------------------------------
    void open_image();
    void open_folder();
    void load_images(const vector<string> &filenames);
    void load_image(const string filename, const string_view buffer = string_view{}, bool should_select = true,
                    const string channel_selector = "");
    void load_url(const string_view url);
    void save_as(const string &filename) const;
    void export_as(const string &filename) const;
    void close_image(int index = -1);
    void close_all_images();
    void reload_image(ImagePtr image, bool shall_select = false);
    //-----------------------------------------------------------------------------

    //-----------------------------------------------------------------------------
    // access to images
    //-----------------------------------------------------------------------------
    int           num_images() const { return int(m_images.size()); }
    int           num_visible_images() const { return int(m_visible_images.size()); }
    int           current_image_index() const { return m_current; }
    int           reference_image_index() const { return m_reference; }
    bool          is_valid(int index) const { return index >= 0 && index < num_images(); }
    ConstImagePtr current_image() const { return image(m_current); }
    ImagePtr      current_image() { return image(m_current); }
    ConstImagePtr reference_image() const { return image(m_reference); }
    ImagePtr      reference_image() { return image(m_reference); }
    int           image_index(ConstImagePtr img) const;
    ConstImagePtr image(int index) const { return is_valid(index) ? m_images[index] : nullptr; }
    ImagePtr      image(int index) { return is_valid(index) ? m_images[index] : nullptr; }
    void          set_current_image_index(int index, bool force = false)
    {
        m_current = force || is_valid(index) ? index : m_current;
    }
    void set_reference_image_index(int index, bool force = false)
    {
        m_reference = force || is_valid(index) ? index : m_reference;
    }
    int next_visible_image_index(int index, Direction_ direction) const;
    int nth_visible_image_index(int n) const;
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
    float2 pixel_at_vp_pos(float2 vp_pos) const;
    /// Calculates the position inside the viewport for the given image pixel coordinate.
    float2 vp_pos_at_pixel(float2 pixel) const;
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
    void center() { m_translate = float2(0.f, 0.f); }
    /// Centers and zooms the view so that the image's display window fits inside the viewport.
    void fit_display_window();
    /// Centers and zooms the view so that the image's data window fits inside the viewport.
    void fit_data_window();
    /// Centers and zooms the view so that the selection fits inside the viewport.
    void fit_selection();
    /// Applies one of the above depending on which auto-fitting mode is enabled (if any).
    void auto_fit_viewport();
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

    float4 pixel_value(int2 pixel, bool raw, int which_image) const;

    // load font with the specified name at the specified size
    ImFont *font(const string &name) const;

    ImGui::Action &action(const string &name) { return m_actions[name]; }

    float      &gamma_live() { return m_gamma_live; }
    float      &gamma() { return m_gamma; }
    float      &exposure_live() { return m_exposure_live; }
    float      &exposure() { return m_exposure; }
    float      &offset_live() { return m_offset_live; }
    float      &offset() { return m_offset; }
    Tonemap_   &tonemap() { return m_tonemap; }
    Colormap_   colormap() { return m_colormaps[m_colormap_index]; }
    BlendMode_ &blend_mode() { return m_blend_mode; }
    bool       &clamp_to_LDR() { return m_clamp_to_LDR; }
    bool       &dithering_on() { return m_dither; }
    bool       &draw_grid_on() { return m_draw_grid; }
    bool       &draw_pixel_info_on() { return m_draw_pixel_info; }
    AxisScale  &histogram_x_scale() { return m_x_scale; }
    AxisScale  &histogram_y_scale() { return m_y_scale; }
    Box2i      &roi_live() { return m_roi_live; }
    Box2i      &roi() { return m_roi; }
    bool       &draw_clip_warnings() { return m_draw_clip_warnings; }
    float2     &clip_range() { return m_clip_range; }

private:
    void load_fonts();

    void   handle_mouse_interaction();
    void   calculate_viewport();
    float2 center_offset() const;
    Box2f  scaled_display_window(ConstImagePtr img) const;
    Box2f  scaled_data_window(ConstImagePtr img) const;
    float2 image_position(ConstImagePtr img) const;
    float2 image_scale(ConstImagePtr img) const;

    void draw_background();
    void draw_channel_stats_window();
    void draw_pixel_inspector_window();
    void draw_about_dialog();
    void draw_pixel_info() const;
    void draw_pixel_grid() const;
    void draw_image() const;
    void draw_image_border() const;
    void draw_tool_decorations() const;
    void draw_file_window();
    void draw_top_toolbar();
    void draw_color_picker();
    void draw_menus();
    void draw_status_bar();
    void draw_develop_windows();
    void draw_tweak_window();
    void process_shortcuts();
    bool process_event(void *event);
    void set_image_textures();
    void draw_command_palette();
    void update_visibility();

    void setup_rendering();

    void pixel_color_widget(const int2 &pixel, int &color_mode, int which_image, bool allow_copy = false,
                            float width = 0.f) const;

private:
    //-----------------------------------------------------------------------------
    // Private data members
    //-----------------------------------------------------------------------------

    RenderPass      *m_render_pass = nullptr;
    Shader          *m_shader      = nullptr;
    vector<ImagePtr> m_images;
    set<fs::path>    m_active_directories; ///< Set of directories containing the currently loaded images
    int              m_current = -1, m_reference = -1;

    BackgroundImageLoader m_image_loader;

    int m_remaining_download = 0;

    float m_exposure = 0.f, m_exposure_live = 0.f, m_offset = 0.f, m_offset_live = 0.f, m_gamma = 1.0f,
          m_gamma_live  = 1.0f;
    AxisScale m_x_scale = AxisScale_Asinh, m_y_scale = AxisScale_Linear;
    bool      m_clamp_to_LDR = false, m_dither = true, m_draw_grid = true, m_draw_pixel_info = true,
         m_draw_watched_pixels = true, m_draw_data_window = true, m_draw_display_window = true,
         m_draw_clip_warnings = false, m_show_FPS = false;
    float2 m_clip_range{0.f, 1.f}; ///< Values outside this range will have zebra stripes if m_draw_clip_warnings = true
    Box2i  m_roi{int2{0}}, m_roi_live{int2{0}};

    void cancel_autofit() { m_auto_fit_selection = m_auto_fit_display = m_auto_fit_data = false; }

    // Image display parameters.
    float m_zoom_sensitivity = 1.0717734625f;

    bool      m_auto_fit_display   = false; ///< Continually keep the image display window fit within the viewport
    bool      m_auto_fit_data      = false; ///< Continually keep the image data window fit within the viewport
    bool      m_auto_fit_selection = false; ///< Continually keep the selection box fit within the viewport
    bool2     m_flip               = {false, false}; ///< Whether to flip the image horizontally and/or vertically
    float     m_zoom               = 1.f;            ///< The zoom factor (image pixel size / logical pixel size)
    float2    m_translate          = {0.f, 0.f};     ///< The panning offset of the image
    Channels_ m_channel            = Channels_::Channels_RGBA; ///< Which channel to display
    Tonemap_  m_tonemap            = Tonemap_Gamma;
    static constexpr Colormap_ m_colormaps[] = {
        Colormap_Viridis, Colormap_Plasma,   Colormap_Inferno,  Colormap_Hot,      Colormap_Cool,    Colormap_Pink,
        Colormap_Jet,     Colormap_Spectral, Colormap_Turbo,    Colormap_Twilight, Colormap_RdBu,    Colormap_BrBG,
        Colormap_PiYG,    Colormap_IceFire,  Colormap_CoolWarm, Colormap_Greys,    Colormap_AbsGreys};
    int             m_colormap_index   = 1;
    bool            m_reverse_colormap = false;
    BlendMode_      m_blend_mode = BlendMode_::BlendMode_Normal; ///< How to blend the current and reference images
    BackgroundMode_ m_bg_mode =
        BackgroundMode_::BGMode_Dark_Checker;     ///< How the background around the image should be rendered
    float4 m_bg_color = {0.3f, 0.3f, 0.3f, 1.0f}; ///< The background color if m_bg_mode == BGMode_Custom_Color

    float2 m_viewport_min, m_viewport_size;

    HelloImGui::RunnerParams m_params;

    ImGuiTextFilter m_file_filter, m_channel_filter;
    vector<size_t>  m_visible_images;

    ImFont *m_sans_regular = nullptr, *m_sans_bold = nullptr, *m_mono_regular = nullptr, *m_mono_bold = nullptr;

    map<string, ImGui::Action>     m_actions;
    HelloImGui::EdgeToolbarOptions m_top_toolbar_options;

    bool m_watch_files_for_changes = false; ///< Whether to watch files for changes

    Theme m_theme;

    MouseMode m_mouse_mode = MouseMode_PanZoom;

    struct WatchedPixel
    {
        int2 pixel;
        int3 color_mode{0, 0, 0}; //!< Color mode for current, reference, and composite pixels
    };
    vector<WatchedPixel> m_watched_pixels;
    int                  m_status_color_mode = 0;

    bool  m_play_forward   = false;
    bool  m_play_backward  = false;
    bool  m_play_stopped   = true;
    float m_playback_speed = 24.f;

    bool  m_open_help            = false;
    bool  m_open_command_palette = false;
    bool  m_show_developer_menu  = false;
    bool  m_show_demo_window     = false;
    bool  m_show_debug_window    = false;
    bool  m_show_tweak_window    = false;
    bool  m_show_bg_color_picker = false;
    bool  m_request_sort         = false;
    bool  m_short_names          = false;
    int   m_file_list_mode       = 1;    // 0: images only; 1: list; 2: tree;
    float m_scroll_to_next_frame = -1.f; // <0: don't focus; >=0 center ratio to focus on next frame
};

/// Create the global singleton HDRViewApp instance
void init_hdrview(optional<float> exposure, optional<float> gamma, optional<bool> dither, optional<bool> force_sdr,
                  optional<bool> apple_keys, const vector<string> &in_files = {});

/// Return a pointer to the global singleton HDRViewApp instance
HDRViewApp *hdrview();
