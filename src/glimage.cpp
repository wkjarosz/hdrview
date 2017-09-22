/*! \file glimage.cpp
    \author Wojciech Jarosz
*/
#include "glimage.h"
#include "common.h"
#include "colorspace.h"
#include <random>
#include <nanogui/common.h>
#include <nanogui/glutil.h>
#include <cmath>

using namespace nanogui;
using namespace Eigen;
using namespace std;

namespace
{
void makeHistograms(MatrixX3f & linHist, MatrixX3f & rgbHist, const HDRImage & img, float gain)
{
    const int numBins = 256;
    linHist = rgbHist = MatrixX3f::Zero(numBins, 3);
    float d = 1.f / (img.width() * img.height());
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
        {
            Color4 clin = img(x,y) * gain;
            Color4 crgb = LinearToSRGB(img(x, y)) * gain;

            linHist(clamp(int(floor(clin[0] * numBins)), 0, numBins - 1), 0) += d;
            linHist(clamp(int(floor(clin[1] * numBins)), 0, numBins - 1), 1) += d;
            linHist(clamp(int(floor(clin[2] * numBins)), 0, numBins - 1), 2) += d;

            rgbHist(clamp(int(floor(crgb[0] * numBins)), 0, numBins - 1), 0) += d;
            rgbHist(clamp(int(floor(crgb[1] * numBins)), 0, numBins - 1), 1) += d;
            rgbHist(clamp(int(floor(crgb[2] * numBins)), 0, numBins - 1), 2) += d;
        }
}

}

GLImage::GLImage() :
    m_filename(), m_cachedHistogramExposure(NAN)
{
    // empty
}


GLImage::~GLImage()
{
    clear();
}


GLuint GLImage::glTextureId() const
{
    if (!m_texture)
        init();
    return m_texture;
}

void GLImage::init() const
{
    // Allocate texture memory for the image
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width(), height(),
                 0, GL_RGBA, GL_FLOAT, (const GLvoid *) m_image.data());

	glGenerateMipmap(GL_TEXTURE_2D);  //Generate num_mipmaps number of mipmaps here.

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const GLfloat borderColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

}


void GLImage::clear()
{
    if (m_texture)
        glDeleteTextures(1, &m_texture);
    m_texture = 0;
}

bool GLImage::undo()
{
    if (m_history.undo(m_image))
    {
        init();
        return true;
    }
    return false;
}

bool GLImage::redo()
{
    if (m_history.redo(m_image))
    {
        init();
        return true;
    }
    return false;
}

bool GLImage::load(const std::string & filename)
{
    m_history = CommandHistory();
    m_filename = filename;
    return m_image.load(filename);
}

bool GLImage::save(const std::string & filename,
          float gain, float gamma,
          bool sRGB, bool dither) const
{
    m_history.markSaved();
    return m_image.save(filename, gain, gamma, sRGB, dither);
}

const MatrixX3f & GLImage::linearHistogram(float exposure) const
{
    if (m_histogramDirty || exposure != m_cachedHistogramExposure)
    {
        makeHistograms(m_linearHistogram, m_sRGBHistogram, m_image, pow(2.0f, exposure));
        m_histogramDirty = false;
        m_cachedHistogramExposure = exposure;
    }
    return m_linearHistogram;
}

const MatrixX3f & GLImage::sRGBHistogram(float exposure) const
{
    if (m_histogramDirty || exposure != m_cachedHistogramExposure)
    {
        makeHistograms(m_linearHistogram, m_sRGBHistogram, m_image, pow(2.0f, exposure));
        m_histogramDirty = false;
        m_cachedHistogramExposure = exposure;
    }
    return m_sRGBHistogram;
}