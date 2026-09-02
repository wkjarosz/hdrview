//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
#include "imageio/alpha.h"
#include "imageio/image_loader.h"
#include <cstring>
#include <iostream>
#include <spdlog/fmt/fmt.h>
#include <stdexcept>

#include "app.h"
#include "heif.h" // for HEIFCodec, which the stubs below need as much as the real implementation
#include "imgui.h"

using namespace std;

#if !HDRVIEW_ENABLE_LIBHEIF

// Return JSON describing libheif availability, version, and features (including compression support)
json get_heif_info() { return json{{"name", "libheif"}}; }

bool is_heif_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_heif_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("HEIF/AVIF support not enabled in this build.");
}

void save_heif_image(const Image &, std::ostream &, std::string_view, float, int, bool, bool, HEIFCodec,
                     TransferFunction)
{
    throw std::runtime_error("HEIF/AVIF support not enabled in this build.");
}

void save_heif_image(const Image &, std::ostream &, std::string_view, const struct HEIFSaveOptions *)
{
    throw std::runtime_error("HEIF/AVIF support not enabled in this build.");
}

std::vector<std::string> heif_encoder_names(HEIFCodec) { return {}; }

HEIFSaveOptions *heif_parameters_gui(HEIFCodec) { return nullptr; }

#else

#include "colorspace.h"
#include "common.h"
#include "exif.h"
#include "fonts.h"
#include "gainmap.h"
#include "heif.h"
#include "icc.h"

#include "imgui_ext.h"
#include "json.h"
#include "timer.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>

#include <libheif/heif.h>
// Sequences and HTJ2K were added in libheif 1.20.0
#if LIBHEIF_NUMERIC_VERSION >= ((1 << 24) | (20 << 16) | (0 << 8) | 0) // 1.20.0
#include <libheif/heif_sequences.h>
#define HEIF_HAS_SEQUENCES_HTJ2K 1
#else
#define HEIF_HAS_SEQUENCES_HTJ2K 0
#endif
#include <memory>

using HeifContextPtr     = std::unique_ptr<heif_context, void (*)(heif_context *)>;
using HeifImagePtr       = std::unique_ptr<heif_image, void (*)(const heif_image *)>;
using HeifImageHandlePtr = std::unique_ptr<heif_image_handle, void (*)(const heif_image_handle *)>;
using HeifEncoderPtr     = std::unique_ptr<heif_encoder, void (*)(heif_encoder *)>;
using HeifNCLXPtr        = std::unique_ptr<heif_color_profile_nclx, void (*)(heif_color_profile_nclx *)>;
#if HEIF_HAS_SEQUENCES_HTJ2K
using HeifTrackPtr = std::unique_ptr<heif_track, void (*)(heif_track *)>;
#endif
using HeifDecodingOptionsPtr = std::unique_ptr<heif_decoding_options, void (*)(heif_decoding_options *)>;

struct HEIFSaveOptions
{
    float            gain      = 1.f;
    int              quality   = 95;
    bool             lossless  = false;
    bool             use_alpha = true;
    TransferFunction tf        = {TransferFunction::sRGB, 2.2f};
    size_t           encoder   = 0u;
    HEIFCodec        codec     = HEIFCodec::Any;
};

static HEIFSaveOptions s_opts;

static std::vector<const heif_encoder_descriptor *> s_encoder_descriptors;
static std::vector<HeifEncoderPtr>                  s_encoders;
static bool                                         s_encoders_initialized = false;

//! libheif registers its codec plugins in heif_init().
/*!
    Distributions that ship those codecs as separate shared objects -- Ubuntu, Debian and Fedora all do --
    register none of them until it is called, leaving only whatever was compiled into the library itself.
    Without this HDRView finds no HEVC or AV1 encoder on such a system, and silently falls back to the
    built-in JPEG one, which cannot store alpha. There is no matching heif_deinit(): the plugins stay
    loaded for the life of the process, which is exactly as long as they are wanted.
*/
static void ensure_heif_initialized()
{
    static std::once_flag once;
    std::call_once(once,
                   []
                   {
                       if (auto err = heif_init(nullptr); err.code != heif_error_Ok)
                           spdlog::warn("HEIF: heif_init() failed ({}); codec plugins may be unavailable.",
                                        err.message);
                   });
}

//! JPEG has no alpha channel; every other codec HEIF can carry does.
static bool codec_stores_alpha(heif_compression_format format) { return format != heif_compression_JPEG; }

//! Whether `format` may be written under `codec`.
/*!
    An AV1 image item reports the 'avif' brand, and libheif makes the primary item's brand the file's
    major brand -- so an AV1 payload is an AVIF whatever the file is named. AV1 is therefore AVIF's
    alone, and the HEIF entry offers the codecs a .heif may actually carry.
*/
static bool codec_admits(HEIFCodec codec, heif_compression_format format)
{
    switch (codec)
    {
    case HEIFCodec::HEVC: return format == heif_compression_HEVC;
    case HEIFCodec::AV1: return format == heif_compression_AV1;
    case HEIFCodec::HEIF: return format != heif_compression_AV1;
    default: return true;
    }
}

//! Indices into s_encoders whose codec `codec` admits, in the order libheif reports them.
static std::vector<size_t> encoders_for(HEIFCodec codec)
{
    std::vector<size_t> result;
    for (size_t i = 0; i < s_encoder_descriptors.size(); ++i)
        if (s_encoders[i] &&
            codec_admits(codec, heif_encoder_descriptor_get_compression_format(s_encoder_descriptors[i])))
            result.push_back(i);
    return result;
}

//! The encoder to start on: the first that implements `codec`, preferring HEVC when anything goes and,
//! where there is a choice, one that can store alpha.
static size_t default_encoder_for(HEIFCodec codec, bool need_alpha)
{
    auto candidates = encoders_for(codec);
    if (candidates.empty())
        return 0u;

    auto first_matching = [&](heif_compression_format format) -> const size_t *
    {
        for (const auto &i : candidates)
            if (heif_encoder_descriptor_get_compression_format(s_encoder_descriptors[i]) == format)
                return &i;
        return nullptr;
    };

    // a .heif conventionally holds HEVC, and a caller that named no codec takes AV1 as the next best
    if (codec == HEIFCodec::Any || codec == HEIFCodec::HEIF)
        for (auto format : {heif_compression_HEVC, heif_compression_AV1})
            if (auto *i = first_matching(format))
                return *i;

    if (need_alpha)
        for (const auto &i : candidates)
            if (codec_stores_alpha(heif_encoder_descriptor_get_compression_format(s_encoder_descriptors[i])))
                return i;

    return candidates.front();
}

static void init_heif_supported_formats()
{
    if (s_encoders_initialized)
        return;

    ensure_heif_initialized();

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
                s_encoders.emplace_back(enc, heif_encoder_release);
            else
                s_encoders.emplace_back(nullptr, heif_encoder_release);
        }
    }

    s_encoders_initialized = true;
}

// Return JSON describing libheif availability, version, and features (including compression support)
json get_heif_info()
{
    json j;
    j["enabled"] = true;
    j["name"]    = "libheif";
    j["version"] = heif_get_version();

    json features    = json::object();
    json compression = json::object();

    auto add_codec = [&](const char *name, heif_compression_format comp)
    {
        bool dec = (heif_have_decoder_for_format(comp) != 0);
        bool enc = (heif_have_encoder_for_format(comp) != 0);
        json entry{{"decoder", dec}, {"encoder", enc}};

        // libheif returns the descriptors in descending plugin priority, so the first is the one it will
        // pick. That matters for AV1, where dav1d and libaom can both be present and one is about twice
        // as fast as the other.
        const heif_decoder_descriptor *descriptors[8];
        if (int n = heif_get_decoder_descriptors(comp, descriptors, 8); n > 0)
            entry["decoder_used"] = heif_decoder_descriptor_get_name(descriptors[0]);

        compression[name] = entry;
    };

    add_codec("AVC", heif_compression_AVC);
    add_codec("AV1", heif_compression_AV1);
    add_codec("HEVC", heif_compression_HEVC);
    add_codec("JPEG", heif_compression_JPEG);
    add_codec("JPEG2000", heif_compression_JPEG2000);
#if HEIF_HAS_SEQUENCES_HTJ2K
    add_codec("HTJ2K", heif_compression_HTJ2K);
#endif
    add_codec("Uncompressed", heif_compression_uncompressed);
    add_codec("VVC", heif_compression_VVC);
    add_codec("EVC", heif_compression_EVC);

    features["compression"] = compression;
    j["features"]           = features;
    return j;
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
static heif_transfer_characteristics transfer_function_to_heif(TransferFunction tf)
{
    switch (tf.type)
    {
    case TransferFunction::Linear: return heif_transfer_characteristic_linear;
    case TransferFunction::sRGB: return heif_transfer_characteristic_IEC_61966_2_1;
    case TransferFunction::ITU: return heif_transfer_characteristic_ITU_R_BT_709_5;
    case TransferFunction::BT2100_PQ: return heif_transfer_characteristic_ITU_R_BT_2100_0_PQ;
    case TransferFunction::BT2100_HLG: return heif_transfer_characteristic_ITU_R_BT_2100_0_HLG;
    case TransferFunction::ST240: return heif_transfer_characteristic_SMPTE_240M;
    case TransferFunction::Log100: return heif_transfer_characteristic_logarithmic_100;
    case TransferFunction::Log100_Sqrt10: return heif_transfer_characteristic_logarithmic_100_sqrt10;
    case TransferFunction::IEC61966_2_4: return heif_transfer_characteristic_IEC_61966_2_4;
    case TransferFunction::DCI_P3: return heif_transfer_characteristic_SMPTE_ST_428_1;
    default: return heif_transfer_characteristic_IEC_61966_2_1; // fallback to sRGB
    }
}

static bool is_heif_transfer_supported(TransferFunction tf)
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

static bool linearize_colors(float *pixels, int3 size, bool keep_primaries, const heif_color_profile_nclx *nclx,
                             string *profile_description = nullptr, Chromaticities *c = nullptr)
{
    if (!nclx)
        return false;

    CICPProfile cicp((uint8_t)nclx->color_primaries, (uint8_t)nclx->transfer_characteristics,
                     (uint8_t)nclx->matrix_coefficients, nclx->full_range_flag != 0);

    if (!cicp)
    {
        spdlog::warn(
            "HEIF: CICP profile is not valid or unsupported (primaries: {}, transfer: {}, matrix: {}, full_range: {})",
            (int)nclx->color_primaries, (int)nclx->transfer_characteristics, (int)nclx->matrix_coefficients,
            (int)nclx->full_range_flag);
        return false;
    }

    return cicp.linearize_pixels(pixels, size, keep_primaries, profile_description, c);
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
}

// Helper: process decoded heif image (populate metadata, linearize and copy channels)
// Process decoded heif image: create an Image, populate metadata, linearize and copy channels.
// Returns a newly created ImagePtr.
static ImagePtr process_decoded_heif_image(heif_image *himage, const heif_color_profile_nclx *handle_level_nclx,
                                           const std::vector<uint8_t> &handle_level_icc_profile,
                                           const ImageLoadOptions &opts, int3 &size, int cpp, int num_planes,
                                           const heif_channel out_planes[], const string &partname,
                                           AlphaType_ alpha_type)
{
    int img_w = heif_image_get_width(himage, out_planes[0]);
    int img_h = heif_image_get_height(himage, out_planes[0]);
    if (img_w != size.x || img_h != size.y)
    {
        spdlog::warn("Image size mismatch: {}x{} vs {}x{}", img_w, img_h, size.x, size.y);
        size.x = img_w;
        size.y = img_h;
    }
    // create the Image object now that we know the (possibly corrected) size and channels
    auto image                                      = make_shared<Image>(size.xy(), size.z);
    image->partname                                 = partname;
    image->metadata["header"]["Decoded colorspace"] = {
        {"value", int(heif_image_get_colorspace(himage))},
        {"string", fmt::format("{} ({})", colorspace_name(heif_image_get_colorspace(himage)),
                               int(heif_image_get_colorspace(himage)))},
        {"type", "int"}};
    image->metadata["header"]["Decoded chroma"] = {
        {"value", int(heif_image_get_chroma_format(himage))},
        {"string", fmt::format("{} ({})", chroma_name(heif_image_get_chroma_format(himage)),
                               int(heif_image_get_chroma_format(himage)))},
        {"type", "int"}};

    // NCLX: prefer image-level then fall back to handle-level
    auto image_level_nclx = HeifNCLXPtr(
        [&]
        {
            heif_color_profile_nclx *nclx_raw = nullptr;
            if (auto err1 = heif_image_get_nclx_color_profile(himage, &nclx_raw); err1.code == heif_error_Ok)
                return nclx_raw;
            else
                return (heif_color_profile_nclx *)nullptr;
        }(),
        heif_nclx_color_profile_free);

    const heif_color_profile_nclx *nclx = nullptr;
    if (image_level_nclx)
    {
        nclx = image_level_nclx.get();
        CICPProfile cicp((uint8_t)nclx->color_primaries, (uint8_t)nclx->transfer_characteristics,
                         (uint8_t)nclx->matrix_coefficients, nclx->full_range_flag != 0);
        image->metadata["header"]["CICP profile (image level)"] =
            json{{"value", cicp.to_array()}, {"string", cicp.short_name()}, {"type", "array"}};
    }
    // else
    if (handle_level_nclx)
    {
        nclx = handle_level_nclx;
        CICPProfile cicp((uint8_t)nclx->color_primaries, (uint8_t)nclx->transfer_characteristics,
                         (uint8_t)nclx->matrix_coefficients, nclx->full_range_flag != 0);
        image->metadata["header"]["CICP profile (handle level)"] =
            json{{"value", cicp.to_array()}, {"string", cicp.short_name()}, {"type", "array"}};
    }

    // get the image-level icc profile.
    std::vector<uint8_t> image_level_icc_profile;
    {
        image_level_icc_profile.resize(heif_image_get_raw_color_profile_size(himage));
        if (auto err =
                heif_image_get_raw_color_profile(himage, reinterpret_cast<void *>(image_level_icc_profile.data()));
            err.code != heif_error_Ok)
            image_level_icc_profile.clear();
    }

    // first try the image level, then the handle level
    if (!image_level_icc_profile.empty())
    {
        image->icc_data = image_level_icc_profile;
        image->metadata["header"]["ICC color profile"] =
            json{{"value", 2}, {"string", "present at image level"}, {"type", "enum"}};
    }
    else if (!handle_level_icc_profile.empty())
    {
        image->icc_data = handle_level_icc_profile;
        image->metadata["header"]["ICC color profile"] =
            json{{"value", 1}, {"string", "present at handle level"}, {"type", "enum"}};
    }

    spdlog::debug("Copying image channels...");
    Timer timer;
    // the code below works for both interleaved (RGBA) and planar (YA) channel layouts
    for (int p = 0; p < num_planes; ++p)
    {
        int            bytes_per_line = 0;
        const uint8_t *pixels         = heif_image_get_plane(himage, out_planes[p], &bytes_per_line);
        int            bpp_storage    = heif_image_get_bits_per_pixel(himage, out_planes[p]);
        int            bpc            = heif_image_get_bits_per_pixel_range(himage, out_planes[p]);
        spdlog::debug("Bits per pixel: {}; Bits per pixel storage: {}; Channels per pixel: {}; Bytes per line: {}", bpc,
                      bpp_storage, cpp, bytes_per_line);
        if (bpp_storage != cpp * 16 && bpp_storage != cpp * 8)
            throw runtime_error(fmt::format("Unsupported bits per pixel: {}", bpp_storage));
        if (p == 0)
        {
            image->metadata["pixel format"] = fmt::format("{}-bit ({} bpc)", size.z * bpc, bpc);
            image->set_bits_per_sample(bpc);
        }
        float bpc_div = 1.f / ((1 << bpc) - 1);

        // Copy pixels into a contiguous float buffer, normalized to [0,1] -- tens of millions of samples at
        // full resolution, so it runs on the thread pool like the linearize and channel-copy stages below.
        // Branching on the sample width per row rather than per sample lets each loop vectorize, and the
        // buffer is a unique_ptr because value-initializing a vector this size costs more than the loop
        // that goes on to overwrite every element of it.
        auto       float_pixels = std::unique_ptr<float[]>(new float[(size_t)size.x * size.y * cpp]);
        const bool is_16bit     = (bpp_storage == cpp * 16);
        const int  block_size   = std::max(1, 1024 * 1024 / (size.x * cpp));
        parallel_for(blocked_range<int>(0, size.y, block_size),
                     [&](int begin_y, int end_y, int, int)
                     {
                         for (int y = begin_y; y < end_y; ++y)
                         {
                             float *out = &float_pixels[(size_t)y * size.x * cpp];
                             if (is_16bit)
                             {
                                 auto row = reinterpret_cast<const uint16_t *>(pixels + y * (size_t)bytes_per_line);
                                 for (int i = 0; i < size.x * cpp; ++i) out[i] = bpc_div * row[i];
                             }
                             else
                             {
                                 auto row = reinterpret_cast<const uint8_t *>(pixels + y * (size_t)bytes_per_line);
                                 for (int i = 0; i < size.x * cpp; ++i) out[i] = bpc_div * row[i];
                             }
                         }
                     });

        // Alpha is not a color: it carries neither a transfer function nor primaries, so it is copied
        // through as decoded. Only the monochrome path reaches this with a separate alpha plane -- the
        // interleaved paths hand alpha to linearize_pixels(), which already leaves the last channel alone.
        if (out_planes[p] != heif_channel_Alpha)
        {
            // Inverting the transfer function does not commute with multiplication by alpha; see
            // imageio/alpha.h.
            unpremultiply_before_transfer(float_pixels.get(), int3{size.xy(), cpp}, alpha_type);

            if (opts.override_profile)
            {
                spdlog::info("Ignoring embedded color profile with user-specified profile: {} {}",
                             color_gamut_name(opts.gamut_override), transfer_function_name(opts.tf_override));

                string         profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::Unspecified);
                Chromaticities chr;
                if (linearize_pixels(float_pixels.get(), int3{size.xy(), cpp},
                                     gamut_chromaticities(opts.gamut_override), opts.tf_override, opts.keep_primaries,
                                     &profile_desc, &chr))
                {
                    image->chromaticities = chr;
                    profile_desc += " (override)";
                }
                image->metadata["color profile"] = profile_desc;
            }
            else
            {
                // only prefer the nclx if it exists and it specifies an HDR transfer function
                bool prefer_icc =
                    !image->icc_data.empty() &&
                    (!nclx || (nclx->transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_HLG &&
                               nclx->transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_PQ));

                spdlog::debug("prefer_icc: {}, nclx transfer function: {}", prefer_icc,
                              nclx ? int(nclx->transfer_characteristics) : -1);
                string         profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::Unspecified);
                Chromaticities chr;
                // for SDR profiles, try to transform the interleaved data using the icc profile.
                // Then try the nclx profile
                if ((prefer_icc && ICCProfile(image->icc_data)
                                       .linearize_pixels(float_pixels.get(), int3{size.xy(), cpp}, opts.keep_primaries,
                                                         &profile_desc, &chr)) ||
                    linearize_colors(float_pixels.get(), int3{size.xy(), cpp}, opts.keep_primaries, nclx, &profile_desc,
                                     &chr))
                    image->chromaticities = chr;
                else
                    // icc and nclx profiles failed or not present, so we can only assume we are sRGB/BT709
                    to_linear(float_pixels.get(), int3{size.xy(), cpp}, TransferFunction::Unspecified);

                image->metadata["color profile"] = profile_desc;
            }

            repremultiply_after_transfer(float_pixels.get(), int3{size.xy(), cpp}, alpha_type);
        }

        // copy the interleaved float pixels into the channels
        for (int c = 0; c < cpp; ++c)
            image->channels[p * cpp + c].copy_from_interleaved(float_pixels.get(), size.x, size.y, cpp, c,
                                                               [](float v) { return v; });
    }

    spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));
    return image;
}

//! Decode an auxiliary image's samples into normalized floats, applying no color management.
/*!
    An auxiliary image such as a gain map is not a picture -- its samples are coefficients -- so
    pushing it through the base image's ICC or NCLX profile would be meaningless. Unlike
    process_decoded_heif_image(), this normalizes the samples to [0,1] and stops there.

    \param aux_handle  Handle of the auxiliary image to decode
    \return            The decoded map, at whatever resolution and channel count the file stored it
*/
static GainmapImage decode_aux_gainmap(heif_image_handle *aux_handle)
{
    heif_colorspace preferred_colorspace = heif_colorspace_undefined;
    heif_chroma     preferred_chroma     = heif_chroma_undefined;
    heif_image_handle_get_preferred_decoding_colorspace(aux_handle, &preferred_colorspace, &preferred_chroma);

    // Apple's maps are monochrome; decoding one as RGB would trigger a needless upsample to three
    // identical planes. Anything else is asked for as interleaved RGB.
    const bool mono = (preferred_chroma == heif_chroma_monochrome);

    const heif_colorspace out_colorspace = mono ? heif_colorspace_monochrome : heif_colorspace_RGB;
    const heif_chroma     out_chroma     = mono ? heif_chroma_monochrome : heif_chroma_interleaved_RRGGBB_LE;
    const heif_channel    out_plane      = mono ? heif_channel_Y : heif_channel_interleaved;
    const int             channels       = mono ? 1 : 3;

    check_image_dimensions(heif_image_handle_get_width(aux_handle), heif_image_handle_get_height(aux_handle),
                           "HEIF auxiliary image");

    HeifImagePtr himage(
        [&]
        {
            heif_image *raw_img = nullptr;
            throw_on_error(heif_decode_image(aux_handle, &raw_img, out_colorspace, out_chroma, nullptr),
                           "Failed to decode HEIF auxiliary image");
            return raw_img;
        }(),
        heif_image_release);

    const int w = heif_image_get_width(himage.get(), out_plane);
    const int h = heif_image_get_height(himage.get(), out_plane);
    if (w <= 0 || h <= 0)
        throw runtime_error{"HEIF auxiliary image decoded to an empty plane"};

    int            bytes_per_line = 0;
    const uint8_t *plane          = heif_image_get_plane(himage.get(), out_plane, &bytes_per_line);
    if (!plane)
        throw runtime_error{"HEIF auxiliary image has no pixel data"};

    const int bpp_storage = heif_image_get_bits_per_pixel(himage.get(), out_plane);
    const int bpc         = heif_image_get_bits_per_pixel_range(himage.get(), out_plane);
    if (bpp_storage != channels * 16 && bpp_storage != channels * 8)
        throw runtime_error{fmt::format("Unsupported bits per pixel in HEIF auxiliary image: {}", bpp_storage)};

    const bool  is_16bit = (bpp_storage == channels * 16);
    const float bpc_div  = 1.f / ((1 << bpc) - 1);

    GainmapImage out;
    out.size     = int2{w, h};
    out.channels = channels;
    out.pixels.resize(size_t(w) * h * channels);
    for (int y = 0; y < h; ++y)
    {
        auto row8  = reinterpret_cast<const uint8_t *>(plane + y * (size_t)bytes_per_line);
        auto row16 = reinterpret_cast<const uint16_t *>(plane + y * (size_t)bytes_per_line);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < channels; ++c)
                out.pixels[(size_t(y) * w + x) * channels + c] =
                    bpc_div * (is_16bit ? row16[channels * x + c] : row8[channels * x + c]);
    }

    return out;
}

//! Reconstruct \p image's HDR rendition from an Apple gain map, when the file carries one.
/*!
    Apple stores its gain maps as auxiliary images typed urn:com:apple:photo:<year>:aux:hdrgainmap,
    and puts the reconstruction strength in the primary image's maker note rather than alongside the
    map itself. ISO 21496-1 gain maps in HEIC are a different mechanism -- 'tmap' derived items --
    that libheif has no API for yet, so files carrying only those are still read as their base
    rendition.

    \param ihandle  Handle of the top-level image whose auxiliary images to search
    \param image    Base image, already linearized. Modified in place
    \param opts     Load options, for the target headroom
*/
static void apply_heif_gainmap(const heif_image_handle *ihandle, Image &image, const ImageLoadOptions &opts)
{
    // Alpha and depth are auxiliary images too, but neither is ever a gain map, and libheif has
    // already folded any alpha into the base image by this point.
    static constexpr int filter = LIBHEIF_AUX_IMAGE_FILTER_OMIT_ALPHA | LIBHEIF_AUX_IMAGE_FILTER_OMIT_DEPTH;

    const int num_aux = heif_image_handle_get_number_of_auxiliary_images(ihandle, filter);
    if (num_aux <= 0)
        return;

    spdlog::debug("Image has {} auxiliary image(s).", num_aux);

    std::vector<heif_item_id> aux_ids((size_t)num_aux);
    heif_image_handle_get_list_of_auxiliary_image_IDs(ihandle, filter, aux_ids.data(), num_aux);

    for (auto aux_id : aux_ids)
    {
        heif_image_handle *raw_aux = nullptr;
        if (heif_image_handle_get_auxiliary_image_handle(ihandle, aux_id, &raw_aux).code != heif_error_Ok || !raw_aux)
        {
            spdlog::warn("Failed to get auxiliary image handle for id {}", aux_id);
            continue;
        }
        HeifImageHandlePtr aux(raw_aux, heif_image_handle_release);

        string      aux_type;
        const char *type_str = nullptr;
        if (heif_image_handle_get_auxiliary_type(aux.get(), &type_str).code == heif_error_Ok && type_str)
        {
            aux_type = type_str;
            heif_image_handle_release_auxiliary_type(aux.get(), &type_str);
        }

        spdlog::info("Auxiliary image {}: '{}'", aux_id, aux_type);

        if (!is_apple_gainmap_type(aux_type))
            continue;

        image.metadata["header"]["Auxiliary image type"] = {
            {"value", aux_type}, {"string", aux_type}, {"type", "string"}};

        try
        {
            apply_apple_gainmap(image, decode_aux_gainmap(aux.get()), apple_gainmap_params(image.exif),
                                opts.gainmap_headroom, opts.gainmap_renditions);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Failed to apply Apple gain map: {}", e.what());
        }

        // Stop at the first: a file carrying both Apple's 2020 and 2023 aux types is
        // describing one gain map twice, not two different ones.
        return;
    }
}

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

    // Extract file-level metadata: MIME type and main brand
    const char *mime          = heif_get_file_mime_type(reinterpret_cast<const uint8_t *>(raw_data.data()),
                                                        (int)std::min<size_t>(raw_size, std::numeric_limits<int>::max()));
    char        main_brand[5] = {0, 0, 0, 0, 0};
    heif_brand_to_fourcc(heif_read_main_brand(reinterpret_cast<const uint8_t *>(raw_data.data()),
                                              (int)std::min<size_t>(raw_size, std::numeric_limits<int>::max())),
                         main_brand);

    vector<ImagePtr> images;
    try
    {
        // Create C API context and read from memory
        HeifContextPtr ctx(heif_context_alloc(), heif_context_free);
        if (!ctx)
            throw std::runtime_error("Failed to allocate heif context");

        throw_on_error(heif_context_read_from_memory_without_copy(
                           ctx.get(), reinterpret_cast<const void *>(raw_data.data()), raw_size, nullptr),
                       "Failed to read HEIF memory");

        ImGuiTextFilter filter{opts.channel_selector.c_str()};
        filter.Build();

        std::vector<heif_item_id> item_ids(heif_context_get_number_of_top_level_images(ctx.get()));
        heif_context_get_list_of_top_level_image_IDs(ctx.get(), item_ids.data(), (int)item_ids.size());

        if (!item_ids.empty())
        {
            heif_item_id primary_id = 0;
            throw_on_error(heif_context_get_primary_image_ID(ctx.get(), &primary_id), "Failed to get primary image ID");

            // remove the primary item from the list of all items
            for (size_t i = 0; i < item_ids.size(); ++i)
                if (item_ids[i] == primary_id)
                {
                    item_ids.erase(item_ids.begin() + i);
                    break;
                }

            int num_subimages = 1 + int(item_ids.size());

            spdlog::info("Found {} subimages", num_subimages);

            for (int subimage = 0; subimage < num_subimages; ++subimage)
            {
                spdlog::info("Loading subimage {}...", subimage);
                auto id = (subimage == 0) ? primary_id : item_ids[subimage - 1];

                if (auto name = fmt::format("{:d}.R,G,B", id); !filter.PassFilter(name.c_str()))
                {
                    spdlog::debug("Color channels '{}' filtered out by channel selector '{}'", name,
                                  opts.channel_selector);
                    continue;
                }

                heif_image_handle *raw_ihandle = nullptr;
                if (heif_context_get_image_handle(ctx.get(), id, &raw_ihandle).code != heif_error_Ok || !raw_ihandle)
                {
                    spdlog::warn("Failed to get image handle for id {}", id);
                    continue;
                }
                HeifImageHandlePtr ihandle(raw_ihandle, heif_image_handle_release);

                // We query the preferred chroma here so that we can decode monochrome images as monochrome.
                // All other types are decoded as RGB.
                heif_colorspace preferred_colorspace = heif_colorspace_undefined;
                heif_chroma     preferred_chroma     = heif_chroma_undefined;
                heif_image_handle_get_preferred_decoding_colorspace(ihandle.get(), &preferred_colorspace,
                                                                    &preferred_chroma);
                spdlog::info("Preferred decoding colorspace: {}, chroma: {}", (int)preferred_colorspace,
                             (int)preferred_chroma);

                int3 size{heif_image_handle_get_width(ihandle.get()), heif_image_handle_get_height(ihandle.get()), 0};
                check_image_dimensions(size.x, size.y, "HEIF");
                bool has_alpha = heif_image_handle_has_alpha_channel(ihandle.get()) != 0;

                heif_chroma     out_chroma;
                heif_colorspace out_colorspace;
                heif_channel    out_planes[2] = {heif_channel_Y, heif_channel_Alpha};
                int             cpp           = 0; // channels per plane
                int             num_planes    = 1;
                if (preferred_chroma == heif_chroma_monochrome)
                {
                    out_chroma     = heif_chroma_monochrome;
                    out_colorspace = heif_colorspace_monochrome;
                    out_planes[0]  = heif_channel_Y;
                    size.z         = has_alpha ? 2 : 1;
                    cpp            = 1;
                    num_planes     = size.z;
                }
                else
                {
                    out_chroma = has_alpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE;
                    out_colorspace = heif_colorspace_RGB;
                    out_planes[0]  = heif_channel_interleaved;
                    size.z         = has_alpha ? 4 : 3;
                    cpp            = size.z;
                    num_planes     = 1;
                }
                spdlog::info("Image size: {}", size);

                auto handle_level_nclx = HeifNCLXPtr(
                    [&]
                    {
                        heif_color_profile_nclx *nclx_raw = nullptr;
                        if (auto err = heif_image_handle_get_nclx_color_profile(ihandle.get(), &nclx_raw);
                            err.code == heif_error_Ok)
                        {
                            spdlog::debug("Found handle-level NCLX color profile: color primaries = {}, transfer "
                                          "characteristics = {}, matrix coefficients = {}",
                                          (int)nclx_raw->color_primaries, (int)nclx_raw->transfer_characteristics,
                                          (int)nclx_raw->matrix_coefficients);
                            return nclx_raw;
                        }
                        else
                            return (heif_color_profile_nclx *)nullptr;
                    }(),
                    heif_nclx_color_profile_free);

                std::vector<uint8_t> handle_level_icc_profile;
                {
                    handle_level_icc_profile.resize(heif_image_handle_get_raw_color_profile_size(ihandle.get()));
                    if (auto err2 = heif_image_handle_get_raw_color_profile(
                            ihandle.get(), reinterpret_cast<void *>(handle_level_icc_profile.data()));
                        err2.code != heif_error_Ok)
                        handle_level_icc_profile.clear();
                }

                spdlog::info("Decoding heif image...");
                HeifImagePtr himage(
                    [&]
                    {
                        heif_image            *raw_img = nullptr;
                        HeifDecodingOptionsPtr options(heif_decoding_options_alloc(), heif_decoding_options_free);
                        options->color_conversion_options.preferred_chroma_upsampling_algorithm =
                            heif_chroma_upsampling_bilinear;
                        options->color_conversion_options.only_use_preferred_chroma_algorithm = true;
                        // options->ignore_transformations                                       = true;
                        heif_error err;
                        throw_on_error(
                            err = heif_decode_image(ihandle.get(), &raw_img, out_colorspace, out_chroma, options.get()),
                            "Failed to decode HEIF image");

                        // show decoding warnings
                        for (int i = 0;; i++)
                        {
                            int n = heif_image_get_decoding_warnings(raw_img, i, &err, 1);
                            if (n == 0)
                                break;

                            spdlog::warn("HEIF decoding warning: {}", err.message);
                        }
                        return raw_img;
                    }(),
                    heif_image_release);
                // Resolved before decoding: the color management inside brackets itself with it.
                const AlphaType_ alpha_type =
                    effective_alpha_type(opts, !has_alpha ? AlphaType_None
                                                          : (heif_image_handle_is_premultiplied_alpha(ihandle.get())
                                                                 ? AlphaType_PremultipliedLinear
                                                                 : AlphaType_Straight));

                // create Image from decoded heif_image; process_decoded_heif_image will create and fill pixel data
                ImagePtr image =
                    process_decoded_heif_image(himage.get(), handle_level_nclx.get(), handle_level_icc_profile, opts,
                                               size, cpp, num_planes, out_planes, fmt::format("{:d}", id), alpha_type);

                // preserve file-level metadata that comes from the handle/context
                image->filename                         = filename;
                image->alpha_type                       = alpha_type;
                image->metadata["header"]["MIME type"]  = {{"value", mime}, {"string", mime}, {"type", "string"}};
                image->metadata["header"]["Main brand"] = {
                    {"value", main_brand}, {"string", main_brand}, {"type", "string"}};
                image->metadata["loader"] = "libheif" + std::string(" (" + std::string(main_brand) + ")");
                image->metadata["header"]["Preferred colorspace"] = {
                    {"value", int(preferred_colorspace)},
                    {"string",
                     fmt::format("{} ({})", colorspace_name(preferred_colorspace), int(preferred_colorspace))},
                    {"type", "int"}};
                image->metadata["header"]["Preferred chroma"] = {
                    {"value", int(preferred_chroma)},
                    {"string", fmt::format("{} ({})", chroma_name(preferred_chroma), int(preferred_chroma))},
                    {"type", "int"}};

                // EXIF data block includes 4-byte length prefix that we need to skip
                {
                    std::vector<uint8_t> exif_data, xmp_data;
                    int num_blocks = heif_image_handle_get_number_of_metadata_blocks(ihandle.get(), nullptr);

                    if (num_blocks > 0)
                        spdlog::debug("Image has {} metadata block(s).", num_blocks);

                    spdlog::info("Found {} metadata block(s). Attempting to decode...", num_blocks);
                    std::vector<heif_item_id> block_IDs(num_blocks);
                    heif_image_handle_get_list_of_metadata_block_IDs(ihandle.get(), nullptr, block_IDs.data(),
                                                                     num_blocks);
                    for (auto block_ID : block_IDs)
                    {
                        const string_view type = heif_image_handle_get_metadata_type(ihandle.get(), block_ID);
                        const string_view content_type =
                            heif_image_handle_get_metadata_content_type(ihandle.get(), block_ID);
                        size_t data_size = heif_image_handle_get_metadata_size(ihandle.get(), block_ID);
                        if (data_size <= 4)
                        {
                            spdlog::warn("Failed to get size of EXIF data.");
                            continue;
                        }
                        if (type != "Exif" && content_type != "application/rdf+xml")
                            continue;

                        try
                        {
                            std::vector<uint8_t> data(data_size);
                            throw_on_error(heif_image_handle_get_metadata(ihandle.get(), block_ID, data.data()),
                                           "Failed to get EXIF metadata block");

                            if (type == "Exif")
                            {
                                exif_data = std::move(data);
                                spdlog::info("Successfully retrieved EXIF metadata block ({} bytes)", data_size);
                            }
                            else if (content_type == "application/rdf+xml")
                            {
                                xmp_data = std::move(data);
                                spdlog::info("Successfully retrieved XMP metadata block ({} bytes)", data_size);
                            }
                        }
                        catch (const std::exception &e)
                        {
                            spdlog::warn(fmt::format("Failed to read metadata block: {}", e.what()));
                            continue;
                        }
                    }

                    if (exif_data.size() > 4)
                    {
                        try
                        {
                            image->exif                = Exif{exif_data.data() + 4, exif_data.size() - 4};
                            image->metadata["exif"]    = image->exif.to_json();
                            image->orientation_applied = true;
                            spdlog::debug("EXIF metadata successfully parsed: {}", image->metadata["exif"].dump(2));
                        }
                        catch (const std::exception &e)
                        {
                            spdlog::warn("Exception while parsing EXIF chunk: {}", e.what());
                            image->exif.reset();
                        }
                    }

                    if (xmp_data.size() > 4)
                        image->xmp_data = std::move(xmp_data);
                }

                // After the EXIF block above, since Apple's gain maps are sized by its maker note.
                apply_heif_gainmap(ihandle.get(), *image, opts);

                images.emplace_back(image);
            }
        }

        // --- sequence tracks: decode all images from any image/video sequence tracks
#if HEIF_HAS_SEQUENCES_HTJ2K
        if (heif_context_has_sequence(ctx.get()))
        {
            std::vector<uint32_t> track_ids(heif_context_number_of_sequence_tracks(ctx.get()));
            heif_context_get_track_ids(ctx.get(), track_ids.data());
            if (!track_ids.empty())
                spdlog::info("Found {} sequence track(s). Attempting to decode sequence images...", track_ids.size());

            for (auto track_id : track_ids)
            {
                heif_track  *raw_track = heif_context_get_track(ctx.get(), track_id);
                HeifTrackPtr track(raw_track, heif_track_release);
                if (!track)
                {
                    spdlog::warn("Failed to get track {}", track_id);
                    continue;
                }

                heif_track_type ttype = heif_track_get_track_handler_type(track.get());
                // Only decode visual/image sequence or video tracks
                if (ttype != heif_track_type_image_sequence && ttype != heif_track_type_video)
                    continue;

                // optional: get track resolution
                uint16_t tr_w = 0, tr_h = 0;
                if (heif_track_get_image_resolution(track.get(), &tr_w, &tr_h).code != heif_error_Ok)
                    tr_w = tr_h = 0;
                int3 size{tr_w, tr_h, 3};
                spdlog::info("Decoding sequence track {} (type: {}, resolution: {}x{})...", track_id, (int)ttype, tr_w,
                             tr_h);

                int frame_index = 0;
                while (true)
                {
                    heif_image *raw_img = nullptr;
                    heif_error  terr    = heif_track_decode_next_image(track.get(), &raw_img, heif_colorspace_RGB,
                                                                       heif_chroma_interleaved_RRGGBB_LE, nullptr);
                    if (terr.code == heif_error_End_of_sequence)
                        break;
                    if (terr.code != heif_error_Ok || !raw_img)
                    {
                        spdlog::warn("Error decoding sequence image on track {}: {}", track_id, terr.message);
                        break;
                    }

                    // show decoding warnings
                    for (int i = 0;; i++)
                    {
                        int n = heif_image_get_decoding_warnings(raw_img, i, &terr, 1);
                        if (n == 0)
                            break;

                        spdlog::warn("HEIF decoding warning: {}", terr.message);
                    }

                    int    track_digits = (int)std::to_string(std::max<size_t>(1, track_ids.size())).length();
                    string partname     = fmt::format("track {:0{}}.frame {:04}", track_id, track_digits, frame_index);
                    if (!filter.PassFilter(partname.c_str()))
                    {
                        spdlog::debug("Skipping frame {} of track {} (filtered out by channel selector)", frame_index,
                                      track_id);
                        ++frame_index;
                        continue;
                    }

                    spdlog::debug("Decoded frame {} of track {}", frame_index, track_id);

                    HeifImagePtr himage(raw_img, heif_image_release);

                    heif_channel out_planes[2] = {heif_channel_interleaved, heif_channel_Alpha};
                    ImagePtr image = process_decoded_heif_image(himage.get(), nullptr, {}, opts, size, 3, 1, out_planes,
                                                                partname, AlphaType_None);
                    image->filename                         = filename;
                    image->metadata["header"]["MIME type"]  = {{"value", mime}, {"string", mime}, {"type", "string"}};
                    image->metadata["header"]["Main brand"] = {
                        {"value", main_brand}, {"string", main_brand}, {"type", "string"}};
                    image->metadata["loader"] = "libheif" + std::string(" (" + std::string(main_brand) + ")");
                    images.emplace_back(image);
                    ++frame_index;
                }
            }
        }
#endif // HEIF_HAS_SEQUENCES_HTJ2K
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
    catch (...)
    {
        // spdlog::debug("Cannot load image with libheif: {}", e.what());
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

    ensure_heif_initialized();

    // The encoder the options name has to actually implement the codec they ask for -- a stale index, or
    // one left over from the other of the two dialog entries, would otherwise write the wrong thing.
    size_t     encoder_index = params->encoder;
    const auto candidates    = encoders_for(params->codec);
    if (std::find(candidates.begin(), candidates.end(), encoder_index) == candidates.end())
        encoder_index = default_encoder_for(params->codec, params->use_alpha);

    if (encoder_index >= s_encoders.size() || !s_encoders[encoder_index])
        throw std::runtime_error("HEIF: no encoder available");

    // Grab the shared C encoder instance configured by the GUI.
    auto enc = s_encoders[encoder_index].get();
    try
    {
        Timer timer;

        int                        w = 0, h = 0, n = 0;
        std::unique_ptr<uint8_t[]> pixels8;
        void                      *pixels = nullptr;
        pixels8 = img.as_interleaved<uint8_t>(&w, &h, &n, params->gain, params->tf, true, true, true);

        // Not every codec a HEIF can hold has an alpha channel -- JPEG does not -- and losing one is a real
        // change to the image, so it is said out loud rather than written quietly.
        const auto encoder_format =
            heif_encoder_descriptor_get_compression_format(s_encoder_descriptors[encoder_index]);
        const bool group_alpha = group_has_alpha(img.groups[img.selected_group].type);
        bool       keep_alpha  = params->use_alpha;
        if (keep_alpha && group_alpha && !codec_stores_alpha(encoder_format))
        {
            spdlog::warn("HEIF: the {} encoder stores no alpha channel; saving without it.",
                         heif_encoder_descriptor_get_name(s_encoder_descriptors[encoder_index]));
            keep_alpha = false;
        }

        // HEIF stores one or three color channels, each optionally with alpha. A group carrying alpha
        // loses it when it cannot be kept; a two-channel U,V pair carries no alpha and has no
        // one-channel reading, so it pads out to RGB with a zero third channel, as the viewport draws it.
        const int n_out = (group_alpha && !keep_alpha) ? n - 1 : (n == 2 && !group_alpha ? 3 : n);
        if (n_out != n)
        {
            const size_t               num_pixels = (size_t)w * h;
            std::unique_ptr<uint8_t[]> repacked(new uint8_t[num_pixels * n_out]);
            for (size_t i = 0; i < num_pixels; ++i)
                for (int c = 0; c < n_out; ++c) repacked[i * n_out + c] = (c < n) ? pixels8[i * n + c] : uint8_t(0);
            pixels8.swap(repacked);
            n = n_out;
        }
        pixels = pixels8.get();

        if (!pixels || w <= 0 || h <= 0)
            throw std::runtime_error("HEIF: empty image or invalid image dimensions");

        if (n < 1 || n > 4)
            throw std::invalid_argument("HEIF/AVIF output supports at most 4 channels");

        // One or two channels is a gray image, which HEIF stores as 4:0:0 monochrome rather than as three
        // equal color planes -- the same form load_heif_image() already decodes.
        const bool mono      = n <= 2;
        const bool has_alpha = n == 2 || n == 4;

        // Create heif image via C API
        HeifContextPtr ctx(heif_context_alloc(), heif_context_free);
        if (!ctx)
            throw std::runtime_error("HEIF: Failed to allocate encoding context");

        HeifImagePtr heif_img(
            [&]
            {
                heif_image *raw_heif_img = nullptr;
                throw_on_error(
                    heif_image_create(w, h, mono ? heif_colorspace_monochrome : heif_colorspace_RGB,
                                      mono ? heif_chroma_monochrome
                                           : (n == 4 ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB),
                                      &raw_heif_img),
                    "HEIF: Failed to create heif image");
                return raw_heif_img;
            }(),
            heif_image_release);

        if (mono)
        {
            // Monochrome keeps its channels in separate planes, so the interleaved buffer is split apart.
            throw_on_error(heif_image_add_plane(heif_img.get(), heif_channel_Y, w, h, 8),
                           "HEIF: Failed to add luma plane");
            if (has_alpha)
                throw_on_error(heif_image_add_plane(heif_img.get(), heif_channel_Alpha, w, h, 8),
                               "HEIF: Failed to add alpha plane");

            int      y_stride = 0, a_stride = 0;
            uint8_t *y_plane = heif_image_get_plane(heif_img.get(), heif_channel_Y, &y_stride);
            uint8_t *a_plane =
                has_alpha ? heif_image_get_plane(heif_img.get(), heif_channel_Alpha, &a_stride) : nullptr;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const uint8_t *src                = pixels8.get() + ((size_t)y * w + x) * n;
                    y_plane[y * (size_t)y_stride + x] = src[0];
                    if (a_plane)
                        a_plane[y * (size_t)a_stride + x] = src[1];
                }
        }
        else
        {
            throw_on_error(heif_image_add_plane(heif_img.get(), heif_channel_interleaved, w, h, 8),
                           "HEIF: Failed to add interleaved plane");

            int      stride    = 0;
            uint8_t *plane     = heif_image_get_plane(heif_img.get(), heif_channel_interleaved, &stride);
            size_t   row_bytes = w * n * sizeof(uint8_t);
            for (int y = 0; y < h; ++y) memcpy(plane + y * (size_t)stride, pixels8.get() + y * w * n, row_bytes);
        }

        // Set color profile (nclx)
        {
            auto nclx = HeifNCLXPtr(heif_nclx_color_profile_alloc(), heif_nclx_color_profile_free);
            if (!nclx)
                throw std::runtime_error("HEIF: Failed to allocate nclx profile");
            nclx->color_primaries = heif_color_primaries_ITU_R_BT_709_5; // TODO map from img.chromaticities
            auto heif_tf          = transfer_function_to_heif(params->tf);
            if (!is_heif_transfer_supported(params->tf))
            {
                spdlog::warn("HEIF: Transfer function '{}' not supported, falling back to sRGB.",
                             transfer_function_name(params->tf));
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
            heif_image_handle_release);

        throw_on_error(heif_context_set_primary_image(ctx.get(), out_handle.get()),
                       "HEIF: Failed to set primary image");

        // Writer trampoline to write to std::ostream
        static struct heif_writer c_writer = {
            1,
            [](struct heif_context * /*ctx*/, const void *data, size_t size,
               void *userdata) -> struct heif_error{std::ostream *os = reinterpret_cast<std::ostream *>(userdata);
        struct heif_error herr = heif_error_success;
        os->write(reinterpret_cast<const char *>(data), size);
        if (!*os)
        {
            herr.code    = heif_error_Encoding_error;
            herr.message = "Failed to write to output stream";
        }
        return herr;
    }
};

throw_on_error(heif_context_write(ctx.get(), &c_writer, &os),
               "HEIF: failed while writing encoded data to output stream");

spdlog::info("Saved image to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
}
catch (const std::exception &err) { throw std::runtime_error(fmt::format("HEIF error: {}", err.what())); }
}

// libaom accepts lossless coding only with chroma delta-q off, and the tuning metric libheif's aom plugin
// picks by default for a still image ("auto", which becomes AOM_TUNE_IQ) turns delta-q on -- so the two
// have to be set together. Under lossless coding the tuning metric has nothing to trade off, the output
// being bit-exact either way, so pinning it costs nothing. Plugins with no "tune" parameter reject the call.
static void set_heif_lossless(heif_encoder *enc, bool lossless)
{
    heif_encoder_set_lossless(enc, lossless);
    (void)heif_encoder_set_parameter_string(enc, "tune", lossless ? "ssim" : "auto");
}

void save_heif_image(const Image &img, std::ostream &os, std::string_view filename, float gain, int quality,
                     bool lossless, bool use_alpha, HEIFCodec codec, TransferFunction tf)
{
    // The encoder table is built lazily, and only heif_parameters_gui() does it; a non-GUI caller would
    // otherwise find it empty.
    init_heif_supported_formats();
    if (encoders_for(codec).empty())
        throw std::runtime_error("HEIF: no encoder available for the requested codec");

    HEIFSaveOptions params;
    params.gain      = gain;
    params.quality   = quality;
    params.lossless  = lossless;
    params.use_alpha = use_alpha;
    params.tf        = tf;
    params.codec     = codec;
    params.encoder   = default_encoder_for(codec, use_alpha);
    set_heif_lossless(s_encoders[params.encoder].get(), lossless);
    heif_encoder_set_lossy_quality(s_encoders[params.encoder].get(), quality);

    save_heif_image(img, os, filename, &params);
}

// GUI parameter function
std::vector<std::string> heif_encoder_names(HEIFCodec codec)
{
    init_heif_supported_formats();

    std::vector<std::string> names;
    for (size_t i : encoders_for(codec)) names.emplace_back(heif_encoder_descriptor_get_name(s_encoder_descriptors[i]));
    return names;
}

HEIFSaveOptions *heif_parameters_gui(HEIFCodec codec)
{
    init_heif_supported_formats();

    // The dialog's format list already chose the codec, so the combo below picks only among the encoders
    // implementing it. Each entry remembers its own choice: switching to AVIF and back must not leave
    // HEIF on the AV1 encoder AVIF had to move to, nor the other way round.
    static std::map<HEIFCodec, size_t> s_encoder_choice;

    s_opts.codec          = codec;
    const auto candidates = encoders_for(codec);
    auto      &remembered = s_encoder_choice[codec];
    if (std::find(candidates.begin(), candidates.end(), remembered) == candidates.end())
        remembered = default_encoder_for(codec, s_opts.use_alpha);
    s_opts.encoder = remembered;

    if (ImGui::PE::Begin("HEIF/AVIF Save Options",
                         ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
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
                if (ImGui::BeginCombo("##Transfer function", transfer_function_name(s_opts.tf).c_str()))
                {
                    for (int i = TransferFunction::Linear; i < TransferFunction::Count; ++i)
                    {
                        if (!is_heif_transfer_supported((TransferFunction::Type_)i))
                            continue;
                        bool selected = (s_opts.tf.type == (TransferFunction::Type_)i);
                        if (ImGui::Selectable(
                                transfer_function_name({(TransferFunction::Type_)i, s_opts.tf.gamma}).c_str(),
                                selected))
                            s_opts.tf.type = (TransferFunction::Type_)i;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::Tooltip("Encode the pixel values using this transfer function.");
                if (s_opts.tf.type == TransferFunction::Gamma)
                {
                    ImGui::Indent();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Gamma");
                    ImGui::SameLine(HelloImGui::EmSize(9.f));
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::SliderFloat("##Gamma", &s_opts.tf.gamma, 0.1f, 5.f);
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
            // A build often has just one encoder for a given codec -- AV1 has three implementations in
            // libheif but distributions package them separately -- and a dropdown offering one choice is
            // only a dropdown by accident. Then it is simply named.
            if (candidates.empty())
                ImGui::TextUnformatted("No encoder available");
            else if (candidates.size() == 1)
                ImGui::TextUnformatted(heif_encoder_descriptor_get_name(s_encoder_descriptors[candidates.front()]));
            else if (ImGui::BeginCombo("##Encoder",
                                       heif_encoder_descriptor_get_id_name(s_encoder_descriptors[s_opts.encoder])))
            {
                for (size_t i : candidates)
                {
                    bool selected = (s_opts.encoder == i);
                    if (ImGui::Selectable(heif_encoder_descriptor_get_name(s_encoder_descriptors[i]), selected))
                        s_opts.encoder = remembered = i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        if (enc_open && !candidates.empty())
        {
            auto selected_encoder = s_encoder_descriptors[s_opts.encoder];
            auto enc              = s_encoders[s_opts.encoder].get();

            // Lossless
            if (heif_encoder_descriptor_supports_lossless_compression(selected_encoder))
            {
                ImGui::BeginDisabled(!heif_encoder_descriptor_supports_lossy_compression(selected_encoder));
                bool lossless =
                    s_opts.lossless || !heif_encoder_descriptor_supports_lossy_compression(selected_encoder);
                if (ImGui::PE::Checkbox("Lossless", &lossless, "Use lossless compression."))
                {
                    s_opts.lossless = lossless;
                    set_heif_lossless(enc, lossless);
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
                    heif_encoder_set_lossy_quality(enc, s_opts.quality);
                ImGui::EndDisabled();
            }

            if (ImGui::PE::TreeNode("Advanced", ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DrawLinesFull))
            {
                auto params = heif_encoder_list_parameters(enc);
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
                        (void)heif_encoder_parameter_integer_valid_values(enc, name, &have_minimum, &have_maximum,
                                                                          &minimum, &maximum, &num_valid_values,
                                                                          &valid_values);

                        int cur = 0;
                        (void)heif_encoder_get_parameter_integer(enc, name, &cur);

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
                                        (void)heif_encoder_set_parameter_integer(enc, name, cur);
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
                                    (void)heif_encoder_set_parameter_integer(enc, name, cur);
                            }
                            else
                            {
                                if (ImGui::PE::DragInt(uppercase_name, &cur))
                                    (void)heif_encoder_set_parameter_integer(enc, name, cur);
                            }
                        }
                    }
                    break;
                    case heif_encoder_parameter_type_boolean:
                    {
                        int cur = 0;
                        (void)heif_encoder_get_parameter_boolean(enc, name, &cur);
                        bool b = (cur != 0);
                        if (ImGui::PE::Checkbox(uppercase_name, &b))
                            (void)heif_encoder_set_parameter_boolean(enc, name, b ? 1 : 0);
                    }
                    break;
                    case heif_encoder_parameter_type_string:
                    {
                        const char *const *valid_options = nullptr;
                        (void)heif_encoder_parameter_string_valid_values(enc, name, &valid_options);

                        constexpr int bufsize      = 512;
                        char          buf[bufsize] = {0};
                        (void)heif_encoder_get_parameter_string(enc, name, buf, bufsize);

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
                                                (void)heif_encoder_set_parameter_string(enc, name, valid_options[k]);
                                            if (selected)
                                                ImGui::SetItemDefaultFocus();
                                        }
                                        ImGui::EndCombo();
                                    }
                                }
                                else
                                {
                                    if (ImGui::InputText(("##" + std::string(name)).c_str(), buf, bufsize))
                                        (void)heif_encoder_set_parameter_string(enc, name, buf);
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
