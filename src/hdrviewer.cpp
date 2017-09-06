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
#define NOMINMAX
#include <tinydir.h>

using namespace std;

HDRViewScreen::HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args) :
    Screen(Vector2i(800,600), "HDRView", true),
    console(spdlog::get("console"))
{
    setBackground(Vector3f(0.1f, 0.1f, 0.1f));

    Theme * thm = new Theme(mNVGContext);
    thm->mStandardFontSize     = 16;
    thm->mButtonFontSize       = 15;
    thm->mTextBoxFontSize      = 14;
    setTheme(thm);

    thm = new Theme(mNVGContext);
    thm->mStandardFontSize     = 16;
    thm->mButtonFontSize       = 15;
    thm->mTextBoxFontSize      = 14;
    thm->mButtonCornerRadius   = 2;
    thm->mWindowHeaderHeight   = 0;
    thm->mWindowDropShadowSize = 0;
    thm->mWindowCornerRadius   = 0;
    thm->mWindowFillFocused    = Color(.2f,.2f,.2f,.9f);
    thm->mWindowFillUnfocused  = Color(.2f,.2f,.2f,.9f);

	m_imageView = new HDRImageViewer(this);

    m_topPanel = new Window(this, "");
    m_topPanel->setId("top panel");
    m_topPanel->setTheme(thm);
    m_topPanel->setPosition(Vector2i(0, 0));
    m_topPanel->setLayout(new BoxLayout(Orientation::Horizontal,
                                            Alignment::Middle, 5, 5));

    //
    // create status bar widgets
    //
    m_statusBar = new Window(this, "");
    m_statusBar->setTheme(thm);

    m_pixelInfoLabel = new Label(m_statusBar, "", "sans");
    m_pixelInfoLabel->setFontSize(thm->mTextBoxFontSize);
    m_pixelInfoLabel->setPosition(Vector2i(6, 0));

    m_zoomLabel = new Label(m_statusBar, "100% (1 : 1)", "sans");
    m_zoomLabel->setFontSize(thm->mTextBoxFontSize);

    //
    // create side panel
    //
    m_sidePanel = new Window(this, "");
    m_sidePanel->setId("layers");
    m_sidePanel->setTheme(thm);

    m_sideScrollPanel = new VScrollPanel(m_sidePanel);
    m_sidePanelContents = new Widget(m_sideScrollPanel);
    m_sidePanelContents->setLayout(new BoxLayout(Orientation::Vertical,
                                                 Alignment::Fill, 4, 4));

    //
    // create file/layers panel
    //
    {
        auto w = new Button(m_sidePanelContents, "File", ENTYPO_ICON_CHEVRON_DOWN);
        w->setBackgroundColor(Color(15, 100, 185, 75));
        w->setFlags(Button::ToggleButton);
        w->setPushed(true);
        w->setIconPosition(Button::IconPosition::Right);
        m_layersPanel = new LayersPanel(m_sidePanelContents, this, m_imageView);

        w->setChangeCallback([&,w](bool value)
                             {
                                 w->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_RIGHT);
                                 m_layersPanel->setVisible(value);
                                 performLayout();
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
                 performLayout();
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

	    m_editPanel = new EditImagePanel(m_sidePanelContents, this, m_imageView);
        m_editPanel->setVisible(false);
	    m_editPanel->setId("edit panel");

        w->setChangeCallback([&,w](bool value)
         {
             w->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_RIGHT);
             m_editPanel->setVisible(value);
             performLayout();
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
            performLayout();
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
            performLayout();
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


	    m_imageView->setLayerSelectedCallback([&](int i)
	                                     {
		                                     updateCaption();
		                                     m_layersPanel->enableDisableButtons();
		                                     m_editPanel->enableDisableButtons();
		                                     m_layersPanel->selectLayer(i);
		                                     if (m_imageView->currentImage())
			                                     m_histogramPanel->setImage(&m_imageView->currentImage()->image());
		                                     else
			                                     m_histogramPanel->clear();
	                                     });

	    m_imageView->setNumLayersCallback([&](void)
	                                      {
		                                      updateCaption();
		                                      m_layersPanel->enableDisableButtons();
		                                      m_editPanel->enableDisableButtons();
		                                      m_layersPanel->repopulateLayerList();
		                                      m_layersPanel->selectLayer(m_imageView->currentImageIndex());
	                                      });
	    m_imageView->setImageChangedCallback([&](int i)
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
            performLayout();
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

    updateZoomLabel();


    m_sidePanel->setVisible(false);
	performLayout();

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
    const GLImage * img = m_imageView->currentImage();
    if (img)
        setCaption(string("HDRView [") + img->filename() + (img->isModified() ? "*" : "") + "]");
    else
        setCaption(string("HDRView"));
}

bool HDRViewScreen::dropEvent(const vector<string> & filenames)
{
	return m_imageView->loadImages(filenames);
}


void HDRViewScreen::flipImage(bool h)
{
    if (h)
	    m_imageView->modifyImage([](HDRImage &img)
	                {
		                img = img.flippedHorizontal();
		                return new LambdaUndo([](HDRImage &img2) { img2 = img2.flippedHorizontal(); });
	                });
    else
	    m_imageView->modifyImage([](HDRImage &img)
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
	                m_imageView->redo();
	            else
	                m_imageView->undo();

	            return true;
            }
            return false;

        case GLFW_KEY_BACKSPACE:
	        m_imageView->closeImage(m_imageView->currentImageIndex());
            return true;
        case 'W':
            if (modifiers & GLFW_MOD_SUPER)
            {
	            m_imageView->closeImage(m_imageView->currentImageIndex());
                return true;
            }
            return false;

        case '=':
        case GLFW_KEY_KP_ADD:
            m_imageView->setZoomLevel(m_imageView->zoomLevel()+1);
            updateZoomLabel();
            return true;

        case '-':
        case GLFW_KEY_KP_SUBTRACT:
	        m_imageView->setZoomLevel(m_imageView->zoomLevel()-1);
            updateZoomLabel();
            return true;
        case '[':
            if (modifiers & GLFW_MOD_SUPER)
            {
                m_imageView->sendLayerBackward();
                return true;
            }
        case ']':
            if (modifiers & GLFW_MOD_SUPER)
            {
	            m_imageView->bringLayerForward();
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

        case ' ': m_imageView->setOffset(Vector2f::Zero());
            drawAll();
            return true;

        case GLFW_KEY_HOME: m_imageView->setOffset(Vector2f::Zero());
		    m_imageView->setZoomLevel(0);
            updateZoomLabel();
            drawAll();
            return true;

        case 'T':
            m_topPanel->setVisible(!m_topPanel->visible());
		    performLayout();
            return true;

        case 'H':
            m_helpDialog->setVisible(!m_helpDialog->visible());
            m_helpDialog->center();
            m_helpButton->setPushed(m_helpDialog->visible());
            return true;

        case 'L':
            m_sidePanel->setVisible(!m_sidePanel->visible());
            m_layersButton->setPushed(m_sidePanel->visible());
		    performLayout();
            return true;

        case GLFW_KEY_PAGE_DOWN:
        case GLFW_KEY_DOWN:
            if (m_imageView->numImages())
	            m_imageView->selectLayer(mod(m_imageView->currentImageIndex()+1, m_imageView->numImages()));
            break;

        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_UP:
	        m_imageView->selectLayer(mod(m_imageView->currentImageIndex()-1, m_imageView->numImages()));
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

void HDRViewScreen::performLayout()
{
    Screen::performLayout();

    // make the top panel full-width
    m_topPanel->setPosition(Vector2i(0,0));
    int topPanelHeight = m_topPanel->preferredSize(mNVGContext).y();
    m_topPanel->setSize(Vector2i(width(), topPanelHeight));

	topPanelHeight = m_topPanel->visible() ? topPanelHeight : 0;

    // put the status bar full-width at the bottom
    m_statusBar->setSize(Vector2i(width(), (m_statusBar->theme()->mTextBoxFontSize+4)));
    m_statusBar->setPosition(Vector2i(0, height()-m_statusBar->height()));

    int sidePanelHeight = height() - topPanelHeight - m_statusBar->height();

    m_sidePanelContents->setFixedWidth(195);
    m_sideScrollPanel->setFixedWidth(195+12);

    // put the side panel directly below the top panel on the left side
	int sidePanelWidth = 195+12;
    m_sidePanel->setPosition(Vector2i(0, topPanelHeight));
    m_sidePanel->setSize(Vector2i(sidePanelWidth, sidePanelHeight));

    int zoomWidth = m_zoomLabel->preferredSize(mNVGContext).x();
    m_zoomLabel->setWidth(zoomWidth);
    m_zoomLabel->setPosition(Vector2i(width()-zoomWidth-6, 0));

    int lheight2 = std::min(sidePanelHeight, m_sidePanelContents->preferredSize(mNVGContext).y());
    m_sideScrollPanel->setFixedHeight(lheight2);

	sidePanelWidth = m_sidePanel->visible() ? sidePanelWidth : 0;
	m_imageView->setPosition(Vector2i(sidePanelWidth, topPanelHeight));
	m_imageView->setSize(Vector2i(std::max(0, width()-sidePanelWidth), sidePanelHeight));
}


bool HDRViewScreen::resizeEvent(const Vector2i &)
{
    if (m_helpDialog->visible())
        m_helpDialog->center();
    performLayout();
    drawAll();
    return true;
}

bool HDRViewScreen::scrollEvent(const Vector2i &p, const Vector2f &rel)
{
    if (!Screen::scrollEvent(p, rel))
	    m_imageView->moveOffset((8 * rel).cast<int>());
    return false;
}

bool HDRViewScreen::mouseButtonEvent(const Vector2i &p, int button, bool down, int modifiers)
{
    // only set drag state if the other widgets aren't taking the event and
    // we are over the main canvas
    if (Screen::mouseButtonEvent(p, button, down, modifiers) ||
        (m_sidePanel->visible() && m_sidePanel->contains(p)) ||
        (m_topPanel->visible() && m_topPanel->contains(p)) ||
        (m_statusBar->visible() && m_statusBar->contains(p)))
    {
        m_drag = false;
        return true;
    }

    m_drag = down && button == GLFW_MOUSE_BUTTON_1;

    return true;
}


bool HDRViewScreen::mouseMotionEvent(const Vector2i &p, const Vector2i &rel, int button, int modifiers)
{
    if (m_drag && button & (1 << GLFW_MOUSE_BUTTON_1))
    {
	    m_imageView->moveOffset(rel);
        return true;
    }

    if (Screen::mouseMotionEvent(p, rel, button, modifiers))
        return true;

    Vector2i pixel = m_imageView->screenToImage(p);
    const GLImage * img = m_imageView->currentImage();
    if (img && (pixel.array() >= 0).all() && (pixel.array() < img->size().array()).all())
    {
        Color4 pixelVal = img->image()(pixel.x(), pixel.y());
        Color4 iPixelVal = (pixelVal * powf(2.0f, m_imageView->exposure()) * 255).min(255.0f).max(0.0f);
        string s =
            fmt::format("({: 4d},{: 4d}) = ({: 6.3f}, {: 6.3f}, {: 6.3f}, {: 6.3f}) / ({: 3d}, {: 3d}, {: 3d}, {: 3d})",
                 pixel.x(), pixel.y(), pixelVal[0], pixelVal[1], pixelVal[2], pixelVal[3],
                 (int)round(iPixelVal[0]), (int)round(iPixelVal[1]), (int)round(iPixelVal[2]), (int)round(iPixelVal[3]));
        m_pixelInfoLabel->setCaption(s);
    }
    else
        m_pixelInfoLabel->setCaption("");

    m_statusBar->performLayout(mNVGContext);

    return true;
}

void HDRViewScreen::updateZoomLabel()
{
    float realZoom = m_imageView->zoom() * pixelRatio();
    int ratio1 = (realZoom < 1.0f) ? 1 : (int)round(realZoom);
    int ratio2 = (realZoom < 1.0f) ? (int)round(1.0f/realZoom) : 1;
    string cap = fmt::format("{:7.3f}% ({:d} : {:d})", realZoom * 100, ratio1, ratio2);
    m_zoomLabel->setCaption(cap);
    performLayout();
}

void HDRViewScreen::drawContents()
{
	performLayout();
}