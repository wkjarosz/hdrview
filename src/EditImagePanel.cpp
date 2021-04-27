//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "EditImagePanel.h"
#include "Common.h"
#include "GLImage.h"
#include "HDRViewScreen.h"
#include "HDRImage.h"
#include "ImageListPanel.h"
#include "EnvMap.h"
#include "Colorspace.h"
#include "HSLGradient.h"
#include "MultiGraph.h"
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
	auto fBox = gui->add_variable(name, variable);
	fBox->set_spinnable(true);
	fBox->number_format("%1.2f");
	fBox->set_value_increment(step);
	fBox->set_min_max_values(mn, mx);
	fBox->set_tooltip(help);

	auto fSlider = new Slider(parent);
	fSlider->set_value(variable);
	fSlider->set_range({mn, mx});
	fSlider->set_tooltip(help);
	gui->add_widget("", fSlider);

	auto fCb = [fBox,fSlider,cb,&variable](float v)
	{
		variable = v;
		fBox->set_value(v);
		fSlider->set_value(v);
		cb();
	};
	fSlider->set_callback(fCb);
	fBox->set_callback(fCb);
	return fCb;
}


void addOKCancelButtons(FormHelper * gui, Window * window,
                        const function<void()> &OKCallback,
                        const function<void()> &cancelCallback = nullptr)
{
	auto spacer = new Widget(window);
	spacer->set_fixed_height(15);
	gui->add_widget("", spacer);

	auto w = new Widget(window);
	w->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 5));
	auto b = new Button(w, "Cancel", window->theme()->m_message_alt_button_icon);
	b->set_callback(
		[window,cancelCallback]()
		{
			if (cancelCallback)
				cancelCallback();
			window->dispose();
		});
	b = new Button(w, "OK", window->theme()->m_message_primary_button_icon);
	b->set_callback(
		[window,OKCallback]()
		{
			OKCallback();
			window->dispose();
		});
	gui->add_widget("", w);
}

Button * createColorSpaceButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static string name = "Convert color space...";
	static EColorSpace src = LinearSRGB_CS, dst = CIEXYZ_CS;
	auto b = new Button(parent, name, FA_PALETTE);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(125, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);

			gui->add_variable("Source:", src, true)->set_items(colorSpaceNames());
			gui->add_variable("Destination:", dst, true)->set_items(colorSpaceNames());

			addOKCancelButtons(gui, window,
				[&]()
				{
					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->unaryExpr([](const Color4 & c){return c.convert(dst, src);}).eval()),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * createExposureGammaButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static string name = "Exposure/Gamma...";
	static float exposure = 0.0f;
	static float gamma = 1.0f;
	static float offset = 0.0f;
	auto b = new Button(parent, name, FA_ADJUST);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(55, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes


			// graph
			auto graph = new MultiGraph(window, Color(255, 255, 255, 30));
			graph->add_plot(Color(255, 255, 255, 200));
			graph->add_plot(Color(255, 255, 255, 50));
			graph->set_fixed_size(Vector2i(200, 200));
			graph->set_filled(false);
			graph->set_well(false);
			graph->set_values(linspaced(257, 0.0f, 1.0f), 0);
			graph->set_values({0.5f, 0.5f}, 2);
			int numTicks = 5;
			// create the x tick marks
			auto xTicks = linspaced(numTicks, 0.0f, 1.0f);
			// create the x tick labels
			vector<string> xTickLabels(numTicks);
			for (int i = 0; i < numTicks; ++i)
				xTickLabels[i] = fmt::format("{:.2f}", xTicks[i]);
			graph->set_xticks(xTicks, xTickLabels);
			graph->set_yticks(xTicks);
			gui->add_widget("", graph);

			auto graphCb = [graph]()
			{
				auto lCurve = linspaced(257, 0.0f, 1.0f);
				for (auto&& i : lCurve)
					i = pow(pow(2.0f, exposure) * i + offset, 1.0f/gamma);
				graph->set_values(lCurve, 1);
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
				[&]()
				{
					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							spdlog::get("console")->debug("{}; {}; {}", exposure, offset, gamma);
							return {make_shared<HDRImage>((Color4(pow(2.0f, exposure), 1.f) * (*img) + Color4(offset, 0.f)).pow(Color4(1.0f/gamma))),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * createBrightnessContrastButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
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
	auto b = new Button(parent, name, FA_ADJUST);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(100, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes


			// graph
			auto graph = new MultiGraph(window, Color(255, 255, 255, 30));
			graph->add_plot(inactiveColor);
			graph->add_plot(activeColor);
			graph->add_plot(Color(255, 255, 255, 50));
			graph->set_fixed_size(Vector2i(200, 200));
			graph->set_filled(false);
			graph->set_well(false);
			graph->set_values(linspaced(257, 0.0f, 1.0f), 0);
			graph->set_values({0.5f, 0.5f}, 3);
			int numTicks = 5;
			// create the x tick marks
			auto xTicks = linspaced(numTicks, 0.0f, 1.0f);
			// create the x tick labels
			vector<string> xTickLabels(numTicks);
			for (int i = 0; i < numTicks; ++i)
				xTickLabels[i] = fmt::format("{:.2f}", xTicks[i]);
			graph->set_xticks(xTicks, xTickLabels);
			graph->set_yticks(xTicks);

			gui->add_widget("", graph);

			auto graphCb = [graph]()
			{
				float slope = float(std::tan(lerp(0.0, M_PI_2, contrast/2.0 + 0.5)));
				float midpoint = (1.f-brightness)/2.f;
				float bias = (brightness + 1.f) / 2.f;
				auto lCurve = linspaced(257, 0.0f, 1.0f);
				for (auto&& i : lCurve)
					i = brightnessContrastL(i, slope, midpoint);
				lCurve.back() = 1;
				graph->set_values(lCurve, 1);

				auto nlCurve = linspaced(257, 0.0f, 1.0f);
				for (auto&& i : nlCurve)
					i = brightnessContrastNL(i, slope, bias);

				nlCurve.back() = 1;
				graph->set_values(nlCurve, 2);
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

			auto lCheck = gui->add_variable("Linear:", linear, true);
			gui->add_variable("Channel:", channel, true)->set_items({"RGB", "Luminance", "Chromaticity"});

			lCheck->set_callback(
				[graph](bool b)
				{
					linear = b;
					graph->set_foreground_color(linear ? activeColor : inactiveColor, 1);
					graph->set_foreground_color(linear ? inactiveColor : activeColor, 2);
				});

			graph->set_drag_callback(
				[bCb,cCb](const Vector2f & frac)
				{
					bCb(lerp(1.f, -1.f, clamp01(frac.x())));
					cCb(lerp(-1.f, 1.f, clamp01(frac.y())));
				});

			addOKCancelButtons(gui, window,
               [&]()
               {
	               imagesPanel->modify_image(
		               [&](const shared_ptr<const HDRImage> &img) -> ImageCommandResult
		               {
			               return {make_shared<HDRImage>(img->brightnessContrast(brightness, contrast, linear, channelMap[channel])),
			                       nullptr};
		               });
               });

			window->center();
			window->request_focus();
		});
	return b;
}

Button * createFilmicTonemappingButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static string name = "Filmic tonemapping...";
	static FilmicToneCurve::FullCurve fCurve;
	static FilmicToneCurve::CurveParamsUser params;
	static float vizFstops = 1.f;
	static const auto activeColor = Color(255, 255, 255, 200);
	auto b = new Button(parent, name, FA_ADJUST);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(55, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			// graph
			MultiGraph* graph = new MultiGraph(window, Color(255, 255, 255, 30));
			graph->add_plot(activeColor);
			graph->set_fixed_size(Vector2i(200, 200));
			graph->set_filled(false);
			graph->set_well(false);
			gui->add_widget("", graph);

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

				graph->set_values(linspaced(257, 0.0f, range), 0);
				auto lCurve = linspaced(257, 0.0f, range);
				for (auto&& i : lCurve)
					i = fCurve.eval(i);
				graph->set_values(lCurve, 1);

				int numTicks = 5;
				// create the x tick marks
				auto xTicks = linspaced(numTicks, 0.0f, 1.0f);
				// create the x tick labels
				vector<string> xTickLabels(numTicks);
				for (int i = 0; i < numTicks; ++i)
					xTickLabels[i] = fmt::format("{:.2f}", range*xTicks[i]);
				graph->set_xticks(xTicks, xTickLabels);
				graph->set_yticks(linspaced(3, 0.0f, 1.0f));
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
				[&]()
				{
				   imagesPanel->modify_image(
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
			window->request_focus();
		});
	return b;
}

Button * createHueSaturationButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static string name = "Hue/Saturation...";
	static float hue = 0.0f;
	static float saturation = 0.0f;
	static float lightness = 0.0f;
	auto b = new Button(parent, name, FA_PALETTE);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(55, 20));

			Widget* spacer = nullptr;

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes



			auto fixedRainbow = new HSLGradient(window);
			auto dynamicRainbow = new HSLGradient(window);
			fixedRainbow->set_fixed_width(256);
			dynamicRainbow->set_fixed_width(256);

			auto cb = [dynamicRainbow]()
			{
				dynamicRainbow->set_hue_offset(hue);
				dynamicRainbow->set_saturation((saturation + 100.f)/200.f);
				dynamicRainbow->set_lightness((lightness + 100.f)/200.f);
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
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);

			gui->add_widget("", fixedRainbow);

			spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);

			gui->add_widget("", dynamicRainbow);

			addOKCancelButtons(gui, window,
			                   [&]()
			                   {
				                   imagesPanel->modify_image(
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
			window->request_focus();
		});
	return b;
}

Button * createGaussianFilterButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static float width = 1.0f, height = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static bool exact = false;
	static string name = "Gaussian blur...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
			// window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->add_variable("Width:", width);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w->set_value_increment(5.f);
			w->set_units("px");
			w = gui->add_variable("Height:", height);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w->set_value_increment(5.f);
			w->set_units("px");

			gui->add_variable("Border mode X:", borderModeX, true)
			   ->set_items(HDRImage::borderModeNames());
			gui->add_variable("Border mode Y:", borderModeY, true)
			   ->set_items(HDRImage::borderModeNames());

			gui->add_variable("Exact (slow!):", exact, true);


			addOKCancelButtons(gui, window,
				[&]()
				{
					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(exact ? img->GaussianBlurred(width, height, progress, borderModeX, borderModeY) :
							        img->fastGaussianBlurred(width, height, progress, borderModeX, borderModeY)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * createBoxFilterButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static float width = 1.0f, height = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Box blur...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->add_variable("Width:", width);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w->set_units("px");
			w = gui->add_variable("Height:", height);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w->set_units("px");

			gui->add_variable("Border mode X:", borderModeX, true)
			   ->set_items(HDRImage::borderModeNames());
			gui->add_variable("Border mode Y:", borderModeY, true)
			   ->set_items(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&]()
				{
					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->boxBlurred(width, height, progress, borderModeX, borderModeY)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * createBilateralFilterButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static float rangeSigma = 1.0f, valueSigma = 0.1f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Bilateral filter...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->add_variable("Range sigma:", rangeSigma);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w = gui->add_variable("Value sigma:", valueSigma);
			w->set_spinnable(true);
			w->set_min_value(0.0f);

			gui->add_variable("Border mode X:", borderModeX, true)
			   ->set_items(HDRImage::borderModeNames());
			gui->add_variable("Border mode Y:", borderModeY, true)
			   ->set_items(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&]()
				{
					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->bilateralFiltered(valueSigma, rangeSigma,
							                              progress, borderModeX, borderModeY)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * createUnsharpMaskFilterButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static float sigma = 1.0f, strength = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Unsharp mask...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->add_variable("Sigma:", sigma);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w = gui->add_variable("Strength:", strength);
			w->set_spinnable(true);
			w->set_min_value(0.0f);

			gui->add_variable("Border mode X:", borderModeX, true)
			   ->set_items(HDRImage::borderModeNames());
			gui->add_variable("Border mode Y:", borderModeY, true)
			   ->set_items(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&]()
				{
					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->unsharpMasked(sigma, strength, progress, borderModeX, borderModeY)),
							        nullptr};
						});
				});


			window->center();
			window->request_focus();
		});
	return b;
}

Button * createMedianFilterButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static float radius = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Median filter...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->add_variable("Radius:", radius);
			w->set_spinnable(true);
			w->set_min_value(0.0f);

			gui->add_variable("Border mode X:", borderModeX, true)
			   ->set_items(HDRImage::borderModeNames());
			gui->add_variable("Border mode Y:", borderModeY, true)
			   ->set_items(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&]()
				{
					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->medianFiltered(radius, progress, borderModeX, borderModeY)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * createResizeButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static int width = 128, height = 128;
	static string name = "Resize...";
	static bool aspect = true;
	auto b = new Button(parent, name, FA_EXPAND);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(0, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
			window->set_modal(true);

			auto row = new Widget(window);
			row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			width = imagesPanel->current_image()->width();
			height = imagesPanel->current_image()->height();

			auto w = new IntBox<int>(row, width);
			auto link = new ToolButton(row, FA_LINK);
			auto h = new IntBox<int>(row, height);

			w->set_spinnable(true);
			w->set_enabled(true);
			w->set_editable(true);
			w->set_min_value(1);
			w->set_font_size(gui->widget_font_size());
			w->set_fixed_size(Vector2i(80, gui->fixed_size().y()));
			w->set_alignment(TextBox::Alignment::Right);
			w->set_units("px");

			link->set_fixed_size(Vector2i(20,20));
			link->set_pushed(aspect);

			h->set_spinnable(true);
			h->set_enabled(true);
			h->set_editable(true);
			h->set_min_value(1);
			h->set_font_size(gui->widget_font_size());
			h->set_fixed_size(Vector2i(80, gui->fixed_size().y()));
			h->set_alignment(TextBox::Alignment::Right);
			h->set_units("px");

			link->set_change_callback(
				[w,imagesPanel](bool preserve)
				{
					if (preserve)
					{
						float aspect = imagesPanel->current_image()->width() / (float)imagesPanel->current_image()->height();
						width = max(1, (int)round(height * aspect));
						w->set_value(width);
					}
					aspect = preserve;
				});

			w->set_callback(
				[h,link,imagesPanel](int w)
				{
					width = w;
					if (link->pushed())
					{
						float aspect = imagesPanel->current_image()->width() / (float)imagesPanel->current_image()->height();
						height = max(1, (int)round(w / aspect));
						h->set_value(height);
					}
				});

			h->set_callback(
				[w,link,imagesPanel](int h)
				{
					height = h;
					if (link->pushed())
					{
						float aspect = imagesPanel->current_image()->width() / (float)imagesPanel->current_image()->height();
						width = max(1, (int)round(height * aspect));
						w->set_value(width);
					}
				});


			gui->add_widget("", row);

			addOKCancelButtons(gui, window,
				[&]()
				{
					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->resized(width, height)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * createRemapButton(Widget *parent, HDRViewScreen *screen, ImageListPanel *imagesPanel)
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
	auto b = new Button(parent, name, FA_GLOBE);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(135, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			width = imagesPanel->current_image()->width();
			auto w = gui->add_variable("Width:", width);
			w->set_spinnable(true);
			w->set_min_value(1);
			w->set_units("px");

			height = imagesPanel->current_image()->height();
			auto h = gui->add_variable("Height:", height);
			h->set_spinnable(true);
			h->set_min_value(1);
			h->set_units("px");

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

			w->set_callback(
				[h,recomputeH](int w)
				{
					width = w;
					recomputeH();
					h->set_value(height);
				});

			h->set_callback(
				[w,recomputeW](int h)
				{
					height = h;
					recomputeW();
					w->set_value(width);
				});


			auto autoAspectCheckbox = gui->add_variable("Auto aspect ratio:", autoAspect, true);

			auto src = gui->add_variable("Source map:", from, true);
			auto dst = gui->add_variable("Target map:", to, true);

			src->set_items(envMappingNames());
			src->set_callback([gui,recomputeW](EEnvMappingUVMode m)
			                 {
				                 from = m;
				                 recomputeW();
				                 gui->refresh();
			                 });
			dst->set_items(envMappingNames());
			dst->set_callback([gui,recomputeW](EEnvMappingUVMode m)
			                 {
				                 to = m;
				                 recomputeW();
				                 gui->refresh();
			                 });

			auto spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);

			auto btn = new Button(window, "Swap source/target", FA_EXCHANGE_ALT);
			btn->set_callback([gui,recomputeW,recomputeH](){std::swap(from,to);recomputeW();recomputeH();gui->refresh();});
			btn->set_fixed_size(gui->fixed_size());
			gui->add_widget(" ", btn);

			autoAspectCheckbox->set_callback(
				[w,recomputeW](bool preserve)
				{
					autoAspect = preserve;
					recomputeW();
					w->set_value(width);
				});

			recomputeW();
			gui->refresh();


			gui->add_variable("Sampler:", sampler, true)
			   ->set_items(HDRImage::samplerNames());
			gui->add_variable("Border mode X:", borderModeX, true)
			   ->set_items(HDRImage::borderModeNames());
			gui->add_variable("Border mode Y:", borderModeY, true)
			   ->set_items(HDRImage::borderModeNames());

			w = gui->add_variable("Super-samples:", samples);
			w->set_spinnable(true);
			w->set_min_value(1);

			addOKCancelButtons(gui, window,
				[&]()
				{
//					auto dst2xyz = envMapUVToXYZ(to);
//					auto xyz2src = XYZToEnvMapUV(from);
//					auto warp = [dst2xyz,xyz2src](const Vector2f &uv) { return xyz2src(dst2xyz(uv)); };
					auto warp = [](const Eigen::Vector2f &uv) { return convertEnvMappingUV(from, to, uv); };

					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->resampled(width, height, progress, warp, samples, sampler,
							                                            borderModeX, borderModeY)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * createShiftButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static HDRImage::BorderMode borderModeX = HDRImage::REPEAT, borderModeY = HDRImage::REPEAT;
	static float dx = 0.f, dy = 0.f;
	static string name = "Shift...";
	auto b = new Button(parent, name, FA_ARROWS_ALT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(125, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//           window->set_modal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

			auto w = gui->add_variable("X offset:", dx);
			w->set_spinnable(true);
			w->set_units("px");

			w = gui->add_variable("Y offset:", dy);
			w->set_spinnable(true);
			w->set_units("px");

			gui->add_variable("Sampler:", sampler, true)
			   ->set_items(HDRImage::samplerNames());
			gui->add_variable("Border mode X:", borderModeX, true)
			   ->set_items(HDRImage::borderModeNames());
			gui->add_variable("Border mode Y:", borderModeY, true)
			   ->set_items(HDRImage::borderModeNames());

			addOKCancelButtons(gui, window,
				[&]()
				{
					imagesPanel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							// by default use a no-op passthrough warp function
							function<Eigen::Vector2f(const Eigen::Vector2f &)> shift =
								[&](const Eigen::Vector2f &uv)
								{
									return (uv + Eigen::Vector2f(dx / img->width(), dy / img->height())).eval();
								};
							return {make_shared<HDRImage>(img->resampled(img->width(), img->height(),
							                                             progress, shift, 1, sampler,
							                                             borderModeX, borderModeY)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}


Button * createCanvasSizeButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
{
	static int width = 128, height = 128;
	static Color bgColor(.8f, .8f, .8f, 1.f);
	static float alpha = 1.f;
	static float EV = 0.f;
	static HDRImage::CanvasAnchor anchor = HDRImage::MIDDLE_CENTER;
	static string name = "Canvas size...";
	static bool relative = false;
	auto b = new Button(parent, name, FA_CROP);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//			window->set_modal(true);

			width = imagesPanel->current_image()->width();
			auto w = gui->add_variable("Width:", width);
			w->set_spinnable(true);
			w->set_min_value(1);
			w->set_units("px");

			height = imagesPanel->current_image()->height();
			auto h = gui->add_variable("Height:", height);
			h->set_spinnable(true);
			h->set_min_value(1);
			h->set_units("px");

			relative = false;
			auto r = gui->add_variable("Relative:", relative, true);
			r->set_callback(
				[w,h,imagesPanel](bool rel)
				{
					if (rel)
					{
						w->set_min_value(-imagesPanel->current_image()->width()+1);
						h->set_min_value(-imagesPanel->current_image()->height()+1);
						width = w->value() - imagesPanel->current_image()->width();
						height = h->value() - imagesPanel->current_image()->height();
						w->set_value(width);
						h->set_value(height);
					}
					else
					{
						w->set_min_value(1);
						h->set_min_value(1);
						width = w->value() + imagesPanel->current_image()->width();
						height = h->value() + imagesPanel->current_image()->height();
						w->set_value(width);
						h->set_value(height);
					}
					relative = rel;
				});

			auto spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);


			auto w2 = new Widget(window);
			int bw = gui->fixed_size().y();
			int pad = 2;
			w2->set_layout(new GridLayout(Orientation::Horizontal, 3, Alignment::Fill, 0, pad));
			vector<Button *> buttonGroup;

			int icons[] = {FA_PLUS, 		FA_ARROW_UP, 	FA_PLUS,
						   FA_ARROW_LEFT, 	FA_PLUS, 		FA_ARROW_RIGHT,
						   FA_PLUS, 		FA_ARROW_DOWN, 	FA_PLUS};

			for (size_t i = 0; i < sizeof(icons)/sizeof(icons[0]); ++i)
			{
				Button * btn = new Button(w2, "", icons[i]);

				btn->set_flags(Button::RadioButton);
				btn->set_fixed_size(Vector2i(bw, bw));
				btn->set_pushed(i == (size_t)anchor);
				btn->set_change_callback([i](bool b){if (b) anchor = (HDRImage::CanvasAnchor)i;});
				buttonGroup.push_back(btn);
			}

			w2->set_fixed_size(Vector2i(3*bw+2*pad, 3*bw+2*pad));
			gui->add_widget("Anchor:", w2);


			spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);

			auto popupBtn = new PopupButton(window, "", 0);
			popupBtn->set_background_color(Color(bgColor.r(), bgColor.g(), bgColor.b(), alpha));
			gui->add_widget("Extension color:", popupBtn);

			auto popup = popupBtn->popup();
			popup->set_layout(new GroupLayout());

			auto colorwheel = new ColorWheel(popup);
			colorwheel->set_color(Color(bgColor.r(), bgColor.g(), bgColor.b(), alpha));

			auto panel = new Widget(popup);
//			panel->set_layout(new GridLayout(Orientation::Horizontal, 3, Alignment::Fill, 0, 0));
			auto agrid = new AdvancedGridLayout({0, 20, 0}, {});
			agrid->set_margin(0);
			agrid->set_col_stretch(1, 1);
			panel->set_layout(agrid);

			auto colorBtn = new Button(popup, "Pick");

			//
			// opacity
			//

			agrid->append_row(0);
			agrid->set_anchor(new Label(panel, "Opacity:"), AdvancedGridLayout::Anchor(0, agrid->row_count()-1));

			auto floatBox = new FloatBox<float>(panel, alpha * 100.0f);
			agrid->set_anchor(floatBox, AdvancedGridLayout::Anchor(2, agrid->row_count()-1));
			floatBox->set_units("%");
			floatBox->number_format("%3.1f");
			floatBox->set_editable(true);
			floatBox->set_min_value(0.f);
			floatBox->set_max_value(100.f);
			floatBox->set_spinnable(true);
			floatBox->set_fixed_width(60);
			floatBox->set_alignment(TextBox::Alignment::Right);

			agrid->append_row(0);
			auto slider = new Slider(panel);
			agrid->set_anchor(slider, AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));
			slider->set_value(alpha * 100.0f);
			slider->set_range({0.0f,100.0f});

			slider->set_callback([floatBox,colorBtn](float a) {
				alpha = a / 100.f;
				floatBox->set_value(a);
				float f = pow(2.f, EV);
				colorBtn->set_background_color(Color(bgColor.r() * f, bgColor.g() * f, bgColor.b() * f, alpha));
			});

			floatBox->set_callback([slider,colorBtn](float a) {
				alpha = a / 100.f;
				slider->set_value(a);
				float f = pow(2.f, EV);
				colorBtn->set_background_color(Color(bgColor.r() * f, bgColor.g() * f, bgColor.b() * f, alpha));
			});

			agrid->append_row(10);

			//
			// EV
			//
			agrid->append_row(0);
			agrid->set_anchor(new Label(panel, "EV:"), AdvancedGridLayout::Anchor(0, agrid->row_count()-1));

			floatBox = new FloatBox<float>(panel, 0.f);
			agrid->set_anchor(floatBox, AdvancedGridLayout::Anchor(2, agrid->row_count()-1));
			floatBox->number_format("%1.2f");
			floatBox->set_editable(true);
			floatBox->set_spinnable(true);
			floatBox->set_fixed_width(60);
			floatBox->set_alignment(TextBox::Alignment::Right);

			agrid->append_row(0);
			slider = new Slider(panel);
			agrid->set_anchor(slider, AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));
			slider->set_value(0.0f);
			slider->set_range({-9.0f,9.0f});

			slider->set_callback([floatBox,colorBtn](float ev) {
				EV = ev;
				floatBox->set_value(EV);
				float f = pow(2.f, EV);
				colorBtn->set_background_color(Color(bgColor.r() * f, bgColor.g() * f, bgColor.b() * f, alpha));
			});

			floatBox->set_callback([slider,colorBtn](float ev) {
				EV = ev;
				slider->set_value(EV);
				float f = pow(2.f, EV);
				colorBtn->set_background_color(Color(bgColor.r() * f, bgColor.g() * f, bgColor.b() * f, alpha));
			});


			colorBtn->set_background_color(Color(bgColor.r() * pow(2.f, EV), bgColor.g() * pow(2.f, EV), bgColor.b() * pow(2.f, EV), alpha));

			colorwheel->set_callback([colorBtn](const Color &c) {
				bgColor.r() = c.r();
				bgColor.g() = c.g();
				bgColor.b() = c.b();
				float f = pow(2.f, EV);
				colorBtn->set_background_color(Color(bgColor.r() * f, bgColor.g() * f, bgColor.b() * f, alpha));
			});

			colorBtn->set_change_callback([popupBtn](bool pushed) {
				if (pushed)
				{
					float f = pow(2.f, EV);
					popupBtn->set_background_color(Color(bgColor.r() * f, bgColor.g() * f, bgColor.b() * f, alpha));
					popupBtn->set_pushed(false);
				}
			});

			addOKCancelButtons(gui, window,
				[&, popup]()
				{
					popup->dispose();
					imagesPanel->modify_image(
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
			window->request_focus();
		});
	return b;
}



Button * createFreeTransformButton(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
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
	auto b = new Button(parent, name, FA_CLONE);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, imagesPanel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(0, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
//			window->set_modal(true);

			auto row = new Widget(window);
			row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto x = new FloatBox<float>(row, translateX);
			x->set_spinnable(true);
			x->set_enabled(true);
			x->set_editable(true);
			x->set_font_size(gui->widget_font_size());
			x->set_fixed_size(Vector2i(65+12, gui->fixed_size().y()));
			x->set_alignment(TextBox::Alignment::Right);
			x->set_units("px");
			x->set_callback([](float v){translateX = v;});
			x->set_tooltip("Set horizontal translation.");

			auto y = new FloatBox<float>(row, translateY);
			y->set_spinnable(true);
			y->set_enabled(true);
			y->set_editable(true);
			y->set_font_size(gui->widget_font_size());
			y->set_fixed_size(Vector2i(65+13, gui->fixed_size().y()));
			y->set_alignment(TextBox::Alignment::Right);
			y->set_units("px");
			y->set_callback([](float v){translateY = v;});
			y->set_tooltip("Set vertical translation.");

			gui->add_widget("Translate:", row);


			auto spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);


			row = new Widget(window);
			row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto w = new FloatBox<float>(row, scaleX);
			auto link = new ToolButton(row, FA_LINK);
			auto h = new FloatBox<float>(row, scaleY);

			w->set_spinnable(true);
			w->set_enabled(true);
			w->set_editable(true);
			w->set_font_size(gui->widget_font_size());
			w->set_fixed_size(Vector2i(65, gui->fixed_size().y()));
			w->set_alignment(TextBox::Alignment::Right);
			w->set_units("%");
			w->set_tooltip("Set horizontal scale.");
			w->set_callback(
				[h](float v)
				{
					scaleX = v;
					if (uniformScale) scaleY = scaleX;
					h->set_value(scaleY);
				});

			link->set_fixed_size(Vector2i(20,20));
			link->set_pushed(uniformScale);
			link->set_tooltip("Lock the X and Y scale factors to maintain aspect ratio.");
			link->set_change_callback(
				[w,h](bool b)
				{
					uniformScale = b;
					if (uniformScale) scaleX = scaleY;
					w->set_value(scaleX);
					h->set_value(scaleY);
				});

			h->set_spinnable(true);
			h->set_enabled(true);
			h->set_editable(true);
			h->set_font_size(gui->widget_font_size());
			h->set_fixed_size(Vector2i(65, gui->fixed_size().y()));
			h->set_alignment(TextBox::Alignment::Right);
			h->set_units("%");
			h->set_tooltip("Set vertical scale.");
			h->set_callback(
				[w](float v)
				{
					scaleY = v;
					if (uniformScale) scaleX = scaleY;
					w->set_value(scaleX);
				});

			gui->add_widget("Scale:", row);


			spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);


			row = new Widget(window);
			row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto a = new FloatBox<float>(row, angle);
			a->set_spinnable(true);
			a->set_enabled(true);
			a->set_editable(true);
			a->set_font_size(gui->widget_font_size());
			a->set_fixed_size(Vector2i(160-2*25, gui->fixed_size().y()));
			a->set_alignment(TextBox::Alignment::Right);
			a->set_units("°");
			a->set_tooltip("Set rotation angle in degrees.");
			a->set_callback([](float v){angle = v;});

			auto ccww = new Button(row, "", FA_UNDO);
			ccww->set_fixed_size(Vector2i(20,20));
			ccww->set_flags(Button::Flags::RadioButton);
			ccww->set_pushed(!cw);
			ccww->set_tooltip("Rotate in the counter-clockwise direction.");
			ccww->set_change_callback([](bool b){cw = !b;});

			auto cww = new Button(row, "", FA_REDO);
			cww->set_fixed_size(Vector2i(20,20));
			cww->set_flags(Button::Flags::RadioButton);
			cww->set_pushed(cw);
			cww->set_tooltip("Rotate in the clockwise direction.");
			cww->set_change_callback([](bool b){cw = b;});

			gui->add_widget("Rotate:", row);


			spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);


			row = new Widget(window);
			row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto shx = new FloatBox<float>(row, shearX);
			shx->set_spinnable(true);
			shx->set_enabled(true);
			shx->set_editable(true);
			shx->set_font_size(gui->widget_font_size());
			shx->set_fixed_size(Vector2i(65+12, gui->fixed_size().y()));
			shx->set_alignment(TextBox::Alignment::Right);
			shx->set_units("°");
			shx->set_tooltip("Set horizontal skew/shear in degrees.");
			shx->set_callback([](float v){shearX = v;});

			auto shy = new FloatBox<float>(row, shearY);
			shy->set_spinnable(true);
			shy->set_enabled(true);
			shy->set_editable(true);
			shy->set_font_size(gui->widget_font_size());
			shy->set_fixed_size(Vector2i(65+13, gui->fixed_size().y()));
			shy->set_alignment(TextBox::Alignment::Right);
			shy->set_units("°");
			shy->set_tooltip("Set vertical skew/shear in degrees.");
			shy->set_callback([](float v){shearY = v;});

			gui->add_widget("Shear:", row);


			spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);


			row = new Widget(window);
			int bw = gui->fixed_size().y();
			int pad = 2;
			row->set_layout(new GridLayout(Orientation::Horizontal, 3, Alignment::Fill, 0, pad));
			vector<Button *> buttonGroup;

			int icons[] = {FA_PLUS, 		FA_ARROW_UP, 	FA_PLUS,
						   FA_ARROW_LEFT, 	FA_PLUS, 		FA_ARROW_RIGHT,
						   FA_PLUS, 		FA_ARROW_DOWN, 	FA_PLUS};

			for (size_t i = 0; i < sizeof(icons)/sizeof(icons[0]); ++i)
			{
				Button * btn = new Button(row, "", icons[i]);

				btn->set_flags(Button::RadioButton);
				btn->set_fixed_size(Vector2i(bw, bw));
				btn->set_pushed(i == (size_t)anchor);
				btn->set_change_callback([i](bool b){if (b) anchor = (HDRImage::CanvasAnchor)i;});

				buttonGroup.push_back(btn);
			}

			row->set_fixed_size(Vector2i(3*bw+2*pad, 3*bw+2*pad));
			gui->add_widget("Reference point:", row);


			spacer = new Widget(window);
			spacer->set_fixed_height(10);
			gui->add_widget("", spacer);


			gui->add_variable("Sampler:", sampler, true)
			   ->set_items(HDRImage::samplerNames());
			gui->add_variable("Border mode X:", borderModeX, true)
			   ->set_items(HDRImage::borderModeNames());
			gui->add_variable("Border mode Y:", borderModeY, true)
			   ->set_items(HDRImage::borderModeNames());

			auto s = gui->add_variable("Super-samples:", samples);
			s->set_spinnable(true);
			s->set_min_value(1);

			addOKCancelButtons(gui, window,
			                   [&]()
			                   {
				                   imagesPanel->modify_image(
					                   [&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
					                   {
						                   Eigen::Affine2f t(Eigen::Affine2f::Identity());

						                   Eigen::Vector2f origin(0.f, 0.f);

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
						                   t.scale(Eigen::Vector2f(1.f/img->width(), 1.f/img->height()));
						                   t.translate(Eigen::Vector2f(translateX, translateY));
						                   t.rotate(cw ? angle/180.f * M_PI : -angle/180.f * M_PI);
						                   Eigen::Matrix2f sh;
						                   sh << 1, tan(shearX/180.f * M_PI), tan(shearY/180.f * M_PI), 1;
						                   t.linear() *= sh;
						                   t.scale(Eigen::Vector2f(scaleX, scaleY)*.01f);
						                   t.scale(Eigen::Vector2f(img->width(), img->height()));
						                   t.translate(-origin);

						                   t = t.inverse();

						                   function<Eigen::Vector2f(const Eigen::Vector2f &)> warp =
							                   [t](const Eigen::Vector2f &uv)
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
			window->request_focus();
		});
	return b;
}

}


EditImagePanel::EditImagePanel(Widget *parent, HDRViewScreen * screen, ImageListPanel * imagesPanel)
	: Widget(parent), m_screen(screen), m_imagesPanel(imagesPanel)
{
	const int spacing = 2;
	set_layout(new GroupLayout(2, 4, 8, 10));


	new Label(this, "History", "sans-bold");

	auto buttonRow = new Widget(this);
	buttonRow->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

	m_undoButton = new Button(buttonRow, "Undo", FA_REPLY);
	m_undoButton->set_callback([&](){m_imagesPanel->undo();});
	m_redoButton = new Button(buttonRow, "Redo", FA_SHARE);
	m_redoButton->set_callback([&](){m_imagesPanel->redo();});

	new Label(this, "Pixel/domain transformations", "sans-bold");

	auto grid = new Widget(this);
	grid->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

	// flip h
	m_filterButtons.push_back(new Button(grid, "Flip H", FA_ARROWS_ALT_H));
	m_filterButtons.back()->set_callback([&](){m_screen->flip_image(true);});
	m_filterButtons.back()->set_fixed_height(21);

	// rotate cw
	m_filterButtons.push_back(new Button(grid, "Rotate CW", FA_REDO));
	m_filterButtons.back()->set_fixed_height(21);
	m_filterButtons.back()->set_callback(
		[this]()
		{
			m_imagesPanel->modify_image(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->rotated90CW()),
					        make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated90CCW(); },
					                                [](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated90CW(); })};
				});
		});

	// flip v
	m_filterButtons.push_back(new Button(grid, "Flip V", FA_ARROWS_ALT_V));
	m_filterButtons.back()->set_callback([&](){m_screen->flip_image(false);});
	m_filterButtons.back()->set_fixed_height(21);

	// rotate ccw
	m_filterButtons.push_back(new Button(grid, "Rotate CCW", FA_UNDO));
	m_filterButtons.back()->set_fixed_height(21);
	m_filterButtons.back()->set_callback(
		[this]()
		{
			m_imagesPanel->modify_image(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->rotated90CCW()),
					        make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated90CW(); },
					                                [](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated90CCW(); })};
				});
		});

	// shift
	m_filterButtons.push_back(createShiftButton(grid, m_screen, m_imagesPanel));
	// canvas size
	m_filterButtons.push_back(createCanvasSizeButton(grid, m_screen, m_imagesPanel));

	// resize
	m_filterButtons.push_back(createResizeButton(grid, m_screen, m_imagesPanel));

	// free transform
	m_filterButtons.push_back(createFreeTransformButton(grid, m_screen, m_imagesPanel));

	// remap
	m_filterButtons.push_back(createRemapButton(grid, m_screen, m_imagesPanel));


	new Label(this, "Color/range adjustments", "sans-bold");
	buttonRow = new Widget(this);
	auto agrid = new AdvancedGridLayout({0, spacing, 0}, {}, 0);
	agrid->set_col_stretch(0, 1.0f);
	agrid->set_col_stretch(2, 1.0f);
	buttonRow->set_layout(agrid);

	agrid->append_row(0);
	// invert
	m_filterButtons.push_back(new Button(buttonRow, "Invert", FA_IMAGE));
	m_filterButtons.back()->set_fixed_height(21);
	m_filterButtons.back()->set_callback(
		[this]()
		{
			m_imagesPanel->modify_image(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->inverted()),
					        make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->inverted(); })};
				});
		});
	agrid->set_anchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1));

	// clamp
	m_filterButtons.push_back(new Button(buttonRow, "Clamp", FA_ADJUST));
	m_filterButtons.back()->set_fixed_height(21);
	m_filterButtons.back()->set_callback(
		[this]()
		{
			m_imagesPanel->modify_image(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->unaryExpr(
						[](const Color4 & c)
						{
							return Color4(clamp01(c.r), clamp01(c.g), clamp01(c.b), clamp01(c.a));
						}).eval()), nullptr };
				});
		});
	agrid->set_anchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(2, agrid->row_count()-1));

//	buttonRow = new Widget(this);
//	buttonRow->set_layout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, 2));

	agrid->append_row(spacing);  // spacing

	m_filterButtons.push_back(createExposureGammaButton(buttonRow, m_screen, m_imagesPanel));
	agrid->append_row(0);
	agrid->set_anchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	agrid->append_row(spacing);  // spacing
	m_filterButtons.push_back(createBrightnessContrastButton(buttonRow, m_screen, m_imagesPanel));
	agrid->append_row(0);
	agrid->set_anchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	agrid->append_row(spacing);  // spacing
	m_filterButtons.push_back(createFilmicTonemappingButton(buttonRow, m_screen, m_imagesPanel));
	agrid->append_row(0);
	agrid->set_anchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	agrid->append_row(spacing);  // spacing
	m_filterButtons.push_back(createHueSaturationButton(buttonRow, m_screen, m_imagesPanel));
	agrid->append_row(0);
	agrid->set_anchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	agrid->append_row(spacing);  // spacing
	m_filterButtons.push_back(createColorSpaceButton(buttonRow, m_screen, m_imagesPanel));
	agrid->append_row(0);
	agrid->set_anchor(m_filterButtons.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	new Label(this, "Filters", "sans-bold");
	buttonRow = new Widget(this);
	buttonRow->set_layout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, spacing));
	m_filterButtons.push_back(createGaussianFilterButton(buttonRow, m_screen, m_imagesPanel));
	m_filterButtons.push_back(createBoxFilterButton(buttonRow, m_screen, m_imagesPanel));
	m_filterButtons.push_back(createBilateralFilterButton(buttonRow, m_screen, m_imagesPanel));
	m_filterButtons.push_back(createUnsharpMaskFilterButton(buttonRow, m_screen, m_imagesPanel));
	m_filterButtons.push_back(createMedianFilterButton(buttonRow, m_screen, m_imagesPanel));
}


void EditImagePanel::draw(NVGcontext *ctx)
{
	auto img = m_imagesPanel->current_image();

	bool can_modify = img && img->can_modify();

	if (enabled() != can_modify)
	{
		set_enabled(can_modify);
		for (auto btn : m_filterButtons)
			btn->set_enabled(can_modify);
	}

	m_undoButton->set_enabled(can_modify && img->has_undo());
	m_redoButton->set_enabled(can_modify && img->has_redo());


	Widget::draw(ctx);
}