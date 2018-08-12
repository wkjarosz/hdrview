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
#include "hslgradient.h"
#include "multigraph.h"
#include "FilmicToneCurve.h"
#include <spdlog/spdlog.h>
#include <Eigen/Geometry>

using namespace std;

namespace
{

std::function<void(float)> createFloatBoxAndSlider(
	FormHelper * gui, Widget * parent,
	string name, float & variable,
	float mn, float mx, float step,
	std::function<void(void)> cb,
	string help = "")
{
	auto fBox = gui->addVariable(name, variable);
	fBox->setSpinnable(true);
	fBox->numberFormat("%1.2f");
	fBox->setValueIncrement(step);
	fBox->setMinMaxValues(mn, mx);
	fBox->setTooltip(help);

	auto fSlider = new Slider(parent);
	fSlider->setValue(variable);
	fSlider->setRange({mn, mx});
	fSlider->setTooltip(help);
	gui->addWidget("", fSlider);

	auto fCb = [fBox,fSlider,cb,&variable](float v)
	{
		variable = v;
		fBox->setValue(v);
		fSlider->setValue(v);
		cb();
	};
	fSlider->setCallback(fCb);
	fBox->setCallback(fCb);
	return fCb;
}


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
	static float offset = 0.0f;
	auto b = new Button(parent, name, ENTYPO_ICON_ADJUST);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(55, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes


			// graph
			auto graph = new MultiGraph(window, Color(255, 255, 255, 30));
			graph->addPlot(Color(255, 255, 255, 200));
			graph->addPlot(Color(255, 255, 255, 50));
			graph->setFixedSize(Vector2i(200, 200));
			graph->setFilled(false);
			graph->setWell(false);
			graph->setValues(VectorXf::LinSpaced(257, 0.0f, 1.0f), 0);
			graph->setValues(VectorXf::Constant(2, 0.5f), 2);
			int numTicks = 5;
			// create the x tick marks
			VectorXf xTicks = VectorXf::LinSpaced(numTicks, 0.0f, 1.0f);
			// create the x tick labels
			vector<string> xTickLabels(numTicks);
			for (int i = 0; i < numTicks; ++i)
				xTickLabels[i] = fmt::format("{:.2f}", xTicks[i]);
			graph->setXTicks(xTicks, xTickLabels);
			graph->setYTicks(xTicks);
			gui->addWidget("", graph);

			auto graphCb = [graph]()
			{
				VectorXf lCurve = VectorXf::LinSpaced(257, 0.0f, 1.0f).unaryExpr(
					[](float v)
					{
						return pow(pow(2.0f, exposure) * v + offset, 1.0f/gamma);
					});
				graph->setValues(lCurve, 1);
			};

			graphCb();


			createFloatBoxAndSlider(gui, window,
			                        "Exposure:", exposure,
			                        -10.f, 10.f, 0.1f, graphCb);

			createFloatBoxAndSlider(gui, window,
			                        "Offset:", offset,
			                        -1.f, 1.f, 0.01f, graphCb);

			createFloatBoxAndSlider(gui, window,
			                        "Gamma:", gamma,
			                        0.0001f, 10.f, 0.1f, graphCb);

			addOKCancelButtons(gui, window,
				[&, window]()
				{
					imageMgr->modifyImage(
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							spdlog::get("console")->debug("{}; {}; {}", exposure, offset, gamma);
							return {make_shared<HDRImage>((Color4(pow(2.0f, exposure), 1.f) * (*img) + Color4(offset, 0.f)).pow(Color4(1.0f/gamma))),
							        nullptr};
						});
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createBrightnessContrastButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static string name = "Brightness/Contrast...";
	static float brightness = 0.0f;
	static float contrast = 0.0f;
	static bool linear = false;
	static const auto activeColor = Color(255, 255, 255, 200);
	static const auto inactiveColor = Color(255, 255, 255, 25);
	static enum EChannel
	{
		RGB = 0,
		LUMINANCE,
		CHROMATICITY
	} channel = RGB;
	static ::EChannel channelMap[] = { ::EChannel::RGB, ::EChannel::LUMINANCE, ::EChannel::CIE_CHROMATICITY };
	auto b = new Button(parent, name, ENTYPO_ICON_VOLUME);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(100, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes


			// graph
			auto graph = new MultiGraph(window, Color(255, 255, 255, 30));
			graph->addPlot(inactiveColor);
			graph->addPlot(activeColor);
			graph->addPlot(Color(255, 255, 255, 50));
			graph->setFixedSize(Vector2i(200, 200));
			graph->setFilled(false);
			graph->setWell(false);
			graph->setValues(VectorXf::LinSpaced(257, 0.0f, 1.0f), 0);
			graph->setValues(VectorXf::Constant(2, 0.5f), 3);
			int numTicks = 5;
			// create the x tick marks
			VectorXf xTicks = VectorXf::LinSpaced(numTicks, 0.0f, 1.0f);
			// create the x tick labels
			vector<string> xTickLabels(numTicks);
			for (int i = 0; i < numTicks; ++i)
				xTickLabels[i] = fmt::format("{:.2f}", xTicks[i]);
			graph->setXTicks(xTicks, xTickLabels);
			graph->setYTicks(xTicks);

			gui->addWidget("", graph);

			auto graphCb = [graph]()
			{
				float slope = float(std::tan(lerp(0.0, M_PI_2, contrast/2.0 + 0.5)));
				float midpoint = (1.f-brightness)/2.f;
				float bias = (brightness + 1.f) / 2.f;
				VectorXf lCurve = VectorXf::LinSpaced(257, 0.0f, 1.0f).unaryExpr(
					[slope, midpoint](float v)
					{
						return brightnessContrastL(v, slope, midpoint);
					});
				lCurve.tail<1>()(0) = 1;
				graph->setValues(lCurve, 1);

				VectorXf nlCurve = VectorXf::LinSpaced(257, 0.0f, 1.0f).unaryExpr(
					[slope, bias](float v)
					{
						return brightnessContrastNL(v, slope, bias);
					});
				nlCurve.tail<1>()(0) = 1;
				graph->setValues(nlCurve, 2);
			};

			graphCb();

			// brightness
			string help = "Shift the 50% gray midpoint.\n\n"
						  "Setting brightness > 0 boosts a previously darker value to 50%, "
						  "while brightness < 0 dims a previously brighter value to 50%.";

			auto bCb = createFloatBoxAndSlider(gui, window,
			                        "Brightness:", brightness,
			                        -1.f, 1.f, 0.01f, graphCb, help);

			help = "Change the slope/gradient at the new 50% midpoint.";
			auto cCb = createFloatBoxAndSlider(gui, window,
			                        "Contrast:", contrast,
			                        -1.f, 1.f, 0.01f, graphCb, help);

			auto lCheck = gui->addVariable("Linear:", linear, true);
			gui->addVariable("Channel:", channel, true)->setItems({"RGB", "Luminance", "Chromaticity"});

			lCheck->setCallback(
				[graph,graphCb](bool b)
				{
					linear = b;
					graph->setForegroundColor(linear ? activeColor : inactiveColor, 1);
					graph->setForegroundColor(linear ? inactiveColor : activeColor, 2);
				});

			graph->setDragCallback(
				[bCb,cCb](const Vector2f & frac)
				{
					bCb(lerp(1.f, -1.f, clamp01(frac.x())));
					cCb(lerp(-1.f, 1.f, clamp01(frac.y())));
				});

			addOKCancelButtons(gui, window,
               [&, window]()
               {
	               imageMgr->modifyImage(
		               [&](const shared_ptr<const HDRImage> &img) -> ImageCommandResult
		               {
			               return {make_shared<HDRImage>(img->brightnessContrast(brightness, contrast, linear, channelMap[channel])),
			                       nullptr};
		               });
               });

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createFilmicTonemappingButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static string name = "Filmic tonemapping...";
	static FilmicToneCurve::FullCurve fCurve;
	static FilmicToneCurve::CurveParamsUser params;
	static float vizFstops = 1.f;
	static const auto activeColor = Color(255, 255, 255, 200);
	auto b = new Button(parent, name, ENTYPO_ICON_VOLUME);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(55, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			// graph
			MultiGraph* graph = new MultiGraph(window, Color(255, 255, 255, 30));
			graph->addPlot(activeColor);
			graph->setFixedSize(Vector2i(200, 200));
			graph->setFilled(false);
			graph->setWell(false);
			gui->addWidget("", graph);

			auto graphCb = [graph]()
			{
				float range = pow(2.f, vizFstops);
				FilmicToneCurve::CurveParamsDirect directParams;
				FilmicToneCurve::calcDirectParamsFromUser(directParams, params);
				FilmicToneCurve::createCurve(fCurve, directParams);

//				spdlog::get("console")->debug(
//					"\n"
//				    "x0: {}\n"
//				    "y0: {}\n"
//					"x1: {}\n"
//					"y1: {}\n"
//					"W: {}\n"
//					"gamma: {}\n"
//					"overshootX: {}\n"
//					"overshootY: {}\n",
//				    directParams.x0,
//				    directParams.y0,
//				    directParams.x1,
//				    directParams.y1,
//				    directParams.W ,
//				    directParams.gamma,
//				    directParams.overshootX,
//				    directParams.overshootY);

				graph->setValues(VectorXf::LinSpaced(257, 0.0f, range), 0);
				VectorXf lCurve = VectorXf::LinSpaced(257, 0.0f, range).unaryExpr(
					[](float v)
					{
						return fCurve.eval(v);
					});
				graph->setValues(lCurve, 1);

				int numTicks = 5;
				// create the x tick marks
				VectorXf xTicks = VectorXf::LinSpaced(numTicks, 0.0f, 1.0f);
				// create the x tick labels
				vector<string> xTickLabels(numTicks);
				for (int i = 0; i < numTicks; ++i)
					xTickLabels[i] = fmt::format("{:.2f}", range*xTicks[i]);
				graph->setXTicks(xTicks, xTickLabels);
				graph->setYTicks(VectorXf::LinSpaced(3, 0.0f, 1.0f));
			};

			graphCb();

			createFloatBoxAndSlider(gui, window,
			                        "Graph F-stops:", vizFstops,
			                        0.f, 10.f, 0.1f, graphCb);

			createFloatBoxAndSlider(gui, window,
			                        "Toe strength:", params.toeStrength,
			                        0.f, 1.f, 0.01f, graphCb);

			createFloatBoxAndSlider(gui, window,
			                        "Toe length:", params.toeLength,
			                        0.f, 1.f, 0.01f, graphCb);

			createFloatBoxAndSlider(gui, window,
			                        "Shoulder strength:", params.shoulderStrength,
			                        0.f, 10.f, 0.1f, graphCb);

			createFloatBoxAndSlider(gui, window,
			                        "Shoulder length:", params.shoulderLength,
			                        0.f, 1.f, 0.01f, graphCb);

			createFloatBoxAndSlider(gui, window,
			                        "Shoulder angle:", params.shoulderAngle,
			                        0.f, 1.f, 0.01f, graphCb);

			createFloatBoxAndSlider(gui, window,
			                        "Gamma:", params.gamma,
			                        0.f, 5.f, 0.01f, graphCb);

			addOKCancelButtons(gui, window,
				[&, window]()
				{
				   imageMgr->modifyImage(
				       [&](const shared_ptr<const HDRImage> &img) -> ImageCommandResult
				       {
				           return {make_shared<HDRImage>(img->unaryExpr(
				               [](const Color4 & c)
				               {
//					               float srcLum = c.average();
//					               float dstLum = fCurve.eval(srcLum);
////					               return c * (dstLum/srcLum);
//					               return Color4(c.r * (dstLum/srcLum),
//					                             c.g * (dstLum/srcLum),
//					                             c.b * (dstLum/srcLum),
//					                             c.a);
				                   return Color4(fCurve.eval(c.r),
				                                 fCurve.eval(c.g),
				                                 fCurve.eval(c.b),
				                                 c.a);
				               }).eval()), nullptr};
				       });
				});

			window->center();
			window->requestFocus();
		});
	return b;
}

Button * createHueSaturationButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static string name = "Hue/Saturation...";
	static float hue = 0.0f;
	static float saturation = 0.0f;
	static float lightness = 0.0f;
	auto b = new Button(parent, name, ENTYPO_ICON_PALETTE);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(55, 20));

			Widget* spacer = nullptr;

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes



			auto fixedRainbow = new HSLGradient(window);
			auto dynamicRainbow = new HSLGradient(window);
			fixedRainbow->setFixedWidth(256);
			dynamicRainbow->setFixedWidth(256);

			auto cb = [dynamicRainbow]()
			{
				dynamicRainbow->setHueOffset(hue);
				dynamicRainbow->setSaturation((saturation + 100.f)/200.f);
				dynamicRainbow->setLightness((lightness + 100.f)/200.f);
			};

			createFloatBoxAndSlider(gui, window,
			                        "Hue:", hue,
			                        -180.f, 180.f, 1.f, cb);

			createFloatBoxAndSlider(gui, window,
			                        "Saturation:", saturation,
			                        -100.f, 100.f, 1.f, cb);

			createFloatBoxAndSlider(gui, window,
			                        "Lightness:", lightness,
			                        -100.f, 100.f, 1.f, cb);

			spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

			gui->addWidget("", fixedRainbow);

			spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

			gui->addWidget("", dynamicRainbow);

			addOKCancelButtons(gui, window,
			                   [&, window]()
			                   {
				                   imageMgr->modifyImage(
					                   [&](const shared_ptr<const HDRImage> &img) -> ImageCommandResult
					                   {
						                   return {make_shared<HDRImage>(
							                   img->unaryExpr(
								                   [](const Color4 & c)
								                   {
									                   return c.HSLAdjust(hue, (saturation+100.f)/100.f, (lightness)/100.f);
								                   }).eval()), nullptr};
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
			w->setUnits("px");
			w = gui->addVariable("Height:", height);
			w->setSpinnable(true);
			w->setMinValue(0.0f);
			w->setValueIncrement(5.f);
			w->setUnits("px");

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
			w->setUnits("px");
			w = gui->addVariable("Height:", height);
			w->setSpinnable(true);
			w->setMinValue(0.0f);
			w->setUnits("px");

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
			gui->setFixedSize(Vector2i(0, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
			window->setModal(true);

			auto row = new Widget(window);
			row->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			width = imageMgr->currentImage()->width();
			height = imageMgr->currentImage()->height();

			auto w = new IntBox<int>(row, width);
			auto link = new ToolButton(row, ENTYPO_ICON_LINK);
			auto h = new IntBox<int>(row, height);

			w->setSpinnable(true);
			w->setEnabled(true);
			w->setEditable(true);
			w->setMinValue(1);
			w->setFontSize(gui->widgetFontSize());
			w->setFixedSize(Vector2i(80, gui->fixedSize().y()));
			w->setAlignment(TextBox::Alignment::Right);
			w->setUnits("px");

			link->setFixedSize(Vector2i(20,20));
			link->setPushed(aspect);

			h->setSpinnable(true);
			h->setEnabled(true);
			h->setEditable(true);
			h->setMinValue(1);
			h->setFontSize(gui->widgetFontSize());
			h->setFixedSize(Vector2i(80, gui->fixedSize().y()));
			h->setAlignment(TextBox::Alignment::Right);
			h->setUnits("px");

			link->setChangeCallback(
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

			w->setCallback(
				[h,link,imageMgr](int w)
				{
					width = w;
					if (link->pushed())
					{
						float aspect = imageMgr->currentImage()->width() / (float)imageMgr->currentImage()->height();
						height = max(1, (int)round(w / aspect));
						h->setValue(height);
					}
				});

			h->setCallback(
				[w,link,imageMgr](int h)
				{
					height = h;
					if (link->pushed())
					{
						float aspect = imageMgr->currentImage()->width() / (float)imageMgr->currentImage()->height();
						width = max(1, (int)round(height * aspect));
						w->setValue(width);
					}
				});


			gui->addWidget("", row);

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

Button * createRemapButton(Widget *parent, HDRViewScreen *screen, HDRImageManager *imageMgr)
{
	static EEnvMappingUVMode from = ANGULAR_MAP, to = ANGULAR_MAP;
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static int width = 128, height = 128;
	static bool autoAspect = true;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static int samples = 1;

	static float autoAspects[] =
		{
			1.f,
			1.f,
			2.f,
			2.f,
			0.75f
		};

	static string name = "Remap...";
	auto b = new Button(parent, name, ENTYPO_ICON_GLOBE);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(135, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			width = imageMgr->currentImage()->width();
			auto w = gui->addVariable("Width:", width);
			w->setSpinnable(true);
			w->setMinValue(1);
			w->setUnits("px");

			height = imageMgr->currentImage()->height();
			auto h = gui->addVariable("Height:", height);
			h->setSpinnable(true);
			h->setMinValue(1);
			h->setUnits("px");

			auto recomputeW = []()
			{
				if (autoAspect)
					width = max(1, (int)round(height * autoAspects[to]));
			};
			auto recomputeH = []()
			{
				if (autoAspect)
					height = max(1, (int)round(width / autoAspects[to]));
			};

			w->setCallback(
				[h,recomputeH](int w)
				{
					width = w;
					recomputeH();
					h->setValue(height);
				});

			h->setCallback(
				[w,recomputeW](int h)
				{
					height = h;
					recomputeW();
					w->setValue(width);
				});


			auto autoAspectCheckbox = gui->addVariable("Auto aspect ratio:", autoAspect, true);

			auto src = gui->addVariable("Source map:", from, true);
			auto dst = gui->addVariable("Target map:", to, true);

			src->setItems(envMappingNames());
			src->setCallback([gui,recomputeW](EEnvMappingUVMode m)
			                 {
				                 from = m;
				                 recomputeW();
				                 gui->refresh();
			                 });
			dst->setItems(envMappingNames());
			dst->setCallback([gui,recomputeW](EEnvMappingUVMode m)
			                 {
				                 to = m;
				                 recomputeW();
				                 gui->refresh();
			                 });

			auto spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

			auto btn = new Button(window, "Swap source/target", ENTYPO_ICON_SWITCH);
			btn->setCallback([gui,recomputeW,recomputeH](){std::swap(from,to);recomputeW();recomputeH();gui->refresh();});
			btn->setFixedSize(gui->fixedSize());
			gui->addWidget(" ", btn);

			autoAspectCheckbox->setCallback(
				[w,recomputeW](bool preserve)
				{
					autoAspect = preserve;
					recomputeW();
					w->setValue(width);
				});

			recomputeW();
			gui->refresh();


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
//					auto dst2xyz = envMapUVToXYZ(to);
//					auto xyz2src = XYZToEnvMapUV(from);
//					auto warp = [dst2xyz,xyz2src](const Vector2f &uv) { return xyz2src(dst2xyz(uv)); };
					auto warp = [](const Vector2f &uv) { return convertEnvMappingUV(from, to, uv); };

					imageMgr->modifyImage(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->resampled(width, height, progress, warp, samples, sampler,
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
			w->setUnits("px");

			w = gui->addVariable("Y offset:", dy);
			w->setSpinnable(true);
			w->setUnits("px");

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
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							// by default use a no-op passthrough warp function
							function<Vector2f(const Vector2f &)> shift =
								[&](const Vector2f &uv)
								{
									return (uv + Vector2f(dx / img->width(), dy / img->height())).eval();
								};
							return {make_shared<HDRImage>(img->resampled(img->width(), img->height(),
							                                             progress, shift, 1, sampler,
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
			w->setUnits("px");

			height = imageMgr->currentImage()->height();
			auto h = gui->addVariable("Height:", height);
			h->setSpinnable(true);
			h->setMinValue(1);
			h->setUnits("px");

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
//			panel->setLayout(new GridLayout(Orientation::Horizontal, 3, Alignment::Fill, 0, 0));
			auto agrid = new AdvancedGridLayout({0, 20, 0}, {});
			agrid->setMargin(0);
			agrid->setColStretch(1, 1);
			panel->setLayout(agrid);

			auto colorBtn = new Button(popup, "Pick");

			//
			// opacity
			//

			agrid->appendRow(0);
			agrid->setAnchor(new Label(panel, "Opacity:"), AdvancedGridLayout::Anchor(0, agrid->rowCount()-1));

			auto floatBox = new FloatBox<float>(panel, alpha * 100.0f);
			agrid->setAnchor(floatBox, AdvancedGridLayout::Anchor(2, agrid->rowCount()-1));
			floatBox->setUnits("%");
			floatBox->numberFormat("%3.1f");
			floatBox->setEditable(true);
			floatBox->setMinValue(0.f);
			floatBox->setMaxValue(100.f);
			floatBox->setSpinnable(true);
			floatBox->setFixedWidth(60);
			floatBox->setAlignment(TextBox::Alignment::Right);

			agrid->appendRow(0);
			auto slider = new Slider(panel);
			agrid->setAnchor(slider, AdvancedGridLayout::Anchor(0, agrid->rowCount()-1, 3, 1));
			slider->setValue(alpha * 100.0f);
			slider->setRange({0.0f,100.0f});

			slider->setCallback([floatBox,colorBtn](float a) {
				alpha = a / 100.f;
				floatBox->setValue(a);
				colorBtn->setBackgroundColor(Color(bgColor.head<3>() * pow(2.f, EV), alpha));
			});

			floatBox->setCallback([slider,colorBtn](float a) {
				alpha = a / 100.f;
				slider->setValue(a);
				colorBtn->setBackgroundColor(Color(bgColor.head<3>() * pow(2.f, EV), alpha));
			});

			agrid->appendRow(10);

			//
			// EV
			//
			agrid->appendRow(0);
			agrid->setAnchor(new Label(panel, "EV:"), AdvancedGridLayout::Anchor(0, agrid->rowCount()-1));

			floatBox = new FloatBox<float>(panel, 0.f);
			agrid->setAnchor(floatBox, AdvancedGridLayout::Anchor(2, agrid->rowCount()-1));
			floatBox->numberFormat("%1.2f");
			floatBox->setEditable(true);
			floatBox->setSpinnable(true);
			floatBox->setFixedWidth(60);
			floatBox->setAlignment(TextBox::Alignment::Right);

			agrid->appendRow(0);
			slider = new Slider(panel);
			agrid->setAnchor(slider, AdvancedGridLayout::Anchor(0, agrid->rowCount()-1, 3, 1));
			slider->setValue(0.0f);
			slider->setRange({-9.0f,9.0f});

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



Button * createFreeTransformButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float translateX = 0, translateY = 0;
	static float scaleX = 100.0f, scaleY = 100.0f;
	static bool uniformScale = true;
	static float angle = 0.0f;
	static bool cw = false;
	static float shearX = 0, shearY = 0;
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static HDRImage::BorderMode borderModeX = HDRImage::REPEAT, borderModeY = HDRImage::REPEAT;
	static HDRImage::CanvasAnchor anchor = HDRImage::MIDDLE_CENTER;
	static int samples = 1;
	static string name = "Transform...";
	auto b = new Button(parent, name, ENTYPO_ICON_RESIZE_FULL);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(0, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//			window->setModal(true);

			auto row = new Widget(window);
			row->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto x = new FloatBox<float>(row, translateX);
			x->setSpinnable(true);
			x->setEnabled(true);
			x->setEditable(true);
			x->setFontSize(gui->widgetFontSize());
			x->setFixedSize(Vector2i(65+12, gui->fixedSize().y()));
			x->setAlignment(TextBox::Alignment::Right);
			x->setUnits("px");
			x->setCallback([](float v){translateX = v;});
			x->setTooltip("Set horizontal translation.");

			auto y = new FloatBox<float>(row, translateY);
			y->setSpinnable(true);
			y->setEnabled(true);
			y->setEditable(true);
			y->setFontSize(gui->widgetFontSize());
			y->setFixedSize(Vector2i(65+13, gui->fixedSize().y()));
			y->setAlignment(TextBox::Alignment::Right);
			y->setUnits("px");
			y->setCallback([](float v){translateY = v;});
			y->setTooltip("Set vertical translation.");

			gui->addWidget("Translate:", row);


			auto spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);


			row = new Widget(window);
			row->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto w = new FloatBox<float>(row, scaleX);
			auto link = new ToolButton(row, ENTYPO_ICON_LINK);
			auto h = new FloatBox<float>(row, scaleY);

			w->setSpinnable(true);
			w->setEnabled(true);
			w->setEditable(true);
			w->setFontSize(gui->widgetFontSize());
			w->setFixedSize(Vector2i(65, gui->fixedSize().y()));
			w->setAlignment(TextBox::Alignment::Right);
			w->setUnits("%");
			w->setTooltip("Set horizontal scale.");
			w->setCallback(
				[h](float v)
				{
					scaleX = v;
					if (uniformScale) scaleY = scaleX;
					h->setValue(scaleY);
				});

			link->setFixedSize(Vector2i(20,20));
			link->setPushed(uniformScale);
			link->setTooltip("Lock the X and Y scale factors to maintain aspect ratio.");
			link->setChangeCallback(
				[w,h](bool b)
				{
					uniformScale = b;
					if (uniformScale) scaleX = scaleY;
					w->setValue(scaleX);
					h->setValue(scaleY);
				});

			h->setSpinnable(true);
			h->setEnabled(true);
			h->setEditable(true);
			h->setFontSize(gui->widgetFontSize());
			h->setFixedSize(Vector2i(65, gui->fixedSize().y()));
			h->setAlignment(TextBox::Alignment::Right);
			h->setUnits("%");
			h->setTooltip("Set vertical scale.");
			h->setCallback(
				[w](float v)
				{
					scaleY = v;
					if (uniformScale) scaleX = scaleY;
					w->setValue(scaleX);
				});

			gui->addWidget("Scale:", row);


			spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);


			row = new Widget(window);
			row->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto a = new FloatBox<float>(row, angle);
			a->setSpinnable(true);
			a->setEnabled(true);
			a->setEditable(true);
			a->setFontSize(gui->widgetFontSize());
			a->setFixedSize(Vector2i(160-2*25, gui->fixedSize().y()));
			a->setAlignment(TextBox::Alignment::Right);
			a->setUnits("°");
			a->setTooltip("Set rotation angle in degrees.");
			a->setCallback([](float v){angle = v;});

			auto ccww = new Button(row, "", ENTYPO_ICON_CCW);
			ccww->setFixedSize(Vector2i(20,20));
			ccww->setFlags(Button::Flags::RadioButton);
			ccww->setPushed(!cw);
			ccww->setTooltip("Rotate in the counter-clockwise direction.");
			ccww->setChangeCallback([](bool b){cw = !b;});

			auto cww = new Button(row, "", ENTYPO_ICON_CW);
			cww->setFixedSize(Vector2i(20,20));
			cww->setFlags(Button::Flags::RadioButton);
			cww->setPushed(cw);
			cww->setTooltip("Rotate in the clockwise direction.");
			cww->setChangeCallback([](bool b){cw = b;});

			gui->addWidget("Rotate:", row);


			spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);


			row = new Widget(window);
			row->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto shx = new FloatBox<float>(row, shearX);
			shx->setSpinnable(true);
			shx->setEnabled(true);
			shx->setEditable(true);
			shx->setFontSize(gui->widgetFontSize());
			shx->setFixedSize(Vector2i(65+12, gui->fixedSize().y()));
			shx->setAlignment(TextBox::Alignment::Right);
			shx->setUnits("°");
			shx->setTooltip("Set horizontal skew/shear in degrees.");
			shx->setCallback([](float v){shearX = v;});

			auto shy = new FloatBox<float>(row, shearY);
			shy->setSpinnable(true);
			shy->setEnabled(true);
			shy->setEditable(true);
			shy->setFontSize(gui->widgetFontSize());
			shy->setFixedSize(Vector2i(65+13, gui->fixedSize().y()));
			shy->setAlignment(TextBox::Alignment::Right);
			shy->setUnits("°");
			shy->setTooltip("Set vertical skew/shear in degrees.");
			shy->setCallback([](float v){shearY = v;});

			gui->addWidget("Shear:", row);


			spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);


			row = new Widget(window);
			row->setLayout(new GridLayout(Orientation::Horizontal, 3, Alignment::Fill, 0, 2));
			vector<Button *> buttonGroup;

			buttonGroup.push_back(new Button(row, "+"));
			buttonGroup.push_back(new Button(row, "-"));
			buttonGroup.push_back(new Button(row, "+"));

			buttonGroup.push_back(new Button(row, "|"));
			buttonGroup.push_back(new Button(row, "+"));
			buttonGroup.push_back(new Button(row, "|"));

			buttonGroup.push_back(new Button(row, "+"));
			buttonGroup.push_back(new Button(row, "-"));
			buttonGroup.push_back(new Button(row, "+"));

			for (size_t i = 0; i < buttonGroup.size(); ++i)
			{
				Button * btn = buttonGroup[i];
				btn->setFlags(Button::RadioButton);
				btn->setFixedSize(Vector2i(16, 16));
				btn->setPushed(i == (size_t)anchor);
				btn->setIcon(i == (size_t)anchor ? ENTYPO_ICON_STOP : 0);
				if (i == (size_t)anchor)
					btn->setCaption("");
				btn->setChangeCallback(
					[i,btn](bool b)
					{
						if (b)
						{
							anchor = (HDRImage::CanvasAnchor) i;
							btn->setIcon(ENTYPO_ICON_STOP);
							btn->setCaption("");
						}
						else
						{
							btn->setCaption(i % 2 ? "-" : "+");
							if (i == 3 || i == 5)
								btn->setCaption("|");
							btn->setIcon(0);
						}
					});
			}

			row->setFixedSize(Vector2i(52, 52));
			gui->addWidget("Reference point:", row);


			spacer = new Widget(window);
			spacer->setFixedHeight(10);
			gui->addWidget("", spacer);


			gui->addVariable("Sampler:", sampler, true)
			   ->setItems(HDRImage::samplerNames());
			gui->addVariable("Border mode X:", borderModeX, true)
			   ->setItems(HDRImage::borderModeNames());
			gui->addVariable("Border mode Y:", borderModeY, true)
			   ->setItems(HDRImage::borderModeNames());

			auto s = gui->addVariable("Super-samples:", samples);
			s->setSpinnable(true);
			s->setMinValue(1);

			addOKCancelButtons(gui, window,
			                   [&, window]()
			                   {
				                   imageMgr->modifyImage(
					                   [&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
					                   {
						                   Affine2f t(Affine2f::Identity());

						                   Vector2f origin(0.f, 0.f);

						                   // find top-left corner
						                   switch (anchor)
						                   {
							                   case HDRImage::TOP_RIGHT:
							                   case HDRImage::MIDDLE_RIGHT:
							                   case HDRImage::BOTTOM_RIGHT:
								                   origin.x() = 1.f;
								                   break;

							                   case HDRImage::TOP_CENTER:
							                   case HDRImage::MIDDLE_CENTER:
							                   case HDRImage::BOTTOM_CENTER:
								                   origin.x() = 0.5f;
								                   break;

							                   case HDRImage::TOP_LEFT:
							                   case HDRImage::MIDDLE_LEFT:
							                   case HDRImage::BOTTOM_LEFT:
							                   default:
								                   origin.x() = 0.f;
								                   break;
						                   }
						                   switch (anchor)
						                   {
							                   case HDRImage::BOTTOM_LEFT:
							                   case HDRImage::BOTTOM_CENTER:
							                   case HDRImage::BOTTOM_RIGHT:
								                   origin.y() = 1.f;
								                   break;

							                   case HDRImage::MIDDLE_LEFT:
							                   case HDRImage::MIDDLE_CENTER:
							                   case HDRImage::MIDDLE_RIGHT:
								                   origin.y() = 0.5f;
								                   break;

							                   case HDRImage::TOP_LEFT:
							                   case HDRImage::TOP_CENTER:
							                   case HDRImage::TOP_RIGHT:
							                   default:
								                   origin.y() = 0.f;
								                   break;
						                   }

						                   t.translate(origin);
						                   t.scale(Vector2f(1.f/img->width(), 1.f/img->height()));
						                   t.translate(Vector2f(translateX, translateY));
						                   t.rotate(cw ? angle/180.f * M_PI : -angle/180.f * M_PI);
						                   Matrix2f sh;
						                   sh << 1, tan(shearX/180.f * M_PI), tan(shearY/180.f * M_PI), 1;
						                   t.linear() *= sh;
						                   t.scale(Vector2f(scaleX, scaleY)*.01f);
						                   t.scale(Vector2f(img->width(), img->height()));
						                   t.translate(-origin);

						                   t = t.inverse();

						                   function<Vector2f(const Vector2f &)> warp =
							                   [t](const Vector2f &uv)
							                   {
								                   return (t * uv).eval();
							                   };
						                   return {make_shared<HDRImage>(img->resampled(img->width(), img->height(),
						                                                                progress, warp, samples, sampler,
						                                                                borderModeX, borderModeY)),
						                           nullptr};
					                   });
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
	const int spacing = 2;
	setLayout(new GroupLayout(2, 4, 8, 10));

	new Label(this, "History", "sans-bold");

	auto buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

	m_undoButton = new Button(buttonRow, "Undo", ENTYPO_ICON_REPLY);
	m_undoButton->setCallback([&](){m_imageMgr->undo();});
	m_redoButton = new Button(buttonRow, "Redo", ENTYPO_ICON_FORWARD);
	m_redoButton->setCallback([&](){m_imageMgr->redo();});

	new Label(this, "Pixel/domain transformations", "sans-bold");

	auto grid = new Widget(this);
	grid->setLayout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

	// flip h
	m_filterButtons.push_back(new Button(grid, "Flip H", ENTYPO_ICON_LEFT_BOLD));
	m_filterButtons.back()->setCallback([&](){m_screen->flipImage(true);});
	m_filterButtons.back()->setFixedHeight(21);

	// rotate cw
	m_filterButtons.push_back(new Button(grid, "Rotate CW", ENTYPO_ICON_CW));
	m_filterButtons.back()->setFixedHeight(21);
	m_filterButtons.back()->setCallback(
		[this]()
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
		[this]()
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

	// resize
	m_filterButtons.push_back(createResizeButton(grid, m_screen, m_imageMgr));

	// free transform
	m_filterButtons.push_back(createFreeTransformButton(grid, m_screen, m_imageMgr));

	// remap
	m_filterButtons.push_back(createRemapButton(grid, m_screen, m_imageMgr));


	new Label(this, "Color/range adjustments", "sans-bold");
	buttonRow = new Widget(this);
	auto agrid = new AdvancedGridLayout({0, spacing, 0}, {}, 0);
	agrid->setColStretch(0, 1.0f);
	agrid->setColStretch(2, 1.0f);
	buttonRow->setLayout(agrid);

	agrid->appendRow(0);
	// invert
	m_filterButtons.push_back(new Button(buttonRow, "Invert", ENTYPO_ICON_ADJUST));
	m_filterButtons.back()->setFixedHeight(21);
	m_filterButtons.back()->setCallback(
		[this]()
		{
			m_imageMgr->modifyImage(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->inverted()),
					        make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->inverted(); })};
				});
		});
	agrid->setAnchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->rowCount()-1));

	// clamp
	m_filterButtons.push_back(new Button(buttonRow, "Clamp", ENTYPO_ICON_ADJUST));
	m_filterButtons.back()->setFixedHeight(21);
	m_filterButtons.back()->setCallback(
		[this]()
		{
			m_imageMgr->modifyImage(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->unaryExpr(
						[](const Color4 & c)
						{
							return Color4(clamp01(c.r), clamp01(c.g), clamp01(c.b), clamp01(c.a));
						}).eval()), nullptr };
				});
		});
	agrid->setAnchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(2, agrid->rowCount()-1));

//	buttonRow = new Widget(this);
//	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, 2));

	agrid->appendRow(spacing);  // spacing

	m_filterButtons.push_back(createExposureGammaButton(buttonRow, m_screen, m_imageMgr));
	agrid->appendRow(0);
	agrid->setAnchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->rowCount()-1, 3, 1));

	agrid->appendRow(spacing);  // spacing
	m_filterButtons.push_back(createBrightnessContrastButton(buttonRow, m_screen, m_imageMgr));
	agrid->appendRow(0);
	agrid->setAnchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->rowCount()-1, 3, 1));

	agrid->appendRow(spacing);  // spacing
	m_filterButtons.push_back(createFilmicTonemappingButton(buttonRow, m_screen, m_imageMgr));
	agrid->appendRow(0);
	agrid->setAnchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->rowCount()-1, 3, 1));

	agrid->appendRow(spacing);  // spacing
	m_filterButtons.push_back(createHueSaturationButton(buttonRow, m_screen, m_imageMgr));
	agrid->appendRow(0);
	agrid->setAnchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->rowCount()-1, 3, 1));

	agrid->appendRow(spacing);  // spacing
	m_filterButtons.push_back(createColorSpaceButton(buttonRow, m_screen, m_imageMgr));
	agrid->appendRow(0);
	agrid->setAnchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->rowCount()-1, 3, 1));

	new Label(this, "Filters", "sans-bold");
	buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, spacing));
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