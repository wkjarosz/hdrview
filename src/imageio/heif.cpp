//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
#include <cstring>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>

#include "app.h"
#include "imgui.h"

using namespace std;

#ifndef HDRVIEW_ENABLE_LIBHEIF

bool is_heif_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_heif_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("HEIF/AVIF support not enabled in this build.");
}

void save_heif_image(const Image &, std::ostream &, std::string_view, float, int, bool, int, bool, bool,
                     TransferFunction, float)
{
    throw std::runtime_error("HEIF/AVIF support not enabled in this build.");
}

void save_heif_image(const Image &, std::ostream &, std::string_view, const struct HEIFSaveOptions *)
{
    throw std::runtime_error("HEIF/AVIF support not enabled in this build.");
}

HEIFSaveOptions *heif_parameters_gui() { return nullptr; }

#else

#include "colorspace.h"
#include "exif.h"
#include "fonts.h"
#include "heif.h"
#include "icc.h"
#include "imgui_ext.h"
#include "json.h"
#include "timer.h"
#include <cstdint>
#include <cstdio>

#include <libheif/heif.h>
#include <memory>

using HeifContextPtr     = std::shared_ptr<heif_context>;
using HeifImagePtr       = std::shared_ptr<heif_image>;
using HeifImageHandlePtr = std::shared_ptr<heif_image_handle>;
using HeifEncoderPtr     = std::shared_ptr<heif_encoder>;
using HeifNCLXPtr        = std::shared_ptr<heif_color_profile_nclx>;

struct HEIFSaveOptions
{
    float             gain      = 1.f;
    int               quality   = 95;
    bool              lossless  = false;
    bool              use_alpha = true;
    TransferFunction_ tf        = TransferFunction_sRGB;
    float             gamma     = 2.2f;
    size_t            encoder   = 0u;
};

static HEIFSaveOptions s_opts;

static std::vector<const heif_encoder_descriptor *> s_encoder_descriptors;
static std::vector<HeifEncoderPtr>                  s_encoders;
static bool                                         s_encoders_initialized = false;

static void init_heif_supported_formats()
{
    if (s_encoders_initialized)
        return;

    int num = heif_get_encoder_descriptors(heif_compression_undefined, nullptr, nullptr, 0);
    if (num > 0)
    {
        std::vector<const heif_encoder_descriptor *> descriptors(num);
        int got = heif_get_encoder_descriptors(heif_compression_undefined, nullptr, descriptors.data(), num);
        descriptors.resize(got);
        s_encoder_descriptors = descriptors;

        // Initialize one shared encoder instance per compression format so GUI can
        // edit parameters directly on the C encoder objects.
        for (auto desc : s_encoder_descriptors)
        {
            heif_encoder *enc = nullptr;
            if (heif_context_get_encoder(nullptr, desc, &enc).code == heif_error_Ok && enc)
                s_encoders.push_back(HeifEncoderPtr(enc, [](heif_encoder *e) { heif_encoder_release(e); }));
            else
                s_encoders.push_back(nullptr);
        }
    }

    s_encoders_initialized = true;
}

// Helper: throw a std::runtime_error when a libheif call returns an error
static inline void throw_on_error(const struct heif_error &e, const char *ctx_msg = "libheif error")
{
    if (e.code != heif_error_Ok)
    {
        if (e.message && e.message[0])
            throw std::runtime_error(std::string(e.message));
        else
            throw std::runtime_error(std::string(ctx_msg));
    }
}

static heif_transfer_characteristics transfer_function_to_heif(TransferFunction_ tf)
{
    switch (tf)
    {
    case TransferFunction_Linear: return heif_transfer_characteristic_linear;
    case TransferFunction_sRGB: return heif_transfer_characteristic_IEC_61966_2_1;
    case TransferFunction_ITU: return heif_transfer_characteristic_ITU_R_BT_709_5;
    case TransferFunction_BT2100_PQ: return heif_transfer_characteristic_ITU_R_BT_2100_0_PQ;
    case TransferFunction_BT2100_HLG: return heif_transfer_characteristic_ITU_R_BT_2100_0_HLG;
    case TransferFunction_ST240: return heif_transfer_characteristic_SMPTE_240M;
    case TransferFunction_Log100: return heif_transfer_characteristic_logarithmic_100;
    case TransferFunction_Log100_Sqrt10: return heif_transfer_characteristic_logarithmic_100_sqrt10;
    case TransferFunction_IEC61966_2_4: return heif_transfer_characteristic_IEC_61966_2_4;
    case TransferFunction_DCI_P3: return heif_transfer_characteristic_SMPTE_ST_428_1;
    default: return heif_transfer_characteristic_IEC_61966_2_1; // fallback to sRGB
    }
}

static bool is_heif_transfer_supported(TransferFunction_ tf)
{
    switch (transfer_function_to_heif(tf))
    {
    case heif_transfer_characteristic_linear:
    case heif_transfer_characteristic_IEC_61966_2_1:
    case heif_transfer_characteristic_ITU_R_BT_709_5:
    case heif_transfer_characteristic_ITU_R_BT_2100_0_PQ:
    case heif_transfer_characteristic_ITU_R_BT_2100_0_HLG:
    case heif_transfer_characteristic_SMPTE_240M:
    case heif_transfer_characteristic_logarithmic_100:
    case heif_transfer_characteristic_logarithmic_100_sqrt10:
    case heif_transfer_characteristic_IEC_61966_2_4:
    case heif_transfer_characteristic_SMPTE_ST_428_1: return true;
    default: return false;
    }
}

static bool linearize_colors(float *pixels, int3 size, const heif_color_profile_nclx *nclx,
                             string *tf_description = nullptr, Chromaticities *c = nullptr)
{
    if (!nclx)
        return false;

    if (c)
        *c = chromaticities_from_cicp((int)nclx->color_primaries);

    float             gamma = 1.f;
    TransferFunction_ tf    = transfer_function_from_cicp((int)nclx->transfer_characteristics, &gamma);

    if (tf == TransferFunction_Unspecified)
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

vector<ImagePtr> load_heif_image(istream &is, string_view filename, const ImageLoadOptions &opts)
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
        // Create C API context and read from memory
        HeifContextPtr ctx(heif_context_alloc(), [](heif_context *c) { heif_context_free(c); });
        if (!ctx)
            throw std::runtime_error("Failed to allocate heif context");

        throw_on_error(heif_context_read_from_memory_without_copy(
                           ctx.get(), reinterpret_cast<const void *>(raw_data.data()), raw_size, nullptr),
                       "Failed to read HEIF memory");

        heif_item_id primary_id = 0;
        throw_on_error(heif_context_get_primary_image_ID(ctx.get(), &primary_id), "Failed to get primary image ID");

        int                       num_top = heif_context_get_number_of_top_level_images(ctx.get());
        std::vector<heif_item_id> item_ids(num_top);
        if (num_top > 0)
            heif_context_get_list_of_top_level_image_IDs(ctx.get(), item_ids.data(), num_top);

        // remove the primary item from the list of all items
        for (size_t i = 0; i < item_ids.size(); ++i)
            if (item_ids[i] == primary_id)
            {
                item_ids.erase(item_ids.begin() + i);
                break;
            }

        int num_subimages = 1 + int(item_ids.size());

        spdlog::info("Found {} subimages", num_subimages);

        ImGuiTextFilter filter{opts.channel_selector.c_str()};
        filter.Build();

        // just get the primary image for now
        for (int subimage = 0; subimage < num_subimages; ++subimage)
        {
            spdlog::info("Loading subimage {}...", subimage);
            auto id = (subimage == 0) ? primary_id : item_ids[subimage - 1];

            if (auto name = fmt::format("{:d}.R,G,B", id); !filter.PassFilter(name.c_str()))
            {
                spdlog::debug("Color channels '{}' filtered out by channel selector '{}'", name, opts.channel_selector);
                continue;
            }

            heif_image_handle *raw_ihandle = nullptr;
            if (heif_context_get_image_handle(ctx.get(), id, &raw_ihandle).code != heif_error_Ok || !raw_ihandle)
            {
                spdlog::warn("Failed to get image handle for id {}", id);
                continue;
            }
            HeifImageHandlePtr ihandle(raw_ihandle, [](heif_image_handle *h) { heif_image_handle_release(h); });

            heif_colorspace preferred_colorspace = heif_colorspace_undefined;
            heif_chroma     preferred_chroma     = heif_chroma_undefined;
            heif_image_handle_get_preferred_decoding_colorspace(ihandle.get(), &preferred_colorspace,
                                                                &preferred_chroma);
            spdlog::info("Preferred decoding colorspace: {}, chroma: {}", (int)preferred_colorspace,
                         (int)preferred_chroma);

            int3 size{heif_image_handle_get_width(ihandle.get()), heif_image_handle_get_height(ihandle.get()), 0};
            bool has_alpha = heif_image_handle_has_alpha_channel(ihandle.get()) != 0;

            heif_chroma     out_chroma;
            heif_colorspace out_colorspace;
            heif_channel    out_planes[2] = {heif_channel_Y, heif_channel_Alpha};
            int             cpp           = 0; // channels per plane
            int             num_planes    = 1;
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
            image->file_has_straight_alpha = has_alpha && !heif_image_handle_is_premultiplied_alpha(ihandle.get());

            // Add file-level metadata: MIME type and main brand (from libheif helpers)
            const char *mime =
                heif_get_file_mime_type(reinterpret_cast<const uint8_t *>(raw_data.data()),
                                        (int)std::min<size_t>(raw_size, std::numeric_limits<int>::max()));
            char main_brand[5] = {0, 0, 0, 0, 0};
            heif_brand_to_fourcc(heif_read_main_brand(reinterpret_cast<const uint8_t *>(raw_data.data()),
                                                      (int)std::min<size_t>(raw_size, std::numeric_limits<int>::max())),
                                 main_brand);

            image->metadata["header"]["mime type"]  = {{"value", std::string(mime ? mime : "")},
                                                       {"string", std::string(mime ? mime : "")},
                                                       {"type", "string"}};
            image->metadata["header"]["main brand"] = {
                {"value", std::string(main_brand)}, {"string", std::string(main_brand)}, {"type", "string"}};

            image->metadata["loader"] = "libheif" + std::string(" (" + std::string(main_brand) + ")");
            image->metadata["header"]["preferred colorspace"] = {
                {"value", int(preferred_colorspace)},
                {"string", fmt::format("{} ({})", colorspace_name(preferred_colorspace), int(preferred_colorspace))},
                {"type", "int"}};
            image->metadata["header"]["preferred chroma"] = {
                {"value", int(preferred_chroma)},
                {"string", fmt::format("{} ({})", chroma_name(preferred_chroma), int(preferred_chroma))},
                {"type", "int"}};

            // EXIF metadata (handle-level)
            if (int num_blocks = heif_image_handle_get_number_of_metadata_blocks(ihandle.get(), "Exif"); num_blocks > 0)
            {
                spdlog::info("Found {} EXIF metadata block(s). Attempting to decode...", num_blocks);
                std::vector<heif_item_id> block_IDs(num_blocks);
                heif_image_handle_get_list_of_metadata_block_IDs(ihandle.get(), "Exif", block_IDs.data(), num_blocks);
                for (auto block_ID : block_IDs)
                {
                    size_t data_size = heif_image_handle_get_metadata_size(ihandle.get(), block_ID);
                    if (data_size <= 4)
                    {
                        spdlog::warn("Failed to get size of EXIF data.");
                        continue;
                    }
                    try
                    {
                        std::vector<uint8_t> exif_data(data_size);
                        throw_on_error(heif_image_handle_get_metadata(ihandle.get(), block_ID, exif_data.data()),
                                       "Failed to get EXIF metadata block");
                        // EXIF data block includes 4-byte length prefix that we need to skip
                        auto j                  = exif_to_json(exif_data.data() + 4, exif_data.size() - 4);
                        image->metadata["exif"] = j;
                        spdlog::debug("EXIF metadata successfully parsed: {}", image->metadata.dump(2));
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::warn(e.what());
                    }
                }
            }

            spdlog::info("Decoding heif image...");
            HeifImagePtr himage(
                [&]
                {
                    heif_image *raw_img = nullptr;
                    throw_on_error(heif_decode_image(ihandle.get(), &raw_img, out_colorspace, out_chroma, nullptr),
                                   "Failed to decode HEIF image");
                    return raw_img;
                }(),
                [](heif_image *h) { heif_image_release(h); });

            int img_w = heif_image_get_width(himage.get(), out_planes[0]);
            int img_h = heif_image_get_height(himage.get(), out_planes[0]);
            if (img_w != size.x || img_h != size.y)
            {
                spdlog::warn("Image size mismatch: {}x{} vs {}x{}", img_w, img_h, size.x, size.y);
                size.x = img_w;
                size.y = img_h;
            }

            image->metadata["header"]["decoded colorspace"] = {
                {"value", int(heif_image_get_colorspace(himage.get()))},
                {"string", fmt::format("{} ({})", colorspace_name(heif_image_get_colorspace(himage.get())),
                                       int(heif_image_get_colorspace(himage.get())))},
                {"type", "int"}};
            image->metadata["header"]["decoded chroma"] = {
                {"value", int(heif_image_get_chroma_format(himage.get()))},
                {"string", fmt::format("{} ({})", chroma_name(heif_image_get_chroma_format(himage.get())),
                                       int(heif_image_get_chroma_format(himage.get())))},
                {"type", "int"}};

            // NCLX: prefer image-level then fall back to handle-level
            auto nclx = HeifNCLXPtr(
                [&]
                {
                    heif_color_profile_nclx *nclx_raw = nullptr;
                    if (auto err1 = heif_image_get_nclx_color_profile(himage.get(), &nclx_raw);
                        err1.code == heif_error_Ok)
                    {
                        image->metadata["header"]["nclx profile"] =
                            json{{"value", 2}, {"string", "present at image level"}, {"type", "enum"}};
                        return nclx_raw;
                    }
                    else
                    {
                        nclx_raw = nullptr;
                        spdlog::warn("No image-level nclx color profile found: {}. Trying at handle level",
                                     err1.message);
                        if (auto err = heif_image_handle_get_nclx_color_profile(ihandle.get(), &nclx_raw);
                            err.code == heif_error_Ok)
                        {
                            image->metadata["header"]["nclx profile"] =
                                json{{"value", 1}, {"string", "present at handle level"}, {"type", "enum"}};
                            return nclx_raw;
                        }
                        else
                        {
                            spdlog::warn("No handle-level nclx color profile found either: {}", err.message);
                            image->metadata["header"]["nclx profile"] =
                                json{{"value", 0}, {"string", "missing"}, {"type", "enum"}};
                            return heif_nclx_color_profile_alloc();
                        }
                    }
                }(),
                [](heif_color_profile_nclx *p) { heif_nclx_color_profile_free(p); });

            // get the icc profile. first try the image level, then the handle level
            std::vector<uint8_t> icc_profile;
            {
                icc_profile.resize(heif_image_get_raw_color_profile_size(himage.get()));
                if (auto err =
                        heif_image_get_raw_color_profile(himage.get(), reinterpret_cast<void *>(icc_profile.data()));
                    err.code != heif_error_Ok)
                {
                    spdlog::info("No image-level ICC profile found: {}. Trying at handle level", err.message);
                    icc_profile.resize(heif_image_handle_get_raw_color_profile_size(ihandle.get()));
                    if (auto err2 = heif_image_handle_get_raw_color_profile(
                            raw_ihandle, reinterpret_cast<void *>(icc_profile.data()));
                        err2.code != heif_error_Ok)
                    {
                        spdlog::warn("Could not read handle-level ICC profile either: {}", err2.message);
                        icc_profile.clear();
                    }
                    else
                    {
                        spdlog::info("File contains a handle-level ICC profile.");
                        image->metadata["header"]["icc profile"] =
                            json{{"value", 1}, {"string", "present at handle level"}, {"type", "enum"}};
                    }
                }
                else
                {
                    spdlog::info("File contains an image-level ICC profile.");
                    image->metadata["header"]["icc profile"] =
                        json{{"value", 2}, {"string", "present at image level"}, {"type", "enum"}};
                }
            }

            spdlog::info("Copying image channels...");
            Timer timer;
            // the code below works for both interleaved (RGBA) and planar (YA) channel layouts
            for (int p = 0; p < num_planes; ++p)
            {
                int            bytes_per_line = 0;
                const uint8_t *pixels         = heif_image_get_plane(himage.get(), out_planes[p], &bytes_per_line);
                int            bpp_storage    = heif_image_get_bits_per_pixel(himage.get(), out_planes[p]);
                int            bpc            = heif_image_get_bits_per_pixel_range(himage.get(), out_planes[p]);
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
                bool          is_16bit = (bpp_storage == cpp * 16);
                for (int y = 0; y < size.y; ++y)
                {
                    auto row8  = reinterpret_cast<const uint8_t *>(pixels + y * (size_t)bytes_per_line);
                    auto row16 = reinterpret_cast<const uint16_t *>(pixels + y * (size_t)bytes_per_line);
                    for (int x = 0; x < size.x; ++x)
                        for (int c = 0; c < cpp; ++c)
                            float_pixels[(y * size.x + x) * cpp + c] =
                                bpc_div * (is_16bit ? row16[cpp * x + c] : row8[cpp * x + c]);
                }

                if (opts.tf != TransferFunction_Unspecified)
                {
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
                        linearize_colors(float_pixels.data(), int3{size.xy(), cpp}, nclx.get(), &tf_description, &chr))
                    {
                        image->chromaticities                = chr;
                        image->metadata["transfer function"] = tf_description;
                    }
                    else
                        image->metadata["transfer function"] = transfer_function_name(TransferFunction_Unspecified);
                }
                else
                {
                    // FIXME: probably still want to use the icc/nclx profile to get chromaticities
                    // linearize using the user-specified transfer function
                    to_linear(float_pixels.data(), int3{size.xy(), cpp}, opts.tf, opts.gamma);
                    image->metadata["transfer function"] = transfer_function_name(opts.tf, opts.gamma);
                }

                // copy the interleaved float pixels into the channels
                for (int c = 0; c < cpp; ++c)
                    image->channels[p * cpp + c].copy_from_interleaved(float_pixels.data(), size.x, size.y, cpp, c,
                                                                       [](float v) { return v; });
            }

            spdlog::info("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));
            images.emplace_back(image);
        }
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

// Opaque pointer version
void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, const HEIFSaveOptions *params)
{
    if (!params)
        throw std::invalid_argument("HEIFSaveOptions pointer is null");

    if (params->encoder >= s_encoders.size() || !s_encoders[params->encoder])
        throw std::runtime_error("HEIF: no encoder available");

    // Grab the shared C encoder instance configured by the GUI.
    auto enc = s_encoders[params->encoder].get();
    try
    {
        Timer timer;

        int                        w = 0, h = 0, n = 0;
        std::unique_ptr<uint8_t[]> pixels8;
        void                      *pixels = nullptr;
        pixels8 = img.as_interleaved<uint8_t>(&w, &h, &n, params->gain, params->tf, params->gamma, true, true, true);
        if (!params->use_alpha && n == 4)
        {
            size_t                     num_pixels = w * h;
            std::unique_ptr<uint8_t[]> rgb_pixels(new uint8_t[num_pixels * 3]);
            for (size_t i = 0, j = 0; i < num_pixels; ++i, j += 4)
            {
                rgb_pixels[i * 3 + 0] = pixels8[j + 0];
                rgb_pixels[i * 3 + 1] = pixels8[j + 1];
                rgb_pixels[i * 3 + 2] = pixels8[j + 2];
            }
            pixels8.swap(rgb_pixels);
            n = 3;
        }
        pixels = pixels8.get();

        if (!pixels || w <= 0 || h <= 0)
            throw std::runtime_error("HEIF: empty image or invalid image dimensions");

        if (n != 3 && n != 4)
            throw std::invalid_argument("HEIF/AVIF output only supports 3 or 4 channels (RGB or RGBA)");

        // Create heif image via C API
        HeifContextPtr ctx(heif_context_alloc(), [](heif_context *c) { heif_context_free(c); });
        if (!ctx)
            throw std::runtime_error("HEIF: Failed to allocate encoding context");

        HeifImagePtr heif_img(
            [&]
            {
                heif_image *raw_heif_img = nullptr;
                throw_on_error(heif_image_create(w, h, heif_colorspace_RGB,
                                                 (n == 4) ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB,
                                                 &raw_heif_img),
                               "HEIF: Failed to create heif image");
                return raw_heif_img;
            }(),
            [](heif_image *h) { heif_image_release(h); });

        throw_on_error(heif_image_add_plane(heif_img.get(), heif_channel_interleaved, w, h, 8),
                       "HEIF: Failed to add interleaved plane");

        int      stride = 0;
        uint8_t *plane  = heif_image_get_plane(heif_img.get(), heif_channel_interleaved, &stride);
        // Copy pixel data
        size_t row_bytes = w * n * sizeof(uint8_t);
        for (int y = 0; y < h; ++y) memcpy(plane + y * (size_t)stride, pixels8.get() + y * w * n, row_bytes);

        // Set color profile (nclx)
        {
            auto nclx = HeifNCLXPtr(heif_nclx_color_profile_alloc(),
                                    [](heif_color_profile_nclx *p) { heif_nclx_color_profile_free(p); });
            if (!nclx)
                throw std::runtime_error("HEIF: Failed to allocate nclx profile");
            nclx->color_primaries = heif_color_primaries_ITU_R_BT_709_5; // TODO map from img.chromaticities
            auto heif_tf          = transfer_function_to_heif(params->tf);
            if (!is_heif_transfer_supported(params->tf))
            {
                spdlog::warn("HEIF: Transfer function '{}' not supported, falling back to sRGB.",
                             transfer_function_name(params->tf, params->gamma));
                heif_tf = heif_transfer_characteristic_IEC_61966_2_1;
            }
            nclx->transfer_characteristics = heif_tf;
            nclx->matrix_coefficients      = heif_matrix_coefficients_ITU_R_BT_709_5;
            nclx->full_range_flag          = true;
            if (heif_image_set_nclx_color_profile(heif_img.get(), nclx.get()).code != heif_error_Ok)
                spdlog::warn("HEIF: Failed to attach NCLX profile to image");
        }

        // Encode
        HeifImageHandlePtr out_handle(
            [&]
            {
                heif_image_handle *out_handle_raw = nullptr;
                throw_on_error(heif_context_encode_image(ctx.get(), heif_img.get(), enc, nullptr, &out_handle_raw),
                               "HEIF: encode failed");
                if (!out_handle_raw)
                    throw std::runtime_error("HEIF: encode returned NULL handle");
                return out_handle_raw;
            }(),
            [](heif_image_handle *h) { heif_image_handle_release(h); });

        throw_on_error(heif_context_set_primary_image(ctx.get(), out_handle.get()),
                       "HEIF: Failed to set primary image");

        // Writer trampoline to write to std::ostream
        static struct heif_writer c_writer = {
            1, [](struct heif_context * /*ctx*/, const void *data, size_t size, void *userdata) -> struct heif_error
            {
                std::ostream     *os   = reinterpret_cast<std::ostream *>(userdata);
                struct heif_error herr = heif_error_success;
                os->write(reinterpret_cast<const char *>(data), size);
                if (!*os)
                {
                    herr.code    = heif_error_Encoding_error;
                    herr.message = "Failed to write to output stream";
                }
                return herr;
            }};

        throw_on_error(heif_context_write(ctx.get(), &c_writer, &os),
                       "HEIF: failed while writing encoded data to output stream");

        spdlog::info("Saved image to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
    }
    catch (const std::exception &err)
    {
        throw std::runtime_error(fmt::format("HEIF error: {}", err.what()));
    }
}

void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, float gain, int quality,
                     bool lossless, bool use_alpha, int format_index, TransferFunction_ tf, float gamma)
{
    HEIFSaveOptions params;
    params.gain      = gain;
    params.quality   = quality;
    params.lossless  = lossless;
    params.use_alpha = use_alpha;
    params.tf        = tf;
    params.gamma     = gamma;
    params.encoder   = (size_t)clamp(format_index, 0, int(s_encoders.size()) - 1);
    heif_encoder_set_lossless(s_encoders[params.encoder].get(), lossless);
    heif_encoder_set_lossy_quality(s_encoders[params.encoder].get(), quality);

    save_heif_image(img, os, filename, &params);
}

// GUI parameter function
HEIFSaveOptions *heif_parameters_gui()
{
    init_heif_supported_formats();

    if (ImGui::PE::Begin("HEIF/AVIF Save Options", ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthStretch);

        // Gain (custom widget with button)
        ImGui::PE::Entry(
            "Gain",
            [&]
            {
                ImGui::BeginGroup();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::IconButtonSize().x -
                                        ImGui::GetStyle().ItemInnerSpacing.x);
                auto changed = ImGui::SliderFloat("##Gain", &s_opts.gain, 0.1f, 10.0f);
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::IconButton(ICON_MY_EXPOSURE))
                    s_opts.gain = exp2f(hdrview()->exposure());
                ImGui::Tooltip("Set gain from the current viewport exposure value.");
                ImGui::EndGroup();
                return changed;
            },
            "Multiply the pixels by this value before saving.");

        // Transfer function
        ImGui::PE::Entry(
            "Transfer function",
            [&]
            {
                if (ImGui::BeginCombo("##Transfer function",
                                      transfer_function_name(s_opts.tf, 1.f / s_opts.gamma).c_str()))
                {
                    for (int i = TransferFunction_Linear; i < TransferFunction_Count; ++i)
                    {
                        if (!is_heif_transfer_supported((TransferFunction_)i))
                            continue;
                        bool selected = (s_opts.tf == (TransferFunction_)i);
                        if (ImGui::Selectable(transfer_function_name((TransferFunction_)i, 1.f / s_opts.gamma).c_str(),
                                              selected))
                            s_opts.tf = (TransferFunction_)i;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::Tooltip("Encode the pixel values using this transfer function.");
                if (s_opts.tf == TransferFunction_Gamma)
                {
                    ImGui::Indent();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Gamma");
                    ImGui::SameLine(HelloImGui::EmSize(9.f));
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::SliderFloat("##Gamma", &s_opts.gamma, 0.1f, 5.f);
                    ImGui::Unindent();
                }
                return true;
            },
            "Encode the pixel values using this transfer function.");

        // Include alpha
        ImGui::PE::Checkbox("Include alpha", &s_opts.use_alpha);

        auto enc_open =
            ImGui::PE::TreeNode("Encoder", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        {
            if (ImGui::BeginCombo("##Encoder", heif_encoder_descriptor_get_name(s_encoder_descriptors[s_opts.encoder])))
            {
                for (size_t i = 0; i < s_encoder_descriptors.size(); ++i)
                {
                    bool selected = (s_opts.encoder == i);
                    if (ImGui::Selectable(heif_encoder_descriptor_get_id_name(s_encoder_descriptors[i]), selected))
                        s_opts.encoder = i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        if (enc_open)
        {
            auto  selected_encoder = s_encoder_descriptors[s_opts.encoder];
            auto &enc              = s_encoders[s_opts.encoder];

            // Lossless
            if (heif_encoder_descriptor_supports_lossless_compression(selected_encoder))
            {
                ImGui::BeginDisabled(!heif_encoder_descriptor_supports_lossy_compression(selected_encoder));
                bool lossless =
                    s_opts.lossless || !heif_encoder_descriptor_supports_lossy_compression(selected_encoder);
                if (ImGui::PE::Checkbox("Lossless", &lossless, "Use lossless compression."))
                {
                    s_opts.lossless = lossless;
                    heif_encoder_set_lossless(enc.get(), lossless);
                }
                ImGui::EndDisabled();
            }

            // Quality
            if (heif_encoder_descriptor_supports_lossy_compression(selected_encoder))
            {
                ImGui::BeginDisabled(s_opts.lossless);
                if (ImGui::PE::SliderInt(
                        "Quality", &s_opts.quality, 1, 100, "%d", 0,
                        "Controls the quality of the encoded image for lossy compression (1 = worst, 100 = best)."))
                    heif_encoder_set_lossy_quality(enc.get(), s_opts.quality);
                ImGui::EndDisabled();
            }

            if (ImGui::PE::TreeNode("Advanced", ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DrawLinesFull))
            {
                auto params = heif_encoder_list_parameters(enc.get());
                for (int i = 0; params && params[i]; ++i)
                {
                    auto name = heif_encoder_parameter_get_name(params[i]);
                    // we handle the common "Lossless" and "Quality" parameters separately
                    if (!name || std::string(name) == "lossless" || std::string(name) == "quality")
                        continue;

                    std::string uppercase_name(name);
                    if (!uppercase_name.empty())
                        uppercase_name[0] =
                            static_cast<char>(std::toupper(static_cast<unsigned char>(uppercase_name[0])));

                    auto type = heif_encoder_parameter_get_type(params[i]);

                    ImGui::PushID(i);

                    switch (type)
                    {
                    case heif_encoder_parameter_type_integer:
                    {
                        int        have_minimum = 0, have_maximum = 0, minimum = 0, maximum = 0, num_valid_values = 0;
                        const int *valid_values = nullptr;
                        (void)heif_encoder_parameter_integer_valid_values(enc.get(), name, &have_minimum, &have_maximum,
                                                                          &minimum, &maximum, &num_valid_values,
                                                                          &valid_values);

                        int cur = 0;
                        (void)heif_encoder_get_parameter_integer(enc.get(), name, &cur);

                        if (num_valid_values > 0)
                        {
                            std::string preview = std::to_string(cur);
                            if (ImGui::BeginCombo(("##" + std::string(name)).c_str(), preview.c_str()))
                            {
                                for (int k = 0; k < num_valid_values; ++k)
                                {
                                    bool selected = (cur == valid_values[k]);
                                    if (ImGui::Selectable(std::to_string(valid_values[k]).c_str(), selected))
                                    {
                                        cur = valid_values[k];
                                        (void)heif_encoder_set_parameter_integer(enc.get(), name, cur);
                                    }
                                    if (selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                        }
                        else
                        {
                            if (have_minimum && have_maximum)
                            {
                                if (ImGui::PE::SliderInt(uppercase_name, &cur, minimum, maximum))
                                    (void)heif_encoder_set_parameter_integer(enc.get(), name, cur);
                            }
                            else
                            {
                                if (ImGui::PE::DragInt(uppercase_name, &cur))
                                    (void)heif_encoder_set_parameter_integer(enc.get(), name, cur);
                            }
                        }
                    }
                    break;
                    case heif_encoder_parameter_type_boolean:
                    {
                        int cur = 0;
                        (void)heif_encoder_get_parameter_boolean(enc.get(), name, &cur);
                        bool b = (cur != 0);
                        if (ImGui::PE::Checkbox(uppercase_name, &b))
                            (void)heif_encoder_set_parameter_boolean(enc.get(), name, b ? 1 : 0);
                    }
                    break;
                    case heif_encoder_parameter_type_string:
                    {
                        const char *const *valid_options = nullptr;
                        (void)heif_encoder_parameter_string_valid_values(enc.get(), name, &valid_options);

                        constexpr int bufsize      = 512;
                        char          buf[bufsize] = {0};
                        (void)heif_encoder_get_parameter_string(enc.get(), name, buf, bufsize);

                        ImGui::PE::Entry(
                            uppercase_name,
                            [&]
                            {
                                if (valid_options)
                                {
                                    std::string preview(buf);
                                    if (ImGui::BeginCombo(("##" + std::string(name)).c_str(), preview.c_str()))
                                    {
                                        for (int k = 0; valid_options[k]; ++k)
                                        {
                                            bool selected = (preview == valid_options[k]);
                                            if (ImGui::Selectable(valid_options[k], selected))
                                                (void)heif_encoder_set_parameter_string(enc.get(), name,
                                                                                        valid_options[k]);
                                            if (selected)
                                                ImGui::SetItemDefaultFocus();
                                        }
                                        ImGui::EndCombo();
                                    }
                                }
                                else
                                {
                                    if (ImGui::InputText(("##" + std::string(name)).c_str(), buf, bufsize))
                                        (void)heif_encoder_set_parameter_string(enc.get(), name, buf);
                                }
                                return false;
                            });
                    }
                    break;
                    }

                    ImGui::PopID();
                }
                ImGui::PE::TreePop();
            }
            ImGui::PE::TreePop();
        }

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = HEIFSaveOptions{};

    return &s_opts;
}

#endif
