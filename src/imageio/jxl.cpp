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
#include <type_traits>

using namespace std;

#ifndef HDRVIEW_ENABLE_JPEGXL

bool is_jxl_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_jxl_image(istream &is, const string &filename)
{
    throw runtime_error("JPEG-XL support not enabled in this build.");
}

#else

#include <ImfHeader.h>
#include <ImfStandardAttributes.h>
#include <jxl/cms.h>
#include <jxl/cms_interface.h>
#include <jxl/codestream_header.h>
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>
#include <jxl/types.h>

#include "cms.h"
#include "colorspace.h"
#include "timer.h"

static string color_encoding_info(const JxlColorEncoding &enc)
{
    string out;
    out += format_indented(4, "White point xy: {} {}\n", enc.white_point_xy[0], enc.white_point_xy[1]);
    out += format_indented(4, "Red primary xy: {} {}\n", enc.primaries_red_xy[0], enc.primaries_red_xy[1]);
    out += format_indented(4, "Green primary xy: {} {}\n", enc.primaries_green_xy[0], enc.primaries_green_xy[1]);
    out += format_indented(4, "Blue primary xy: {} {}\n", enc.primaries_blue_xy[0], enc.primaries_blue_xy[1]);

    // Print out the name of the transfer function in human readable form
    switch (enc.transfer_function)
    {
    case JXL_TRANSFER_FUNCTION_709: out += format_indented(4, "Transfer function: 709\n"); break;
    case JXL_TRANSFER_FUNCTION_SRGB: out += format_indented(4, "Transfer function: sRGB\n"); break;
    case JXL_TRANSFER_FUNCTION_GAMMA: out += format_indented(4, "Transfer function: gamma: {}\n", enc.gamma); break;
    case JXL_TRANSFER_FUNCTION_LINEAR: out += format_indented(4, "Transfer function: linear\n"); break;
    case JXL_TRANSFER_FUNCTION_PQ: out += format_indented(4, "Transfer function: PQ\n"); break;
    case JXL_TRANSFER_FUNCTION_HLG: out += format_indented(4, "Transfer function: HLG\n"); break;
    case JXL_TRANSFER_FUNCTION_DCI: out += format_indented(4, "Transfer function: DCI\n"); break;
    case JXL_TRANSFER_FUNCTION_UNKNOWN:
    default: out += format_indented(4, "Transfer function: unknown\n"); break;
    }

    // print out the rendering intent in human readible form
    switch (enc.rendering_intent)
    {
    case JXL_RENDERING_INTENT_PERCEPTUAL: out += format_indented(4, "Rendering intent: perceptual\n"); break;
    case JXL_RENDERING_INTENT_RELATIVE: out += format_indented(4, "Rendering intent: relative\n"); break;
    case JXL_RENDERING_INTENT_SATURATION: out += format_indented(4, "Rendering intent: saturation\n"); break;
    case JXL_RENDERING_INTENT_ABSOLUTE: out += format_indented(4, "Rendering intent: absolute\n"); break;
    default: out += format_indented(4, "Rendering intent: unknown\n"); break;
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

static bool linearize_colors(vector<float> &pixels, int3 size, JxlColorEncoding file_enc, string &tf_description,
                             Imf::Chromaticities *chromaticities = nullptr)
{
    Timer timer;
    spdlog::info("Linearizing pixel values using encoded profile.");
    if (chromaticities)
    {
        chromaticities->red   = Imath::V2f(file_enc.primaries_red_xy[0], file_enc.primaries_red_xy[1]);
        chromaticities->green = Imath::V2f(file_enc.primaries_green_xy[0], file_enc.primaries_green_xy[1]);
        chromaticities->blue  = Imath::V2f(file_enc.primaries_blue_xy[0], file_enc.primaries_blue_xy[1]);
        chromaticities->white = Imath::V2f(file_enc.white_point_xy[0], file_enc.white_point_xy[1]);
    }

    float            gamma = file_enc.gamma;
    TransferFunction tf;
    switch (file_enc.transfer_function)
    {
    case JXL_TRANSFER_FUNCTION_709:
        tf_description = rec709_2020_tf;
        tf             = TransferFunction_Rec709_2020;
        break;
    case JXL_TRANSFER_FUNCTION_PQ:
        tf_description = pq_tf;
        tf             = TransferFunction_Rec2100_PQ;
        break;
    case JXL_TRANSFER_FUNCTION_HLG:
        tf_description = hlg_tf;
        tf             = TransferFunction_Rec2100_HLG;
        break;
    case JXL_TRANSFER_FUNCTION_LINEAR:
        tf_description = linear_tf;
        tf             = TransferFunction_Linear;
        break;
    case JXL_TRANSFER_FUNCTION_GAMMA:
        tf_description = fmt::format("{} ({})", gamma_tf, gamma);
        tf             = TransferFunction_Gamma;
        break;
    case JXL_TRANSFER_FUNCTION_SRGB: [[fallthrough]];
    default: tf_description = srgb_tf; tf = TransferFunction_sRGB;
    }

    if (tf == TransferFunction_Rec2100_HLG && size.z == 3)
    {
        // HLG needs to operate on all three channels at once
        parallel_for(blocked_range<int>(0, size.x * size.y, 1024 * 1024),
                     [&pixels](int start, int end, int, int)
                     {
                         auto rgb_pixels = reinterpret_cast<float3 *>(pixels.data() + start * 3);
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
    spdlog::debug("Linearizing pixel data using encoded profile took {} seconds", (timer.elapsed() / 1000.f));
    return true;
}

static bool linearize_colors(vector<float> &pixels, int3 size, const vector<uint8_t> &icc_profile,
                             string &tf_description, Imf::Chromaticities *chromaticities = nullptr)
{
    if (icc_profile.empty())
        return false;

#ifndef HDRVIEW_ENABLE_LCMS2
    return false;
#else
    Timer timer;
    spdlog::info("Linearizing pixel values using image's ICC color profile.");

    auto            profile_in = cms::open_profile_from_mem(icc_profile);
    cmsCIExyY       whitepoint;
    cmsCIExyYTRIPLE primaries;
    cms::extract_chromaticities(profile_in, primaries, whitepoint);
    if (chromaticities)
    {
        // print out the chromaticities
        spdlog::info("Applying chromaticities deduced from image's ICC profile:");
        spdlog::info("    Red primary: ({}, {})", primaries.Red.x, primaries.Red.y);
        spdlog::info("    Green primary: ({}, {})", primaries.Green.x, primaries.Green.y);
        spdlog::info("    Blue primary: ({}, {})", primaries.Blue.x, primaries.Blue.y);
        spdlog::info("    White point: ({}, {})", whitepoint.x, whitepoint.y);
        chromaticities->red   = Imath::V2f(primaries.Red.x, primaries.Red.y);
        chromaticities->green = Imath::V2f(primaries.Green.x, primaries.Green.y);
        chromaticities->blue  = Imath::V2f(primaries.Blue.x, primaries.Blue.y);
        chromaticities->white = Imath::V2f(whitepoint.x, whitepoint.y);
    }

    auto            profile_out = cms::create_linear_RGB_profile(whitepoint, primaries);
    cmsUInt32Number format      = TYPE_GRAY_FLT;
    if (size.z == 3)
        format = TYPE_RGB_FLT;
    else if (size.z == 4)
        format = TYPE_RGBA_FLT;
    auto xform = cms::Transform{
        cmsCreateTransform(profile_in.get(), format, profile_out.get(), format, INTENT_ABSOLUTE_COLORIMETRIC,
                           (size.z == 4 ? cmsFLAGS_COPY_ALPHA : 0) | cmsFLAGS_HIGHRESPRECALC | cmsFLAGS_NOCACHE)};
    if (!xform)
    {
        spdlog::error("Could not create ICC color transform.");
        return false;
    }
    else
    {
        auto desc = cms::profile_description(profile_in);
        spdlog::info("ICC profile description: '{}'", desc);

        parallel_for(blocked_range<int>(0, size.x * size.y, 1024 * 1024),
                     [xf = xform.get(), &pixels, size](int start, int end, int unit_index, int thread_index)
                     {
                         auto data_p = pixels.data() + start * size.z;
                         cmsDoTransform(xf, data_p, data_p, (end - start));
                     });
        tf_description = fmt::format("ICC profile{}", desc.empty() ? "" : " (" + desc + ")");
    }
    spdlog::debug("Linearizing pixel data using image's ICC color profile took {} seconds", (timer.elapsed() / 1000.f));
    return true;
#endif // HDRVIEW_ENABLE_LCMS2
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

vector<ImagePtr> load_jxl_image(istream &is, const string &filename)
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
            fmt::format("Failed to read : {} bytes, read : {} bytes", raw_size, (size_t)is.gcount())};

    std::vector<float>   pixels;
    JxlColorEncoding     file_enc;
    JxlColorEncoding     target_enc;
    JxlBasicInfo         info;
    std::vector<uint8_t> icc_profile, target_profile;
    bool                 has_encoded_profile = false;
    std::vector<string>  extra_channel_names;
    int                  first_alpha = -1;
    int3                 size{0, 0, 0};
    string               frame_name;
    int                  frame_number = 0;

    // CmsData m_cms;

    vector<ImagePtr> images;
    ImagePtr         image;
    {
        // Multi-threaded parallel runner.
        auto runner = JxlResizableParallelRunnerMake(nullptr);
        auto dec    = JxlDecoderMake(nullptr);
        if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING |
                                                                        JXL_DEC_FULL_IMAGE | JXL_DEC_FRAME))
            throw invalid_argument{"JxlDecoderSubscribeEvents failed"};

        if (JXL_DEC_SUCCESS != JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner, runner.get()))
            throw invalid_argument{"JxlDecoderSetParallelRunner failed"};

        JxlPixelFormat format;

        JxlDecoderSetInput(dec.get(), reinterpret_cast<const uint8_t *>(raw_data.data()), raw_data.size());
        JxlDecoderCloseInput(dec.get());

        for (;;)
        {
            JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

            if (status == JXL_DEC_ERROR)
                throw invalid_argument{"JPEG XL decoder error"};
            else if (status == JXL_DEC_NEED_MORE_INPUT)
                throw invalid_argument{"JPEG XL decoder error, already provided all input"};
            else if (status == JXL_DEC_BASIC_INFO)
            {
                spdlog::warn("JXL_DEC_BASIC_INFO");
                if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info))
                    throw invalid_argument{"JxlDecoderGetBasicInfo failed"};

                spdlog::info("JPEG XL {}x{} image with {} color channels and {} extra channels", info.xsize, info.ysize,
                             info.num_color_channels, info.num_extra_channels);

                size = int3{(int)info.xsize, (int)info.ysize, (int)info.num_color_channels};

                spdlog::info("size: {}x{}x{}", size.x, size.y, size.z);

                if (info.xsize * info.ysize * (info.num_color_channels + info.num_extra_channels) == 0)
                    throw invalid_argument{
                        fmt::format("{}x{} image with {} color channels and {} extra channels has zero pixels",
                                    info.xsize, info.ysize, info.num_color_channels, info.num_extra_channels)};

                int count_alpha = 0, count_depth = 0, count_spot = 0, count_mask = 0, count_black = 0, count_cfa = 0,
                    count_thermal = 0;
                extra_channel_names.resize(info.num_extra_channels);
                for (uint32_t i = 0; i < info.num_extra_channels; ++i)
                {
                    JxlExtraChannelInfo eci;
                    if (JXL_DEC_SUCCESS != JxlDecoderGetExtraChannelInfo(dec.get(), i, &eci))
                    {
                        spdlog::error("JxlDecoderGetExtraChannelInfo failed");
                        continue;
                    }

                    // save the index of the first encountered alpha channel
                    if (eci.type == JXL_CHANNEL_ALPHA && first_alpha < 0)
                        first_alpha = i;

                    string       channel_name;
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
                            type_name =
                                fmt::format("spot color{}", count_spot ? " (" + to_string(count_spot) + ")" : "");
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
                spdlog::warn("JXL_DEC_COLOR_ENCODING");
                // Get the ICC color profile of the pixel data
                size_t icc_size;

                if (JXL_DEC_SUCCESS !=
                    JxlDecoderGetICCProfileSize(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, &icc_size))
                    throw invalid_argument{"JxlDecoderGetICCProfileSize failed"};

                icc_profile.resize(icc_size);
                if (JXL_DEC_SUCCESS != JxlDecoderGetColorAsICCProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL,
                                                                      icc_profile.data(), icc_profile.size()))
                    throw invalid_argument{"JxlDecoderGetColorAsICCProfile failed"};
                else
                    spdlog::info("JPEG XL file has an ICC color profile");

                if (JXL_DEC_SUCCESS != JxlDecoderGetICCProfileSize(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA, &icc_size))
                    throw invalid_argument{"JxlDecoderGetICCProfileSize failed"};

                target_profile.resize(icc_size);
                if (JXL_DEC_SUCCESS != JxlDecoderGetColorAsICCProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                                                                      target_profile.data(), target_profile.size()))
                    throw invalid_argument{"JxlDecoderGetColorAsICCProfile failed"};

                if (JXL_DEC_SUCCESS ==
                    JxlDecoderGetColorAsEncodedProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, &file_enc))
                {
                    has_encoded_profile = true;
                    spdlog::info("JPEG XL file has an encoded color profile:\n{}", color_encoding_info(file_enc));
                }
                else
                    spdlog::warn("JPEG XL file has no encoded color profile. Colors distortions may occur.");

                if (JXL_DEC_SUCCESS ==
                    JxlDecoderGetColorAsEncodedProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA, &target_enc))
                {
                    spdlog::info("libJXL understands that we set a target color encoding:\n{}",
                                 color_encoding_info(target_enc));
                }
                else
                    spdlog::warn("libJXL does NOT understand that we set a target color encoding");
            }
            else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
            {
                spdlog::warn("JXL_DEC_NEED_IMAGE_OUT_BUFFER");

                spdlog::info("size: {}x{}x{}", size.x, size.y, size.z);

                uint32_t num_channels = info.num_color_channels; // + (info.alpha_bits ? 1 : 0);
                format                = {num_channels, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};

                size_t buffer_size;
                if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size))
                    throw invalid_argument{"JxlDecoderImageOutBufferSize failed"};

                spdlog::info("after1");
                auto num_floats    = info.xsize * info.ysize * format.num_channels;
                auto expected_size = num_floats * sizeof(float);
                if (buffer_size != expected_size)
                    throw invalid_argument{
                        fmt::format("Invalid out buffer size {}. Expected {}", buffer_size, expected_size)};

                spdlog::info("after2");
                pixels.resize(num_floats);
                void *pixels_buffer = static_cast<void *>(pixels.data());
                if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec.get(), &format, pixels_buffer, expected_size))
                    throw invalid_argument{"JxlDecoderSetImageOutBuffer failed"};

                image                          = make_shared<Image>(size.xy(), size.z);
                image->filename                = filename;
                image->partname                = frame_name;
                image->file_has_straight_alpha = info.alpha_bits && !info.alpha_premultiplied;
                image->metadata["loader"]      = "libjxl";
                image->metadata["bit depth"]   = fmt::format("{} bits per sample", info.bits_per_sample);

                spdlog::info("after3");
                for (uint32_t i = 0; i < info.num_extra_channels; ++i)
                {
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
                spdlog::warn("JXL_DEC_FULL_IMAGE");

                // only prefer the encoded profile if it exists and it specifies an HDR transfer function
                bool prefer_icc = !has_encoded_profile || (file_enc.transfer_function != JXL_TRANSFER_FUNCTION_PQ &&
                                                           file_enc.transfer_function != JXL_TRANSFER_FUNCTION_HLG);

                string              tf_description;
                Imf::Chromaticities chromaticities;
                if ((prefer_icc && linearize_colors(pixels, size, icc_profile, tf_description, &chromaticities)) ||
                    linearize_colors(pixels, size, file_enc, tf_description, &chromaticities))
                {
                    Imf::addChromaticities(image->header, chromaticities);
                    image->metadata["transfer function"] = tf_description;
                }
                else
                    image->metadata["transfer function"] = "unknown";

                Timer timer;
                for (int c = 0; c < size.z; ++c)
                    image->channels[c].copy_from_interleaved(pixels.data(), size.x, size.y, size.z, c,
                                                             [](float v) { return v; });
                spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));

                images.push_back(image);
            }
            else if (status == JXL_DEC_FRAME)
            {
                // Nothing to do
                spdlog::warn("JXL_DEC_FRAME");

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
    }

    return images;
}

#endif
