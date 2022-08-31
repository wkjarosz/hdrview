//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "editimagepanel.h"
#include "box.h"             // for Box
#include "color.h"           // for Color4
#include "common.h"          // for clamp01
#include "filters/filters.h" // for create_bilateral_filter_btn, create_box...
#include "fwd.h"             // for ConstHDRImagePtr, ConstXPUImagePtr, HDR...
#include "hdrimage.h"        // for HDRImage
#include "hdrviewscreen.h"
#include "helpwindow.h"
#include "imagelistpanel.h" // for ImageListPanel
#include <commandhistory.h> // for ImageCommandResult, LambdaUndo
#include <memory>           // for shared_ptr, make_shared
#include <nanogui/button.h> // for Button
#include <nanogui/icons.h>  // for FA_ADJUST, FA_ARROWS_ALT_H, FA_ARROWS_A...
#include <nanogui/label.h>  // for Label
#include <nanogui/layout.h> // for AdvancedGridLayout, AdvancedGridLayout:...
#include <nanogui/opengl.h>
#include <nanogui/vector.h> // for Array, Color
#include <nanogui/widget.h> // for Widget
#include <utility>          // for pair
#include <well.h>           // for Well

#include <spdlog/fmt/ostr.h>

using namespace std;

void EditImagePanel::cut()
{
    auto img = m_images_panel->current_image();
    if (!img)
        return;

    auto roi = img->roi();
    if (!roi.has_volume())
        roi = img->box();

    m_clipboard = make_shared<HDRImage>(roi.size().x(), roi.size().y());
    m_clipboard->copy_paste(img->image(), roi, 0, 0, true);

    m_images_panel->async_modify_current(
        [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
        {
            return {make_shared<HDRImage>(
                        img->apply_function([](const Color4 &c) { return Color4(c.r, c.g, c.b, 0.f); }, xpuimg->roi())),
                    nullptr};
        });
}

void EditImagePanel::copy()
{
    auto img = m_images_panel->current_image();
    if (!img)
        return;

    auto roi = img->roi();
    if (!roi.has_volume())
        roi = img->box();

    m_clipboard = make_shared<HDRImage>(roi.size().x(), roi.size().y());
    m_clipboard->copy_paste(img->image(), roi, 0, 0, true);
}

void EditImagePanel::paste()
{
    auto img = m_images_panel->current_image();

    if (!img)
        return;

    auto roi = img->roi();
    if (!roi.has_volume())
        roi = img->box();

    m_images_panel->async_modify_current(
        [this, roi](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
        {
            auto result = make_shared<HDRImage>(*img);
            result->copy_paste(*m_clipboard, Box2i(), roi.min.x(), roi.min.y());
            // m_images_panel->current_image()->roi() = Box2i();
            return {result, nullptr};
        });
}

void EditImagePanel::seamless_paste()
{
    auto img = m_images_panel->current_image();

    if (!img)
        return;

    auto roi = img->roi();
    if (!roi.has_volume())
        roi = img->box();

    m_images_panel->async_modify_current(
        [this, roi](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg,
                    AtomicProgress &progress) -> ImageCommandResult
        {
            auto result = make_shared<HDRImage>(*img);
            result->seamless_copy_paste(progress, *m_clipboard, Box2i(), roi.min.x(), roi.min.y());
            // m_images_panel->current_image()->roi() = Box2i();
            return {result, nullptr};
        });
}

void EditImagePanel::fill(const Color &nfg)
{
    spdlog::trace("Filling with: {}", nfg);
    m_images_panel->async_modify_selected(
        [nfg](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
        {
            Color4 fg(nfg.r(), nfg.g(), nfg.b(), nfg.a());
            return {make_shared<HDRImage>(img->apply_function([&fg](const Color4 &c) { return fg; }, xpuimg->roi())),
                    nullptr};
        });
}

void EditImagePanel::rotate(bool clockwise)
{
    m_images_panel->async_modify_selected(
        [clockwise](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
        {
            return {make_shared<HDRImage>(clockwise ? img->rotated_90_cw() : img->rotated_90_ccw()),
                    make_shared<LambdaUndo>([clockwise](HDRImagePtr &img2)
                                            { *img2 = clockwise ? img2->rotated_90_ccw() : img2->rotated_90_cw(); },
                                            [clockwise](HDRImagePtr &img2)
                                            { *img2 = clockwise ? img2->rotated_90_cw() : img2->rotated_90_ccw(); })};
        });
}

void EditImagePanel::add_shortcuts(HelpWindow *w)
{
    auto section_name = "Edit";
    w->add_shortcut(section_name, HelpWindow::COMMAND + "+Z / " + HelpWindow::COMMAND + "+Shift+Z", "Undo/Redo");
    w->add_shortcut(section_name, HelpWindow::COMMAND + "+C", "Copy");
    w->add_shortcut(section_name, HelpWindow::COMMAND + "+V / " + HelpWindow::COMMAND + "+Shift+V",
                    "Paste/Seamless paste");
    w->add_shortcut(section_name, HelpWindow::COMMAND + "+[ / " + HelpWindow::COMMAND + "+]",
                    "Rotate counter-/clockwise");
    w->add_shortcut(section_name, HelpWindow::COMMAND + "+Delete", "Fill with background color");
}

bool EditImagePanel::keyboard_event(int key, int /* scancode */, int action, int modifiers)
{
    if (action == GLFW_RELEASE)
        return false;

    switch (key)
    {
    case 'X':
        spdlog::trace("Key `X` pressed");
        if (modifiers & SYSTEM_COMMAND_MOD)
        {
            cut();
            return true;
        }
        break;

    case 'C':
        spdlog::trace("Key `C` pressed");
        if (modifiers & SYSTEM_COMMAND_MOD)
        {
            copy();
            return true;
        }
        break;

    case 'V':
        spdlog::trace("Key `V` pressed");
        if ((modifiers & SYSTEM_COMMAND_MOD) && (modifiers & GLFW_MOD_SHIFT))
        {
            seamless_paste();
            return true;
        }
        else if (modifiers & SYSTEM_COMMAND_MOD)
        {
            paste();
            return true;
        }
        break;

    case '[':
    case ']':
        spdlog::trace("Key `[` or `]` pressed");
        if (modifiers & SYSTEM_COMMAND_MOD)
        {
            rotate(key == ']');
            return true;
        }
        break;

    case GLFW_KEY_BACKSPACE:
        spdlog::trace("Key BACKSPACE pressed");
        if (modifiers & SYSTEM_COMMAND_MOD)
        {
            fill(m_screen->background()->color());
            return true;
        }
        break;
    }

    return false;
}

EditImagePanel::EditImagePanel(Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel,
                               HDRImageView *image_view) :
    Well(parent, 1, Color(150, 32), Color(0, 50)),
    m_screen(screen), m_images_panel(images_panel),
    // m_image_view(image_view),
    m_clipboard(nullptr)
{
    const int spacing = 2;
    set_layout(new GroupLayout(10, 4, 8, 10));

    new Label(this, "History", "sans-bold");

    auto button_row = new Widget(this);
    button_row->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

    m_undo_btn = new Button(button_row, "Undo", FA_REPLY);
    m_undo_btn->set_callback([&]() { m_images_panel->undo(); });
    m_redo_btn = new Button(button_row, "Redo", FA_SHARE);
    m_redo_btn->set_callback([&]() { m_images_panel->redo(); });

    new Label(this, "Copy/Paste", "sans-bold");

    button_row = new Widget(this);
    button_row->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

    m_filter_btns.push_back(new Button(button_row, "Cut", FA_CUT));
    m_filter_btns.back()->set_callback([this]() { cut(); });

    m_filter_btns.push_back(new Button(button_row, "Copy", FA_COPY));
    m_filter_btns.back()->set_callback([this]() { copy(); });

    button_row = new Widget(this);
    button_row->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

    m_filter_btns.push_back(new Button(button_row, "Paste", FA_PASTE));
    m_filter_btns.back()->set_callback([this]() { paste(); });

    m_filter_btns.push_back(new Button(button_row, "Seamless paste", FA_PASTE));
    m_filter_btns.back()->set_callback([this]() { seamless_paste(); });

    new Label(this, "Pixel/domain transformations", "sans-bold");

    auto grid = new Widget(this);
    grid->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

    // flip h
    m_filter_btns.push_back(new Button(grid, "Flip H", FA_ARROWS_ALT_H));
    m_filter_btns.back()->set_callback(
        [&]()
        {
            m_images_panel->async_modify_selected(
                [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                {
                    return {make_shared<HDRImage>(img->flipped_horizontal()),
                            make_shared<LambdaUndo>([](HDRImagePtr &img2) { *img2 = img2->flipped_horizontal(); })};
                });
        });
    m_filter_btns.back()->set_fixed_height(21);

    // rotate cw
    m_filter_btns.push_back(new Button(grid, "Rotate CW", FA_REDO));
    m_filter_btns.back()->set_fixed_height(21);
    m_filter_btns.back()->set_callback([this]() { rotate(true); });

    // flip v
    m_filter_btns.push_back(new Button(grid, "Flip V", FA_ARROWS_ALT_V));
    m_filter_btns.back()->set_callback(
        [&]()
        {
            m_images_panel->async_modify_selected(
                [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                {
                    return {make_shared<HDRImage>(img->flipped_vertical()),
                            make_shared<LambdaUndo>([](HDRImagePtr &img2) { *img2 = img2->flipped_vertical(); })};
                });
        });
    m_filter_btns.back()->set_fixed_height(21);

    // rotate ccw
    m_filter_btns.push_back(new Button(grid, "Rotate CCW", FA_UNDO));
    m_filter_btns.back()->set_fixed_height(21);
    m_filter_btns.back()->set_callback([this]() { rotate(false); });

    // shift
    m_filter_btns.push_back(create_shift_btn(grid, m_screen, m_images_panel));
    // canvas size
    m_filter_btns.push_back(create_canvas_size_btn(grid, m_screen, m_images_panel));

    // resize
    m_filter_btns.push_back(create_resize_btn(grid, m_screen, m_images_panel));

    // crop
    m_filter_btns.push_back(new Button(grid, "Crop", FA_CROP));
    m_filter_btns.back()->set_fixed_height(21);
    m_filter_btns.back()->set_callback(
        [this]()
        {
            m_images_panel->async_modify_selected(
                [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                {
                    auto roi = xpuimg->roi();
                    if (!roi.has_volume())
                        roi = img->box();
                    HDRImage result(roi.size().x(), roi.size().y());
                    result.copy_paste(*img, roi, 0, 0);
                    xpuimg->roi() = Box2i();
                    return {make_shared<HDRImage>(result), nullptr};
                });
        });

    // remap
    m_filter_btns.push_back(create_remap_btn(grid, m_screen, m_images_panel));

    // free transform
    m_filter_btns.push_back(create_free_xform_btn(grid, m_screen, m_images_panel));

    new Label(this, "Color/range adjustments", "sans-bold");
    button_row = new Widget(this);
    auto agrid = new AdvancedGridLayout({0, spacing, 0}, {}, 0);
    agrid->set_col_stretch(0, 1.0f);
    agrid->set_col_stretch(2, 1.0f);
    button_row->set_layout(agrid);

    agrid->append_row(0);
    // invert
    m_filter_btns.push_back(new Button(button_row, "Invert", FA_IMAGE));
    m_filter_btns.back()->set_fixed_height(21);
    m_filter_btns.back()->set_callback(
        [this]()
        {
            m_images_panel->async_modify_selected(
                [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                {
                    auto roi = xpuimg->roi();
                    return {make_shared<HDRImage>(img->inverted(roi)),
                            make_shared<LambdaUndo>([roi](HDRImagePtr &img2) { *img2 = img2->inverted(roi); })};
                });
        });
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count() - 1));

    // clamp
    m_filter_btns.push_back(new Button(button_row, "Clamp", FA_ADJUST));
    m_filter_btns.back()->set_fixed_height(21);
    m_filter_btns.back()->set_callback(
        [this]()
        {
            m_images_panel->async_modify_selected(
                [](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                {
                    return {make_shared<HDRImage>(img->apply_function(
                                [](const Color4 &c)
                                { return Color4(clamp01(c.r), clamp01(c.g), clamp01(c.b), clamp01(c.a)); },
                                xpuimg->roi())),
                            nullptr};
                });
        });
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(2, agrid->row_count() - 1));

    agrid->append_row(spacing); // spacing

    //
    agrid->append_row(0);
    // m_filter_btns.push_back(create_flatten_btn(button_row, m_screen, m_images_panel));
    m_filter_btns.push_back(new Button(button_row, "Flatten", FA_CHESS_BOARD));
    m_filter_btns.back()->set_fixed_height(21);
    m_filter_btns.back()->set_callback(
        [this]()
        {
            m_images_panel->async_modify_selected(
                [this](const ConstHDRImagePtr &img, const ConstXPUImagePtr &xpuimg) -> ImageCommandResult
                {
                    Color  nbg = m_screen->background()->color();
                    Color4 bg(nbg.r(), nbg.g(), nbg.b(), nbg.a());
                    return {make_shared<HDRImage>(
                                img->apply_function([&bg](const Color4 &c) { return c.over(bg); }, xpuimg->roi())),
                            nullptr};
                });
        });
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count() - 1));
    m_filter_btns.push_back(create_fill_btn(button_row, m_screen, m_images_panel));
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(2, agrid->row_count() - 1));

    agrid->append_row(spacing); // spacing
    m_filter_btns.push_back(create_zap_gremlins_btn(button_row, m_screen, m_images_panel));
    agrid->append_row(0);
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count() - 1, 3, 1));

    agrid->append_row(spacing); // spacing
    m_filter_btns.push_back(create_channel_mixer_btn(button_row, m_screen, m_images_panel));
    agrid->append_row(0);
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count() - 1, 3, 1));

    agrid->append_row(spacing); // spacing
    m_filter_btns.push_back(create_exposure_gamma_btn(button_row, m_screen, m_images_panel));
    agrid->append_row(0);
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count() - 1, 3, 1));

    agrid->append_row(spacing); // spacing
    m_filter_btns.push_back(create_brightness_constract_btn(button_row, m_screen, m_images_panel));
    agrid->append_row(0);
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count() - 1, 3, 1));

    agrid->append_row(spacing); // spacing
    m_filter_btns.push_back(create_filmic_tonemapping_btn(button_row, m_screen, m_images_panel));
    agrid->append_row(0);
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count() - 1, 3, 1));

    agrid->append_row(spacing); // spacing
    m_filter_btns.push_back(create_hsl_btn(button_row, m_screen, m_images_panel));
    agrid->append_row(0);
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count() - 1, 3, 1));

    agrid->append_row(spacing); // spacing
    m_filter_btns.push_back(create_colorspace_btn(button_row, m_screen, m_images_panel));
    agrid->append_row(0);
    agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count() - 1, 3, 1));

    new Label(this, "Filters", "sans-bold");
    button_row = new Widget(this);
    button_row->set_layout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, spacing));
    m_filter_btns.push_back(create_gaussian_filter_btn(button_row, m_screen, m_images_panel));
    m_filter_btns.push_back(create_box_blur_btn(button_row, m_screen, m_images_panel));
    m_filter_btns.push_back(create_bilateral_filter_btn(button_row, m_screen, m_images_panel));
    m_filter_btns.push_back(create_unsharp_mask_filter_btn(button_row, m_screen, m_images_panel));
    m_filter_btns.push_back(create_median_filter_btn(button_row, m_screen, m_images_panel));
}

void EditImagePanel::draw(NVGcontext *ctx)
{
    auto img = m_images_panel->current_image();

    bool can_modify = img && img->can_modify();

    if (enabled() != can_modify)
    {
        set_enabled(can_modify);
        for (auto btn : m_filter_btns) btn->set_enabled(can_modify);
    }

    m_undo_btn->set_enabled(can_modify && img->has_undo());
    m_redo_btn->set_enabled(can_modify && img->has_redo());

    Well::draw(ctx);
}