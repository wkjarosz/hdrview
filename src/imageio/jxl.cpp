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

static void print_color_encoding_info(const JxlColorEncoding &enc)
{
    spdlog::info("White point xy: {} {}", enc.white_point_xy[0], enc.white_point_xy[1]);
    spdlog::info("Red primary xy: {} {}", enc.primaries_red_xy[0], enc.primaries_red_xy[1]);
    spdlog::info("Green primary xy: {} {}", enc.primaries_green_xy[0], enc.primaries_green_xy[1]);
    spdlog::info("Blue primary xy: {} {}", enc.primaries_blue_xy[0], enc.primaries_blue_xy[1]);

    // Print out the name of the transfer function in human readable form
    switch (enc.transfer_function)
    {
    case JXL_TRANSFER_FUNCTION_709: spdlog::info("Transfer function: 709"); break;
    case JXL_TRANSFER_FUNCTION_SRGB: spdlog::info("Transfer function: sRGB"); break;
    case JXL_TRANSFER_FUNCTION_GAMMA: spdlog::info("Transfer function: gamma: {}", enc.gamma); break;
    case JXL_TRANSFER_FUNCTION_LINEAR: spdlog::info("Transfer function: linear"); break;
    case JXL_TRANSFER_FUNCTION_PQ: spdlog::info("Transfer function: PQ"); break;
    case JXL_TRANSFER_FUNCTION_HLG: spdlog::info("Transfer function: HLG"); break;
    case JXL_TRANSFER_FUNCTION_DCI: spdlog::info("Transfer function: DCI"); break;
    case JXL_TRANSFER_FUNCTION_UNKNOWN:
    default: spdlog::info("Transfer function: unknown"); break;
    }

    // print out the rendering intent in human readible form as a switch statement
    switch (enc.rendering_intent)
    {
    case JXL_RENDERING_INTENT_PERCEPTUAL: spdlog::info("Rendering intent: perceptual"); break;
    case JXL_RENDERING_INTENT_RELATIVE: spdlog::info("Rendering intent: relative"); break;
    case JXL_RENDERING_INTENT_SATURATION: spdlog::info("Rendering intent: saturation"); break;
    case JXL_RENDERING_INTENT_ABSOLUTE: spdlog::info("Rendering intent: absolute"); break;
    default: spdlog::info("Rendering intent: unknown"); break;
    }
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

    int3                 size{0, 0, 0};
    std::vector<float>   pixels;
    JxlColorEncoding     file_enc;
    JxlColorEncoding     target_enc;
    JxlBasicInfo         info;
    std::vector<uint8_t> icc_profile, target_profile;
    bool                 has_encoded_profile = false;

    // CmsData m_cms;

    ImagePtr image{nullptr};

    {
        // Multi-threaded parallel runner.
        auto runner = JxlResizableParallelRunnerMake(nullptr);
        auto dec    = JxlDecoderMake(nullptr);
        if (JXL_DEC_SUCCESS !=
            JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE))
            throw invalid_argument{"JxlDecoderSubscribeEvents failed"};

        if (JXL_DEC_SUCCESS != JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner, runner.get()))
            throw invalid_argument{"JxlDecoderSetParallelRunner failed"};

        JxlPixelFormat format;

        // const JxlCmsInterface cmsInterface{.init_data   = &m_cms,
        //                                    .init        = CmsInit,
        //                                    .get_src_buf = CmsGetSrcBuffer,
        //                                    .get_dst_buf = CmsGetDstBuffer,
        //                                    .run         = CmsRun,
        //                                    .destroy     = CmsDestroy};
        // if (JXL_DEC_SUCCESS != JxlDecoderSetCms(dec.get(), cmsInterface))
        //     throw invalid_argument{"Failed to set CMS."};

        JxlDecoderSetCms(dec.get(), *JxlGetDefaultCms());

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
                if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info))
                    throw invalid_argument{"JxlDecoderGetBasicInfo failed"};

                size.x = (int)info.xsize;
                size.y = (int)info.ysize;
                size.z = (int)info.num_color_channels + info.num_extra_channels;

                image                          = make_shared<Image>(size.xy(), size.z);
                image->filename                = filename;
                image->file_has_straight_alpha = size.z > 3 && !info.alpha_premultiplied;

                format = {(uint32_t)size.z, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};
                JxlResizableParallelRunnerSetThreads(runner.get(),
                                                     JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
            }
            else if (status == JXL_DEC_COLOR_ENCODING)
            {
                // static constexpr JxlColorEncoding linear = {
                //     JXL_COLOR_SPACE_RGB,            // color_space
                //     JXL_WHITE_POINT_D65,            // white_point
                //     {},                             // white_point_xy
                //     JXL_PRIMARIES_SRGB,             // primaries
                //     {},                             // primaries_red_xy
                //     {},                             // primaries_green_xy
                //     {},                             // primaries_blue_xy
                //     JXL_TRANSFER_FUNCTION_LINEAR,   // transfer_function
                //     1.0,                            // gamma
                //     JXL_RENDERING_INTENT_PERCEPTUAL // rendering_intent
                // };

                // if (JXL_DEC_SUCCESS != JxlDecoderSetOutputColorProfile(dec.get(), &linear, nullptr, 0))
                //     // if (JXL_DEC_SUCCESS != JxlDecoderSetPreferredColorProfile(dec.get(), &linear))
                //     throw invalid_argument{"Failed to set output color space."};

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
                    spdlog::info("JPEG XL file has an encoded color profile:");
                    print_color_encoding_info(file_enc);
                }
                else
                    spdlog::warn("JPEG XL file has no encoded color profile. Colors distortions may occur.");

                if (JXL_DEC_SUCCESS ==
                    JxlDecoderGetColorAsEncodedProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA, &target_enc))
                {
                    spdlog::info("libJXL understands that we set a target color encoding:");
                    print_color_encoding_info(target_enc);
                }
                else
                    spdlog::warn("libJXL does NOT understand that we set a target color encoding");
            }
            else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
            {
                size_t buffer_size;
                if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size))
                    throw invalid_argument{"JxlDecoderImageOutBufferSize failed"};

                if (buffer_size != product(size) * sizeof(float))
                    throw invalid_argument{
                        fmt::format("Invalid out buffer size {} {}", buffer_size, product(size) * sizeof(float))};

                pixels.resize(product(size));
                void  *pixels_buffer      = static_cast<void *>(pixels.data());
                size_t pixels_buffer_size = pixels.size() * sizeof(float);
                if (JXL_DEC_SUCCESS !=
                    JxlDecoderSetImageOutBuffer(dec.get(), &format, pixels_buffer, pixels_buffer_size))
                    throw invalid_argument{"JxlDecoderSetImageOutBuffer failed"};
            }
            else if (status == JXL_DEC_FULL_IMAGE)
            {
                // Nothing to do. Do not yet return. If the image is an animation, more
                // full frames may be decoded. This example only keeps the last one.
            }
            else if (status == JXL_DEC_FRAME)
            {
                // Nothing to do
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

    if (product(size) == 0)
        throw invalid_argument{"Image has zero pixels."};

    bool colors_linearized = false;
#ifdef HDRVIEW_ENABLE_LCMS2
    // transform the interleaved data using the icc profile
    if (!icc_profile.empty() && !has_encoded_profile)
    {
        spdlog::info("JPEG-XL: Linearizing pixel values using image's ICC color profile.");

        auto            profile_in = cms::open_profile_from_mem(icc_profile);
        cmsCIExyY       whitepoint;
        cmsCIExyYTRIPLE primaries;
        cms::extract_chromaticities(profile_in, primaries, whitepoint);
        // print out the chromaticities
        spdlog::info("JPEG-XL: Applying chromaticities deduced from image's ICC profile:");
        spdlog::info("    Red primary: ({}, {})", primaries.Red.x, primaries.Red.y);
        spdlog::info("    Green primary: ({}, {})", primaries.Green.x, primaries.Green.y);
        spdlog::info("    Blue primary: ({}, {})", primaries.Blue.x, primaries.Blue.y);
        spdlog::info("    White point: ({}, {})", whitepoint.x, whitepoint.y);
        Imf::addChromaticities(image->header, {Imath::V2f(primaries.Red.x, primaries.Red.y),
                                               Imath::V2f(primaries.Green.x, primaries.Green.y),
                                               Imath::V2f(primaries.Blue.x, primaries.Blue.y),
                                               Imath::V2f(whitepoint.x, whitepoint.y)});

        auto            profile_out = cms::create_linear_RGB_profile(whitepoint, primaries);
        cmsUInt32Number format      = TYPE_GRAY_FLT;
        if (size.z == 3)
            format = TYPE_RGB_FLT;
        else if (size.z == 4)
            format = TYPE_RGBA_FLT;
        if (auto xform =
                cms::Transform{cmsCreateTransform(profile_in.get(), format, profile_out.get(), format,
                                                  INTENT_ABSOLUTE_COLORIMETRIC, size.z == 4 ? cmsFLAGS_COPY_ALPHA : 0)})
        {
            spdlog::info("JPEG-XL: ICC profile description: '{}'", cms::profile_description(profile_in));
            cmsDoTransform(xform.get(), pixels.data(), pixels.data(), pixels.size() / size.z);
            colors_linearized = true;
        }
        else
            spdlog::error("JPEG-XL: Could not create color transform.");
    }
#endif // HDRVIEW_ENABLE_LCMS2

    Timer timer;
    for (int c = 0; c < size.z; ++c)
        image->channels[c].copy_from_interleaved(pixels.data(), size.x, size.y, size.z, c, [](float v) { return v; });
    spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));

    if (has_encoded_profile && !colors_linearized)
    {
        spdlog::info("JPEG-XL: Linearizing pixel values using encoded profile.");
        Imf::Chromaticities chromaticities;
        chromaticities.red   = Imath::V2f(file_enc.primaries_red_xy[0], file_enc.primaries_red_xy[1]);
        chromaticities.green = Imath::V2f(file_enc.primaries_green_xy[0], file_enc.primaries_green_xy[1]);
        chromaticities.blue  = Imath::V2f(file_enc.primaries_blue_xy[0], file_enc.primaries_blue_xy[1]);
        chromaticities.white = Imath::V2f(file_enc.white_point_xy[0], file_enc.white_point_xy[1]);
        Imf::addChromaticities(image->header, chromaticities);
        Imf::addWhiteLuminance(image->header, info.intensity_target);

        // apply transfer function
        if (image->channels.size() <= 2)
        {
            if (file_enc.transfer_function == JXL_TRANSFER_FUNCTION_PQ)
                image->channels[0].apply([](float v, int x, int y) { return EOTF_PQ(v) / 255.f; });
            else if (file_enc.transfer_function == JXL_TRANSFER_FUNCTION_HLG)
                image->channels[0].apply([](float v, int x, int y) { return EOTF_HLG(v, v) / 255.f; });
            else
                image->channels[0].apply([](float v, int x, int y) { return SRGBToLinear(v); });
        }
        else if (image->channels.size() == 3 || image->channels.size() == 4)
        {
            // HLG needs to operate on all three channels at once
            if (file_enc.transfer_function == JXL_TRANSFER_FUNCTION_HLG)
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
                    if (file_enc.transfer_function == JXL_TRANSFER_FUNCTION_PQ)
                        image->channels[c].apply([](float v, int x, int y) { return EOTF_PQ(v) / 255.f; });
                    else
                        image->channels[c].apply([](float v, int x, int y) { return SRGBToLinear(v); });
            }
        }
        else
            spdlog::warn("HEIF: Don't know how to apply transfer function to {} channels", image->channels.size());
    }

    return {image};
}

#endif
