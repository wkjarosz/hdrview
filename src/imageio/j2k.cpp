//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "imageio/j2k.h"

#include "app.h"
#include "fonts.h"
#include "imgui.h"
#include "imgui_ext.h"

/// Options for writing a JPEG 2000 file. \see j2k_parameters_gui()
struct J2KSaveOptions
{
    float            gain            = 1.f;
    TransferFunction tf              = TransferFunction::sRGB;
    bool             dither          = true;
    int              bit_depth_index = 2;
    bool             reversible      = true;
    float            ratio           = 20.f; ///< target compression ratio, ignored when reversible
    int              num_resolutions = 6;
    bool             mct             = true;
    J2KContainer     container       = J2KContainer::JP2;
};

static J2KSaveOptions s_opts;

#if !HDRVIEW_ENABLE_J2K

using namespace std;

json get_j2k_info() { return json{{"name", "OpenJPEG"}}; }

bool is_j2k_image(istream &) noexcept { return false; }

vector<ImagePtr> load_j2k_image(istream &, string_view, const ImageLoadOptions &)
{
    throw runtime_error("JPEG 2000 support not enabled in this build.");
}

J2KSaveOptions *j2k_parameters_gui(J2KContainer) { return &s_opts; }

void save_j2k_image(const Image &, ostream &, string_view, const J2KSaveOptions *)
{
    throw runtime_error("JPEG 2000 support not enabled in this build.");
}

void save_j2k_image(const Image &, ostream &, string_view, float, bool, int, J2KContainer, TransferFunction, bool)
{
    throw runtime_error("JPEG 2000 support not enabled in this build.");
}

#else

#include "colorspace.h"
#include "common.h"
#include "image.h"
#include "imageio/alpha.h"
#include "imageio/exif.h"
#include "imageio/icc.h"
#include "imageio/image_loader.h"
#include "timer.h"

#include <openjpeg.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <thread>

using namespace std;

namespace
{

constexpr int k_bit_depths[]   = {8, 12, 16};
constexpr int k_num_bit_depths = (int)(sizeof(k_bit_depths) / sizeof(k_bit_depths[0]));
/// The most components a codestream may declare, per ISO/IEC 15444-1.
constexpr uint32_t k_max_components = 16384;

/// The 12-byte JP2 signature box that opens every file in the JP2 family.
constexpr uint8_t k_jp2_signature[12] = {0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A};

// UUIDs under which the JP2 family stores EXIF and XMP in a 'uuid' box
constexpr uint8_t k_exif_uuid[16]       = {0x4A, 0x70, 0x67, 0x54, 0x69, 0x66, 0x66, 0x45,
                                           0x78, 0x69, 0x66, 0x2D, 0x3E, 0x4A, 0x50, 0x32};
constexpr uint8_t k_exif_uuid_adobe[16] = {0x05, 0x37, 0xCD, 0xAB, 0x9D, 0x0C, 0x44, 0x31,
                                           0xA7, 0x2A, 0xFA, 0x56, 0x1F, 0x2A, 0x11, 0x3E};
constexpr uint8_t k_xmp_uuid[16]        = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
                                           0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};

uint16_t read_u16(const uint8_t *p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint64_t read_u64(const uint8_t *p) { return (uint64_t(read_u32(p)) << 32) | read_u32(p + 4); }

struct Bytes
{
    const uint8_t *data = nullptr;
    size_t         size = 0;

    bool  empty() const { return !data || size == 0; }
    Bytes subspan(size_t offset, size_t count) const { return {data + offset, count}; }
};

struct Box
{
    string_view type;
    Bytes       payload;
};

/// Reads the box starting at `in`, storing its full length (header included) in `total`.
/**
    Returns false at the end of a box sequence and on any malformed length, in which case `total` covers
    what is left so a caller stepping by it terminates.
*/
bool read_box(Bytes in, Box &box, size_t &total)
{
    total = in.size;
    if (in.size < 8)
        return false;

    size_t length = read_u32(in.data);
    size_t header = 8;
    if (length == 1)
    {
        if (in.size < 16)
            return false;
        length = (size_t)read_u64(in.data + 8);
        header = 16;
    }
    else if (length == 0)
        length = in.size;

    if (length < header || length > in.size)
        return false;

    box.type    = string_view{reinterpret_cast<const char *>(in.data) + 4, 4};
    box.payload = in.subspan(header, length - header);
    total       = length;
    return true;
}

/// What the JP2 boxes around a codestream say, for the parts OpenJPEG does not report itself.
struct Jp2Boxes
{
    string          brand;
    Bytes           icc;
    vector<uint8_t> exif;
    vector<uint8_t> xmp;
    vector<uint8_t> xml;
    vector<Bytes>   codestreams;
};

/// Takes the restricted ICC profile out of a 'colr' box body, for the paths OpenJPEG does not parse itself.
/**
    Its enumerated color space needs no reading here: OpenJPEG reports that one as opj_image_t::color_space.
*/
void parse_colr(Bytes box, Jp2Boxes &out)
{
    const uint8_t meth = box.size >= 3 ? box.data[0] : 0;
    if ((meth == 2 || meth == 3) && box.size > 3 && out.icc.empty())
        out.icc = box.subspan(3, box.size - 3);
}

/// Walks the top-level boxes, descending only into 'jp2h', where the color specification lives.
void parse_jp2_boxes(Bytes in, Jp2Boxes &out, int depth = 0)
{
    Box    box;
    size_t total = 0;
    for (Bytes rest = in; !rest.empty(); rest = rest.subspan(total, rest.size - total))
    {
        if (!read_box(rest, box, total))
            break;

        if (box.type == "ftyp" && box.payload.size >= 4)
            out.brand.assign(reinterpret_cast<const char *>(box.payload.data), 4);
        else if (box.type == "jp2c")
            out.codestreams.push_back(box.payload);
        else if (box.type == "colr")
            parse_colr(box.payload, out);
        else if (box.type == "xml " && out.xml.empty())
            out.xml.assign(box.payload.data, box.payload.data + box.payload.size);
        else if (box.type == "uuid" && box.payload.size > 16)
        {
            const Bytes body = box.payload.subspan(16, box.payload.size - 16);
            if (memcmp(box.payload.data, k_xmp_uuid, 16) == 0)
                out.xmp.assign(body.data, body.data + body.size);
            else if (memcmp(box.payload.data, k_exif_uuid, 16) == 0 ||
                     memcmp(box.payload.data, k_exif_uuid_adobe, 16) == 0)
                out.exif.assign(body.data, body.data + body.size);
        }
        else if (box.type == "jp2h" && depth < 4)
            parse_jp2_boxes(box.payload, out, depth + 1);
    }
}

/// Rsiz bit 14 marks a codestream that needs a Part-15 (high-throughput) decoder.
bool codestream_is_high_throughput(Bytes cs)
{
    return cs.size >= 8 && cs.data[0] == 0xFF && cs.data[1] == 0x4F && cs.data[2] == 0xFF && cs.data[3] == 0x51 &&
           (read_u16(cs.data + 6) & 0x4000) != 0;
}

struct MemStream
{
    Bytes  bytes;
    size_t pos = 0;
};

OPJ_SIZE_T mem_read(void *buffer, OPJ_SIZE_T count, void *user_data)
{
    auto *m = static_cast<MemStream *>(user_data);
    if (m->pos >= m->bytes.size)
        return (OPJ_SIZE_T)-1;

    OPJ_SIZE_T n = std::min(count, (OPJ_SIZE_T)(m->bytes.size - m->pos));
    memcpy(buffer, m->bytes.data + m->pos, n);
    m->pos += n;
    return n;
}

OPJ_OFF_T mem_skip(OPJ_OFF_T count, void *user_data)
{
    auto *m = static_cast<MemStream *>(user_data);
    if (count < 0)
        return -1;

    m->pos = std::min(m->pos + (size_t)count, m->bytes.size);
    return (OPJ_OFF_T)m->pos;
}

OPJ_BOOL mem_seek(OPJ_OFF_T offset, void *user_data)
{
    auto *m = static_cast<MemStream *>(user_data);
    if (offset < 0 || (size_t)offset > m->bytes.size)
        return OPJ_FALSE;

    m->pos = (size_t)offset;
    return OPJ_TRUE;
}

struct OutStream
{
    vector<uint8_t> bytes;
    size_t          pos = 0;
};

OPJ_SIZE_T out_write(void *buffer, OPJ_SIZE_T count, void *user_data)
{
    auto *o = static_cast<OutStream *>(user_data);
    if (o->pos + count > o->bytes.size())
        o->bytes.resize(o->pos + count);
    memcpy(o->bytes.data() + o->pos, buffer, count);
    o->pos += count;
    return count;
}

OPJ_OFF_T out_skip(OPJ_OFF_T count, void *user_data)
{
    auto *o = static_cast<OutStream *>(user_data);
    if (count < 0)
        return -1;

    o->pos += (size_t)count;
    if (o->pos > o->bytes.size())
        o->bytes.resize(o->pos);
    return count;
}

OPJ_BOOL out_seek(OPJ_OFF_T offset, void *user_data)
{
    auto *o = static_cast<OutStream *>(user_data);
    if (offset < 0)
        return OPJ_FALSE;

    o->pos = (size_t)offset;
    if (o->pos > o->bytes.size())
        o->bytes.resize(o->pos);
    return OPJ_TRUE;
}

void log_opj_message(const char *msg, spdlog::level::level_enum level)
{
    if (!msg)
        return;
    string_view text{msg};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
    if (!text.empty())
        spdlog::log(level, "OpenJPEG: {}", text);
}

void install_handlers(opj_codec_t *codec)
{
    opj_set_error_handler(codec, [](const char *msg, void *) { log_opj_message(msg, spdlog::level::debug); }, nullptr);
    opj_set_warning_handler(codec, [](const char *msg, void *) { log_opj_message(msg, spdlog::level::warn); }, nullptr);
    opj_set_info_handler(codec, [](const char *msg, void *) { log_opj_message(msg, spdlog::level::trace); }, nullptr);
}

using CodecPtr  = unique_ptr<opj_codec_t, void (*)(opj_codec_t *)>;
using StreamPtr = unique_ptr<opj_stream_t, void (*)(opj_stream_t *)>;
using OpjImgPtr = unique_ptr<opj_image_t, void (*)(opj_image_t *)>;

const char *color_space_name(OPJ_COLOR_SPACE cs)
{
    switch (cs)
    {
    case OPJ_CLRSPC_SRGB: return "sRGB";
    case OPJ_CLRSPC_GRAY: return "grayscale";
    case OPJ_CLRSPC_SYCC: return "sYCC";
    case OPJ_CLRSPC_EYCC: return "e-sYCC";
    case OPJ_CLRSPC_CMYK: return "CMYK";
    case OPJ_CLRSPC_UNSPECIFIED: return "unspecified";
    default: return "unknown";
    }
}

const char *progression_order_name(OPJ_PROG_ORDER order)
{
    switch (order)
    {
    case OPJ_LRCP: return "LRCP";
    case OPJ_RLCP: return "RLCP";
    case OPJ_RPCL: return "RPCL";
    case OPJ_PCRL: return "PCRL";
    case OPJ_CPRL: return "CPRL";
    default: return "unknown";
    }
}

json header_entry(const json &value, string_view str, string_view type, string_view description)
{
    return json{{"value", value}, {"string", str}, {"type", type}, {"description", description}};
}

/// Reads component `c` of `img` at reference-grid pixel (x, y), normalized to [0,1] or [-1,1] when signed.
/**
    A component may be subsampled, carrying its own origin and spacing on the reference grid and fewer
    samples than the image has pixels.
*/
float component_value(const opj_image_t *img, uint32_t c, int x, int y)
{
    const opj_image_comp_t &comp = img->comps[c];

    const int64_t sx = ((int64_t)img->x0 + x) / (int64_t)comp.dx - (int64_t)comp.x0;
    const int64_t sy = ((int64_t)img->y0 + y) / (int64_t)comp.dy - (int64_t)comp.y0;
    const int64_t cx = std::clamp<int64_t>(sx >> comp.factor, 0, (int64_t)comp.w - 1);
    const int64_t cy = std::clamp<int64_t>(sy >> comp.factor, 0, (int64_t)comp.h - 1);

    const uint32_t magnitude_bits = comp.prec - (comp.sgnd ? 1u : 0u);
    const double   scale          = 1.0 / (double)(((uint64_t)1 << magnitude_bits) - 1ull);
    return float(comp.data[cy * (int64_t)comp.w + cx] * scale);
}

/// Converts sYCC to RGB in place, over the first three of `n` interleaved channels.
void sycc_to_rgb(float *pixels, size_t num_pixels, int n, bool chroma_is_signed)
{
    const float offset = chroma_is_signed ? 0.f : 0.5f;
    for (size_t i = 0; i < num_pixels; ++i)
    {
        float      *p = pixels + i * n;
        const float y = p[0], cb = p[1] - offset, cr = p[2] - offset;
        p[0] = y + 1.402f * cr;
        p[1] = y - 0.344136f * cb - 0.714136f * cr;
        p[2] = y + 1.772f * cb;
    }
}

} // namespace

json get_j2k_info() { return json{{"enabled", true}, {"name", "OpenJPEG"}, {"version", opj_version()}}; }

/// Whether `is` opens with either JPEG 2000 syntax.
/**
    OpenJPEG publishes no such test: opj_create_decompress() has to be told which syntax to expect, and the
    magic bytes that decide it live in its command-line tools rather than in the library.
*/
bool is_j2k_image(istream &is) noexcept
{
    auto start = is.tellg();
    bool ret   = false;
    try
    {
        uint8_t magic[12] = {0};
        is.read(reinterpret_cast<char *>(magic), sizeof(magic));
        const auto got = (size_t)is.gcount();

        // a bare codestream opens with SOC immediately followed by SIZ; a JP2-family file with the
        // signature box
        if (got >= 4 && magic[0] == 0xFF && magic[1] == 0x4F && magic[2] == 0xFF && magic[3] == 0x51)
            ret = true;
        else if (got >= sizeof(k_jp2_signature) && memcmp(magic, k_jp2_signature, sizeof(k_jp2_signature)) == 0)
            ret = true;
    }
    catch (...)
    {
    }

    is.clear();
    is.seekg(start);
    return ret;
}

namespace
{

/// Decodes one codestream into an Image, applying the color pipeline `opts` asks for.
/**
    `cs` is what the decoder reads, which for the JP2 syntax is the whole file rather than the codestream
    alone, so `syntax` names the bytes the codestream markers actually start at.
*/
ImagePtr decode_codestream(Bytes cs, Bytes syntax, OPJ_CODEC_FORMAT format, const Jp2Boxes &boxes, string_view filename,
                           const ImageLoadOptions &opts)
{
    CodecPtr codec{opj_create_decompress(format), opj_destroy_codec};
    if (!codec)
        throw runtime_error{"Failed to create a JPEG 2000 decoder."};
    install_handlers(codec.get());

    opj_dparameters_t params;
    opj_set_default_decoder_parameters(&params);
    if (!opj_setup_decoder(codec.get(), &params))
        throw runtime_error{"Failed to set up the JPEG 2000 decoder."};
    opj_codec_set_threads(codec.get(), (int)std::max(1u, std::thread::hardware_concurrency()));
    // a truncated codestream is worth as much of an image as it holds, which is what a viewer wants
    opj_decoder_set_strict_mode(codec.get(), OPJ_FALSE);

    MemStream ms{cs, 0};
    StreamPtr stream{opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_TRUE), opj_stream_destroy};
    if (!stream)
        throw runtime_error{"Failed to create a JPEG 2000 stream."};
    opj_stream_set_user_data(stream.get(), &ms, nullptr);
    opj_stream_set_user_data_length(stream.get(), cs.size);
    opj_stream_set_read_function(stream.get(), mem_read);
    opj_stream_set_skip_function(stream.get(), mem_skip);
    opj_stream_set_seek_function(stream.get(), mem_seek);

    OpjImgPtr img{nullptr, opj_image_destroy};
    if (opj_image_t *raw = nullptr; opj_read_header(stream.get(), codec.get(), &raw) && raw)
        img.reset(raw);
    else
        throw invalid_argument{"Failed to read the JPEG 2000 header."};

    if (img->x1 <= img->x0 || img->y1 <= img->y0 || img->numcomps == 0)
        throw invalid_argument{"JPEG 2000 image has an empty image region or no components."};
    check_image_dimensions((int64_t)img->x1 - img->x0, (int64_t)img->y1 - img->y0, "JPEG 2000");
    if (img->numcomps > k_max_components)
        throw invalid_argument{fmt::format("JPEG 2000 image declares {} components.", img->numcomps)};

    if (!opj_decode(codec.get(), stream.get(), img.get()))
        throw runtime_error{"Failed to decode the JPEG 2000 image."};
    opj_end_decompress(codec.get(), stream.get());

    for (uint32_t c = 0; c < img->numcomps; ++c)
        if (!img->comps[c].data || img->comps[c].w == 0 || img->comps[c].h == 0 || img->comps[c].prec == 0 ||
            img->comps[c].prec > 31 || img->comps[c].dx == 0 || img->comps[c].dy == 0)
            throw invalid_argument{fmt::format("JPEG 2000 component {} was not decoded.", c)};

    const int2 size{(int)(img->x1 - img->x0), (int)(img->y1 - img->y0)};

    // OpenJPEG hands over the profile it read from the 'colr' box, but only for the syntaxes whose boxes it
    // parses, and it also parks CIELab parameters in the same field under a length of zero
    vector<uint8_t> icc;
    if (img->icc_profile_buf && img->icc_profile_len > 0)
        icc.assign(img->icc_profile_buf, img->icc_profile_buf + img->icc_profile_len);
    else if (!boxes.icc.empty())
        icc.assign(boxes.icc.data, boxes.icc.data + boxes.icc.size);

    OPJ_COLOR_SPACE cs_enum = img->color_space;
    if (cs_enum == OPJ_CLRSPC_UNSPECIFIED || cs_enum == OPJ_CLRSPC_UNKNOWN)
        cs_enum = img->numcomps <= 2 ? OPJ_CLRSPC_GRAY : OPJ_CLRSPC_SRGB;

    const bool cmyk = cs_enum == OPJ_CLRSPC_CMYK && img->numcomps >= 4;
    const bool gray = cs_enum == OPJ_CLRSPC_GRAY || img->numcomps < 3;
    // ICCProfile turns four ink channels into RGB through the file's own profile, the way the JPEG XL loader
    // does. Without one there is nothing to convert them with, so they keep their own names and their values.
    const bool cmyk_to_rgb = cmyk && img->numcomps == 4 && !icc.empty() && ICCProfile(icc).is_CMYK();
    if (cmyk && !cmyk_to_rgb)
        spdlog::warn("No CMYK ICC profile to convert this image's ink channels with; leaving them as they are.");

    // the cdef box names the alpha component, which OpenJPEG reports per component and orders last; with no
    // cdef, a second or fourth component is alpha by convention. CMYK's fourth is ink, not opacity.
    uint32_t num_color = cmyk ? 4u : (gray ? 1u : 3u);
    bool     has_alpha = false;
    for (uint32_t c = 0; c < img->numcomps; ++c)
        if (img->comps[c].alpha)
            has_alpha = true;
    if (!has_alpha && !cmyk)
        has_alpha = img->numcomps == num_color + 1;
    // a cdef box can name an alpha component without leaving a spare one, and the color components win
    if (num_color + (has_alpha ? 1u : 0u) > img->numcomps)
        has_alpha = false;

    const uint32_t num_extra = img->numcomps - num_color - (has_alpha ? 1u : 0u);

    vector<string> names;
    if (cmyk && !cmyk_to_rgb)
        names.insert(names.end(), {"C", "M", "Y", "K"});
    else if (num_color == 1)
        names.push_back("Y");
    else
    {
        names.insert(names.end(), {"R", "G", "B"});
        // the conversion writes RGB over the four ink channels and a fourth that is opaque everywhere
        if (cmyk_to_rgb)
            names.push_back("A");
    }
    if (has_alpha)
        names.push_back("A");
    for (uint32_t c = 0; c < num_extra; ++c) names.push_back(fmt::format("extra.{}", c));

    const int  nc    = (int)names.size();
    const auto image = make_shared<Image>(size, names);
    image->filename  = filename;
    // JPEG 2000 stores straight alpha unless a cdef box says otherwise, and OpenJPEG does not report that
    image->set_transparency(has_alpha ? TransparencyType_Straight : TransparencyType_None,
                            transparency_override_of(opts));
    image->metadata["loader"] = "openjpeg";

    uint32_t min_prec = img->comps[0].prec, max_prec = img->comps[0].prec;
    bool     signed_samples = false, subsampled = false;
    for (uint32_t c = 0; c < img->numcomps; ++c)
    {
        min_prec       = std::min(min_prec, img->comps[c].prec);
        max_prec       = std::max(max_prec, img->comps[c].prec);
        signed_samples = signed_samples || img->comps[c].sgnd;
        subsampled     = subsampled || img->comps[c].dx != 1 || img->comps[c].dy != 1;
    }
    image->set_bits_per_sample((int)max_prec);
    image->metadata["pixel format"] =
        min_prec == max_prec ? fmt::format("{} channel{} x {}-bit {}", img->numcomps, img->numcomps == 1 ? "" : "s",
                                           max_prec, signed_samples ? "signed" : "unsigned")
                             : fmt::format("{} channels x {}-{}-bit {}", img->numcomps, min_prec, max_prec,
                                           signed_samples ? "signed" : "unsigned");

    auto &header = image->metadata["header"];
    header["Color space"] =
        header_entry(color_space_name(cs_enum), color_space_name(cs_enum), "string", "Color space of the codestream");
    const bool high_throughput = codestream_is_high_throughput(syntax);
    header["Block coder"] =
        header_entry(high_throughput ? "HTJ2K" : "JPEG 2000", high_throughput ? "high-throughput (Part 15)" : "Part 1",
                     "string", "Block coder the codestream needs");
    if (!boxes.brand.empty())
        header["Brand"] = header_entry(boxes.brand, boxes.brand, "string", "Major brand from the file type box");
    if (subsampled)
    {
        set<string> distinct;
        for (uint32_t c = 0; c < img->numcomps; ++c)
            distinct.insert(fmt::format("{}:{}", img->comps[c].dx, img->comps[c].dy));
        string ratios;
        for (const auto &r : distinct) ratios += (ratios.empty() ? "" : ", ") + r;
        header["Subsampling"] = header_entry(ratios, ratios, "string", "Component sampling on the reference grid");
    }

    if (opj_codestream_info_v2 *info = opj_get_cstr_info(codec.get()))
    {
        const auto &tile = info->m_default_tile_info;
        if (info->tw > 1 || info->th > 1)
        {
            const string tiles = fmt::format("{} x {} tiles of {} x {}", info->tw, info->th, info->tdx, info->tdy);
            header["Tiling"]   = header_entry(tiles, tiles, "string", "Tile grid of the codestream");
        }
        header["Quality layers"] =
            header_entry(tile.numlayers, fmt::format("{}", tile.numlayers), "int", "Number of quality layers");
        header["Progression order"] = header_entry(progression_order_name((OPJ_PROG_ORDER)tile.prg),
                                                   progression_order_name((OPJ_PROG_ORDER)tile.prg), "string",
                                                   "Order in which the packets are stored");
        if (tile.tccp_info)
        {
            const uint32_t levels = tile.tccp_info[0].numresolutions > 0 ? tile.tccp_info[0].numresolutions - 1 : 0;
            header["Decomposition levels"] =
                header_entry(levels, fmt::format("{}", levels), "int", "Wavelet decomposition levels");
            const char *wavelet = tile.tccp_info[0].qmfbid ? "5/3 reversible" : "9/7 irreversible";
            header["Wavelet"]   = header_entry(wavelet, wavelet, "string", "Wavelet transform used");
        }
        opj_destroy_cstr_info(&info);
    }

    if (!boxes.exif.empty())
    {
        try
        {
            image->exif = Exif{boxes.exif};
            if (image->exif.valid())
                image->metadata["exif"] = image->exif.to_json();
        }
        catch (const exception &e)
        {
            spdlog::warn("Failed to parse EXIF data: {}", e.what());
        }
    }
    // an 'xml ' box is where several encoders put XMP, there being no XMP box in Part 1
    image->xmp_data = !boxes.xmp.empty() ? boxes.xmp : boxes.xml;

    Timer        timer;
    const size_t num_pixels = (size_t)size.x * size.y;
    // every component is its own channel, so this gather is already the layout the image wants
    vector<float> pixels(num_pixels * nc);

    const int block_size = std::max(1, 1024 * 1024 / std::max(1, size.x));
    parallel_for(blocked_range<int>(0, size.y, block_size),
                 [&](int begin_y, int end_y, int, int)
                 {
                     for (int y = begin_y; y < end_y; ++y)
                         for (int x = 0; x < size.x; ++x)
                         {
                             float *p = pixels.data() + ((size_t)y * size.x + x) * nc;
                             for (int c = 0; c < nc; ++c) p[c] = component_value(img.get(), (uint32_t)c, x, y);
                         }
                 });

    if ((cs_enum == OPJ_CLRSPC_SYCC || cs_enum == OPJ_CLRSPC_EYCC) && !gray && !cmyk)
        sycc_to_rgb(pixels.data(), num_pixels, nc, img->comps[1].sgnd != 0);

    const int3 buffer_size{size.x, size.y, nc};
    string     profile_desc;
    unpremultiply_before_transfer(pixels.data(), buffer_size, image->transparency);
    image->icc_data = icc;

    Chromaticities chr;
    if (opts.override_profile)
    {
        spdlog::info("Ignoring embedded color profile and linearizing using requested transfer function: {}",
                     transfer_function_name(opts.tf_override));
        if (linearize_pixels(pixels.data(), buffer_size, gamut_chromaticities(opts.gamut_override), opts.tf_override,
                             opts.keep_primaries, &profile_desc, &chr))
            image->chromaticities = chr;
        profile_desc += " (user override)";
    }
    else if (!image->icc_data.empty() &&
             ICCProfile(image->icc_data)
                 .linearize_pixels(pixels.data(), buffer_size, opts.keep_primaries, &profile_desc, &chr))
    {
        spdlog::info("Linearizing colors using ICC profile.");
        image->chromaticities = chr;
    }
    else if (linearize_pixels(pixels.data(), buffer_size, Chromaticities(), TransferFunction::sRGB, opts.keep_primaries,
                              &profile_desc, &chr))
    {
        // Part 1 signals no transfer function, and both enumerated RGB spaces it defines are sRGB-encoded
        spdlog::info("Linearizing colors using the sRGB transfer function: {}", profile_desc);
        image->chromaticities = chr;
    }
    repremultiply_after_transfer(pixels.data(), buffer_size, image->transparency);
    image->metadata["color profile"] = profile_desc;

    for (int c = 0; c < nc; ++c)
        image->channels[c].copy_from_interleaved(pixels.data(), size.x, size.y, nc, c, [](float v) { return v; });

    spdlog::debug("Decoding {}x{} JPEG 2000 image took {} seconds.", size.x, size.y, timer.elapsed() / 1000.f);
    return image;
}

} // namespace

vector<ImagePtr> load_j2k_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "J2K"};
    if (!is_j2k_image(is))
        throw invalid_argument{"Not a JPEG 2000 file"};

    is.clear();
    is.seekg(0, ios::end);
    const size_t raw_size = (size_t)is.tellg();
    is.seekg(0, ios::beg);

    vector<uint8_t> raw(raw_size);
    is.read(reinterpret_cast<char *>(raw.data()), raw_size);
    if ((size_t)is.gcount() != raw_size)
        throw invalid_argument{fmt::format("Failed to read {} bytes, got {}", raw_size, (size_t)is.gcount())};

    const Bytes all{raw.data(), raw_size};
    const bool  boxed =
        raw_size >= sizeof(k_jp2_signature) && memcmp(raw.data(), k_jp2_signature, sizeof(k_jp2_signature)) == 0;

    Jp2Boxes boxes;
    if (boxed)
        parse_jp2_boxes(all, boxes);

    ImGuiTextFilter filter{opts.channel_selector.c_str()};
    filter.Build();

    vector<ImagePtr> images;
    if (boxed)
    {
        // OpenJPEG's JP2 reader handles the whole file, applying its palette and channel definitions; the
        // extracted codestreams are the fallback for the family members it does not parse, such as JPM
        try
        {
            const Bytes first = boxes.codestreams.empty() ? all : boxes.codestreams.front();
            images.push_back(decode_codestream(all, first, OPJ_CODEC_JP2, boxes, filename, opts));
        }
        catch (const exception &e)
        {
            if (boxes.codestreams.empty())
                throw;
            spdlog::warn("Reading '{}' as a JP2 failed ({}); decoding its {} codestream(s) directly.", filename,
                         e.what(), boxes.codestreams.size());
        }
    }

    if (images.empty())
    {
        const vector<Bytes> streams = boxed ? boxes.codestreams : vector<Bytes>{all};
        for (size_t i = 0; i < streams.size(); ++i)
        {
            const string partname = streams.size() > 1 ? fmt::format("codestream {:04}", i) : "";
            if (!partname.empty() && !filter.PassFilter(partname.c_str()))
                continue;

            auto image      = decode_codestream(streams[i], streams[i], OPJ_CODEC_J2K, boxes, filename, opts);
            image->partname = partname;
            images.push_back(image);
        }
    }

    if (images.empty())
        throw invalid_argument{"No JPEG 2000 codestream could be decoded."};

    return images;
}

void save_j2k_image(const Image &img, ostream &os, string_view filename, const J2KSaveOptions *params)
{
    if (!params)
        throw invalid_argument("J2KSaveOptions pointer is null.");

    ScopedMDC mdc{"IO", "J2K"};
    Timer     timer;

    const int bit_depth = k_bit_depths[std::clamp(params->bit_depth_index, 0, k_num_bit_depths - 1)];

    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved<uint16_t>(&w, &h, &n, params->gain, params->tf, params->dither);
    if (!pixels || w <= 0 || h <= 0 || n < 1 || n > 4)
        throw runtime_error{"JPEG 2000: unsupported image dimensions or channel count"};

    const bool has_alpha = group_has_alpha(img.groups[img.selected_group].type);
    // one and two component images are grayscale and grayscale+alpha, but a two-channel U,V pair is neither,
    // so it pads out to RGB with a zero third component as the viewport draws it
    if (n == 2 && !has_alpha)
    {
        const size_t           num_pixels = (size_t)w * h;
        unique_ptr<uint16_t[]> widened{new uint16_t[num_pixels * 3]};
        for (size_t i = 0; i < num_pixels; ++i)
        {
            widened[i * 3 + 0] = pixels[i * 2 + 0];
            widened[i * 3 + 1] = pixels[i * 2 + 1];
            widened[i * 3 + 2] = 0;
        }
        pixels = std::move(widened);
        n      = 3;
    }

    opj_image_cmptparm_t cmptparm[4];
    memset(cmptparm, 0, sizeof(cmptparm));
    for (int c = 0; c < n; ++c)
    {
        cmptparm[c].prec = (OPJ_UINT32)bit_depth;
        cmptparm[c].bpp  = (OPJ_UINT32)bit_depth;
        cmptparm[c].sgnd = 0;
        cmptparm[c].dx   = 1;
        cmptparm[c].dy   = 1;
        cmptparm[c].w    = (OPJ_UINT32)w;
        cmptparm[c].h    = (OPJ_UINT32)h;
    }

    OpjImgPtr img_out{opj_image_create((OPJ_UINT32)n, cmptparm, n >= 3 ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY),
                      opj_image_destroy};
    if (!img_out)
        throw runtime_error{"JPEG 2000: failed to allocate the image"};

    img_out->x0 = 0;
    img_out->y0 = 0;
    img_out->x1 = (OPJ_UINT32)w;
    img_out->y1 = (OPJ_UINT32)h;
    if (has_alpha)
        img_out->comps[n - 1].alpha = 1;

    const int shift = 16 - bit_depth;
    for (int c = 0; c < n; ++c)
    {
        OPJ_INT32 *dst = img_out->comps[c].data;
        for (size_t i = 0, num = (size_t)w * h; i < num; ++i) dst[i] = (OPJ_INT32)(pixels[i * n + c] >> shift);
    }

    opj_cparameters_t cp;
    opj_set_default_encoder_parameters(&cp);
    cp.tcp_numlayers  = 1;
    cp.cp_disto_alloc = 1;
    cp.irreversible   = params->reversible ? 0 : 1;
    // OpenJPEG reads a rate of 0 as lossless
    cp.tcp_rates[0] = params->reversible ? 0.f : std::max(1.f, params->ratio);
    cp.tcp_mct      = (n >= 3 && params->mct) ? 1 : 0;
    // each decomposition halves the image, and OpenJPEG refuses a level count the smaller side cannot take
    int max_levels = 1;
    while ((std::min(w, h) >> max_levels) > 0 && max_levels < 32) ++max_levels;
    cp.numresolution = std::clamp(params->num_resolutions, 1, max_levels);

    CodecPtr codec{opj_create_compress(params->container == J2KContainer::J2K ? OPJ_CODEC_J2K : OPJ_CODEC_JP2),
                   opj_destroy_codec};
    if (!codec)
        throw runtime_error{"JPEG 2000: failed to create an encoder"};
    install_handlers(codec.get());

    if (!opj_setup_encoder(codec.get(), &cp, img_out.get()))
        throw runtime_error{"JPEG 2000: failed to set up the encoder"};
    opj_codec_set_threads(codec.get(), (int)std::max(1u, std::thread::hardware_concurrency()));

    OutStream out;
    StreamPtr stream{opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_FALSE), opj_stream_destroy};
    if (!stream)
        throw runtime_error{"JPEG 2000: failed to create a stream"};
    opj_stream_set_user_data(stream.get(), &out, nullptr);
    opj_stream_set_write_function(stream.get(), out_write);
    opj_stream_set_skip_function(stream.get(), out_skip);
    opj_stream_set_seek_function(stream.get(), out_seek);

    spdlog::info("Saving {}-channel, {}x{} pixels {}-bit {} JPEG 2000 image.", n, w, h, bit_depth,
                 params->reversible ? "lossless" : "lossy");

    if (!opj_start_compress(codec.get(), img_out.get(), stream.get()) || !opj_encode(codec.get(), stream.get()) ||
        !opj_end_compress(codec.get(), stream.get()))
        throw runtime_error{"JPEG 2000: failed to encode the image"};

    os.write(reinterpret_cast<const char *>(out.bytes.data()), (streamsize)out.bytes.size());
    if (!os.good())
        throw runtime_error{"JPEG 2000: failed to write the encoded image"};

    spdlog::info("Saved JPEG 2000 image to \"{}\" in {} seconds.", filename, timer.elapsed() / 1000.f);
}

void save_j2k_image(const Image &img, ostream &os, string_view filename, float gain, bool lossless, int bit_depth,
                    J2KContainer container, TransferFunction tf, bool dither)
{
    J2KSaveOptions opts;
    opts.gain       = gain;
    opts.tf         = tf;
    opts.dither     = dither;
    opts.reversible = lossless;
    opts.container  = container;
    opts.bit_depth_index =
        (int)(std::find(k_bit_depths, k_bit_depths + k_num_bit_depths, bit_depth) - k_bit_depths) % k_num_bit_depths;
    save_j2k_image(img, os, filename, &opts);
}

J2KSaveOptions *j2k_parameters_gui(J2KContainer container)
{
    s_opts.container = container;

    if (ImGui::PE::Begin("JPEG 2000 Save Options",
                         ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
    {
        ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthStretch);

        ImGui::PE::Entry(
            "Gain",
            [&]
            {
                ImGui::BeginGroup();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::IconButtonSize().x -
                                        ImGui::GetStyle().ItemInnerSpacing.x);
                auto changed = ImGui::SliderFloat("##Gain", &s_opts.gain, 0.1f, 10.0f);
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::IconButton(ICON_MY_EXPOSURE))
                    s_opts.gain = exp2f(hdrview()->exposure());
                ImGui::Tooltip("Set gain from the current viewport exposure value.");
                ImGui::EndGroup();
                return changed;
            },
            "Multiply the pixels by this value before saving.");

        ImGui::PE::Entry(
            "Transfer function",
            [&]
            {
                if (ImGui::BeginCombo("##Transfer function", transfer_function_name(s_opts.tf).c_str()))
                {
                    for (int i = TransferFunction::Linear; i <= TransferFunction::DCI_P3; ++i)
                    {
                        bool is_selected = (s_opts.tf.type == (TransferFunction::Type_)i);
                        if (ImGui::Selectable(
                                transfer_function_name({(TransferFunction::Type_)i, s_opts.tf.gamma}).c_str(),
                                is_selected))
                            s_opts.tf.type = (TransferFunction::Type_)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                return true;
            },
            "Encode the pixel values using this transfer function.\nWARNING: JPEG 2000 records no transfer "
            "function of its own, so other software will read this file as sRGB.");

        if (s_opts.tf.type == TransferFunction::Gamma)
            ImGui::PE::SliderFloat("Gamma", &s_opts.tf.gamma, 0.1f, 5.f, "%.3f", 0,
                                   "When using a gamma transfer function, this is the gamma value to use.");

        ImGui::PE::Combo("Bit depth", &s_opts.bit_depth_index,
                         "8-bit\0"
                         "12-bit\0"
                         "16-bit\0");
        ImGui::PE::Checkbox("Dither", &s_opts.dither);
        ImGui::PE::Checkbox("Lossless", &s_opts.reversible,
                            "Compress with the reversible 5/3 wavelet, which reproduces the samples exactly. "
                            "Turn off for the irreversible 9/7 wavelet at a chosen compression ratio.");
        if (!s_opts.reversible)
            ImGui::PE::SliderFloat("Compression ratio", &s_opts.ratio, 1.f, 200.f, "%.0f:1",
                                   ImGuiSliderFlags_Logarithmic, "Target ratio of uncompressed to compressed size.");
        ImGui::PE::SliderInt("Resolutions", &s_opts.num_resolutions, 1, 10, "%d", ImGuiSliderFlags_None,
                             "Number of resolution levels in the wavelet pyramid, which is what lets a decoder "
                             "read a reduced-size image without decoding all of it.");
        ImGui::PE::Checkbox("Component transform", &s_opts.mct,
                            "Decorrelate the three color components before the wavelet, which compresses RGB "
                            "images considerably better.");

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = J2KSaveOptions{};

    return &s_opts;
}

#endif // HDRVIEW_ENABLE_J2K
