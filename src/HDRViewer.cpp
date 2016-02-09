/*! \file HDRViewer.cpp
    \author Wojciech Jarosz
*/
#include "HDRViewer.h"
#include <iostream>     // std::cout, std::fixed
#include <iomanip>      // std::setprecision

using namespace std;

//! Cast between arbitrary types using string streams.
template<typename result_type, typename arg_type>
result_type lexical_cast(const arg_type &arg)
{
    std::stringstream interpreter;
    interpreter << arg;
    result_type result = result_type();
    interpreter >> result;
    return result;
}


HDRViewScreen::HDRViewScreen(float exposure, float gamma, vector<string> args) :
    Screen(Vector2i(640,480), "HDRView", true),
    m_exposure(exposure), m_gamma(gamma)
{
    setBackground(Vector3f(0.1, 0.1, 0.1));
    Theme * thm = new Theme(mNVGContext);
    thm->mWindowHeaderHeight = 0;
    thm->mWindowDropShadowSize = 0;
    thm->mWindowCornerRadius = 0;
    thm->mWindowFillFocused = Color(.2f,.2f,.2f,.9f);
    thm->mWindowFillUnfocused = Color(.2f,.2f,.2f,.9f);

    m_controlPanel = new Window(this, "");
    m_controlPanel->setId("control panel");
    m_controlPanel->setTheme(thm);
    m_controlPanel->setPosition(Vector2i(0, 0));
    m_controlPanel->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 5, 5));

    //
    // create status bar widgets
    //
    m_statusBar = new Window(this, "");
    m_statusBar->setTheme(thm);

    m_pixelInfoLabel = new Label(m_statusBar, "(---,---)", "sans");
    m_pixelInfoLabel->setFontSize(12);
    m_pixelInfoLabel->setPosition(Vector2i(6, 0));
    
    m_zoomLabel = new Label(m_statusBar, "100% (1 : 1)", "sans");
    m_zoomLabel->setFontSize(12);

    //
    // create layers panel
    //
    {
        m_layers = new Window(this, "");
        m_layers->setId("layers");
        m_layers->setTheme(thm);
        m_layers->setLayout(new GroupLayout(10, 4, 8, 10));
        new Label(m_layers, "File operations", "sans-bold");
        // Widget *filePanel = new Widget(m_layers);
        // filePanel->setLayout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 5));
        
        Button *b = new Button(m_layers, "Open image", ENTYPO_ICON_SQUARED_PLUS);
        b->setBackgroundColor(Color(0, 100, 0, 75));
        b->setTooltip("Load an image and add it to the set of opened images.");
        b->setFontSize(15);
        b->setCallback([this] {
            string file = file_dialog({ {"EXR", "OpenEXR image"},
                {"PNG", "Portable Network Graphic"},
                {"JPG", "Jpeg image"},
                {"TGA", "Targa image"},
                {"BMP", "Windows Bitmap image"},
                {"GIF", "GIF image"},
                {"HDR", "Radiance rgbE format"},
                {"PPM", "Portable pixel map"},
                {"PSD", "Photoshop document"}  }, false);
            if (file.size())
                dropEvent({file});
        });

        Button *b1 = new Button(m_layers, "Save image", ENTYPO_ICON_SAVE);
        b1->setBackgroundColor(Color(0, 0, 100, 75));
        b1->setTooltip("Save the image to disk.");
        b1->setFontSize(15);
        b1->setCallback([this] {
            if (!currentImage())
                return;
            string file = file_dialog({
                {"PNG", "Portable Network Graphic"},
                {"JPG", "Jpeg image"},
                {"TGA", "Targa image"},
                {"BMP", "Windows Bitmap image"},
                {"HDR", "Radiance rgbE format"}}, true);
            
            if (file.size())
            {
                try
                {
                    currentImage()->save(file, powf(2.0f, m_exposure),
                                         m_gamma, m_sRGB->checked(),
                                         m_dither->checked());
                }
                catch (std::runtime_error & e)
                {
                    new MessageDialog(this, MessageDialog::Type::Warning, "Error",
                          string("Could not save image due to an error:\n") + e.what());
                }
            }
        });
        
        m_closeButton = new Button(m_layers, "Close image", ENTYPO_ICON_SQUARED_MINUS);
        m_closeButton->setBackgroundColor(Color(100, 0, 0, 75));
        m_closeButton->setTooltip("Close the currently selected image.");
        m_closeButton->setFontSize(15);
        m_closeButton->setEnabled(m_images.size() > 0);
        m_closeButton->setCallback([&] { closeCurrentImage(); });
        
        // Button * b0 = new Button(m_layers->buttonPanel(), "", ENTYPO_ICON_CROSS);
        // b0->setCallback([this]
        //    {
        //        this->m_layers->setVisible(false);
        //        this->m_layersButton->setPushed(false);
        //    });

        new Label(m_layers, "Opened images:", "sans-bold");

        m_vscrollContainer = new Widget(m_layers);
        // m_vscrollContainer->setLayout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));
        
        m_layerScrollPanel = new VScrollPanel(m_vscrollContainer);
        m_layerListWidget = new Widget(m_layerScrollPanel);
        m_layerListWidget->setId("layer list widget");
        m_layerListWidget->setLayout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 5));

        // VScrollPanel *vscroll = new VScrollPanel(popup);
        // ImagePanel *imgPanel = new ImagePanel(vscroll);
        // imgPanel->setImages(icons);

        // m_vscrollContainer->setHeight(40);


    }

    dropEvent(args);
    
    //
    // create top panel controls
    //
    {
        m_helpButton = new ToolButton(m_controlPanel, ENTYPO_ICON_CIRCLED_HELP);
        m_helpButton->setTooltip("Bring up the help dialog.");
        m_helpButton->setFixedSize(Vector2i(22,22));
        m_helpButton->setFontSize(15);
        m_helpButton->setChangeCallback([this](bool value) {m_helpDialog->setVisible(value);});

        m_layersButton = new ToolButton(m_controlPanel, ENTYPO_ICON_FOLDER);
        m_layersButton->setTooltip("Bring up the images dialog to load/remove images, and cycle through open images.");
        m_layersButton->setFixedSize(Vector2i(22,22));
        m_layersButton->setFontSize(15);
        m_layersButton->setChangeCallback([&,this](bool value) {m_layers->setVisible(value);performLayout();});

        m_exposureLabel = new Label(m_controlPanel, "Exposure", "sans-bold");
        m_exposureLabel->setFontSize(16);

        m_exposureSlider = new Slider(m_controlPanel);

        m_exposureTextBox = new TextBox(m_controlPanel);
        m_exposureTextBox->setEditable(true);
        {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3) << m_exposure;
            m_exposureTextBox->setValue(ss.str());
        }
        m_exposureTextBox->setFixedSize(Vector2i(40,15));
        m_exposureTextBox->setFontSize(14);
        m_exposureTextBox->setAlignment(TextBox::Alignment::Right);
        m_exposureTextBox->setCallback([this](const std::string & value)
        {
            this->m_exposure = lexical_cast<float>(value);
            this->m_exposureSlider->setValue(this->m_exposure / 20.0f + 0.5f);
            return true;
        });

        m_exposureSlider->setValue(m_exposure / 20.0f + 0.5f);
        m_exposureSlider->setFixedWidth(40);
        m_exposureSlider->setCallback([this](float value)
        {
            this->m_exposure = (value - 0.5f) * 20;
            std::stringstream ss;
            ss << std::fixed << std::setprecision(2) << this->m_exposure;
            this->m_exposureTextBox->setValue(ss.str());
        });

        m_sRGB = new CheckBox(m_controlPanel, "sRGB   ");
        m_gammaLabel = new Label(m_controlPanel, "Gamma", "sans-bold");
        m_gammaSlider = new Slider(m_controlPanel);

        m_gammaTextBox = new TextBox(m_controlPanel);
        m_gammaTextBox->setEditable(true);
        {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3) << m_gamma;
            m_gammaTextBox->setValue(ss.str());
        }
        m_gammaTextBox->setFixedSize(Vector2i(40,15));
        m_gammaTextBox->setFontSize(14);
        m_gammaTextBox->setAlignment(TextBox::Alignment::Right);
        m_gammaTextBox->setCallback([this](const std::string & value)
        {
            float v = lexical_cast<float>(value);
            this->m_gamma = v;
            this->m_gammaSlider->setValue((v - 0.1f) / (10.0f-0.1f));
            return true;
        });

        m_gammaSlider->setValue((m_gamma - 0.1f) / (10.0f-0.1f));
        m_gammaSlider->setFixedWidth(40);
        m_gammaSlider->setCallback([this](float value)
        {
            this->m_gamma = 0.1f + value * (10.0f - 0.1f);
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3) << m_gamma;
            this->m_gammaTextBox->setValue(ss.str());
        });

        m_gammaTextBox->setCallback([this](const std::string & value)
        {
            float v = lexical_cast<float>(value);
            this->m_gamma = v;
            this->m_gammaSlider->setValue((v - 0.1f) / (10.0f-0.1f));
            return true;
        });

        m_sRGB->setCallback([this](bool value)
        {
            m_gammaSlider->setEnabled(!value);
            m_gammaTextBox->setEnabled(!value);
            m_gammaLabel->setEnabled(!value);
            m_gammaLabel->setColor(value ? mTheme->mDisabledTextColor : mTheme->mTextColor);
            // m_gammaSlider->setVisible(!value);
            // m_gammaTextBox->setVisible(!value);
            // m_gammaLabel->setVisible(!value);
            performLayout();
        });

        m_dither = new CheckBox(m_controlPanel, "Dither  ");
        m_drawGrid = new CheckBox(m_controlPanel, "Grid  ");
        m_drawValues = new CheckBox(m_controlPanel, "RGB values  ");
        m_dither->setChecked(true);
        m_drawGrid->setChecked(true);
        m_drawValues->setChecked(true);
    }

    //
    // create help dialog
    //
    {
        vector<pair<string, string> > helpStrings =
        {
            {"h",       "Toggle this help panel"},
            {"l",       "Toggle the layer panel"},
            {"r",       "Reload image"},
            {"-/+",     "Zoom out/in"},
            {"[SCROLL]","Pan the image"},
            {"g/G",     "Decrease/Increase gamma"},
            {"e/E",     "Decrease/Increase exposure"},
            {"f",       "Flip image about horizontal axis"},
            {"m",       "Mirror image about vertical axis"},
            {"n",       "Negate image"},
            {"1/2/3/4", "View the R/G/B/RGB channels"},
            {"[SPACE]", "Re-center view"},
            {"[PG_UP]", "Previous image"},
            {"[PG_DN]", "Next image"}
        };

        m_helpDialog = new Window(this, "Help");
        m_helpDialog->setId("help dialog");
        // m_helpDialog->setTheme(thm);
        m_helpDialog->setVisible(false);
        GridLayout *layout = new GridLayout(Orientation::Horizontal, 2,
                           Alignment::Middle, 15, 5);
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
        button->setCallback([this]
            {
                this->m_helpDialog->setVisible(false);
                this->m_helpButton->setPushed(false);
            });
    }

    updateZoomLabel();
    m_layerListWidget->performLayout(mNVGContext);
    m_vscrollContainer->performLayout(mNVGContext);

    m_ditherer.init();

    m_sRGB->setChecked(true);
    m_sRGB->callback()(true);

    // m_controlPanel->requestFocus();

    m_layers->setVisible(false);

    drawAll();
    setVisible(true);
    glfwSwapInterval(1);
}

HDRViewScreen::~HDRViewScreen()
{
    // empty
}


ImageQuad * HDRViewScreen::currentImage()
{
    if (m_current < 0 || m_current >= int(m_images.size()))
        return nullptr;
    return m_images[m_current];
}
const ImageQuad * HDRViewScreen::currentImage() const
{
    if (m_current < 0 || m_current >= int(m_images.size()))
        return nullptr;
    return m_images[m_current];
}


void HDRViewScreen::closeImage(int index)
{
    ImageQuad * img = (index < 0 || index >= int(m_images.size())) ? nullptr : m_images[index];

    if (img)
    {
        delete img;
        m_images.erase(m_images.begin()+index);
        repopulateLayerList();
        if (index < m_current)
            setSelectedLayer(m_current-1);
        else
            setSelectedLayer(m_current >= int(m_images.size()) ? m_images.size()-1 : m_current);
        if (m_closeButton) m_closeButton->setEnabled(m_images.size() > 0);
    }
}

void HDRViewScreen::closeCurrentImage()
{
    closeImage(m_current);
}


void HDRViewScreen::updateCaption()
{
    if (currentImage())
        setCaption(string("HDRView [") + currentImage()->filename() + "]");
    else
        setCaption(string("HDRView"));
}

void HDRViewScreen::setSelectedLayer(int index)
{
    if (m_images.empty() || index < 0 || index >= int(m_images.size()))
    {
        cerr << "Trying to select an invalid image index" << endl;
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
}

void HDRViewScreen::repopulateLayerList()
{
    int index = 0;
    
    for (auto child : m_layerButtons)
        m_layerListWidget->removeChild(child);
    
    m_layerButtons.clear();
    for (const auto img : m_images)
    {
        size_t start = img->filename().rfind("/")+1;
        string shortname = img->filename().substr(start == string::npos ? 0 : start);
        
        Button *b = new Button(m_layerListWidget, shortname);
        b->setFlags(Button::RadioButton);
        b->setFixedSize(Vector2i(b->width(),22));
        b->setFontSize(14);
        b->setCallback([&, index]
            {
                setSelectedLayer(index);
            });
        m_layerButtons.push_back(b);

        index++;
    }
    
    for (auto b : m_layerButtons)
        b->setButtonGroup(m_layerButtons);

    m_layerListWidget->performLayout(mNVGContext);
    // m_layerScrollPanel->performLayout(mNVGContext);
    m_vscrollContainer->performLayout(mNVGContext);
    performLayout();
}

bool HDRViewScreen::dropEvent(const vector<string> &filenames)
{
    size_t numErrors = 0;
    vector<bool> loadedOK;
    for (auto i : filenames)
    {
        ImageQuad* image = new ImageQuad();
        if (image->load(i.c_str()))
        {
            loadedOK.push_back(true);
            image->init();
            m_images.push_back(image);
            printf("Loaded \"%s\" [%dx%d]\n", i.c_str(), image->width(), image->height());
        }
        else
        {
            loadedOK.push_back(false);
            numErrors++;
            delete image;
        }
    }
    if (m_closeButton)
        m_closeButton->setEnabled(m_images.size() > 0);
    
    repopulateLayerList();

    setSelectedLayer(m_images.size()-1);

    if (numErrors)
    {
        string badFiles;
        for (size_t i = 0; i < loadedOK.size(); ++i)
        {
            if (!loadedOK[i])
                badFiles += filenames[i] + "\n";
        }
        new MessageDialog(this, MessageDialog::Type::Warning, "Error",
                          "Could not load:\n " + badFiles);
        return numErrors == filenames.size();
    }

    return true;
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
            setVisible(false);
            return true;
        case GLFW_KEY_EQUAL:
        {
            if (m_zoom < 20) m_zoom++;

            m_zoomf = powf(2.0f, m_zoom/2.0f);

            updateZoomLabel();

            return true;
        }
        case GLFW_KEY_MINUS:
        {
            if (m_zoom > -20) m_zoom--;

            m_zoomf = powf(2.0f, m_zoom/2.0f);

            updateZoomLabel();

            return true;
        }
        case GLFW_KEY_G:
        {
            if (modifiers & GLFW_MOD_SHIFT)
                m_gamma += 0.1f;
            else
            {
                m_gamma -= 0.1f;
                if (m_gamma <= 0.0f)
                    m_gamma = 0;
            }
            m_gammaSlider->setValue((m_gamma - 0.1f) / (10.0f-0.1f));
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3) << m_gamma;
            m_gammaTextBox->setValue(ss.str());
            return true;
        }
        case GLFW_KEY_E:
        {
            if (modifiers & GLFW_MOD_SHIFT)
                m_exposure += 0.25f;
            else
                m_exposure -= 0.25f;
            m_exposureSlider->setValue(m_exposure / 20.0f + 0.5f);
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3) << m_exposure;
            m_exposureTextBox->setValue(ss.str());
            return true;
        }
        
        case GLFW_KEY_F:
            m_flipV = !m_flipV;
            return true;
        
        case GLFW_KEY_M:
            m_flipH = !m_flipH;
            return true;
        
        case GLFW_KEY_SPACE:
            m_imagePan = Vector2f::Zero();
            drawAll();
            return true;

        case GLFW_KEY_T:
            m_controlPanel->setVisible(!m_controlPanel->visible());
            return true;

        case GLFW_KEY_H:
            m_helpDialog->setVisible(!m_helpDialog->visible());
            m_helpDialog->center();
            m_helpButton->setPushed(m_helpDialog->visible());
            return true;

        case GLFW_KEY_L:
            m_layers->setVisible(!m_layers->visible());
            m_layersButton->setPushed(m_layers->visible());
            return true;
            
        case GLFW_KEY_PAGE_DOWN:
            if (!m_images.empty())
                setSelectedLayer((m_current+1) % int(m_images.size()));
            break;

        case GLFW_KEY_PAGE_UP:
            setSelectedLayer((m_current-1 < 0) ? m_images.size()-1 : (m_current-1) % int(m_images.size()));
            break;

        case GLFW_KEY_1:
            m_channels = Vector3f(1.0f, 0.0f, 0.0f);
            return true;
        case GLFW_KEY_2:
            m_channels = Vector3f(0.0f, 1.0f, 0.0f);
            return true;
        case GLFW_KEY_3:
            m_channels = Vector3f(0.0f, 0.0f, 1.0f);
            return true;
        case GLFW_KEY_4:
            m_channels = Vector3f(1.0f, 1.0f, 1.0f);
            return true;

        default:
            return Screen::keyboardEvent(key, scancode, action, modifiers);
    }
    return false;
}

void HDRViewScreen::performLayout()
{
    for (auto c : mChildren)
        c->performLayout(mNVGContext);

    // make the control panel full-width
    m_controlPanel->setPosition(Vector2i(0,0));
    int controlPanelHeight = m_controlPanel->preferredSize(mNVGContext).y();
    m_controlPanel->setSize(Vector2i(width(), controlPanelHeight));
    
    // put the layers panel directly below the control panel on the left side
    m_layers->setPosition(Vector2i(0,controlPanelHeight));
    m_layers->setSize(m_layers->preferredSize(mNVGContext));

    // put the status bar full-width at the bottom
    m_statusBar->setSize(Vector2i(width(), 18));
    m_statusBar->setPosition(Vector2i(0, height()-m_statusBar->height()));

    int zoomWidth = m_zoomLabel->preferredSize(mNVGContext).x();
    m_zoomLabel->setWidth(zoomWidth);
    m_zoomLabel->setPosition(Vector2i(width()-zoomWidth-6, 0));

    int lheight = (height() - m_vscrollContainer->absolutePosition().y() - m_statusBar->height() - 10);
    int lheight2 = std::min(lheight, m_layerListWidget->preferredSize(mNVGContext).y());
    m_layerScrollPanel->setFixedHeight(lheight2);
    m_vscrollContainer->setFixedHeight(lheight);
    
    m_layerListWidget->setFixedWidth(m_layerListWidget->preferredSize(mNVGContext).x());
    m_vscrollContainer->setFixedWidth(m_layerListWidget->preferredSize(mNVGContext).x()+18);
    m_layerScrollPanel->setFixedWidth(m_layerListWidget->preferredSize(mNVGContext).x()+18);

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

bool HDRViewScreen::mouseMotionEvent(const Vector2i &p, const Vector2i &rel, int button, int)
{
    if (button & (1 << GLFW_MOUSE_BUTTON_1))
        m_imagePan += rel.cast<float>() / m_zoomf;

    Vector2i pixel = screenToImage(p);
    auto img = currentImage();
    if (img && (pixel.array() >= 0).all() && (pixel.array() < img->size().array()).all())
    {
        Color pixelVal = img->pixel(pixel.x(), pixel.y());
        Color iPixelVal = (pixelVal * powf(2.0f, m_exposure) * 255).cwiseMin(255.0f).cwiseMax(0.0f);
        char buf[1024];
        snprintf(buf, 1024, "(%4d,%4d) = (%6.3g, %6.3g, %6.3g) / (%3d, %3d, %3d)",
                 pixel.x(), pixel.y(), pixelVal[0], pixelVal[1], pixelVal[2],
                 (int)round(iPixelVal[0]), (int)round(iPixelVal[1]), (int)round(iPixelVal[2]));
        m_pixelInfoLabel->setCaption(buf);
    }
    else
        m_pixelInfoLabel->setCaption("");
    
    m_statusBar->performLayout(mNVGContext);

    return true;
}


bool HDRViewScreen::mouseDragEvent(const Vector2i &, const Vector2i &rel, int, int)
{
    m_imagePan += rel.cast<float>() / m_zoomf;

    return true;
}

void HDRViewScreen::updateZoomLabel()
{
    // only necessary before the first time drawAll is called
    glfwGetFramebufferSize(mGLFWWindow, &mFBSize[0], &mFBSize[1]);
    glfwGetWindowSize(mGLFWWindow, &mSize[0], &mSize[1]);
    mPixelRatio = (float) mFBSize[0] / (float) mSize[0];

    float realZoom = m_zoomf * mPixelRatio;

    char buf[1024];
    int ratio1 = (realZoom < 1.0f) ? 1 : (int)round(realZoom);
    int ratio2 = (realZoom < 1.0f) ? (int)round(1.0f/realZoom) : 1;
    snprintf(buf, 1024, "%7.3f%% (%d : %d)", realZoom * 100, ratio1, ratio2);
    m_zoomLabel->setCaption(buf);
    performLayout();
}

void HDRViewScreen::drawContents()
{
    performLayout();
    auto img = currentImage();
    if (img)
    {
        glViewport(0, 0, mFBSize[0], mFBSize[1]);

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

        Matrix4f flip;
        flip.setIdentity();
        flip(0,0) = m_flipH ? -1.0f : 1.0f;
        flip(1,1) = m_flipV ? -1.0f : 1.0f;

        Matrix4f mvp;
        mvp.setIdentity();
        mvp = scale * trans * imageScale * flip;

        m_ditherer.bind();
        img->draw(mvp, powf(2.0f, m_exposure), m_gamma, m_sRGB->checked(), m_dither->checked(), m_channels);

        drawPixelLabels();
        drawGrid(mvp);
    }
    
    glViewport(0, 0, mFBSize[0], mFBSize[1]);
}


void HDRViewScreen::drawGrid(const Matrix4f & mvp) const
{
    auto img = currentImage();
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
        "    out_color = vec4(1, 1, 1, 1);\n"
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
    float xFlip = (m_flipH ? -1.0f : 1.0f);
    float yFlip = (m_flipV ? 1.0f : -1.0f);
    // horizontal lines
    for (int j = minJ; j <= maxJ; ++j)
    {
        positions.col(2*line+0) << xFlip * (2 * minI/float(img->width()) - 1), yFlip * (2 * j/float(img->height()) - 1);
        positions.col(2*line+1) << xFlip * (2 * maxI/float(img->width()) - 1), yFlip * (2 * j/float(img->height()) - 1);
        line++;
    }
    // vertical lines
    for (int i = minI; i <= maxI; ++i)
    {
        positions.col(2*line+0) << xFlip * (2 * i/float(img->width()) - 1), yFlip * (2 * minJ/float(img->height()) - 1);
        positions.col(2*line+1) << xFlip * (2 * i/float(img->width()) - 1), yFlip * (2 * maxJ/float(img->height()) - 1);
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
    auto img = currentImage();
    // if pixels are big enough, draw color labels on each visible pixel
    if (!m_drawValues->checked() || m_zoomf < 32 || !img)
        return;

    // TODO: account for flipping

    Vector2i xy0 = topLeftImageCorner2Screen();
    int minJ = max(0, int(-xy0.y() / m_zoomf));
    int maxJ = min(img->height()-1, int(ceil((mFBSize.y()/mPixelRatio - xy0.y())/m_zoomf)));
    int minI = max(0, int(-xy0.x() / m_zoomf));
    int maxI = min(img->width()-1, int(ceil((mFBSize.x()/mPixelRatio - xy0.x())/m_zoomf)));
    for (int j = minJ; j <= maxJ; ++j)
    {
        for (int i = minI; i <= maxI; ++i)
        {
            Vector3f pixel( img->rgba()[4*i + 4*img->width() * j],
                            img->rgba()[4*i + 4*img->width() * j + 1],
                            img->rgba()[4*i + 4*img->width() * j + 2]);

            float luminance = 1.0/3.0f * (pixel.x() + pixel.y() + pixel.z()) * pow(2.0f, m_exposure);

            char buf[1024];
            snprintf(buf, 1024, "%1.3f\n%1.3f\n%1.3f", pixel[0], pixel[1], pixel[2]);

            drawText(imageToScreen(Vector2i(i,j)), buf,
                     luminance > 0.5f ? Color(0.0f, 0.0f, 0.0f, 0.5f) : Color(1.0f, 1.0f, 1.0f, 0.5f),
                     m_zoomf/32.0f * 10, m_zoomf);
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
    nvgFontSize(mNVGContext, fontSize);
    nvgFillColor(mNVGContext, color);
    if (fixedWidth > 0)
    {
        nvgTextAlign(mNVGContext, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgTextBox(mNVGContext, pos.x(), pos.y(), fixedWidth, text.c_str(), nullptr);
    }
    else
    {
        nvgTextAlign(mNVGContext, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgText(mNVGContext, pos.x(), pos.y() + fontSize, text.c_str(), nullptr);
    }
}


Vector2i
HDRViewScreen::imageToScreen(const Vector2i & pixel) const
{
    if (!currentImage())
        return Vector2i(0,0);

    int xFlipFactor = m_flipH ? currentImage()->width() - 1 : 0;
    int yFlipFactor = m_flipV ? currentImage()->height() - 1 : 0;
    Vector2i sxy((pixel.x() - xFlipFactor) * m_zoomf, (pixel.y() - yFlipFactor) * m_zoomf);
    if (m_flipH)
        sxy.x() = -sxy.x();
    if (m_flipV)
        sxy.y() = -sxy.y();

    sxy += topLeftImageCorner2Screen();
    return sxy;
}

Vector2i HDRViewScreen::screenToImage(const Vector2i & p) const
{
    if (!currentImage())
        return Vector2i(0,0);

    Vector2i xy0 = topLeftImageCorner2Screen();

    Vector2i xy(int(floor((p[0] - xy0.x()) / m_zoomf)),
                int(floor((p[1] - xy0.y()) / m_zoomf)));
    if (m_flipH) xy[0] = currentImage()->width() - 1 - xy[0];
    if (m_flipV) xy[1] = currentImage()->height() - 1 - xy[1];

    return xy;
}
