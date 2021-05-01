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
#include "imageview.h"
#include <nanogui/texture.h>
#include <nanogui/shader.h>
#include <nanogui/renderpass.h>

using namespace nanogui;
// using namespace Eigen;

class HDRViewScreen : public Screen
{
public:
    HDRViewScreen(float exposure, float gamma, bool sRGB, bool dither, std::vector<std::string> args);
    ~HDRViewScreen() override;

	// overridden virtual functions from Screen
    void draw_contents() override;
    bool drop_event(const std::vector<std::string> &filenames) override;
	bool mouse_button_event(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;
	bool mouse_motion_event(const nanogui::Vector2i& p, const nanogui::Vector2i& rel, int button, int modifiers) override;
	bool keyboard_event(int key, int scancode, int action, int modifiers) override;

	bool load_image();
	void save_image();
	void ask_close_image(int index);
	void ask_close_all_images();
	void flip_image(bool h);
	void clear_focus_path() {m_focus_path.clear();}

	void update_caption();

private:
	void toggle_help_window();
	void update_layout();
	bool at_side_panel_edge(const nanogui::Vector2i& p)
	{
		return p.x() - m_sidePanel->fixed_width() < 10 && p.x() - m_sidePanel->fixed_width() > -5;
	}

	Window * m_topPanel = nullptr;
	Window * m_sidePanel = nullptr;
	Window * m_statusBar = nullptr;
	HDRImageView * image_view = nullptr;
	ImageListPanel * m_imagesPanel = nullptr;

    Button * m_helpButton = nullptr;
    Button * m_sidePanelButton = nullptr;
	HelpWindow* m_helpWindow = nullptr;
	Label * m_zoomLabel = nullptr;
	Label * m_pixelInfoLabel = nullptr;

	VScrollPanel * m_sideScrollPanel = nullptr;
	Widget * m_sidePanelContents = nullptr;

	double m_guiAnimationStart;
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

	using ImageHolder = std::shared_ptr<HDRImage>;
    std::vector<std::pair<ref<Texture>, ImageHolder>> m_images;

};
