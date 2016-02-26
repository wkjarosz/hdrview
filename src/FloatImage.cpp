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

// #define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "pfm.h"

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
    // try PNG, JPG, HDR, etc files first
    int n, w, h;
    float * data = stbi_loadf(filename.c_str(), &w, &h, &n, 4);
    if (data)
    {
        resize(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                operator()(x,y) = Color4(data[4*(x + y*w) + 0],
                                       data[4*(x + y*w) + 1],
                                       data[4*(x + y*w) + 2],
                                       data[4*(x + y*w) + 3]);
        return true;
    }
    std::cout << "HI" << endl;
    // then try pfm
    float * pfm_data = nullptr;
    try
    {
        int w = 0, h = 0;
        pfm_data = load_pfm(filename.c_str(), &w, &h, &n);
        if (pfm_data)
        {
            if (n == 3)
            {
                resize(w, h);

                // convert 3-channel pfm data to 4-channel internal representation
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        operator()(x,y) = Color4(pfm_data[3*(x + y*w) + 0],
                                               pfm_data[3*(x + y*w) + 1],
                                               pfm_data[3*(x + y*w) + 2],
                                               1.0f);

                delete [] pfm_data;
                return true;
            }
            else
                throw runtime_error("Unsupported number of channels in PFM");
        }
    }
    catch (const exception &e)
    {
        delete [] pfm_data;
        resize(0,0);
        cerr << e.what() << endl;
    }

    std::cout << "HI2" << endl;
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
        cerr << "ERROR: Unable to read image file \"" << filename << "\": " << e.what() << endl;
        resize(0,0);
        return false;
    }

    return true;
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

    if (extension == "hdr")
        return stbi_write_hdr(filename.c_str(), width(), height(), 4, (const float *) data());
    else if (extension == "pfm")
        return write_pfm(filename.c_str(), width(), height(), 4, (const float *) data());
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
                    Color4 c = operator()(x,y);
                    p.r = c[0];
                    p.g = c[1];
                    p.b = c[2];
                    p.a = c[3];
                }
            
                file.setFrameBuffer(&pixels[0][0], 1, 0);
                file.writePixels(1);
            }
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
        float invGamma = 1.0f/gamma;
        for (int y = 0; y < height(); ++y)
            for (int x = 0; x < width(); ++x)
            {
                Color4 c = operator()(x,y);
                c *= gain;
                if (sRGB)
                   c = Color4(toSRGB(c[0]), toSRGB(c[1]), toSRGB(c[2]), c[3]);
                else
                   c = Color4(::pow(c[0], invGamma), ::pow(c[1], invGamma), ::pow(c[2], invGamma), c[3]);

                if (dither)
                {
                    int xmod = x % 256;
                    int ymod = y % 256;
                    float ditherValue = (dither_matrix256[xmod + ymod * 256]/65536.0f - 0.5f)/255.0f;
                    c += Color4(Color3(ditherValue), 0.0f);
                }

                // convert to [0-255] range
                c = (c * 255.0f).max(0.0f).min(255.0f);
                
                data[3*x + 3*y*width() + 0] = (int) c[0];
                data[3*x + 3*y*width() + 1] = (int) c[1];
                data[3*x + 3*y*width() + 2] = (int) c[2];
            }

        if (extension == "png")
            return stbi_write_png(filename.c_str(), width(), height(),
                                    3, &data[0], sizeof(unsigned char)*width()*3);
        else if (extension == "bmp")
            return stbi_write_bmp(filename.c_str(), width(), height(), 3, &data[0]);
        else if (extension == "tga")
            return stbi_write_tga(filename.c_str(), width(), height(), 3, &data[0]);
        else
            throw runtime_error("Could not determine desired file type from extension.");
    }
}