//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file test_export_roundtrip.cpp

    Every writer against every channel-group layout HDRView can select for export.

    Each cell saves an image, reads it back, and compares against what Image::rgba_pixel() shows for it.
    The comparison is on straight (unpremultiplied) color, so a format that cannot store alpha is judged on
    the same footing as one that can, and alpha itself is checked only where it survives.
*/

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/exr.h"
#include "imageio/heif.h"
#include "imageio/image_loader.h"
#include "imageio/j2k.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/pfm.h"
#include "imageio/png.h"
#include "imageio/qoi.h"
#include "imageio/stb.h"
#include "imageio/tiff.h"
#include "imageio/webp.h"

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#if HDRVIEW_ENABLE_LIBJXL
#include <jxl/types.h>
#endif

// AddressSanitizer reports a spurious stack-use-after-scope inside libjxl's SizeHeader::Set ->
// FixedAspectRatios (lib/jxl/headers.cc): the in-bounds read kRatios[ratio - 1] of a function-local
// constexpr array the optimizer emitted into .rodata. No suppressions file can filter that class, so the
// JPEG-XL row drops out of the table under ASan alone.
#if defined(__SANITIZE_ADDRESS__)
#define HDRVIEW_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HDRVIEW_TEST_ASAN 1
#endif
#endif

namespace
{

enum Caps : unsigned
{
    Cap_Float = 1u << 0, ///< stores unbounded values, and is therefore written linear
    Cap_Alpha = 1u << 1, ///< can store an alpha channel
    Cap_Lossy = 1u << 2, ///< moves samples even at maximum quality
};

struct Writer
{
    const char *name;
    const char *ext;
    unsigned    caps;
    void (*save)(const Image &, std::ostream &, TransferFunction);
};

// clang-format off
const Writer k_writers[] = {
    {"exr", ".exr", Cap_Float | Cap_Alpha,
     // the default-argument form, which has to fall back to something sized for this image
     [](const Image &i, std::ostream &o, TransferFunction) { save_exr_image(i, o, "a.exr"); }},
    {"pfm", ".pfm", Cap_Float,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_pfm_image(i, o, "a.pfm", 1.f, t); }},
    {"hdr_stb", ".hdr", Cap_Float,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_hdr(i, o, "a.hdr", 1.f, t); }},
    {"png16", ".png", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", 1.f, false, false, true, t); }},
    {"png8", ".png", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", 1.f, false, false, false, t); }},
    {"png_stb", ".png", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_png(i, o, "a.png", 1.f, t); }},
    {"tga_stb", ".tga", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_tga(i, o, "a.tga", 1.f, t); }},
    {"bmp_stb", ".bmp", 0u,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_bmp(i, o, "a.bmp", 1.f, t); }},
    {"jpg_stb", ".jpg", Cap_Lossy,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_jpg(i, o, "a.jpg", 1.f, t); }},
#if HDRVIEW_ENABLE_LIBTIFF
    {"tiff8", ".tif", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", 1.f, t, 1, 0); }},
    {"tiff_float", ".tif", Cap_Float | Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", 1.f, t, 1, 2); }},
#endif
    {"qoi", ".qoi", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_qoi_image(i, o, "a.qoi", 1.f, t.type == TransferFunction::sRGB, false); }},
#if HDRVIEW_ENABLE_LIBJPEG
    {"jpg", ".jpg", Cap_Lossy,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_jpg_image(i, o, "a.jpg", 1.f, t.type == TransferFunction::sRGB, false, 100, false); }},
#endif
#if HDRVIEW_ENABLE_J2K
    // both syntaxes, and both ends of the bit depths the dialog offers
    {"jp2_16", ".jp2", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_j2k_image(i, o, "a.jp2", 1.f, true, 16, J2KContainer::JP2, t, false); }},
    {"jp2_8", ".jp2", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_j2k_image(i, o, "a.jp2", 1.f, true, 8, J2KContainer::JP2, t, false); }},
    {"j2k", ".j2k", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_j2k_image(i, o, "a.j2k", 1.f, true, 16, J2KContainer::J2K, t, false); }},
#endif
#if HDRVIEW_ENABLE_LIBWEBP
    {"webp", ".webp", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_webp_image(i, o, "a.webp", 1.f, 100.f, true, t); }},
#endif
#if HDRVIEW_ENABLE_LIBJXL && !defined(HDRVIEW_TEST_ASAN)
    {"jxl", ".jxl", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_jxl_image(i, o, "a.jxl", 1.f, /*lossless*/ true, 100.f, t, JXL_TYPE_UINT16); }},
#endif
#if HDRVIEW_ENABLE_LIBHEIF
    // both codecs the dialog offers, since they take different paths through libheif
    {"heif", ".heif", Cap_Alpha | Cap_Lossy,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_heif_image(i, o, "a.heif", 1.f, 100, true, true, HEIFCodec::HEVC, t); }},
    {"avif", ".avif", Cap_Alpha | Cap_Lossy,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_heif_image(i, o, "a.avif", 1.f, 100, true, true, HEIFCodec::AV1, t); }},
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

/// Values low enough that unpremultiplying on the way out cannot clamp, and distinct per channel.
ImagePtr make_image(const Layout &layout, float alpha)
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
        const float value    = is_alpha ? alpha : v;
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

float tolerance(const Writer &w)
{
    if (w.caps & Cap_Lossy)
        return 0.04f;
    return (w.caps & Cap_Float) ? 2e-3f : 0.012f; // 8-bit writers quantize, and some dither
}

} // namespace

TEST_CASE("every writer round-trips every channel-group layout as the viewport shows it")
{
    for (const auto &writer : k_writers)
        for (const auto &layout : k_layouts)
        {
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

            std::string           load_error;
            std::vector<ImagePtr> reloaded;
            try
            {
                std::istringstream in(out.str(), std::ios::binary);
                reloaded = load_image(in, std::string("a") + writer.ext);
            }
            catch (const std::exception &e)
            {
                load_error = e.what();
            }
            CHECK_MESSAGE(load_error.empty(), "load threw: ", load_error);
            if (!load_error.empty())
                continue;
            CHECK(reloaded.size() == 1);
            if (reloaded.size() != 1)
                continue;

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

TEST_CASE("every writer either converts a wide-gamut image to sRGB or records the gamut it kept")
{
    // as_interleaved() converts to sRGB unless a writer opts out, and one that opts out has to record the
    // primaries its samples are in; doing neither leaves a file that reads back as sRGB
    const Chromaticities bt2020{{0.708f, 0.292f}, {0.170f, 0.797f}, {0.131f, 0.046f}, {0.3127f, 0.3290f}};

    for (const auto &writer : k_writers)
    {
        INFO("writer = ", std::string{writer.name});

        auto img            = make_image(k_layouts[3] /* RGB */, /*alpha*/ 1.f);
        img->chromaticities = bt2020;
        color_conversion_matrix(img->M_to_sRGB, bt2020, gamut_chromaticities(ColorGamut_sRGB_BT709));

        const float4 want = img->rgba_pixel(int2{1, 0}, Target_Primary);

        const TransferFunction tf = (writer.caps & Cap_Float) ? TransferFunction{TransferFunction::Linear}
                                                              : TransferFunction{TransferFunction::sRGB};
        std::ostringstream     out(std::ios::binary);
        std::string            error;
        try
        {
            writer.save(*img, out, tf);
        }
        catch (const std::exception &e)
        {
            error = e.what();
        }
        if (error.find("no encoder available") != std::string::npos)
            continue; // see the note on codec availability in the layout table above
        CHECK_MESSAGE(error.empty(), "save threw: ", error);
        if (!error.empty())
            continue;

        std::istringstream    in(out.str(), std::ios::binary);
        std::vector<ImagePtr> reloaded;
        try
        {
            reloaded = load_image(in, std::string("a") + writer.ext);
        }
        catch (const std::exception &e)
        {
            error = e.what();
        }
        CHECK_MESSAGE(error.empty(), "load threw: ", error);
        if (!error.empty() || reloaded.size() != 1)
            continue;

        const float4 got = reloaded[0]->rgba_pixel(int2{1, 0}, Target_Primary);
        for (int c = 0; c < 3; ++c) CHECK(got[c] == doctest::Approx(want[c]).epsilon(tolerance(writer)));
    }
}
