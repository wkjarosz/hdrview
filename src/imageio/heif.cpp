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
#include "libheif/heif.h"
#include "libheif/heif_cxx.h"
#include "timer.h"
#include <ImfHeader.h>
#include <ImfStandardAttributes.h>

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
            auto id          = (subimage == 0) ? primary_id : item_ids[subimage - 1];
            auto ihandle     = ctx->get_image_handle(id);
            auto raw_ihandle = ihandle.get_raw_image_handle();

            if (ihandle.empty() || !raw_ihandle)
                continue;

            heif_color_profile_nclx *nclx = nullptr;
            auto                     err  = heif_image_handle_get_nclx_color_profile(raw_ihandle, &nclx);
            if (err.code == heif_error_Color_profile_does_not_exist)
                spdlog::info("HEIF: No handle-level nclx color profile found");

            if (heif_image_handle_get_raw_color_profile_size(raw_ihandle) != 0)
                spdlog::warn("HEIF: File contains an ICC profile, but this is not supported; Ignoring.");

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

            spdlog::info("Decoding heif image...");
            auto himage = ihandle.decode_image(out_colorspace, out_chroma);

            if (himage.get_width(out_planes[0]) != size.x || himage.get_height(out_planes[0]) != size.y)
            {
                spdlog::warn("HEIF: Image size mismatch: {}x{} vs {}x{}", himage.get_width(out_planes[0]),
                             himage.get_height(out_planes[0]), size.x, size.y);
                size.x = himage.get_width(out_planes[0]);
                size.y = himage.get_height(out_planes[0]);
            }

            if (!nclx)
            {
                // Try to get the raw nclx color profile from the image. A tricky bit is that
                // the C++ API doesn't give us a direct way to get the image ptr, we
                // need to resort to some casting trickery, with knowledge that the C++
                // heif::Image class consists solely of a std::shared_ptr to a
                // heif_image.
                const heif_image *raw_image = reinterpret_cast<std::shared_ptr<heif_image> *>(&himage)->get();
                err                         = heif_image_get_nclx_color_profile(raw_image, &nclx);
                if (err.code == heif_error_Color_profile_does_not_exist)
                    spdlog::warn(
                        "HEIF: No image-level nclx color profile found. Will assume sRGB/IEC 61966-2-1 colorspace.");
            }

            if (nclx)
            {
                switch (nclx->transfer_characteristics)
                {
                case heif_transfer_characteristic_unspecified:
                    spdlog::warn("HEIF: Doesn't specify a transfer function; assuming sRGB");
                    break;
                case heif_transfer_characteristic_IEC_61966_2_1:
                    spdlog::info("HEIF: Applying sRGB/IEC 61966-2-1 transfer function");
                    break;
                case heif_transfer_characteristic_ITU_R_BT_2100_0_PQ:
                    spdlog::info("HEIF: Applying PQ transfer function");
                    break;
                case heif_transfer_characteristic_ITU_R_BT_2100_0_HLG:
                    spdlog::info("HEIF: Applying HLG transfer function");
                    break;
                default:
                    spdlog::error("HEIF: Transfer characteristics {} not implemented. Applying sRGB/IEC 61966-2-1 "
                                  "transfer function instead.",
                                  (int)nclx->transfer_characteristics);
                    break;
                }

                spdlog::info("Adding chromaticities to image header...");
                Imf::addChromaticities(image->header, {{nclx->color_primary_red_x, nclx->color_primary_red_y},
                                                       {nclx->color_primary_green_x, nclx->color_primary_green_y},
                                                       {nclx->color_primary_blue_x, nclx->color_primary_blue_y},
                                                       {nclx->color_primary_white_x, nclx->color_primary_white_y}});
            }
            else
                spdlog::warn("HEIF: No color profile found, assuming Rec. 709/sRGB primaries and whitepoint.");

            spdlog::info("Copying image channels...");
            Timer timer;
            // the code below works for both interleaved (RGBA) and planar (YA) channel layouts
            for (int p = 0; p < num_planes; ++p)
            {
                int  bytes_per_line = 0;
                auto pixels         = himage.get_plane(out_planes[p], &bytes_per_line);
                int  bpp_storage    = himage.get_bits_per_pixel(out_planes[p]);
                int  bpp            = himage.get_bits_per_pixel_range(out_planes[p]);
                spdlog::debug("Bits per pixel: {} {}", bpp, bpp_storage);
                spdlog::debug("Bytes per line: {}", bytes_per_line);

                float bppDiv = 1.f / ((1 << bpp) - 1);

                // iterate over the channels in the plane
                for (int c = 0; c < cpp; ++c)
                    if (bpp_storage <= 8)
                        image->channels[p * cpp + c].copy_from_interleaved(
                            reinterpret_cast<uint8_t *>(pixels), size.x, size.y, cpp, c,
                            [bppDiv](uint8_t v) { return v * bppDiv; }, bytes_per_line / sizeof(uint8_t));
                    else
                        image->channels[p * cpp + c].copy_from_interleaved(
                            reinterpret_cast<uint16_t *>(pixels), size.x, size.y, cpp, c,
                            [bppDiv](uint16_t v) { return v * bppDiv; }, bytes_per_line / sizeof(uint16_t));

                // apply transfer function
                if (image->channels.size() <= 2)
                {
                    if (nclx && nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_2100_0_PQ)
                        image->channels[0].apply([](float v, int x, int y) { return EOTF_PQ(v) / 255.f; });
                    else if (nclx && nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_2100_0_HLG)
                        image->channels[0].apply([](float v, int x, int y) { return EOTF_HLG(v, v) / 255.f; });
                    else
                        image->channels[0].apply([](float v, int x, int y) { return SRGBToLinear(v); });
                }
                else if (image->channels.size() == 3 || image->channels.size() == 4)
                {
                    // HLG needs to operate on all three channels at once
                    if (nclx && nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_2100_0_HLG)
                    {
                        int block_size = std::max(1, 1024 * 1024 / size.x);
                        parallel_for(blocked_range<int>(0, size.y, block_size),
                                     [r = &image->channels[0], g = &image->channels[1],
                                      b = &image->channels[2]](int begin_y, int end_y, int unit_index, int thread_index)
                                     {
                                         for (int y = begin_y; y < end_y; ++y)
                                             for (int x = 0; x < r->width(); ++x)
                                             {
                                                 auto E_p   = float3{(*r)(x, y), (*g)(x, y), (*b)(x, y)};
                                                 auto E     = EOTF_HLG(E_p) / 255.f;
                                                 (*r)(x, y) = E[0];
                                                 (*g)(x, y) = E[1];
                                                 (*b)(x, y) = E[2];
                                             }
                                     });
                    }
                    // PQ and sRGB operate independently on color channels
                    else
                    {
                        for (int c = 0; c < 3; ++c)
                            if (nclx &&
                                nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_2100_0_PQ)
                                image->channels[c].apply([](float v, int x, int y) { return EOTF_PQ(v) / 255.f; });
                            else
                                image->channels[c].apply([](float v, int x, int y) { return SRGBToLinear(v); });
                    }
                }
                else
                    spdlog::warn("HEIF: Don't know how to apply transfer function to {} channels",
                                 image->channels.size());
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
