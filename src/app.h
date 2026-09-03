/** \file app.h
    \author Wojciech Jarosz
*/

#pragma once

#include "annotations.h"
#include "box.h"
#include "colormap.h"
#include "display_colorspace.h"
#include "edit/commands.h"
#include "edit/edit_ops.h"
#include "edit/envmap.h"
#include "edit/filters.h"
#include "edit/subject.h"
#include "edit/undo.h"
#include "imageio/image_loader.h"
#include "imgui_ext.h"
#if HDRVIEW_ENABLE_IPC
#include "ipc/ipc_server.h"
#endif
#include "json.h"
#include "renderpass.h"
#include "shader.h"
#include "theme.h"
#include <deque>
#include <filesystem>
#include <functional>
#include <hello_imgui/runner_callbacks.h>
#include <hello_imgui/runner_params.h>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
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

/// Name of the action that switches to the given tool; the only mapping from MouseMode_ to the action registry.
const char *mouse_mode_action_name(MouseMode m);

class HDRViewApp
{
public:
    HDRViewApp(optional<float> exposure, optional<float> gamma, optional<bool> dither, optional<bool> force_sdr,
               optional<bool> force_apple_keys, vector<string> in_files = {});

    void run();

#ifdef HDRVIEW_ENABLE_GUI_TEST_ENGINE
    /// Turn on Dear ImGui Test Engine for this app instance; must be called before run().
    /**
        `register_tests` is invoked once the engine is ready and should IM_REGISTER_TEST(engine, ...) every
        test; all of them are then queued, and run() returns once the queue is empty.
    */
    void enable_gui_test_engine(void (*register_tests)(ImGuiTestEngine *));
    /// Valid only after run() returns; {tested, succeeded} counts from the Test Engine queue.
    std::pair<int, int> test_engine_result() const { return {m_test_engine_tested, m_test_engine_succeeded}; }
    /// The buffer a screenshot must be read back from, or nullptr when the window's own framebuffer will do.
    /**
        Only the colorpass's offscreen target still holds extended sRGB; the window is display-referred.
    */
    const RenderPass *capture_source() const { return m_color_managed ? m_colorpass_pass.get() : nullptr; }
#endif

    RenderPass *renderpass() { return m_render_pass.get(); }
    Shader     *shader() { return m_shader.get(); }

    //-----------------------------------------------------------------------------
    // loading, saving, and closing images
    //-----------------------------------------------------------------------------
    void open_image();
    void open_folder();
    void load_images(const vector<string> &filenames);
    void load_image(const string filename, std::optional<string_view> buffer = std::nullopt, bool should_select = true,
                    const ImageLoadOptions &opts = {}, ImagePtr to_replace = nullptr);
    /// Download `url` and load it as an image (Emscripten only). `to_replace` reloads in place, as reload_image() does.
    void load_url(string_view url, bool should_select = true, ImagePtr to_replace = nullptr,
                  const ImageLoadOptions &opts = load_image_options());
    /// Ask about unsaved edits, then close via close_image_immediately() once the prompt is answered
    void close_image(int index = -1);
    void close_all_images();
    void close_image_immediately(int index = -1);
    void close_all_images_immediately();
    void reload_image(ImagePtr image, bool shall_select = false);
    /// Put a copy of the current image -- or of the selection, if there is one -- beside it, and select it.
    /**
        Not an undoable edit: the history belongs to an image, so closing the copy is what takes it back.
    */
    void duplicate_image();

    /// Run \p action_name with \p group standing in for the selected one, then put the selection back.
    /**
        Pointing at a group is not selecting it: only the operation is told which group was meant.
    */
    void invoke_action_on_group(const std::string &action_name, int image_index, int group);
    /// Point at \p group of image \p image_index for the duration of \p body.
    /**
        While this is in force, the commands and the menu that offers them address that group instead of
        the current one.
    */
    void with_target_group(int image_index, int group, const std::function<void()> &body);
    /// The image an edit acts on: the one being pointed at, or the current one.
    ImagePtr target_image();
    /// The groups of \p img an edit acts on: the selected ones, or a group pointed at from outside them.
    /**
        Falls back to the group on screen for an image with nothing selected.
    */
    std::vector<int> target_groups(const ConstImagePtr &img) const;
    /// Put \p img into the list just after the current image, named \p partname, and select it.
    void add_image_beside_current(ImagePtr img, const std::string &partname);
    /// Whether `image` came from somewhere reload_image() could read it again.
    /**
        Answers from how the image was loaded, never from the filesystem: it gates a shortcut, so it is
        evaluated every frame.
    */
    bool can_reload(const ConstImagePtr &image) const;
    /// The background loader, which also owns the watched-directory and recent-file lists.
    BackgroundImageLoader       &image_loader() { return m_image_loader; }
    const BackgroundImageLoader &image_loader() const { return m_image_loader; }

    //-----------------------------------------------------------------------------
    // saving/loading an entire session (loaded images, current/reference selection, blend mode,
    // and view/display settings) to/from a user-chosen .hsess file
    //-----------------------------------------------------------------------------
    void save_session();
    void load_session();
    void load_session(const string &filename);
    /// The "HDRView session" manifest for the images and view settings as they stand.
    /**
        \p path_of supplies each image's "path": filesystem-relative for a plain .hsess, an in-bundle
        location for a zip export.
    */
    json build_session_manifest(const std::function<string(ConstImagePtr)> &path_of) const;
    void export_session_bundle();
    // Emscripten's "Load session..." entry point: uploads a .zip and loads it strictly as a session bundle,
    // erroring if it contains no manifest.
    void open_session_bundle();
    // Looks for a session manifest ("*.hsess") at the root of `zip_bytes` (a zip archive named `zip_name`).
    // If found, loads it as a session and returns true; false lets the caller fall back to treating the zip
    // as a plain multi-image archive.
    bool try_load_zip_as_session(string_view zip_bytes, const string &zip_name);
    //-----------------------------------------------------------------------------

    //-----------------------------------------------------------------------------
    // running work on the main thread
    //-----------------------------------------------------------------------------
    /// Run \p f on the main thread at the start of the next frame, before the GUI is drawn.
    /**
        Safe to call from any thread; keep \p f short, since it runs inline in the frame.
    */
    void post_to_main_thread(std::function<void()> f);

    /// Nudge the frame loop into drawing, from any thread.
    /**
        The runner otherwise idles on window events, which work arriving over a socket does not produce.
    */
    void wake_event_loop();
    //-----------------------------------------------------------------------------

#if HDRVIEW_ENABLE_IPC
    //-----------------------------------------------------------------------------
    // receiving live images from a renderer (see src/app-ipc.cpp and src/ipc/)
    //-----------------------------------------------------------------------------
    /// Begin accepting connections on 127.0.0.1:`port`. False if the port could not be bound.
    bool start_ipc_listening(uint16_t port);
    void stop_ipc_listening();
    /// Start or stop listening, and settle `m_ipc_listen_requested` on whatever happened.
    void set_ipc_listening(bool listen);
    /// Change the port, rebinding if already listening.
    void set_ipc_port(uint16_t port);
    /// The port the GUI toggle and the CLI flag listen on. Settable so a second HDRView can coexist.
    uint16_t  ipc_port() const { return m_ipc_port; }
    uint16_t &ipc_port() { return m_ipc_port; }
    /// Index of the image whose `filename` is `name`, or -1. How the protocol identifies images.
    int image_index_by_name(std::string_view name) const;
    /// The listener's controls and status, drawn above the watched-folder list.
    void draw_ipc_gui();
    //-----------------------------------------------------------------------------
#endif

    //-----------------------------------------------------------------------------
    // editing images (see src/app-edit.cpp)
    //-----------------------------------------------------------------------------
    /// What an edit command runs against: \p img, or the image being pointed at when none is given.
    EditContext edit_context(ImagePtr img = nullptr);

    /// Resample every channel of \p img into a new \p size, replacing the image as one undoable step.
    /**
        On a worker behind a cancellable progress bar, since a resampler costs seconds a channel; a resize
        fast enough to run synchronously goes through Image::resample() instead.
    */
    void resample_image_async(const ImagePtr &img, const std::string &name, int2 size, const ChannelResampler &op);

    /// Filter the subject's channels of \p img, likewise on a worker, as one undoable step.
    /**
        \p filter produces one rectangle but is handed the whole channel, since its kernel reads past the
        rectangle's edges, and which of the subject's channels it is -- 0 for the first, so a group's R, G,
        B, A arrive as 0, 1, 2, 3. Returns having only started the work; a canceled filter changes nothing.
    */
    void modify_channels_async(const ImagePtr &img, const std::string &name, const EditSubject &subject,
                               const ChannelFilter &filter);

    /// Draws the progress bar for a filter started by modify_channels_async(), and its Cancel button.
    void draw_filter_progress_dialog(bool &open);

    /// Run \p cmd, or open its dialog when it has one. The single path by which an edit command is invoked.
    void invoke_edit_command(EditCommand &cmd);
    /// Run \p cmd once per image it covers, each with its own undo entry; see edit_command_images().
    void apply_edit_command(EditCommand &cmd);
    /// The images one invocation of \p cmd covers.
    std::vector<ImagePtr> edit_command_images(const EditCommand &cmd);
    /// Whether \p cmd could run now: the image accepts edits, and the command itself is satisfied.
    bool edit_command_enabled(const EditCommand &cmd);
    /// The shell, the "Apply to" selector and the Cancel/Confirm footer that every command's dialog wears.
    void draw_edit_command_dialog(EditCommand &cmd, bool &open);

    /// Draws the "apply to" controls inline, for a dialog that carries them beside its own parameters.
    void draw_edit_subject_selector();

    /// The subject the menu's edits use, shown and changed under Edit > Apply to.
    EditSubject &edit_subject() { return m_edit_subject; }

    /// Whether the two scopes would name different channels for \p img; false for a single-group image.
    static bool scope_matters(const ConstImagePtr &img);

    /// Whether any open image has edits that are not in its file.
    bool any_image_modified() const;

    /// Reverse the most recent edit of every selected image, each stepping its own history.
    /**
        False if the current image had none, which is what draw_history_window()'s walk to a clicked entry
        terminates on.
    */
    bool undo();
    /// Reapply the edit undo() last reversed, across the selection. False if the current image had none.
    bool redo();

    /// What stepping \p img's history invalidates: the statistics cache and the GPU textures.
    /**
        An edit arriving through modify_image() invalidates the same things through EditContext::edited.
    */
    void after_modify(const ImagePtr &img);
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
    void          set_current_image_index(int index, bool force = false);
    void          set_reference_image_index(int index, bool force = false)
    {
        m_reference = force || is_valid(index) ? index : m_reference;
    }
    int next_visible_image_index(int index, Direction_ direction) const;
    int nth_visible_image_index(int n) const;
    //-----------------------------------------------------------------------------

    //-----------------------------------------------------------------------------
    // the multi-selection
    //-----------------------------------------------------------------------------
    /**
        A target is an (image, channel group) pair. Any number of them can be selected, but only one is
        current and only one is reference, and the current one is always also selected; an image counts as
        selected when any of its groups is. The flags live on Channel, read through
        Image::is_group_selected(). Every entry point below maintains that.
    */
    /// Every selected target, as (image index, group index) pairs, in the order the panel lists them.
    std::vector<std::pair<int, int>> selected_targets() const;
    /// Make group \p group of image \p index the current target.
    /**
        One that was already selected becomes the selection's current member; one that was not replaces the
        whole selection.
    */
    void set_current_group(int index, int group);
    /// Add group \p group of image \p index to the selection, or take it out.
    void toggle_group_selected(int index, int group);
    /// Select every image between the current one and \p index, each by the group it is showing.
    void select_image_range_to(int index);
    /// Select every target between the current one and (\p index, \p group), inclusive.
    void select_group_range_to(int index, int group);
    /// Every selected image, in list order; the current one alone when nothing is selected.
    std::vector<ImagePtr> selected_images();
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

    /// Mirrors a pixel coordinate about the current image's display window along the flipped axes.
    /**
        An involution, and the only place the flip enters the pixel <-> viewport transforms.
    */
    float2 flip_pixel(float2 pixel) const;

    /// Where the quad the image shader samples \p img over starts, as a fraction of the viewport.
    /**
        The viewport position of the image's data-window min corner, derived from vp_pos_at_pixel().
    */
    float2 image_position(ConstImagePtr img) const;
    /// The extent of that same quad, as a fraction of the viewport. Negative along a flipped axis.
    float2 image_scale(ConstImagePtr img) const;

    float  pixel_ratio() const;
    float2 viewport_size() const { return m_viewport_size; }
    bool   vp_pos_in_viewport(float2 vp_pos) const
    {
        return all(gequal(vp_pos, 0.f)) && all(less(vp_pos, m_viewport_size));
    }
    bool app_pos_in_viewport(float2 app_pos) const { return vp_pos_in_viewport(vp_pos_at_app_pos(app_pos)); }
    bool pixel_in_viewport(float2 pixel) const { return vp_pos_in_viewport(vp_pos_at_pixel(pixel)); }

    /// True when the mouse is over the image viewport and nothing else (a panel, a popup, ...) claims it.
    /**
        The geometric *_in_viewport() checks above don't account for occlusion by other windows.
    */
    bool mouse_over_viewport() const
    {
        return app_pos_in_viewport(ImGui::GetIO().MousePos) && !ImGui::GetIO().WantCaptureMouse;
    }

    /// The most recently hovered image pixel while the mouse was over the viewport.
    /**
        Held once the mouse moves elsewhere so hover-driven widgets stay interactable while it travels to
        them. nullopt only if the viewport has never been hovered this session.
    */
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

    /// Respond to a touch gesture the browser reported. \see install_touch_handlers() in platform_utils
    /**
        Scales the view by \p scale about the fingers while moving whatever was under \p from_app_pos to
        \p to_app_pos, so the image follows the fingers.

        \param num_touches  Fingers currently on the screen
        \param scale        The first two fingers' separation, as a ratio of what it just was
        \param from_app_pos Midpoint between them before this event
        \param to_app_pos   Midpoint between them after it

        Public because Emscripten's event API takes a plain function pointer.
    */
    void touch_gesture(int num_touches, float scale, float2 from_app_pos, float2 to_app_pos);
    /// Zoom in to the next power of two
    void zoom_in();
    /// Zoom out to the previous power of two
    void  zoom_out();
    float zoom_level() const;
    void  set_zoom_level(float l);

    /// The range m_zoom is kept within, outside of which the viewport transforms stop being invertible.
    /**
        A zoom of zero divides by zero in pixel_at_vp_pos(), and the non-finite pixel coordinate that
        results is undefined behavior to convert to int2.
    */
    static constexpr float MIN_ZOOM = 0.01f, MAX_ZOOM = 512.f;
    /// Sets the zoom factor, clamped to [MIN_ZOOM, MAX_ZOOM]. Every path that changes the zoom goes here.
    void set_zoom(float zoom);
    /// The zoom factor: image pixel size / logical pixel size. Always within [MIN_ZOOM, MAX_ZOOM].
    float zoom() const { return m_zoom; }
    //-----------------------------------------------------------------------------

    float4 pixel_value(int2 pixel, bool raw, int which_image) const;

    /// Applies the same exposure/tonemap/gamma pipeline as pixel_value(..., raw=false, ...).
    /**
        Takes an arbitrary linear value rather than a pixel lookup, e.g. to preview a computed statistic.
    */
    float4 tonemap_value(float4 value) const;

    // load font with the specified name at the specified size
    ImFont *font(const string &name) const;

    ImGui::Action &action(const string &name) { return m_actions[name]; }

    /// The tool the mouse is currently in.
    MouseMode mouse_mode() const { return m_mouse_mode; }
    /// Switch to the given tool.
    /**
        The only way to set the mode: it also updates the m_mouse_mode_enabled array the tool actions
        expose as their selected state.
    */
    void set_mouse_mode(MouseMode m);

    float      &gamma_live() { return m_gamma_live; }
    float      &gamma() { return m_gamma; }
    float      &exposure_live() { return m_exposure_live; }
    float      &exposure() { return m_exposure; }
    float      &offset_live() { return m_offset_live; }
    float      &offset() { return m_offset; }
    Tonemap_   &tonemap() { return m_tonemap; }
    Colormap_   colormap() { return m_colormaps[m_colormap_index]; }
    bool       &reverse_colormap() { return m_reverse_colormap; }
    BlendMode_ &blend_mode() { return m_blend_mode; }
    bool       &clamp_to_LDR() { return m_clamp_to_LDR; }
    bool       &dithering_on() { return m_dither; }
    bool       &draw_grid_on() { return m_draw_grid; }
    /// Whether the image list shows only the unique portion of each file name.
    bool      &short_names() { return m_short_names; }
    bool      &draw_pixel_info_on() { return m_draw_pixel_info; }
    AxisScale &histogram_x_scale() { return m_x_scale; }
    AxisScale &histogram_y_scale() { return m_y_scale; }
    float     &histogram_height() { return m_histogram_height; }
    Box2i     &roi_live() { return m_roi_live; }
    Box2i     &roi() { return m_roi; }
    /// The color the viewport draws behind the image, for the edits that composite against it.
    float4 background_color() const { return m_bg_color; }

    /// What cut or copy last took; null until one of them has run. Paste writes it back.
    ConstImagePtr clipboard() const { return m_clipboard; }
    void          set_clipboard(ImagePtr img) { m_clipboard = std::move(img); }

    /// Set the selection, both the committed rectangle and the one drawn over the viewport.
    /**
        The two exist apart only so a drag can update what is drawn each frame and commit once on release.
    */
    void    set_selection(const Box2i &box) { m_roi = m_roi_live = box; }
    bool2  &clip_warnings() { return m_clip_warnings; }
    float2 &clip_range() { return m_clip_range; }

    /// Height of the Pixel statistics histogram plot, in em, when it has never been resized
    static constexpr float default_histogram_height = 9.f;

    /// Display peak as a multiple of SDR white (1 = no headroom, 8 = three stops of it).
    /**
        Re-read per frame: dimming the display lowers SDR white and so raises this, and on Wayland the real
        value arrives a moment after startup. 0 means the display has not told us, not "no headroom".
    */
    float display_headroom() const;

private:
    //-----------------------------------------------------------------------------
    // Subsystem state, defined in the .cpp file that owns it
    //-----------------------------------------------------------------------------
    struct UnconfirmedSession; // app-file-io.cpp
    struct LoadingSession;     // app-file-io.cpp
    struct RunningFilter;      // app-edit.cpp
#if HDRVIEW_ENABLE_IPC
    struct IpcRates; // app-ipc.cpp
#endif

    void load_fonts();

    // Begins asynchronously loading the images `load` lists, populating m_loading_session; the rest of the
    // session is applied by finish_loading_session() once every image has arrived.
    void begin_session_load(const UnconfirmedSession &load);
    // Matches an image that has just finished loading to the session entry that asked for it.
    void resolve_loading_session_image(const ImagePtr &new_image);
    // Called once every image in m_loading_session has been resolved (successfully or not); rebuilds m_images
    // in the saved order, then applies current/reference selection, blend mode, and view settings.
    void finish_loading_session();

    void   handle_mouse_interaction();
    void   calculate_viewport();
    float2 center_offset() const;
    Box2f  scaled_display_window(ConstImagePtr img) const;

    void draw_background();
    void draw_statistics_window();
    void draw_history_window();
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
    /// Draws the current and reference images' vector overlays, if either has one.
    void draw_vector_overlays() const;
    void draw_file_window();
    void draw_top_toolbar();
    void draw_tool_palette();
    void draw_menus();
    void draw_status_bar();
    void draw_developer_windows();
    void draw_tweak_window();
    void process_shortcuts();
    void set_image_textures();
    void update_visibility();

    void setup_rendering();
    /// Re-query the display's color space and (re)create colorpass resources if it now needs them.
    /**
        Called once per frame, before either colorpass half runs.
    */
    void update_colorpass();
    void begin_colorpass_frame();
    void end_colorpass_frame();
    void cleanup_colorpass();

    /// True when this display can show more than standard dynamic range.
    /**
        That is, when the HDR-only UI (the "Clamp to LDR" control) is meaningful. Combines the framebuffer
        we were granted with what the display reports about itself.
    */
    bool supports_hdr() const;

    void pixel_color_widget(const int2 &pixel, int &color_mode, int which_image, bool allow_copy = false,
                            float width = 0.f, const string &trailing_label = {},
                            const std::function<void()> &extra_popup_items = {}) const;

private:
    //-----------------------------------------------------------------------------
    // Constructor setup: the phases called in order from HDRViewApp(), and what they hand each other.
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

    /// What a "Discard unsaved changes?" prompt should do once the user says yes.
    /**
        The modal answers a frame or more later, so what was being asked about has to be remembered.
    */
    enum class PendingDiscard
    {
        None,
        CloseImage,
        CloseAll,
        Quit
    };
    PendingDiscard m_pending_discard     = PendingDiscard::None;
    int            m_pending_close_index = -1;
    void           draw_confirm_discard_dialog(bool &open);
    /// Do the thing the prompt was asking about, now that it has been confirmed.
    void apply_pending_discard();

    /// The filter running off the main thread, if any; see start_filter().
    std::shared_ptr<RunningFilter> m_running_filter;

    /// Filters waiting for the one in flight; only one runs at a time (one progress dialog, one Cancel).
    /**
        An edit over a multi-selection arrives as one call per image in a single frame.
    */
    std::vector<std::function<void()>> m_filter_queue;

    /// Take \p running as the filter in flight and set it going.
    /**
        On a worker with a progress dialog, or inline where there are no threads.
    */
    void start_filter(std::shared_ptr<RunningFilter> running);
    /// Applies a finished filter's results, or clears an abandoned one. Called once a frame.
    void drain_running_filter();
    /// Starts the next queued filter, if a fan-out left any waiting.
    void start_next_filter();

    /// The shared body of undo() and redo().
    bool step_selected_histories(bool forward);

    /// What the menu's edits apply to; see Edit > Apply to.
    EditSubject m_edit_subject;

    /// What cut or copy last took, and paste writes back. One for the application, as a clipboard is.
    ImagePtr m_clipboard;

    /// Every edit command, built once at startup; see edit/commands.h.
    /**
        Owns each command's parameters, so a dialog reopens with the settings it was left with.
    */
    vector<EditCommandPtr> m_edit_commands;

    BackgroundImageLoader m_image_loader;

    /// Work posted from other threads by post_to_main_thread(), drained once per frame.
    std::mutex                         m_main_thread_mutex;
    std::vector<std::function<void()>> m_main_thread_queue;

    /// The group the Images panel's context menu is pointing at (-1 when none), and the image it belongs to.
    /**
        A group index means nothing on its own, so the two are only ever set and read together.
    */
    ImagePtr m_target_image_override;
    int      m_target_group_override = -1;
    void     drain_main_thread_queue();

#if HDRVIEW_ENABLE_IPC
    IpcServer m_ipc_server;
    uint16_t  m_ipc_port = k_default_ipc_port;

    /// p_selected for the "Listen for image updates" toggle; the truth is whether the socket is bound.
    /**
        Refreshed from is_listening() every frame, since binding can fail and --listen can turn it on at
        startup.
    */
    bool m_ipc_listen_requested = false;

    /// The rates the activity readout shows; created on first use by draw_ipc_gui().
    std::shared_ptr<IpcRates> m_ipc_rates;

    // Each applies one already-decoded packet, on the main thread. Decoding happens on the receive thread;
    // see start_ipc_listening().
    void apply_ipc_open(const IpcOpenImage &info);
    void apply_ipc_reload(const IpcReloadImage &info);
    void apply_ipc_close(const IpcCloseImage &info);
    void apply_ipc_create(const IpcCreateImage &info);
    void apply_ipc_update(const IpcUpdateImage &info);
    void apply_ipc_vector_graphics(const IpcVectorGraphics &info);
#endif

    int m_remaining_download = 0;

    /// Number of fingers on the screen, tracked only under Emscripten.
    /**
        The GLFW port synthesizes mouse events from the first touch and drops the rest ("we don't handle
        multitouch", Context.cpp), so during a pinch it still reports a left-drag; panning is suppressed
        while this exceeds one.
    */
    int m_active_touches = 0;

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
    // All null/inert unless the display needs color management. See app-colorpass.cpp.
    //-----------------------------------------------------------------------------
    std::unique_ptr<Texture>    m_color_texture;
    std::unique_ptr<RenderPass> m_colorpass_pass;
    std::unique_ptr<RenderPass> m_resolve_pass;
    std::unique_ptr<Shader>     m_colorpass_shader;
    /// The color space the display currently wants, re-queried once per frame by update_colorpass().
    /**
        Moving the window between monitors or toggling the OS's HDR mode then takes effect live.
    */
    DisplayColorSpace m_display_cs;
    /// Rec.709 -> m_display_cs.chroma, recomputed only when m_display_cs changes.
    float3x3 m_gamut_matrix{la::identity};
    /// True when the display needs the colorpass to convert away from HDRView's extended-sRGB convention.
    /**
        E.g. Windows/Linux scRGB or Linux/Wayland PQ. Only ever true on the OpenGL/GLFW backend.
    */
    bool m_color_managed = false;
    /// True once we've given up on the colorpass (shader or FBO creation failed), so we don't retry forever.
    bool m_colorpass_failed = false;
    /// True when the float framebuffer we asked for was granted.
    /**
        hello_imgui clears the request when it can't be satisfied. Drives the HDR-related UI, replacing
        hasEdrSupport() off of macOS.
    */
    bool m_float_buffer = false;
    /// The --sdr command-line flag: behave as if the display were plain SDR.
    /**
        Checked by supports_hdr(), so it also hides the HDR-only UI.
    */
    bool m_force_sdr = false;

    //-----------------------------------------------------------------------------
    // Exposure/gamma/tonemap and viewport overlay display flags
    //-----------------------------------------------------------------------------
    float m_exposure = 0.f, m_exposure_live = 0.f, m_offset = 0.f, m_offset_live = 0.f, m_gamma = 1.0f,
          m_gamma_live  = 1.0f;
    AxisScale m_x_scale = AxisScale_Asinh, m_y_scale = AxisScale_Linear;
    float     m_histogram_height = default_histogram_height;

    bool m_clamp_to_LDR = false, m_dither = true, m_draw_grid = true, m_draw_pixel_info = true,
         m_draw_watched_pixels = true, m_draw_data_window = true, m_draw_display_window = true, m_show_FPS = false,
         m_draw_annotations = true;
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

    /// Index of the annotation being worked on, or -1 when none on the current image is.
    int  active_annotation() const;
    void set_active_annotation(int index);
    /// Everything a drag was doing is undone, and the annotation put back as it was.
    void cancel_annotation_drag();
    /// Create, move and resize annotations; called from handle_mouse_interaction() for the annotate tool.
    void handle_annotate_tool();
    /// Where the image is on screen and how big, as both the overlay and hit testing need it.
    VgTransform viewport_transform() const;

    MouseMode m_mouse_mode = MouseMode_PanZoom;
    /// Selected state of each tool action, kept in sync with m_mouse_mode by set_mouse_mode().
    /**
        The tool actions point their Action::p_selected at these.
    */
    bool m_mouse_mode_enabled[MouseMode_COUNT] = {true, false, false, false};

    /// Which shape the annotate tool draws, and the look every new annotation starts with.
    Annotation::Shape m_annotation_shape = Annotation::Shape::Rect;
    Annotation        m_annotation_style;

    /// The annotation being worked on, and the image whose list that index refers to.
    /**
        The two travel together because an index alone means nothing once the current image changes;
        active_annotation() hands back -1 unless they still agree. Called active rather than selected,
        which in this class already means the rectangular ROI and the channel-group selection both.
    */
    ImagePtr m_active_annotation_on;
    int      m_active_annotation = -1;

    /// What a drag with the annotate tool is doing to the active annotation.
    enum class AnnotationDrag
    {
        None,
        Creating,
        Moving,
        Resizing
    };
    AnnotationDrag m_annotation_drag        = AnnotationDrag::None;
    int            m_annotation_drag_handle = -1;
    Annotation     m_annotation_drag_start; ///< As it was at mouse-down, so Escape can put it back
    float2         m_annotation_grab{0.f};  ///< Where in the image the drag began, for a move

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
        int3 color_mode{0, 0, 0}; ///< Color mode for current, reference, and composite pixels
    };
    vector<WatchedPixel> m_watched_pixels;
    int                  m_status_color_mode = 0;
    /// Which pixel the status bar reports: 0 current, 1 reference, 2 composite.
    /**
        The `which_image` argument pixel_color_widget() takes. Persisted, unlike the value format beside it.
    */
    int m_status_pixel_target = 2;

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
    // Floating tool palette (see draw_tool_palette())
    //-----------------------------------------------------------------------------
    bool m_show_tool_palette      = true;  ///< Whether the palette is drawn over the viewport at all
    bool m_tool_palette_collapsed = false; ///< Folded down to a single button showing the active tool
    bool m_tool_palette_vertical  = true;  ///< Lay the buttons out as a column rather than a row
    int  m_tool_palette_corner    = 0;     ///< Viewport corner it is anchored to: 0=TL, 1=TR, 2=BL, 3=BR
    bool m_tool_palette_dragging  = false; ///< The user is dragging it, so don't re-anchor it this frame

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
    // Linear search by title; only ever called from a user-triggered action, never per-frame.
    PopupDialog &dialog(const string &title);

    /// A parsed session file waiting on the user to confirm closing the currently-open images.
    std::shared_ptr<UnconfirmedSession> m_unconfirmed_session;

    /// The session whose images are loading asynchronously; see begin_session_load().
    std::shared_ptr<LoadingSession> m_loading_session;
};

/// Create the global singleton HDRViewApp instance
void init_hdrview(optional<float> exposure, optional<float> gamma, optional<bool> dither, optional<bool> force_sdr,
                  optional<bool> apple_keys, const vector<string> &in_files = {});

/// Return a pointer to the global singleton HDRViewApp instance
HDRViewApp *hdrview();
