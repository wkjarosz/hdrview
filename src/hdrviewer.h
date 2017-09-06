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
#include "commandhistory.h"

using namespace nanogui;
using namespace Eigen;

class HDRViewScreen : public Screen
{
public:
    HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, std::vector<std::string> args);
    ~HDRViewScreen() override;

	// overridden virtual functions from Screen
    void drawContents() override;
    bool dropEvent(const std::vector<std::string> &filenames) override;
    bool keyboardEvent(int key, int scancode, int action, int modifiers) override;
    bool mouseButtonEvent(const Vector2i &p, int button, bool down, int modifiers) override;
    bool mouseMotionEvent(const Vector2i &p, const Vector2i &rel, int button, int modifiers) override;
    bool resizeEvent(const Vector2i &) override;
    bool scrollEvent(const Vector2i &p, const Vector2f &rel) override;
    void performLayout() override;

	void flipImage(bool h);
	void clearFocusPath() {mFocusPath.clear();}

private:

    void closeCurrentImage();
    void updateCaption();

    void updateZoomLabel();

    bool m_drag = false;

	Window * m_topPanel = nullptr;
	Window * m_sidePanel = nullptr;
	Window * m_statusBar = nullptr;
	HDRImageViewer * m_imageView = nullptr;

    Button * m_helpButton = nullptr;
    Button * m_layersButton = nullptr;
	Window * m_helpDialog = nullptr;
	Label * m_zoomLabel = nullptr;
	Label * m_pixelInfoLabel = nullptr;

	VScrollPanel * m_sideScrollPanel = nullptr;
	Widget * m_sidePanelContents = nullptr;
    LayersPanel * m_layersPanel = nullptr;
	EditImagePanel * m_editPanel = nullptr;
	HistogramPanel  * m_histogramPanel = nullptr;

    MessageDialog * m_okToQuitDialog = nullptr;

    std::shared_ptr<spdlog::logger> console;
};
