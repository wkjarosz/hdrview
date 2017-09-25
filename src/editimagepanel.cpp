//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "editimagepanel.h"
#include "common.h"
#include "glimage.h"
#include "hdrviewer.h"
#include "hdrimage.h"
#include "hdrimagemanager.h"
#include "envmap.h"
#include "colorspace.h"
#include <spdlog/spdlog.h>

using namespace std;

namespace
{


void addOKCancelButtons(FormHelper * gui, Window * window, const std::function<void()> &callback)
{
	auto spacer = new Widget(window);
	spacer->setFixedHeight(15);
	gui->addWidget("", spacer);

	auto w = new Widget(window);
	w->setLayout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 5));
	auto b = new Button(w, "Cancel", ENTYPO_ICON_CIRCLED_CROSS);
	b->setCallback([&, window]() { window->dispose(); });
	b = new Button(w, "OK", ENTYPO_ICON_CHECK);
	b->setCallback(callback);
	gui->addWidget("", w);
}

Button * createColorSpaceButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static string name = "Convert color space...";
	static EColorSpace src = LinearSRGB_CS, dst = CIEXYZ_CS;
	auto b = new Button(parent, name, ENTYPO_ICON_PALETTE);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(125, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);

			gui->addVariable("Source:", src, true)
			   ->setItems(colorSpaceNames());
			gui->addVariable("Destination:", dst, true)
			   ->setItems(colorSpaceNames());

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);
							img = img.unaryExpr([](const Color4 & c){return c.convert(dst, src);}).eval();
							return undo;
						});
					window->dispose();
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createExposureGammaButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static string name = "Exposure/Gamma...";
	static float exposure = 0.0f;
	static float gamma = 1.0f;
	auto b = new Button(parent, name, ENTYPO_ICON_ADJUST);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(75, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->addVariable("Exposure:", exposure);
			w->setSpinnable(true);
			w->setMinValue(-10.f);
			w->setMaxValue( 10.f);
			w = gui->addVariable("Gamma:", gamma);
			w->setSpinnable(true);
			w->setMinValue(0.0001f);
			w->setMaxValue(10.0f);

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);
							img = (Color4(pow(2.0f, exposure)) * img).pow(Color4(1.0f/gamma));
							return undo;
						});
					window->dispose();
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createGaussianFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float width = 1.0f, height = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static bool exact = false;
	static string name = "Gaussian blur...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(75, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
			// window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->addVariable("Width:", width);
			w->setSpinnable(true);
			w->setMinValue(0.0f);
			w = gui->addVariable("Height:", height);
			w->setSpinnable(true);
			w->setMinValue(0.0f);

			gui->addVariable("Border mode X:", borderModeX, true)
			   ->setItems(HDRImage::borderModeNames());
			gui->addVariable("Border mode Y:", borderModeY, true)
			   ->setItems(HDRImage::borderModeNames());

			gui->addVariable("Exact (slow!):", exact, true);


			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);
							img = exact ? img.GaussianBlurred(width, height, borderModeX, borderModeY) :
							      img.fastGaussianBlurred(width, height, borderModeX, borderModeY);
							return undo;
						});
					window->dispose();
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createBoxFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float width = 1.0f, height = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Box blur...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(75, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->addVariable("Width:", width);
			w->setSpinnable(true);
			w->setMinValue(0.0f);
			w = gui->addVariable("Height:", height);
			w->setSpinnable(true);
			w->setMinValue(0.0f);

			gui->addVariable("Border mode X:", borderModeX, true)
			   ->setItems(HDRImage::borderModeNames());
			gui->addVariable("Border mode Y:", borderModeY, true)
			   ->setItems(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);
							img = img.boxBlurred(width, height, borderModeX, borderModeY);
							return undo;
						});
					window->dispose();
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createBilateralFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float rangeSigma = 1.0f, valueSigma = 0.1f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Bilateral filter...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(75, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->addVariable("Range sigma:", rangeSigma);
			w->setSpinnable(true);
			w->setMinValue(0.0f);
			w = gui->addVariable("Value sigma:", valueSigma);
			w->setSpinnable(true);
			w->setMinValue(0.0f);

			gui->addVariable("Border mode X:", borderModeX, true)
			   ->setItems(HDRImage::borderModeNames());
			gui->addVariable("Border mode Y:", borderModeY, true)
			   ->setItems(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);
							img = img.bilateralFiltered(valueSigma, rangeSigma, borderModeX,
							                            borderModeY);
							return undo;
						});
					window->dispose();
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createUnsharpMaskFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float sigma = 1.0f, strength = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Unsharp mask...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(75, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->addVariable("Sigma:", sigma);
			w->setSpinnable(true);
			w->setMinValue(0.0f);
			w = gui->addVariable("Strength:", strength);
			w->setSpinnable(true);
			w->setMinValue(0.0f);

			gui->addVariable("Border mode X:", borderModeX, true)
			   ->setItems(HDRImage::borderModeNames());
			gui->addVariable("Border mode Y:", borderModeY, true)
			   ->setItems(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);
							img =
								img.unsharpMasked(sigma, strength, borderModeX, borderModeY);
							return undo;
						});
					window->dispose();
				});


			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createMedianFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float radius = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Median filter...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(75, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->addVariable("Radius:", radius);
			w->setSpinnable(true);
			w->setMinValue(0.0f);

			gui->addVariable("Border mode X:", borderModeX, true)
			   ->setItems(HDRImage::borderModeNames());
			gui->addVariable("Border mode Y:", borderModeY, true)
			   ->setItems(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);
							img = img.medianFiltered(radius, borderModeX, borderModeY);
							return undo;
						});
					window->dispose();
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createResizeButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static int width = 128, height = 128;
	static string name = "Resize...";
	static bool aspect = true;
	auto b = new Button(parent, name, ENTYPO_ICON_RESIZE_FULL);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(75, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
			window->setModal(true);

			width = imageMgr->currentImage()->width();
			auto w = gui->addVariable("Width:", width);
			w->setSpinnable(true);
			w->setMinValue(1);

			height = imageMgr->currentImage()->height();
			auto h = gui->addVariable("Height:", height);
			h->setSpinnable(true);
			h->setMinValue(1);

			auto preserveCheckbox = gui->addVariable("Preserve aspect ratio:", aspect, true);
			preserveCheckbox->setCallback(
				[w,imageMgr](bool preserve)
				{
					if (preserve)
					{
						float aspect = imageMgr->currentImage()->width() / (float)imageMgr->currentImage()->height();
						width = max(1, (int)round(height * aspect));
						w->setValue(width);
					}
					aspect = preserve;
				});

			w->setCallback([h,preserveCheckbox,imageMgr](int w) {
				width = w;
				if (preserveCheckbox->checked())
				{
					float aspect = imageMgr->currentImage()->width() / (float)imageMgr->currentImage()->height();
					height = max(1, (int)round(w / aspect));
					h->setValue(height);
				}
			});

			h->setCallback([w,preserveCheckbox,imageMgr](int h) {
				height = h;
				if (preserveCheckbox->checked())
				{
					float aspect = imageMgr->currentImage()->width() / (float)imageMgr->currentImage()->height();
					width = max(1, (int)round(height * aspect));
					w->setValue(width);
				}
			});

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);
							img = img.resized(width, height);
							return undo;
						});
					window->dispose();
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createResampleButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	enum MappingMode
	{
		ANGULAR_MAP = 0,
		MIRROR_BALL,
		LAT_LONG,
		CUBE_MAP
	};
	static MappingMode from = ANGULAR_MAP, to = ANGULAR_MAP;
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static int width = 128, height = 128;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static int samples = 1;

	static string name = "Remap...";
	auto b = new Button(parent, name, ENTYPO_ICON_MAP);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(125, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			width = imageMgr->currentImage()->width();
			auto w = gui->addVariable("Width:", width);
			w->setSpinnable(true);
			w->setMinValue(1);

			height = imageMgr->currentImage()->height();
			w = gui->addVariable("Height:", height);
			w->setSpinnable(true);
			w->setMinValue(1);

			gui->addVariable("Source parametrization:", from, true)
			   ->setItems({"Angular map", "Mirror ball", "Longitude-latitude", "Cube map"});
			gui->addVariable("Target parametrization:", to, true)
			   ->setItems({"Angular map", "Mirror ball", "Longitude-latitude", "Cube map"});
			gui->addVariable("Sampler:", sampler, true)
			   ->setItems(HDRImage::samplerNames());
			gui->addVariable("Border mode X:", borderModeX, true)
			   ->setItems(HDRImage::borderModeNames());
			gui->addVariable("Border mode Y:", borderModeY, true)
			   ->setItems(HDRImage::borderModeNames());

			w = gui->addVariable("Super-samples:", samples);
			w->setSpinnable(true);
			w->setMinValue(1);

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					// by default use a no-op passthrough warp function
					function<Vector2f(const Vector2f &)> warp = [](const Vector2f &uv) { return uv; };

					UV2XYZFn dst2xyz;
					XYZ2UVFn xyz2src;

					if (from != to)
					{
						if (from == ANGULAR_MAP)
							xyz2src = XYZToAngularMap;
						else if (from == MIRROR_BALL)
							xyz2src = XYZToMirrorBall;
						else if (from == LAT_LONG)
							xyz2src = XYZToLatLong;
						else if (from == CUBE_MAP)
							xyz2src = XYZToCubeMap;

						if (to == ANGULAR_MAP)
							dst2xyz = angularMapToXYZ;
						else if (to == MIRROR_BALL)
							dst2xyz = mirrorBallToXYZ;
						else if (to == LAT_LONG)
							dst2xyz = latLongToXYZ;
						else if (to == CUBE_MAP)
							dst2xyz = cubeMapToXYZ;

						warp = [&](const Vector2f &uv) { return xyz2src(dst2xyz(Vector2f(uv(0), uv(1)))); };
					}

					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);
							img = img.resampled(width, height, warp, samples, sampler,
							                    borderModeX, borderModeY);
							return undo;
						});
					window->dispose();
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createShiftButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static HDRImage::BorderMode borderModeX = HDRImage::REPEAT, borderModeY = HDRImage::REPEAT;
	static float dx = 0.f, dy = 0.f;
	static string name = "Shift...";
	auto b = new Button(parent, name, ENTYPO_ICON_SWITCH);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(125, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->addVariable("X offset:", dx);
			w->setSpinnable(true);

			w = gui->addVariable("Y offset:", dy);
			w->setSpinnable(true);

			gui->addVariable("Sampler:", sampler, true)
			   ->setItems(HDRImage::samplerNames());
			gui->addVariable("Border mode X:", borderModeX, true)
			   ->setItems(HDRImage::borderModeNames());
			gui->addVariable("Border mode Y:", borderModeY, true)
			   ->setItems(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							// by default use a no-op passthrough warp function
							function<Vector2f(const Vector2f &)> shift = [&](const Vector2f &uv)
							{
								return (uv + Vector2f(dx / img.width(), dy / img.height()))
									.eval();
							};

							auto undo = new FullImageUndo(img);
							img = img.resampled(img.width(), img.height(), shift, 1, sampler,
							                    borderModeX, borderModeY);
							return undo;
						});
					window->dispose();
				});
			
			window->center();
			window->requestFocus();
		});
	return b;
}


Button * createCanvasSizeButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static int width = 128, height = 128;
	static Color4 bgColor(.8f, .8f, .8f, 1.f);
	enum CanvasAnchor : int
	{
		TOP_LEFT = 0,
		TOP_CENTER,
		TOP_RIGHT,
		MIDDLE_LEFT,
		MIDDLE_CENTER,
		MIDDLE_RIGHT,
		BOTTOM_LEFT,
		BOTTOM_CENTER,
		BOTTOM_RIGHT,
		NUM_CANVAS_ANCHORS
	};
	static CanvasAnchor anchor = MIDDLE_CENTER;
	static string name = "Canvas size...";
	static bool relative = false;
	auto b = new Button(parent, name, ENTYPO_ICON_RESIZE_FULL);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(75, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//			window->setModal(true);

			width = imageMgr->currentImage()->width();
			auto w = gui->addVariable("Width:", width);
			w->setSpinnable(true);
			w->setMinValue(1);

			height = imageMgr->currentImage()->height();
			auto h = gui->addVariable("Height:", height);
			h->setSpinnable(true);
			h->setMinValue(1);

			relative = false;
			auto r = gui->addVariable("Relative:", relative, true);
			r->setCallback(
				[w,h,imageMgr](bool rel)
				{
					if (rel)
					{
						w->setMinValue(-imageMgr->currentImage()->width()+1);
						h->setMinValue(-imageMgr->currentImage()->height()+1);
						width = w->value() - imageMgr->currentImage()->width();
						height = h->value() - imageMgr->currentImage()->height();
						w->setValue(width);
						h->setValue(height);
					}
					else
					{
						w->setMinValue(1);
						h->setMinValue(1);
						width = w->value() + imageMgr->currentImage()->width();
						height = h->value() + imageMgr->currentImage()->height();
						w->setValue(width);
						h->setValue(height);
					}
					relative = rel;
				});

			auto spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

			auto w2 = new Widget(window);
			w2->setLayout(new GridLayout(Orientation::Horizontal, 3, Alignment::Minimum, 0, 0));
			vector<Button *> buttonGroup;
			buttonGroup.push_back(new Button(w2, ""));
			buttonGroup.push_back(new Button(w2, "", ENTYPO_ICON_UP));
			buttonGroup.push_back(new Button(w2, ""));
			buttonGroup.push_back(new Button(w2, "", ENTYPO_ICON_LEFT));
			buttonGroup.push_back(new Button(w2, "", ENTYPO_ICON_DOT));
			buttonGroup.push_back(new Button(w2, "", ENTYPO_ICON_RIGHT));
			buttonGroup.push_back(new Button(w2, ""));
			buttonGroup.push_back(new Button(w2, "", ENTYPO_ICON_DOWN));
			buttonGroup.push_back(new Button(w2, ""));

			for (size_t i = 0; i < buttonGroup.size(); ++i)
			{
				Button * b = buttonGroup[i];
				b->setFlags(Button::RadioButton);
				b->setFixedSize(Vector2i(25, 25));
				b->setPushed(i == (size_t)anchor);
				b->setChangeCallback([i](bool b){if (b) anchor = (CanvasAnchor)i;});
			}

			gui->addWidget("Anchor:", w2);

			spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

			auto popupBtn = new PopupButton(window, "", 0);
			popupBtn->setBackgroundColor(Color(bgColor.r, bgColor.g, bgColor.b, bgColor.a));
			gui->addWidget("Extension color:", popupBtn);

			auto popup = popupBtn->popup();
			popup->setLayout(new GroupLayout());

			auto colorwheel = new ColorWheel(popup);
			colorwheel->setColor(Color(bgColor.r, bgColor.g, bgColor.b, bgColor.a));

			//
			// opacity
			//
			auto panel = new Widget(popup);
			panel->setLayout(new GridLayout(Orientation::Horizontal, 3,
			                               Alignment::Fill, 0, 0));
			new Label(panel, "Opacity:");

			auto slider = new Slider(panel);
			slider->setValue(bgColor.a);
			slider->setFixedWidth(100);

			auto floatBox = new FloatBox<float>(panel, 100.0f);
			floatBox->setUnits("%");
			floatBox->numberFormat("%3.1f");
			floatBox->setEditable(true);
			floatBox->setFixedWidth(50);
			floatBox->setAlignment(TextBox::Alignment::Right);

			slider->setCallback([floatBox](float value) {
				floatBox->setValue(value * 100);
				bgColor.a = value;
			});

			floatBox->setCallback([slider](float value) {
				slider->setValue(value / 100.f);
				bgColor.a = value;
			});

			//
			// EV
			//
			new Label(panel, "EV:");

			slider = new Slider(panel);
			slider->setValue(0.0f);
			slider->setRange({-9.0f,9.0f});
			slider->setFixedWidth(100);

			floatBox = new FloatBox<float>(panel, 0.f);
			floatBox->numberFormat("%1.2f");
			floatBox->setEditable(true);
			floatBox->setFixedWidth(50);
			floatBox->setAlignment(TextBox::Alignment::Right);

			slider->setCallback([floatBox,colorwheel](float value) {
				floatBox->setValue(value);
				float gain = pow(2.f, value);
				bgColor = Color4(colorwheel->color().r() * gain,
				                 colorwheel->color().g() * gain,
				                 colorwheel->color().b() * gain, bgColor.a);
			});

			floatBox->setCallback([slider,colorwheel](float value) {
				slider->setValue(value);
				float gain = pow(2.f, value);
				bgColor = Color4(colorwheel->color().r() * gain,
				                 colorwheel->color().g() * gain,
				                 colorwheel->color().b() * gain, bgColor.a);
			});

			auto colorBtn = new Button(popup, "Pick");
			colorBtn->setBackgroundColor(Color(bgColor.r, bgColor.g, bgColor.b, bgColor.a));

			colorwheel->setCallback([colorBtn,floatBox](const Color &value) {
				float gain = pow(2.f, floatBox->value());
				bgColor = Color4(value.r() * gain,
				                 value.g() * gain,
				                 value.b() * gain, bgColor.a);
				colorBtn->setBackgroundColor(Color(bgColor.r, bgColor.g, bgColor.b, bgColor.a));
			});

			colorBtn->setChangeCallback([colorBtn, popupBtn](bool pushed) {
				if (pushed)
				{
					popupBtn->setBackgroundColor(Color(bgColor.r, bgColor.g, bgColor.b, bgColor.a));
					popupBtn->setPushed(false);
				}
			});

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					cout << "anchor: " << anchor << endl;
					imageMgr->modifyImage(
						[&](HDRImage &img)
						{
							auto undo = new FullImageUndo(img);

							int oldW = imageMgr->currentImage()->width();
							int oldH = imageMgr->currentImage()->height();

							int newW = relative ? width + oldW : width;
							int newH = relative ? height + oldH : height;

							spdlog::get("console")->debug("oldW: {}, newW: {}; oldH: {}, newH: {}", oldW, newW, oldH, newH);
							spdlog::get("console")->debug("relative: {}", relative);
							spdlog::get("console")->debug("width: {}, height: {}", width, height);

							// fill in new regions with border value
							img = HDRImage::Constant(newW, newH, bgColor);

							Vector2i tlDst(0,0);
							// find top-left corner
							switch (anchor)
							{
								case TOP_RIGHT:
								case MIDDLE_RIGHT:
								case BOTTOM_RIGHT:
									tlDst.x() = newW-oldW;
									break;

								case TOP_CENTER:
								case MIDDLE_CENTER:
								case BOTTOM_CENTER:
									tlDst.x() = (newW-oldW)/2;
									break;

								case TOP_LEFT:
								case MIDDLE_LEFT:
								case BOTTOM_LEFT:
								default:
									tlDst.x() = 0;
									break;
							}
							switch (anchor)
							{
								case BOTTOM_LEFT:
								case BOTTOM_CENTER:
								case BOTTOM_RIGHT:
									tlDst.y() = newH-oldH;
									break;

								case MIDDLE_LEFT:
								case MIDDLE_CENTER:
								case MIDDLE_RIGHT:
									tlDst.y() = (newH-oldH)/2;
									break;

								case TOP_LEFT:
								case TOP_CENTER:
								case TOP_RIGHT:
								default:
									tlDst.y() = 0;
									break;
							}

							Vector2i tlSrc(0,0);
							if (tlDst.x() < 0)
							{
								tlSrc.x() = -tlDst.x();
								tlDst.x() = 0;
							}
							if (tlDst.y() < 0)
							{
								tlSrc.y() = -tlDst.y();
								tlDst.y() = 0;
							}

							Vector2i bs(min(oldW, newW), min(oldH, newH));


							spdlog::get("console")->debug("dst.x0: {}, dst.y0: {}; src.x0: {}, src.y0: {}\ndst.w: {}, dst.h: {}",
							                              tlDst.x(), tlDst.y(), tlSrc.x(), tlSrc.y(), bs.x(), bs.y());
							img.block(tlDst.x(), tlDst.y(),
							          bs.x(), bs.y()) = undo->image().block(tlSrc.x(), tlSrc.y(),
							                                                bs.x(), bs.y());

							return undo;
						});
					window->dispose();
				});
			
			window->center();
			window->requestFocus();
		});
	return b;
}

}


EditImagePanel::EditImagePanel(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
	: Widget(parent), m_screen(screen), m_imageMgr(imageMgr)
{
	setLayout(new GroupLayout(2, 4, 8, 10));

	new Label(this, "History", "sans-bold");

	auto buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 2));

	m_undoButton = new Button(buttonRow, "Undo", ENTYPO_ICON_REPLY);
	m_undoButton->setCallback([&](){m_imageMgr->undo();});
	m_redoButton = new Button(buttonRow, "Redo", ENTYPO_ICON_FORWARD);
	m_redoButton->setCallback([&](){m_imageMgr->redo();});

	new Label(this, "Pixel/domain transformations", "sans-bold");

	auto grid = new Widget(this);
	grid->setLayout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 2));

	// flip h
	m_filterButtons.push_back(new Button(grid, "Flip H", ENTYPO_ICON_LEFT_BOLD));
	m_filterButtons.back()->setCallback([&](){m_screen->flipImage(true);});
	m_filterButtons.back()->setFixedHeight(21);

	// rotate cw
	m_filterButtons.push_back(new Button(grid, "Rotate CW", ENTYPO_ICON_CW));
	m_filterButtons.back()->setFixedHeight(21);
	m_filterButtons.back()->setCallback(
		[&]()
		{
			m_imageMgr->modifyImage(
				[&](HDRImage &img)
				{
					img = img.rotated90CW();
					return new LambdaUndo([](HDRImage &img2) { img2 = img2.rotated90CCW(); },
					                      [](HDRImage &img2) { img2 = img2.rotated90CW(); });
				});
		});

	// flip v
	m_filterButtons.push_back(new Button(grid, "Flip V", ENTYPO_ICON_DOWN_BOLD));
	m_filterButtons.back()->setCallback([&](){m_screen->flipImage(false);});
	m_filterButtons.back()->setFixedHeight(21);

	// rotate ccw
	m_filterButtons.push_back(new Button(grid, "Rotate CCW", ENTYPO_ICON_CCW));
	m_filterButtons.back()->setFixedHeight(21);
	m_filterButtons.back()->setCallback(
		[&]()
		{
			m_imageMgr->modifyImage(
				[&](HDRImage &img)
				{
					img = img.rotated90CCW();
					return new LambdaUndo([](HDRImage &img2) { img2 = img2.rotated90CW(); },
					                      [](HDRImage &img2) { img2 = img2.rotated90CCW(); });
				});
		});

	// shift
	m_filterButtons.push_back(createShiftButton(grid, m_screen, m_imageMgr));
	// canvas size
	m_filterButtons.push_back(createCanvasSizeButton(grid, m_screen, m_imageMgr));


	m_filterButtons.push_back(createResizeButton(grid, m_screen, m_imageMgr));
	// resample
	m_filterButtons.push_back(createResampleButton(grid, m_screen, m_imageMgr));


	new Label(this, "Color/range adjustments", "sans-bold");
	buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, 2));
	m_filterButtons.push_back(createExposureGammaButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createColorSpaceButton(buttonRow, m_screen, m_imageMgr));

	new Label(this, "Filters", "sans-bold");
	buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, 2));
	m_filterButtons.push_back(createGaussianFilterButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createBoxFilterButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createBilateralFilterButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createUnsharpMaskFilterButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createMedianFilterButton(buttonRow, m_screen, m_imageMgr));
}


void EditImagePanel::enableDisableButtons()
{
	if (m_undoButton)
		m_undoButton->setEnabled(m_imageMgr->currentImage() && m_imageMgr->currentImage()->hasUndo());
	if (m_redoButton)
		m_redoButton->setEnabled(m_imageMgr->currentImage() && m_imageMgr->currentImage()->hasRedo());

	for_each(m_filterButtons.begin(), m_filterButtons.end(),
	         [&](Widget * w){w->setEnabled(m_imageMgr->currentImage()); });
}