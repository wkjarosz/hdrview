//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "HDRViewScreen.h"
#include "GLImage.h"
#include "EditImagePanel.h"
#include "ImageListPanel.h"
#include <iostream>
#include "Common.h"
#include "CommandHistory.h"
#include "HDRImageViewer.h"
#include "HelpWindow.h"
#define NOMINMAX
#include <tinydir.h>

using namespace std;

HDRViewScreen::HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args) :
    Screen(nanogui::Vector2i(800,600), "HDRView", true),
//    m_imageMgr(new HDRImageManager()),
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

	m_topPanel = new Window(this, "");
	m_topPanel->set_theme(panelTheme);
	m_topPanel->set_position(nanogui::Vector2i(0,0));
	m_topPanel->set_fixed_height(30);
	m_topPanel->set_layout(new BoxLayout(Orientation::Horizontal,
	                                    Alignment::Middle, 5, 5));

	m_sidePanel = new Window(this, "");
	m_sidePanel->set_theme(panelTheme);

	m_imageView = new HDRImageViewer(this, this);
	m_imageView->set_grid_threshold(10);
	m_imageView->set_pixel_info_threshold(40);

	m_statusBar = new Window(this, "");
	m_statusBar->set_theme(panelTheme);
	m_statusBar->set_fixed_height(m_statusBar->theme()->m_text_box_font_size+1);

    //
    // create status bar widgets
    //

	m_pixelInfoLabel = new Label(m_statusBar, "", "sans");
	m_pixelInfoLabel->set_font_size(thm->m_text_box_font_size);
	m_pixelInfoLabel->set_position(nanogui::Vector2i(6, 0));

	m_zoomLabel = new Label(m_statusBar, "100% (1 : 1)", "sans");
	m_zoomLabel->set_font_size(thm->m_text_box_font_size);

    //
    // create side panel widgets
    //

	m_sideScrollPanel = new VScrollPanel(m_sidePanel);
	m_sidePanelContents = new Widget(m_sideScrollPanel);
	m_sidePanelContents->set_layout(new BoxLayout(Orientation::Vertical,
	                                             Alignment::Fill, 4, 4));
	m_sidePanelContents->set_fixed_width(213);
	m_sideScrollPanel->set_fixed_width(m_sidePanelContents->fixed_width() + 12);
	m_sidePanel->set_fixed_width(m_sideScrollPanel->fixed_width());

    //
    // create file/images panel
    //

    auto btn = new Button(m_sidePanelContents, "File", FA_CHEVRON_DOWN);
	btn->set_flags(Button::ToggleButton);
	btn->set_pushed(true);
	btn->set_font_size(18);
	btn->set_icon_position(Button::IconPosition::Right);
    m_imagesPanel = new ImageListPanel(m_sidePanelContents, this, m_imageView);

	btn->set_change_callback([this,btn](bool value)
                         {
	                         btn->set_icon(value ? FA_CHEVRON_DOWN : FA_CHEVRON_LEFT);
	                         m_imagesPanel->set_visible(value);
                             update_layout();
                             m_sidePanelContents->perform_layout(m_nvg_context);
                         });

    // //
    // // create edit panel
    // //

	// btn = new Button(m_sidePanelContents, "Edit", FA_CHEVRON_LEFT);
	// btn->set_flags(Button::ToggleButton);
	// btn->set_font_size(18);
	// btn->set_icon_position(Button::IconPosition::Right);

	// auto editPanel = new EditImagePanel(m_sidePanelContents, this, m_imagesPanel);
	// editPanel->set_visible(false);

	// btn->set_change_callback([this,btn,editPanel](bool value)
	//  {
	// 	 btn->set_icon(value ? FA_CHEVRON_DOWN : FA_CHEVRON_LEFT);
	// 	 editPanel->set_visible(value);
	// 	 update_layout();
	// 	 m_sidePanelContents->perform_layout(m_nvg_context);
	//  });
	// editPanel->perform_layout(m_nvg_context);

    //
    // create top panel controls
    //

	m_helpButton = new Button{m_topPanel, "", FA_QUESTION};
	m_helpButton->set_fixed_size(nanogui::Vector2i(25, 25));
	m_helpButton->set_change_callback([this](bool) { toggle_help_window(); });
	m_helpButton->set_tooltip("Information about using HDRView.");
	m_helpButton->set_flags(Button::ToggleButton);

    m_sidePanelButton = new Button(m_topPanel, "", FA_BARS);
    new Label(m_topPanel, "EV", "sans-bold");
    auto exposureSlider = new Slider(m_topPanel);
    auto exposureTextBox = new FloatBox<float>(m_topPanel, exposure);
	auto normalizeButton = new Button(m_topPanel, "", FA_BOLT);
	normalizeButton->set_fixed_size(nanogui::Vector2i(19, 19));
	normalizeButton->set_callback([this]()
	                             {
		                             auto img = m_imagesPanel->current_image();
		                             if (!img)
			                             return;
		                             Color4 mC = img->image().max();
		                             float mCf = max(mC[0], mC[1], mC[2]);
		                             console->debug("max value: {}", mCf);
		                             m_imageView->set_exposure(log2(1.0f/mCf));
		                             m_imagesPanel->request_histogram_update(true);
	                             });
	normalizeButton->set_tooltip("Normalize exposure.");
	auto resetButton = new Button(m_topPanel, "", FA_SYNC);
	resetButton->set_fixed_size(nanogui::Vector2i(19, 19));
	resetButton->set_callback([this]()
	                             {
		                             m_imageView->set_exposure(0.0f);
		                             m_imageView->set_gamma(2.2f);
		                             m_imageView->set_sRGB(true);
		                             m_imagesPanel->request_histogram_update(true);
	                             });
	resetButton->set_tooltip("Reset tonemapping.");

    auto sRGBCheckbox = new CheckBox(m_topPanel, "sRGB   ");
    auto gammaLabel = new Label(m_topPanel, "Gamma", "sans-bold");
    auto gammaSlider = new Slider(m_topPanel);
    auto gammaTextBox = new FloatBox<float>(m_topPanel);

    m_sidePanelButton->set_tooltip("Bring up the images dialog to load/remove images, and cycle through open images.");
    m_sidePanelButton->set_flags(Button::ToggleButton);
	m_sidePanelButton->set_pushed(true);
    m_sidePanelButton->set_fixed_size(nanogui::Vector2i(25, 25));
    m_sidePanelButton->set_change_callback([this](bool value)
    {
	    m_guiAnimationStart = glfwGetTime();
	    m_guiTimerRunning = true;
	    m_animationGoal = EAnimationGoal(m_animationGoal ^ SIDE_PANEL);
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
	                                 m_imageView->set_exposure(e);
                                 });
    exposureSlider->set_callback([this](float v)
						        {
							        m_imageView->set_exposure(round(4*v) / 4.0f);
						        });
	exposureSlider->set_final_callback([this](float v)
	                                 {
		                                 m_imageView->set_exposure(round(4*v) / 4.0f);
		                                 m_imagesPanel->request_histogram_update(true);
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
                                    m_imageView->set_gamma(value);
                                    gammaSlider->set_value(value);
                                });
    gammaSlider->set_callback(
	    [&,gammaSlider,gammaTextBox](float value)
	    {
		    float g = max(gammaSlider->range().first, round(10*value) / 10.0f);
		    m_imageView->set_gamma(g);
		    gammaTextBox->set_value(g);
		    gammaSlider->set_value(g);       // snap values
	    });
    gammaSlider->set_fixed_width(100);
    gammaSlider->set_range({0.02f,9.0f});
    gammaSlider->set_value(gamma);
    gammaTextBox->set_value(gamma);

    m_imageView->set_exposure_callback([this,exposureTextBox,exposureSlider](float e)
                                     {
	                                     exposureTextBox->set_value(e);
	                                     exposureSlider->set_value(e);
	                                     m_imagesPanel->request_histogram_update();
                                     });
    m_imageView->set_gamma_callback([gammaTextBox,gammaSlider](float g)
                                  {
	                                  gammaTextBox->set_value(g);
	                                  gammaSlider->set_value(g);
                                  });
	m_imageView->set_sRGB_callback([sRGBCheckbox,gammaTextBox,gammaSlider](bool b)
	                              {
		                              sRGBCheckbox->set_checked(b);
		                              gammaTextBox->set_enabled(!b);
		                              gammaTextBox->set_spinnable(!b);
		                              gammaSlider->set_enabled(!b);
	                              });
    m_imageView->set_exposure(exposure);
    m_imageView->set_gamma(gamma);
    m_imageView
	    ->set_pixel_hover_callback([this](const nanogui::Vector2i &pixelCoord, const Color4 &pixelVal, const Color4 &iPixelVal)
	                            {
		                            auto img = m_imagesPanel->current_image();

		                            if (img && img->contains(pixelCoord))
		                            {
			                            string s = fmt::format(
				                            "({: 4d},{: 4d}) = ({: 6.3f}, {: 6.3f}, {: 6.3f}, {: 6.3f}) / ({: 3d}, {: 3d}, {: 3d}, {: 3d})",
				                            pixelCoord.x(), pixelCoord.y(),
				                            pixelVal[0], pixelVal[1], pixelVal[2], pixelVal[3],
				                            (int) round(iPixelVal[0]), (int) round(iPixelVal[1]),
				                            (int) round(iPixelVal[2]), (int) round(iPixelVal[3]));
			                            m_pixelInfoLabel->set_caption(s);
		                            }
		                            else
			                            m_pixelInfoLabel->set_caption("");

		                            m_statusBar->perform_layout(m_nvg_context);
	                            });

    m_imageView->set_zoom_callback([this](float zoom)
                                {
                                    float realZoom = zoom * pixel_ratio();
                                    int numer = (realZoom < 1.0f) ? 1 : (int)round(realZoom);
                                    int denom = (realZoom < 1.0f) ? (int)round(1.0f/realZoom) : 1;
                                    m_zoomLabel->set_caption(fmt::format("{:7.2f}% ({:d} : {:d})", realZoom * 100, numer, denom));
                                    update_layout();
                                });

	sRGBCheckbox->set_callback([&,gammaSlider,gammaTextBox,gammaLabel](bool value)
    {
        m_imageView->set_sRGB(value);
        gammaSlider->set_enabled(!value);
	    gammaTextBox->set_spinnable(!value);
        gammaTextBox->set_enabled(!value);
        gammaLabel->set_enabled(!value);
        gammaLabel->set_color(value ? m_theme->m_disabled_text_color : m_theme->m_text_color);
        update_layout();
    });

	sRGBCheckbox->set_checked(sRGB);
	sRGBCheckbox->callback()(sRGB);

    (new CheckBox(m_topPanel, "Dither  ",
                 [&](bool v) { m_imageView->set_dithering(v); }))->set_checked(m_imageView->dithering_on());
    (new CheckBox(m_topPanel, "Grid  ",
                 [&](bool v) { m_imageView->set_draw_grid(v); }))->set_checked(m_imageView->draw_grid_on());
    (new CheckBox(m_topPanel, "RGB values  ",
                 [&](bool v) { m_imageView->set_draw_values(v); }))->set_checked(m_imageView->draw_values_on());

	drop_event(args);

	this->set_size(nanogui::Vector2i(1024, 800));
	update_layout();
	set_resize_callback([&](nanogui::Vector2i)
	                  {
		                //   update_layout();
	                  });

    set_visible(true);
    // glfwSwapInterval(1);
}

HDRViewScreen::~HDRViewScreen()
{
    // empty
}


void HDRViewScreen::update_caption()
{
    auto img = m_imagesPanel->current_image();
    if (img)
        set_caption(string("HDRView [") + img->filename() + (img->is_modified() ? "*" : "") + "]");
    else
        set_caption(string("HDRView"));
}

bool HDRViewScreen::drop_event(const vector<string> & filenames)
{
	try
	{
		m_imagesPanel->load_images(filenames);

		// Ensure the new image button will have the correct visibility state.
		m_imagesPanel->set_filter(m_imagesPanel->filter());
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
		m_imagesPanel->close_image();
		cout << "curr: " << m_imagesPanel->current_image_index() << endl;
	};

	auto curr = m_imagesPanel->current_image_index();
	auto next = m_imagesPanel->next_visible_image(curr, Forward);
	cout << "curr: " << curr << "; next: " << next << endl;
	if (auto img = m_imagesPanel->image(curr))
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
	for (int i = 0; i < m_imagesPanel->num_images(); ++i)
		anyModified |= m_imagesPanel->image(i)->is_modified();

	if (anyModified)
	{
		auto dialog = new MessageDialog(this, MessageDialog::Type::Warning, "Warning!",
		                                "Some images have unsaved modifications. Close all images anyway?", "Yes", "Cancel", true);
		dialog->set_callback([this](int close){if (close == 0) m_imagesPanel->close_all_images();});
	}
	else
		m_imagesPanel->close_all_images();
}


void HDRViewScreen::toggle_help_window()
{
	if (m_helpWindow)
	{
		m_helpWindow->dispose();
		m_helpWindow = nullptr;
		m_helpButton->set_pushed(false);
	}
	else
	{
		m_helpWindow = new HelpWindow{this, [this] { toggle_help_window(); }};
		m_helpWindow->center();
		m_helpWindow->request_focus();
		m_helpButton->set_pushed(true);
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
		if (!m_imagesPanel->current_image())
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
			m_imagesPanel->save_image(filename, m_imageView->exposure(), m_imageView->gamma(),
			                      m_imageView->sRGB(), m_imageView->dithering_on());
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
		m_imagesPanel->modify_image(
		    [](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
		    {
			    return {make_shared<HDRImage>(img->flippedHorizontal()),
			            make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->flippedHorizontal(); })};
		    });
    else
		m_imagesPanel->modify_image(
		    [](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
		    {
			    return {make_shared<HDRImage>(img->flippedVertical()),
			            make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->flippedVertical(); })};
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
            if (!m_okToQuitDialog)
            {
                m_okToQuitDialog = new MessageDialog(this,
                        MessageDialog::Type::Warning, "Warning!",
                        "Do you really want to quit?", "Yes", "No", true);
                m_okToQuitDialog->set_callback([this](int result)
                    {
                        this->set_visible(result != 0);
                        m_okToQuitDialog = nullptr;
                    });
	            m_okToQuitDialog->request_focus();
            }
            else if (m_okToQuitDialog->visible())
            {
                // dialog already visible, dismiss it
                m_okToQuitDialog->dispose();
                m_okToQuitDialog = nullptr;
            }
            return true;
        }
        case GLFW_KEY_ENTER:
        {
             if (m_okToQuitDialog && m_okToQuitDialog->visible())
                 // quit dialog already visible, "enter" clicks OK, and quits
                 m_okToQuitDialog->callback()(0);
             else
                return true;
        }

        case 'Z':
            if (modifiers & SYSTEM_COMMAND_MOD)
            {
                if (modifiers & GLFW_MOD_SHIFT)
					m_imagesPanel->redo();
				else
					m_imagesPanel->undo();

	            return true;
            }
            return false;

        case GLFW_KEY_BACKSPACE:
	        ask_close_image(m_imagesPanel->current_image_index());
            return true;
        case 'W':
            if (modifiers & SYSTEM_COMMAND_MOD)
            {
	            if (modifiers & GLFW_MOD_SHIFT)
		            ask_close_all_images();
	            else
	                ask_close_image(m_imagesPanel->current_image_index());
                return true;
            }
            return false;

	    case 'O':
		    if (modifiers & SYSTEM_COMMAND_MOD)
		    {
			    load_image();
			    return true;
		    }
		    return false;

        case '=':
        case GLFW_KEY_KP_ADD:
		    m_imageView->zoom_in();
            return true;

        case '-':
        case GLFW_KEY_KP_SUBTRACT:
		    m_imageView->zoom_out();
            return true;

        case 'G':
            if (modifiers & GLFW_MOD_SHIFT)
	            m_imageView->set_gamma(m_imageView->gamma() + 0.02f);
            else
	            m_imageView->set_gamma(max(0.02f, m_imageView->gamma() - 0.02f));
            return true;
        case 'E':
            if (modifiers & GLFW_MOD_SHIFT)
	            m_imageView->set_exposure(m_imageView->exposure() + 0.25f);
            else
	            m_imageView->set_exposure(m_imageView->exposure() - 0.25f);
            return true;

        case 'F':
			if (modifiers & SYSTEM_COMMAND_MOD)
				m_imagesPanel->focus_filter();
			else
				flip_image(false);
			return true;

        case 'M':
            flip_image(true);
            return true;

        case ' ':
	        m_imageView->center();
            draw_all();
            return true;

        case 'T':
		    m_guiAnimationStart = glfwGetTime();
		    m_guiTimerRunning = true;
		    m_animationGoal = EAnimationGoal(m_animationGoal ^ TOP_PANEL);
		    update_layout();
            return true;

        case 'H':
		    toggle_help_window();
            return true;

        case GLFW_KEY_TAB:
	        if (modifiers & GLFW_MOD_SHIFT)
	        {
		        bool setVis = !((m_animationGoal & SIDE_PANEL) || (m_animationGoal & TOP_PANEL) || (m_animationGoal & BOTTOM_PANEL));
		        m_guiAnimationStart = glfwGetTime();
		        m_guiTimerRunning = true;
		        m_animationGoal = setVis ? EAnimationGoal(TOP_PANEL | SIDE_PANEL | BOTTOM_PANEL) : EAnimationGoal(0);
	        }
		    else if (modifiers & GLFW_MOD_ALT)
			{
				m_imagesPanel->swap_current_selected_with_previous();
			}
	        else
	        {
		        m_guiAnimationStart = glfwGetTime();
		        m_guiTimerRunning = true;
		        m_animationGoal = EAnimationGoal(m_animationGoal ^ SIDE_PANEL);
	        }

		    update_layout();
            return true;

        case GLFW_KEY_DOWN:
	        if (modifiers & SYSTEM_COMMAND_MOD)
	        {
		        m_imagesPanel->send_image_backward();
		        return true;
	        }
	        else if (m_imagesPanel->num_images())
            {
				m_imagesPanel->set_current_image_index(m_imagesPanel->next_visible_image(m_imagesPanel->current_image_index(), Backward));
	            return true;
            }
		    return false;

        case GLFW_KEY_UP:
	        if (modifiers & SYSTEM_COMMAND_MOD)
	        {
		        m_imagesPanel->bring_image_forward();
		        return true;
	        }
	        else if (m_imagesPanel->num_images())
	        {
				m_imagesPanel->set_current_image_index(m_imagesPanel->next_visible_image(m_imagesPanel->current_image_index(), Forward));
		        return true;
	        }
		    return false;

        case GLFW_KEY_KP_0:
	    case '0':
		    if (modifiers & SYSTEM_COMMAND_MOD)
		    {
			    m_imageView->center();
			    m_imageView->fit();
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
			m_imagesPanel->set_channel(EChannel(idx));
			return true;
		}
		else if (modifiers & GLFW_MOD_SHIFT && idx < NUM_BLEND_MODES)
		{
			m_imagesPanel->set_blend_mode(EBlendMode(idx));
			return true;
		}
		else
		{
			auto nth = m_imagesPanel->nth_visible_image_index(idx);
			if (nth >= 0)
				m_imagesPanel->set_current_image_index(nth);
		}
		return false;
	}

    return false;
}

bool HDRViewScreen::mouse_button_event(const nanogui::Vector2i &p, int button, bool down, int modifiers)
{
	if (button == GLFW_MOUSE_BUTTON_1 && down && at_side_panel_edge(p))
	{
		m_draggingSidePanel = true;

		// prevent Screen::cursorPosCallbackEvent from calling dragEvent on other widgets
		m_drag_active = false;
		m_drag_widget = nullptr;
		return true;
	}
	else
		m_draggingSidePanel = false;

	return Screen::mouse_button_event(p, button, down, modifiers);
}

bool HDRViewScreen::mouse_motion_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int modifiers)
{
	if (m_draggingSidePanel || at_side_panel_edge(p))
	{
		m_sidePanel->set_cursor(Cursor::HResize);
		m_sideScrollPanel->set_cursor(Cursor::HResize);
		m_sidePanelContents->set_cursor(Cursor::HResize);
		m_imageView->set_cursor(Cursor::HResize);
	}
	else
	{
		m_sidePanel->set_cursor(Cursor::Arrow);
		m_sideScrollPanel->set_cursor(Cursor::Arrow);
		m_sidePanelContents->set_cursor(Cursor::Arrow);
		m_imageView->set_cursor(Cursor::Arrow);
	}

	if (m_draggingSidePanel)
	{
		int w = ::clamp(p.x(), 206, m_size.x() - 10);
		m_sidePanelContents->set_fixed_width(w);
		m_sideScrollPanel->set_fixed_width(w + 12);
		m_sidePanel->set_fixed_width(m_sideScrollPanel->fixed_width());
		update_layout();
		return true;
	}

	return Screen::mouse_motion_event(p, rel, button, modifiers);
}


void HDRViewScreen::update_layout()
{
	int headerHeight = m_topPanel->fixed_height();
	int sidePanelWidth = m_sidePanel->fixed_width();
	int footerHeight = m_statusBar->fixed_height();

	static int headerShift = 0;
	static int sidePanelShift = 0;
	static int footerShift = 0;

	if (m_guiTimerRunning)
	{
		const double duration = 0.2;
		double elapsed = glfwGetTime() - m_guiAnimationStart;
		// stop the animation after 2 seconds
		if (elapsed > duration)
		{
			m_guiTimerRunning = false;
			sidePanelShift = (m_animationGoal & SIDE_PANEL) ? 0 : -sidePanelWidth;
			headerShift = (m_animationGoal & TOP_PANEL) ? 0 : -headerHeight;
			footerShift = (m_animationGoal & BOTTOM_PANEL) ? 0 : footerHeight;

			m_sidePanelButton->set_pushed(m_animationGoal & SIDE_PANEL);
		}
		// animate the location of the panels
		else
		{
			// only animate the sidepanel if it isn't already at the goal position
			if (((m_animationGoal & SIDE_PANEL) && sidePanelShift != 0) ||
				(!(m_animationGoal & SIDE_PANEL) && sidePanelShift != -sidePanelWidth))
			{
				double start = (m_animationGoal & SIDE_PANEL) ? double(-sidePanelWidth) : 0.0;
				double end = (m_animationGoal & SIDE_PANEL) ? 0.0 : double(-sidePanelWidth);
				sidePanelShift = static_cast<int>(round(lerp(start, end, smoothStep(0.0, duration, elapsed))));
				m_sidePanelButton->set_pushed(true);
			}
			// only animate the header if it isn't already at the goal position
			if (((m_animationGoal & TOP_PANEL) && headerShift != 0) ||
				(!(m_animationGoal & TOP_PANEL) && headerShift != -headerHeight))
			{
				double start = (m_animationGoal & TOP_PANEL) ? double(-headerHeight) : 0.0;
				double end = (m_animationGoal & TOP_PANEL) ? 0.0 : double(-headerHeight);
				headerShift = static_cast<int>(round(lerp(start, end, smoothStep(0.0, duration, elapsed))));
			}

			// only animate the footer if it isn't already at the goal position
			if (((m_animationGoal & BOTTOM_PANEL) && footerShift != 0) ||
				(!(m_animationGoal & BOTTOM_PANEL) && footerShift != footerHeight))
			{
				double start = (m_animationGoal & BOTTOM_PANEL) ? double(footerHeight) : 0.0;
				double end = (m_animationGoal & BOTTOM_PANEL) ? 0.0 : double(footerHeight);
				footerShift = static_cast<int>(round(lerp(start, end, smoothStep(0.0, duration, elapsed))));
			}
		}
	}

	m_topPanel->set_position(nanogui::Vector2i(0,headerShift));
	m_topPanel->set_fixed_width(width());

	int middleHeight = height() - headerHeight - footerHeight - headerShift + footerShift;

	m_sidePanel->set_position(nanogui::Vector2i(sidePanelShift,headerShift+headerHeight));
	m_sidePanel->set_fixed_height(middleHeight);


	m_imageView->set_position(nanogui::Vector2i(sidePanelShift+sidePanelWidth,headerShift+headerHeight));
	m_imageView->set_fixed_width(width() - sidePanelShift-sidePanelWidth);
	m_imageView->set_fixed_height(middleHeight);
	m_statusBar->set_position(nanogui::Vector2i(0,headerShift+headerHeight+middleHeight));
	m_statusBar->set_fixed_width(width());

	int lh = std::min(middleHeight, m_sidePanelContents->preferred_size(m_nvg_context).y());
	m_sideScrollPanel->set_fixed_height(lh);

	int zoomWidth = m_zoomLabel->preferred_size(m_nvg_context).x();
    m_zoomLabel->set_width(zoomWidth);
    m_zoomLabel->set_position(nanogui::Vector2i(width()-zoomWidth-6, 0));

	perform_layout();
}

void HDRViewScreen::draw_contents()
{
	m_imagesPanel->run_requested_callbacks();
	update_layout();
}
