//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "stb.h"
#include "image.h"
#include "timer.h"
#include <cstdint>
#include <cstring>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>

// these pragmas ignore warnings about unused static functions
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

// since other libraries might include old versions of stb headers, we declare stb static here
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

using namespace std;

static const stbi_io_callbacks stbi_callbacks = {
    // read
    [](void *user, char *data, int size)
    {
        auto stream = reinterpret_cast<istream *>(user);
        stream->read(data, size);
        return (int)stream->gcount();
    },
    // seek
    [](void *user, int size) { reinterpret_cast<istream *>(user)->seekg(size, ios_base::cur); },
    // eof
    [](void *user) { return (int)reinterpret_cast<istream *>(user)->eof(); },
};

static bool supported_format(istream &is, json &j) noexcept
{
    is.clear();
    is.seekg(0);
    try
    {
        stbi__context s;
        stbi__start_callbacks(&s, (stbi_io_callbacks *)&stbi_callbacks, &is);

        // these are ordered like stbi__load_main does for speed an reliability
        if (stbi__png_test(&s))
            j["format"] = "png";
        else if (stbi__bmp_test(&s))
            j["format"] = "bmp";
        else if (stbi__gif_test(&s))
            j["format"] = "gif";
        else if (stbi__psd_test(&s))
            j["format"] = "psd";
        else if (stbi__pic_test(&s))
            j["format"] = "pic";
        else if (stbi__jpeg_test(&s))
            j["format"] = "jpeg";
        else if (stbi__pnm_test(&s))
            j["format"] = "pnm";
        else if (stbi__hdr_test(&s))
            j["format"] = "hdr";
        else if (stbi__tga_test(&s))
            j["format"] = "tga";
    }
    catch (...)
    {
        //
    }
    // shouldn't be necessary, but just in case:
    // rewind
    is.clear();
    is.seekg(0);
    return j.contains("format");
}

bool is_stb_image(istream &is) noexcept
{
    json j;
    return supported_format(is, j);
}

vector<ImagePtr> load_stb_image(istream &is, const string_view filename)
{
    ScopedMDC mdc{"IO", "STB"};
    // stbi doesn't do proper srgb, but uses gamma=2.2 instead, so override it.
    // we'll do our own srgb correction
    stbi_ldr_to_hdr_scale(1.0f);
    stbi_ldr_to_hdr_gamma(1.0f);

    bool is_hdr = stbi_is_hdr_from_callbacks(&stbi_callbacks, &is) != 0;
    is.clear();
    is.seekg(0);
    bool is_16_bit = stbi_is_16_bit_from_callbacks(&stbi_callbacks, &is);
    is.clear();
    is.seekg(0);

    using DataBuffer = std::unique_ptr<void, void (*)(void *)>;
    DataBuffer data{nullptr, stbi_image_free};

    int4 size{0, 0, 0, 1}; // width, height, channels, frames

    if (is_hdr)
        data = DataBuffer{(void *)stbi_loadf_from_callbacks(&stbi_callbacks, &is, &size.x, &size.y, &size.z, 0),
                          stbi_image_free};
    else
    {
        stbi__context s;
        stbi__start_callbacks(&s, (stbi_io_callbacks *)&stbi_callbacks, &is);
        bool is_gif = stbi__gif_test(&s);
        is.clear();
        is.seekg(0);

        if (is_gif)
        {
            stbi__start_callbacks(&s, (stbi_io_callbacks *)&stbi_callbacks, &is);
            int *delays; // we'll just load all frames, and ignore delays
            data = DataBuffer{stbi__load_gif_main(&s, &delays, &size.x, &size.y, &size.w, &size.z, 0), stbi_image_free};
        }
        else if (is_16_bit)
            data = DataBuffer{(void *)stbi_load_16_from_callbacks(&stbi_callbacks, &is, &size.x, &size.y, &size.z, 0),
                              stbi_image_free};
        else
            data = DataBuffer{stbi_load_from_callbacks(&stbi_callbacks, &is, &size.x, &size.y, &size.z, 0),
                              stbi_image_free};
    }

    if (!data)
        throw invalid_argument{stbi_failure_reason()};

    if (product(size) == 0)
        throw invalid_argument{"Image has zero pixels."};

    json j;
    if (!supported_format(is, j))
        throw runtime_error{"loaded the image, but then couldn't figure out the format (this should never happen)."};

    bool linearize = j["format"] != "hdr";
    if (linearize)
        spdlog::info("Assuming STB image is sRGB encoded, linearizing.");

    Timer            timer;
    vector<ImagePtr> images(size.w);
    void            *data_ptr = (void *)data.get();
    for (int frame = 0; frame < size.w; ++frame)
    {
        auto &image = images[frame];

        image                          = make_shared<Image>(size.xy(), size.z);
        image->filename                = filename;
        image->file_has_straight_alpha = true;
        if (size.w > 1)
            image->partname = fmt::format("frame {:04}", frame);
        image->metadata["loader"] = fmt::format("stb_image ({})", j["format"].get<string>());

        if (!linearize)
            image->metadata["pixel format"] = "8:8:8:8 rgbe";
        else if (is_16_bit)
            image->metadata["pixel format"] = fmt::format("{}-bit ({} bpc)", 16 * size.z, 16);
        else
            image->metadata["pixel format"] = fmt::format("{} bbp", 8);

        image->metadata["transfer function"] = linearize ? transfer_function_name(TransferFunction_Unknown)
                                                         : transfer_function_name(TransferFunction_Linear);

        if (is_hdr)
        {
            for (int c = 0; c < size.z; ++c)
                image->channels[c].copy_from_interleaved((float *)data_ptr, size.x, size.y, size.z, c,
                                                         [](float v) { return v; });
            data_ptr = (float *)data_ptr + size.x * size.y * size.z;
        }
        else
        {
            Timer timer;
            if (is_16_bit)
            {
                for (int c = 0; c < size.z; ++c)
                    image->channels[c].copy_from_interleaved((uint16_t *)data_ptr, size.x, size.y, size.z, c,
                                                             [c](uint16_t v)
                                                             {
                                                                 float f = dequantize_full(v);
                                                                 return c != 3 ? sRGB_to_linear(f) : f;
                                                             });
                data_ptr = (uint16_t *)data_ptr + size.x * size.y * size.z;
            }
            else
            {
                for (int c = 0; c < size.z; ++c)
                    image->channels[c].copy_from_interleaved((uint8_t *)data_ptr, size.x, size.y, size.z, c,
                                                             [c](uint8_t v)
                                                             {
                                                                 float f = dequantize_full(v);
                                                                 return c != 3 ? sRGB_to_linear(f) : f;
                                                             });
                data_ptr = (uint8_t *)data_ptr + size.x * size.y * size.z;
            }
        }
    }
    spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));

    return images;
}

void save_stb_image(const Image &img, ostream &os, const string_view filename, float gain, bool sRGB, bool dither)
{
    Timer             timer;
    static const auto ostream_write_func = [](void *context, void *data, int size)
    { reinterpret_cast<ostream *>(context)->write(reinterpret_cast<char *>(data), size); };

    string extension = to_lower(get_extension(filename));

    if (extension == ".hdr")
    {
        // get interleaved HDR pixel data
        int  w = 0, h = 0, n = 0;
        auto pixels = img.as_interleaved_floats(&w, &h, &n, gain);
        if (stbi_write_hdr_to_func(ostream_write_func, &os, w, h, n, pixels.get()) == 0)
            throw runtime_error("Failed to write HDR image via stb.");
        spdlog::info("Saved HDR image via stb to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
    }
    else
    {
        // get interleaved LDR pixel data
        int  w = 0, h = 0, n = 0;
        auto pixels = img.as_interleaved_bytes(&w, &h, &n, gain, sRGB, dither);

        bool success = false;
        if (extension == ".png")
            success = stbi_write_png_to_func(ostream_write_func, &os, w, h, n, pixels.get(), 0) != 0;
        else if (extension == ".bmp")
            success = stbi_write_bmp_to_func(ostream_write_func, &os, w, h, n, pixels.get()) != 0;
        else if (extension == ".tga")
            success = stbi_write_tga_to_func(ostream_write_func, &os, w, h, n, pixels.get()) != 0;
        else if (extension == ".jpg" || extension == ".jpeg")
            success = stbi_write_jpg_to_func(ostream_write_func, &os, w, h, n, pixels.get(), 100) != 0;
        else
            throw invalid_argument(
                fmt::format("Could not determine desired file type from extension \"{}\".", extension));
        if (success)
            spdlog::info("Saved {} image via stb to '{}' in {} seconds.", to_upper(extension.substr(1)), filename,
                         (timer.elapsed() / 1000.f));
        else
            throw runtime_error(fmt::format("Failed to write {} image via stb.", to_upper(extension.substr(1))));
    }
}
