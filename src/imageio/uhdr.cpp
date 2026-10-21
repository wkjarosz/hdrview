//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "uhdr.h"
#include "colorspace.h"
#include "exif.h"
#include "gainmap.h"
#include "hdr_metadata.h"
#include "image.h"
#include "imageio/alpha.h"
#include "imageio/image_loader.h"
#include "imageio/streams.h"
#include "imgui.h"
#include "save_options.h"
#include "timer.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <half.h>
#include <iostream>
#include <spdlog/fmt/fmt.h>
#include <stdexcept>

#include "icc.h"
#include "imgui_ext.h"

using namespace std;

struct UHDRSaveOptions
{
    int   quality           = 95;
    int   gainmap_quality   = 95;
    bool  use_multi_channel = true; ///< a gain per color channel, which a rendition clipped per channel needs
    int   gainmap_scale     = 1;
    float gainmap_gamma     = 1.f;
    int   preset            = 1;
    int   sdr_base          = 0; ///< 0: HDRView's SDR rendition of the view, 1: libultrahdr's tone map
};

static UHDRSaveOptions s_opts;

#if !HDRVIEW_ENABLE_LIBUHDR

// Return JSON describing libuhdr availability (disabled stub)
json get_uhdr_info() { return {{"name", "libuhdr"}}; }

bool is_uhdr_image(istream &is) noexcept { return false; }

bool uhdr_supported_tf(TransferFunction tf) noexcept { return false; }

vector<ImagePtr> load_uhdr_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("UltraHDR support not enabled in this build.");
}

void save_uhdr_image(const Image &, ostream &, string_view, int, int, bool, int, float, const Image *)
{
    throw runtime_error("UltraHDR support not enabled in this build.");
}

UHDRSaveOptions *uhdr_parameters_gui(SaveContext &) { return &s_opts; }

void save_uhdr_image(const Image &, std::ostream &, std::string_view, const UHDRSaveOptions *, const Image *)
{
    throw runtime_error("UltraHDR support not enabled in this build.");
}

#else

#include <ultrahdr_api.h>

static uhdr_color_transfer_t uhdr_tf(TransferFunction tf)
{
    switch (tf.type)
    {
    case TransferFunction::Linear: return UHDR_CT_LINEAR;
    case TransferFunction::sRGB: return UHDR_CT_SRGB;
    case TransferFunction::BT2100_PQ: return UHDR_CT_PQ;
    case TransferFunction::BT2100_HLG: return UHDR_CT_HLG;
    default: return UHDR_CT_UNSPECIFIED;
    }
}

bool uhdr_supported_tf(TransferFunction tf) noexcept { return uhdr_tf(tf) != UHDR_CT_UNSPECIFIED; }

// Return JSON describing libuhdr availability and version
json get_uhdr_info()
{
    json j;
    j["enabled"] = true;
    j["name"]    = "libuhdr";
#ifdef UHDR_LIB_VERSION_STR
    j["version"] = UHDR_LIB_VERSION_STR;
#else
    j["version"] = "";
#endif
    j["features"] = json::object();
    return j;
}

bool is_uhdr_image(istream &is) noexcept
{
    bool ret = false;
    try
    {
        // an Ultra HDR image is a JPEG, so rule out everything else before reading the whole file
        unsigned char soi[3] = {};
        if (is.read(reinterpret_cast<char *>(soi), sizeof(soi)) && soi[0] == 0xFF && soi[1] == 0xD8 && soi[2] == 0xFF)
        {
            // libultrahdr copies the data and only reads it
            const StreamBytes data{is};
            ret = ::is_uhdr_image(const_cast<uint8_t *>(data.data()), (int)data.size());
        }
    }
    catch (...)
    {
        ret = false;
    }

    // rewind
    is.clear();
    is.seekg(0);
    return ret;
}

vector<ImagePtr> load_uhdr_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "UHDR"};
    if (!is.good())
        throw invalid_argument("invalid file stream.");

    using Decoder = unique_ptr<uhdr_codec_private_t, decltype(&uhdr_release_decoder)>;
    auto decoder  = Decoder{uhdr_create_decoder(), &uhdr_release_decoder};

    auto throw_if_error = [](uhdr_error_info_t status)
    {
        if (status.error_code != UHDR_CODEC_OK)
            throw invalid_argument(fmt::format("Error decoding image: {}", status.detail));
    };

    {
        const StreamBytes data{is};
        if (data.size() == 0)
            throw invalid_argument{fmt::format("File '{}' is empty", filename)};

        // libultrahdr copies the data and only reads it
        uhdr_compressed_image_t compressed_image{
            const_cast<uint8_t *>(data.data()), /**< Pointer to a block of data to decode */
            data.size(),                        /**< size of the data buffer */
            data.size(),                        /**< maximum size of the data buffer */
            UHDR_CG_UNSPECIFIED,                /**< Color Gamut */
            UHDR_CT_UNSPECIFIED,                /**< Color Transfer */
            UHDR_CR_UNSPECIFIED                 /**< Color Range */
        };
        throw_if_error(uhdr_dec_set_image(decoder.get(), &compressed_image));
        throw_if_error(uhdr_dec_set_out_color_transfer(decoder.get(), UHDR_CT_LINEAR));

        // libultrahdr applies the gain map itself, so hand it the target headroom; left unset it
        // reconstructs the map in full
        if (std::isfinite(opts.gainmap_headroom))
        {
            const float boost = std::max(std::exp2(opts.gainmap_headroom), 1.f);
            spdlog::info("Limiting gain map reconstruction to {:.3f} stops ({:.3f}x).", opts.gainmap_headroom, boost);
            throw_if_error(uhdr_dec_set_out_max_display_boost(decoder.get(), boost));
        }
        throw_if_error(uhdr_dec_set_out_img_format(decoder.get(), UHDR_IMG_FMT_64bppRGBAHalfFloat));
        throw_if_error(uhdr_dec_probe(decoder.get()));
        spdlog::debug("base image: {}x{}", uhdr_dec_get_image_width(decoder.get()),
                      uhdr_dec_get_image_height(decoder.get()));
        check_image_dimensions(uhdr_dec_get_image_width(decoder.get()), uhdr_dec_get_image_height(decoder.get()),
                               "Ultra HDR");
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

    auto image      = make_shared<Image>(size, 4);
    image->filename = filename;
    image->set_transparency(TransparencyType_Straight, transparency_override_of(opts));
    image->metadata["loader"] = "libuhdr";

    if (auto md = uhdr_dec_get_gainmap_metadata(decoder.get()))
    {
        spdlog::debug("Found gainmap metadata: hdr_capacity_min={} hdr_capacity_max={} use_base_cg={}",
                      md->hdr_capacity_min, md->hdr_capacity_max, md->use_base_cg);

        // Add gainmap metadata to header
        image->metadata["header"]["HDR capacity min"] = {
            {"value", md->hdr_capacity_min},
            {"string", std::to_string(md->hdr_capacity_min)},
            {"type", "float"},
            {"description", "Minimum display boost value for which the map is applied completely."}};
        image->metadata["header"]["HDR capacity max"] = {
            {"value", md->hdr_capacity_max},
            {"string", std::to_string(md->hdr_capacity_max)},
            {"type", "float"},
            {"description", "Maximum display boost value for which the map is applied completely."}};
        image->metadata["header"]["Use base cg"] = {
            {"value", md->use_base_cg},
            {"string", md->use_base_cg ? "true" : "false"},
            {"type", "bool"},
            {"description", "Is gainmap application space same as base image color space"}};

        // Arrays: max_content_boost, min_content_boost, gamma, offset_sdr, offset_hdr
        image->metadata["header"]["Max content boost"] = {
            {"value", std::vector<float>(md->max_content_boost, md->max_content_boost + 3)},
            {"string",
             fmt::format("[{}, {}, {}]", md->max_content_boost[0], md->max_content_boost[1], md->max_content_boost[2])},
            {"type", "float3"},
            {"description", "Value to control how much brighter an image can get, when shown on an HDR display, "
                            "relative to the SDR rendition. This is constant for a given image."}};

        image->metadata["header"]["Min content boost"] = {
            {"value", std::vector<float>(md->min_content_boost, md->min_content_boost + 3)},
            {"string",
             fmt::format("[{}, {}, {}]", md->min_content_boost[0], md->min_content_boost[1], md->min_content_boost[2])},
            {"type", "float3"},
            {"description", "Value to control how much darker an image can get, when shown on an HDR display, relative "
                            "to the SDR rendition. This is constant for a given image."}};
        image->metadata["header"]["Gainmap gamma"] = {
            {"value", std::vector<float>(md->gamma, md->gamma + 3)},
            {"string", fmt::format("[{}, {}, {}]", md->gamma[0], md->gamma[1], md->gamma[2])},
            {"type", "float3"}};
        image->metadata["header"]["offset SDR"] = {
            {"value", std::vector<float>(md->offset_sdr, md->offset_sdr + 3)},
            {"string", fmt::format("[{}, {}, {}]", md->offset_sdr[0], md->offset_sdr[1], md->offset_sdr[2])},
            {"type", "float3"},
            {"description", "The offset applies to the SDR pixel values during gainmap generation and application."}};
        image->metadata["header"]["offset HDR"] = {
            {"value", std::vector<float>(md->offset_hdr, md->offset_hdr + 3)},
            {"string", fmt::format("[{}, {}, {}]", md->offset_hdr[0], md->offset_hdr[1], md->offset_hdr[2])},
            {"type", "float3"},
            {"description", "The offset applies to the HDR pixel values during gainmap generation and application."}};
    }

    const uhdr_mem_block_t *exif_data = uhdr_dec_get_exif(decoder.get());
    if (exif_data && exif_data->data && exif_data->data_sz > 0)
    {
        spdlog::debug("Found EXIF data of size {} bytes", exif_data->data_sz);

        try
        {
            image->exif             = Exif{reinterpret_cast<uint8_t *>(exif_data->data), exif_data->data_sz};
            auto j                  = image->exif.to_json();
            image->metadata["exif"] = j;
            spdlog::debug("EXIF metadata successfully parsed: {}", j.dump(2));
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Exception while parsing EXIF chunk: {}", e.what());
            image->exif.reset();
        }
    }

    // Save the chromaticities of the file's pixel data
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

    const uhdr_mem_block_t *icc_data = uhdr_dec_get_icc(decoder.get());
    if (icc_data && icc_data->data && icc_data->data_sz > 0)
    {
        // libuhdr hands back the APP2 marker's whole payload: the "ICC_PROFILE\0" signature, this chunk's
        // sequence number and the chunk count, and only then the profile. libjpeg-based readers strip that
        // 14-byte header, so the sizes the two report for one file differ by it.
        //
        // libuhdr keeps only the first such marker, so a profile split across several arrives truncated;
        // ICCProfile then rejects it and the file falls back to its CICP-derived gamut, handled above.
        static constexpr uint8_t icc_sig[]  = {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', '\0'};
        static constexpr size_t  icc_hdr_sz = sizeof(icc_sig) + 2;

        auto         bytes = reinterpret_cast<const uint8_t *>(icc_data->data);
        const size_t skip =
            (icc_data->data_sz > icc_hdr_sz && memcmp(bytes, icc_sig, sizeof(icc_sig)) == 0) ? icc_hdr_sz : 0;

        spdlog::debug("Found ICC data of size {} bytes, {} of which is APP2 header", icc_data->data_sz, skip);

        image->icc_data.assign(bytes + skip, bytes + icc_data->data_sz);
        image->metadata["color profile"] = ICCProfile(image->icc_data).description();
    }

    {
        Timer timer;

        // copy half-float pixel data into a float buffer first
        std::vector<float> pixels((size_t)size.x * size.y * 4);
        parallel_for(
            blocked_range<int>(0, size.y, 512),
            [pixels_ptr = pixels.data(), half_data = reinterpret_cast<half *>(decoded_image->planes[UHDR_PLANE_PACKED]),
             stride_y = decoded_image->stride[UHDR_PLANE_PACKED] * 4, width = size.x](int start, int end, int, int)
            {
                for (int y = start; y < end; ++y)
                {
                    size_t src = (size_t)y * (size_t)stride_y;
                    size_t dst = (size_t)y * (size_t)width * 4;
                    for (int x = 0; x < width; ++x)
                    {
                        // copy 4 channels
                        pixels_ptr[dst + x * 4 + 0] = (float)(half_data[src + x * 4 + 0]);
                        pixels_ptr[dst + x * 4 + 1] = (float)(half_data[src + x * 4 + 1]);
                        pixels_ptr[dst + x * 4 + 2] = (float)(half_data[src + x * 4 + 2]);
                        pixels_ptr[dst + x * 4 + 3] = (float)(half_data[src + x * 4 + 3]);
                    }
                }
            });

        if (!opts.keep_primaries && decoded_image->cg != UHDR_CG_BT_709 && image->chromaticities)
        {
            // if we are not keeping primaries, convert to Rec. 709/sRGB
            auto rec709 = Chromaticities();
            convert_primaries(pixels.data(), {size, 4}, *image->chromaticities, rec709);
            image->chromaticities = rec709;
        }

        // copy the interleaved float pixels into the channels
        for (int c = 0; c < 4; ++c)
            image->channels[c].copy_from_interleaved(pixels.data(), size.x, size.y, 4, c, [](float v) { return v; });

        spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed() / 1000.f));
    }

    uhdr_raw_image_t *gainmap = uhdr_get_decoded_gainmap_image(decoder.get()); // freed by decoder destructor
    int2              gainmap_size(gainmap->w, gainmap->h);

    spdlog::debug("Gainmap image: {}x{}; stride: {}; cg: {}; ct: {}; range: {}", gainmap->w, gainmap->h,
                  gainmap->stride[UHDR_PLANE_PACKED], (int)gainmap->cg, (int)gainmap->ct, (int)gainmap->range);

    // if the gainmap is an unexpected size or format, we are done
    if ((gainmap_size.x > size.x || gainmap_size.y > size.y) ||
        (gainmap->fmt != UHDR_IMG_FMT_32bppRGBA8888 && gainmap->fmt != UHDR_IMG_FMT_8bppYCbCr400 &&
         gainmap->fmt != UHDR_IMG_FMT_24bppRGB888))
        return {image};

    if (!opts.gainmap_renditions)
        return {image};

    // otherwise, keep the gain map as its own channel group. Only the gain map: libuhdr applies the
    // map inside its own decoder and returns just the result, so unlike HDRView's own gain-map paths
    // this one never has the base rendition in hand to keep.

    const int num_components =
        gainmap->fmt == UHDR_IMG_FMT_32bppRGBA8888 ? 4 : (gainmap->fmt == UHDR_IMG_FMT_24bppRGB888 ? 3 : 1);

    {
        Timer timer;

        GainmapImage gm;
        gm.size     = gainmap_size;
        gm.channels = num_components;
        gm.pixels.resize((size_t)gainmap_size.x * gainmap_size.y * num_components);

        const auto byte_data = reinterpret_cast<const uint8_t *>(gainmap->planes[UHDR_PLANE_PACKED]);
        const int  stride_y  = gainmap->stride[UHDR_PLANE_PACKED] * num_components;
        for (int y = 0; y < gainmap_size.y; ++y)
            for (int x = 0; x < gainmap_size.x; ++x)
                for (int c = 0; c < num_components; ++c)
                    gm.pixels[((size_t)y * gainmap_size.x + x) * num_components + c] =
                        dequantize_full(byte_data[(size_t)y * stride_y + x * num_components + c]);

        // libuhdr has already applied the map to produce the pixels above, so these channels exist to
        // be inspected. They keep the file's own encoding: an ISO 21496-1 map is not sRGB-encoded, and
        // linearizing it as if it were would misreport every value.
        append_gainmap_channels(*image, resample_gainmap(gm, size, false));

        spdlog::debug("Copying gainmap data took: {} seconds.", (timer.elapsed() / 1000.f));
    }

    return {image};
}

namespace
{

/// The gamut libultrahdr names \p chr by, or BT.709's when it names no gamut those primaries match.
uhdr_color_gamut_t uhdr_gamut(const std::optional<Chromaticities> &chr)
{
    switch (chr ? named_color_gamut(*chr) : ColorGamut_sRGB_BT709)
    {
    case ColorGamut_Display_P3_SMPTE432: return UHDR_CG_DISPLAY_P3;
    case ColorGamut_BT2020_2100: return UHDR_CG_BT_2100;
    default: return UHDR_CG_BT_709;
    }
}

/// \p pixels, \p n channels each, widened to the RGBA libultrahdr's packed formats take, opaque.
template <typename T>
std::unique_ptr<T[]> to_rgba(std::unique_ptr<T[]> pixels, int w, int h, int n, T opaque)
{
    if (n == 4)
        return pixels;
    const size_t num  = size_t(w) * size_t(h);
    auto         rgba = std::make_unique<T[]>(num * 4);
    for (size_t i = 0; i < num; ++i)
    {
        for (int c = 0; c < 3; ++c) rgba[4 * i + c] = pixels[n * i + c];
        rgba[4 * i + 3] = opaque;
    }
    return rgba;
}

} // namespace

void save_uhdr_image(const Image &img, std::ostream &os, std::string_view filename, const UHDRSaveOptions *opts,
                     const Image *sdr_base)
{
    if (opts == nullptr)
        throw std::invalid_argument("UHDRSaveOptions pointer is null");

    Timer timer;

    // in the image's primaries where libultrahdr can name them, else converted to BT.709's
    const uhdr_color_gamut_t gamut   = uhdr_gamut(img.chromaticities);
    const bool               convert = gamut == UHDR_CG_BT_709 && img.chromaticities &&
                                       named_color_gamut(*img.chromaticities) != ColorGamut_sRGB_BT709;

    // the HDR rendition as libultrahdr takes it: linear half floats, 1.0 at SDR white
    int  w = 0, h = 0, n = 0;
    auto hdr = img.as_interleaved<half>(&w, &h, &n, TransferFunction::Linear, false, true, convert);
    if (n != 3 && n != 4)
        throw invalid_argument("Can only save images with 3 or 4 channels in UltraHDR right now.");
    hdr = to_rgba(std::move(hdr), w, h, n, half(1.f));

    auto throw_if_error = [](uhdr_error_info_t status)
    {
        if (status.error_code != UHDR_CODEC_OK)
            throw invalid_argument(fmt::format("UltraHDR: Error encoding image: {}", status.detail));
    };

    using Encoder = unique_ptr<uhdr_codec_private_t, decltype(&uhdr_release_encoder)>;
    auto encoder  = Encoder{uhdr_create_encoder(), &uhdr_release_encoder};

    uhdr_raw_image_t hdr_image{UHDR_IMG_FMT_64bppRGBAHalfFloat,
                               gamut,
                               UHDR_CT_LINEAR,
                               UHDR_CR_FULL_RANGE,
                               (unsigned)w,
                               (unsigned)h,
                               {hdr.get(), nullptr, nullptr},
                               {(unsigned)w, 0u, 0u}};
    throw_if_error(uhdr_enc_set_raw_image(encoder.get(), &hdr_image, UHDR_HDR_IMG));

    // the SDR base, as the view shows it at SDR white; without one, libultrahdr tone-maps the HDR rendition
    std::unique_ptr<uint8_t[]> sdr;
    uhdr_raw_image_t           sdr_image{};
    if (sdr_base && opts->sdr_base == 0)
    {
        int sw = 0, sh = 0, sn = 0;
        sdr = sdr_base->as_interleaved<uint8_t>(&sw, &sh, &sn, TransferFunction::sRGB, true, true, convert);
        if (sw != w || sh != h || (sn != 3 && sn != 4))
            throw invalid_argument("UltraHDR: the SDR rendition does not match the HDR one.");
        sdr       = to_rgba(std::move(sdr), w, h, sn, uint8_t(255));
        sdr_image = {UHDR_IMG_FMT_32bppRGBA8888,
                     gamut,
                     UHDR_CT_SRGB,
                     UHDR_CR_FULL_RANGE,
                     (unsigned)w,
                     (unsigned)h,
                     {sdr.get(), nullptr, nullptr},
                     {(unsigned)w, 0u, 0u}};
        throw_if_error(uhdr_enc_set_raw_image(encoder.get(), &sdr_image, UHDR_SDR_IMG));
    }

    // The gain map's boosts run from 1 to the peak the HDR rendition was made for, and no further, since the HDR
    // rendition is never darker than the SDR one. Left to itself, libultrahdr spans whatever ratios it meets, offset
    // by only 1e-7: a black pixel that dither lifts by one code value in the SDR base asks for 14 stops below 1,
    // spreading the map's 8 bits over more than three times the range they need.
    if (const auto md = mastering_display(img.metadata.value("header", json::object())); md && md->max_nits > 0.f)
    {
        const float peak = std::clamp(md->max_nits, HDR_REFERENCE_WHITE_NITS, 10000.f);
        throw_if_error(uhdr_enc_set_target_display_peak_brightness(encoder.get(), peak));
        throw_if_error(uhdr_enc_set_min_max_content_boost(encoder.get(), 1.f, peak / HDR_REFERENCE_WHITE_NITS));
    }

    throw_if_error(uhdr_enc_set_quality(encoder.get(), opts->quality, UHDR_BASE_IMG));
    throw_if_error(uhdr_enc_set_quality(encoder.get(), opts->gainmap_quality, UHDR_GAIN_MAP_IMG));
    throw_if_error(uhdr_enc_set_using_multi_channel_gainmap(encoder.get(), opts->use_multi_channel));
    throw_if_error(uhdr_enc_set_gainmap_scale_factor(encoder.get(), opts->gainmap_scale));
    throw_if_error(uhdr_enc_set_gainmap_gamma(encoder.get(), opts->gainmap_gamma));
    throw_if_error(
        uhdr_enc_set_preset(encoder.get(), opts->preset == 0 ? UHDR_USAGE_REALTIME : UHDR_USAGE_BEST_QUALITY));

    throw_if_error(uhdr_encode(encoder.get()));

    auto output = uhdr_get_encoded_stream(encoder.get()); // owned by the encoder

    os.write(static_cast<char *>(output->data), output->data_sz);
    spdlog::info("Writing UltraHDR image to \"{}\" took: {} seconds.", filename, (timer.elapsed() / 1000.f));
}

void save_uhdr_image(const Image &img, ostream &os, const string_view filename, int base_quality, int gainmap_quality,
                     bool use_multi_channel_gainmap, int gainmap_scale_factor, float gainmap_gamma,
                     const Image *sdr_base)
{
    UHDRSaveOptions opts;
    opts.quality           = base_quality;
    opts.gainmap_quality   = gainmap_quality;
    opts.use_multi_channel = use_multi_channel_gainmap;
    opts.gainmap_scale     = gainmap_scale_factor;
    opts.gainmap_gamma     = gainmap_gamma;
    opts.sdr_base          = sdr_base ? 0 : 1;
    save_uhdr_image(img, os, filename, &opts, sdr_base);
}

UHDRSaveOptions *uhdr_parameters_gui(SaveContext &ctx)
{
    if (begin_options_table(ctx))
    {

        ImGui::PE::Combo("SDR base", &s_opts.sdr_base, "HDRView's rendition\0libultrahdr's tone map\0", 2,
                         "The SDR image every reader shows, which the gain map brightens on an HDR display.\n\n"
                         "HDRView's rendition: the view as it looks at SDR white.\n"
                         "libultrahdr's tone map: libultrahdr's own tone mapping of the HDR rendition.");

        // Base quality
        ImGui::PE::SliderInt("Quality", &s_opts.quality, 1, 100, "%d", 0,
                             "The quality factor to be used while encoding SDR intent.\n[0-100]");

        ImGui::PE::Combo("Preset", &s_opts.preset, "Realtime\0Best quality\0", 2,
                         "Choose encoding preset.\n\nRealtime: tune encoder settings for performance.\nBest quality: "
                         "tune encoder settings for quality.");

        if (ImGui::PE::TreeNode("Gain map", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull |
                                                ImGuiTreeNodeFlags_SpanFullWidth))
        {
            ImGui::PE::SliderInt("Quality", &s_opts.gainmap_quality, 1, 100, "%d", 0,
                                 "The quality factor to be used while encoding gain map image.\n[0-100]");

            ImGui::PE::Checkbox("Multi-channel", &s_opts.use_multi_channel,
                                "Use multi-channel gainmap for better color fidelity.");

            ImGui::PE::SliderInt("Scale factor", &s_opts.gainmap_scale, 1, 5, "%d", 0,
                                 "The downscaling factor for the gainmap image.\n[1-5]");

            ImGui::PE::SliderFloat(
                "Gamma", &s_opts.gainmap_gamma, 0.1f, 5.0f, "%.3f", 0,
                "The gamma correction to be applied on the gainmap image.\n[any positive real number "
                "(1.0 : default)]");
            ImGui::PE::TreePop();
        }

        if (reset_entry())
            s_opts = UHDRSaveOptions{};

        ImGui::PE::End();
    }

    ctx.hdr               = true;
    ctx.records_primaries = true; //< BT.709, Display P3 or BT.2100, else converted to BT.709's
    ctx.sdr_base          = s_opts.sdr_base == 0;
    return &s_opts;
}

#endif
