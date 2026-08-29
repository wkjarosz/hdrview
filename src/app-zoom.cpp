#include "app.h"
#include "image.h"
#include "imgui_internal.h"

#include <cmath>

#ifdef HELLOIMGUI_USE_SDL2
#include <SDL.h>
#endif

using namespace std;
using namespace HelloImGui;

/*!
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
    // The fit-to-window ratios divide by a window's size, which is zero for a degenerate one, and clamp()
    // would pass the resulting NaN straight through -- both of its comparisons are false for one. Reject
    // non-finite input first and fall back to 1:1.
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

//! The nudge that keeps a zoom already sitting on a power of two from being rounded to the wrong side of
//! it by float error in log2(), while being far too small to reach the neighbouring stop.
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
    // window's min corner still has to be pulled back to the viewport's margin; flipped, it is
    // zoom*(display_window.max - pixel) + offset, which already puts that corner at zero.
    return (viewport_size() - dw.size()) / 2.f - select(m_flip, float2{0.f}, dw.min);
}

// The quad the image shader samples an image over spans its data window, uv 0 at the min corner and uv 1
// at the max one. Both ends come from vp_pos_at_pixel(), so the drawn image, the overlays and the pixel
// readouts share one transform -- including which display window a flip mirrors about, which is always the
// current image's, never the reference's own.
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
    // The exposure/offset step the image shader applies, on the same premultiplied values: the offset is
    // a straight-color quantity, so it enters scaled by alpha (see main() in image-shader.sglsl). Readouts
    // built on this therefore report what the viewport shows, translucent pixels included.
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

void HDRViewApp::handle_mouse_interaction()
{
    auto &io = ImGui::GetIO();
    if (io.WantCaptureMouse || !current_image())
        return;

    auto vp_mouse_pos   = vp_pos_at_app_pos(io.MousePos);
    bool cancel_autofit = false;

#if defined(__EMSCRIPTEN__)
    static constexpr float scroll_multiplier = 10.0f;
#else
    static constexpr float scroll_multiplier = 1.0f;
#endif
    auto scroll = float2{io.MouseWheelH, io.MouseWheel} * scroll_multiplier;

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
        float2 drag_delta{ImGui::GetMouseDragDelta(ImGuiMouseButton_Left)};
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            cancel_autofit = true;
            reposition_pixel_to_vp_pos(vp_mouse_pos + drag_delta, pixel_at_vp_pos(vp_mouse_pos));
            ImGui::ResetMouseDragDelta();
        }
    }

    if (cancel_autofit)
        this->cancel_autofit();
}

bool HDRViewApp::process_event(void *e)
{
#ifdef HELLOIMGUI_USE_SDL2
    auto &io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return false;

    SDL_Event *event = static_cast<SDL_Event *>(e);
    switch (event->type)
    {
    case SDL_QUIT: spdlog::trace("Got an SDL_QUIT event"); break;
    case SDL_WINDOWEVENT: spdlog::trace("Got an SDL_WINDOWEVENT event"); break;
    case SDL_MOUSEWHEEL: spdlog::trace("Got an SDL_MOUSEWHEEL event"); break;
    case SDL_MOUSEMOTION: spdlog::trace("Got an SDL_MOUSEMOTION event"); break;
    case SDL_MOUSEBUTTONDOWN: spdlog::trace("Got an SDL_MOUSEBUTTONDOWN event"); break;
    case SDL_MOUSEBUTTONUP: spdlog::trace("Got an SDL_MOUSEBUTTONUP event"); break;
    case SDL_FINGERMOTION: spdlog::trace("Got an SDL_FINGERMOTION event"); break;
    case SDL_FINGERDOWN: spdlog::trace("Got an SDL_FINGERDOWN event"); break;
    case SDL_MULTIGESTURE:
    {
        spdlog::trace("Got an SDL_MULTIGESTURE event; numFingers: {}; dDist: {}; x: {}, y: {}; io.MousePos: {}, {}; "
                      "io.MousePosFrac: {}, {}",
                      event->mgesture.numFingers, event->mgesture.dDist, event->mgesture.x, event->mgesture.y,
                      io.MousePos.x, io.MousePos.y, io.MousePos.x / io.DisplaySize.x, io.MousePos.y / io.DisplaySize.y);
        constexpr float cPinchZoomThreshold(0.0001f);
        constexpr float cPinchScale(80.0f);
        if (event->mgesture.numFingers == 2 && fabs(event->mgesture.dDist) >= cPinchZoomThreshold)
        {
            // Zoom in/out by positive/negative mPinch distance
            zoom_at_vp_pos(event->mgesture.dDist * cPinchScale, vp_pos_at_app_pos(io.MousePos));
            return true;
        }
    }
    break;
    case SDL_FINGERUP: spdlog::trace("Got an SDL_FINGERUP event"); break;
    }
#endif
    (void)e; // prevent unreferenced formal parameter warning
    return false;
}
