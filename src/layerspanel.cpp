//
// Created by Wojciech Jarosz on 9/4/17.
//

#include "layerspanel.h"
#include "hdrviewer.h"
#include "glimage.h"
#include "hdrimagemanager.h"
#include "imagebutton.h"

using namespace std;

NAMESPACE_BEGIN(nanogui)

LayersPanel::LayersPanel(Widget *parent, HDRViewScreen * screen, HDRImageManager * imgMgr)
	: Widget(parent), m_screen(screen), m_imageMgr(imgMgr)
{
	setLayout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 5, 5));

	Widget *buttonRow = new Widget(this);
	buttonRow->setLayout(new BoxLayout(Orientation::Horizontal,
	                                   Alignment::Fill, 0, 2));

	auto b = new Button(buttonRow, "", ENTYPO_ICON_FOLDER);
	b->setFixedSize(Vector2i(47, 25));
	b->setTooltip("Load an image and add it to the set of opened images.");
	b->setCallback([this]{m_screen->loadImage();});

	m_saveButton = new Button(buttonRow, "", ENTYPO_ICON_SAVE);
	m_saveButton->setEnabled(m_imageMgr->currentImage());
	m_saveButton->setFixedSize(Vector2i(47, 25));
	m_saveButton->setTooltip("Save the image to disk.");
	m_saveButton->setCallback([this]{m_screen->saveImage();});


	m_bringForwardButton = new Button(buttonRow, "", ENTYPO_ICON_UP_BOLD);
	m_bringForwardButton->setFixedSize(Vector2i(25, 25));
	m_bringForwardButton->setTooltip("Bring the image forward/up the stack.");
	m_bringForwardButton->setCallback([this]{m_imageMgr->bringLayerForward();});

	m_sendBackwardButton = new Button(buttonRow, "", ENTYPO_ICON_DOWN_BOLD);
	m_sendBackwardButton->setFixedSize(Vector2i(25, 25));
	m_sendBackwardButton->setTooltip("Send the image backward/down the stack.");
	m_sendBackwardButton->setCallback([this]{m_imageMgr->sendLayerBackward();});

	m_closeButton = new Button(buttonRow, "", ENTYPO_ICON_CIRCLED_CROSS);
	m_closeButton->setFixedSize(Vector2i(25, 25));
	m_closeButton->setTooltip("Close image");
	m_closeButton->setCallback([this]{m_screen->askCloseImage(m_imageMgr->currentImageIndex());});
}


void LayersPanel::repopulateLayerList()
{
	// this currently just clears all the widgets and recreates all of them
	// from scratch. this doesn't scale, but should be fine unless you have a
	// lot of images, and makes the logic a lot simpler.

	// clear everything
	if (m_layerListWidget)
		removeChild(m_layerListWidget);

	m_layerListWidget = new Widget(this);
	m_layerListWidget->setLayout(new BoxLayout(Orientation::Vertical, Alignment::Fill));

	m_screen->clearFocusPath();
	m_layerButtons.clear();

	int index = 0;
	for (int i = 0; i < m_imageMgr->numImages(); ++i)
	{
		auto img = m_imageMgr->image(i);
		auto b = new ImageButton(m_layerListWidget, img->filename());
		b->setId(i);
		b->setIsModified(img->isModified());
		b->setTooltip(fmt::format("Path: {:s}\n\nResolution: ({:d}, {:d})", img->filename(), img->width(), img->height()));

		b->setSelectedCallback([&, index]{m_imageMgr->selectLayer(index);});
		m_layerButtons.push_back(b);

		index++;
	}

	m_screen->performLayout();
}

void LayersPanel::enableDisableButtons()
{
	m_saveButton->setEnabled(m_imageMgr->currentImage());
	m_closeButton->setEnabled(m_imageMgr->currentImage());
	m_bringForwardButton->setEnabled(m_imageMgr->currentImage() && m_imageMgr->currentImageIndex() > 0);
	m_sendBackwardButton->setEnabled(m_imageMgr->currentImage() && m_imageMgr->currentImageIndex() < m_imageMgr->numImages()-1);
}

void LayersPanel::selectLayer(int newIndex)
{
	// deselect the old layer
	for (auto btn : m_layerButtons)
		btn->setIsSelected(false);

	// select the new layer
	if (newIndex >= 0 && newIndex < int(m_layerButtons.size()))
		m_layerButtons[newIndex]->setIsSelected(true);
}

NAMESPACE_END(nanogui)