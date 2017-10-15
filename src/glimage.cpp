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
    ret->linearHistogram = ret->sRGBHistogram = ret->logHistogram = MatrixX3f::Zero(numBins, 3);
	ret->exposure = exposure;
	ret->average = 0;

	ret->maximum = img.max().Color3::max();
	ret->minimum = img.min().Color3::min();

	Color4 gain(pow(2.f, exposure), 1.f);
	float d = 1.f / (img.width() * img.height());

    for (int y = 0; y < img.height(); ++y)
    {
        for (int x = 0; x < img.width(); ++x)
        {
            Color4 clin = gain * img(x,y);
            Color4 crgb = LinearToSRGB(clin);

	        ret->average += clin[0] + clin[1] + clin[2];

	        ret->linearHistogram(clamp(int(floor(clin[0] * numBins)), 0, numBins - 1), 0) += d;
	        ret->linearHistogram(clamp(int(floor(clin[1] * numBins)), 0, numBins - 1), 1) += d;
	        ret->linearHistogram(clamp(int(floor(clin[2] * numBins)), 0, numBins - 1), 2) += d;

	        ret->sRGBHistogram(clamp(int(floor(crgb[0] * numBins)), 0, numBins - 1), 0) += d;
	        ret->sRGBHistogram(clamp(int(floor(crgb[1] * numBins)), 0, numBins - 1), 1) += d;
	        ret->sRGBHistogram(clamp(int(floor(crgb[2] * numBins)), 0, numBins - 1), 2) += d;

	        ret->logHistogram(clamp(int(floor(normalizedLogScale(clin[0]) * numBins)), 0, numBins - 1), 0) += d;
	        ret->logHistogram(clamp(int(floor(normalizedLogScale(clin[1]) * numBins)), 0, numBins - 1), 1) += d;
	        ret->logHistogram(clamp(int(floor(normalizedLogScale(clin[2]) * numBins)), 0, numBins - 1), 2) += d;
        }
    }

	ret->average /= 3 * img.width() * img.height();


	// Normalize each histogram according to its 10th-largest bin
	MatrixX3f * hists[3] = {&ret->linearHistogram, &ret->sRGBHistogram, &ret->logHistogram};
	MatrixXf temp;
	for (int i = 0; i < 3; ++i)
	{
		temp = *(hists[i]);
		DenseIndex idx = temp.size() - 10;
		nth_element(temp.data(), temp.data() + idx, temp.data() + temp.size());
		*(hists[i]) /= temp(idx);
	}

	return ret;
}

} // namespace



LazyGLTextureLoader::~LazyGLTextureLoader()
{
	if (m_texture)
		glDeleteTextures(1, &m_texture);
}

bool LazyGLTextureLoader::uploadToGPU(const std::shared_ptr<const HDRImage> &img,
                                      int milliseconds,
                                      int chunkSize)
{
	if (img->isNull())
	{
		m_dirty = false;
		return false;
	}

	// check if we need to upload the image to the GPU
	if (!m_dirty && m_texture)
		return false;

	Timer timer;
	// Allocate texture memory for the image
	if (!m_texture)
		glGenTextures(1, &m_texture);

	glBindTexture(GL_TEXTURE_2D, m_texture);

	// allocate a new texture and set parameters only if this is the first scanline
	if (m_nextScanline == 0)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
		             img->width(), img->height(),
		             0, GL_RGBA, GL_FLOAT, nullptr);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

		glPixelStorei(GL_UNPACK_ROW_LENGTH, img->width());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		const GLfloat borderColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

	int maxLines = max(1, chunkSize / img->width());
	while (true)
	{
		// compute tile size, accounting for partial tiles at boundary
		int remaining = img->height() - m_nextScanline;
		int numLines = std::min(maxLines, remaining);

		glPixelStorei(GL_UNPACK_SKIP_ROWS, m_nextScanline);
		glTexSubImage2D(GL_TEXTURE_2D,
		                0,		                     // level
		                0, m_nextScanline,	         // xoffset, yoffset
		                img->width(), numLines,      // tile width and height
		                GL_RGBA,			         // format
		                GL_FLOAT,		             // type
		                (const GLvoid *) img->data());

		m_nextScanline += maxLines;

		if (m_nextScanline >= img->height())
		{
			// done
			m_nextScanline = -1;
			m_dirty = false;
			break;
		}
		if (timer.elapsed() > milliseconds)
			break;
	}

	m_uploadTime += timer.lap();

	if (!m_dirty)
	{
		spdlog::get("console")->trace("Uploading texture to GPU took {} ms", m_uploadTime);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
		glGenerateMipmap(GL_TEXTURE_2D);  //Generate num_mipmaps number of mipmaps here.
		spdlog::get("console")->trace("Generating mipmaps took {} ms", timer.lap());
	}

	return !m_dirty;
}



GLImage::GLImage() :
    m_image(make_shared<HDRImage>()),
    m_filename(),
    m_cachedHistogramExposure(NAN),
    m_histogramDirty(true),
    m_imageModifyDoneCallback(GLImage::VoidVoidFunc())
{
    // empty
}


GLImage::~GLImage()
{

}

float GLImage::progress() const
{
	checkAsyncResult();
	return m_asyncCommand ? m_asyncCommand->progress() : 1.0f;
}
bool GLImage::isModified() const    { checkAsyncResult(); return m_history.isModified(); }
bool GLImage::hasUndo() const       { checkAsyncResult(); return m_history.hasUndo(); }
bool GLImage::hasRedo() const       { checkAsyncResult(); return m_history.hasRedo(); }

bool GLImage::canModify() const
{
	return !m_asyncCommand;
}

void GLImage::asyncModify(const ImageCommandWithProgress & command)
{
	// make sure any pending edits are done
	waitForAsyncResult();

	m_asyncCommand = make_shared<AsyncTask<ImageCommandResult>>([this,command](AtomicProgress & prog){return command(m_image, prog);});
	m_asyncRetrieved = false;
	m_asyncCommand->compute();
}

void GLImage::asyncModify(const ImageCommand &command)
{
	// make sure any pending edits are done
	waitForAsyncResult();

	m_asyncCommand = make_shared<AsyncTask<ImageCommandResult>>([this,command](void){return command(m_image);});
	m_asyncRetrieved = false;
	m_asyncCommand->compute();
}

bool GLImage::undo()
{
	// make sure any pending edits are done
	waitForAsyncResult();

	if (m_history.undo(m_image))
	{
		m_histogramDirty = true;
		m_texture.setDirty();
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
		m_texture.setDirty();
		return true;
	}
	return false;
}

bool GLImage::checkAsyncResult() const
{
	if (!m_asyncCommand || !m_asyncCommand->ready())
		return false;

	return waitForAsyncResult();
}

void GLImage::modifyFinished() const
{
	m_asyncCommand = nullptr;
	if (m_imageModifyDoneCallback)
		m_imageModifyDoneCallback();
}


bool GLImage::waitForAsyncResult() const
{
	// nothing to wait for
	if (!m_asyncCommand)
		return false;

	if (!m_asyncRetrieved)
	{
		// now retrieve the result and copy it out of the async task
		auto result = m_asyncCommand->get();

		// if there is no undo, treat this as an image load
		if (!result.second)
		{
			if (result.first)
			{
				m_history = CommandHistory();
				m_image = result.first;
			}
		}
		else
		{
			m_history.addCommand(result.second);
			m_image = result.first;
		}

		m_asyncRetrieved = true;
		m_histogramDirty = true;
		m_texture.setDirty();

		if (!result.first)
		{
			// image loading failed
			modifyFinished();
			return false;
		}
	}

	// now set the progress bar to busy as we upload to GPU
	m_asyncCommand->setProgress(-1.f);

	uploadToGPU();

	return true;
}


void GLImage::uploadToGPU() const
{
	if (m_texture.uploadToGPU(m_image))
		// now that we grabbed the results and uploaded to GPU, destroy the task
		modifyFinished();
}


GLuint GLImage::glTextureId() const
{
	checkAsyncResult();
	uploadToGPU();
    return m_texture.textureID();
}


bool GLImage::load(const std::string & filename)
{
	// make sure any pending edits are done
	waitForAsyncResult();

    m_history = CommandHistory();
    m_filename = filename;
    m_histogramDirty = true;
	m_texture.setDirty();
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

    if ((!m_histograms || m_histogramDirty || exposure != m_cachedHistogramExposure) && !m_image->isNull())
    {
        m_histograms = make_shared<LazyHistogram>(
	        [this,exposure](void)
	        {
		        return makeHistograms(*m_image, exposure);
	        });
        m_histograms->compute();
        m_histogramDirty = false;
        m_cachedHistogramExposure = exposure;
    }
}