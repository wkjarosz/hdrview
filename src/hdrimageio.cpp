//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrimage.h"
#include "dithermatrix256.h"    // for dither_matrix256
#include <ImfArray.h>            // for Array2D
#include <ImfRgbaFile.h>         // for RgbaInputFile, RgbaOutputFile
#include <ImathBox.h>            // for Box2i
#include <ImfTestFile.h>         // for isOpenExrFile
#include <ImathVec.h>            // for Vec2
#include <ImfRgba.h>             // for Rgba, RgbaChannels::WRITE_RGBA
#include <ctype.h>               // for tolower
#include <algorithm>             // for transform
#include <stdexcept>             // for runtime_error, out_of_range
#include <string>                // for allocator, operator==, basic_string
#include <vector>                // for vector
#include "common.h"              // for lerp, mod, clamp, getExtension
#include "colorspace.h"
#include "parallelfor.h"
#include "timer.h"
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

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning (pop)
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"     // for stbi_write_bmp, stbi_write_hdr, stbi...

#include "pfm.h"
#include "ppm.h"


using namespace nanogui;
using namespace std;

// local functions
namespace
{

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
			spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed()/1000.f));

			stbi_image_free(float_data);
			return true;
		}
		else
			errors = stbi_failure_reason();
	}


    // then try pfm
	if (is_pfm_image(filename.c_str()))
    {
	    float * float_data = 0;
	    try
	    {
		    w = 0;
		    h = 0;

		    if ((float_data = load_pfm_image(filename.c_str(), &w, &h, &n)))
		    {
			    if (n == 3)
			    {
				    resize(w, h);

				    Timer timer;
				    // convert 3-channel pfm data to 4-channel internal representation
				    copyPixelsFromArray(*this, float_data, w, h, 3, false);
				    spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed() / 1000.f));

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
		    errors = e.what();
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

		    spdlog::debug("Reading EXR image took: {} seconds.", (timer.lap() / 1000.f));

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

		    spdlog::debug("Copying EXR image data took: {} seconds.", (timer.lap() / 1000.f));
		    return true;
	    }
	    catch (const exception &e)
	    {
		    resize(0, 0);
		    errors = e.what();
	    }
    }

	try
	{
		load_dng(filename);

		return true;
	}
	catch (const exception &e)
	{
		resize(0,0);
		// only report errors to the user if the extension was actually dng
		if (extension == "dng")
			errors = e.what();
	}

    spdlog::error("ERROR: Unable to read image file \"{}\":\n\t{}\n", filename, errors);

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
                imgCopy = imgCopy.apply_function([](const Color4 & c){return LinearToSRGB(c);});
            else if (gamma != 1.0f)
                imgCopy = imgCopy.apply_function([&gammaC](const Color4 & c){return pow(c, gammaC);});
        }
    }

    if (extension == "hdr")
        return stbi_write_hdr(filename.c_str(), width(), height(), 4, (const float *) img->data()) != 0;
    else if (extension == "pfm")
        return write_pfm_image(filename.c_str(), width(), height(), 4, (const float *) img->data()) != 0;
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
            spdlog::debug("Copying pixel data took: {} seconds.", (timer.lap()/1000.f));

            file.setFrameBuffer(&pixels[0][0], 1, width());
            file.writePixels(height());

            spdlog::debug("Writing EXR image took: {} seconds.", (timer.lap()/1000.f));
			return true;
        }
        catch (const exception &e)
        {
            spdlog::error("ERROR: Unable to write image file \"{}\": {}", filename, e.what());
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
        spdlog::debug("Tonemapping to 8bit took: {} seconds.", (timer.elapsed()/1000.f));

        if (extension == "ppm")
            return write_ppm_image(filename.c_str(), width(), height(), 3, &data[0]);
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