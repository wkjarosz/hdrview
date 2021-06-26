//
// Copyright (C) 2003-2021 Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include <nanogui/icons.h>
#include <nanogui/nanogui.h>

class Tool
{
public:
    enum ETool : uint32_t
    {
        Tool_None = 0,
        Tool_Rectangular_Marquee,
        Tool_Brush,
        Tool_Eraser,
        Tool_Clone_Stamp,
        Tool_Eyedropper,
        Tool_Ruler,
        Tool_Line,
        Tool_Num_Tools
    };

    Tool(HDRViewScreen *, HDRImageView *, ImageListPanel *, const std::string &name, const std::string &tooltip,
         int icon, ETool tool);

    nlohmann::json &     all_tool_settings();
    const nlohmann::json all_tool_settings() const;
    nlohmann::json &     this_tool_settings();
    const nlohmann::json this_tool_settings() const;
    virtual void         write_settings();

    void set_options_bar(nanogui::Widget *options) { m_options = options; }

    virtual nanogui::Widget *    create_options_bar(nanogui::Widget *parent) { return m_options; }
    virtual nanogui::ToolButton *create_toolbutton(nanogui::Widget *parent);
    virtual void                 set_active(bool b);
    virtual void                 draw(NVGcontext *ctx) const;
    virtual bool                 mouse_button(const nanogui::Vector2i &p, int button, bool down, int modifiers);
    virtual bool mouse_drag(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int modifiers);
    virtual bool keyboard(int key, int scancode, int action, int modifiers);

protected:
    void draw_crosshairs(NVGcontext *ctx, const nanogui::Vector2i &p) const;

    std::string m_name;
    std::string m_tooltip;
    int         m_icon;
    ETool       m_tool;

    HDRViewScreen *      m_screen;
    HDRImageView *       m_image_view;
    ImageListPanel *     m_images_panel;
    nanogui::ToolButton *m_button;
    nanogui::Widget *    m_options;
};

class HandTool : public Tool
{
public:
    HandTool(HDRViewScreen *, HDRImageView *, ImageListPanel *, const std::string & = "Hand tool",
             const std::string & = "Pan around or zoom into the image.", int icon = FA_HAND_PAPER,
             ETool tool = Tool_None);

    virtual nanogui::Widget *create_options_bar(nanogui::Widget *parent) override;
};

class RectangularMarquee : public Tool
{
public:
    RectangularMarquee(HDRViewScreen *, HDRImageView *, ImageListPanel *, const std::string & = "Rectangular Marquee",
                       const std::string & = "Make a selection in the shape of a rectangle.", int icon = FA_EXPAND,
                       ETool tool = Tool_Rectangular_Marquee);

    virtual bool mouse_button(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool mouse_drag(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button,
                            int modifiers) override;

protected:
    nanogui::Vector2i m_roi_clicked;
};

class BrushTool : public Tool
{
public:
    BrushTool(HDRViewScreen *, HDRImageView *, ImageListPanel *, const std::string &name = "Brush tool",
              const std::string &tooltip = "Paint with the foreground or background color.", int icon = FA_PAINT_BRUSH,
              ETool tool = Tool_Brush);

    virtual void             write_settings() override;
    virtual nanogui::Widget *create_options_bar(nanogui::Widget *parent) override;
    virtual void             draw(NVGcontext *ctx) const override;
    virtual bool             mouse_button(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool             mouse_drag(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button,
                                        int modifiers) override;
    virtual bool             keyboard(int key, int scancode, int action, int modifiers) override;

protected:
    std::shared_ptr<Brush>    m_brush;
    nanogui::Slider *         m_size_slider;
    nanogui::IntBox<int> *    m_size_textbox;
    nanogui::Slider *         m_hardness_slider;
    nanogui::FloatBox<float> *m_hardness_textbox;
    nanogui::Slider *         m_flow_slider;
    nanogui::FloatBox<float> *m_flow_textbox;
    nanogui::Slider *         m_angle_slider;
    nanogui::FloatBox<float> *m_angle_textbox;
    nanogui::Slider *         m_roundness_slider;
    nanogui::FloatBox<float> *m_roundness_textbox;
    nanogui::Slider *         m_spacing_slider;
    nanogui::FloatBox<float> *m_spacing_textbox;
    nanogui::CheckBox *       m_smoothing_checkbox;
    bool                      m_smoothing = true;

    nanogui::Vector2i m_p0, m_p1;
    nanogui::Vector2i m_clicked;
};

class EraserTool : public BrushTool
{
public:
    EraserTool(HDRViewScreen *, HDRImageView *, ImageListPanel *, const std::string &name = "Eraser tool",
               const std::string &tooltip = "Makes pixels transparent.", int icon = FA_ERASER,
               ETool tool = Tool_Eraser);

    virtual bool mouse_button(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool mouse_drag(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button,
                            int modifiers) override;

protected:
};

class CloneStampTool : public BrushTool
{
public:
    CloneStampTool(HDRViewScreen *, HDRImageView *, ImageListPanel *, const std::string &name = "Clone stamp",
                   const std::string &tooltip = "Paints with pixels from another part of the image.",
                   int icon = FA_STAMP, ETool tool = Tool_Clone_Stamp);

    virtual void draw(NVGcontext *ctx) const override;
    virtual bool mouse_button(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool mouse_drag(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button,
                            int modifiers) override;

protected:
    nanogui::Vector2i m_src_click;
    nanogui::Vector2i m_dst_click;

    bool m_has_src = false;
    bool m_has_dst = false;
};

class Eyedropper : public Tool
{
public:
    Eyedropper(HDRViewScreen *, HDRImageView *, ImageListPanel *, const std::string &name = "Eyedropper",
               const std::string &tooltip = "Sample colors from the image.", int icon = FA_EYE_DROPPER,
               ETool tool = Tool_Eyedropper);

    virtual void             write_settings() override;
    virtual nanogui::Widget *create_options_bar(nanogui::Widget *parent) override;
    virtual bool             mouse_button(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool             mouse_drag(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button,
                                        int modifiers) override;
    virtual void             draw(NVGcontext *ctx) const override;

protected:
    int m_size = 0;
};

class Ruler : public Tool
{
public:
    Ruler(HDRViewScreen *, HDRImageView *, ImageListPanel *, const std::string &name = "Ruler",
          const std::string &tooltip = "Measure distances and angles.", int icon = FA_RULER, ETool tool = Tool_Ruler);

    float distance() const;
    float angle() const;

    virtual bool mouse_button(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool mouse_drag(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button,
                            int modifiers) override;
    virtual void draw(NVGcontext *ctx) const override;

protected:
    bool is_valid(const nanogui::Vector2i &p) const;

    nanogui::Vector2i m_start_pixel, m_end_pixel;
};

class LineTool : public Ruler
{
public:
    LineTool(HDRViewScreen *, HDRImageView *, ImageListPanel *, const std::string &name = "Line tool",
             const std::string &tooltip = "Draw lines.", int icon = FA_SLASH, ETool tool = Tool_Line);

    virtual void             write_settings() override;
    virtual nanogui::Widget *create_options_bar(nanogui::Widget *parent) override;

    virtual bool keyboard(int key, int scancode, int action, int modifiers) override;
    virtual bool mouse_button(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;
    virtual bool mouse_drag(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button,
                            int modifiers) override;

    virtual void draw(NVGcontext *ctx) const override;

protected:
    float                     m_width = 2.f;
    nanogui::Slider *         m_width_slider;
    nanogui::FloatBox<float> *m_width_textbox;
    bool                      m_dragging = false;
};
