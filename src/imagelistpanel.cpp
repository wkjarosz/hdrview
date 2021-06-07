//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#if defined(_WIN32)
#  ifndef NOMINMAX
#  define NOMINMAX 1
#  endif
#endif
#include <GLFW/glfw3.h>

#include "imagelistpanel.h"
#include "hdrviewscreen.h"
#include "imagebutton.h"
#include "hdrimageview.h"
#include "multigraph.h"
#include "colorslider.h"
#include "well.h"
#include "timer.h"
#include "xpuimage.h"
#include <tinydir.h>
#include <set>
#include <spdlog/spdlog.h>
#include <alphanum.h>

using namespace nanogui;
using namespace std;

ImageListPanel::ImageListPanel(Widget *parent, HDRViewScreen * screen, HDRImageView * img_view)
	: Well(parent, 1, Color(150, 32), Color(0, 50)),
      m_screen(screen),
      m_image_view(img_view),
	  m_modify_done_callback([](int){}),
	  m_num_images_callback([](){})
{
	// set_id("image list panel");
	set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 10, 5));

	// histogram mode selection GUI elements
	{
		auto grid = new Widget(this);
		grid->set_layout(new GridLayout(Orientation::Horizontal, 3, Alignment::Fill, 0, 2));

		new Label(grid, "Histogram:", "sans", 14);

		m_yaxis_scale = new ComboBox(grid, {"Linear", "Log"});
		m_yaxis_scale->set_tooltip("Set the scale for the Y axis.");
		m_yaxis_scale->set_fixed_height(19);

		m_xaxis_scale = new ComboBox(grid, {"Linear", "sRGB", "Log"});
		m_xaxis_scale->set_tooltip("Set the scale for the X axis.");
		m_xaxis_scale->set_fixed_height(19);

		m_xaxis_scale->set_selected_index(1);
		m_yaxis_scale->set_selected_index(0);
		m_xaxis_scale->set_callback([this](int) { update_histogram(); });
		m_yaxis_scale->set_callback([this](int) { update_histogram(); });
	}

	// histogram and file buttons
	{
		auto row = new Widget(this);
		row->set_layout(new BoxLayout(Orientation::Vertical,
		                             Alignment::Fill, 0, 4));
		m_graph = new MultiGraph(row, Color(255, 0, 0, 200));
		m_graph->add_plot(Color(0, 255, 0, 200));
		m_graph->add_plot(Color(0, 0, 255, 200));

		row = new Widget(this);
		row->set_layout(new GridLayout(Orientation::Horizontal, 5, Alignment::Fill, 0, 2));

		auto b = new Button(row, "", FA_FOLDER_OPEN);
		b->set_fixed_height(25);
		b->set_tooltip("Load an image.");
		b->set_callback([this] { m_screen->load_image(); });

		b = new Button(row, "", FA_FILE);
		b->set_fixed_height(25);
		b->set_tooltip("Create a new image.");
		// b->set_callback([this] { m_screen->load_image(); });

		b = new Button(row, "", FA_CLONE);
		b->set_fixed_height(25);
		b->set_tooltip("Duplicate current image.");
		// b->set_callback([this] { m_screen->load_image(); });

		m_save_btn = new Button(row, "", FA_SAVE);
		m_save_btn->set_enabled(current_image() != nullptr);
		m_save_btn->set_fixed_height(25);
		m_save_btn->set_tooltip("Save the image to disk.");
		m_save_btn->set_callback([this] { m_screen->save_image(); });

		m_close_btn = new Button(row, "", FA_TIMES_CIRCLE);
		m_close_btn->set_fixed_height(25);
		m_close_btn->set_tooltip("Close image");
		m_close_btn->set_callback([this] { m_screen->ask_close_image(current_image_index()); });
	}

	// channel and blend mode GUI elements
	{
		auto grid = new Widget(this);
		auto agl = new AdvancedGridLayout({0, 4, 0});
		grid->set_layout(agl);
		agl->set_col_stretch(2, 1.0f);

		agl->append_row(0);
		agl->set_anchor(new Label(grid, "Mode:", "sans", 14),
		               AdvancedGridLayout::Anchor(0, agl->row_count() - 1, Alignment::Fill, Alignment::Fill));

		m_blend_modes = new ComboBox(grid, blendModeNames());
		m_blend_modes->set_fixed_height(19);
		m_blend_modes->set_callback([img_view](int b) { img_view->set_blend_mode(EBlendMode(b)); });
		agl->set_anchor(m_blend_modes,
		               AdvancedGridLayout::Anchor(2, agl->row_count() - 1, Alignment::Fill, Alignment::Fill));

		agl->append_row(4);  // spacing
		agl->append_row(0);

		agl->set_anchor(new Label(grid, "Channel:", "sans", 14),
		               AdvancedGridLayout::Anchor(0, agl->row_count() - 1, Alignment::Fill, Alignment::Fill));

		m_channels = new ComboBox(grid, channelNames());
		m_channels->set_fixed_height(19);
		set_channel(EChannel::RGB);
		m_channels->set_callback([img_view](int c) { img_view->set_channel(EChannel(c)); });
		agl->set_anchor(m_channels,
		               AdvancedGridLayout::Anchor(2, agl->row_count() - 1, Alignment::Fill, Alignment::Fill));
	}

	// filter/search of open images GUI elements
	{
		auto grid = new Widget(this);
		auto agl = new AdvancedGridLayout({0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0});
		grid->set_layout(agl);
		agl->set_col_stretch(0, 1.0f);

		agl->append_row(0);

		m_filter = new TextBox(grid, "");
		m_erase_btn = new Button(grid, "", FA_BACKSPACE);
		m_regex_btn = new Button(grid, ".*");
		m_align_btn = new Button(grid, "", FA_ALIGN_LEFT);
		m_sort_btn = new Button(grid, "", FA_SORT_ALPHA_DOWN);
		m_use_short_btn = new Button(grid, "", FA_HIGHLIGHTER);

		m_filter->set_editable(true);
		m_filter->set_alignment(TextBox::Alignment::Left);
		m_filter->set_callback([this](const string& filter){ return set_filter(filter); });

		m_filter->set_placeholder("Find");
		m_filter->set_tooltip("Filter open image list so that only images with a filename containing the search string will be visible.");

		agl->set_anchor(m_filter,
		               AdvancedGridLayout::Anchor(0, agl->row_count() - 1, Alignment::Fill, Alignment::Fill));


		m_erase_btn->set_fixed_size({19, 19});
		m_erase_btn->set_tooltip("Clear the search string.");
		m_erase_btn->set_change_callback([this](bool b){ set_filter(""); });
		agl->set_anchor(m_erase_btn,
		               AdvancedGridLayout::Anchor(2, agl->row_count() - 1, Alignment::Minimum, Alignment::Fill));


		m_regex_btn->set_fixed_size({19, 19});
		m_regex_btn->set_tooltip("Treat search string as a regular expression.");
		m_regex_btn->set_flags(Button::ToggleButton);
		m_regex_btn->set_pushed(false);
		m_regex_btn->set_change_callback([this](bool b){ set_use_regex(b); });
		agl->set_anchor(m_regex_btn,
		               AdvancedGridLayout::Anchor(4, agl->row_count() - 1, Alignment::Minimum, Alignment::Fill));


		m_align_btn->set_fixed_size({19, 19});
		m_align_btn->set_tooltip("Toggle aligning filenames left vs. right.");
		m_align_btn->set_callback([this]()
			{
				m_align_btn->set_icon(m_align_left ? FA_ALIGN_LEFT : FA_ALIGN_RIGHT);
				m_align_left = !m_align_left;

				// now set alignment on all buttons
				auto& buttons = m_image_list->children();
				for (int i = 0; i < num_images(); ++i)
				{
					auto img = image(i);
					auto btn = dynamic_cast<ImageButton*>(buttons[i]);

					btn->set_alignment(m_align_left ? ImageButton::Alignment::Left : ImageButton::Alignment::Right);
				}
			});
		agl->set_anchor(m_align_btn,
		               AdvancedGridLayout::Anchor(6, agl->row_count() - 1, Alignment::Minimum, Alignment::Fill));


		m_sort_btn->set_fixed_size({19, 19});
		m_sort_btn->set_tooltip("Sort the image list. Cycles through 4 sorting modes:\n\n"
										 "Alphabetic increasing.\n"
										 "Alphabetic decreasing.\n"
										 "Image size decreasing.\n"
										 "Image size increasing.\n\n"
										 "When image names are aligned right, alphabetic sorting sorts by reversed filename (useful for sorting files with same extension together).");
		m_sort_btn->set_callback([this]
			{
				sort_images();
				if (m_sort_btn->icon() == FA_SORT_ALPHA_DOWN)
					m_sort_btn->set_icon(FA_SORT_ALPHA_DOWN_ALT);
				else if (m_sort_btn->icon() == FA_SORT_ALPHA_DOWN_ALT)
					m_sort_btn->set_icon(FA_SORT_AMOUNT_DOWN);
				else if (m_sort_btn->icon() == FA_SORT_AMOUNT_DOWN)
					m_sort_btn->set_icon(FA_SORT_AMOUNT_DOWN_ALT);
				else if (m_sort_btn->icon() == FA_SORT_AMOUNT_DOWN_ALT)
					m_sort_btn->set_icon(FA_SORT_ALPHA_DOWN);
			});
		agl->set_anchor(m_sort_btn,
		               AdvancedGridLayout::Anchor(8, agl->row_count() - 1, Alignment::Minimum, Alignment::Fill));


		m_use_short_btn->set_fixed_size({19, 19});
		m_use_short_btn->set_tooltip("Toggle showing full filenames vs. only the unique portion of each filename.");
		m_use_short_btn->set_flags(Button::ToggleButton);
		m_use_short_btn->set_pushed(false);
		m_use_short_btn->set_change_callback([this](bool b){ m_update_filter_requested = true; });
		agl->set_anchor(m_use_short_btn,
		               AdvancedGridLayout::Anchor(10, agl->row_count() - 1, Alignment::Minimum, Alignment::Fill));

	}

	m_num_images_callback =
		[this](void)
		{
			m_screen->update_caption();
			repopulate_image_list();
			set_reference_image_index(-1);
		};

	m_modify_done_callback =
		[this](int i)
		{
			m_screen->update_caption();
			request_buttons_update();
			set_filter(filter());
            request_histogram_update();
			m_screen->redraw();
			m_image_modify_done_requested = true;
		};
}

EBlendMode ImageListPanel::blend_mode() const
{
	return EBlendMode(m_blend_modes->selected_index());
}

void ImageListPanel::set_blend_mode(EBlendMode mode)
{
	m_blend_modes->set_selected_index(mode);
	m_image_view->set_blend_mode(mode);
}

EChannel ImageListPanel::channel() const
{
	return EChannel(m_channels->selected_index());
}

void ImageListPanel::set_channel(EChannel channel)
{
	m_channels->set_selected_index(channel);
	m_image_view->set_channel(channel);
}

void ImageListPanel::focus_filter() {m_filter->request_focus();}


void ImageListPanel::repopulate_image_list()
{
	// this currently just clears all the widgets and recreates all of them
	// from scratch. this doesn't scale, but should be fine unless you have a
	// lot of images, and makes the logic a lot simpler.

	// prevent crash when the focus path includes any of the widgets we are destroying
	m_screen->clear_focus_path();

	// clear everything
	if (m_image_list)
		remove_child(m_image_list);

	m_image_list = new Well(this);
	m_image_list->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 1));
	
	for (int i = 0; i < num_images(); ++i)
	{
		auto btn = new ImageButton(m_image_list, image(i)->filename());
		btn->set_image_id(i+1);
		btn->set_selected_callback([&](int j){set_current_image_index(nth_visible_image_index(j));});
		btn->set_reference_callback([&](int j){set_reference_image_index(nth_visible_image_index(j));});
	}

    update_buttons();

	update_filter();

	m_screen->perform_layout();
}

void ImageListPanel::update_buttons()
{
	auto& buttons = m_image_list->children();
    for (int i = 0; i < num_images(); ++i)
    {
        auto img = image(i);
        auto btn = dynamic_cast<ImageButton*>(buttons[i]);

        btn->set_is_selected(i == m_current);
        btn->set_is_reference(i == m_reference);
        btn->set_caption(img->filename());
        btn->set_is_modified(img->is_modified());
        btn->set_progress(img->progress());
        btn->set_tooltip(
                fmt::format("Path: {}\n\nResolution: ({}, {})", img->filename(), img->width(), img->height()));
    }

    m_histogram_update_requested = true;
//    update_histogram();

    m_buttons_update_requested = false;
}

void ImageListPanel::enable_disable_buttons()
{
	bool has_image = current_image() != nullptr;
	bool has_valid_image = has_image && !current_image()->is_null();
	m_save_btn->set_enabled(has_valid_image);
	m_close_btn->set_enabled(has_image);
}

void ImageListPanel::sort_images()
{
	auto compare_filename = [&](int i, int j)
	{
		bool decreasing = m_sort_btn->icon() == FA_SORT_ALPHA_DOWN_ALT || m_sort_btn->icon() == FA_SORT_AMOUNT_DOWN_ALT;
		bool by_size = m_sort_btn->icon() == FA_SORT_AMOUNT_DOWN || m_sort_btn->icon() == FA_SORT_AMOUNT_DOWN_ALT;
		
		if (by_size)
		{
			int i_size = image(i)->width() * image(i)->height();
			int j_size = image(j)->width() * image(j)->height();

			if (decreasing)
				return i_size > j_size;
			else
				return i_size < j_size;
		}
		else
		{
			string i_name = image(i)->filename();
			string j_name = image(j)->filename();
			if (m_align_left)
			{
				if (decreasing)
					return doj::alphanum_comp(i_name, j_name) < 0;
				else
					return doj::alphanum_comp(j_name, i_name) < 0;
			}
			else
			{
    			std::string rev_i_name = string(i_name.rbegin(), i_name.rend());
				std::string rev_j_name = string(j_name.rbegin(), j_name.rend());
				if (decreasing)
					return doj::alphanum_comp(rev_i_name, rev_j_name) < 0;
				else
					return doj::alphanum_comp(rev_j_name, rev_i_name) < 0;
			}
		}
	};

	auto find_index_of_max = [&](int end)
	{
		int max_index_so_far = 0;

		for (int i = 0; i <= end; ++i)
			if (compare_filename(i, max_index_so_far))
				max_index_so_far = i;

		return max_index_so_far;
	};

	// selection sort
	for (int end = num_images()-1; end >= 0; --end)
	{
        int mx = find_index_of_max(end);
        // swap the item at index mx with the item at index end
		swap_images(mx, end);
	}

	m_screen->request_layout_update();
}

bool ImageListPanel::swap_images(int old_index, int new_index)
{
 	if (old_index == new_index || !is_valid(old_index) || !is_valid(new_index))
		// invalid image indices and/or do nothing
        return false;

	auto old_btn = dynamic_cast<ImageButton*>(m_image_list->child_at(old_index));
	auto new_btn = dynamic_cast<ImageButton*>(m_image_list->child_at(new_index));

	old_btn->inc_ref();
	new_btn->inc_ref();
	
	// swap the buttons' image ids
	auto tmp = old_btn->image_id();
	old_btn->set_image_id(new_btn->image_id());
	new_btn->set_image_id(tmp);

	// now swap them in their parent's lists
	m_image_list->remove_child(old_btn);
	m_image_list->remove_child(new_btn);

	if (old_index < new_index)
	{
		m_image_list->add_child(old_index, new_btn);
		m_image_list->add_child(new_index, old_btn);
	}
	else
	{
		m_image_list->add_child(new_index, old_btn);
		m_image_list->add_child(old_index, new_btn);
	}

	old_btn->dec_ref();
	new_btn->dec_ref();

	// swap the images
	std::swap(m_images[old_index], m_images[new_index]);


	// with a simple swap, none of the other imagebuttons are affected

	return true;
}


bool ImageListPanel::move_image_to(int old_index, int new_index)
{
 	if (old_index == new_index || !is_valid(old_index) || !is_valid(new_index))
		// invalid image indices and/or do nothing
        return false;

    auto* button = m_image_list->child_at(old_index);
    button->inc_ref();
    m_image_list->remove_child_at(old_index);
    m_image_list->add_child(new_index, button);
    button->dec_ref();

	// update all button image ids in between 
    int start_i = std::min(old_index, new_index);
    int end_i = std::max(old_index, new_index);
	
	// compute visible index of first image
	int visible_i = 0;
	for (int i = 0; i < start_i; ++i)
		if (nth_image_is_visible(i))
			visible_i++;

    for (int i = start_i; i <= end_i; ++i)
	{
        auto* b = dynamic_cast<ImageButton*>(m_image_list->child_at(i));
		if (nth_image_is_visible(i))
        	b->set_image_id(++visible_i);
    }

	// helper function to update an image index from before to after the image move
	auto update_index = [old_index, new_index](int i)
		{
			if (i == old_index)
				i = new_index;
			else if (old_index < new_index)
			{
				if (i > old_index && i <= new_index)
					i -= 1;
			}
			else if (old_index > new_index)
			{
				if (i < old_index && i >= new_index)
					i += 1;
			}
			return i;
		};

	m_current = update_index(m_current);
	m_reference = update_index(m_reference);

	// now move the actual image
    auto img = m_images[old_index];
    m_images.erase(m_images.begin() + old_index);
    m_images.insert(m_images.begin() + new_index, img);
    
    // requestLayoutUpdate();
	return true;
}

bool ImageListPanel::bring_image_forward()
{
	int curr = current_image_index();
	int next = next_visible_image(curr, Forward);

	if (!move_image_to(curr, next))
		return false;

	return true;
}


bool ImageListPanel::send_image_backward()
{
	int curr = current_image_index();
	int next = next_visible_image(curr, Backward);

	if (!move_image_to(curr, next))
		return false;

	return true;
}


bool ImageListPanel::mouse_button_event(const nanogui::Vector2i &p, int button, bool down, int modifiers)
{
	// check if we are trying to drag an image button
    if (down)
	{
		auto w = find_widget(p);
		int i = m_image_list->child_index(w);
		if (i >= 0)
		{
			const auto* btn = dynamic_cast<ImageButton*>(w);
			m_dragged_image_btn_id = i;
			m_dragging_image_btn = true;
			m_dragging_start_pos = p - btn->position();
		}
    }

    if (Widget::mouse_button_event(p, button, down, modifiers))
        return true;

    if (!down)
	{
        m_dragging_image_btn = false;
		m_screen->request_layout_update();
	}

    return false;
}

bool ImageListPanel::mouse_motion_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int modifiers)
{
	if (Widget::mouse_motion_event(p, rel, button, modifiers))
        return true;

	if (m_dragging_image_btn)
	{
        auto& buttons = m_image_list->children();
		auto w = find_widget(p);
		int i = m_image_list->child_index(w);
		if (i >= 0)
		{
			auto* btn = dynamic_cast<ImageButton*>(w);
			auto pos = btn->position();
			pos.y() += ((int)m_dragged_image_btn_id - (int)i) * btn->size().y();
			btn->set_position(pos);
			btn->mouse_enter_event(p, false);

			move_image_to(m_dragged_image_btn_id, i);
			m_dragged_image_btn_id = i;
		}
    
        dynamic_cast<ImageButton*>(buttons[m_dragged_image_btn_id])->set_position(p - m_dragging_start_pos);
		m_screen->request_layout_update();
    }

    return false;
}

void ImageListPanel::draw(NVGcontext *ctx)
{
	if (m_buttons_update_requested)
		update_buttons();

	// if it has been more than 2 seconds since we requested a histogram update, then update it
	if (m_histogram_update_requested &&
		(glfwGetTime() - m_histogram_request_time) > 1.0)
		update_histogram();

	if (m_update_filter_requested)
		update_filter();

	if (m_histogram_dirty &&
		current_image() &&
		!current_image()->is_null() &&
		current_image()->histograms() &&
		current_image()->histograms()->ready() &&
		current_image()->histograms()->get())
	{
		auto lazyHist = current_image()->histograms();
		int idx = m_xaxis_scale->selected_index();
		int idxY = m_yaxis_scale->selected_index();
		vector<float> hist[3];
		hist[0] = lazyHist->get()->histogram[idx].values[0];
		hist[1] = lazyHist->get()->histogram[idx].values[1];
		hist[2] = lazyHist->get()->histogram[idx].values[2];
		auto ticks = lazyHist->get()->histogram[idx].xTicks;
		auto labels = lazyHist->get()->histogram[idx].xTickLabels;
		
		if (idxY != 0)
			for (int c = 0; c < 3; ++c)
				for_each(hist[c].begin(), hist[c].end(), [](float & v){v = normalizedLogScale(v);});
		
		m_graph->set_values(hist[0], 0);
		m_graph->set_values(hist[1], 1);
		m_graph->set_values(hist[2], 2);
		m_graph->set_xticks(ticks, labels);

		auto yTicks = linspaced(9, 0.0f, 1.0f);
		if (idxY != 0)
			for_each(yTicks.begin(), yTicks.end(), [](float & v){v = normalizedLogScale(v);});
		m_graph->set_yticks(yTicks);

		float gain = pow(2.f, m_image_view->exposure());
		m_graph->set_left_header(fmt::format("{:.3f}", lazyHist->get()->minimum * gain));
		m_graph->set_center_header(fmt::format("{:.3f}", lazyHist->get()->average * gain));
		m_graph->set_right_header(fmt::format("{:.3f}", lazyHist->get()->maximum * gain));
		m_histogram_dirty = false;
	}
	enable_disable_buttons();

	if (num_images() != (int)m_image_list->children().size())
		spdlog::error("Number of buttons and images don't match!");
	else
	{
		auto& buttons = m_image_list->children();
		for (int i = 0; i < num_images(); ++i)
		{
			auto img = image(i);
			auto btn = dynamic_cast<ImageButton*>(buttons[i]);
			btn->set_progress(img->progress());
			btn->set_is_modified(img->is_modified());
		}
	}

	Well::draw(ctx);
}


void ImageListPanel::update_histogram()
{
	m_histogram_dirty = true;

	if (current_image())
		current_image()->recompute_histograms(m_image_view->exposure());
	else
    {
        m_graph->set_values(std::vector<float>(), 0);
        m_graph->set_values(std::vector<float>(), 1);
        m_graph->set_values(std::vector<float>(), 2);

        m_graph->set_left_header("");
        m_graph->set_center_header("");
        m_graph->set_right_header("");

        m_graph->set_xticks(std::vector<float>(), {});
        m_graph->set_yticks(std::vector<float>());
    }

	m_histogram_update_requested = false;
	m_histogram_request_time = glfwGetTime();
}

void ImageListPanel::request_histogram_update(bool force)
{
	if (force)
		update_histogram();
	else// if (!m_histogram_update_requested)
	{
		// if no histogram update is pending, then queue one up, and start the timer
		m_histogram_update_requested = true;
		m_histogram_request_time = glfwGetTime();
	}
}

void ImageListPanel::request_buttons_update()
{
	// if no button update is pending, then queue one up, and start the timer
	m_buttons_update_requested = true;
}


void ImageListPanel::run_requested_callbacks()
{
	if (m_image_modify_done_requested)
	{
		spdlog::trace("running requested callbacks");
		// remove any images that are not being modified and are null
		bool num_images_changed = false;

		// iterate through the images, and remove the ones that didn't load properly
		auto it = m_images.begin();
		while (it != m_images.end())
		{
			int i = it - m_images.begin();
			auto img = m_images[i];
			if (img && img->can_modify() && img->is_null())
			{
				it = m_images.erase(it);

				if (i < m_current)
					m_current--;
				else if (m_current >= int(m_images.size()))
					m_current = m_images.size() - 1;

				num_images_changed = true;
			}
			else
				++it;
		}

		m_image_view->set_current_image(current_image());

		if (num_images_changed)
		{
			m_screen->update_caption();

			m_num_images_callback();
		}

		m_modify_done_callback(m_current);	// TODO: make this use the modified image
		
		m_image_modify_done_requested = false;
	}
}

shared_ptr<const XPUImage> ImageListPanel::image(int index) const
{
	return is_valid(index) ? m_images[index] : nullptr;
}

shared_ptr<XPUImage> ImageListPanel::image(int index)
{
	return is_valid(index) ? m_images[index] : nullptr;
}

bool ImageListPanel::set_current_image_index(int index, bool forceCallback)
{
	if (index == m_current && !forceCallback)
		return false;

	auto& buttons = m_image_list->children();
	if (is_valid(m_current))
		dynamic_cast<ImageButton*>(buttons[m_current])->set_is_selected(false);
	// for (size_t i = 0; i < buttons.size(); ++i)
	// 	dynamic_cast<ImageButton*>(buttons[i])->set_is_selected(false);
	if (is_valid(index))
		dynamic_cast<ImageButton*>(buttons[index])->set_is_selected(true);

	m_previous = m_current;
	m_current = index;
	m_image_view->set_current_image(current_image());
	m_screen->update_caption();
    update_histogram();

	return true;
}

bool ImageListPanel::set_reference_image_index(int index)
{
	if (index == m_reference)
		return false;

	auto& buttons = m_image_list->children();
	if (is_valid(m_reference))
		dynamic_cast<ImageButton*>(buttons[m_reference])->set_is_reference(false);
	if (is_valid(index))
		dynamic_cast<ImageButton*>(buttons[index])->set_is_reference(true);

	m_reference = index;
	m_image_view->set_reference_image(reference_image());

	return true;
}

void ImageListPanel::load_images(const vector<string> & filenames)
{
	vector<string> all_filenames;

	const static set<string> extensions = {"exr", "png", "jpg", "jpeg", "hdr", "pic", "pfm", "ppm", "bmp", "tga", "psd"};

	// first just assemble all the images we will need to load by traversing any directories
	for (auto i : filenames)
	{
		tinydir_dir dir;
		if (tinydir_open(&dir, i.c_str()) != -1)
		{
			try
			{
				// filename is actually a directory, traverse it
				spdlog::info("Loading images in \"{}\"...", dir.path);
				while (dir.has_next)
				{
					tinydir_file file;
					if (tinydir_readfile(&dir, &file) == -1)
						throw runtime_error("Error getting file");

					if (!file.is_reg)
					{
						if (tinydir_next(&dir) == -1)
							throw runtime_error("Error getting next file");
						continue;
					}

					// only consider image files we support
					string ext = file.extension;
					transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
					if (!extensions.count(ext))
					{
						if (tinydir_next(&dir) == -1)
							throw runtime_error("Error getting next file");
						continue;
					}

					all_filenames.push_back(file.path);

					if (tinydir_next(&dir) == -1)
						throw runtime_error("Error getting next file");
				}

				tinydir_close(&dir);
			}
			catch (const exception & e)
			{
				spdlog::error("Error listing directory: ({}).", e.what());
			}
		}
		else
		{
			all_filenames.push_back(i);
		}
		tinydir_close(&dir);
	}

	// now start a bunch of asynchronous image loads
	for (auto filename : all_filenames)
	{
		shared_ptr<XPUImage> image = make_shared<XPUImage>();
		image->set_modify_done_callback([this](){m_screen->pop_gui_refresh(); m_image_modify_done_requested = true;});
		image->set_filename(filename);
		m_screen->push_gui_refresh();
		m_screen->request_layout_update();
		image->async_modify(
				[filename](const shared_ptr<const HDRImage> &) -> ImageCommandResult
				{
					Timer timer;
					spdlog::info("Trying to load image \"{}\"", filename);
					shared_ptr<HDRImage> ret = load_image(filename);
					if (ret)
						spdlog::info("Loaded \"{}\" [{:d}x{:d}] in {} seconds", filename, ret->width(), ret->height(), timer.elapsed() / 1000.f);
					else
						spdlog::info("Loading \"{}\" failed", filename);
					return {ret, nullptr};
				});
        image->recompute_histograms(m_image_view->exposure());
		m_images.emplace_back(image);
	}

	m_num_images_callback();
	set_current_image_index(int(m_images.size() - 1));
}

bool ImageListPanel::save_image(const string & filename, float exposure, float gamma, bool sRGB, bool dither)
{
	if (!current_image() || !filename.size())
		return false;

    if (current_image()->save(filename, powf(2.0f, exposure), gamma, sRGB, dither))
    {
        current_image()->set_filename(filename);
        m_modify_done_callback(m_current);

        return true;
    }
    else
        return false;
}

bool ImageListPanel::close_image()
{
	if (!current_image())
		return false;

	// select the next image down the list, or the previous if closing the bottom-most image
	int next = next_visible_image(m_current, Backward);
	if (next < m_current)
        next = next_visible_image(m_current, Forward);

	m_images.erase(m_images.begin() + m_current);

	int new_index = next;
	if (m_current < next)
		new_index--;
	else if (next >= int(m_images.size()))
		new_index = m_images.size() - 1;

	set_current_image_index(new_index, true);
	// for now just forget the previous selection when closing any image
	m_previous = -1;
	m_num_images_callback();
	return true;
}

void ImageListPanel::close_all_images()
{
	m_images.clear();

	m_current = -1;
	m_reference = -1;
	m_previous = -1;

	m_image_view->set_current_image(nullptr);
	m_image_view->set_reference_image(nullptr);
    m_screen->update_caption();

	m_num_images_callback();
}

void ImageListPanel::modify_image(const ImageCommand & command)
{
	if (current_image())
	{
		m_images[m_current]->set_modify_done_callback([this](){m_screen->pop_gui_refresh(); m_image_modify_done_requested = true;});
		m_screen->push_gui_refresh(); 
		m_screen->request_layout_update();
		m_images[m_current]->async_modify(
				[command](const shared_ptr<const HDRImage> & img)
				{
					auto ret = command(img);

					// if no undo was provided, just create a FullImageUndo
					if (!ret.second)
						ret.second = make_shared<FullImageUndo>(*img);

					return ret;
				});
        m_screen->update_caption();
	}
}

void ImageListPanel::modify_image(const ImageCommandWithProgress & command)
{
	if (current_image())
	{
		m_images[m_current]->set_modify_done_callback([this](){m_screen->pop_gui_refresh(); m_image_modify_done_requested = true;});
		m_screen->push_gui_refresh(); 
		m_screen->request_layout_update();
		m_images[m_current]->async_modify(
				[command](const shared_ptr<const HDRImage> & img, AtomicProgress &progress)
				{
					auto ret = command(img, progress);

					// if no undo was provided, just create a FullImageUndo
					if (!ret.second)
						ret.second = make_shared<FullImageUndo>(*img);

					return ret;
				});
        m_screen->update_caption();
	}
}

void ImageListPanel::undo()
{
	if (current_image())
	{
		m_images[m_current]->set_modify_done_callback(nullptr);
		if (m_images[m_current]->undo())
			m_modify_done_callback(m_current);
	}
}

void ImageListPanel::redo()
{
	if (current_image())
	{
		m_images[m_current]->set_modify_done_callback(nullptr);
		if (m_images[m_current]->redo())
			m_modify_done_callback(m_current);
	}
}




// The following functions are adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.


bool ImageListPanel::set_filter(const string& filter)
{
    m_filter->set_value(filter);
    m_erase_btn->set_visible(!filter.empty());
    m_update_filter_requested = true;
    return true;
}

std::string ImageListPanel::filter() const
{
    return m_filter->value();
}

bool ImageListPanel::use_regex() const
{
    return m_regex_btn->pushed();
}

void ImageListPanel::set_use_regex(bool value)
{
    m_regex_btn->set_pushed(value);
    m_update_filter_requested = true;
}


void ImageListPanel::update_filter()
{
    string filter = m_filter->value();
	m_previous = -1;

    // filename filtering
    {
        vector<string> active_image_names;
        size_t id = 1;
		auto& buttons = m_image_list->children();
        for (int i = 0; i < num_images(); ++i)
        {
            auto img = image(i);
            auto btn = dynamic_cast<ImageButton*>(buttons[i]);

            btn->set_visible(matches(img->filename(), filter, use_regex()));
            if (btn->visible())
            {
                btn->set_image_id(id++);
                active_image_names.emplace_back(img->filename());
            }
        }

        // determine common parts of filenames
        // taken from tev
        int begin_short_offset = 0;
        int end_short_offset = 0;
        if (!active_image_names.empty())
        {
            string first = active_image_names.front();
            int firstSize = (int)first.size();
            if (firstSize > 0)
            {
                bool all_start_with_same_char = false;
                do
                {
                    int len = codePointLength(first[begin_short_offset]);

                    all_start_with_same_char = all_of
						(
							begin(active_image_names),
							end(active_image_names),
							[&first, begin_short_offset, len](const string& name)
							{
								if (begin_short_offset + len > (int)name.size())
									return false;

								for (int i = begin_short_offset; i < begin_short_offset + len; ++i)
									if (name[i] != first[i])
										return false;

								return true;
							}
						);

                    if (all_start_with_same_char)
                        begin_short_offset += len;
                }
                while (all_start_with_same_char && begin_short_offset < firstSize);

                bool all_end_with_same_char;
                do
                {
                    char lastChar = first[firstSize - end_short_offset - 1];
                    all_end_with_same_char = all_of
						(
							begin(active_image_names),
							end(active_image_names),
							[lastChar, end_short_offset](const string& name)
							{
								int index = (int)name.size() - end_short_offset - 1;
								return index >= 0 && name[index] == lastChar;
							}
						);

                    if (all_end_with_same_char)
                        ++end_short_offset;
                }
                while (all_end_with_same_char && end_short_offset < firstSize);
            }
        }

        for (int i = 0; i < num_images(); ++i)
        {
            auto btn = dynamic_cast<ImageButton*>(buttons[i]);

            if (!btn->visible())
                continue;

            auto img = image(i);
            btn->set_caption(img->filename());
            btn->set_highlight_range(begin_short_offset, end_short_offset);
            btn->set_hide_unhighlighted(m_use_short_btn->pushed());
        }

        if (m_current == -1 || (current_image() && !dynamic_cast<ImageButton*>(buttons[m_current])->visible()))
            set_current_image_index(nth_visible_image_index(0));

        if (m_reference == -1 || (reference_image() && !dynamic_cast<ImageButton*>(buttons[reference_image_index()])->visible()))
            set_reference_image_index(-1);
    }

    m_update_filter_requested = false;

    m_screen->perform_layout();
}



int ImageListPanel::next_visible_image(int index, EDirection direction) const
{
    if (!num_images())
        return -1;

    int dir = direction == Forward ? -1 : 1;

    // If the image does not exist, start at image 0.
    int start_index = max(0, index);

	auto& buttons = m_image_list->children();
    int i = start_index;
    do
    {
        i = (i + num_images() + dir) % num_images();
    }
    while (!dynamic_cast<ImageButton*>(buttons[i])->visible() && i != start_index);

    return i;
}

int ImageListPanel::nth_visible_image_index(int n) const
{
	if (n < 0) return -1;

	auto& buttons = m_image_list->children();
    int last_visible = -1;
    for (int i = 0; i < num_images(); ++i)
    {
        if (dynamic_cast<ImageButton*>(buttons[i])->visible())
        {
            last_visible = i;
            if (n == 0)
                break;

            --n;
        }
    }
    return last_visible;
}

bool ImageListPanel::nth_image_is_visible(int n) const
{
    return n >= 0 && n < int(m_image_list->children().size()) && m_image_list->children()[n]->visible();
}


