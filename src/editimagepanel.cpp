//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "editimagepanel.h"
#include "common.h"
#include "xpuimage.h"
#include "hdrviewscreen.h"
#include "hdrimage.h"
#include "imagelistpanel.h"
#include "envmap.h"
#include "colorspace.h"
#include "hslgradient.h"
#include "colorslider.h"
#include "colorwheel.h"
#include "hdrcolorpicker.h"
#include "multigraph.h"
#include "filmictonecurve.h"
#include <spdlog/spdlog.h>
#include <Eigen/Geometry>

using namespace std;

namespace
{

std::function<void(float)> create_floatbox_and_slider(
	FormHelper * gui, Widget * parent,
	string name, float & variable,
	float mn, float mx, float step,
	std::function<void(void)> cb,
	string help = "")
{
	auto box = gui->add_variable(name, variable);
	box->set_spinnable(true);
	box->number_format("%3.2f");
	box->set_value_increment(step);
	box->set_min_max_values(mn, mx);
	box->set_fixed_width(65);
	box->set_tooltip(help);

	auto slider = new Slider(parent);
	slider->set_value(variable);
	slider->set_range({mn, mx});
	slider->set_tooltip(help);
	gui->add_widget("", slider);

	auto f_cb = [box,slider,cb,&variable](float v)
	{
		variable = v;
		box->set_value(v);
		slider->set_value(v);
		cb();
	};
	slider->set_callback(f_cb);
	box->set_callback(f_cb);
	return f_cb;
}


void add_ok_cancel_btns(FormHelper * gui, Window * window,
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

Button * create_colorspace_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static string name = "Convert color space...";
	static EColorSpace src = LinearSRGB_CS, dst = CIEXYZ_CS;
	auto b = new Button(parent, name, FA_PALETTE);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(125, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);

			gui->add_variable("Source:", src, true)->set_items(colorSpaceNames());
			gui->add_variable("Destination:", dst, true)->set_items(colorSpaceNames());
			
			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->unaryExpr([](const Color4 & c){return convertColorSpace(c, dst, src);}).eval()),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_exposure_gamma_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static string name = "Exposure/Gamma...";
	static float exposure = 0.0f;
	static float gamma = 1.0f;
	static float offset = 0.0f;
	auto b = new Button(parent, name, FA_ADJUST);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(55, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);


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


			create_floatbox_and_slider(gui, window,
			                        "Exposure:", exposure,
			                        -10.f, 10.f, 0.1f, graphCb);

			create_floatbox_and_slider(gui, window,
			                        "Offset:", offset,
			                        -1.f, 1.f, 0.01f, graphCb);

			create_floatbox_and_slider(gui, window,
			                        "Gamma:", gamma,
			                        0.0001f, 10.f, 0.1f, graphCb);

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
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

Button * create_brightness_constract_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
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
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(100, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);


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

			auto bCb = create_floatbox_and_slider(gui, window,
			                        "Brightness:", brightness,
			                        -1.f, 1.f, 0.01f, graphCb, help);

			help = "Change the slope/gradient at the new 50% midpoint.";
			auto cCb = create_floatbox_and_slider(gui, window,
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

			
			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> &img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->brightness_contrast(brightness, contrast, linear, channelMap[channel])),
									nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_filmic_tonemapping_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static string name = "Filmic tonemapping...";
	static FilmicToneCurve::FullCurve fCurve;
	static FilmicToneCurve::CurveParamsUser params;
	static float vizFstops = 1.f;
	static const auto activeColor = Color(255, 255, 255, 200);
	auto b = new Button(parent, name, FA_ADJUST);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(55, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);

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

			create_floatbox_and_slider(gui, window,
			                        "Graph F-stops:", vizFstops,
			                        0.f, 10.f, 0.1f, graphCb);

			create_floatbox_and_slider(gui, window,
			                        "Toe strength:", params.toeStrength,
			                        0.f, 1.f, 0.01f, graphCb);

			create_floatbox_and_slider(gui, window,
			                        "Toe length:", params.toeLength,
			                        0.f, 1.f, 0.01f, graphCb);

			create_floatbox_and_slider(gui, window,
			                        "Shoulder strength:", params.shoulderStrength,
			                        0.f, 10.f, 0.1f, graphCb);

			create_floatbox_and_slider(gui, window,
			                        "Shoulder length:", params.shoulderLength,
			                        0.f, 1.f, 0.01f, graphCb);

			create_floatbox_and_slider(gui, window,
			                        "Shoulder angle:", params.shoulderAngle,
			                        0.f, 1.f, 0.01f, graphCb);

			create_floatbox_and_slider(gui, window,
			                        "Gamma:", params.gamma,
			                        0.f, 5.f, 0.01f, graphCb);

			add_ok_cancel_btns(gui, window,
				[&]()
				{
				   images_panel->modify_image(
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
			window->request_focus();
		});
	return b;
}

Button * create_hsl_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static string name = "Hue/Saturation...";
	static float hue = 0.0f;
	static float saturation = 0.0f;
	static float lightness = 0.0f;
	auto b = new Button(parent, name, FA_PALETTE);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(55, 20));

			Widget* spacer = nullptr;

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);



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

			create_floatbox_and_slider(gui, window,
			                        "Hue:", hue,
			                        -180.f, 180.f, 1.f, cb);

			create_floatbox_and_slider(gui, window,
			                        "Saturation:", saturation,
			                        -100.f, 100.f, 1.f, cb);

			create_floatbox_and_slider(gui, window,
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

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> &img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(
								img->unaryExpr(
									[](Color4 c)
									{
										HSLAdjust(&c[0], &c[1], &c[2], hue, (saturation+100.f)/100.f, (lightness)/100.f);
										return c;
									}).eval()), nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_gaussian_filter_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static float width = 1.0f, height = 1.0f;
	static HDRImage::BorderMode border_mode_x = HDRImage::EDGE, border_mode_y = HDRImage::EDGE;
	static bool exact = false;
	static string name = "Gaussian blur...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
			window->set_modal(true);

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

			gui->add_variable("Border mode X:", border_mode_x, true)
			   ->set_items(HDRImage::border_mode_names());
			gui->add_variable("Border mode Y:", border_mode_y, true)
			   ->set_items(HDRImage::border_mode_names());

			gui->add_variable("Exact (slow!):", exact, true);

			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(exact ? img->gaussian_blurred(width, height, progress, border_mode_x, border_mode_y) :
							        img->fast_gaussian_blurred(width, height, progress, border_mode_x, border_mode_y)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_box_filter_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static float width = 1.0f, height = 1.0f;
	static HDRImage::BorderMode border_mode_x = HDRImage::EDGE, border_mode_y = HDRImage::EDGE;
	static string name = "Box blur...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);

			auto w = gui->add_variable("Width:", width);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w->set_units("px");
			w = gui->add_variable("Height:", height);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w->set_units("px");

			gui->add_variable("Border mode X:", border_mode_x, true)
			   ->set_items(HDRImage::border_mode_names());
			gui->add_variable("Border mode Y:", border_mode_y, true)
			   ->set_items(HDRImage::border_mode_names());

			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->box_blurred(width, height, progress, border_mode_x, border_mode_y)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_bilateral_filter_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static float rangeSigma = 1.0f, valueSigma = 0.1f;
	static HDRImage::BorderMode border_mode_x = HDRImage::EDGE, border_mode_y = HDRImage::EDGE;
	static string name = "Bilateral filter...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);

			auto w = gui->add_variable("Range sigma:", rangeSigma);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w = gui->add_variable("Value sigma:", valueSigma);
			w->set_spinnable(true);
			w->set_min_value(0.0f);

			gui->add_variable("Border mode X:", border_mode_x, true)
			   ->set_items(HDRImage::border_mode_names());
			gui->add_variable("Border mode Y:", border_mode_y, true)
			   ->set_items(HDRImage::border_mode_names());

			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->bilateral_filtered(valueSigma, rangeSigma,
							                              progress, border_mode_x, border_mode_y)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_unsharp_mask_filter_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static float sigma = 1.0f, strength = 1.0f;
	static HDRImage::BorderMode border_mode_x = HDRImage::EDGE, border_mode_y = HDRImage::EDGE;
	static string name = "Unsharp mask...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);

			auto w = gui->add_variable("Sigma:", sigma);
			w->set_spinnable(true);
			w->set_min_value(0.0f);
			w = gui->add_variable("Strength:", strength);
			w->set_spinnable(true);
			w->set_min_value(0.0f);

			gui->add_variable("Border mode X:", border_mode_x, true)
			   ->set_items(HDRImage::border_mode_names());
			gui->add_variable("Border mode Y:", border_mode_y, true)
			   ->set_items(HDRImage::border_mode_names());

			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->unsharp_masked(sigma, strength, progress, border_mode_x, border_mode_y)),
							        nullptr};
						});
				});


			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_median_filter_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static float radius = 1.0f;
	static HDRImage::BorderMode border_mode_x = HDRImage::EDGE, border_mode_y = HDRImage::EDGE;
	static string name = "Median filter...";
	auto b = new Button(parent, name, FA_TINT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);

			auto w = gui->add_variable("Radius:", radius);
			w->set_spinnable(true);
			w->set_min_value(0.0f);

			gui->add_variable("Border mode X:", border_mode_x, true)
			   ->set_items(HDRImage::border_mode_names());
			gui->add_variable("Border mode Y:", border_mode_y, true)
			   ->set_items(HDRImage::border_mode_names());

			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->median_filtered(radius, progress, border_mode_x, border_mode_y)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_resize_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static int width = 128, height = 128;
	static string name = "Resize...";
	static bool aspect = true;
	auto b = new Button(parent, name, FA_EXPAND);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(0, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
			window->set_modal(true);

			auto row = new Widget(window);
			row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			width = images_panel->current_image()->width();
			height = images_panel->current_image()->height();

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
				[w,images_panel](bool preserve)
				{
					if (preserve)
					{
						float aspect = images_panel->current_image()->width() / (float)images_panel->current_image()->height();
						width = max(1, (int)round(height * aspect));
						w->set_value(width);
					}
					aspect = preserve;
				});

			w->set_callback(
				[h,link,images_panel](int w)
				{
					width = w;
					if (link->pushed())
					{
						float aspect = images_panel->current_image()->width() / (float)images_panel->current_image()->height();
						height = max(1, (int)round(w / aspect));
						h->set_value(height);
					}
				});

			h->set_callback(
				[w,link,images_panel](int h)
				{
					height = h;
					if (link->pushed())
					{
						float aspect = images_panel->current_image()->width() / (float)images_panel->current_image()->height();
						width = max(1, (int)round(height * aspect));
						w->set_value(width);
					}
				});


			gui->add_widget("", row);

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
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

Button * create_remap_btn(Widget *parent, HDRViewScreen *screen, ImageListPanel *images_panel)
{
	static EEnvMappingUVMode from = ANGULAR_MAP, to = ANGULAR_MAP;
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static int width = 128, height = 128;
	static bool autoAspect = true;
	static HDRImage::BorderMode border_mode_x = HDRImage::EDGE, border_mode_y = HDRImage::EDGE;
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
	auto b = new Button(parent, name, FA_GLOBE_AMERICAS);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(135, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);

			width = images_panel->current_image()->width();
			auto w = gui->add_variable("Width:", width);
			w->set_spinnable(true);
			w->set_min_value(1);
			w->set_units("px");

			height = images_panel->current_image()->height();
			auto h = gui->add_variable("Height:", height);
			h->set_spinnable(true);
			h->set_min_value(1);
			h->set_units("px");

			auto recompute_w = []()
			{
				if (autoAspect)
					width = max(1, (int)round(height * autoAspects[to]));
			};
			auto recompute_h = []()
			{
				if (autoAspect)
					height = max(1, (int)round(width / autoAspects[to]));
			};

			w->set_callback(
				[h,recompute_h](int w)
				{
					width = w;
					recompute_h();
					h->set_value(height);
				});

			h->set_callback(
				[w,recompute_w](int h)
				{
					height = h;
					recompute_w();
					w->set_value(width);
				});


			auto auto_aspect_checkbox = gui->add_variable("Auto aspect ratio:", autoAspect, true);

			auto src = gui->add_variable("Source map:", from, true);
			auto dst = gui->add_variable("Target map:", to, true);

			src->set_items(envMappingNames());
			src->set_callback([gui,recompute_w](EEnvMappingUVMode m)
			                 {
				                 from = m;
				                 recompute_w();
				                 gui->refresh();
			                 });
			dst->set_items(envMappingNames());
			dst->set_callback([gui,recompute_w](EEnvMappingUVMode m)
			                 {
				                 to = m;
				                 recompute_w();
				                 gui->refresh();
			                 });

			auto spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);

			auto btn = new Button(window, "Swap source/target", FA_EXCHANGE_ALT);
			btn->set_callback([gui,recompute_w,recompute_h](){std::swap(from,to);recompute_w();recompute_h();gui->refresh();});
			btn->set_fixed_size(gui->fixed_size());
			gui->add_widget(" ", btn);

			auto_aspect_checkbox->set_callback(
				[w,recompute_w](bool preserve)
				{
					autoAspect = preserve;
					recompute_w();
					w->set_value(width);
				});

			recompute_w();
			gui->refresh();


			gui->add_variable("Sampler:", sampler, true)
			   ->set_items(HDRImage::sampler_names());
			gui->add_variable("Border mode X:", border_mode_x, true)
			   ->set_items(HDRImage::border_mode_names());
			gui->add_variable("Border mode Y:", border_mode_y, true)
			   ->set_items(HDRImage::border_mode_names());

			w = gui->add_variable("Super-samples:", samples);
			w->set_spinnable(true);
			w->set_min_value(1);

			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
//					auto dst2xyz = envMapUVToXYZ(to);
//					auto xyz2src = XYZToEnvMapUV(from);
//					auto warp = [dst2xyz,xyz2src](const Vector2f &uv) { return xyz2src(dst2xyz(uv)); };
					auto warp = [](const Eigen::Vector2f &uv) { return convertEnvMappingUV(from, to, uv); };

					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img, AtomicProgress & progress) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->resampled(width, height, progress, warp, samples, sampler,
							                                            border_mode_x, border_mode_y)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_shift_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static HDRImage::BorderMode border_mode_x = HDRImage::REPEAT, border_mode_y = HDRImage::REPEAT;
	static float dx = 0.f, dy = 0.f;
	static string name = "Shift...";
	auto b = new Button(parent, name, FA_ARROWS_ALT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(125, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
          	window->set_modal(true);

			auto w = gui->add_variable("X offset:", dx);
			w->set_spinnable(true);
			w->set_units("px");

			w = gui->add_variable("Y offset:", dy);
			w->set_spinnable(true);
			w->set_units("px");

			gui->add_variable("Sampler:", sampler, true)
			   ->set_items(HDRImage::sampler_names());
			gui->add_variable("Border mode X:", border_mode_x, true)
			   ->set_items(HDRImage::border_mode_names());
			gui->add_variable("Border mode Y:", border_mode_y, true)
			   ->set_items(HDRImage::border_mode_names());

			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
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
							                                             border_mode_x, border_mode_y)),
							        nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}


Widget * create_anchor_widget(Widget * window, HDRImage::CanvasAnchor & anchor, int bw)
{
	auto row = new Widget(window);
	int pad = 2;
	row->set_layout(new GridLayout(Orientation::Horizontal, 3, Alignment::Fill, 0, pad));
	vector<Button *> button_group;

	int icons[] = {FA_PLUS, 				FA_GRIP_LINES, 	FA_PLUS,
				   FA_GRIP_LINES_VERTICAL, 	FA_PLUS, 		FA_GRIP_LINES_VERTICAL,
				   FA_PLUS, 				FA_GRIP_LINES, 	FA_PLUS};

	for (size_t i = 0; i < sizeof(icons)/sizeof(icons[0]); ++i)
	{
		Button * btn = new Button(row, "", icons[i]);

		btn->set_flags(Button::RadioButton);
		btn->set_fixed_size(Vector2i(bw, bw));
		btn->set_pushed(i == (size_t)anchor);
		btn->set_change_callback([i, &anchor](bool b){if (b) anchor = (HDRImage::CanvasAnchor)i;});
		button_group.push_back(btn);
	}

	row->set_fixed_size(Vector2i(3*bw+2*pad, 3*bw+2*pad));
	return row;
}

Button * create_canvas_size_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static int width = 128, height = 128;
	static Color bg(.8f, .8f, .8f, 1.f);
	static float EV = 0.f;
	static HDRImage::CanvasAnchor anchor = HDRImage::MIDDLE_CENTER;
	static string name = "Canvas size...";
	static bool relative = false;
	auto b = new Button(parent, name, FA_CROP_ALT);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
			window->set_modal(true);

			width = images_panel->current_image()->width();
			auto w = gui->add_variable("Width:", width);
			w->set_spinnable(true);
			w->set_min_value(1);
			w->set_units("px");

			height = images_panel->current_image()->height();
			auto h = gui->add_variable("Height:", height);
			h->set_spinnable(true);
			h->set_min_value(1);
			h->set_units("px");

			relative = false;
			auto r = gui->add_variable("Relative:", relative, true);
			r->set_callback(
				[w,h,images_panel](bool rel)
				{
					if (rel)
					{
						w->set_min_value(-images_panel->current_image()->width()+1);
						h->set_min_value(-images_panel->current_image()->height()+1);
						width = w->value() - images_panel->current_image()->width();
						height = h->value() - images_panel->current_image()->height();
						w->set_value(width);
						h->set_value(height);
					}
					else
					{
						w->set_min_value(1);
						h->set_min_value(1);
						width = w->value() + images_panel->current_image()->width();
						height = h->value() + images_panel->current_image()->height();
						w->set_value(width);
						h->set_value(height);
					}
					relative = rel;
				});

			auto spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);

			gui->add_widget("Anchor:", create_anchor_widget(window, anchor, gui->fixed_size().y()));

			spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);

			auto color_btn = new HDRColorPicker(window, bg, EV);
			gui->add_widget(name, color_btn);
			color_btn->set_final_callback([](const Color & c, float e){
				bg = c;
				EV = e;
			});

			auto popup = color_btn->popup();
			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&, popup]()
				{
					popup->dispose();
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							int newW = relative ? width + img->width() : width;
							int newH = relative ? height + img->height() : height;

							float gain = pow(2.f, EV);
							Color4 c(bg.r() * gain, bg.g() * gain, bg.b() * gain, bg.a());

							return {make_shared<HDRImage>(img->resized_canvas(newW, newH, anchor, c)),
							        nullptr};
						});
				},
				[popup](){ popup->dispose(); });

			window->center();
			window->request_focus();
		});
	return b;
}



Button * create_free_xform_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static float translate_x = 0, translate_y = 0;
	static float scale_x = 100.0f, scale_y = 100.0f;
	static bool uniform_scale = true;
	static float angle = 0.0f;
	static bool cw = false;
	static float shear_x = 0, shear_y = 0;
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static HDRImage::BorderMode border_mode_x = HDRImage::REPEAT, border_mode_y = HDRImage::REPEAT;
	static HDRImage::CanvasAnchor anchor = HDRImage::MIDDLE_CENTER;
	static int samples = 1;
	static string name = "Transform...";
	auto b = new Button(parent, name, FA_CLONE);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(0, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
			window->set_modal(true);

			auto row = new Widget(window);
			row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto x = new FloatBox<float>(row, translate_x);
			x->set_spinnable(true);
			x->set_enabled(true);
			x->set_editable(true);
			x->set_font_size(gui->widget_font_size());
			x->set_fixed_size(Vector2i(65+12, gui->fixed_size().y()));
			x->set_alignment(TextBox::Alignment::Right);
			x->set_units("px");
			x->set_callback([](float v){translate_x = v;});
			x->set_tooltip("Set horizontal translation.");

			auto y = new FloatBox<float>(row, translate_y);
			y->set_spinnable(true);
			y->set_enabled(true);
			y->set_editable(true);
			y->set_font_size(gui->widget_font_size());
			y->set_fixed_size(Vector2i(65+13, gui->fixed_size().y()));
			y->set_alignment(TextBox::Alignment::Right);
			y->set_units("px");
			y->set_callback([](float v){translate_y = v;});
			y->set_tooltip("Set vertical translation.");

			gui->add_widget("Translate:", row);


			auto spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);


			row = new Widget(window);
			row->set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Fill, 0, 5));

			auto w = new FloatBox<float>(row, scale_x);
			auto link = new ToolButton(row, FA_LINK);
			auto h = new FloatBox<float>(row, scale_y);

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
					scale_x = v;
					if (uniform_scale) scale_y = scale_x;
					h->set_value(scale_y);
				});

			link->set_fixed_size(Vector2i(20,20));
			link->set_pushed(uniform_scale);
			link->set_tooltip("Lock the X and Y scale factors to maintain aspect ratio.");
			link->set_change_callback(
				[w,h](bool b)
				{
					uniform_scale = b;
					if (uniform_scale) scale_x = scale_y;
					w->set_value(scale_x);
					h->set_value(scale_y);
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
					scale_y = v;
					if (uniform_scale) scale_x = scale_y;
					w->set_value(scale_x);
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

			auto shx = new FloatBox<float>(row, shear_x);
			shx->set_spinnable(true);
			shx->set_enabled(true);
			shx->set_editable(true);
			shx->set_font_size(gui->widget_font_size());
			shx->set_fixed_size(Vector2i(65+12, gui->fixed_size().y()));
			shx->set_alignment(TextBox::Alignment::Right);
			shx->set_units("°");
			shx->set_tooltip("Set horizontal skew/shear in degrees.");
			shx->set_callback([](float v){shear_x = v;});

			auto shy = new FloatBox<float>(row, shear_y);
			shy->set_spinnable(true);
			shy->set_enabled(true);
			shy->set_editable(true);
			shy->set_font_size(gui->widget_font_size());
			shy->set_fixed_size(Vector2i(65+13, gui->fixed_size().y()));
			shy->set_alignment(TextBox::Alignment::Right);
			shy->set_units("°");
			shy->set_tooltip("Set vertical skew/shear in degrees.");
			shy->set_callback([](float v){shear_y = v;});

			gui->add_widget("Shear:", row);


			spacer = new Widget(window);
			spacer->set_fixed_height(5);
			gui->add_widget("", spacer);

			gui->add_widget("Reference point:", create_anchor_widget(window, anchor, gui->fixed_size().y()));

			spacer = new Widget(window);
			spacer->set_fixed_height(10);
			gui->add_widget("", spacer);


			gui->add_variable("Sampler:", sampler, true)
			   ->set_items(HDRImage::sampler_names());
			gui->add_variable("Border mode X:", border_mode_x, true)
			   ->set_items(HDRImage::border_mode_names());
			gui->add_variable("Border mode Y:", border_mode_y, true)
			   ->set_items(HDRImage::border_mode_names());

			auto s = gui->add_variable("Super-samples:", samples);
			s->set_spinnable(true);
			s->set_min_value(1);

			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
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
							t.translate(Eigen::Vector2f(translate_x, translate_y));
							t.rotate(cw ? angle/180.f * M_PI : -angle/180.f * M_PI);
							Eigen::Matrix2f sh;
							sh << 1, tan(shear_x/180.f * M_PI), tan(shear_y/180.f * M_PI), 1;
							t.linear() *= sh;
							t.scale(Eigen::Vector2f(scale_x, scale_y)*.01f);
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
																		border_mode_x, border_mode_y)),
									nullptr};
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_flatten_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static Color bg(.8f, .8f, .8f, 1.f);
	static float EV = 0.f;
	static string name = "Flatten...";
	auto b = new Button(parent, name, FA_CHESS_BOARD);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(75, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
			window->set_modal(true);

			auto color_btn = new HDRColorPicker(window, bg, EV);
			gui->add_widget(name, color_btn);
			color_btn->set_final_callback([](const Color & c, float e){
				bg = c;
				EV = e;
			});

			auto popup = color_btn->popup();
			screen->request_layout_update();

			add_ok_cancel_btns(gui, window,
				[&, popup]()
				{
					popup->dispose();
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->unaryExpr(
								[](const Color4 & c)
								{
									float alpha = c.a + bg.a() * (1.f - c.a);
									float gain = pow(2.f, EV);
									return Color4(Color3(c.r, c.g, c.b) * gain * c.a +
												  Color3(bg.r(), bg.g(), bg.b()) * gain * bg.a() * (1.f-c.a), alpha);
								}).eval()), nullptr };
						});
				},
				[popup](){ popup->dispose(); });

			window->center();
			window->request_focus();
		});
	return b;
}

Button * create_fill_btn(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
{
	static string name = "Fill...";
	static std::array<bool, 4> enabled = {true, true, true, true};
	// static Color value(0.8f);
	static Color value(0.0f, 1.f);
	auto b = new Button(parent, name, FA_FILL);
	b->set_fixed_height(21);
	b->set_callback(
		[&, screen, images_panel]()
		{
			FormHelper *gui = new FormHelper(screen);
			gui->set_fixed_size(Vector2i(200, 20));

			auto window = gui->add_window(Vector2i(10, 10), name);
			window->set_modal(true);

			auto row = new Widget(window);
			auto layout = new GridLayout(Orientation::Horizontal, 4, Alignment::Middle, 0, 5);
			layout->set_col_alignment({ Alignment::Maximum, Alignment::Fill, Alignment::Fill, Alignment::Minimum });
			row->set_layout(layout);

			string names[] = {"Red :", "Green : ", "Blue :", "Alpha :"};
			std::vector<ColorSlider*> sliders;
			std::vector<FloatBox<float>*> float_boxes;
			for (int i = 0; i < 4; ++i)
			{
				new Label(row, names[i], "sans-bold");

				auto slider = new ColorSlider(row, value, ColorSlider::ColorMode(i));
				slider->set_color(value);
				slider->set_value(value[i]);
				slider->set_range({0.f, 1.f});
				slider->set_fixed_width(250);
				slider->set_enabled(enabled[i]);

				auto box = new FloatBox(row, value[i]);
				box->set_spinnable(true);
				box->number_format("%3.2f");
				box->set_min_max_values(0.f, 1.f);
				box->set_fixed_width(50);
				box->set_enabled(enabled[i]);
				box->set_units("");
				box->set_alignment(TextBox::Alignment::Right);

				sliders.push_back(slider);
				float_boxes.push_back(box);
				
				(new CheckBox(row, "", [&,i,box,slider](const bool & b) {enabled[i] = b; box->set_enabled(b); slider->set_enabled(b);}))->set_checked(enabled[i]);
			}

			for (size_t i = 0; i < sliders.size(); ++i)
			{
				auto cb = [i,float_boxes,sliders](float v)
				{
					value[i] = v;
					float_boxes[i]->set_value(v);
					sliders[i]->set_value(v);
					for (auto slider : sliders)
						slider->set_color(value);
				};

				sliders[i]->set_callback(cb);
				float_boxes[i]->set_callback(cb);
			}

			gui->add_widget("", row);

			add_ok_cancel_btns(gui, window,
				[&]()
				{
					images_panel->modify_image(
						[&](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
						{
							return {make_shared<HDRImage>(img->unaryExpr(
								[&](const Color4 & c)
								{
									return Color4(enabled[0] ? value[0] : c[0],
												  enabled[1] ? value[1] : c[1],
												  enabled[2] ? value[2] : c[2],
												  enabled[3] ? value[3] : c[3]);
								}).eval()), nullptr };
						});
				});

			window->center();
			window->request_focus();
		});
	return b;
}

}


EditImagePanel::EditImagePanel(Widget *parent, HDRViewScreen * screen, ImageListPanel * images_panel)
	: Widget(parent), m_screen(screen), m_images_panel(images_panel)
{
	const int spacing = 2;
	set_layout(new GroupLayout(2, 4, 8, 10));

	new Label(this, "History", "sans-bold");

	auto buttonRow = new Widget(this);
	buttonRow->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

	m_undo_btn = new Button(buttonRow, "Undo", FA_REPLY);
	m_undo_btn->set_callback([&](){m_images_panel->undo();});
	m_redo_btn = new Button(buttonRow, "Redo", FA_SHARE);
	m_redo_btn->set_callback([&](){m_images_panel->redo();});

	new Label(this, "Pixel/domain transformations", "sans-bold");

	auto grid = new Widget(this);
	grid->set_layout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, spacing));

	// flip h
	m_filter_btns.push_back(new Button(grid, "Flip H", FA_ARROWS_ALT_H));
	m_filter_btns.back()->set_callback([&](){m_screen->flip_image(true);});
	m_filter_btns.back()->set_fixed_height(21);

	// rotate cw
	m_filter_btns.push_back(new Button(grid, "Rotate CW", FA_REDO));
	m_filter_btns.back()->set_fixed_height(21);
	m_filter_btns.back()->set_callback(
		[this]()
		{
			m_images_panel->modify_image(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->rotated_90_cw()),
					        make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated_90_ccw(); },
					                                [](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated_90_cw(); })};
				});
		});

	// flip v
	m_filter_btns.push_back(new Button(grid, "Flip V", FA_ARROWS_ALT_V));
	m_filter_btns.back()->set_callback([&](){m_screen->flip_image(false);});
	m_filter_btns.back()->set_fixed_height(21);

	// rotate ccw
	m_filter_btns.push_back(new Button(grid, "Rotate CCW", FA_UNDO));
	m_filter_btns.back()->set_fixed_height(21);
	m_filter_btns.back()->set_callback(
		[this]()
		{
			m_images_panel->modify_image(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->rotated_90_ccw()),
					        make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated_90_cw(); },
					                                [](shared_ptr<HDRImage> & img2) { *img2 = img2->rotated_90_ccw(); })};
				});
		});

	// shift
	m_filter_btns.push_back(create_shift_btn(grid, m_screen, m_images_panel));
	// canvas size
	m_filter_btns.push_back(create_canvas_size_btn(grid, m_screen, m_images_panel));

	// resize
	m_filter_btns.push_back(create_resize_btn(grid, m_screen, m_images_panel));

	// free transform
	m_filter_btns.push_back(create_free_xform_btn(grid, m_screen, m_images_panel));

	// remap
	m_filter_btns.push_back(create_remap_btn(grid, m_screen, m_images_panel));


	new Label(this, "Color/range adjustments", "sans-bold");
	buttonRow = new Widget(this);
	auto agrid = new AdvancedGridLayout({0, spacing, 0}, {}, 0);
	agrid->set_col_stretch(0, 1.0f);
	agrid->set_col_stretch(2, 1.0f);
	buttonRow->set_layout(agrid);

	agrid->append_row(0);
	// invert
	m_filter_btns.push_back(new Button(buttonRow, "Invert", FA_IMAGE));
	m_filter_btns.back()->set_fixed_height(21);
	m_filter_btns.back()->set_callback(
		[this]()
		{
			m_images_panel->modify_image(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->inverted()),
					        make_shared<LambdaUndo>([](shared_ptr<HDRImage> & img2) { *img2 = img2->inverted(); })};
				});
		});
	agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1));

	// clamp
	m_filter_btns.push_back(new Button(buttonRow, "Clamp", FA_ADJUST));
	m_filter_btns.back()->set_fixed_height(21);
	m_filter_btns.back()->set_callback(
		[this]()
		{
			m_images_panel->modify_image(
				[](const shared_ptr<const HDRImage> & img) -> ImageCommandResult
				{
					return {make_shared<HDRImage>(img->unaryExpr(
						[](const Color4 & c)
						{
							return Color4(clamp01(c.r), clamp01(c.g), clamp01(c.b), clamp01(c.a));
						}).eval()), nullptr };
				});
		});
	agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(2, agrid->row_count()-1));

	agrid->append_row(spacing);  // spacing

	//
	agrid->append_row(0);
	m_filter_btns.push_back(create_flatten_btn(buttonRow, m_screen, m_images_panel));
	agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1));
	m_filter_btns.push_back(create_fill_btn(buttonRow, m_screen, m_images_panel));
	agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(2, agrid->row_count()-1));

	agrid->append_row(spacing);  // spacing
	m_filter_btns.push_back(create_exposure_gamma_btn(buttonRow, m_screen, m_images_panel));
	agrid->append_row(0);
	agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	agrid->append_row(spacing);  // spacing
	m_filter_btns.push_back(create_brightness_constract_btn(buttonRow, m_screen, m_images_panel));
	agrid->append_row(0);
	agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	agrid->append_row(spacing);  // spacing
	m_filter_btns.push_back(create_filmic_tonemapping_btn(buttonRow, m_screen, m_images_panel));
	agrid->append_row(0);
	agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	agrid->append_row(spacing);  // spacing
	m_filter_btns.push_back(create_hsl_btn(buttonRow, m_screen, m_images_panel));
	agrid->append_row(0);
	agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	agrid->append_row(spacing);  // spacing
	m_filter_btns.push_back(create_colorspace_btn(buttonRow, m_screen, m_images_panel));
	agrid->append_row(0);
	agrid->set_anchor(m_filter_btns.back(), AdvancedGridLayout::Anchor(0, agrid->row_count()-1, 3, 1));

	new Label(this, "Filters", "sans-bold");
	buttonRow = new Widget(this);
	buttonRow->set_layout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, spacing));
	m_filter_btns.push_back(create_gaussian_filter_btn(buttonRow, m_screen, m_images_panel));
	m_filter_btns.push_back(create_box_filter_btn(buttonRow, m_screen, m_images_panel));
	m_filter_btns.push_back(create_bilateral_filter_btn(buttonRow, m_screen, m_images_panel));
	m_filter_btns.push_back(create_unsharp_mask_filter_btn(buttonRow, m_screen, m_images_panel));
	m_filter_btns.push_back(create_median_filter_btn(buttonRow, m_screen, m_images_panel));
}


void EditImagePanel::draw(NVGcontext *ctx)
{
	auto img = m_images_panel->current_image();

	bool can_modify = img && img->can_modify();

	if (enabled() != can_modify)
	{
		set_enabled(can_modify);
		for (auto btn : m_filter_btns)
			btn->set_enabled(can_modify);
	}

	m_undo_btn->set_enabled(can_modify && img->has_undo());
	m_redo_btn->set_enabled(can_modify && img->has_redo());


	Widget::draw(ctx);
}