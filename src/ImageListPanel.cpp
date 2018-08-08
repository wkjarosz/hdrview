//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "ImageListPanel.h"
#include "HDRViewer.h"
#include "GLImage.h"
#include "ImageButton.h"
#include "HDRImageViewer.h"
#include "MultiGraph.h"
#include "Well.h"
#include <spdlog/spdlog.h>
#include "Timer.h"
#include <tinydir.h>
#include <set>


using namespace std;

ImageListPanel::ImageListPanel(Widget *parent, HDRViewScreen * screen, HDRImageViewer * imgViewer)
	: Widget(parent),
	  m_imageModifyDoneRequested(false),
	  m_imageModifyDoneCallback([](int){}),
	  m_numImagesCallback([](){}),
      m_screen(screen),
      m_imageViewer(imgViewer)
{
	setId("image list panel");
	setLayout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 5, 5));

	// histogram mode selection GUI elements
	{
		auto grid = new Widget(this);
		auto agl = new AdvancedGridLayout({0, 4, 0, 4, 0});
		grid->setLayout(agl);
		agl->setColStretch(2, 1.0f);
		agl->setColStretch(4, 1.0f);

		agl->appendRow(0);
		agl->setAnchor(new Label(grid, "Histogram:", "sans", 14),
		               AdvancedGridLayout::Anchor(0, agl->rowCount() - 1, Alignment::Fill, Alignment::Fill));

		m_yAxisScale = new ComboBox(grid);
		m_yAxisScale->setTooltip("Set the scale for the Y axis.");
		m_yAxisScale->setItems({"Linear", "Log"});
		m_yAxisScale->setFixedHeight(19);
		agl->setAnchor(m_yAxisScale,
		               AdvancedGridLayout::Anchor(2, agl->rowCount() - 1, 1, 1, Alignment::Fill, Alignment::Fill));

		m_xAxisScale = new ComboBox(grid);
		m_xAxisScale->setTooltip("Set the scale for the X axis.");
		m_xAxisScale->setItems({"Linear", "sRGB", "Log"});
		m_xAxisScale->setFixedHeight(19);
		agl->setAnchor(m_xAxisScale,
		               AdvancedGridLayout::Anchor(4, agl->rowCount() - 1, 1, 1, Alignment::Fill, Alignment::Fill));

		m_xAxisScale->setSelectedIndex(1);
		m_yAxisScale->setSelectedIndex(0);
		m_xAxisScale->setCallback([this](int) { updateHistogram(); });
		m_yAxisScale->setCallback([this](int) { updateHistogram(); });
	}

	// histogram and file buttons
	{
		auto row = new Widget(this);
		row->setLayout(new BoxLayout(Orientation::Vertical,
		                             Alignment::Fill, 0, 4));
		m_graph = new MultiGraph(row, Color(255, 0, 0, 150));
		m_graph->addPlot(Color(0, 255, 0, 150));
		m_graph->addPlot(Color(0, 0, 255, 150));

		row = new Widget(this);
		row->setLayout(new GridLayout(Orientation::Horizontal, 5, Alignment::Fill, 0, 2));

		auto b = new Button(row, "", ENTYPO_ICON_FOLDER);
		b->setFixedHeight(25);
		b->setTooltip("Load an image and add it to the set of opened images.");
		b->setCallback([this] { m_screen->loadImage(); });

		m_saveButton = new Button(row, "", ENTYPO_ICON_SAVE);
		m_saveButton->setEnabled(currentImage() != nullptr);
		m_saveButton->setFixedHeight(25);
		m_saveButton->setTooltip("Save the image to disk.");
		m_saveButton->setCallback([this] { m_screen->saveImage(); });

		m_bringForwardButton = new Button(row, "", ENTYPO_ICON_ARROW_BOLD_UP);
		m_bringForwardButton->setFixedHeight(25);
		m_bringForwardButton->setTooltip("Bring the image forward/up the stack.");
		m_bringForwardButton->setCallback([this]{this->bringImageForward();});

		m_sendBackwardButton = new Button(row, "", ENTYPO_ICON_ARROW_BOLD_DOWN);
		m_sendBackwardButton->setFixedHeight(25);
		m_sendBackwardButton->setTooltip("Send the image backward/down the stack.");
		m_sendBackwardButton->setCallback([this]{sendImageBackward();});

		m_closeButton = new Button(row, "", ENTYPO_ICON_CIRCLE_WITH_CROSS);
		m_closeButton->setFixedHeight(25);
		m_closeButton->setTooltip("Close image");
		m_closeButton->setCallback([this] { m_screen->askCloseImage(currentImageIndex()); });
	}

	// channel and blend mode GUI elements
	{
		auto grid = new Widget(this);
		auto agl = new AdvancedGridLayout({0, 4, 0});
		grid->setLayout(agl);
		agl->setColStretch(2, 1.0f);

		agl->appendRow(0);
		agl->setAnchor(new Label(grid, "Mode:", "sans", 14),
		               AdvancedGridLayout::Anchor(0, agl->rowCount() - 1, Alignment::Fill, Alignment::Fill));

		m_blendModes = new ComboBox(grid);
		m_blendModes->setItems(blendModeNames());
		m_blendModes->setFixedHeight(19);
		m_blendModes->setCallback([imgViewer](int b) { imgViewer->setBlendMode(EBlendMode(b)); });
		agl->setAnchor(m_blendModes,
		               AdvancedGridLayout::Anchor(2, agl->rowCount() - 1, Alignment::Fill, Alignment::Fill));

		agl->appendRow(4);  // spacing
		agl->appendRow(0);

		agl->setAnchor(new Label(grid, "Channel:", "sans", 14),
		               AdvancedGridLayout::Anchor(0, agl->rowCount() - 1, Alignment::Fill, Alignment::Fill));

		m_channels = new ComboBox(grid, channelNames());
		m_channels->setFixedHeight(19);
		setChannel(EChannel::RGB);
		m_channels->setCallback([imgViewer](int c) { imgViewer->setChannel(EChannel(c)); });
		agl->setAnchor(m_channels,
		               AdvancedGridLayout::Anchor(2, agl->rowCount() - 1, Alignment::Fill, Alignment::Fill));
	}

	// filter/search of open images GUI elemen ts
	{
		auto grid = new Widget(this);
		auto agl = new AdvancedGridLayout({0, 2, 0, 2, 0, 2, 0});
		grid->setLayout(agl);
		agl->setColStretch(0, 1.0f);

		agl->appendRow(0);

		m_filter = new TextBox(grid, "");
		m_eraseButton = new Button(grid, "", ENTYPO_ICON_ERASE);
		m_regexButton = new Button(grid, ".*");
		m_useShortButton = new Button(grid, "", ENTYPO_ICON_LIST);

		m_filter->setEditable(true);
		m_filter->setAlignment(TextBox::Alignment::Left);
		m_filter->setCallback([this](const string& filter){ return setFilter(filter); });

		m_filter->setPlaceholder("Find");
		m_filter->setTooltip("Filter open image list so that only images with a filename containing the search string will be visible.");

		agl->setAnchor(m_filter,
		               AdvancedGridLayout::Anchor(0, agl->rowCount() - 1, Alignment::Fill, Alignment::Fill));


		m_eraseButton->setFixedWidth(19);
		m_eraseButton->setFixedHeight(19);
		m_eraseButton->setTooltip("Clear the search string.");
		m_eraseButton->setChangeCallback([this](bool b){ setFilter(""); });
		agl->setAnchor(m_eraseButton,
		               AdvancedGridLayout::Anchor(2, agl->rowCount() - 1, Alignment::Minimum, Alignment::Fill));


		m_regexButton->setFixedWidth(19);
		m_regexButton->setFixedHeight(19);
		m_regexButton->setTooltip("Treat search string as a regular expression.");
		m_regexButton->setFlags(Button::ToggleButton);
		m_regexButton->setPushed(false);
		m_regexButton->setChangeCallback([this](bool b){ setUseRegex(b); });
		agl->setAnchor(m_regexButton,
		               AdvancedGridLayout::Anchor(4, agl->rowCount() - 1, Alignment::Minimum, Alignment::Fill));


		m_useShortButton->setFixedWidth(19);
		m_useShortButton->setFixedHeight(19);
		m_useShortButton->setTooltip("Toggle showing full filenames vs. only the unique portion of each filename.");
		m_useShortButton->setFlags(Button::ToggleButton);
		m_useShortButton->setPushed(false);
		m_useShortButton->setChangeCallback([this](bool b){ m_updateFilterRequested = true; });
		agl->setAnchor(m_useShortButton,
		               AdvancedGridLayout::Anchor(6, agl->rowCount() - 1, Alignment::Minimum, Alignment::Fill));

	}

	m_numImagesCallback =
		[this](void)
		{
			m_screen->updateCaption();
			repopulateImageList();
			setReferenceImageIndex(-1);
		};

	m_imageModifyDoneCallback =
		[this](int i)
		{
			m_screen->updateCaption();
			requestButtonsUpdate();
			setFilter(filter());
            requestHistogramUpdate();
		};
}

EBlendMode ImageListPanel::blendMode() const
{
	return EBlendMode(m_blendModes->selectedIndex());
}

void ImageListPanel::setBlendMode(EBlendMode mode)
{
	m_blendModes->setSelectedIndex(mode);
	m_imageViewer->setBlendMode(mode);
}

EChannel ImageListPanel::channel() const
{
	return EChannel(m_channels->selectedIndex());
}

void ImageListPanel::setChannel(EChannel channel)
{
	m_channels->setSelectedIndex(channel);
	m_imageViewer->setChannel(channel);
}

void ImageListPanel::focusFilter() {m_filter->requestFocus();}


void ImageListPanel::repopulateImageList()
{
	// this currently just clears all the widgets and recreates all of them
	// from scratch. this doesn't scale, but should be fine unless you have a
	// lot of images, and makes the logic a lot simpler.

	// prevent crash when the focus path includes any of the widgets we are destroying
	m_screen->clearFocusPath();
	m_imageButtons.clear();

	// clear everything
	if (m_imageListWidget)
		removeChild(m_imageListWidget);

	m_imageListWidget = new Well(this);
	m_imageListWidget->setLayout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0));

	for (int i = 0; i < numImages(); ++i)
	{
		auto btn = new ImageButton(m_imageListWidget, image(i)->filename());
		btn->setImageId(i+1);
		btn->setSelectedCallback([&,i](int){setCurrentImageIndex(i);});
		btn->setReferenceCallback([&,i](int){setReferenceImageIndex(i);});

		m_imageButtons.push_back(btn);
	}

    updateButtons();

	updateFilter();

	m_screen->performLayout();
}

void ImageListPanel::updateButtons()
{
    for (int i = 0; i < numImages(); ++i)
    {
        auto img = image(i);
        auto btn = m_imageButtons[i];

        btn->setIsSelected(i == m_current);
        btn->setIsReference(i == m_reference);
        btn->setCaption(img->filename());
        btn->setIsModified(img->isModified());
        btn->setProgress(img->progress());
        btn->setTooltip(
                fmt::format("Path: {:s}\n\nResolution: ({:d}, {:d})", img->filename(), img->width(), img->height()));
    }

    m_histogramUpdateRequested = true;
//    updateHistogram();

    m_buttonsUpdateRequested = false;
}

void ImageListPanel::enableDisableButtons()
{
	bool hasImage = currentImage() != nullptr;
	bool hasValidImage = hasImage && !currentImage()->isNull();
	m_saveButton->setEnabled(hasValidImage);
	m_closeButton->setEnabled(hasImage);
}


bool ImageListPanel::swapImages(int index1, int index2)
{
	if (!isValid(index1) || !isValid(index2))
		// invalid image indices, do nothing
		return false;

	swap(m_images[index1], m_images[index2]);
	m_imageButtons[index1]->swapWith(*m_imageButtons[index2]);

	return true;
}

bool ImageListPanel::bringImageForward()
{
	int curr = currentImageIndex();
	int next = nextVisibleImage(curr, Forward);

	if (!swapImages(curr, next))
		return false;

	return setCurrentImageIndex(next);
}


bool ImageListPanel::sendImageBackward()
{
	int curr = currentImageIndex();
	int next = nextVisibleImage(curr, Backward);

	if (!swapImages(curr, next))
		return false;

	return setCurrentImageIndex(next);
}

void ImageListPanel::draw(NVGcontext *ctx)
{
	if (m_buttonsUpdateRequested)
		updateButtons();

	// if it has been more than 2 seconds since we requested a histogram update, then update it
	if (m_histogramUpdateRequested &&
		(glfwGetTime() - m_histogramRequestTime) > 1.0)
		updateHistogram();

	if (m_updateFilterRequested)
		updateFilter();

	if (m_histogramDirty &&
		currentImage() &&
		!currentImage()->isNull() &&
		currentImage()->histograms() &&
		currentImage()->histograms()->ready())
	{
		auto lazyHist = currentImage()->histograms();
		int idx = m_xAxisScale->selectedIndex();
		int idxY = m_yAxisScale->selectedIndex();
		auto hist = lazyHist->get()->histogram[idx].values;
		auto ticks = lazyHist->get()->histogram[idx].xTicks;
		auto labels = lazyHist->get()->histogram[idx].xTickLabels;
		m_graph->setValues(idxY == 0 ? hist.col(0) : hist.col(0).unaryExpr([](float v){return normalizedLogScale(v);}).eval(), 0);
		m_graph->setValues(idxY == 0 ? hist.col(1) : hist.col(1).unaryExpr([](float v){return normalizedLogScale(v);}).eval(), 1);
		m_graph->setValues(idxY == 0 ? hist.col(2) : hist.col(2).unaryExpr([](float v){return normalizedLogScale(v);}).eval(), 2);
		m_graph->setXTicks(ticks, labels);
		VectorXf yTicks = VectorXf::LinSpaced(9,0.0f,1.0f);
		if (idxY != 0) yTicks = yTicks.unaryExpr([](float v){return normalizedLogScale(v);});
		m_graph->setYTicks(yTicks);
		m_graph->setLeftHeader(fmt::format("{:.3f}", lazyHist->get()->minimum));
		m_graph->setCenterHeader(fmt::format("{:.3f}", lazyHist->get()->average));
		m_graph->setRightHeader(fmt::format("{:.3f}", lazyHist->get()->maximum));
		m_histogramDirty = false;
	}
	enableDisableButtons();

	if (numImages() != (int)m_imageButtons.size())
		spdlog::get("console")->error("Number of buttons and images don't match!");
	else
	{
		for (int i = 0; i < numImages(); ++i)
		{
			auto img = image(i);
			auto btn = m_imageButtons[i];
			btn->setProgress(img->progress());
			btn->setIsModified(img->isModified());
		}
	}

	Widget::draw(ctx);
}


void ImageListPanel::updateHistogram()
{
	m_histogramDirty = true;

	if (currentImage())
		currentImage()->recomputeHistograms(m_imageViewer->exposure());
	else
    {
        m_graph->setValues(VectorXf(), 0);
        m_graph->setValues(VectorXf(), 1);
        m_graph->setValues(VectorXf(), 2);

        m_graph->setLeftHeader("");
        m_graph->setCenterHeader("");
        m_graph->setRightHeader("");

        m_graph->setXTicks(VectorXf(), {});
        m_graph->setYTicks(VectorXf());
    }

	m_histogramUpdateRequested = false;
	m_histogramRequestTime = glfwGetTime();
}

void ImageListPanel::requestHistogramUpdate(bool force)
{
	if (force)
		updateHistogram();
	else// if (!m_histogramUpdateRequested)
	{
		// if no histogram update is pending, then queue one up, and start the timer
		m_histogramUpdateRequested = true;
		m_histogramRequestTime = glfwGetTime();
	}
}

void ImageListPanel::requestButtonsUpdate()
{
	// if no button update is pending, then queue one up, and start the timer
	m_buttonsUpdateRequested = true;
}

















void ImageListPanel::runRequestedCallbacks()
{
	if (m_imageModifyDoneRequested.exchange(false))
	{
		// remove any images that are not being modified and are null
		bool numImagesChanged = false;

		// iterate through the images, and remove the ones that didn't load properly
		auto it = m_images.begin();
		while (it != m_images.end())
		{
			int i = it - m_images.begin();
			auto img = m_images[i];
			if (img && img->canModify() && img->isNull())
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

		if (numImagesChanged)
		{
			m_imageViewer->setCurrentImage(currentImage());
			m_screen->updateCaption();

			m_numImagesCallback();
		}

		m_imageModifyDoneCallback(m_current);	// TODO: make this use the modified image
	}
}

shared_ptr<const GLImage> ImageListPanel::image(int index) const
{
	return isValid(index) ? m_images[index] : nullptr;
}

shared_ptr<GLImage> ImageListPanel::image(int index)
{
	return isValid(index) ? m_images[index] : nullptr;
}

bool ImageListPanel::setCurrentImageIndex(int index, bool forceCallback)
{
	if (index == m_current && !forceCallback)
		return false;

	if (isValid(m_current))
		m_imageButtons[m_current]->setIsSelected(false);
	if (isValid(index))
		m_imageButtons[index]->setIsSelected(true);

	m_previous = m_current;
	m_current = index;
	m_imageViewer->setCurrentImage(currentImage());
	m_screen->updateCaption();
    updateHistogram();

	return true;
}

bool ImageListPanel::setReferenceImageIndex(int index)
{
	if (index == m_reference)
		return false;

	if (isValid(m_reference))
		m_imageButtons[m_reference]->setIsReference(false);
	if (isValid(index))
		m_imageButtons[index]->setIsReference(true);

	m_reference = index;
	m_imageViewer->setReferenceImage(referenceImage());

	return true;
}

void ImageListPanel::loadImages(const vector<string> & filenames)
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
		image->setImageModifyDoneCallback([this](){m_imageModifyDoneRequested = true;});
		image->setFilename(filename);
		image->asyncModify(
				[filename](const shared_ptr<const HDRImage> &) -> ImageCommandResult
				{
					Timer timer;
					spdlog::get("console")->info("Trying to load image \"{}\"", filename);
					shared_ptr<HDRImage> ret = loadImage(filename);
					if (ret)
						spdlog::get("console")->info("Loaded \"{}\" [{:d}x{:d}] in {} seconds", filename, ret->width(), ret->height(), timer.elapsed() / 1000.f);
					else
						spdlog::get("console")->info("Loading \"{}\" failed", filename);
					return {ret, nullptr};
				});
        image->recomputeHistograms(m_imageViewer->exposure());
		m_images.emplace_back(image);
	}

	m_numImagesCallback();
	setCurrentImageIndex(int(m_images.size() - 1));
}

bool ImageListPanel::saveImage(const string & filename, float exposure, float gamma, bool sRGB, bool dither)
{
	if (!currentImage() || !filename.size())
		return false;

    if (currentImage()->save(filename, powf(2.0f, exposure), gamma, sRGB, dither))
    {
        currentImage()->setFilename(filename);
        m_imageModifyDoneCallback(m_current);

        return true;
    }
    else
        return false;
}

bool ImageListPanel::closeImage()
{
	if (!currentImage())
		return false;

	// select the next image down the list, or the previous if closing the bottom-most image
	int next = nextVisibleImage(m_current, Backward);
	if (next < m_current)
        next = nextVisibleImage(m_current, Forward);

	m_images.erase(m_images.begin() + m_current);

	int newIndex = next;
	if (m_current < next)
		newIndex--;
	else if (next >= int(m_images.size()))
		newIndex = m_images.size() - 1;

	setCurrentImageIndex(newIndex, true);
	// for now just forget the previous selection when closing any image
	m_previous = -1;
	m_numImagesCallback();
	return true;
}

void ImageListPanel::closeAllImages()
{
	m_images.clear();

	m_current = -1;
	m_reference = -1;
	m_previous = -1;


    m_imageViewer->setCurrentImage(currentImage());
    m_screen->updateCaption();

	m_numImagesCallback();
}

void ImageListPanel::modifyImage(const ImageCommand & command)
{
	if (currentImage())
	{
		m_images[m_current]->asyncModify(
				[command](const shared_ptr<const HDRImage> & img)
				{
					auto ret = command(img);

					// if no undo was provided, just create a FullImageUndo
					if (!ret.second)
						ret.second = make_shared<FullImageUndo>(*img);

					return ret;
				});
        m_screen->updateCaption();
	}
}

void ImageListPanel::modifyImage(const ImageCommandWithProgress & command)
{
	if (currentImage())
	{
		m_images[m_current]->asyncModify(
				[command](const shared_ptr<const HDRImage> & img, AtomicProgress &progress)
				{
					auto ret = command(img, progress);

					// if no undo was provided, just create a FullImageUndo
					if (!ret.second)
						ret.second = make_shared<FullImageUndo>(*img);

					return ret;
				});
        m_screen->updateCaption();
	}
}

void ImageListPanel::undo()
{
	if (currentImage() && m_images[m_current]->undo())
		m_imageModifyDoneCallback(m_current);
}

void ImageListPanel::redo()
{
	if (currentImage() && m_images[m_current]->redo())
		m_imageModifyDoneCallback(m_current);
}




// The following functions are adapted from tev:
// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.


bool ImageListPanel::setFilter(const string& filter)
{
    m_filter->setValue(filter);
    m_eraseButton->setVisible(!filter.empty());
    m_updateFilterRequested = true;
    return true;
}

std::string ImageListPanel::filter() const
{
    return m_filter->value();
}

bool ImageListPanel::useRegex() const
{
    return m_regexButton->pushed();
}

void ImageListPanel::setUseRegex(bool value)
{
    m_regexButton->setPushed(value);
    m_updateFilterRequested = true;
}


void ImageListPanel::updateFilter()
{
    string filter = m_filter->value();
	m_previous = -1;

    // Image filtering
    {
        vector<string> activeImageNames;
        size_t id = 1;
        for (int i = 0; i < numImages(); ++i)
        {
            auto img = image(i);
            auto btn = m_imageButtons[i];

            btn->setVisible(matches(img->filename(), filter, useRegex()));
            if (btn->visible())
            {
                btn->setImageId(id++);
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

        for (int i = 0; i < numImages(); ++i)
        {
            auto btn = m_imageButtons[i];

            if (!btn->visible())
                continue;

            auto img = image(i);
            btn->setCaption(img->filename());

            if (m_useShortButton->pushed())
            {
                btn->setHighlightRange(beginShortOffset, endShortOffset);
                btn->setCaption(btn->highlighted());
                btn->setHighlightRange(0, 0);
            }
            else
            {
                btn->setHighlightRange(beginShortOffset, endShortOffset);
            }

        }

        if (m_current == -1 || (currentImage() && !m_imageButtons[m_current]->visible()))
            setCurrentImageIndex(nthVisibleImageIndex(0));

        if (m_reference == -1 || (referenceImage() && !m_imageButtons[referenceImageIndex()]->visible()))
            setReferenceImageIndex(-1);
    }

    m_updateFilterRequested = false;

    m_screen->performLayout();
}



int ImageListPanel::nextVisibleImage(int index, EDirection direction) const
{
    if (!numImages())
        return -1;

    int dir = direction == Forward ? -1 : 1;

    // If the image does not exist, start at image 0.
    int startIndex = max(0, index);

    int i = startIndex;
    do
    {
        i = (i + numImages() + dir) % numImages();
    }
    while (!m_imageButtons[i]->visible() && i != startIndex);

    return i;
}

int ImageListPanel::nthVisibleImageIndex(int n) const
{
    int lastVisible = -1;
    for (int i = 0; i < numImages(); ++i)
    {
        if (m_imageButtons[i]->visible())
        {
            lastVisible = i;
            if (n == 0)
                break;

            --n;
        }
    }
    return lastVisible;
}

bool ImageListPanel::nthImageIsVisible(int n) const
{
    return n >= 0 && n < int(m_imageButtons.size()) && m_imageButtons[n]->visible();
}


