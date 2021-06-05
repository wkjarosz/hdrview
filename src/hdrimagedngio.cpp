//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrimage.h"
#include <stdexcept>             // for runtime_error, out_of_range
#include <string>                // for allocator, operator==, basic_string
#include <vector>                // for vector
#include "parallelfor.h"
#include "timer.h"
#include <spdlog/spdlog.h>
#include <ImathMatrix.h>

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

// since NanoVG includes an old version of stb_image, we declare it static here
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION

#define TINY_DNG_LOADER_IMPLEMENTATION
#include "tiny_dng_loader.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning (pop)
#endif

using namespace nanogui;
using namespace std;
using Imath::M33f;
using Imath::V2f;
using Imath::V3f;

// local functions
namespace
{
void demosaic_green_linear(HDRImage & raw, const nanogui::Vector2i &red_offset);
void demosaic_red_blue_linear(HDRImage & raw, const nanogui::Vector2i &red_offset);
void demosaic_red_blue_green_guided_linear(HDRImage & raw, const nanogui::Vector2i &red_offset);
void demosaic_green_horizontal(HDRImage &res, const HDRImage &raw, const nanogui::Vector2i &red_offset);
void demosaic_green_vertical(HDRImage & img, const HDRImage &raw, const nanogui::Vector2i &red_offset);
void demosaic_green_malvar(HDRImage & raw, const nanogui::Vector2i &red_offset);
void demosaic_red_blue_malvar(HDRImage & raw, const nanogui::Vector2i &red_offset);
void demosaic_AHD(HDRImage & raw, const nanogui::Vector2i &red_offset, const M33f &camera_to_XYZ);
void demosaic_green_phelippeau(HDRImage & raw, const nanogui::Vector2i &red_offset);
void demosaic_border(HDRImage & raw, size_t border);

inline unsigned short endianSwap(unsigned short val);
void decode12BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian);
void decode14BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian);
void decode16BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian);
void printImageInfo(const tinydng::DNGImage & image);
HDRImage develop(vector<float> & raw,
                 const tinydng::DNGImage & param1,
                 const tinydng::DNGImage & param2);
void malvar_green(HDRImage &raw, int c, const nanogui::Vector2i & red_offset);
void malvar_red_or_blue_at_green(HDRImage &raw, int c, const nanogui::Vector2i &red_offset, bool horizontal);
void malvar_red_or_blue(HDRImage &raw, int c1, int c2, const nanogui::Vector2i &red_offset);
void bilinear_red_blue(HDRImage &raw, int c, const nanogui::Vector2i & red_offset);
void green_based_red_or_blue(HDRImage &raw, int c, const nanogui::Vector2i &red_offset);
inline float clamp2(float value, float mn, float mx);
inline float clamp4(float value, float a, float b, float c, float d);
inline float interp_green_h(const HDRImage &raw, int x, int y);
inline float interp_green_v(const HDRImage &raw, int x, int y);
inline float ghG(const Array2Df & G, int i, int j);
inline float gvG(const Array2Df & G, int i, int j);
inline int bayer_color(int x, int y);
inline nanogui::Vector3f camera_to_Lab(const V3f & c, const M33f & camera_to_XYZ, const vector<float> & LUT);

} // namespace


void HDRImage::load_dng(const string & filename)
{
	vector<tinydng::DNGImage> images;
	{
		std::string err, warn;
		vector<tinydng::FieldInfo> customFields;
		bool ret = tinydng::LoadDNG(filename.c_str(), customFields, &images, &err, &warn);

		if (ret == false)
			throw runtime_error("Failed to load DNG. " + err);
	}

	// DNG files sometimes only store the orientation in one of the images,
	// instead of all of them. find any set value and save it
	int orientation = 0;
	for (size_t i = 0; i < images.size(); i++)
	{
		spdlog::debug("Image [{}] size = {} x {}.", i, images[i].width, images[i].height);
		spdlog::debug("Image [{}] orientation = {}", i, images[i].orientation);
		if (images[i].orientation != 0)
			orientation = images[i].orientation;
	}

	// Find largest image based on width.
	size_t imageIndex = size_t(-1);
	{
		size_t largest = 0;
		int largestWidth = images[0].width;
		for (size_t i = 0; i < images.size(); i++)
		{
			if (largestWidth < images[i].width)
			{
				largest = i;
				largestWidth = images[i].width;
			}
		}

		imageIndex = largest;
	}
	tinydng::DNGImage & image = images[imageIndex];


	spdlog::debug("\nLargest image within DNG:");
	printImageInfo(image);
	spdlog::debug("\nLast image within DNG:");
	printImageInfo(images.back());

	spdlog::debug("Loading image [{}].", imageIndex);

	int w = image.width;
	int h = image.height;

	// Convert to float.
	vector<float> hdr;
	bool endianSwap = false;        // TODO

	int spp = image.samples_per_pixel;
	if (image.bits_per_sample == 12)
		decode12BitToFloat(hdr, &(image.data.at(0)), w, h * spp, endianSwap);
	else if (image.bits_per_sample == 14)
		decode14BitToFloat(hdr, &(image.data.at(0)), w, h * spp, endianSwap);
	else if (image.bits_per_sample == 16)
		decode16BitToFloat(hdr, &(image.data.at(0)), w, h * spp, endianSwap);
	else
		throw runtime_error("Error loading DNG: Unsupported bits_per_sample : " + to_string(spp));

	float inv_scale = 1.0f / static_cast<float>((1 << image.bits_per_sample));
	if (spp == 3)
	{
		spdlog::debug("Decoding a 3 sample-per-pixel DNG image.");
		// normalize
		parallel_for(0, hdr.size(), [&hdr,inv_scale](int i)
		{
			hdr[i] *= inv_scale;
		});

		// Create color image & normalize intensity.
		resize(w, h);

		Timer timer;
		// normalize
		parallel_for(0, h, [this,w,inv_scale,&hdr](int y)
		{
			for (int x = 0; x < w; ++x)
			{
				int index = 3 * y * w + x;
				(*this)(x, y) = Color4(hdr[index] * inv_scale + 0,
										hdr[index] * inv_scale + 1,
										hdr[index] * inv_scale + 2, 1.0f);
			}
		});
		spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed()/1000.f));
	}
	else if (spp == 1)
	{
		// Create grayscale image & normalize intensity.
		spdlog::debug("Decoding a 1 sample-per-pixel DNG image.");
		Timer timer;
		*this = develop(hdr, image, images.back());
		spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed()/1000.f));
	}
	else
		throw runtime_error("Error loading DNG: Unsupported samples per pixel: " + to_string(spp));


	int start_x = ::clamp(image.active_area[1], 0, w);
	int end_x = ::clamp(image.active_area[3], 0, w);
	int start_y = ::clamp(image.active_area[0], 0, h);
	int end_y = ::clamp(image.active_area[2], 0, h);

	// FIXME
	// *this = block(start_x, start_y,
	//               end_x-start_x,
	//               end_y-start_y).eval();
	HDRImage copy(end_x-start_x, end_y-start_y);
	copy.copy_subimage(*this, Box2i(Vector2i(start_x, start_y), Vector2i(end_x, end_y)), 0, 0);
	*this = copy;

	enum Orientations
	{
		ORIENTATION_TOPLEFT = 1,
		ORIENTATION_TOPRIGHT = 2,
		ORIENTATION_BOTRIGHT = 3,
		ORIENTATION_BOTLEFT = 4,
		ORIENTATION_LEFTTOP = 5,
		ORIENTATION_RIGHTTOP = 6,
		ORIENTATION_RIGHTBOT = 7,
		ORIENTATION_LEFTBOT = 8
	};

	// now rotate image based on stored orientation
	switch (orientation)
	{
		case ORIENTATION_TOPRIGHT: *this = flipped_horizontal(); break;
		case ORIENTATION_BOTRIGHT: *this = flipped_vertical().flipped_horizontal(); break;
		case ORIENTATION_BOTLEFT : *this = flipped_vertical(); break;
		case ORIENTATION_LEFTTOP : *this = rotated_90_ccw().flipped_vertical(); break;
		case ORIENTATION_RIGHTTOP: *this = rotated_90_cw(); break;
		case ORIENTATION_RIGHTBOT: *this = rotated_90_cw().flipped_vertical(); break;
		case ORIENTATION_LEFTBOT : *this = rotated_90_ccw(); break;
		default: break;// none (0), or ORIENTATION_TOPLEFT
	}
}


/*!
 * \brief Multiplies a raw image by the Bayer mosaic pattern so that only a single
 * R, G, or B channel is non-zero for each pixel.
 *
 * We assume the canonical Bayer pattern looks like:
 *
 * \rst
 * +---+---+
 * | R | G |
 * +---+---+
 * | G | B |
 * +---+---+
 *
 * \endrst
 *
 * and the pattern is tiled across the entire image.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern
 */
void HDRImage::bayer_mosaic(const nanogui::Vector2i &red_offset)
{
    Color4 mosaic[2][2] = {{Color4(1.f, 0.f, 0.f, 1.f), Color4(0.f, 1.f, 0.f, 1.f)},
                           {Color4(0.f, 1.f, 0.f, 1.f), Color4(0.f, 0.f, 1.f, 1.f)}};
    for (int y = 0; y < height(); ++y)
    {
        int r = mod(y - red_offset.y(), 2);
        for (int x = 0; x < width(); ++x)
        {
            int c = mod(x - red_offset.x(), 2);
            (*this)(x,y) *= mosaic[r][c];
        }
    }
}



// /*!
//  * \brief Reduce some remaining color fringing and zipper artifacts by median-filtering the
//  * red-green and blue-green differences as originally proposed by Freeman.
//  */
// HDRImage HDRImage::median_filter_bayer_artifacts() const
// {
//     AtomicProgress progress;
//     HDRImage color_diff = apply_function([](const Color4 & c){return Color4(c.r-c.g,c.g,c.b-c.g,c.a);});
//     color_diff = color_diff.median_filtered(1.f, 0, AtomicProgress(progress, .5f))
//                            .median_filtered(1.f, 2, AtomicProgress(progress, .5f));
//     return apply_function(color_diff, [](const Color4 & i, const Color4 & med){return Color4(med.r + i.g, i.g, med.b + i.g, i.a);});
// }


// local functions
namespace
{

// Taken from http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
const M33f XYZD65TosRGB(
	3.2406f, -1.5372f, -0.4986f,
	-0.9689f,  1.8758f,  0.0415f,
	0.0557f, -0.2040f,  1.0570f);

const M33f XYZ_D50_to_XYZ_D65(
	0.9555766f, -0.0230393f, 0.0631636f,
	-0.0282895f,  1.0099416f, 0.0210077f,
	0.0122982f, -0.0204830f, 1.3299098f
);

// Taken from http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
const M33f XYZ_D50_to_sRGB(
	3.2404542f, -1.5371385f, -0.4985314f,
	-0.9692660f,  1.8760108f,  0.0415560f,
	0.0556434f, -0.2040259f,  1.0572252
);

M33f compute_camera_to_XYZ_D50(const tinydng::DNGImage &param)
{
	//
	// The full DNG color-correction model is described in the
	// "Mapping Camera Color Space to CIE XYZ Space" section of the DNG spec.
	//
	// Let n be the dimensionality of the camera color space (usually 3 or 4).
	// Let CM be the n-by-3 matrix interpolated from the ColorMatrix1 and ColorMatrix2 tags.
	// Let CC be the n-by-n matrix interpolated from the CameraCalibration1 and CameraCalibration2 tags (or identity matrices, if the signatures don't match).
	// Let AB be the n-by-n matrix, which is zero except for the diagonal entries, which are defined by the AnalogBalance tag.
	// Let RM be the 3-by-n matrix interpolated from the ReductionMatrix1 and ReductionMatrix2 tags.
	// Let FM be the 3-by-n matrix interpolated from the ForwardMatrix1 and ForwardMatrix2 tags.

	// TODO: the color correction code below is not quite correct

	// if the ForwardMatrix is included:
	
	// FIXME: need matrix class with inverse
	if (false)//param.has_forward_matrix2)
	{
		M33f FM(param.forward_matrix2[0][0], param.forward_matrix2[0][1], param.forward_matrix2[0][2],
				param.forward_matrix2[1][0], param.forward_matrix2[1][1], param.forward_matrix2[1][2],
				param.forward_matrix2[2][0], param.forward_matrix2[2][1], param.forward_matrix2[2][2]);
		M33f CC(param.camera_calibration2[0][0], param.camera_calibration2[0][1], param.camera_calibration2[0][2],
				param.camera_calibration2[1][0], param.camera_calibration2[1][1], param.camera_calibration2[1][2],
				param.camera_calibration2[2][0], param.camera_calibration2[2][1], param.camera_calibration2[2][2]);
		M33f AB;
		AB.x[0][0] = param.analog_balance[0];
		AB.x[1][1] = param.analog_balance[1];
		AB.x[2][2] = param.analog_balance[2];

		V3f CameraNeutral(param.as_shot_neutral[0],
						  param.as_shot_neutral[1],
						  param.as_shot_neutral[2]);
		M33f ABCC = (AB * CC).inverse();
		V3f ReferenceNeutral = CameraNeutral * ABCC;
		M33f D;
		D.x[0][0] = 1.f/ReferenceNeutral[0];
		D.x[1][1] = 1.f/ReferenceNeutral[1];
		D.x[2][2] = 1.f/ReferenceNeutral[2];

		return FM * D * ABCC;
	}
	else
	{
		M33f CM(param.color_matrix2[0][0], param.color_matrix2[0][1], param.color_matrix2[0][2],
				param.color_matrix2[1][0], param.color_matrix2[1][1], param.color_matrix2[1][2],
				param.color_matrix2[2][0], param.color_matrix2[2][1], param.color_matrix2[2][2]);

		return CM.inverse();
	}
}



/*!
 * \brief Interpolate the missing red and blue pixels using a simple linear or bilinear interpolation.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_red_blue_linear(HDRImage & raw, const nanogui::Vector2i &red_offset)
{
    bilinear_red_blue(raw, 0, red_offset);
    bilinear_red_blue(raw, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, (red_offset.y() + 1) % 2));
}


/*!
 * \brief Compute the missing green pixels using a simple bilinear interpolation.
 * from the 4 neighbors.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_linear(HDRImage & raw, const nanogui::Vector2i &red_offset)
{
	parallel_for(1, raw.height()-1-red_offset.y(), 2, [&raw,&red_offset](int yy)
    {
        int t = yy + red_offset.y();
        for (int xx = 1; xx < raw.width()-1-red_offset.x(); xx += 2)
        {
            int l = xx + red_offset.x();

            // coordinates of the missing green pixels (red and blue) in
            // this Bayer tile are: (l,t) and (r,b)
            int r = l+1;
            int b = t+1;

            raw(l, t).g = 0.25f * (raw(l, t - 1).g + raw(l, t + 1).g + raw(l - 1, t).g + raw(l + 1, t).g);
            raw(r, b).g = 0.25f * (raw(r, b - 1).g + raw(r, b + 1).g + raw(r - 1, b).g + raw(r + 1, b).g);
        }
    });
}

/*!
 * \brief Interpolate the missing red and blue pixels using a linear or bilinear interpolation
 * guided by the green channel, which is assumed already demosaiced.
 *
 * The interpolation is equivalent to performing (bi)linear interpolation of the red-green and
 * blue-green differences, and then adding green back into the interpolated result. This inject
 * some of the higher resolution of the green channel, and reduces color fringing under the
 * assumption that the color channels in natural images are positively correlated.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_red_blue_green_guided_linear(HDRImage & raw, const nanogui::Vector2i &red_offset)
{
    green_based_red_or_blue(raw, 0, red_offset);
    green_based_red_or_blue(raw, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, (red_offset.y() + 1) % 2));
}

/*!
 * \brief Compute the missing green pixels using vertical linear interpolation.
 *
 * @param raw       The source raw pixel data.
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_horizontal(HDRImage &res, const HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    parallel_for(red_offset.y(), res.height(), 2, [&res,&raw,&red_offset](int y)
    {
        for (int x = 2+red_offset.x(); x < res.width()-2; x += 2)
        {
            // populate the green channel into the red and blue pixels
            res(x  , y  ).g = interp_green_h(raw, x, y);
            res(x+1, y+1).g = interp_green_h(raw, x + 1, y + 1);
        }
    });
}


/*!
 * \brief Compute the missing green pixels using horizontal linear interpolation.
 *
 * @param raw       The source raw pixel data.
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_vertical(HDRImage & res, const HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    parallel_for(2+red_offset.y(), res.height()-2, 2, [&res,&raw,&red_offset](int y)
    {
        for (int x = red_offset.x(); x < res.width(); x += 2)
        {
            res(x  , y  ).g = interp_green_v(raw, x, y);
            res(x+1, y+1).g = interp_green_v(raw, x + 1, y + 1);
        }
    });
}

/*!
 * \brief Interpolate the missing green pixels using the method by Malvar et al. 2004.
 *
 * The method uses a plus "+" shaped 5x5 filter, which is linear, except--to reduce
 * ringing/over-shooting--the interpolation is not allowed to extrapolate higher or
 * lower than the surrounding green pixels.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_malvar(HDRImage & raw, const nanogui::Vector2i &red_offset)
{
    // fill in missing green at red pixels
    malvar_green(raw, 0, red_offset);
    // fill in missing green at blue pixels
    malvar_green(raw, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, (red_offset.y() + 1) % 2));
}




/*!
 * \brief Interpolate the missing red and blue pixels using the method by Malvar et al. 2004.
 *
 * The interpolation for each channel is guided by the available information from all other
 * channels. The green channel is assumed to already be demosaiced.
 *
 * The method uses a 5x5 linear filter.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_red_blue_malvar(HDRImage & raw, const nanogui::Vector2i &red_offset)
{
    // fill in missing red horizontally
    malvar_red_or_blue_at_green(raw, 0, nanogui::Vector2i((red_offset.x() + 1) % 2, red_offset.y()), true);
    // fill in missing red vertically
    malvar_red_or_blue_at_green(raw, 0, nanogui::Vector2i(red_offset.x(), (red_offset.y() + 1) % 2), false);

    // fill in missing blue horizontally
    malvar_red_or_blue_at_green(raw, 2, nanogui::Vector2i(red_offset.x(), (red_offset.y() + 1) % 2), true);
    // fill in missing blue vertically
    malvar_red_or_blue_at_green(raw, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, red_offset.y()), false);

    // fill in missing red at blue
    malvar_red_or_blue(raw, 0, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, (red_offset.y() + 1) % 2));
    // fill in missing blue at red
    malvar_red_or_blue(raw, 2, 0, red_offset);
}

/*!
 * \brief Demosaic the image using the "Adaptive Homogeneity-Directed" interpolation
 * approach proposed by Hirakawa et al. 2004.
 *
 * The approach is fairly expensive, but produces the best results.
 *
 * The method first creates two competing full-demosaiced images: one where the
 * green channel is interpolated vertically, and the other horizontally. In both
 * images the red and green are demosaiced using the corresponding green channel
 * as a guide.
 *
 * The two candidate images are converted to XYZ (using the supplied \a camera_to_XYZ
 * matrix) subsequently to CIE L*a*b* space in order to determine how perceptually
 * "homogeneous" each pixel neighborhood is.
 *
 * "Homogeneity maps" are created for the two candidate imates which count, for each
 * pixel, the number of perceptually similar pixels among the 4 neighbors in the
 * cardinal directions.
 *
 * Finally, the output image is formed by choosing for each pixel the demosaiced
 * result which has the most homogeneous "votes" in the surrounding 3x3 neighborhood.
 *
 * @param red_offset     The x,y offset to the first red pixel in the Bayer pattern.
 * @param camera_to_XYZ   The matrix that transforms from sensor values to XYZ with
 *                      D65 white point.
 */
void demosaic_AHD(HDRImage & raw, const nanogui::Vector2i &red_offset, const M33f &camera_to_XYZ)
{
    using Image3f = Array2D<nanogui::Vector3f>;
    using HomoMap = Array2D<uint8_t>;
    HDRImage rgb_H = raw;
    HDRImage rgb_V = raw;
    Image3f lab_H(raw.width(), raw.height(), nanogui::Vector3f(0.f));
    Image3f lab_V(raw.width(), raw.height(), nanogui::Vector3f(0.f));
    HomoMap homo_H = HomoMap(raw.width(), raw.height());
    HomoMap homo_V = HomoMap(raw.width(), raw.height());

    // interpolate green channel both horizontally and vertically
    demosaic_green_horizontal(rgb_H, raw, red_offset);
    demosaic_green_vertical(rgb_V, raw, red_offset);

    // interpolate the red and blue using the green as a guide
    demosaic_red_blue_green_guided_linear(rgb_H, red_offset);
    demosaic_red_blue_green_guided_linear(rgb_V, red_offset);

    // Scale factor to push XYZ values to [0,1] range
    // FIXME
    float scale = 1.0f;// / (max().max() * camera_to_XYZ.max());

    // Precompute a table for the nonlinear part of the CIELab conversion
    vector<float> Lab_LUT;
    Lab_LUT.reserve(0xFFFF);
    parallel_for(0, Lab_LUT.size(), [&Lab_LUT](int i)
    {
        float r = i * 1.0f / (Lab_LUT.size()-1);
        Lab_LUT[i] = r > 0.008856 ? std::pow(r, 1.0f / 3.0f) : 7.787f*r + 4.0f/29.0f;
    });

    // convert both interpolated images to CIE L*a*b* so we can compute perceptual differences
    parallel_for(0, rgb_H.height(), [&rgb_H,&lab_H,&camera_to_XYZ,&Lab_LUT,scale](int y)
    {
        for (int x = 0; x < rgb_H.width(); ++x)
            lab_H(x,y) = camera_to_Lab(V3f(rgb_H(x,y)[0], rgb_H(x,y)[1], rgb_H(x,y)[2])*scale, camera_to_XYZ, Lab_LUT);
    });
    parallel_for(0, rgb_V.height(), [&rgb_V,&lab_V,&camera_to_XYZ,&Lab_LUT,scale](int y)
    {
        for (int x = 0; x < rgb_V.width(); ++x)
            lab_V(x,y) = camera_to_Lab(V3f(rgb_V(x,y)[0], rgb_V(x,y)[1], rgb_V(x,y)[2])*scale, camera_to_XYZ, Lab_LUT);
    });

    // Build homogeneity maps from the CIELab images which count, for each pixel,
    // the number of visually similar neighboring pixels
    static const int neighbor[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
    parallel_for(1, lab_H.height()-1, [&homo_H,&homo_V,&lab_H,&lab_V](int y)
    {
        for (int x = 1; x < lab_H.width()-1; ++x)
        {
            float ldiffH[4], ldiffV[4], abdiffH[4], abdiffV[4];

            for (int i = 0; i < 4; i++)
            {
                int dx = neighbor[i][0];
                int dy = neighbor[i][1];

                // Local luminance and chromaticity differences to the 4 neighbors for both interpolations directions
                ldiffH[i] = std::abs(lab_H(x,y)[0] - lab_H(x+dx,y+dy)[0]);
                ldiffV[i] = std::abs(lab_V(x,y)[0] - lab_V(x+dx,y+dy)[0]);
                abdiffH[i] = ::square(lab_H(x,y)[1] - lab_H(x+dx,y+dy)[1]) +
                             ::square(lab_H(x,y)[2] - lab_H(x+dx,y+dy)[2]);
                abdiffV[i] = ::square(lab_V(x,y)[1] - lab_V(x+dx,y+dy)[1]) +
                             ::square(lab_V(x,y)[2] - lab_V(x+dx,y+dy)[2]);
            }

            float leps = std::min(std::max(ldiffH[0], ldiffH[1]),
                                  std::max(ldiffV[2], ldiffV[3]));
            float abeps = std::min(std::max(abdiffH[0], abdiffH[1]),
                                   std::max(abdiffV[2], abdiffV[3]));

            // Count number of neighboring pixels that are visually similar
            for (int i = 0; i < 4; i++)
            {
                if (ldiffH[i] <= leps && abdiffH[i] <= abeps)
                    homo_H(x,y)++;
                if (ldiffV[i] <= leps && abdiffV[i] <= abeps)
                    homo_V(x,y)++;
            }
        }
    });

    // Combine the most homogenous pixels for the final result
    parallel_for(1, raw.height()-1, [&raw,&homo_H,&homo_V,&rgb_H,&rgb_V](int y)
    {
        for (int x = 1; x < raw.width()-1; ++x)
        {
            // Sum up the homogeneity of both images in a 3x3 window
            int hmH = 0, hmV = 0;
            for (int j = y-1; j <= y+1; j++)
                for (int i = x-1; i <= x+1; i++)
                {
                    hmH += homo_H(i, j);
                    hmV += homo_V(i, j);
                }

            if (hmH > hmV)
            {
                // horizontal interpolation is more homogeneous
                raw(x,y) = rgb_H(x,y);
            }
            else if (hmV > hmH)
            {
                // vertical interpolation is more homogeneous
                raw(x,y) = rgb_V(x,y);
            }
            else
            {
                // No clear winner, blend
                Color4 blend = (rgb_H(x,y) + rgb_V(x,y)) * 0.5f;
                raw(x,y) = blend;
            }
        }
    });

    // Now handle the boundary pixels
    demosaic_border(raw, 3);
}



/*!
 * \brief Interpolate the missing green pixels using the method by Phelippeau et al. 2009.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_phelippeau(HDRImage & raw, const nanogui::Vector2i &red_offset)
{
	Array2Df Gh(raw.width(), raw.height());
    Array2Df Gv(raw.width(), raw.height());

    // populate horizontally interpolated green
    parallel_for(red_offset.y(), raw.height(), 2, [&raw,&Gh,&red_offset](int y)
    {
        for (int x = 2+red_offset.x(); x < raw.width() - 2; x += 2)
        {
            Gh(x  , y  ) = interp_green_h(raw, x, y);
            Gh(x+1, y+1) = interp_green_h(raw, x + 1, y + 1);
        }
    });

    // populate vertically interpolated green
    parallel_for(2+red_offset.y(), raw.height()-2, 2, [&raw,&Gv,&red_offset](int y)
    {
        for (int x = red_offset.x(); x < raw.width(); x += 2)
        {
            Gv(x  , y  ) = interp_green_v(raw, x, y);
            Gv(x+1, y+1) = interp_green_v(raw, x + 1, y + 1);
        }
    });

    parallel_for(2+red_offset.y(), raw.height()-2, 2, [&raw,&Gh,&Gv,red_offset](int y)
    {
        for (int x = 2+red_offset.x(); x < raw.width()-2; x += 2)
        {
            float ghGh = ghG(Gh, x, y);
            float ghGv = ghG(Gv, x, y);
            float gvGh = gvG(Gh, x, y);
            float gvGv = gvG(Gv, x, y);

            raw(x, y).g = (ghGh + gvGh <= gvGv + ghGv) ? Gh(x, y) : Gv(x, y);

            x++;
            y++;

            ghGh = ghG(Gh, x, y);
            ghGv = ghG(Gv, x, y);
            gvGh = gvG(Gh, x, y);
            gvGv = gvG(Gv, x, y);

            raw(x, y).g = (ghGh + gvGh <= gvGv + ghGv) ? Gh(x, y) : Gv(x, y);
        }
    });
}



/*!
 * \brief   Demosaic the border of the image using naive averaging.
 *
 * Provides a results for all border pixels using a straight averge of the available pixels
 * in the 3x3 neighborhood. Useful in combination with more sophisticated methods which
 * require a larger window, and therefore cannot produce results at the image boundary.
 *
 * @param border    The size of the border in pixels.
 */
void demosaic_border(HDRImage & raw, size_t border)
{
    parallel_for(0, raw.height(), [&](size_t y)
    {
        for (size_t x = 0; x < (size_t)raw.width(); ++x)
        {
            // skip the center of the image
            if (x == border && y >= border && y < raw.height() - border)
                x = raw.width() - border;

            nanogui::Vector3f sum = nanogui::Vector3f(0.f);
            nanogui::Vector3i count = nanogui::Vector3i(0.f);

            for (size_t ys = y - 1; ys <= y + 1; ++ys)
            {
                for (size_t xs = x - 1; xs <= x + 1; ++xs)
                {
                    // rely on xs and ys = -1 to wrap around to max value
                    // since they are unsigned
                    if (ys < (size_t)raw.height() && xs < (size_t)raw.width())
                    {
                        int c = bayer_color(xs, ys);
                        sum[c] += raw(xs,ys)[c];
                        ++count[c];
                    }
                }
            }

            int col = bayer_color(x, y);
            for (int c = 0; c < 3; ++c)
            {
                if (col != c)
                    raw(x,y)[c] = count[c] ? (sum[c] / count[c]) : 1.0f;
            }
        }
    });
}


HDRImage develop(vector<float> & raw,
                 const tinydng::DNGImage & param1,
                 const tinydng::DNGImage & param2)
{
	Timer timer;

	int width = param1.width;
	int height = param1.height;
	int black_level = param1.black_level[0];
	int white_level = param1.white_level[0];
	Vector2i red_offset(param1.active_area[1] % 2, param1.active_area[0] % 2);

	HDRImage developed(width, height);

	M33f camera_to_XYZ_D50 = compute_camera_to_XYZ_D50(param2);
	M33f camera_to_sRGB = XYZ_D50_to_sRGB * camera_to_XYZ_D50;

	// Chapter 5 of DNG spec
	// Map raw values to linear reference values (i.e. adjust for black and white level)
	//
	// we also apply white balance before demosaicing here because it increases the
	// correlation between the color channels and reduces artifacts
	V3f wb(param2.as_shot_neutral[0], param2.as_shot_neutral[1], param2.as_shot_neutral[2]);
	const float inv_scale = 1.0f / (white_level - black_level);
	parallel_for(0, developed.height(), [&developed,&raw,black_level,inv_scale,&wb](int y)
	{
		for (int x = 0; x < developed.width(); x++)
		{
			float v = ::clamp((raw[y * developed.width() + x] - black_level)*inv_scale, 0.f, 1.f);
			V3f rgb(v,v,v);
			rgb = rgb / wb;
			developed(x,y) = Color4(rgb.x,rgb.y,rgb.z,1.f);
		}
	});

	//
	// demosaic
	//

    // {
	// 	// naive linear
    //     demosaic_green_linear(developed, red_offset);
    //     demosaic_red_blue_linear(developed, red_offset);
    // }
	// {
	// 	// green guided linear
    //     demosaic_green_linear(developed, red_offset);
    //     demosaic_red_blue_green_guided_linear(developed, red_offset);
    // }
    // {
	// 	// malvar
    //     demosaic_green_malvar(developed, red_offset);
    //     demosaic_red_blue_malvar(developed, red_offset);
    // }
	// AHD
	demosaic_AHD(developed, red_offset, XYZ_D50_to_XYZ_D65 * camera_to_XYZ_D50);

	// color correction
	// also undo the white balance since the color correction matrix already includes it
	parallel_for(0, developed.height(), [&developed,&camera_to_sRGB,&wb](int y)
	{
		for (int x = 0; x < developed.width(); x++)
		{
			V3f rgb(developed(x,y).r, developed(x,y).g, developed(x,y).b);
			rgb = rgb * wb;
			V3f sRGB = rgb * camera_to_sRGB;
			developed(x,y) = Color4(sRGB.x,sRGB.y,sRGB.z,1.f);
		}
	});

	spdlog::debug("Developing DNG image took {} seconds.", (timer.elapsed()/1000.f));
	return developed;
}


inline unsigned short endianSwap(unsigned short val)
{
	unsigned short ret;

	unsigned char *buf = reinterpret_cast<unsigned char *>(&ret);

	unsigned short x = val;
	buf[1] = static_cast<unsigned char>(x);
	buf[0] = static_cast<unsigned char>(x >> 8);

	return ret;
}


// The decode functions below are adapted from syoyo's dng2exr, in the tinydng library within the
// ext subfolder

//
// Decode 12bit integer image into floating point HDR image
//
void decode12BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian)
{
	Timer timer;

	int offsets[2][2] = {{0, 1}, {1, 2}};
	int bitShifts[2] = {4, 0};

	image.resize(static_cast<size_t>(width * height));

	parallel_for(0, height, [&image,width,&offsets,&bitShifts,data,swapEndian](int y)
	{
		for (int x = 0; x < width; x++)
		{
			unsigned char buf[3];

			// Calculate load address for 12bit pixel(three 8 bit pixels)
			int n = int(y * width + x);

			// 24 = 12bit * 2 pixel, 8bit * 3 pixel
			int n2 = n % 2;           // used for offset & bitshifts
			int addr3 = (n / 2) * 3;  // 8bit pixel pos
			int odd = (addr3 % 2);

			int bit_shift;
			bit_shift = bitShifts[n2];

			int offset[2];
			offset[0] = offsets[n2][0];
			offset[1] = offsets[n2][1];

			if (swapEndian)
			{
				// load with short byte swap
				if (odd)
				{
					buf[0] = data[addr3 - 1];
					buf[1] = data[addr3 + 2];
					buf[2] = data[addr3 + 1];
				}
				else
				{
					buf[0] = data[addr3 + 1];
					buf[1] = data[addr3 + 0];
					buf[2] = data[addr3 + 3];
				}
			}
			else
			{
				buf[0] = data[addr3 + 0];
				buf[1] = data[addr3 + 1];
				buf[2] = data[addr3 + 2];
			}
			unsigned int b0 = static_cast<unsigned int>(buf[offset[0]] & 0xff);
			unsigned int b1 = static_cast<unsigned int>(buf[offset[1]] & 0xff);

			unsigned int val = (b0 << 8) | b1;
			val = 0xfff & (val >> bit_shift);

			image[static_cast<size_t>(y * width + x)] = static_cast<float>(val);
		}
	});

	spdlog::debug("decode12BitToFloat took: {} seconds.", (timer.lap() / 1000.f));
}

//
// Decode 14bit integer image into floating point HDR image
//
void decode14BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian)
{
	Timer timer;

	int offsets[4][3] = {{0, 0, 1}, {1, 2, 3}, {3, 4, 5}, {5, 5, 6}};
	int bitShifts[4] = {2, 4, 6, 0};

	image.resize(static_cast<size_t>(width * height));

	parallel_for(0, height, [&image,width,&offsets,&bitShifts,data,swapEndian](int y)
	{
		for (int x = 0; x < width; x++)
		{
			unsigned char buf[7];

			// Calculate load address for 14bit pixel(three 8 bit pixels)
			int n = int(y * width + x);

			// 56 = 14bit * 4 pixel, 8bit * 7 pixel
			int n4 = n % 4;           // used for offset & bitshifts
			int addr7 = (n / 4) * 7;  // 8bit pixel pos
			int odd = (addr7 % 2);

			int offset[3];
			offset[0] = offsets[n4][0];
			offset[1] = offsets[n4][1];
			offset[2] = offsets[n4][2];

			int bit_shift;
			bit_shift = bitShifts[n4];

			if (swapEndian)
			{
				// load with short byte swap
				if (odd)
				{
					buf[0] = data[addr7 - 1];
					buf[1] = data[addr7 + 2];
					buf[2] = data[addr7 + 1];
					buf[3] = data[addr7 + 4];
					buf[4] = data[addr7 + 3];
					buf[5] = data[addr7 + 6];
					buf[6] = data[addr7 + 5];
				}
				else
				{
					buf[0] = data[addr7 + 1];
					buf[1] = data[addr7 + 0];
					buf[2] = data[addr7 + 3];
					buf[3] = data[addr7 + 2];
					buf[4] = data[addr7 + 5];
					buf[5] = data[addr7 + 4];
					buf[6] = data[addr7 + 7];
				}
			}
			else
			{
				memcpy(buf, &data[addr7], 7);
			}
			unsigned int b0 = static_cast<unsigned int>(buf[offset[0]] & 0xff);
			unsigned int b1 = static_cast<unsigned int>(buf[offset[1]] & 0xff);
			unsigned int b2 = static_cast<unsigned int>(buf[offset[2]] & 0xff);

			// unsigned int val = (b0 << 16) | (b1 << 8) | b2;
			// unsigned int val = (b2 << 16) | (b0 << 8) | b0;
			unsigned int val = (b0 << 16) | (b1 << 8) | b2;
			// unsigned int val = b2;
			val = 0x3fff & (val >> bit_shift);

			image[static_cast<size_t>(y * width + x)] = static_cast<float>(val);
		}
	});

	spdlog::debug("decode14BitToFloat took: {} seconds.", (timer.lap() / 1000.f));
}

//
// Decode 16bit integer image into floating point HDR image
//
void decode16BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian)
{
	Timer timer;

	image.resize(static_cast<size_t>(width * height));
	unsigned short *ptr = reinterpret_cast<unsigned short *>(data);

	parallel_for(0, height, [&image,width,ptr,swapEndian](int y)
	{
		for (int x = 0; x < width; x++)
		{
			unsigned short val = ptr[y * width + x];
			if (swapEndian)
				val = endianSwap(val);

			// range will be [0, 65535]
			image[static_cast<size_t>(y * width + x)] = static_cast<float>(val);
		}
	});

	spdlog::debug("decode16BitToFloat took: {} seconds.", (timer.lap() / 1000.f));
}

char get_colorname(int c)
{
	switch (c)
	{
		case 0:
			return 'R';
		case 1:
			return 'G';
		case 2:
			return 'B';
		case 3:
			return 'C';
		case 4:
			return 'M';
		case 5:
			return 'Y';
		case 6:
			return 'W';
		default:
			return '?';
	}
}

void printImageInfo(const tinydng::DNGImage & image)
{
	spdlog::debug("width = {}.", image.width);
	spdlog::debug("height = {}.", image.height);
	spdlog::debug("bits per pixel = {}.", image.bits_per_sample);
	spdlog::debug("bits per pixel(original) = {}", image.bits_per_sample_original);
	spdlog::debug("samples per pixel = {}", image.samples_per_pixel);
	spdlog::debug("sample format = {}", image.sample_format);

	spdlog::debug("version = {}", image.version);

	for (int s = 0; s < image.samples_per_pixel; s++)
	{
		spdlog::debug("white_level[{}] = {}", s, image.white_level[s]);
		spdlog::debug("black_level[{}] = {}", s, image.black_level[s]);
	}

	spdlog::debug("tile_width = {}", image.tile_width);
	spdlog::debug("tile_length = {}", image.tile_length);
	spdlog::debug("tile_offset = {}", image.tile_offset);
	spdlog::debug("tile_offset = {}", image.tile_offset);

	spdlog::debug("cfa_layout = {}", image.cfa_layout);
	spdlog::debug("cfa_plane_color = {}{}{}{}",
	               get_colorname(image.cfa_plane_color[0]),
	               get_colorname(image.cfa_plane_color[1]),
	               get_colorname(image.cfa_plane_color[2]),
	               get_colorname(image.cfa_plane_color[3]));
	spdlog::debug("cfa_pattern[2][2] = \n {}, {},\n {}, {}",
	               image.cfa_pattern[0][0],
	               image.cfa_pattern[0][1],
	               image.cfa_pattern[1][0],
	               image.cfa_pattern[1][1]);

	spdlog::debug("active_area = \n {}, {},\n {}, {}",
	               image.active_area[0],
	               image.active_area[1],
	               image.active_area[2],
	               image.active_area[3]);

	spdlog::debug("calibration_illuminant1 = {}", image.calibration_illuminant1);
	spdlog::debug("calibration_illuminant2 = {}", image.calibration_illuminant2);

	spdlog::debug("color_matrix1 = ");
	for (size_t k = 0; k < 3; k++)
		spdlog::debug("{} {} {}",
		               image.color_matrix1[k][0],
		               image.color_matrix1[k][1],
		               image.color_matrix1[k][2]);

	spdlog::debug("color_matrix2 = ");
	for (size_t k = 0; k < 3; k++)
		spdlog::debug("{} {} {}",
		               image.color_matrix2[k][0],
		               image.color_matrix2[k][1],
		               image.color_matrix2[k][2]);

	if (true)//image.has_forward_matrix2)
	{
		spdlog::debug("forward_matrix1 found = ");
		for (size_t k = 0; k < 3; k++)
			spdlog::debug("{} {} {}",
			               image.forward_matrix1[k][0],
			               image.forward_matrix1[k][1],
			               image.forward_matrix1[k][2]);
	}
	else
		spdlog::debug("forward_matrix2 not found!");

	if (true)//image.has_forward_matrix2)
	{
		spdlog::debug("forward_matrix2 found = ");
		for (size_t k = 0; k < 3; k++)
			spdlog::debug("{} {} {}",
			               image.forward_matrix2[k][0],
			               image.forward_matrix2[k][1],
			               image.forward_matrix2[k][2]);
	}
	else
		spdlog::debug("forward_matrix2 not found!");

	spdlog::debug("camera_calibration1 = ");
	for (size_t k = 0; k < 3; k++)
		spdlog::debug("{} {} {}",
		               image.camera_calibration1[k][0],
		               image.camera_calibration1[k][1],
		               image.camera_calibration1[k][2]);

	spdlog::debug("orientation = {}", image.orientation);

	spdlog::debug("camera_calibration2 = ");
	for (size_t k = 0; k < 3; k++)
		spdlog::debug("{} {} {}",
		               image.camera_calibration2[k][0],
		               image.camera_calibration2[k][1],
		               image.camera_calibration2[k][2]);

	if (image.has_analog_balance)
		spdlog::debug("analog_balance = {} , {} , {}",
		               image.analog_balance[0],
		               image.analog_balance[1],
		               image.analog_balance[2]);
	else
		spdlog::debug("analog_balance not found!");

	if (image.has_as_shot_neutral)
		spdlog::debug("as_shot_neutral = {} , {} , {}",
		               image.as_shot_neutral[0],
		               image.as_shot_neutral[1],
		               image.as_shot_neutral[2]);
	else
		spdlog::debug("shot_neutral not found!");
}


inline nanogui::Vector3f camera_to_Lab(const V3f & c, const M33f & camera_to_XYZ, const vector<float> & LUT)
{
    V3f xyz = c * camera_to_XYZ;

    for (int i = 0; i < 3; ++i)
        xyz[i] = LUT[::clamp((int) (xyz[i] * LUT.size()), 0, int(LUT.size()-1))];

    return nanogui::Vector3f(116.0f * xyz[1] - 16, 500.0f * (xyz[0] - xyz[1]), 200.0f * (xyz[1] - xyz[2]));
}

inline int bayer_color(int x, int y)
{
    const int bayer[2][2] = {{0,1},{1,2}};

    return bayer[y % 2][x % 2];
}

inline float clamp2(float value, float mn, float mx)
{
    if (mn > mx)
        std::swap(mn, mx);
    return ::clamp(value, mn, mx);
}

inline float clamp4(float value, float a, float b, float c, float d)
{
    float mn = min(a, b, c, d);
    float mx = max(a, b, c, d);
    return ::clamp(value, mn, mx);
}

inline float interp_green_h(const HDRImage &raw, int x, int y)
{
    float v = 0.50f * (raw(x - 1, y).g + raw(x + 1, y).g + raw(x, y).g) -
              0.25f * (raw(x - 2, y).g + raw(x + 2, y).g);
    // Don't extrapolate past the neighboring green values
    return clamp2(v, raw(x - 1, y).g, raw(x + 1, y).g);
}

inline float interp_green_v(const HDRImage &raw, int x, int y)
{
    float v = 0.50f * (raw(x, y - 1).g + raw(x, y + 1).g + raw(x, y).g) -
              0.25f * (raw(x, y - 2).g + raw(x, y + 2).g);
    // Don't extrapolate past the neighboring green values
    return clamp2(v, raw(x, y - 1).g, raw(x, y + 1).g);
}

inline float ghG(const Array2Df & G, int i, int j)
{
    return fabs(G(i-1,j) - G(i,j)) + fabs(G(i+1,j) - G(i,j));
}

inline float gvG(const Array2Df & G, int i, int j)
{
    return fabs(G(i,j-1) - G(i,j)) + fabs(G(i,j+1) - G(i,j));
}


void malvar_green(HDRImage &raw, int c, const nanogui::Vector2i & red_offset)
{
    // fill in half of the missing locations (R or B)
    parallel_for(2, raw.height()-2-red_offset.y(), 2, [&raw,c,&red_offset](int yy)
    {
        int y = yy + red_offset.y();
        for (int xx = 2; xx < raw.width()-2-red_offset.x(); xx += 2)
        {
            int x = xx + red_offset.x();
            float v = (4.f * raw(x,y)[c]
                         + 2.f * (raw(x, y-1)[1] + raw(x-1, y)[1] +
                                  raw(x, y+1)[1] + raw(x+1, y)[1])
                         - 1.f * (raw(x, y-2)[c] + raw(x-2, y)[c] +
                                  raw(x, y+2)[c] + raw(x+2, y)[c]))/8.f;
            raw(x,y)[1] = clamp4(v, raw(x, y-1)[1], raw(x-1, y)[1],
                                    raw(x, y+1)[1], raw(x+1, y)[1]);
        }
    });
}

void malvar_red_or_blue_at_green(HDRImage &raw, int c, const nanogui::Vector2i &red_offset, bool horizontal)
{
    int dx = (horizontal) ? 1 : 0;
    int dy = (horizontal) ? 0 : 1;
    // fill in half of the missing locations (R or B)
    parallel_for(2+red_offset.y(), raw.height()-2, 2, [&raw,c,&red_offset,dx,dy](int y)
    {
        for (int x = 2+red_offset.x(); x < raw.width()-2; x += 2)
        {
            raw(x,y)[c] = (5.f * raw(x,y)[1]
                         - 1.f * (raw(x-1,y-1)[1] + raw(x+1,y-1)[1] + raw(x+1,y+1)[1] + raw(x-1,y+1)[1] + raw(x-2,y)[1] + raw(x+2,y)[1])
                         + .5f * (raw(x,y-2)[1] + raw(x,y+2)[1])
                         + 4.f * (raw(x-dx,y-dy)[c] + raw(x+dx,y+dy)[c]))/8.f;
        }
    });
}

void malvar_red_or_blue(HDRImage &raw, int c1, int c2, const nanogui::Vector2i &red_offset)
{
    // fill in half of the missing locations (R or B)
    parallel_for(2+red_offset.y(), raw.height()-2, 2, [&raw,c1,c2,&red_offset](int y)
    {
        for (int x = 2+red_offset.x(); x < raw.width()-2; x += 2)
        {
            raw(x,y)[c1] = (6.f * raw(x,y)[c2]
                          + 2.f * (raw(x-1,y-1)[c1] + raw(x+1,y-1)[c1] + raw(x+1,y+1)[c1] + raw(x-1,y+1)[c1])
                          - 3/2.f * (raw(x,y-2)[c2] + raw(x,y+2)[c2] + raw(x-2,y)[c2] + raw(x+2,y)[c2]))/8.f;
        }
    });
}


// takes as input a raw image and returns a single-channel
// 2D image corresponding to the red or blue channel using simple interpolation
void bilinear_red_blue(HDRImage &raw, int c, const nanogui::Vector2i & red_offset)
{
    // diagonal interpolation
    parallel_for(red_offset.y() + 1, raw.height()-1, 2, [&raw,c,&red_offset](int y)
    {
        for (int x = red_offset.x() + 1; x < raw.width() - 1; x += 2)
            raw(x, y)[c] = 0.25f * (raw(x - 1, y - 1)[c] + raw(x + 1, y - 1)[c] +
                                    raw(x - 1, y + 1)[c] + raw(x + 1, y + 1)[c]);
    });

    // horizontal interpolation
    parallel_for(red_offset.y(), raw.height(), 2, [&raw,c,&red_offset](int y)
    {
        for (int x = red_offset.x() + 1; x < raw.width() - 1; x += 2)
            raw(x, y)[c] = 0.5f * (raw(x - 1, y)[c] + raw(x + 1, y)[c]);
    });

    // vertical interpolation
    parallel_for(red_offset.y() + 1, raw.height() - 1, 2, [&raw,c,&red_offset](int y)
    {
        for (int x = red_offset.x(); x < raw.width(); x += 2)
            raw(x, y)[c] = 0.5f * (raw(x, y - 1)[c] + raw(x, y + 1)[c]);
    });
}


// takes as input a raw image and returns a single-channel
// 2D image corresponding to the red or blue channel using green based interpolation
void green_based_red_or_blue(HDRImage &raw, int c, const nanogui::Vector2i &red_offset)
{
    // horizontal interpolation
    parallel_for(red_offset.y(), raw.height(), 2, [&raw,c,&red_offset](int y)
    {
        for (int x = red_offset.x() + 1; x < raw.width() - 1; x += 2)
            raw(x, y)[c] = std::max(0.f, 0.5f * (raw(x - 1, y)[c] + raw(x + 1, y)[c] -
                                                 raw(x - 1, y)[1] - raw(x + 1, y)[1]) + raw(x, y)[1]);
    });

    // vertical interpolation
    parallel_for(red_offset.y() + 1, raw.height() - 1, 2, [&raw,c,&red_offset](int y)
    {
        for (int x = red_offset.x(); x < raw.width(); x += 2)
            raw(x, y)[c] = std::max(0.f, 0.5f * (raw(x, y - 1)[c] + raw(x, y + 1)[c] -
                                                 raw(x, y - 1)[1] - raw(x, y + 1)[1]) + raw(x, y)[1]);
    });

    // diagonal interpolation
    parallel_for(red_offset.y() + 1, raw.height() - 1, 2, [&raw,c,&red_offset](int y)
    {
        for (int x = red_offset.x() + 1; x < raw.width() - 1; x += 2)
            raw(x, y)[c] = std::max(0.f, 0.25f * (raw(x - 1, y - 1)[c] + raw(x + 1, y - 1)[c] +
                                                  raw(x - 1, y + 1)[c] + raw(x + 1, y + 1)[c] -
                                                  raw(x - 1, y - 1)[1] - raw(x + 1, y - 1)[1] -
                                                  raw(x - 1, y + 1)[1] - raw(x + 1, y + 1)[1]) + raw(x, y)[1]);
    });
}

} // namespace