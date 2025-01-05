//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "stb.h"
#include "dithermatrix256.h"
#include "image.h"
#include "texture.h"
#include "timer.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <fstream>
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

bool is_stb_image(istream &is) noexcept
{
    bool ret = false;
    try
    {
        stbi__context s;
        stbi__start_callbacks(&s, (stbi_io_callbacks *)&stbi_callbacks, &is);

        ret = stbi__jpeg_test(&s) || stbi__png_test(&s) || stbi__bmp_test(&s) || stbi__gif_test(&s) ||
              stbi__psd_test(&s) || stbi__pic_test(&s) || stbi__pnm_test(&s) || stbi__hdr_test(&s) ||
              stbi__tga_test(&s);
    }
    catch (...)
    {
        //
    }

    // rewind
    is.clear();
    is.seekg(0);
    return ret;
}

vector<ImagePtr> load_stb_image(istream &is, const string &filename)
{
    // stbi doesn't do proper srgb, but uses gamma=2.2 instead, so override it.
    // we'll do our own srgb correction
    stbi_ldr_to_hdr_scale(1.0f);
    stbi_ldr_to_hdr_gamma(1.0f);

    int3 size;
    using FloatBuffer = std::unique_ptr<float[], void (*)(void *)>;
    auto float_data =
        FloatBuffer{stbi_loadf_from_callbacks(&stbi_callbacks, &is, &size.x, &size.y, &size.z, 0), stbi_image_free};
    if (float_data)
    {
        auto image      = make_shared<Image>(size.xy(), size.z);
        image->filename = filename;

        bool linearize = !stbi_is_hdr(filename.c_str());

        for (int c = 0; c < size.z; ++c)
        {
            Timer timer;
            image->channels[c].copy_from_interleaved(float_data.get(), size.x, size.y, size.z, c,
                                                     [](float v) { return v; });
            if (linearize && c != 3)
                image->channels[c].apply([linearize](float v, int x, int y)
                                         { return Channel::dequantize(v, x, y, linearize, true); });
            spdlog::debug("Copying image channel {} took: {} seconds.", c, (timer.elapsed() / 1000.f));
        }
        return {image};
    }
    else
        throw invalid_argument(stbi_failure_reason());
}

bool save_stb_image(const Image &img, ostream &os, const string &filename, float gain, float gamma, bool sRGB,
                    bool dither)
{
    static const auto ostream_write_func = [](void *context, void *data, int size)
    { reinterpret_cast<ostream *>(context)->write(reinterpret_cast<char *>(data), size); };

    string extension = to_lower(get_extension(filename));

    // convert floating-point image to 8-bit per channel with dithering
    int             n = img.groups[img.selected_group].num_channels;
    int             w = img.size().x;
    int             h = img.size().y;
    vector<uint8_t> data(w * h * n, 0);

    gamma = 1.f / gamma;
    Timer timer;
    // convert channel data into n-channel interleaved format expected by stb_image
    parallel_for(blocked_range<int>(0, h),
                 [&img, &data, gain, gamma, sRGB, dither, w, n](int y, int, int, int)
                 {
                     for (int x = 0; x < w; ++x)
                     {
                         float d = dither ? (tent_dither(x, y)) / 255.0f : 0.f;

                         for (int c = 0; c < n; ++c)
                         {
                             const Channel &chan = img.channels[img.groups[img.selected_group].channels[c]];
                             float          v    = gain * chan(x, y);
                             if (sRGB)
                                 v = LinearToSRGB(v);
                             else if (gamma != 1.0f)
                                 v = pow(v, gamma);

                             // unpremultiply
                             if (n > 3 && c < 3)
                             {
                                 float a = img.channels[img.groups[img.selected_group].channels[3]](x, y);
                                 if (a != 0.f)
                                     v /= a;
                             }

                             if (c < 3)
                                 v += d;

                             // convert to [0-255] range
                             v                           = clamp(v * 255.0f, 0.0f, 255.0f);
                             data[n * x + n * y * w + c] = (uint8_t)v;
                         }
                     }
                 });
    spdlog::debug("Tonemapping to 8bit took: {} seconds.", (timer.elapsed() / 1000.f));

    // if (extension == "ppm")
    //     return write_ppm_image(filename.c_str(), width(), height(), 3, &data[0]);
    // else
    if (extension == "png")
        return stbi_write_png_to_func(ostream_write_func, &os, w, h, n, &data[0], 0) != 0;
    else if (extension == "bmp")
        return stbi_write_bmp_to_func(ostream_write_func, &os, w, h, n, &data[0]) != 0;
    else if (extension == "tga")
        return stbi_write_tga_to_func(ostream_write_func, &os, w, h, n, &data[0]) != 0;
    else if (extension == "jpg" || extension == "jpeg")
        return stbi_write_jpg_to_func(ostream_write_func, &os, w, h, n, &data[0], 100) != 0;
    else
        throw invalid_argument(fmt::format("Could not determine desired file type from extension \"{}\".", extension));
}
