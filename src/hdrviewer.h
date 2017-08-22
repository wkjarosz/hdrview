/*! \file hdrviewer.h
    \author Wojciech Jarosz
*/
#pragma once

#include <nanogui/nanogui.h>
#include <vector>
#include <iostream>
#include <spdlog/spdlog.h>
#include "glimage.h"
#include "gldithertexture.h"


using namespace nanogui;
using namespace std;
using namespace Eigen;
namespace spd = spdlog;


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

    GLImage * currentImage();
    const GLImage * currentImage() const;

    void closeCurrentImage();
    void closeImage(int index);
    void enableDisableButtons();
    void updateCaption();
    void repopulateLayerList();
    void setSelectedLayer(int index);
    void sendLayerBackward();
    void bringLayerForeward();

    void flipImage(bool h);

    Button * createGaussianFilterButton(Widget * parent);
    Button * createFastGaussianFilterButton(Widget * parent);
    Button * createBoxFilterButton(Widget * parent);
    Button * createBilateralFilterButton(Widget * parent);
    Button * createUnsharpMaskFilterButton(Widget * parent);
    Button * createMedianFilterButton(Widget * parent);
    Button * createResizeButton(Widget * parent);


    void drawGrid(const Matrix4f & mvp) const;
    void drawPixelLabels() const;
    void drawText(const Vector2i & pos,
                  const std::string & text,
                  const Color & col = Color(1.0f, 1.0f, 1.0f, 1.0f),
                  int fontSize = 10,
                  int fixedWidth = 0) const;
    Vector2i topLeftImageCorner2Screen() const;
    Vector2i screenToImage(const Vector2i & p) const;
    Vector2i imageToScreen(const Vector2i & pixel) const;
    void updateZoomLabel();

    int m_GUIScaleFactor = 1;

    GLDitherTexture m_ditherer;
    vector<GLImage*> m_images;
    int m_current = -1;

    float m_exposure = 0.f;
    float m_gamma = 2.2f;
    Vector3f m_channels = Vector3f(1.0f, 1.0f, 1.0f);

    Vector2f m_imagePan = Vector2f::Zero();
    int m_zoom = 0;
    float m_zoomf = 1.0f;
    bool m_drag = false;

    Window * m_controlPanel = nullptr;
    Button * m_helpButton = nullptr;
    Button * m_layersButton = nullptr;
    Button * m_saveButton = nullptr;
    Button * m_undoButton = nullptr;
    Button * m_redoButton = nullptr;
    Window * m_sidePanel = nullptr;
    VScrollPanel * m_sideScrollPanel = nullptr;
    Widget * m_sidePanelContents = nullptr;
    Widget * m_layersPanelContents = nullptr;
    Widget * m_layerListWidget = nullptr;
    Widget * m_editPanelContents = nullptr;
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
    vector<Button*> m_filterButtons;
    vector<Button*> m_layerButtons;

    std::shared_ptr<spdlog::logger> console;
};
