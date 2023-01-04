//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include "xpuimage.h"
#include <nanogui/canvas.h>
#include <nanogui/texture.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using namespace nanogui;

/*!
 * @class 	HDRImageView hdrimageview.h
 * @brief	Widget used to manage and display multiple HDR images.
 */
class HDRImageView : public Canvas
{
public:
    using TextureRef     = ref<Texture>;
    using PixelCallback  = std::function<void(const Vector2i &, char **, size_t)>;
    using MouseCallback  = std::function<bool(const Vector2i &p, int button, bool down, int modifiers)>;
    using DragCallback   = std::function<bool(const Vector2i &p, const Vector2i &rel, int button, int modifiers)>;
    using MotionCallback = DragCallback;
    using FloatCallback  = std::function<void(float)>;
    using BoolCallback   = std::function<void(bool)>;
    using VoidCallback   = std::function<void(void)>;
    using ROICallback    = std::function<void(const Box2i &)>;
    using DrawCallback   = std::function<void(NVGcontext *ctx)>;

    /// Initialize the widget
    HDRImageView(Widget *parent, const nlohmann::json &settings = nlohmann::json::object());

    void write_settings(nlohmann::json &j) const;

    void add_shortcuts(HelpWindow *w);

    // Widget implementation
    virtual bool keyboard_event(int key, int scancode, int action, int modifiers) override;
    virtual bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool mouse_motion_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
    virtual bool mouse_drag_event(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
    virtual bool scroll_event(const Vector2i &p, const Vector2f &rel) override;
    virtual void draw(NVGcontext *ctx) override;
    virtual void draw_contents() override;

    // Getters and setters
    void set_current_image(XPUImagePtr cur);
    void set_reference_image(XPUImagePtr ref);

    /// Return the pixel offset of the zoomed image rectangle
    Vector2f offset() const { return m_offset; }
    /// Set the pixel offset of the zoomed image rectangle
    void set_offset(const Vector2f &offset) { m_offset = offset; }

    float zoom_sensitivity() const { return m_zoom_sensitivity; }
    void  set_zoom_sensitivity(float zoom_sensitivity) { m_zoom_sensitivity = zoom_sensitivity; }

    float grid_threshold() const { return m_grid_threshold; }
    void  set_grid_threshold(float grid_threshold) { m_grid_threshold = grid_threshold; }

    float pixel_info_threshold() const { return m_pixel_info_threshold; }
    void  set_pixel_info_threshold(float pixel_info_threshold) { m_pixel_info_threshold = pixel_info_threshold; }

    // Image transformation functions.

    /// Calculates the image pixel coordinates of the given position on the widget.
    Vector2f pixel_at_position(const Vector2f &position) const;

    /// Calculates the position inside the widget for the given image pixel coordinate.
    Vector2f position_at_pixel(const Vector2f &pixel) const;

    /// Calculates the position inside the screen for the given image pixel coordinate.
    Vector2f screen_position_at_pixel(const Vector2f &pixel) const;

    /**
     * Modifies the internal state of the image viewer widget so that the provided
     * position on the widget has the specified image pixel coordinate. Also clamps the values of offset
     * to the sides of the widget.
     */
    void set_pixel_at_position(const Vector2f &position, const Vector2f &pixel);

    /// Centers the image without affecting the scaling factor.
    void center();

    /// Centers and scales the image so that it fits inside the widget.
    void fit();

    /**
     * Changes the scale factor by the provided amount modified by the zoom sensitivity member variable.
     * The scaling occurs such that the image pixel coordinate under the focused position remains in
     * the same screen position before and after the scaling.
     */
    void zoom_by(float amount, const Vector2f &focusPosition);

    /// Zoom in to the next power of two
    void zoom_in();

    /// Zoom out to the previous power of two
    void zoom_out();

    float zoom_level() const { return m_zoom_level; }
    void  set_zoom_level(float l);

    float zoom() const { return m_zoom; }

    EChannel channel() { return m_channel; }
    void     set_channel(EChannel c) { m_channel = c; }

    EBlendMode blend_mode() { return m_blend_mode; }
    void       set_blend_mode(EBlendMode b) { m_blend_mode = b; }

    float gamma() const { return m_gamma; }
    void  set_gamma(float g)
    {
        if (m_gamma != g)
        {
            m_gamma = g;
            m_gamma_callback(g);
        }
    }

    float exposure() const { return m_exposure; }
    void  set_exposure(float e)
    {
        if (m_exposure != e)
        {
            m_exposure = e;
            m_exposure_callback(e);
        }
    }
    void reset_tonemapping()
    {
        set_exposure(0.0f);
        set_gamma(2.2f);
        set_sRGB(true);
    }
    void normalize_exposure()
    {
        if (!m_current_image)
            return;
        Color4 mC  = m_current_image->image().max();
        float  mCf = std::max({mC[0], mC[1], mC[2]});
        set_exposure(log2(1.0f / mCf));
    }

    bool sRGB() const { return m_sRGB; }
    void set_sRGB(bool b)
    {
        m_sRGB = b;
        m_sRGB_callback(b);
    }

    void set_LDR(bool value) { m_LDR = value; }
    bool LDR() const { return m_LDR; }

    bool dithering_on() const { return m_dither; }
    void set_dithering(bool b) { m_dither = b; }

    bool draw_grid_on() const { return m_draw_grid; }
    void set_draw_grid(bool b) { m_draw_grid = b; }

    bool draw_pixel_info_on() const { return m_draw_pixel_info; }
    void set_draw_pixel_info(bool b) { m_draw_pixel_info = b; }

    Color4 tonemap(const Color4 &color) const;

    // Callback functions

    /// Callback executed whenever the gamma value has been changed, e.g. via @ref set_gamma
    FloatCallback gamma_callback() const { return m_gamma_callback; }
    void          set_gamma_callback(const FloatCallback &cb) { m_gamma_callback = cb; }

    /// Callback executed whenever the exposure value has been changed, e.g. via @ref set_exposure
    FloatCallback exposure_callback() const { return m_exposure_callback; }
    void          set_exposure_callback(const FloatCallback &cb) { m_exposure_callback = cb; }

    /// Callback executed whenever the sRGB setting has been changed, e.g. via @ref set_sRGB
    BoolCallback sRGB_callback() const { return m_sRGB_callback; }
    void         set_sRGB_callback(const BoolCallback &cb) { m_sRGB_callback = cb; }

    /// Callback executed when the zoom level changes
    FloatCallback zoom_callback() const { return m_zoom_callback; }
    void          set_zoom_callback(const FloatCallback &cb) { m_zoom_callback = cb; }

    /// Callback that is used to acquire information about pixel components
    PixelCallback pixel_callback() const { return m_pixel_callback; }
    void          set_pixel_callback(const PixelCallback &cb) { m_pixel_callback = cb; }

    /// Callback executed on mouse clicks
    MouseCallback mouse_callback() const { return m_mouse_callback; }
    void          set_mouse_callback(const MouseCallback &cb) { m_mouse_callback = cb; }

    /// Callback executed on mouse clicks
    DragCallback drag_callback() const { return m_drag_callback; }
    void         set_drag_callback(const DragCallback &cb) { m_drag_callback = cb; }

    /// Callback executed when the mouse moves over the widget
    MotionCallback motion_callback() const { return m_motion_callback; }
    void           set_motion_callback(const MotionCallback &cb) { m_motion_callback = cb; }

    /// Callback executed when we change which image is displayed
    VoidCallback changed_callback() const { return m_changed_callback; }
    void         set_changed_callback(const VoidCallback &cb) { m_changed_callback = cb; }

    /// Callback executed at the end of drawing
    DrawCallback draw_callback() const { return m_draw_callback; }
    void         set_draw_callback(const DrawCallback &cb) { m_draw_callback = cb; }

protected:
    Vector2f position_f() const { return Vector2f(m_pos); }
    Vector2f size_f() const { return Vector2f(m_size); }

    Vector2i image_size(ConstXPUImagePtr img) const { return img ? img->size() : Vector2i(0, 0); }
    Vector2f image_size_f(ConstXPUImagePtr img) const { return Vector2f(image_size(img)); }
    Vector2f scaled_image_size_f(ConstXPUImagePtr img) const { return m_zoom * image_size_f(img); }

    Vector2f center_offset(ConstXPUImagePtr img) const;

    // Helper drawing methods.
    void draw_widget_border(NVGcontext *ctx) const;
    void draw_image_border(NVGcontext *ctx) const;
    void draw_helpers(NVGcontext *ctx) const;
    void draw_pixel_grid(NVGcontext *ctx) const;
    void draw_pixel_info(NVGcontext *ctx) const;
    void draw_ROI(NVGcontext *ctx) const;
    void image_position_and_scale(Vector2f &position, Vector2f &scale, ConstXPUImagePtr image);

    XPUImagePtr m_current_image;
    XPUImagePtr m_reference_image;
    TextureRef  m_null_image;

    ref<Shader> m_image_shader;
    TextureRef  m_dither_tex;

    float m_exposure = 0.f, m_gamma = 2.2f;
    bool  m_sRGB = true, m_LDR = false, m_dither = true, m_draw_grid = true, m_draw_pixel_info = true;

    // Image display parameters.
    float      m_zoom;                                  ///< The scale/zoom of the image
    float      m_zoom_level;                            ///< The zoom level
    Vector2f   m_offset     = 0;                        ///< The panning offset of the
    EChannel   m_channel    = EChannel::RGB;            ///< Which channel to display
    EBlendMode m_blend_mode = EBlendMode::NORMAL_BLEND; ///< How to blend the current and reference images

    // Fine-tuning parameters.
    float m_zoom_sensitivity = 1.0717734625f;

    // Image info parameters.
    float m_grid_threshold       = -1;
    float m_pixel_info_threshold = -1;

    // various callback functions
    FloatCallback  m_exposure_callback;
    FloatCallback  m_gamma_callback;
    BoolCallback   m_sRGB_callback;
    FloatCallback  m_zoom_callback;
    PixelCallback  m_pixel_callback;
    MouseCallback  m_mouse_callback;
    DragCallback   m_drag_callback;
    MotionCallback m_motion_callback;
    VoidCallback   m_changed_callback;
    DrawCallback   m_draw_callback;

    Vector2i m_clicked;
};
