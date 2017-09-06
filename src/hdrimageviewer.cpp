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
	  m_gammaCallback(std::function<void(bool)>()),
	  m_imageChangedCallback(std::function<void(int)>()),
	  m_numLayersCallback(std::function<void(void)>()),
	  m_layerSelectedCallback(std::function<void(int)>())
{
	m_ditherer.init();
}


const GLImage * HDRImageViewer::currentImage() const
{
	return image(m_current);
}

GLImage * HDRImageViewer::currentImage()
{
	return image(m_current);
}

const GLImage * HDRImageViewer::image(int index) const
{
	return (index < 0 || index >= int(m_images.size())) ? nullptr : m_images[index];
}

GLImage * HDRImageViewer::image(int index)
{
	return (index < 0 || index >= int(m_images.size())) ? nullptr : m_images[index];
}

void HDRImageViewer::selectLayer(int index)
{
	if (index != m_current)
	{
		m_current = index;
		m_layerSelectedCallback(m_current);
	}
}

bool HDRImageViewer::loadImages(const vector<string> & filenames)
{
	size_t numErrors = 0;
	vector<pair<string, bool> > loadedOK;
	for (auto i : filenames)
	{
		tinydir_dir dir;
		if (tinydir_open(&dir, i.c_str()) != -1)
		{
			try
			{
				// filename is actually a directory, traverse it
				spdlog::get("console")->info("Loading images in \"{}\"...", dir.path);
				while (dir.has_next)
				{
					tinydir_file file;
					if (tinydir_readfile(&dir, &file) == -1)
						throw std::runtime_error("Error getting file");

					if (!file.is_reg)
					{
						if (tinydir_next(&dir) == -1)
							throw std::runtime_error("Error getting next file");
						continue;
					}

					// only consider image files we support
					string ext = file.extension;
					transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
					if (ext != "exr" && ext != "png" && ext != "jpg" &&
						ext != "jpeg" && ext != "hdr" && ext != "pic" &&
						ext != "pfm" && ext != "ppm" && ext != "bmp" &&
						ext != "tga" && ext != "psd")
					{
						if (tinydir_next(&dir) == -1)
							throw std::runtime_error("Error getting next file");
						continue;
					}

					GLImage* image = new GLImage();
					if (image->load(file.path))
					{
						loadedOK.push_back({file.path, true});
						image->init();
						m_images.push_back(image);
						spdlog::get("console")->info("Loaded \"{}\" [{:d}x{:d}]", file.name,
						                             image->width(), image->height());
					}
					else
					{
						loadedOK.push_back({file.name, false});
						numErrors++;
						delete image;
					}

					if (tinydir_next(&dir) == -1)
						throw std::runtime_error("Error getting next file");
				}

				tinydir_close(&dir);
			}
			catch (const exception & e)
			{
				spdlog::get("console")->error("Error listing directory: ({}).", e.what());
			}
		}
		else
		{
			GLImage* image = new GLImage();
			if (image->load(i))
			{
				loadedOK.push_back({i, true});
				image->init();
				m_images.push_back(image);
				spdlog::get("console")->info("Loaded \"{}\" [{:d}x{:d}]", i, image->width(), image->height());
			}
			else
			{
				loadedOK.push_back({i, false});
				numErrors++;
				delete image;
			}
		}
		tinydir_close(&dir);
	}

	m_numLayersCallback();
	selectLayer(int(m_images.size()-1));

	if (numErrors)
	{
		string badFiles;
		for (size_t i = 0; i < loadedOK.size(); ++i)
		{
			if (!loadedOK[i].second)
				badFiles += loadedOK[i].first + "\n";
		}
		new MessageDialog(m_screen, MessageDialog::Type::Warning, "Error", "Could not load:\n " + badFiles);
		return numErrors == filenames.size();
	}

	return true;
}

void HDRImageViewer::saveImage()
{
	if (!currentImage())
		return;

	string filename = file_dialog({
		                          {"png", "Portable Network Graphic"},
		                          {"pfm", "Portable Float Map"},
		                          {"ppm", "Portable PixMap"},
		                          {"tga", "Targa image"},
		                          {"bmp", "Windows Bitmap image"},
		                          {"hdr", "Radiance rgbE format"},
		                          {"exr", "OpenEXR image"}
	                          }, true);

	if (filename.size())
	{
		try
		{
			currentImage()->save(filename, powf(2.0f, m_exposure), m_gamma, m_sRGB, m_dither);
		}
		catch (std::runtime_error &e)
		{
			new MessageDialog(m_screen, MessageDialog::Type::Warning, "Error",
			                  string("Could not save image due to an error:\n") + e.what());
		}

		m_numLayersCallback();
	}
}

void HDRImageViewer::closeImage(int index)
{
	const GLImage * img = image(index);

	if (img)
	{
		auto closeIt = [&,index](int close = 0)
		{
			if (close != 0)
				return;

			delete m_images[index];
			m_images.erase(m_images.begin()+index);

			int newIndex = m_current;
			if (index < m_current)
				newIndex--;
			else if (m_current >= int(m_images.size()))
				newIndex = m_images.size()-1;

			selectLayer(newIndex);
			m_numLayersCallback();
		};

		if (img->isModified())
		{
			auto dialog = new MessageDialog(m_screen, MessageDialog::Type::Warning, "Warning!",
			                                "Image is modified. Close anyway?", "Close", "Cancel", true);
			dialog->setCallback(closeIt);
		}
		else
			closeIt();
	}
}

void HDRImageViewer::modifyImage(const std::function<ImageCommandUndo*(HDRImage & img)> & command)
{
	if (currentImage())
	{
		m_images[m_current]->modify(command);
		m_imageChangedCallback(m_current);
	}
}
void HDRImageViewer::undo()
{
	if (currentImage() && m_images[m_current]->undo())
		m_imageChangedCallback(m_current);
}
void HDRImageViewer::redo()
{
	if (currentImage() && m_images[m_current]->redo())
		m_imageChangedCallback(m_current);
}


void HDRImageViewer::sendLayerBackward()
{
    if (m_images.empty() || m_current == 0)
        // do nothing
        return;

    std::swap(m_images[m_current], m_images[m_current-1]);
	m_current--;

	m_imageChangedCallback(m_current);
	m_layerSelectedCallback(m_current);
}


void HDRImageViewer::bringLayerForward()
{
    if (m_images.empty() || m_current == int(m_images.size()-1))
        // do nothing
        return;

    std::swap(m_images[m_current], m_images[m_current+1]);
	m_current++;

	m_imageChangedCallback(m_current);
	m_layerSelectedCallback(m_current);
}

void HDRImageViewer::draw(NVGcontext* ctx)
{
	Widget::draw(ctx);
	nvgEndFrame(ctx); // Flush the NanoVG draw stack, not necessary to call nvgBeginFrame afterwards.

	const GLImage * img = currentImage();
	if (img)
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
		trans.rightCols<1>() = Vector4f( 2 * m_offset.x() / screenSize.x(),
		                                -2 * m_offset.y() / screenSize.y(),
		                                 0.0f, 1.0f);
		Matrix4f scale;
		scale.setIdentity();
		scale(0,0) = m_zoom;
		scale(1,1) = m_zoom;

		Matrix4f imageScale;
		imageScale.setIdentity();
		imageScale(0,0) = float(img->size()[0]) / screenSize.x();
		imageScale(1,1) = float(img->size()[1]) / screenSize.y();

		Matrix4f offset;
		offset.setIdentity();
		offset.rightCols<1>() = Vector4f( positionInScreen.x() / screenSize.x(),
		                                 -positionInScreen.y() / screenSize.y(),
		                                 0.0f, 1.0f);

		Matrix4f mvp;
		mvp.setIdentity();
		mvp = offset * scale * trans * imageScale;

		m_ditherer.bind();
		img->draw(mvp, powf(2.0f, m_exposure), m_gamma, m_sRGB, m_dither, m_channels);

		drawPixelLabels(ctx);
		drawPixelGrid(ctx, mvp);

		glDisable(GL_SCISSOR_TEST);
	}
}

void HDRImageViewer::drawPixelGrid(NVGcontext* ctx, const Matrix4f &mvp) const
{
	const GLImage * img = currentImage();
	if (!m_drawGrid || m_zoom < 8 || !img)
		return;

	Vector2i xy0 = topLeftImageCorner2Screen();
	int minJ = max(0, int(-xy0.y() / m_zoom));
	int maxJ = min(img->height(), int(ceil((m_screen->size().y() - xy0.y())/m_zoom)));
	int minI = max(0, int(-xy0.x() / m_zoom));
	int maxI = min(img->width(), int(ceil((m_screen->size().x() - xy0.x())/m_zoom)));

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
	const GLImage * img = currentImage();
	// if pixels are big enough, draw color labels on each visible pixel
	if (!m_drawValues || m_zoom < 32 || !img)
		return;

	Vector2i xy0 = topLeftImageCorner2Screen();
	int minJ = max(0, int(-xy0.y() / m_zoom));
	int maxJ = min(img->height()-1, int(ceil((m_screen->size().y() - xy0.y())/m_zoom)));
	int minI = max(0, int(-xy0.x() / m_zoom));
	int maxI = min(img->width()-1, int(ceil((m_screen->size().x() - xy0.x())/m_zoom)));
	for (int j = minJ; j <= maxJ; ++j)
	{
		for (int i = minI; i <= maxI; ++i)
		{
			Color4 pixel = img->image()(i, j);

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
	const GLImage * img = currentImage();
	if (!img)
		return Vector2i(0,0);

	return Vector2i(int(m_offset[0] * m_zoom) + int(-img->size()[0] / 2.f * m_zoom) + int(m_screen->size().x() / 2.f) + absolutePosition().x() / 2,
	                int(m_offset[1] * m_zoom) + int(-img->size()[1] / 2.f * m_zoom) + int(m_screen->size().y() / 2.f) + absolutePosition().y() / 2);
}

Vector2i HDRImageViewer::imageToScreen(const Vector2i & pixel) const
{
	const GLImage * img = currentImage();
	if (!img)
		return Vector2i(0,0);

	return Vector2i(pixel.x() * m_zoom, pixel.y() * m_zoom) + topLeftImageCorner2Screen();
}

Vector2i HDRImageViewer::screenToImage(const Vector2i & p) const
{
	const GLImage * img = currentImage();
	if (!img)
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