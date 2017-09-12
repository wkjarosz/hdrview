/*! \file hdrviewer.cpp
    \author Wojciech Jarosz
*/
#include "hdrviewer.h"
#include "glimage.h"
#include "editimagepanel.h"
#include "imagelistpanel.h"
#include <iostream>
#include "common.h"
#include "commandhistory.h"
#include "hdrimageviewer.h"
#include "hdrimagemanager.h"
#include "helpwindow.h"
#define NOMINMAX
#include <tinydir.h>

using namespace std;

HDRViewScreen::HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args) :
    Screen(Vector2i(800,600), "HDRView", true),
    m_imageMgr(new HDRImageManager()),
    console(spdlog::get("console"))
{
    setBackground(Color(0.23f, 1.0f));

    Theme * thm = new Theme(mNVGContext);
    thm->mStandardFontSize     = 16;
    thm->mButtonFontSize       = 15;
    thm->mTextBoxFontSize      = 14;
    setTheme(thm);

	Theme * panelTheme = new Theme(mNVGContext);
	panelTheme = new Theme(mNVGContext);
	panelTheme->mStandardFontSize     = 16;
	panelTheme->mButtonFontSize       = 15;
	panelTheme->mTextBoxFontSize      = 14;
	panelTheme->mButtonCornerRadius   = 2;
	panelTheme->mWindowHeaderHeight   = 0;
	panelTheme->mWindowDropShadowSize = 0;
	panelTheme->mWindowCornerRadius   = 0;
	panelTheme->mWindowFillFocused    = Color(.2f,.2f,.2f,.9f);
	panelTheme->mWindowFillUnfocused  = Color(.2f,.2f,.2f,.9f);


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
	m_imageView->setPixelInfoThreshold(20);

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
	m_sidePanelContents->setFixedWidth(195);
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
    m_imagesPanel = new ImageListPanel(m_sidePanelContents, this, m_imageMgr, m_imageView);

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

	auto editPanel = new EditImagePanel(m_sidePanelContents, this, m_imageMgr);
    editPanel->setVisible(false);

	btn->setChangeCallback([this,btn,editPanel](bool value)
     {
	     btn->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_LEFT);
         editPanel->setVisible(value);
         updateLayout();
         m_sidePanelContents->performLayout(mNVGContext);
     });

    //
    // create top panel controls
    //

	m_helpButton = new Button{m_topPanel, "", ENTYPO_ICON_HELP};
	m_helpButton->setFixedSize(Vector2i(25, 25));
	m_helpButton->setChangeCallback([this](bool) { toggleHelpWindow(); });
	m_helpButton->setTooltip("Information about using HDRView.");
	m_helpButton->setFlags(Button::ToggleButton);

    m_sidePanelButton = new Button(m_topPanel, "", ENTYPO_ICON_LIST);
    new Label(m_topPanel, "EV", "sans-bold");
    auto exposureSlider = new Slider(m_topPanel);
    auto exposureTextBox = new FloatBox<float>(m_topPanel, exposure);
	auto normalizeButton = new Button(m_topPanel, "", ENTYPO_ICON_FLASH);
	normalizeButton->setFixedSize(Vector2i(19, 19));
	normalizeButton->setCallback([this](void)
	                             {
		                             auto img = m_imageMgr->currentImage();
		                             if (!img)
			                             return;
		                             float mC = img->image().maxCoeff().max();
		                             m_imageView->setExposure(log2(1.0f/mC));
	                             });
	normalizeButton->setTooltip("Normalize exposure.");
	auto resetButton = new Button(m_topPanel, "", ENTYPO_ICON_BACK_IN_TIME);
	resetButton->setFixedSize(Vector2i(19, 19));
	resetButton->setCallback([this](void)
	                             {
		                             m_imageView->setExposure(0.0f);
		                             m_imageView->setGamma(2.2f);
		                             m_imageView->setSRGB(true);
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
	    m_guiTimer.reset();
	    m_guiTimerRunning = true;
	    m_animationGoal = EAnimationGoal(m_animationGoal ^ SIDE_PANEL);
        updateLayout();
    });

    exposureTextBox->numberFormat("%1.2f");
    exposureTextBox->setEditable(true);
    exposureTextBox->setFixedWidth(35);
    exposureTextBox->setAlignment(TextBox::Alignment::Right);
    exposureTextBox->setCallback([this](float e)
                                 {
                                       m_imageView->setExposure(e);
                                 });
    exposureSlider->setCallback([this](float v)
						        {
							        m_imageView->setExposure(round(4*v) / 4.0f);
						        });
    exposureSlider->setFixedWidth(100);
    exposureSlider->setRange({-9.0f,9.0f});
    exposureTextBox->setValue(exposure);

    gammaTextBox->setEditable(true);
    gammaTextBox->numberFormat("%1.3f");
    gammaTextBox->setFixedWidth(40);
    gammaTextBox->setAlignment(TextBox::Alignment::Right);
    gammaTextBox->setCallback([this,gammaSlider](float value)
                                {
                                    m_imageView->setGamma(value);
                                    gammaSlider->setValue(value);
                                });
    gammaSlider->setCallback([&,gammaSlider,gammaTextBox](float value)
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
	                                     m_imagesPanel->enableDisableButtons();
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
		                              gammaSlider->setEnabled(!b);
	                              });
    m_imageView->setExposure(exposure);
    m_imageView->setGamma(gamma);
    m_imageView
	    ->setPixelHoverCallback([this](const Vector2i &pixelCoord, const Color4 &pixelVal, const Color4 &iPixelVal)
	                            {
		                            auto img = m_imageMgr->currentImage();

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

	m_imageMgr->setCurrentImageCallback([this, editPanel](void)
	                                    {
		                                    m_imageView->setCurrentImage(m_imageMgr->currentImage());
		                                    updateCaption();
		                                    m_imagesPanel->setCurrentImage(m_imageMgr->currentImageIndex());
		                                    m_imagesPanel->enableDisableButtons();
		                                    editPanel->enableDisableButtons();
	                                    });

	m_imageMgr->setReferenceImageCallback([this](void)
	                                    {
		                                    m_imageView->setReferenceImage(m_imageMgr->referenceImage());
		                                    m_imagesPanel->setReferenceImage(m_imageMgr->referenceImageIndex());
	                                    });

	m_imageMgr->setNumImagesCallback([this, editPanel](void)
	                                 {
		                                 updateCaption();
		                                 m_imagesPanel->enableDisableButtons();
		                                 editPanel->enableDisableButtons();
		                                 m_imagesPanel->repopulateImageList();
		                                 m_imagesPanel->setCurrentImage(m_imageMgr->currentImageIndex());
		                                 m_imageMgr->setReferenceImageIndex(-1);
	                                 });
    m_imageMgr->setImageChangedCallback([this,editPanel](int i)
                                         {
	                                         updateCaption();
	                                         m_imagesPanel->enableDisableButtons();
	                                         editPanel->enableDisableButtons();
	                                         m_imagesPanel->repopulateImageList();
	                                         m_imagesPanel->setCurrentImage(i);
	                                         m_imagesPanel->setReferenceImage(m_imageMgr->referenceImageIndex());
                                         });


	sRGBCheckbox->setCallback([&,gammaSlider,gammaTextBox,gammaLabel](bool value)
    {
        m_imageView->setSRGB(value);
        gammaSlider->setEnabled(!value);
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

	setResizeCallback([&](Vector2i)
	                  {
		                  updateLayout();
		                  drawAll();
	                  });

	this->setSize(Vector2i(1024, 800));


    drawAll();
    setVisible(true);
    glfwSwapInterval(1);
}

HDRViewScreen::~HDRViewScreen()
{
    // empty
}


void HDRViewScreen::updateCaption()
{
    auto img = m_imageMgr->currentImage();
    if (img)
        setCaption(string("HDRView [") + img->filename() + (img->isModified() ? "*" : "") + "]");
    else
        setCaption(string("HDRView"));
}

bool HDRViewScreen::dropEvent(const vector<string> & filenames)
{
	try
	{
		m_imageMgr->loadImages(filenames);
	}
	catch (std::runtime_error &e)
	{
		new MessageDialog(this, MessageDialog::Type::Warning, "Error",
		                  string("Could not load:\n ") + e.what());
		return false;
	}
	return true;
}

void HDRViewScreen::askCloseImage(int index)
{
	if (auto img = m_imageMgr->image(index))
	{
		if (img->isModified())
		{
			auto dialog = new MessageDialog(this, MessageDialog::Type::Warning, "Warning!",
			                                "Image has unsaved modifications. Close anyway?", "Close anyway", "Cancel", true);
			dialog->setCallback([&,index](int close){if (close == 0) m_imageMgr->closeImage(index);});
		}
		else
			m_imageMgr->closeImage(index);
	}
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
	string file = file_dialog(
		{
			{"exr", "OpenEXR image"},
			{"png", "Portable Network Graphic"},
			{"pfm", "Portable Float Map"},
			{"ppm", "Portable PixMap"},
			{"jpg", "Jpeg image"},
			{"tga", "Targa image"},
			{"bmp", "Windows Bitmap image"},
			{"gif", "GIF image"},
			{"hdr", "Radiance rgbE format"},
			{"ppm", "Portable pixel map"},
			{"psd", "Photoshop document"}
		}, false);

	if (file.size())
		return dropEvent({file});
	return false;
}

void HDRViewScreen::saveImage()
{
	try
	{
		if (!m_imageMgr->currentImage())
			return;

		string filename = file_dialog({
			                              {"png", "Portable Network Graphic"},
			                              {"pfm", "Portable Float Map"},
			                              {"ppm", "Portable PixMap"},
			                              {"tga", "Targa image"},
			                              {"bmp", "Windows Bitmap image"},
			                              {"hdr", "Radiance rgbE format"},
			                              {"exr", "OpenEXR image"}
		                              }, true);

		if (filename.size())
			m_imageMgr->saveImage(filename, m_imageView->exposure(), m_imageView->gamma(),
			                      m_imageView->sRGB(), m_imageView->ditheringOn());
	}
	catch (std::runtime_error &e)
	{
		new MessageDialog(this, MessageDialog::Type::Warning, "Error",
		                  string("Could not save image due to an error:\n") + e.what());
	}
}


void HDRViewScreen::flipImage(bool h)
{
    if (h)
	    m_imageMgr->modifyImage([](HDRImage &img)
	                {
		                img = img.flippedHorizontal();
		                return new LambdaUndo([](HDRImage &img2) { img2 = img2.flippedHorizontal(); });
	                });
    else
	    m_imageMgr->modifyImage([](HDRImage &img)
	                {
		                img = img.flippedVertical();
		                return new LambdaUndo([](HDRImage &img2) { img2 = img2.flippedVertical(); });
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
            if (modifiers & GLFW_MOD_SUPER)
            {
                if (modifiers & GLFW_MOD_SHIFT)
	                m_imageMgr->redo();
	            else
	                m_imageMgr->undo();

	            return true;
            }
            return false;

        case GLFW_KEY_BACKSPACE:
	        askCloseImage(m_imageMgr->currentImageIndex());
            return true;
        case 'W':
            if (modifiers & GLFW_MOD_SUPER)
            {
	            askCloseImage(m_imageMgr->currentImageIndex());
                return true;
            }
            return false;

	    case 'O':
		    if (modifiers & GLFW_MOD_SUPER)
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
		    m_guiTimer.reset();
		    m_guiTimerRunning = true;
		    m_animationGoal = EAnimationGoal(m_animationGoal ^ TOP_PANEL);
		    updateLayout();
            return true;

        case 'H':
		    toggleHelpWindow();
            return true;

        case GLFW_KEY_TAB:
	        if (modifiers & GLFW_MOD_SHIFT)
	        {
		        bool setVis = !((m_animationGoal & SIDE_PANEL) || (m_animationGoal & TOP_PANEL) || (m_animationGoal & BOTTOM_PANEL));
		        m_guiTimer.reset();
		        m_guiTimerRunning = true;
		        m_animationGoal = setVis ? EAnimationGoal(TOP_PANEL | SIDE_PANEL | BOTTOM_PANEL) : EAnimationGoal(0);
	        }
		    else
	        {
		        m_guiTimer.reset();
		        m_guiTimerRunning = true;
		        m_animationGoal = EAnimationGoal(m_animationGoal ^ SIDE_PANEL);
	        }
		    updateLayout();
            return true;

        case GLFW_KEY_DOWN:
	        if (modifiers & GLFW_MOD_SUPER)
	        {
		        m_imageMgr->sendImageBackward();
		        return true;
	        }
	        else if (m_imageMgr->numImages())
            {
	            m_imageMgr->setCurrentImageIndex(mod(m_imageMgr->currentImageIndex() + 1, m_imageMgr->numImages()));
	            return true;
            }
		    return false;

        case GLFW_KEY_UP:
	        if (modifiers & GLFW_MOD_SUPER)
	        {
		        m_imageMgr->bringImageForward();
		        return true;
	        }
	        else if (m_imageMgr->numImages())
	        {
		        m_imageMgr->setCurrentImageIndex(mod(m_imageMgr->currentImageIndex() - 1, m_imageMgr->numImages()));
		        return true;
	        }
		    return false;

	    case '0':
		    if (modifiers & GLFW_MOD_SUPER)
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

		if (modifiers & GLFW_MOD_SUPER && idx <= 6)
		{
			m_imagesPanel->setChannel(EChannel(idx));
			return true;
		}
		else if (modifiers & GLFW_MOD_SHIFT && idx <= 7)
		{
			m_imagesPanel->setBlendMode(EBlendMode(idx));
			return true;
		}
		else
		{
			if (m_imageMgr->numImages() > idx)
				m_imageMgr->setCurrentImageIndex(idx);
		}
		return false;
	}

    return false;
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
		float duration = 200.f;
		float elapsed = m_guiTimer.elapsed();
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
				float start = (m_animationGoal & SIDE_PANEL) ? float(-sidePanelWidth) : 0.f;
				float end = (m_animationGoal & SIDE_PANEL) ? 0.f : float(-sidePanelWidth);
				sidePanelShift = round(lerp(start, end, smoothStep(0.f, duration, elapsed)));
				m_sidePanelButton->setPushed(true);
			}
			// only animate the header if it isn't already at the goal position
			if (((m_animationGoal & TOP_PANEL) && headerShift != 0) ||
				(!(m_animationGoal & TOP_PANEL) && headerShift != -headerHeight))
			{
				float start = (m_animationGoal & TOP_PANEL) ? float(-headerHeight) : 0.f;
				float end = (m_animationGoal & TOP_PANEL) ? 0.f : float(-headerHeight);
				headerShift = round(lerp(start, end, smoothStep(0.f, duration, elapsed)));
			}

			// only animate the footer if it isn't already at the goal position
			if (((m_animationGoal & BOTTOM_PANEL) && footerShift != 0) ||
				(!(m_animationGoal & BOTTOM_PANEL) && footerShift != footerHeight))
			{
				float start = (m_animationGoal & BOTTOM_PANEL) ? float(footerHeight) : 0.f;
				float end = (m_animationGoal & BOTTOM_PANEL) ? 0.f : float(footerHeight);
				footerShift = round(lerp(start, end, smoothStep(0.f, duration, elapsed)));
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
	updateLayout();
}