//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "common.h" // for lerp, mod, clamp, getExtension
#include "exr_std_streams.h"
#include "image.h"
#include "parallelfor.h"
#include "texture.h"
#include "timer.h"
#include <ImfChannelList.h>
#include <ImfChromaticities.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfRgbaYca.h>
#include <ImfStandardAttributes.h>
#include <ImfTestFile.h> // for isOpenExrFile
#include <fstream>
#include <stdexcept> // for runtime_error, out_of_range

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>

#include "dithermatrix256.h"

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

#include "pfm.h"

using namespace std;

// static methods and member definitions
//

const float3 Image::Rec709_luminance_weights = float3{&Imf::RgbaYca::computeYw(Imf::Chromaticities{})[0]};

//
// end static methods and member definitions

// static local functions
//

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

static void copy_into_channel(Channel &channel, const float data[], int w, int h, int n, int c, bool linearize)
{
    bool dither = true;
    parallel_for(blocked_range<int>(0, h),
                 [&channel, n, c, w, &data, linearize, dither](int y, int, int, int)
                 {
                     for (int x = 0; x < w; ++x)
                     {
                         int   xmod = x % 256;
                         int   ymod = y % 256;
                         float d    = dither ? (dither_matrix256[xmod + ymod * 256] + 0.5f) / 65536.f : 0.5f;
                         int   i    = x + y * w;
                         float v    = data[n * i + c];
                         // perform unbiased quantization as in http://eastfarthing.com/blog/2015-12-19-color/
                         channel(i) = linearize ? SRGBToLinear(((v * 255.f) + d) / 256.0f) : v;
                     }
                 });
}

static bool is_stb_image(std::istream &is)
{
    stbi__context s;
    stbi__start_callbacks(&s, (stbi_io_callbacks *)&stbi_callbacks, &is);

    bool ret = stbi__jpeg_test(&s) || stbi__png_test(&s) || stbi__bmp_test(&s) || stbi__gif_test(&s) ||
               stbi__psd_test(&s) || stbi__pic_test(&s) || stbi__pnm_test(&s) || stbi__hdr_test(&s) ||
               stbi__tga_test(&s);

    // rewind
    is.clear();
    is.seekg(0);
    return ret;
}

static vector<ImagePtr> load_stb_image(std::istream &is, const string &filename)
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
            copy_into_channel(image->channels[c], float_data.get(), size.x, size.y, size.z, c, linearize && c != 3);
            spdlog::debug("Copying image channel {} took: {} seconds.", c, (timer.elapsed() / 1000.f));
        }
        return {image};
    }
    else
        throw invalid_argument(stbi_failure_reason());
}

static vector<ImagePtr> load_pfm_image(std::istream &is, const string &filename)
{
    int3 size;
    if (auto float_data = load_pfm_image(is, filename, &size.x, &size.y, &size.z))
    {
        auto image      = make_shared<Image>(size.xy(), size.z);
        image->filename = filename;

        Timer timer;
        for (int c = 0; c < size.z; ++c)
            copy_into_channel(image->channels[c], float_data.get(), size.x, size.y, size.z, c, false);
        spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed() / 1000.f));
        return {image};
    }
    else
        throw invalid_argument("Could not load PFM image.");
}

static vector<ImagePtr> load_exr_image(StdIStream &is, const string &filename)
{
    Imf::MultiPartInputFile infile{is};

    if (infile.parts() <= 0)
        throw invalid_argument{"EXR file contains no parts!"};

    vector<ImagePtr> images;
    for (int p = 0; p < infile.parts(); ++p)
    {
        Imf::InputPart part{infile, p};

        Imath::Box2i dataWindow    = part.header().dataWindow();
        Imath::Box2i displayWindow = part.header().displayWindow();
        int2         size          = {dataWindow.max.x - dataWindow.min.x + 1, dataWindow.max.y - dataWindow.min.y + 1};

        if (size.x <= 0 || size.y <= 0)
        {
            spdlog::warn("EXR part {}: '{}' has zero pixels, skipping...", p,
                         part.header().hasName() ? part.header().name() : "unnamed");
            continue;
        }

        images.emplace_back(make_shared<Image>());
        auto  &data        = *images.back();
        string part_prefix = "";

        if (part.header().hasName())
        {
            data.partname = part.header().name();
            part_prefix   = data.partname + "."s;
        }

        data.data_window    = {{dataWindow.min.x, dataWindow.min.y}, {dataWindow.max.x + 1, dataWindow.max.y + 1}};
        data.display_window = {{displayWindow.min.x, displayWindow.min.y},
                               {displayWindow.max.x + 1, displayWindow.max.y + 1}};

        if (data.data_window.is_empty())
            throw invalid_argument{fmt::format("EXR image has invalid data window: [{},{}] - [{},{}]",
                                               data.data_window.min.x, data.data_window.min.y, data.data_window.max.x,
                                               data.data_window.max.y)};

        if (data.display_window.is_empty())
            throw invalid_argument{fmt::format("EXR image has invalid display window: [{},{}] - [{},{}]",
                                               data.display_window.min.x, data.display_window.min.y,
                                               data.display_window.max.x, data.display_window.max.y)};

        const auto &channels = part.header().channels();

        Imf::FrameBuffer framebuffer;
        for (auto c = channels.begin(); c != channels.end(); ++c)
        {
            string name = c.name();

            data.channels.emplace_back(name, size);
            framebuffer.insert(c.name(), Imf::Slice::Make(Imf::FLOAT, data.channels.back().data(), dataWindow, 0, 0,
                                                          c.channel().xSampling, c.channel().ySampling));
        }

        part.setFrameBuffer(framebuffer);
        part.readPixels(dataWindow.min.y, dataWindow.max.y);

        // now up-res any subsampled channels
        int i = 0;
        for (auto c = part.header().channels().begin(); c != part.header().channels().end(); ++c, ++i)
        {
            int xs = c.channel().xSampling;
            int ys = c.channel().ySampling;
            if (xs == 1 && ys == 1)
                continue;

            spdlog::warn("EXR channel '{}' is subsampled. Only rudimentary subsampling is supported.", c.name());
            Array2Df tmp = data.channels[i];

            int subsampled_width = size.x / xs;
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) data.channels[i]({x, y}) = tmp(x / xs + (y / ys) * subsampled_width);
        }

        if (Imf::hasWhiteLuminance(part.header()))
            spdlog::debug("File has white luminance info.");
        else
            spdlog::debug("File does NOT have white luminance info.");

        // If the file specifies a chromaticity attribute, we'll need to convert to sRGB/Rec709.
        if (Imf::hasChromaticities(part.header()))
        {
            Imf::Chromaticities rec709_cr; // default rec709 (sRGB) primaries
            Imf::Chromaticities file_cr = Imf::chromaticities(part.header());
            if (file_cr != rec709_cr)
            {
                // Imath matrices multiply row vectors to their left, so are read from left-to-right.
                // This transforms from the file's RGB to Rec.709 RGB (via XYZ)
                Imath::M44f M = Imf::RGBtoXYZ(file_cr, 1) * Imf::XYZtoRGB(rec709_cr, 1);

                for (int m = 0; m < 4; ++m)
                    for (int n = 0; n < 4; ++n) data.M_to_Rec709[m][n] = M.x[m][n];

                spdlog::info("Converting pixel values to Rec709/sRGB primaries and whitepoint.");
            }

            data.luminance_weights = float3{&Imf::RgbaYca::computeYw(file_cr)[0]};

            spdlog::debug("M_to_Rec709 = {}", data.M_to_Rec709);
            spdlog::debug("Yw = {}", data.luminance_weights);
        }
    }
    return images;
}

//
// end static location functions

vector<ImagePtr> Image::load(istream &is, const string &filename)
{
    spdlog::info("Loading from file: {}", filename);
    Timer timer;
    try
    {
        StdIStream exr_is{is, filename.c_str()};

        vector<ImagePtr> images;
        // try stb library first
        if (is_stb_image(is))
            images = load_stb_image(is, filename);
        // then try pfm
        else if (is_pfm_image(is))
            images = load_pfm_image(is, filename);
        else if (Imf::isOpenExrFile(exr_is))
            images = load_exr_image(exr_is, filename);
        else
            throw invalid_argument("This doesn't seem to be a supported image file.");

        for (auto i : images)
        {
            i->finalize();
            i->filename = filename;
            spdlog::info("Loaded image in {:f} seconds:\n{:s}", timer.elapsed() / 1000.f, i->to_string());
        }
        return images;
    }
    catch (const exception &e)
    {
        spdlog::error("Unable to read image file \"{}\":\n\t{}", filename, e.what());
    }
    return {};
}

vector<ImagePtr> Image::load(const string &filename)
{
    std::ifstream is{filename, std::ios_base::binary};
    return load(is, filename);
}

bool Image::save(ostream &os, const string &filename, float gain, float gamma, bool sRGB, bool dither) const
{
    static const auto ostream_write_func = [](void *context, void *data, int size)
    { reinterpret_cast<ostream *>(context)->write(reinterpret_cast<char *>(data), size); };

    string extension = to_lower(get_extension(filename));

    auto img = this;
    // HDRImage img_copy;

    bool hdr_format = (extension == "hdr") || (extension == "pfm") || (extension == "exr");

    // // if we need to tonemap, then modify a copy of the image data
    // if (gain != 1.0f || sRGB || gamma != 1.0f)
    // {
    //     Color4 gainC  = Color4(gain, gain, gain, 1.0f);
    //     Color4 gammaC = Color4(1.0f / gamma, 1.0f / gamma, 1.0f / gamma, 1.0f);

    //     img_copy = *this;
    //     img      = &img_copy;

    //     if (gain != 1.0f)
    //         img_copy *= gainC;

    //     // only do gamma or sRGB tonemapping if we are saving to an LDR format
    //     if (!hdr_format)
    //     {
    //         if (sRGB)
    //             img_copy = img_copy.apply_function([](const Color4 &c) { return LinearToSRGB(c); });
    //         else if (gamma != 1.0f)
    //             img_copy = img_copy.apply_function([&gammaC](const Color4 &c) { return pow(c, gammaC); });
    //     }
    // }

    // if (extension == "hdr")
    //     return stbi_write_hdr(filename.c_str(), width(), height(), 4, (const float *)img->data()) != 0;
    // else if (extension == "pfm")
    //     return write_pfm_image(filename.c_str(), width(), height(), 4, (const float *)img->data()) != 0;
    // else if (extension == "exr")
    // {
    //     try
    //     {
    //         Imf::setGlobalThreadCount(std::thread::hardware_concurrency());
    //         Imf::RgbaOutputFile     file(filename.c_str(), width(), height(), Imf::WRITE_RGBA);
    //         Imf::Array2D<Imf::Rgba> pixels(height(), width());

    //         Timer timer;
    //         // copy image data over to Rgba pixels
    //         parallel_for(0, height(),
    //                      [this, img, &pixels](int y)
    //                      {
    //                          for (int x = 0; x < width(); ++x)
    //                          {
    //                              Imf::Rgba &p = pixels[y][x];
    //                              Color4     c = (*img)(x, y);
    //                              p.r          = c[0];
    //                              p.g          = c[1];
    //                              p.b          = c[2];
    //                              p.a          = c[3];
    //                          }
    //                      });
    //         spdlog::debug("Copying pixel data took: {} seconds.", (timer.lap() / 1000.f));

    //         file.setFrameBuffer(&pixels[0][0], 1, width());
    //         file.writePixels(height());

    //         spdlog::debug("Writing EXR image took: {} seconds.", (timer.lap() / 1000.f));
    //         return true;
    //     }
    //     catch (const exception &e)
    //     {
    //         spdlog::error("Unable to write image file \"{}\": {}", filename, e.what());
    //         return false;
    //     }
    // }
    // else
    {
        // convert floating-point image to 8-bit per channel with dithering
        int                   n = groups[selected_group].num_channels;
        int                   w = size().x;
        int                   h = size().y;
        vector<unsigned char> data(w * h * n, 0);

        gamma = 1.f / gamma;
        Timer timer;
        // convert 3-channel pfm data to 4-channel internal representation
        parallel_for(0, h,
                     [this, img, &data, gain, gamma, sRGB, dither, w, n](int y)
                     {
                         for (int x = 0; x < w; ++x)
                         {
                             int   xmod = x % 256;
                             int   ymod = y % 256;
                             float d = dither ? (dither_matrix256[xmod + ymod * 256] / 65536.0f - 0.5f) / 255.0f : 0.f;

                             for (int c = 0; c < n; ++c)
                             {
                                 const Channel &chan = img->channels[img->groups[selected_group].channels[c]];
                                 float          v    = gain * chan(x, y);
                                 if (sRGB)
                                     v = LinearToSRGB(v);
                                 else if (gamma != 1.0f)
                                     v = pow(v, gamma);

                                 // unpremultiply
                                 if (n > 3 && c < 3)
                                 {
                                     float a = img->channels[img->groups[selected_group].channels[3]](x, y);
                                     if (a != 0.f)
                                         v /= a;
                                 }

                                 if (c < 3)
                                     v += d;

                                 // convert to [0-255] range
                                 v                           = clamp(v * 255.0f, 0.0f, 255.0f);
                                 data[n * x + n * y * w + c] = (unsigned char)v;
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
            throw invalid_argument(
                fmt::format("Could not determine desired file type from extension \"{}\".", extension));
    }
}

bool Image::save(const string &filename, float gain, float gamma, bool sRGB, bool dither) const
{
    std::ofstream os{filename, std::ios_base::binary};
    return save(os, filename, gain, gamma, sRGB, dither);
}