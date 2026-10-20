//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file test_export_roundtrip.cpp

    Every format HDRView writes, at each setting of its options, saved and read back.

    k_variants lists the settings and what each promises. Two sweeps hold them to it:
    - every setting a dialog opens with, against every channel-group layout HDRView can save, compared on
      straight (unpremultiplied) color so a format that cannot store alpha is judged on the same footing as
      one that can;
    - every setting, against every transfer function its dialog offers, compared in code values within the
      quantization step or the lossy codec's error the setting declares, with the primaries, light levels and
      white luminance it records, and on macOS as ImageIO reads it, which each dialog's caveats must predict.
*/

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/exr.h"
#include "imageio/exr_save_options.h"
#include "imageio/hdr_metadata.h"
#include "imageio/heif.h"
#include "imageio/image_loader.h"
#include "imageio/j2k.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/pfm.h"
#include "imageio/png.h"
#include "imageio/qoi.h"
#include "imageio/save_options.h"
#include "imageio/stb.h"
#include "imageio/streams.h"
#include "imageio/tiff.h"
#include "imageio/webp.h"
#include "test_support.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
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
    Cap_Default = 1u << 0, ///< the setting the save dialog opens with, which the layout sweep runs
    Cap_Float   = 1u << 1, ///< stores unbounded values, and is therefore written linear
    Cap_Alpha   = 1u << 2, ///< can store an alpha channel
    Cap_Lossy   = 1u << 3, ///< moves samples even at maximum quality
    Cap_Levels  = 1u << 4, ///< records a PQ or HLG image's content light level and mastering display
    Cap_White   = 1u << 5, ///< records the luminance the values' (1, 1, 1) stands for
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

/// One setting of a format's options, and what it promises.
struct Variant
{
    const char *name;
    const char *ext;
    unsigned    caps;
    float       step;   ///< how far a code value may move: the quantization step, or the lossy codec's error
    Offers      offers; ///< the transfer functions its dialog offers, each of which it must label
    Save        save;
};

constexpr float k_8bit  = 1.f / 255.f; ///< a code value step at 8 bits, which rounding stays within
constexpr float k_10bit = 1.f / 1023.f;

bool offers_linear(TransferFunction tf) { return tf.type == TransferFunction::Linear; }

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
    {"exr", ".exr", Cap_Default | Cap_Float | Cap_Alpha | Cap_White, 1e-3f, offers_linear,
     [](const Image &i, std::ostream &o, TransferFunction) { save_exr_image(i, o, "a.exr"); }},
    {"exr float zip", ".exr", Cap_Float | Cap_Alpha | Cap_White, 1e-6f, offers_linear, save_exr<0, Imf::ZIP_COMPRESSION>},
    {"exr half rle", ".exr", Cap_Float | Cap_Alpha | Cap_White, 1e-3f, offers_linear, save_exr<1, Imf::RLE_COMPRESSION>},
    {"exr half dwaa", ".exr", Cap_Float | Cap_Alpha | Cap_White | Cap_Lossy, 0.02f, offers_linear, save_exr<1, Imf::DWAA_COMPRESSION>},
    {"pfm", ".pfm", Cap_Default | Cap_Float, 1e-6f, pfm_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction) { save_pfm_image(i, o, "a.pfm"); }},
    {"hdr_stb", ".hdr", Cap_Default | Cap_Float, 0.008f, [](TransferFunction t) { return stb_offers_tf(t, true); },
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_hdr(i, o, "a.hdr"); }},
    {"png8", ".png", Cap_Default | Cap_Alpha | k_png_levels, k_8bit, png_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", false, false, false, t); }},
    {"png8 interlaced", ".png", Cap_Alpha | k_png_levels, k_8bit, png_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", false, true, false, t); }},
    {"png16", ".png", Cap_Alpha | k_png_levels, 1e-4f, png_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", false, false, true, t); }},
    {"png_stb", ".png", Cap_Default | Cap_Alpha, k_8bit, [](TransferFunction t) { return stb_offers_tf(t, false); },
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_png(i, o, "a.png", false); }},
    {"tga_stb", ".tga", Cap_Default | Cap_Alpha, k_8bit, [](TransferFunction t) { return stb_offers_tf(t, false); },
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_tga(i, o, "a.tga", false); }},
    {"bmp_stb", ".bmp", Cap_Default, k_8bit, [](TransferFunction t) { return stb_offers_tf(t, false); },
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_bmp(i, o, "a.bmp", false); }},
    {"jpg_stb", ".jpg", Cap_Default | Cap_Lossy, 0.03f, [](TransferFunction t) { return stb_offers_tf(t, false); },
     [](const Image &i, std::ostream &o, TransferFunction) { save_stb_jpg(i, o, "a.jpg", false); }},
#if HDRVIEW_ENABLE_LIBTIFF
    // the save dialog's compressions (0 none, 1 LZW, 2 ZIP, 3 PackBits) and data types (0 8-bit, 1 16-bit, 2 float);
    // an ICC profile, which lcms reads in float, moves float samples by a few 1e-6
    {"tiff8 lzw", ".tif", Cap_Default | Cap_Alpha, k_8bit, [](TransferFunction t) { return tiff_offers_tf(t, 0); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 1, 0); }},
    {"tiff8 none", ".tif", Cap_Alpha, k_8bit, [](TransferFunction t) { return tiff_offers_tf(t, 0); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 0, 0); }},
    {"tiff16 zip", ".tif", Cap_Alpha, 1e-4f, [](TransferFunction t) { return tiff_offers_tf(t, 1); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 2, 1); }},
    {"tiff16 packbits", ".tif", Cap_Alpha, 1e-4f, [](TransferFunction t) { return tiff_offers_tf(t, 1); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 3, 1); }},
    {"tiff float lzw", ".tif", Cap_Float | Cap_Alpha, 1e-5f, [](TransferFunction t) { return tiff_offers_tf(t, 2); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 1, 2); }},
#endif
    {"qoi", ".qoi", Cap_Default | Cap_Alpha, k_8bit, qoi_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_qoi_image(i, o, "a.qoi", t.type == TransferFunction::sRGB, false); }},
#if HDRVIEW_ENABLE_LIBJPEG
    {"jpg q95", ".jpg", Cap_Default | Cap_Lossy, 0.03f, jpg_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction)
     { save_jpg_image(i, o, "a.jpg", false, 95, false); }},
    {"jpg q60 progressive", ".jpg", Cap_Lossy, 0.06f, jpg_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction)
     { save_jpg_image(i, o, "a.jpg", false, 60, true); }},
#endif
#if HDRVIEW_ENABLE_J2K
    // both syntaxes, every bit depth the dialog offers, and its lossy mode
    {"jp2 16", ".jp2", Cap_Default | Cap_Alpha, 1e-4f, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", true, 16, J2KContainer::JP2, t, false); }},
    {"jp2 12", ".jp2", Cap_Alpha, 1.f / 4095.f, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", true, 12, J2KContainer::JP2, t, false); }},
    {"jp2 10", ".jp2", Cap_Alpha, k_10bit, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", true, 10, J2KContainer::JP2, t, false); }},
    {"jp2 8", ".jp2", Cap_Alpha, k_8bit, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", true, 8, J2KContainer::JP2, t, false); }},
    {"jp2 16 lossy", ".jp2", Cap_Alpha | Cap_Lossy, 0.03f, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::JP2); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.jp2", false, 16, J2KContainer::JP2, t, false); }},
    {"j2k 16", ".j2k", Cap_Alpha, 1e-4f, [](TransferFunction t) { return j2k_offers_tf(t, J2KContainer::J2K); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_j2k_image(i, o, "a.j2k", true, 16, J2KContainer::J2K, t, false); }},
#endif
#if HDRVIEW_ENABLE_LIBWEBP
    // lossy WebP is 8-bit 4:2:0, whose error at quality 95 a steep transfer function such as Log 100 amplifies
    {"webp q95", ".webp", Cap_Default | Cap_Alpha | Cap_Lossy, 0.05f, webp_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_webp_image(i, o, "a.webp", 95.f, false, t); }},
    {"webp lossless", ".webp", Cap_Alpha, k_8bit, webp_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_webp_image(i, o, "a.webp", 100.f, true, t); }},
#endif
#if HDRVIEW_ENABLE_LIBJXL && !defined(HDRVIEW_TEST_ASAN) // see test_asan.h
    // a gamma decodes through the ICC profile libjxl synthesizes, whose fixed-point curve moves a lossless
    // file's samples by about 1e-4
    {"jxl f32 q95", ".jxl", Cap_Default | Cap_Alpha | Cap_Lossy, 0.03f, [](TransferFunction t) { return jxl_offers_tf(t); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", false, 95.f, t, JXL_TYPE_FLOAT); }},
    {"jxl f32 lossless", ".jxl", Cap_Alpha, 3e-4f, [](TransferFunction t) { return jxl_offers_tf(t); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", true, 100.f, t, JXL_TYPE_FLOAT); }},
    {"jxl f16 lossless", ".jxl", Cap_Alpha, 1e-3f, [](TransferFunction t) { return jxl_offers_tf(t); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", true, 100.f, t, JXL_TYPE_FLOAT16); }},
    {"jxl u16 lossless", ".jxl", Cap_Alpha, 3e-4f, [](TransferFunction t) { return jxl_offers_tf(t); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", true, 100.f, t, JXL_TYPE_UINT16); }},
    {"jxl u8 lossless", ".jxl", Cap_Alpha, k_8bit, [](TransferFunction t) { return jxl_offers_tf(t); },
     [](const Image &i, std::ostream &o, TransferFunction t) { save_jxl_image(i, o, "a.jxl", true, 100.f, t, JXL_TYPE_UINT8); }},
#endif
#if HDRVIEW_ENABLE_LIBHEIF
    // both codecs the dialog offers, since they take different paths through libheif; "lossless" still converts
    // to YCbCr, which moves samples by a few steps
    {"heif", ".heif", Cap_Default | Cap_Alpha | Cap_Lossy | k_heif_levels, 0.04f, heif_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.heif", 95, false, true, HEIFCodec::HEVC, t); }},
    {"heif lossless 8", ".heif", Cap_Alpha | Cap_Lossy | k_heif_levels, 4 * k_8bit, heif_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.heif", 100, true, true, HEIFCodec::HEVC, t, 8); }},
    {"heif lossless 10", ".heif", Cap_Alpha | Cap_Lossy | k_heif_levels, 4 * k_10bit, heif_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.heif", 100, true, true, HEIFCodec::HEVC, t, 10); }},
    {"avif", ".avif", Cap_Default | Cap_Alpha | Cap_Lossy | k_heif_levels, 0.04f, heif_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.avif", 95, false, true, HEIFCodec::AV1, t); }},
    {"avif lossless 8", ".avif", Cap_Alpha | Cap_Lossy | k_heif_levels, 4 * k_8bit, heif_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.avif", 100, true, true, HEIFCodec::AV1, t, 8); }},
    {"avif lossless 10", ".avif", Cap_Alpha | Cap_Lossy | k_heif_levels, 4 * k_10bit, heif_offers_tf,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_heif_image(i, o, "a.avif", 100, true, true, HEIFCodec::AV1, t, 10); }},
    {"avif lossless 12", ".avif", Cap_Alpha | Cap_Lossy | k_heif_levels, 4.f / 4095.f, heif_offers_tf,
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
// clang-format on

/// An image of \p layout, distinct per channel and at unit \p brightness too dim to clamp when unpremultiplied.
ImagePtr make_image(const Layout &layout, float alpha, float brightness = 1.f)
{
    // not square: a transposed stride is invisible in a square image
    const int2 size{8, 6};
    auto       img = std::make_shared<Image>();
    for (auto name : layout.channels) img->channels.emplace_back(name, size);
    img->display_window = img->data_window = Box2i{int2{0}, size};

    float v = 0.12f;
    for (size_t c = 0; c < layout.channels.size(); ++c)
    {
        const bool  is_alpha = std::string(layout.channels[c]) == "A";
        const float value    = is_alpha ? alpha : v * brightness;
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) img->channels[c](x, y) = value;
        if (!is_alpha)
            v += 0.13f;
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

/// The warning \p variant's dialog shows for \p tf, for the sweep's image, whose primaries are BT.2020's.
Caveat caveat_of(const Variant &variant, const TransferFunction &tf)
{
    const std::string ext = variant.ext;
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

/// The layout sweep's relative tolerance, on straight color at the default setting.
float tolerance(const Variant &v)
{
    if (v.caps & Cap_Lossy)
        return 0.04f;
    return (v.caps & Cap_Float) ? 2e-3f : 0.012f; // 8-bit writers quantize, and some dither
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

            auto img = make_image(layout, /*alpha*/ 0.75f);

            // a float format records no transfer function and is written linear; the integer ones
            // round-trip theirs and are written sRGB, as the save dialog defaults them
            const TransferFunction tf = (writer.caps & Cap_Float) ? TransferFunction{TransferFunction::Linear}
                                                                  : TransferFunction{TransferFunction::sRGB};

            std::ostringstream out(std::ios::binary);
            // PFM stores 1, 3 or 4 channels and has no two-channel form; everything else takes everything
            if (std::string{writer.name} == "pfm" && layout.channels.size() == 2)
            {
                CHECK_THROWS(writer.save(*img, out, tf));
                continue;
            }

            // every cell is judged on its own, or the first failure hides every cell after it
            std::string save_error;
            try
            {
                writer.save(*img, out, tf);
            }
            catch (const std::exception &e)
            {
                save_error = e.what();
            }
            // which codecs libheif can encode depends on the plugins the build found; the Windows CPM
            // build has no HEVC encoder at all
            if (save_error.find("no encoder available") != std::string::npos)
                continue;
            CHECK_MESSAGE(save_error.empty(), "save threw: ", save_error);
            if (!save_error.empty() || out.str().empty())
                continue;

            // the app hands loaders a SpanIStream over bytes in memory or a FileIStream for a file on disk,
            // which they may read without a copy or through the file itself; each must decode what a plain
            // stream does
            const std::string     bytes = out.str();
            const std::string     name  = std::string("a") + writer.ext;
            const auto            path  = std::filesystem::temp_directory_path() / ("hdrview_roundtrip" + name);
            std::string           load_error;
            std::vector<ImagePtr> reloaded, from_span, from_disk;
            try
            {
                std::istringstream in(bytes, std::ios::binary);
                reloaded = load_image(in, name);

                SpanIStream span{bytes};
                from_span = load_image(span, name);

                std::ofstream{path, std::ios::binary}.write(bytes.data(), bytes.size());
                FileIStream file{path};
                from_disk = load_image(file, name);
            }
            catch (const std::exception &e)
            {
                load_error = e.what();
            }
            std::error_code ec;
            std::filesystem::remove(path, ec);
            CHECK_MESSAGE(load_error.empty(), "load threw: ", load_error);
            if (!load_error.empty())
                continue;
            CHECK(reloaded.size() == 1);
            if (reloaded.size() != 1)
                continue;
            for (const auto *other : {&from_span, &from_disk})
            {
                REQUIRE(other->size() == 1);
                const auto a = hdrview_test::samples(reloaded[0]), b = hdrview_test::samples(other->front());
                // bitwise, so NaNs compare equal
                CHECK((a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0));
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

            const float3 want = straight_rgb(img->rgba_pixel(int2{1, 0}, Target_Primary));
            const float4 got4 = reloaded[0]->rgba_pixel(int2{1, 0}, Target_Primary);
            const float3 got  = straight_rgb(got4);
            const float  tol  = tolerance(writer);

            for (int c = 0; c < 3; ++c) CHECK(got[c] == doctest::Approx(want[c]).epsilon(tol));

            // alpha only where the format can hold one, and only where the group carries one
            const bool group_alpha = group_has_alpha(img->groups[img->selected_group].type);
            if (group_alpha && (writer.caps & Cap_Alpha))
                CHECK(got4[3] == doctest::Approx(0.75f).epsilon(tol));
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
        // the transfer function the dialog opens with, linear for a float format and sRGB otherwise, is always offered
        CHECK(variant.offers(
            TransferFunction{(variant.caps & Cap_Float) ? TransferFunction::Linear : TransferFunction::sRGB}));

        for (const auto &tf : tfs)
        {
            if (!variant.offers(tf))
                continue;
            INFO("transfer function = ", transfer_function_name(tf));

            // for an HDR transfer function, an image brightened 8x so its channels straddle SDR white, which one
            // clipping there misses
            const bool hdr_tf    = tf.type == TransferFunction::BT2100_PQ || tf.type == TransferFunction::BT2100_HLG;
            auto       img       = make_image(k_layouts[3] /* RGB */, /*alpha*/ 1.f, hdr_tf ? 8.f : 1.f);
            img->chromaticities  = bt2020;
            img->compute_color_transform();
            const float4 want = img->rgba_pixel(int2{1, 0}, Target_Primary);

            // what a setting that has room for it records: a white luminance, and for an HDR transfer function the
            // display the image was mastered on
            const MasteringDisplay mastered{bt2020, k_mastering_display_min_nits, 1000.f};
            if (hdr_tf)
                add_mastering_display(img->metadata["header"], mastered.primaries, mastered.min_nits, mastered.max_nits,
                                      "test");
            else
                add_white_luminance(img->metadata["header"], 150.f, "test");

            std::ostringstream out(std::ios::binary);
            std::string        error;
            try
            {
                variant.save(*img, out, tf);
            }
            catch (const std::exception &e)
            {
                error = e.what();
            }
            if (error.find("no encoder available") != std::string::npos)
                continue; // which codecs libheif can encode depends on the plugins the build found
            CHECK_MESSAGE(error.empty(), "save threw: ", error);
            if (!error.empty())
                continue;

            std::istringstream    in(out.str(), std::ios::binary);
            std::vector<ImagePtr> reloaded;
            try
            {
                reloaded = load_image(in, std::string("a") + variant.ext);
            }
            catch (const std::exception &e)
            {
                error = e.what();
            }
            CHECK_MESSAGE(error.empty(), "load threw: ", error);
            if (!error.empty() || reloaded.size() != 1)
                continue;
            const auto &back = reloaded[0];

            // Compared in code values, where the setting's step is a step whatever the level, and in the primaries the
            // samples were quantized in: the image's own when the file kept and recorded them, sRGB's when it
            // converted. A file read back with the wrong transfer function or primaries is off by far more.
            const bool kept = back->chromaticities && approx_equal(*back->chromaticities, bt2020, 1e-3f);
            const auto M    = kept ? inverse(img->M_to_sRGB) : float3x3{la::identity};
            const auto got  = from_linear(mul(M, back->rgba_pixel(int2{1, 0}, Target_Primary).xyz()), tf);
            const auto ref  = from_linear(mul(M, want.xyz()), tf);
            for (int c = 0; c < 3; ++c) CHECK(std::abs(got[c] - ref[c]) <= variant.step);

#if defined(__APPLE__)
            // and as macOS reads it, which is how every application there sees the file
            if (const auto decoded = hdrview_test::apple_decode(out.str()))
            {
                const float4 apple  = (*decoded)(1, 0);
                const auto   a_code = from_linear(apple.xyz(), tf), w_code = from_linear(want.xyz(), tf);
                float      worst = 0.f;
                for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(a_code[c] - w_code[c]));
                // A setting whose dialog does not warn that macOS misreads it must read within Apple's 8-bit-or-so
                // precision; one that warns must be off by more than half that, or the warning has gone stale.
                const float tolerance = std::max(variant.step, 0.025f);
                if (caveat_of(variant, tf).readers & Misreader_macOS)
                    CHECK_MESSAGE(worst > tolerance / 2,
                                  "the dialog warns that macOS misreads this file, which it reads "
                                  "right: the warning is stale");
                else
                    CHECK_MESSAGE(worst <= tolerance, "macOS reads this file as other values, off by ", worst,
                                  ", and the dialog does not warn of it");
            }
#endif
            const json header = back->metadata.value("header", json::object());
            if (!hdr_tf && (variant.caps & Cap_White))
                CHECK(white_luminance(header).value_or(0.f) == doctest::Approx(150.f));
            if (hdr_tf && (variant.caps & Cap_Levels))
            {
                // the content light level, measured here from the channels themselves
                double     brightest = 0., total = 0.;
                const int2 size = img->size();
                for (int y = 0; y < size.y; ++y)
                    for (int x = 0; x < size.x; ++x)
                    {
                        float pixel = 0.f;
                        for (int c = 0; c < 3; ++c) pixel = std::max(pixel, img->channels[c](x, y));
                        brightest = std::max(brightest, (double)pixel);
                        total += pixel;
                    }
                const auto cll = content_light_level(header);
                REQUIRE(cll);
                CHECK(cll->max_cll == doctest::Approx(brightest * HDR_REFERENCE_WHITE_NITS).epsilon(0.01));
                CHECK(cll->max_fall ==
                      doctest::Approx(total / (size.x * size.y) * HDR_REFERENCE_WHITE_NITS).epsilon(0.01));

                const auto md = mastering_display(header);
                REQUIRE(md);
                for (auto [g, w] : {std::pair{md->primaries.red, mastered.primaries.red},
                                    std::pair{md->primaries.green, mastered.primaries.green},
                                    std::pair{md->primaries.blue, mastered.primaries.blue},
                                    std::pair{md->primaries.white, mastered.primaries.white}})
                    CHECK(la::maxelem(la::abs(g - w)) <= 1e-4f);
                CHECK(md->max_nits == doctest::Approx(mastered.max_nits));
                CHECK(md->min_nits == doctest::Approx(mastered.min_nits));
            }
#if HDRVIEW_ENABLE_LIBJXL
            // JPEG XL has only the peak, as its intensity target
            if (hdr_tf && std::string{variant.ext} == ".jxl")
                CHECK(header.at("intensity target").at("value").get<float>() == doctest::Approx(mastered.max_nits));
#endif
        }
    }
}
