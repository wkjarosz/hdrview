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
#include <spdlog/fmt/fmt.h>
#define NOMINMAX
#include <tinydir.h>
#include <algorithm>

using namespace std;

HDRViewScreen::HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args) :
    Screen(Vector2i(800,600), "HDRView", true),
    m_exposure(0.0f), m_gamma(sRGB ? 2.2f : gamma),
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
    auto sideLayout = new BoxLayout(Orientation::Vertical,
                                    Alignment::Fill, 4, 4);
    m_sidePanelContents->setLayout(sideLayout);

    //
    // create file/layers panel
    //
    {
        auto w = new Button(m_sidePanelContents, "File", ENTYPO_ICON_CHEVRON_DOWN);
        w->setBackgroundColor(Color(15, 100, 185, 75));
        w->setFlags(Button::ToggleButton);
        w->setPushed(true);
        w->setIconPosition(Button::IconPosition::Right);
        m_layersPanel = new LayersPanel(m_sidePanelContents, this);

        w->setChangeCallback([&,w,sideLayout](bool value)
                             {
                                 w->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_RIGHT);
                                 m_layersPanel->setVisible(value);
                                 m_sidePanelContents->performLayout(mNVGContext);
                                 performLayout();
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
                 m_sidePanelContents->performLayout(mNVGContext);
                 performLayout();
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

	    m_editPanel = new EditImagePanel(m_sidePanelContents, this);
        m_editPanel->setVisible(false);
	    m_editPanel->setId("edit panel");

        w->setChangeCallback([&,w](bool value)
         {
             w->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_RIGHT);
             m_editPanel->setVisible(value);
             m_sidePanelContents->performLayout(mNVGContext);
             performLayout();
         });
    }

    dropEvent(args);

    //
    // create top panel controls
    //
    {
        auto about = new Button(m_topPanel, "", ENTYPO_ICON_INFO);
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

        m_helpButton = new Button(m_topPanel, "", ENTYPO_ICON_CIRCLED_HELP);
        m_helpButton->setTooltip("Bring up the help dialog.");
        m_helpButton->setFlags(Button::ToggleButton);
        m_helpButton->setFixedSize(Vector2i(25, 25));
        m_helpButton->setChangeCallback([&](bool value)
        {
            m_helpDialog->setVisible(value);
            if (value)
                m_helpDialog->center();
        });

        m_layersButton = new Button(m_topPanel, "", ENTYPO_ICON_LIST);
        m_layersButton->setTooltip("Bring up the images dialog to load/remove images, and cycle through open images.");
        m_layersButton->setFlags(Button::ToggleButton);
        m_layersButton->setFixedSize(Vector2i(25, 25));
        m_layersButton->setChangeCallback([&](bool value)
        {
            m_sidePanel->setVisible(value);
            performLayout();
        });

        new Label(m_topPanel, "EV", "sans-bold");
        m_exposureSlider = new Slider(m_topPanel);
        m_exposureTextBox = new FloatBox<float>(m_topPanel, m_exposure);

        m_exposureTextBox->numberFormat("%1.2f");
        m_exposureTextBox->setEditable(true);
        m_exposureTextBox->setFixedWidth(35);
        m_exposureTextBox->setAlignment(TextBox::Alignment::Right);
        auto exposureTextBoxCB = [&](float value)
        {
            changeExposure(value);
        };
        m_exposureTextBox->setCallback(exposureTextBoxCB);
        m_exposureSlider->setCallback([&](float value)
        {
            changeExposure(round(4*value) / 4.0f);
        });
        m_exposureSlider->setFixedWidth(100);
        m_exposureSlider->setRange({-9.0f,9.0f});
        m_exposureTextBox->setValue(m_exposure);
        changeExposure(m_exposure);


        m_sRGB = new CheckBox(m_topPanel, "sRGB   ");

        m_gammaLabel = new Label(m_topPanel, "Gamma", "sans-bold");
        m_gammaSlider = new Slider(m_topPanel);
        m_gammaTextBox = new FloatBox<float>(m_topPanel);

        m_gammaTextBox->setEditable(true);
        m_gammaTextBox->numberFormat("%1.3f");
        m_gammaTextBox->setFixedWidth(40);
        m_gammaTextBox->setAlignment(TextBox::Alignment::Right);
        auto gammaTextBoxCB = [&](float value)
        {
            m_gamma = value;
            m_gammaSlider->setValue(m_gamma);
        };
        m_gammaTextBox->setCallback(gammaTextBoxCB);
        m_gammaSlider->setCallback([&](float value)
        {
            m_gamma = max(m_gammaSlider->range().first, round(10*value) / 10.0f);
            m_gammaTextBox->setValue(m_gamma);
            m_gammaSlider->setValue(m_gamma);
        });
        m_gammaSlider->setFixedWidth(100);
        m_gammaSlider->setRange({0.02f,9.0f});
        m_gammaTextBox->setValue(m_gamma);
        gammaTextBoxCB(m_gamma);

        m_sRGB->setCallback([&](bool value)
        {
            m_gammaSlider->setEnabled(!value);
            m_gammaTextBox->setEnabled(!value);
            m_gammaLabel->setEnabled(!value);
            m_gammaLabel->setColor(value ? mTheme->mDisabledTextColor : mTheme->mTextColor);
            performLayout();
        });

        m_dither = new CheckBox(m_topPanel, "Dither  ");
        m_drawGrid = new CheckBox(m_topPanel, "Grid  ");
        m_drawValues = new CheckBox(m_topPanel, "RGB values  ");
        m_dither->setChecked(dither);
        m_drawGrid->setChecked(true);
        m_drawValues->setChecked(true);
    }

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


    m_ditherer.init();

    m_sRGB->setChecked(sRGB);
    m_sRGB->callback()(sRGB);

    // m_topPanel->requestFocus();

    m_sidePanel->setVisible(false);

    drawAll();
    setVisible(true);
    glfwSwapInterval(1);
}

HDRViewScreen::~HDRViewScreen()
{
    // empty
}


void HDRViewScreen::changeExposure(float newVal)
{
    if (newVal == m_exposure)
        return;

    m_exposure = newVal;
    m_exposureTextBox->setValue(m_exposure);
    m_exposureSlider->setValue(m_exposure);
    if (m_histogramPanel)
        m_histogramPanel->setImage(currentImage()->image());
}

void HDRViewScreen::runFilter(const std::function<ImageCommandUndo*(HDRImage & img)> & command)
{
	if (!currentImage())
		return;

	currentImage()->modify(command);

	updateCaption();
	repopulateLayerList();
	setSelectedLayer(m_current);
}


void HDRViewScreen::saveImage()
{
	if (!currentImage())
		return;

	string file = file_dialog(
		{
			{"png", "Portable Network Graphic"},
			{"pfm", "Portable Float Map"},
			{"ppm", "Portable PixMap"},
			{"tga", "Targa image"},
			{"bmp", "Windows Bitmap image"},
			{"hdr", "Radiance rgbE format"},
			{"exr", "OpenEXR image"}
		}, true);

	if (file.size())
	{
		try
		{
			currentImage()->save(file, powf(2.0f, m_exposure),
			                     m_gamma, m_sRGB->checked(),
			                     m_dither->checked());
		}
		catch (std::runtime_error &e)
		{
			new MessageDialog(this, MessageDialog::Type::Warning, "Error",
			                  string("Could not save image due to an error:\n") + e.what());
		}
		updateCaption();
		repopulateLayerList();
		setSelectedLayer(m_current);
	}
}

GLImage * HDRViewScreen::currentImage()
{
    if (m_current < 0 || m_current >= int(m_images.size()))
        return nullptr;
    return m_images[m_current];
}

const GLImage * HDRViewScreen::currentImage() const
{
    if (m_current < 0 || m_current >= int(m_images.size()))
        return nullptr;
    return m_images[m_current];
}


void HDRViewScreen::closeImage(int index)
{
    GLImage * img = (index < 0 || index >= int(m_images.size())) ? nullptr : m_images[index];

    if (img)
    {
        auto closeIt = [&,index](int close = 0)
        {
            if (close != 0)
                return;

            delete m_images[index];
            m_images.erase(m_images.begin()+index);
            repopulateLayerList();
            if (index < m_current)
                setSelectedLayer(m_current-1);
            else
                setSelectedLayer(m_current >= int(m_images.size()) ? int(m_images.size()-1) : m_current);
            enableDisableButtons();
        };

        if (img->isModified())
        {
            auto dialog = new MessageDialog(this, MessageDialog::Type::Warning, "Warning!",
                                            "Image is modified. Close anyway?", "Close", "Cancel", true);
            dialog->setCallback(closeIt);
        }
        else
            closeIt();
    }
}

void HDRViewScreen::closeCurrentImage()
{
    closeImage(m_current);
}


void HDRViewScreen::updateCaption()
{
    const GLImage * img = currentImage();
    if (img)
        setCaption(string("HDRView [") + img->filename() + (img->isModified() ? "*" : "") + "]");
    else
        setCaption(string("HDRView"));
}

void HDRViewScreen::setSelectedLayer(int index)
{
    if (m_images.empty() || index < 0 || index >= int(m_images.size()))
    {
        m_current = -1;
        updateCaption();
        return;
    }

    m_layersPanel->selectLayer(index);
    m_current = index;
    updateCaption();
    enableDisableButtons();

	// update histogram
	m_histogramPanel->setImage(currentImage()->image());
}


void HDRViewScreen::sendLayerBackward()
{
    if (m_images.empty() || m_current == 0)
        // do nothing
        return;

    std::swap(m_images[m_current], m_images[m_current-1]);
    repopulateLayerList();
    m_current--;
    setSelectedLayer(m_current);
}


void HDRViewScreen::bringLayerForeward()
{
    if (m_images.empty() || m_current == int(m_images.size()-1))
        // do nothing
        return;

    std::swap(m_images[m_current], m_images[m_current+1]);
    repopulateLayerList();
    m_current++;
    setSelectedLayer(m_current);
}

bool HDRViewScreen::undo()
{
    if (!currentImage())
        return false;

    currentImage()->undo();
    updateCaption();
    repopulateLayerList();
    setSelectedLayer(m_current);
    return true;
}

bool HDRViewScreen::redo()
{
    if (!currentImage())
        return false;

    currentImage()->redo();
    updateCaption();
    repopulateLayerList();
    setSelectedLayer(m_current);
    return true;
}

void HDRViewScreen::repopulateLayerList()
{
    m_layersPanel->repopulateLayerList();
}

void HDRViewScreen::enableDisableButtons()
{
    m_layersPanel->enableDisableButtons();
	m_editPanel->enableDisableButtons();
}

bool HDRViewScreen::dropEvent(const vector<string> &filenames)
{
    size_t numErrors = 0;
    vector<pair<string, bool> > loadedOK;
    for (auto i : filenames)
    {
        tinydir_dir dir;
    	if (tinydir_open(&dir, i.c_str()) != -1)
    	{
            try
            {
                // filename is actually a directory, traverse it
                console->info("Loading images in \"{}\"...", dir.path);
            	while (dir.has_next)
            	{
            		tinydir_file file;
            		if (tinydir_readfile(&dir, &file) == -1)
                        throw std::runtime_error("Error getting file");

                    if (!file.is_reg)
                    {
                        if (tinydir_next(&dir) == -1)
                            throw std::runtime_error("Error getting next file");
                        continue;
                    }

                    // only consider image files we support
                    string ext = file.extension;
                    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext != "exr" && ext != "png" && ext != "jpg" &&
                        ext != "jpeg" && ext != "hdr" && ext != "pic" &&
                        ext != "pfm" && ext != "ppm" && ext != "bmp" &&
                        ext != "tga" && ext != "psd")
                    {
                        if (tinydir_next(&dir) == -1)
                            throw std::runtime_error("Error getting next file");
                        continue;
                    }

                    GLImage* image = new GLImage();
                    if (image->load(file.path))
                    {
                        loadedOK.push_back({file.path, true});
                        image->init();
                        m_images.push_back(image);
                        console->info("Loaded \"{}\" [{:d}x{:d}]", file.name,
                               image->width(), image->height());
                    }
                    else
                    {
                        loadedOK.push_back({file.name, false});
                        numErrors++;
                        delete image;
                    }

            		if (tinydir_next(&dir) == -1)
                        throw std::runtime_error("Error getting next file");
            	}

                tinydir_close(&dir);
            }
            catch (const exception & e)
            {
                console->error("Error listing directory: ({}).", e.what());
            }
        }
        else
        {
            GLImage* image = new GLImage();
            if (image->load(i))
            {
                loadedOK.push_back({i, true});
                image->init();
                m_images.push_back(image);
                console->info("Loaded \"{}\" [{:d}x{:d}]", i, image->width(), image->height());
            }
            else
            {
                loadedOK.push_back({i, false});
                numErrors++;
                delete image;
            }
        }
        tinydir_close(&dir);
    }

    enableDisableButtons();
    repopulateLayerList();
    setSelectedLayer(int(m_images.size()-1));

    if (numErrors)
    {
        string badFiles;
        for (size_t i = 0; i < loadedOK.size(); ++i)
        {
            if (!loadedOK[i].second)
                badFiles += loadedOK[i].first + "\n";
        }
        new MessageDialog(this, MessageDialog::Type::Warning, "Error",
                          "Could not load:\n " + badFiles);
        return numErrors == filenames.size();
    }

    return true;
}


void HDRViewScreen::flipImage(bool h)
{
    if (h)
        runFilter([](HDRImage & img)
            {
                img = img.flippedHorizontal();
                return new LambdaUndo([](HDRImage & img2){img2 = img2.flippedHorizontal();});
            });
    else
        runFilter([](HDRImage & img)
           {
               img = img.flippedVertical();
               return new LambdaUndo([](HDRImage & img2){img2 = img2.flippedVertical();});
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
                    return redo();
                else
                    return undo();
            }
            return false;

        case GLFW_KEY_BACKSPACE:
            closeCurrentImage();
            return true;
        case 'W':
            if (modifiers & GLFW_MOD_SUPER)
            {
                closeCurrentImage();
                return true;
            }
            return false;

        case '=':
        case GLFW_KEY_KP_ADD:
            if (m_zoom < 20) m_zoom++;
            m_zoomf = powf(2.0f, m_zoom/2.0f);
            updateZoomLabel();
            return true;

        case '-':
        case GLFW_KEY_KP_SUBTRACT:
            if (m_zoom > -20) m_zoom--;
            m_zoomf = powf(2.0f, m_zoom/2.0f);
            updateZoomLabel();
            return true;
        case '[':
            if (modifiers & GLFW_MOD_SUPER)
            {
                sendLayerBackward();
                return true;
            }
        case ']':
            if (modifiers & GLFW_MOD_SUPER)
            {
                bringLayerForeward();
                return true;
            }
        case 'G':
            if (modifiers & GLFW_MOD_SHIFT)
                m_gamma += 0.02f;
            else
            {
                m_gamma -= 0.02f;
                if (m_gamma <= 0.0f)
                    m_gamma = 0.02f;
            }
            m_gammaSlider->setValue(m_gamma);
            m_gammaTextBox->setValue(m_gamma);
            return true;

        case 'E':
            if (modifiers & GLFW_MOD_SHIFT)
                changeExposure(m_exposure + 0.25f);
            else
                changeExposure(m_exposure - 0.25f);
            return true;

        case 'D':
            m_dither->setChecked(!m_dither->checked());
            return true;

        case 'F':
            flipImage(false);
            return true;


        case 'M':
            flipImage(true);
            return true;

        case ' ':
            m_imagePan = Vector2f::Zero();
            drawAll();
            return true;

        case GLFW_KEY_HOME:
            m_imagePan = Vector2f::Zero();
            m_zoom = 0;
            m_zoomf = 1.0f;
            updateZoomLabel();
            drawAll();
            return true;

        case 'T':
            m_topPanel->setVisible(!m_topPanel->visible());
            return true;

        case 'H':
            m_helpDialog->setVisible(!m_helpDialog->visible());
            m_helpDialog->center();
            m_helpButton->setPushed(m_helpDialog->visible());
            return true;

        case 'L':
            m_sidePanel->setVisible(!m_sidePanel->visible());
            m_layersButton->setPushed(m_sidePanel->visible());
            return true;

        case GLFW_KEY_PAGE_DOWN:
        case GLFW_KEY_DOWN:
            if (!m_images.empty())
                setSelectedLayer((m_current+1) % int(m_images.size()));
            break;

        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_UP:
            setSelectedLayer((m_current-1 < 0) ? int(m_images.size()-1) : (m_current-1) % int(m_images.size()));
            break;

        case '1':
            m_channels = Vector3f(1.0f, 0.0f, 0.0f);
            return true;
        case '2':
            m_channels = Vector3f(0.0f, 1.0f, 0.0f);
            return true;
        case '3':
            m_channels = Vector3f(0.0f, 0.0f, 1.0f);
            return true;
        case '4':
            m_channels = Vector3f(1.0f, 1.0f, 1.0f);
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

    // put the status bar full-width at the bottom
    m_statusBar->setSize(Vector2i(width(), (m_statusBar->theme()->mTextBoxFontSize+4)));
    m_statusBar->setPosition(Vector2i(0, height()-m_statusBar->height()));

    int sidePanelHeight = height() - m_topPanel->height() - m_statusBar->height();

    m_sidePanelContents->setFixedWidth(195);
    m_sideScrollPanel->setFixedWidth(195+12);

    // put the side panel directly below the top panel on the left side
    m_sidePanel->setPosition(Vector2i(0, topPanelHeight));
    m_sidePanel->setSize(Vector2i(195+12, sidePanelHeight));


    int zoomWidth = m_zoomLabel->preferredSize(mNVGContext).x();
    m_zoomLabel->setWidth(zoomWidth);
    m_zoomLabel->setPosition(Vector2i(width()-zoomWidth-6, 0));

    int lheight2 = std::min(sidePanelHeight, m_sidePanelContents->preferredSize(mNVGContext).y());
    m_sideScrollPanel->setFixedHeight(lheight2);
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
        m_imagePan += (8 * rel) / m_zoomf;
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
        m_imagePan += rel.cast<float>() / m_zoomf;
        return true;
    }

    if (Screen::mouseMotionEvent(p, rel, button, modifiers))
        return true;

    Vector2i pixel = screenToImage(p);
    const GLImage * img = currentImage();
    if (img && (pixel.array() >= 0).all() && (pixel.array() < img->size().array()).all())
    {
        Color4 pixelVal = img->image()(pixel.x(), pixel.y());
        Color4 iPixelVal = (pixelVal * powf(2.0f, m_exposure) * 255).min(255.0f).max(0.0f);
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
    float realZoom = m_zoomf * pixelRatio();
    int ratio1 = (realZoom < 1.0f) ? 1 : (int)round(realZoom);
    int ratio2 = (realZoom < 1.0f) ? (int)round(1.0f/realZoom) : 1;
    string cap = fmt::format("{:7.3f}% ({:d} : {:d})", realZoom * 100, ratio1, ratio2);
    m_zoomLabel->setCaption(cap);
    performLayout();
}

void HDRViewScreen::drawContents()
{
    performLayout();
    const GLImage * img = currentImage();
    if (img)
    {

        Matrix4f trans;
        trans.setIdentity();
        trans.rightCols<1>() = Vector4f( 2 * m_imagePan[0]/size().x(),
                                        -2 * m_imagePan[1]/size().y(),
                                         0.0f, 1.0f);
        Matrix4f scale;
        scale.setIdentity();
        scale(0,0) = m_zoomf;
        scale(1,1) = m_zoomf;

        Matrix4f imageScale;
        imageScale.setIdentity();
        imageScale(0,0) = float(img->size()[0]) / size().x();
        imageScale(1,1) = float(img->size()[1]) / size().y();

        Matrix4f mvp;
        mvp.setIdentity();
        mvp = scale * trans * imageScale;

        m_ditherer.bind();
        img->draw(mvp, powf(2.0f, m_exposure), m_gamma, m_sRGB->checked(), m_dither->checked(), m_channels);

        drawPixelLabels();
        drawPixelGrid(mvp);
    }
}


void HDRViewScreen::drawPixelGrid(const Matrix4f &mvp) const
{
    const GLImage * img = currentImage();
    if (!m_drawGrid->checked() || m_zoomf < 8 || !img)
        return;

    Vector2i xy0 = topLeftImageCorner2Screen();
    int minJ = max(0, int(-xy0.y() / m_zoomf));
    int maxJ = min(img->height(), int(ceil((size().y() - xy0.y())/m_zoomf)));
    int minI = max(0, int(-xy0.x() / m_zoomf));
    int maxI = min(img->width(), int(ceil((size().x() - xy0.x())/m_zoomf)));

    nvgBeginPath(mNVGContext);

    // draw vertical lines
    for (int i = minI; i <= maxI; ++i)
    {
        Vector2i sxy0 = imageToScreen(Vector2i(i,minJ));
        Vector2i sxy1 = imageToScreen(Vector2i(i,maxJ));
        nvgMoveTo(mNVGContext, sxy0.x(), sxy0.y());
        nvgLineTo(mNVGContext, sxy1.x(), sxy1.y());
    }

    // draw horizontal lines
    for (int j = minJ; j <= maxJ; ++j)
    {
        Vector2i sxy0 = imageToScreen(Vector2i(minI, j));
        Vector2i sxy1 = imageToScreen(Vector2i(maxI, j));
        nvgMoveTo(mNVGContext, sxy0.x(), sxy0.y());
        nvgLineTo(mNVGContext, sxy1.x(), sxy1.y());
    }

    nvgStrokeWidth(mNVGContext, 2.0f);
    nvgStrokeColor(mNVGContext, Color(1.0f, 1.0f, 1.0f, 0.2f));
    nvgStroke(mNVGContext);
}

void
HDRViewScreen::drawPixelLabels() const
{
    const GLImage * img = currentImage();
    // if pixels are big enough, draw color labels on each visible pixel
    if (!m_drawValues->checked() || m_zoomf < 32 || !img)
        return;

    Vector2i xy0 = topLeftImageCorner2Screen();
    int minJ = max(0, int(-xy0.y() / m_zoomf));
    int maxJ = min(img->height()-1, int(ceil((size().y() - xy0.y())/m_zoomf)));
    int minI = max(0, int(-xy0.x() / m_zoomf));
    int maxI = min(img->width()-1, int(ceil((size().x() - xy0.x())/m_zoomf)));
    for (int j = minJ; j <= maxJ; ++j)
    {
        for (int i = minI; i <= maxI; ++i)
        {
            Color4 pixel = img->image()(i, j);

            float luminance = pixel.luminance() * pow(2.0f, m_exposure);

            string text = fmt::format("{:1.3f}\n{:1.3f}\n{:1.3f}", pixel[0], pixel[1], pixel[2]);

            drawText(imageToScreen(Vector2i(i,j)), text,
                     luminance > 0.5f ? Color(0.0f, 0.0f, 0.0f, 0.5f) : Color(1.0f, 1.0f, 1.0f, 0.5f),
                     int(m_zoomf/32.0f * 10), int(m_zoomf));
        }
    }
}


void
HDRViewScreen::drawText(const Vector2i & pos,
                              const string & text,
                              const Color & color,
                              int fontSize,
                              int fixedWidth) const
{
    nvgFontFace(mNVGContext, "sans");
    nvgFontSize(mNVGContext, (float) fontSize);
    nvgFillColor(mNVGContext, color);
    if (fixedWidth > 0)
    {
        nvgTextAlign(mNVGContext, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgTextBox(mNVGContext, (float) pos.x(), (float) pos.y(), (float) fixedWidth, text.c_str(), nullptr);
    }
    else
    {
        nvgTextAlign(mNVGContext, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgText(mNVGContext, (float) pos.x(), (float) pos.y() + fontSize, text.c_str(), nullptr);
    }
}

Vector2i HDRViewScreen::topLeftImageCorner2Screen() const
{
    const GLImage * img = currentImage();
    if (!img)
        return Vector2i(0,0);

    return Vector2i(int(m_imagePan[0] * m_zoomf) + int(-img->size()[0] / 2.0 * m_zoomf) + int(size().x() / 2.0f),
                    int(m_imagePan[1] * m_zoomf) + int(-img->size()[1] / 2.0 * m_zoomf) + int(size().y() / 2.0f));
}

Vector2i HDRViewScreen::imageToScreen(const Vector2i & pixel) const
{
    const GLImage * img = currentImage();
    if (!img)
        return Vector2i(0,0);

    Vector2i sxy(pixel.x() * m_zoomf, pixel.y() * m_zoomf);
    sxy += topLeftImageCorner2Screen();
    return sxy;
}

Vector2i HDRViewScreen::screenToImage(const Vector2i & p) const
{
    const GLImage * img = currentImage();
    if (!img)
        return Vector2i(0,0);

    Vector2i xy0 = topLeftImageCorner2Screen();

    Vector2i xy(int(floor((p[0] - xy0.x()) / m_zoomf)),
                int(floor((p[1] - xy0.y()) / m_zoomf)));
    if (false) xy[0] = img->width() - 1 - xy[0];
    if (false) xy[1] = img->height() - 1 - xy[1];

    return xy;
}
