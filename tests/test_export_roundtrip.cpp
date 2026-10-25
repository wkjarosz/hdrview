//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file test_export_roundtrip.cpp

    Every format HDRView writes, at each setting of its options, saved and read back.

    k_variants lists the settings and what each promises. Four sweeps hold them to it:
    - every setting a dialog opens with, against every channel-group layout HDRView can save, compared at every
      pixel on straight (unpremultiplied) color so a format that cannot store alpha is judged on the same footing
      as one that can;
    - every setting, against every transfer function its dialog offers, compared at every pixel in code values
      within the quantization step or the lossy codec's error the setting declares, with its alpha, the loader
      that claims the file, the primaries, light levels and white luminance it records, and on macOS as ImageIO
      reads it, which each dialog's caveats must predict;
    - every setting a dialog opens with, loaded through a channel selector;
    - and the same settings' files cut short, which must load cleanly or not at all.
*/

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/dds.h"
#include "imageio/exr.h"
#include "imageio/exr_save_options.h"
#include "imageio/hdr_metadata.h"
#include "imageio/heif.h"
#include "imageio/icc.h"
#include "imageio/image_loader.h"
#include "imageio/j2k.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/pfm.h"
#include "imageio/png.h"
#include "imageio/qoi.h"
#include "imageio/raw.h"
#include "imageio/save_options.h"
#include "imageio/stb.h"
#include "imageio/streams.h"
#include "imageio/tiff.h"
#include "imageio/uhdr.h"
#include "imageio/webp.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if HDRVIEW_ENABLE_LIBJXL
#include <jxl/types.h>
#endif
#if HDRVIEW_ENABLE_LIBPNG
// libpng gained cICP, cLLI and mDCV in 1.6.45, which a -local build may predate; png.cpp guards on the same macros
#include <png.h>
#endif
#if HDRVIEW_ENABLE_LIBHEIF
#include <libheif/heif.h>
#endif
#include "test_apple.h"

namespace
{

enum Caps : unsigned
{
    Cap_Default       = 1u << 0, ///< the setting the save dialog opens with, which the layout sweep runs
    Cap_Float         = 1u << 1, ///< stores unbounded values, and is therefore written linear
    Cap_Alpha         = 1u << 2, ///< can store an alpha channel
    Cap_Lossy         = 1u << 3, ///< moves samples even at maximum quality
    Cap_Levels        = 1u << 4, ///< records a PQ or HLG image's content light level and mastering display
    Cap_White         = 1u << 5, ///< records the luminance the values' (1, 1, 1) stands for
    Cap_Premultiplied = 1u << 6, ///< quantizes color multiplied by alpha, so straight color moves by step / alpha
    Cap_Refuses_Cut   = 1u << 7, ///< refuses a file cut short rather than decoding what arrived
    Cap_Dithers       = 1u << 8, ///< dithers what it quantizes, a step of the depth it stores
};

#if defined(PNG_cICP_SUPPORTED) && defined(PNG_cLLI_SUPPORTED) && defined(PNG_mDCV_SUPPORTED)
constexpr unsigned k_png_levels = Cap_Levels;
#else
constexpr unsigned k_png_levels = 0u;
#endif
#if defined(LIBHEIF_NUMERIC_VERSION) && LIBHEIF_NUMERIC_VERSION >= ((1 << 24) | (15 << 16))
constexpr unsigned k_heif_levels = Cap_Levels;
#else
constexpr unsigned k_heif_levels = 0u;
#endif

using Save   = void (*)(const Image &, std::ostream &, TransferFunction);
using Offers = bool (*)(TransferFunction);
using Sniff  = bool (*)(std::istream &);

/// One setting of a format's options, and what it promises.
struct Variant
{
    const char *name;
    const char *ext;
    const char *loader; ///< the default_loaders() entry that must claim the file
    unsigned    caps;
    float       step;    ///< how far a code value may move: the quantization step, or the lossy codec's worst error
    Offers      offers;  ///< the transfer functions its dialog offers, each of which it must label
    Offers      records; ///< the transfer functions under which the file records its primaries, as its options report
    Save        save;
};

constexpr float k_8bit          = 1.f / 255.f;  ///< a code value step at 8 bits, which rounding stays within
constexpr float k_8bit_dithered = 1.5f / 255.f; ///< a tent dither moves a sample a step, then rounding half of one
constexpr float k_10bit_dithered = 1.5f / 1023.f;
constexpr float k_12bit_dithered = 1.5f / 4095.f;

bool offers_linear(TransferFunction tf) { return tf.type == TransferFunction::Linear; }
bool always(TransferFunction) { return true; }
bool never(TransferFunction) { return false; }
bool icc_labels(TransferFunction tf) { return ICCProfile::can_label(tf); }

bool is_exr(std::istream &is) { return is_exr_image(is, "a.exr"); }

/// Every loader's sniff, in the order default_loaders() in image_loader.cpp asks them.
const std::pair<const char *, Sniff> k_sniffs[] = {
    {"openexr", is_exr},
#if HDRVIEW_ENABLE_LIBUHDR
    {"libultrahdr", is_uhdr_image},
#endif
#if HDRVIEW_ENABLE_LIBJPEG
    {"libjpg", is_jpg_image},
#endif
    {"qoi", is_qoi_image},
#if HDRVIEW_ENABLE_LIBJXL
    {"libjxl", is_jxl_image},
#endif
#if HDRVIEW_ENABLE_J2K
    {"openjpeg", is_j2k_image},
#endif
    {"dds", is_dds_image},
#if HDRVIEW_ENABLE_LIBHEIF
    {"libheif", is_heif_image},
#endif
#if HDRVIEW_ENABLE_LIBPNG
    {"libpng", is_png_image},
#endif
#if HDRVIEW_ENABLE_LIBWEBP
    {"libwebp", is_webp_image},
#endif
#if HDRVIEW_ENABLE_LIBRAW
    {"libraw", is_raw_image},
#endif
#if HDRVIEW_ENABLE_LIBTIFF
    {"libtiff", is_tiff_image},
#endif
    {"stb", is_stb_image},
    {"pfm", is_pfm_image},
};

#if HDRVIEW_ENABLE_LIBPNG
constexpr const char *k_png_loader = "libpng";
#else
constexpr const char *k_png_loader = "stb";
#endif
#if HDRVIEW_ENABLE_LIBJPEG
constexpr const char *k_jpg_loader = "libjpg";
#else
constexpr const char *k_jpg_loader = "stb";
#endif

/// The loader load_image() hands \p bytes to: the first whose sniff claims them, or "" if none does.
std::string claiming_loader(const std::string &bytes)
{
    for (const auto &[name, sniff] : k_sniffs)
    {
        SpanIStream is{bytes};
        if (sniff(is))
            return name;
    }
    return "";
}

template <int pixel_type, Imf::Compression compression>
void save_exr(const Image &img, std::ostream &os, TransferFunction)
{
    auto opts        = exr_default_save_options(img);
    opts.pixel_type  = pixel_type;
    opts.compression = compression;
    save_exr_image(img, os, "a.exr", &opts);
}

// clang-format off
const Variant k_variants[] = {
    // the default-argument form, which has to fall back to something sized for this image
    {"exr", ".exr", "openexr", Cap_Default | Cap_Float | Cap_Alpha | Cap_White | Cap_Refuses_Cut, 1e-3f, offers_linear, always,
     [](const Image &i, std::ostream &o, TransferFunction) { save_exr_image(i, o, "a.exr"); }},
    {"exr float zip", ".exr", "openexr", Cap_Float | Cap_Alpha | Cap_White, 1e-6f, offers_linear, always, save_exr<0, Imf::ZIP_COMPRESSION>},
    {"exr half rle", ".exr", "openexr", Cap_Float | Cap_Alpha | Cap_White, 1e-3f, offers_linear, always, save_exr<1, Imf::RLE_COMPRESSION>},
    {"exr half dwaa", ".exr", "openexr", Cap_Float | Cap_Alpha | Cap_White | Cap_Lossy, 0.02f, offers_linear, always, save_exr<1, Imf::DWAA_COMPRESSION>},
    {"pfm", ".pfm", "pfm", Cap_Default | Cap_Float, 1e-6f, pfm_offers_tf, never,
     [](const Image &i, std::ostream &o, TransferFunction) { save_pfm_image(i, o, "a.pfm"); }},
    {"hdr_stb", ".hdr", "stb", Cap_Default | Cap_Float, 0.008f, [](TransferFunction t) { return stb_offers_tf(t, true); }, never,
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_hdr(i, o, "a.hdr"); }},
    {"png8", ".png", k_png_loader, Cap_Default | Cap_Alpha | k_png_levels, k_8bit, png_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", false, false, false, t); }},
    {"png8 interlaced", ".png", k_png_loader, Cap_Alpha | k_png_levels, k_8bit, png_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", false, true, false, t); }},
    {"png16", ".png", k_png_loader, Cap_Alpha | k_png_levels, 1e-4f, png_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", false, false, true, t); }},
    {"png_stb", ".png", k_png_loader, Cap_Default | Cap_Alpha, k_8bit, [](TransferFunction t) { return stb_offers_tf(t, false); }, never,
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_png(i, o, "a.png", false); }},
    {"tga_stb", ".tga", "stb", Cap_Default | Cap_Alpha, k_8bit, [](TransferFunction t) { return stb_offers_tf(t, false); }, never,
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_tga(i, o, "a.tga", false); }},
    {"bmp_stb", ".bmp", "stb", Cap_Default, k_8bit, [](TransferFunction t) { return stb_offers_tf(t, false); }, never,
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_bmp(i, o, "a.bmp", false); }},
    {"jpg_stb", ".jpg", k_jpg_loader, Cap_Default | Cap_Lossy, 0.03f, [](TransferFunction t) { return stb_offers_tf(t, false); }, never,
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_jpg(i, o, "a.jpg", false); }},
#if HDRVIEW_ENABLE_LIBTIFF
    // the save dialog's compressions (0 none, 1 LZW, 2 ZIP, 3 PackBits) and data types (0 8-bit, 1 16-bit, 2 float);
    // an ICC profile, which lcms reads in float, moves float samples by a few 1e-6
    {"tiff8 lzw", ".tif", "libtiff", Cap_Default | Cap_Alpha | Cap_Premultiplied | Cap_Dithers, k_8bit_dithered, [](TransferFunction t) { return tiff_offers_tf(t, 0); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 1, 0); }},
    {"tiff8 none", ".tif", "libtiff", Cap_Alpha | Cap_Premultiplied | Cap_Dithers, k_8bit_dithered, [](TransferFunction t) { return tiff_offers_tf(t, 0); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 0, 0); }},
    {"tiff16 zip", ".tif", "libtiff", Cap_Alpha | Cap_Premultiplied | Cap_Dithers, 1e-4f, [](TransferFunction t) { return tiff_offers_tf(t, 1); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 2, 1); }},
    {"tiff16 packbits", ".tif", "libtiff", Cap_Alpha | Cap_Premultiplied | Cap_Dithers, 1e-4f, [](TransferFunction t) { return tiff_offers_tf(t, 1); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 3, 1); }},
    {"tiff float lzw", ".tif", "libtiff", Cap_Float | Cap_Alpha, 1e-5f, [](TransferFunction t) { return tiff_offers_tf(t, 2); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 1, 2); }},
#endif
    {"qoi", ".qoi", "qoi", Cap_Default | Cap_Alpha, k_8bit, qoi_offers_tf, never,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_qoi_image(i, o, "a.qoi", t.type == TransferFunction::sRGB, false); }},
#if HDRVIEW_ENABLE_LIBJPEG
    // libjpeg subsamples chroma 4:2:0, which moves the image's corner furthest
    {"jpg q95", ".jpg", "libjpg", Cap_Default | Cap_Lossy, 0.07f, jpg_offers_tf, never,
     [](const Image &i, std::ostream &o, TransferFunction)
     { save_jpg_image(i, o, "a.jpg", false, 95, false); }},
    {"jpg q60 progressive", ".jpg", "libjpg", Cap_Lossy, 0.1f, jpg_offers_tf, never,
     [](const Image &i, std::ostream &o, TransferFunction)
     { save_jpg_image(i, o, "a.jpg", false, 60, true); }},
#endif
#if HDRVIEW_ENABLE_J2K
    // both syntaxes, every bit depth the dialog offers, and its lossy mode
    {"jp2 16", ".jp2", "openjpeg", Cap_Default | Cap_Alpha, 1e-4f, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", true, 16, J2KContainer::JP2, t, false); }},
    {"jp2 12", ".jp2", "openjpeg", Cap_Alpha | Cap_Dithers, k_12bit_dithered, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", true, 12, J2KContainer::JP2, t, true); }},
    {"jp2 10", ".jp2", "openjpeg", Cap_Alpha | Cap_Dithers, k_10bit_dithered, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", true, 10, J2KContainer::JP2, t, true); }},
    {"jp2 8", ".jp2", "openjpeg", Cap_Alpha | Cap_Dithers, k_8bit_dithered, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", true, 8, J2KContainer::JP2, t, true); }},
    // the dialog's compression ratio leaves an 8x6 image few bytes, its alpha's included
    {"jp2 16 lossy", ".jp2", "openjpeg", Cap_Alpha | Cap_Lossy, 0.15f, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); }, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", false, 16, J2KContainer::JP2, t, false); }},
    {"j2k 16", ".j2k", "openjpeg", Cap_Alpha, 1e-4f, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::J2K); }, never,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.j2k", true, 16, J2KContainer::J2K, t, false); }},
#endif
#if HDRVIEW_ENABLE_LIBWEBP
    // lossy WebP is 8-bit 4:2:0, whose error at quality 95 a steep transfer function such as Log 100 amplifies
    {"webp q95", ".webp", "libwebp", Cap_Default | Cap_Alpha | Cap_Lossy, 0.05f, webp_offers_tf, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_webp_image(i, o, "a.webp", 95.f, false, t); }},
    {"webp lossless", ".webp", "libwebp", Cap_Alpha | Cap_Dithers, k_8bit_dithered, webp_offers_tf, icc_labels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_webp_image(i, o, "a.webp", 100.f, true, t); }},
#endif
#if HDRVIEW_ENABLE_LIBJXL && !defined(HDRVIEW_TEST_ASAN) // see test_asan.h
    // a gamma decodes through the ICC profile libjxl synthesizes, whose fixed-point curve moves a lossless
    // file's samples by about 1e-4
    {"jxl f32 q95", ".jxl", "libjxl", Cap_Default | Cap_Alpha | Cap_Lossy, 0.035f, [](TransferFunction t) { return jxl_offers_tf(t); }, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", false, 95.f, t, JXL_TYPE_FLOAT); }},
    {"jxl f32 lossless", ".jxl", "libjxl", Cap_Alpha, 3e-4f, [](TransferFunction t) { return jxl_offers_tf(t); }, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", true, 100.f, t, JXL_TYPE_FLOAT); }},
    {"jxl f16 lossless", ".jxl", "libjxl", Cap_Alpha, 1e-3f, [](TransferFunction t) { return jxl_offers_tf(t); }, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", true, 100.f, t, JXL_TYPE_FLOAT16); }},
    {"jxl u16 lossless", ".jxl", "libjxl", Cap_Alpha | Cap_Dithers, 3e-4f, [](TransferFunction t) { return jxl_offers_tf(t); }, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", true, 100.f, t, JXL_TYPE_UINT16); }},
    {"jxl u8 lossless", ".jxl", "libjxl", Cap_Alpha | Cap_Dithers, k_8bit_dithered, [](TransferFunction t) { return jxl_offers_tf(t); }, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", true, 100.f, t, JXL_TYPE_UINT8); }},
#endif
#if HDRVIEW_ENABLE_LIBHEIF
    // both codecs the dialog offers, since they take different paths through libheif
    {"heif", ".heif", "libheif", Cap_Default | Cap_Alpha | Cap_Lossy | k_heif_levels, 0.055f, heif_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.heif", 95, false, true, HEIFCodec::HEVC, t); }},
    {"heif lossless 8", ".heif", "libheif", Cap_Alpha | k_heif_levels | Cap_Dithers, k_8bit_dithered, heif_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.heif", 100, true, true, HEIFCodec::HEVC, t, 8); }},
    {"heif lossless 10", ".heif", "libheif", Cap_Alpha | k_heif_levels | Cap_Dithers, k_10bit_dithered, heif_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.heif", 100, true, true, HEIFCodec::HEVC, t, 10); }},
    {"avif", ".avif", "libheif", Cap_Default | Cap_Alpha | Cap_Lossy | k_heif_levels, 0.055f, heif_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.avif", 95, false, true, HEIFCodec::AV1, t); }},
    {"avif lossless 8", ".avif", "libheif", Cap_Alpha | k_heif_levels | Cap_Dithers, k_8bit_dithered, heif_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.avif", 100, true, true, HEIFCodec::AV1, t, 8); }},
    {"avif lossless 10", ".avif", "libheif", Cap_Alpha | k_heif_levels | Cap_Dithers, k_10bit_dithered, heif_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.avif", 100, true, true, HEIFCodec::AV1, t, 10); }},
    {"avif lossless 12", ".avif", "libheif", Cap_Alpha | k_heif_levels | Cap_Dithers, k_12bit_dithered, heif_offers_tf, always,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.avif", 100, true, true, HEIFCodec::AV1, t, 12); }},
#endif
};

struct Layout
{
    const char              *label;
    std::vector<const char *> channels;
};

// The narrow groups are the point: a writer is normally built against only RGB and RGBA.
const Layout k_layouts[] = {
    {"Y", {"Y"}},                     // a lone luma channel
    {"YA", {"Y", "A"}},               // luma plus alpha
    {"UV", {"U", "V"}},               // a two-channel pair that is not gray and has no alpha
    {"RGB", {"R", "G", "B"}},         //
    {"RGBA", {"R", "G", "B", "A"}},   //
    {"YC", {"RY", "Y", "BY"}},        // OpenEXR luminance-chroma
    {"YCA", {"RY", "Y", "BY", "A"}},  // luminance-chroma plus alpha
};
const Layout &k_rgb  = k_layouts[3];
const Layout &k_rgba = k_layouts[4];
// clang-format on

const int2  k_size{8, 6};           ///< not square: a transposed stride is invisible in a square image
const float k_hdr_brightness = 8.f; ///< the gain for an HDR transfer function, which puts most samples above SDR white

/// Straight (unpremultiplied) color channel \p c at (\p x, \p y), at unit \p brightness.
/**
    Distinct per channel and per pixel, so a flipped, transposed or misaddressed write shows. Every channel follows
    one smooth gradient, which a lossy codec keeps far better than an edge, and stays at or below 0.5, so neither an
    SDR encoding nor the luminance-chroma layouts' RGB clamps.
*/
float straight_value(int c, int x, int y, float brightness)
{
    const float t = float(x + k_size.x * y) / float(k_size.x * k_size.y - 1);
    return brightness * (0.05f + 0.35f * t + 0.05f * c);
}

/// Coverage at (\p x, \p y), from 0.03 to exactly 1, varying down the columns where color varies along the rows.
float alpha_value(int x, int y)
{
    const float t = float(y + k_size.y * x) / float(k_size.x * k_size.y - 1);
    return 1.f - 0.97f * (1.f - t);
}

/// An image of \p layout holding straight_value() at \p brightness, and alpha_value() if it has alpha.
ImagePtr make_image(const Layout &layout, float brightness = 1.f)
{
    auto img = std::make_shared<Image>();
    for (auto name : layout.channels) img->channels.emplace_back(name, k_size);
    img->display_window = img->data_window = Box2i{int2{0}, k_size};

    int color = 0;
    for (size_t c = 0; c < layout.channels.size(); ++c)
    {
        const bool is_alpha = std::string(layout.channels[c]) == "A";
        for (int y = 0; y < k_size.y; ++y)
            for (int x = 0; x < k_size.x; ++x)
                img->channels[c](x, y) = is_alpha ? alpha_value(x, y) : straight_value(color, x, y, brightness);
        if (!is_alpha)
            ++color;
    }
    // as every loader does for a straight-alpha file, so finalize() premultiplies and the writers'
    // unpremultiply step has something to undo
    img->transparency = TransparencyType_Straight;
    img->finalize();
    return img;
}

/// Color with the premultiply divided back out, which is comparable whether or not alpha survived.
float3 straight_rgb(const float4 &premultiplied)
{
    const float a = std::max(1e-4f, premultiplied[3]);
    return float3{premultiplied[0] / a, premultiplied[1] / a, premultiplied[2] / a};
}

/// How far \p v lets a straight code value move at coverage \p alpha.
/**
    Dividing alpha back out scales a quantized premultiplied sample's error by 1 / alpha, and a lossy encoder spends
    fewer bits on color the less it covers, so both may move that much further.
*/
float tolerance(const Variant &v, float alpha)
{ return (v.caps & (Cap_Premultiplied | Cap_Lossy)) ? v.step / alpha : v.step; }

/// The transfer function the save dialog opens \p v with: linear for a float format, sRGB otherwise.
TransferFunction default_tf(const Variant &v)
{ return TransferFunction{(v.caps & Cap_Float) ? TransferFunction::Linear : TransferFunction::sRGB}; }

/// What \p v writes for \p img under \p tf, or nothing if this build has no encoder for it.
/**
    Libheif's codecs depend on the plugins the build found, and the Windows CPM build has no HEVC encoder at all.
    Any other failure is reported, and the cell skipped, so the first failure does not hide every cell after it.
*/
std::optional<std::string> save(const Variant &v, const Image &img, const TransferFunction &tf)
{
    std::ostringstream out(std::ios::binary);
    std::string        error;
    try
    {
        v.save(img, out, tf);
    }
    catch (const std::exception &e)
    {
        error = e.what();
    }
    if (error.find("no encoder available") != std::string::npos)
        return std::nullopt;
    CHECK_MESSAGE(error.empty(), "save threw: ", error);
    CHECK_FALSE(out.str().empty());
    if (!error.empty() || out.str().empty())
        return std::nullopt;
    return out.str();
}

/// \p bytes loaded through load_image() as a file named for \p v's extension.
std::vector<ImagePtr> load(const Variant &v, const std::string &bytes, const ImageLoadOptions &opts = {})
{
    SpanIStream in{bytes};
    return load_image(in, std::string("a") + v.ext, opts);
}

/// The warning \p variant's dialog shows for \p tf, for the sweep's image, whose primaries are BT.2020's.
Caveat caveat_of(const Variant &variant, const TransferFunction &tf)
{
    const std::string ext = variant.ext;
    if (ext == ".exr")
        return exr_caveat(variant.caps & Cap_Alpha);
    if (ext == ".png" && std::string{variant.name}.rfind("png", 0) == 0)
        return png_caveat(tf);
#if HDRVIEW_ENABLE_LIBHEIF
    if (ext == ".heif" || ext == ".avif")
        return heif_caveat(tf);
#endif
#if HDRVIEW_ENABLE_LIBJXL
    if (ext == ".jxl")
        return jxl_caveat(tf, std::string{variant.name}.find("lossless") != std::string::npos);
#endif
#if HDRVIEW_ENABLE_LIBTIFF
    if (ext == ".tif")
        return tiff_caveat(tf, true);
#endif
    return {};
}

} // namespace

TEST_CASE("every writer round-trips every channel-group layout as the viewport shows it")
{
    for (const auto &writer : k_variants)
        for (const auto &layout : k_layouts)
        {
            if (!(writer.caps & Cap_Default))
                continue;
            INFO("writer = ", std::string{writer.name}, ", layout = ", std::string{layout.label});

            auto img = make_image(layout);

            // a float format records no transfer function and is written linear; the integer ones
            // round-trip theirs and are written sRGB, as the save dialog defaults them
            const TransferFunction tf = default_tf(writer);

            // PFM stores 1, 3 or 4 channels and has no two-channel form; everything else takes everything
            if (std::string{writer.name} == "pfm" && layout.channels.size() == 2)
            {
                std::ostringstream out(std::ios::binary);
                CHECK_THROWS(writer.save(*img, out, tf));
                continue;
            }

            const auto bytes = save(writer, *img, tf);
            if (!bytes)
                continue;

            // the app hands loaders a SpanIStream over bytes in memory or a FileIStream for a file on disk,
            // which they may read without a copy or through the file itself; both must decode the same
            const auto reloaded = load(writer, *bytes);
            CHECK(reloaded.size() == 1);
            if (reloaded.size() != 1)
                continue;
            {
                const auto path =
                    std::filesystem::temp_directory_path() / (std::string("hdrview_roundtrip") + writer.ext);
                std::ofstream{path, std::ios::binary}.write(bytes->data(), bytes->size());
                std::vector<ImagePtr> from_disk;
                {
                    FileIStream file{path};
                    from_disk = load_image(file, path.filename().string());
                }
                std::error_code ec;
                std::filesystem::remove(path, ec);
                CHECK(from_disk.size() == 1);
                if (from_disk.size() == 1)
                {
                    const auto a = hdrview_test::samples(reloaded[0]), b = hdrview_test::samples(from_disk[0]);
                    // bitwise, so NaNs compare equal
                    CHECK((a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0));
                }
            }

            // Two cells are open questions about what the file should hold. They still run this far, so a
            // crash or failed decode is caught; only the color comparison is skipped.
            //
            //  - A writer storing two channels natively puts a U,V pair in a gray+alpha container, so V
            //    becomes transparency and the reload displays gray. Writers that cannot store two channels
            //    pad to RGB, which is what the viewport shows.
            //  - PFM has no four-channel form. HDRView writes one as an extension with alpha divided back
            //    out, but load_pfm_image() does not read the fourth channel back as alpha.
            const bool open_question =
                std::string{layout.label} == "UV" || (std::string{writer.name} == "pfm" && layout.channels.size() == 4);
            if (open_question)
                continue;

            // every pixel, in code values and straight, so a format that cannot store alpha is judged on the same
            // footing as one that can; alpha only where the format can hold one and the group carries one
            const bool check_alpha =
                (writer.caps & Cap_Alpha) && group_has_alpha(img->groups[img->selected_group].type);
            float worst = 0.f, worst_alpha = 0.f;
            int2  worst_at{0};
            for (int y = 0; y < k_size.y; ++y)
                for (int x = 0; x < k_size.x; ++x)
                {
                    const float4 want4 = img->rgba_pixel(int2{x, y}, Target_Primary);
                    const float4 got4  = reloaded[0]->rgba_pixel(int2{x, y}, Target_Primary);
                    const float3 want = from_linear(straight_rgb(want4), tf), got = from_linear(straight_rgb(got4), tf);
                    const float  tol = tolerance(writer, want4[3]);
                    for (int c = 0; c < 3; ++c)
                        if (const float e = std::abs(got[c] - want[c]) / tol; !(e <= worst))
                        {
                            worst    = e;
                            worst_at = int2{x, y};
                        }
                    if (check_alpha)
                        worst_alpha = std::max(worst_alpha, std::abs(got4[3] - want4[3]) / writer.step);
                }
            CHECK_MESSAGE(worst <= 1.f, "color is off by ", worst, " of the tolerance at ", worst_at.x, ",",
                          worst_at.y);
            CHECK_MESSAGE(worst_alpha <= 1.f, "alpha is off by ", worst_alpha, " steps");
        }
}

TEST_CASE("every setting of every writer round-trips each transfer function its dialog offers, labeled as it promises")
{
    // A wide-gamut image, which a setting keeps and records or converts to sRGB's primaries, under every transfer
    // function. Gamma takes four exponents, since a reader may read them all as one (macOS reads every JPEG XL gamma as
    // 1.8): 1.8, which no other transfer function shares, 2.2 and 2.8, which CICP names, and 2.6, JPEG XL's DCI. A
    // setting must label each transfer function its dialog offers, or the file reads back as some other one; one it
    // does not offer is the saver's to refuse or write as one it can label, which the dialog never asks for.
    const Chromaticities bt2020 = gamut_chromaticities(ColorGamut_BT2020_2100);

    std::vector<TransferFunction> tfs;
    for (int i = TransferFunction::Linear; i < TransferFunction::Count; ++i)
        if (i != TransferFunction::Gamma)
            tfs.push_back({(TransferFunction::Type_)i});
    tfs.push_back({TransferFunction::Gamma, 1.8f});
    tfs.push_back({TransferFunction::Gamma, 2.2f});
    tfs.push_back({TransferFunction::Gamma, 2.6f});
    tfs.push_back({TransferFunction::Gamma, 2.8f});

    for (const auto &variant : k_variants)
    {
        INFO("setting = ", std::string{variant.name});
        // the transfer function the dialog opens with is always offered
        CHECK(variant.offers(default_tf(variant)));

        for (const auto &tf : tfs)
        {
            if (!variant.offers(tf))
                continue;
            INFO("transfer function = ", transfer_function_name(tf));

            // for an HDR transfer function, an image brightened so its channels straddle SDR white, which one
            // clipping there misses; and coverage from near 0 to 1 wherever the setting can store it
            const bool hdr_tf   = tf.type == TransferFunction::BT2100_PQ || tf.type == TransferFunction::BT2100_HLG;
            const bool alpha    = variant.caps & Cap_Alpha;
            auto       img      = make_image(alpha ? k_rgba : k_rgb, hdr_tf ? k_hdr_brightness : 1.f);
            img->chromaticities = bt2020;
            img->compute_color_transform();

            // what a setting that has room for it records: a white luminance, and for an HDR transfer function the
            // display the image was mastered on
            const MasteringDisplay mastered{bt2020, k_mastering_display_min_nits, 1000.f};
            if (hdr_tf)
                add_mastering_display(img->metadata["header"], mastered.primaries, mastered.min_nits, mastered.max_nits,
                                      "test");
            else
                add_white_luminance(img->metadata["header"], 150.f, "test");

            const auto bytes = save(variant, *img, tf);
            if (!bytes)
                continue;

            // the file is its own format's, and no loader load_image() asks first mistakes it for one of theirs
            CHECK(claiming_loader(*bytes) == variant.loader);

            const auto reloaded = load(variant, *bytes);
            CHECK(reloaded.size() == 1);
            if (reloaded.size() != 1)
                continue;
            const auto &back = reloaded[0];

            // Compared in code values, where the setting's step is a step whatever the level, and in the primaries the
            // samples were quantized in: the image's own when the file kept and recorded them, sRGB's when it
            // converted. A file read back with the wrong transfer function or primaries is off by far more.
            const bool kept = back->chromaticities && approx_equal(*back->chromaticities, bt2020, 1e-3f);
            if (variant.records(tf))
                CHECK_MESSAGE(kept,
                              "the setting reports that it records the image's primaries, and they were not kept");
            const auto M = kept ? inverse(img->M_to_sRGB) : float3x3{la::identity};

            float worst = 0.f, worst_alpha = 0.f;
            int2  worst_at{0};
            for (int y = 0; y < k_size.y; ++y)
                for (int x = 0; x < k_size.x; ++x)
                {
                    const float4 want4 = img->rgba_pixel(int2{x, y}, Target_Primary);
                    const float4 got4  = back->rgba_pixel(int2{x, y}, Target_Primary);
                    const auto   got   = from_linear(mul(M, straight_rgb(got4)), tf);
                    const auto   ref   = from_linear(mul(M, straight_rgb(want4)), tf);
                    const float  tol   = tolerance(variant, want4[3]);
                    for (int c = 0; c < 3; ++c)
                        if (const float e = std::abs(got[c] - ref[c]) / tol; !(e <= worst))
                        {
                            worst    = e;
                            worst_at = int2{x, y};
                        }
                    if (alpha)
                        worst_alpha = std::max(worst_alpha, std::abs(got4[3] - want4[3]) / variant.step);
                }

            CHECK_MESSAGE(worst <= 1.f, "color is off by ", worst, " of the tolerance at ", worst_at.x, ",",
                          worst_at.y);
            CHECK_MESSAGE(worst_alpha <= 1.f, "alpha is off by ", worst_alpha, " steps");

#if defined(__APPLE__)
            // and as macOS reads it, which is how every application there sees the file
            if (const auto decoded = hdrview_test::apple_decode(*bytes))
            {
                // A setting whose dialog does not warn that macOS misreads it must read within the setting's own
                // tolerance plus Apple's 8-bit-or-so precision, which ImageIO's premultiplying divides by alpha; one
                // that warns must be off by more than half that, or the warning has gone stale.
                float apple_worst = 0.f;
                for (int y = 0; y < k_size.y; ++y)
                    for (int x = 0; x < k_size.x; ++x)
                    {
                        const float4 want4  = img->rgba_pixel(int2{x, y}, Target_Primary);
                        const auto   w_code = from_linear(straight_rgb(want4), tf);
                        const float  tol    = tolerance(variant, want4[3]) + 0.025f / want4[3];
                        const float4 apple  = (*decoded)(x, y);
                        const auto   a_code = from_linear(straight_rgb(apple), tf);
                        for (int c = 0; c < 3; ++c)
                            apple_worst = std::max(apple_worst, std::abs(a_code[c] - w_code[c]) / tol);
                    }
                if (caveat_of(variant, tf).readers & Misreader_macOS)
                    CHECK_MESSAGE(apple_worst > 0.5f, "the dialog warns that macOS misreads this file, which it reads "
                                                      "right: the warning is stale");
                else
                    CHECK_MESSAGE(apple_worst <= 1.f, "macOS reads this file as other values, off by ", apple_worst,
                                  " of the tolerance, and the dialog does not warn of it");
            }
#endif
            const json header = back->metadata.value("header", json::object());
            if (!hdr_tf && (variant.caps & Cap_White))
                CHECK(white_luminance(header).value_or(0.f) == doctest::Approx(150.f));
            if (hdr_tf && (variant.caps & Cap_Levels))
            {
                // the content light level of the straight color, which alpha does not dim
                double brightest = 0., total = 0.;
                for (int y = 0; y < k_size.y; ++y)
                    for (int x = 0; x < k_size.x; ++x)
                    {
                        float pixel = 0.f;
                        for (int c = 0; c < 3; ++c) pixel = std::max(pixel, straight_value(c, x, y, k_hdr_brightness));
                        brightest = std::max(brightest, (double)pixel);
                        total += pixel;
                    }
                const auto cll = content_light_level(header);
                CHECK(cll);
                if (cll)
                {
                    CHECK(cll->max_cll == doctest::Approx(brightest * HDR_REFERENCE_WHITE_NITS).epsilon(0.01));
                    CHECK(cll->max_fall ==
                          doctest::Approx(total / (k_size.x * k_size.y) * HDR_REFERENCE_WHITE_NITS).epsilon(0.01));
                }

                const auto md = mastering_display(header);
                CHECK(md);
                if (md)
                {
                    for (auto [g, w] : {std::pair{md->primaries.red, mastered.primaries.red},
                                        std::pair{md->primaries.green, mastered.primaries.green},
                                        std::pair{md->primaries.blue, mastered.primaries.blue},
                                        std::pair{md->primaries.white, mastered.primaries.white}})
                        CHECK(la::maxelem(la::abs(g - w)) <= 1e-4f);
                    CHECK(md->max_nits == doctest::Approx(mastered.max_nits));
                    CHECK(md->min_nits == doctest::Approx(mastered.min_nits));
                }
            }
#if HDRVIEW_ENABLE_LIBJXL
            // JPEG XL has only the peak, as its intensity target
            if (hdr_tf && std::string{variant.ext} == ".jxl")
                CHECK(header.at("intensity target").at("value").get<float>() == doctest::Approx(mastered.max_nits));
#endif
        }
    }
}

TEST_CASE("a PFM in Photoshop's row order stores the standard file's rows the other way round")
{
    // The format stores the bottom row first, which the sweep above holds HDRView and macOS to; Photoshop reads and
    // writes the top row first, so its option must flip the rows and nothing else. The image's rows all differ.
    auto               img = make_image(k_rgb);
    std::ostringstream standard(std::ios::binary), photoshop(std::ios::binary);
    save_pfm_image(*img, standard, "a.pfm");
    save_pfm_image(*img, photoshop, "a.pfm", true);
    const std::string a = standard.str(), b = photoshop.str();
    REQUIRE(a.size() == b.size());

    const size_t row = size_t(k_size.x) * 3 * sizeof(float), header = a.size() - row * size_t(k_size.y);
    CHECK(a.compare(0, header, b, 0, header) == 0);
    int reversed = 0, same = 0;
    for (int y = 0; y < k_size.y; ++y)
    {
        reversed += a.compare(header + size_t(y) * row, row, b, header + size_t(k_size.y - 1 - y) * row, row) == 0;
        same += a.compare(header + size_t(y) * row, row, b, header + size_t(y) * row, row) == 0;
    }
    CHECK(reversed == k_size.y);
    CHECK(same == 0);
}

TEST_CASE("every writer's file loads with a channel selector applied, whichever stage applies it")
{
    // EXR and JPEG XL apply the selector while decoding, and load_image() applies it to every other format's
    // channels afterwards
    for (const auto &writer : k_variants)
    {
        if (!(writer.caps & Cap_Default))
            continue;
        INFO("writer = ", std::string{writer.name});

        const auto bytes = save(writer, *make_image(k_rgba), default_tf(writer));
        if (!bytes)
            continue;
        const auto with = [&](const char *selector)
        {
            ImageLoadOptions opts;
            opts.channel_selector = selector;
            return load(writer, *bytes, opts);
        };
        const auto names = [](const Image &img)
        {
            std::vector<std::string> out;
            for (const auto &c : img.channels) out.push_back(c.name);
            return out;
        };
        const auto find = [](const Image &img, const std::string &name) -> const Channel *
        {
            for (const auto &c : img.channels)
                if (c.name == name)
                    return &c;
            return nullptr;
        };

        // each check that leaves nothing to look at skips the writer, so one failure does not hide the rest
        const auto all = with("");
        CHECK(all.size() == 1);
        if (all.size() != 1)
            continue;
        const Image &full = *all[0];

        // excluding alpha by its unqualified name leaves every other channel. finalize() multiplied a straight
        // file's color by the alpha now gone, so what remains is the color the file stored, undivided.
        const auto no_alpha = with("-.A");
        CHECK(no_alpha.size() == 1);
        if (no_alpha.size() != 1)
            continue;
        auto want_names = names(full);
        want_names.erase(std::remove(want_names.begin(), want_names.end(), "A"), want_names.end());
        CHECK(names(*no_alpha[0]) == want_names);

        const Channel *alpha   = find(full, "A");
        const bool     divided = alpha && full.transparency == TransparencyType_Straight;
        float          worst   = 0.f;
        for (const auto &c : no_alpha[0]->channels)
            if (const Channel *f = find(full, c.name); f && c.size() == f->size())
                for (int i = 0; i < c.num_elements(); ++i)
                {
                    const float want = divided ? (*f)(i) / std::max(k_small_alpha, (*alpha)(i)) : (*f)(i);
                    worst            = std::max(worst, std::abs(c(i) - want) / std::max(1e-3f, std::abs(want)));
                }
        CHECK(worst <= 1e-5f);

        // a selector matching nothing yields no image at all
        CHECK(with("NoSuchChannel").empty());

        // and one naming a single channel, the same samples as the load without alpha
        const auto     green = with("G");
        const Channel *g     = find(*no_alpha[0], "G");
        CHECK(green.size() == 1);
        CHECK(g);
        if (green.size() != 1 || !g)
            continue;
        CHECK(names(*green[0]) == std::vector<std::string>{"G"});
        std::vector<float> want;
        for (int i = 0; i < g->num_elements(); ++i) want.push_back((*g)(i));
        CHECK(hdrview_test::samples(green[0]) == want);
    }
}

TEST_CASE("every writer's file, cut short anywhere, loads cleanly or not at all")
{
    for (const auto &writer : k_variants)
    {
        if (!(writer.caps & Cap_Default))
            continue;
        INFO("writer = ", std::string{writer.name});

        const auto bytes = save(writer, *make_image(k_rgba), default_tf(writer));
        if (!bytes)
            continue;
        const size_t n = bytes->size();
        for (size_t len : {size_t(1), size_t(8), size_t(32), n / 8, n / 4, n / 2, 3 * n / 4, n - 1})
        {
            INFO("cut at ", len, " of ", n);
            std::vector<ImagePtr> images;
            CHECK_NOTHROW(images = load(writer, bytes->substr(0, len)));
            if (writer.caps & Cap_Refuses_Cut)
                CHECK(images.empty());
            for (const auto &img : images)
            {
                CHECK(img->size().x > 0);
                CHECK(img->size().y > 0);
                CHECK_FALSE(img->channels.empty());
                for (const auto &c : img->channels) CHECK(c.size() == img->size());
            }
        }
    }
}

TEST_CASE("every lossless integer writer quantizes at the depth it stores: values on its grid come back exactly, and "
          "a dithering one averages others back to themselves")
{
    // a patch large enough for the dither to average out over, every sample at one encoded value
    const int2 size{64, 64};
    const auto patch = [&](float encoded, const TransferFunction &tf)
    {
        auto img = std::make_shared<Image>();
        for (auto name : {"R", "G", "B"}) img->channels.emplace_back(name, size);
        img->display_window = img->data_window = Box2i{int2{0}, size};
        const float linear                     = to_linear(encoded, tf);
        for (auto &c : img->channels)
            for (int i = 0; i < c.num_elements(); ++i) c(i) = linear;
        img->finalize();
        return img;
    };

    for (const auto &writer : k_variants)
    {
        if (writer.caps & (Cap_Float | Cap_Lossy))
            continue;
        CAPTURE(std::string(writer.name));
        const TransferFunction tf = default_tf(writer);
        REQUIRE(writer.offers(tf));

        const auto probe = save(writer, *patch(0.5f, tf), tf);
        if (!probe)
            continue;
        const auto probed = load(writer, *probe);
        REQUIRE(probed.size() == 1);
        // the depth the file stores, as its loader reports it; 0 for floating-point samples, which nothing quantizes,
        // and which a writer that dithers cannot be storing
        const int bits = probed.front()->channels.front().bits_per_sample;
        CAPTURE(bits);
        if (bits == 0)
        {
            CHECK((writer.caps & Cap_Dithers) == 0);
            continue;
        }
        REQUIRE((bits >= 8 && bits <= 16));
        const float max_code = float((1u << bits) - 1u);

        // the codes a patch at \p encoded comes back as: their mean, and the furthest from \p want
        const auto read_back = [&](float encoded, float want) -> std::optional<std::pair<double, float>>
        {
            const auto bytes = save(writer, *patch(encoded, tf), tf);
            if (!bytes)
                return std::nullopt;
            const auto loaded = load(writer, *bytes);
            REQUIRE(loaded.size() == 1);
            const auto &img   = *loaded.front();
            double      sum   = 0.0;
            float       worst = 0.f;
            for (int c = 0; c < 3; ++c)
                for (int i = 0; i < img.channels[c].num_elements(); ++i)
                {
                    const float code = from_linear(img.channels[c](i), tf) * max_code;
                    sum += code;
                    worst = std::max(worst, std::abs(code - want));
                }
            return std::pair{sum / (3.0 * size.x * size.y), worst};
        };

        // on the grid, at both ends and between: an ICC profile read in float moves a sample by a few 1e-6
        for (float code : {0.f, 1.f, std::round(max_code / 3.f), max_code - 1.f, max_code})
        {
            CAPTURE(code);
            if (const auto got = read_back(code / max_code, code))
                CHECK(got->second < 0.1f);
        }

        // halfway between two codes, which rounding alone takes half a step off
        if (writer.caps & Cap_Dithers)
        {
            const float want = std::round(0.4f * max_code) + 0.5f;
            CAPTURE(want);
            if (const auto got = read_back(want / max_code, want))
                CHECK(std::abs(got->first - want) < 0.05);
        }
    }
}
