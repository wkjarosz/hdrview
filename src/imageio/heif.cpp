//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
#include "texture.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace std;

#ifndef HDRVIEW_ENABLE_HEIF

bool is_heif_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_heif_image(istream &is, const string_view filename)
{
    throw runtime_error("HEIF/AVIF support not enabled in this build.");
}

#else

#include "colorspace.h"
#include "heif.h"
#include "icc.h"
#include "libheif/heif.h"
#include "libheif/heif_cxx.h"
#include "timer.h"
#include <ImfHeader.h>
#include <ImfStandardAttributes.h>

static bool linearize_colors(float *pixels, int3 size, heif_color_profile_nclx *nclx, string *tf_description = nullptr,
                             float2 *red = nullptr, float2 *green = nullptr, float2 *blue = nullptr,
                             float2 *white = nullptr)
{
    if (!nclx)
        return false;

    Timer timer;
    spdlog::info("Linearizing pixel values using nclx.");
    if (red)
        *red = float2(nclx->color_primary_red_x, nclx->color_primary_red_y);
    if (green)
        *green = float2(nclx->color_primary_green_x, nclx->color_primary_green_y);
    if (blue)
        *blue = float2(nclx->color_primary_blue_x, nclx->color_primary_blue_y);
    if (white)
        *white = float2(nclx->color_primary_white_x, nclx->color_primary_white_y);

    float            gamma = 1.f;
    TransferFunction tf;
    string           tf_desc;
    switch (nclx->transfer_characteristics)
    {
    case heif_transfer_characteristic_ITU_R_BT_709_5:
        tf_desc = rec709_2020_tf;
        tf      = TransferFunction_Rec709_2020;
        break;
    case heif_transfer_characteristic_ITU_R_BT_2100_0_PQ:
        tf_desc = pq_tf;
        tf      = TransferFunction_Rec2100_PQ;
        break;
    case heif_transfer_characteristic_ITU_R_BT_2100_0_HLG:
        tf_desc = hlg_tf;
        tf      = TransferFunction_Rec2100_HLG;
        break;
    case heif_transfer_characteristic_linear:
        tf_desc = linear_tf;
        tf      = TransferFunction_Linear;
        break;
    case heif_transfer_characteristic_IEC_61966_2_1: [[fallthrough]];
    default: tf_desc = srgb_tf; tf = TransferFunction_sRGB;
    }

    if (tf_description)
        *tf_description = tf_desc;

    if (tf == TransferFunction_Rec2100_HLG && size.z == 3)
    {
        // HLG needs to operate on all three channels at once
        parallel_for(blocked_range<int>(0, size.x * size.y, 1024 * 1024),
                     [&pixels](int start, int end, int, int)
                     {
                         auto rgb_pixels = reinterpret_cast<float3 *>(pixels + start * 3);
                         for (int i = start; i < end; ++i) rgb_pixels[i] = EOTF_HLG(rgb_pixels[i]) / 255.f;
                     });
    }
    else
    {
        // other transfer functions apply to each channel independently
        parallel_for(blocked_range<int>(0, size.x * size.y * size.z, 1024 * 1024),
                     [&pixels, tf, gamma](int start, int end, int, int)
                     {
                         for (int i = start; i < end; ++i) pixels[i] = to_linear(pixels[i], tf, gamma);
                     });
    }
    return true;
}

vector<ImagePtr> load_heif_image(istream &is, const string_view filename)
{
    // calculate size of stream
    is.clear();
    is.seekg(0, is.end);
    size_t raw_size = is.tellg();
    is.seekg(0, is.beg);

    // read in the whole stream
    vector<char> raw_data(raw_size);
    is.read(raw_data.data(), raw_size);
    if ((size_t)is.gcount() != raw_size)
        throw invalid_argument{
            fmt::format("HEIF: Failed to read : {} bytes, read : {} bytes", raw_size, (size_t)is.gcount())};

    vector<ImagePtr> images;
    try
    {
        unique_ptr<heif::Context> ctx{new heif::Context};

        ctx->read_from_memory_without_copy(reinterpret_cast<void *>(raw_data.data()), raw_size);

        auto primary_id = ctx->get_primary_image_ID();            // id of primary image
        auto item_ids   = ctx->get_list_of_top_level_image_IDs(); // ids of all other images

        // remove the primary item from the list of all items
        for (size_t i = 0; i < item_ids.size(); ++i)
            if (item_ids[i] == primary_id)
            {
                item_ids.erase(item_ids.begin() + i);
                break;
            }

        int num_subimages = 1 + int(item_ids.size());

        spdlog::info("HEIF: Found {} subimages", num_subimages);

        // just get the primary image for now
        for (int subimage = 0; subimage < num_subimages; ++subimage)
        {
            spdlog::info("HEIF: Loading subimage {}...", subimage);
            auto                     id          = (subimage == 0) ? primary_id : item_ids[subimage - 1];
            auto                     ihandle     = ctx->get_image_handle(id);
            auto                     raw_ihandle = ihandle.get_raw_image_handle();
            heif_color_profile_nclx *nclx        = nullptr;
            std::vector<uint8_t>     icc_profile;

            if (ihandle.empty() || !raw_ihandle)
                continue;

            auto err = heif_image_handle_get_nclx_color_profile(raw_ihandle, &nclx);
            if (err.code != heif_error_Ok)
                spdlog::info("HEIF: No handle-level nclx color profile found");

            if (size_t icc_size = heif_image_handle_get_raw_color_profile_size(raw_ihandle); icc_size != 0)
            {
                spdlog::info("HEIF: File contains a handle-level ICC profile.");
                icc_profile.resize(icc_size);
                err =
                    heif_image_handle_get_raw_color_profile(raw_ihandle, reinterpret_cast<void *>(icc_profile.data()));
                if (err.code != heif_error_Ok)
                {
                    spdlog::info("HEIF: Could not read handle-level ICC profile.");
                    icc_profile.clear();
                }
            }

            heif_colorspace preferred_colorspace;
            heif_chroma     preferred_chroma;
            err = heif_image_handle_get_preferred_decoding_colorspace(raw_ihandle, &preferred_colorspace,
                                                                      &preferred_chroma);
            spdlog::info("Preferred decoding colorspace: {}, chroma: {}", (int)preferred_colorspace,
                         (int)preferred_chroma);

            int3            size{(int)ihandle.get_width(), (int)ihandle.get_height(), 0};
            bool            has_alpha = ihandle.has_alpha_channel();
            heif_chroma     out_chroma;
            heif_colorspace out_colorspace;
            heif_channel    out_planes[2] = {heif_channel_Y, heif_channel_Alpha};
            int             cpp; // channels per plane
            int             num_planes = 1;
            switch (preferred_chroma)
            {
            case heif_chroma_monochrome:
                out_chroma     = heif_chroma_monochrome;
                out_colorspace = heif_colorspace_monochrome;
                out_planes[0]  = heif_channel_Y;
                size.z         = has_alpha ? 2 : 1;
                cpp            = 1;
                num_planes     = size.z;
                break;
            default:
                out_chroma     = has_alpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE;
                out_colorspace = heif_colorspace_RGB;
                out_planes[0]  = heif_channel_interleaved;
                size.z         = has_alpha ? 4 : 3;
                cpp            = size.z;
                num_planes     = 1;
                break;
            }
            spdlog::info("Image size: {}", size);

            auto image                     = make_shared<Image>(size.xy(), size.z);
            image->filename                = filename;
            image->partname                = subimage != 0 ? fmt::format("{:d}", id) : "";
            image->file_has_straight_alpha = has_alpha && !ihandle.is_premultiplied_alpha();
            image->metadata["loader"]      = "libheif";

            spdlog::info("Decoding heif image...");
            auto himage = ihandle.decode_image(out_colorspace, out_chroma);

            if (himage.get_width(out_planes[0]) != size.x || himage.get_height(out_planes[0]) != size.y)
            {
                spdlog::warn("HEIF: Image size mismatch: {}x{} vs {}x{}", himage.get_width(out_planes[0]),
                             himage.get_height(out_planes[0]), size.x, size.y);
                size.x = himage.get_width(out_planes[0]);
                size.y = himage.get_height(out_planes[0]);
            }

            spdlog::info("Decoded colorspace: {}, chroma: {}", (int)himage.get_colorspace(),
                         (int)himage.get_chroma_format());

            // A tricky bit is that the C++ API doesn't give us a direct way to get the image ptr, we need to resort
            // to some casting trickery, with knowledge that the C++ heif::Image class consists solely of a
            // std::shared_ptr to a heif_image.
            const heif_image *raw_image = reinterpret_cast<std::shared_ptr<heif_image> *>(&himage)->get();

            // is this needed or will the handle-level functions return a profile even if its at the image level?
            if (!nclx)
            {
                err = heif_image_get_nclx_color_profile(raw_image, &nclx);
                if (err.code == heif_error_Color_profile_does_not_exist)
                    spdlog::warn(
                        "HEIF: No image-level nclx color profile found. Will assume sRGB/IEC 61966-2-1 colorspace.");
            }
            if (icc_profile.empty())
            {
                if (size_t icc_size = heif_image_get_raw_color_profile_size(raw_image); icc_size != 0)
                {
                    spdlog::info("HEIF: File contains an image-level ICC profile.");
                    icc_profile.resize(icc_size);
                    err = heif_image_get_raw_color_profile(raw_image, reinterpret_cast<void *>(icc_profile.data()));
                    if (err.code != heif_error_Ok)
                    {
                        spdlog::info("HEIF: Could not read image-level ICC profile");
                        icc_profile.clear();
                    }
                }
            }

            spdlog::info("Copying image channels...");
            Timer timer;
            // the code below works for both interleaved (RGBA) and planar (YA) channel layouts
            for (int p = 0; p < num_planes; ++p)
            {
                int            bytes_per_line = 0;
                const uint8_t *pixels         = himage.get_plane(out_planes[p], &bytes_per_line);
                int            bpp_storage    = himage.get_bits_per_pixel(out_planes[p]);
                int            bpp            = himage.get_bits_per_pixel_range(out_planes[p]);
                spdlog::info("Bits per pixel: {} {}", bpp, bpp_storage);
                spdlog::info("Bytes per line: {}", bytes_per_line);
                if (bpp_storage != cpp * 16)
                    throw runtime_error(
                        fmt::format("HEIF: Got {} bits per pixel, but expected {}", bpp_storage, cpp * 16));
                if (p == 0)
                    image->metadata["bit depth"] = fmt::format("{}-bit ({} bpc)", size.z * bpp, bpp);

                float bppDiv = 1.f / ((1 << bpp) - 1);

                // copy pixels into a contiguous float buffer and normalize values to [0,1]
                vector<float> float_pixels(size.x * size.y * cpp);
                for (int y = 0; y < size.y; ++y)
                {
                    auto row = reinterpret_cast<const uint16_t *>(pixels + y * bytes_per_line);
                    for (int x = 0; x < size.x; ++x)
                        for (int c = 0; c < cpp; ++c)
                            float_pixels[(y * size.x + x) * cpp + c] = row[cpp * x + c] * bppDiv;
                }

                bool colors_linearized = false;

                // only prefer the nclx if it exists and it specifies an HDR transfer function
                bool prefer_icc =
                    !nclx || (nclx->transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_HLG &&
                              nclx->transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_PQ);

                string tf_description;
                float2 red, green, blue, white;
                // for SDR profiles, try to transform the interleaved data using the icc profile.
                // Then try the nclx profile
                if ((prefer_icc && icc::linearize_colors(float_pixels.data(), int3{size.xy(), cpp}, icc_profile,
                                                         &tf_description, &red, &green, &blue, &white)) ||
                    linearize_colors(float_pixels.data(), size, nclx, &tf_description, &red, &green, &blue, &white))
                {
                    Imf::addChromaticities(image->header, {Imath::V2f(red.x, red.y), Imath::V2f(green.x, green.y),
                                                           Imath::V2f(blue.x, blue.y), Imath::V2f(white.x, white.y)});
                    image->metadata["transfer function"] = tf_description;
                    colors_linearized                    = true;
                }
                else
                    image->metadata["transfer function"] = "unknown";

                // copy the interleaved float pixels into the channels
                for (int c = 0; c < cpp; ++c)
                    image->channels[p * cpp + c].copy_from_interleaved(float_pixels.data(), size.x, size.y, cpp, c,
                                                                       [](float v) { return v; });
            }

            spdlog::info("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));
            images.emplace_back(image);
        }
    }
    catch (const heif::Error &err)
    {
        std::string e = err.get_message();
        throw invalid_argument{fmt::format("HEIF: {}", e.empty() ? "unknown exception" : e)};
    }
    catch (const exception &err)
    {
        std::string e = err.what();
        throw invalid_argument{fmt::format("HEIF: {}", e.empty() ? "unknown exception" : e)};
    }
    catch (...)
    {
        throw invalid_argument{"HEIF: unknown exception"};
    }

    return images;
}

bool is_heif_image(istream &is) noexcept
{
    bool ret = false;
    try
    {
        uint8_t magic[12];
        is.read(reinterpret_cast<char *>(magic), sizeof(magic));

        heif_filetype_result filetype_check = heif_check_filetype(magic, std::min(sizeof(magic), (size_t)is.gcount()));
        if (filetype_check == heif_filetype_no)
            throw invalid_argument{"HEIF: Not a HEIF/AVIF file"};
        ret = true;
        if (filetype_check == heif_filetype_yes_unsupported)
            spdlog::warn("This is an unsupported HEIF/AVIF file. Reading will probably fail.");
    }
    catch (const exception &e)
    {
        spdlog::debug("Cannot load image with libheif: {}", e.what());
        ret = false;
    }

    is.clear();
    is.seekg(0);
    return ret;
}

#endif
