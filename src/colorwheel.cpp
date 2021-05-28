//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorwheel.h"
#include "colorspace.h"
#include "xpuimage.h"
#include <nanogui/theme.h>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <iostream>
#include <hdrview_resources.h>

NAMESPACE_BEGIN(nanogui)

ColorWheel2::ColorWheel2(Widget *parent, const Color& rgb, int components)
    : Widget(parent), m_drag_region(None), m_visible_componets(components) {
    set_color(rgb);
}

Vector2i ColorWheel2::preferred_size(NVGcontext *) const {
    return { 130, 130 };
}

void ColorWheel2::draw(NVGcontext *ctx) {

    Widget::draw(ctx);

    if (!m_visible)
        return;

    NVGcontext* vg = ctx;

    float hue = m_hue;
    NVGpaint paint;

    nvgSave(vg);

    Vector2f center(Vector2f(m_pos) + Vector2f(m_size) * 0.5f);
    float outer_radius = std::min(m_size.x(), m_size.y()) * 0.5f - 5.0f;
    float inner_radius = outer_radius * .75f;
    float u = std::min(std::max(outer_radius/50, 1.5f), 2.f);

    float aeps = 0.5f / outer_radius;   // half a pixel arc length in radians (2pi cancels out).

    if (m_visible_componets & WHEEL)
    {
        for (int i = 0; i < 6; i++) {
            float a0 = (float)i / 6.0f * NVG_PI * 2.0f - aeps;
            float a1 = (float)(i+1.0f) / 6.0f * NVG_PI * 2.0f + aeps;
            nvgBeginPath(vg);
            nvgArc(vg, center.x(), center.y(), inner_radius, a0, a1, NVG_CW);
            nvgArc(vg, center.x(), center.y(), outer_radius, a1, a0, NVG_CCW);
            nvgClosePath(vg);
            float ax = center.x() + cosf(a0) * (inner_radius + outer_radius) * 0.5f;
            float ay = center.y() + sinf(a0) * (inner_radius + outer_radius) * 0.5f;
            float bx = center.x() + cosf(a1) * (inner_radius + outer_radius) * 0.5f;
            float by = center.y() + sinf(a1) * (inner_radius + outer_radius) * 0.5f;
            paint = nvgLinearGradient(vg, ax, ay, bx, by,
                                    nvgHSLA(a0 / (NVG_PI * 2), 1.0f, 0.55f, 255),
                                    nvgHSLA(a1 / (NVG_PI * 2), 1.0f, 0.55f, 255));
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }

        nvgBeginPath(vg);
        nvgCircle(vg, center.x(), center.y(), inner_radius - 0.5f);
        nvgCircle(vg, center.x(), center.y(), outer_radius + 0.5f);

        // Selector
        nvgSave(vg);
        nvgTranslate(vg, center.x(),center.y());
        nvgRotate(vg, hue*NVG_PI*2);

        // Marker on
        nvgStrokeWidth(vg, u);
        nvgBeginPath(vg);
        nvgRect(vg, inner_radius-1,-2*u,outer_radius-inner_radius+2,4*u);
        nvgStrokeColor(vg, nvgRGBA(255,255,255,192));
        nvgStroke(vg);

        paint = nvgBoxGradient(vg, inner_radius-3,-5,outer_radius-inner_radius+6,10, 2,4, nvgRGBA(0,0,0,128), nvgRGBA(0,0,0,0));
        nvgBeginPath(vg);
        nvgRect(vg, inner_radius-2-10,-4-10,outer_radius-inner_radius+4+20,8+20);
        nvgRect(vg, inner_radius-2,-4,outer_radius-inner_radius+4,8);
        nvgPathWinding(vg, NVG_HOLE);
        nvgFillPaint(vg, paint);
        nvgFill(vg);

        nvgRestore(vg);
    }


    nvgSave(vg);
    nvgTranslate(vg, center.x(),center.y());

    // four corner circles
    Color current = color();
    Color colors[2][2] = {{Color(1.f, 1.f), Color(current.r(), current.g(), current.b(), 1.f)},
                          {Color(0.f, 1.f), Color(current.r(), current.g(), current.b(), 0.f)}};
    int corner_components[2][2] = {{ColorWheel2::WHITE, ColorWheel2::OPAQUE},
                                   {ColorWheel2::BLACK, ColorWheel2::TRANS}};
    float cr = outer_radius * std::sqrt(2.f) * 0.1f;


    auto checker = hdrview_image_icon(screen()->nvg_context(), checker4,
                                 NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_NEAREST);
    int checker_w, checker_h;
    nvgImageSize(vg, checker, &checker_w, &checker_h);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
        {
            if (!(m_visible_componets & corner_components[j][i]))
                continue;
            Vector2f circle_center = {(i * 2 - 1) * (outer_radius - cr),
                                      (j * 2 - 1) * (outer_radius - cr)};
            nvgStrokeWidth(vg, u);
            nvgBeginPath(vg);
            nvgCircle(vg, circle_center.x(), circle_center.y(), cr);
            NVGpaint paint = nvgImagePattern(vg, circle_center.x(), circle_center.y(),
                                             checker_w, checker_h, 0, checker,
                                             m_enabled ? 0.5f : 0.25f);
            nvgFillPaint(vg, paint);
            nvgFill(vg);

            nvgFillColor(vg, colors[j][i]);
            nvgStrokeColor(vg, nvgRGBA(192,192,192,255));
            nvgFill(vg);
            nvgStroke(vg);
        }

    // Center square
    if (m_visible_componets & ColorWheel2::PATCH)
    {
        float r = (inner_radius - 2)/std::sqrt(2);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, -r, -r, r*2, r*2, 2);

        paint = nvgLinearGradient(vg, -r, 0, r, 0, nvgRGBA(255, 255, 255, 255), nvgHSLA(hue, 1.0f, 0.5f, 255));
        nvgFillPaint(vg, paint);
        nvgFill(vg);
        paint = nvgLinearGradient(vg, 0, r, 0, -r, nvgRGBA(0, 0, 0, 255), nvgRGBA(0, 0, 0, 0));
        nvgFillPaint(vg, paint);
        nvgFill(vg);

        // Select circle on square
        float sy = 2*r*(1.f - m_value - 0.5f);
        float sx = 2*r*(m_saturation - 0.5f);
        nvgStrokeWidth(vg, u);
        nvgBeginPath(vg);
        nvgCircle(vg, sx, sy, std::min(10.f, outer_radius/10));
        nvgStrokeColor(vg, nvgRGBA(255,255,255,192));
        nvgFillColor(vg, current);
        nvgFill(vg);
        nvgStroke(vg);
    }

    nvgRestore(vg);
    nvgRestore(vg);
}

bool ColorWheel2::mouse_motion_event(const Vector2i &p, const Vector2i & /*rel*/, int /*button*/, int /*modifiers*/)
{
    return adjust_position(p, All, false) != None;
}

bool ColorWheel2::mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) {
    Widget::mouse_button_event(p, button, down, modifiers);
    if (!m_enabled || button != GLFW_MOUSE_BUTTON_1)
        return false;

    if (down) {
        m_drag_region = adjust_position(p);
        return m_drag_region != None;
    } else {
        adjust_position(p, All, false);
        m_drag_region = None;
        return true;
    }
}

bool ColorWheel2::mouse_drag_event(const Vector2i &p, const Vector2i &,
                                int, int) {
    return adjust_position(p, m_drag_region) != None;
}

ColorWheel2::Region ColorWheel2::adjust_position(const Vector2i &p, Region considered_regions, bool adjust) {
    
    Vector2f mouse = p - m_pos;
    float outer_radius = std::min(m_size.x(), m_size.y()) * 0.5f - 5.0f;
    float inner_radius = outer_radius * .75f;
    mouse -= Vector2f(m_size) * 0.5f;
    float mouse_radius = norm(mouse);

    set_tooltip("");

    if ((considered_regions & HueCircle) &&
        ((mouse_radius >= inner_radius && mouse_radius <= outer_radius) || (considered_regions == HueCircle))) {
        if (!(considered_regions & HueCircle))
            return None;

        set_tooltip("Select a hue for the color by dragging in this circle.");

        if (adjust)
        {
            m_hue = std::atan(mouse.y() / mouse.x());
            if (mouse.x() < 0)
                m_hue += NVG_PI;
            m_hue /= 2*NVG_PI;

            if (m_callback)
                m_callback(color());
        }
        return HueCircle;
    }

    float r = (inner_radius - 2)/std::sqrt(2.f);
    
    bool square_test = fabs(mouse.x()) < r && fabs(mouse.y()) < r;

    if ((considered_regions & InnerPatch) &&
        (square_test || considered_regions == InnerPatch)) {
        if (!(considered_regions & InnerPatch))
            return None;

        set_tooltip("Select the saturation and value by dragging in this square.");
            
        if (adjust)
        {
            m_saturation = std::min(std::max(0.f, 0.5f*(mouse.x()+r)/r), 1.f);
            m_value = std::min(std::max(0.f, 0.5f*(r-mouse.y())/r), 1.f);
            if (m_callback)
                m_callback(color());
        }
        return InnerPatch;
    }

    if (considered_regions & Circles) {

        Color current = color();
        Region corners[2][2] = {{TLCircle, TRCircle},
                                {BLCircle, BRCircle}};
        Color colors[2][2] = {{Color(1.f, 1.f), Color(current.r(), current.g(), current.b(), 1.f)},
                              {Color(0.f, 1.f), Color(current.r(), current.g(), current.b(), 0.f)}};
        std::string tips[2][2] = {{"Set to white.", "Set alpha to 1."},
                                  {"Set to black.", "Set alpha to 0."}};
        float cr = outer_radius * std::sqrt(2.f) * 0.1f;
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
            {
                Vector2f circle_center = {(i * 2 - 1) * (outer_radius - cr),
                                          (j * 2 - 1) * (outer_radius - cr)};
                if (squared_norm(circle_center - mouse) > cr*cr)
                    continue;

                set_tooltip(tips[j][i]);

                if (adjust)
                {
                    set_color(colors[j][i]);
                    if (m_callback)
                        m_callback(color());
                }
                return corners[j][i];
            }
    }

    return None;
}

Color ColorWheel2::hue2rgb(float h) const {
    float s = 1., v = 1.;

    if (h < 0) h += 1;

    int i = int(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    float r = 0, g = 0, b = 0;
    switch (i % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        case 5: r = v, g = p, b = q; break;
    }

    return { r, g, b, 1.f };
}

Color ColorWheel2::color() const {
    Color rgb    = hue2rgb(m_hue);
    Color black  { 0.f, 0.f, 0.f, 1.f };
    Color white  { 1.f, 1.f, 1.f, 1.f };
    Color ret = (m_saturation*rgb + (1.f-m_saturation) * white) * m_value;
    ret.a() = m_alpha;
    return ret;
}

void ColorWheel2::set_color(const Color &rgb) {
    float r = rgb[0], g = rgb[1], b = rgb[2];

    float M = std::max({ r, g, b });
    float m = std::min({ r, g, b });

    if (M == m) {
        float l = M;
        m_hue = 0.f;
        m_saturation = 0.f;
        m_value = l;
    } else {
        float d = M - m, h;

        if (M == r)
            h = (g - b) / d + (g < b ? 6 : 0);
        else if (M == g)
            h = (b - r) / d + 2;
        else
            h = (r - g) / d + 4;
        h /= 6;

        m_value = M;
        m_saturation = (M != 0.f) ? d / M : 0.f;
        m_hue = h;
    }

    m_alpha = rgb.a();
}

NAMESPACE_END(nanogui)

