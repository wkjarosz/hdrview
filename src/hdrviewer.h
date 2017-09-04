/*! \file hdrviewer.h
    \author Wojciech Jarosz
*/
#pragma once

#include <nanogui/nanogui.h>
#include <vector>
#include <iostream>
#include <spdlog/spdlog.h>
#include "gldithertexture.h"
#include "fwd.h"

using namespace nanogui;
using namespace std;
using namespace Eigen;

class HDRViewScreen : public Screen
{
public:
    HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, vector<string> args);
    virtual ~HDRViewScreen();

    void drawContents();
    bool dropEvent(const std::vector<string> &filenames);
    bool keyboardEvent(int key, int scancode, int action, int modifiers);
    void framebufferSizeChanged() {drawAll();}
    bool mouseButtonEvent(const Vector2i &p, int button, bool down, int modifiers);
    bool mouseMotionEvent(const Vector2i &p, const Vector2i &rel, int button, int modifiers);
    bool resizeEvent(const Vector2i &);
    bool scrollEvent(const Vector2i &p, const Vector2f &rel);
    void performLayout();

	void runFilter(const std::function<ImageCommandUndo*(HDRImage & img)> & command);
	bool undo();
	bool redo();
	void flipImage(bool h);
	void saveImage();

	const vector<GLImage*>& images() const {return m_images;}
	GLImage * currentImage();
	const GLImage * currentImage() const;

	void setSelectedLayer(int index);
	void closeImage(int index);
	void clearFocusPath() {mFocusPath.clear();}

private:

	void changeExposure(float newVal);
    void closeCurrentImage();
    void enableDisableButtons();
    void updateCaption();
    void repopulateLayerList();
    void sendLayerBackward();
    void bringLayerForeward();


    void drawPixelGrid(const Matrix4f &mvp) const;
    void drawPixelLabels() const;
    void drawText(const Vector2i & pos,
                  const string & text,
                  const Color & col = Color(1.0f, 1.0f, 1.0f, 1.0f),
                  int fontSize = 10,
                  int fixedWidth = 0) const;
    Vector2i topLeftImageCorner2Screen() const;
    Vector2i screenToImage(const Vector2i & p) const;
    Vector2i imageToScreen(const Vector2i & pixel) const;
    void updateZoomLabel();

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

    Window * m_topPanel = nullptr;
    Button * m_helpButton = nullptr;
    Button * m_layersButton = nullptr;
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


    Window * m_sidePanel = nullptr;
    VScrollPanel * m_sideScrollPanel = nullptr;
    Widget * m_sidePanelContents = nullptr;

    LayersPanel * m_layersPanel = nullptr;
	EditImagePanel * m_editPanel = nullptr;
	HistogramPanel  * m_histogramPanel = nullptr;

    MessageDialog * m_okToQuitDialog = nullptr;

    shared_ptr<spdlog::logger> console;
};
