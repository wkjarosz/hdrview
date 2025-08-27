#include "app.h"
#include "hello_imgui/hello_imgui.h"
#include "imgui_internal.h"

#ifdef HELLOIMGUI_USE_SDL2
#include <SDL.h>
#endif

using namespace std;

static constexpr float MIN_ZOOM = 0.01f;
static constexpr float MAX_ZOOM = 512.f;

void HDRViewApp::fit_display_window()
{
    if (auto img = current_image())
    {
        m_zoom = minelem(viewport_size() / img->display_window.size());
        center();
    }
}

void HDRViewApp::fit_data_window()
{
    if (auto img = current_image())
    {
        m_zoom = minelem(viewport_size() / img->data_window.size());

        auto center_pos   = float2(viewport_size() / 2.f);
        auto center_pixel = Box2f(img->data_window).center();
        reposition_pixel_to_vp_pos(center_pos, center_pixel);
    }
}

void HDRViewApp::fit_selection()
{
    if (current_image() && m_roi.has_volume())
    {
        m_zoom = minelem(viewport_size() / m_roi.size());

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

void HDRViewApp::set_zoom_level(float level)
{
    m_zoom = clamp(std::pow(m_zoom_sensitivity, level) / pixel_ratio(), MIN_ZOOM, MAX_ZOOM);
}

void HDRViewApp::zoom_at_vp_pos(float amount, float2 focus_vp_pos)
{
    if (amount == 0.f)
        return;

    auto  focused_pixel = pixel_at_vp_pos(focus_vp_pos); // save focused pixel coord before modifying zoom
    float scale_factor  = std::pow(m_zoom_sensitivity, amount);
    m_zoom              = clamp(scale_factor * m_zoom, MIN_ZOOM, MAX_ZOOM);
    // reposition so focused_pixel is still under focus_app_pos
    reposition_pixel_to_vp_pos(focus_vp_pos, focused_pixel);
}

void HDRViewApp::zoom_in()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = float2(viewport_size() / 2.f);
    auto center_pixel = pixel_at_vp_pos(center_pos);

    // determine next higher power of 2 zoom level
    float level_for_sensitivity = ceil(log(m_zoom) / log(2.f) + 0.5f);
    float new_scale             = std::pow(2.f, level_for_sensitivity);
    m_zoom                      = clamp(new_scale, MIN_ZOOM, MAX_ZOOM);
    reposition_pixel_to_vp_pos(center_pos, center_pixel);
}

void HDRViewApp::zoom_out()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = float2(viewport_size() / 2.f);
    auto center_pixel = pixel_at_vp_pos(center_pos);

    // determine next lower power of 2 zoom level
    float level_for_sensitivity = std::floor(log(m_zoom) / log(2.f) - 0.5f);
    float new_scale             = std::pow(2.f, level_for_sensitivity);
    m_zoom                      = clamp(new_scale, MIN_ZOOM, MAX_ZOOM);
    reposition_pixel_to_vp_pos(center_pos, center_pixel);
}

void HDRViewApp::reposition_pixel_to_vp_pos(float2 position, float2 pixel)
{
    if (auto img = current_image())
        pixel = select(m_flip, img->display_window.max - pixel - 1, pixel);

    // Calculate where the new offset must be in order to satisfy the image position equation.
    m_translate = position - (pixel * m_zoom) - center_offset();
}

Box2f HDRViewApp::scaled_display_window(ConstImagePtr img) const
{
    Box2f dw = img ? Box2f{img->display_window} : Box2f{{0, 0}, {0, 0}};
    dw.min *= m_zoom;
    dw.max *= m_zoom;
    return dw;
}

Box2f HDRViewApp::scaled_data_window(ConstImagePtr img) const
{
    Box2f dw = img ? Box2f{img->data_window} : Box2f{{0, 0}, {0, 0}};
    dw.min *= m_zoom;
    dw.max *= m_zoom;
    return dw;
}

float HDRViewApp::pixel_ratio() const { return ImGui::GetIO().DisplayFramebufferScale.x; }

float2 HDRViewApp::center_offset() const
{
    auto   dw     = scaled_display_window(current_image());
    float2 offset = (viewport_size() - dw.size()) / 2.f - dw.min;

    // Adjust for flipping: if flipped, offset from the opposite side
    // if (current_image())
    {
        if (m_flip.x)
            offset.x += dw.min.x;
        if (m_flip.y)
            offset.y += dw.min.y;
    }
    return offset;
}

float2 HDRViewApp::image_position(ConstImagePtr img) const
{
    auto   dw  = scaled_data_window(img);
    auto   dsw = scaled_display_window(img);
    float2 pos = m_translate + center_offset() + select(m_flip, dsw.max - dw.min, dw.min);

    // Adjust for flipping: move the image to the opposite side if flipped
    // if (img)
    // {
    //     if (m_flip.x)
    //         pos.x += m_offset.x + center_offset().x + (dsw.max.x - dw.min.x);
    //     if (m_flip.y)
    //         pos.y += m_offset.y + center_offset().y + (dsw.max.y - dw.min.y);
    // }
    return pos / viewport_size();
}

float2 HDRViewApp::image_scale(ConstImagePtr img) const
{
    auto   dw    = scaled_data_window(img);
    float2 scale = dw.size() / viewport_size();

    // Negate scale for flipped axes
    // if (img)
    {
        if (m_flip.x)
            scale.x = -scale.x;
        if (m_flip.y)
            scale.y = -scale.y;
    }
    return scale;
}

int HDRViewApp::next_visible_image_index(int index, EDirection direction) const
{
    return next_matching_index(m_images, index, [](size_t, const ImagePtr &img) { return img->visible; }, direction);
}

int HDRViewApp::nth_visible_image_index(int n) const
{
    return (int)nth_matching_index(m_images, (size_t)n, [](size_t, const ImagePtr &img) { return img->visible; });
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

    return raw ? value
               : ::tonemap(float4{powf(2.f, m_exposure_live) * value.xyz() + m_offset_live, value.w}, m_gamma_live,
                           m_tonemap, m_colormaps[m_colormap_index], m_reverse_colormap);
}

void HDRViewApp::calculate_viewport()
{
    auto &io = ImGui::GetIO();
    //
    // calculate the viewport sizes
    // fbsize is the size of the window in physical pixels while accounting for dpi factor on retina
    // screens. For retina displays, io.DisplaySize is the size of the window in logical pixels so we it by
    // io.DisplayFramebufferScale to get the physical pixel size for the framebuffer.
    spdlog::trace("DisplayFramebufferScale: {}, DpiWindowSizeFactor: {}, DpiFontLoadingFactor: {}",
                  float2{io.DisplayFramebufferScale}, HelloImGui::DpiWindowSizeFactor(),
                  HelloImGui::DpiFontLoadingFactor());
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
