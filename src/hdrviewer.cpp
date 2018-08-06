//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrviewer.h"
#include "glimage.h"
#include "editimagepanel.h"
#include "imagelistpanel.h"
#include <iostream>
#include "common.h"
#include "commandhistory.h"
#include "hdrimageviewer.h"
#include "helpwindow.h"
#define NOMINMAX
#include <tinydir.h>

using namespace std;

HDRViewScreen::HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args) :
    Screen(Vector2i(800,600), "HDRView", true),
//    m_imageMgr(new HDRImageManager()),
    console(spdlog::get("console"))
{
    setBackground(Color(0.23f, 1.0f));

    Theme * thm = new Theme(mNVGContext);
    thm->mStandardFontSize     = 16;
    thm->mButtonFontSize       = 15;
    thm->mTextBoxFontSize      = 14;
	thm->mWindowCornerRadius   = 4;
	thm->mWindowFillUnfocused  = Color(40, 250);
	thm->mWindowFillFocused    = Color(45, 250);
    setTheme(thm);

	Theme * panelTheme = new Theme(mNVGContext);
	panelTheme = new Theme(mNVGContext);
	panelTheme->mStandardFontSize     = 16;
	panelTheme->mButtonFontSize       = 15;
	panelTheme->mTextBoxFontSize      = 14;
	panelTheme->mWindowCornerRadius   = 0;
	panelTheme->mWindowFillUnfocused  = Color(50, 255);
	panelTheme->mWindowFillFocused    = Color(52, 255);
	panelTheme->mButtonCornerRadius   = 2;
	panelTheme->mWindowHeaderHeight   = 0;
	panelTheme->mWindowDropShadowSize = 0;


	//
	// Construct the top-level widgets
	//

	m_topPanel = new Window(this, "");
	m_topPanel->setTheme(panelTheme);
	m_topPanel->setPosition(Vector2i(0,0));
	m_topPanel->setFixedHeight(30);
	m_topPanel->setLayout(new BoxLayout(Orientation::Horizontal,
	                                    Alignment::Middle, 5, 5));

	m_sidePanel = new Window(this, "");
	m_sidePanel->setTheme(panelTheme);

	m_imageView = new HDRImageViewer(this, this);
	m_imageView->setGridThreshold(20);
	m_imageView->setPixelInfoThreshold(40);

	m_statusBar = new Window(this, "");
	m_statusBar->setTheme(panelTheme);
	m_statusBar->setFixedHeight(m_statusBar->theme()->mTextBoxFontSize+1);

    //
    // create status bar widgets
    //

	m_pixelInfoLabel = new Label(m_statusBar, "", "sans");
	m_pixelInfoLabel->setFontSize(thm->mTextBoxFontSize);
	m_pixelInfoLabel->setPosition(Vector2i(6, 0));

	m_zoomLabel = new Label(m_statusBar, "100% (1 : 1)", "sans");
	m_zoomLabel->setFontSize(thm->mTextBoxFontSize);

    //
    // create side panel widgets
    //

	m_sideScrollPanel = new VScrollPanel(m_sidePanel);
	m_sidePanelContents = new Widget(m_sideScrollPanel);
	m_sidePanelContents->setLayout(new BoxLayout(Orientation::Vertical,
	                                             Alignment::Fill, 4, 4));
	m_sidePanelContents->setFixedWidth(213);
	m_sideScrollPanel->setFixedWidth(m_sidePanelContents->fixedWidth() + 12);
	m_sidePanel->setFixedWidth(m_sideScrollPanel->fixedWidth());

    //
    // create file/images panel
    //

    auto btn = new Button(m_sidePanelContents, "File", ENTYPO_ICON_CHEVRON_DOWN);
	btn->setFlags(Button::ToggleButton);
	btn->setPushed(true);
	btn->setFontSize(18);
	btn->setIconPosition(Button::IconPosition::Right);
    m_imagesPanel = new ImageListPanel(m_sidePanelContents, this, m_imageView);

	btn->setChangeCallback([this,btn](bool value)
                         {
	                         btn->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_LEFT);
	                         m_imagesPanel->setVisible(value);
                             updateLayout();
                             m_sidePanelContents->performLayout(mNVGContext);
                         });

    //
    // create edit panel
    //

	btn = new Button(m_sidePanelContents, "Edit", ENTYPO_ICON_CHEVRON_LEFT);
	btn->setFlags(Button::ToggleButton);
	btn->setFontSize(18);
	btn->setIconPosition(Button::IconPosition::Right);

	auto editPanel = new EditImagePanel(m_sidePanelContents, this, m_imagesPanel);
	editPanel->setVisible(false);

	btn->setChangeCallback([this,btn,editPanel](bool value)
	 {
		 btn->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_LEFT);
		 editPanel->setVisible(value);
		 updateLayout();
		 m_sidePanelContents->performLayout(mNVGContext);
	 });
	editPanel->performLayout(mNVGContext);

    //
    // create top panel controls
    //

	m_helpButton = new Button{m_topPanel, "", ENTYPO_ICON_HELP};
	m_helpButton->setFixedSize(Vector2i(25, 25));
	m_helpButton->setChangeCallback([this](bool) { toggleHelpWindow(); });
	m_helpButton->setTooltip("Information about using HDRView.");
	m_helpButton->setFlags(Button::ToggleButton);

    m_sidePanelButton = new Button(m_topPanel, "", ENTYPO_ICON_MENU);
    new Label(m_topPanel, "EV", "sans-bold");
    auto exposureSlider = new Slider(m_topPanel);
    auto exposureTextBox = new FloatBox<float>(m_topPanel, exposure);
	auto normalizeButton = new Button(m_topPanel, "", ENTYPO_ICON_FLASH);
	normalizeButton->setFixedSize(Vector2i(19, 19));
	normalizeButton->setCallback([this](void)
	                             {
		                             auto img = m_imagesPanel->currentImage();
		                             if (!img)
			                             return;
		                             Color4 mC = img->image().max();
		                             float mCf = max(mC[0], mC[1], mC[2]);
		                             console->debug("max value: {}", mCf);
		                             m_imageView->setExposure(log2(1.0f/mCf));
		                             m_imagesPanel->requestHistogramUpdate(true);
	                             });
	normalizeButton->setTooltip("Normalize exposure.");
	auto resetButton = new Button(m_topPanel, "", ENTYPO_ICON_CYCLE);
	resetButton->setFixedSize(Vector2i(19, 19));
	resetButton->setCallback([this](void)
	                             {
		                             m_imageView->setExposure(0.0f);
		                             m_imageView->setGamma(2.2f);
		                             m_imageView->setSRGB(true);
		                             m_imagesPanel->requestHistogramUpdate(true);
	                             });
	resetButton->setTooltip("Reset tonemapping.");

    auto sRGBCheckbox = new CheckBox(m_topPanel, "sRGB   ");
    auto gammaLabel = new Label(m_topPanel, "Gamma", "sans-bold");
    auto gammaSlider = new Slider(m_topPanel);
    auto gammaTextBox = new FloatBox<float>(m_topPanel);

    m_sidePanelButton->setTooltip("Bring up the images dialog to load/remove images, and cycle through open images.");
    m_sidePanelButton->setFlags(Button::ToggleButton);
	m_sidePanelButton->setPushed(true);
    m_sidePanelButton->setFixedSize(Vector2i(25, 25));
    m_sidePanelButton->setChangeCallback([this](bool value)
    {
	    m_guiAnimationStart = glfwGetTime();
	    m_guiTimerRunning = true;
	    m_animationGoal = EAnimationGoal(m_animationGoal ^ SIDE_PANEL);
        updateLayout();
    });

    exposureTextBox->numberFormat("%1.2f");
    exposureTextBox->setEditable(true);
	exposureTextBox->setSpinnable(true);
    exposureTextBox->setFixedWidth(50);
	exposureTextBox->setMinValue(-9.0f);
	exposureTextBox->setMaxValue( 9.0f);
    exposureTextBox->setAlignment(TextBox::Alignment::Right);
    exposureTextBox->setCallback([this](float e)
                                 {
	                                 m_imageView->setExposure(e);
                                 });
    exposureSlider->setCallback([this](float v)
						        {
							        m_imageView->setExposure(round(4*v) / 4.0f);
						        });
	exposureSlider->setFinalCallback([this](float v)
	                                 {
		                                 m_imageView->setExposure(round(4*v) / 4.0f);
		                                 m_imagesPanel->requestHistogramUpdate(true);
	                                 });
    exposureSlider->setFixedWidth(100);
    exposureSlider->setRange({-9.0f,9.0f});
    exposureTextBox->setValue(exposure);

    gammaTextBox->setEditable(true);
	gammaTextBox->setSpinnable(true);
    gammaTextBox->numberFormat("%1.3f");
    gammaTextBox->setFixedWidth(55);
	gammaTextBox->setMinValue(0.02f);
	gammaTextBox->setMaxValue(9.0f);

    gammaTextBox->setAlignment(TextBox::Alignment::Right);
    gammaTextBox->setCallback([this,gammaSlider](float value)
                                {
                                    m_imageView->setGamma(value);
                                    gammaSlider->setValue(value);
                                });
    gammaSlider->setCallback(
	    [&,gammaSlider,gammaTextBox](float value)
	    {
		    float g = max(gammaSlider->range().first, round(10*value) / 10.0f);
		    m_imageView->setGamma(g);
		    gammaTextBox->setValue(g);
		    gammaSlider->setValue(g);       // snap values
	    });
    gammaSlider->setFixedWidth(100);
    gammaSlider->setRange({0.02f,9.0f});
    gammaSlider->setValue(gamma);
    gammaTextBox->setValue(gamma);

    m_imageView->setExposureCallback([this,exposureTextBox,exposureSlider](float e)
                                     {
	                                     exposureTextBox->setValue(e);
	                                     exposureSlider->setValue(e);
	                                     m_imagesPanel->requestHistogramUpdate();
                                     });
    m_imageView->setGammaCallback([gammaTextBox,gammaSlider](float g)
                                  {
	                                  gammaTextBox->setValue(g);
	                                  gammaSlider->setValue(g);
                                  });
	m_imageView->setSRGBCallback([sRGBCheckbox,gammaTextBox,gammaSlider](bool b)
	                              {
		                              sRGBCheckbox->setChecked(b);
		                              gammaTextBox->setEnabled(!b);
		                              gammaTextBox->setSpinnable(!b);
		                              gammaSlider->setEnabled(!b);
	                              });
    m_imageView->setExposure(exposure);
    m_imageView->setGamma(gamma);
    m_imageView
	    ->setPixelHoverCallback([this](const Vector2i &pixelCoord, const Color4 &pixelVal, const Color4 &iPixelVal)
	                            {
		                            auto img = m_imagesPanel->currentImage();

		                            if (img && img->contains(pixelCoord))
		                            {
			                            string s = fmt::format(
				                            "({: 4d},{: 4d}) = ({: 6.3f}, {: 6.3f}, {: 6.3f}, {: 6.3f}) / ({: 3d}, {: 3d}, {: 3d}, {: 3d})",
				                            pixelCoord.x(), pixelCoord.y(),
				                            pixelVal[0], pixelVal[1], pixelVal[2], pixelVal[3],
				                            (int) round(iPixelVal[0]), (int) round(iPixelVal[1]),
				                            (int) round(iPixelVal[2]), (int) round(iPixelVal[3]));
			                            m_pixelInfoLabel->setCaption(s);
		                            }
		                            else
			                            m_pixelInfoLabel->setCaption("");

		                            m_statusBar->performLayout(mNVGContext);
	                            });

    m_imageView->setZoomCallback([this](float zoom)
                                {
                                    float realZoom = zoom * pixelRatio();
                                    int ratio1 = (realZoom < 1.0f) ? 1 : (int)round(realZoom);
                                    int ratio2 = (realZoom < 1.0f) ? (int)round(1.0f/realZoom) : 1;
                                    m_zoomLabel->setCaption(fmt::format("{:7.3f}% ({:d} : {:d})", realZoom * 100, ratio1, ratio2));
                                    updateLayout();
                                });

	sRGBCheckbox->setCallback([&,gammaSlider,gammaTextBox,gammaLabel](bool value)
    {
        m_imageView->setSRGB(value);
        gammaSlider->setEnabled(!value);
	    gammaTextBox->setSpinnable(!value);
        gammaTextBox->setEnabled(!value);
        gammaLabel->setEnabled(!value);
        gammaLabel->setColor(value ? mTheme->mDisabledTextColor : mTheme->mTextColor);
        updateLayout();
    });

	sRGBCheckbox->setChecked(sRGB);
	sRGBCheckbox->callback()(sRGB);

    (new CheckBox(m_topPanel, "Dither  ",
                 [&](bool v) { m_imageView->setDithering(v); }))->setChecked(m_imageView->ditheringOn());
    (new CheckBox(m_topPanel, "Grid  ",
                 [&](bool v) { m_imageView->setDrawGrid(v); }))->setChecked(m_imageView->drawGridOn());
    (new CheckBox(m_topPanel, "RGB values  ",
                 [&](bool v) { m_imageView->setDrawValues(v); }))->setChecked(m_imageView->drawValuesOn());

	dropEvent(args);

	this->setSize(Vector2i(1024, 800));
	updateLayout();
	setResizeCallback([&](Vector2i)
	                  {
		                  updateLayout();
		                  drawAll();
	                  });

    setVisible(true);
    glfwSwapInterval(1);
}

HDRViewScreen::~HDRViewScreen()
{
    // empty
}


void HDRViewScreen::updateCaption()
{
    auto img = m_imagesPanel->currentImage();
    if (img)
        setCaption(string("HDRView [") + img->filename() + (img->isModified() ? "*" : "") + "]");
    else
        setCaption(string("HDRView"));
}

bool HDRViewScreen::dropEvent(const vector<string> & filenames)
{
	try
	{
		m_imagesPanel->loadImages(filenames);

		// Ensure the new image button will have the correct visibility state.
		m_imagesPanel->setFilter(m_imagesPanel->filter());
	}
	catch (const exception &e)
	{
		new MessageDialog(this, MessageDialog::Type::Warning, "Error",
		                  string("Could not load:\n ") + e.what());
		return false;
	}
	return true;
}

void HDRViewScreen::askCloseImage(int)
{
	auto closeit = [this](int curr, int next)
	{
		m_imagesPanel->closeImage();
//
////			if (!m_imagesPanel->nthImageIsVisible(curr))
//			{
////				m_imageMgr->setCurrentImageIndex(-1);
//				auto next = m_imagesPanel->nextVisibleImage(0, Forward);
//				cout << "curr: " << curr << "; next: " << next << endl;
//				m_imageMgr->setCurrentImageIndex(next, true);
//				cout << "curr: " << m_imageMgr->currentImageIndex() << endl;
//			}
//			m_imageMgr->numImagesCallback()();
		cout << "curr: " << m_imagesPanel->currentImageIndex() << endl;
	};

	auto curr = m_imagesPanel->currentImageIndex();
	auto next = m_imagesPanel->nextVisibleImage(curr, Forward);
	cout << "curr: " << curr << "; next: " << next << endl;
	if (auto img = m_imagesPanel->image(curr))
	{
		if (img->isModified())
		{
			auto dialog = new MessageDialog(this, MessageDialog::Type::Warning, "Warning!",
			                                "Image has unsaved modifications. Close anyway?", "Yes", "Cancel", true);
			dialog->setCallback([curr,next,closeit](int close){if (close == 0) closeit(curr,next);});
		}
		else
			closeit(curr,next);
	}
}

void HDRViewScreen::askCloseAllImages()
{
	bool anyModified = false;
	for (int i = 0; i < m_imagesPanel->numImages(); ++i)
		anyModified |= m_imagesPanel->image(i)->isModified();

	if (anyModified)
	{
		auto dialog = new MessageDialog(this, MessageDialog::Type::Warning, "Warning!",
		                                "Some images have unsaved modifications. Close all images anyway?", "Yes", "Cancel", true);
		dialog->setCallback([this](int close){if (close == 0) m_imagesPanel->closeAllImages();});
	}
	else
		m_imagesPanel->closeAllImages();
}


void HDRViewScreen::toggleHelpWindow()
{
	if (m_helpWindow)
	{
		m_helpWindow->dispose();
		m_helpWindow = nullptr;
		m_helpButton->setPushed(false);
	}
	else
	{
		m_helpWindow = new HelpWindow{this, [this] { toggleHelpWindow(); }};
		m_helpWindow->center();
		m_helpWindow->requestFocus();
		m_helpButton->setPushed(true);
	}

	updateLayout();
}


bool HDRViewScreen::loadImage()
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
	glfwFocusWindow(mGLFWWindow);

	if (files.size())
		return dropEvent(files);
	return false;
}

void HDRViewScreen::saveImage()
{
	try
	{
		if (!m_imagesPanel->currentImage())
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
		glfwFocusWindow(mGLFWWindow);

		if (filename.size())
			m_imagesPanel->saveImage(filename, m_imageView->exposure(), m_imageView->gamma(),
			                      m_imageView->sRGB(), m_imageView->ditheringOn());
	}
	catch (const exception &e)
	{
		new MessageDialog(this, MessageDialog::Type::Warning, "Error",
		                  string("Could not save image due to an error:\n") + e.what());
	}
}


void HDRViewScreen::flipImage(bool h)
{
    if (h)
		m_imagesPanel->modifyImage(
		    [](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
		    {
			    return {make_shared<HDRImage>(img->flippedHorizontal()),
			            make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->flippedHorizontal(); })};
		    });
    else
		m_imagesPanel->modifyImage(
		    [](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
		    {
			    return {make_shared<HDRImage>(img->flippedVertical()),
			            make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->flippedVertical(); })};
		    });
}


bool HDRViewScreen::keyboardEvent(int key, int scancode, int action, int modifiers)
{
	if (Screen::keyboardEvent(key, scancode, action, modifiers))
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
                m_okToQuitDialog->setCallback([this](int result)
                    {
                        this->setVisible(result != 0);
                        m_okToQuitDialog = nullptr;
                    });
	            m_okToQuitDialog->requestFocus();
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
	        askCloseImage(m_imagesPanel->currentImageIndex());
            return true;
        case 'W':
            if (modifiers & SYSTEM_COMMAND_MOD)
            {
	            if (modifiers & GLFW_MOD_SHIFT)
		            askCloseAllImages();
	            else
	                askCloseImage(m_imagesPanel->currentImageIndex());
                return true;
            }
            return false;

	    case 'O':
		    if (modifiers & SYSTEM_COMMAND_MOD)
		    {
			    loadImage();
			    return true;
		    }
		    return false;

        case '=':
        case GLFW_KEY_KP_ADD:
		    m_imageView->zoomIn();
            return true;

        case '-':
        case GLFW_KEY_KP_SUBTRACT:
		    m_imageView->zoomOut();
            return true;

        case 'G':
            if (modifiers & GLFW_MOD_SHIFT)
	            m_imageView->setGamma(m_imageView->gamma() + 0.02f);
            else
	            m_imageView->setGamma(max(0.02f, m_imageView->gamma() - 0.02f));
            return true;
        case 'E':
            if (modifiers & GLFW_MOD_SHIFT)
	            m_imageView->setExposure(m_imageView->exposure() + 0.25f);
            else
	            m_imageView->setExposure(m_imageView->exposure() - 0.25f);
            return true;

        case 'F':
            flipImage(false);
            return true;

        case 'M':
            flipImage(true);
            return true;

        case ' ':
	        m_imageView->center();
            drawAll();
            return true;

        case 'T':
		    m_guiAnimationStart = glfwGetTime();
		    m_guiTimerRunning = true;
		    m_animationGoal = EAnimationGoal(m_animationGoal ^ TOP_PANEL);
		    updateLayout();
            return true;

        case 'H':
		    toggleHelpWindow();
            return true;

    	case 'P':
    		if (modifiers & SYSTEM_COMMAND_MOD)
			    m_imagesPanel->focusFilter();
		    return true;

        case GLFW_KEY_TAB:
	        if (modifiers & GLFW_MOD_SHIFT)
	        {
		        bool setVis = !((m_animationGoal & SIDE_PANEL) || (m_animationGoal & TOP_PANEL) || (m_animationGoal & BOTTOM_PANEL));
		        m_guiAnimationStart = glfwGetTime();
		        m_guiTimerRunning = true;
		        m_animationGoal = setVis ? EAnimationGoal(TOP_PANEL | SIDE_PANEL | BOTTOM_PANEL) : EAnimationGoal(0);
	        }
		    else
	        {
		        m_guiAnimationStart = glfwGetTime();
		        m_guiTimerRunning = true;
		        m_animationGoal = EAnimationGoal(m_animationGoal ^ SIDE_PANEL);
	        }
		    updateLayout();
            return true;

        case GLFW_KEY_DOWN:
	        if (modifiers & SYSTEM_COMMAND_MOD)
	        {
		        m_imagesPanel->sendImageBackward();
		        return true;
	        }
	        else if (m_imagesPanel->numImages())
            {
				m_imagesPanel->setCurrentImageIndex(m_imagesPanel->nextVisibleImage(m_imagesPanel->currentImageIndex(), Backward));
	            return true;
            }
		    return false;

        case GLFW_KEY_UP:
	        if (modifiers & SYSTEM_COMMAND_MOD)
	        {
		        m_imagesPanel->bringImageForward();
		        return true;
	        }
	        else if (m_imagesPanel->numImages())
	        {
				m_imagesPanel->setCurrentImageIndex(m_imagesPanel->nextVisibleImage(m_imagesPanel->currentImageIndex(), Forward));
		        return true;
	        }
		    return false;

	    case '0':
		    if (modifiers & SYSTEM_COMMAND_MOD)
		    {
			    m_imageView->center();
			    m_imageView->fit();
			    drawAll();
			    return true;
		    }
		    return false;
    }

	if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
	{
		int idx = (key - GLFW_KEY_1) % 10;

		if (modifiers & SYSTEM_COMMAND_MOD && idx < NUM_CHANNELS)
		{
			m_imagesPanel->setChannel(EChannel(idx));
			return true;
		}
		else if (modifiers & GLFW_MOD_SHIFT && idx < NUM_BLEND_MODES)
		{
			m_imagesPanel->setBlendMode(EBlendMode(idx));
			return true;
		}
		else
		{
			auto nth = m_imagesPanel->nthVisibleImageIndex(idx);
			if (nth > 0)
				m_imagesPanel->setCurrentImageIndex(nth);
		}
		return false;
	}

    return false;
}

bool HDRViewScreen::mouseButtonEvent(const Vector2i &p, int button, bool down, int modifiers)
{
	if (button == GLFW_MOUSE_BUTTON_1 && down && atSidePanelEdge(p))
	{
		m_draggingSidePanel = true;

		// prevent Screen::cursorPosCallbackEvent from calling dragEvent on other widgets
		mDragActive = false;
		mDragWidget = nullptr;
		return true;
	}
	else
		m_draggingSidePanel = false;

	return Screen::mouseButtonEvent(p, button, down, modifiers);
}

bool HDRViewScreen::mouseMotionEvent(const Eigen::Vector2i &p, const Eigen::Vector2i &rel, int button, int modifiers)
{
	if (m_draggingSidePanel || atSidePanelEdge(p))
	{
		m_sidePanel->setCursor(Cursor::HResize);
		m_sideScrollPanel->setCursor(Cursor::HResize);
		m_sidePanelContents->setCursor(Cursor::HResize);
		m_imageView->setCursor(Cursor::HResize);
	}
	else
	{
		m_sidePanel->setCursor(Cursor::Arrow);
		m_sideScrollPanel->setCursor(Cursor::Arrow);
		m_sidePanelContents->setCursor(Cursor::Arrow);
		m_imageView->setCursor(Cursor::Arrow);
	}

	if (m_draggingSidePanel)
	{
		int w = clamp(p.x(), 206, mSize.x() - 10);
		m_sidePanelContents->setFixedWidth(w);
		m_sideScrollPanel->setFixedWidth(w + 12);
		m_sidePanel->setFixedWidth(m_sideScrollPanel->fixedWidth());
		updateLayout();
		return true;
	}

	return Screen::mouseMotionEvent(p, rel, button, modifiers);
}


void HDRViewScreen::updateLayout()
{
	int headerHeight = m_topPanel->fixedHeight();
	int sidePanelWidth = m_sidePanel->fixedWidth();
	int footerHeight = m_statusBar->fixedHeight();

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

			m_sidePanelButton->setPushed(m_animationGoal & SIDE_PANEL);
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
				sidePanelShift = round(lerp(start, end, smoothStep(0.0, duration, elapsed)));
				m_sidePanelButton->setPushed(true);
			}
			// only animate the header if it isn't already at the goal position
			if (((m_animationGoal & TOP_PANEL) && headerShift != 0) ||
				(!(m_animationGoal & TOP_PANEL) && headerShift != -headerHeight))
			{
				double start = (m_animationGoal & TOP_PANEL) ? double(-headerHeight) : 0.0;
				double end = (m_animationGoal & TOP_PANEL) ? 0.0 : double(-headerHeight);
				headerShift = round(lerp(start, end, smoothStep(0.0, duration, elapsed)));
			}

			// only animate the footer if it isn't already at the goal position
			if (((m_animationGoal & BOTTOM_PANEL) && footerShift != 0) ||
				(!(m_animationGoal & BOTTOM_PANEL) && footerShift != footerHeight))
			{
				double start = (m_animationGoal & BOTTOM_PANEL) ? double(footerHeight) : 0.0;
				double end = (m_animationGoal & BOTTOM_PANEL) ? 0.0 : double(footerHeight);
				footerShift = round(lerp(start, end, smoothStep(0.0, duration, elapsed)));
			}
		}
	}

	m_topPanel->setPosition(Vector2i(0,headerShift));
	m_topPanel->setFixedWidth(width());

	int middleHeight = height() - headerHeight - footerHeight - headerShift + footerShift;

	m_sidePanel->setPosition(Vector2i(sidePanelShift,headerShift+headerHeight));
	m_sidePanel->setFixedHeight(middleHeight);


	m_imageView->setPosition(Vector2i(sidePanelShift+sidePanelWidth,headerShift+headerHeight));
	m_imageView->setFixedWidth(width() - sidePanelShift-sidePanelWidth);
	m_imageView->setFixedHeight(middleHeight);
	m_statusBar->setPosition(Vector2i(0,headerShift+headerHeight+middleHeight));
	m_statusBar->setFixedWidth(width());

	int lh = std::min(middleHeight, m_sidePanelContents->preferredSize(mNVGContext).y());
	m_sideScrollPanel->setFixedHeight(lh);

	int zoomWidth = m_zoomLabel->preferredSize(mNVGContext).x();
    m_zoomLabel->setWidth(zoomWidth);
    m_zoomLabel->setPosition(Vector2i(width()-zoomWidth-6, 0));

	performLayout();
}

void HDRViewScreen::drawContents()
{
	m_imagesPanel->runRequestedCallbacks();
	updateLayout();
}