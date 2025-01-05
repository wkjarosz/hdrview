//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "common.h" // for lerp, mod, clamp, getExtension
#include "image.h"
#include "texture.h"
#include "timer.h"
#include <fstream>
#include <stdexcept> // for runtime_error, out_of_range

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>

#include "imageio/exr.h"
#include "imageio/pfm.h"
#include "imageio/stb.h"
#include "imageio/uhdr.h"

using namespace std;

vector<ImagePtr> Image::load(istream &is, const string &filename)
{
    spdlog::info("Loading from file: {}", filename);
    Timer timer;
    try
    {
        vector<ImagePtr> images;

        if (is_exr_image(is, filename))
        {
            spdlog::info("Detected EXR image.");
            images = load_exr_image(is, filename);
        }
        else if (is_uhdr_image(is))
        {
            spdlog::info("Detected UltraHDR JPEG image. Loading via libultrahdr.");
            images = load_uhdr_image(is, filename);
        }
        else if (is_stb_image(is))
        {
            spdlog::info("Detected stb-compatible image. Loading via stb_image.");
            images = load_stb_image(is, filename);
        }
        else if (is_pfm_image(is))
        {
            spdlog::info("Detected PFM image.");
            images = load_pfm_image(is, filename);
        }
        else
            throw invalid_argument("This doesn't seem to be a supported image file.");

        for (auto i : images)
        {
            i->finalize();
            i->filename   = filename;
            i->short_name = i->file_and_partname();
            spdlog::info("Loaded image in {:f} seconds:\n{:s}", timer.elapsed() / 1000.f, i->to_string());
        }
        return images;
    }
    catch (const exception &e)
    {
        spdlog::error("Unable to read image file \"{}\":\n\t{}", filename, e.what());
    }
    return {};
}

vector<ImagePtr> Image::load(const string &filename)
{
    std::ifstream is{filename, std::ios_base::binary};
    return load(is, filename);
}

bool Image::save(ostream &os, const string &filename, float gain, float gamma, bool sRGB, bool dither) const
{
    return save_stb_image(*this, os, filename, gain, gamma, sRGB, dither);

    // static const auto ostream_write_func = [](void *context, void *data, int size)
    // { reinterpret_cast<ostream *>(context)->write(reinterpret_cast<char *>(data), size); };

    // string extension = to_lower(get_extension(filename));

    // auto img = this;
    // HDRImage img_copy;

    // bool hdr_format = (extension == "hdr") || (extension == "pfm") || (extension == "exr");

    // // if we need to tonemap, then modify a copy of the image data
    // if (gain != 1.0f || sRGB || gamma != 1.0f)
    // {
    //     Color4 gainC  = Color4(gain, gain, gain, 1.0f);
    //     Color4 gammaC = Color4(1.0f / gamma, 1.0f / gamma, 1.0f / gamma, 1.0f);

    //     img_copy = *this;
    //     img      = &img_copy;

    //     if (gain != 1.0f)
    //         img_copy *= gainC;

    //     // only do gamma or sRGB tonemapping if we are saving to an LDR format
    //     if (!hdr_format)
    //     {
    //         if (sRGB)
    //             img_copy = img_copy.apply_function([](const Color4 &c) { return LinearToSRGB(c); });
    //         else if (gamma != 1.0f)
    //             img_copy = img_copy.apply_function([&gammaC](const Color4 &c) { return pow(c, gammaC); });
    //     }
    // }

    // if (extension == "hdr")
    //     return stbi_write_hdr(filename.c_str(), width(), height(), 4, (const float *)img->data()) != 0;
    // else if (extension == "pfm")
    //     return write_pfm_image(filename.c_str(), width(), height(), 4, (const float *)img->data()) != 0;
    // else if (extension == "exr")
    // {
    //     try
    //     {
    //         Imf::setGlobalThreadCount(std::thread::hardware_concurrency());
    //         Imf::RgbaOutputFile     file(filename.c_str(), width(), height(), Imf::WRITE_RGBA);
    //         Imf::Array2D<Imf::Rgba> pixels(height(), width());

    //         Timer timer;
    //         // copy image data over to Rgba pixels
    //         parallel_for(0, height(),
    //                      [this, img, &pixels](int y)
    //                      {
    //                          for (int x = 0; x < width(); ++x)
    //                          {
    //                              Imf::Rgba &p = pixels[y][x];
    //                              Color4     c = (*img)(x, y);
    //                              p.r          = c[0];
    //                              p.g          = c[1];
    //                              p.b          = c[2];
    //                              p.a          = c[3];
    //                          }
    //                      });
    //         spdlog::debug("Copying pixel data took: {} seconds.", (timer.lap() / 1000.f));

    //         file.setFrameBuffer(&pixels[0][0], 1, width());
    //         file.writePixels(height());

    //         spdlog::debug("Writing EXR image took: {} seconds.", (timer.lap() / 1000.f));
    //         return true;
    //     }
    //     catch (const exception &e)
    //     {
    //         spdlog::error("Unable to write image file \"{}\": {}", filename, e.what());
    //         return false;
    //     }
    // }
    // else
}

bool Image::save(const string &filename, float gain, float gamma, bool sRGB, bool dither) const
{
    std::ofstream os{filename, std::ios_base::binary};
    return save(os, filename, gain, gamma, sRGB, dither);
}