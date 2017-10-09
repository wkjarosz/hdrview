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


void addOKCancelButtons(FormHelper * gui, Window * window,
                        const function<void()> &OKCallback,
                        const function<void()> &cancelCallback = nullptr)
{
	auto spacer = new Widget(window);
	spacer->setFixedHeight(15);
	gui->addWidget("", spacer);

	auto w = new Widget(window);
	w->setLayout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 5));
	auto b = new Button(w, "Cancel", ENTYPO_ICON_CIRCLED_CROSS);
	b->setCallback(
		[window,cancelCallback]()
		{
			if (cancelCallback)
				cancelCallback();
			window->dispose();
		});
	b = new Button(w, "OK", ENTYPO_ICON_CHECK);
	b->setCallback(
		[window,OKCallback]()
		{
			OKCallback();
			window->dispose();
		});
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
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->unaryExpr([](const Color4 & c){return c.convert(dst, src);}).eval()),
							        nullptr};
						});
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
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>((Color4(pow(2.0f, exposure), 1.f) * (*img)).pow(Color4(1.0f/gamma))),
							        nullptr};
						});
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
			w->setValueIncrement(5.f);
			w = gui->addVariable("Height:", height);
			w->setSpinnable(true);
			w->setMinValue(0.0f);
			w->setValueIncrement(5.f);

			gui->addVariable("Border mode X:", borderModeX, true)
			   ->setItems(HDRImage::borderModeNames());
			gui->addVariable("Border mode Y:", borderModeY, true)
			   ->setItems(HDRImage::borderModeNames());

			gui->addVariable("Exact (slow!):", exact, true);


			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(exact ? img->GaussianBlurred(width, height, progress, borderModeX, borderModeY) :
							        img->fastGaussianBlurred(width, height, progress, borderModeX, borderModeY)),
							        nullptr};
						});
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
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->boxBlurred(width, height, progress, borderModeX, borderModeY)),
							        nullptr};
						});
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
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->bilateralFiltered(valueSigma, rangeSigma,
							                              progress, borderModeX, borderModeY)),
							        nullptr};
						});
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
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->unsharpMasked(sigma, strength, progress, borderModeX, borderModeY)),
							        nullptr};
						});
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
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->medianFiltered(radius, progress, borderModeX, borderModeY)),
							        nullptr};
						});
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
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->resized(width, height)),
							        nullptr};
						});
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

					UV2XYZFn * dst2xyz;
					XYZ2UVFn * xyz2src;

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
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->resampled(width, height, warp, samples, sampler,
							                                            borderModeX, borderModeY)),
							        nullptr};
						});
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
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							// by default use a no-op passthrough warp function
							function<Vector2f(const Vector2f &)> shift =
								[&](const Vector2f &uv)
								{
									return (uv + Vector2f(dx / img->width(), dy / img->height())).eval();
								};
							return {make_shared<HDRImage>(img->resampled(img->width(), img->height(), shift, 1, sampler,
							                                            borderModeX, borderModeY)),
							        nullptr};
						});
				});

			window->center();
			window->requestFocus();
		});
	return b;
}


Button * createCanvasSizeButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static int width = 128, height = 128;
	static Color bgColor(.8f, .8f, .8f, 1.f);
	static float alpha = 1.f;
	static float EV = 0.f;
	static HDRImage::CanvasAnchor anchor = HDRImage::MIDDLE_CENTER;
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
				b->setChangeCallback([i](bool b){if (b) anchor = (HDRImage::CanvasAnchor)i;});
			}

			gui->addWidget("Anchor:", w2);

			spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

			auto popupBtn = new PopupButton(window, "", 0);
			popupBtn->setBackgroundColor(Color(bgColor.head<3>(), alpha));
			gui->addWidget("Extension color:", popupBtn);

			auto popup = popupBtn->popup();
			popup->setLayout(new GroupLayout());

			auto colorwheel = new ColorWheel(popup);
			colorwheel->setColor(Color(bgColor.head<3>(), alpha));

			auto panel = new Widget(popup);
			panel->setLayout(new GridLayout(Orientation::Horizontal, 3, Alignment::Fill, 0, 0));
			auto colorBtn = new Button(popup, "Pick");

			//
			// opacity
			//
			new Label(panel, "Opacity:");

			auto slider = new Slider(panel);
			slider->setValue(alpha);
			slider->setFixedWidth(100);

			auto floatBox = new FloatBox<float>(panel, 100.0f);
			floatBox->setUnits("%");
			floatBox->numberFormat("%3.1f");
			floatBox->setEditable(true);
			floatBox->setFixedWidth(50);
			floatBox->setAlignment(TextBox::Alignment::Right);

			slider->setCallback([floatBox,colorBtn](float a) {
				alpha = a;
				floatBox->setValue(a * 100);
				colorBtn->setBackgroundColor(Color(bgColor.head<3>() * pow(2.f, EV), alpha));
			});

			floatBox->setCallback([slider,colorBtn](float a) {
				alpha = a;
				slider->setValue(a / 100.f);
				colorBtn->setBackgroundColor(Color(bgColor.head<3>() * pow(2.f, EV), alpha));
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

			slider->setCallback([floatBox,colorBtn](float ev) {
				EV = ev;
				floatBox->setValue(EV);
				colorBtn->setBackgroundColor(Color(bgColor.head<3>() * pow(2.f, EV), alpha));
			});

			floatBox->setCallback([slider,colorBtn](float ev) {
				EV = ev;
				slider->setValue(EV);
				colorBtn->setBackgroundColor(Color(bgColor.head<3>() * pow(2.f, EV), alpha));
			});


			colorBtn->setBackgroundColor(Color(bgColor.head<3>() * pow(2.f, EV), alpha));

			colorwheel->setCallback([colorBtn,floatBox](const Color &c) {
				bgColor.head<3>() = c.head<3>();
				colorBtn->setBackgroundColor(Color(bgColor.head<3>() * pow(2.f, EV), alpha));
			});

			colorBtn->setChangeCallback([colorBtn, popupBtn](bool pushed) {
				if (pushed)
				{
					float gain = pow(2.f, EV);
					popupBtn->setBackgroundColor(Color(bgColor.head<3>() * gain, alpha));
					popupBtn->setPushed(false);
				}
			});

			addOKCancelButtons(gui, window,
				[&, window, popup]()
				{
					popup->dispose();
					imageMgr->modifyImage(
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							int newW = relative ? width + img->width() : width;
							int newH = relative ? height + img->height() : height;

							float gain = pow(2.f, EV);
							Color4 c(bgColor.r() * gain, bgColor.g() * gain, bgColor.b() * gain, alpha);

							return {make_shared<HDRImage>(img->resizedCanvas(newW, newH, anchor, c)),
							        nullptr};
						});
				},
				[popup](){ popup->dispose(); });

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
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->rotated90CW()),
					        make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated90CCW(); },
					                                [](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated90CW(); })};
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
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->rotated90CCW()),
					        make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated90CW(); },
					                                [](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated90CCW(); })};
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


void EditImagePanel::draw(NVGcontext *ctx)
{
	auto img = m_imageMgr->currentImage();

	bool canModify = img && img->canModify();

	if (enabled() != canModify)
	{
		setEnabled(canModify);
		for (auto btn : m_filterButtons)
			btn->setEnabled(canModify);
	}

	m_undoButton->setEnabled(canModify && img->hasUndo());
	m_redoButton->setEnabled(canModify && img->hasRedo());


	Widget::draw(ctx);
}