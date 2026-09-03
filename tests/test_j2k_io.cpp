//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "colorspace.h"
#include "image.h"
#include "imageio/j2k.h"

#include "test_support.h"

#include <openjpeg.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace hdrview_test;

namespace
{

/// Options that leave the decoded samples as the codestream stored them, normalized to [0,1].
ImageLoadOptions raw_load_options()
{
    ImageLoadOptions opts;
    opts.override_profile = true;
    opts.tf_override      = TransferFunction::Linear;
    opts.gamut_override   = ColorGamut_sRGB_BT709;
    return opts;
}

/// One component of a codestream to be built by encode_with_openjpeg().
struct Component
{
    uint32_t             prec = 8;
    bool                 sgnd = false;
    uint32_t             dx = 1, dy = 1;
    std::vector<int32_t> samples; ///< dx/dy-subsampled, so ceil(w/dx) x ceil(h/dy) of them
};

struct OutStream
{
    std::string bytes;
    size_t      pos = 0;
};

OPJ_SIZE_T out_write(void *buffer, OPJ_SIZE_T count, void *user_data)
{
    auto *o = static_cast<OutStream *>(user_data);
    if (o->pos + count > o->bytes.size())
        o->bytes.resize(o->pos + count);
    std::memcpy(&o->bytes[o->pos], buffer, count);
    o->pos += count;
    return count;
}

OPJ_OFF_T out_skip(OPJ_OFF_T count, void *user_data)
{
    auto *o = static_cast<OutStream *>(user_data);
    o->pos += (size_t)count;
    if (o->pos > o->bytes.size())
        o->bytes.resize(o->pos);
    return count;
}

OPJ_BOOL out_seek(OPJ_OFF_T offset, void *user_data)
{
    auto *o = static_cast<OutStream *>(user_data);
    o->pos  = (size_t)offset;
    if (o->pos > o->bytes.size())
        o->bytes.resize(o->pos);
    return OPJ_TRUE;
}

/// A lossless JPEG 2000 file holding `comps` verbatim, built by OpenJPEG rather than by our own writer.
/**
    Going straight to the library is what lets these cases cover component shapes HDRView's writer never
    emits: precisions other than 8, 12 and 16, signed samples, and subsampled components.
*/
std::string encode_with_openjpeg(int w, int h, const std::vector<Component> &comps, OPJ_COLOR_SPACE cs,
                                 OPJ_CODEC_FORMAT format)
{
    std::vector<opj_image_cmptparm_t> parms(comps.size());
    std::memset(parms.data(), 0, parms.size() * sizeof(opj_image_cmptparm_t));
    for (size_t c = 0; c < comps.size(); ++c)
    {
        parms[c].prec = comps[c].prec;
        parms[c].bpp  = comps[c].prec;
        parms[c].sgnd = comps[c].sgnd ? 1 : 0;
        parms[c].dx   = comps[c].dx;
        parms[c].dy   = comps[c].dy;
        parms[c].w    = (uint32_t)((w + comps[c].dx - 1) / comps[c].dx);
        parms[c].h    = (uint32_t)((h + comps[c].dy - 1) / comps[c].dy);
    }

    opj_image_t *img = opj_image_create((uint32_t)comps.size(), parms.data(), cs);
    REQUIRE(img != nullptr);
    img->x0 = img->y0 = 0;
    img->x1           = (uint32_t)w;
    img->y1           = (uint32_t)h;
    for (size_t c = 0; c < comps.size(); ++c)
    {
        REQUIRE(comps[c].samples.size() == (size_t)img->comps[c].w * img->comps[c].h);
        std::memcpy(img->comps[c].data, comps[c].samples.data(), comps[c].samples.size() * sizeof(int32_t));
    }

    opj_cparameters_t cp;
    opj_set_default_encoder_parameters(&cp);
    cp.tcp_numlayers  = 1;
    cp.cp_disto_alloc = 1;
    cp.tcp_rates[0]   = 0.f;
    cp.irreversible   = 0;
    cp.numresolution  = 1;

    opj_codec_t *codec = opj_create_compress(format);
    REQUIRE(codec != nullptr);
    REQUIRE(opj_setup_encoder(codec, &cp, img));

    OutStream     out;
    opj_stream_t *stream = opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_FALSE);
    REQUIRE(stream != nullptr);
    opj_stream_set_user_data(stream, &out, nullptr);
    opj_stream_set_write_function(stream, out_write);
    opj_stream_set_skip_function(stream, out_skip);
    opj_stream_set_seek_function(stream, out_seek);

    const bool ok =
        opj_start_compress(codec, img, stream) && opj_encode(codec, stream) && opj_end_compress(codec, stream);
    opj_stream_destroy(stream);
    opj_destroy_codec(codec);
    opj_image_destroy(img);
    REQUIRE(ok);

    out.bytes.resize(out.pos);
    return out.bytes;
}

/// A gray codestream whose samples run over the whole range `prec` and `sgnd` can hold.
std::string ramp_codestream(uint32_t prec, bool sgnd, int w, int h, std::vector<int32_t> &wrote,
                            OPJ_CODEC_FORMAT format = OPJ_CODEC_JP2)
{
    const int64_t top = ((int64_t)1 << (prec - (sgnd ? 1 : 0))) - 1;
    const int64_t bot = sgnd ? -top : 0;

    wrote.clear();
    for (int i = 0; i < w * h; ++i) wrote.push_back((int32_t)(bot + (top - bot) * i / std::max(1, w * h - 1)));

    return encode_with_openjpeg(w, h, {{prec, sgnd, 1, 1, wrote}}, OPJ_CLRSPC_GRAY, format);
}

/// Wraps `payload` in a box of type `type`.
std::string box(const char type[5], const std::string &payload)
{
    std::string out;
    put(out, (uint32_t)(8 + payload.size()), Endian::Big);
    out += std::string(type, 4);
    return out + payload;
}

/// Splices extra top-level boxes into a JP2 file, just before its codestream box.
std::string with_extra_boxes(const std::string &jp2, const std::string &extra)
{
    const size_t at = jp2.find("jp2c");
    REQUIRE(at != std::string::npos);
    return jp2.substr(0, at - 4) + extra + jp2.substr(at - 4);
}

/// A little-endian TIFF holding one IFD0 entry, which is what an EXIF block is.
std::string exif_blob(uint16_t tag, uint16_t value)
{
    std::string out;
    out += "II";
    put(out, (uint16_t)42);
    put(out, (uint32_t)8);
    put(out, (uint16_t)1);
    put(out, tag);
    put(out, (uint16_t)3); // SHORT
    put(out, (uint32_t)1);
    put(out, value);
    put(out, (uint16_t)0);
    put(out, (uint32_t)0);
    return out;
}

/// The body of a 'uuid' box: the sixteen identifying bytes followed by the payload.
std::string uuid_payload(const uint8_t uuid[16], const std::string &payload)
{
    return std::string((const char *)uuid, 16) + payload;
}

constexpr uint8_t k_xmp_uuid[16]  = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
                                     0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};
constexpr uint8_t k_exif_uuid[16] = {0x4A, 0x70, 0x67, 0x54, 0x69, 0x66, 0x66, 0x45,
                                     0x78, 0x69, 0x66, 0x2D, 0x3E, 0x4A, 0x50, 0x32};

} // namespace

TEST_CASE("Component precision and signedness decode to the range they encode")
{
    // the reader takes any precision up to 31, but OpenJPEG's reversible encoder overflows past 24, so the
    // sweep stops where a codestream can still be built
    for (uint32_t prec : {1u, 2u, 4u, 8u, 10u, 12u, 16u, 24u})
        for (bool sgnd : {false, true})
        {
            if (prec == 1 && sgnd)
                continue; // a signed one-bit component has no magnitude bits

            CAPTURE(prec);
            CAPTURE(sgnd);

            std::vector<int32_t> wrote;
            const auto           bytes = ramp_codestream(prec, sgnd, 8, 4, wrote);
            const auto           img   = load_bytes(load_j2k_image, bytes, "ramp.jp2", raw_load_options());
            REQUIRE(img);
            REQUIRE(img->channels.size() == 1);
            REQUIRE(img->channels[0].bits_per_sample == (int)prec);

            const double top = (double)(((int64_t)1 << (prec - (sgnd ? 1 : 0))) - 1);
            for (size_t i = 0; i < wrote.size(); ++i)
                CHECK(img->channels[0](int(i)) == doctest::Approx(wrote[i] / top).epsilon(1e-6));
        }
}

TEST_CASE("A subsampled component is upsampled onto the reference grid")
{
    const int w = 8, h = 4;

    // luma at full rate, chroma at half in both directions, all three constant per chroma sample so the
    // expected value at every pixel is the chroma sample covering it
    std::vector<int32_t> y(w * h), cb((w / 2) * (h / 2)), cr((w / 2) * (h / 2));
    for (int i = 0; i < w * h; ++i) y[i] = 128;
    for (size_t i = 0; i < cb.size(); ++i)
    {
        cb[i] = int32_t(128 + i);
        cr[i] = int32_t(128 - (int)i);
    }

    const auto bytes = encode_with_openjpeg(w, h, {{8, false, 1, 1, y}, {8, false, 2, 2, cb}, {8, false, 2, 2, cr}},
                                            OPJ_CLRSPC_SRGB, OPJ_CODEC_JP2);
    const auto img   = load_bytes(load_j2k_image, bytes, "subsampled.jp2", raw_load_options());
    REQUIRE(img);
    REQUIRE(img->channels.size() == 3);

    for (int py = 0; py < h; ++py)
        for (int px = 0; px < w; ++px)
        {
            CAPTURE(px);
            CAPTURE(py);
            const size_t c = (size_t)(py / 2) * (w / 2) + px / 2;
            CHECK(img->channels[1](px, py) == doctest::Approx(cb[c] / 255.).epsilon(1e-5));
            CHECK(img->channels[2](px, py) == doctest::Approx(cr[c] / 255.).epsilon(1e-5));
        }
}

TEST_CASE("Both JPEG 2000 syntaxes decode to the same pixels")
{
    std::vector<int32_t> wrote;
    const auto           jp2 = ramp_codestream(12, false, 8, 4, wrote, OPJ_CODEC_JP2);
    const auto           j2k = ramp_codestream(12, false, 8, 4, wrote, OPJ_CODEC_J2K);

    const auto from_jp2 = load_bytes(load_j2k_image, jp2, "ramp.jp2", raw_load_options());
    const auto from_j2k = load_bytes(load_j2k_image, j2k, "ramp.j2k", raw_load_options());
    REQUIRE(from_jp2);
    REQUIRE(from_j2k);
    CHECK(samples(from_jp2) == samples(from_j2k));

    // only the boxed syntax records a brand, and only it can carry a color space or metadata
    CHECK(from_jp2->metadata["header"].contains("Brand"));
    CHECK_FALSE(from_j2k->metadata["header"].contains("Brand"));
}

TEST_CASE("A lossy write is smaller than a lossless one and stays close to the samples")
{
    const int2 size{64, 64};
    // smooth content, since a lossy codec's error bound says nothing about a synthetic discontinuity: at
    // 20:1 OpenJPEG moves a per-pixel sawtooth by the whole range
    auto img = test_image(size, 3, [&](int c, int x, int y)
                          { return 0.1f + 0.8f * float(x + y + 4 * c) / float(size.x + size.y + 8); });

    std::ostringstream lossless(std::ios::binary), lossy(std::ios::binary);
    save_j2k_image(*img, lossless, "a.jp2", 1.f, true, 8, J2KContainer::JP2, TransferFunction::Linear, false);
    save_j2k_image(*img, lossy, "b.jp2", 1.f, false, 8, J2KContainer::JP2, TransferFunction::Linear, false);
    CHECK(lossy.str().size() < lossless.str().size());

    const auto decoded = load_bytes(load_j2k_image, lossy.str(), "b.jp2", raw_load_options());
    REQUIRE(decoded);

    double worst = 0., total = 0.;
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                const double e = std::abs(double(decoded->channels[c](x, y) - img->channels[c](x, y)));
                worst          = std::max(worst, e);
                total += e;
            }
    CHECK(worst > 0.);
    CHECK(worst < 0.05);
    CHECK(total / (3 * size.x * size.y) < 0.01);
}

TEST_CASE("The sniff accepts both JPEG 2000 syntaxes and nothing else")
{
    std::vector<int32_t> wrote;
    for (const auto &bytes :
         {ramp_codestream(8, false, 4, 4, wrote, OPJ_CODEC_JP2), ramp_codestream(8, false, 4, 4, wrote, OPJ_CODEC_J2K)})
    {
        std::istringstream in(bytes, std::ios::binary);
        CHECK(is_j2k_image(in));
        CHECK(in.tellg() == std::streampos(0));
    }

    // a JPEG SOI, a PNG signature, an ISOBMFF ftyp and an empty file are all near misses
    for (const std::string &other : {std::string("\xFF\xD8\xFF\xE0", 4), std::string("\x89PNG\r\n\x1A\n", 8),
                                     std::string("\0\0\0\x18"
                                                 "ftypavif",
                                                 12),
                                     std::string()})
    {
        std::istringstream in(other, std::ios::binary);
        CHECK_FALSE(is_j2k_image(in));
    }
}

TEST_CASE("A JP2's EXIF and XMP boxes reach the metadata the info panel reads")
{
    std::vector<int32_t> wrote;
    const auto           plain = ramp_codestream(8, false, 4, 4, wrote, OPJ_CODEC_JP2);

    const std::string xmp = "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                            "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
                            "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
                            "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" "
                            "xmp:CreatorTool=\"HDRView\"/>\n"
                            "</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";

    // writers disagree over whether the EXIF box repeats JPEG's "Exif\0\0" marker before the TIFF header
    for (const std::string prefix : {std::string{}, std::string("Exif\0\0", 6)})
    {
        CAPTURE(prefix.size());

        // 0x0112 is Orientation, and 3 is a 180-degree rotation
        const auto with_meta =
            with_extra_boxes(plain, box("uuid", uuid_payload(k_exif_uuid, prefix + exif_blob(0x0112, 3))) +
                                        box("uuid", uuid_payload(k_xmp_uuid, xmp)));

        // through load_image(), since finalize() is what turns the raw buffers into the metadata tree
        const auto img = load_bytes(with_meta, "meta.jp2");
        REQUIRE(img);

        REQUIRE(img->metadata.contains("exif"));
        REQUIRE(img->metadata["exif"].is_object());
        CHECK(img->metadata["exif"].dump().find("Orientation") != std::string::npos);

        REQUIRE(img->metadata.contains("xmp"));
        REQUIRE(img->metadata["xmp"].is_object());
        CHECK(img->metadata["xmp"]["xmp"]["CreatorTool"] == "HDRView");
    }
}

TEST_CASE("A JP2 file's ICC profile and XMP reach the image")
{
    std::vector<int32_t> wrote;
    const auto           plain = ramp_codestream(8, false, 4, 4, wrote, OPJ_CODEC_JP2);

    const std::string xmp = "<?xpacket begin=\"\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\"></x:xmpmeta>";
    std::string       uuid_payload(16, '\0');
    const uint8_t     xmp_uuid[16] = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
                                      0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};
    std::memcpy(&uuid_payload[0], xmp_uuid, 16);

    const auto img = load_bytes(load_j2k_image, with_extra_boxes(plain, box("uuid", uuid_payload + xmp)), "meta.jp2");
    REQUIRE(img);
    CHECK(std::string(img->xmp_data.begin(), img->xmp_data.end()) == xmp);
}

TEST_CASE("A JP2 whose codestream OpenJPEG cannot read as a JP2 still decodes from its jp2c box")
{
    std::vector<int32_t> wrote;
    auto                 jp2 = ramp_codestream(8, false, 4, 4, wrote, OPJ_CODEC_JP2);

    // the brand is all that says which family member this is, and a reader that trusts it alone fails here
    const size_t brand_at = jp2.find("ftyp");
    REQUIRE(brand_at != std::string::npos);
    jp2.replace(brand_at + 4, 4, "jpm ");
    // and the header box is what OpenJPEG's JP2 reader needs, so without it only the fallback can work
    const size_t header_at = jp2.find("jp2h");
    REQUIRE(header_at != std::string::npos);
    jp2.replace(header_at, 4, "junk");

    const auto img = load_bytes(load_j2k_image, jp2, "compound.jpm", raw_load_options());
    REQUIRE(img);
    REQUIRE(img->channels.size() == 1);
    for (size_t i = 0; i < wrote.size(); ++i)
        CHECK(img->channels[0](int(i)) == doctest::Approx(wrote[i] / 255.).epsilon(1e-5));
}

TEST_CASE("Truncated and corrupted JPEG 2000 files are refused rather than crashing")
{
    std::vector<int32_t> wrote;
    const auto           valid = ramp_codestream(8, false, 16, 16, wrote, OPJ_CODEC_JP2);

    int decoded = 0, refused = 0;
    for (size_t cut = 1; cut < valid.size(); cut += 7)
    {
        CAPTURE(cut);
        try
        {
            if (load_bytes(load_j2k_image, valid.substr(0, cut), "cut.jp2"))
                ++decoded;
        }
        catch (const std::exception &)
        {
            ++refused;
        }
    }
    CHECK(decoded + refused > 0);

    for (size_t at = 0; at < valid.size(); at += 11)
    {
        CAPTURE(at);
        std::string flipped = valid;
        flipped[at]         = (char)(~(uint8_t)flipped[at]);
        try
        {
            load_bytes(load_j2k_image, flipped, "flipped.jp2");
        }
        catch (const std::exception &)
        {
        }
    }
}

TEST_CASE("A codestream declaring impossible dimensions is refused before it is decoded")
{
    std::vector<int32_t> wrote;
    auto                 j2k = ramp_codestream(8, false, 4, 4, wrote, OPJ_CODEC_J2K);

    // SIZ follows SOC: FF4F FF51 Lsiz(2) Rsiz(2) Xsiz(4) Ysiz(4)
    REQUIRE(j2k.size() > 16);
    for (int i = 0; i < 4; ++i)
    {
        j2k[8 + i]  = (char)0x7F;
        j2k[12 + i] = (char)0x7F;
    }

    CHECK_THROWS_AS(load_bytes(load_j2k_image, j2k, "huge.j2k"), std::exception);
}

#ifdef HDRVIEW_TEST_J2K_DIR

#include <filesystem>

// A corpus of real files, most derived from one source image, holding what encoders actually emit:
// palettes, tiles, ICC profiles, several precisions, HTJ2K codestreams, and deliberately damaged files.
TEST_CASE("Every file in a real JPEG 2000 corpus either decodes or is refused")
{
    namespace fs = std::filesystem;

    int decoded = 0, refused = 0, high_throughput = 0;
    for (const auto &entry : fs::directory_iterator(HDRVIEW_TEST_J2K_DIR))
    {
        if (!entry.is_regular_file())
            continue;

        const auto path = entry.path();
        CAPTURE(path.filename().string());

        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        if (!is_j2k_image(in))
            continue;

        try
        {
            const auto images = load_j2k_image(in, path.filename().string());
            REQUIRE_FALSE(images.empty());
            for (const auto &img : images)
            {
                CHECK(img->size().x > 0);
                CHECK(img->size().y > 0);
                CHECK_FALSE(img->channels.empty());
                for (const auto &ch : img->channels) CHECK(ch.size() == img->size());
                // each header entry is an object of value/string/type/description, as the info panel wants
                if (img->metadata["header"]["Block coder"].value("value", std::string{}) == "HTJ2K")
                    ++high_throughput;
            }
            ++decoded;
        }
        catch (const std::exception &)
        {
            ++refused;
        }
    }

    CAPTURE(decoded);
    CAPTURE(refused);
    CAPTURE(high_throughput);
    // a corpus that decoded nothing would pass every check above without testing anything
    CHECK(decoded > 0);
    CHECK(high_throughput > 0);
}

#endif // HDRVIEW_TEST_J2K_DIR
