//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#define QOI_NO_STDIO
#define QOI_IMPLEMENTATION
#include <qoi.h>

#include "colorspace.h"
#include "image.h"
#include "texture.h"
#include "timer.h"
#include <ImfHeader.h>
#include <ImfStandardAttributes.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "dithermatrix256.h"

using namespace std;

bool is_qoi_image(istream &is) noexcept
{

    bool ret = false;
    try
    {
        char b[4];
        is.read(b, sizeof(b));
        ret = !!is && is.gcount() == sizeof(b) && string(b, sizeof(b)) == "qoif";
    }
    catch (...)
    {
        //
    }

    is.clear();
    is.seekg(0);
    return ret;
}

vector<ImagePtr> load_qoi_image(istream &is, const string &filename)
{
    char magic[4];
    is.read(magic, 4);
    string magic_string(magic, 4);
    if (magic_string != "qoif")
        throw invalid_argument{fmt::format("QOI: invalid magic string '{}'", magic_string)};

    // calculate size of stream
    is.clear();
    is.seekg(0, is.end);
    size_t raw_size = is.tellg();
    is.seekg(0, is.beg);

    // read in the whole stream
    vector<char> raw_data(raw_size);
    is.read(raw_data.data(), raw_size);

    qoi_desc                                     desc;
    std::unique_ptr<void, decltype(std::free) *> decoded_data{
        qoi_decode(raw_data.data(), static_cast<int>(raw_size), &desc, 0), std::free};
    if (!decoded_data.get())
        throw invalid_argument{"Failed to decode data from the QOI format."};

    int3 size{static_cast<int>(desc.width), static_cast<int>(desc.height), static_cast<int>(desc.channels)};
    if (product(size) == 0)
        throw invalid_argument{"Image has zero pixels."};

    auto image      = make_shared<Image>(size.xy(), size.z);
    image->filename = filename;

    bool linearize = desc.colorspace != QOI_LINEAR;

    Timer timer;
    for (int c = 0; c < size.z; ++c)
    {
        image->channels[c].copy_from_interleaved(reinterpret_cast<uint8_t *>(decoded_data.get()), size.x, size.y,
                                                 size.z, c, [](uint8_t v) { return v / 255.f; });
        if (c < 3 && linearize)
            image->channels[c].apply([linearize, c](float v, int x, int y)
                                     { return Channel::dequantize(v, x, y, linearize, c != 3); });
    }
    // if we have an alpha channel, premultiply the other channels by it
    // this needs to be done after the values have been made linear
    if (size.z > 3)
        for (int c = 0; c < 3; ++c)
            image->channels[c].apply([&alpha = image->channels[3]](float v, int x, int y) { return alpha(x, y) * v; });
    spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));

    // insert into the header, but no conversion necessary since HDRView uses BT 709 internally
    Imf::addChromaticities(image->header, color_space_chromaticity("sRGB/BT 709"));

    return {image};
}

bool save_qoi_image(const Image &img, ostream &os, const string &filename, float gain, float gamma, bool sRGB,
                    bool dither)
{
    const int w = img.size().x;
    const int h = img.size().y;
    const int n = img.groups[img.selected_group].num_channels;

    // The QOI image format expects nChannels to be either 3 for RGB data or 4 for RGBA.
    if (n != 4 && n != 3)
        throw invalid_argument{
            fmt::format("Invalid number of channels {}. QOI format expects either 3 or 4 channels.", n)};

    // get interleaved LDR pixel data
    std::unique_ptr<uint8_t[]> pixels(new uint8_t[w * h * n]);
    {
        Timer          timer;
        const Channel *alpha = n > 3 ? &img.channels[img.groups[img.selected_group].channels[3]] : nullptr;

        // unpremultiply, tonemap, and dither the color channels
        for (int c = 0; c < n; ++c)
            img.channels[img.groups[img.selected_group].channels[c]].copy_to_interleaved(
                pixels.get(), n, c,
                [gain, sRGB, c, dither, alpha](float v, int x, int y)
                {
                    // only gamma correct and premultiply the RGB channels.
                    // alpha channel gets stored linearly.
                    if (c < 3)
                    {
                        v *= gain;

                        // unpremultiply
                        if (alpha)
                        {
                            float a = (*alpha)(x, y);
                            if (a != 0.f)
                                v /= a;
                        }

                        if (sRGB)
                            v = LinearToSRGB(v);
                    }

                    return (uint8_t)clamp(v * 255.0f + (dither ? tent_dither(x, y) : 0.f), 0.0f, 255.0f);
                });

        spdlog::debug("Tonemapping to 8bit took: {} seconds.", (timer.elapsed() / 1000.f));
    }

    // write the data
    const qoi_desc desc{
        static_cast<unsigned int>(w),                             // width
        static_cast<unsigned int>(h),                             // height
        static_cast<unsigned char>(n),                            // number of channels
        static_cast<unsigned char>(sRGB ? QOI_SRGB : QOI_LINEAR), // colorspace
    };
    int encoded_size = 0;

    std::unique_ptr<void, decltype(std::free) *> encoded_data{qoi_encode(pixels.get(), &desc, &encoded_size),
                                                              std::free};

    if (!encoded_data.get())
        throw invalid_argument{"Failed to encode data into the QOI format."};

    os.write(reinterpret_cast<char *>(encoded_data.get()), encoded_size);

    return true;
}
