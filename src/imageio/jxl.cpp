//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "app.h"
#include "exif.h"
#include "image.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>

#include "fonts.h"
#include "imgui_ext.h"

using namespace std;

struct JXLSaveOptions
{
    float            gain            = 1.f;
    bool             lossless        = false;
    int              quality         = 95;
    int              data_type_index = 0;
    TransferFunction tf              = TransferFunction::BT2100_PQ;
};

static JXLSaveOptions s_opts;

#ifndef HDRVIEW_ENABLE_JPEGXL

bool is_jxl_image(istream &is) noexcept { return false; }

bool jxl_supported_tf(TransferFunction::Type tf) noexcept { return false; }

vector<ImagePtr> load_jxl_image(istream &is, string_view filename)
{
    throw runtime_error("JPEG-XL support not enabled in this build.");
}

JXLSaveOptions *jxl_parameters_gui() { return &s_opts; }

void save_jxl_image(const Image &img, std::ostream &os, std::string_view filename, JXLSaveOptions *params)
{
    throw runtime_error("JPEG-XL support not enabled in this build.");
}

#else

#include <jxl/codestream_header.h>
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>
#include <jxl/types.h>
#include <jxl/version.h>

#include "colorspace.h"
#include "icc.h"
#include "timer.h"

static TransferFunction transfer_function_from_color_encoding(const JxlColorEncoding &enc)
{
    switch (enc.transfer_function)
    {
    case JXL_TRANSFER_FUNCTION_709: return TransferFunction::ITU;
    case JXL_TRANSFER_FUNCTION_SRGB: return TransferFunction::sRGB;
    case JXL_TRANSFER_FUNCTION_GAMMA: return {TransferFunction::Gamma, (float)enc.gamma};
    case JXL_TRANSFER_FUNCTION_LINEAR: return TransferFunction::Linear;
    case JXL_TRANSFER_FUNCTION_PQ: return TransferFunction::BT2100_PQ;
    case JXL_TRANSFER_FUNCTION_HLG: return TransferFunction::BT2100_HLG;
    case JXL_TRANSFER_FUNCTION_DCI: return TransferFunction::DCI_P3;
    case JXL_TRANSFER_FUNCTION_UNKNOWN: return TransferFunction::Unspecified;
    }
}

static JxlTransferFunction jxl_tf(TransferFunction tf) noexcept
{
    switch (tf.type)
    {
    case TransferFunction::Linear: return JXL_TRANSFER_FUNCTION_LINEAR;
    case TransferFunction::Gamma: return JXL_TRANSFER_FUNCTION_GAMMA;
    case TransferFunction::sRGB: return JXL_TRANSFER_FUNCTION_SRGB;
    case TransferFunction::ITU: return JXL_TRANSFER_FUNCTION_709;
    case TransferFunction::BT2100_PQ: return JXL_TRANSFER_FUNCTION_PQ;
    case TransferFunction::BT2100_HLG: return JXL_TRANSFER_FUNCTION_HLG;
    case TransferFunction::DCI_P3: return JXL_TRANSFER_FUNCTION_DCI;
    default: return JXL_TRANSFER_FUNCTION_UNKNOWN;
    }
}

bool jxl_supported_tf(TransferFunction::Type_ tf) noexcept { return jxl_tf(tf) != JXL_TRANSFER_FUNCTION_UNKNOWN; }

static string color_encoding_info(const JxlColorEncoding &enc)
{
    string out;
    out += format_indented(4, "White point xy: {} {}\n", enc.white_point_xy[0], enc.white_point_xy[1]);
    out += format_indented(4, "Red primary xy: {} {}\n", enc.primaries_red_xy[0], enc.primaries_red_xy[1]);
    out += format_indented(4, "Green primary xy: {} {}\n", enc.primaries_green_xy[0], enc.primaries_green_xy[1]);
    out += format_indented(4, "Blue primary xy: {} {}\n", enc.primaries_blue_xy[0], enc.primaries_blue_xy[1]);

    auto tf = transfer_function_from_color_encoding(enc);
    out += format_indented(4, "Transfer function: {}\n", transfer_function_name(tf));

    // print out the rendering intent in human readible form
    switch (enc.rendering_intent)
    {
    case JXL_RENDERING_INTENT_PERCEPTUAL: out += format_indented(4, "Rendering intent: perceptual\n"); break;
    case JXL_RENDERING_INTENT_RELATIVE: out += format_indented(4, "Rendering intent: relative\n"); break;
    case JXL_RENDERING_INTENT_SATURATION: out += format_indented(4, "Rendering intent: saturation\n"); break;
    case JXL_RENDERING_INTENT_ABSOLUTE: out += format_indented(4, "Rendering intent: absolute\n"); break;
    default: out += format_indented(4, "Rendering intent: unknown\n"); break;
    }

    switch (enc.color_space)
    {
    case JXL_COLOR_SPACE_RGB: out += format_indented(4, "Color space: RGB\n"); break;
    case JXL_COLOR_SPACE_GRAY: out += format_indented(4, "Color space: Gray\n"); break;
    case JXL_COLOR_SPACE_XYB: out += format_indented(4, "Color space: XYB\n"); break;
    case JXL_COLOR_SPACE_UNKNOWN: out += format_indented(4, "Color space: unknown\n"); break;
    default: out += format_indented(4, "Color space: unknown ({})\n", (int)enc.color_space); break;
    }
    return out;
}

bool is_jxl_image(istream &is) noexcept
{
    bool ret = false;
    try
    {
        // read only enough of the file to determine if it is a JPEG XL file
        uint8_t magic[128]{};
        is.read(reinterpret_cast<char *>(magic), sizeof(magic));
        // if ((size_t)is.gcount() != sizeof(magic))
        //     throw invalid_argument{
        //         fmt::format("Failed to read : {} bytes, read : {} bytes", sizeof(magic), (size_t)is.gcount())};

        JxlSignature signature = JxlSignatureCheck(magic, std::min(sizeof(magic), (size_t)is.gcount()));
        if (signature == JXL_SIG_CODESTREAM || signature == JXL_SIG_CONTAINER)
            ret = true;
        else
            invalid_argument{"Not a JPEG XL file"};
    }
    catch (const exception &e)
    {
        spdlog::debug("Cannot load image with libjxl: {}", e.what());
        ret = false;
    }

    is.clear();
    is.seekg(0);
    return ret;
}

static bool linearize_colors(float *pixels, int3 size, JxlColorEncoding file_enc, string *tf_description = nullptr,
                             Chromaticities *c = nullptr)
{
    Timer timer;
    spdlog::info("Linearizing pixel values using encoded profile.");
    if (c)
    {
        c->red   = float2(file_enc.primaries_red_xy[0], file_enc.primaries_red_xy[1]);
        c->green = float2(file_enc.primaries_green_xy[0], file_enc.primaries_green_xy[1]);
        c->blue  = float2(file_enc.primaries_blue_xy[0], file_enc.primaries_blue_xy[1]);
        c->white = float2(file_enc.white_point_xy[0], file_enc.white_point_xy[1]);
    }

    auto tf = file_enc.transfer_function == JXL_TRANSFER_FUNCTION_GAMMA
                  ? TransferFunction{TransferFunction::Gamma, (float)file_enc.gamma}
                  : transfer_function_from_cicp((int)file_enc.transfer_function);

    if (tf.type == TransferFunction::Unspecified)
        spdlog::warn("JPEG-XL: CICP transfer function ({}) is not recognized, assuming sRGB",
                     (int)file_enc.transfer_function);

    if (tf_description)
        *tf_description = transfer_function_name(tf);

    to_linear(pixels, size, tf);

    return true;
}

//
// Notes for future self after experimenting with libjxl v0.11.1 in January 2025, which don't seem to be well documented
// elsewhere:
//
// - Not setting any CMS (via JxlDecoderSetCms) and not specifying any color profile (via
//   JxlDecoderSetOutputColorProfile or JxlDecoderSetPreferredColorProfile). Setting the output JxlPixelFormat to
//   JXL_TYPE_FLOAT:
//     - The decoded image is Gray or RGB floating point, but applying the inv. transfer function and converting color
//       gamut is your responsibility. This is what we do now.
// - Setting the desired JxlDecoderSetOutputColorProfile to JXL_TRANSFER_FUNCTION_LINEAR with either the default (via
//   JxlGetDefaultCms) or custom (via JxlCmsInterface) CMS means that the CMS functions are never called!
//     - Retrieving the JXL_COLOR_PROFILE_TARGET_DATA JxlDecoderGetColorAsEncodedProfile seems to just return the
//       profile from JXL_COLOR_PROFILE_TARGET_ORIGINAL.
//     - SDR encoded RGB images in sRGB, P3, and Rec2020 seem to linearlized and color corrected properly (ProPhoto RGB
//       doesn't match). Images using HDR transfer functions (PQ or HLG) are too dark.
// - Setting the desired JxlDecoderSetOutputColorProfile to JXL_TRANSFER_FUNCTION_SRGB does call the CMS functions.
//

vector<ImagePtr> load_jxl_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "JXL"};
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

    std::vector<float>               pixels;
    JxlColorEncoding                 file_enc;
    JxlBasicInfo                     info;
    std::vector<uint8_t>             icc_profile;
    bool                             has_encoded_profile = false;
    std::vector<JxlExtraChannelInfo> extra_channel_infos;
    std::vector<string>              extra_channel_names;
    int3                             size{0, 0, 0};
    string                           frame_name;
    int                              frame_number        = 0;
    bool                             skip_color          = true;
    int                              first_black_channel = -1;
    bool                             is_cmyk             = false;
    bool                             prefer_icc          = false;

    vector<ImagePtr> images;
    ImagePtr         image;

    // Multi-threaded parallel runner.
    auto runner = JxlResizableParallelRunnerMake(nullptr);
    auto dec    = JxlDecoderMake(nullptr);
    if (JXL_DEC_SUCCESS !=
        JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE |
                                                 JXL_DEC_FRAME | JXL_DEC_BOX | JXL_DEC_BOX_COMPLETE))
        throw invalid_argument{"JxlDecoderSubscribeEvents failed"};

    if (JXL_DEC_SUCCESS != JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner, runner.get()))
        throw invalid_argument{"JxlDecoderSetParallelRunner failed"};

    if (JXL_DEC_SUCCESS != JxlDecoderSetDecompressBoxes(dec.get(), JXL_TRUE))
        throw invalid_argument{"Failed to set decompress boxes."};

    if (JXL_DEC_SUCCESS != JxlDecoderSetUnpremultiplyAlpha(dec.get(), JXL_FALSE))
        throw invalid_argument{"Failed to set unpremultiply alpha."};

    JxlPixelFormat format;

    if (JXL_DEC_SUCCESS !=
        JxlDecoderSetInput(dec.get(), reinterpret_cast<const uint8_t *>(raw_data.data()), raw_data.size()))
        throw invalid_argument{"Failed to set input for decoder."};

    JxlDecoderCloseInput(dec.get());

    ImGuiTextFilter filter{opts.channel_selector.c_str()};
    filter.Build();

    std::vector<uint8_t> exif_buffer;
    std::vector<uint8_t> xmp_buffer;
    while (true)
    {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

        if (status == JXL_DEC_ERROR)
            throw invalid_argument{"Decoder error"};
        else if (status == JXL_DEC_BOX)
        {
            JxlBoxType type = {};
            if (JXL_DEC_SUCCESS != JxlDecoderGetBoxType(dec.get(), type, JXL_TRUE))
                throw invalid_argument{"Failed to get box type."};
            auto stype = string{type, type + sizeof(type)};
            spdlog::debug("Box type: '{}'", string{type, type + sizeof(type)});

            if (stype != "Exif" && stype != "xml ")
                continue;

            // read in entire box with a growing buffer
            //
            // we don't know the size of the box yet, so we need to allocate a buffer that can grow
            // Start with 1 KiB and double by a factor of 2 until we have enough space.
            vector<uint8_t> tmp_buffer(1024);
            size_t          prev_size  = tmp_buffer.size();
            size_t          output_pos = 0;

            if (JXL_DEC_SUCCESS != JxlDecoderSetBoxBuffer(dec.get(), tmp_buffer.data(), tmp_buffer.size()))
                throw invalid_argument{"Failed to set initial box buffer."};

            while ((status = JxlDecoderProcessInput(dec.get())) == JXL_DEC_BOX_NEED_MORE_OUTPUT)
            {
                size_t remaining = JxlDecoderReleaseBoxBuffer(dec.get());
                spdlog::info("Doubling box buffer size from {} to {} bytes; remaining: {}; output_pos: {}",
                             tmp_buffer.size(), tmp_buffer.size() * 2, remaining, output_pos);
                output_pos += prev_size - remaining;
                tmp_buffer.resize(tmp_buffer.size() * 2);
                prev_size = tmp_buffer.size() - output_pos;
                JxlDecoderSetBoxBuffer(dec.get(), tmp_buffer.data() + output_pos, prev_size);
            }

            if (status != JXL_DEC_BOX_COMPLETE)
                throw invalid_argument{"Failed to process box."};

            tmp_buffer.resize(tmp_buffer.size() - JxlDecoderReleaseBoxBuffer(dec.get()));

            if (stype == "Exif")
            {
                try
                {
                    if (tmp_buffer.size() < 4)
                        throw invalid_argument{"Invalid EXIF data: box size is smaller than 4 bytes."};

                    uint32_t offset = *(uint32_t *)tmp_buffer.data();
                    if (is_little_endian())
                        offset = swap_bytes(offset);

                    if (offset + 4 > tmp_buffer.size())
                        throw invalid_argument{"Invalid EXIF data: offset is larger than box size."};

                    spdlog::debug("EXIF data offset: {}", offset);
                    exif_buffer.assign(tmp_buffer.data() + 4 + offset, tmp_buffer.data() + tmp_buffer.size());
                }
                catch (const invalid_argument &e)
                {
                    spdlog::warn("Failed to parse exif data: {}", e.what());
                }
            }
            else if (stype == "xml ")
            {
                xmp_buffer = tmp_buffer;
                spdlog::info("XMP data size: {}", xmp_buffer.size());
            }
        }
        else if (status == JXL_DEC_NEED_MORE_INPUT)
            throw invalid_argument{"Decoder error, already provided all input"};
        else if (status == JXL_DEC_BASIC_INFO)
        {
            spdlog::debug("JXL_DEC_BASIC_INFO");
            if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info))
                throw invalid_argument{"JxlDecoderGetBasicInfo failed"};

            if (info.xsize * info.ysize * (info.num_color_channels + info.num_extra_channels) == 0)
                throw invalid_argument{
                    fmt::format("{}x{} image with {} color channels and {} extra channels has zero pixels", info.xsize,
                                info.ysize, info.num_color_channels, info.num_extra_channels)};

            size = int3{(int)info.xsize, (int)info.ysize, (int)info.num_color_channels + (info.alpha_bits ? 1 : 0)};

            spdlog::info("JPEG XL {}x{} image with {} color channels ({} including alpha) and {} extra channels",
                         size.x, size.y, info.num_color_channels, size.z, info.num_extra_channels);

            int count_alpha = 0, count_depth = 0, count_spot = 0, count_mask = 0, count_black = 0, count_cfa = 0,
                count_thermal = 0;
            extra_channel_infos.resize(info.num_extra_channels);
            extra_channel_names.resize(info.num_extra_channels);
            for (size_t i = 0; i < extra_channel_infos.size(); ++i)
            {
                auto &eci = extra_channel_infos[i];
                if (JXL_DEC_SUCCESS != JxlDecoderGetExtraChannelInfo(dec.get(), i, &eci))
                {
                    spdlog::error("JxlDecoderGetExtraChannelInfo failed");
                    continue;
                }

                // Check if there is a black channel (CMYK K channel) among the extra channels
                if (first_black_channel < 0 && eci.type == JXL_CHANNEL_BLACK)
                    first_black_channel = static_cast<int>(i);

                vector<char> name(eci.name_length + 1, 0);
                // first try to create the channel name from the name in the codestream
                if (eci.name_length &&
                    (JXL_DEC_SUCCESS == JxlDecoderGetExtraChannelName(dec.get(), i, name.data(), name.size())))
                    extra_channel_names[i] = string(name.data());
                else
                {
                    // otherwise, create a name based on the channel type
                    string type_name;
                    switch (eci.type)
                    {
                    case JXL_CHANNEL_ALPHA:
                        type_name = fmt::format("A{}", count_alpha ? " (" + to_string(count_alpha) + ")" : "");
                        count_alpha++;
                        break;
                    case JXL_CHANNEL_DEPTH:
                        type_name = fmt::format("depth{}", count_depth ? " (" + to_string(count_depth) + ")" : "");
                        count_depth++;
                        break;
                    case JXL_CHANNEL_SPOT_COLOR:
                        type_name = fmt::format("spot color{}", count_spot ? " (" + to_string(count_spot) + ")" : "");
                        count_spot++;
                        break;
                    case JXL_CHANNEL_SELECTION_MASK:
                        type_name = fmt::format("mask{}", count_mask ? " (" + to_string(count_mask) + ")" : "");
                        count_mask++;
                        break;
                    case JXL_CHANNEL_BLACK:
                        type_name = fmt::format("black{}", count_black ? " (" + to_string(count_black) + ")" : "");
                        count_black++;
                        break;
                    case JXL_CHANNEL_CFA:
                        type_name = fmt::format("CFA{}", count_cfa ? " (" + to_string(count_cfa) + ")" : "");
                        count_cfa++;
                        break;
                    case JXL_CHANNEL_THERMAL:
                        type_name =
                            fmt::format("thermal{}", count_thermal ? " (" + to_string(count_thermal) + ")" : "");
                        count_thermal++;
                        break;
                    case JXL_CHANNEL_RESERVED0: type_name = "reserved0"; break;
                    case JXL_CHANNEL_RESERVED1: type_name = "reserved1"; break;
                    case JXL_CHANNEL_RESERVED2: type_name = "reserved2"; break;
                    case JXL_CHANNEL_RESERVED3: type_name = "reserved3"; break;
                    case JXL_CHANNEL_RESERVED4: type_name = "reserved4"; break;
                    case JXL_CHANNEL_RESERVED5: type_name = "reserved5"; break;
                    case JXL_CHANNEL_RESERVED6: type_name = "reserved6"; break;
                    case JXL_CHANNEL_RESERVED7: type_name = "reserved7"; break;
                    case JXL_CHANNEL_UNKNOWN: type_name = "unknown"; break;
                    case JXL_CHANNEL_OPTIONAL: type_name = "optional"; break;
                    default: type_name = fmt::format("extra channel {}", i); break;
                    }
                    extra_channel_names[i] = type_name;
                }
                spdlog::info("Extra channel {}: '{}'", i, extra_channel_names[i]);
            }

            spdlog::info("Uses original profile: {}", info.uses_original_profile);

            JxlResizableParallelRunnerSetThreads(runner.get(),
                                                 JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
        }
        else if (status == JXL_DEC_COLOR_ENCODING)
        {
            spdlog::debug("JXL_DEC_COLOR_ENCODING");
            // Get the ICC color profile of the pixel data
            size_t icc_size;

            if (JXL_DEC_SUCCESS != JxlDecoderGetICCProfileSize(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA, &icc_size))
                throw invalid_argument{"JxlDecoderGetICCProfileSize failed"};

            icc_profile.resize(icc_size);
            if (JXL_DEC_SUCCESS != JxlDecoderGetColorAsICCProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                                                                  icc_profile.data(), icc_profile.size()))
                throw invalid_argument{"JxlDecoderGetColorAsICCProfile failed"};
            else
            {
                is_cmyk = icc::is_cmyk(icc_profile);
                spdlog::info("JPEG XL file has an {} ICC color profile", is_cmyk ? "CMYK" : "RGB");
            }

            if (JXL_DEC_SUCCESS ==
                JxlDecoderGetColorAsEncodedProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA, &file_enc))
            {
                has_encoded_profile = true;
                spdlog::info("JPEG XL file has an encoded color profile:\n{}", color_encoding_info(file_enc));
            }

            // only prefer the encoded profile if it exists and it specifies an HDR transfer function
            prefer_icc = !icc_profile.empty() &&
                         (!has_encoded_profile || (file_enc.transfer_function != JXL_TRANSFER_FUNCTION_PQ &&
                                                   file_enc.transfer_function != JXL_TRANSFER_FUNCTION_HLG));
            spdlog::info("Will {}prefer ICC profile for linearization.", prefer_icc ? "" : "not ");
        }
        else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
        {
            spdlog::debug("JXL_DEC_NEED_IMAGE_OUT_BUFFER");

            spdlog::info("size: {}x{}x{}", size.x, size.y, size.z);

            spdlog::info("JPEG XL file has {} color channels", size.z);
            format = {(uint32_t)size.z, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};

            image                          = make_shared<Image>(size.xy(), size.z);
            image->filename                = filename;
            image->partname                = frame_name;
            image->file_has_straight_alpha = info.alpha_bits && !info.alpha_premultiplied;
            image->metadata["loader"]      = "libjxl";
            image->metadata["pixel format"] =
                fmt::format("{}-bit ({} bpc)", size.z * info.bits_per_sample, info.bits_per_sample);
            image->metadata["header"]["intrinsic width"] = {
                {"value", int(info.intrinsic_xsize)}, {"string", to_string(info.intrinsic_xsize)}, {"type", "int"}};
            image->metadata["header"]["intrinsic height"] = {
                {"value", int(info.intrinsic_ysize)}, {"string", to_string(info.intrinsic_ysize)}, {"type", "int"}};
            image->metadata["header"]["has preview"] = {
                {"value", bool(info.have_preview)}, {"string", info.have_preview ? "true" : "false"}, {"type", "bool"}};
            image->metadata["header"]["intensity target"] = {
                {"value", info.intensity_target}, {"string", to_string(info.intensity_target)}, {"type", "float"}};
            image->metadata["header"]["has animation"] = {{"value", bool(info.have_animation)},
                                                          {"string", info.have_animation ? "true" : "false"},
                                                          {"type", "bool"}};
            image->metadata["header"]["has container"] = {{"value", bool(info.have_container)},
                                                          {"string", info.have_container ? "true" : "false"},
                                                          {"type", "bool"}};
            image->metadata["header"]["min nits"]      = {
                {"value", info.min_nits}, {"string", to_string(info.min_nits)}, {"type", "float"}};
            image->metadata["header"]["orientation"] = {
                {"value", info.orientation}, {"string", to_string(info.orientation)}, {"type", "enum"}};
            image->metadata["header"]["relative to max display"] = {{"value", info.relative_to_max_display},
                                                                    {"string", to_string(info.relative_to_max_display)},
                                                                    {"type", "int"}};
            image->metadata["header"]["linear below"]            = {
                {"value", info.linear_below}, {"string", to_string(info.linear_below)}, {"type", "float"}};

            skip_color = false;
            {
                auto name = (frame_name.empty()) ? string("R,G,B") : frame_name + "." + string("R,G,B");
                if ((skip_color = !filter.PassFilter(&name[0], &name[0] + name.size())))
                    spdlog::debug("Color channels '{}' filtered out by channel selector '{}'", name,
                                  opts.channel_selector);

                size_t buffer_size;
                if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size))
                    throw invalid_argument{"JxlDecoderImageOutBufferSize failed"};

                auto num_floats    = info.xsize * info.ysize * format.num_channels;
                auto expected_size = num_floats * sizeof(float);
                if (buffer_size != expected_size)
                    throw invalid_argument{
                        fmt::format("Invalid out buffer size {}. Expected {}", buffer_size, expected_size)};

                pixels.resize(num_floats);
                void *pixels_buffer = static_cast<void *>(pixels.data());
                if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec.get(), &format, pixels_buffer, expected_size))
                    throw invalid_argument{"JxlDecoderSetImageOutBuffer failed"};
            }

            for (uint32_t i = 0; i < info.num_extra_channels; ++i)
            {
                auto name = (frame_name.empty()) ? extra_channel_names[i] : frame_name + "." + extra_channel_names[i];
                if (!filter.PassFilter(&name[0], &name[0] + name.size()))
                {
                    spdlog::debug("Skipping extra channel {}: '{}' (filtered out by channel selector '{}')", i, name,
                                  opts.channel_selector);
                    continue;
                }

                // skip alpha channels, since they are handled as part of the color channels above
                if (extra_channel_infos[i].type == JXL_CHANNEL_ALPHA)
                    continue;

                spdlog::info("Adding extra channel buffer for channel {}: '{}'", i, extra_channel_names[i]);
                image->channels.emplace_back(extra_channel_names[i], size.xy());
                auto  &channel = image->channels.back();
                size_t buffer_size;
                if (JXL_DEC_SUCCESS != JxlDecoderExtraChannelBufferSize(dec.get(), &format, &buffer_size, i))
                {
                    spdlog::error("JxlDecoderExtraChannelBufferSize failed. Skipping extra channel {}", i);
                    continue;
                }

                auto num_floats    = info.xsize * info.ysize;
                auto expected_size = num_floats * sizeof(float);
                if (buffer_size != expected_size)
                {
                    spdlog::error("Invalid extra channel buffer size {}; expected {}. Skipping extra channel {}",
                                  buffer_size, expected_size, i);
                    continue;
                }
                if (JXL_DEC_SUCCESS !=
                    JxlDecoderSetExtraChannelBuffer(dec.get(), &format, channel.data(), buffer_size, i))
                {
                    spdlog::error("JxlDecoderSetExtraChannelBuffer failed. Skipping extra channel {}", i);
                    continue;
                }
                spdlog::info("read in extra channel {}", i);
            }
        }
        else if (status == JXL_DEC_FULL_IMAGE)
        {
            spdlog::debug("JXL_DEC_FULL_IMAGE");
            if (skip_color)
            {
                spdlog::debug("Skipping image, all channels filtered out by channel selector '{}'",
                              opts.channel_selector);
                continue;
            }

            string         tf_description;
            Chromaticities chr;

            // for premultiplied files, JPEGL-XL premultiplies by the non-linear alpha value, so we must unpremultiply
            // before applying the inverse transfer function, then premultiply again
            if (info.alpha_premultiplied)
            {
                int block_size = std::max(1, 1024 * 1024 / size.x);
                parallel_for(blocked_range<int>(0, size.y, block_size),
                             [&pixels, &size](int begin_y, int end_y, int, int)
                             {
                                 for (int y = begin_y; y < end_y; ++y)
                                 {
                                     for (int x = 0; x < size.x; ++x)
                                     {
                                         const size_t scanline = (x + y * size.x) * size.z;
                                         const float  alpha    = pixels[scanline + size.z - 1];
                                         const float  factor   = alpha == 0.0f ? 1.0f : 1.0f / alpha;
                                         for (int c = 0; c < size.z - 1; ++c) pixels[scanline + c] *= factor;
                                     }
                                 }
                             });
            }

            vector<float> alpha_copy;

            // If a black channel exists and there is an alpha channel in the interleaved array, swap alpha and black
            spdlog::debug("prefer_icc: {}, is_cmyk: {}, first_black_channel: {}, size.z: {}", prefer_icc, is_cmyk,
                          first_black_channel, size.z);

            // This black/alpha copying nonsense is needed because libjxl's API makes it cumbersome to support
            // CMYK files at the moment: libjxl only supports a maximum of 3 color channels + alpha when
            // decoding. All other channels are treated as extra. The black (K) channel in a CMYK file is
            // hence an extra channel, and not decoded as part of the <=4 color channels.
            //
            // We therefore swap the black channel for the alpha channel in the pixel array before applying the ICC
            // profile, and then swap them back afterwards.
            if (opts.tf_override.type == TransferFunction::Unspecified && prefer_icc && is_cmyk &&
                first_black_channel >= 0 && size.z > 1)
            {
                size_t alpha_channel_idx = size.z - 1;
                float *black_data        = image->channels[size.z + first_black_channel].data();
                // Allocate and copy the alpha channel into alpha_copy
                alpha_copy.resize(size.x * size.y);
                for (int y = 0; y < size.y; ++y)
                {
                    for (int x = 0; x < size.x; ++x)
                    {
                        size_t idx            = (x + y * size.x) * size.z + alpha_channel_idx;
                        size_t black_idx      = x + y * size.x;
                        alpha_copy[black_idx] = pixels[idx];
                        pixels[idx]           = black_data[black_idx];
                    }
                }
                spdlog::info("Swapped alpha channel in interleaved array with black channel data.");
            }

            if (!icc_profile.empty())
                image->icc_data = icc_profile;

            if (opts.tf_override.type == TransferFunction::Unspecified)
            {
                if ((prefer_icc && icc::linearize_colors(pixels.data(), size, icc_profile, &tf_description, &chr)) ||
                    linearize_colors(pixels.data(), size, file_enc, &tf_description, &chr))
                {
                    image->chromaticities                = chr;
                    image->metadata["transfer function"] = tf_description;
                }
                else
                    image->metadata["transfer function"] = transfer_function_name(TransferFunction::Unspecified);
            }
            else
            {
                spdlog::info("Ignoring embedded color profile and linearizing using requested transfer function: {}",
                             transfer_function_name(opts.tf_override));
                try
                {
                    // some CICP transfer functions always correspond to certain primaries, try to deduce that
                    image->chromaticities = chromaticities_from_cicp(transfer_function_to_cicp(opts.tf_override.type));
                }
                catch (...)
                {
                    spdlog::warn("Failed to infer chromaticities from transfer function CICP value: {}",
                                 int(opts.tf_override.type));
                }
                // use the transfer function specified by the user
                to_linear(pixels.data(), size, opts.tf_override);
                image->metadata["transfer function"] = transfer_function_name(opts.tf_override);
            }

            if (opts.tf_override.type == TransferFunction::Unspecified && prefer_icc && is_cmyk &&
                first_black_channel >= 0 && size.z > 1)
            {
                size_t alpha_channel_idx = size.z - 1;
                // Copy from alpha_copy back into the alpha channel
                for (int y = 0; y < size.y; ++y)
                {
                    for (int x = 0; x < size.x; ++x)
                    {
                        size_t idx       = (x + y * size.x) * size.z + alpha_channel_idx;
                        size_t black_idx = x + y * size.x;
                        pixels[idx]      = alpha_copy[black_idx];
                    }
                }
                spdlog::info("Swapped alpha channel in interleaved array back with black channel data.");
            }

            // premultiply again
            if (info.alpha_premultiplied)
            {
                int block_size = std::max(1, 1024 * 1024 / size.x);
                parallel_for(blocked_range<int>(0, size.y, block_size),
                             [&pixels, &size](int begin_y, int end_y, int, int)
                             {
                                 for (int y = begin_y; y < end_y; ++y)
                                 {
                                     for (int x = 0; x < size.x; ++x)
                                     {
                                         const size_t scanline = (x + y * size.x) * size.z;
                                         const float  alpha    = pixels[scanline + size.z - 1];
                                         const float  factor   = alpha == 0.0f ? 1.0f : alpha;
                                         for (int c = 0; c < size.z - 1; ++c) pixels[scanline + c] *= factor;
                                     }
                                 }
                             });
            }

            // copy the interleaved float pixels into the channels
            for (int c = 0; c < size.z; ++c)
                image->channels[c].copy_from_interleaved(pixels.data(), size.x, size.y, size.z, c,
                                                         [](float v) { return v; });

            // apply transfer function to extra channels
            for (size_t i = size.z; i < image->channels.size(); ++i)
            {
                auto &channel      = image->channels[i];
                auto &channel_info = extra_channel_infos[i - size.z];

                // alpha channels don't have transfer function applied, so skip them
                // also skip the black channel if we already included it as a CMYK black channel
                if (channel_info.type == JXL_CHANNEL_ALPHA)
                    continue;

                spdlog::info("Applying transfer function to extra channel '{}'", channel.name);

                if (opts.tf_override.type != TransferFunction::Unspecified)
                {
                    if ((prefer_icc && icc::linearize_colors(channel.data(), int3{size.xy(), 1}, icc_profile)) ||
                        linearize_colors(channel.data(), int3{size.xy(), 1}, file_enc))
                    {
                        //
                    }
                }
                else
                    // use the transfer function specified by the user
                    to_linear(channel.data(), int3{size.xy(), 1}, opts.tf_override);
            }

            images.push_back(image);
        }
        else if (status == JXL_DEC_FRAME)
        {
            spdlog::debug("JXL_DEC_FRAME");

            JxlFrameHeader frame_header;
            if (JXL_DEC_SUCCESS != JxlDecoderGetFrameHeader(dec.get(), &frame_header))
                spdlog::error("JxlDecoderGetFrameHeader failed. Trying to continue...");

            std::vector<char> name_buffer(frame_header.name_length + 1);
            if (frame_header.name_length &&
                (JXL_DEC_SUCCESS == JxlDecoderGetFrameName(dec.get(), name_buffer.data(), name_buffer.size())))
            {
                frame_name = name_buffer.data();
                spdlog::info("JPEG XL frame name: {}", name_buffer.data());
            }
            else if (info.have_animation)
                frame_name = fmt::format("frame {:04}", frame_number);
            else
                frame_name = "";

            frame_number++;
        }
        else if (status == JXL_DEC_SUCCESS)
        {
            // All decoding successfully finished.
            // It's not required to call JxlDecoderReleaseInput(dec.get()) here since
            // the decoder will be destroyed.
            break;
        }
        else
            throw invalid_argument{"Unknown decoder status"};
    }

    spdlog::debug("Saving EXIF and XMP metadata to image");
    if (!exif_buffer.empty())
        try
        {
            auto j = exif_to_json(exif_buffer);
            spdlog::debug("JPEG-XL: EXIF metadata successfully parsed: {}", j.dump(2));

            // assign exif metadata to all images
            for (auto &&image : images)
            {
                image->metadata["exif"] = j;
                image->exif_data        = exif_buffer;
            }
        }
        catch (const std::exception &e)
        {
            spdlog::warn("JPEG-XL: Exception while parsing EXIF chunk: {}", e.what());
        }
    if (!xmp_buffer.empty())
    {
        auto xmp = string(xmp_buffer.data(), xmp_buffer.data() + xmp_buffer.size());
        spdlog::debug("XMP: {}", xmp);
        // assign xmp metadata to all images
        for (auto &&image : images)
        {
            image->xmp_data                  = xmp_buffer;
            image->metadata["header"]["XMP"] = {
                {"value", xmp}, {"string", xmp}, {"type", "string"}, {"documentation", "XMP metadata"}};
        }
    }

    return images;
}

void save_jxl_image(const Image &img, std::ostream &os, std::string_view filename, float gain, bool lossless,
                    float quality, TransferFunction tf, int data_type)
{
    Timer timer;

    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);

    JxlTransferFunction jtf = jxl_tf(tf);

    if (jtf == JXL_TRANSFER_FUNCTION_UNKNOWN)
        throw std::runtime_error("JPEG XL: unsupported transfer function");

    if (!(data_type == JXL_TYPE_FLOAT || data_type == JXL_TYPE_UINT8 || data_type == JXL_TYPE_UINT16 ||
          data_type == JXL_TYPE_FLOAT16))
        throw std::runtime_error("JPEG XL: unsupported data type");

    int                         w = 0, h = 0, n = 0;
    std::unique_ptr<uint8_t[]>  pixels_u8;
    std::unique_ptr<uint16_t[]> pixels_u16;
    std::unique_ptr<half[]>     pixels_f16;
    std::unique_ptr<float[]>    pixels_f32;
    const void                 *pixels = nullptr;

    size_t pixel_bytes;

    JxlPixelFormat format;
    format.endianness = JXL_NATIVE_ENDIAN;
    format.align      = 0;

    format.data_type = (JxlDataType)data_type;
    switch (data_type)
    {
    case JXL_TYPE_UINT8:
        info.bits_per_sample          = 8;
        info.exponent_bits_per_sample = 0;
        pixels_u8                     = img.as_interleaved<uint8_t>(&w, &h, &n, gain, tf, true, true, false);
        pixels                        = pixels_u8.get();
        pixel_bytes                   = sizeof(uint8_t) * w * h * n;
        break;
    case JXL_TYPE_UINT16:
        info.bits_per_sample          = 16;
        info.exponent_bits_per_sample = 0;
        pixels_u16                    = img.as_interleaved<uint16_t>(&w, &h, &n, gain, tf, true, true, false);
        pixels                        = pixels_u16.get();
        pixel_bytes                   = sizeof(uint16_t) * w * h * n;
        break;
    case JXL_TYPE_FLOAT16:
        info.bits_per_sample          = 16;
        info.exponent_bits_per_sample = 5;
        pixels_f16                    = img.as_interleaved<half>(&w, &h, &n, gain, tf, false, true, false);
        pixels                        = pixels_f16.get();
        pixel_bytes                   = sizeof(half) * w * h * n;
        break;
    case JXL_TYPE_FLOAT:
        info.bits_per_sample          = 32;
        info.exponent_bits_per_sample = 8;
        pixels_f32                    = img.as_interleaved<float>(&w, &h, &n, gain, tf, false, true, false);
        pixels                        = pixels_f32.get();
        pixel_bytes                   = sizeof(float) * w * h * n;
        break;
    }
    format.num_channels = n;

    if (!pixels || w <= 0 || h <= 0)
        throw std::runtime_error("JPEG XL: empty image or invalid image dimensions");

    info.xsize                 = w;
    info.ysize                 = h;
    info.num_color_channels    = n == 1 ? 1 : 3;
    info.num_extra_channels    = (n == 2 || n == 4) ? 1 : 0;
    info.alpha_bits            = (n == 2 || n == 4) ? info.bits_per_sample : 0;
    info.alpha_exponent_bits   = (n == 2 || n == 4) ? info.exponent_bits_per_sample : 0;
    info.uses_original_profile = 0;

    JxlEncoderPtr enc    = JxlEncoderMake(nullptr);
    auto          runner = JxlResizableParallelRunnerMake(nullptr);
    if (JXL_ENC_SUCCESS != JxlEncoderSetParallelRunner(enc.get(), JxlResizableParallelRunner, runner.get()))
        throw std::runtime_error("JxlEncoderSetParallelRunner failed");

    if (JXL_ENC_SUCCESS != JxlEncoderSetBasicInfo(enc.get(), &info))
        throw std::runtime_error("JxlEncoderSetBasicInfo failed");

    // Set color encoding
    JxlColorEncoding color_encoding;
    memset(&color_encoding, 0, sizeof(color_encoding));
    color_encoding.color_space       = JXL_COLOR_SPACE_RGB;
    color_encoding.transfer_function = jtf;
    color_encoding.gamma             = tf.gamma;
    color_encoding.rendering_intent  = JXL_RENDERING_INTENT_RELATIVE;

    Chromaticities c{}; // sRGB/Rec709, D65
    if (!img.chromaticities || approx_equal(*img.chromaticities, c))
    {
        color_encoding.white_point = JXL_WHITE_POINT_D65;
        color_encoding.primaries   = JXL_PRIMARIES_SRGB;
    }
    else
    {
        color_encoding.white_point = JXL_WHITE_POINT_CUSTOM;
        color_encoding.primaries   = JXL_PRIMARIES_CUSTOM;
        c                          = *img.chromaticities;
    }

    color_encoding.white_point_xy[0]     = c.white.x;
    color_encoding.white_point_xy[1]     = c.white.y;
    color_encoding.primaries_red_xy[0]   = c.red.x;
    color_encoding.primaries_red_xy[1]   = c.red.y;
    color_encoding.primaries_green_xy[0] = c.green.x;
    color_encoding.primaries_green_xy[1] = c.green.y;
    color_encoding.primaries_blue_xy[0]  = c.blue.x;
    color_encoding.primaries_blue_xy[1]  = c.blue.y;

    if (JXL_ENC_SUCCESS != JxlEncoderSetColorEncoding(enc.get(), &color_encoding))
        throw std::runtime_error("JxlEncoderSetColorEncoding failed");

    JxlEncoderFrameSettings *frame_settings = JxlEncoderFrameSettingsCreate(enc.get(), nullptr);
    if (!frame_settings)
        throw std::runtime_error("JxlEncoderFrameSettingsCreate failed");

    float distance = JxlEncoderDistanceFromQuality(quality);
    if (JXL_ENC_SUCCESS != JxlEncoderSetFrameDistance(frame_settings, distance))
        throw std::runtime_error("JxlEncoderSetFrameDistance failed");

    // if (JXL_ENC_SUCCESS != JxlEncoderSetCodestreamLevel(enc.get(), 10))
    //     throw std::runtime_error("JxlEncoderSetCodestreamLevel failed");
    if (JXL_ENC_SUCCESS != JxlEncoderSetFrameLossless(frame_settings, lossless ? JXL_TRUE : JXL_FALSE))
        throw std::runtime_error("JxlEncoderSetFrameLossless failed");

    if (JXL_ENC_SUCCESS != JxlEncoderAddImageFrame(frame_settings, &format, pixels, pixel_bytes))
    {
        throw std::runtime_error(fmt::format("JxlEncoderAddImageFrame failed: {}", (int)JxlEncoderGetError(enc.get())));
    }

    JxlEncoderCloseInput(enc.get());

    std::vector<uint8_t> outbuf(1024 * 1024); // Start with 1MB
    uint8_t             *next_out  = outbuf.data();
    size_t               avail_out = outbuf.size();
    while (true)
    {
        JxlEncoderStatus status = JxlEncoderProcessOutput(enc.get(), &next_out, &avail_out);
        if (status == JXL_ENC_ERROR)
            throw std::runtime_error("JxlEncoderProcessOutput failed");
        if (status == JXL_ENC_SUCCESS)
            break;
        size_t used = next_out - outbuf.data();
        outbuf.resize(outbuf.size() * 2);
        next_out  = outbuf.data() + used;
        avail_out = outbuf.size() - used;
    }
    size_t out_size = next_out - outbuf.data();
    os.write(reinterpret_cast<char *>(outbuf.data()), out_size);

    spdlog::info("Saved JPEG XL image to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

static int s_data_types[] = {JXL_TYPE_FLOAT, JXL_TYPE_FLOAT16, JXL_TYPE_UINT8, JXL_TYPE_UINT16};

JXLSaveOptions *jxl_parameters_gui()
{
    if (ImGui::PE::Begin("JPEG-XL Save Options", ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthStretch);

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

        ImGui::PE::Entry(
            "Transfer function",
            [&]
            {
                if (ImGui::BeginCombo("##Transfer function", transfer_function_name(s_opts.tf).c_str()))
                {
                    for (int i = TransferFunction::Linear; i <= TransferFunction::DCI_P3; ++i)
                    {
                        bool is_selected = (s_opts.tf.type == (TransferFunction::Type_)i);
                        if (ImGui::Selectable(
                                transfer_function_name({(TransferFunction::Type_)i, s_opts.tf.gamma}).c_str(),
                                is_selected))
                            s_opts.tf.type = (TransferFunction::Type_)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                return true;
            },
            "Encode the pixel values using this transfer function.");

        if (s_opts.tf.type == TransferFunction::Gamma)
            ImGui::PE::SliderFloat("Gamma", &s_opts.tf.gamma, 0.1f, 5.f, "%.3f", 0,
                                   "When using a gamma transfer function, this is the gamma value to use.");

        ImGui::PE::Combo("Pixel format", &s_opts.data_type_index, "Float32\0Float16\0UInt8\0UInt16\0");

        ImGui::PE::Checkbox(
            "Lossless", &s_opts.lossless,
            "If enabled, the image will be saved using lossless compression. Quality setting will be ignored.");

        ImGui::BeginDisabled(s_opts.lossless);
        ImGui::PE::SliderInt("Quality", &s_opts.quality, 1, 100, "%d", 0, "Quality level for lossy compression.");
        ImGui::EndDisabled();

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = JXLSaveOptions{};

    return &s_opts;
}

// throws on error
void save_jxl_image(const Image &img, std::ostream &os, std::string_view filename, JXLSaveOptions *params)
{
    if (params == nullptr)
        throw std::invalid_argument("JXLSaveOptions pointer is null");

    save_jxl_image(img, os, filename, params->gain, params->lossless, params->quality, params->tf,
                   s_data_types[params->data_type_index]);
}

#endif
