//
// Created by Wojciech Jarosz on 9/4/17.
//

#include "layerspanel.h"
#include "hdrviewer.h"
#include "glimage.h"
#include "hdrimagemanager.h"

using namespace std;

NAMESPACE_BEGIN(nanogui)

LayersPanel::LayersPanel(Widget *parent, HDRViewScreen * screen, HDRImageManager * imgMgr)
	: Widget(parent), m_screen(screen), m_imageMgr(imgMgr)
{
	setLayout(new GroupLayout(2, 4, 8, 10));

	new Label(this, "File operations", "sans-bold");

	Widget *buttonRow = new Widget(this);
	buttonRow->setLayout(new BoxLayout(Orientation::Horizontal,
	                                   Alignment::Fill, 0, 4));

	Button *b = new Button(buttonRow, "Open", ENTYPO_ICON_FOLDER);
	b->setFixedWidth(84);
	b->setBackgroundColor(Color(0, 100, 0, 75));
	b->setTooltip("Load an image and add it to the set of opened images.");
	b->setCallback([&] {m_screen->loadImage();});

	m_saveButton = new Button(buttonRow, "Save", ENTYPO_ICON_SAVE);
	m_saveButton->setEnabled(m_imageMgr->currentImage());
	m_saveButton->setFixedWidth(84);
	m_saveButton->setBackgroundColor(Color(0, 0, 100, 75));
	m_saveButton->setTooltip("Save the image to disk.");
	m_saveButton->setCallback([&]{m_screen->saveImage();});

	new Label(this, "Opened images:", "sans-bold");
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
	m_layerListWidget->setId("layer list widget");

	// a GridLayout seems to cause a floating point exception when there are no
	// images, so use a BoxLayout instead, which seems to work fine
	if (m_imageMgr->numImages())
	{
		GridLayout *grid = new GridLayout(Orientation::Horizontal, 2,
		                                  Alignment::Fill, 0, 1);
		grid->setSpacing(1, 5);
		m_layerListWidget->setLayout(grid);
	}
	else
		m_layerListWidget->setLayout(new BoxLayout(Orientation::Vertical,
		                                           Alignment::Fill, 0, 5));

	m_screen->clearFocusPath();
	m_layerButtons.clear();

	int index = 0;
	for (int i = 0; i < m_imageMgr->numImages(); ++i)
	{
		auto img = m_imageMgr->image(i);

		size_t start = img->filename().rfind("/")+1;
		string filename = img->filename().substr(start == string::npos ? 0 : start);
		string shortname = filename;
		if (filename.size() > 8+8+3+4)
			shortname = filename.substr(0, 8) + "..." + filename.substr(filename.size()-12);

		Button *b = new Button(m_layerListWidget, shortname, img->isModified() ? ENTYPO_ICON_PENCIL : 0);
		b->setFlags(Button::RadioButton);
		b->setTooltip(fmt::format("Path: {:s}\n\nResolution: ({:d}, {:d})", img->filename(), img->width(), img->height()));

		b->setFixedSize(Vector2i(145,25));
		b->setCallback([&, index]{m_imageMgr->selectLayer(index);});
		m_layerButtons.push_back(b);

		// create a close button for the layer
		b = new Button(m_layerListWidget, "", ENTYPO_ICON_ERASE);
		b->setFixedSize(Vector2i(25,25));
		b->setTooltip("Close image");
		b->setCallback([&, index]{m_screen->askCloseImage(index);});

		index++;
	}

	for (auto b : m_layerButtons)
		b->setButtonGroup(m_layerButtons);

	m_screen->performLayout();
}

void LayersPanel::enableDisableButtons()
{
	if (m_saveButton)
		m_saveButton->setEnabled(m_imageMgr->currentImage());
}

void LayersPanel::selectLayer(int newIndex)
{
	// deselect the old layer
	for (auto btn : m_layerButtons)
		btn->setPushed(false);

	// select the new layer
	if (newIndex >= 0 && newIndex < int(m_layerButtons.size()))
		m_layerButtons[newIndex]->setPushed(true);
}

NAMESPACE_END(nanogui)