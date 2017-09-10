//
// Created by Wojciech Jarosz on 9/4/17.
//

#include "imagelistpanel.h"
#include "hdrviewer.h"
#include "glimage.h"
#include "hdrimagemanager.h"
#include "imagebutton.h"
#include "hdrimageviewer.h"

using namespace std;

NAMESPACE_BEGIN(nanogui)

ImageListPanel::ImageListPanel(Widget *parent, HDRViewScreen * screen, HDRImageManager * imgMgr, HDRImageViewer * imgViewer)
	: Widget(parent), m_screen(screen), m_imageMgr(imgMgr), m_imageViewer(imgViewer)
{
	setId("image list panel");
	setLayout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 5, 5));

	auto row = new Widget(this);
	row->setLayout(new BoxLayout(Orientation::Horizontal,
	                                   Alignment::Fill, 0, 2));

	auto b = new Button(row, "", ENTYPO_ICON_FOLDER);
	b->setFixedSize(Vector2i(47, 25));
	b->setTooltip("Load an image and add it to the set of opened images.");
	b->setCallback([this]{m_screen->loadImage();});

	m_saveButton = new Button(row, "", ENTYPO_ICON_SAVE);
	m_saveButton->setEnabled(m_imageMgr->currentImage());
	m_saveButton->setFixedSize(Vector2i(47, 25));
	m_saveButton->setTooltip("Save the image to disk.");
	m_saveButton->setCallback([this]{m_screen->saveImage();});

	m_bringForwardButton = new Button(row, "", ENTYPO_ICON_UP_BOLD);
	m_bringForwardButton->setFixedSize(Vector2i(25, 25));
	m_bringForwardButton->setTooltip("Bring the image forward/up the stack.");
	m_bringForwardButton->setCallback([this]{ m_imageMgr->bringImageForward();});

	m_sendBackwardButton = new Button(row, "", ENTYPO_ICON_DOWN_BOLD);
	m_sendBackwardButton->setFixedSize(Vector2i(25, 25));
	m_sendBackwardButton->setTooltip("Send the image backward/down the stack.");
	m_sendBackwardButton->setCallback([this]{ m_imageMgr->sendImageBackward();});

	m_closeButton = new Button(row, "", ENTYPO_ICON_CIRCLED_CROSS);
	m_closeButton->setFixedSize(Vector2i(25, 25));
	m_closeButton->setTooltip("Close image");
	m_closeButton->setCallback([this]{m_screen->askCloseImage(m_imageMgr->currentImageIndex());});

	row = new Widget(this);
	row->setLayout(new BoxLayout(Orientation::Horizontal,
	                                   Alignment::Fill, 0, 2));
	(new Label(row, "Mode:"))->setFontSize(14);
	m_blendModes = new ComboBox(row);
	m_blendModes->setItems(blendModeNames());
	m_blendModes->setFixedSize(Vector2i(144, 20));
	m_blendModes->setCallback([imgViewer](int b){imgViewer->setBlendMode(EBlendMode(b));});

	row = new Widget(this);
	row->setLayout(new BoxLayout(Orientation::Horizontal,
	                             Alignment::Fill, 0, 2));
	(new Label(row, "Channel:"))->setFontSize(14);
	m_channels = new ComboBox(row, channelNames());
	m_channels->setFixedSize(Vector2i(132, 20));
	setChannel(EChannel::RGB);
	m_channels->setCallback([imgViewer](int c){imgViewer->setChannel(EChannel(c));});


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

	m_imageListWidget = new Widget(this);
	m_imageListWidget->setLayout(new BoxLayout(Orientation::Vertical, Alignment::Fill));


	int index = 0;
	for (int i = 0; i < m_imageMgr->numImages(); ++i)
	{
		auto img = m_imageMgr->image(i);
		auto b = new ImageButton(m_imageListWidget, img->filename());
		b->setId(i+1);
		b->setIsModified(img->isModified());
		b->setTooltip(fmt::format("Path: {:s}\n\nResolution: ({:d}, {:d})", img->filename(), img->width(), img->height()));

		b->setSelectedCallback([&](int i){m_imageMgr->setCurrentImageIndex(i-1);});
		b->setReferenceCallback([&](int i){m_imageMgr->setReferenceImageIndex(i-1);});
		m_imageButtons.push_back(b);

		index++;
	}

	m_screen->performLayout();
}

void ImageListPanel::enableDisableButtons()
{
	m_saveButton->setEnabled(m_imageMgr->currentImage());
	m_closeButton->setEnabled(m_imageMgr->currentImage());
	m_bringForwardButton->setEnabled(m_imageMgr->currentImage() && m_imageMgr->currentImageIndex() > 0);
	m_sendBackwardButton->setEnabled(m_imageMgr->currentImage() && m_imageMgr->currentImageIndex() < m_imageMgr->numImages()-1);
}

void ImageListPanel::setCurrentImage(int newIndex)
{
	for (int i = 0; i < (int) m_imageButtons.size(); ++i)
		m_imageButtons[i]->setIsSelected(i == newIndex ? true : false);
}

void ImageListPanel::setReferenceImage(int newIndex)
{
	for (int i = 0; i < (int) m_imageButtons.size(); ++i)
		m_imageButtons[i]->setIsReference(i == newIndex ? true : false);
}

NAMESPACE_END(nanogui)