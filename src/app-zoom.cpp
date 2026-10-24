#include "app.h"
#include "image.h"
#include "imgui_internal.h"

#include <cmath>

using namespace std;
using namespace HelloImGui;

namespace
{
/// The display window the view shows \p img in, empty without one.
Box2f display_window_of(const ConstImagePtr &img) { return img ? Box2f{img->display_window} : Box2f{float2{0.f}}; }

/// The display window of the image \p preview shows: a reshaping edit's result, or else \p img.
Box2f previewed_window(const EditPreview &preview, const ConstImagePtr &img)
{ return preview.window ? Box2f{*preview.window} : display_window_of(img); }
} // namespace

/**
    Mirrors a pixel coordinate about a display window, along whichever axes are flipped.

    Pixel coordinates are continuous, with pixel \c i covering <tt>[i, i+1)</tt>, so the mirror of the
    whole window <tt>[0, max)</tt> is <tt>max - p</tt> -- the same convention image_position() hands the
    shader. The involution makes pixel_at_vp_pos() and vp_pos_at_pixel() inverses whether flipped or not.
*/
float2 HDRViewApp::flip_pixel(float2 pixel, const Box2f &display_window) const
{ return select(m_flip, display_window.max - pixel, pixel); }

float2 HDRViewApp::pixel_at_vp_pos(float2 vp_pos) const
{ return pixel_at_vp_pos(vp_pos, display_window_of(current_image())); }

float2 HDRViewApp::vp_pos_at_pixel(float2 pixel) const
{ return vp_pos_at_pixel(pixel, display_window_of(current_image())); }

float2 HDRViewApp::pixel_at_vp_pos(float2 vp_pos, const Box2f &display_window) const
{ return flip_pixel((vp_pos - (m_translate + center_offset(display_window))) / m_zoom, display_window); }

float2 HDRViewApp::vp_pos_at_pixel(float2 pixel, const Box2f &display_window) const
{ return m_zoom * flip_pixel(pixel, display_window) + (m_translate + center_offset(display_window)); }

void HDRViewApp::set_zoom(float zoom)
{
    // The fit-to-window ratios divide by a window's size, zero for a degenerate one, and clamp() passes the
    // resulting NaN straight through, since both of its comparisons are false for one.
    m_zoom = std::isfinite(zoom) ? clamp(zoom, MIN_ZOOM, MAX_ZOOM) : 1.f;
}

void HDRViewApp::fit_display_window()
{
    if (auto img = current_image())
    {
        set_zoom(minelem(viewport_size() / img->display_window.size()));
        center();
    }
}

void HDRViewApp::fit_data_window()
{
    if (auto img = current_image())
    {
        set_zoom(minelem(viewport_size() / img->data_window.size()));

        auto center_pos   = float2(viewport_size() / 2.f);
        auto center_pixel = Box2f(img->data_window).center();
        reposition_pixel_to_vp_pos(center_pos, center_pixel);
    }
}

void HDRViewApp::fit_selection()
{
    if (current_image() && m_roi.has_volume())
    {
        set_zoom(minelem(viewport_size() / m_roi.size()));

        auto center_pos   = float2(viewport_size() / 2.f);
        auto center_pixel = Box2f(m_roi).center();
        reposition_pixel_to_vp_pos(center_pos, center_pixel);
    }
}

void HDRViewApp::auto_fit_viewport()
{
    if (m_auto_fit_display)
        fit_display_window();
    if (m_auto_fit_data)
        fit_data_window();
    if (m_auto_fit_selection)
        fit_selection();
}

float HDRViewApp::zoom_level() const { return log(m_zoom * pixel_ratio()) / log(m_zoom_sensitivity); }

void HDRViewApp::set_zoom_level(float level) { set_zoom(std::pow(m_zoom_sensitivity, level) / pixel_ratio()); }

void HDRViewApp::zoom_at_vp_pos(float amount, float2 focus_vp_pos)
{
    if (amount == 0.f)
        return;

    auto  focused_pixel = pixel_at_vp_pos(focus_vp_pos); // save focused pixel coord before modifying zoom
    float scale_factor  = std::pow(m_zoom_sensitivity, amount);
    set_zoom(scale_factor * m_zoom);
    // reposition so focused_pixel is still under focus_app_pos
    reposition_pixel_to_vp_pos(focus_vp_pos, focused_pixel);
}

/// Nudge that keeps float error in log2() from rounding a power-of-two zoom to the wrong side of itself.
/** Far too small to reach the neighboring stop. */
static constexpr float k_zoom_step_epsilon = 1e-4f;

void HDRViewApp::zoom_in()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = float2(viewport_size() / 2.f);
    auto center_pixel = pixel_at_vp_pos(center_pos);

    // the next power of two strictly above the current zoom, whether or not the zoom is one itself
    set_zoom(std::pow(2.f, std::floor(std::log2(m_zoom) + k_zoom_step_epsilon) + 1.f));
    reposition_pixel_to_vp_pos(center_pos, center_pixel);
}

void HDRViewApp::zoom_out()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = float2(viewport_size() / 2.f);
    auto center_pixel = pixel_at_vp_pos(center_pos);

    // the next power of two strictly below the current zoom, whether or not the zoom is one itself
    set_zoom(std::pow(2.f, std::ceil(std::log2(m_zoom) - k_zoom_step_epsilon) - 1.f));
    reposition_pixel_to_vp_pos(center_pos, center_pixel);
}

void HDRViewApp::reposition_pixel_to_vp_pos(float2 position, float2 pixel)
{
    // Calculate where the new offset must be in order to satisfy the image position equation.
    const Box2f w = display_window_of(current_image());
    m_translate   = position - (flip_pixel(pixel, w) * m_zoom) - center_offset(w);
}

float HDRViewApp::pixel_ratio() const { return ImGui::GetIO().DisplayFramebufferScale.x; }

float2 HDRViewApp::center_offset(const Box2f &display_window) const
{
    const Box2f dw{display_window.min * m_zoom, display_window.max * m_zoom};
    // Center the display window in the viewport. Unflipped, the mapping is zoom*pixel + offset, so the
    // window's min corner has to be pulled back to the viewport's margin; flipped, it is
    // zoom*(display_window.max - pixel) + offset, which already puts that corner at zero.
    return (viewport_size() - dw.size()) / 2.f - select(m_flip, float2{0.f}, dw.min);
}

// The quad the image shader samples an image over spans its data window, uv 0 at the min corner and uv 1 at
// the max one. Both ends come from vp_pos_at_pixel(), so the drawn image, the overlays and the pixel readouts
// share one transform, including the display window a flip mirrors about, always the current image's.
float2 HDRViewApp::image_position(ConstImagePtr img) const
{
    const Box2f dw = img ? Box2f{img->data_window} : Box2f{{0, 0}, {0, 0}};
    return vp_pos_at_pixel(dw.min) / viewport_size();
}

float2 HDRViewApp::image_scale(ConstImagePtr img) const
{
    const Box2f dw = img ? Box2f{img->data_window} : Box2f{{0, 0}, {0, 0}};
    return (vp_pos_at_pixel(dw.max) - vp_pos_at_pixel(dw.min)) / viewport_size();
}

int HDRViewApp::next_visible_image_index(int index, Direction_ direction) const
{
    return next_matching_index(m_images, index, [](size_t, const ImagePtr &img) { return img->visible; }, direction);
}

int HDRViewApp::nth_visible_image_index(int n) const
{
    return int(n < (int)m_visible_images.size() ? m_visible_images[n] : m_images.size());
    // nth_matching_index(m_images, (size_t)n, [](size_t, const ImagePtr &img) { return img->visible; });
}

int HDRViewApp::image_index(ConstImagePtr img) const
{
    for (int i = 0; i < num_images(); ++i)
        if (m_images[i] == img)
            return i;
    return -1; // not found
}

float4 HDRViewApp::pixel_value(int2 p, bool raw, int which_image) const
{
    auto img1 = current_image();
    auto img2 = reference_image();

    // the channels the viewer shows of one image, isolated with that image's own alpha as the shader does
    auto shown = [&](const ConstImagePtr &img, Target_ target)
    {
        return img ? shader::choose_channel(img->rgba_pixel(p, target), m_channel,
                                            img->transparency == TransparencyType_Straight)
                   : float4{0.f};
    };

    float4 value{0.f};
    if (which_image == 0)
        value = raw ? (img1 ? img1->raw_pixel(p, Target_Primary) : float4{0.f}) : shown(img1, Target_Primary);
    else if (which_image == 1)
        value = raw ? (img2 ? img2->raw_pixel(p, Target_Secondary) : float4{0.f}) : shown(img2, Target_Secondary);
    else if (which_image == 2)
        // the shader blends only when there is a reference to blend with
        value = img2 ? blend(shown(img1, Target_Primary), shown(img2, Target_Secondary), m_blend_mode)
                     : shown(img1, Target_Primary);

    return raw ? value : tonemap_value(value);
}

float4 HDRViewApp::tonemap_value(float4 value) const { return tonemap_value(value, m_rolloff); }

float4 HDRViewApp::tonemap_value(float4 value, const Rolloff &rolloff) const
{
    // the image shader's exposure, tonemap and rolloff steps, as main() in image-shader.sglsl applies them
    float4 shown = ::tonemap(shader::expose(value, powf(2.f, m_exposure_live), m_offset_live), m_gamma_live,
                             m_tonemap, m_colormaps[m_colormap_index], m_reverse_colormap);
    if (rolloff.enabled)
        shown =
            shader::roll_off(shown, m_tonemap, rolloff.preserve_hue, rolloff.ceiling, rolloff.has_peak, rolloff.peak);
    return shown;
}

void HDRViewApp::calculate_viewport()
{
    auto &io = ImGui::GetIO();
    // The viewport is the central dockspace node, falling back to the whole window before the dockspace
    // exists. Both are in logical pixels, the same space as io.DisplaySize and ImGui's MousePos.
    spdlog::trace("DisplayFramebufferScale: {}, DpiWindowSizeFactor: {}, FontScaleDpi: {}",
                  float2{io.DisplayFramebufferScale}, DpiWindowSizeFactor(), ImGui::GetStyle().FontScaleDpi);
    m_viewport_min  = {0.f, 0.f};
    m_viewport_size = io.DisplaySize;
    if (auto id = m_params.dockingParams.dockSpaceIdFromName("MainDockSpace"))
        if (auto central_node = ImGui::DockBuilderGetCentralNode(*id))
        {
            m_viewport_size = central_node->Size;
            m_viewport_min  = central_node->Pos;
        }
}

/// One notch of a discrete mouse wheel, in the units a precise scrolling device reports.
static constexpr float k_units_per_notch = 10.f;

/// Frames without any wheel input after which the next event is taken to start a fresh gesture.
static constexpr int k_scroll_gesture_gap = 10;

/// Puts a wheel delta on one scale, whichever kind of device produced it.
/**
    Every backend HDRView builds against reports a discrete wheel as one whole unit per notch, and a precise
    device (a trackpad) as a stream of small fractions adding up to many units over a gesture. Nothing but
    the magnitude tells the two apart, so a whole-numbered delta is taken for notches and brought up to the
    rate the fractions arrive at. The classification is latched for the length of a gesture, so a precise
    device landing on a whole unit part way through one doesn't jump a frame.
*/
static float2 scroll_units(float2 wheel)
{
    static bool discrete         = true;
    static int  last_input_frame = 0;

    if (wheel == float2{0.f})
        return wheel;

    const int frame = ImGui::GetFrameCount();
    if (frame - last_input_frame > k_scroll_gesture_gap)
        discrete = true;
    last_input_frame = frame;

    if (wheel != round(wheel))
        discrete = false;

    return discrete ? wheel * k_units_per_notch : wheel;
}

std::optional<float2> HDRViewApp::preview_uv(float2 vp_pos) const
{
    const EditPreview *preview = edit_preview();
    if (!preview)
        return std::nullopt;
    const Box2f w = previewed_window(*preview, current_image());
    return (pixel_at_vp_pos(vp_pos, w) - w.min) / w.size();
}

void HDRViewApp::handle_mouse_interaction()
{
    auto &io = ImGui::GetIO();
    if (!current_image())
        return;

    // A save or edit dialog is modal but lets the viewport be panned and zoomed around it: where the mouse is over the
    // viewport and no window, which a modal leaves only its own and its popups, and for a drag only if it began there.
    // It showed last frame, since this runs before this frame's dialogs.
    bool navigate_only = false, may_scroll = true, may_drag = true;
    if (io.WantCaptureMouse)
    {
        const bool around = ImGui::GetFrameCount() - m_navigable_dialog_frame <= 1 &&
                            app_pos_in_viewport(io.MousePos) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_dragging_around_dialog = around;
            // a drag begun on a preview that takes drags steers it to the end, and one begun beside it pans
            const auto at      = preview_uv(vp_pos_at_app_pos(io.MousePos));
            m_steering_preview = around && at && all(gequal(*at, float2{0.f})) && all(lequal(*at, float2{1.f}));
        }
        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_dragging_around_dialog = m_steering_preview = false;
        if (!around && !m_dragging_around_dialog)
            return;
        navigate_only = true;
        // a drag of one of the dialog's own widgets, carried out over the viewport, is the widget's
        may_drag   = m_dragging_around_dialog;
        may_scroll = around && (m_dragging_around_dialog || !ImGui::IsMouseDown(ImGuiMouseButton_Left));
    }

    auto vp_mouse_pos   = vp_pos_at_app_pos(io.MousePos);
    bool cancel_autofit = false;

    auto scroll = scroll_units(float2{io.MouseWheelH, io.MouseWheel});

    if (may_scroll && length2(scroll) > 0.f)
    {
        cancel_autofit = true;
        if (ImGui::IsKeyDown(ImGuiMod_Shift))
            // panning
            reposition_pixel_to_vp_pos(vp_mouse_pos + scroll * 4.f, pixel_at_vp_pos(vp_mouse_pos));
        else
            zoom_at_vp_pos(scroll.y / 4.f, vp_mouse_pos);
    }

    const MouseMode mouse_mode = navigate_only ? MouseMode(MouseMode_PanZoom) : m_mouse_mode;
    if (mouse_mode == MouseMode_RectangularSelection)
    {
        // set m_roi based on dragged region
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_roi_live = Box2i{int2{0}};
        else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            m_roi_live.make_empty();
            m_roi_live.enclose(int2{pixel_at_app_pos(io.MouseClickedPos[0])});
            m_roi_live.enclose(int2{pixel_at_app_pos(io.MousePos)});
        }
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            m_roi = m_roi_live;
    }
    else if (mouse_mode == MouseMode_Annotate)
        handle_annotate_tool();
    else if (mouse_mode == MouseMode_ColorInspector)
    {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            // add watched pixel
            m_watched_pixels.emplace_back(WatchedPixel{int2{pixel_at_app_pos(io.MousePos)}});
        else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (m_watched_pixels.size())
                m_watched_pixels.back().pixel = int2{pixel_at_app_pos(io.MousePos)};
        }
    }
    else
    {
        if (may_drag && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            // A drag begun on a preview that takes drags steers it, from where the drag last was to where it is, in
            // the uv of the image previewed; Alt pans the view over it instead.
            const float2 delta{ImGui::GetMouseDragDelta(ImGuiMouseButton_Left)};
            bool         steered = false;
            if (m_steering_preview && !ImGui::IsKeyDown(ImGuiMod_Alt) && m_active_touches < 2)
                if (const auto from = preview_uv(vp_mouse_pos - delta), to = preview_uv(vp_mouse_pos); from && to)
                {
                    const EditPreview *p = edit_preview();
                    steered = p->command->drag_preview(*from, *to, previewed_window(*p, current_image()).size());
                }

            // A second finger means a pinch, while the first still drives a synthesized left-drag, so
            // panning would fight the zoom. The drag is consumed meanwhile: left to accumulate, it would
            // pan by the whole pinch's travel the moment a finger lifts.
            if (!steered && m_active_touches < 2)
            {
                cancel_autofit = true;
                reposition_pixel_to_vp_pos(vp_mouse_pos + delta, pixel_at_vp_pos(vp_mouse_pos));
            }
            ImGui::ResetMouseDragDelta();
        }
    }

    if (cancel_autofit)
        this->cancel_autofit();
}

void HDRViewApp::touch_gesture(int num_touches, float scale, float2 from_app_pos, float2 to_app_pos)
{
    m_active_touches = num_touches;

    if (scale == 1.f && from_app_pos == to_app_pos)
        return;

    // Pin whatever the fingers' midpoint was over to wherever that midpoint has moved, and magnify by the
    // ratio their separation grew by, so the image stays under the fingers.
    const float2 to    = vp_pos_at_app_pos(to_app_pos);
    const float2 pixel = pixel_at_vp_pos(vp_pos_at_app_pos(from_app_pos));
    set_zoom(scale * m_zoom);
    reposition_pixel_to_vp_pos(to, pixel);

    cancel_autofit();
}
