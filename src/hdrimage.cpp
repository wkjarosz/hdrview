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


#define CALL_MEMBER_FN(object,ptrToMember)  ((object).*(ptrToMember))
HDRImage HDRImage::resample(int w, int h,
                                UV2XYZFn dst2xyz, XYZ2UVFn xyz2src,
                                PixelSamplerFn sampler,
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
                    Vector2f dstUV = Vector2f((x+i)/w, (y+j)/h);
                    Vector2f srcUV = xyz2src(dst2xyz(dstUV));
                    sum += CALL_MEMBER_FN(*this, sampler)(srcUV(0)*width(), srcUV(1)*height(), mode);
                }
            }
            result(x,y) = sum/(superSample*superSample);
        }
    return result;
}


HDRImage HDRImage::convolve(const ArrayXXf & kernel, BorderMode mode) const
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

HDRImage HDRImage::gaussianBlurX(float sigmaX, BorderMode mode, float truncateX) const
{
    return convolve(horizontalGaussianKernel(sigmaX, truncateX), mode);
}

HDRImage HDRImage::gaussianBlurY(float sigmaY, BorderMode mode, float truncateY) const
{
    return convolve(horizontalGaussianKernel(sigmaY, truncateY).transpose(), mode);
}

// Use principles of seperabiltity to blur an image using 2 1D Gaussian Filters
HDRImage HDRImage::gaussianBlur(float sigmaX, float sigmaY, BorderMode mode,
                                    float truncateX, float truncateY) const
{
    // blur using 2, 1D filters in the x and y directions
    return gaussianBlurX(sigmaX, mode, truncateX).gaussianBlurY(sigmaY, mode, truncateY);
}


// sharpen an image
HDRImage HDRImage::unsharpMask(float sigma, float strength, BorderMode mode) const
{
    return *this + Color4(strength) * (*this - fastGaussianBlur(sigma, sigma, mode));
}



HDRImage HDRImage::median(float radius, int channel, BorderMode mode) const
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


HDRImage HDRImage::bilateral(float sigmaRange, float sigmaDomain, BorderMode mode, float truncateDomain)
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
                // ignore pixels outside the image
                if (xx < 0 || xx >= width()) continue;

                for (int yFilter = -radius; yFilter <= radius; yFilter++)
                {
                    int yy = y+yFilter;
                    // ignore pixels outside the image
                    if (yy < 0 || yy >= height()) continue;

                    // calculate the squared distance between the 2 pixels (in range)
                    float rangeExp = ::pow(operator()(xx,yy) - operator()(x,y), 2).sum();
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


HDRImage HDRImage::iteratedBoxBlur(float sigma, int iterations, BorderMode mode) const
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
    //      sqrt(V(w, n)) = sigma
    //
    // for w, given n and sigma.
    //
    // This gives us:
    //
    //      V(w, n) = sigma^2
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
        imFilter = imFilter.boxBlur(hw, mode);

    return imFilter;
}

HDRImage HDRImage::fastGaussianBlur(float sigmaX, float sigmaY, BorderMode mode) const
{
    // See comments in HDRImage::iteratedBoxBlur for derivation of width
    int hw = round((std::sqrt(12.f/6) * sigmaX - 1)/2.f);
    int hh = round((std::sqrt(12.f/6) * sigmaY - 1)/2.f);

    HDRImage im;
    // do horizontal blurs
    if (hw < 3)
        // use a separable Gaussian
        im = gaussianBlurX(sigmaX, mode);
    else
        // approximate Gaussian with 6 box blurs
        im = boxBlurX(hw, mode).boxBlurX(hw, mode).boxBlurX(hw, mode).
             boxBlurX(hw, mode).boxBlurX(hw, mode).boxBlurX(hw, mode);

    // now do vertical blurs
    if (hh < 3)
        // use a separable Gaussian
        im = im.gaussianBlurY(sigmaY, mode);
    else
        // approximate Gaussian with 6 box blurs
        im = im.boxBlurY(hh, mode).boxBlurY(hh, mode).boxBlurY(hh, mode).
                boxBlurY(hh, mode).boxBlurY(hh, mode).boxBlurY(hh, mode);

    return im;
}

// TODO actually use the mode parameter
HDRImage HDRImage::boxBlurX(int leftSize, int rightSize, BorderMode mode) const
{
    HDRImage imFilter(width(), height());

    // cannot blur by more than the whole image width
    // code below would access outside the bounds of the array without this
    leftSize = std::min(width(), leftSize);
    rightSize = std::min(width(), rightSize);

    Color4 scale(1.f);
    scale /= leftSize + rightSize + 1;

    // allocate enough storage for a single scanline
    Base out = Base(width(), 1);
    for (int y = 0; y < height(); ++y)
    {
        // take the y-th scanline
        const auto in = col(y);

        int right = 0;
        int left = 0;

        // in the beginning, left side of kernel is outside the image boundary.
        // assume the first pixel is replicated to the left.
        out(0) = leftSize * in(left);
        for (int x = 0; x <= rightSize; ++x)
            out(0) += in(right++);

        // continue to fill until left side of kernel is within image boundary.
        for (int x = 1; x <= leftSize; ++x)
            out(x) = out(x-1) + in(right++) - in(left);

        // now entire kernel is contained in the image bounds.
        // start sliding both ends of the kernel
        for (int x = leftSize + 1; x < width() - rightSize - 1; ++x)
            out(x) = out(x-1) + in(right++) - in(left++);

        // finish up as we approach the right side of the scanline.
        // replicate the right-most pixel as kernel extends past the boundary.
        for (int x = width() - rightSize - 1; x < width(); ++x)
            out(x) = out(x-1) + in(right) - in(left++);

        imFilter.block(0, y, width(), 1) = out * scale;
    }

    return imFilter;
}

// TODO actually use the mode parameter
HDRImage HDRImage::boxBlurY(int leftSize, int rightSize, BorderMode mode) const
{
    HDRImage imFilter(width(), height());

    // cannot blur by more than the whole image width
    // code below would access outside the bounds of the array without this
    leftSize = std::min(height(), leftSize);
    rightSize = std::min(height(), rightSize);

    Color4 scale(1.f);
    scale /= leftSize + rightSize + 1;

    // allocate enough storage for a single vertical scanline
    Base out = Base(height(), 1);
    for (int x = 0; x < width(); ++x)
    {
        // take the x-th vertical scanline
        const auto in = row(x);

        int bottom = 0;
        int top = 0;

        out(0) = leftSize * in(top);
        for (int y = 0; y <= rightSize; ++y)
            out(0) += in(bottom++);

        for (int y = 1; y <= leftSize; ++y)
            out(y) = out(y-1) + in(bottom++) - in(top);

        for (int y = leftSize + 1; y < height() - rightSize - 1; ++y)
            out(y) = out(y-1) + in(bottom++) - in(top++);

        for (int y = height() - rightSize - 1; y < height(); ++y)
            out(y) = out(y-1) + in(bottom) - in(top++);

        imFilter.block(x, 0, 1, height()) = out.transpose() * scale;
    }

    return imFilter;
}

HDRImage HDRImage::halfSize() const
{
    HDRImage result(width() / 2, height() / 2);

    for (int y = 0; y < result.height(); ++y)
    {
        int cy = 2 * y;
        for (int x = 0; x < result.width(); ++x)
        {
            int cx = 2 * x;
            result(x,y) = 0.25f * ((*this)(cx  , cy) + (*this)(cx  , cy+1) +
                                   (*this)(cx+1, cy) + (*this)(cx+1, cy+1));
        }
    }

    return result;
}


HDRImage HDRImage::doubleSize() const
{
    HDRImage result(width() * 2, height() * 2);

    for (int y = 0; y < result.height(); ++y)
    {
        int cy = y / 2;
        for (int x = 0; x < result.width(); ++x)
        {
            int cx = x / 2;
            result(x, y) = (*this)(cx, cy);
        }
    }

    return result;
}


HDRImage HDRImage::smoothScale(int w, int h) const
{
    float wInv = 1.0f / w;
    float hInv = 1.0f / h;
    int wOld = width();
    int hOld = height();

    Color4 sum;

    // resize horizontally
    HDRImage xBuffer(w, hOld);
    {
        const HDRImage & src = *this;
        for (int y = 0; y < hOld; ++y)
        {
            float fx1 = 0.0f;
            int ix1 = 0;
            float fracX1 = 1.0f;

            for (int x = 0; x < w; ++x)
            {
                float fx2 = (x + 1.0f) * wInv * wOld;
                int ix2 = int(fx2);
                float fracX2 = fx2 - ix2;

                sum = src(ix1, y) * fracX1;
                if (ix2 < wOld)
                    sum += src(ix2, y) * fracX2;

                int i;
                for (i = ix1 + 1; i < ix2; i++)
                    sum += src(i, y);

                xBuffer(x, y) = sum / (fracX1 + fracX2 + i - ix1 - 1.0f);

                fx1 = fx2;
                ix1 = ix2;
                fracX1 = 1.0f - fracX2;
            }
        }
    }

    // resize vertically
    HDRImage yBuffer(w, h);
    {
        const HDRImage & src = xBuffer;
        for (int x = 0; x < w; ++x)
        {
            float fy1 = 0.0f;
            int iy1 = 0;
            float fracY1 = 1.0f;

            for (int y = 0; y < h; ++y)
            {
                float fy2 = (y + 1.0f) * hInv * hOld;
                int iy2 = int(fy2);
                float fracY2 = fy2 - iy2;

                sum = src(x, iy1) * fracY1;
                if (iy2 < hOld)
                    sum += src(x, iy2) * fracY2;

                int i;
                for (i = iy1+1; i < iy2; i++)
                    sum += src(x, i);

                yBuffer(x, y) = sum / (fracY1 + fracY2 + i - iy1 - 1);

                fy1 = fy2;
                iy1 = iy2;
                fracY1 = 1.0f - fracY2;
            }
        }
    }

    return yBuffer;
}


bool HDRImage::load(const string & filename)
{
    string errors;

    // try PNG, JPG, HDR, etc files first
    int n, w, h;
    float * float_data = stbi_loadf(filename.c_str(), &w, &h, &n, 4);
    if (float_data)
    {
        resize(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                operator()(x,y) = Color4(float_data[4*(x + y*w) + 0],
                                         float_data[4*(x + y*w) + 1],
                                         float_data[4*(x + y*w) + 2],
                                         float_data[4*(x + y*w) + 3]);
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
                        operator()(x,y) = Color4(float_data[3*(x + y*w) + 0],
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
                operator()(i, row) = Color4(p.r, p.g, p.b, p.a);
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
                      bool sRGB, bool dither)
{
    string extension = getExtension(filename);

    transform(extension.begin(),
              extension.end(),
              extension.begin(),
              ::tolower);

    HDRImage* img = this;
    HDRImage imgCopy;

    // if we need to tonemap, then modify a copy of the image data
    if (gain != 1.0f || sRGB || gamma != 1.0f)
    {
        imgCopy = *this;
        img = &imgCopy;

        if (gain != 1.0f)
            imgCopy *= Color4(gain);

        if (sRGB)
            imgCopy = imgCopy.unaryExpr(ptr_fun((Color4 (*)(const Color4&))toSRGB));
        else if (gamma != 1.0f)
            imgCopy = imgCopy.pow(Color4(1.0f/gamma));
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
