/*! \file hdrviewer.cpp
    \author Wojciech Jarosz
*/
#include "hdrviewer.h"
#include <iostream>
#define NOMINMAX
#include <tinydir.h>
#include <spdlog/fmt/fmt.h>
#include <algorithm>
#include "envmap.h"

using namespace std;


HDRViewScreen::HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args) :
    Screen(Vector2i(800,600), "HDRView", true),
    m_GUIScaleFactor(1),
    m_exposure(exposure), m_gamma(sRGB ? 2.2f : gamma),
    console(spdlog::get("console"))
{
    setBackground(Vector3f(0.1f, 0.1f, 0.1f));
    Theme * scaledTheme = new Theme(mNVGContext);
    scaledTheme->mStandardFontSize                 = 16*m_GUIScaleFactor;
    scaledTheme->mButtonFontSize                   = 15*m_GUIScaleFactor;
    scaledTheme->mTextBoxFontSize                  = 14*m_GUIScaleFactor;
    scaledTheme->mWindowCornerRadius               = 2*m_GUIScaleFactor;
    scaledTheme->mWindowHeaderHeight               = 30*m_GUIScaleFactor;
    scaledTheme->mWindowDropShadowSize             = 10*m_GUIScaleFactor;
    scaledTheme->mButtonCornerRadius               = 2*m_GUIScaleFactor;

    this->setTheme(scaledTheme);

    Theme * thm = new Theme(mNVGContext);
    thm->mStandardFontSize                 = 16*m_GUIScaleFactor;
    thm->mButtonFontSize                   = 15*m_GUIScaleFactor;
    thm->mTextBoxFontSize                  = 14*m_GUIScaleFactor;
    thm->mButtonCornerRadius               = 2*m_GUIScaleFactor;
    thm->mWindowHeaderHeight = 0;
    thm->mWindowDropShadowSize = 0;
    thm->mWindowCornerRadius = 0;
    thm->mWindowFillFocused = Color(.2f,.2f,.2f,.9f);
    thm->mWindowFillUnfocused = Color(.2f,.2f,.2f,.9f);

    m_controlPanel = new Window(this, "");
    m_controlPanel->setId("control panel");
    m_controlPanel->setTheme(thm);
    m_controlPanel->setPosition(Vector2i(0, 0));
    m_controlPanel->setLayout(new BoxLayout(Orientation::Horizontal,
                                            Alignment::Middle,
                                            5*m_GUIScaleFactor,
                                            5*m_GUIScaleFactor));

    //
    // create status bar widgets
    //
    m_statusBar = new Window(this, "");
    m_statusBar->setTheme(thm);

    m_pixelInfoLabel = new Label(m_statusBar, "", "sans");
    m_pixelInfoLabel->setFontSize(thm->mTextBoxFontSize*m_GUIScaleFactor);
    m_pixelInfoLabel->setPosition(Vector2i(6, 0)*m_GUIScaleFactor);

    m_zoomLabel = new Label(m_statusBar, "100% (1 : 1)", "sans");
    m_zoomLabel->setFontSize(thm->mTextBoxFontSize*m_GUIScaleFactor);

    //
    // create side panel
    //
    m_sidePanel = new Window(this, "");
    m_sidePanel->setId("layers");
    m_sidePanel->setTheme(thm);

    m_sideScrollPanel = new VScrollPanel(m_sidePanel);
    m_sidePanelContents = new Widget(m_sideScrollPanel);
    auto sideLayout = new BoxLayout(Orientation::Vertical,
                                    Alignment::Fill,
                                    4*m_GUIScaleFactor, 4*m_GUIScaleFactor);
    m_sidePanelContents->setLayout(sideLayout);

    //
    // create side panel
    //
    {
        auto w = new Button(m_sidePanelContents, "File", ENTYPO_ICON_CHEVRON_DOWN);
        w->setBackgroundColor(Color(15, 100, 185, 75));
        w->setFlags(Button::ToggleButton);
        w->setPushed(true);
        w->setIconPosition(Button::IconPosition::Right);
        m_layersPanelContents = new Widget(m_sidePanelContents);

        w->setChangeCallback([&,w,sideLayout](bool value)
                             {
                                 w->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_RIGHT);
                                 m_layersPanelContents->setVisible(value);
                                 m_sidePanelContents->performLayout(mNVGContext);
                                 performLayout();
                             });

        m_layersPanelContents->setId("layers panel");
        m_layersPanelContents->setLayout(new GroupLayout(2 * m_GUIScaleFactor,
                                                         4 * m_GUIScaleFactor,
                                                         8 * m_GUIScaleFactor,
                                                         10 * m_GUIScaleFactor));
        {
            new Label(m_layersPanelContents, "File operations", "sans-bold");

            Widget *buttonRow = new Widget(m_layersPanelContents);
            buttonRow->setLayout(new BoxLayout(Orientation::Horizontal,
                                               Alignment::Fill, 0, 4));

            Button *b = new Button(buttonRow, "Open", ENTYPO_ICON_FOLDER);
            b->setFixedWidth(84);
            b->setBackgroundColor(Color(0, 100, 0, 75));
            b->setTooltip("Load an image and add it to the set of opened images.");
            b->setCallback([&] {
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
                    dropEvent({file});
            });

            m_saveButton = new Button(buttonRow, "Save", ENTYPO_ICON_SAVE);
            m_saveButton->setEnabled(m_images.size() > 0);
            m_saveButton->setFixedWidth(84);
            m_saveButton->setBackgroundColor(Color(0, 0, 100, 75));
            m_saveButton->setTooltip("Save the image to disk.");
            m_saveButton->setCallback([&]
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
            });

            new Label(m_layersPanelContents, "Opened images:", "sans-bold");
        }
    }

    //
    // create edit panel
    //
    {
        auto w = new Button(m_sidePanelContents, "Edit", ENTYPO_ICON_CHEVRON_RIGHT);
        w->setBackgroundColor(Color(15, 100, 185, 75));
        w->setFlags(Button::ToggleButton);
        w->setIconPosition(Button::IconPosition::Right);

        m_editPanelContents = new Widget(m_sidePanelContents);
        m_editPanelContents->setVisible(false);

        w->setChangeCallback([&,w](bool value)
            {
                w->setIcon(value ? ENTYPO_ICON_CHEVRON_DOWN : ENTYPO_ICON_CHEVRON_RIGHT);
                m_editPanelContents->setVisible(value);
                m_sidePanelContents->performLayout(mNVGContext);
                performLayout();
            });

        m_editPanelContents->setId("edit panel");
        m_editPanelContents->setLayout(new GroupLayout(2 * m_GUIScaleFactor,
                                                 4 * m_GUIScaleFactor,
                                                 8 * m_GUIScaleFactor,
                                                 10 * m_GUIScaleFactor));

        new Label(m_editPanelContents, "History", "sans-bold");

        auto buttonRow = new Widget(m_editPanelContents);
        buttonRow->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 4));

        m_undoButton = new Button(buttonRow, "Undo", ENTYPO_ICON_REPLY);
        m_undoButton->setFixedWidth(84);
        m_undoButton->setCallback([&](){undo();});
        m_redoButton = new Button(buttonRow, "Redo", ENTYPO_ICON_FORWARD);
        m_redoButton->setFixedWidth(84);
        m_redoButton->setCallback([&](){redo();});

        new Label(m_editPanelContents, "Transformations", "sans-bold");

        // flip h
        w = new Button(m_editPanelContents, "Flip H", ENTYPO_ICON_LEFT_BOLD);
        w->setCallback([&](){flipImage(true);});
        m_filterButtons.push_back(w);

        // flip v
        w = new Button(m_editPanelContents, "Flip V", ENTYPO_ICON_DOWN_BOLD);
        w->setCallback([&](){flipImage(false);});
        m_filterButtons.push_back(w);

        // rotate cw
        w = new Button(m_editPanelContents, "Rotate CW", ENTYPO_ICON_CW);
        w->setCallback([&]()
                       {
                           if (!currentImage())
                               return;

                           currentImage()->modify([&](HDRImage & img)
                              {
                                  img = img.rotated90CW();
                                  return new LambdaUndo([](HDRImage & img2){img2 = img2.rotated90CCW();},
                                                        [](HDRImage & img2){img2 = img2.rotated90CW();});
                              });
                           updateCaption();
                           repopulateLayerList();
                           setSelectedLayer(m_current);
                       });
        m_filterButtons.push_back(w);

        // rotate ccw
        w = new Button(m_editPanelContents, "Rotate CCW", ENTYPO_ICON_CCW);
        w->setCallback([&]()
           {
               if (!currentImage())
                   return;

               currentImage()->modify([&](HDRImage & img)
                  {
                      img = img.rotated90CCW();
                      return new LambdaUndo([](HDRImage & img2){img2 = img2.rotated90CW();},
                                            [](HDRImage & img2){img2 = img2.rotated90CCW();});
                  });

               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           });
        m_filterButtons.push_back(w);

        // shift
        m_filterButtons.push_back(createShiftButton(m_editPanelContents));


        new Label(m_editPanelContents, "Resize/resample", "sans-bold");
        m_filterButtons.push_back(createResizeButton(m_editPanelContents));
        m_filterButtons.push_back(createResampleButton(m_editPanelContents));

        new Label(m_editPanelContents, "Filters", "sans-bold");
        m_filterButtons.push_back(createGaussianFilterButton(m_editPanelContents));
        m_filterButtons.push_back(createFastGaussianFilterButton(m_editPanelContents));
        m_filterButtons.push_back(createBoxFilterButton(m_editPanelContents));
        m_filterButtons.push_back(createBilateralFilterButton(m_editPanelContents));
        m_filterButtons.push_back(createUnsharpMaskFilterButton(m_editPanelContents));
        m_filterButtons.push_back(createMedianFilterButton(m_editPanelContents));
    }

    dropEvent(args);

    //
    // create top panel controls
    //
    {
        auto about = new Button(m_controlPanel, "", ENTYPO_ICON_INFO);
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

        m_helpButton = new Button(m_controlPanel, "", ENTYPO_ICON_CIRCLED_HELP);
        m_helpButton->setTooltip("Bring up the help dialog.");
        m_helpButton->setFlags(Button::ToggleButton);
        m_helpButton->setFixedSize(Vector2i(25, 25));
        m_helpButton->setChangeCallback([&](bool value)
        {
            m_helpDialog->setVisible(value);
            if (value)
                m_helpDialog->center();
        });

        m_layersButton = new Button(m_controlPanel, "", ENTYPO_ICON_LIST);
        m_layersButton->setTooltip("Bring up the images dialog to load/remove images, and cycle through open images.");
        m_layersButton->setFlags(Button::ToggleButton);
        m_layersButton->setFixedSize(Vector2i(25, 25));
        m_layersButton->setChangeCallback([&](bool value)
        {
            m_sidePanel->setVisible(value);
            performLayout();
        });

        new Label(m_controlPanel, "EV", "sans-bold");
        m_exposureSlider = new Slider(m_controlPanel);
        m_exposureTextBox = new FloatBox<float>(m_controlPanel, m_exposure);

        m_exposureTextBox->numberFormat("%1.2f");
        m_exposureTextBox->setEditable(true);
        m_exposureTextBox->setFixedWidth(35*m_GUIScaleFactor);
        m_exposureTextBox->setAlignment(TextBox::Alignment::Right);
        auto exposureTextBoxCB = [&](float value)
        {
            m_exposure = value;
            m_exposureSlider->setValue(m_exposure);
        };
        m_exposureTextBox->setCallback(exposureTextBoxCB);
        m_exposureSlider->setCallback([&](float value)
        {
            m_exposure = round(4*value) / 4.0f;
            m_exposureTextBox->setValue(m_exposure);
            m_exposureSlider->setValue(m_exposure);
        });
        m_exposureSlider->setFixedWidth(100*m_GUIScaleFactor);
        m_exposureSlider->setRange({-9.0f,9.0f});
        m_exposureTextBox->setValue(m_exposure);
        exposureTextBoxCB(m_exposure);


        m_sRGB = new CheckBox(m_controlPanel, "sRGB   ");

        m_gammaLabel = new Label(m_controlPanel, "Gamma", "sans-bold");
        m_gammaSlider = new Slider(m_controlPanel);
        m_gammaTextBox = new FloatBox<float>(m_controlPanel);

        m_gammaTextBox->setEditable(true);
        m_gammaTextBox->numberFormat("%1.3f");
        m_gammaTextBox->setFixedWidth(40*m_GUIScaleFactor);
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
        m_gammaSlider->setFixedWidth(100*m_GUIScaleFactor);
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

        m_dither = new CheckBox(m_controlPanel, "Dither  ");
        m_drawGrid = new CheckBox(m_controlPanel, "Grid  ");
        m_drawValues = new CheckBox(m_controlPanel, "RGB values  ");
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
        // m_helpDialog->setTheme(thm);
        m_helpDialog->setVisible(false);
        GridLayout *layout = new GridLayout(Orientation::Horizontal, 2,
                                            Alignment::Middle,
                                            15*m_GUIScaleFactor,
                                            5*m_GUIScaleFactor);
        layout->setColAlignment({ Alignment::Maximum, Alignment::Fill });
        layout->setSpacing(0, 10*m_GUIScaleFactor);
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
    m_layerListWidget->performLayout(mNVGContext);

    m_ditherer.init();

    m_sRGB->setChecked(sRGB);
    m_sRGB->callback()(sRGB);

    // m_controlPanel->requestFocus();

    m_sidePanel->setVisible(false);

    drawAll();
    setVisible(true);
    glfwSwapInterval(1);
}

HDRViewScreen::~HDRViewScreen()
{
    // empty
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

    if (m_current >= 0 && m_current < int(m_layerButtons.size()))
        m_layerButtons[m_current]->setPushed(false);
    if (index >= 0 && index < int(m_layerButtons.size()))
        m_layerButtons[index]->setPushed(true);
    m_current = index;
    updateCaption();
    enableDisableButtons();
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
    // this currently just clears all the widges and recreates all of them
    // from scratch. this doesn't scale, but shoud be fine unless you have a
    // lot of images, and makes the logic a lot simpler.

    // clear everything
    if (m_layerListWidget)
        m_layersPanelContents->removeChild(m_layerListWidget);
    m_layerListWidget = new Widget(m_layersPanelContents);
    m_layerListWidget->setId("layer list widget");

    // a GridLayout seems to cause a floating point exception when there are no
    // images, so use a BoxLayout instead, which seems to work fine
    if (m_images.size())
    {
        GridLayout *grid = new GridLayout(Orientation::Horizontal, 2,
                                                    Alignment::Fill,
                                                    0, 1*m_GUIScaleFactor);
        grid->setSpacing(1, 5*m_GUIScaleFactor);
        m_layerListWidget->setLayout(grid);
    }
    else
        m_layerListWidget->setLayout(new BoxLayout(Orientation::Vertical,
                                                    Alignment::Fill,
                                                    0, 5*m_GUIScaleFactor));

    mFocusPath.clear();
    m_layerButtons.clear();

    int index = 0;
    for (const auto img : m_images)
    {
        size_t start = img->filename().rfind("/")+1;
        string filename = img->filename().substr(start == string::npos ? 0 : start);
        string shortname = filename;
        if (filename.size() > 8+8+3+4)
            shortname = filename.substr(0, 8) + "..." + filename.substr(filename.size()-12);

        Button *b = new Button(m_layerListWidget, shortname, img->isModified() ? ENTYPO_ICON_PENCIL : 0);
        b->setFlags(Button::RadioButton);
        b->setTooltip(filename);
        b->setFixedSize(Vector2i(145,25)*m_GUIScaleFactor);
        b->setCallback([&, index]{setSelectedLayer(index);});
        m_layerButtons.push_back(b);

        // create a close button for the layer
        b = new Button(m_layerListWidget, "", ENTYPO_ICON_ERASE);
        b->setFixedSize(Vector2i(25,25)*m_GUIScaleFactor);
        b->setCallback([&, index]{closeImage(index);});

        index++;
    }

    for (auto b : m_layerButtons)
        b->setButtonGroup(m_layerButtons);

    m_layerListWidget->performLayout(mNVGContext);
    performLayout();
}

void HDRViewScreen::enableDisableButtons()
{
    if (m_saveButton)
        m_saveButton->setEnabled(m_images.size() > 0);

    if (m_editPanelContents)
        for_each(m_filterButtons.begin(), m_filterButtons.end(),
                 [&](Widget * w){w->setEnabled(m_images.size() > 0); });

    if (m_undoButton)
        m_undoButton->setEnabled(currentImage() && currentImage()->hasUndo());
    if (m_redoButton)
        m_redoButton->setEnabled(currentImage() && currentImage()->hasRedo());
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
    if (!currentImage())
        return;

    if (h)
        currentImage()->modify([](HDRImage & img)
            {
                img = img.flippedHorizontal();
                return new LambdaUndo([](HDRImage & img2){img2 = img2.flippedHorizontal();});
            });
    else
        currentImage()->modify([](HDRImage & img)
           {
               img = img.flippedVertical();
               return new LambdaUndo([](HDRImage & img2){img2 = img2.flippedVertical();});
           });
    updateCaption();
    repopulateLayerList();
    setSelectedLayer(m_current);
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
                m_exposure += 0.25f;
            else
                m_exposure -= 0.25f;
            m_exposureSlider->setValue(m_exposure);
            m_exposureTextBox->setValue(m_exposure);
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
            m_controlPanel->setVisible(!m_controlPanel->visible());
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

    // make the control panel full-width
    m_controlPanel->setPosition(Vector2i(0,0));
    int controlPanelHeight = m_controlPanel->preferredSize(mNVGContext).y();
    m_controlPanel->setSize(Vector2i(width(), controlPanelHeight));

    // put the status bar full-width at the bottom
    m_statusBar->setSize(Vector2i(width(), (m_statusBar->theme()->mTextBoxFontSize+4)*m_GUIScaleFactor));
    m_statusBar->setPosition(Vector2i(0, height()-m_statusBar->height()));

    int sidePanelHeight = height() - m_controlPanel->height() - m_statusBar->height();

    m_sidePanelContents->setFixedWidth(195);
    m_sideScrollPanel->setFixedWidth(195+12*m_GUIScaleFactor);

    // put the layers panel directly below the control panel on the left side
    m_sidePanel->setPosition(Vector2i(0, controlPanelHeight));
    m_sidePanel->setSize(Vector2i(195+12*m_GUIScaleFactor, sidePanelHeight));


    int zoomWidth = m_zoomLabel->preferredSize(mNVGContext).x();
    m_zoomLabel->setWidth(zoomWidth);
    m_zoomLabel->setPosition(Vector2i(width()-zoomWidth-6*m_GUIScaleFactor, 0));

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
        (m_controlPanel->visible() && m_controlPanel->contains(p)) ||
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
    // only necessary before the first time drawAll is called
    glfwGetFramebufferSize(mGLFWWindow, &mFBSize[0], &mFBSize[1]);
    glfwGetWindowSize(mGLFWWindow, &mSize[0], &mSize[1]);
    mPixelRatio = (float) mFBSize[0] / (float) mSize[0];

    float realZoom = m_zoomf * mPixelRatio;
    int ratio1 = (realZoom < 1.0f) ? 1 : (int)round(realZoom);
    int ratio2 = (realZoom < 1.0f) ? (int)round(1.0f/realZoom) : 1;
    string cap = fmt::format("{:7.3f}% ({:d} : {:d})", realZoom * 100, ratio1, ratio2);
    m_zoomLabel->setCaption(cap);
    performLayout();
}

void HDRViewScreen::drawContents()
{
    glViewport(0, 0, mFBSize[0], mFBSize[1]);
    performLayout();
    const GLImage * img = currentImage();
    if (img)
    {

        Matrix4f trans;
        trans.setIdentity();
        trans.rightCols<1>() = Vector4f( 2 * m_imagePan[0]/(mFBSize[0]/mPixelRatio),
                                        -2 * m_imagePan[1]/(mFBSize[1]/mPixelRatio),
                                         0.0f, 1.0f);
        Matrix4f scale;
        scale.setIdentity();
        scale(0,0) = m_zoomf;
        scale(1,1) = m_zoomf;

        Matrix4f imageScale;
        imageScale.setIdentity();
        imageScale(0,0) = float(img->size()[0] * mPixelRatio)/mFBSize[0];
        imageScale(1,1) = float(img->size()[1] * mPixelRatio)/mFBSize[1];

        Matrix4f mvp;
        mvp.setIdentity();
        mvp = scale * trans * imageScale;

        m_ditherer.bind();
        img->draw(mvp, powf(2.0f, m_exposure), m_gamma, m_sRGB->checked(), m_dither->checked(), m_channels);

        drawPixelLabels();
        drawGrid(mvp);
    }
}


void HDRViewScreen::drawGrid(const Matrix4f & mvp) const
{
    const GLImage * img = currentImage();
    if (!m_drawGrid->checked() || m_zoomf < 8 || !img)
        return;

    GLShader shader;
    shader.init(
        "Grid renderer",

        /* Vertex shader */
        "#version 330\n"
        "uniform mat4 modelViewProj;\n"
        "in vec2 position;\n"
        "void main() {\n"
        "    gl_Position = modelViewProj * vec4(position.x, position.y, 0.0, 1.0);\n"
        "}",

        /* Fragment shader */
        "#version 330\n"
        "out vec4 out_color;\n"
        "void main() {\n"
        "    out_color = vec4(0.5, 0.5, 0.5, 0.5);\n"
        "}"
    );

    Vector2i xy0 = topLeftImageCorner2Screen();

    int minJ = max(0, int(-xy0.y() / m_zoomf));
    int maxJ = min(img->height(), int(ceil((mFBSize.y()/mPixelRatio - xy0.y())/m_zoomf)));

    int minI = max(0, int(-xy0.x() / m_zoomf));
    int maxI = min(img->width(), int(ceil((mFBSize.x()/mPixelRatio - xy0.x())/m_zoomf)));

    int numLines = (maxJ - minJ + 1) + (maxI - minI + 1);
    MatrixXu indices(2, numLines);

    for (int i = 0; i < numLines; ++i)
        indices.col(i) << 2 * i, 2 * i + 1;

    MatrixXf positions(2, numLines * 2);

    int line = 0;
    // horizontal lines
    for (int j = minJ; j <= maxJ; ++j)
    {
        positions.col(2*line+0) << (2 * minI/float(img->width()) - 1), -(2 * j/float(img->height()) - 1);
        positions.col(2*line+1) << (2 * maxI/float(img->width()) - 1), -(2 * j/float(img->height()) - 1);
        line++;
    }
    // vertical lines
    for (int i = minI; i <= maxI; ++i)
    {
        positions.col(2*line+0) << (2 * i/float(img->width()) - 1), -(2 * minJ/float(img->height()) - 1);
        positions.col(2*line+1) << (2 * i/float(img->width()) - 1), -(2 * maxJ/float(img->height()) - 1);
        line++;
    }

    shader.bind();
    shader.uploadIndices(indices);
    shader.uploadAttrib("position", positions);

    shader.setUniform("modelViewProj", mvp);
    shader.drawIndexed(GL_LINES, 0, numLines);

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
    int maxJ = min(img->height()-1, int(ceil((mFBSize.y()/mPixelRatio - xy0.y())/m_zoomf)));
    int minI = max(0, int(-xy0.x() / m_zoomf));
    int maxI = min(img->width()-1, int(ceil((mFBSize.x()/mPixelRatio - xy0.x())/m_zoomf)));
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

    return Vector2i(int(m_imagePan[0] * m_zoomf) + int(-img->size()[0] / 2.0 * m_zoomf) + int(mFBSize[0] / 2.0f / mPixelRatio),
                    int(m_imagePan[1] * m_zoomf) + int(-img->size()[1] / 2.0 * m_zoomf) + int(mFBSize[1] / 2.0f / mPixelRatio));
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


Button * HDRViewScreen::createGaussianFilterButton(Widget * parent)
{
    static float width = 1.0f, height = 1.0f;
    static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
    string name = "Gaussian blur...";
    auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
    b->setCallback([&,name]()
       {
           FormHelper *gui = new FormHelper(this);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("Width:", width);
           w->setSpinnable(true);
           w->setMinValue(0.0f);
           w = gui->addVariable("Height:", height);
           w->setSpinnable(true);
           w->setMinValue(0.0f);

	       gui->addVariable("Border mode X:", borderModeX, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});
	       gui->addVariable("Border mode Y:", borderModeY, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});

           gui->addButton("Cancel", [&,window](){window->dispose();})->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&,window]()
           {
               if (!currentImage())
                   return;

               currentImage()->modify([&](HDRImage & img)
                  {
                      auto undo = new FullImageUndo(img);
                      img = img.GaussianBlurred(width, height, borderModeX, borderModeY);
                      return undo;
                  });
               window->dispose();
               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           })->setIcon(ENTYPO_ICON_CHECK);

           performLayout();
           window->center();
           window->requestFocus();
       });
    return b;
}

Button * HDRViewScreen::createFastGaussianFilterButton(Widget * parent)
{
    static float width = 1.0f, height = 1.0f;
    static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
    string name = "Fast Gaussian blur...";
    auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
    b->setCallback([&,name]()
       {
           FormHelper *gui = new FormHelper(this);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("Width:", width);
           w->setSpinnable(true);
           w->setMinValue(0.0f);
           w = gui->addVariable("Height:", height);
           w->setSpinnable(true);
           w->setMinValue(0.0f);

           gui->addVariable("Border mode X:", borderModeX, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});
           gui->addVariable("Border mode Y:", borderModeY, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});

           gui->addButton("Cancel", [&,window](){window->dispose();})->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&,window]()
           {
               if (!currentImage())
                   return;

               currentImage()->modify([&](HDRImage & img)
                                      {
                                          auto undo = new FullImageUndo(img);
                                          img = img.fastGaussianBlurred(width, height, borderModeX, borderModeY);
                                          return undo;
                                      });
               window->dispose();
               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           })->setIcon(ENTYPO_ICON_CHECK);

           performLayout();
           window->center();
           window->requestFocus();
       });
    return b;
}

Button * HDRViewScreen::createBoxFilterButton(Widget * parent)
{
    static float width = 1.0f, height = 1.0f;
    static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
    string name = "Box blur...";
    auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
    b->setCallback([&,name]()
       {
           FormHelper *gui = new FormHelper(this);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("Width:", width);
           w->setSpinnable(true);
           w->setMinValue(0.0f);
           w = gui->addVariable("Height:", height);
           w->setSpinnable(true);
           w->setMinValue(0.0f);

           gui->addVariable("Border mode X:", borderModeX, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});
           gui->addVariable("Border mode Y:", borderModeY, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});

           gui->addButton("Cancel", [&,window](){window->dispose();})->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&,window]()
           {
               if (!currentImage())
                   return;

               currentImage()->modify([&](HDRImage & img)
                  {
                      auto undo = new FullImageUndo(img);
                      img = img.boxBlurred(width, height, borderModeX, borderModeY);
                      return undo;
                  });
               window->dispose();
               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           })->setIcon(ENTYPO_ICON_CHECK);

           performLayout();
           window->center();
           window->requestFocus();
       });
    return b;
}

Button * HDRViewScreen::createBilateralFilterButton(Widget * parent)
{
    static float rangeSigma = 1.0f, valueSigma = 0.1f;
    static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
    string name = "Bilateral filter...";
    auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
    b->setCallback([&,name]()
       {
           FormHelper *gui = new FormHelper(this);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("Range sigma:", rangeSigma);
           w->setSpinnable(true);
           w->setMinValue(0.0f);
           w = gui->addVariable("Value sigma:", valueSigma);
           w->setSpinnable(true);
           w->setMinValue(0.0f);

           gui->addVariable("Border mode X:", borderModeX, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});
           gui->addVariable("Border mode Y:", borderModeY, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});

           gui->addButton("Cancel", [&,window](){window->dispose();})->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&,window]()
           {
               if (!currentImage())
                   return;
               currentImage()->modify([&](HDRImage & img)
                  {
                      auto undo = new FullImageUndo(img);
                      img = img.bilateralFiltered(valueSigma, rangeSigma, borderModeX, borderModeY);
                      return undo;
                  });
               window->dispose();
               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           })->setIcon(ENTYPO_ICON_CHECK);

           performLayout();
           window->center();
           window->requestFocus();
       });
    return b;
}

Button * HDRViewScreen::createUnsharpMaskFilterButton(Widget * parent)
{
    static float sigma = 1.0f, strength = 1.0f;
    static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
    string name = "Unsharp mask...";
    auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
    b->setCallback([&,name]()
       {
           FormHelper *gui = new FormHelper(this);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("Sigma:", sigma);
           w->setSpinnable(true);
           w->setMinValue(0.0f);
           w = gui->addVariable("Strength:", strength);
           w->setSpinnable(true);
           w->setMinValue(0.0f);

           gui->addVariable("Border mode X:", borderModeX, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});
           gui->addVariable("Border mode Y:", borderModeY, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});

           gui->addButton("Cancel", [&,window](){window->dispose();})->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&,window]()
           {
               if (!currentImage())
                   return;
               currentImage()->modify([&](HDRImage & img)
                  {
                      auto undo = new FullImageUndo(img);
                      img = img.unsharpMasked(sigma, strength, borderModeX, borderModeY);
                      return undo;
                  });
               window->dispose();
               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           })->setIcon(ENTYPO_ICON_CHECK);

           performLayout();
           window->center();
           window->requestFocus();
       });
    return b;
}

Button * HDRViewScreen::createMedianFilterButton(Widget * parent)
{
    static float radius = 1.0f;
    static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
    string name = "Median filter...";
    auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
    b->setCallback([&,name]()
       {
           FormHelper *gui = new FormHelper(this);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("Radius:", radius);
           w->setSpinnable(true);
           w->setMinValue(0.0f);

           gui->addVariable("Border mode X:", borderModeX, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});
           gui->addVariable("Border mode Y:", borderModeY, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});

           gui->addButton("Cancel", [&,window](){window->dispose();})->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&,window]()
           {
               if (!currentImage())
                   return;
               currentImage()->modify([&](HDRImage & img)
                  {
                      auto undo = new FullImageUndo(img);
                      img = img.medianFiltered(radius, borderModeX, borderModeY);
                      return undo;
                  });
               window->dispose();
               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           })->setIcon(ENTYPO_ICON_CHECK);

           performLayout();
           window->center();
           window->requestFocus();
       });
    return b;
}


Button * HDRViewScreen::createResizeButton(Widget * parent)
{
    static int width = 128, height = 128;
    string name = "Resize...";
    auto b = new Button(parent, name, ENTYPO_ICON_RESIZE_FULL);
    b->setCallback([&,name]()
       {
           FormHelper *gui = new FormHelper(this);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
           window->setModal(true);

           width = currentImage()->width();
           auto w = gui->addVariable("Width:", width);
           w->setSpinnable(true);
           w->setMinValue(1);

           height = currentImage()->height();
           w = gui->addVariable("Height:", height);
           w->setSpinnable(true);
           w->setMinValue(1);

           gui->addButton("Cancel", [&,window](){window->dispose();})->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&,window]()
           {
               if (!currentImage())
                   return;

               currentImage()->modify([&](HDRImage & img)
                  {
                      auto undo = new FullImageUndo(img);
                      img = img.resized(width, height);
                      return undo;
                  });
               window->dispose();
               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           })->setIcon(ENTYPO_ICON_CHECK);

           performLayout();
           window->center();
           window->requestFocus();
       });
    return b;
}


Button * HDRViewScreen::createResampleButton(Widget * parent)
{
    enum MappingMode
    {
        ANGULAR_MAP = 0,
        MIRROR_BALL,
        LAT_LONG,
        CUBE_MAP
    };
    static MappingMode from = ANGULAR_MAP, to = ANGULAR_MAP;
    static HDRImage::Sampler sampler = HDRImage::BILINEAR;
    static int width = 128, height = 128;
    static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
    static int samples = 1;

    string name = "Remap...";
    auto b = new Button(parent, name, ENTYPO_ICON_RESIZE_FULL);
    b->setCallback([&,name]()
       {
           FormHelper *gui = new FormHelper(this);
           gui->setFixedSize(Vector2i(125, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           width = currentImage()->width();
           auto w = gui->addVariable("Width:", width);
           w->setSpinnable(true);
           w->setMinValue(1);

           height = currentImage()->height();
           w = gui->addVariable("Height:", height);
           w->setSpinnable(true);
           w->setMinValue(1);

           gui->addVariable("Source parametrization:", from, true)->setItems({"Angular map", "Mirror ball", "Longitude-latitude", "Cube map"});
           gui->addVariable("Target parametrization:", to, true)->setItems({"Angular map", "Mirror ball", "Longitude-latitude", "Cube map"});
           gui->addVariable("Sampler:", sampler, true)->setItems({"Nearest neighbor", "Bilinear", "Bicubic"});
           gui->addVariable("Border mode X:", borderModeX, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});
           gui->addVariable("Border mode Y:", borderModeY, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});

           w = gui->addVariable("Super-samples:", samples);
           w->setSpinnable(true);
           w->setMinValue(1);

           gui->addButton("Cancel", [&,window](){window->dispose();})->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&,window]()
           {
               if (!currentImage())
                   return;

               // by default use a no-op passthrough warp function
               function<Vector2f(const Vector2f&)> warp = [](const Vector2f & uv) {return uv;};

               UV2XYZFn dst2xyz;
               XYZ2UVFn xyz2src;

               if (from != to)
               {
                   if (from == ANGULAR_MAP)
                       xyz2src = XYZToAngularMap;
                   else if (from == MIRROR_BALL)
                       xyz2src = XYZToMirrorBall;
                   else if (from == LAT_LONG)
                       xyz2src = XYZToLatLong;
                   else if (from == CUBE_MAP)
                       xyz2src = XYZToCubeMap;

                   if (to == ANGULAR_MAP)
                       dst2xyz = angularMapToXYZ;
                   else if (to == MIRROR_BALL)
                       dst2xyz = mirrorBallToXYZ;
                   else if (to == LAT_LONG)
                       dst2xyz = latLongToXYZ;
                   else if (to == CUBE_MAP)
                       dst2xyz = cubeMapToXYZ;

                   warp = [&](const Vector2f & uv){return xyz2src(dst2xyz(Vector2f(uv(0), uv(1))));};
               }

               currentImage()->modify([&](HDRImage & img)
                  {
                      auto undo = new FullImageUndo(img);
                      img = img.resampled(width, height, warp, samples, sampler, borderModeX, borderModeY);
                      return undo;
                  });
               window->dispose();
               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           })->setIcon(ENTYPO_ICON_CHECK);

           performLayout();
           window->center();
           window->requestFocus();
       });
    return b;
}

Button * HDRViewScreen::createShiftButton(Widget * parent)
{
    static HDRImage::Sampler sampler = HDRImage::BILINEAR;
    static HDRImage::BorderMode borderModeX = HDRImage::REPEAT, borderModeY = HDRImage::REPEAT;
    static float dx = 0.f, dy = 0.f;
    string name = "Shift...";
    auto b = new Button(parent, name, ENTYPO_ICON_HAIR_CROSS);
    b->setCallback([&,name]()
       {
           FormHelper *gui = new FormHelper(this);
           gui->setFixedSize(Vector2i(125, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("X offset:", dx);
           w->setSpinnable(true);

           w = gui->addVariable("Y offset:", dy);
           w->setSpinnable(true);

           gui->addVariable("Sampler:", sampler, true)->setItems({"Nearest neighbor", "Bilinear", "Bicubic"});
           gui->addVariable("Border mode X:", borderModeX, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});
           gui->addVariable("Border mode Y:", borderModeY, true)->setItems({"Black", "Edge", "Repeat", "Mirror"});

           gui->addButton("Cancel", [&,window](){window->dispose();})->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&,window]()
           {
               if (!currentImage())
                   return;

               // by default use a no-op passthrough warp function
               function<Vector2f(const Vector2f&)> shift = [&](const Vector2f & uv)
               {
                   return (uv + Vector2f(dx/currentImage()->width(), dy/currentImage()->height())).eval();
               };

               currentImage()->modify([&](HDRImage & img)
                  {
                      auto undo = new FullImageUndo(img);
                      img = img.resampled(img.width(), img.height(), shift, 1, sampler, borderModeX, borderModeY);
                      return undo;
                  });
               window->dispose();
               updateCaption();
               repopulateLayerList();
               setSelectedLayer(m_current);
           })->setIcon(ENTYPO_ICON_CHECK);

           performLayout();
           window->center();
           window->requestFocus();
       });
    return b;
}


