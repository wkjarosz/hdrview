//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "common.h" // for lerp, mod, clamp, getExtension
#include "image.h"
#include "timer.h"
#include <stdexcept> // for runtime_error, out_of_range

#include <spdlog/stopwatch.h>

#include "imageio/dds.h"
#include "imageio/exr.h"
#include "imageio/heif.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/pfm.h"
#include "imageio/png.h"
#include "imageio/qoi.h"
#include "imageio/stb.h"
#include "imageio/uhdr.h"

using namespace std;

vector<ImagePtr> Image::load(istream &is, string_view filename, string_view channel_selector)
{
    spdlog::info("Loading from file: {}", filename);
    ScopedMDC mdc{"file", string(get_basename(filename))};
    Timer     timer;
    try
    {
        if (!is.good())
            throw invalid_argument("Invalid input stream");

        vector<ImagePtr> images;

        if (is_exr_image(is, filename))
        {
            spdlog::info("Detected EXR image.");
            images = load_exr_image(is, filename, channel_selector);
        }
        else if (is_uhdr_image(is))
        {
            spdlog::info("Detected UltraHDR JPEG image. Loading via libultrahdr.");
            images = load_uhdr_image(is, filename);
        }
        else if (is_jpg_image(is))
        {
            spdlog::info("Detected JPEG image. Loading via libjpeg.");
            images = load_jpg_image(is, filename, channel_selector);
        }
        else if (is_qoi_image(is))
        {
            spdlog::info("Detected QOI image.");
            images = load_qoi_image(is, filename);
        }
        else if (is_jxl_image(is))
        {
            spdlog::info("Detected JPEG XL image. Loading via libjxl.");
            images = load_jxl_image(is, filename, channel_selector);
        }
        // is_heif_image falsely claims many dds files are heif files, and then fails, so we put dds earlier
        else if (is_dds_image(is))
        {
            spdlog::info("Detected dds-compatible image. Loading via smalldds.");
            images = load_dds_image(is, filename, channel_selector);
        }
        else if (is_heif_image(is))
        {
            spdlog::info("Detected HEIF image.");
            images = load_heif_image(is, filename, channel_selector);
        }
        else if (is_png_image(is))
        {
            spdlog::info("Detected PNG image. Loading via libpng.");
            images = load_png_image(is, filename, channel_selector);
        }
        else if (is_stb_image(is))
        {
            spdlog::info("Detected stb-compatible image. Loading via stb_image.");
            images = load_stb_image(is, filename);
        }
        else if (is_pfm_image(is))
        {
            spdlog::info("Detected PFM image.");
            images = load_pfm_image(is, filename);
        }
        else
            throw invalid_argument("This doesn't seem to be a supported image file.");

        for (auto i : images)
        {
            try
            {
                i->finalize();
                i->filename   = filename;
                i->short_name = i->file_and_partname();

                // If multiple image "parts" were loaded and they have names, store these names in the image's channel
                // selector. This is useful if we later want to reload a specific image part from the original file.
                if (i->partname.empty())
                    i->channel_selector = string{channel_selector};
                else
                {
                    const auto selector_parts = split(channel_selector, ",");
                    if (channel_selector.empty())
                        i->channel_selector = i->partname;
                    else if (find(begin(selector_parts), end(selector_parts), i->partname) == end(selector_parts))
                        i->channel_selector = fmt::format("{},{}", i->partname, channel_selector);
                    else
                        i->channel_selector = string{channel_selector};
                }

                spdlog::info("Loaded image in {:f} seconds:\n{:s}", timer.elapsed() / 1000.f, i->to_string());
            }
            catch (const exception &e)
            {
                spdlog::error("Skipping image loaded from \"{}\" due to error:\n\t{}", filename, e.what());
                continue; // skip this image
            }
        }
        return images;
    }
    catch (const exception &e)
    {
        spdlog::error("Unable to load image file \"{}\":\n\t{}", filename, e.what());
    }
    return {};
}

void Image::save(ostream &os, string_view _filename, float gain, bool sRGB, bool dither) const
{
    string extension = to_lower(get_extension(_filename));
    if (extension == ".exr")
        return save_exr_image(*this, os, _filename);
    else if (extension == ".jpg" || extension == ".jpeg")
        return save_jpg_image(*this, os, _filename, 95, true, gain, sRGB, dither);
    // else if (extension == ".jpg" || extension == ".jpeg")
    //     return save_uhdr_image(*this, os, _filename, gain);
    else if (extension == ".pfm")
        return save_pfm_image(*this, os, _filename, gain);
    else if (extension == ".qoi")
        return save_qoi_image(*this, os, _filename, gain, sRGB, dither);
    else if (extension == ".png")
        return save_png_image(*this, os, _filename, gain, sRGB, dither);
    else
        return save_stb_image(*this, os, _filename, gain, sRGB, dither);
}

std::unique_ptr<uint8_t[]> Image::as_interleaved_bytes(int *w, int *h, int *n, float gain, bool sRGB, bool dither) const
{
    Timer timer;
    *w                   = size().x;
    *h                   = size().y;
    *n                   = groups[selected_group].num_channels;
    const Channel *alpha = *n > 3 ? &channels[groups[selected_group].channels[3]] : nullptr;

    std::unique_ptr<uint8_t[]> pixels(new uint8_t[(*w) * (*h) * (*n)]);

    int block_size = std::max(1, 1024 * 1024 / (*w));
    parallel_for(
        blocked_range<int>(0, *h, block_size),
        [this, alpha, w = *w, n = *n, data = pixels.get(), gain, sRGB, dither](int begin_y, int end_y, int, int)
        {
            int y_stride = w * n;
            for (int y = begin_y; y < end_y; ++y)
                for (int x = 0; x < w; ++x)
                {
                    auto rgba_pixel = data + y * y_stride + n * x;
                    for (int c = 0; c < n; ++c)
                    {
                        float v = channels[groups[selected_group].channels[c]](x, y);

                        // only gamma correct and premultiply the RGB channels.
                        // alpha channel gets stored linearly.
                        if (c < 3)
                        {
                            v *= gain;

                            // unpremultiply
                            if (alpha)
                                v /= std::max(k_small_alpha, (*alpha)(x, y));

                            if (sRGB)
                                v = linear_to_sRGB(v);
                        }

                        rgba_pixel[c] = quantize_full<uint8_t>(v, dither, x, y);
                    }
                }
        });

    spdlog::debug("Getting interleaved 8-bit pixels took: {} seconds.", (timer.elapsed() / 1000.f));
    return pixels;
}

std::unique_ptr<uint16_t[]> Image::as_interleaved_shorts(int *w, int *h, int *n, float gain, bool sRGB,
                                                         bool dither) const
{
    Timer timer;
    *w                   = size().x;
    *h                   = size().y;
    *n                   = groups[selected_group].num_channels;
    const Channel *alpha = *n > 3 ? &channels[groups[selected_group].channels[3]] : nullptr;

    std::unique_ptr<uint16_t[]> pixels(new uint16_t[(*w) * (*h) * (*n)]);

    int block_size = std::max(1, 1024 * 1024 / (*w));
    parallel_for(
        blocked_range<int>(0, *h, block_size),
        [this, alpha, w = *w, n = *n, data = pixels.get(), gain, sRGB, dither](int begin_y, int end_y, int, int)
        {
            int y_stride = w * n;
            for (int y = begin_y; y < end_y; ++y)
                for (int x = 0; x < w; ++x)
                {
                    auto rgba_pixel = data + y * y_stride + n * x;
                    for (int c = 0; c < n; ++c)
                    {
                        float v = channels[groups[selected_group].channels[c]](x, y);

                        // only gamma correct and premultiply the RGB channels.
                        // alpha channel gets stored linearly.
                        if (c < 3)
                        {
                            v *= gain;

                            // unpremultiply
                            if (alpha)
                                v /= std::max(k_small_alpha, (*alpha)(x, y));

                            if (sRGB)
                                v = linear_to_sRGB(v);
                        }

                        rgba_pixel[c] = quantize_full<uint16_t>(v, dither, x, y);
                    }
                }
        });

    spdlog::debug("Getting interleaved 16-bit pixels took: {} seconds.", (timer.elapsed() / 1000.f));
    return pixels;
}

std::unique_ptr<float[]> Image::as_interleaved_floats(int *w, int *h, int *n, float gain) const
{
    Timer timer;
    *w                   = size().x;
    *h                   = size().y;
    *n                   = groups[selected_group].num_channels;
    const Channel *alpha = *n > 3 ? &channels[groups[selected_group].channels[3]] : nullptr;

    std::unique_ptr<float[]> pixels(new float[(*w) * (*h) * (*n)]);

    // for (int c = 0; c < (*n); ++c)
    //     channels[groups[selected_group].channels[c]].copy_to_interleaved(pixels.get(), *n, c,
    //                                                                      [gain, c, alpha](float v, int x, int y)
    //                                                                      {
    //                                                                          v *= gain;

    //                                                                          // unpremultiply
    //                                                                          if (alpha)
    //                                                                            v /= std::max(k_small_alpha,
    //                                                                            (*alpha)(x, y));
    //                                                                          return v;
    //                                                                      });

    int block_size = std::max(1, 1024 * 1024 / (*w));
    parallel_for(blocked_range<int>(0, *h, block_size),
                 [this, alpha, w = *w, n = *n, data = pixels.get(), gain](int begin_y, int end_y, int, int)
                 {
                     int y_stride = w * n;
                     for (int y = begin_y; y < end_y; ++y)
                         for (int x = 0; x < w; ++x)
                         {
                             auto rgba_pixel = data + y * y_stride + n * x;
                             for (int c = 0; c < n; ++c)
                             {
                                 float v = channels[groups[selected_group].channels[c]](x, y);

                                 v *= gain;

                                 // unpremultiply
                                 if (alpha)
                                     v /= std::max(k_small_alpha, (*alpha)(x, y));

                                 rgba_pixel[c] = v;
                             }
                         }
                 });

    spdlog::debug("Getting interleaved floating-point pixels took: {} seconds.", (timer.elapsed() / 1000.f));
    return pixels;
}

std::unique_ptr<half[]> Image::as_interleaved_halves(int *w, int *h, int *n, float gain) const
{
    auto floats = as_interleaved_floats(w, h, n, gain);

    // copy floats to halfs
    int                     size = (*w) * (*h) * (*n);
    std::unique_ptr<half[]> pixels(new half[size]);
    for (int i = 0; i < size; ++i) pixels[i] = (half)floats[i];
    return pixels;
}