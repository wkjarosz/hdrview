/*
    src/colorpicker.cpp -- push button with a popup to tweak a color value

    This widget was contributed by Christian Schueller.

    NanoGUI was developed by Wenzel Jakob <wenzel.jakob@epfl.ch>.
    The widget drawing code is based on the NanoVG demo application
    by Mikko Mononen.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include "hdrcolorpicker.h"
#include "colorslider.h"
#include <nanogui/textbox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/colorwheel.h>
#include <spdlog/spdlog.h>
#include <iostream>

NAMESPACE_BEGIN(nanogui)

HDRColorPicker::HDRColorPicker(Widget *parent, const Color& color, float exposure, int components)
    : PopupButton(parent, ""),
    m_color(color), m_previous_color(color),
    m_exposure(exposure), m_previous_exposure(0.f)
{
    set_background_color(m_color);
    Popup *popup = this->popup();
    popup->set_layout(new GroupLayout());

    // initialize callback to do nothing; this is for users to hook into
    // receiving a new color value
    m_callback = [](const Color &, float) {};
    m_final_callback = [](const Color &, float) {};

    // set the color wheel to the specified color
    m_color_wheel = new ColorWheel2(popup, m_color, components);

    // add the sub-widget that contains the color sliders
    auto panel = new Widget(popup);
	auto agrid = new AdvancedGridLayout({0, 20, 0}, {});
	agrid->set_margin(0);
	agrid->set_col_stretch(1, 1);
	panel->set_layout(agrid);

    // set the pick button to the specified color
    m_pick_button = new Button(popup, "Pick");
    m_pick_button->set_background_color(m_color);
    m_pick_button->set_text_color(m_color.contrasting_color());

    // set the reset button to the specified color
    m_reset_button = new Button(popup, "Reset");
    m_reset_button->set_background_color(m_color);
    m_reset_button->set_text_color(m_color.contrasting_color());
    m_reset_button->set_visible(components & RESET_BTN);

    PopupButton::set_change_callback([&](bool) {
        if (m_pick_button->pushed()) {
            set_color(m_color);
            m_final_callback(m_color, m_exposure);
        }
    });

	
    // create the color sliders and hook up callbacks

	//
	// RGBA
	//

	std::string channel_names[] = {"Red", "Green", "Blue", "Alpha", "Exposure"};
    int slider_components[] = {R_SLIDER, G_SLIDER, B_SLIDER, A_SLIDER, E_SLIDER};
    int box_components[] = {R_BOX, G_BOX, B_BOX, A_BOX, E_BOX};
	std::vector<ColorSlider*> sliders;
	std::vector<FloatBox<float>*> float_boxes;
	for (int c = 0; c < 5; ++c)
	{
        std::string tip = fmt::format("Change the color's {} value", channel_names[c]);
		agrid->append_row(0);
        auto l = new Label(panel, channel_names[c] + ":");
		agrid->set_anchor(l, AdvancedGridLayout::Anchor(0, agrid->row_count()-1));

		auto float_box = new FloatBox<float>(panel, c < 4 ? m_color[c] : 0.f);
		agrid->set_anchor(float_box, AdvancedGridLayout::Anchor(2, agrid->row_count()-1));
		float_box->number_format("%1.3f");
		float_box->set_editable(true);
        std::pair<float, float> range = {0.f, 1.f};
        if (c == 4)
            range = {-9.f, 9.f};
		float_box->set_min_value(range.first);
		float_box->set_max_value(range.second);
		float_box->set_spinnable(true);
		float_box->set_value_increment(c < 4 ? 0.01f : 0.125f);
		float_box->set_fixed_width(60);
		float_box->set_alignment(TextBox::Alignment::Right);
        float_box->set_tooltip(tip);

		agrid->append_row(0);
		auto slider = new ColorSlider(panel, m_color, ColorSlider::ColorMode(c));
		agrid->set_anchor(slider, AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));
		slider->set_color(m_color);
        slider->set_value(c < 4 ? m_color[c] : 0.f);
        slider->set_range(range);
        slider->set_tooltip(tip);

        l->set_visible(components & box_components[c]);
        float_box->set_visible(components & box_components[c]);
        slider->set_visible(components & slider_components[c]);
		if ((components & slider_components[c]) && (components & box_components[c]))
            agrid->append_row(10);

		float_boxes.push_back(float_box);
		sliders.push_back(slider);
	}

    // helper function shared by some of the callbacks below
    m_sync_helper = [float_boxes,sliders,this]() {
		for (size_t i = 0; i < sliders.size(); ++i) 
		{
			sliders[i]->set_color(m_color);
            float_boxes[i]->set_value(i < 4 ? m_color[i] : m_exposure);
            if (i == 4)
                sliders[i]->set_value(m_exposure);
		}

        auto e_color = exposed_color();
        auto t_color = e_color.contrasting_color();

        m_pick_button->set_background_color(e_color);
        m_pick_button->set_text_color(t_color);
	};

	for (size_t c = 0; c < sliders.size(); ++c)
	{
		auto callback = [this,c](float v) {
            if (c < 4)
                m_color[c] = v;
            else
                m_exposure = v;

			m_color_wheel->set_color(m_color);

            m_sync_helper();
            m_callback(m_color, m_exposure);
		};

		sliders[c]->set_callback(callback);
		float_boxes[c]->set_callback(callback);
	}

	m_color_wheel->set_callback([this](const Color &c) {
        m_color = c;
		m_sync_helper();
        m_callback(m_color, m_exposure);
	});

	m_pick_button->set_callback([this]() {
        if (m_pushed)
		{
            set_pushed(false);
            set_color(m_color_wheel->color());
            m_final_callback(m_color, m_exposure);
		}
	});

    m_reset_button->set_callback([this]() {
        update_all(m_previous_color, m_previous_exposure);

        m_callback(m_color, m_exposure);
        m_final_callback(m_color, m_exposure);
    });
}

Color HDRColorPicker::exposed_color() const {
    float gain = pow(2.f, m_exposure);
    Color e_color = m_color * gain;
    e_color.a() = m_color.a();
    return e_color;
}

void HDRColorPicker::update_all(const Color& c, float e) {
        m_color = c;
        m_exposure = e;

        m_color_wheel->set_color(m_color);

        Color e_color = exposed_color();
        Color t_color = e_color.contrasting_color();

        set_background_color(e_color);
        set_text_color(t_color);

        m_sync_helper();

        m_reset_button->set_background_color(e_color);
        m_reset_button->set_text_color(t_color);
        m_previous_color = m_color;
        m_previous_exposure = m_exposure;
}

void HDRColorPicker::set_color(const Color& color) {
    /* Ignore set_color() calls when the user is currently editing */
    if (!m_pushed) {
        update_all(color, m_exposure);
    }
}

void HDRColorPicker::set_exposure(float e) {
    /* Ignore set_exposure() calls when the user is currently editing */
    if (!m_pushed) {
        m_exposure = e;
        set_color(m_color);
    }
}

NAMESPACE_END(nanogui)
