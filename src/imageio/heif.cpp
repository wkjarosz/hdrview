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

#include "cms.h"
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

#ifdef HDRVIEW_ENABLE_LCMS2
                // transform the interleaved data using the icc profile
                if (!icc_profile.empty() && prefer_icc)
                {
                    spdlog::info("Linearizing pixel values using image's ICC color profile.");

                    cmsUInt32Number format;
                    switch (cpp)
                    {
                    case 1:
                        format = TYPE_GRAY_FLT;
                        spdlog::info("HEIF: grayscale");
                        break;
                    case 2: spdlog::error("HEIF: unexpected channels per plane: 2"); break;
                    case 3:
                        format = TYPE_RGB_FLT;
                        spdlog::info("HEIF: RGB");
                        break;
                    case 4:
                    default:
                        format = TYPE_RGBA_FLT;
                        spdlog::info("HEIF: RGBA");
                        break;
                    }

                    auto profile_in = cms::open_profile_from_mem(icc_profile);

                    cmsCIExyY       whitepoint;
                    cmsCIExyYTRIPLE primaries;
                    cms::extract_chromaticities(profile_in, primaries, whitepoint);
                    // print out the chromaticities
                    spdlog::info("HEIF: Applying chromaticities deduced from image's ICC profile:");
                    spdlog::info("    Red primary: ({}, {})", primaries.Red.x, primaries.Red.y);
                    spdlog::info("    Green primary: ({}, {})", primaries.Green.x, primaries.Green.y);
                    spdlog::info("    Blue primary: ({}, {})", primaries.Blue.x, primaries.Blue.y);
                    spdlog::info("    White point: ({}, {})", whitepoint.x, whitepoint.y);
                    Imf::addChromaticities(image->header, {Imath::V2f(primaries.Red.x, primaries.Red.y),
                                                           Imath::V2f(primaries.Green.x, primaries.Green.y),
                                                           Imath::V2f(primaries.Blue.x, primaries.Blue.y),
                                                           Imath::V2f(whitepoint.x, whitepoint.y)});

                    auto profile_out = cms::create_linear_RGB_profile(whitepoint, primaries);

                    if (profile_in && profile_out)
                    {
                        spdlog::info("HEIF: ICC profile description: '{}'", cms::profile_description(profile_in));
                        if (auto xform = cms::Transform{cmsCreateTransform(profile_in.get(), format, profile_out.get(),
                                                                           format, INTENT_ABSOLUTE_COLORIMETRIC,
                                                                           cpp == 4 ? cmsFLAGS_COPY_ALPHA : 0)})
                        {
                            cmsDoTransform(xform.get(), float_pixels.data(), float_pixels.data(), size.x * size.y);
                            colors_linearized = true;
                        }
                        else
                            spdlog::error("HEIF: Could not create color transformation.");
                    }
                    else
                        spdlog::error("HEIF: Could not create ICC profile for color transformation.");
                }
#endif // HDRVIEW_ENABLE_LCMS2

                // copy the interleaved float pixels into the channels
                for (int c = 0; c < cpp; ++c)
                    image->channels[p * cpp + c].copy_from_interleaved(float_pixels.data(), size.x, size.y, cpp, c,
                                                                       [](float v) { return v; });

                // now check the nclx profile and apply the transfer function
                if (nclx && !colors_linearized)
                {
                    spdlog::info("HEIF: Applying chromaticities stored in image's NCLX profile.");
                    spdlog::info("    Red primary: ({}, {})", nclx->color_primary_red_x, nclx->color_primary_red_y);
                    spdlog::info("    Green primary: ({}, {})", nclx->color_primary_green_x,
                                 nclx->color_primary_green_y);
                    spdlog::info("    Blue primary: ({}, {})", nclx->color_primary_blue_x, nclx->color_primary_blue_y);
                    spdlog::info("    White point: ({}, {})", nclx->color_primary_white_x, nclx->color_primary_white_y);

                    Imf::addChromaticities(image->header, {{nclx->color_primary_red_x, nclx->color_primary_red_y},
                                                           {nclx->color_primary_green_x, nclx->color_primary_green_y},
                                                           {nclx->color_primary_blue_x, nclx->color_primary_blue_y},
                                                           {nclx->color_primary_white_x, nclx->color_primary_white_y}});

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
                    case heif_transfer_characteristic_ITU_R_BT_709_5:
                        spdlog::info("HEIF: Applying BT.709 transfer function");
                        break;
                    default:
                        spdlog::error("HEIF: Transfer characteristics {} not implemented. Applying sRGB/IEC 61966-2-1 "
                                      "transfer function instead.",
                                      (int)nclx->transfer_characteristics);
                        break;
                    }

                    // apply transfer function
                    if (image->channels.size() <= 2)
                    {
                        if (nclx && nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_2100_0_PQ)
                            image->channels[0].apply([](float v, int x, int y) { return EOTF_PQ(v) / 255.f; });
                        else if (nclx &&
                                 nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_2100_0_HLG)
                            image->channels[0].apply([](float v, int x, int y) { return EOTF_HLG(v, v) / 255.f; });
                        else if (nclx && nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_709_5)
                            image->channels[0].apply([](float v, int x, int y) { return Rec2020ToLinear(v); });
                        else if (nclx)
                            image->channels[0].apply([](float v, int x, int y) { return SRGBToLinear(v); });
                    }
                    else if (image->channels.size() == 3 || image->channels.size() == 4)
                    {
                        // HLG needs to operate on all three channels at once
                        if (nclx && nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_2100_0_HLG)
                        {
                            int block_size = std::max(1, 1024 * 1024 / size.x);
                            parallel_for(blocked_range<int>(0, size.y, block_size),
                                         [r = &image->channels[0], g = &image->channels[1], b = &image->channels[2]](
                                             int begin_y, int end_y, int unit_index, int thread_index)
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
                        // The other transfer functions operate independently on color channels
                        else
                        {
                            for (int c = 0; c < 3; ++c)
                                if (nclx &&
                                    nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_2100_0_PQ)
                                    image->channels[c].apply([](float v, int x, int y) { return EOTF_PQ(v) / 255.f; });
                                else if (nclx &&
                                         nclx->transfer_characteristics == heif_transfer_characteristic_ITU_R_BT_709_5)
                                    image->channels[c].apply([](float v, int x, int y) { return Rec2020ToLinear(v); });
                                else if (nclx &&
                                         nclx->transfer_characteristics == heif_transfer_characteristic_IEC_61966_2_1)
                                    image->channels[c].apply([](float v, int x, int y) { return SRGBToLinear(v); });
                        }
                    }
                    else
                        spdlog::warn("HEIF: Don't know how to apply transfer function to {} channels",
                                     image->channels.size());
                }
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
