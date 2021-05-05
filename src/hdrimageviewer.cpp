//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrimageviewer.h"
#include "hdrviewscreen.h"
#include <tinydir.h>
#include <utility>
using namespace std;

namespace
{
const float MIN_ZOOM = 0.01f;

const float MAX_ZOOM = 512.f;
}

HDRImageViewer::HDRImageViewer(Widget* parent, HDRViewScreen* screen)
	: Widget(parent), m_screen(screen), m_zoom(1.f / m_screen->pixel_ratio()), m_offset(nanogui::Vector2f(0.0f)),
	  m_exposure_callback(std::function<void(float)>()), m_gamma_callback(std::function<void(float)>()),
	  m_sRGB_callback(std::function<void(bool)>()), m_zoom_callback(std::function<void(float)>()),
	  m_pixelHoverCallback(std::function<void(const nanogui::Vector2i&, const Color4&, const Color4&)>())
{

}

nanogui::Vector2f HDRImageViewer::screen_size_f() const
{
	return nanogui::Vector2f(m_screen->size());
}

nanogui::Vector2f HDRImageViewer::image_coordinate_at(const nanogui::Vector2f& position) const
{
	auto imagePosition = position - (m_offset + center_offset(m_currentImage));
	return imagePosition / m_zoom;
}

nanogui::Vector2f HDRImageViewer::clamped_image_coordinate_at(const nanogui::Vector2f& position) const
{
	auto imageCoordinate = image_coordinate_at(position);
	return nanogui::min(nanogui::max(imageCoordinate, nanogui::Vector2f(0.0f)), image_size_f(m_currentImage));
}

nanogui::Vector2f HDRImageViewer::position_for_coordinate(const nanogui::Vector2f& imageCoordinate) const
{
	return m_zoom * imageCoordinate + (m_offset + center_offset(m_currentImage));
}

nanogui::Vector2f HDRImageViewer::screen_position_for_coordinate(const nanogui::Vector2f& imageCoordinate) const
{
	return position_for_coordinate(imageCoordinate) + position_f();
}

void HDRImageViewer::set_image_coordinate_at(const nanogui::Vector2f& position, const nanogui::Vector2f& imageCoordinate)
{
	// Calculate where the new offset must be in order to satisfy the image position equation.
	// Round the floating point values to balance out the floating point to integer conversions.
	m_offset = position - (imageCoordinate * m_zoom);

	// Clamp offset so that the image remains near the screen.
	m_offset = nanogui::max(nanogui::min(m_offset, size_f()), -scaled_image_size_f(m_currentImage));

	m_offset -= center_offset(m_currentImage);
}

void HDRImageViewer::center()
{
	m_offset = nanogui::Vector2f(0.f, 0.f);
}

void HDRImageViewer::fit()
{
	// Calculate the appropriate scaling factor.
	nanogui::Vector2f factor(size_f() / image_size_f(m_currentImage));
	m_zoom = std::min(factor[0], factor[1]);
	center();
	m_zoom_callback(m_zoom);
}

void HDRImageViewer::move_offset(const nanogui::Vector2f& delta)
{
	// Apply the delta to the offset.
	m_offset += delta;

	// Prevent the image from going out of bounds.
	auto scaledSize = scaled_image_size_f(m_currentImage);
	if (m_offset.x() + scaledSize.x() < 0)
		m_offset.x() = -scaledSize.x();
	if (m_offset.x() > size_f().x())
		m_offset.x() = size_f().x();
	if (m_offset.y() + scaledSize.y() < 0)
		m_offset.y() = -scaledSize.y();
	if (m_offset.y() > size_f().y())
		m_offset.y() = size_f().y();
}

void HDRImageViewer::set_zoom_level(float level)
{
	m_zoom = ::clamp(std::pow(m_zoom_sensitivity, level), MIN_ZOOM, MAX_ZOOM);
	m_zoom_level = log(m_zoom) / log(m_zoom_sensitivity);
	m_zoom_callback(m_zoom);
}

void HDRImageViewer::zoom_by(float amount, const nanogui::Vector2f& focusPosition)
{
	auto focusedCoordinate = image_coordinate_at(focusPosition);
	float scaleFactor = std::pow(m_zoom_sensitivity, amount);
	m_zoom = ::clamp(scaleFactor * m_zoom, MIN_ZOOM, MAX_ZOOM);
	m_zoom_level = log(m_zoom) / log(m_zoom_sensitivity);
	set_image_coordinate_at(focusPosition, focusedCoordinate);
	m_zoom_callback(m_zoom);
}

void HDRImageViewer::zoom_in()
{
	// keep position at center of window fixed while zooming
	auto centerPosition = nanogui::Vector2f(size_f() / 2.f);
	auto centerCoordinate = image_coordinate_at(centerPosition);

	// determine next higher power of 2 zoom level
	float levelForPow2Sensitivity = ceil(log(m_zoom) / log(2.f) + 0.5f);
	float newScale = std::pow(2.f, levelForPow2Sensitivity);
	m_zoom = ::clamp(newScale, MIN_ZOOM, MAX_ZOOM);
	m_zoom_level = log(m_zoom) / log(m_zoom_sensitivity);
	set_image_coordinate_at(centerPosition, centerCoordinate);
	m_zoom_callback(m_zoom);
}

void HDRImageViewer::zoom_out()
{
	// keep position at center of window fixed while zooming
	auto centerPosition = nanogui::Vector2f(size_f() / 2.f);
	auto centerCoordinate = image_coordinate_at(centerPosition);

	// determine next lower power of 2 zoom level
	float levelForPow2Sensitivity = floor(log(m_zoom) / log(2.f) - 0.5f);
	float newScale = std::pow(2.f, levelForPow2Sensitivity);
	m_zoom = ::clamp(newScale, MIN_ZOOM, MAX_ZOOM);
	m_zoom_level = log(m_zoom) / log(m_zoom_sensitivity);
	set_image_coordinate_at(centerPosition, centerCoordinate);
	m_zoom_callback(m_zoom);
}

bool HDRImageViewer::mouse_drag_event(const nanogui::Vector2i& p, const nanogui::Vector2i& rel, int button, int /*modifiers*/)
{
	if (button & (1 << GLFW_MOUSE_BUTTON_LEFT))
	{
		set_image_coordinate_at(p + rel, image_coordinate_at(p));
		return true;
	}
	return false;
}

bool HDRImageViewer::mouse_motion_event(const nanogui::Vector2i& p, const nanogui::Vector2i& rel, int button, int modifiers)
{
	if (Widget::mouse_motion_event(p, rel, button, modifiers))
		return true;

	if (!m_currentImage)
		return false;

	nanogui::Vector2i pixel(image_coordinate_at((p - m_pos)));
	Color4 pixelVal(0.f);
	Color4 iPixelVal(0.f);
	if (m_currentImage->contains(pixel))
	{
		pixelVal = m_currentImage->image()(pixel.x(), pixel.y());
		iPixelVal = (pixelVal * pow(2.f, m_exposure) * 255).min(255.f).max(0.f);
	}

	m_pixelHoverCallback(pixel, pixelVal, iPixelVal);

	return false;
}

bool HDRImageViewer::scroll_event(const nanogui::Vector2i& p, const nanogui::Vector2f& rel)
{
	if (Widget::scroll_event(p, rel))
		return true;

	// query glfw directly to check if a modifier key is pressed
	int lState = glfwGetKey(m_screen->glfw_window(), GLFW_KEY_LEFT_SHIFT);
	int rState = glfwGetKey(m_screen->glfw_window(), GLFW_KEY_RIGHT_SHIFT);

	if (lState == GLFW_PRESS || rState == GLFW_PRESS)
	{
		// panning
		set_image_coordinate_at(nanogui::Vector2f(p) + rel * 4.f, image_coordinate_at(p));
		return true;
	}
	else if (m_screen->modifiers() == 0)
	{
		// zooming
		float v = rel.y();
		if (std::abs(v) < 1)
			v = std::copysign(1.f, v);
		zoom_by(v / 4.f, p - position());

		return true;

	}

	return false;
}

bool HDRImageViewer::grid_visible() const
{
	return m_draw_grid && (m_grid_threshold != -1) && (m_zoom > m_grid_threshold);
}

bool HDRImageViewer::pixel_info_visible() const
{
	return m_draw_values && (m_pixel_info_threshold != -1) && (m_zoom > m_pixel_info_threshold);
}

bool HDRImageViewer::helpers_visible() const
{
	return grid_visible() || pixel_info_visible();
}

nanogui::Vector2f HDRImageViewer::center_offset(ConstImagePtr img) const
{
	return (size_f() - scaled_image_size_f(std::move(img))) / 2;
}

void HDRImageViewer::draw(NVGcontext* ctx)
{
	Widget::draw(ctx);
	nvgEndFrame(ctx); // Flush the NanoVG draw stack, not necessary to call nvgBeginFrame afterwards.

	nanogui::Vector2f screenSize(m_screen->size());
	nanogui::Vector2f positionInScreen(absolute_position());

	glEnable(GL_SCISSOR_TEST);
	float r = m_screen->pixel_ratio();
	glScissor(positionInScreen.x() * r, (screenSize.y() - positionInScreen.y() - size().y()) * r, size().x() * r,
			  size().y() * r);

	glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	if (m_currentImage && !m_currentImage->is_null())
	{
		nanogui::Vector2f pCurrent, sCurrent;
		image_position_and_scale(pCurrent, sCurrent, m_currentImage);

		if (m_referenceImage)
		{
			nanogui::Vector2f pReference, sReference;
			image_position_and_scale(pReference, sReference, m_referenceImage);
			m_shader
				.draw(m_currentImage->glTextureId(), m_referenceImage->glTextureId(), sCurrent, pCurrent, sReference,
					  pReference, powf(2.0f, m_exposure), m_gamma, m_sRGB, m_dither, m_channel, m_blendMode);
		}
		else
		{
			m_shader.draw(m_currentImage->glTextureId(), sCurrent, pCurrent, powf(2.0f, m_exposure), m_gamma, m_sRGB,
						  m_dither, m_channel, m_blendMode);
		}

		draw_image_border(ctx);

		if (helpers_visible())
			draw_helpers(ctx);
	}

	glDisable(GL_SCISSOR_TEST);

	draw_widget_border(ctx);
}

void HDRImageViewer::image_position_and_scale(nanogui::Vector2f& position, nanogui::Vector2f& scale, shared_ptr<const XPUImage> image)
{
	scale = scaled_image_size_f(image) / screen_size_f();
	position = (nanogui::Vector2f(absolute_position()) + m_offset + center_offset(image)) / screen_size_f();
}

void HDRImageViewer::draw_widget_border(NVGcontext* ctx) const
{
	// Draw an inner drop shadow. (adapted from nanogui::Window) and tev
	int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;
	NVGpaint shadowPaint =
		nvgBoxGradient(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), cr * 2, ds * 2, m_theme->m_transparent,
					   m_theme->m_drop_shadow);

	nvgSave(ctx);
	nvgResetScissor(ctx);
	nvgBeginPath(ctx);
	nvgRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
	nvgRoundedRect(ctx, m_pos.x() + ds, m_pos.y() + ds, m_size.x() - 2 * ds, m_size.y() - 2 * ds, cr);
	nvgPathWinding(ctx, NVG_HOLE);
	nvgFillPaint(ctx, shadowPaint);
	nvgFill(ctx);
	nvgRestore(ctx);
}

void HDRImageViewer::draw_image_border(NVGcontext* ctx) const
{
	int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;

	nanogui::Vector2i borderPosition = m_pos + nanogui::Vector2i(m_offset + center_offset(m_currentImage));
	nanogui::Vector2i borderSize(scaled_image_size_f(m_currentImage));

	if (m_referenceImage)
	{
		borderPosition = nanogui::min(borderPosition, m_pos + nanogui::Vector2i(m_offset + center_offset(m_referenceImage)));
		borderSize = nanogui::max(borderSize, nanogui::Vector2i(scaled_image_size_f(m_referenceImage)));
	}

	// Draw a drop shadow
	NVGpaint shadowPaint =
		nvgBoxGradient(ctx, borderPosition.x(), borderPosition.y(), borderSize.x(), borderSize.y(), cr * 2, ds * 2,
					   m_theme->m_drop_shadow, m_theme->m_transparent);

	nvgSave(ctx);
	nvgBeginPath(ctx);
	nvgScissor(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
	nvgRect(ctx, borderPosition.x() - ds, borderPosition.y() - ds, borderSize.x() + 2 * ds, borderSize.y() + 2 * ds);
	nvgRoundedRect(ctx, borderPosition.x(), borderPosition.y(), borderSize.x(), borderSize.y(), cr);
	nvgPathWinding(ctx, NVG_HOLE);
	nvgFillPaint(ctx, shadowPaint);
	nvgFill(ctx);
	nvgRestore(ctx);

	// draw a line border
	nvgSave(ctx);
	nvgBeginPath(ctx);
	nvgScissor(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
	nvgStrokeWidth(ctx, 2.0f);
	nvgRect(ctx, borderPosition.x() - 0.5f, borderPosition.y() - 0.5f, borderSize.x() + 1, borderSize.y() + 1);
	nvgStrokeColor(ctx, Color(0.5f, 0.5f, 0.5f, 1.0f));
	nvgStroke(ctx);
	nvgResetScissor(ctx);
	nvgRestore(ctx);
}

void HDRImageViewer::draw_helpers(NVGcontext* ctx) const
{
	if (grid_visible())
		draw_pixel_grid(ctx);
	if (pixel_info_visible())
		draw_pixel_info(ctx);
}

void HDRImageViewer::draw_pixel_grid(NVGcontext* ctx) const
{
	nanogui::Vector2f xy0 = screen_position_for_coordinate(nanogui::Vector2f(0.0f));
	int minJ = max(0, int(-xy0.y() / m_zoom));
	int maxJ = min(m_currentImage->height(), int(ceil((m_screen->size().y() - xy0.y()) / m_zoom)));
	int minI = max(0, int(-xy0.x() / m_zoom));
	int maxI = min(m_currentImage->width(), int(ceil((m_screen->size().x() - xy0.x()) / m_zoom)));

	nvgBeginPath(ctx);

	// draw vertical lines
	for (int i = minI; i <= maxI; ++i)
	{
		nanogui::Vector2f sxy0 = screen_position_for_coordinate(nanogui::Vector2f(i, minJ));
		nanogui::Vector2f sxy1 = screen_position_for_coordinate(nanogui::Vector2f(i, maxJ));
		nvgMoveTo(ctx, sxy0.x(), sxy0.y());
		nvgLineTo(ctx, sxy1.x(), sxy1.y());
	}

	// draw horizontal lines
	for (int j = minJ; j <= maxJ; ++j)
	{
		nanogui::Vector2f sxy0 = screen_position_for_coordinate(nanogui::Vector2f(minI, j));
		nanogui::Vector2f sxy1 = screen_position_for_coordinate(nanogui::Vector2f(maxI, j));
		nvgMoveTo(ctx, sxy0.x(), sxy0.y());
		nvgLineTo(ctx, sxy1.x(), sxy1.y());
	}

	nvgStrokeWidth(ctx, 2.0f);
	float factor = clamp01((m_zoom - m_grid_threshold)/(2*m_grid_threshold));
	float alpha = lerp(0.0f, 0.2f, smoothStep(0.0f, 1.0f, factor));
	nvgStrokeColor(ctx, Color(1.0f, 1.0f, 1.0f, alpha));
	nvgStroke(ctx);
}

void HDRImageViewer::draw_pixel_info(NVGcontext* ctx) const
{
	nanogui::Vector2f xy0 = screen_position_for_coordinate(nanogui::Vector2f(0.0f));
	int minJ = max(0, int(-xy0.y() / m_zoom));
	int maxJ = min(m_currentImage->height() - 1, int(ceil((m_screen->size().y() - xy0.y()) / m_zoom)));
	int minI = max(0, int(-xy0.x() / m_zoom));
	int maxI = min(m_currentImage->width() - 1, int(ceil((m_screen->size().x() - xy0.x()) / m_zoom)));

	float factor = clamp01((m_zoom - m_pixel_info_threshold)/(2*m_pixel_info_threshold));
	float alpha = lerp(0.0f, 0.5f, smoothStep(0.0f, 1.0f, factor));

	for (int j = minJ; j <= maxJ; ++j)
	{
		for (int i = minI; i <= maxI; ++i)
		{
			Color4 pixel = m_currentImage->image()(i, j);
			float luminance = pixel.luminance() * pow(2.0f, m_exposure);
			string text = fmt::format("{:1.3f}\n{:1.3f}\n{:1.3f}", pixel[0], pixel[1], pixel[2]);

			auto pos = screen_position_for_coordinate(nanogui::Vector2f(i, j));
			nvgFontFace(ctx, "sans");
			nvgFontSize(ctx, m_zoom / 31.0f * 10);
			nvgFillColor(ctx, luminance > 0.5f ? Color(0.0f, 0.0f, 0.0f, alpha) : Color(1.0f, 1.0f, 1.0f, alpha));
			nvgTextAlign(ctx, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgTextBox(ctx, pos.x(), pos.y(), m_zoom, text.c_str(), nullptr);
		}
	}
}