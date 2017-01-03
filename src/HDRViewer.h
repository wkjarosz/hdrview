/*! \file HDRViewer.h
    \author Wojciech Jarosz
*/
#pragma once

#include <nanogui/nanogui.h>
#include <vector>
#include <iostream>
#include "ImageQuad.h"
#include "FullScreenDitherer.h"


using namespace nanogui;
using namespace std;
using namespace Eigen;

class HDRViewScreen : public Screen
{
public:
    HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args);
    virtual ~HDRViewScreen();

    void drawContents();
    bool dropEvent(const std::vector<std::string> &filenames);
    bool keyboardEvent(int key, int scancode, int action, int modifiers);
    void framebufferSizeChanged() {drawAll();}
    bool mouseButtonEvent(const Vector2i &p, int button, bool down, int modifiers);
    bool mouseMotionEvent(const Vector2i &p, const Vector2i &rel, int button, int modifiers);
    bool resizeEvent(const Vector2i &);
    bool scrollEvent(const Vector2i &p, const Vector2f &rel);
    void performLayout();

private:

    ImageQuad * currentImage();
    const ImageQuad * currentImage() const;

    void closeCurrentImage();
    void closeImage(int index);

    void updateCaption();

    void repopulateLayerList();
    void setSelectedLayer(int index);


    Vector2i topLeftImageCorner2Screen() const
    {
        if (!currentImage())
            return Vector2i(0,0);

        return Vector2i(int(m_imagePan[0] * m_zoomf) + int(-currentImage()->size()[0] / 2.0 * m_zoomf) + int(mFBSize[0] / 2.0f / mPixelRatio),
                        int(m_imagePan[1] * m_zoomf) + int(-currentImage()->size()[1] / 2.0 * m_zoomf) + int(mFBSize[1] / 2.0f / mPixelRatio));
    }

    void drawGrid(const Matrix4f & mvp) const;
    void drawPixelLabels() const;
    void drawText(const Vector2i & pos,
                  const std::string & text,
                  const Color & col = Color(1.0f, 1.0f, 1.0f, 1.0f),
                  int fontSize = 10,
                  int fixedWidth = 0) const;
    Vector2i screenToImage(const Vector2i & p) const;
    Vector2i imageToScreen(const Vector2i & pixel) const;
    void updateZoomLabel();

    int m_GUIScaleFactor = 1;

    FullScreenDitherer m_ditherer;
    vector<ImageQuad*> m_images;
    int m_current = -1;

    float m_exposure = 0.f;
    float m_gamma = 2.2f;
    Vector3f m_channels = Vector3f(1.0f, 1.0f, 1.0f);

    Vector2f m_imagePan = Vector2f::Zero();
    int m_zoom = 0;
    float m_zoomf = 1.0f;
    bool m_flipH = false, m_flipV = false, m_drag = false;

    Window * m_controlPanel = nullptr;
    Button * m_helpButton = nullptr;
    Button * m_layersButton = nullptr;
    Button * m_saveButton = nullptr;
    Window * m_layersPanel = nullptr;
    VScrollPanel * m_layerScrollPanel = nullptr;
    Widget * m_vscrollContainer = nullptr;
    Widget * m_layerListWidget = nullptr;
    Window * m_helpDialog = nullptr;
    FloatBox<float> * m_exposureTextBox = nullptr;
    Slider * m_exposureSlider = nullptr;
    Label * m_gammaLabel = nullptr;
    FloatBox<float> * m_gammaTextBox = nullptr;
    Slider * m_gammaSlider = nullptr;
    CheckBox * m_sRGB = nullptr;
    CheckBox * m_dither = nullptr;
    CheckBox * m_drawGrid = nullptr;
    CheckBox * m_drawValues = nullptr;
    Window * m_statusBar = nullptr;
    Label * m_zoomLabel = nullptr;
    Label * m_pixelInfoLabel = nullptr;
    MessageDialog * m_okToQuitDialog = nullptr;

    vector<Button*> m_layerButtons;
};
