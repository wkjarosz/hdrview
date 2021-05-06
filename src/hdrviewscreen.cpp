//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrviewscreen.h"
#include "editimagepanel.h"
#include "imagelistpanel.h"
#include "imageview.h"
#include "common.h"
#include "commandhistory.h"
#include "helpwindow.h"
#include "xpuimage.h"
#include <iostream>
#include <nanogui/opengl.h>
#define NOMINMAX
#include <tinydir.h>
#include <thread>

using namespace std;

HDRViewScreen::HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args) :
    Screen(nanogui::Vector2i(800,600), "HDRView", true),
    console(spdlog::get("console"))
{
    set_background(Color(0.23f, 1.0f));

    auto thm = new Theme(m_nvg_context);
    thm->m_standard_font_size     = 16;
    thm->m_button_font_size       = 15;
    thm->m_text_box_font_size     = 14;
	thm->m_window_corner_radius   = 4;
	thm->m_window_fill_unfocused  = Color(40, 250);
	thm->m_window_fill_focused    = Color(45, 250);
    set_theme(thm);

	auto panelTheme = new Theme(m_nvg_context);
	panelTheme = new Theme(m_nvg_context);
	panelTheme->m_standard_font_size     = 16;
	panelTheme->m_button_font_size       = 15;
	panelTheme->m_text_box_font_size     = 14;
	panelTheme->m_window_corner_radius   = 0;
	panelTheme->m_window_fill_unfocused  = Color(50, 255);
	panelTheme->m_window_fill_focused    = Color(52, 255);
	panelTheme->m_button_corner_radius   = 2;
	panelTheme->m_window_header_height   = 0;
	panelTheme->m_window_drop_shadow_size = 0;


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

	m_image_view = new ::HDRImageView(this);
	m_image_view->set_grid_threshold(10);
	m_image_view->set_pixel_info_threshold(40);


	m_status_bar = new Window(this, "");
	m_status_bar->set_theme(panelTheme);
	m_status_bar->set_fixed_height(m_status_bar->theme()->m_text_box_font_size+1);

    //
    // create status bar widgets
    //

	m_pixel_info_label = new Label(m_status_bar, "", "sans");
	m_pixel_info_label->set_font_size(thm->m_text_box_font_size);
	m_pixel_info_label->set_position(nanogui::Vector2i(6, 0));

	m_zoom_label = new Label(m_status_bar, "100% (1 : 1)", "sans");
	m_zoom_label->set_font_size(thm->m_text_box_font_size);

    //
    // create side panel widgets
    //

	m_side_scroll_panel = new VScrollPanel(m_side_panel);
	m_side_panel_contents = new Widget(m_side_scroll_panel);
	m_side_panel_contents->set_layout(new BoxLayout(Orientation::Vertical,
	                                             Alignment::Fill, 4, 4));
	m_side_panel_contents->set_fixed_width(213);
	m_side_scroll_panel->set_fixed_width(m_side_panel_contents->fixed_width() + 12);
	m_side_panel->set_fixed_width(m_side_scroll_panel->fixed_width());

    //
    // create file/images panel
    //

    auto btn = new Button(m_side_panel_contents, "File", FA_CHEVRON_DOWN);
	btn->set_flags(Button::ToggleButton);
	btn->set_pushed(true);
	btn->set_font_size(18);
	btn->set_icon_position(Button::IconPosition::Right);
    m_images_panel = new ImageListPanel(m_side_panel_contents, this, m_image_view);

	btn->set_change_callback([this,btn](bool value)
                         {
	                         btn->set_icon(value ? FA_CHEVRON_DOWN : FA_CHEVRON_LEFT);
	                         m_images_panel->set_visible(value);
                             update_layout();
                             m_side_panel_contents->perform_layout(m_nvg_context);
                         });

    //
    // create edit panel
    //

	btn = new Button(m_side_panel_contents, "Edit", FA_CHEVRON_LEFT);
	btn->set_flags(Button::ToggleButton);
	btn->set_font_size(18);
	btn->set_icon_position(Button::IconPosition::Right);

	auto editPanel = new EditImagePanel(m_side_panel_contents, this, m_images_panel);
	editPanel->set_visible(false);

	btn->set_change_callback([this,btn,editPanel](bool value)
	 {
		 btn->set_icon(value ? FA_CHEVRON_DOWN : FA_CHEVRON_LEFT);
		 editPanel->set_visible(value);
		 update_layout();
		 m_side_panel_contents->perform_layout(m_nvg_context);
	 });
	editPanel->perform_layout(m_nvg_context);

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
    auto exposureSlider = new Slider(m_top_panel);
    auto exposureTextBox = new FloatBox<float>(m_top_panel, exposure);
	auto normalizeButton = new Button(m_top_panel, "", FA_MAGIC);
	normalizeButton->set_fixed_size(nanogui::Vector2i(19, 19));
	normalizeButton->set_callback([this]()
	                             {
		                             auto img = m_images_panel->current_image();
		                             if (!img)
			                             return;
		                             Color4 mC = img->image().max();
		                             float mCf = max(mC[0], mC[1], mC[2]);
		                             console->debug("max value: {}", mCf);
		                             m_image_view->set_exposure(log2(1.0f/mCf));
		                             m_images_panel->request_histogram_update(true);
	                             });
	normalizeButton->set_tooltip("Normalize exposure.");
	auto resetButton = new Button(m_top_panel, "", FA_SYNC);
	resetButton->set_fixed_size(nanogui::Vector2i(19, 19));
	resetButton->set_callback([this]()
	                             {
		                             m_image_view->set_exposure(0.0f);
		                             m_image_view->set_gamma(2.2f);
		                             m_image_view->set_sRGB(true);
		                             m_images_panel->request_histogram_update(true);
	                             });
	resetButton->set_tooltip("Reset tonemapping.");

    auto sRGBCheckbox = new CheckBox(m_top_panel, "sRGB   ");
    auto gammaLabel = new Label(m_top_panel, "Gamma", "sans-bold");
    auto gammaSlider = new Slider(m_top_panel);
    auto gammaTextBox = new FloatBox<float>(m_top_panel);

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
        update_layout();
    });

    exposureTextBox->number_format("%1.2f");
    exposureTextBox->set_editable(true);
	exposureTextBox->set_spinnable(true);
    exposureTextBox->set_fixed_width(50);
	exposureTextBox->set_min_value(-9.0f);
	exposureTextBox->set_max_value( 9.0f);
    exposureTextBox->set_alignment(TextBox::Alignment::Right);
    exposureTextBox->set_callback([this](float e)
                                 {
	                                 m_image_view->set_exposure(e);
                                 });
    exposureSlider->set_callback([this](float v)
						        {
							        m_image_view->set_exposure(round(4*v) / 4.0f);
						        });
	exposureSlider->set_final_callback([this](float v)
	                                 {
		                                 m_image_view->set_exposure(round(4*v) / 4.0f);
		                                 m_images_panel->request_histogram_update(true);
	                                 });
    exposureSlider->set_fixed_width(100);
    exposureSlider->set_range({-9.0f,9.0f});
    exposureTextBox->set_value(exposure);

    gammaTextBox->set_editable(true);
	gammaTextBox->set_spinnable(true);
    gammaTextBox->number_format("%1.3f");
    gammaTextBox->set_fixed_width(55);
	gammaTextBox->set_min_value(0.02f);
	gammaTextBox->set_max_value(9.0f);

    gammaTextBox->set_alignment(TextBox::Alignment::Right);
    gammaTextBox->set_callback([this,gammaSlider](float value)
                                {
                                    m_image_view->set_gamma(value);
                                    gammaSlider->set_value(value);
                                });
    gammaSlider->set_callback(
	    [&,gammaSlider,gammaTextBox](float value)
	    {
		    float g = max(gammaSlider->range().first, round(10*value) / 10.0f);
		    m_image_view->set_gamma(g);
		    gammaTextBox->set_value(g);
		    gammaSlider->set_value(g);       // snap values
	    });
    gammaSlider->set_fixed_width(100);
    gammaSlider->set_range({0.02f,9.0f});
    gammaSlider->set_value(gamma);
    gammaTextBox->set_value(gamma);

    m_image_view->set_exposure_callback([this,exposureTextBox,exposureSlider](float e)
                                     {
	                                     exposureTextBox->set_value(e);
	                                     exposureSlider->set_value(e);
	                                     m_images_panel->request_histogram_update();
                                     });
    m_image_view->set_gamma_callback([gammaTextBox,gammaSlider](float g)
                                  {
	                                  gammaTextBox->set_value(g);
	                                  gammaSlider->set_value(g);
                                  });
	m_image_view->set_sRGB_callback([sRGBCheckbox,gammaTextBox,gammaSlider](bool b)
	                              {
		                              sRGBCheckbox->set_checked(b);
		                              gammaTextBox->set_enabled(!b);
		                              gammaTextBox->set_spinnable(!b);
		                              gammaSlider->set_enabled(!b);
	                              });
    m_image_view->set_exposure(exposure);
    m_image_view->set_gamma(gamma);

    m_image_view->set_zoom_callback([this](float zoom)
                                {
                                    float realZoom = zoom * pixel_ratio();
                                    int numer = (realZoom < 1.0f) ? 1 : (int)round(realZoom);
                                    int denom = (realZoom < 1.0f) ? (int)round(1.0f/realZoom) : 1;
                                    m_zoom_label->set_caption(fmt::format("{:7.2f}% ({:d} : {:d})", realZoom * 100, numer, denom));
                                    update_layout();
                                });

	sRGBCheckbox->set_callback([&,gammaSlider,gammaTextBox,gammaLabel](bool value)
    {
        m_image_view->set_sRGB(value);
        gammaSlider->set_enabled(!value);
	    gammaTextBox->set_spinnable(!value);
        gammaTextBox->set_enabled(!value);
        gammaLabel->set_enabled(!value);
        gammaLabel->set_color(value ? m_theme->m_disabled_text_color : m_theme->m_text_color);
        update_layout();
    });

	sRGBCheckbox->set_checked(sRGB);
	sRGBCheckbox->callback()(sRGB);

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



	drop_event(args);


	this->set_size(nanogui::Vector2i(1024, 800));
	update_layout();
	set_resize_callback([&](nanogui::Vector2i)
	                  {
		                //   update_layout();
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
					console->trace("refreshing gui");
            }
        }
    );
}

HDRViewScreen::~HDRViewScreen()
{
	m_gui_refresh_thread.join();
}


void HDRViewScreen::update_caption()
{
    auto img = m_images_panel->current_image();
    if (img)
        set_caption(string("HDRView [") + img->filename() + (img->is_modified() ? "*" : "") + "]");
    else
        set_caption(string("HDRView"));
}

bool HDRViewScreen::drop_event(const vector<string> & filenames)
{
	try
	{
		m_images_panel->load_images(filenames);

		// Ensure the new image button will have the correct visibility state.
		m_images_panel->set_filter(m_images_panel->filter());
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

	update_layout();
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


void HDRViewScreen::flip_image(bool h)
{
    if (h)
		m_images_panel->modify_image(
		    [](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
		    {
			    return {make_shared<HDRImage>(img->flipped_horizontal()),
			            make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->flipped_horizontal(); })};
		    });
    else
		m_images_panel->modify_image(
		    [](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
		    {
			    return {make_shared<HDRImage>(img->flipped_vertical()),
			            make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->flipped_vertical(); })};
		    });
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
			console->trace("KEY BACKSPACE pressed");
	        ask_close_image(m_images_panel->current_image_index());
            return true;
        case 'W':
			console->trace("KEY `W` pressed");
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
			console->trace("KEY `O` pressed");
		    if (modifiers & SYSTEM_COMMAND_MOD)
		    {
			    load_image();
			    return true;
		    }
		    return false;

        case '=':
        case GLFW_KEY_KP_ADD:
			console->trace("KEY `=` pressed");
			m_image_view->zoom_in();
            return true;

        case '-':
        case GLFW_KEY_KP_SUBTRACT:
			console->trace("KEY `-` pressed");
			m_image_view->zoom_out();
            return true;

        case 'G':
			console->trace("KEY `G` pressed");
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
			console->trace("KEY `E` pressed");
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
			console->trace("KEY `F` pressed");
			if (modifiers & SYSTEM_COMMAND_MOD)
				m_images_panel->focus_filter();
			else
				flip_image(false);
			return true;

        case 'M':
			console->trace("KEY `M` pressed");
            flip_image(true);
            return true;

        case ' ':
			console->trace("KEY ` ` pressed");
	        m_image_view->center();
            draw_all();
            return true;

        case 'T':
			console->trace("KEY `T` pressed");
		    m_gui_animation_start = glfwGetTime();
			push_gui_refresh();
			m_animation_running = true;
		    m_animation_goal = EAnimationGoal(m_animation_goal ^ TOP_PANEL);
		    update_layout();
            return true;

        case 'H':
			console->trace("KEY `H` pressed");
		    toggle_help_window();
            return true;

        case GLFW_KEY_TAB:
			console->trace("KEY TAB pressed");
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

		    update_layout();
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

	return Screen::mouse_button_event(p, button, down, modifiers);
}

bool HDRViewScreen::mouse_motion_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int modifiers)
{
	ConstImagePtr img = m_images_panel->current_image();
	if (img)
	{
		nanogui::Vector2i pixelCoord(m_image_view->image_coordinate_at((p - m_image_view->position())));
		const HDRImage & image = img->image();
		if (image.contains(pixelCoord.x(), pixelCoord.y()))
		{
			Color4 pixelVal = image(pixelCoord.x(), pixelCoord.y());
			Color4 iPixelVal = (pixelVal * pow(2.f, m_image_view->exposure()) * 255).min(255.f).max(0.f);
			string s = fmt::format(
				"({: 4d},{: 4d}) = ({: 6.3f}, {: 6.3f}, {: 6.3f}, {: 6.3f}) / ({: 3d}, {: 3d}, {: 3d}, {: 3d})",
				pixelCoord.x(), pixelCoord.y(),
				pixelVal[0], pixelVal[1], pixelVal[2], pixelVal[3],
				(int) round(iPixelVal[0]), (int) round(iPixelVal[1]),
				(int) round(iPixelVal[2]), (int) round(iPixelVal[3]));
			m_pixel_info_label->set_caption(s);
		}
		else
			m_pixel_info_label->set_caption("");

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
		int w = ::clamp(p.x(), 206, m_size.x() - 10);
		m_side_panel_contents->set_fixed_width(w);
		m_side_scroll_panel->set_fixed_width(w + 12);
		m_side_panel->set_fixed_width(m_side_scroll_panel->fixed_width());
		update_layout();
		return true;
	}

	return Screen::mouse_motion_event(p, rel, button, modifiers);
}


void HDRViewScreen::update_layout()
{
	int headerHeight = m_top_panel->fixed_height();
	int sidePanelWidth = m_side_panel->fixed_width();
	int footerHeight = m_status_bar->fixed_height();

	static int headerShift = 0;
	static int sidePanelShift = 0;
	static int footerShift = 0;

	if (m_animation_running)
	{
		cout << "doing gui animation" << std::endl;
		const double duration = 0.2;
		double elapsed = glfwGetTime() - m_gui_animation_start;
		// stop the animation after 2 seconds
		if (elapsed > duration)
		{
			pop_gui_refresh();
			m_animation_running = false;
			sidePanelShift = (m_animation_goal & SIDE_PANEL) ? 0 : -sidePanelWidth;
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

	m_side_panel->set_position(nanogui::Vector2i(sidePanelShift,headerShift+headerHeight));
	m_side_panel->set_fixed_height(middleHeight);

	m_image_view->set_position(nanogui::Vector2i(sidePanelShift+sidePanelWidth,headerShift+headerHeight));
	m_image_view->set_fixed_width(width() - sidePanelShift-sidePanelWidth);
	m_image_view->set_fixed_height(middleHeight);

	m_status_bar->set_position(nanogui::Vector2i(0,headerShift+headerHeight+middleHeight));
	m_status_bar->set_fixed_width(width());

	int lh = std::min(middleHeight, m_side_panel_contents->preferred_size(m_nvg_context).y());
	m_side_scroll_panel->set_fixed_height(lh);

	int zoomWidth = m_zoom_label->preferred_size(m_nvg_context).x();
    m_zoom_label->set_width(zoomWidth);
    m_zoom_label->set_position(nanogui::Vector2i(width()-zoomWidth-6, 0));

	perform_layout();
}

void HDRViewScreen::draw_contents()
{
	clear();

	// console->trace("HDRViewScreen::draw_contents");
	m_images_panel->run_requested_callbacks();

	if (auto img = m_images_panel->current_image())
	{
		img->check_async_result();
		img->upload_to_GPU();
	}

	update_layout();
	// redraw();
}
