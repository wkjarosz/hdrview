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

#include "ultrahdr_api.h"

using namespace std;

// static methods and member definitions
//

const float3 Image::Rec709_luminance_weights = float3{&Imf::RgbaYca::computeYw(Imf::Chromaticities{})[0]};

//
// end static methods and member definitions

// static local functions
//

////////////////////////////////////////////////////////////////////////////////
// Color space conversions
// Sample, See,
// https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html#_bt_709_bt_2020_primary_conversion_example

const float4x4 kP3ToBt709{{1.22494f, -0.042057f, -0.019638f, 0.f},
                          {-0.22494f, 1.042057f, -0.078636f, 0.f},
                          {0.0f, 0.0f, 1.098274f, 0.f},
                          {0.0f, 0.0f, 0.f, 1.f}};

const float4x4 kBt2100ToBt709{{1.660491f, -0.124551f, -0.018151f, 0.f},
                              {-0.587641f, 1.1329f, -0.100579f, 0.f},
                              {-0.07285f, -0.008349f, 1.11873f, 0.f},
                              {0.0f, 0.0f, 0.f, 1.f}};

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

static bool is_uhdr_image(std::istream &is)
{
    if (!is.good())
        return false;

    bool ret = false;
    try
    {
        // calculate size of stream
        is.seekg(0, std::ios::end);
        size_t size = (size_t)is.tellg();
        is.seekg(0, std::ios::beg);
        if (size <= 0)
            throw invalid_argument{"Stream is empty"};

        // allocate memory to store contents of file and read it in
        std::unique_ptr<char[]> data(new char[size]);
        is.read(reinterpret_cast<char *>(data.get()), size);

        if ((size_t)is.gcount() != size)
            throw invalid_argument{
                fmt::format("Failed to read : {} bytes, read : {} bytes", size, (size_t)is.gcount())};

        // we could just call ::is_uhdr_image now, but we want to report the error in case this is not a uhdr image
        // ret = ::is_uhdr_image(data.get(), size);

        auto throw_if_error = [](uhdr_error_info_t status)
        {
            if (status.error_code != UHDR_CODEC_OK)
                throw invalid_argument(fmt::format("UltraHDR: Error decoding image: {}", status.detail));
        };

        using Decoder = std::unique_ptr<uhdr_codec_private_t, void (&)(uhdr_codec_private_t *)>;
        auto decoder  = Decoder{uhdr_create_decoder(), uhdr_release_decoder};

        uhdr_compressed_image_t compressed_image{
            data.get(),          /**< Pointer to a block of data to decode */
            size,                /**< size of the data buffer */
            size,                /**< maximum size of the data buffer */
            UHDR_CG_UNSPECIFIED, /**< Color Gamut */
            UHDR_CT_UNSPECIFIED, /**< Color Transfer */
            UHDR_CR_UNSPECIFIED  /**< Color Range */
        };

        throw_if_error(uhdr_dec_set_image(decoder.get(), &compressed_image));
        throw_if_error(uhdr_dec_probe(decoder.get()));

        ret = true;
    }
    catch (const std::exception &e)
    {
        spdlog::debug("Cannot load image with UltraHDR: {}", e.what());
        ret = false;
    }

    // rewind
    is.clear();
    is.seekg(0);
    return ret;
}

static vector<ImagePtr> load_uhdr_image(std::istream &is, const string &filename)
{
    if (!is.good())
        throw invalid_argument("UltraHDR: invalid file stream.");

    using Decoder = std::unique_ptr<uhdr_codec_private_t, void (&)(uhdr_codec_private_t *)>;
    auto decoder  = Decoder{uhdr_create_decoder(), uhdr_release_decoder};

    auto throw_if_error = [](uhdr_error_info_t status)
    {
        if (status.error_code != UHDR_CODEC_OK)
            throw invalid_argument(fmt::format("UltraHDR: Error decoding image: {}", status.detail));
    };

    {
        // calculate size of stream
        is.seekg(0, std::ios::end);
        size_t size = (size_t)is.tellg();
        is.seekg(0, std::ios::beg);
        if (size <= 0)
            throw invalid_argument{fmt::format("File '{}' is empty", filename)};

        // allocate memory to store contents of file and read it in
        std::unique_ptr<char[]> data(new char[size]);
        is.read(reinterpret_cast<char *>(data.get()), size);

        if ((size_t)is.gcount() != size)
            throw invalid_argument{
                fmt::format("UltraHDR: Failed to read : {} bytes, read : {} bytes", size, (size_t)is.gcount())};

        uhdr_compressed_image_t compressed_image{
            data.get(),          /**< Pointer to a block of data to decode */
            size,                /**< size of the data buffer */
            size,                /**< maximum size of the data buffer */
            UHDR_CG_UNSPECIFIED, /**< Color Gamut */
            UHDR_CT_UNSPECIFIED, /**< Color Transfer */
            UHDR_CR_UNSPECIFIED  /**< Color Range */
        };
        throw_if_error(uhdr_dec_set_image(decoder.get(), &compressed_image));
        throw_if_error(uhdr_dec_set_out_color_transfer(decoder.get(), UHDR_CT_LINEAR));
        throw_if_error(uhdr_dec_set_out_img_format(decoder.get(), UHDR_IMG_FMT_64bppRGBAHalfFloat));
        throw_if_error(uhdr_dec_probe(decoder.get()));
        spdlog::debug("UltraHDR: base image: {}x{}", uhdr_dec_get_image_width(decoder.get()),
                      uhdr_dec_get_image_height(decoder.get()));
        throw_if_error(uhdr_decode(decoder.get()));
        // going out of scope deallocate contents of data
    }

    uhdr_raw_image_t *decoded_image = uhdr_get_decoded_image(decoder.get()); // freed by decoder destructor
    if (!decoded_image)
        throw invalid_argument{"UltraHDR: Decode image failed."};
    if (decoded_image->fmt != UHDR_IMG_FMT_64bppRGBAHalfFloat)
        throw invalid_argument{"UltraHDR: Unexpected output format."};

    spdlog::debug("UltraHDR: base image: {}x{}; stride: {}; cg: {}; ct: {}; range: {}", decoded_image->w,
                  decoded_image->h, decoded_image->stride[UHDR_PLANE_PACKED], (int)decoded_image->cg,
                  (int)decoded_image->ct, (int)decoded_image->range);

    int2 size = int2(decoded_image->w, decoded_image->h);

    auto image      = make_shared<Image>(size, 4);
    image->filename = filename;

    size_t block_size = std::max(1u, 1024u * 1024u / decoded_image->w);
    parallel_for(blocked_range<int>(0, decoded_image->h, block_size),
                 [&image, decoded_image](int begin_y, int end_y, int unit_index, int thread_index)
                 {
                     auto data = reinterpret_cast<char *>(decoded_image->planes[UHDR_PLANE_PACKED]);
                     for (int y = begin_y; y < end_y; ++y)
                     {
                         auto scanline =
                             reinterpret_cast<::half *>(data + y * decoded_image->stride[UHDR_PLANE_PACKED] * 8);
                         for (unsigned x = 0; x < decoded_image->w; ++x)
                         {
                             image->channels[0](x, y) = scanline[x * 4 + 0];
                             image->channels[1](x, y) = scanline[x * 4 + 1];
                             image->channels[2](x, y) = scanline[x * 4 + 2];
                             image->channels[3](x, y) = scanline[x * 4 + 3];
                         }
                     }
                 });

    // HDRView assumes the Rec 709 primaries/gamut. Set the matrix to convert to it
    if (decoded_image->cg == UHDR_CG_DISPLAY_P3)
    {
        image->M_to_Rec709 = kP3ToBt709;
        spdlog::info("Converting pixel values to Rec. 709/sRGB primaries and whitepoint from Display P3.");
    }
    else if (decoded_image->cg == UHDR_CG_BT_2100)
    {
        image->M_to_Rec709 = kBt2100ToBt709;
        spdlog::info("Converting pixel values to Rec. 709/sRGB primaries and whitepoint from Rec. 2100.");
    }

    uhdr_raw_image_t *gainmap = uhdr_get_decoded_gainmap_image(decoder.get()); // freed by decoder destructor
    int2              gainmap_size{gainmap->w, gainmap->h};

    spdlog::debug("UltraHDR: gainmap image: {}x{}; stride: {}; cg: {}; ct: {}; range: {}", gainmap->w, gainmap->h,
                  gainmap->stride[UHDR_PLANE_PACKED], (int)gainmap->cg, (int)gainmap->ct, (int)gainmap->range);

    // if the gainmap is an unexpected size or format, we are done
    if ((gainmap_size.x > size.x || gainmap_size.y > size.y) ||
        (gainmap->fmt != UHDR_IMG_FMT_32bppRGBA8888 && gainmap->fmt != UHDR_IMG_FMT_8bppYCbCr400 &&
         gainmap->fmt != UHDR_IMG_FMT_24bppRGB888))
        return {image};

    // otherwise, extract the gain map as a separate channel group

    int num_components =
        gainmap->fmt == UHDR_IMG_FMT_32bppRGBA8888 ? 4 : (gainmap->fmt == UHDR_IMG_FMT_24bppRGB888 ? 3 : 1);

    if (num_components == 1)
        image->channels.emplace_back("gainmap.Y", size);
    if (num_components >= 3)
    {
        image->channels.emplace_back("gainmap.R", size);
        image->channels.emplace_back("gainmap.G", size);
        image->channels.emplace_back("gainmap.B", size);
    }
    if (num_components == 4)
        image->channels.emplace_back("gainmap.A", size);

    block_size = std::max(1u, 1024u * 1024u / gainmap->w);
    parallel_for(blocked_range<int>(0, gainmap->h, block_size),
                 [&image, gainmap, num_components](int begin_y, int end_y, int unit_index, int thread_index)
                 {
                     // gainmap->planes contains interleaved, 8bit grainmap channels
                     auto data = reinterpret_cast<uint8_t *>(gainmap->planes[UHDR_PLANE_PACKED]);
                     // Copy a block of values into each of the separate channels in image
                     for (int y = begin_y; y < end_y; ++y)
                     {
                         auto scanline = reinterpret_cast<uint8_t *>(data + y * gainmap->stride[UHDR_PLANE_PACKED] *
                                                                                num_components);
                         for (unsigned x = 0; x < gainmap->w; ++x)
                             for (int c = 0; c < num_components; ++c)
                             {
                                 uint8_t v                    = scanline[x * num_components + c];
                                 float   d                    = 0.5f;
                                 image->channels[4 + c](x, y) = SRGBToLinear((v + d) / 256.0f);
                             }
                     }
                 });

    // resize the data in the channels if necessary
    if (gainmap_size.x < size.x && gainmap_size.y < size.y)
    {
        int xs = size.x / gainmap_size.x;
        int ys = size.x / gainmap_size.x;
        spdlog::debug("Resizing gainmap resolution {}x{} by factor {}x{} to match image resolution {}x{}.",
                      gainmap_size.x, gainmap_size.y, xs, ys, size.x, size.y);
        for (int c = 0; c < num_components; ++c)
        {
            Array2Df tmp = image->channels[4 + c];

            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) image->channels[4 + c](x, y) = tmp(x / xs, y / ys);
        }
    }

    return {image};
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
        auto &data = *images.back();

        if (part.header().hasName())
            data.partname = part.header().name();
        if (auto owner = part.header().findTypedAttribute<Imf::StringAttribute>("owner"))
            data.owner = owner->value();
        if (auto comments = part.header().findTypedAttribute<Imf::StringAttribute>("comments"))
            data.comments = comments->value();
        if (auto capture_date = part.header().findTypedAttribute<Imf::StringAttribute>("capDate"))
            data.capture_date = capture_date->value();

        // OpenEXR library's boxes include the max element, our boxes don't, so we increment by 1
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
        // FIXME: OpenEXR v3.3.0 and above seems to break this subsample channel loading
        // see https://github.com/AcademySoftwareFoundation/openexr/issues/1949
        // Until that is fixed in the next release, we are sticking with v3.2.4
        int i = 0;
        for (auto c = part.header().channels().begin(); c != part.header().channels().end(); ++c, ++i)
        {
            int xs = c.channel().xSampling;
            int ys = c.channel().ySampling;
            if (xs == 1 && ys == 1)
                continue;

            spdlog::warn("EXR channel '{}' is subsampled ({},{}). Only rudimentary subsampling is supported.", c.name(),
                         xs, ys);
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

                spdlog::info("Converting pixel values to Rec. 709/sRGB primaries and whitepoint.");
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

        if (Imf::isOpenExrFile(exr_is))
        {
            spdlog::info("Detected EXR image.");
            images = load_exr_image(exr_is, filename);
        }
        else if (is_uhdr_image(is))
        {
            spdlog::info("Detected UltraHDR JPEG image. Loading via libultrahdr.");
            images = load_uhdr_image(is, filename);
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
            i->finalize();
            i->filename   = filename;
            i->short_name = i->file_and_partname();
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

    // bool hdr_format = (extension == "hdr") || (extension == "pfm") || (extension == "exr");

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
        int             n = groups[selected_group].num_channels;
        int             w = size().x;
        int             h = size().y;
        vector<uint8_t> data(w * h * n, 0);

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
            throw invalid_argument(
                fmt::format("Could not determine desired file type from extension \"{}\".", extension));
    }
}

bool Image::save(const string &filename, float gain, float gamma, bool sRGB, bool dither) const
{
    std::ofstream os{filename, std::ios_base::binary};
    return save(os, filename, gain, gamma, sRGB, dither);
}