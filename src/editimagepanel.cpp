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
	static float offset = 0.0f;
	auto b = new Button(parent, name, ENTYPO_ICON_ADJUST);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(75, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			// exposure
			auto eFloat = gui->addVariable("Exposure:", exposure);
			eFloat->setSpinnable(true);
			eFloat->numberFormat("%1.2f");
			eFloat->setValueIncrement(0.1f);
			eFloat->setMinMaxValues(-10.f, 10.f);

			auto eSlider = new Slider(window);
			eSlider->setValue(exposure);
			eSlider->setRange({-10.f, 10.f});
			gui->addWidget("", eSlider);

			// offset
			auto oFloat = gui->addVariable("Offset:", offset);
			oFloat->setSpinnable(true);
			oFloat->numberFormat("%1.2f");
			oFloat->setValueIncrement(0.01);
			oFloat->setMinMaxValues(-1.f, 1.f);

			auto oSlider = new Slider(window);
			oSlider->setValue(offset);
			oSlider->setRange({-1.f, 1.f});
			gui->addWidget("", oSlider);

			// gamma
			auto gFloat = gui->addVariable("Gamma:", gamma);
			gFloat->setSpinnable(true);
			gFloat->numberFormat("%1.2f");
			gFloat->setValueIncrement(0.1f);
			gFloat->setMinMaxValues(0.0001f, 10.f);

			auto gSlider = new Slider(window);
			gSlider->setValue(gamma);
			gSlider->setRange({0.0001f, 10.f});
			gui->addWidget("", gSlider);



			auto spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

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


			auto eCb = [eFloat,eSlider,graphCb](float e)
			{
				exposure = e;
				eFloat->setValue(e);
				eSlider->setValue(e);
				graphCb();
			};

			auto oCb = [oFloat,oSlider,graphCb](float o)
			{
				offset = o;
				oFloat->setValue(o);
				oSlider->setValue(o);
				graphCb();
			};

			auto gCb = [gFloat,gSlider,graphCb](float g)
			{
				gamma = g;
				gFloat->setValue(g);
				gSlider->setValue(g);
				graphCb();
			};

			eSlider->setCallback(eCb);
			eFloat->setCallback(eCb);
			oSlider->setCallback(oCb);
			oFloat->setCallback(oCb);
			gSlider->setCallback(gCb);
			gFloat->setCallback(gCb);

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

			// brightness
			string help = "Shift the 50% gray midpoint.\n\n"
						  "Setting brightness > 0 boosts a previously darker value to 50%, "
						  "while brightness < 0 dims a previously brighter value to 50%.";

			auto bFloat = gui->addVariable("Brightness:", brightness);
			bFloat->setSpinnable(true);
			bFloat->numberFormat("%1.2f");
			bFloat->setValueIncrement(0.01f);
			bFloat->setMinMaxValues(-1.f, 1.f);
			bFloat->setTooltip(help);

			auto bSlider = new Slider(window);
			bSlider->setValue(brightness);
			bSlider->setRange({-1.f, 1.f});
			bSlider->setTooltip(help);
			gui->addWidget("", bSlider);

			// contrast
			help = "Change the slope/gradient at the new 50% midpoint.";
			auto cFloat = gui->addVariable("Contrast:", contrast);
			cFloat->setSpinnable(true);
			cFloat->numberFormat("%1.2f");
			cFloat->setValueIncrement(0.01f);
			cFloat->setMinMaxValues(-1.f, 1.f);
			cFloat->setTooltip(help);

			auto cSlider = new Slider(window);
			cSlider->setValue(contrast);
			cSlider->setRange({-1.f, 1.f});
			cSlider->setTooltip(help);
			gui->addWidget("", cSlider);

			auto lCheck = gui->addVariable("Linear:", linear, true);
			gui->addVariable("Channel:", channel, true)->setItems({"RGB", "Luminance", "Chromaticity"});

			auto spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

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

			auto bCb = [bFloat,bSlider,graphCb](float b)
			{
				brightness = b;
				bFloat->setValue(b);
				bSlider->setValue(b);
				graphCb();
			};
			bSlider->setCallback(bCb);
			bFloat->setCallback(bCb);

			auto cCb = [cFloat,cSlider,graphCb](float c)
			{
				contrast = c;
				cFloat->setValue(c);
				cSlider->setValue(c);
				graphCb();
			};
			cSlider->setCallback(cCb);
			cFloat->setCallback(cCb);

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
	static const auto activeColor = Color(255, 255, 255, 200);
	auto b = new Button(parent, name, ENTYPO_ICON_VOLUME);
	b->setFixedHeight(21);
	b->setCallback(
		[&, parent, screen, imageMgr]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->setFixedSize(Vector2i(100, 20));

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			// toe strength
			auto tsFloat = gui->addVariable("Toe Strength:", params.toeStrength);
			tsFloat->setSpinnable(true);
			tsFloat->numberFormat("%1.2f");
			tsFloat->setValueIncrement(0.01f);
			tsFloat->setMinMaxValues(0.f, 1.f);

			auto tsSlider = new Slider(window);
			tsSlider->setValue(params.toeStrength);
			tsSlider->setRange({0.f, 1.f});
			gui->addWidget("", tsSlider);

			// toe length
			auto tlFloat = gui->addVariable("Toe length:", params.toeLength);
			tlFloat->setSpinnable(true);
			tlFloat->numberFormat("%1.2f");
			tlFloat->setValueIncrement(0.01f);
			tlFloat->setMinMaxValues(0.f, 1.f);

			auto tlSlider = new Slider(window);
			tlSlider->setValue(params.toeLength);
			tlSlider->setRange({0.f, 1.f});
			gui->addWidget("", tlSlider);

			// shoulder strength
			auto ssFloat = gui->addVariable("Shoulder strength:", params.shoulderStrength);
			ssFloat->setSpinnable(true);
			ssFloat->numberFormat("%1.2f");
			ssFloat->setValueIncrement(0.1f);
			ssFloat->setMinMaxValues(0.f, 10.f);

			auto ssSlider = new Slider(window);
			ssSlider->setValue(params.shoulderStrength);
			ssSlider->setRange({0.f, 10.f});
			gui->addWidget("", ssSlider);

			// shoulder length
			auto slFloat = gui->addVariable("Shoulder length:", params.shoulderLength);
			slFloat->setSpinnable(true);
			slFloat->numberFormat("%1.2f");
			slFloat->setValueIncrement(0.01f);
			slFloat->setMinMaxValues(0.f, 1.f);

			auto slSlider = new Slider(window);
			slSlider->setValue(params.shoulderLength);
			slSlider->setRange({0.f, 1.f});
			gui->addWidget("", slSlider);

			// shoulder angle
			auto saFloat = gui->addVariable("Shoulder angle:", params.shoulderAngle);
			saFloat->setSpinnable(true);
			saFloat->numberFormat("%1.2f");
			saFloat->setValueIncrement(0.01f);
			saFloat->setMinMaxValues(0.f, 1.f);

			auto saSlider = new Slider(window);
			saSlider->setValue(params.shoulderAngle);
			saSlider->setRange({0.f, 1.f});
			gui->addWidget("", saSlider);

			// gamma
			auto gFloat = gui->addVariable("Gamma:", params.gamma);
			gFloat->setSpinnable(true);
			gFloat->numberFormat("%1.2f");
			gFloat->setValueIncrement(0.01f);
			gFloat->setMinMaxValues(0.01f, 5.f);

			auto gSlider = new Slider(window);
			gSlider->setValue(params.gamma);
			gSlider->setRange({0.01f, 5.f});
			gui->addWidget("", gSlider);


			auto spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

			// graph
			auto graph = new MultiGraph(window, Color(255, 255, 255, 30));
			graph->addPlot(activeColor);
			graph->setFixedSize(Vector2i(200, 150));
			graph->setFilled(false);
			graph->setWell(false);
			gui->addWidget("", graph);

			auto graphCb = [graph]()
			{
				float range = pow(2.f, params.shoulderStrength);
				FilmicToneCurve::CurveParamsDirect directParams;
				FilmicToneCurve::calcDirectParamsFromUser(directParams, params);
				FilmicToneCurve::createCurve(fCurve, directParams);
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

			auto tsCb = [tsFloat,tsSlider,graphCb](float v)
			{
				params.toeStrength = v;
				tsFloat->setValue(v);
				tsSlider->setValue(v);
				graphCb();
			};
			tsSlider->setCallback(tsCb);
			tsFloat->setCallback(tsCb);
			
			auto tlCb = [tlFloat,tlSlider,graphCb](float v)
			{
				params.toeLength = v;
				tlFloat->setValue(v);
				tlSlider->setValue(v);
				graphCb();
			};
			tlSlider->setCallback(tlCb);
			tlFloat->setCallback(tlCb);
			
			auto ssCb = [ssFloat,ssSlider,graphCb](float v)
			{
				params.shoulderStrength = v;
				ssFloat->setValue(v);
				ssSlider->setValue(v);
				graphCb();
			};
			ssSlider->setCallback(ssCb);
			ssFloat->setCallback(ssCb);

			auto slCb = [slFloat,slSlider,graphCb](float v)
			{
				params.shoulderLength = v;
				slFloat->setValue(v);
				slSlider->setValue(v);
				graphCb();
			};
			slSlider->setCallback(slCb);
			slFloat->setCallback(slCb);

			auto saCb = [saFloat,saSlider,graphCb](float v)
			{
				params.shoulderAngle = v;
				saFloat->setValue(v);
				saSlider->setValue(v);
				graphCb();
			};
			saSlider->setCallback(saCb);
			saFloat->setCallback(saCb);

			auto gCb = [gFloat,gSlider,graphCb](float v)
			{
				params.gamma = v;
				gFloat->setValue(v);
				gSlider->setValue(v);
				graphCb();
			};
			gSlider->setCallback(gCb);
			gFloat->setCallback(gCb);

			addOKCancelButtons(gui, window,
			                   [&, window]()
			                   {
				                   imageMgr->modifyImage(
					                   [&](const shared_ptr<const HDRImage> &img) -> ImageCommandResult
					                   {
						                   return {make_shared<HDRImage>(img->unaryExpr(
							                   [](const Color4 & c)
							                   {
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
			gui->setFixedSize(Vector2i(75, 20));

			Widget* spacer = nullptr;

			auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			// hue
			auto hFloat = gui->addVariable("Hue:", hue);
			hFloat->setSpinnable(true);
			hFloat->numberFormat("%1.1f");
			hFloat->setValueIncrement(1.f);
			hFloat->setMinMaxValues(-180.f, 180.f);

			auto hSlider = new Slider(window);
			hSlider->setValue(hue);
			hSlider->setRange({-180.f, 180.f});
			gui->addWidget("", hSlider);

			// saturation
			auto sFloat = gui->addVariable("Saturation:", saturation);
			sFloat->setSpinnable(true);
			sFloat->numberFormat("%1.1f");
			sFloat->setValueIncrement(1.f);
			sFloat->setMinMaxValues(-100.f, 100.f);

			auto sSlider = new Slider(window);
			sSlider->setValue(saturation);
			sSlider->setRange({-100.f, 100.f});
			gui->addWidget("", sSlider);

			// lightness
			auto lFloat = gui->addVariable("Lightness:", lightness);
			lFloat->setSpinnable(true);
			lFloat->numberFormat("%1.1f");
			lFloat->setValueIncrement(1.f);
			lFloat->setMinMaxValues(-100.f, 100.f);

			auto lSlider = new Slider(window);
			lSlider->setValue(lightness);
			lSlider->setRange({-100.f, 100.f});
			gui->addWidget("", lSlider);


			spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

			auto fixedRainbow = new HSLGradient(window);
			gui->addWidget("", fixedRainbow);

			spacer = new Widget(window);
			spacer->setFixedHeight(5);
			gui->addWidget("", spacer);

			auto dynamicRainbow = new HSLGradient(window);
			gui->addWidget("", dynamicRainbow);

			hSlider->setCallback([hFloat,dynamicRainbow](float h){ hue = h; hFloat->setValue(h); dynamicRainbow->setHueOffset(h); });
			hFloat->setCallback([hSlider,dynamicRainbow](float h){ hue = h; hSlider->setValue(h); dynamicRainbow->setHueOffset(h);});

			sSlider->setCallback([sFloat,dynamicRainbow](float s){ saturation = s; sFloat->setValue(s); dynamicRainbow->setSaturation((s + 100.f)/200.f); });
			sFloat->setCallback([sSlider,dynamicRainbow](float s){ saturation = s; sSlider->setValue(s); dynamicRainbow->setSaturation((s + 100.f)/200.f);});

			lSlider->setCallback([lFloat,dynamicRainbow](float l){ lightness = l; lFloat->setValue(l); dynamicRainbow->setLightness((l + 100.f)/200.f); });
			lFloat->setCallback([lSlider,dynamicRainbow](float l){ lightness = l; lSlider->setValue(l); dynamicRainbow->setLightness((l + 100.f)/200.f);});

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


	m_filterButtons.push_back(createResizeButton(grid, m_screen, m_imageMgr));
	// resample
	m_filterButtons.push_back(createResampleButton(grid, m_screen, m_imageMgr));


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