//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>

#include "exif.h"
#include "imgui.h"

using namespace std;

#ifndef HDRVIEW_ENABLE_HEIF

bool is_heif_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_heif_image(istream &is, string_view filename, string_view channel_selector)
{
    throw runtime_error("HEIF/AVIF support not enabled in this build.");
}

#else

#include "colorspace.h"
#include "heif.h"
#include "icc.h"
#include "timer.h"
#include <ImfHeader.h>
#include <ImfStandardAttributes.h>
#include <libheif/heif.h>
#include <libheif/heif_cxx.h>

static bool linearize_colors(float *pixels, int3 size, heif_color_profile_nclx *nclx, string *tf_description = nullptr,
                             Chromaticities *c = nullptr)
{
    if (!nclx)
        return false;

    if (c)
    {
        c->red   = float2(nclx->color_primary_red_x, nclx->color_primary_red_y);
        c->green = float2(nclx->color_primary_green_x, nclx->color_primary_green_y);
        c->blue  = float2(nclx->color_primary_blue_x, nclx->color_primary_blue_y);
        c->white = float2(nclx->color_primary_white_x, nclx->color_primary_white_y);
    }

    float            gamma = 1.f;
    TransferFunction tf    = transfer_function_from_cicp((int)nclx->transfer_characteristics, &gamma);

    if (tf == TransferFunction_Unknown)
        spdlog::warn("HEIF: cICP transfer function ({}) is not recognized, assuming sRGB",
                     (int)nclx->transfer_characteristics);

    if (tf_description)
        *tf_description = transfer_function_name(tf);

    to_linear(pixels, size, tf, gamma);

    return true;
}

// Add preferred colorspace to header
static auto colorspace_name(heif_colorspace cs)
{
    switch (cs)
    {
    case heif_colorspace_YCbCr: return "YCbCr";
    case heif_colorspace_RGB: return "RGB";
    case heif_colorspace_monochrome: return "Monochrome";
    case heif_colorspace_nonvisual: return "Nonvisual";
    case heif_colorspace_undefined: return "Undefined";
    default: return "Unknown";
    }
};

// Add preferred chroma to header
static auto chroma_name(heif_chroma ch)
{
    switch (ch)
    {
    case heif_chroma_monochrome: return "Monochrome";
    case heif_chroma_420: return "4:2:0";
    case heif_chroma_422: return "4:2:2";
    case heif_chroma_444: return "4:4:4";
    case heif_chroma_interleaved_RGB: return "Interleaved RGB";
    case heif_chroma_interleaved_RGBA: return "Interleaved RGBA";
    case heif_chroma_interleaved_RRGGBB_BE: return "Interleaved RRGGBB (BE)";
    case heif_chroma_interleaved_RRGGBBAA_BE: return "Interleaved RRGGBBAA (BE)";
    case heif_chroma_interleaved_RRGGBB_LE: return "Interleaved RRGGBB (LE)";
    case heif_chroma_interleaved_RRGGBBAA_LE: return "Interleaved RRGGBBAA (LE)";
    case heif_chroma_undefined: return "Undefined";
    default: return "Unknown";
    }
};

vector<ImagePtr> load_heif_image(istream &is, string_view filename, string_view channel_selector)
{
    ScopedMDC mdc{"IO", "HEIF"};
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
            fmt::format("Failed to read : {} bytes, read : {} bytes", raw_size, (size_t)is.gcount())};

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

        spdlog::info("Found {} subimages", num_subimages);

        ImGuiTextFilter filter{string(channel_selector).c_str()};
        filter.Build();

        // just get the primary image for now
        for (int subimage = 0; subimage < num_subimages; ++subimage)
        {
            spdlog::info("Loading subimage {}...", subimage);
            auto id = (subimage == 0) ? primary_id : item_ids[subimage - 1];

            if (auto name = fmt::format("{:d}.R,G,B", id); !filter.PassFilter(name.c_str()))
            {
                spdlog::debug("Color channels '{}' filtered out by channel selector '{}'", name, channel_selector);
                continue;
            }

            auto                     ihandle     = ctx->get_image_handle(id);
            auto                     raw_ihandle = ihandle.get_raw_image_handle();
            heif_color_profile_nclx *nclx        = nullptr;
            std::vector<uint8_t>     icc_profile;

            if (ihandle.empty() || !raw_ihandle)
                continue;

            auto err = heif_image_handle_get_nclx_color_profile(raw_ihandle, &nclx);
            if (err.code != heif_error_Ok)
                spdlog::info("No handle-level nclx color profile found");

            if (size_t icc_size = heif_image_handle_get_raw_color_profile_size(raw_ihandle); icc_size != 0)
            {
                spdlog::info("File contains a handle-level ICC profile.");
                icc_profile.resize(icc_size);
                err =
                    heif_image_handle_get_raw_color_profile(raw_ihandle, reinterpret_cast<void *>(icc_profile.data()));
                if (err.code != heif_error_Ok)
                {
                    spdlog::info("Could not read handle-level ICC profile.");
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
            image->partname                = fmt::format("{:d}", id);
            image->file_has_straight_alpha = has_alpha && !ihandle.is_premultiplied_alpha();
            image->metadata["loader"]      = "libheif";
            image->metadata["header"]["nclx profile"] =
                nclx ? json{{"value", 1}, {"string", "present at handle level"}, {"type", "enum"}}
                     : json{{"value", 0}, {"string", "missing"}, {"type", "enum"}};
            image->metadata["header"]["icc profile"] =
                icc_profile.size() ? json{{"value", 1}, {"string", "present at handle level"}, {"type", "enum"}}
                                   : json{{"value", 0}, {"string", "missing"}, {"type", "enum"}};
            image->metadata["header"]["preferred colorspace"] = {
                {"value", int(preferred_colorspace)},
                {"string", fmt::format("{} ({})", colorspace_name(preferred_colorspace), int(preferred_colorspace))},
                {"type", "int"}};
            image->metadata["header"]["preferred chroma"] = {
                {"value", int(preferred_chroma)},
                {"string", fmt::format("{} ({})", chroma_name(preferred_chroma), int(preferred_chroma))},
                {"type", "int"}};

            // try to get exif metadata
            int num_metadata_blocks = heif_image_handle_get_number_of_metadata_blocks(raw_ihandle, "Exif");
            if (num_metadata_blocks > 0)
            {
                spdlog::info("Found {} EXIF metadata block(s). Attempting to decode...", num_metadata_blocks);

                vector<heif_item_id> metadataIDs(num_metadata_blocks);
                heif_image_handle_get_list_of_metadata_block_IDs(raw_ihandle, "Exif", metadataIDs.data(),
                                                                 num_metadata_blocks);
                for (int i = 0; i < num_metadata_blocks; ++i)
                {
                    size_t exif_size = heif_image_handle_get_metadata_size(raw_ihandle, metadataIDs[i]);
                    if (exif_size <= 4)
                    {
                        spdlog::warn("Failed to get size of EXIF data.");
                        continue;
                    }

                    vector<uint8_t> exif_data(exif_size);
                    if (auto error = heif_image_handle_get_metadata(raw_ihandle, metadataIDs[i], exif_data.data());
                        error.code != heif_error_Ok)
                    {
                        spdlog::warn("Failed to read EXIF data: {}", error.message);
                        continue;
                    }

                    try
                    {
                        // exclude the first four bytes, which are the length
                        auto j                  = exif_to_json(exif_data.data() + 4, exif_size - 4);
                        image->metadata["exif"] = j;
                        spdlog::debug("EXIF metadata successfully parsed: {}", image->metadata.dump(2));
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::warn("Exception while parsing EXIF chunk: {}", e.what());
                    }
                }
            }

            spdlog::info("Decoding heif image...");
            auto himage = ihandle.decode_image(out_colorspace, out_chroma);

            if (himage.get_width(out_planes[0]) != size.x || himage.get_height(out_planes[0]) != size.y)
            {
                spdlog::warn("Image size mismatch: {}x{} vs {}x{}", himage.get_width(out_planes[0]),
                             himage.get_height(out_planes[0]), size.x, size.y);
                size.x = himage.get_width(out_planes[0]);
                size.y = himage.get_height(out_planes[0]);
            }

            image->metadata["header"]["decoded colorspace"] = {
                {"value", int(himage.get_colorspace())},
                {"string",
                 fmt::format("{} ({})", colorspace_name(himage.get_colorspace()), int(himage.get_colorspace()))},
                {"type", "int"}};

            image->metadata["header"]["decoded chroma"] = {
                {"value", int(himage.get_chroma_format())},
                {"string",
                 fmt::format("{} ({})", chroma_name(himage.get_chroma_format()), int(himage.get_chroma_format()))},
                {"type", "int"}};

            // A tricky bit is that the C++ API doesn't give us a direct way to get the image ptr, we need to resort
            // to some casting trickery, with knowledge that the C++ heif::Image class consists solely of a
            // std::shared_ptr to a heif_image.
            const heif_image *raw_image = reinterpret_cast<std::shared_ptr<heif_image> *>(&himage)->get();

            // is this needed or will the handle-level functions return a profile even if its at the image level?
            if (!nclx)
            {
                err = heif_image_get_nclx_color_profile(raw_image, &nclx);
                if (err.code == heif_error_Color_profile_does_not_exist)
                    spdlog::warn("No image-level nclx color profile found. Will assume sRGB/IEC 61966-2-1 colorspace.");
                else if (err.code == heif_error_Ok)
                    image->metadata["header"]["nclx profile"] =
                        json{{"value", 2}, {"string", "present at image level"}, {"type", "enum"}};
            }
            if (icc_profile.empty())
            {
                if (size_t icc_size = heif_image_get_raw_color_profile_size(raw_image); icc_size != 0)
                {
                    icc_profile.resize(icc_size);
                    err = heif_image_get_raw_color_profile(raw_image, reinterpret_cast<void *>(icc_profile.data()));
                    if (err.code != heif_error_Ok)
                    {
                        spdlog::info("Could not read image-level ICC profile");
                        icc_profile.clear();
                    }
                    else
                        image->metadata["header"]["icc profile"] =
                            json{{"value", 2}, {"string", "present at image level"}, {"type", "enum"}};
                }
            }

            spdlog::info("Copying image channels...");
            Timer timer;
            // the code below works for both interleaved (RGBA) and planar (YA) channel layouts
            for (int p = 0; p < num_planes; ++p)
            {
                size_t         bytes_per_line = 0;
                const uint8_t *pixels         = himage.get_plane(out_planes[p], &bytes_per_line);
                int            bpp_storage    = himage.get_bits_per_pixel(out_planes[p]);
                int            bpc            = himage.get_bits_per_pixel_range(out_planes[p]);
                spdlog::debug(
                    "Bits per pixel: {}; Bits per pixel storage: {}; Channels per pixel: {}; Bytes per line: {}", bpc,
                    bpp_storage, cpp, bytes_per_line);
                if (bpp_storage != cpp * 16 && bpp_storage != cpp * 8)
                    throw runtime_error(fmt::format("Unsupported bits per pixel: {}", bpp_storage));
                if (p == 0)
                    image->metadata["pixel format"] = fmt::format("{}-bit ({} bpc)", size.z * bpc, bpc);
                float bpc_div = 1.f / ((1 << bpc) - 1);

                // copy pixels into a contiguous float buffer and normalize values to [0,1]
                spdlog::debug("Copying to contiguous float buffer");
                vector<float> float_pixels(size.x * size.y * cpp);
                bool          is_16bit = bpp_storage == cpp * 16;
                for (int y = 0; y < size.y; ++y)
                {
                    auto row8  = reinterpret_cast<const uint8_t *>(pixels + y * bytes_per_line);
                    auto row16 = reinterpret_cast<const uint16_t *>(pixels + y * bytes_per_line);
                    for (int x = 0; x < size.x; ++x)
                        for (int c = 0; c < cpp; ++c)
                            float_pixels[(y * size.x + x) * cpp + c] =
                                bpc_div * (is_16bit ? row16[cpp * x + c] : row8[cpp * x + c]);
                }
                spdlog::debug("done copying to continuguous float buffer");

                // only prefer the nclx if it exists and it specifies an HDR transfer function
                bool prefer_icc = // false;
                    !nclx || (nclx->transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_HLG &&
                              nclx->transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_PQ);

                string         tf_description;
                Chromaticities chr;
                // for SDR profiles, try to transform the interleaved data using the icc profile.
                // Then try the nclx profile
                if ((prefer_icc && icc::linearize_colors(float_pixels.data(), int3{size.xy(), cpp}, icc_profile,
                                                         &tf_description, &chr)) ||
                    linearize_colors(float_pixels.data(), int3{size.xy(), cpp}, nclx, &tf_description, &chr))
                {
                    image->chromaticities                = chr;
                    image->metadata["transfer function"] = tf_description;
                }
                else
                    image->metadata["transfer function"] = transfer_function_name(TransferFunction_Unknown);

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
        throw invalid_argument{e.empty() ? "unknown exception" : e};
    }
    catch (const exception &err)
    {
        std::string e = err.what();
        throw invalid_argument{e.empty() ? "unknown exception" : e};
    }
    catch (...)
    {
        throw invalid_argument{"unknown exception"};
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

        heif_filetype_result filetype_check =
            heif_check_filetype(magic, std::min((int)sizeof(magic), (int)is.gcount()));
        if (filetype_check == heif_filetype_no)
            throw invalid_argument{"Not a HEIF/AVIF file"};
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
