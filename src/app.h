/** \file app.h
    \author Wojciech Jarosz
*/

#pragma once

#include "box.h"
#include "colormap.h"
#include "display_colorspace.h"
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

/// Name of the action that switches to the given tool. This is the only mapping from MouseMode_ to the
/// action registry, so the Tools menu, the tool palette, and the tests can all be driven by the enum.
const char *mouse_mode_action_name(MouseMode m);

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
    /// The buffer a screenshot should be read back from, or nullptr when that is the window's own
    /// framebuffer. Only the colorpass's offscreen target still holds HDRView's extended sRGB; the window
    /// holds display-referred values, which a PNG cannot represent. See app-windows.cpp.
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
    //! Download `url` and load it as an image (Emscripten only). `to_replace` reloads in place, as reload_image() does.
    void load_url(string_view url, bool should_select = true, ImagePtr to_replace = nullptr,
                  const ImageLoadOptions &opts = load_image_options());
    //! Close an image, first asking about edits that are not in any file.
    /*!
        The prompt is answered a frame or more later, so these return having only opened it; the
        *_immediately() variants below are what actually closes, and are also the path for an image with
        nothing to lose.
    */
    void close_image(int index = -1);
    void close_all_images();
    void close_image_immediately(int index = -1);
    void close_all_images_immediately();
    void reload_image(ImagePtr image, bool shall_select = false);
    //! Whether `image` came from somewhere reload_image() could read it again.
    /*!
        Answers from how the image was loaded, never from the filesystem: this gates a keyboard shortcut, so
        it is evaluated every frame. A file that has since been deleted still answers yes, and the reload
        reports it.
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
    // running work on the main thread
    //-----------------------------------------------------------------------------
    /*!
        Queue \p f to run on the main thread near the start of the next frame.

        The image list, the images themselves, and the graphics API are all main-thread-only, so anything
        arriving on another thread -- pixels pushed in by a renderer, say -- has to come through here rather
        than touch them directly. Safe to call from any thread, including the main one.

        Queued work runs before the frame's GUI is drawn, so a batch of it lands atomically as far as the
        drawn frame is concerned. Keep each callable short: it runs inline on the frame.
    */
    void post_to_main_thread(std::function<void()> f);

    /*!
        Nudge the frame loop into drawing, from any thread.

        The runner idles by waiting on window events, so when nothing is moving on screen a frame can be up
        to the idle timeout away. Work that arrives from outside the window system -- pixels pushed in over
        a socket -- produces no such event, and without this would be drawn whenever the next frame happened
        to come around.
    */
    void wake_event_loop();
    //-----------------------------------------------------------------------------

#if HDRVIEW_ENABLE_IPC
    //-----------------------------------------------------------------------------
    // receiving live images from a renderer (see src/app-ipc.cpp and src/ipc/)
    //-----------------------------------------------------------------------------
    /// Begin accepting connections on 127.0.0.1:`port`. False if the port could not be bound.
    bool             start_ipc_listening(uint16_t port);
    void             stop_ipc_listening();
    const IpcServer &ipc_server() const { return m_ipc_server; }
    /// Start or stop listening, and settle `m_ipc_listen_requested` on whatever actually happened.
    void set_ipc_listening(bool listen);
    /// Change the port, rebinding if already listening.
    void set_ipc_port(uint16_t port);
    /// The port the GUI toggle and the CLI flag listen on. Settable so a second HDRView can coexist.
    uint16_t  ipc_port() const { return m_ipc_port; }
    uint16_t &ipc_port() { return m_ipc_port; }
    /// Index of the image whose `filename` is `name`, or -1. How the protocol identifies images.
    int image_index_by_name(std::string_view name) const;
    /// The listener's controls and status, drawn above the watched-folder list they sit alongside.
    void draw_ipc_gui();
    //-----------------------------------------------------------------------------
#endif

    //-----------------------------------------------------------------------------
    // editing images (see src/app-edit.cpp)
    //-----------------------------------------------------------------------------
    /*!
        Apply one edit to \p img and record how to reverse it. The only thing that writes image pixels.

        Everything an edit has to get right besides the pixels themselves lives here: refusing images
        that cannot be edited, stopping the statistics tasks that are reading the samples, invalidating
        the caches keyed on them, and pushing the undo entry. An operation that went around this would
        leave a histogram describing pixels that are gone and no way back to them, so nothing else may
        write pixels.

        \param [] img       Image to change
        \param [] name      Shown beside "Undo"/"Redo", e.g. "Rotate 90 CW"
        \param [] op        Makes the change
        \param [] make_undo Builds the entry that reverses it, called *before* `op` so it can capture
                            whatever `op` is about to overwrite
        \returns Whether the edit was applied; false when the image refuses edits (see can_edit()).
    */
    bool modify_image(const ImagePtr &img, const std::string &name, const std::function<void(Image &)> &op,
                      const std::function<UndoPtr(const Image &)> &make_undo);

    /*!
        A geometric edit, which is reversed by performing its opposite rather than by storing pixels.

        \p forward and \p backward must be exact inverses of each other -- flips and quarter turns are,
        since every sample survives them.
    */
    bool modify_image_reversibly(const ImagePtr &img, const std::string &name,
                                 const std::function<void(Image &)> &forward,
                                 const std::function<void(Image &)> &backward);

    /*!
        Whether \p img accepts edits at all.

        False for an image a renderer is still streaming into: its pixels are owned by the other process,
        so an edit would be overwritten by the next tile and undoing one would restore samples that have
        since been replaced.
    */
    static bool can_edit(const ConstImagePtr &img);

    /*!
        The channels \p subject names, and the rectangle of them it covers, in image coordinates.

        The rectangle is the data window, narrowed to the selection when the subject asks for that and
        there is one. An empty channel list or a rectangle with no volume means there is nothing to edit.
    */
    std::pair<std::vector<int>, Box2i> resolve_subject(const ConstImagePtr &img, const EditSubject &subject) const;

    /*!
        Apply \p op to every sample the subject covers, as one undoable edit.

        \p op is handed a sample, its position in image coordinates, and which of the subject's channels
        it belongs to -- 0 for the first, so a group's R, G, B, A arrive as 0, 1, 2, 3 -- and returns what
        to replace it with. That last one is what lets an edit differ per component, as filling with a
        color does. The samples it writes go to the GPU as the one rectangle they occupy, and the entry that
        reverses them stores that same rectangle -- so an edit confined to a selection costs the selection,
        not the image.

        \returns Whether anything was edited; false for an image that refuses edits or a subject that
                 names nothing.
    */
    bool modify_pixels(const ImagePtr &img, const std::string &name, const EditSubject &subject,
                       const std::function<float(float, int2, int)> &op);

    /*!
        Apply an edit that changes the image's shape, as one undoable step.

        For crop, canvas resize, and anything else that changes how many samples there are or how many
        channels hold them. The whole channel list is saved rather than a rectangle of it, since there is
        no rectangle of the old samples that describes the new ones.

        Rebuilds the layer tree afterwards and refits the view, which a shape change invalidates.
    */
    bool modify_structure(const ImagePtr &img, const std::string &name, const std::function<void(Image &)> &op);

    /*!
        Apply a neighborhood filter to the subject's channels, as one undoable edit.

        \p filter is handed a whole channel and the rectangle of it to produce, in channel-local
        coordinates, and returns an array of just that rectangle. The two are separate because a filter
        reads the samples around each one it writes: those outside a selection are real samples and belong
        in the answer, so it must see the whole channel -- but it only has to *compute* the rectangle, and
        only has to read as far past it as its kernel reaches. Filtering the whole channel and keeping the
        middle would make a small selection cost the whole image.
    */
    bool modify_channels(const ImagePtr &img, const std::string &name, const EditSubject &subject,
                         const std::function<Array2Df(const Array2Df &, const Box2i &)> &filter);

    /*!
        As modify_channels(), but computed off the main thread with a progress bar that can cancel it.

        For filters slow enough that running them inline would freeze the window with no way out. The
        filtering happens on a worker; the image is only touched once every channel is done, back on the
        main thread and through the same chokepoint, so the edit still lands as one undoable step.

        Returns having only started the work. A canceled filter changes nothing at all -- its partial
        result is discarded rather than applied, since a half-filtered image is not a state anyone asked
        for.
    */
    /*!
        Replace the image wholesale with something computed from it, off the main thread.

        For the environment-map operations, which resample every channel into a new size rather than
        writing back into the one they read -- so there is no rectangle to record and the whole channel
        list is saved instead.

        \p op returns the new samples for one channel at \p size; every channel is put through it and the
        results swapped in together, back on the main thread, as one undoable step.
    */
    void modify_image_async(const ImagePtr &img, const std::string &name, int2 size,
                            const std::function<Array2Df(const Array2Df &, FilterProgress)> &op);

    void modify_channels_async(const ImagePtr &img, const std::string &name, const EditSubject &subject,
                               const std::function<Array2Df(const Array2Df &, const Box2i &, FilterProgress)> &filter);

    //! Draws the progress bar for a filter started by modify_channels_async(), and its Cancel button.
    void draw_filter_progress_dialog(bool &open);

    /// Draws the "apply to" controls inline, for a dialog that carries them beside its own parameters.
    void draw_edit_subject_selector();

    // The parameterized point edits. Each applies on confirm rather than as its controls move; see
    // draw_exposure_gamma_dialog().
    void draw_exposure_gamma_dialog(bool &open);
    void draw_brightness_contrast_dialog(bool &open);
    void draw_fill_dialog(bool &open);
    void draw_canvas_size_dialog(bool &open);
    void draw_image_size_dialog(bool &open);
    void draw_blur_dialog(bool &open);
    void draw_unsharp_mask_dialog(bool &open);
    void draw_median_dialog(bool &open);
    void draw_zap_gremlins_dialog(bool &open);
    void draw_remap_dialog(bool &open);
    void draw_irradiance_dialog(bool &open);

    /// The subject the menu's edits use, shown and changed under Edit > Apply to.
    EditSubject &edit_subject() { return m_edit_subject; }

    /// Whether the two scopes would name different channels for \p img; false for a single-group image.
    static bool scope_matters(const ConstImagePtr &img);

    /// Whether any open image has edits that are not in its file.
    bool any_image_modified() const;

    /// Reverse the current image's most recent edit. False if there was nothing to undo.
    bool undo();
    /// Reapply the edit undo() last reversed. False if there was nothing to redo.
    bool redo();

    //! Everything a completed edit invalidates, applied to \p img.
    /*!
        The derived data an edit makes stale -- the statistics cache, the GPU textures, and, when the
        channels themselves changed, the layer and group tree built from them. Kept in one place so an
        edit and an undo of that same edit cannot invalidate different things.
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

    /// Mirrors a pixel coordinate about the current image's display window along the flipped axes; an
    /// involution, and the only place the flip enters the pixel <-> viewport transforms.
    float2 flip_pixel(float2 pixel) const;

    /// Where the quad the image shader samples \p img over starts, as a fraction of the viewport: the
    /// viewport position of the image's data-window min corner. Derived from vp_pos_at_pixel(), so what is
    /// drawn and what the overlays and pixel readouts report can never describe different places.
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

    //! Respond to a touch gesture the browser reported. \see install_touch_handlers() in platform_utils
    /*!
        Scales the view by \p scale about the fingers while moving whatever was under \p from_app_pos to
        \p to_app_pos, so the image follows the fingers: two fingers moving together pan, and spreading
        them apart magnifies by exactly the ratio they spread by.

        \param [] num_touches  Fingers currently on the screen
        \param [] scale        The first two fingers' separation, as a ratio of what it just was
        \param [] from_app_pos Midpoint between them before this event
        \param [] to_app_pos   Midpoint between them after it

        Public because Emscripten's event API takes a plain function pointer, so the listener that calls
        this cannot be a member.
    */
    void touch_gesture(int num_touches, float scale, float2 from_app_pos, float2 to_app_pos);
    /// Zoom in to the next power of two
    void zoom_in();
    /// Zoom out to the previous power of two
    void  zoom_out();
    float zoom_level() const;
    void  set_zoom_level(float l);

    /// The range m_zoom is kept within. Outside it the viewport transforms stop being invertible: a zoom of
    /// zero divides by zero in pixel_at_vp_pos(), and the resulting non-finite pixel coordinate is
    /// undefined behavior to convert to the int2 the selection and pixel inspector need.
    static constexpr float MIN_ZOOM = 0.01f, MAX_ZOOM = 512.f;
    /// Sets the zoom factor, clamped to [MIN_ZOOM, MAX_ZOOM]. Every path that changes the zoom goes
    /// through here, including the ones restoring it from a session file.
    void set_zoom(float zoom);
    /// The zoom factor: image pixel size / logical pixel size. Always within [MIN_ZOOM, MAX_ZOOM].
    float zoom() const { return m_zoom; }

    /// The ranges the exposure/offset/gamma sliders offer. These bound the sliders' drag travel only:
    /// Ctrl+click text entry deliberately goes out of bounds (no ImGuiSliderFlags_ClampOnInput), and the
    /// exposure/gamma keyboard shortcuts step past them, so values outside these are reachable and are
    /// kept as given.
    static constexpr float2 EXPOSURE_RANGE{-9.f, 9.f}, OFFSET_RANGE{-1.f, 1.f}, GAMMA_RANGE{0.02f, 9.f};

    /// Smallest gamma that still describes a power curve. It is inverted before use, so zero divides by
    /// zero and a negative value sends a black pixel to infinity; nothing above this needs a bound.
    static constexpr float MIN_GAMMA = 1e-4f;
    //-----------------------------------------------------------------------------

    float4 pixel_value(int2 pixel, bool raw, int which_image) const;

    //! Applies the same exposure/tonemap/gamma pipeline as pixel_value(..., raw=false, ...), for an
    //! arbitrary linear value rather than a pixel lookup (e.g. to preview a computed statistic).
    float4 tonemap_value(float4 value) const;

    // load font with the specified name at the specified size
    ImFont *font(const string &name) const;

    ImGui::Action &action(const string &name) { return m_actions[name]; }

    /// The tool the mouse is currently in.
    MouseMode mouse_mode() const { return m_mouse_mode; }
    /// Switch to the given tool. The tools are mutually exclusive, so this is the only way to set the
    /// mode: it also updates the whole m_mouse_mode_enabled array the tool actions expose as their
    /// selected state.
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
    //! Whether the image list shows only the unique portion of each file name.
    bool      &short_names() { return m_short_names; }
    bool      &draw_pixel_info_on() { return m_draw_pixel_info; }
    AxisScale &histogram_x_scale() { return m_x_scale; }
    AxisScale &histogram_y_scale() { return m_y_scale; }
    float     &histogram_height() { return m_histogram_height; }
    Box2i     &roi_live() { return m_roi_live; }
    Box2i     &roi() { return m_roi; }

    //! Set the selection, both the committed rectangle and the one drawn over the viewport.
    /*!
        The two exist apart only so that a drag can update what is drawn on every frame and commit once on
        release. Everywhere else they move together: a selection that has been cleared but is still drawn,
        or drawn but not acted on, is a bug rather than a state worth having.
    */
    void    set_selection(const Box2i &box) { m_roi = m_roi_live = box; }
    bool2  &clip_warnings() { return m_clip_warnings; }
    float2 &clip_range() { return m_clip_range; }

    /// Height of the Pixel statistics histogram plot, in em, when it has never been resized
    static constexpr float default_histogram_height = 9.f;

    /// How much brighter than SDR reference white this display can currently go, as a multiple of it.
    /*!
        HDRView renders in extended sRGB, where 1.0 *is* the display's SDR reference white, so this is
        also the largest value the display can show: 1 means no headroom, 8 means three stops of it.

        Not a fixed property of the panel. It is a ratio of the display's ceiling to its SDR white, and
        it is the denominator that moves: dimming the display lowers SDR white while the panel's peak
        stays put, so headroom *grows*. Read it per frame rather than caching it -- on Wayland the real
        values also arrive a moment after startup, so an early read sees the placeholder below.

        Returns 0 when the display has not told us, which is *not* the same as "no headroom": callers
        should omit headroom-dependent UI in that case rather than treating it as SDR.
    */
    float display_headroom() const;

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
                            float width = 0.f, const string &trailing_label = {},
                            const std::function<void()> &extra_popup_items = {}) const;

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

    //! What a "Discard unsaved changes?" prompt should do once the user says yes.
    /*!
        Closing an image or quitting throws away edits that are not in any file, so both ask first. The
        answer arrives a frame or more later, from the modal, which is why what was being asked about has
        to be remembered rather than acted on in place.
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
    //! Do the thing the prompt was asking about, now that it has been confirmed.
    void apply_pending_discard();

    //! A filter running off the main thread, with what is needed to finish or abandon it.
    /*!
        Held for as long as the work runs. The worker writes only into `results` and `progress`; the main
        thread reads `done` once a frame and applies the results, so nothing is shared mutably in both
        directions.
    */
    struct RunningFilter
    {
        ImagePtr              image;
        std::string           name;
        std::vector<int>      channels;
        Box2i                 bounds;
        std::vector<Array2Df> results;
        FilterProgress        progress{true};
        std::atomic<bool>     done{false};
    };
    //! Set when the running work replaces the image rather than a rectangle of it; see
    //! modify_image_async(). drain_running_filter() then swaps the channels instead of uploading tiles.
    bool m_running_filter_resizes = false;
    int2 m_running_filter_size{0};

    std::unique_ptr<RunningFilter> m_running_filter;
    //! Applies a finished filter's results, or clears an abandoned one. Called once a frame.
    void drain_running_filter();

    //! What the menu's edits apply to; see Edit > Apply to.
    EditSubject m_edit_subject;

    BackgroundImageLoader m_image_loader;

    //! Work posted from other threads by post_to_main_thread(), drained once per frame.
    std::mutex                         m_main_thread_mutex;
    std::vector<std::function<void()>> m_main_thread_queue;
    void                               drain_main_thread_queue();

#if HDRVIEW_ENABLE_IPC
    IpcServer m_ipc_server;
    uint16_t  m_ipc_port = k_default_ipc_port;

    //! What the "Listen for image updates" toggle shows, mirrored from the server once per frame.
    /*!
        An Action needs a bool to point p_selected at, but the truth is whether the socket is bound -- which
        the toggle does not decide on its own, since binding can fail and --listen can turn it on at
        startup. Mirroring it every frame (rather than in draw_ipc_gui, which does not run while the panel
        is collapsed) keeps the palette's checkmark honest.
    */
    bool m_ipc_listen_requested = false;

    //! Turns the listener's running totals into the rates the activity readout shows.
    /*!
        Sampled over a window rather than per frame: at 60 fps the per-frame deltas are one or two packets
        and the number would be unreadable noise.
    */
    struct IpcRates
    {
        double   sampled_at    = 0.0; //!< ImGui::GetTime() of the last sample
        uint64_t last_packets  = 0;
        uint64_t last_bytes    = 0;
        double   packets_per_s = 0.0;
        double   bytes_per_s   = 0.0;

        void update(const IpcActivity &now, double time);
    };
    IpcRates m_ipc_rates;

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

    //! Number of fingers currently on the screen, tracked only under Emscripten.
    /*!
        The GLFW port synthesizes mouse events from the first touch and drops the rest ("we don't handle
        multitouch", Context.cpp), so during a two-finger pinch it is still reporting a left-drag. Panning
        is suppressed while this exceeds one, or the image would pan and zoom at the same time.
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
    /// Selected state of each tool action, kept in sync with m_mouse_mode by set_mouse_mode(). The tool
    /// actions point their Action::p_selected at these, which is what draws the checkmark in the Tools
    /// menu and the pressed look in the tool palette.
    bool m_mouse_mode_enabled[MouseMode_COUNT] = {true, false, false};

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
    //! Which pixel the status bar reports: 0 current, 1 reference, 2 composite -- the `which_image`
    //! argument pixel_color_widget() takes. Persisted, unlike the value format beside it: this chooses
    //! which data is shown rather than how, and is not something to re-pick every launch.
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
            bool     alpha_is_transparency = true;
            int      selected_group = 0, reference_group = 0;
            ImagePtr loaded; ///< Set once this entry's image arrives; still null => not yet arrived, or failed
        };
        vector<Entry> entries; ///< One per saved "images" entry, in file order; the same path may repeat
        int           current_index = -1, reference_index = -1; ///< Index into entries, or -1 if unset
        BlendMode_    blend_mode;
        json          view; ///< The session file's "view" sub-object, applied verbatim once loading completes

        // An arriving image is matched to the earliest not-yet-filled entry sharing its load options.
        // Entries sharing that key are guaranteed content-identical (loaded with identical options), so any
        // valid one-to-one assignment among them is correct regardless of which physical async load happens to
        // finish first -- this is what makes it safe to load the same file more than once in one session (e.g.
        // to compare two channel groups of it, or the same file with and without alpha as transparency).
        using Key = std::tuple<fs::path, string, bool>; ///< path, channel_selector, alpha_is_transparency
        map<Key, deque<int>> unresolved;
    };
    optional<PendingSession> m_pending_session;
};

/// Create the global singleton HDRViewApp instance
void init_hdrview(optional<float> exposure, optional<float> gamma, optional<bool> dither, optional<bool> force_sdr,
                  optional<bool> apple_keys, const vector<string> &in_files = {});

/// Return a pointer to the global singleton HDRViewApp instance
HDRViewApp *hdrview();
