//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/nanogui.h>
#include <vector>
#include <iostream>
#include <spdlog/spdlog.h>
#include "fwd.h"
#include "commandhistory.h"
#include "timer.h"

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
	bool mouseButtonEvent(const Eigen::Vector2i &p, int button, bool down, int modifiers) override;
	bool mouseMotionEvent(const Eigen::Vector2i& p, const Eigen::Vector2i& rel, int button, int modifiers) override;
	bool keyboardEvent(int key, int scancode, int action, int modifiers) override;

	bool loadImage();
	void saveImage();
	void askCloseImage(int index);
	void flipImage(bool h);
	void clearFocusPath() {mFocusPath.clear();}

	int modifiers() const {return mModifiers;}

private:
	void toggleHelpWindow();
	void updateLayout();
    void closeCurrentImage();
    void updateCaption();
	bool atSidePanelEdge(const Eigen::Vector2i& p)
	{
		return p.x() - m_sidePanel->fixedWidth() < 0 && p.x() - m_sidePanel->fixedWidth() > -5;
	}

	Window * m_topPanel = nullptr;
	Window * m_sidePanel = nullptr;
	Window * m_statusBar = nullptr;
	HDRImageViewer * m_imageView = nullptr;
	HDRImageManager * m_imageMgr;
	ImageListPanel * m_imagesPanel = nullptr;

    Button * m_helpButton = nullptr;
    Button * m_sidePanelButton = nullptr;
	HelpWindow* m_helpWindow = nullptr;
	Label * m_zoomLabel = nullptr;
	Label * m_pixelInfoLabel = nullptr;

	VScrollPanel * m_sideScrollPanel = nullptr;
	Widget * m_sidePanelContents = nullptr;

	Timer m_guiTimer;
	bool m_guiTimerRunning = false;
	enum EAnimationGoal : int
	{
		TOP_PANEL       = 1 << 0,
		SIDE_PANEL      = 1 << 1,
		BOTTOM_PANEL    = 1 << 2,
	} m_animationGoal = EAnimationGoal(TOP_PANEL|SIDE_PANEL|BOTTOM_PANEL);

    MessageDialog * m_okToQuitDialog = nullptr;

	bool m_draggingSidePanel = false;

    std::shared_ptr<spdlog::logger> console;
};
