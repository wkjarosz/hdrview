/*! \file hdrviewer.cpp
    \author Wojciech Jarosz
*/
#include "hdrviewer.h"
#include "histogrampanel.h"
#include "glimage.h"
#include "editimagepanel.h"
#include "layerspanel.h"
#include <iostream>
#include "common.h"
#include "commandhistory.h"
#include "hdrimageviewer.h"
#include "hdrimagemanager.h"
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

	m_verticalScreenSplit = new Widget(this);
	m_verticalScreenSplit->setLayout(new BoxLayout{Orientation::Vertical, Alignment::Fill});

	m_topPanel = new Widget(m_verticalScreenSplit);
	m_topPanel->setFixedHeight(30);
	m_topPanel->setLayout(new BoxLayout(Orientation::Horizontal,
	                                    Alignment::Middle, 5, 5));

	m_horizontalScreenSplit = new Widget(m_verticalScreenSplit);
	m_horizontalScreenSplit->setLayout(new BoxLayout{Orientation::Horizontal, Alignment::Fill});

	m_sidePanel = new Widget(m_horizontalScreenSplit);
	m_sidePanel->setFixedWidth(207);
	m_sidePanel->setVisible(false);

	m_imageView = new HDRImageViewer(m_horizontalScreenSplit, this);
	m_imageView->setGridThreshold(20);
	m_imageView->setPixelInfoThreshold(20);

	m_statusBar = new Widget(m_verticalScreenSplit);
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
    // create side panel
    //

    m_sideScrollPanel = new VScrollPanel(m_sidePanel);
    m_sidePanelContents = new Widget(m_sideScrollPanel);
    m_sidePanelContents->setLayout(new BoxLayout(Orientation::Vertical,
                                                 Alignment::Fill, 4, 4));
	m_sidePanelContents->setFixedWidth(195);
    m_sideScrollPanel->setFixedWidth(195+12);

    //
    // create file/layers panel
    //
    {
        auto w = new Button(m_sidePanelContents, "File", ENTYPO_ICON_CHEVRON_DOWN);
        w->setBackgroundColor(Color(15, 100, 185, 75));
        w->setFlags(Button::ToggleButton);
        w->setPushed(true);
        w->setIconPosition(Button::IconPosition::Right);
        m_layersPanel = new LayersPanel(m_sidePanelContents, this, m_imageMgr);

        w->setChangeCallback([&,w](bool value)
                             {
                                 w->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_RIGHT);
                                 m_layersPanel->setVisible(value);
	                             updateLayout();
	                             m_sidePanelContents->performLayout(mNVGContext);
                             });
    }

    //
    // create histogram panel
    //
    {
        auto w = new Button(m_sidePanelContents, "Histogram", ENTYPO_ICON_CHEVRON_RIGHT);
        w->setBackgroundColor(Color(15, 100, 185, 75));
        w->setFlags(Button::ToggleButton);
        w->setIconPosition(Button::IconPosition::Right);

        auto w2 = new Widget(m_sidePanelContents);
        w2->setVisible(false);
        w2->setId("histogram panel");
        w2->setLayout(new GroupLayout(2, 4, 8, 10));

        new Label(w2, "Histogram", "sans-bold");
        m_histogramPanel = new HistogramPanel(w2);

        w->setChangeCallback([&,w,w2](bool value)
             {
                 w->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_RIGHT);
                 w2->setVisible(value);
	             updateLayout();
	             m_sidePanelContents->performLayout(mNVGContext);
             });
    }

    //
    // create edit panel
    //
    {
        auto w = new Button(m_sidePanelContents, "Edit", ENTYPO_ICON_CHEVRON_RIGHT);
        w->setBackgroundColor(Color(15, 100, 185, 75));
        w->setFlags(Button::ToggleButton);
        w->setIconPosition(Button::IconPosition::Right);

	    m_editPanel = new EditImagePanel(m_sidePanelContents, this, m_imageMgr);
        m_editPanel->setVisible(false);
	    m_editPanel->setId("edit panel");

        w->setChangeCallback([&,w](bool value)
         {
             w->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_RIGHT);
             m_editPanel->setVisible(value);
	         updateLayout();
	         m_sidePanelContents->performLayout(mNVGContext);
         });
    }

    //
    // create top panel controls
    //
    {
        auto about = new Button(m_topPanel, "", ENTYPO_ICON_INFO);
	    m_helpButton = new Button(m_topPanel, "", ENTYPO_ICON_CIRCLED_HELP);
	    m_layersButton = new Button(m_topPanel, "", ENTYPO_ICON_LIST);
	    new Label(m_topPanel, "EV", "sans-bold");
	    auto exposureSlider = new Slider(m_topPanel);
	    auto exposureTextBox = new FloatBox<float>(m_topPanel, exposure);

	    auto sRGB = new CheckBox(m_topPanel, "sRGB   ");
	    auto gammaLabel = new Label(m_topPanel, "Gamma", "sans-bold");
	    auto gammaSlider = new Slider(m_topPanel);
	    auto gammaTextBox = new FloatBox<float>(m_topPanel);

        about->setFixedSize(Vector2i(25, 25));
        about->setCallback([&]() {
            auto dlg = new MessageDialog(
                this, MessageDialog::Type::Information, "About HDRView",
                "Copyright (c) Wojciech Jarosz\n\n"
                "HDRView is a simple research-oriented tool for examining, "
                "comparing, and converting high-dynamic range images.\n\n"
                "HDRView is freely available under a 3-clause BSD license.");
	        updateLayout();
            dlg->center();
        });

        m_helpButton->setTooltip("Bring up the help dialog.");
        m_helpButton->setFlags(Button::ToggleButton);
        m_helpButton->setFixedSize(Vector2i(25, 25));
        m_helpButton->setChangeCallback([&](bool value)
        {
            m_helpDialog->setVisible(value);
            if (value)
                m_helpDialog->center();
        });

        m_layersButton->setTooltip("Bring up the images dialog to load/remove images, and cycle through open images.");
        m_layersButton->setFlags(Button::ToggleButton);
        m_layersButton->setFixedSize(Vector2i(25, 25));
        m_layersButton->setChangeCallback([&](bool value)
        {
            m_sidePanel->setVisible(value);
	        updateLayout();
        });



        exposureTextBox->numberFormat("%1.2f");
        exposureTextBox->setEditable(true);
        exposureTextBox->setFixedWidth(35);
        exposureTextBox->setAlignment(TextBox::Alignment::Right);
        exposureTextBox->setCallback([&](float e)
                                     {
	                                       m_imageView->setExposure(e);
                                     });
        exposureSlider->setCallback([&](float v)
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
        gammaTextBox->setCallback([&,gammaSlider](float value)
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

	    m_imageView->setExposureCallback([exposureTextBox,exposureSlider](float e)
	                                     {
		                                     exposureTextBox->setValue(e);
		                                     exposureSlider->setValue(e);
	                                     });
	    m_imageView->setGammaCallback([gammaTextBox,gammaSlider](float g)
	                                  {
		                                  gammaTextBox->setValue(g);
		                                  gammaSlider->setValue(g);
	                                  });
	    m_imageView->setExposure(exposure);
	    m_imageView->setGamma(gamma);
	    m_imageView
		    ->setPixelHoverCallback([&](const Vector2i &pixelCoord, const Color4 &pixelVal, const Color4 &iPixelVal)
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

	    m_imageView->setZoomCallback([&](float zoom)
                                    {
	                                    float realZoom = zoom * pixelRatio();
	                                    int ratio1 = (realZoom < 1.0f) ? 1 : (int)round(realZoom);
	                                    int ratio2 = (realZoom < 1.0f) ? (int)round(1.0f/realZoom) : 1;
	                                    m_zoomLabel->setCaption(fmt::format("{:7.3f}% ({:d} : {:d})", realZoom * 100, ratio1, ratio2));
	                                    updateLayout();
                                    });



	    m_imageMgr->setLayerSelectedCallback([&](int i)
	                                     {
		                                     m_imageView->bindImage(m_imageMgr->currentImage());
		                                     updateCaption();
		                                     m_layersPanel->enableDisableButtons();
		                                     m_editPanel->enableDisableButtons();
		                                     m_layersPanel->selectLayer(i);
		                                     m_histogramPanel->setImage(m_imageMgr->currentImage());
	                                     });

	    m_imageMgr->setNumLayersCallback([&](void)
	                                      {
		                                      updateCaption();
		                                      m_layersPanel->enableDisableButtons();
		                                      m_editPanel->enableDisableButtons();
		                                      m_layersPanel->repopulateLayerList();
		                                      m_layersPanel->selectLayer(m_imageMgr->currentImageIndex());
	                                      });
	    m_imageMgr->setImageChangedCallback([&](int i)
	                                         {
		                                         updateCaption();
		                                         m_layersPanel->enableDisableButtons();
		                                         m_editPanel->enableDisableButtons();
		                                         m_layersPanel->repopulateLayerList();
		                                         m_layersPanel->selectLayer(i);
	                                         });


	    sRGB->setCallback([&,gammaSlider,gammaTextBox,gammaLabel](bool value)
        {
	        m_imageView->setSRGB(value);
            gammaSlider->setEnabled(!value);
            gammaTextBox->setEnabled(!value);
            gammaLabel->setEnabled(!value);
            gammaLabel->setColor(value ? mTheme->mDisabledTextColor : mTheme->mTextColor);
	        updateLayout();
        });

	    sRGB->setChecked(sRGB);
	    sRGB->callback()(sRGB);

	    (new CheckBox(m_topPanel, "Dither  ",
                     [&](bool v) { m_imageView->setDithering(v); }))->setChecked(m_imageView->ditheringOn());
	    (new CheckBox(m_topPanel, "Grid  ",
                     [&](bool v) { m_imageView->setDrawGrid(v); }))->setChecked(m_imageView->drawGridOn());
	    (new CheckBox(m_topPanel, "RGB values  ",
                     [&](bool v) { m_imageView->setDrawValues(v); }))->setChecked(m_imageView->drawValuesOn());
    }

	dropEvent(args);

    //
    // create help dialog
    //
    {
        vector<pair<string, string> > helpStrings =
        {
            {"h",           "Toggle this help panel"},
            {"l",           "Toggle the layer panel"},
            {"t",           "Toggle the toolbar"},
            {"-/+",         "Zoom out/in"},
            {"[SCROLL]",    "Pan the image"},
            {"[SPACE]",     "Re-center view"},
            {"[HOME]",      "Re-center and reset zoom"},
            {"g/G",         "Decrease/Increase gamma"},
            {"e/E",         "Decrease/Increase exposure"},
            {"d",           "Toggle dither"},
            {"f",           "Flip image about horizontal axis"},
            {"m",           "Mirror image about vertical axis"},
            {"1/2/3/4",     "View the R/G/B/RGB channels"},
            {"[DELETE]",    "Close current image"},
            {"[UP/PG_UP]",  "Previous image"},
            {"[DN/PG_DN]",  "Next image"},
            {"CMD-[",       "Send layer backward"},
            {"CMD-]",       "Bring layer foreward"}
        };

        m_helpDialog = new Window(this, "Help");
        m_helpDialog->setId("help dialog");
        m_helpDialog->setVisible(false);
        GridLayout *layout = new GridLayout(Orientation::Horizontal, 2,
                                            Alignment::Middle,
                                            15,
                                            5);
        layout->setColAlignment({ Alignment::Maximum, Alignment::Fill });
        layout->setSpacing(0, 10);
        m_helpDialog->setLayout(layout);

        new Label(m_helpDialog, "key", "sans-bold");
        new Label(m_helpDialog, "Action", "sans-bold");

        for (auto item : helpStrings)
        {
            new Label(m_helpDialog, item.first);
            new Label(m_helpDialog, item.second);
        }

        m_helpDialog->center();

        Button *button = new Button(m_helpDialog->buttonPanel(), "", ENTYPO_ICON_CROSS);
        button->setCallback([&]
            {
                m_helpDialog->setVisible(false);
                m_helpButton->setPushed(false);
            });
    }

	setResizeCallback([&](Vector2i)
	                  {
		                  if (m_helpDialog->visible())
			                  m_helpDialog->center();
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

    // disable events if there is a modal dialog
    if (mFocusPath.size() > 1)
    {
        const Window *window = dynamic_cast<Window *>(mFocusPath[mFocusPath.size() - 2]);
        if (window && window->modal())
        {
            if (!window->contains(mMousePos))
                return false;
        }
    }

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

        // undo & redo
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

        case '=':
        case GLFW_KEY_KP_ADD:
		    m_imageView->zoomIn();
            return true;

        case '-':
        case GLFW_KEY_KP_SUBTRACT:
		    m_imageView->zoomOut();
            return true;
        case '[':
            if (modifiers & GLFW_MOD_SUPER)
            {
	            m_imageMgr->sendLayerBackward();
                return true;
            }
        case ']':
            if (modifiers & GLFW_MOD_SUPER)
            {
	            m_imageMgr->bringLayerForward();
                return true;
            }
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

        case GLFW_KEY_HOME:
	        m_imageView->center();
		    m_imageView->fit();
            drawAll();
            return true;

        case 'T':
            m_topPanel->setVisible(!m_topPanel->visible());
		    updateLayout();
            return true;

        case 'H':
            m_helpDialog->setVisible(!m_helpDialog->visible());
            m_helpDialog->center();
            m_helpButton->setPushed(m_helpDialog->visible());
            return true;

        case 'L':
            m_sidePanel->setVisible(!m_sidePanel->visible());
            m_layersButton->setPushed(m_sidePanel->visible());
		    updateLayout();
            return true;

        case GLFW_KEY_PAGE_DOWN:
        case GLFW_KEY_DOWN:
            if (m_imageMgr->numImages())
	            m_imageMgr->selectLayer(mod(m_imageMgr->currentImageIndex()+1, m_imageMgr->numImages()));
            break;

        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_UP:
	        m_imageMgr->selectLayer(mod(m_imageMgr->currentImageIndex()-1, m_imageMgr->numImages()));
            break;

        case '1':
	        m_imageView->setChannel(Vector3f(1.0f, 0.0f, 0.0f));
            return true;
        case '2':
	        m_imageView->setChannel(Vector3f(0.0f, 1.0f, 0.0f));
            return true;
        case '3':
	        m_imageView->setChannel(Vector3f(0.0f, 0.0f, 1.0f));
            return true;
        case '4':
	        m_imageView->setChannel(Vector3f(1.0f, 1.0f, 1.0f));
            return true;
    }
    return false;
}


void HDRViewScreen::updateLayout()
{
	int headerHeight = m_topPanel->visible() ? m_topPanel->fixedHeight() : 0;
	int sidePanelWidth = m_sidePanel->visible() ? m_sidePanel->fixedWidth() : 0;
	int footerHeight = m_statusBar->visible() ? m_statusBar->fixedHeight() : 0;

	int middleHeight = height() - headerHeight - footerHeight;

	m_imageView->setFixedSize(mSize - Vector2i(sidePanelWidth, headerHeight+footerHeight));
	m_sidePanel->setFixedHeight(middleHeight);

	m_verticalScreenSplit->setFixedSize(mSize);

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