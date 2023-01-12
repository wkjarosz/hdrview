//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrimage.h"
#include "hdrimageraw.h"
#include "parallelfor.h"
#include "timer.h"
#include <ImathMatrix.h>
#include <spdlog/spdlog.h>
#include <stdexcept> // for runtime_error, out_of_range
#include <string>    // for allocator, operator==, basic_string
#include <vector>    // for vector

// these pragmas ignore warnings about unused static functions
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

// since NanoVG includes an old version of stb_image, we declare it static here
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION

#define TINY_DNG_LOADER_IMPLEMENTATION
#include "tiny_dng_loader.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

using namespace nanogui;
using namespace std;
using Imath::M33f;
using Imath::V2f;
using Imath::V3f;

// local functions
namespace
{

inline unsigned short endianSwap(unsigned short val);
void     decode12BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian);
void     decode14BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian);
void     decode16BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian);
void     printImageInfo(const tinydng::DNGImage &image);
HDRImage develop(vector<float> &raw, const tinydng::DNGImage &param1, const tinydng::DNGImage &param2);

} // namespace

void HDRImage::load_dng(const string &filename)
{
    vector<tinydng::DNGImage> images;
    {
        std::string                err, warn;
        vector<tinydng::FieldInfo> customFields;
        bool                       ret = tinydng::LoadDNG(filename.c_str(), customFields, &images, &err, &warn);

        if (ret == false)
            throw runtime_error("Failed to load DNG. " + err);
    }

    // DNG files sometimes only store the orientation in one of the images,
    // instead of all of them. find any set value and save it
    int orientation = 0;
    for (size_t i = 0; i < images.size(); i++)
    {
        spdlog::debug("Image [{}] size = {} x {}.", i, images[i].width, images[i].height);
        spdlog::debug("Image [{}] orientation = {}", i, images[i].orientation);
        if (images[i].orientation != 0)
            orientation = images[i].orientation;
    }

    // Find largest image based on width.
    size_t imageIndex = size_t(-1);
    {
        size_t largest      = 0;
        int    largestWidth = images[0].width;
        for (size_t i = 0; i < images.size(); i++)
        {
            if (largestWidth < images[i].width)
            {
                largest      = i;
                largestWidth = images[i].width;
            }
        }

        imageIndex = largest;
    }
    tinydng::DNGImage &image = images[imageIndex];

    spdlog::debug("\nLargest image within DNG:");
    printImageInfo(image);
    spdlog::debug("\nLast image within DNG:");
    printImageInfo(images.back());

    spdlog::debug("Loading image [{}].", imageIndex);

    int w = image.width;
    int h = image.height;

    // Convert to float.
    vector<float> hdr;
    bool          endianSwap = false; // TODO

    int spp = image.samples_per_pixel;
    if (image.bits_per_sample == 12)
        decode12BitToFloat(hdr, &(image.data.at(0)), w, h * spp, endianSwap);
    else if (image.bits_per_sample == 14)
        decode14BitToFloat(hdr, &(image.data.at(0)), w, h * spp, endianSwap);
    else if (image.bits_per_sample == 16)
        decode16BitToFloat(hdr, &(image.data.at(0)), w, h * spp, endianSwap);
    else
        throw runtime_error("Error loading DNG: Unsupported bits_per_sample : " + to_string(spp));

    float inv_scale = 1.0f / static_cast<float>((1 << image.bits_per_sample));
    if (spp == 3)
    {
        spdlog::debug("Decoding a 3 sample-per-pixel DNG image.");
        // normalize
        parallel_for(0, hdr.size(), [&hdr, inv_scale](int i) { hdr[i] *= inv_scale; });

        // Create color image & normalize intensity.
        resize(w, h);

        Timer timer;
        // normalize
        parallel_for(0, h,
                     [this, w, inv_scale, &hdr](int y)
                     {
                         for (int x = 0; x < w; ++x)
                         {
                             int index     = 3 * y * w + x;
                             (*this)(x, y) = Color4(hdr[index] * inv_scale + 0, hdr[index] * inv_scale + 1,
                                                    hdr[index] * inv_scale + 2, 1.0f);
                         }
                     });
        spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed() / 1000.f));
    }
    else if (spp == 1)
    {
        // Create grayscale image & normalize intensity.
        spdlog::debug("Decoding a 1 sample-per-pixel DNG image.");
        Timer timer;
        *this = develop(hdr, image, images.back());
        spdlog::debug("Copying image data took: {} seconds.", (timer.elapsed() / 1000.f));
    }
    else
        throw runtime_error("Error loading DNG: Unsupported samples per pixel: " + to_string(spp));

    int start_x = ::clamp(image.active_area[1], 0, w);
    int end_x   = ::clamp(image.active_area[3], 0, w);
    int start_y = ::clamp(image.active_area[0], 0, h);
    int end_y   = ::clamp(image.active_area[2], 0, h);

    // FIXME
    // *this = block(start_x, start_y,
    //               end_x-start_x,
    //               end_y-start_y).eval();
    HDRImage copy(end_x - start_x, end_y - start_y);
    copy.copy_paste(*this, Box2i(Vector2i(start_x, start_y), Vector2i(end_x, end_y)), 0, 0);
    *this = copy;

    enum Orientations
    {
        ORIENTATION_TOPLEFT  = 1,
        ORIENTATION_TOPRIGHT = 2,
        ORIENTATION_BOTRIGHT = 3,
        ORIENTATION_BOTLEFT  = 4,
        ORIENTATION_LEFTTOP  = 5,
        ORIENTATION_RIGHTTOP = 6,
        ORIENTATION_RIGHTBOT = 7,
        ORIENTATION_LEFTBOT  = 8
    };

    // now rotate image based on stored orientation
    switch (orientation)
    {
    case ORIENTATION_TOPRIGHT: *this = flipped_horizontal(); break;
    case ORIENTATION_BOTRIGHT: *this = flipped_vertical().flipped_horizontal(); break;
    case ORIENTATION_BOTLEFT: *this = flipped_vertical(); break;
    case ORIENTATION_LEFTTOP: *this = rotated_90_ccw().flipped_vertical(); break;
    case ORIENTATION_RIGHTTOP: *this = rotated_90_cw(); break;
    case ORIENTATION_RIGHTBOT: *this = rotated_90_cw().flipped_vertical(); break;
    case ORIENTATION_LEFTBOT: *this = rotated_90_ccw(); break;
    default: break; // none (0), or ORIENTATION_TOPLEFT
    }
}

// /*!
//  * \brief Reduce some remaining color fringing and zipper artifacts by median-filtering the
//  * red-green and blue-green differences as originally proposed by Freeman.
//  */
// HDRImage HDRImage::median_filter_bayer_artifacts() const
// {
//     AtomicProgress progress;
//     HDRImage color_diff = apply_function([](const Color4 & c){return Color4(c.r-c.g,c.g,c.b-c.g,c.a);});
//     color_diff = color_diff.median_filtered(1.f, 0, AtomicProgress(progress, .5f))
//                            .median_filtered(1.f, 2, AtomicProgress(progress, .5f));
//     return apply_function(color_diff, [](const Color4 & i, const Color4 & med){return Color4(med.r + i.g, i.g, med.b
//     + i.g, i.a);});
// }

// local functions
namespace
{

// Taken from http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
// const M33f XYZ_D65_to_sRGB(3.2406f, -1.5372f, -0.4986f, -0.9689f, 1.8758f, 0.0415f, 0.0557f, -0.2040f, 1.0570f);

const M33f XYZ_D50_to_XYZ_D65(0.9555766f, -0.0230393f, 0.0631636f, -0.0282895f, 1.0099416f, 0.0210077f, 0.0122982f,
                              -0.0204830f, 1.3299098f);

// Taken from http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
const M33f XYZ_D50_to_sRGB(3.2404542f, -1.5371385f, -0.4985314f, -0.9692660f, 1.8760108f, 0.0415560f, 0.0556434f,
                           -0.2040259f, 1.0572252);

M33f compute_camera_to_XYZ_D50(const tinydng::DNGImage &param)
{
    //
    // The full DNG color-correction model is described in the
    // "Mapping Camera Color Space to CIE XYZ Space" section of the DNG spec.
    //
    // Let n be the dimensionality of the camera color space (usually 3 or 4).
    // Let CM be the n-by-3 matrix interpolated from the ColorMatrix1 and ColorMatrix2 tags.
    // Let CC be the n-by-n matrix interpolated from the CameraCalibration1 and CameraCalibration2 tags (or identity
    // matrices, if the signatures don't match). Let AB be the n-by-n matrix, which is zero except for the diagonal
    // entries, which are defined by the AnalogBalance tag. Let RM be the 3-by-n matrix interpolated from the
    // ReductionMatrix1 and ReductionMatrix2 tags. Let FM be the 3-by-n matrix interpolated from the ForwardMatrix1 and
    // ForwardMatrix2 tags.

    // TODO: the color correction code below is not quite correct

    // if the ForwardMatrix is included:

    // FIXME: need matrix class with inverse
    if (false) // param.has_forward_matrix2)
    {
        M33f FM(param.forward_matrix2[0][0], param.forward_matrix2[0][1], param.forward_matrix2[0][2],
                param.forward_matrix2[1][0], param.forward_matrix2[1][1], param.forward_matrix2[1][2],
                param.forward_matrix2[2][0], param.forward_matrix2[2][1], param.forward_matrix2[2][2]);
        M33f CC(param.camera_calibration2[0][0], param.camera_calibration2[0][1], param.camera_calibration2[0][2],
                param.camera_calibration2[1][0], param.camera_calibration2[1][1], param.camera_calibration2[1][2],
                param.camera_calibration2[2][0], param.camera_calibration2[2][1], param.camera_calibration2[2][2]);
        M33f AB;
        AB.x[0][0] = param.analog_balance[0];
        AB.x[1][1] = param.analog_balance[1];
        AB.x[2][2] = param.analog_balance[2];

        V3f  CameraNeutral(param.as_shot_neutral[0], param.as_shot_neutral[1], param.as_shot_neutral[2]);
        M33f ABCC             = (AB * CC).inverse();
        V3f  ReferenceNeutral = CameraNeutral * ABCC;
        M33f D;
        D.x[0][0] = 1.f / ReferenceNeutral[0];
        D.x[1][1] = 1.f / ReferenceNeutral[1];
        D.x[2][2] = 1.f / ReferenceNeutral[2];

        return FM * D * ABCC;
    }
    else
    {
        M33f CM(param.color_matrix2[0][0], param.color_matrix2[0][1], param.color_matrix2[0][2],
                param.color_matrix2[1][0], param.color_matrix2[1][1], param.color_matrix2[1][2],
                param.color_matrix2[2][0], param.color_matrix2[2][1], param.color_matrix2[2][2]);

        return CM.inverse();
    }
}

HDRImage develop(vector<float> &raw, const tinydng::DNGImage &param1, const tinydng::DNGImage &param2)
{
    Timer timer;

    int      width       = param1.width;
    int      height      = param1.height;
    int      black_level = param1.black_level[0];
    int      white_level = param1.white_level[0];
    Vector2i red_offset(param1.active_area[1] % 2, param1.active_area[0] % 2);

    HDRImage developed(width, height);

    M33f camera_to_XYZ_D50 = compute_camera_to_XYZ_D50(param2);
    M33f camera_to_sRGB    = XYZ_D50_to_sRGB * camera_to_XYZ_D50;

    // Chapter 5 of DNG spec
    // Map raw values to linear reference values (i.e. adjust for black and white level)
    //
    // we also apply white balance before demosaicing here because it increases the
    // correlation between the color channels and reduces artifacts
    V3f         wb(param2.as_shot_neutral[0], param2.as_shot_neutral[1], param2.as_shot_neutral[2]);
    const float inv_scale = 1.0f / (white_level - black_level);
    parallel_for(0, developed.height(),
                 [&developed, &raw, black_level, inv_scale, &wb](int y)
                 {
                     for (int x = 0; x < developed.width(); x++)
                     {
                         float v = ::clamp((raw[y * developed.width() + x] - black_level) * inv_scale, 0.f, 1.f);
                         V3f   rgb(v, v, v);
                         rgb             = rgb / wb;
                         developed(x, y) = Color4(rgb.x, rgb.y, rgb.z, 1.f);
                     }
                 });

    //
    // demosaic
    //

    // demosaic_linear(developed, red_offset)
    // demosaic_green_guided_linear(developed, red_offset);
    // demosaic_Malvar(developed, red_offset)
    demosaic_AHD(developed, red_offset, XYZ_D50_to_XYZ_D65 * camera_to_XYZ_D50);

    // color correction
    // also undo the white balance since the color correction matrix already includes it
    parallel_for(0, developed.height(),
                 [&developed, &camera_to_sRGB, &wb](int y)
                 {
                     for (int x = 0; x < developed.width(); x++)
                     {
                         V3f rgb(developed(x, y).r, developed(x, y).g, developed(x, y).b);
                         rgb             = rgb * wb;
                         V3f sRGB        = rgb * camera_to_sRGB;
                         developed(x, y) = Color4(sRGB.x, sRGB.y, sRGB.z, 1.f);
                     }
                 });

    spdlog::debug("Developing DNG image took {} seconds.", (timer.elapsed() / 1000.f));
    return developed;
}

inline unsigned short endianSwap(unsigned short val)
{
    unsigned short ret;

    unsigned char *buf = reinterpret_cast<unsigned char *>(&ret);

    unsigned short x = val;
    buf[1]           = static_cast<unsigned char>(x);
    buf[0]           = static_cast<unsigned char>(x >> 8);

    return ret;
}

// The decode functions below are adapted from syoyo's dng2exr, in the tinydng library within the
// ext subfolder

//
// Decode 12bit integer image into floating point HDR image
//
void decode12BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian)
{
    Timer timer;

    int offsets[2][2] = {{0, 1}, {1, 2}};
    int bitShifts[2]  = {4, 0};

    image.resize(static_cast<size_t>(width * height));

    parallel_for(0, height,
                 [&image, width, &offsets, &bitShifts, data, swapEndian](int y)
                 {
                     for (int x = 0; x < width; x++)
                     {
                         unsigned char buf[3];

                         // Calculate load address for 12bit pixel(three 8 bit pixels)
                         int n = int(y * width + x);

                         // 24 = 12bit * 2 pixel, 8bit * 3 pixel
                         int n2    = n % 2;       // used for offset & bitshifts
                         int addr3 = (n / 2) * 3; // 8bit pixel pos
                         int odd   = (addr3 % 2);

                         int bit_shift;
                         bit_shift = bitShifts[n2];

                         int offset[2];
                         offset[0] = offsets[n2][0];
                         offset[1] = offsets[n2][1];

                         if (swapEndian)
                         {
                             // load with short byte swap
                             if (odd)
                             {
                                 buf[0] = data[addr3 - 1];
                                 buf[1] = data[addr3 + 2];
                                 buf[2] = data[addr3 + 1];
                             }
                             else
                             {
                                 buf[0] = data[addr3 + 1];
                                 buf[1] = data[addr3 + 0];
                                 buf[2] = data[addr3 + 3];
                             }
                         }
                         else
                         {
                             buf[0] = data[addr3 + 0];
                             buf[1] = data[addr3 + 1];
                             buf[2] = data[addr3 + 2];
                         }
                         unsigned int b0 = static_cast<unsigned int>(buf[offset[0]] & 0xff);
                         unsigned int b1 = static_cast<unsigned int>(buf[offset[1]] & 0xff);

                         unsigned int val = (b0 << 8) | b1;
                         val              = 0xfff & (val >> bit_shift);

                         image[static_cast<size_t>(y * width + x)] = static_cast<float>(val);
                     }
                 });

    spdlog::debug("decode12BitToFloat took: {} seconds.", (timer.lap() / 1000.f));
}

//
// Decode 14bit integer image into floating point HDR image
//
void decode14BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian)
{
    Timer timer;

    int offsets[4][3] = {{0, 0, 1}, {1, 2, 3}, {3, 4, 5}, {5, 5, 6}};
    int bitShifts[4]  = {2, 4, 6, 0};

    image.resize(static_cast<size_t>(width * height));

    parallel_for(0, height,
                 [&image, width, &offsets, &bitShifts, data, swapEndian](int y)
                 {
                     for (int x = 0; x < width; x++)
                     {
                         unsigned char buf[7];

                         // Calculate load address for 14bit pixel(three 8 bit pixels)
                         int n = int(y * width + x);

                         // 56 = 14bit * 4 pixel, 8bit * 7 pixel
                         int n4    = n % 4;       // used for offset & bitshifts
                         int addr7 = (n / 4) * 7; // 8bit pixel pos
                         int odd   = (addr7 % 2);

                         int offset[3];
                         offset[0] = offsets[n4][0];
                         offset[1] = offsets[n4][1];
                         offset[2] = offsets[n4][2];

                         int bit_shift;
                         bit_shift = bitShifts[n4];

                         if (swapEndian)
                         {
                             // load with short byte swap
                             if (odd)
                             {
                                 buf[0] = data[addr7 - 1];
                                 buf[1] = data[addr7 + 2];
                                 buf[2] = data[addr7 + 1];
                                 buf[3] = data[addr7 + 4];
                                 buf[4] = data[addr7 + 3];
                                 buf[5] = data[addr7 + 6];
                                 buf[6] = data[addr7 + 5];
                             }
                             else
                             {
                                 buf[0] = data[addr7 + 1];
                                 buf[1] = data[addr7 + 0];
                                 buf[2] = data[addr7 + 3];
                                 buf[3] = data[addr7 + 2];
                                 buf[4] = data[addr7 + 5];
                                 buf[5] = data[addr7 + 4];
                                 buf[6] = data[addr7 + 7];
                             }
                         }
                         else
                         {
                             memcpy(buf, &data[addr7], 7);
                         }
                         unsigned int b0 = static_cast<unsigned int>(buf[offset[0]] & 0xff);
                         unsigned int b1 = static_cast<unsigned int>(buf[offset[1]] & 0xff);
                         unsigned int b2 = static_cast<unsigned int>(buf[offset[2]] & 0xff);

                         // unsigned int val = (b0 << 16) | (b1 << 8) | b2;
                         // unsigned int val = (b2 << 16) | (b0 << 8) | b0;
                         unsigned int val = (b0 << 16) | (b1 << 8) | b2;
                         // unsigned int val = b2;
                         val = 0x3fff & (val >> bit_shift);

                         image[static_cast<size_t>(y * width + x)] = static_cast<float>(val);
                     }
                 });

    spdlog::debug("decode14BitToFloat took: {} seconds.", (timer.lap() / 1000.f));
}

//
// Decode 16bit integer image into floating point HDR image
//
void decode16BitToFloat(vector<float> &image, unsigned char *data, int width, int height, bool swapEndian)
{
    Timer timer;

    image.resize(static_cast<size_t>(width * height));
    unsigned short *ptr = reinterpret_cast<unsigned short *>(data);

    parallel_for(0, height,
                 [&image, width, ptr, swapEndian](int y)
                 {
                     for (int x = 0; x < width; x++)
                     {
                         unsigned short val = ptr[y * width + x];
                         if (swapEndian)
                             val = endianSwap(val);

                         // range will be [0, 65535]
                         image[static_cast<size_t>(y * width + x)] = static_cast<float>(val);
                     }
                 });

    spdlog::debug("decode16BitToFloat took: {} seconds.", (timer.lap() / 1000.f));
}

char get_colorname(int c)
{
    switch (c)
    {
    case 0: return 'R';
    case 1: return 'G';
    case 2: return 'B';
    case 3: return 'C';
    case 4: return 'M';
    case 5: return 'Y';
    case 6: return 'W';
    default: return '?';
    }
}

void printImageInfo(const tinydng::DNGImage &image)
{
    spdlog::debug("width = {}.", image.width);
    spdlog::debug("height = {}.", image.height);
    spdlog::debug("bits per pixel = {}.", image.bits_per_sample);
    spdlog::debug("bits per pixel(original) = {}", image.bits_per_sample_original);
    spdlog::debug("samples per pixel = {}", image.samples_per_pixel);
    spdlog::debug("sample format = {}", image.sample_format);

    spdlog::debug("version = {}", image.version);

    for (int s = 0; s < image.samples_per_pixel; s++)
    {
        spdlog::debug("white_level[{}] = {}", s, image.white_level[s]);
        spdlog::debug("black_level[{}] = {}", s, image.black_level[s]);
    }

    spdlog::debug("tile_width = {}", image.tile_width);
    spdlog::debug("tile_length = {}", image.tile_length);
    spdlog::debug("tile_offset = {}", image.tile_offset);
    spdlog::debug("tile_offset = {}", image.tile_offset);

    spdlog::debug("cfa_layout = {}", image.cfa_layout);
    spdlog::debug("cfa_plane_color = {}{}{}{}", get_colorname(image.cfa_plane_color[0]),
                  get_colorname(image.cfa_plane_color[1]), get_colorname(image.cfa_plane_color[2]),
                  get_colorname(image.cfa_plane_color[3]));
    spdlog::debug("cfa_pattern[2][2] = \n {}, {},\n {}, {}", image.cfa_pattern[0][0], image.cfa_pattern[0][1],
                  image.cfa_pattern[1][0], image.cfa_pattern[1][1]);

    spdlog::debug("active_area = \n {}, {},\n {}, {}", image.active_area[0], image.active_area[1], image.active_area[2],
                  image.active_area[3]);

    spdlog::debug("calibration_illuminant1 = {}", image.calibration_illuminant1);
    spdlog::debug("calibration_illuminant2 = {}", image.calibration_illuminant2);

    spdlog::debug("color_matrix1 = ");
    for (size_t k = 0; k < 3; k++)
        spdlog::debug("{} {} {}", image.color_matrix1[k][0], image.color_matrix1[k][1], image.color_matrix1[k][2]);

    spdlog::debug("color_matrix2 = ");
    for (size_t k = 0; k < 3; k++)
        spdlog::debug("{} {} {}", image.color_matrix2[k][0], image.color_matrix2[k][1], image.color_matrix2[k][2]);

    if (true) // image.has_forward_matrix2)
    {
        spdlog::debug("forward_matrix1 found = ");
        for (size_t k = 0; k < 3; k++)
            spdlog::debug("{} {} {}", image.forward_matrix1[k][0], image.forward_matrix1[k][1],
                          image.forward_matrix1[k][2]);
    }
    else
        spdlog::debug("forward_matrix2 not found!");

    if (true) // image.has_forward_matrix2)
    {
        spdlog::debug("forward_matrix2 found = ");
        for (size_t k = 0; k < 3; k++)
            spdlog::debug("{} {} {}", image.forward_matrix2[k][0], image.forward_matrix2[k][1],
                          image.forward_matrix2[k][2]);
    }
    else
        spdlog::debug("forward_matrix2 not found!");

    spdlog::debug("camera_calibration1 = ");
    for (size_t k = 0; k < 3; k++)
        spdlog::debug("{} {} {}", image.camera_calibration1[k][0], image.camera_calibration1[k][1],
                      image.camera_calibration1[k][2]);

    spdlog::debug("orientation = {}", image.orientation);

    spdlog::debug("camera_calibration2 = ");
    for (size_t k = 0; k < 3; k++)
        spdlog::debug("{} {} {}", image.camera_calibration2[k][0], image.camera_calibration2[k][1],
                      image.camera_calibration2[k][2]);

    if (image.has_analog_balance)
        spdlog::debug("analog_balance = {} , {} , {}", image.analog_balance[0], image.analog_balance[1],
                      image.analog_balance[2]);
    else
        spdlog::debug("analog_balance not found!");

    if (image.has_as_shot_neutral)
        spdlog::debug("as_shot_neutral = {} , {} , {}", image.as_shot_neutral[0], image.as_shot_neutral[1],
                      image.as_shot_neutral[2]);
    else
        spdlog::debug("shot_neutral not found!");
}

} // namespace