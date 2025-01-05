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

    {
        auto  half_data = reinterpret_cast<half *>(decoded_image->planes[UHDR_PLANE_PACKED]);
        int   stride_y  = decoded_image->stride[UHDR_PLANE_PACKED] * 4;
        Timer timer;
        for (int c = 0; c < 4; ++c)
        {
            image->channels[c].copy_from_interleaved(
                half_data, decoded_image->w, decoded_image->h, 4, c, [](half v) { return (float)v; }, stride_y);
            image->channels[c].apply([](float v, int x, int y) { return Channel::dequantize(v, x, y, false, true); });
        }
        spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed() / 1000.f));
    }

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

    {
        auto  byte_data = reinterpret_cast<uint8_t *>(gainmap->planes[UHDR_PLANE_PACKED]);
        int   stride_y  = gainmap->stride[UHDR_PLANE_PACKED] * num_components;
        Timer timer;
        for (int c = 0; c < num_components; ++c)
        {
            image->channels[4 + c].copy_from_interleaved(
                byte_data, gainmap->w, gainmap->h, num_components, c, [](uint8_t v) { return v / 255.f; }, stride_y);
            image->channels[4 + c].apply([](float v, int x, int y)
                                         { return Channel::dequantize(v, x, y, true, true); });
        }
        spdlog::debug("Copying gainmap data took: {} seconds.", (timer.elapsed() / 1000.f));
    }

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
