//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "HDRImage.h"
#include "DitherMatrix256.h"    // for dither_matrix256
#include <ImfArray.h>            // for Array2D
#include <ImfRgbaFile.h>         // for RgbaInputFile, RgbaOutputFile
#include <ImathBox.h>            // for Box2i
#include <ImfTestFile.h>         // for isOpenExrFile
#include <ImathVec.h>            // for Vec2
#include <ImfRgba.h>             // for Rgba, RgbaChannels::WRITE_RGBA
#include <ctype.h>               // for tolower
#include <half.h>                // for half
#include <stdlib.h>              // for abs
#include <algorithm>             // for nth_element, transform
#include <cmath>                 // for floor, pow, exp, ceil, round, sqrt
#include <exception>             // for exception
#include <functional>            // for pointer_to_unary_function, function
#include <stdexcept>             // for runtime_error, out_of_range
#include <string>                // for allocator, operator==, basic_string
#include <vector>                // for vector
#include "Common.h"              // for lerp, mod, clamp, getExtension
#include "Colorspace.h"
#include "ParallelFor.h"
#include "Timer.h"
#include <Eigen/Dense>
#include <spdlog/spdlog.h>

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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"     // for stbi_write_bmp, stbi_write_hdr, stbi...

#include "PFM.h"
#include "PPM.h"


using namespace Eigen;
using namespace std;
// using std::vector;
// using std::runtime_error;
// using std::exception;
// using std::string;
// using std::make_shared;
// using std::shared_ptr;
// using std::to_string;
// using std::invalid_argument;

// local functions
namespace
{

inline unsigned short endianSwap(unsigned short val);
void decode12BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian);
void decode14BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian);
void decode16BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian);
void printImageInfo(const tinydng::DNGImage & image);
HDRImage develop(vector<float> & raw,
                 const tinydng::DNGImage & param1,
                 const tinydng::DNGImage & param2);
void copyPixelsFromArray(HDRImage & img, float * data, int w, int h, int n, bool convertToLinear)
{
	if (n != 3 && n != 4)
		throw runtime_error("Only 3- and 4-channel images are supported.");

	// for every pixel in the image
	parallel_for(0, h, [&img,w,n,data,convertToLinear](int y)
	{
		for (int x = 0; x < w; ++x)
		{
			Color4 c(data[n * (x + y * w) + 0],
			         data[n * (x + y * w) + 1],
			         data[n * (x + y * w) + 2],
			         (n == 3) ? 1.f : data[4 * (x + y * w) + 3]);
			img(x, y) = convertToLinear ? SRGBToLinear(c) : c;
		}
	});
}

bool isSTBImage(const string & filename)
{
	FILE *f = stbi__fopen(filename.c_str(), "rb");
	if (!f)
		return false;

	stbi__context s;
	stbi__start_file(&s,f);

	// try stb library first
	if (stbi__jpeg_test(&s) ||
		stbi__png_test(&s) ||
		stbi__bmp_test(&s) ||
		stbi__gif_test(&s) ||
		stbi__psd_test(&s) ||
		stbi__pic_test(&s) ||
		stbi__pnm_test(&s) ||
		stbi__hdr_test(&s) ||
		stbi__tga_test(&s))
	{
		fclose(f);
		return true;
	}

	fclose(f);
	return false;
}

} // namespace


bool HDRImage::load(const string & filename)
{
	auto console = spdlog::get("console");
    string errors;
	string extension = getExtension(filename);
	transform(extension.begin(),
	          extension.end(),
	          extension.begin(),
	          ::tolower);

    int n, w, h;

	// try stb library first
	if (isSTBImage(filename))
	{
		// stbi doesn't do proper srgb, but uses gamma=2.2 instead, so override it.
		// we'll do our own srgb correction
		stbi_ldr_to_hdr_scale(1.0f);
		stbi_ldr_to_hdr_gamma(1.0f);

		float * float_data = stbi_loadf(filename.c_str(), &w, &h, &n, 4);
		if (float_data)
		{
			resize(w, h);
			bool convertToLinear = !stbi_is_hdr(filename.c_str());
			Timer timer;
			copyPixelsFromArray(*this, float_data, w, h, 4, convertToLinear);
			console->debug("Copying image data took: {} seconds.", (timer.elapsed()/1000.f));

			stbi_image_free(float_data);
			return true;
		}
		else
		{
			errors += string("\t") + stbi_failure_reason() + "\n";
		}
	}


    // then try pfm
	if (isPFMImage(filename.c_str()))
    {
	    float * float_data = 0;
	    try
	    {
		    w = 0;
		    h = 0;

		    if ((float_data = loadPFMImage(filename.c_str(), &w, &h, &n)))
		    {
			    if (n == 3)
			    {
				    resize(w, h);

				    Timer timer;
				    // convert 3-channel pfm data to 4-channel internal representation
				    copyPixelsFromArray(*this, float_data, w, h, 3, false);
				    console->debug("Copying image data took: {} seconds.", (timer.elapsed() / 1000.f));

				    delete [] float_data;
				    return true;
			    }
			    else
				    throw runtime_error("Only 3-channel PFMs are currently supported.");
			    return true;
		    }
		    else
			    throw runtime_error("Could not load PFM image.");
	    }
	    catch (const exception &e)
	    {
		    delete [] float_data;
		    resize(0, 0);
		    errors += string("\t") + e.what() + "\n";
	    }
    }

    // next try exrs
	if (Imf::isOpenExrFile(filename.c_str()))
    {
	    try
	    {
		    // FIXME: the threading below seems to cause issues, but shouldn't.
		    // turning off for now
		    Imf::setGlobalThreadCount(std::thread::hardware_concurrency());
		    Timer timer;

		    Imf::RgbaInputFile file(filename.c_str());
		    Imath::Box2i dw = file.dataWindow();

		    w = dw.max.x - dw.min.x + 1;
		    h = dw.max.y - dw.min.y + 1;

		    Imf::Array2D<Imf::Rgba> pixels(h, w);

		    file.setFrameBuffer(&pixels[0][0] - dw.min.x - dw.min.y * w, 1, w);
		    file.readPixels(dw.min.y, dw.max.y);

		    console->debug("Reading EXR image took: {} seconds.", (timer.lap() / 1000.f));

		    resize(w, h);

		    // copy pixels over to the Image
		    parallel_for(0, h, [this, w, &pixels](int y)
		    {
			    for (int x = 0; x < w; ++x)
			    {
				    const Imf::Rgba &p = pixels[y][x];
				    (*this)(x, y) = Color4(p.r, p.g, p.b, p.a);
			    }
		    });

		    console->debug("Copying EXR image data took: {} seconds.", (timer.lap() / 1000.f));
		    return true;
	    }
	    catch (const exception &e)
	    {
		    resize(0, 0);
		    errors += string("\t") + e.what() + "\n";
	    }
    }

	try
	{
		vector<tinydng::DNGImage> images;
		{
			std::string err;
			vector<tinydng::FieldInfo> customFields;
			bool ret = tinydng::LoadDNG(filename.c_str(), customFields, &images, &err);

			if (ret == false)
				throw runtime_error("Failed to load DNG. " + err);
		}

		// DNG files sometimes only store the orientation in one of the images,
		// instead of all of them. find any set value and save it
		int orientation = 0;
		for (size_t i = 0; i < images.size(); i++)
		{
			console->debug("Image [{}] size = {} x {}.", i, images[i].width, images[i].height);
			console->debug("Image [{}] orientation = {}", i, images[i].orientation);
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


		console->debug("\nLargest image within DNG:");
		printImageInfo(image);
		console->debug("\nLast image within DNG:");
		printImageInfo(images.back());

		console->debug("Loading image [{}].", imageIndex);

		w = image.width;
		h = image.height;

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

		float invScale = 1.0f / static_cast<float>((1 << image.bits_per_sample));
		if (spp == 3)
		{
			console->debug("Decoding a 3 sample-per-pixel DNG image.");
			// normalize
			parallel_for(0, hdr.size(), [&hdr,invScale](int i)
			{
				hdr[i] *= invScale;
			});

			// Create color image & normalize intensity.
			resize(w, h);

			Timer timer;
			// normalize
			parallel_for(0, h, [this,w,invScale,&hdr](int y)
			{
				for (int x = 0; x < w; ++x)
				{
					int index = 3 * y * w + x;
					(*this)(x, y) = Color4(hdr[index] * invScale + 0,
					                       hdr[index] * invScale + 1,
					                       hdr[index] * invScale + 2, 1.0f);
				}
			});
			console->debug("Copying image data took: {} seconds.", (timer.elapsed()/1000.f));
		}
		else if (spp == 1)
		{
			// Create grayscale image & normalize intensity.
			console->debug("Decoding a 1 sample-per-pixel DNG image.");
			Timer timer;
			*this = develop(hdr, image, images.back());
			console->debug("Copying image data took: {} seconds.", (timer.elapsed()/1000.f));
		}
		else
			throw runtime_error("Error loading DNG: Unsupported samples per pixel: " + to_string(spp));


		int startRow = ::clamp(image.active_area[1], 0, w);
		int endRow = ::clamp(image.active_area[3], 0, w);
		int startCol = ::clamp(image.active_area[0], 0, h);
		int endCol = ::clamp(image.active_area[2], 0, h);

		*this = block(startRow, startCol,
		              endRow-startRow,
		              endCol-startCol).eval();

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
			case ORIENTATION_TOPRIGHT: *this = flippedHorizontal(); break;
			case ORIENTATION_BOTRIGHT: *this = flippedVertical().flippedHorizontal(); break;
			case ORIENTATION_BOTLEFT : *this = flippedVertical(); break;
			case ORIENTATION_LEFTTOP : *this = rotated90CCW().flippedVertical(); break;
			case ORIENTATION_RIGHTTOP: *this = rotated90CW(); break;
			case ORIENTATION_RIGHTBOT: *this = rotated90CW().flippedVertical(); break;
			case ORIENTATION_LEFTBOT : *this = rotated90CCW(); break;
			default: break;// none (0), or ORIENTATION_TOPLEFT
		}

		return true;
	}
	catch (const exception &e)
	{
		resize(0,0);
		// only report errors to the user if the extension was actually dng
		if (extension == "dng")
			errors += string("\t") + e.what() + "\n";
	}

    console->error("ERROR: Unable to read image file \"{}\":\n{}", filename, errors);

    return false;
}


shared_ptr<HDRImage> load_image(const string & filename)
{
	shared_ptr<HDRImage> ret = make_shared<HDRImage>();
	if (ret->load(filename))
		return ret;
	return nullptr;
}


bool HDRImage::save(const string & filename,
                    float gain, float gamma,
                    bool sRGB, bool dither) const
{
	auto console = spdlog::get("console");
    string extension = getExtension(filename);

    transform(extension.begin(),
              extension.end(),
              extension.begin(),
              ::tolower);

    auto img = this;
    HDRImage imgCopy;

    bool hdrFormat = (extension == "hdr") || (extension == "pfm") || (extension == "exr");

    // if we need to tonemap, then modify a copy of the image data
    if (gain != 1.0f || sRGB || gamma != 1.0f)
    {
        Color4 gainC = Color4(gain, gain, gain, 1.0f);
        Color4 gammaC = Color4(1.0f / gamma, 1.0f / gamma, 1.0f / gamma, 1.0f);

        imgCopy = *this;
        img = &imgCopy;

        if (gain != 1.0f)
            imgCopy *= gainC;

        // only do gamma or sRGB tonemapping if we are saving to an LDR format
        if (!hdrFormat)
        {
            if (sRGB)
                imgCopy = imgCopy.unaryExpr([](const Color4 & c) {return LinearToSRGB(c);});
            else if (gamma != 1.0f)
                imgCopy = imgCopy.pow(gammaC);
        }
    }

    if (extension == "hdr")
        return stbi_write_hdr(filename.c_str(), width(), height(), 4, (const float *) img->data()) != 0;
    else if (extension == "pfm")
        return writePFMImage(filename.c_str(), width(), height(), 4, (const float *) img->data()) != 0;
    else if (extension == "exr")
    {
        try
        {
            Imf::setGlobalThreadCount(std::thread::hardware_concurrency());
            Imf::RgbaOutputFile file(filename.c_str(), width(), height(), Imf::WRITE_RGBA);
            Imf::Array2D<Imf::Rgba> pixels(height(), width());

            Timer timer;
            // copy image data over to Rgba pixels
            parallel_for(0, height(), [this,img,&pixels](int y)
            {
                for (int x = 0; x < width(); ++x)
                {
                    Imf::Rgba &p = pixels[y][x];
                    Color4 c = (*img)(x, y);
                    p.r = c[0];
                    p.g = c[1];
                    p.b = c[2];
                    p.a = c[3];
                }
            });
            console->debug("Copying pixel data took: {} seconds.", (timer.lap()/1000.f));

            file.setFrameBuffer(&pixels[0][0], 1, width());
            file.writePixels(height());

            console->debug("Writing EXR image took: {} seconds.", (timer.lap()/1000.f));
			return true;
        }
        catch (const exception &e)
        {
            console->error("ERROR: Unable to write image file \"{}\": {}", filename, e.what());
            return false;
        }
    }
    else
    {
        // convert floating-point image to 8-bit per channel with dithering
        vector<unsigned char> data(size()*3, 0);

        Timer timer;
        // convert 3-channel pfm data to 4-channel internal representation
        parallel_for(0, height(), [this,img,&data,dither](int y)
        {
            for (int x = 0; x < width(); ++x)
            {
                Color4 c = (*img)(x, y);
                if (dither)
                {
                    int xmod = x % 256;
                    int ymod = y % 256;
                    float ditherValue = (dither_matrix256[xmod + ymod * 256] / 65536.0f - 0.5f) / 255.0f;
                    c += Color4(Color3(ditherValue), 0.0f);
                }

                // convert to [0-255] range
                c = (c * 255.0f).max(0.0f).min(255.0f);

                data[3 * x + 3 * y * width() + 0] = (unsigned char) c[0];
                data[3 * x + 3 * y * width() + 1] = (unsigned char) c[1];
                data[3 * x + 3 * y * width() + 2] = (unsigned char) c[2];
            }
        });
        console->debug("Tonemapping to 8bit took: {} seconds.", (timer.elapsed()/1000.f));

        if (extension == "ppm")
            return writePPMImage(filename.c_str(), width(), height(), 3, &data[0]);
        else if (extension == "png")
            return stbi_write_png(filename.c_str(), width(), height(),
                                  3, &data[0], sizeof(unsigned char)*width()*3) != 0;
        else if (extension == "bmp")
            return stbi_write_bmp(filename.c_str(), width(), height(), 3, &data[0]) != 0;
        else if (extension == "tga")
            return stbi_write_tga(filename.c_str(), width(), height(), 3, &data[0]) != 0;
        else if (extension == "jpg" || extension == "jpeg")
            return stbi_write_jpg(filename.c_str(), width(), height(), 3, &data[0], 100) != 0;
        else
            throw invalid_argument("Could not determine desired file type from extension.");
    }
}


// local functions
namespace
{


// Taken from http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
const Matrix3f XYZD65TosRGB(
	(Matrix3f() << 3.2406f, -1.5372f, -0.4986f,
		-0.9689f,  1.8758f,  0.0415f,
		0.0557f, -0.2040f,  1.0570f).finished());

const Matrix3f XYZD50ToXYZD65(
	(Matrix3f() << 0.9555766f, -0.0230393f, 0.0631636f,
		-0.0282895f,  1.0099416f, 0.0210077f,
		0.0122982f, -0.0204830f, 1.3299098f).finished());

// Taken from http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
const Matrix3f XYZD50TosRGB(
	(Matrix3f() << 3.2404542f, -1.5371385f, -0.4985314f,
		-0.9692660f,  1.8760108f,  0.0415560f,
		0.0556434f, -0.2040259f,  1.0572252).finished());

Matrix3f computeCameraToXYZD50(const tinydng::DNGImage &param)
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
	if (false)//param.has_forward_matrix2)
	{
		auto FM((Matrix3f() << param.forward_matrix2[0][0], param.forward_matrix2[0][1], param.forward_matrix2[0][2],
			                   param.forward_matrix2[1][0], param.forward_matrix2[1][1], param.forward_matrix2[1][2],
							   param.forward_matrix2[2][0], param.forward_matrix2[2][1], param.forward_matrix2[2][2]).finished());
		auto CC((Matrix3f() << param.camera_calibration2[0][0], param.camera_calibration2[0][1], param.camera_calibration2[0][2],
							   param.camera_calibration2[1][0], param.camera_calibration2[1][1], param.camera_calibration2[1][2],
							   param.camera_calibration2[2][0], param.camera_calibration2[2][1], param.camera_calibration2[2][2]).finished());
		auto AB = Vector3f(param.analog_balance[0], param.analog_balance[1], param.analog_balance[2]).asDiagonal();

		Vector3f CameraNeutral(param.as_shot_neutral[0],
		                       param.as_shot_neutral[1],
		                       param.as_shot_neutral[2]);
		Vector3f ReferenceNeutral = (AB * CC).inverse() * CameraNeutral;
		auto D = (ReferenceNeutral.asDiagonal()).inverse();
		auto CameraToXYZ = FM * D * (AB * CC).inverse();

		return CameraToXYZ;
	}
	else
	{
		auto CM((Matrix3f() << param.color_matrix2[0][0], param.color_matrix2[0][1], param.color_matrix2[0][2],
			                   param.color_matrix2[1][0], param.color_matrix2[1][1], param.color_matrix2[1][2],
				               param.color_matrix2[2][0], param.color_matrix2[2][1], param.color_matrix2[2][2]).finished());

		auto CameraToXYZ = CM.inverse();

		return CameraToXYZ;

	}
}


HDRImage develop(vector<float> & raw,
                 const tinydng::DNGImage & param1,
                 const tinydng::DNGImage & param2)
{
	Timer timer;

	int width = param1.width;
	int height = param1.height;
	int blackLevel = param1.black_level[0];
	int whiteLevel = param1.white_level[0];
	Vector2i redOffset(param1.active_area[1] % 2, param1.active_area[0] % 2);

	HDRImage developed(width, height);

	Matrix3f CameraToXYZD50 = computeCameraToXYZD50(param2);
	Matrix3f CameraTosRGB = XYZD50TosRGB * CameraToXYZD50;

	// Chapter 5 of DNG spec
	// Map raw values to linear reference values (i.e. adjust for black and white level)
	//
	// we also apply white balance before demosaicing here because it increases the
	// correlation between the color channels and reduces artifacts
	Vector3f wb(param2.as_shot_neutral[0], param2.as_shot_neutral[1], param2.as_shot_neutral[2]);
	const float invScale = 1.0f / (whiteLevel - blackLevel);
	parallel_for(0, developed.height(), [&developed,&raw,blackLevel,invScale,&wb](int y)
	{
		for (int x = 0; x < developed.width(); x++)
		{
			float v = ::clamp((raw[y * developed.width() + x] - blackLevel)*invScale, 0.f, 1.f);
			Vector3f rgb = Vector3f(v,v,v);
			rgb = rgb.cwiseQuotient(wb);
			developed(x,y) = Color4(rgb(0),rgb(1),rgb(2),1.f);
		}
	});

	// demosaic
//	developed.demosaicLinear(redOffset);
//	developed.demosaicGreenGuidedLinear(redOffset);
//	developed.demosaicMalvar(redOffset);
	developed.demosaicAHD(redOffset, XYZD50ToXYZD65 * CameraToXYZD50);

	// color correction
	// also undo the white balance since the color correction matrix already includes it
	parallel_for(0, developed.height(), [&developed,&CameraTosRGB,&wb](int y)
	{
		for (int x = 0; x < developed.width(); x++)
		{
			Vector3f rgb(developed(x,y).r, developed(x,y).g, developed(x,y).b);
			rgb = rgb.cwiseProduct(wb);
			Vector3f sRGB = CameraTosRGB * rgb;
			developed(x,y) = Color4(sRGB.x(),sRGB.y(),sRGB.z(),1.f);
		}
	});

	spdlog::get("console")->debug("Developing DNG image took {} seconds.", (timer.elapsed()/1000.f));
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

	spdlog::get("console")->debug("decode12BitToFloat took: {} seconds.", (timer.lap() / 1000.f));
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

	spdlog::get("console")->debug("decode14BitToFloat took: {} seconds.", (timer.lap() / 1000.f));
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

	spdlog::get("console")->debug("decode16BitToFloat took: {} seconds.", (timer.lap() / 1000.f));
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
	auto console = spdlog::get("console");
	console->debug("width = {}.", image.width);
	console->debug("width = {}.", image.width);
	console->debug("height = {}.", image.height);
	console->debug("bits per pixel = {}.", image.bits_per_sample);
	console->debug("bits per pixel(original) = {}", image.bits_per_sample_original);
	console->debug("samples per pixel = {}", image.samples_per_pixel);
	console->debug("sample format = {}", image.sample_format);

	console->debug("version = {}", image.version);

	for (int s = 0; s < image.samples_per_pixel; s++)
	{
		console->debug("white_level[{}] = {}", s, image.white_level[s]);
		console->debug("black_level[{}] = {}", s, image.black_level[s]);
	}

	console->debug("tile_width = {}", image.tile_width);
	console->debug("tile_length = {}", image.tile_length);
	console->debug("tile_offset = {}", image.tile_offset);
	console->debug("tile_offset = {}", image.tile_offset);

	console->debug("cfa_layout = {}", image.cfa_layout);
	console->debug("cfa_plane_color = {}{}{}{}",
	               get_colorname(image.cfa_plane_color[0]),
	               get_colorname(image.cfa_plane_color[1]),
	               get_colorname(image.cfa_plane_color[2]),
	               get_colorname(image.cfa_plane_color[3]));
	console->debug("cfa_pattern[2][2] = \n {}, {},\n {}, {}",
	               image.cfa_pattern[0][0],
	               image.cfa_pattern[0][1],
	               image.cfa_pattern[1][0],
	               image.cfa_pattern[1][1]);

	console->debug("active_area = \n {}, {},\n {}, {}",
	               image.active_area[0],
	               image.active_area[1],
	               image.active_area[2],
	               image.active_area[3]);

	console->debug("calibration_illuminant1 = {}", image.calibration_illuminant1);
	console->debug("calibration_illuminant2 = {}", image.calibration_illuminant2);

	console->debug("color_matrix1 = ");
	for (size_t k = 0; k < 3; k++)
		console->debug("{} {} {}",
		               image.color_matrix1[k][0],
		               image.color_matrix1[k][1],
		               image.color_matrix1[k][2]);

	console->debug("color_matrix2 = ");
	for (size_t k = 0; k < 3; k++)
		console->debug("{} {} {}",
		               image.color_matrix2[k][0],
		               image.color_matrix2[k][1],
		               image.color_matrix2[k][2]);

	if (true)//image.has_forward_matrix2)
	{
		console->debug("forward_matrix1 found = ");
		for (size_t k = 0; k < 3; k++)
			console->debug("{} {} {}",
			               image.forward_matrix1[k][0],
			               image.forward_matrix1[k][1],
			               image.forward_matrix1[k][2]);
	}
	else
		console->debug("forward_matrix2 not found!");

	if (true)//image.has_forward_matrix2)
	{
		console->debug("forward_matrix2 found = ");
		for (size_t k = 0; k < 3; k++)
			console->debug("{} {} {}",
			               image.forward_matrix2[k][0],
			               image.forward_matrix2[k][1],
			               image.forward_matrix2[k][2]);
	}
	else
		console->debug("forward_matrix2 not found!");

	console->debug("camera_calibration1 = ");
	for (size_t k = 0; k < 3; k++)
		console->debug("{} {} {}",
		               image.camera_calibration1[k][0],
		               image.camera_calibration1[k][1],
		               image.camera_calibration1[k][2]);

	console->debug("orientation = {}", image.orientation);

	console->debug("camera_calibration2 = ");
	for (size_t k = 0; k < 3; k++)
		console->debug("{} {} {}",
		               image.camera_calibration2[k][0],
		               image.camera_calibration2[k][1],
		               image.camera_calibration2[k][2]);

	if (image.has_analog_balance)
		console->debug("analog_balance = {} , {} , {}",
		               image.analog_balance[0],
		               image.analog_balance[1],
		               image.analog_balance[2]);
	else
		console->debug("analog_balance not found!");

	if (image.has_as_shot_neutral)
		console->debug("as_shot_neutral = {} , {} , {}",
		               image.as_shot_neutral[0],
		               image.as_shot_neutral[1],
		               image.as_shot_neutral[2]);
	else
		console->debug("shot_neutral not found!");
}

} // namespace