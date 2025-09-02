//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "uhdr.h"
#include "colorspace.h"
#include "exif.h"
#include "image.h"
#include "timer.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <half.h>
#include <iostream>
#include <stdexcept>

using namespace std;

#ifndef HDRVIEW_ENABLE_UHDR

bool is_uhdr_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_uhdr_image(istream &is, string_view filename)
{
    throw runtime_error("UltraHDR support not enabled in this build.");
}

void save_uhdr_image(const Image &img, ostream &os, const string_view filename, float gain, float base_quality,
                     float gainmap_quality, bool use_multi_channel_gainmap, int gainmap_scale_factor,
                     float gainmap_gamma)
{
    throw runtime_error("UltraHDR support not enabled in this build.");
}

#else

#include <ultrahdr_api.h>

bool is_uhdr_image(istream &is) noexcept
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

vector<ImagePtr> load_uhdr_image(istream &is, string_view filename)
{
    ScopedMDC mdc{"IO", "UHDR"};
    if (!is.good())
        throw invalid_argument("invalid file stream.");

    using Decoder = unique_ptr<uhdr_codec_private_t, void (&)(uhdr_codec_private_t *)>;
    auto decoder  = Decoder{uhdr_create_decoder(), uhdr_release_decoder};

    auto throw_if_error = [](uhdr_error_info_t status)
    {
        if (status.error_code != UHDR_CODEC_OK)
            throw invalid_argument(fmt::format("Error decoding image: {}", status.detail));
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
                fmt::format("Failed to read : {} bytes, read : {} bytes", size, (size_t)is.gcount())};

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
        spdlog::debug("base image: {}x{}", uhdr_dec_get_image_width(decoder.get()),
                      uhdr_dec_get_image_height(decoder.get()));
        throw_if_error(uhdr_decode(decoder.get()));
        // going out of scope deallocates contents of data
    }

    uhdr_raw_image_t *decoded_image = uhdr_get_decoded_image(decoder.get()); // freed by decoder destructor
    if (!decoded_image)
        throw invalid_argument{"Decode image failed."};
    if (decoded_image->fmt != UHDR_IMG_FMT_64bppRGBAHalfFloat)
        throw invalid_argument{"Unexpected pixel format."};

    spdlog::debug("base image: {}x{}; stride: {}; cg: {}; ct: {}; range: {}", decoded_image->w, decoded_image->h,
                  decoded_image->stride[UHDR_PLANE_PACKED], (int)decoded_image->cg, (int)decoded_image->ct,
                  (int)decoded_image->range);

    int2 size = int2(decoded_image->w, decoded_image->h);

    auto image                     = make_shared<Image>(size, 4);
    image->filename                = filename;
    image->file_has_straight_alpha = true;
    image->metadata["loader"]      = "libuhdr";

    const uhdr_mem_block_t *exif_data = uhdr_dec_get_exif(decoder.get());
    if (exif_data && exif_data->data && exif_data->data_sz > 0)
    {
        spdlog::debug("Found EXIF data of size {} bytes", exif_data->data_sz);

        try
        {
            auto j                  = exif_to_json(reinterpret_cast<uint8_t *>(exif_data->data), exif_data->data_sz);
            image->metadata["exif"] = j;
            spdlog::debug("EXIF metadata successfully parsed: {}", j.dump(2));
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Exception while parsing EXIF chunk: {}", e.what());
        }
    }

    {
        auto  half_data = reinterpret_cast<half *>(decoded_image->planes[UHDR_PLANE_PACKED]);
        int   stride_y  = decoded_image->stride[UHDR_PLANE_PACKED] * 4;
        Timer timer;
        for (int c = 0; c < 4; ++c)
            image->channels[c].copy_from_interleaved(
                half_data, decoded_image->w, decoded_image->h, 4, c, [](half v) { return (float)v; }, stride_y);

        spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed() / 1000.f));
    }

    // HDRView assumes the Rec 709 primaries/gamut. Set the matrix to convert to it
    if (decoded_image->cg == UHDR_CG_DISPLAY_P3)
    {
        image->chromaticities = gamut_chromaticities(ColorGamut_Display_P3_SMPTE432);
        spdlog::info("File uses Display P3 primaries and whitepoint.");
    }
    else if (decoded_image->cg == UHDR_CG_BT_2100)
    {
        image->chromaticities = gamut_chromaticities(ColorGamut_BT2020_2100);
        spdlog::info("File uses Rec. 2100 primaries and whitepoint.");
    }
    else if (decoded_image->cg == UHDR_CG_BT_709)
    {
        // insert into the header, but no conversion necessary since HDRView uses BT 709 internally
        image->chromaticities = gamut_chromaticities(ColorGamut_sRGB_BT709);
        spdlog::info("File uses Rec. 709/sRGB primaries and whitepoint.");
    }
    else // if (decoded_image->cg == UHDR_CG_UNSPECIFIED)
        spdlog::warn("File does not specify a color gamut. Assuming Rec. 709/sRGB primaries and whitepoint.");

    uhdr_raw_image_t *gainmap = uhdr_get_decoded_gainmap_image(decoder.get()); // freed by decoder destructor
    int2              gainmap_size(gainmap->w, gainmap->h);

    spdlog::debug("Gainmap image: {}x{}; stride: {}; cg: {}; ct: {}; range: {}", gainmap->w, gainmap->h,
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
            image->channels[4 + c].copy_from_interleaved(
                byte_data, gainmap->w, gainmap->h, num_components, c, [](uint8_t v) { return dequantize_full(v); },
                stride_y);

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

void save_uhdr_image(const Image &img, ostream &os, const string_view filename, float gain, float base_quality,
                     float gainmap_quality, bool use_multi_channel_gainmap, int gainmap_scale_factor,
                     float gainmap_gamma)
{
    Timer timer;
    // get interleaved HDR pixel data
    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved<half>(&w, &h, &n, gain);

    if (n != 3 && n != 4)
        throw invalid_argument("Can only save images with 3 or 4 channels in UltraHDR right now.");

    // The UHDR API expects a packed RGBA half-float buffer for UHDR_IMG_FMT_64bppRGBAHalfFloat.
    // If we were given only RGB (n==3) expand to RGBA and set alpha to 1.0.
    const size_t      npixels = size_t(w) * size_t(h);
    std::vector<half> expanded_rgba(npixels * 4); // keep alive until after encode()
    auto              src = reinterpret_cast<half *>(pixels.get());
    for (size_t i = 0; i < npixels; ++i)
    {
        float3 rgb               = {src[3 * i + 0], src[3 * i + 1], src[3 * i + 2]};
        float3 srgb              = mul(img.M_to_sRGB, rgb);
        expanded_rgba[4 * i + 0] = srgb.x;
        expanded_rgba[4 * i + 1] = srgb.y;
        expanded_rgba[4 * i + 2] = srgb.z;
        expanded_rgba[4 * i + 3] = half(1.0f); // opaque alpha
    }
    // Treat as 4-channel data from now on
    n                = 4;
    void *plane0_ptr = expanded_rgba.data();

    auto throw_if_error = [](uhdr_error_info_t status)
    {
        if (status.error_code != UHDR_CODEC_OK)
            throw invalid_argument(fmt::format("UltraHDR: Error decoding image: {}", status.detail));
    };

    using Encoder = unique_ptr<uhdr_codec_private_t, void (&)(uhdr_codec_private_t *)>;
    auto encoder  = Encoder{uhdr_create_encoder(), uhdr_release_encoder};

    uhdr_raw_image_t raw_image{
        UHDR_IMG_FMT_64bppRGBAHalfFloat, /**< Image Format */
        UHDR_CG_BT_709,                  /**< Color Gamut */
        UHDR_CT_LINEAR,                  /**< Color Transfer */
        UHDR_CR_FULL_RANGE,              /**< Color Range */
        (unsigned)w,                     /**< Stored image width */
        (unsigned)h,                     /**< Stored image height */
        {plane0_ptr, nullptr, nullptr},  /**< pointer to the top left pixel for each plane */
        {(unsigned)w, 0u, 0u}            /**< stride in pixels between rows for each plane */
    };

    throw_if_error(uhdr_enc_set_raw_image(encoder.get(), &raw_image, UHDR_HDR_IMG));
    throw_if_error(uhdr_enc_set_quality(encoder.get(), (int)base_quality, UHDR_BASE_IMG));
    throw_if_error(uhdr_enc_set_quality(encoder.get(), (int)gainmap_quality, UHDR_GAIN_MAP_IMG));
    throw_if_error(uhdr_enc_set_using_multi_channel_gainmap(encoder.get(), use_multi_channel_gainmap));
    throw_if_error(uhdr_enc_set_gainmap_scale_factor(encoder.get(), gainmap_scale_factor));
    throw_if_error(uhdr_enc_set_gainmap_gamma(encoder.get(), gainmap_gamma));
    throw_if_error(uhdr_enc_set_preset(encoder.get(), UHDR_USAGE_BEST_QUALITY));

    throw_if_error(uhdr_encode(encoder.get()));

    auto output = uhdr_get_encoded_stream(encoder.get()); // freed by decoder destructor

    os.write(static_cast<char *>(output->data), output->data_sz);
    spdlog::info("Writing UltraHDR image to \"{}\" took: {} seconds.", filename, (timer.elapsed() / 1000.f));
}

#endif
