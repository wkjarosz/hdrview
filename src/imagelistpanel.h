//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/widget.h>
#include <vector>
#include "common.h"
#include "xpuimage.h"
#include "fwd.h"

using namespace nanogui;

class ImageListPanel : public Widget
{
public:
	ImageListPanel(Widget *parent, HDRViewScreen * screen, HDRImageView * imgViewer);

	void draw(NVGcontext *ctx) override;

	bool mouse_button_event(const Vector2i &p, int button, bool down, int modifiers) override;
	bool mouse_motion_event(const Vector2i& p, const Vector2i& rel, int button, int modifiers) override;

	void repopulate_image_list();

	// Const access to the loaded images. Modification only possible via modify_image, undo, redo
	int num_images() const                  {return int(m_images.size());}
	int current_image_index() const         {return m_current;}
	int reference_image_index() const       {return m_reference;}
	ConstImagePtr current_image() const     {return image(m_current);}
	     ImagePtr current_image()           {return image(m_current);}
	ConstImagePtr reference_image() const   {return image(m_reference);}
	     ImagePtr reference_image()         {return image(m_reference);}
	ConstImagePtr image(int index) const;
	     ImagePtr image(int index);

	bool set_current_image_index(int newIndex, bool forceCallback = false);
	bool set_reference_image_index(int newIndex);
	bool swap_current_selected_with_previous() {return is_valid(m_previous) ? set_current_image_index(m_previous) : false;}
	bool move_image_to(int index1, int index2);
	bool send_image_backward();
	bool bring_image_forward();

	// Loading, saving, closing, and rearranging the images in the image stack
	void load_images(const std::vector<std::string> & filenames);
	bool save_image(const std::string & filename, float exposure = 0.f, float gamma = 2.2f,
				   bool sRGB = true, bool dither = true);
	bool close_image();
	void close_all_images();

	// Modify the image data
	void modify_image(const ImageCommand & command);
	void modify_image(const ImageCommandWithProgress & command);
	void undo();
	void redo();

	//
	void run_requested_callbacks();


	void request_buttons_update();
	void request_histogram_update(bool force = false);

	EBlendMode blend_mode() const;
	void set_blend_mode(EBlendMode mode);

	EChannel channel() const;
	void set_channel(EChannel channel);

	bool nth_image_is_visible(int n) const;
	int next_visible_image(int index, EDirection direction) const;
	int nth_visible_image_index(int n) const;

	bool use_regex() const;
	void set_use_regex(bool value);

	bool set_filter(const std::string& filter);
	std::string filter() const;
	void focus_filter();


private:
	void update_buttons();
	void enable_disable_buttons();
	void update_histogram();
	void update_filter();
	bool is_valid(int index) const {return index >= 0 && index < num_images();}

	std::vector<ImagePtr> m_images; ///< The loaded images
	int m_current = -1;             ///< The currently selected image
	int m_reference = -1;           ///< The currently selected reference image

	int m_previous = -1;			///< The previously selected image

	bool m_image_modify_done_requested = false;

	// various callback functions
	std::function<void(int)> m_modify_done_callback;
	std::function<void()> m_num_images_callback;


	HDRViewScreen * m_screen = nullptr;
	HDRImageView * m_image_view = nullptr;
	Button * m_save_btn = nullptr;
	Button * m_close_btn = nullptr;
	Button * m_bring_forward_btn = nullptr;
	Button * m_send_backward_btn = nullptr;
	TextBox * m_filter = nullptr;
	Button* m_erase_btn = nullptr;
	Button* m_regex_btn = nullptr;
	Button * m_use_short_btn = nullptr;
	Widget * m_image_list = nullptr;
	ComboBox * m_blend_modes = nullptr;
	ComboBox * m_channels = nullptr;

	ComboBox * m_xaxis_scale = nullptr,
			 * m_yaxis_scale = nullptr;
	MultiGraph * m_graph = nullptr;
	bool m_histogram_dirty = false;
	bool m_histogram_update_requested = false;
	bool m_update_filter_requested = true;
	bool m_buttons_update_requested = true;
	double m_histogram_request_time;

    
	bool m_dragging_image_btn = false;
    size_t m_dragged_image_btn_id;
    nanogui::Vector2i m_dragging_start_pos;
};