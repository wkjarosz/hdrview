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

    auto image                     = make_shared<Image>(size.xy(), size.z);
    image->filename                = filename;
    image->file_has_straight_alpha = true;

    bool linearize = desc.colorspace != QOI_LINEAR;

    if (linearize)
        spdlog::info("QOI image is sRGB encoded, linearizing.");

    Timer timer;
    for (int c = 0; c < size.z; ++c)
        image->channels[c].copy_from_interleaved(reinterpret_cast<uint8_t *>(decoded_data.get()), size.x, size.y,
                                                 size.z, c, [linearize, c](uint8_t v)
                                                 { return byte_to_f32(v, linearize && c != 3); });

    spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));

    return {image};
}

void save_qoi_image(const Image &img, ostream &os, const string &filename, float gain, float gamma, bool sRGB,
                    bool dither)
{
    Timer timer;
    // get interleaved LDR pixel data
    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved_bytes(&w, &h, &n, gain, gamma, sRGB, dither);

    // The QOI image format only supports RGB or RGBA data.
    if (n != 4 && n != 3)
        throw invalid_argument{
            fmt::format("Invalid number of channels {}. QOI format expects either 3 or 4 channels.", n)};

    // write the data
    const qoi_desc desc{
        static_cast<unsigned int>(w),                             // width
        static_cast<unsigned int>(h),                             // height
        static_cast<unsigned char>(n),                            // number of channels
        static_cast<unsigned char>(sRGB ? QOI_SRGB : QOI_LINEAR), // colorspace
    };
    int encoded_size = 0;

    spdlog::info("Saving {}-channel, {}x{} pixels {} QOI image.", n, w, h, sRGB ? "sRGB" : "linear");
    std::unique_ptr<void, decltype(std::free) *> encoded_data{qoi_encode(pixels.get(), &desc, &encoded_size),
                                                              std::free};

    if (!encoded_data.get())
        throw invalid_argument{"Failed to encode data into the QOI format."};

    os.write(reinterpret_cast<char *>(encoded_data.get()), encoded_size);
    spdlog::info("Saved QOI image to \"{}\" in {} seconds.", filename, (timer.elapsed() / 1000.f));
}
