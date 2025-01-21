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

#include "colorspace.h"
#include "timer.h"

// #include <lcms2.h>

// typedef void *cmsHPROFILE;
// typedef void *cmsHTRANSFORM;

// struct CmsData
// {
//     std::vector<float *> srcBuf;
//     std::vector<float *> dstBuf;

//     cmsHPROFILE   profileIn;
//     cmsHPROFILE   profileOut;
//     cmsHTRANSFORM transform;
// };

static constexpr JxlColorEncoding linear = {
    JXL_COLOR_SPACE_RGB,            // color_space
    JXL_WHITE_POINT_D65,            // white_point
    {},                             // white_point_xy
    JXL_PRIMARIES_SRGB,             // primaries
    {},                             // primaries_red_xy
    {},                             // primaries_green_xy
    {},                             // primaries_blue_xy
    JXL_TRANSFER_FUNCTION_LINEAR,   // transfer_function
    1.0,                            // gamma
    JXL_RENDERING_INTENT_PERCEPTUAL // rendering_intent
};

// static void *CmsInit(void *data, size_t num_threads, size_t pixels_per_thread, const JxlColorProfile *input_profile,
//                      const JxlColorProfile *output_profile, float intensity_target)
// {
//     spdlog::info("CmsInit");
//     auto cms = (CmsData *)data;

//     cms->srcBuf.resize(num_threads);
//     cms->dstBuf.resize(num_threads);

//     for (size_t i = 0; i < num_threads; i++)
//     {
//         cms->srcBuf[i] = new float[pixels_per_thread * 3];
//         cms->dstBuf[i] = new float[pixels_per_thread * 3];
//     }

//     cms->profileIn  = cmsOpenProfileFromMem(input_profile->icc.data, input_profile->icc.size);
//     cms->profileOut = cmsOpenProfileFromMem(output_profile->icc.data, output_profile->icc.size);
//     cms->transform =
//         cmsCreateTransform(cms->profileIn, TYPE_RGB_FLT, cms->profileOut, TYPE_RGB_FLT, INTENT_PERCEPTUAL, 0);

//     return cms;
// }

// static float *CmsGetSrcBuffer(void *data, size_t thread)
// {
//     spdlog::info("CmsGetSrcBuffer");
//     auto cms = (CmsData *)data;
//     return cms->srcBuf[thread];
// }

// static float *CmsGetDstBuffer(void *data, size_t thread)
// {
//     spdlog::info("CmsGetDstBuffer");
//     auto cms = (CmsData *)data;
//     return cms->dstBuf[thread];
// }

// static JXL_BOOL CmsRun(void *data, size_t thread, const float *input, float *output, size_t num_pixels)
// {
//     spdlog::info("CmsRun");
//     auto cms = (CmsData *)data;
//     cmsDoTransform(cms->transform, input, output, num_pixels);
//     return true;
// }

// static void CmsDestroy(void *data)
// {
//     auto cms = (CmsData *)data;

//     cmsDeleteTransform(cms->transform);
//     cmsCloseProfile(cms->profileOut);
//     cmsCloseProfile(cms->profileIn);

//     for (auto &buf : cms->srcBuf) delete[] buf;
//     for (auto &buf : cms->dstBuf) delete[] buf;
// }

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
    JxlBasicInfo         info;
    std::vector<uint8_t> jxl_profile, target_profile;

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
                image->file_has_straight_alpha = !info.alpha_premultiplied;

                format = {(uint32_t)size.z, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};
                JxlResizableParallelRunnerSetThreads(runner.get(),
                                                     JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
            }
            else if (status == JXL_DEC_COLOR_ENCODING)
            {
                // if (JXL_DEC_SUCCESS != JxlDecoderSetOutputColorProfile(dec.get(), nullptr, nullptr, 0))
                //     throw invalid_argument{"Failed to set color space."};
                if (JXL_DEC_SUCCESS != JxlDecoderSetPreferredColorProfile(dec.get(), &linear))
                    throw invalid_argument{"Failed to set color space."};

                // Get the ICC color profile of the pixel data
                size_t icc_size;

                if (JXL_DEC_SUCCESS !=
                    JxlDecoderGetICCProfileSize(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, &icc_size))
                    throw invalid_argument{"JxlDecoderGetICCProfileSize failed"};

                jxl_profile.resize(icc_size);
                if (JXL_DEC_SUCCESS != JxlDecoderGetColorAsICCProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL,
                                                                      jxl_profile.data(), jxl_profile.size()))
                    throw invalid_argument{"JxlDecoderGetColorAsICCProfile failed"};

                // if (JXL_DEC_SUCCESS != JxlDecoderSetOutputColorProfile(dec.get(), &linear, nullptr, 0))
                //     // if (JXL_DEC_SUCCESS != JxlDecoderSetPreferredColorProfile(dec.get(), &linear))
                //     throw invalid_argument{"JxlDecoderSetPreferredColorProfile failed"};

                if (JXL_DEC_SUCCESS != JxlDecoderGetICCProfileSize(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA, &icc_size))
                    throw invalid_argument{"JxlDecoderGetICCProfileSize failed"};

                target_profile.resize(icc_size);
                if (JXL_DEC_SUCCESS != JxlDecoderGetColorAsICCProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                                                                      target_profile.data(), target_profile.size()))
                    throw invalid_argument{"JxlDecoderGetColorAsICCProfile failed"};

                // if (JXL_DEC_SUCCESS ==
                //     JxlDecoderGetColorAsEncodedProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, &file_enc))
                // {
                //     spdlog::info("JPEG XL file has an encoded color profile");

                //     // Imf::Chromaticities chromaticities;
                //     // chromaticities.red   = Imath::V2f(file_enc.primaries_red_xy[0], file_enc.primaries_red_xy[1]);
                //     // chromaticities.green = Imath::V2f(file_enc.primaries_green_xy[0],
                //     // file_enc.primaries_green_xy[1]); chromaticities.blue  =
                //     Imath::V2f(file_enc.primaries_blue_xy[0],
                //     // file_enc.primaries_blue_xy[1]); chromaticities.white = Imath::V2f(file_enc.white_point_xy[0],
                //     // file_enc.white_point_xy[1]); Imf::addChromaticities(image->header, chromaticities);
                //     // Imf::addWhiteLuminance(image->header, info.intensity_target);
                // }
                // else
                //     spdlog::warn("JPEG XL file has no encoded color profile. Colors distortions may occur.");
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

    // std::vector<float> pixels2 = pixels;
    // m_cms.profileIn            = cmsOpenProfileFromMem(jxl_profile.data(), jxl_profile.size());
    // m_cms.profileOut           = cmsCreate_sRGBProfile();
    // // m_cms.profileOut = cmsOpenProfileFromMem(target_profile.data(), target_profile.size());
    // cmsUInt32Number format = TYPE_GRAY_FLT;
    // if (size.z == 3)
    //     format = TYPE_RGB_FLT;
    // else if (size.z = 4)
    //     format = TYPE_RGBA_FLT;
    // m_cms.transform = cmsCreateTransform(m_cms.profileIn, format, m_cms.profileOut, format, INTENT_PERCEPTUAL, 0);
    // cmsDoTransform(m_cms.transform, pixels2.data(), pixels.data(), pixels.size() / size.z);

    Timer timer;
    for (int c = 0; c < size.z; ++c)
        image->channels[c].copy_from_interleaved(pixels.data(), size.x, size.y, size.z, c, [](float v) { return v; });
    spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));

    return {image};
}

#endif
