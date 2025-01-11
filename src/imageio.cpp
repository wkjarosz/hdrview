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
#include "imageio/qoi.h"
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
        else if (is_qoi_image(is))
        {
            spdlog::info("Detected QOI image.");
            images = load_qoi_image(is, filename);
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
    string extension = to_lower(get_extension(filename));
    if (extension == "exr")
        return save_exr_image(*this, os, filename);
    else if (extension == "jpg" || extension == "jpeg")
        return save_uhdr_image(*this, os, filename);
    else if (extension == "qoi")
        return save_qoi_image(*this, os, filename, gain, gamma, sRGB, dither);
    else
        return save_stb_image(*this, os, filename, gain, gamma, sRGB, dither);
}

bool Image::save(const string &filename, float gain, float gamma, bool sRGB, bool dither) const
{
    std::ofstream os{filename, std::ios_base::binary};
    return save(os, filename, gain, gamma, sRGB, dither);
}