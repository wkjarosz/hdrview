//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "uhdr.h"
#include "image.h"
#include "texture.h"
#include "timer.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <fstream>
#include <half.h>
#include <iostream>
#include <stdexcept>

#include "ultrahdr_api.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
// Color space conversions
// Sample, See,
// https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html#_bt_709_bt_2020_primary_conversion_example

static constexpr float4x4 kP3ToBt709{{1.22494f, -0.042057f, -0.019638f, 0.f},
                                     {-0.22494f, 1.042057f, -0.078636f, 0.f},
                                     {0.0f, 0.0f, 1.098274f, 0.f},
                                     {0.0f, 0.0f, 0.f, 1.f}};

static constexpr float4x4 kBt2100ToBt709{{1.660491f, -0.124551f, -0.018151f, 0.f},
                                         {-0.587641f, 1.1329f, -0.100579f, 0.f},
                                         {-0.07285f, -0.008349f, 1.11873f, 0.f},
                                         {0.0f, 0.0f, 0.f, 1.f}};

bool is_uhdr_image(istream &is)
{
    if (!is.good())
        return false;

    bool ret = false;
    try
    {
        // calculate size of stream
        is.seekg(0, ios::end);
        size_t size = (size_t)is.tellg();
        is.seekg(0, ios::beg);
        if (size <= 0)
            throw invalid_argument{"Stream is empty"};

        // allocate memory to store contents of file and read it in
        unique_ptr<char[]> data(new char[size]);
        is.read(reinterpret_cast<char *>(data.get()), size);

        if ((size_t)is.gcount() != size)
            throw invalid_argument{
                fmt::format("Failed to read : {} bytes, read : {} bytes", size, (size_t)is.gcount())};

        // we could just call ::is_uhdr_image now, but we want to report the error in case this is not a uhdr image
        // ret = ::is_uhdr_image(data.get(), size);

        auto throw_if_error = [](uhdr_error_info_t status)
        {
            if (status.error_code != UHDR_CODEC_OK)
                throw invalid_argument(fmt::format("UltraHDR: Error decoding image: {}", status.detail));
        };

        using Decoder = unique_ptr<uhdr_codec_private_t, void (&)(uhdr_codec_private_t *)>;
        auto decoder  = Decoder{uhdr_create_decoder(), uhdr_release_decoder};

        uhdr_compressed_image_t compressed_image{
            data.get(),          /**< Pointer to a block of data to decode */
            size,                /**< size of the data buffer */
            size,                /**< maximum size of the data buffer */
            UHDR_CG_UNSPECIFIED, /**< Color Gamut */
            UHDR_CT_UNSPECIFIED, /**< Color Transfer */
            UHDR_CR_UNSPECIFIED  /**< Color Range */
        };

        throw_if_error(uhdr_dec_set_image(decoder.get(), &compressed_image));
        throw_if_error(uhdr_dec_probe(decoder.get()));

        ret = true;
    }
    catch (const exception &e)
    {
        spdlog::debug("Cannot load image with UltraHDR: {}", e.what());
        ret = false;
    }

    // rewind
    is.clear();
    is.seekg(0);
    return ret;
}

vector<ImagePtr> load_uhdr_image(istream &is, const string &filename)
{
    if (!is.good())
        throw invalid_argument("UltraHDR: invalid file stream.");

    using Decoder = unique_ptr<uhdr_codec_private_t, void (&)(uhdr_codec_private_t *)>;
    auto decoder  = Decoder{uhdr_create_decoder(), uhdr_release_decoder};

    auto throw_if_error = [](uhdr_error_info_t status)
    {
        if (status.error_code != UHDR_CODEC_OK)
            throw invalid_argument(fmt::format("UltraHDR: Error decoding image: {}", status.detail));
    };

    {
        // calculate size of stream
        is.seekg(0, ios::end);
        size_t size = (size_t)is.tellg();
        is.seekg(0, ios::beg);
        if (size <= 0)
            throw invalid_argument{fmt::format("File '{}' is empty", filename)};

        // allocate memory to store contents of file and read it in
        unique_ptr<char[]> data(new char[size]);
        is.read(reinterpret_cast<char *>(data.get()), size);

        if ((size_t)is.gcount() != size)
            throw invalid_argument{
                fmt::format("UltraHDR: Failed to read : {} bytes, read : {} bytes", size, (size_t)is.gcount())};

        uhdr_compressed_image_t compressed_image{
            data.get(),          /**< Pointer to a block of data to decode */
            size,                /**< size of the data buffer */
            size,                /**< maximum size of the data buffer */
            UHDR_CG_UNSPECIFIED, /**< Color Gamut */
            UHDR_CT_UNSPECIFIED, /**< Color Transfer */
            UHDR_CR_UNSPECIFIED  /**< Color Range */
        };
        throw_if_error(uhdr_dec_set_image(decoder.get(), &compressed_image));
        throw_if_error(uhdr_dec_set_out_color_transfer(decoder.get(), UHDR_CT_LINEAR));
        throw_if_error(uhdr_dec_set_out_img_format(decoder.get(), UHDR_IMG_FMT_64bppRGBAHalfFloat));
        throw_if_error(uhdr_dec_probe(decoder.get()));
        spdlog::debug("UltraHDR: base image: {}x{}", uhdr_dec_get_image_width(decoder.get()),
                      uhdr_dec_get_image_height(decoder.get()));
        throw_if_error(uhdr_decode(decoder.get()));
        // going out of scope deallocate contents of data
    }

    uhdr_raw_image_t *decoded_image = uhdr_get_decoded_image(decoder.get()); // freed by decoder destructor
    if (!decoded_image)
        throw invalid_argument{"UltraHDR: Decode image failed."};
    if (decoded_image->fmt != UHDR_IMG_FMT_64bppRGBAHalfFloat)
        throw invalid_argument{"UltraHDR: Unexpected output format."};

    spdlog::debug("UltraHDR: base image: {}x{}; stride: {}; cg: {}; ct: {}; range: {}", decoded_image->w,
                  decoded_image->h, decoded_image->stride[UHDR_PLANE_PACKED], (int)decoded_image->cg,
                  (int)decoded_image->ct, (int)decoded_image->range);

    int2 size = int2(decoded_image->w, decoded_image->h);

    auto image      = make_shared<Image>(size, 4);
    image->filename = filename;

    size_t block_size = std::max(1u, 1024u * 1024u / decoded_image->w);
    parallel_for(blocked_range<int>(0, decoded_image->h, block_size),
                 [&image, decoded_image](int begin_y, int end_y, int unit_index, int thread_index)
                 {
                     auto data = reinterpret_cast<char *>(decoded_image->planes[UHDR_PLANE_PACKED]);
                     for (int y = begin_y; y < end_y; ++y)
                     {
                         auto scanline =
                             reinterpret_cast<half *>(data + y * decoded_image->stride[UHDR_PLANE_PACKED] * 8);
                         for (unsigned x = 0; x < decoded_image->w; ++x)
                         {
                             image->channels[0](x, y) = scanline[x * 4 + 0];
                             image->channels[1](x, y) = scanline[x * 4 + 1];
                             image->channels[2](x, y) = scanline[x * 4 + 2];
                             image->channels[3](x, y) = scanline[x * 4 + 3];
                         }
                     }
                 });

    // HDRView assumes the Rec 709 primaries/gamut. Set the matrix to convert to it
    if (decoded_image->cg == UHDR_CG_DISPLAY_P3)
    {
        image->M_to_Rec709 = kP3ToBt709;
        spdlog::info("Converting pixel values to Rec. 709/sRGB primaries and whitepoint from Display P3.");
    }
    else if (decoded_image->cg == UHDR_CG_BT_2100)
    {
        image->M_to_Rec709 = kBt2100ToBt709;
        spdlog::info("Converting pixel values to Rec. 709/sRGB primaries and whitepoint from Rec. 2100.");
    }

    uhdr_raw_image_t *gainmap = uhdr_get_decoded_gainmap_image(decoder.get()); // freed by decoder destructor
    int2              gainmap_size(gainmap->w, gainmap->h);

    spdlog::debug("UltraHDR: gainmap image: {}x{}; stride: {}; cg: {}; ct: {}; range: {}", gainmap->w, gainmap->h,
                  gainmap->stride[UHDR_PLANE_PACKED], (int)gainmap->cg, (int)gainmap->ct, (int)gainmap->range);

    // if the gainmap is an unexpected size or format, we are done
    if ((gainmap_size.x > size.x || gainmap_size.y > size.y) ||
        (gainmap->fmt != UHDR_IMG_FMT_32bppRGBA8888 && gainmap->fmt != UHDR_IMG_FMT_8bppYCbCr400 &&
         gainmap->fmt != UHDR_IMG_FMT_24bppRGB888))
        return {image};

    // otherwise, extract the gain map as a separate channel group

    int num_components =
        gainmap->fmt == UHDR_IMG_FMT_32bppRGBA8888 ? 4 : (gainmap->fmt == UHDR_IMG_FMT_24bppRGB888 ? 3 : 1);

    if (num_components == 1)
        image->channels.emplace_back("gainmap.Y", size);
    if (num_components >= 3)
    {
        image->channels.emplace_back("gainmap.R", size);
        image->channels.emplace_back("gainmap.G", size);
        image->channels.emplace_back("gainmap.B", size);
    }
    if (num_components == 4)
        image->channels.emplace_back("gainmap.A", size);

    block_size = std::max(1u, 1024u * 1024u / gainmap->w);
    parallel_for(blocked_range<int>(0, gainmap->h, block_size),
                 [&image, gainmap, num_components](int begin_y, int end_y, int unit_index, int thread_index)
                 {
                     // gainmap->planes contains interleaved, 8bit grainmap channels
                     auto data = reinterpret_cast<uint8_t *>(gainmap->planes[UHDR_PLANE_PACKED]);
                     // Copy a block of values into each of the separate channels in image
                     for (int y = begin_y; y < end_y; ++y)
                     {
                         auto scanline = reinterpret_cast<uint8_t *>(data + y * gainmap->stride[UHDR_PLANE_PACKED] *
                                                                                num_components);
                         for (unsigned x = 0; x < gainmap->w; ++x)
                             for (int c = 0; c < num_components; ++c)
                             {
                                 uint8_t v                    = scanline[x * num_components + c];
                                 float   d                    = 0.5f;
                                 image->channels[4 + c](x, y) = SRGBToLinear((v + d) / 256.0f);
                             }
                     }
                 });

    // resize the data in the channels if necessary
    if (gainmap_size.x < size.x && gainmap_size.y < size.y)
    {
        int xs = size.x / gainmap_size.x;
        int ys = size.x / gainmap_size.x;
        spdlog::debug("Resizing gainmap resolution {}x{} by factor {}x{} to match image resolution {}x{}.",
                      gainmap_size.x, gainmap_size.y, xs, ys, size.x, size.y);
        for (int c = 0; c < num_components; ++c)
        {
            Array2Df tmp = image->channels[4 + c];

            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) image->channels[4 + c](x, y) = tmp(x / xs, y / ys);
        }
    }

    return {image};
}
