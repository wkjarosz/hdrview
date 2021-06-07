//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrviewscreen.h"
#include "editimagepanel.h"
#include "imagelistpanel.h"
#include "hdrimageview.h"
#include "common.h"
#include "commandhistory.h"
#include "helpwindow.h"
#include "xpuimage.h"
#include "hdrcolorpicker.h"
#include <iostream>
#include <nanogui/opengl.h>
#define NOMINMAX
#include <tinydir.h>
#include <thread>
#include <spdlog/spdlog.h>

using namespace std;

HDRViewScreen::HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args) :
    Screen(nanogui::Vector2i(800,600), "HDRView", true)
{
    set_background(Color(0.23f, 1.0f));

    auto thm = new Theme(m_nvg_context);
    thm->m_standard_font_size     = 16;
    thm->m_button_font_size       = 15;
    thm->m_text_box_font_size     = 14;
	thm->m_window_corner_radius   = 4;
	thm->m_window_fill_unfocused  = Color(40, 250);
	thm->m_window_fill_focused    = Color(45, 250);
	// thm->m_button_corner_radius   = 4;
	// thm->m_border_light			  = thm->m_transparent;
	// thm->m_border_dark			  = thm->m_transparent;
	// // thm->m_button_gradient_top_focused = thm->m_transparent;
    // thm->m_button_gradient_bot_focused = thm->m_button_gradient_top_focused;
    // // thm->m_button_gradient_top_unfocused = thm->m_transparent;
    // thm->m_button_gradient_bot_unfocused = thm->m_button_gradient_top_unfocused;
    // // thm->m_button_gradient_top_pushed = thm->m_transparent;
    // thm->m_button_gradient_bot_pushed = thm->m_button_gradient_top_pushed;
    set_theme(thm);

	auto panelTheme = new Theme(m_nvg_context);
	panelTheme = new Theme(m_nvg_context);
	panelTheme->m_standard_font_size     = 16;
	panelTheme->m_button_font_size       = 15;
	panelTheme->m_text_box_font_size     = 14;
	panelTheme->m_window_corner_radius   = 0;
	panelTheme->m_window_fill_unfocused  = Color(50, 255);
	panelTheme->m_window_fill_focused    = Color(52, 255);
	panelTheme->m_window_header_height   = 0;
	panelTheme->m_window_drop_shadow_size = 0;
	// panelTheme->m_button_corner_radius   = 4;
	// panelTheme->m_border_light			  = panelTheme->m_transparent;
	// panelTheme->m_border_dark			  = panelTheme->m_transparent;
	// // panelTheme->m_button_gradient_top_focused = panelTheme->m_transparent;
    // panelTheme->m_button_gradient_bot_focused = panelTheme->m_button_gradient_top_focused;
    // panelTheme->m_button_gradient_top_unfocused = panelTheme->m_transparent;
    // panelTheme->m_button_gradient_bot_unfocused = panelTheme->m_transparent;
    // // panelTheme->m_button_gradient_top_pushed = panelTheme->m_transparent;
    // panelTheme->m_button_gradient_bot_pushed = panelTheme->m_button_gradient_top_pushed;

	auto flat_theme = new Theme(m_nvg_context);
	flat_theme = new Theme(m_nvg_context);
	flat_theme->m_standard_font_size     		= 16;
	flat_theme->m_button_font_size       		= 15;
	flat_theme->m_text_box_font_size     		= 14;
	flat_theme->m_window_corner_radius   		= 0;
	flat_theme->m_window_fill_unfocused  		= Color(50, 255);
	flat_theme->m_window_fill_focused    		= Color(52, 255);
	flat_theme->m_window_header_height   		= 0;
	flat_theme->m_window_drop_shadow_size 		= 0;
	flat_theme->m_button_corner_radius   		= 4;
	flat_theme->m_border_light			  		= flat_theme->m_transparent;
	flat_theme->m_border_dark			  		= flat_theme->m_transparent;
	flat_theme->m_button_gradient_top_focused 	= flat_theme->m_transparent;
    flat_theme->m_button_gradient_bot_focused 	= flat_theme->m_button_gradient_top_focused;
    flat_theme->m_button_gradient_top_unfocused = flat_theme->m_transparent;
    flat_theme->m_button_gradient_bot_unfocused = flat_theme->m_transparent;
    flat_theme->m_button_gradient_top_pushed 	= flat_theme->m_transparent;
    flat_theme->m_button_gradient_bot_pushed 	= flat_theme->m_button_gradient_top_pushed;


	//
	// Construct the top-level widgets
	//

	m_top_panel = new Window(this, "");
	m_top_panel->set_theme(panelTheme);
	m_top_panel->set_position(nanogui::Vector2i(0,0));
	m_top_panel->set_fixed_height(30);
	m_top_panel->set_layout(new BoxLayout(Orientation::Horizontal,
	                                    Alignment::Middle, 5, 5));

	m_side_panel = new Window(this, "");
	m_side_panel->set_theme(panelTheme);

	m_tool_panel = new Window(this, "");
	m_tool_panel->set_theme(panelTheme);
	m_tool_panel->set_fixed_width(32);
	m_tool_panel->set_layout(new BoxLayout(Orientation::Vertical,
	                                       Alignment::Fill, 4, 4));
	auto b = new ToolButton(m_tool_panel, FA_HAND_PAPER);
	b->set_pushed(true);
	b->set_flags(Button::Flags::RadioButton);
	b->set_callback([this]{m_tool = HDRViewScreen::Tool_None;});
	b->set_tooltip("Switch to default zoom/pan mode.");
	b->set_icon_extra_scale(1.5f);
	m_toolbuttons.push_back(b);
	
	b = new ToolButton(m_tool_panel, FA_EXPAND);
	b->set_flags(Button::Flags::RadioButton);
	b->set_tooltip("Switch to rectangular marquee selection mode.");
	b->set_callback([this]{m_tool = HDRViewScreen::Tool_Rectangular_Marquee;});
	b->set_icon_extra_scale(1.5f);
	m_toolbuttons.push_back(b);

	m_info_btn = new PopupButton(m_tool_panel, "", FA_INFO_CIRCLE);
	m_info_btn->set_flags(Button::Flags::ToggleButton);
	m_info_btn->set_tooltip("Show info");
	m_info_btn->set_side(Popup::Left);
	m_info_btn->set_chevron_icon(0);
	m_info_btn->set_icon_extra_scale(1.5f);

	// create info popup
	{
		m_info_btn->popup()->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 5, 5));
		auto well = new Well(m_info_btn->popup(), 1, Color(150, 32), Color(0, 50));
		
		well->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 10, 5));

		auto row = new Widget(well);
		row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

		new Label(row, "File:", "sans-bold");
		m_path_info_label = new Label(row, "");
		m_path_info_label->set_fixed_width(135);

		row = new Widget(well);
		row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

		new Label(row, "Resolution:", "sans-bold");
		m_res_info_label = new Label(row, "");
		m_res_info_label->set_fixed_width(135);

		// spacer
		(new Widget(well))->set_fixed_height(5);

		row = new Widget(well);
		row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

		auto tb = new ToolButton(row, FA_EYE_DROPPER);
		tb->set_theme(flat_theme);
		tb->set_enabled(false);
		tb->set_icon_extra_scale(1.5f);

		(new Label(row, "R:\nG:\nB:\nA:", "sans-bold"))->set_fixed_width(15);
		m_color32_info_label = new Label(row, "");
		m_color32_info_label->set_fixed_width(50+24+5);
		m_color8_info_label = new Label(row, "");
		m_color8_info_label->set_fixed_width(50);

		// spacer
		(new Widget(well))->set_fixed_height(5);

		row = new Widget(well);
		row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

		tb = new ToolButton(row, FA_CROSSHAIRS);
		tb->set_theme(flat_theme);
		tb->set_enabled(false);
		tb->set_icon_extra_scale(1.5f);

		(new Label(row, "X:\nY:", "sans-bold"))->set_fixed_width(15);
		m_pixel_info_label = new Label(row, "");
		m_pixel_info_label->set_fixed_width(50);

		tb = new ToolButton(row, FA_EXPAND);
		tb->set_theme(flat_theme);
		tb->set_enabled(false);
		tb->set_icon_extra_scale(1.5f);

		(new Label(row, "W:\nH:", "sans-bold"))->set_fixed_width(20);
		m_roi_info_label = new Label(row, "");
		m_roi_info_label->set_fixed_width(50);

		// spacer
		(new Widget(well))->set_fixed_height(5);

		row = new Widget(well);
		row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

		tb = new ToolButton(row, FA_PERCENTAGE);
		tb->set_theme(flat_theme);
		tb->set_enabled(false);
		tb->set_icon_extra_scale(1.5f);

		(new Label(row, "Min:\nAvg:\nMax:", "sans-bold"))->set_fixed_width(30);
		m_stats_label = new Label(row, "");
		m_stats_label->set_fixed_width(135);
	}


	m_image_view = new HDRImageView(this);
	m_image_view->set_grid_threshold(10);
	m_image_view->set_pixel_info_threshold(40);

	m_status_bar = new Window(this, "");
	m_status_bar->set_theme(panelTheme);
	m_status_bar->set_fixed_height(m_status_bar->theme()->m_text_box_font_size+1);

    //
    // create status bar widgets
    //

	m_status_label = new Label(m_status_bar, "", "sans");
	m_status_label->set_font_size(thm->m_text_box_font_size);
	m_status_label->set_position(nanogui::Vector2i(6, 0));

	m_zoom_label = new Label(m_status_bar, "100% (1 : 1)", "sans");
	m_zoom_label->set_font_size(thm->m_text_box_font_size);

    //
    // create side panel widgets
    //

	m_side_scroll_panel = new VScrollPanel(m_side_panel);
	m_side_panel_contents = new Widget(m_side_scroll_panel);
	m_side_panel_contents->set_layout(new BoxLayout(Orientation::Vertical,
	                                             Alignment::Fill, 4, 4));
	m_side_panel_contents->set_fixed_width(215);
	m_side_scroll_panel->set_fixed_width(m_side_panel_contents->fixed_width() + 12);
	m_side_panel->set_fixed_width(m_side_scroll_panel->fixed_width());


    //
    // create file/images panel
    //

    auto btn = new Button(m_side_panel_contents, "File", FA_CARET_DOWN);
	btn->set_theme(flat_theme);
	btn->set_flags(Button::ToggleButton);
	btn->set_pushed(true);
	btn->set_font_size(18);
	btn->set_icon_position(Button::IconPosition::Left);
    m_images_panel = new ImageListPanel(m_side_panel_contents, this, m_image_view);

    //
    // create edit panel
    //

	auto btn2 = new Button(m_side_panel_contents, "Edit", FA_CARET_RIGHT);
	btn2->set_theme(flat_theme);
	btn2->set_flags(Button::ToggleButton);
	btn2->set_font_size(18);
	btn2->set_icon_position(Button::IconPosition::Left);

	m_edit_panel = new EditImagePanel(m_side_panel_contents, this, m_images_panel);
	m_edit_panel->set_fixed_height(4);

	//
	// image and edit panel callbacks
	//
	
	auto toggle_panel = [this](Button * btn1, Button * btn2, Widget * panel1, Widget * panel2, bool value)
		{
			btn1->set_icon(value ? FA_CARET_DOWN : FA_CARET_RIGHT);
			panel1->set_visible(true);

			panel1->set_fixed_height(value ? 0 : 4);

			// close other panel
			if (value)
			{
				btn2->set_pushed(false);
				btn2->set_icon(FA_CARET_RIGHT);
				panel2->set_fixed_height(4);
			}

			request_layout_update();
			m_side_panel_contents->perform_layout(m_nvg_context);
		};

	btn->set_change_callback([this,btn,btn2,toggle_panel](bool value){toggle_panel(btn, btn2, m_images_panel, m_edit_panel, value);});
	btn2->set_change_callback([this,btn,btn2,toggle_panel](bool value){toggle_panel(btn2, btn, m_edit_panel, m_images_panel, value);});

    //
    // create top panel controls
    //

	m_help_button = new Button{m_top_panel, "", FA_QUESTION};
	m_help_button->set_fixed_size(nanogui::Vector2i(25, 25));
	m_help_button->set_change_callback([this](bool) { toggle_help_window(); });
	m_help_button->set_tooltip("Information about using HDRView.");
	m_help_button->set_flags(Button::ToggleButton);

    m_side_panel_button = new Button(m_top_panel, "", FA_BARS);
    new Label(m_top_panel, "EV", "sans-bold");
    auto exposure_slider = new Slider(m_top_panel);
    auto exposure_textbox = new FloatBox<float>(m_top_panel, exposure);
	auto normalize_button = new Button(m_top_panel, "", FA_MAGIC);
	normalize_button->set_fixed_size(nanogui::Vector2i(19, 19));
	normalize_button->set_callback([this]()
	                             {
		                             auto img = m_images_panel->current_image();
		                             if (!img)
			                             return;
		                             Color4 mC = img->image().max();
		                             float mCf = max(mC[0], mC[1], mC[2]);
		                             spdlog::debug("max value: {}", mCf);
		                             m_image_view->set_exposure(log2(1.0f/mCf));
		                             m_images_panel->request_histogram_update(true);
	                             });
	normalize_button->set_tooltip("Normalize exposure.");
	auto reset_button = new Button(m_top_panel, "", FA_SYNC);
	reset_button->set_fixed_size(nanogui::Vector2i(19, 19));
	reset_button->set_callback([this]()
	                             {
		                             m_image_view->set_exposure(0.0f);
		                             m_image_view->set_gamma(2.2f);
		                             m_image_view->set_sRGB(true);
		                             m_images_panel->request_histogram_update(true);
	                             });
	reset_button->set_tooltip("Reset tonemapping.");

    auto sRGB_checkbox = new CheckBox(m_top_panel, "sRGB   ");
    auto gamma_label = new Label(m_top_panel, "Gamma", "sans-bold");
    auto gamma_slider = new Slider(m_top_panel);
    auto gamma_textbox = new FloatBox<float>(m_top_panel);

    m_side_panel_button->set_tooltip("Bring up the images dialog to load/remove images, and cycle through open images.");
    m_side_panel_button->set_flags(Button::ToggleButton);
	m_side_panel_button->set_pushed(true);
    m_side_panel_button->set_fixed_size(nanogui::Vector2i(25, 25));
    m_side_panel_button->set_change_callback([this](bool value)
    {
	    m_gui_animation_start = glfwGetTime();
		push_gui_refresh();
		m_animation_running = true;
	    m_animation_goal = EAnimationGoal(m_animation_goal ^ SIDE_PANEL);
		request_layout_update();
    });

    exposure_textbox->number_format("%1.2f");
    exposure_textbox->set_editable(true);
	exposure_textbox->set_spinnable(true);
    exposure_textbox->set_fixed_width(50);
	exposure_textbox->set_min_value(-9.0f);
	exposure_textbox->set_max_value( 9.0f);
    exposure_textbox->set_alignment(TextBox::Alignment::Right);
    exposure_textbox->set_callback([this](float e)
                                 {
	                                 m_image_view->set_exposure(e);
                                 });
    exposure_slider->set_callback([this](float v)
						        {
							        m_image_view->set_exposure(round(4*v) / 4.0f);
						        });
	exposure_slider->set_final_callback([this](float v)
	                                 {
		                                 m_image_view->set_exposure(round(4*v) / 4.0f);
		                                 m_images_panel->request_histogram_update(true);
	                                 });
    exposure_slider->set_fixed_width(100);
    exposure_slider->set_range({-9.0f,9.0f});
    exposure_textbox->set_value(exposure);

    gamma_textbox->set_editable(true);
	gamma_textbox->set_spinnable(true);
    gamma_textbox->number_format("%1.3f");
    gamma_textbox->set_fixed_width(55);
	gamma_textbox->set_min_value(0.02f);
	gamma_textbox->set_max_value(9.0f);

    gamma_textbox->set_alignment(TextBox::Alignment::Right);
    gamma_textbox->set_callback([this,gamma_slider](float value)
                                {
                                    m_image_view->set_gamma(value);
                                    gamma_slider->set_value(value);
                                });
    gamma_slider->set_callback(
	    [&,gamma_slider,gamma_textbox](float value)
	    {
		    float g = max(gamma_slider->range().first, round(10*value) / 10.0f);
		    m_image_view->set_gamma(g);
		    gamma_textbox->set_value(g);
		    gamma_slider->set_value(g);       // snap values
	    });
    gamma_slider->set_fixed_width(100);
    gamma_slider->set_range({0.02f,9.0f});
    gamma_slider->set_value(gamma);
    gamma_textbox->set_value(gamma);

    m_image_view->set_exposure_callback([this,exposure_textbox,exposure_slider](float e)
                                     {
	                                     exposure_textbox->set_value(e);
	                                     exposure_slider->set_value(e);
	                                     m_images_panel->request_histogram_update();
                                     });
    m_image_view->set_gamma_callback([gamma_textbox,gamma_slider](float g)
                                  {
	                                  gamma_textbox->set_value(g);
	                                  gamma_slider->set_value(g);
                                  });
	m_image_view->set_sRGB_callback([sRGB_checkbox,gamma_textbox,gamma_slider](bool b)
	                              {
		                              sRGB_checkbox->set_checked(b);
		                              gamma_textbox->set_enabled(!b);
		                              gamma_textbox->set_spinnable(!b);
		                              gamma_slider->set_enabled(!b);
	                              });
    m_image_view->set_exposure(exposure);
    m_image_view->set_gamma(gamma);

    m_image_view->set_zoom_callback([this](float zoom)
                                {
                                    float realZoom = zoom * pixel_ratio();
                                    int numer = (realZoom < 1.0f) ? 1 : (int)round(realZoom);
                                    int denom = (realZoom < 1.0f) ? (int)round(1.0f/realZoom) : 1;
                                    m_zoom_label->set_caption(fmt::format("{:7.2f}% ({:d} : {:d})", realZoom * 100, numer, denom));
									request_layout_update();
                                });

	sRGB_checkbox->set_callback([&,gamma_slider,gamma_textbox,gamma_label](bool value)
    {
        m_image_view->set_sRGB(value);
        gamma_slider->set_enabled(!value);
	    gamma_textbox->set_spinnable(!value);
        gamma_textbox->set_enabled(!value);
        gamma_label->set_enabled(!value);
        gamma_label->set_color(value ? m_theme->m_disabled_text_color : m_theme->m_text_color);
		request_layout_update();
    });

	sRGB_checkbox->set_checked(sRGB);
	sRGB_checkbox->callback()(sRGB);

    (new CheckBox(m_top_panel, "Dither  ",
                 [&](bool v) { m_image_view->set_dithering(v); }))->set_checked(m_image_view->dithering_on());
    (new CheckBox(m_top_panel, "Grid  ",
                 [&](bool v) { m_image_view->set_draw_grid(v); }))->set_checked(m_image_view->draw_grid_on());
    (new CheckBox(m_top_panel, "RGB values  ",
                 [&](bool v) { m_image_view->set_draw_values(v); }))->set_checked(m_image_view->draw_values_on());


	m_image_view->set_pixel_callback(
		[this](const nanogui::Vector2i& index, char **out, size_t size) {
			auto img = m_images_panel->current_image();
			if (img)
			{
				const HDRImage & image = img->image();
				for (int ch = 0; ch < 4; ++ch)
				{
					float value = image(index.x(), index.y())[ch];
					snprintf(out[ch], size, "%f", value);
				}
			}
		}
	);

	m_image_view->set_changed_callback([this]()
		{
			if (auto img = m_images_panel->current_image())
			{
				m_path_info_label->set_caption(fmt::format("{}", img->filename()));
				m_res_info_label->set_caption(fmt::format("{} × {}", img->width(), img->height()));

				perform_layout();
			}
			else
			{
				m_path_info_label->set_caption("");
				m_res_info_label->set_caption("");
				m_stats_label->set_caption("");
			}
		}
	);

	m_image_view->set_roi_callback([this](const Box2i& roi)
		{
			m_roi_info_label->set_caption(roi.has_volume() ? fmt::format("{: 4d}\n{: 4d}", roi.size().x(), roi.size().y()) : "");
		}
	);



	drop_event(args);


	this->set_size(nanogui::Vector2i(1024, 800));
	request_layout_update();
	set_resize_callback([&](nanogui::Vector2i)
	                  {
						request_layout_update();
	                  });

    set_visible(true);
    glfwSwapInterval(1);


	// Nanogui will redraw the screen for key/mouse events, but we need to manually
	// invoke redraw for things like gui animations. do this in a separate thread
    m_gui_refresh_thread = std::thread(
        [this]()
		{
			std::chrono::microseconds idle_quantum = std::chrono::microseconds(1000 * 1000 / 20);
			std::chrono::microseconds anim_quantum = std::chrono::microseconds(1000 * 1000 / 120);
            while (true)
			{
				bool anim = this->should_refresh_gui();
				std::this_thread::sleep_for(anim ? anim_quantum : idle_quantum);
				this->redraw();
				if (anim)
					spdlog::trace("refreshing gui");
            }
        }
    );
}

HDRViewScreen::~HDRViewScreen()
{
	m_gui_refresh_thread.join();
}


void HDRViewScreen::set_tool(ETool t)
{
	m_tool = t;
	for (int i = 0; i < (int)Tool_Num_Tools; ++i)
		m_toolbuttons[i]->set_pushed(i == (int)t);
}

void HDRViewScreen::update_caption()
{
    auto img = m_images_panel->current_image();
    if (img)
        set_caption(string("HDRView [") + img->filename() + (img->is_modified() ? "*" : "") + "]");
    else
        set_caption(string("HDRView"));
}

void HDRViewScreen::bring_to_focus() const
{
    glfwFocusWindow(m_glfw_window);
}

bool HDRViewScreen::drop_event(const vector<string> & filenames)
{
	try
	{
		m_images_panel->load_images(filenames);

		bring_to_focus();

		// Ensure the new image button will have the correct visibility state.
		m_images_panel->set_filter(m_images_panel->filter());
		
		request_layout_update();
	}
	catch (const exception &e)
	{
		new MessageDialog(this, MessageDialog::Type::Warning, "Error",
		                  string("Could not load:\n ") + e.what());
		return false;
	}
	return true;
}

void HDRViewScreen::ask_close_image(int)
{
	auto closeit = [this](int curr, int next)
	{
		m_images_panel->close_image();
		cout << "curr: " << m_images_panel->current_image_index() << endl;
	};

	auto curr = m_images_panel->current_image_index();
	auto next = m_images_panel->next_visible_image(curr, Forward);
	cout << "curr: " << curr << "; next: " << next << endl;
	if (auto img = m_images_panel->image(curr))
	{
		if (img->is_modified())
		{
			auto dialog = new MessageDialog(this, MessageDialog::Type::Warning, "Warning!",
			                                "Image has unsaved modifications. Close anyway?", "Yes", "Cancel", true);
			dialog->set_callback([curr,next,closeit](int close){if (close == 0) closeit(curr,next);});
		}
		else
			closeit(curr,next);
	}
}

void HDRViewScreen::ask_close_all_images()
{
	bool anyModified = false;
	for (int i = 0; i < m_images_panel->num_images(); ++i)
		anyModified |= m_images_panel->image(i)->is_modified();

	if (anyModified)
	{
		auto dialog = new MessageDialog(this, MessageDialog::Type::Warning, "Warning!",
		                                "Some images have unsaved modifications. Close all images anyway?", "Yes", "Cancel", true);
		dialog->set_callback([this](int close){if (close == 0) m_images_panel->close_all_images();});
	}
	else
		m_images_panel->close_all_images();
}


void HDRViewScreen::toggle_help_window()
{
	if (m_help_window)
	{
		m_help_window->dispose();
		m_help_window = nullptr;
		m_help_button->set_pushed(false);
	}
	else
	{
		m_help_window = new HelpWindow{this, [this] { toggle_help_window(); }};
		m_help_window->center();
		m_help_window->request_focus();
		m_help_button->set_pushed(true);
	}

	request_layout_update();
}


bool HDRViewScreen::load_image()
{
	vector<string> files = file_dialog(
		{
			{"exr", "OpenEXR image"},
			{"dng", "Digital Negative raw image"},
			{"png", "Portable Network Graphic image"},
			{"pfm", "Portable FloatMap image"},
			{"ppm", "Portable PixMap image"},
			{"pnm", "Portable AnyMap image"},
			{"jpg", "JPEG image"},
			{"tga", "Truevision Targa image"},
			{"pic", "Softimage PIC image"},
			{"bmp", "Windows Bitmap image"},
			{"gif", "Graphics Interchange Format image"},
			{"hdr", "Radiance rgbE format image"},
			{"psd", "Photoshop document"}
		}, false, true);

	// re-gain focus
	glfwFocusWindow(m_glfw_window);

	if (!files.empty())
		return drop_event(files);
	return false;
}

void HDRViewScreen::save_image()
{
	try
	{
		if (!m_images_panel->current_image())
			return;

		string filename = file_dialog(
			{
				{"exr", "OpenEXR image"},
				{"hdr", "Radiance rgbE format image"},
				{"png", "Portable Network Graphic image"},
				{"pfm", "Portable FloatMap image"},
				{"ppm", "Portable PixMap image"},
				{"pnm", "Portable AnyMap image"},
				{"jpg", "JPEG image"},
				{"jpeg", "JPEG image"},
				{"tga", "Truevision Targa image"},
				{"bmp", "Windows Bitmap image"},
			}, true);

		// re-gain focus
		glfwFocusWindow(m_glfw_window);

		if (!filename.empty())
			m_images_panel->save_image(filename, m_image_view->exposure(), m_image_view->gamma(),
			                      m_image_view->sRGB(), m_image_view->dithering_on());
	}
	catch (const exception &e)
	{
		new MessageDialog(this, MessageDialog::Type::Warning, "Error",
		                  string("Could not save image due to an error:\n") + e.what());
	}
}


bool HDRViewScreen::keyboard_event(int key, int scancode, int action, int modifiers)
{
	if (Screen::keyboard_event(key, scancode, action, modifiers))
		return true;

    if (!action)
        return false;

    switch (key)
    {
        case GLFW_KEY_ESCAPE:
        {
            if (!m_ok_to_quit_dialog)
            {
                m_ok_to_quit_dialog = new MessageDialog(this,
                        MessageDialog::Type::Warning, "Warning!",
                        "Do you really want to quit?", "Yes", "No", true);
                m_ok_to_quit_dialog->set_callback([this](int result)
                    {
                        this->set_visible(result != 0);
                        m_ok_to_quit_dialog = nullptr;
                    });
	            m_ok_to_quit_dialog->request_focus();
            }
            else if (m_ok_to_quit_dialog->visible())
            {
                // dialog already visible, dismiss it
                m_ok_to_quit_dialog->dispose();
                m_ok_to_quit_dialog = nullptr;
            }
            return true;
        }
        case GLFW_KEY_ENTER:
        {
             if (m_ok_to_quit_dialog && m_ok_to_quit_dialog->visible())
                 // quit dialog already visible, "enter" clicks OK, and quits
                 m_ok_to_quit_dialog->callback()(0);
             else
                return true;
        }

        case 'Z':
            if (modifiers & SYSTEM_COMMAND_MOD)
            {
                if (modifiers & GLFW_MOD_SHIFT)
					m_images_panel->redo();
				else
					m_images_panel->undo();

	            return true;
            }
            return false;

        case GLFW_KEY_BACKSPACE:
			spdlog::trace("KEY BACKSPACE pressed");
	        ask_close_image(m_images_panel->current_image_index());
            return true;
        case 'W':
			spdlog::trace("KEY `W` pressed");
            if (modifiers & SYSTEM_COMMAND_MOD)
            {
	            if (modifiers & GLFW_MOD_SHIFT)
		            ask_close_all_images();
	            else
	                ask_close_image(m_images_panel->current_image_index());
                return true;
            }
            return false;

	    case 'O':
			spdlog::trace("KEY `O` pressed");
		    if (modifiers & SYSTEM_COMMAND_MOD)
		    {
			    load_image();
			    return true;
		    }
		    return false;

        case '=':
        case GLFW_KEY_KP_ADD:
			spdlog::trace("KEY `=` pressed");
			m_image_view->zoom_in();
            return true;

        case '-':
        case GLFW_KEY_KP_SUBTRACT:
			spdlog::trace("KEY `-` pressed");
			m_image_view->zoom_out();
            return true;

        case 'G':
			spdlog::trace("KEY `G` pressed");
            if (modifiers & GLFW_MOD_SHIFT)
			{
				m_image_view->set_gamma(m_image_view->gamma() + 0.02f);
			}
            else
			{
				m_image_view->set_gamma(max(0.02f, m_image_view->gamma() - 0.02f));
			}
            return true;
        case 'E':
			spdlog::trace("KEY `E` pressed");
            if (modifiers & GLFW_MOD_SHIFT)
			{
				m_image_view->set_exposure(m_image_view->exposure() + 0.25f);
			}
            else
			{
				m_image_view->set_exposure(m_image_view->exposure() - 0.25f);
			}
            return true;

        case 'F':
			spdlog::trace("KEY `F` pressed");
			if (modifiers & SYSTEM_COMMAND_MOD)
			{
				m_images_panel->focus_filter();
				return true;
			}
			break;

		case 'A':
			spdlog::trace("Key `A` pressed");
			if (modifiers & SYSTEM_COMMAND_MOD)
				m_image_view->select_all();
			break;

		case 'D':
			spdlog::trace("Key `D` pressed");
			if (modifiers & SYSTEM_COMMAND_MOD)
				m_image_view->select_none();
			break;

		case 'C':
			spdlog::trace("Key `C` pressed");
			if (modifiers & SYSTEM_COMMAND_MOD)
				m_edit_panel->copy();
			break;

		case 'V':
			spdlog::trace("Key `V` pressed");
			if (modifiers & SYSTEM_COMMAND_MOD)
				m_edit_panel->paste();
			break;

        case 'M':
			spdlog::trace("KEY `M` pressed");
            set_tool(Tool_Rectangular_Marquee);
            return true;

        case ' ':
			spdlog::trace("KEY ` ` pressed");
	        set_tool(Tool_None);
            return true;

        case 'T':
			spdlog::trace("KEY `T` pressed");
		    m_gui_animation_start = glfwGetTime();
			push_gui_refresh();
			m_animation_running = true;
		    m_animation_goal = EAnimationGoal(m_animation_goal ^ TOP_PANEL);
			request_layout_update();
            return true;

        case 'H':
			spdlog::trace("KEY `H` pressed");
		    toggle_help_window();
            return true;

        case GLFW_KEY_TAB:
			spdlog::trace("KEY TAB pressed");
	        if (modifiers & GLFW_MOD_SHIFT)
	        {
		        bool setVis = !((m_animation_goal & SIDE_PANEL) || (m_animation_goal & TOP_PANEL) || (m_animation_goal & BOTTOM_PANEL));
		        m_gui_animation_start = glfwGetTime();
				push_gui_refresh();
				m_animation_running = true;
		        m_animation_goal = setVis ? EAnimationGoal(TOP_PANEL | SIDE_PANEL | BOTTOM_PANEL) : EAnimationGoal(0);
	        }
		    else if (modifiers & GLFW_MOD_ALT)
			{
				m_images_panel->swap_current_selected_with_previous();
			}
	        else
	        {
		        m_gui_animation_start = glfwGetTime();
				push_gui_refresh();
				m_animation_running = true;
		        m_animation_goal = EAnimationGoal(m_animation_goal ^ SIDE_PANEL);
	        }

			request_layout_update();
            return true;

        case GLFW_KEY_DOWN:
	        if (modifiers & SYSTEM_COMMAND_MOD)
	        {
		        m_images_panel->send_image_backward();
		        return true;
	        }
	        else if (m_images_panel->num_images())
            {
				if (modifiers & GLFW_MOD_SHIFT)
					m_images_panel->set_reference_image_index(m_images_panel->next_visible_image(m_images_panel->reference_image_index(), Backward));
				else
					m_images_panel->set_current_image_index(m_images_panel->next_visible_image(m_images_panel->current_image_index(), Backward));
	            return true;
            }
		    return false;

        case GLFW_KEY_UP:
	        if (modifiers & SYSTEM_COMMAND_MOD)
	        {
		        m_images_panel->bring_image_forward();
		        return true;
	        }
	        else if (m_images_panel->num_images())
	        {
				if (modifiers & GLFW_MOD_SHIFT)
					m_images_panel->set_reference_image_index(m_images_panel->next_visible_image(m_images_panel->reference_image_index(), Forward));
				else
					m_images_panel->set_current_image_index(m_images_panel->next_visible_image(m_images_panel->current_image_index(), Forward));
		        return true;
	        }
		    return false;

        case GLFW_KEY_KP_0:
	    case '0':
		    if (modifiers & SYSTEM_COMMAND_MOD)
		    {
			    m_image_view->center();
			    m_image_view->fit();
			    draw_all();
			    return true;
		    }
		    return false;
    }

	if ((key >= GLFW_KEY_1 && key <= GLFW_KEY_9) || (key >= GLFW_KEY_KP_1 && key <= GLFW_KEY_KP_9))
	{
        int keyOffset = (key >= GLFW_KEY_KP_1) ? GLFW_KEY_KP_1 : GLFW_KEY_1;
        int idx = (key - keyOffset) % 10;

		if (modifiers & SYSTEM_COMMAND_MOD && idx < NUM_CHANNELS)
		{
			m_images_panel->set_channel(EChannel(idx));
			return true;
		}
		else if (modifiers & GLFW_MOD_SHIFT && idx < NUM_BLEND_MODES)
		{
			m_images_panel->set_blend_mode(EBlendMode(idx));
			return true;
		}
		else
		{
			auto nth = m_images_panel->nth_visible_image_index(idx);
			if (nth >= 0)
				m_images_panel->set_current_image_index(nth);
		}
		return false;
	}

    return false;
}

bool HDRViewScreen::mouse_button_event(const nanogui::Vector2i &p, int button, bool down, int modifiers)
{
	if (button == GLFW_MOUSE_BUTTON_1 && down && at_side_panel_edge(p))
	{
		m_dragging_side_panel = true;

		// prevent Screen::cursorPosCallbackEvent from calling dragEvent on other widgets
		m_drag_active = false;
		m_drag_widget = nullptr;
		return true;
	}
	else
		m_dragging_side_panel = false;

	if (m_active_colorpicker && down)
	{
		auto tmp = m_active_colorpicker;
		m_active_colorpicker = nullptr;
		tmp->close_eyedropper();
		spdlog::trace("closing eyedropper");
	}

	return Screen::mouse_button_event(p, button, down, modifiers);
}

bool HDRViewScreen::mouse_motion_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int modifiers)
{
	ConstImagePtr img = m_images_panel->current_image();
	if (img)
	{
		nanogui::Vector2i pixel(m_image_view->image_coordinate_at((p - m_image_view->position())));
		const HDRImage & image = img->image();

		if (image.contains(pixel.x(), pixel.y()))
		{
			Color4 color32 = image(pixel.x(), pixel.y());
			Color4 color8 = (color32 * pow(2.f, m_image_view->exposure()) * 255).min(255.f).max(0.f);
			string s = fmt::format(
				"({: 4d},{: 4d}) = ({: 6.3f}, {: 6.3f}, {: 6.3f}, {: 6.3f}) / ({: 3d}, {: 3d}, {: 3d}, {: 3d})",
				pixel.x(), pixel.y(),
				color32[0], color32[1], color32[2], color32[3],
				(int) round(color8[0]), (int) round(color8[1]),
				(int) round(color8[2]), (int) round(color8[3]));
			m_status_label->set_caption(s);
			
			string s2 = fmt::format(
				"{: 6.3f}\n{: 6.3f}\n{: 6.3f}\n{: 6.3f}",
				color32[0], color32[1], color32[2], color32[3]);
			m_color32_info_label->set_caption(s2);

			string s3 = fmt::format(
				"{: 3d}\n{: 3d}\n{: 3d}\n{: 3d}",
				(int) round(color8[0]), (int) round(color8[1]),
				(int) round(color8[2]), (int) round(color8[3]));
			m_color8_info_label->set_caption(s3);

			m_pixel_info_label->set_caption(fmt::format("{: 4d}\n{: 4d}", pixel.x(), pixel.y()));

			if (m_active_colorpicker)
				m_active_colorpicker->set_color(Color(color32[0], color32[1], color32[2], color32[3]));
		}
		else
		{
			m_status_label->set_caption("");
			m_color32_info_label->set_caption("");
			m_color8_info_label->set_caption("");
			m_pixel_info_label->set_caption("");
		}

		m_status_bar->perform_layout(m_nvg_context);
	}

	if (m_dragging_side_panel || at_side_panel_edge(p))
	{
		m_side_panel->set_cursor(Cursor::HResize);
		m_side_scroll_panel->set_cursor(Cursor::HResize);
		m_side_panel_contents->set_cursor(Cursor::HResize);
		m_image_view->set_cursor(Cursor::HResize);
	}
	else
	{
		m_side_panel->set_cursor(Cursor::Arrow);
		m_side_scroll_panel->set_cursor(Cursor::Arrow);
		m_side_panel_contents->set_cursor(Cursor::Arrow);
		m_image_view->set_cursor(Cursor::Arrow);
	}

	if (m_dragging_side_panel)
	{
		int w = ::clamp(p.x(), 215, m_size.x() - 10);
		m_side_panel_contents->set_fixed_width(w);
		m_side_scroll_panel->set_fixed_width(w + 12);
		m_side_panel->set_fixed_width(m_side_scroll_panel->fixed_width());
		request_layout_update();
		return true;
	}

	return Screen::mouse_motion_event(p, rel, button, modifiers);
}


void HDRViewScreen::update_layout()
{
	int headerHeight = m_top_panel->fixed_height();
	int sidePanelWidth = m_side_panel->fixed_width();
	int toolPanelWidth = m_tool_panel->fixed_width();
	int footerHeight = m_status_bar->fixed_height();

	static int headerShift = 0;
	static int sidePanelShift = 0;
	static int toolPanelShift = 0;
	static int footerShift = 0;

	if (m_animation_running)
	{
		const double duration = 0.2;
		double elapsed = glfwGetTime() - m_gui_animation_start;
		// stop the animation after 2 seconds
		if (elapsed > duration)
		{
			pop_gui_refresh();
			m_animation_running = false;
			sidePanelShift = (m_animation_goal & SIDE_PANEL) ? 0 : -sidePanelWidth;
			toolPanelShift = (m_animation_goal & SIDE_PANEL) ? 0 : toolPanelWidth;
			headerShift = (m_animation_goal & TOP_PANEL) ? 0 : -headerHeight;
			footerShift = (m_animation_goal & BOTTOM_PANEL) ? 0 : footerHeight;

			m_side_panel_button->set_pushed(m_animation_goal & SIDE_PANEL);
		}
		// animate the location of the panels
		else
		{
			// only animate the sidepanel if it isn't already at the goal position
			if (((m_animation_goal & SIDE_PANEL) && sidePanelShift != 0) ||
				(!(m_animation_goal & SIDE_PANEL) && sidePanelShift != -sidePanelWidth))
			{
				double start = (m_animation_goal & SIDE_PANEL) ? double(-sidePanelWidth) : 0.0;
				double end = (m_animation_goal & SIDE_PANEL) ? 0.0 : double(-sidePanelWidth);
				sidePanelShift = static_cast<int>(round(lerp(start, end, smoothStep(0.0, duration, elapsed))));

				start = (m_animation_goal & SIDE_PANEL) ? double(toolPanelWidth) : 0.0;
				end = (m_animation_goal & SIDE_PANEL) ? 0.0 : double(toolPanelWidth);
				toolPanelShift = static_cast<int>(round(lerp(start, end, smoothStep(0.0, duration, elapsed))));
				m_side_panel_button->set_pushed(true);
			}
			// only animate the header if it isn't already at the goal position
			if (((m_animation_goal & TOP_PANEL) && headerShift != 0) ||
				(!(m_animation_goal & TOP_PANEL) && headerShift != -headerHeight))
			{
				double start = (m_animation_goal & TOP_PANEL) ? double(-headerHeight) : 0.0;
				double end = (m_animation_goal & TOP_PANEL) ? 0.0 : double(-headerHeight);
				headerShift = static_cast<int>(round(lerp(start, end, smoothStep(0.0, duration, elapsed))));
			}

			// only animate the footer if it isn't already at the goal position
			if (((m_animation_goal & BOTTOM_PANEL) && footerShift != 0) ||
				(!(m_animation_goal & BOTTOM_PANEL) && footerShift != footerHeight))
			{
				double start = (m_animation_goal & BOTTOM_PANEL) ? double(footerHeight) : 0.0;
				double end = (m_animation_goal & BOTTOM_PANEL) ? 0.0 : double(footerHeight);
				footerShift = static_cast<int>(round(lerp(start, end, smoothStep(0.0, duration, elapsed))));
			}
		}
	}

	m_top_panel->set_position(nanogui::Vector2i(0,headerShift));
	m_top_panel->set_fixed_width(width());

	int middleHeight = height() - headerHeight - footerHeight - headerShift + footerShift;
	int middleWidth = width() - toolPanelWidth + toolPanelShift;

	m_side_panel->set_position(nanogui::Vector2i(sidePanelShift,headerShift+headerHeight));
	m_side_panel->set_fixed_height(middleHeight);

	m_tool_panel->set_position(nanogui::Vector2i(middleWidth, headerShift+headerHeight));
	m_tool_panel->set_fixed_height(middleHeight);

	m_image_view->set_position(nanogui::Vector2i(sidePanelShift+sidePanelWidth,headerShift+headerHeight));
	m_image_view->set_fixed_width(width() - sidePanelShift-sidePanelWidth-toolPanelWidth+toolPanelShift);
	m_image_view->set_fixed_height(middleHeight);

	m_status_bar->set_position(nanogui::Vector2i(0,headerShift+headerHeight+middleHeight));
	m_status_bar->set_fixed_width(width());

	int lh = std::min(middleHeight, m_side_panel_contents->preferred_size(m_nvg_context).y());
	m_side_scroll_panel->set_fixed_height(lh);

	int zoomWidth = m_zoom_label->preferred_size(m_nvg_context).x();
    m_zoom_label->set_width(zoomWidth);
    m_zoom_label->set_position(nanogui::Vector2i(width()-zoomWidth-6, 0));

	perform_layout();

	if (!m_dragging_side_panel)
	{
		// With a changed layout the relative position of the mouse
		// within children changes and therefore should get updated.
		// nanogui does not handle this for us.
		double x, y;
		glfwGetCursorPos(m_glfw_window, &x, &y);
		cursor_pos_callback_event(x, y);
	}
}

void HDRViewScreen::draw_contents()
{
	clear();

	// spdlog::trace("HDRViewScreen::draw_contents");
	m_images_panel->run_requested_callbacks();

	if (auto img = m_images_panel->current_image())
	{
		img->check_async_result();
		img->upload_to_GPU();


		if (!img->is_null() &&
			img->histograms() &&
			img->histograms()->ready() &&
			img->histograms()->get())
		{
			auto lazyHist = img->histograms();
			m_stats_label->set_caption(fmt::format("{:.3f}\n{:.3f}\n{:.3f}",
										lazyHist->get()->minimum, lazyHist->get()->average, lazyHist->get()->maximum));
		}
		else
			m_stats_label->set_caption("");
	}

	if (m_need_layout_update || m_animation_running)
	{
		update_layout();
		// redraw();
		m_need_layout_update = false;
	}

	// draw the colorpicker's eyedropper
	if (m_active_colorpicker)
	{
		if (auto img = m_images_panel->current_image())
		{
			auto center = mouse_pos();

			nanogui::Vector2i pixel(m_image_view->image_coordinate_at((center - m_image_view->position())));
			const HDRImage & image = img->image();

			if (image.contains(pixel.x(), pixel.y()))
			{
				Color4 color_orig = image(pixel.x(), pixel.y());
				Color4 color_toned = m_image_view->tonemap(color_orig);
				Color ng_color(color_toned);

				m_active_colorpicker->set_color(color_orig);

				nvgBeginPath(m_nvg_context);
				nvgCircle(m_nvg_context, center.x(), center.y(), 26);
				nvgFillColor(m_nvg_context, ng_color);
				nvgFill(m_nvg_context);

				nvgStrokeColor(m_nvg_context, Color(0, 255));
				nvgStrokeWidth(m_nvg_context, 2+1.f);
				nvgStroke(m_nvg_context);

				nvgStrokeColor(m_nvg_context, Color(192, 255));
				nvgStrokeWidth(m_nvg_context, 2);
				nvgStroke(m_nvg_context);
				
				nvgFontSize(m_nvg_context, font_size());
				nvgFontFace(m_nvg_context, "icons");
				nvgFillColor(m_nvg_context, ng_color.contrasting_color());
				nvgTextAlign(m_nvg_context, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);

				nvgText(m_nvg_context, center.x(), center.y(), utf8(FA_EYE_DROPPER).data(), nullptr);
			}
		}
	}
}
