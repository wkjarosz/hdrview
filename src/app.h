/** \file app.h
    \author Wojciech Jarosz
*/

#pragma once

#include "box.h"
#include "colormap.h"
#include "display_colorspace.h"
#include "imageio/image_loader.h"
#include "imgui_ext.h"
#include "json.h"
#include "renderpass.h"
#include "shader.h"
#include "theme.h"
#include <deque>
#include <filesystem>
#include <hello_imgui/runner_callbacks.h>
#include <hello_imgui/runner_params.h>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef HDRVIEW_ENABLE_GUI_TEST_ENGINE
struct ImGuiTestEngine;
#endif

namespace fs = std::filesystem;

using std::deque;
using std::map;
using std::optional;
using std::pair;
using std::set;
using std::string;
using std::string_view;
using std::unique_ptr;
using std::vector;

class HDRViewApp
{
public:
    HDRViewApp(optional<float> exposure, optional<float> gamma, optional<bool> dither, optional<bool> force_sdr,
               optional<bool> force_apple_keys, vector<string> in_files = {});

    void run();

#ifdef HDRVIEW_ENABLE_GUI_TEST_ENGINE
    /// Turns on Dear ImGui Test Engine for this app instance. `register_tests` is invoked once the engine is
    /// ready and should call IM_REGISTER_TEST(engine, ...) for every test; all registered tests are then queued
    /// automatically. run() exits once the queue is empty, after which test_engine_result() reports how many of
    /// the queued tests passed. Must be called before run(). Only ever compiled into the hdrview_gui_tests
    /// target — the production HDRView binary and hdrview_tests never define HDRVIEW_ENABLE_GUI_TEST_ENGINE.
    void enable_gui_test_engine(void (*register_tests)(ImGuiTestEngine *));
    /// Valid only after run() returns; {tested, succeeded} counts from the Test Engine queue.
    std::pair<int, int> test_engine_result() const { return {m_test_engine_tested, m_test_engine_succeeded}; }
#endif

    RenderPass *renderpass() { return m_render_pass.get(); }
    Shader     *shader() { return m_shader.get(); }

    //-----------------------------------------------------------------------------
    // loading, saving, and closing images
    //-----------------------------------------------------------------------------
    void open_image();
    void open_folder();
    void load_images(const vector<string> &filenames);
    void load_image(const string filename, const string_view buffer = string_view{}, bool should_select = true,
                    const ImageLoadOptions &opts = {});
    void load_url(const string_view url);
    void close_image(int index = -1);
    void close_all_images();
    void reload_image(ImagePtr image, bool shall_select = false);

    //-----------------------------------------------------------------------------
    // saving/loading an entire session (loaded images, current/reference selection, blend mode,
    // and view/display settings) to/from a user-chosen .hsess file
    //-----------------------------------------------------------------------------
    void save_session();
    void load_session();
    void load_session(const string &filename);
    void export_session_bundle();
    // Emscripten's "Load session..." entry point: uploads a .zip and loads it strictly as a session bundle,
    // erroring rather than falling back to plain image loading if it doesn't contain a manifest.
    void open_session_bundle();
    // Looks for a session manifest ("*.hsess") at the root of `zip_bytes` (a zip archive named `zip_name`,
    // for identity/logging). If found, loads it as a session and returns true; otherwise returns false so
    // the caller can fall back to treating the zip as a plain multi-image archive. Works on native and web.
    bool try_load_zip_as_session(string_view zip_bytes, const string &zip_name);
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

    /// True when the mouse is over the visible image viewport and nothing else (a panel, a popup, ...)
    /// is currently claiming the mouse. The geometric *_in_viewport() checks above don't account for
    /// occlusion by other windows, so hover-based pixel displays should gate on this instead.
    bool mouse_over_viewport() const
    {
        return app_pos_in_viewport(ImGui::GetIO().MousePos) && !ImGui::GetIO().WantCaptureMouse;
    }

    /// The most recently hovered image pixel while the mouse was over the viewport, held (not cleared)
    /// once the mouse moves elsewhere so that hover-driven widgets (e.g. the pixel-color button) stay
    /// interactable instead of disappearing as soon as the mouse leaves the viewport to reach them.
    /// Returns nullopt only if the viewport has never been hovered this session.
    optional<int2> last_hovered_pixel()
    {
        if (mouse_over_viewport())
            m_last_hovered_pixel = int2{pixel_at_app_pos(ImGui::GetIO().MousePos)};
        return m_last_hovered_pixel;
    }
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

    //! Applies the same exposure/tonemap/gamma pipeline as pixel_value(..., raw=false, ...), for an
    //! arbitrary linear value rather than a pixel lookup (e.g. to preview a computed statistic).
    float4 tonemap_value(float4 value) const;

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
    float      &histogram_height() { return m_histogram_height; }
    Box2i      &roi_live() { return m_roi_live; }
    Box2i      &roi() { return m_roi; }
    bool2      &clip_warnings() { return m_clip_warnings; }
    float2     &clip_range() { return m_clip_range; }

    /// Height of the Pixel statistics histogram plot, in em, when it has never been resized
    static constexpr float default_histogram_height = 9.f;

private:
    void load_fonts();

    // Builds the "HDRView session" manifest for the currently loaded images/view settings, shared by
    // save_session() and export_session_bundle() -- they differ only in what "path" each image gets, which
    // `path_of` supplies (a filesystem-relative path for a plain .hsess, an in-bundle location for a zip
    // export).
    json build_session_manifest(const std::function<string(ConstImagePtr)> &path_of) const;

    // Begins asynchronously loading the images listed in a parsed session file `j` (paths resolved relative to
    // `dir`), populating m_pending_session so the per-frame image-loader drain can apply the rest of the session
    // (current/reference selection, blend mode, view settings) once every image has finished loading.
    void begin_session_load(const json &j, const fs::path &dir);
    // Same as begin_session_load(), but for a session bundled inside a zip: `zip_bytes` is the whole
    // archive, and each image entry's "path" is resolved against the zip's own internal entries (extracted
    // into memory and fed to the loader as a buffer) rather than a filesystem directory.
    void begin_bundle_session_load(string_view zip_bytes, const string &zip_name, const json &j);
    // Called once every image in m_pending_session has been resolved (successfully or not); rebuilds m_images
    // in the saved order, then applies current/reference selection, blend mode, and view settings.
    void finish_pending_session();

    void   handle_mouse_interaction();
    void   calculate_viewport();
    float2 center_offset() const;
    Box2f  scaled_display_window(ConstImagePtr img) const;
    Box2f  scaled_data_window(ConstImagePtr img) const;
    float2 image_position(ConstImagePtr img) const;
    float2 image_scale(ConstImagePtr img) const;

    void draw_background();
    void draw_statistics_window();
    void draw_about_dialog(bool &);
    void draw_command_palette(bool &);
    void draw_save_as_dialog(bool &);
    void draw_confirm_load_session_dialog(bool &open);
    void draw_loading_session_dialog(bool &open);
    void draw_pixel_info() const;
    void draw_pixel_grid() const;
    void draw_image() const;
    void draw_image_border() const;
    void draw_tool_decorations() const;
    void draw_file_window();
    void draw_top_toolbar();
    void draw_menus();
    void draw_status_bar();
    void draw_developer_windows();
    void draw_tweak_window();
    void process_shortcuts();
    bool process_event(void *event);
    void set_image_textures();
    void update_visibility();

    void setup_rendering();
    /// Re-query the display's color space and (re)create colorpass resources if it now needs them. Called
    /// once per frame, before either colorpass half runs.
    void update_colorpass();
    void begin_colorpass_frame();
    void end_colorpass_frame();
    void cleanup_colorpass();

    /// True when this display can actually show more than standard dynamic range, i.e. when the HDR-only UI
    /// (the "Clamp to LDR" control) is meaningful. Combines the framebuffer we were actually granted with
    /// what the display reports about itself.
    bool supports_hdr() const;

    void pixel_color_widget(const int2 &pixel, int &color_mode, int which_image, bool allow_copy = false,
                            float width = 0.f, const string &trailing_label = {}) const;

private:
    //-----------------------------------------------------------------------------
    // Constructor setup, broken into named phases called in order from HDRViewApp(); see app.cpp.
    //-----------------------------------------------------------------------------
    struct DockableWindowExtraInfo
    {
        ImGuiKeyChord chord = ImGuiKey_None;
        const char   *icon  = nullptr;
    };
    struct WindowSetupInfo
    {
        ImGuiKey                        mod_key;
        vector<DockableWindowExtraInfo> window_info;
    };

    void            setup_window_and_backend(optional<bool> force_sdr);
    void            setup_hello_imgui_params();
    WindowSetupInfo setup_dockable_windows();
    void            setup_platform_backend_callbacks(vector<string> in_files);
    void            setup_persistence_callbacks(optional<float> force_exposure, optional<float> force_gamma,
                                                optional<bool> force_dither, optional<bool> force_apple_keys);
    void            setup_imgui_style_callbacks();
    void            setup_frame_callbacks();
    void            setup_dialogs(const vector<string> &in_files);
    void            setup_actions(ImGuiKey modKey, const vector<DockableWindowExtraInfo> &window_info);

    //-----------------------------------------------------------------------------
    // Loaded images and the background loader
    //-----------------------------------------------------------------------------
    vector<ImagePtr> m_images;
    set<fs::path>    m_active_directories; ///< Set of directories containing the currently loaded images
    int              m_current = -1, m_reference = -1;

    BackgroundImageLoader m_image_loader;

    int m_remaining_download = 0;

    ImGuiTextFilter m_file_filter, m_channel_filter;
    vector<size_t>  m_visible_images;

    //-----------------------------------------------------------------------------
    // GPU render resources for drawing the main image; the colorpass block below owns a
    // separate set of resources for the HDR display color-management pass.
    //-----------------------------------------------------------------------------
    std::unique_ptr<RenderPass> m_render_pass;
    std::unique_ptr<Shader>     m_shader;

    //-----------------------------------------------------------------------------
    // The colorpass: offscreen HDR color target, the pass that renders into it, the pass that resolves it to
    // the window's framebuffer, the conversion shader, and the bookkeeping that decides whether it's needed.
    // All null/inert unless the display needs color management. See
    // update_colorpass()/begin_colorpass_frame()/end_colorpass_frame() in app-colorpass.cpp.
    //-----------------------------------------------------------------------------
    std::unique_ptr<Texture>    m_color_texture;
    std::unique_ptr<RenderPass> m_colorpass_pass;
    std::unique_ptr<RenderPass> m_resolve_pass;
    std::unique_ptr<Shader>     m_colorpass_shader;
    /// The color space the display currently wants, re-queried once per frame by update_colorpass() so that
    /// moving the window between monitors, or toggling the OS's HDR mode, takes effect live.
    DisplayColorSpace m_display_cs;
    /// Rec.709 -> m_display_cs.chroma, recomputed only when m_display_cs changes.
    float3x3 m_gamut_matrix{la::identity};
    /// True when the display needs the colorpass to convert away from HDRView's extended-sRGB working
    /// convention (e.g. Windows/Linux scRGB or Linux/Wayland PQ). Only ever true on the OpenGL/GLFW backend;
    /// macOS EDR consumes the extended-sRGB output directly.
    bool m_color_managed = false;
    /// True once we've given up on the colorpass (shader or FBO creation failed), so we don't retry forever.
    bool m_colorpass_failed = false;
    /// True when the float framebuffer we asked for was actually granted (hello_imgui clears the request
    /// when it can't be satisfied). Drives the HDR-related UI, replacing hasEdrSupport() off of macOS.
    bool m_float_buffer = false;
    /// The --sdr command-line flag: behave as if the display were plain SDR, whatever it actually is.
    /// Checked by supports_hdr(), so it also hides the HDR-only UI.
    bool m_force_sdr = false;

    //-----------------------------------------------------------------------------
    // Exposure/gamma/tonemap and viewport overlay display flags
    //-----------------------------------------------------------------------------
    float m_exposure = 0.f, m_exposure_live = 0.f, m_offset = 0.f, m_offset_live = 0.f, m_gamma = 1.0f,
          m_gamma_live  = 1.0f;
    AxisScale m_x_scale = AxisScale_Asinh, m_y_scale = AxisScale_Linear;
    float     m_histogram_height = default_histogram_height;

    bool m_clamp_to_LDR = false, m_dither = true, m_draw_grid = true, m_draw_pixel_info = true,
         m_draw_watched_pixels = true, m_draw_data_window = true, m_draw_display_window = true, m_show_FPS = false;
    /// Zebra-stripe values below clip_range.x (x: shadows) and above clip_range.y (y: highlights)
    bool2  m_clip_warnings{false, false};
    float2 m_clip_range{0.f, 1.f};
    Box2i  m_roi{int2{0}}, m_roi_live{int2{0}};

    Tonemap_                   m_tonemap     = Tonemap_Gamma;
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

    //-----------------------------------------------------------------------------
    // Pan/zoom/flip and the viewport's on-screen extent
    //-----------------------------------------------------------------------------
    void cancel_autofit() { m_auto_fit_selection = m_auto_fit_display = m_auto_fit_data = false; }

    float m_zoom_sensitivity = 1.0717734625f;

    bool      m_auto_fit_display   = false; ///< Continually keep the image display window fit within the viewport
    bool      m_auto_fit_data      = false; ///< Continually keep the image data window fit within the viewport
    bool      m_auto_fit_selection = false; ///< Continually keep the selection box fit within the viewport
    bool2     m_flip               = {false, false}; ///< Whether to flip the image horizontally and/or vertically
    float     m_zoom               = 1.f;            ///< The zoom factor (image pixel size / logical pixel size)
    float2    m_translate          = {0.f, 0.f};     ///< The panning offset of the image
    Channels_ m_channel            = Channels_::Channels_RGBA; ///< Which channel to display

    float2         m_viewport_min, m_viewport_size;
    optional<int2> m_last_hovered_pixel; ///< see last_hovered_pixel()

    MouseMode m_mouse_mode = MouseMode_PanZoom;

    HelloImGui::DockableWindow *m_log_window = nullptr; ///< Pointer to log window, captured when constructed

    //-----------------------------------------------------------------------------
    // Hello ImGui / app framework state
    //-----------------------------------------------------------------------------
    HelloImGui::RunnerParams m_params;

#ifdef HDRVIEW_ENABLE_GUI_TEST_ENGINE
    int m_test_engine_tested = 0, m_test_engine_succeeded = 0; ///< see test_engine_result()
#endif

    ImFont *m_sans_regular = nullptr, *m_sans_bold = nullptr, *m_mono_regular = nullptr, *m_mono_bold = nullptr;

    map<string, ImGui::Action>     m_actions;
    HelloImGui::EdgeToolbarOptions m_top_toolbar_options;

    Theme m_theme;

    //-----------------------------------------------------------------------------
    // Pixel inspector / watched pixels
    //-----------------------------------------------------------------------------
    struct WatchedPixel
    {
        int2 pixel;
        int3 color_mode{0, 0, 0}; //!< Color mode for current, reference, and composite pixels
    };
    vector<WatchedPixel> m_watched_pixels;
    int                  m_status_color_mode = 0;

    //-----------------------------------------------------------------------------
    // Playback
    //-----------------------------------------------------------------------------
    bool  m_play_forward   = false;
    bool  m_play_backward  = false;
    bool  m_play_stopped   = true;
    float m_playback_speed = 24.f;

    //-----------------------------------------------------------------------------
    // GUI/dev-window visibility and file-list state
    //-----------------------------------------------------------------------------
    bool m_watch_files_for_changes = false; ///< Whether to watch files for changes

    bool  m_show_developer_menu  = false;
    bool  m_show_demo_window     = false;
    bool  m_show_debug_window    = false;
    bool  m_show_tweak_window    = false;
    bool  m_request_sort         = false;
    bool  m_short_names          = false;
    int   m_file_list_mode       = 1;    // 0: images only; 1: list; 2: tree;
    float m_scroll_to_next_frame = -1.f; // <0: don't focus; >=0 center ratio to focus on next frame

    //-----------------------------------------------------------------------------
    // Popup dialogs
    //-----------------------------------------------------------------------------
    struct PopupDialog
    {
        string                      title;
        std::function<void(bool &)> draw;
        bool                        open;

        PopupDialog(string title_, std::function<void(bool &)> draw_func, bool initially_open = false) :
            title(std::move(title_)), draw(draw_func), open(initially_open)
        {
        }
    };
    // Dispatched once per frame, in registration order (see setup_dialogs()), from ShowGui.
    vector<unique_ptr<PopupDialog>> m_dialogs;
    // Linear search by title; ~8 dialogs, only ever called from a user-triggered action, never per-frame.
    PopupDialog &dialog(const string &title);

    // A parsed session file waiting on the user to confirm closing currently-open images before it starts loading.
    struct PendingSessionLoad
    {
        json     j;
        fs::path dir;
    };
    optional<PendingSessionLoad> m_pending_session_load;

    // Same as PendingSessionLoad, but for a session bundled inside a zip: the zip's bytes must be kept
    // alive (owned here) until the user confirms, since begin_bundle_session_load() needs them again then.
    struct PendingZipSessionLoad
    {
        string zip_bytes;
        string zip_name;
        json   j;
    };
    optional<PendingZipSessionLoad> m_pending_zip_session_load;

    // A session whose images have been issued to the BackgroundImageLoader and are loading asynchronously; the
    // rest of the session (selection, blend mode, view settings) is applied once every entry here has been
    // resolved (successfully or not) by the per-frame image-loader drain in the PostInit-registered callback.
    struct PendingSession
    {
        struct Entry
        {
            fs::path path;
            string   channel_selector;
            int      selected_group = 0, reference_group = 0;
            ImagePtr loaded; ///< Set once this entry's image arrives; still null => not yet arrived, or failed
        };
        vector<Entry> entries; ///< One per saved "images" entry, in file order; the same path may repeat
        int           current_index = -1, reference_index = -1; ///< Index into entries, or -1 if unset
        BlendMode_    blend_mode;
        json          view; ///< The session file's "view" sub-object, applied verbatim once loading completes

        // An arriving image is matched to the earliest not-yet-filled entry sharing its (path, channel_selector).
        // Entries sharing that key are guaranteed content-identical (loaded with identical options), so any
        // valid one-to-one assignment among them is correct regardless of which physical async load happens to
        // finish first -- this is what makes it safe to load the same file more than once in one session (e.g.
        // to compare two channel groups of it side by side).
        map<pair<fs::path, string>, deque<int>> unresolved;
    };
    optional<PendingSession> m_pending_session;
};

/// Create the global singleton HDRViewApp instance
void init_hdrview(optional<float> exposure, optional<float> gamma, optional<bool> dither, optional<bool> force_sdr,
                  optional<bool> apple_keys, const vector<string> &in_files = {});

/// Return a pointer to the global singleton HDRViewApp instance
HDRViewApp *hdrview();
