//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "imagelistpanel.h"
#include "hdrviewscreen.h"
#include "glimage.h"
#include "imagebutton.h"
#include "hdrimageviewer.h"
#include "imageview.h"
#include "multigraph.h"
#include "well.h"
#include <spdlog/spdlog.h>
#include "timer.h"
#include <tinydir.h>
#include <set>


using namespace std;

ImageListPanel::ImageListPanel(Widget *parent, HDRViewScreen * screen, HDRImageView * img_view)
	: Widget(parent),
	  m_modify_done_callback([](int){}),
	  m_num_images_callback([](){}),
      m_screen(screen),
      m_image_view(img_view)
{
	// set_id("image list panel");
	set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 5, 5));

	// histogram mode selection GUI elements
	{
		auto grid = new Widget(this);
		auto agl = new AdvancedGridLayout({0, 4, 0, 4, 0});
		grid->set_layout(agl);
		agl->set_col_stretch(2, 1.0f);
		agl->set_col_stretch(4, 1.0f);

		agl->append_row(0);
		agl->set_anchor(new Label(grid, "Histogram:", "sans", 14),
		               AdvancedGridLayout::Anchor(0, agl->row_count() - 1, Alignment::Fill, Alignment::Fill));

		m_yaxis_scale = new ComboBox(grid);
		m_yaxis_scale->set_tooltip("Set the scale for the Y axis.");
		m_yaxis_scale->set_items({"Linear", "Log"});
		m_yaxis_scale->set_fixed_height(19);
		agl->set_anchor(m_yaxis_scale,
		               AdvancedGridLayout::Anchor(2, agl->row_count() - 1, 1, 1, Alignment::Fill, Alignment::Fill));

		m_xaxis_scale = new ComboBox(grid);
		m_xaxis_scale->set_tooltip("Set the scale for the X axis.");
		m_xaxis_scale->set_items({"Linear", "sRGB", "Log"});
		m_xaxis_scale->set_fixed_height(19);
		agl->set_anchor(m_xaxis_scale,
		               AdvancedGridLayout::Anchor(4, agl->row_count() - 1, 1, 1, Alignment::Fill, Alignment::Fill));

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
		m_graph = new MultiGraph(row, Color(255, 0, 0, 150));
		m_graph->add_plot(Color(0, 255, 0, 150));
		m_graph->add_plot(Color(0, 0, 255, 150));

		row = new Widget(this);
		row->set_layout(new GridLayout(Orientation::Horizontal, 5, Alignment::Fill, 0, 2));

		auto b = new Button(row, "", FA_FOLDER_OPEN);
		b->set_fixed_height(25);
		b->set_tooltip("Load an image and add it to the set of opened images.");
		b->set_callback([this] { m_screen->load_image(); });

		m_save_btn = new Button(row, "", FA_SAVE);
		m_save_btn->set_enabled(current_image() != nullptr);
		m_save_btn->set_fixed_height(25);
		m_save_btn->set_tooltip("Save the image to disk.");
		m_save_btn->set_callback([this] { m_screen->save_image(); });

		m_bring_forward_btn = new Button(row, "", FA_ARROW_UP);
		m_bring_forward_btn->set_fixed_height(25);
		m_bring_forward_btn->set_tooltip("Bring the image forward/up the stack.");
		m_bring_forward_btn->set_callback([this]{this->bring_image_forward();});

		m_send_backward_btn = new Button(row, "", FA_ARROW_DOWN);
		m_send_backward_btn->set_fixed_height(25);
		m_send_backward_btn->set_tooltip("Send the image backward/down the stack.");
		m_send_backward_btn->set_callback([this]{send_image_backward();});

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

		m_blend_modes = new ComboBox(grid);
		m_blend_modes->set_items(blendModeNames());
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

	// filter/search of open images GUI elemen ts
	{
		auto grid = new Widget(this);
		auto agl = new AdvancedGridLayout({0, 2, 0, 2, 0, 2, 0});
		grid->set_layout(agl);
		agl->set_col_stretch(0, 1.0f);

		agl->append_row(0);

		m_filter = new TextBox(grid, "");
		m_erase_btn = new Button(grid, "", FA_BACKSPACE);
		m_regex_btn = new Button(grid, ".*");
		m_use_short_btn = new Button(grid, "", FA_ALIGN_LEFT);

		m_filter->set_editable(true);
		m_filter->set_alignment(TextBox::Alignment::Left);
		m_filter->set_callback([this](const string& filter){ return set_filter(filter); });

		m_filter->set_placeholder("Find");
		m_filter->set_tooltip("Filter open image list so that only images with a filename containing the search string will be visible.");

		agl->set_anchor(m_filter,
		               AdvancedGridLayout::Anchor(0, agl->row_count() - 1, Alignment::Fill, Alignment::Fill));


		m_erase_btn->set_fixed_width(19);
		m_erase_btn->set_fixed_height(19);
		m_erase_btn->set_tooltip("Clear the search string.");
		m_erase_btn->set_change_callback([this](bool b){ set_filter(""); });
		agl->set_anchor(m_erase_btn,
		               AdvancedGridLayout::Anchor(2, agl->row_count() - 1, Alignment::Minimum, Alignment::Fill));


		m_regex_btn->set_fixed_width(19);
		m_regex_btn->set_fixed_height(19);
		m_regex_btn->set_tooltip("Treat search string as a regular expression.");
		m_regex_btn->set_flags(Button::ToggleButton);
		m_regex_btn->set_pushed(false);
		m_regex_btn->set_change_callback([this](bool b){ set_use_regex(b); });
		agl->set_anchor(m_regex_btn,
		               AdvancedGridLayout::Anchor(4, agl->row_count() - 1, Alignment::Minimum, Alignment::Fill));


		m_use_short_btn->set_fixed_width(19);
		m_use_short_btn->set_fixed_height(19);
		m_use_short_btn->set_tooltip("Toggle showing full filenames vs. only the unique portion of each filename.");
		m_use_short_btn->set_flags(Button::ToggleButton);
		m_use_short_btn->set_pushed(false);
		m_use_short_btn->set_change_callback([this](bool b){ m_update_filter_requested = true; });
		agl->set_anchor(m_use_short_btn,
		               AdvancedGridLayout::Anchor(6, agl->row_count() - 1, Alignment::Minimum, Alignment::Fill));

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
			cout << "modify done for image: " << i << endl;
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
	m_image_btns.clear();

	// clear everything
	if (m_image_list_widget)
		remove_child(m_image_list_widget);

	m_image_list_widget = new Well(this);
	m_image_list_widget->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0));

	for (int i = 0; i < num_images(); ++i)
	{
		auto btn = new ImageButton(m_image_list_widget, image(i)->filename());
		btn->set_image_id(i+1);
		btn->set_selected_callback([&](int j){set_current_image_index(j);});
		btn->set_reference_callback([&](int j){set_reference_image_index(j);});

		m_image_btns.push_back(btn);
	}

    update_buttons();

	update_filter();

	m_screen->perform_layout();
}

void ImageListPanel::update_buttons()
{
    for (int i = 0; i < num_images(); ++i)
    {
        auto img = image(i);
        auto btn = m_image_btns[i];

        btn->set_is_selected(i == m_current);
        btn->set_is_reference(i == m_reference);
        btn->set_caption(img->filename());
        btn->set_is_modified(img->is_modified());
        btn->set_progress(img->progress());
        btn->set_tooltip(
                fmt::format("Path: {:s}\n\nResolution: ({:d}, {:d})", img->filename(), img->width(), img->height()));
    }

    m_histogram_update_requested = true;
//    update_histogram();

    m_buttons_update_requested = false;
}

void ImageListPanel::enable_disable_buttons()
{
	bool hasImage = current_image() != nullptr;
	bool hasValidImage = hasImage && !current_image()->is_null();
	m_save_btn->set_enabled(hasValidImage);
	m_close_btn->set_enabled(hasImage);
}


bool ImageListPanel::swap_images(int index1, int index2)
{
	if (!is_valid(index1) || !is_valid(index2))
		// invalid image indices, do nothing
		return false;

	swap(m_images[index1], m_images[index2]);
	m_image_btns[index1]->swap_with(*m_image_btns[index2]);

	return true;
}

bool ImageListPanel::bring_image_forward()
{
	int curr = current_image_index();
	int next = next_visible_image(curr, Forward);

	if (!swap_images(curr, next))
		return false;

	return set_current_image_index(next);
}


bool ImageListPanel::send_image_backward()
{
	int curr = current_image_index();
	int next = next_visible_image(curr, Backward);

	if (!swap_images(curr, next))
		return false;

	return set_current_image_index(next);
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
		current_image()->histograms()->ready())
	{
		auto lazyHist = current_image()->histograms();
		int idx = m_xaxis_scale->selected_index();
		int idxY = m_yaxis_scale->selected_index();
		auto hist = lazyHist->get()->histogram[idx].values;
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

		m_graph->set_left_header(fmt::format("{:.3f}", lazyHist->get()->minimum));
		m_graph->set_center_header(fmt::format("{:.3f}", lazyHist->get()->average));
		m_graph->set_right_header(fmt::format("{:.3f}", lazyHist->get()->maximum));
		m_histogram_dirty = false;
	}
	enable_disable_buttons();

	if (num_images() != (int)m_image_btns.size())
		spdlog::get("console")->error("Number of buttons and images don't match!");
	else
	{
		for (int i = 0; i < num_images(); ++i)
		{
			auto img = image(i);
			auto btn = m_image_btns[i];
			btn->set_progress(img->progress());
			btn->set_is_modified(img->is_modified());
		}
	}

	Widget::draw(ctx);
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
		m_image_modify_done_requested = false;

		spdlog::get("console")->trace("running requested callbacks");
		// remove any images that are not being modified and are null
		bool numImagesChanged = false;

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

				numImagesChanged = true;
			}
			else
				++it;
		}

		m_image_view->set_current_image(current_image() ? current_image()->texture() : nullptr);

		if (numImagesChanged)
		{
			// m_image_view->set_current_image(current_image() ? current_image()->texture() : nullptr);
			m_screen->update_caption();

			m_num_images_callback();
		}

		m_modify_done_callback(m_current);	// TODO: make this use the modified image
	}
}

shared_ptr<const GLImage> ImageListPanel::image(int index) const
{
	return is_valid(index) ? m_images[index] : nullptr;
}

shared_ptr<GLImage> ImageListPanel::image(int index)
{
	return is_valid(index) ? m_images[index] : nullptr;
}

bool ImageListPanel::set_current_image_index(int index, bool forceCallback)
{
	if (index == m_current && !forceCallback)
		return false;

	if (is_valid(m_current))
		m_image_btns[m_current]->set_is_selected(false);
	if (is_valid(index))
		m_image_btns[index]->set_is_selected(true);

	m_previous = m_current;
	m_current = index;
	m_image_view->set_current_image(current_image() ? current_image()->texture() : nullptr);
	m_screen->update_caption();
    update_histogram();

	return true;
}

bool ImageListPanel::set_reference_image_index(int index)
{
	if (index == m_reference)
		return false;

	if (is_valid(m_reference))
		m_image_btns[m_reference]->set_is_reference(false);
	if (is_valid(index))
		m_image_btns[index]->set_is_reference(true);

	m_reference = index;
	m_image_view->set_reference_image(reference_image() ? reference_image()->texture() : nullptr);

	return true;
}

void ImageListPanel::load_images(const vector<string> & filenames)
{
	vector<string> allFilenames;

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
				spdlog::get("console")->info("Loading images in \"{}\"...", dir.path);
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

					allFilenames.push_back(file.path);

					if (tinydir_next(&dir) == -1)
						throw runtime_error("Error getting next file");
				}

				tinydir_close(&dir);
			}
			catch (const exception & e)
			{
				spdlog::get("console")->error("Error listing directory: ({}).", e.what());
			}
		}
		else
		{
			allFilenames.push_back(i);
		}
		tinydir_close(&dir);
	}

	// now start a bunch of asynchronous image loads
	for (auto filename : allFilenames)
	{
		shared_ptr<GLImage> image = make_shared<GLImage>();
		image->set_modify_done_callback([this](){m_screen->pop_gui_refresh(); m_image_modify_done_requested = true;});
		image->set_filename(filename);
		m_screen->push_gui_refresh();
		image->async_modify(
				[filename](const shared_ptr<const HDRImage> &) -> ImageCommandResult
				{
					Timer timer;
					spdlog::get("console")->info("Trying to load image \"{}\"", filename);
					shared_ptr<HDRImage> ret = load_image(filename);
					if (ret)
						spdlog::get("console")->info("Loaded \"{}\" [{:d}x{:d}] in {} seconds", filename, ret->width(), ret->height(), timer.elapsed() / 1000.f);
					else
						spdlog::get("console")->info("Loading \"{}\" failed", filename);
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

	int newIndex = next;
	if (m_current < next)
		newIndex--;
	else if (next >= int(m_images.size()))
		newIndex = m_images.size() - 1;

	set_current_image_index(newIndex, true);
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
    m_screen->update_caption();

	m_num_images_callback();
}

void ImageListPanel::modify_image(const ImageCommand & command)
{
	if (current_image())
	{
		m_images[m_current]->set_modify_done_callback([this](){m_screen->pop_gui_refresh(); m_image_modify_done_requested = true;});
		m_screen->push_gui_refresh(); 
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

    // Image filtering
    {
        vector<string> activeImageNames;
        size_t id = 1;
        for (int i = 0; i < num_images(); ++i)
        {
            auto img = image(i);
            auto btn = m_image_btns[i];

            btn->set_visible(matches(img->filename(), filter, use_regex()));
            if (btn->visible())
            {
                btn->set_image_id(id++);
                activeImageNames.emplace_back(img->filename());
            }
        }

        // determine common parts of filenames
        // taken from tev
        int beginShortOffset = 0;
        int endShortOffset = 0;
        if (!activeImageNames.empty())
        {
            string first = activeImageNames.front();
            int firstSize = (int)first.size();
            if (firstSize > 0)
            {
                bool allStartWithSameChar = false;
                do
                {
                    int len = codePointLength(first[beginShortOffset]);

                    allStartWithSameChar = all_of
                            (
                                    begin(activeImageNames),
                                    end(activeImageNames),
                                    [&first, beginShortOffset, len](const string& name)
                                    {
                                        if (beginShortOffset + len > (int)name.size())
                                            return false;

                                        for (int i = beginShortOffset; i < beginShortOffset + len; ++i)
                                            if (name[i] != first[i])
                                                return false;

                                        return true;
                                    }
                            );

                    if (allStartWithSameChar)
                        beginShortOffset += len;
                }
                while (allStartWithSameChar && beginShortOffset < firstSize);

                bool allEndWithSameChar;
                do
                {
                    char lastChar = first[firstSize - endShortOffset - 1];
                    allEndWithSameChar = all_of
                            (
                                    begin(activeImageNames),
                                    end(activeImageNames),
                                    [lastChar, endShortOffset](const string& name)
                                    {
                                        int index = (int)name.size() - endShortOffset - 1;
                                        return index >= 0 && name[index] == lastChar;
                                    }
                            );

                    if (allEndWithSameChar)
                        ++endShortOffset;
                }
                while (allEndWithSameChar && endShortOffset < firstSize);
            }
        }

        for (int i = 0; i < num_images(); ++i)
        {
            auto btn = m_image_btns[i];

            if (!btn->visible())
                continue;

            auto img = image(i);
            btn->set_caption(img->filename());

            if (m_use_short_btn->pushed())
            {
                btn->set_highlight_range(beginShortOffset, endShortOffset);
                btn->set_caption(btn->highlighted());
                btn->set_highlight_range(0, 0);
            }
            else
            {
                btn->set_highlight_range(beginShortOffset, endShortOffset);
            }

        }

        if (m_current == -1 || (current_image() && !m_image_btns[m_current]->visible()))
            set_current_image_index(nth_visible_image_index(0));

        if (m_reference == -1 || (reference_image() && !m_image_btns[reference_image_index()]->visible()))
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
    int startIndex = max(0, index);

    int i = startIndex;
    do
    {
        i = (i + num_images() + dir) % num_images();
    }
    while (!m_image_btns[i]->visible() && i != startIndex);

    return i;
}

int ImageListPanel::nth_visible_image_index(int n) const
{
    int lastVisible = -1;
    for (int i = 0; i < num_images(); ++i)
    {
        if (m_image_btns[i]->visible())
        {
            lastVisible = i;
            if (n == 0)
                break;

            --n;
        }
    }
    return lastVisible;
}

bool ImageListPanel::nth_image_is_visible(int n) const
{
    return n >= 0 && n < int(m_image_btns.size()) && m_image_btns[n]->visible();
}


