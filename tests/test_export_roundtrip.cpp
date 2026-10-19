//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file test_export_roundtrip.cpp

    Every writer against every channel-group layout HDRView can save.

    Each cell saves an image, reads it back, and compares against what Image::rgba_pixel() shows for it.
    The comparison is on straight (unpremultiplied) color, so a format that cannot store alpha is judged on
    the same footing as one that can, and alpha itself is checked only where it survives.
*/

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/exr.h"
#include "imageio/hdr_metadata.h"
#include "imageio/heif.h"
#include "imageio/image_loader.h"
#include "imageio/j2k.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/pfm.h"
#include "imageio/png.h"
#include "imageio/qoi.h"
#include "imageio/stb.h"
#include "imageio/streams.h"
#include "imageio/tiff.h"
#include "imageio/webp.h"
#include "test_support.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    Cap_Lossy  = 1u << 2, ///< moves samples even at maximum quality
    Cap_Curves = 1u << 3, ///< encodes every transfer function it can name, and records which
    Cap_Levels = 1u << 4, ///< records a PQ or HLG image's content light level and mastering display
    Cap_White  = 1u << 5, ///< records the luminance the values' (1, 1, 1) stands for
};

#if defined(PNG_cICP_SUPPORTED)
constexpr unsigned k_png_curves = Cap_Curves;
#else
constexpr unsigned k_png_curves = 0u;
#endif
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

struct Writer
{
    const char *name;
    const char *ext;
    unsigned    caps;
    void (*save)(const Image &, std::ostream &, TransferFunction);
};

// clang-format off
const Writer k_writers[] = {
    {"exr", ".exr", Cap_Float | Cap_Alpha | Cap_White,
     // the default-argument form, which has to fall back to something sized for this image
     [](const Image &i, std::ostream &o, TransferFunction) { save_exr_image(i, o, "a.exr"); }},
    {"pfm", ".pfm", Cap_Float,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_pfm_image(i, o, "a.pfm", t); }},
    {"hdr_stb", ".hdr", Cap_Float,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_hdr(i, o, "a.hdr", t); }},
    {"png16", ".png", Cap_Alpha | k_png_curves | k_png_levels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", false, false, true, t); }},
    {"png8", ".png", Cap_Alpha | k_png_curves | k_png_levels,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_png_image(i, o, "a.png", false, false, false, t); }},
    {"png_stb", ".png", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_png(i, o, "a.png", t); }},
    {"tga_stb", ".tga", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_tga(i, o, "a.tga", t); }},
    {"bmp_stb", ".bmp", 0u,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_bmp(i, o, "a.bmp", t); }},
    {"jpg_stb", ".jpg", Cap_Lossy,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_stb_jpg(i, o, "a.jpg", t); }},
#if HDRVIEW_ENABLE_LIBTIFF
    {"tiff8", ".tif", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 1, 0); }},
    {"tiff_float", ".tif", Cap_Float | Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_tiff_image(i, o, "a.tif", t, 1, 2); }},
#endif
    {"qoi", ".qoi", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_qoi_image(i, o, "a.qoi", t.type == TransferFunction::sRGB, false); }},
#if HDRVIEW_ENABLE_LIBJPEG
    {"jpg", ".jpg", Cap_Lossy,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_jpg_image(i, o, "a.jpg", t.type == TransferFunction::sRGB, false, 100, false); }},
#endif
#if HDRVIEW_ENABLE_J2K
    // both syntaxes, and both ends of the bit depths the dialog offers
    {"jp2_16", ".jp2", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_j2k_image(i, o, "a.jp2", true, 16, J2KContainer::JP2, t, false); }},
    {"jp2_10", ".jp2", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_j2k_image(i, o, "a.jp2", true, 10, J2KContainer::JP2, t, false); }},
    {"jp2_8", ".jp2", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_j2k_image(i, o, "a.jp2", true, 8, J2KContainer::JP2, t, false); }},
    {"j2k", ".j2k", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_j2k_image(i, o, "a.j2k", true, 16, J2KContainer::J2K, t, false); }},
#endif
#if HDRVIEW_ENABLE_LIBWEBP
    {"webp", ".webp", Cap_Alpha,
     [](const Image &i, std::ostream &o, TransferFunction t) { save_webp_image(i, o, "a.webp", 100.f, true, t); }},
#endif
#if HDRVIEW_ENABLE_LIBJXL && !defined(HDRVIEW_TEST_ASAN)
    {"jxl", ".jxl", Cap_Alpha | Cap_Curves,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_jxl_image(i, o, "a.jxl", /*lossless*/ true, 100.f, t, JXL_TYPE_UINT16); }},
#endif
#if HDRVIEW_ENABLE_LIBHEIF
    // both codecs the dialog offers, since they take different paths through libheif
    {"heif", ".heif", Cap_Alpha | Cap_Lossy | Cap_Curves | k_heif_levels,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_heif_image(i, o, "a.heif", 100, true, true, HEIFCodec::HEVC, t); }},
    {"avif", ".avif", Cap_Alpha | Cap_Lossy | Cap_Curves | k_heif_levels,
     [](const Image &i, std::ostream &o, TransferFunction t)
     { save_heif_image(i, o, "a.avif", 100, true, true, HEIFCodec::AV1, t); }},
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

TEST_CASE("every writer records the gamut and transfer function its samples are in, or converts to ones it can")
{
    // as_interleaved() converts to sRGB unless a writer opts out, and one that opts out has to record the
    // primaries its samples are in; doing neither leaves a file that reads back as sRGB. The same goes for a
    // transfer function: a writer that encodes any curve has to say which, or the file reads back as sRGB.
    const Chromaticities bt2020{{0.708f, 0.292f}, {0.170f, 0.797f}, {0.131f, 0.046f}, {0.3127f, 0.3290f}};

    for (const auto &writer : k_writers)
        for (int i = TransferFunction::Unspecified; i < TransferFunction::Count; ++i)
        {
            // Unspecified stands for the writer's default; one that encodes any curve is also tried on every
            // curve it can name (gamma takes a parameter, which no writer records)
            const auto curve = (TransferFunction::Type_)i;
            if (curve != TransferFunction::Unspecified &&
                (!(writer.caps & Cap_Curves) || curve == TransferFunction::Gamma))
                continue;
#if HDRVIEW_ENABLE_LIBJXL
            if (std::string{writer.name} == "jxl" && curve != TransferFunction::Unspecified && !jxl_supported_tf(curve))
                continue;
#endif
            INFO("writer = ", std::string{writer.name}, ", curve = ", transfer_function_name(curve));

            auto img            = make_image(k_layouts[3] /* RGB */, /*alpha*/ 1.f);
            img->chromaticities = bt2020;
            color_conversion_matrix(img->M_to_sRGB, bt2020, gamut_chromaticities(ColorGamut_sRGB_BT709));

            const float4 want = img->rgba_pixel(int2{1, 0}, Target_Primary);

            // what a writer that has room for it records: a white luminance, and for an HDR curve the display
            // the image was mastered on
            const bool hdr_curve = curve == TransferFunction::BT2100_PQ || curve == TransferFunction::BT2100_HLG;
            const MasteringDisplay mastered{bt2020, k_mastering_display_min_nits, 1000.f};
            if (hdr_curve)
                add_mastering_display(img->metadata["header"], mastered.primaries, mastered.min_nits, mastered.max_nits,
                                      "test");
            else
                add_white_luminance(img->metadata["header"], 150.f, "test");

            // the writer's default: linear for a float format, sRGB otherwise
            const TransferFunction tf = curve != TransferFunction::Unspecified ? TransferFunction{curve}
                                        : (writer.caps & Cap_Float) ? TransferFunction{TransferFunction::Linear}
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
            if (curve == TransferFunction::Unspecified)
                for (int c = 0; c < 3; ++c) CHECK(got[c] == doctest::Approx(want[c]).epsilon(tolerance(writer)));
            else
            {
                // in the curve's code values, where an 8-bit step is 1/255 whatever the level; a file read back
                // with the wrong curve is off by a tenth or more
                const float    step        = (writer.caps & Cap_Lossy) ? 0.02f : 2.f / 255.f;
                const float3x3 M_from_sRGB = inverse(img->M_to_sRGB);
                const float3   got_code    = from_linear(mul(M_from_sRGB, got.xyz()), tf);
                const float3   want_code   = from_linear(mul(M_from_sRGB, want.xyz()), tf);
                for (int c = 0; c < 3; ++c) CHECK(std::abs(got_code[c] - want_code[c]) <= step);
            }

            const json header = reloaded[0]->metadata.value("header", json::object());
            if (!hdr_curve && (writer.caps & Cap_White))
                CHECK(white_luminance(header).value_or(0.f) == doctest::Approx(150.f));
            if (hdr_curve && (writer.caps & Cap_Levels))
            {
                // the content light level of the samples, measured here from the channels themselves
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
                for (auto [got_xy, want_xy] : {std::pair{md->primaries.red, mastered.primaries.red},
                                               std::pair{md->primaries.green, mastered.primaries.green},
                                               std::pair{md->primaries.blue, mastered.primaries.blue},
                                               std::pair{md->primaries.white, mastered.primaries.white}})
                    CHECK(la::maxelem(la::abs(got_xy - want_xy)) <= 1e-4f);
                CHECK(md->max_nits == doctest::Approx(mastered.max_nits));
                CHECK(md->min_nits == doctest::Approx(mastered.min_nits));
            }
#if HDRVIEW_ENABLE_LIBJXL
            // JPEG XL has only the peak, as its intensity target
            if (hdr_curve && std::string{writer.name} == "jxl")
                CHECK(header.at("intensity target").at("value").get<float>() == doctest::Approx(mastered.max_nits));
#endif
        }
}
