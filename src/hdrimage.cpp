/*!
    \file hdrimage.cpp
    \brief Contains the implementation of a floating-point RGBA image class
    \author Wojciech Jarosz
*/
#include "hdrimage.h"
#include "dither-matrix256.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <ImfArray.h>
#include <ImfRgbaFile.h>
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfStringAttribute.h>
#include <half.h>
#include "common.h"

#define STB_IMAGE_IMPLEMENTATION

// since NanoVG includes an old version of stb_image, we declare it static here
#define STB_IMAGE_STATIC

// these pragmas ignore warnings about unused static functions
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning (push, 0)
#endif

#include "stb_image.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning (pop)
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#include "pfm.h"
#include "ppm.h"

using namespace std;
using namespace Eigen;

// local functions
namespace
{

const Color4 g_blackPixel(0,0,0,0);

// create a vector containing the normalized values of a 1D Gaussian filter
ArrayXXf horizontalGaussianKernel(float sigma, float truncate)
{
    // calculate the size of the filter
    int offset = int(ceil(truncate * sigma));
    int filterSize = 2*offset+1;

    ArrayXXf fData(filterSize, 1);

    // compute the un-normalized value of the Gaussian
    float normalizer = 0.0f;
    for (int i = 0; i < filterSize; i++)
    {
        fData(i,0) = std::exp(-pow(i - offset, 2) / (2.0f * pow(sigma, 2)));
        normalizer += fData(i,0);
    }

    // normalize
    for (int i = 0; i < filterSize; i++)
        fData(i,0) /= normalizer;

    return fData;
}

} // namespace


const Color4 & HDRImage::pixel(int x, int y, BorderMode mode) const
{
    if (x >= 0 && x < width() && y >= 0 && y < height())
        return (*this)(x, y);

    switch (mode)
    {
        case EDGE:
            return (*this)(clamp(x, 0, width()-1), clamp(y, 0, height()-1));
        case REPEAT:
            return (*this)(mod(x, width()), mod(y, height()));
        case MIRROR:
            return (*this)(::abs(mod(x+width(), 2*width())-width()),
                           ::abs(mod(y+height(), 2*height())-height()));
        case BLACK:
            return g_blackPixel;
    }
}

Color4 & HDRImage::pixel(int x, int y, BorderMode mode)
{
    if (x >= 0 && x < width() && y >= 0 && y < height())
        return (*this)(x, y);

    switch (mode)
    {
        case EDGE:
            return (*this)(clamp(x, 0, width()-1), clamp(y, 0, height()-1));
        case REPEAT:
            return (*this)(mod(x, width()), mod(y, height()));
        case MIRROR:
            return (*this)(::abs(mod(x+width(), 2*width())-width()),
                           ::abs(mod(y+height(), 2*height())-height()));
        case BLACK:
            throw out_of_range("Cannot assign to out-of-bounds pixel when BorderMode==BLACK.");
    }
}

Color4 HDRImage::nearest(float sx, float sy, BorderMode mode) const
{
    return pixel(floor(sx), floor(sy), mode);
}


Color4 HDRImage::bilinear(float sx, float sy, BorderMode mode) const
{
    int x0 = (int) floor(sx);
    int y0 = (int) floor(sy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    sx -= x0;
    sy -= y0;

    return lerp(lerp(pixel(x0, y0, mode), pixel(x1, y0, mode), sx),
                lerp(pixel(x0, y1, mode), pixel(x1, y1, mode), sx), sy);
}


// photoshop bicubic
Color4 HDRImage::bicubic(float sx, float sy, BorderMode mode) const
{
    int bx = (int) floor(sx);
    int by = (int) floor(sy);

    float A = -0.75f;
    float totalweight = 0;
    Color4 val(0, 0, 0, 0);

    for (int y = by - 1; y < by + 3; y++)
    {
        float disty = fabs(sy - y);
        float yweight = (disty <= 1) ?
            ((A + 2.0f) * disty - (A + 3.0f)) * disty * disty + 1.0f :
            ((A * disty - 5.0f * A) * disty + 8.0f * A) * disty - 4.0f * A;

        for (int x = bx - 1; x < bx + 3; x++)
        {
            float distx = fabs(sx - x);
            float weight = (distx <= 1) ?
                (((A + 2.0f) * distx - (A + 3.0f)) * distx * distx + 1.0f) * yweight :
                (((A * distx - 5.0f * A) * distx + 8.0f * A) * distx - 4.0f * A) * yweight;

            val += pixel(x, y, mode) * weight;
            totalweight += weight;
        }
    }
    val *= 1.0f / totalweight;
    return val;
}


HDRImage HDRImage::resampled(int w, int h,
                             function<Color4(const HDRImage &, float, float, BorderMode)> samplerFn,
                             function<Vector2f(const Vector2f &)> warpFn,
                             int superSample, BorderMode mode) const
{
    HDRImage result(w, h);

    for (int y = 0; y < result.height(); ++y)
        for (int x = 0; x < result.width(); ++x)
        {
            Color4 sum(0,0,0,0);
            for (int yy = 0; yy < superSample; ++yy)
            {
                float j = (yy+0.5f)/superSample;
                for (int xx = 0; xx < superSample; ++xx)
                {
                    float i = (xx+0.5f)/superSample;
                    Vector2f srcUV = warpFn(Vector2f((x+i)/w, (y+j)/h)).array() * Array2f(width(), height());
                    sum += samplerFn(*this, srcUV(0), srcUV(1), mode);
                }
            }
            result(x,y) = sum/(superSample*superSample);
        }
    return result;
}


HDRImage HDRImage::convolved(const ArrayXXf &kernel, BorderMode mode) const
{
    HDRImage imFilter(width(), height());

    int centerX = int((kernel.rows()-1.0)/2.0);
    int centerY = int((kernel.cols()-1.0)/2.0);

    // for every pixel in the image
    for (int x = 0; x < width(); x++)
    {
        for (int y = 0; y < height(); y++)
        {
            Color4 accum(0.0f, 0.0f, 0.0f, 0.0f);
            float weightSum = 0.0f;
            // for every pixel in the kernel
            for (int xFilter = 0; xFilter < kernel.rows(); xFilter++)
            {
                int xx = x-xFilter+centerX;

                for (int yFilter = 0; yFilter < kernel.cols(); yFilter++)
                {
                    int yy = y-yFilter+centerY;
                    accum += kernel(xFilter, yFilter) * pixel(xx, yy, mode);
                    weightSum += kernel(xFilter, yFilter);
                }
            }

            // assign the pixel the value from convolution
            imFilter(x,y) = accum / weightSum;
        }
    }

    return imFilter;
}

HDRImage HDRImage::GaussianBlurredX(float sigmaX, BorderMode mode, float truncateX) const
{
    return convolved(horizontalGaussianKernel(sigmaX, truncateX), mode);
}

HDRImage HDRImage::GaussianBlurredY(float sigmaY, BorderMode mode, float truncateY) const
{
    return convolved(horizontalGaussianKernel(sigmaY, truncateY).transpose(), mode);
}

// Use principles of separability to blur an image using 2 1D Gaussian Filters
HDRImage HDRImage::GaussianBlurred(float sigmaX, float sigmaY, BorderMode mode,
                                   float truncateX, float truncateY) const
{
    // blur using 2, 1D filters in the x and y directions
    return GaussianBlurredX(sigmaX, mode, truncateX).GaussianBlurredY(sigmaY, mode, truncateY);
}


// sharpen an image
HDRImage HDRImage::unsharpMasked(float sigma, float strength, BorderMode mode) const
{
    return *this + Color4(strength) * (*this - fastGaussianBlurred(sigma, sigma, mode));
}



HDRImage HDRImage::medianFiltered(float radius, int channel, BorderMode mode) const
{
    int radiusi = int(ceil(radius));

    vector<float> mBuffer;
    mBuffer.reserve((2*radiusi)*(2*radiusi));

    int xCoord, yCoord;
    HDRImage tempBuffer = *this;

    for (int y = 0; y < height(); y++)
    {
        for (int x = 0; x < width(); x++)
        {
            mBuffer.clear();
            // over all pixels in the neighborhood kernel
            for (int i = -radiusi; i <= radiusi; i++)
            {
                xCoord = x + i;
                for (int j = -radiusi; j <= radiusi; j++)
                {
                    if (i*i + j*j > radius*radius)
                        continue;

                    yCoord = y + j;
                    mBuffer.push_back(pixel(xCoord, yCoord, mode)[channel]);
                }
            }

            int num = mBuffer.size();
            int med = (num-1)/2;

            nth_element(mBuffer.begin() + 0,
                        mBuffer.begin() + med,
                        mBuffer.begin() + mBuffer.size());
            tempBuffer(x,y)[channel] = mBuffer[med];
        }
    }

    return tempBuffer;
}


HDRImage HDRImage::bilateralFiltered(float sigmaRange, float sigmaDomain, BorderMode mode, float truncateDomain) const
{
    HDRImage imFilter(width(), height());

    // calculate the filter size
    int radius = int(ceil(truncateDomain * sigmaDomain));

    // for every pixel in the image
    for (int x = 0; x < imFilter.width(); x++)
    {
        for (int y = 0; y < imFilter.height(); y++)
        {
            // initilize normalizer and sum value to 0 for every pixel location
            float weightSum = 0.0f;
            Color4 accum(0.0f, 0.0f, 0.0f, 0.0f);

            for (int xFilter = -radius; xFilter <= radius; xFilter++)
            {
                int xx = x+xFilter;
                for (int yFilter = -radius; yFilter <= radius; yFilter++)
                {
                    int yy = y+yFilter;

                    // calculate the squared distance between the 2 pixels (in range)
                    float rangeExp = ::pow(pixel(xx,yy,mode) - (*this)(x,y), 2).sum();
                    float domainExp = std::pow(xFilter,2) + std::pow(yFilter,2);

                    // calculate the exponentiated weighting factor from the domain and range
                    float factorDomain = std::exp(-domainExp / (2.0 * std::pow(sigmaDomain,2)));
                    float factorRange = std::exp(-rangeExp / (2.0 * std::pow(sigmaRange,2)));
                    weightSum += factorDomain * factorRange;
                    accum += factorDomain * factorRange * pixel(xx,yy,mode);
                }
            }

            // set pixel in filtered image to weighted sum of values in the filter region
            imFilter(x,y) = accum/weightSum;
        }
    }

    return imFilter;
}


static int nextOddInt(int i)
{
  return (i % 2 == 0) ? i+1 : i;
}


HDRImage HDRImage::iteratedBoxBlurred(float sigma, int iterations, BorderMode mode) const
{
    // Compute box blur size for desired sigma and number of iterations:
    // The kernel resulting from repeated box blurs of the same width is the
    // Irwin–Hall distribution
    // (https://en.wikipedia.org/wiki/Irwin–Hall_distribution)
    //
    // The variance of the Irwin-Hall distribution with n unit-sized boxes:
    //
    //      V(1, n) = n/12.
    //
    // Since V[w * X] = w^2 V[X] where w is a constant, we know that the
    // variance will scale as follows using width-w boxes:
    //
    //      V(w, n) = w^2*n/12.
    //
    // To achieve a certain standard deviation sigma, we want to find solve:
    //
    //      sqrt(V(w, n)) = w*sqrt(n/12) = sigma
    //
    // for w, given n and sigma; which is:
    //
    //      w = sqrt(12/n)*sigma
    //

    int w = nextOddInt(round(std::sqrt(12.f/iterations) * sigma));

    // Now, if width is odd, then we can use a centered box and are good to go.
    // If width is even, then we can't use centered boxes, but must instead
    // use a symmetric pairs of off-centered boxes. For now, just always round
    // up to next odd width
    int hw = (w-1)/2;

    HDRImage imFilter = *this;
    for (int i = 0; i < iterations; i++)
        imFilter = imFilter.boxBlurred(hw, mode);

    return imFilter;
}

HDRImage HDRImage::fastGaussianBlurred(float sigmaX, float sigmaY, BorderMode mode) const
{
    // See comments in HDRImage::iteratedBoxBlurred for derivation of width
    int hw = round((std::sqrt(12.f/6) * sigmaX - 1)/2.f);
    int hh = round((std::sqrt(12.f/6) * sigmaY - 1)/2.f);

    HDRImage im;
    // do horizontal blurs
    if (hw < 3)
        // for small blurs, just use a separable Gaussian
        im = GaussianBlurredX(sigmaX, mode);
    else
        // for large blurs, approximate Gaussian with 6 box blurs
        im = boxBlurredX(hw, mode).boxBlurredX(hw, mode).boxBlurredX(hw, mode).
            boxBlurredX(hw, mode).boxBlurredX(hw, mode).boxBlurredX(hw, mode);

    // now do vertical blurs
    if (hh < 3)
        // for small blurs, just use a separable Gaussian
        im = im.GaussianBlurredY(sigmaY, mode);
    else
        // for large blurs, approximate Gaussian with 6 box blurs
        im = im.boxBlurredY(hh, mode).boxBlurredY(hh, mode).boxBlurredY(hh, mode).
            boxBlurredY(hh, mode).boxBlurredY(hh, mode).boxBlurredY(hh, mode);

    return im;
}


HDRImage HDRImage::boxBlurredX(int leftSize, int rightSize, BorderMode mode) const
{
    HDRImage imFilter(width(), height());

    for (int y = 0; y < height(); ++y)
    {
        // fill up the accumulator
        imFilter(0, y) = 0;
        for (int dx = -leftSize; dx <= rightSize; ++dx)
            imFilter(0, y) += pixel(dx, y, mode);

        for (int x = 1; x < width(); ++x)
            imFilter(x, y) = imFilter(x-1, y) -
                             pixel(x-1-leftSize, y, mode) +
                             pixel(x+rightSize, y, mode);
    }

    return imFilter * Color4(1.f/(leftSize + rightSize + 1));
}

// TODO actually use the mode parameter
HDRImage HDRImage::boxBlurredY(int leftSize, int rightSize, BorderMode mode) const
{
    HDRImage imFilter(width(), height());

    for (int x = 0; x < width(); ++x)
    {
        // fill up the accumulator
        imFilter(x, 0) = 0;
        for (int dy = -leftSize; dy <= rightSize; ++dy)
            imFilter(x, 0) += pixel(x, dy, mode);

        for (int y = 1; y < height(); ++y)
            imFilter(x, y) = imFilter(x, y-1) -
                             pixel(x, y-1-leftSize, mode) +
                             pixel(x, y+rightSize, mode);
    }

    return imFilter * Color4(1.f/(leftSize + rightSize + 1));
}


HDRImage HDRImage::resized(int w, int h) const
{
    HDRImage newImage(w, h);

    if (!stbir_resize_float((const float *)data(), width(), height(), 0,
                            (float *) newImage.data(), w, h, 0, 4))
        throw runtime_error("Failed to resize image.");

    return newImage;
}


bool HDRImage::load(const string & filename)
{
    string errors;

    // try PNG, JPG, HDR, etc files first
    int n, w, h;
    // stbi doesn't do proper srgb, but uses gamma=2.2 instead, so override it.
    // we'll do our own srgb correction
    stbi_ldr_to_hdr_scale(1.0f);
    stbi_ldr_to_hdr_gamma(1.0f);
    bool convert2Linear = !stbi_is_hdr(filename.c_str());

    float * float_data = stbi_loadf(filename.c_str(), &w, &h, &n, 4);
    if (float_data)
    {
        resize(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                Color4 c(float_data[4*(x + y*w) + 0],
                         float_data[4*(x + y*w) + 1],
                         float_data[4*(x + y*w) + 2],
                         float_data[4*(x + y*w) + 3]);
                (*this)(x,y) = convert2Linear ? toLinear(c) : c;
            }
        return true;
    }
    else
    {
        errors += string("\t") + stbi_failure_reason() + "\n";
    }

    // then try pfm/ppm
    try
    {
		w = 0;
		h = 0;
        if (is_pfm(filename.c_str()))
            float_data = load_pfm(filename.c_str(), &w, &h, &n);
        else if (is_ppm(filename.c_str()))
            float_data = load_ppm(filename.c_str(), &w, &h, &n);

        if (float_data)
        {
            if (n == 3)
            {
                resize(w, h);

                // convert 3-channel pfm data to 4-channel internal representation
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        (*this)(x,y) = Color4(float_data[3*(x + y*w) + 0],
                                              float_data[3*(x + y*w) + 1],
                                              float_data[3*(x + y*w) + 2],
                                              1.0f);

                delete [] float_data;
                return true;
            }
            else
                throw runtime_error("Unsupported number of channels in PFM/PPM");
            return true;
        }
    }
    catch (const exception &e)
    {
        delete [] float_data;
        resize(0,0);
        errors += string("\t") + e.what() + "\n";
    }

    // finally try exrs
    try
    {
        Imf::RgbaInputFile file(filename.c_str());
        Imath::Box2i dw = file.dataWindow();

        w = dw.max.x - dw.min.x + 1;
        h = dw.max.y - dw.min.y + 1;
        Imf::Array2D<Imf::Rgba> pixels(1, w);

        int y = dw.min.y;
        int row = 0;
        resize(w,h);

        while (y <= dw.max.y)
        {
            file.setFrameBuffer(&pixels[0][0] - dw.min.x - dw.min.y * w, 1, 0);
            file.readPixels(y, y);

            // copy pixels over to the Image
            for (int i = 0; i < w; ++i)
            {
                const Imf::Rgba &p = pixels[0][i];
                (*this)(i, row) = Color4(p.r, p.g, p.b, p.a);
            }

            y++;
            row++;
        }
        return true;
    }
    catch (const exception &e)
    {
        resize(0,0);
        errors += string("\t") + e.what() + "\n";
    }

    cerr << "ERROR: Unable to read image file \"" << filename << "\": \n" << errors << endl;

    return false;
}

bool HDRImage::save(const string & filename,
                      float gain, float gamma,
                      bool sRGB, bool dither) const
{
    string extension = getExtension(filename);

    transform(extension.begin(),
              extension.end(),
              extension.begin(),
              ::tolower);

    auto img = this;
    HDRImage imgCopy;

    // if we need to tonemap, then modify a copy of the image data
    if (gain != 1.0f || sRGB || gamma != 1.0f)
    {
        imgCopy = *this;
        img = &imgCopy;
        Color4 gainC = Color4(gain, gain, gain, 1.0f);
        Color4 gainG = Color4(1.0f/gamma, 1.0f/gamma, 1.0f/gamma, 1.0f);

        if (gain != 1.0f)
            imgCopy *= gainC;

        if (sRGB)
            imgCopy = imgCopy.unaryExpr(ptr_fun((Color4 (*)(const Color4&))toSRGB));
        else if (gamma != 1.0f)
            imgCopy = imgCopy.pow(gainG);
    }

    if (extension == "hdr")
        return stbi_write_hdr(filename.c_str(), width(), height(), 4, (const float *) img->data()) != 0;
    else if (extension == "pfm")
        return write_pfm(filename.c_str(), width(), height(), 4, (const float *) img->data()) != 0;
    else if (extension == "exr")
    {
        try
        {
            Imf::RgbaOutputFile file(filename.c_str(), width(), height(), Imf::WRITE_RGBA);
            Imf::Array2D<Imf::Rgba> pixels(1, width());

            for (int y = 0; y < height(); ++y)
            {
                // copy pixels over to the Image
                for (int x = 0; x < width(); ++x)
                {
                    Imf::Rgba &p = pixels[0][x];
                    Color4 c = (*img)(x,y);
                    p.r = c[0];
                    p.g = c[1];
                    p.b = c[2];
                    p.a = c[3];
                }

                file.setFrameBuffer(&pixels[0][0], 1, 0);
                file.writePixels(1);
            }
			return true;
        }
        catch (const exception &e)
        {
            cerr << "ERROR: Unable to write image file \"" << filename << "\": " << e.what() << endl;
            return false;
        }
    }
    else
    {
        // convert floating-point image to 8-bit per channel with dithering
        vector<unsigned char> data(size()*3, 0);
        for (int y = 0; y < height(); ++y)
            for (int x = 0; x < width(); ++x)
            {
                Color4 c = (*img)(x,y);
                if (dither)
                {
                    int xmod = x % 256;
                    int ymod = y % 256;
                    float ditherValue = (dither_matrix256[xmod + ymod * 256]/65536.0f - 0.5f)/255.0f;
                    c += Color4(Color3(ditherValue), 0.0f);
                }

                // convert to [0-255] range
                c = (c * 255.0f).max(0.0f).min(255.0f);

                data[3*x + 3*y*width() + 0] = (unsigned char) c[0];
                data[3*x + 3*y*width() + 1] = (unsigned char) c[1];
                data[3*x + 3*y*width() + 2] = (unsigned char) c[2];
            }

        if (extension == "ppm")
            return write_ppm(filename.c_str(), width(), height(), 3, &data[0]);
        else if (extension == "png")
            return stbi_write_png(filename.c_str(), width(), height(),
                                  3, &data[0], sizeof(unsigned char)*width()*3) != 0;
        else if (extension == "bmp")
            return stbi_write_bmp(filename.c_str(), width(), height(), 3, &data[0]) != 0;
        else if (extension == "tga")
            return stbi_write_tga(filename.c_str(), width(), height(), 3, &data[0]) != 0;
        else
            throw runtime_error("Could not determine desired file type from extension.");
    }
}
