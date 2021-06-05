//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrimage.h"
#include <ctype.h>               // for tolower
#include <stdlib.h>              // for abs
#include <algorithm>             // for nth_element, transform
#include <cmath>                 // for floor, pow, exp, ceil, round, sqrt
#include <exception>             // for exception
#include <functional>            // for pointer_to_unary_function, function
#include <stdexcept>             // for runtime_error, out_of_range
#include <string>                // for allocator, operator==, basic_string
#include <vector>                // for vector
#include "common.h"              // for lerp, mod, clamp, getExtension
#include "colorspace.h"
#include "timer.h"
#include <spdlog/spdlog.h>


#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"    // for stbir_resize_float


using namespace std;
using Imath::V2f;
using Imath::V3f;
using Imath::M33f;

// local functions
namespace
{

const Color4 g_blackPixel(0,0,0,0);

// create a vector containing the normalized values of a 1D Gaussian filter
Array2Df horizontal_gaussian_kernel(float sigma, float truncate);
int wrap_coord(int p, int maxP, HDRImage::BorderMode m);

inline void clip_roi(Box2i & roi, const HDRImage & img)
{
    if (roi.has_volume())
        roi.intersect(img.box());
    else
        roi = img.box();
}

} // namespace



// makes a copy of the image, applies func for each pixel in roi of the copy, and returns the result
HDRImage HDRImage::apply_function(function<Color4(const Color4 &)> func, Box2i roi) const
{
    clip_roi(roi, *this);

    HDRImage result = (*this);

    // for (int y = roi.min.y(); y < roi.max.y(); ++y)
    parallel_for(roi.min.y(), roi.max.y(), [this,&func,&roi,&result](int y)
    {
        for (int x = roi.min.x(); x < roi.max.x(); ++x)
            result(x,y) = func((*this)(x,y));
    });

    return result;
}


// applies func for each pixel in roi, and stores result back in this
HDRImage & HDRImage::apply_function(function<Color4(const Color4 &)> func, Box2i roi)
{
    clip_roi(roi, *this);

    // for (int y = roi.min.y(); y < roi.max.y(); ++y)
    parallel_for(roi.min.y(), roi.max.y(), [this,&func,&roi](int y)
    {
        for (int x = roi.min.x(); x < roi.max.x(); ++x)
            (*this)(x,y) = func((*this)(x,y));
    });

    return *this;
}


// for images this and other, computes result = func(this, other) for each pixel in roi, and returns the result
// useful for something like c = a+b
HDRImage HDRImage::apply_function(const HDRImage & other, function<Color4(const Color4 &, const Color4 &)> func, Box2i roi) const
{
    clip_roi(roi, *this);

    HDRImage result = (*this);

    // for (int y = roi.min.y(); y < roi.max.y(); ++y)
    parallel_for(roi.min.y(), roi.max.y(), [this,&func,&roi,&result,&other](int y)
    {
        for (int x = roi.min.x(); x < roi.max.x(); ++x)
            result(x,y) = func((*this)(x,y), other(x,y));
    });

    return result;
}

// for images this and other, computes this = func(this, other) for each pixel in roi, and stores result back in this
// useful for something like a += b
HDRImage & HDRImage::apply_function(const HDRImage & other, function<Color4(const Color4 &, const Color4 &)> func, Box2i roi)
{
    if (roi.has_volume())
        roi.intersect(box());
    else
    {
        roi = box();
        roi.intersect(other.box());
    }

    // for (int y = roi.min.y(); y < roi.max.y(); ++y)
    parallel_for(roi.min.y(), roi.max.y(), [this,&func,&roi,&other](int y)
    {
        for (int x = roi.min.x(); x < roi.max.x(); ++x)
            (*this)(x,y) = func((*this)(x,y), other(x,y));
    });

    return *this;
}


Color4 HDRImage::reduce(function<Color4(const Color4 &, const Color4 &)> func, Box2i roi) const
{
    clip_roi(roi, *this);

    Color4 result = (*this)(roi.min.x(),roi.min.y());

    // for (int y = roi.min.y(); y < roi.max.y(); ++y)
    parallel_for(roi.min.y(), roi.max.y(), [this,&func,&roi,&result](int y)
    {
        for (int x = roi.min.x(); x < roi.max.x(); ++x)
            if (x != roi.min.x() && y != roi.min.y())
                result = func(result, (*this)(x,y));
    });

    return result;
}


void HDRImage::copy_subimage(const HDRImage & src, Box2i roi, int dst_x, int dst_y)
{
    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(src.box());
    else
        roi = src.box();

    if (!roi.has_volume())
        return;

    // clip roi to valid region in this image starting at dst_x and dst_y
    auto old_min = roi.min;
    roi.move_min_to({dst_x, dst_y});
    roi.intersect(box());
    roi.move_min_to(old_min);

    // for every pixel in the image
    parallel_for(roi.min.y(), roi.max.y(), [this,&src,&roi,dst_x,dst_y](int y)
    {
        for (int x = roi.min.x(); x < roi.max.x(); ++x)
            (*this)(dst_x + x-roi.min.x(), dst_y + y-roi.min.y()) = src(x, y);
    });
}

const vector<string> & HDRImage::border_mode_names()
{
	static const vector<string> names =
		{
			"Black",
			"Edge",
			"Repeat",
			"Mirror"
		};
	return names;
}

const vector<string> & HDRImage::sampler_names()
{
	static const vector<string> names =
		{
			"Nearest neighbor",
			"Bilinear",
			"Bicubic"
		};
	return names;
}

const Color4 & HDRImage::pixel(int x, int y, BorderMode mX, BorderMode mY) const
{
	x = wrap_coord(x, width(), mX);
	y = wrap_coord(y, height(), mY);
	if (x < 0 || y < 0)
		return g_blackPixel;

	return (*this)(x, y);
}

Color4 & HDRImage::pixel(int x, int y, BorderMode mX, BorderMode mY)
{
	x = wrap_coord(x, width(), mX);
	y = wrap_coord(y, height(), mY);
	if (x < 0 || y < 0)
		throw out_of_range("Cannot assign to out-of-bounds pixel when BorderMode==BLACK.");

	return (*this)(x, y);
}

Color4 HDRImage::sample(float sx, float sy, Sampler s, BorderMode mX, BorderMode mY) const
{
	switch (s)
	{
		case NEAREST:  return nearest(sx, sy, mX, mY);
		case BILINEAR: return bilinear(sx, sy, mX, mY);
		case BICUBIC:
        default:       return bicubic(sx, sy, mX, mY);
	}
}

Color4 HDRImage::nearest(float sx, float sy, BorderMode mX, BorderMode mY) const
{
    return pixel(std::floor(sx), std::floor(sy), mX, mY);
}

Color4 HDRImage::bilinear(float sx, float sy, BorderMode mX, BorderMode mY) const
{
    // shift so that pixels are defined at their centers
    sx -= 0.5f;
    sy -= 0.5f;

    int x0 = (int) std::floor(sx);
    int y0 = (int) std::floor(sy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    sx -= x0;
    sy -= y0;

    return lerp(lerp(pixel(x0, y0, mX, mY), pixel(x1, y0, mX, mY), sx),
                lerp(pixel(x0, y1, mX, mY), pixel(x1, y1, mX, mY), sx), sy);
}

// photoshop bicubic
Color4 HDRImage::bicubic(float sx, float sy, BorderMode mX, BorderMode mY) const
{
    // shift so that pixels are defined at their centers
    sx -= 0.5f;
    sy -= 0.5f;

    int bx = (int) std::floor(sx);
    int by = (int) std::floor(sy);

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

            val += pixel(x, y, mX, mY) * weight;
            totalweight += weight;
        }
    }
    val *= 1.0f / totalweight;
    return val;
}


HDRImage HDRImage::resampled(int w, int h,
                             AtomicProgress progress,
                             function<nanogui::Vector2f(const nanogui::Vector2f &)> warpFn,
                             int superSample, Sampler sampler, BorderMode mX, BorderMode mY) const
{
    HDRImage result(w, h);

    Timer timer;
    progress.set_num_steps(result.height());
    // for every pixel in the image
    parallel_for(0, result.height(), [this,w,h,&progress,&warpFn,&result,superSample,sampler,mX,mY](int y)
    {
        for (int x = 0; x < result.width(); ++x)
        {
            Color4 sum(0, 0, 0, 0);
            for (int yy = 0; yy < superSample; ++yy)
            {
                float j = (yy + 0.5f) / superSample;
                for (int xx = 0; xx < superSample; ++xx)
                {
                    float i = (xx + 0.5f) / superSample;
                    nanogui::Vector2f srcUV = warpFn(nanogui::Vector2f((x + i) / w, (y + j) / h)) * nanogui::Vector2f(width(), height());
                    sum += sample(srcUV[0], srcUV[1], sampler, mX, mY);
                }
            }
            result(x, y) = sum / (superSample * superSample);
        }
        ++progress;
    });
    spdlog::trace("Resampling took: {} seconds.", (timer.elapsed()/1000.f));
    return result;
}


HDRImage HDRImage::convolved(const Array2Df &kernel, AtomicProgress progress,
                             BorderMode mX, BorderMode mY, Box2i roi) const
{
    HDRImage result = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return result;

    int centerX = int((kernel.width()-1.0)/2.0);
    int centerY = int((kernel.height()-1.0)/2.0);

    Timer timer;
	progress.set_num_steps(roi.size().x());
    // for every pixel in the image
    parallel_for(roi.min.x(), roi.max.x(), [this,&roi,&progress,&kernel,mX,mY,&result,centerX,centerY](int x)
    {
        for (int y = roi.min.y(); y < roi.max.y(); y++)
        {
            Color4 accum(0.0f, 0.0f, 0.0f, 0.0f);
            float weightSum = 0.0f;
            // for every pixel in the kernel
            for (int xFilter = 0; xFilter < kernel.width(); xFilter++)
            {
                int xx = x-xFilter+centerX;

                for (int yFilter = 0; yFilter < kernel.height(); yFilter++)
                {
                    int yy = y-yFilter+centerY;
                    accum += kernel(xFilter, yFilter) * pixel(xx, yy, mX, mY);
                    weightSum += kernel(xFilter, yFilter);
                }
            }

            // assign the pixel the value from convolution
            result(x,y) = accum / weightSum;
        }
        ++progress;
    });
    spdlog::trace("Convolution took: {} seconds.", (timer.elapsed()/1000.f));

    return result;
}

HDRImage HDRImage::gaussian_blurred_x(float sigmaX, AtomicProgress progress, BorderMode mX, float truncateX, Box2i roi) const
{
    return convolved(horizontal_gaussian_kernel(sigmaX, truncateX), progress, mX, mX, roi);
}

HDRImage HDRImage::gaussian_blurred_y(float sigmaY, AtomicProgress progress, BorderMode mY, float truncateY, Box2i roi) const
{
    return convolved(horizontal_gaussian_kernel(sigmaY, truncateY).swapped_dims(), progress, mY, mY, roi);
}

// Use principles of separability to blur an image using 2 1D Gaussian Filters
HDRImage HDRImage::gaussian_blurred(float sigmaX, float sigmaY, AtomicProgress progress,
                                   BorderMode mX, BorderMode mY,
                                   float truncateX, float truncateY, Box2i roi) const
{
    // blur using 2, 1D filters in the x and y directions
    return gaussian_blurred_x(sigmaX, AtomicProgress(progress, .5f), mX, truncateX, roi)
          .gaussian_blurred_y(sigmaY, AtomicProgress(progress, .5f), mY, truncateY, roi);
}


// sharpen an image
HDRImage HDRImage::unsharp_masked(float sigma, float strength, AtomicProgress progress, BorderMode mX, BorderMode mY, Box2i roi) const
{
    Timer timer;
    spdlog::trace("Starting unsharp mask...");
    HDRImage result = fast_gaussian_blurred(sigma, sigma, progress, mX, mY, roi);
    result *= -1.f;
    result += *this;
    result *= strength;
    result += *this;
    // HDRImage result = *this + strength * (*this - fast_gaussian_blurred(sigma, sigma, progress, mX, mY, roi));
    spdlog::trace("Unsharp mask took: {} seconds.", (timer.elapsed()/1000.f));
    return result;
}



HDRImage HDRImage::median_filtered(float radius, int channel, AtomicProgress progress,
                                  BorderMode mX, BorderMode mY, bool round, Box2i roi) const
{
    int radiusi = int(std::ceil(radius));
    HDRImage tempBuffer = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return tempBuffer;

    Timer timer;
    progress.set_num_steps(roi.size().y());
    // for every pixel in the image
    parallel_for(roi.min.y(), roi.max.y(), [this,&roi,&tempBuffer,&progress,radius,radiusi,channel,mX,mY,round](int y)
    {
        vector<float> mBuffer;
        mBuffer.reserve((2*(radiusi+1))*(2*(radiusi+1)));
        for (int x = roi.min.x(); x < roi.max.x(); x++)
        {
            mBuffer.clear();

            int x_coord, y_coord;
            // over all pixels in the neighborhood kernel
            for (int i = -radiusi; i <= radiusi; i++)
            {
                x_coord = x + i;
                for (int j = -radiusi; j <= radiusi; j++)
                {
                    if (round && i*i + j*j > radius*radius)
                        continue;

                    y_coord = y + j;
                    mBuffer.push_back(pixel(x_coord, y_coord, mX, mY)[channel]);
                }
            }

            int num = mBuffer.size();
            int med = (num-1)/2;

            nth_element(mBuffer.begin() + 0,
                        mBuffer.begin() + med,
                        mBuffer.begin() + mBuffer.size());
            tempBuffer(x,y)[channel] = mBuffer[med];
        }
        ++progress;
    });
    spdlog::trace("Median filter took: {} seconds.", (timer.elapsed()/1000.f));

    return tempBuffer;
}


HDRImage HDRImage::bilateral_filtered(float sigmaRange, float sigma_domain,
                                     AtomicProgress progress,
                                     BorderMode mX, BorderMode mY, float truncateDomain, Box2i roi) const
{
    HDRImage filtered = *this;
    
    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return filtered;

    // calculate the filter size
    int radius = int(std::ceil(truncateDomain * sigma_domain));

    Timer timer;
    progress.set_num_steps(roi.size().y());
    // for every pixel in the image
    parallel_for(roi.min.y(), roi.max.y(), [this,&roi,&filtered,&progress,radius,sigmaRange,sigma_domain,mX,mY](int y)
    {
        for (int x = roi.min.x(); x < roi.max.x(); x++)
        {
            // initilize normalizer and sum value to 0 for every pixel location
            float weightSum = 0.0f;
            Color4 accum(0.0f, 0.0f, 0.0f, 0.0f);

            for (int yFilter = -radius; yFilter <= radius; yFilter++)
            {
                int yy = y+yFilter;
                for (int xFilter = -radius; xFilter <= radius; xFilter++)
                {
                    int xx = x+xFilter;
                    // calculate the squared distance between the 2 pixels (in range)
                    float rangeExp = ::pow(pixel(xx,yy,mX,mY) - (*this)(x,y), 2).sum();
                    float domainExp = std::pow(xFilter,2) + std::pow(yFilter,2);

                    // calculate the exponentiated weighting factor from the domain and range
                    float factorDomain = std::exp(-domainExp / (2.0 * std::pow(sigma_domain,2)));
                    float factorRange = std::exp(-rangeExp / (2.0 * std::pow(sigmaRange,2)));
                    weightSum += factorDomain * factorRange;
                    accum += factorDomain * factorRange * pixel(xx,yy,mX,mY);
                }
            }

            // set pixel in filtered image to weighted sum of values in the filter region
            filtered(x,y) = accum/weightSum;
        }
        ++progress;
    });
    spdlog::trace("Bilateral filter took: {} seconds.", (timer.elapsed()/1000.f));

    return filtered;
}


static int nextOddInt(int i)
{
  return (i % 2 == 0) ? i+1 : i;
}


HDRImage HDRImage::iterated_box_blurred(float sigma, int iterations, AtomicProgress progress, BorderMode mX, BorderMode mY, Box2i roi) const
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

    int w = nextOddInt(std::round(std::sqrt(12.f/iterations) * sigma));

    // Now, if width is odd, then we can use a centered box and are good to go.
    // If width is even, then we can't use centered boxes, but must instead
    // use a symmetric pairs of off-centered boxes. For now, just always round
    // up to next odd width
    int hw = (w-1)/2;

    HDRImage result = *this;
    for (int i = 0; i < iterations; i++)
        result = result.box_blurred(hw, AtomicProgress(progress, 1.f/iterations), mX, mY, roi);

    return result;
}

HDRImage HDRImage::fast_gaussian_blurred(float sigmaX, float sigmaY,
                                         AtomicProgress progress,
                                         BorderMode mX, BorderMode mY,
                                         Box2i roi) const
{
    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return *this;

    Timer timer;
    // See comments in HDRImage::iterated_box_blurred for derivation of width
    int hw = std::round((std::sqrt(12.f/6) * sigmaX - 1)/2.f);
    int hh = std::round((std::sqrt(12.f/6) * sigmaY - 1)/2.f);

    HDRImage im;
    // do horizontal blurs
    if (hw < 3)
        // for small blurs, just use a separable Gaussian
        im = gaussian_blurred_x(sigmaX, AtomicProgress(progress, 0.5f), mX, 6.f, roi);
    else
        // for large blurs, approximate Gaussian with 6 box blurs
        im = box_blurred_x(hw, AtomicProgress(progress, .5f/6.f), mX, roi.expanded(5*hw))
	        .box_blurred_x(hw, AtomicProgress(progress, .5f/6.f), mX, roi.expanded(4*hw))
	        .box_blurred_x(hw, AtomicProgress(progress, .5f/6.f), mX, roi.expanded(3*hw))
	        .box_blurred_x(hw, AtomicProgress(progress, .5f/6.f), mX, roi.expanded(2*hw))
	        .box_blurred_x(hw, AtomicProgress(progress, .5f/6.f), mX, roi.expanded(1*hw))
	        .box_blurred_x(hw, AtomicProgress(progress, .5f/6.f), mX, roi);

    // now do vertical blurs
    if (hh < 3)
        // for small blurs, just use a separable Gaussian
        im = im.gaussian_blurred_y(sigmaY, AtomicProgress(progress, 0.5f), mY, 6.f, roi);
    else
        // for large blurs, approximate Gaussian with 6 box blurs
        im = im.box_blurred_y(hh, AtomicProgress(progress, .5f/6.f), mY, roi.expanded(5*hh))
               .box_blurred_y(hh, AtomicProgress(progress, .5f/6.f), mY, roi.expanded(4*hh))
               .box_blurred_y(hh, AtomicProgress(progress, .5f/6.f), mY, roi.expanded(3*hh))
               .box_blurred_y(hh, AtomicProgress(progress, .5f/6.f), mY, roi.expanded(2*hh))
               .box_blurred_y(hh, AtomicProgress(progress, .5f/6.f), mY, roi.expanded(1*hh))
               .box_blurred_y(hh, AtomicProgress(progress, .5f/6.f), mY, roi);

    // copy just the roi
    HDRImage im2 = *this;
    im2.copy_subimage(im, roi, roi.min.x(), roi.min.y());

    spdlog::trace("fast_gaussian_blurred filter took: {} seconds.", (timer.elapsed()/1000.f));
    return im2;
}


HDRImage HDRImage::box_blurred_x(int l_size, int r_size, AtomicProgress progress, BorderMode mX, Box2i roi) const
{
    HDRImage filtered = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return filtered;

    Timer timer;
	progress.set_num_steps(roi.size().y());
    // for every pixel in the image
    parallel_for(roi.min.y(), roi.max.y(), [this,&roi,&filtered,&progress,l_size,r_size,mX](int y)
    {
        // fill up the accumulator
        int x = roi.min.x();
        filtered(x, y) = 0;
        for (int dx = -l_size; dx <= r_size; ++dx)
            filtered(x, y) += pixel(x+dx, y, mX, mX);
        
        // blur all other pixels
        for (x = roi.min.x()+1; x < roi.max.x(); ++x)
            filtered(x, y) = filtered(x-1, y) -
                             pixel(x-1-l_size, y, mX, mX) +
                             pixel(x+r_size, y, mX, mX);
        // normalize
        for (x = roi.min.x(); x < roi.max.x(); ++x)
            filtered(x, y) *= 1.f/(l_size + r_size + 1);

	    ++progress;
    });
    spdlog::trace("box_blurred_x filter took: {} seconds.", (timer.elapsed()/1000.f));

    return filtered;
}


HDRImage HDRImage::box_blurred_y(int l_size, int r_size, AtomicProgress progress, BorderMode mY, Box2i roi) const
{
    HDRImage filtered = *this;

    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return filtered;

    Timer timer;
	progress.set_num_steps(roi.size().x());
    // for every pixel in the image
    parallel_for(roi.min.x(), roi.max.x(), [this,&roi,&filtered,&progress,l_size,r_size,mY](int x)
    {
        // fill up the accumulator
        int y = roi.min.y();
        filtered(x, y) = 0;
        for (int dy = -l_size; dy <= r_size; ++dy)
            filtered(x, y) += pixel(x, y+dy, mY, mY);

        // blur all other pixels
        for (y = roi.min.y()+1; y < roi.max.y(); ++y)
            filtered(x, y) = filtered(x, y-1) -
                             pixel(x, y-1-l_size, mY, mY) +
                             pixel(x, y+r_size, mY, mY);

        // normalize
        for (y = roi.min.y(); y < roi.max.y(); ++y)
            filtered(x,y) *= 1.f/(l_size + r_size + 1);

	    ++progress;
    });
    spdlog::trace("box_blurred_y filter took: {} seconds.", (timer.elapsed()/1000.f));

    return filtered;
}

HDRImage HDRImage::resized_canvas(int newW, int newH, CanvasAnchor anchor, const Color4 & bgColor) const
{
    int oldW = width();
    int oldH = height();

    // fill in new regions with border value
    HDRImage img = HDRImage(newW, newH, bgColor);

    nanogui::Vector2i tlDst(0,0);
    // find top-left corner
    switch (anchor)
    {
        case HDRImage::TOP_RIGHT:
        case HDRImage::MIDDLE_RIGHT:
        case HDRImage::BOTTOM_RIGHT:
            tlDst.x() = newW-oldW;
            break;

        case HDRImage::TOP_CENTER:
        case HDRImage::MIDDLE_CENTER:
        case HDRImage::BOTTOM_CENTER:
            tlDst.x() = (newW-oldW)/2;
            break;

        case HDRImage::TOP_LEFT:
        case HDRImage::MIDDLE_LEFT:
        case HDRImage::BOTTOM_LEFT:
        default:
            tlDst.x() = 0;
            break;
    }
    switch (anchor)
    {
        case HDRImage::BOTTOM_LEFT:
        case HDRImage::BOTTOM_CENTER:
        case HDRImage::BOTTOM_RIGHT:
            tlDst.y() = newH-oldH;
            break;

        case HDRImage::MIDDLE_LEFT:
        case HDRImage::MIDDLE_CENTER:
        case HDRImage::MIDDLE_RIGHT:
            tlDst.y() = (newH-oldH)/2;
            break;

        case HDRImage::TOP_LEFT:
        case HDRImage::TOP_CENTER:
        case HDRImage::TOP_RIGHT:
        default:
            tlDst.y() = 0;
            break;
    }

    nanogui::Vector2i tlSrc(0,0);
    if (tlDst.x() < 0)
    {
        tlSrc.x() = -tlDst.x();
        tlDst.x() = 0;
    }
    if (tlDst.y() < 0)
    {
        tlSrc.y() = -tlDst.y();
        tlDst.y() = 0;
    }

    nanogui::Vector2i bs(std::min(oldW, newW), std::min(oldH, newH));

    img.copy_subimage(*this, Box2i(tlSrc, tlSrc+bs), tlDst.x(), tlDst.y());
    return img;
}


HDRImage HDRImage::resized(int w, int h) const
{
    HDRImage newImage(w, h);

    if (!stbir_resize_float((const float *) data(), width(), height(), 0,
                            (float *) newImage.data(), w, h, 0, 4))
        throw runtime_error("Failed to resize image.");

    return newImage;
}


/*!
 * @brief           Apply a global brightness+contrast adjustment to the RGB pixel values.
 *
 * @param b         The brightness in the range [-1,1] where 0 means no change.
 *                  Changes the brightness by shifting the midpoint by b/2.
 * @param c         The contrast in the range [-1,1] where 0 means no change.
 *                  c sets the slope of the mapping at the midpoint where
 *                   -1 -> all gray/no contrast; horizontal line;
 *                    0 -> no change; 45 degree diagonal line;
 *                    1 -> no gray/black & white; vertical line.
 *                  If linear is False, this changes the contrast by shifting the 0.25 value by c/4.
 * @param linear    Whether to linear or non-linear remapping.
 *                  The non-linear mapping keeps values within [0,1], while
 *                  the linear mapping may produce negative values and values > 1.
 * @param channel   Apply the adjustment to the specified channel(s).
 *                  Valid values are RGB, LUMINANCE or CIE_L, and CIE_CHROMATICITY.
 *                  All other values result in a no-op.
 * @return          The adjusted image.
 */
HDRImage HDRImage::brightness_contrast(float b, float c, bool linear, EChannel channel, Box2i roi) const
{
    // ensure valid ROI
    if (roi.has_volume())
        roi.intersect(box());
    else
        roi = box();

    if (!roi.has_volume())
        return *this;

    float slope = float(std::tan(lerp(0.0, M_PI_2, c/2.0 + 0.5)));
    
    if (linear)
    {
        float midpoint = (1.f-b)/2.f;

        if (channel == RGB)
            return apply_function(
                [slope,midpoint](const Color4 &c)
                {
                    return Color4(brightnessContrastL(c.r, slope, midpoint),
                                  brightnessContrastL(c.g, slope, midpoint),
                                  brightnessContrastL(c.b, slope, midpoint), c.a);
                }, roi);
        else if (channel == LUMINANCE || channel == CIE_L)
            return apply_function(
                [slope,midpoint](const Color4 &c)
                {
                    Color4 lab = convertColorSpace(c, CIELab_CS, LinearSRGB_CS);
                    return convertColorSpace(Color4(brightnessContrastL(lab.r, slope, midpoint),
                                             lab.g, lab.b, c.a), LinearSRGB_CS, CIELab_CS);
                }, roi);
        else if (channel == CIE_CHROMATICITY)
            return apply_function(
                [slope,midpoint](const Color4 &c)
                {
                    Color4 lab = convertColorSpace(c, CIELab_CS, LinearSRGB_CS);
                    return convertColorSpace(Color4(lab.r,
                                  brightnessContrastL(lab.g, slope, midpoint),
                                  brightnessContrastL(lab.b, slope, midpoint),
                                  c.a), LinearSRGB_CS, CIELab_CS);
                }, roi);
        else
            return *this;
    }
    else
    {
        float aB = (b + 1.f) / 2.f;

        if (channel == RGB)
            return apply_function(
                [aB, slope](const Color4 &c)
                {
                    return Color4(brightnessContrastNL(c.r, slope, aB),
                                  brightnessContrastNL(c.g, slope, aB),
                                  brightnessContrastNL(c.b, slope, aB),
                                  c.a);
                }, roi);
        else if (channel == LUMINANCE || channel == CIE_L)
            return apply_function(
                [aB, slope](const Color4 &c)
                {
                    Color4 lab = convertColorSpace(c, CIELab_CS, LinearSRGB_CS);
                    return convertColorSpace(Color4(brightnessContrastNL(lab.r, slope, aB),
                                  lab.g, lab.b, c.a), LinearSRGB_CS, CIELab_CS);
                }, roi);
        else if (channel == CIE_CHROMATICITY)
            return apply_function(
                [aB, slope](const Color4 &c)
                {
                    Color4 lab = convertColorSpace(c, CIELab_CS, LinearSRGB_CS);
                    return convertColorSpace(Color4(lab.r,
                                  brightnessContrastNL(lab.g, slope, aB),
                                  brightnessContrastNL(lab.b, slope, aB),
                                  c.a), LinearSRGB_CS, CIELab_CS);
                }, roi);
        else
            return *this;
    }
}

HDRImage HDRImage::inverted(Box2i roi) const
{
	return apply_function([](const Color4 &c) { return Color4(1.f - c.r, 1.f - c.g, 1.f - c.b, c.a); }, roi);
}



// local functions
namespace
{

// create a vector containing the normalized values of a 1D Gaussian filter
Array2Df horizontal_gaussian_kernel(float sigma, float truncate)
{
    // calculate the size of the filter
    int offset = int(std::ceil(truncate * sigma));
    int filterSize = 2*offset+1;

    Array2Df fData(filterSize, 1);

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

int wrap_coord(int p, int maxP, HDRImage::BorderMode m)
{
    if (p >= 0 && p < maxP)
        return p;

    switch (m)
    {
        case HDRImage::EDGE:
            return ::clamp(p, 0, maxP - 1);
        case HDRImage::REPEAT:
            return mod(p, maxP);
        case HDRImage::MIRROR:
        {
            int frac = mod(p, maxP);
            return (::abs(p) / maxP % 2 != 0) ? maxP - 1 - frac : frac;
        }
        case HDRImage::BLACK:
        default:
            return -1;
    }
}

} // namespace