#include "app.h"
#include "image.h"
#include "imgui_internal.h"

#include <cmath>

using namespace std;
using namespace HelloImGui;

/**
    Mirrors a pixel coordinate about the current image's display window, along whichever axes are flipped.

    Pixel coordinates are continuous, with pixel \c i covering <tt>[i, i+1)</tt>, so the mirror of the
    whole window <tt>[0, max)</tt> is <tt>max - p</tt> -- the same convention image_position() hands the
    shader. The involution makes pixel_at_vp_pos() and vp_pos_at_pixel() inverses whether flipped or not.
*/
float2 HDRViewApp::flip_pixel(float2 pixel) const
{
    if (auto img = current_image())
        return select(m_flip, float2{img->display_window.max} - pixel, pixel);
    return pixel;
}

float2 HDRViewApp::pixel_at_vp_pos(float2 vp_pos) const
{
    return flip_pixel((vp_pos - (m_translate + center_offset())) / m_zoom);
}

float2 HDRViewApp::vp_pos_at_pixel(float2 pixel) const
{
    return m_zoom * flip_pixel(pixel) + (m_translate + center_offset());
}

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
    m_translate = position - (flip_pixel(pixel) * m_zoom) - center_offset();
}

Box2f HDRViewApp::scaled_display_window(ConstImagePtr img) const
{
    Box2f dw = img ? Box2f{img->display_window} : Box2f{{0, 0}, {0, 0}};
    dw.min *= m_zoom;
    dw.max *= m_zoom;
    return dw;
}

float HDRViewApp::pixel_ratio() const { return ImGui::GetIO().DisplayFramebufferScale.x; }

float2 HDRViewApp::center_offset() const
{
    const Box2f dw = scaled_display_window(current_image());
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

    float4 value;

    if (which_image == 0)
        value = img1 ? (raw ? img1->raw_pixel(p, Target_Primary) : img1->rgba_pixel(p, Target_Primary)) : float4{0.f};
    else if (which_image == 1)
        value =
            img2 ? (raw ? img2->raw_pixel(p, Target_Secondary) : img2->rgba_pixel(p, Target_Secondary)) : float4{0.f};
    else if (which_image == 2)
    {
        auto rgba1 = img1 ? img1->rgba_pixel(p, Target_Primary) : float4{0.f};
        auto rgba2 = img2 ? img2->rgba_pixel(p, Target_Secondary) : float4{0.f};
        value      = blend(rgba1, rgba2, m_blend_mode);
    }

    return raw ? value : tonemap_value(value);
}

float4 HDRViewApp::tonemap_value(float4 value) const
{
    // The exposure/offset step the image shader applies, on the same premultiplied values: the offset is a
    // straight-color quantity, so it enters scaled by alpha (see main() in image-shader.sglsl).
    float3 exposed = powf(2.f, m_exposure_live) * value.xyz() + m_offset_live * value.w;
    return ::tonemap(float4{exposed, value.w}, m_gamma_live, m_tonemap, m_colormaps[m_colormap_index],
                     m_reverse_colormap);
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

int HDRViewApp::active_annotation() const
{
    auto img = current_image();
    if (!img || m_active_annotation_on != img || m_active_annotation < 0 ||
        m_active_annotation >= int(img->annotations.size()))
        return -1;
    return m_active_annotation;
}

void HDRViewApp::set_active_annotation(int index)
{
    m_active_annotation    = index;
    m_active_annotation_on = index < 0 ? nullptr : current_image();
}

void HDRViewApp::cancel_annotation_drag()
{
    // Against the image the drag began on, which is not necessarily the one that is current now.
    const auto held  = m_active_annotation_on;
    const int  index = m_active_annotation;
    if (held && index >= 0 && index < int(held->annotations.size()))
    {
        if (m_annotation_drag == AnnotationDrag::Creating)
            held->annotations.erase(held->annotations.begin() + index);
        else if (m_annotation_drag != AnnotationDrag::None)
            held->annotations[size_t(index)] = m_annotation_drag_start;
    }

    if (m_annotation_drag == AnnotationDrag::Creating)
        set_active_annotation(-1);

    m_annotation_drag        = AnnotationDrag::None;
    m_annotation_drag_handle = -1;
}

// How far the cursor must travel before a scribble keeps another point, and how far a point may stray from
// its neighbors before being dropped again. Both screen pixels: a stroke should record the same shape
// however far the image is zoomed.
static constexpr float k_scribble_step = 2.f, k_scribble_tolerance = 1.f;

void HDRViewApp::handle_annotate_tool()
{
    auto  img = current_image();
    auto &io  = ImGui::GetIO();

    // A drag whose image is no longer current is abandoned, half-drawn shape and all, rather than carried
    // on over whatever playback or a close has put in its place.
    if (m_annotation_drag != AnnotationDrag::None && m_active_annotation_on != img)
    {
        cancel_annotation_drag();
        set_active_annotation(-1);
    }

    if (m_annotation_drag != AnnotationDrag::None && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        cancel_annotation_drag();
        return;
    }

    // Slop and handle radius are screen quantities, like everything else about how an annotation looks.
    const float slop   = 0.25f * HelloImGui::EmSize();
    const float radius = 0.3f * HelloImGui::EmSize();

    // Screen pixels per image pixel, which turns the two scribble constants into the image distances the
    // points are actually kept in.
    const float xform_scale = viewport_transform().scale;

    auto        &list  = img->annotations;
    const float2 pixel = pixel_at_app_pos(io.MousePos);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto xform  = viewport_transform();
        const int  active = active_annotation();

        // Handles of the annotation already in hand come first: they are drawn over everything, and often
        // lie over a neighbor that would otherwise take the click.
        if (active >= 0)
            if (const int h = handle_at(list[size_t(active)], io.MousePos, xform, radius); h >= 0)
            {
                m_annotation_drag        = AnnotationDrag::Resizing;
                m_annotation_drag_handle = h;
                m_annotation_drag_start  = list[size_t(active)];
                return;
            }

        if (const int i = annotation_at(list, io.MousePos, xform, slop); i >= 0)
        {
            set_active_annotation(i);
            m_annotation_drag       = AnnotationDrag::Moving;
            m_annotation_drag_start = list[size_t(i)];
            m_annotation_grab       = pixel;
            return;
        }

        // Empty space draws a new one, this being a drawing tool; the pan tool and a scroll still move the
        // view. A click that never becomes a drag is undone on release, which is how clicking away clears
        // the selection.
        Annotation a = m_annotation_style;
        a.shape      = m_annotation_shape;
        a.points     = {pixel, pixel};
        list.push_back(a);
        set_active_annotation(int(list.size()) - 1);
        m_annotation_drag       = AnnotationDrag::Creating;
        m_annotation_drag_start = a;
        return;
    }

    const int active = active_annotation();
    if (m_annotation_drag == AnnotationDrag::None || active < 0)
        return;

    Annotation &a = list[size_t(active)];

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        switch (m_annotation_drag)
        {
        case AnnotationDrag::Creating:
            if (a.shape == Annotation::Shape::Freehand)
            {
                // A point per frame would record how fast the cursor moved rather than where it went, so
                // one is kept only once the last is a couple of screen pixels behind.
                const float step = k_scribble_step / std::max(xform_scale, 1e-6f);
                if (length(pixel - a.points.back()) >= step)
                    a.points.push_back(pixel);
            }
            else
                a.p1() = pixel;
            break;

        case AnnotationDrag::Moving:
        {
            // Against the annotation as it was at mouse-down, so a move never accumulates rounding.
            const float2 delta = pixel - m_annotation_grab;
            a.points           = m_annotation_drag_start.points;
            for (auto &p : a.points) p += delta;
        }
        break;

        case AnnotationDrag::Resizing:
            // Dragging a corner past its opposite renumbers the handles, so the drag follows the index
            // back rather than keeping hold of a corner that has moved out from under the cursor.
            m_annotation_drag_handle = move_annotation_handle(a, m_annotation_drag_handle, pixel);
            break;

        default: break;
        }
    }
    else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (m_annotation_drag == AnnotationDrag::Creating)
        {
            // A captured scribble holds far more points than its shape needs. Simplified once, on release,
            // rather than while it is being drawn, so what is dropped is judged against the whole stroke.
            if (a.shape == Annotation::Shape::Freehand)
                a.points = simplify_polyline(a.points, k_scribble_tolerance / std::max(xform_scale, 1e-6f));

            // A click that never became a drag leaves nothing behind, rather than a shape with no extent
            // that is invisible and cannot be taken hold of again.
            if (a.bounds().min == a.bounds().max)
            {
                list.pop_back();
                set_active_annotation(-1);
            }
        }

        m_annotation_drag        = AnnotationDrag::None;
        m_annotation_drag_handle = -1;
    }
}

void HDRViewApp::handle_mouse_interaction()
{
    auto &io = ImGui::GetIO();
    if (io.WantCaptureMouse || !current_image())
        return;

    auto vp_mouse_pos   = vp_pos_at_app_pos(io.MousePos);
    bool cancel_autofit = false;

    auto scroll = scroll_units(float2{io.MouseWheelH, io.MouseWheel});

    if (length2(scroll) > 0.f)
    {
        cancel_autofit = true;
        if (ImGui::IsKeyDown(ImGuiMod_Shift))
            // panning
            reposition_pixel_to_vp_pos(vp_mouse_pos + scroll * 4.f, pixel_at_vp_pos(vp_mouse_pos));
        else
            zoom_at_vp_pos(scroll.y / 4.f, vp_mouse_pos);
    }

    if (m_mouse_mode == MouseMode_RectangularSelection)
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
    else if (m_mouse_mode == MouseMode_Annotate)
        handle_annotate_tool();
    else if (m_mouse_mode == MouseMode_ColorInspector)
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
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            // A second finger means a pinch, while the first still drives a synthesized left-drag, so
            // panning would fight the zoom. The drag is consumed meanwhile: left to accumulate, it would
            // pan by the whole pinch's travel the moment a finger lifts.
            if (m_active_touches < 2)
            {
                cancel_autofit = true;
                reposition_pixel_to_vp_pos(vp_mouse_pos + float2{ImGui::GetMouseDragDelta(ImGuiMouseButton_Left)},
                                           pixel_at_vp_pos(vp_mouse_pos));
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
