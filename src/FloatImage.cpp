/*!
    \file FloatImage.cpp
    \brief Contains the implementation of a floating-point RGBA image class
    \author Wojciech Jarosz
*/
#include "FloatImage.h"
#include "dither-matrix256.h"
#include <math.h>
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

// local functions
namespace
{

string getFileExtension(const string& filename)
{
    if (filename.find_last_of(".") != string::npos)
        return filename.substr(filename.find_last_of(".")+1);
    return "";
}

float toSRGB(float value)
{
    if (value < 0.0031308f)
       return 12.92f * value;
    return 1.055f * pow(value, 0.41666f) - 0.055f;
}

} // namespace


bool FloatImage::load(const string & filename)
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

bool FloatImage::save(const string & filename,
                      float gain, float gamma,
                      bool sRGB, bool dither)
{
    string extension = getFileExtension(filename);

    transform(extension.begin(),
              extension.end(),
              extension.begin(),
              ::tolower);

    FloatImage* img = this;
    FloatImage imgCopy;

    // if we need to tonemap, then modify a copy of the image data
    if (gain != 1.0f || sRGB || gamma != 1.0f)
    {
        imgCopy = *this;
        img = &imgCopy;

        if (gain != 1.0f)
            imgCopy *= gain;

        for (int y = 0; y < height(); ++y)
            for (int x = 0; x < width(); ++x)
                imgCopy(x,y) = sRGB ?
                                Color4(toSRGB(imgCopy(x,y)[0]),
                                       toSRGB(imgCopy(x,y)[1]),
                                       toSRGB(imgCopy(x,y)[2]),
                                       imgCopy(x,y)[3]) :
                                Color4(powf(imgCopy(x,y)[0], 1.0f/gamma),
                                       powf(imgCopy(x,y)[1], 1.0f/gamma),
                                       powf(imgCopy(x,y)[2], 1.0f/gamma),
                                       imgCopy(x,y)[3]);
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
