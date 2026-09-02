//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "gainmap.h"

#include "colorspace.h"
#include "common.h"
#include "image.h"
#include "timer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <tinyxml2.h>

using namespace std;

bool is_apple_gainmap_type(string_view aux_type)
{
    const auto lower = to_lower(aux_type);
    return lower.find("apple") != string::npos && lower.find("hdrgainmap") != string::npos;
}

AppleGainmapParams apple_gainmap_params(const Exif &exif)
{
    AppleGainmapParams params;
    if (!exif.valid())
    {
        spdlog::warn("Apple gain map with no maker note to size it by; using the weakest reconstruction.");
        return params;
    }

    params.hdr_headroom = (float)exif.apple_makernote_value(0x21).value_or(params.hdr_headroom);
    params.hdr_gain     = (float)exif.apple_makernote_value(0x30).value_or(params.hdr_gain);
    return params;
}

GainmapImage gainmap_from_image(const Image &map)
{
    GainmapImage gm;
    if (map.channels.empty())
        return gm;

    gm.size     = map.channels.front().size();
    gm.channels = std::min((int)map.channels.size(), 3);
    gm.pixels.resize((size_t)gm.size.x * gm.size.y * gm.channels);

    for (int c = 0; c < gm.channels; ++c)
    {
        if (map.channels[c].size() != gm.size)
            return GainmapImage{};

        for (int y = 0; y < gm.size.y; ++y)
            for (int x = 0; x < gm.size.x; ++x)
                gm.pixels[((size_t)y * gm.size.x + x) * gm.channels + c] = map.channels[c](x, y);
    }

    return gm;
}

float AppleGainmapParams::stops() const
{
    // piecewise-linear fit relating the two maker-note fields to reconstruction strength, from
    // https://developer.apple.com/forums/thread/709331
    if (hdr_headroom < 1.f)
        return hdr_gain <= 0.01f ? -20.f * hdr_gain + 1.8f : -0.101f * hdr_gain + 1.601f;
    else
        return hdr_gain <= 0.01f ? -70.f * hdr_gain + 3.f : -0.303f * hdr_gain + 2.303f;
}

//! Sample \p src bilinearly at the location destination pixel (\p x, \p y) of a \p dst_size grid maps to.
static float sample_bilinear(const vector<float> &src, int2 src_size, int channels, int c, int2 dst_size, int x, int y)
{
    // align pixel centers, so upsampling doesn't shift the map half a destination pixel against the base
    const float sx = (x + 0.5f) * (float(src_size.x) / dst_size.x) - 0.5f;
    const float sy = (y + 0.5f) * (float(src_size.y) / dst_size.y) - 0.5f;

    const int   x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
    const float fx = sx - x0, fy = sy - y0;

    const auto at = [&](int xi, int yi)
    {
        xi = std::clamp(xi, 0, src_size.x - 1);
        yi = std::clamp(yi, 0, src_size.y - 1);
        return src[(size_t(yi) * src_size.x + xi) * channels + c];
    };

    const float top    = at(x0, y0) * (1.f - fx) + at(x0 + 1, y0) * fx;
    const float bottom = at(x0, y0 + 1) * (1.f - fx) + at(x0 + 1, y0 + 1) * fx;
    return top * (1.f - fy) + bottom * fy;
}

GainmapImage resample_gainmap(const GainmapImage &gainmap, int2 size, bool linearize)
{
    if (!gainmap.valid() || size.x <= 0 || size.y <= 0)
        return {};

    vector<float>        linearized;
    const vector<float> *src = &gainmap.pixels;
    if (linearize)
    {
        // Apple documents Rec. 709 for the map, but Preview's own output only reproduces under sRGB
        // (headroom fit 7.72 vs the maker note's 7.695; tev reads it as sRGB too). See commit 8b38d616.
        linearized = gainmap.pixels;
        for (auto &v : linearized) v = (float)sRGB_to_linear(v);
        src = &linearized;
    }

    GainmapImage out;
    out.size     = size;
    out.channels = gainmap.channels;
    out.pixels.resize((size_t)size.x * size.y * out.channels);

    parallel_for(blocked_range<int>(0, size.y, 128),
                 [&](int begin_y, int end_y, int, int)
                 {
                     for (int y = begin_y; y < end_y; ++y)
                         for (int x = 0; x < size.x; ++x)
                             for (int c = 0; c < out.channels; ++c)
                                 out.pixels[((size_t)y * size.x + x) * out.channels + c] =
                                     sample_bilinear(*src, gainmap.size, gainmap.channels, c, size, x, y);
                 });

    return out;
}

//! Copy \p count channels of \p src into \p image under \p prefix, as their own group.
static void append_group(Image &image, const GainmapImage &src, const char *prefix, int count)
{
    static const char *mono[] = {"Y", "A"};
    static const char *rgba[] = {"R", "G", "B", "A"};

    const int first = (int)image.channels.size();
    for (int c = 0; c < count; ++c)
        image.channels.emplace_back(string{prefix} + (count < 3 ? mono[c] : rgba[c]), src.size);

    parallel_for(blocked_range<int>(0, src.size.y, 128),
                 [&, first, count](int begin_y, int end_y, int, int)
                 {
                     for (int y = begin_y; y < end_y; ++y)
                         for (int x = 0; x < src.size.x; ++x)
                             for (int c = 0; c < count; ++c)
                                 image.channels[first + c](x, y) =
                                     src.pixels[((size_t)y * src.size.x + x) * src.channels + c];
                 });
}

void append_gainmap_channels(Image &image, const GainmapImage &gainmap)
{
    if (image.channels.empty() || !gainmap.valid())
        return;

    // name the channels the way Image does, so finalize() gathers them into a group like any other layer
    append_group(image, gainmap, "gainmap.", std::min(gainmap.channels, 4));
}

void append_base_rendition(Image &image, int num_base)
{
    if (image.channels.empty())
        return;

    const int2 size = image.channels.front().size();

    // color channels only; alpha is not part of the rendition the gain map converts
    std::vector<int> color;
    for (int c = 0; c < num_base && c < (int)image.channels.size(); ++c)
        if (image.channels[c].name != "A")
            color.push_back(c);

    if (color.empty())
        return;

    // "base", not "sdr": ISO's own vocabulary, and a base-HDR JPEG XL keeps its HDR rendition here
    static const char *mono[] = {"base.Y"};
    static const char *rgb[]  = {"base.R", "base.G", "base.B"};

    const int first = (int)image.channels.size();
    for (size_t c = 0; c < color.size(); ++c) image.channels.emplace_back(color.size() < 3 ? mono[c] : rgb[c], size);

    parallel_for(blocked_range<int>(0, size.y, 128),
                 [&, first](int begin_y, int end_y, int, int)
                 {
                     for (size_t c = 0; c < color.size(); ++c)
                     {
                         const auto &from = image.channels[color[c]];
                         auto       &to   = image.channels[first + (int)c];
                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < size.x; ++x) to(x, y) = from(x, y);
                     }
                 });
}

void apply_apple_gainmap(Image &image, const GainmapImage &gainmap, const AppleGainmapParams &params,
                         float target_stops, bool keep_renditions)
{
    if (image.channels.empty())
    {
        spdlog::warn("Gain map: base image has no channels; skipping.");
        return;
    }

    if (!gainmap.valid())
    {
        spdlog::warn("Gain map: decoded map is empty or malformed ({}x{}, {} channels); skipping.", gainmap.size.x,
                     gainmap.size.y, gainmap.channels);
        return;
    }

    const int2 size = image.channels.front().size();

    // captured before appending anything, so the scaling loop below covers only the base image's channels
    const int num_base = (int)image.channels.size();

    const GainmapImage full = resample_gainmap(gainmap, size, true);
    if (!full.valid())
        return;

    const float stops    = params.stops();
    const float applied  = std::clamp(stops, 0.f, target_stops);
    const float headroom = std::exp2(applied);

    image.metadata["header"]["Gain map"] = {
        {"value", "Apple"},
        {"string", "Apple (urn:com:apple:photo:aux:hdrgainmap)"},
        {"type", "string"},
        {"description", "Vendor format Apple used for HDR photos before ISO 21496-1."}};
    image.metadata["header"]["Gain map headroom"] = {
        {"value", stops},
        {"string", fmt::format("{:.3f} stops", stops)},
        {"type", "float"},
        {"description", "Stops of brightening the gain map asks for, derived from Apple maker-note tags "
                        "0x21 (HDR headroom) and 0x30 (HDR gain)."}};
    image.metadata["header"]["Gain map applied"] = {
        {"value", applied},
        {"string", applied < stops ? fmt::format("{:.3f} stops (limited from {:.3f})", applied, stops)
                                   : fmt::format("{:.3f} stops", applied)},
        {"type", "float"},
        {"description", "Stops actually reconstructed, after the target headroom in the image loading options."}};

    spdlog::info("Apple gain map: {}x{}x{}, asks for {:.3f} stops, applying {:.3f} (maker note 0x21={}, 0x30={})",
                 gainmap.size.x, gainmap.size.y, gainmap.channels, stops, applied, params.hdr_headroom,
                 params.hdr_gain);

    // keep the file's own renditions before the base is scaled out from under them
    if (keep_renditions)
    {
        append_base_rendition(image, num_base);
        append_gainmap_channels(image, full);
    }

    if (headroom <= 1.f)
    {
        spdlog::debug("Apple gain map: target headroom is {:.3f}; leaving base pixels alone.", headroom);
        return;
    }

    Timer timer;

    parallel_for(blocked_range<int>(0, size.y, 128),
                 [&, num_base, headroom](int begin_y, int end_y, int, int)
                 {
                     for (int c = 0; c < num_base; ++c)
                     {
                         // scaling alpha would change transparency, not brightness
                         if (image.channels[c].name == "A")
                             continue;

                         // a monochrome map drives every color channel; a per-channel one pairs up with
                         // the first three, and any beyond that reuse the last
                         const int p    = std::min(c, full.channels - 1);
                         auto     &base = image.channels[c];

                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < size.x; ++x)
                             {
                                 const float g = full.pixels[((size_t)y * size.x + x) * full.channels + p];
                                 base(x, y) *= 1.f + (headroom - 1.f) * g;
                             }
                     }
                 });

    spdlog::debug("Applying gain map took: {} seconds.", (timer.elapsed() / 1000.f));
}

// ------------------------------------------------------------------------------------------------
// ISO 21496-1
// ------------------------------------------------------------------------------------------------

namespace
{

//! Reads the big-endian fields of an ISO 21496-1 metadata blob, refusing to run off its end.
class IsoReader
{
public:
    IsoReader(const uint8_t *data, size_t size) : m_data(data), m_size(size) {}

    uint8_t  u8() { return read<uint8_t>(); }
    uint16_t u16() { return read<uint16_t>(); }
    uint32_t u32() { return read<uint32_t>(); }
    int32_t  i32() { return (int32_t)read<uint32_t>(); }

    //! A rational whose numerator and denominator are stored as a pair, per the spec.
    float rational_u() { return ratio((float)u32()); }
    float rational_i() { return ratio((float)i32()); }

    //! The same value where a single denominator, read once, is shared by every field.
    float over(float denominator, bool is_signed)
    {
        const float n = is_signed ? (float)i32() : (float)u32();
        return n / denominator;
    }

    size_t consumed() const { return m_pos; }

private:
    float ratio(float numerator)
    {
        const auto d = (float)u32();
        if (d == 0.f)
            throw invalid_argument{"ISO 21496-1: rational with a zero denominator"};

        return numerator / d;
    }

    template <typename T>
    T read()
    {
        if (m_pos + sizeof(T) > m_size)
            throw invalid_argument{fmt::format("ISO 21496-1: metadata ends after {} bytes, mid-field", m_size)};

        T v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v = (T)((v << 8) | m_data[m_pos + i]);
        m_pos += sizeof(T);
        return v;
    }

    const uint8_t *m_data;
    size_t         m_size;
    size_t         m_pos = 0;
};

// Bits of the flags byte. Only UseBaseColorSpace and IsMultiChannel are in ISO 21496-1 itself; libultrahdr
// writes the other two, and other readers honor them.
enum IsoFlags : uint8_t
{
    IsoFlag_BackwardDirection    = 1u << 2,
    IsoFlag_UseCommonDenominator = 1u << 3,
    IsoFlag_UseBaseColorSpace    = 1u << 6,
    IsoFlag_IsMultiChannel       = 1u << 7,
};

} // namespace

float IsoGainmapParams::weight(float target_stops) const
{
    const float span = alternate_headroom - base_headroom;
    if (span == 0.f)
        return 0.f;

    // how far from the base rendition towards the alternate one the target sits; the span's sign carries
    // the direction, so a darker alternate gives a weight of 0 for an unbounded target and -1 for zero
    const float t = std::clamp((target_stops - base_headroom) / span, 0.f, 1.f);
    return span < 0.f ? -t : t;
}

IsoGainmapParams parse_iso_gainmap(const uint8_t *data, size_t size)
{
    IsoReader r{data, size};

    const auto minimum_version = r.u16();
    const auto writer_version  = r.u16();

    // a writer needing fields this version does not define raises minimum_version, so anything above 0 is
    // unreadable here; a raised writer_version alone is fine, and may leave trailing bytes to ignore
    if (minimum_version != 0)
        throw invalid_argument{fmt::format("ISO 21496-1: unsupported minimum version {}", minimum_version)};

    IsoGainmapParams p;
    p.version = fmt::format("ISO 21496-1 v{} (writer v{})", minimum_version, writer_version);

    const uint8_t flags    = r.u8();
    const int     channels = (flags & IsoFlag_IsMultiChannel) ? 3 : 1;

    p.use_base_color_space = (flags & IsoFlag_UseBaseColorSpace) != 0;

    const bool common_denominator = (flags & IsoFlag_UseCommonDenominator) != 0;

    if (common_denominator)
    {
        const auto d = (float)r.u32();
        if (d == 0.f)
            throw invalid_argument{"ISO 21496-1: shared denominator is zero"};

        p.base_headroom      = r.over(d, false);
        p.alternate_headroom = r.over(d, false);

        for (int c = 0; c < channels; ++c)
        {
            p.min[c]              = r.over(d, true);
            p.max[c]              = r.over(d, true);
            p.gamma[c]            = r.over(d, false);
            p.base_offset[c]      = r.over(d, true);
            p.alternate_offset[c] = r.over(d, true);
        }
    }
    else
    {
        p.base_headroom      = r.rational_u();
        p.alternate_headroom = r.rational_u();

        for (int c = 0; c < channels; ++c)
        {
            p.min[c]              = r.rational_i();
            p.max[c]              = r.rational_i();
            p.gamma[c]            = r.rational_u();
            p.base_offset[c]      = r.rational_i();
            p.alternate_offset[c] = r.rational_i();
        }
    }

    // a single-channel map drives all three the same way
    for (int c = channels; c < 3; ++c)
    {
        p.min[c]              = p.min[0];
        p.max[c]              = p.max[0];
        p.gamma[c]            = p.gamma[0];
        p.base_offset[c]      = p.base_offset[0];
        p.alternate_offset[c] = p.alternate_offset[0];
    }

    if (flags & IsoFlag_BackwardDirection)
    {
        std::swap(p.base_headroom, p.alternate_headroom);
        std::swap(p.base_offset, p.alternate_offset);
    }

    spdlog::debug("ISO 21496-1: {} channel(s), base headroom {:.4f}, alternate {:.4f}, base color space {}", channels,
                  p.base_headroom, p.alternate_headroom, p.use_base_color_space);

    return p;
}

// at namespace scope because GCC's -Wunused-but-set-variable reports a function-local one as unused,
// not seeing through the lambda below that reads it
constexpr string_view k_hdrgm_ns = "http://ns.adobe.com/hdr-gain-map/1.0/";

std::optional<IsoGainmapParams> parse_hdrgm_xmp(const char *xml, size_t len)
{
    if (!xml || len == 0)
        return std::nullopt;

    // an XMP blob is usually wrapped in <?xpacket?> processing instructions, which tinyxml2 rejects as a
    // malformed declaration; HEIF stores the packet without them
    string_view body{xml, len};
    if (const size_t open_pi = body.find("<?xpacket"); open_pi != string_view::npos)
    {
        const size_t open_end = body.find("?>", open_pi);
        if (open_end == string_view::npos)
            return std::nullopt;

        body.remove_prefix(open_end + 2);

        if (const size_t close_pi = body.rfind("<?xpacket"); close_pi != string_view::npos)
            body = body.substr(0, close_pi);
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(body.data(), body.size()) != tinyxml2::XML_SUCCESS)
        return std::nullopt;

    // hdrgm properties hang off an rdf:Description, as attributes when they are single-valued and as
    // <hdrgm:Name><rdf:Seq><rdf:li>...</rdf:li></rdf:Seq></hdrgm:Name> when they are per-channel. Match on
    // the prefix bound to the hdrgm namespace instead of walking the RDF model.
    string     prefix;
    const auto find_prefix = [&](auto &&self, const tinyxml2::XMLElement *e) -> void
    {
        if (!e || !prefix.empty())
            return;

        for (auto *a = e->FirstAttribute(); a; a = a->Next())
        {
            const string_view name{a->Name()};
            if (name.rfind("xmlns:", 0) == 0 && string_view{a->Value()} == k_hdrgm_ns)
            {
                prefix = string{name.substr(6)} + ":";
                return;
            }
        }

        for (auto *c = e->FirstChildElement(); c; c = c->NextSiblingElement()) self(self, c);
    };
    find_prefix(find_prefix, doc.RootElement());

    if (prefix.empty())
        return std::nullopt;

    // Collect every hdrgm-prefixed property, single values and sequences alike, into name -> values.
    std::map<string, std::vector<string>> props;
    const auto                            collect = [&](auto &&self, const tinyxml2::XMLElement *e) -> void
    {
        if (!e)
            return;

        for (auto *a = e->FirstAttribute(); a; a = a->Next())
        {
            const string_view name{a->Name()};
            if (name.rfind(prefix, 0) == 0)
                props[string{name.substr(prefix.size())}] = {a->Value()};
        }

        for (auto *c = e->FirstChildElement(); c; c = c->NextSiblingElement())
        {
            const string_view name{c->Name()};
            if (name.rfind(prefix, 0) == 0)
            {
                std::vector<string> values;
                // an rdf:Seq or rdf:Bag of rdf:li, or a bare text value
                for (auto *container = c->FirstChildElement(); container; container = container->NextSiblingElement())
                    for (auto *li = container->FirstChildElement(); li; li = li->NextSiblingElement())
                        if (const char *t = li->GetText())
                            values.emplace_back(t);

                if (values.empty())
                    if (const char *t = c->GetText())
                        values.emplace_back(t);

                if (!values.empty())
                    props[string{name.substr(prefix.size())}] = std::move(values);
            }

            self(self, c);
        }
    };
    collect(collect, doc.RootElement());

    if (props.empty())
        return std::nullopt;

    const auto to_float = [](const string &s, float fallback)
    {
        try
        {
            return std::stof(s);
        }
        catch (const std::exception &)
        {
            return fallback;
        }
    };

    // every hdrgm property but GainMapMax and HDRCapacityMax has a default in Adobe's schema
    const auto rgb = [&](const char *name, float3 fallback, bool required)
    {
        const auto it = props.find(name);
        if (it == props.end())
        {
            if (required)
                throw invalid_argument{fmt::format("hdrgm XMP: required property '{}' is missing", name)};
            return fallback;
        }

        const auto &v = it->second;
        float3      out{to_float(v[0], fallback[0])};
        for (size_t c = 1; c < v.size() && c < 3; ++c) out[int(c)] = to_float(v[c], out[0]);
        // a single value drives all three channels; two is malformed, so treat the rest as the first
        if (v.size() == 2)
            out[2] = out[0];
        return out;
    };

    const auto scalar = [&](const char *name, float fallback, bool required)
    {
        const auto it = props.find(name);
        if (it == props.end())
        {
            if (required)
                throw invalid_argument{fmt::format("hdrgm XMP: required property '{}' is missing", name)};
            return fallback;
        }
        return to_float(it->second.front(), fallback);
    };

    IsoGainmapParams p;
    p.version = fmt::format("Adobe hdrgm XMP v{}", props.count("Version") ? props.at("Version").front() : string{"?"});

    p.min              = rgb("GainMapMin", float3{0.f}, false);
    p.max              = rgb("GainMapMax", float3{1.f}, true);
    p.gamma            = max(rgb("Gamma", float3{1.f}, false), float3{1e-3f});
    p.base_offset      = rgb("OffsetSDR", float3{1.f / 64.f}, false);
    p.alternate_offset = rgb("OffsetHDR", float3{1.f / 64.f}, false);
    p.max              = max(p.max, p.min);

    p.base_headroom      = scalar("HDRCapacityMin", 0.f, false);
    p.alternate_headroom = std::max(scalar("HDRCapacityMax", 1.f, true), p.base_headroom);

    // the XMP schema has no equivalent of the ISO color-space flag; Adobe's maps apply in the base space
    p.use_base_color_space = true;

    // older writers spelled the direction two different ways
    const auto base_rendition = props.find("BaseRendition");
    const auto base_is_hdr    = props.find("BaseRenditionIsHDR");
    if ((base_rendition != props.end() &&
         (base_rendition->second.front() == "HDR" || base_rendition->second.front() == "HighDynamicRange")) ||
        (base_is_hdr != props.end() &&
         (base_is_hdr->second.front() == "True" || base_is_hdr->second.front() == "true")))
    {
        std::swap(p.base_headroom, p.alternate_headroom);
        std::swap(p.base_offset, p.alternate_offset);
    }

    return p;
}

void apply_iso_gainmap(Image &image, const GainmapImage &gainmap, const IsoGainmapParams &params, float target_stops,
                       bool keep_renditions)
{
    if (image.channels.empty())
    {
        spdlog::warn("Gain map: base image has no channels; skipping.");
        return;
    }

    if (!gainmap.valid())
    {
        spdlog::warn("Gain map: decoded map is empty or malformed ({}x{}, {} channels); skipping.", gainmap.size.x,
                     gainmap.size.y, gainmap.channels);
        return;
    }

    // decode the map out of its gamma and back into log2 gain before the resize: the spec interpolates in
    // log space, and the two orders do not commute
    GainmapImage decoded;
    decoded.size     = gainmap.size;
    decoded.channels = gainmap.channels;
    decoded.pixels.resize(gainmap.pixels.size());

    const size_t pixel_count = (size_t)gainmap.size.x * gainmap.size.y;
    parallel_for(blocked_range<size_t>(0, pixel_count, 8192),
                 [&](size_t begin, size_t end, int, int)
                 {
                     for (size_t i = begin; i < end; ++i)
                         for (int c = 0; c < gainmap.channels; ++c)
                         {
                             // gain-map values are normalized, so anything outside [0,1] is codec
                             // overshoot. Gamma is commonly around 0.25, making the exponent below
                             // about 4, so an unclamped 1.19 would decode to twice the brightening
                             // the file's map asks for.
                             const float v = std::clamp(gainmap.pixels[i * gainmap.channels + c], 0.f, 1.f);

                             // the channel index saturates, so a 4-channel map's alpha reuses blue's
                             // curve instead of reading past the parameters
                             const int   p         = std::min(c, 2);
                             const float recovered = std::pow(v, 1.f / params.gamma[p]);

                             decoded.pixels[i * gainmap.channels + c] =
                                 params.min[p] * (1.f - recovered) + params.max[p] * recovered;
                         }
                 });

    const int2 size     = image.channels.front().size();
    const int  num_base = (int)image.channels.size();

    // already in log space, so no transfer function to undo on the way in
    const GainmapImage full = resample_gainmap(decoded, size, false);
    if (!full.valid())
        return;

    const float weight = params.weight(target_stops);

    image.metadata["header"]["Gain map"]          = {{"value", params.version},
                                                     {"string", params.version},
                                                     {"type", "string"},
                                                     {"description", "Standardized gain map, as written by Android, Adobe, "
                                                                              "and recent Apple software."}};
    image.metadata["header"]["Gain map headroom"] = {
        {"value", params.alternate_headroom},
        {"string",
         fmt::format("{:.3f} stops (base rendition {:.3f})", params.alternate_headroom, params.base_headroom)},
        {"type", "float"},
        {"description", "Headroom the alternate rendition is graded for, and the base rendition it converts from."}};
    image.metadata["header"]["Gain map applied"] = {
        {"value", weight},
        {"string", fmt::format("{:.3f} of the way to the alternate rendition", weight)},
        {"type", "float"},
        {"description", "How much of the map was applied, after the target headroom in the image loading options. "
                        "Negative when the alternate rendition is the darker one."}};

    spdlog::info("ISO gain map: {}x{}x{}, base headroom {:.3f} -> alternate {:.3f}, applying weight {:.3f}",
                 gainmap.size.x, gainmap.size.y, gainmap.channels, params.base_headroom, params.alternate_headroom,
                 weight);

    if (!params.use_base_color_space)
        spdlog::warn("Gain map declares the alternate image's color space as the application space; HDRView applies "
                     "it in the base image's space, which will shift saturated colors.");

    // keep the file's own renditions before the base is scaled out from under them
    if (keep_renditions)
    {
        append_base_rendition(image, num_base);
        append_gainmap_channels(image, full);
    }

    if (weight == 0.f)
    {
        spdlog::debug("ISO gain map: weight is zero; leaving base pixels alone.");
        return;
    }

    Timer timer;

    parallel_for(blocked_range<int>(0, size.y, 128),
                 [&, num_base, weight](int begin_y, int end_y, int, int)
                 {
                     for (int c = 0; c < num_base; ++c)
                     {
                         // scaling alpha would change transparency, not brightness
                         if (image.channels[c].name == "A")
                             continue;

                         const int p    = std::min(c, 2);
                         const int gc   = std::min(c, full.channels - 1);
                         auto     &base = image.channels[c];

                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < size.x; ++x)
                             {
                                 const float lb = full.pixels[((size_t)y * size.x + x) * full.channels + gc];
                                 base(x, y)     = (base(x, y) + params.base_offset[p]) * std::exp2(lb * weight) -
                                              params.alternate_offset[p];
                             }
                     }
                 });

    spdlog::debug("Applying gain map took: {} seconds.", (timer.elapsed() / 1000.f));
}
