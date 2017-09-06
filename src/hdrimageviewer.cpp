//
// Created by Wojciech Jarosz on 9/4/17.
//

#include "hdrimageviewer.h"
#include "hdrviewer.h"
#include <tinydir.h>

using namespace std;

namespace
{

void drawText(NVGcontext* ctx,
              const Vector2i & pos,
              const std::string & text,
              const Color & col = Color(1.0f, 1.0f, 1.0f, 1.0f),
              int fontSize = 10,
              int fixedWidth = 0);
}

NAMESPACE_BEGIN(nanogui)

HDRImageViewer::HDRImageViewer(HDRViewScreen * parent)
	: Widget(parent), m_screen(parent),
	  m_exposureCallback(std::function<void(bool)>()),
	  m_gammaCallback(std::function<void(bool)>())
{
	m_ditherer.init();
}

void HDRImageViewer::draw(NVGcontext* ctx)
{
	Widget::draw(ctx);
	nvgEndFrame(ctx); // Flush the NanoVG draw stack, not necessary to call nvgBeginFrame afterwards.

	if (m_image)
	{
		Vector2f screenSize = m_screen->size().cast<float>();
		Vector2f positionInScreen = absolutePosition().cast<float>();

		glEnable(GL_SCISSOR_TEST);
		float r = m_screen->pixelRatio();
		glScissor(positionInScreen.x() * r,
		          (screenSize.y() - positionInScreen.y() - size().y()) * r,
		          size().x() * r, size().y() * r);

		Matrix4f trans;
		trans.setIdentity();
		trans.rightCols<1>() = Vector4f(2 * m_offset.x() / screenSize.x(),
		                                -2 * m_offset.y() / screenSize.y(),
		                                0.0f, 1.0f);
		Matrix4f scale;
		scale.setIdentity();
		scale(0, 0) = m_zoom;
		scale(1, 1) = m_zoom;

		Matrix4f imageScale;
		imageScale.setIdentity();
		imageScale(0, 0) = float(m_image->size()[0]) / screenSize.x();
		imageScale(1, 1) = float(m_image->size()[1]) / screenSize.y();

		Matrix4f offset;
		offset.setIdentity();
		offset.rightCols<1>() = Vector4f(positionInScreen.x() / screenSize.x(),
		                                 -positionInScreen.y() / screenSize.y(),
		                                 0.0f, 1.0f);

		Matrix4f mvp;
		mvp.setIdentity();
		mvp = offset * scale * trans * imageScale;

		m_ditherer.bind();
		m_image->draw(mvp, powf(2.0f, m_exposure), m_gamma, m_sRGB, m_dither, m_channels);

		drawPixelLabels(ctx);
		drawPixelGrid(ctx, mvp);

		glDisable(GL_SCISSOR_TEST);
	}

	// Draw an inner drop shadow. (adapted from nanogui::Window) and tev
	int ds = mTheme->mWindowDropShadowSize, cr = mTheme->mWindowCornerRadius;
	NVGpaint shadowPaint = nvgBoxGradient(ctx, mPos.x(), mPos.y(), mSize.x(), mSize.y(), cr * 2, ds * 2,
	                                      mTheme->mTransparent, mTheme->mDropShadow);

	nvgSave(ctx);
	nvgResetScissor(ctx);
	nvgBeginPath(ctx);
	nvgRect(ctx, mPos.x(), mPos.y(), mSize.x(), mSize.y());
	nvgRoundedRect(ctx, mPos.x() + ds, mPos.y() + ds, mSize.x() - 2 * ds, mSize.y() - 2 * ds, cr);
	nvgPathWinding(ctx, NVG_HOLE);
	nvgFillPaint(ctx, shadowPaint);
	nvgFill(ctx);
	nvgRestore(ctx);
}

void HDRImageViewer::drawPixelGrid(NVGcontext* ctx, const Matrix4f &mvp) const
{
	if (!m_drawGrid || m_zoom < 8 || !m_image)
		return;

	Vector2i xy0 = topLeftImageCorner2Screen();
	int minJ = max(0, int(-xy0.y() / m_zoom));
	int maxJ = min(m_image->height(), int(ceil((m_screen->size().y() - xy0.y())/m_zoom)));
	int minI = max(0, int(-xy0.x() / m_zoom));
	int maxI = min(m_image->width(), int(ceil((m_screen->size().x() - xy0.x())/m_zoom)));

	nvgBeginPath(ctx);

	// draw vertical lines
	for (int i = minI; i <= maxI; ++i)
	{
		Vector2i sxy0 = imageToScreen(Vector2i(i,minJ));
		Vector2i sxy1 = imageToScreen(Vector2i(i,maxJ));
		nvgMoveTo(ctx, sxy0.x(), sxy0.y());
		nvgLineTo(ctx, sxy1.x(), sxy1.y());
	}

	// draw horizontal lines
	for (int j = minJ; j <= maxJ; ++j)
	{
		Vector2i sxy0 = imageToScreen(Vector2i(minI, j));
		Vector2i sxy1 = imageToScreen(Vector2i(maxI, j));
		nvgMoveTo(ctx, sxy0.x(), sxy0.y());
		nvgLineTo(ctx, sxy1.x(), sxy1.y());
	}

	nvgStrokeWidth(ctx, 2.0f);
	nvgStrokeColor(ctx, Color(1.0f, 1.0f, 1.0f, 0.2f));
	nvgStroke(ctx);
}

void HDRImageViewer::drawPixelLabels(NVGcontext* ctx) const
{
	// if pixels are big enough, draw color labels on each visible pixel
	if (!m_drawValues || m_zoom < 32 || !m_image)
		return;

	Vector2i xy0 = topLeftImageCorner2Screen();
	int minJ = max(0, int(-xy0.y() / m_zoom));
	int maxJ = min(m_image->height()-1, int(ceil((m_screen->size().y() - xy0.y())/m_zoom)));
	int minI = max(0, int(-xy0.x() / m_zoom));
	int maxI = min(m_image->width()-1, int(ceil((m_screen->size().x() - xy0.x())/m_zoom)));
	for (int j = minJ; j <= maxJ; ++j)
	{
		for (int i = minI; i <= maxI; ++i)
		{
			Color4 pixel = m_image->image()(i, j);

			float luminance = pixel.luminance() * pow(2.0f, m_exposure);

			string text = fmt::format("{:1.3f}\n{:1.3f}\n{:1.3f}", pixel[0], pixel[1], pixel[2]);

			drawText(ctx, imageToScreen(Vector2i(i,j)), text,
			         luminance > 0.5f ? Color(0.0f, 0.0f, 0.0f, 0.5f) : Color(1.0f, 1.0f, 1.0f, 0.5f),
			         int(m_zoom/32.0f * 10), int(m_zoom));
		}
	}
}

Vector2i HDRImageViewer::topLeftImageCorner2Screen() const
{
	if (!m_image)
		return Vector2i(0,0);

	return Vector2i(int(m_offset[0] * m_zoom) + int(-m_image->size()[0] / 2.f * m_zoom) + int(m_screen->size().x() / 2.f) + absolutePosition().x() / 2,
	                int(m_offset[1] * m_zoom) + int(-m_image->size()[1] / 2.f * m_zoom) + int(m_screen->size().y() / 2.f) + absolutePosition().y() / 2);
}

Vector2i HDRImageViewer::imageToScreen(const Vector2i & pixel) const
{
	if (!m_image)
		return Vector2i(0,0);

	return Vector2i(pixel.x() * m_zoom, pixel.y() * m_zoom) + topLeftImageCorner2Screen();
}

Vector2i HDRImageViewer::screenToImage(const Vector2i & p) const
{
	if (!m_image)
		return Vector2i(0,0);

	Vector2i xy0 = topLeftImageCorner2Screen();
	return Vector2i(int(floor((p[0] - xy0.x()) / m_zoom)),
	                int(floor((p[1] - xy0.y()) / m_zoom)));;
}

NAMESPACE_END(nanogui)

namespace
{

void drawText(NVGcontext* ctx,
              const Vector2i & pos,
              const string & text,
              const Color & color,
              int fontSize,
              int fixedWidth)
{
	nvgFontFace(ctx, "sans");
	nvgFontSize(ctx, (float) fontSize);
	nvgFillColor(ctx, color);
	if (fixedWidth > 0)
	{
		nvgTextAlign(ctx, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		nvgTextBox(ctx, (float) pos.x(), (float) pos.y(), (float) fixedWidth, text.c_str(), nullptr);
	}
	else
	{
		nvgTextAlign(ctx, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		nvgText(ctx, (float) pos.x(), (float) pos.y() + fontSize, text.c_str(), nullptr);
	}
}

}