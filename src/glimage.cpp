//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "glimage.h"
#include "common.h"
#include "timer.h"
#include "colorspace.h"
#include "parallelfor.h"
#include <random>
#include <nanogui/common.h>
#include <nanogui/glutil.h>
#include <cmath>
#include <spdlog/spdlog.h>

using namespace nanogui;
using namespace Eigen;
using namespace std;

namespace
{
shared_ptr<ImageHistogram> makeHistograms(const HDRImage & img, float exposure)
{
	static const int numBins = 256;

	auto ret = make_shared<ImageHistogram>();
    ret->linearHistogram = ret->sRGBHistogram = MatrixX3f::Zero(numBins, 3);
	ret->exposure = exposure;
	ret->average = 0;
	ret->maximum = -numeric_limits<float>::infinity();
	ret->minimum = numeric_limits<float>::infinity();

	Color4 gain(pow(2.f, exposure), 1.f);
	float d = 1.f / (img.width() * img.height());

//    parallel_for(0, img.height(), [&ret, &img, gain, d](int y)
    for (int y = 0; y < img.height(); ++y)
    {
        for (int x = 0; x < img.width(); ++x)
        {
            Color4 clin = img(x,y);
            Color4 crgb = LinearToSRGB(img(x, y) * gain);

	        for (int c = 0; c < 3; ++c)
	        {
		        float val = clin[c];
		        ret->average += val;
		        ret->maximum = max(ret->maximum, val);
		        ret->minimum = min(ret->minimum, val);
	        }

	        clin *= gain;

	        ret->linearHistogram(clamp(int(floor(clin[0] * numBins)), 0, numBins - 1), 0) += d;
	        ret->linearHistogram(clamp(int(floor(clin[1] * numBins)), 0, numBins - 1), 1) += d;
	        ret->linearHistogram(clamp(int(floor(clin[2] * numBins)), 0, numBins - 1), 2) += d;

	        ret->sRGBHistogram(clamp(int(floor(crgb[0] * numBins)), 0, numBins - 1), 0) += d;
	        ret->sRGBHistogram(clamp(int(floor(crgb[1] * numBins)), 0, numBins - 1), 1) += d;
	        ret->sRGBHistogram(clamp(int(floor(crgb[2] * numBins)), 0, numBins - 1), 2) += d;
        }
    }
//	);

	ret->average /= img.width() * img.height();

	return ret;
}

} // namespace

GLImage::GLImage() :
    m_image(make_shared<HDRImage>()),
    m_filename(),
    m_cachedHistogramExposure(NAN), m_histogramDirty(true)
{
    // empty
}


GLImage::~GLImage()
{
    if (m_texture)
        glDeleteTextures(1, &m_texture);
    m_texture = 0;
}

bool GLImage::checkAsyncResult() const
{
	if (!m_asyncCommand || !m_asyncCommand->ready())
		return false;

	return waitForAsyncResult();
}


bool GLImage::waitForAsyncResult() const
{
	if (!m_asyncCommand)
		return false;

	auto result = m_asyncCommand->get();

	m_history.addCommand(result.second);
	m_image = result.first;
	m_histogramDirty = true;

	// now that we grabbed the results, destroy the task
	m_asyncCommand = nullptr;

	init();

	return true;
}


GLuint GLImage::glTextureId() const
{
    if (!m_texture || checkAsyncResult())
        init();
    return m_texture;
}

void GLImage::init() const
{
	Timer timer;
    // Allocate texture memory for the image
	if (!m_texture)
        glGenTextures(1, &m_texture);
	spdlog::get("console")->trace("generating texture took: {} ms", timer.lap());
    glBindTexture(GL_TEXTURE_2D, m_texture);
	spdlog::get("console")->trace("binding texture took: {} ms", timer.lap());

//    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width(), height(),
//                 0, GL_RGBA, GL_FLOAT, (const GLvoid *) m_image->data());

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width(), height(), 0, GL_RGBA, GL_FLOAT, nullptr);

	spdlog::get("console")->trace("allocating GPU texture data took: {} ms", timer.lap());

	glPixelStorei(GL_UNPACK_ROW_LENGTH, width());

	Vector2i tileSize(512, 512);
	Vector2i numTiles(std::ceil(float(width()) / tileSize.x()),
	                  std::ceil(float(height()) / tileSize.y()));
	Vector2i tile;
	Timer tileTimer;
	for (tile.y() = 0; tile.y() < numTiles.y(); ++tile.y())
	{
		for (tile.x() = 0; tile.x() < numTiles.x(); ++tile.x())
		{
			Vector2i tlCorner = tile.cwiseProduct(tileSize);

			// compute tile size, accounting for partial tiles at boundary
			Vector2i remaining = size() - tlCorner;
			Vector2i tSize = tileSize.cwiseMin(remaining);

			glPixelStorei(GL_UNPACK_SKIP_PIXELS, tlCorner.x());
			glPixelStorei(GL_UNPACK_SKIP_ROWS, tlCorner.y());

			glTexSubImage2D(GL_TEXTURE_2D,
			                0,				             // level
			                tlCorner.x(), tlCorner.y(),	 // xoffset, yoffset
			                tSize.x(), tSize.y(),        // tile width and height
			                GL_RGBA,			         // format
			                GL_FLOAT,		             // type
							(const GLvoid *) m_image->data());
		}
	}
	spdlog::get("console")->trace("uploading texture data to GPU took: {} ms", timer.lap());

	glGenerateMipmap(GL_TEXTURE_2D);  //Generate num_mipmaps number of mipmaps here.
	spdlog::get("console")->trace("generating mipmaps took: {} ms", timer.lap());

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const GLfloat borderColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
}

bool GLImage::canModify() const
{
    return !m_asyncCommand || m_asyncCommand->ready();
}

void GLImage::asyncModify(const ImageCommandWithProgress & command)
{
	// make sure any pending edits are done
	waitForAsyncResult();

	m_asyncCommand = make_shared<AsyncTask<ImageCommandResult>>([this,&command](AtomicProgress & prog){return command(m_image, prog);});
	m_asyncCommand->compute();
}

void GLImage::asyncModify(const ImageCommand &command)
{
	// make sure any pending edits are done
	waitForAsyncResult();

    m_asyncCommand = make_shared<AsyncTask<ImageCommandResult>>([this,&command](void){return command(m_image);});
    m_asyncCommand->compute();
}

bool GLImage::undo()
{
	Timer timer;
    // make sure any pending edits are done
	waitForAsyncResult();
	spdlog::get("console")->debug("getting result took: {} ms", timer.lap());

	if (m_history.undo(m_image))
    {
	    spdlog::get("console")->debug("undoing took: {} ms", timer.lap());
        m_histogramDirty = true;
	    init();
	    spdlog::get("console")->debug("initializing GL texture took: {} ms", timer.lap());
        return true;
    }
    return false;
}

bool GLImage::redo()
{
    // make sure any pending edits are done
	waitForAsyncResult();

	if (m_history.redo(m_image))
    {
        m_histogramDirty = true;
	    init();
        return true;
    }
    return false;
}

bool GLImage::load(const std::string & filename)
{
	// make sure any pending edits are done
	waitForAsyncResult();

    m_history = CommandHistory();
    m_filename = filename;
    m_histogramDirty = true;
    return m_image->load(filename);
}

bool GLImage::save(const std::string & filename,
                   float gain, float gamma,
                   bool sRGB, bool dither) const
{
	// make sure any pending edits are done
	waitForAsyncResult();

    m_history.markSaved();
    return m_image->save(filename, gain, gamma, sRGB, dither);
}

void GLImage::recomputeHistograms(float exposure) const
{
	checkAsyncResult();

    if ((!m_histograms || m_histogramDirty || exposure != m_cachedHistogramExposure) && canModify())
    {
        m_histograms = make_shared<LazyHistograms>(
	        [this,exposure](void)
	        {
		        return makeHistograms(*m_image, exposure);
	        });
        m_histograms->compute();
        m_histogramDirty = false;
        m_cachedHistogramExposure = exposure;
    }
}