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

	bool loadImage();
	void saveImage();
	void askCloseImage(int index);
	void flipImage(bool h);
	void clearFocusPath() {mFocusPath.clear();}

private:
	void toggleHelpWindow();
	void updateLayout();
    void closeCurrentImage();
    void updateCaption();

	Widget * m_topPanel = nullptr;
	Widget * m_sidePanel = nullptr;
	Widget * m_statusBar = nullptr;
	HDRImageViewer * m_imageView = nullptr;
	HDRImageManager * m_imageMgr;

    Button * m_helpButton = nullptr;
    Button * m_sidePanelButton = nullptr;
	HelpWindow* m_helpWindow = nullptr;
	Label * m_zoomLabel = nullptr;
	Label * m_pixelInfoLabel = nullptr;

	VScrollPanel * m_sideScrollPanel = nullptr;
	Widget * m_sidePanelContents = nullptr;

    MessageDialog * m_okToQuitDialog = nullptr;

    std::shared_ptr<spdlog::logger> console;
};
