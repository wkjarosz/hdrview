//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "pfm.h"
#include "image.h"
#include "texture.h"
#include "timer.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace std;

namespace
{

float reinterpret_as_host_endian(float f, bool big_endian)
{
    static_assert(sizeof(float) == sizeof(unsigned int), "Sizes must match");

    const auto *byte = (const unsigned char *)&f;
    uint32_t    i;
    if (big_endian)
        i = (byte[3] << 0) | (byte[2] << 8) | (byte[1] << 16) | (byte[0] << 24);
    else
        i = (byte[0] << 0) | (byte[1] << 8) | (byte[2] << 16) | (byte[3] << 24);

    float ret;
    memcpy(&ret, &i, sizeof(float));
    return ret;
}

} // end namespace

bool is_pfm_image(istream &is) noexcept
{
    if (!is.good())
        return false;

    auto start = is.tellg();
    bool ret   = false;

    try
    {
        string magic;
        int    width, height;
        float  scale;

        is >> magic >> width >> height >> scale;

        ret = is.good() && (magic == "Pf" || magic == "PF" || magic == "PF4") && width > 0 && height > 0 &&
              isfinite(scale) && scale != 0;
    }
    catch (...)
    {
    }

    // rewind
    is.clear();
    is.seekg(start);
    return ret;
}

unique_ptr<float[]> load_pfm_image(istream &is, const string &filename, int *width, int *height, int *num_channels)
{
    try
    {
        Timer  timer;
        string magic;
        float  scale;

        is >> magic >> *width >> *height >> scale;

        if (magic == "Pf")
            *num_channels = 1;
        else if (magic == "PF")
            *num_channels = 3;
        else if (magic == "PF4")
            *num_channels = 4;
        else
            throw invalid_argument(
                fmt::format("load_pfm_image: Could not deduce number of channels from PFM magic string {}", magic));

        if (*width <= 0 || *height <= 0)
            throw invalid_argument(
                fmt::format("load_pfm_image: Invalid image width ({}) or height ({})", *width, *height));

        if (!isfinite(scale) || scale == 0)
            throw invalid_argument(fmt::format("load_pfm_image: Invalid PFM scale {}", scale));

        bool big_endian = scale > 0.f;
        scale           = fabsf(scale);

        size_t                   num_floats = static_cast<size_t>((*width) * (*height) * (*num_channels));
        auto                     num_bytes  = num_floats * sizeof(float);
        std::unique_ptr<float[]> data(new float[num_floats]);

        // skip last newline at the end of the header.
        char c;
        while (is.get(c) && c != '\r' && c != '\n');

        // Read the rest of the file
        is.read(reinterpret_cast<char *>(data.get()), num_bytes);
        if (is.gcount() < (streamsize)num_bytes)
            throw invalid_argument{
                fmt::format("load_pfm_image: Expected {} bytes, but could only read {} bytes", is.gcount(), num_bytes)};

        // multiply data by scale factor
        for (size_t i = 0; i < num_floats; ++i) data[i] = scale * reinterpret_as_host_endian(data[i], big_endian);

        spdlog::debug("Reading PFM image '{}' took: {} seconds.", filename, (timer.elapsed() / 1000.f));

        return data;
    }
    catch (const exception &e)
    {
        throw invalid_argument{fmt::format("{} in file '{}'", e.what(), filename)};
    }
}

vector<ImagePtr> load_pfm_image(std::istream &is, const string &filename)
{
    int3 size;
    auto float_data = load_pfm_image(is, filename, &size.x, &size.y, &size.z);
    auto image      = make_shared<Image>(size.xy(), size.z);
    image->filename = filename;

    Timer timer;
    for (int c = 0; c < size.z; ++c)
        image->channels[c].copy_from_interleaved(float_data.get(), size.x, size.y, size.z, c,
                                                 [](float v) { return v; });
    spdlog::debug("Copying image data for took: {} seconds.", (timer.elapsed() / 1000.f));
    return {image};
}

void write_pfm_image(ostream &os, const string &filename, int width, int height, int num_channels, const float data[])
{
    if (!os)
        throw invalid_argument("write_pfm_image: Error opening file '" + filename);

    string magic;

    if (num_channels == 1)
        magic = "Pf";
    else if (num_channels == 3)
        magic = "PF";
    else if (num_channels == 4)
        magic = "PF4";
    else
        throw invalid_argument(fmt::format("write_pfm_image: Unsupported number of channels {} when writing file "
                                           "\"{}\". PFM format only supports 1, 3, or 4 channels.",
                                           num_channels, filename));
    os << magic << "\n";
    os << width << " " << height << "\n";

    // determine system endianness
    bool little_endian = false;
    {
        constexpr int n = 1;
        // little endian if true
        if (*(char *)&n == 1)
            little_endian = true;
    }

    os << (little_endian ? "-1.0000000\n" : "1.0000000\n");

    os.write((const char *)data, width * height * sizeof(float) * num_channels);
}

void save_pfm_image(const Image &img, ostream &os, const string &filename, float gain)
{
    Timer timer;
    // get interleaved LDR pixel data
    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved_floats(&w, &h, &n, gain);
    write_pfm_image(os, filename, w, h, n, pixels.get());
    spdlog::info("Saved PFM image to \"{}\" in {} seconds.", filename, (timer.elapsed() / 1000.f));
}
