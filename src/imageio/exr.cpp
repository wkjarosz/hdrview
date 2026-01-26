//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "exr.h"
#include "colorspace.h"
#include "common.h" // for lerp, mod, clamp, getExtension
#include "exr_std_streams.h"
#include "image.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "timer.h"
#include <ImfBoxAttribute.h>
#include <ImfChannelList.h>
#include <ImfChannelListAttribute.h>
#include <ImfChromaticitiesAttribute.h>
#include <ImfCompression.h>
#include <ImfCompressionAttribute.h>
#include <ImfDoubleAttribute.h>
#include <ImfEnvmapAttribute.h>
#include <ImfFloatAttribute.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfIntAttribute.h>
#include <ImfKeyCodeAttribute.h>
#include <ImfLineOrderAttribute.h>
#include <ImfMatrixAttribute.h>
#include <ImfMultiPartInputFile.h>
#include <ImfOutputFile.h>
#include <ImfPreviewImageAttribute.h>
#include <ImfRationalAttribute.h>
#include <ImfStandardAttributes.h>
#include <ImfStringAttribute.h>
#include <ImfStringVectorAttribute.h>
#include <ImfTestFile.h> // for isOpenExrFile
#include <ImfTileDescriptionAttribute.h>
#include <ImfTiledOutputFile.h>
#include <ImfTimeCodeAttribute.h>
#include <ImfVecAttribute.h>
#include <ImfVersion.h>
#include <hello_imgui/dpi_aware.h>
#include <sstream>
#include <stdexcept> // for runtime_error, out_of_range

using namespace std;

// OpenEXR 3.3+ added HTJ2K compression and helper functions
// For older versions, we need to provide fallbacks
#if !defined(OPENEXR_VERSION_MAJOR) || (OPENEXR_VERSION_MAJOR < 3) ||                                                  \
    (OPENEXR_VERSION_MAJOR == 3 && OPENEXR_VERSION_MINOR < 3)
#define HDRVIEW_OPENEXR_PRE_3_3
#endif

namespace
{
#ifdef HDRVIEW_OPENEXR_PRE_3_3
// Fallback for getCompressionNameFromId for OpenEXR < 3.3
void getCompressionNameFromId(Imf::Compression c, std::string &name)
{
    switch (c)
    {
    case Imf::NO_COMPRESSION: name = "none"; break;
    case Imf::RLE_COMPRESSION: name = "rle"; break;
    case Imf::ZIPS_COMPRESSION: name = "zips"; break;
    case Imf::ZIP_COMPRESSION: name = "zip"; break;
    case Imf::PIZ_COMPRESSION: name = "piz"; break;
    case Imf::PXR24_COMPRESSION: name = "pxr24"; break;
    case Imf::B44_COMPRESSION: name = "b44"; break;
    case Imf::B44A_COMPRESSION: name = "b44a"; break;
    case Imf::DWAA_COMPRESSION: name = "dwaa"; break;
    case Imf::DWAB_COMPRESSION: name = "dwab"; break;
    default: name = "unknown"; break;
    }
}

// Fallback for getCompressionDescriptionFromId for OpenEXR < 3.3
void getCompressionDescriptionFromId(Imf::Compression c, std::string &desc)
{
    switch (c)
    {
    case Imf::NO_COMPRESSION: desc = "No compression"; break;
    case Imf::RLE_COMPRESSION: desc = "Run-length encoding"; break;
    case Imf::ZIPS_COMPRESSION: desc = "Zlib compression, one scan line at a time"; break;
    case Imf::ZIP_COMPRESSION: desc = "Zlib compression, 16 scan lines at a time"; break;
    case Imf::PIZ_COMPRESSION: desc = "Piz-based wavelet compression"; break;
    case Imf::PXR24_COMPRESSION: desc = "Lossy 24-bit float compression"; break;
    case Imf::B44_COMPRESSION: desc = "Lossy 4-by-4 pixel block compression, fixed rate"; break;
    case Imf::B44A_COMPRESSION: desc = "Lossy 4-by-4 pixel block compression, flat fields are compressed more"; break;
    case Imf::DWAA_COMPRESSION: desc = "Lossy DCT based compression, 32 scanlines at a time"; break;
    case Imf::DWAB_COMPRESSION: desc = "Lossy DCT based compression, 256 scanlines at a time"; break;
    default: desc = "Unknown compression"; break;
    }
}
#endif

json attribute_to_json(const Imf::Attribute &a)
{
    json        j;
    std::string type = a.typeName();
    j["type"]        = type;
    if (const auto *ta = dynamic_cast<const Imf::Box2iAttribute *>(&a))
    {
        auto &b           = ta->value();
        j["value"]["min"] = {b.min.x, b.min.y};
        j["value"]["max"] = {b.max.x, b.max.y};
        j["string"]       = fmt::format("({}, {}) - ({}, {})", b.min.x, b.min.y, b.max.x, b.max.y);
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::Box2fAttribute *>(&a))
    {
        auto &b           = ta->value();
        j["value"]["min"] = {b.min.x, b.min.y};
        j["value"]["max"] = {b.max.x, b.max.y};
        j["string"]       = fmt::format("({}, {}) - ({}, {})", b.min.x, b.min.y, b.max.x, b.max.y);
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::ChannelListAttribute *>(&a))
    {
        json               channels = json::array();
        std::ostringstream oss;
        for (Imf::ChannelList::ConstIterator i = ta->value().begin(); i != ta->value().end(); ++i)
        {
            json ch;
            ch["name"]      = i.name();
            ch["type"]      = i.channel().type;
            ch["xSampling"] = i.channel().xSampling;
            ch["ySampling"] = i.channel().ySampling;
            ch["pLinear"]   = i.channel().pLinear;
            channels.push_back(ch);

            oss << (i == ta->value().begin() ? "" : "\n");
            oss << i.name() << ", ";
            // Print pixel type as string
            switch (i.channel().type)
            {
            case Imf::HALF: oss << "half"; break;
            case Imf::FLOAT: oss << "float"; break;
            case Imf::UINT: oss << "uint"; break;
            default: oss << "unknown type (" << int(i.channel().type) << ")"; break;
            }
            oss << ", sampling " << i.channel().xSampling << " " << i.channel().ySampling;
            if (i.channel().pLinear)
                oss << ", plinear";
        }
        j["value"]["channels"] = channels;
        j["string"]            = oss.str();
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::ChromaticitiesAttribute *>(&a))
    {
        auto &c             = ta->value();
        j["value"]["red"]   = {c.red.x, c.red.y};
        j["value"]["green"] = {c.green.x, c.green.y};
        j["value"]["blue"]  = {c.blue.x, c.blue.y};
        j["value"]["white"] = {c.white.x, c.white.y};
        j["string"] = fmt::format("red ({}, {})\ngreen ({}, {})\nblue ({}, {})\nwhite ({}, {})", c.red.x, c.red.y,
                                  c.green.x, c.green.y, c.blue.x, c.blue.y, c.white.x, c.white.y);
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::CompressionAttribute *>(&a))
    {
        j["value"] = ta->value();
        std::string comp_str;
#ifdef HDRVIEW_OPENEXR_PRE_3_3
        getCompressionNameFromId(ta->value(), comp_str);
#else
        Imf::getCompressionNameFromId(ta->value(), comp_str);
#endif
        j["string"] = comp_str;
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::DoubleAttribute *>(&a))
    {
        j["value"]  = ta->value();
        j["string"] = fmt::format("{}", ta->value());
    }
    if (const auto *ta = dynamic_cast<const Imf::EnvmapAttribute *>(&a))
    {
        j["value"] = ta->value();
        string str;
        switch (ta->value())
        {
        case Imf::ENVMAP_LATLONG: str = "latitude-longitude map"; break;
        case Imf::ENVMAP_CUBE: str = "cube-face map"; break;
        default: str = fmt::format("map type {}", int(ta->value())); break;
        }
        j["string"] = str;
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::FloatAttribute *>(&a))
    {
        j["value"]  = ta->value();
        j["string"] = fmt::format("{}", ta->value());
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::IntAttribute *>(&a))
    {
        j["value"]  = ta->value();
        j["string"] = fmt::format("{}", ta->value());
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::KeyCodeAttribute *>(&a))
    {
        j["value"]["filmMfcCode"]   = ta->value().filmMfcCode();
        j["value"]["filmType"]      = ta->value().filmType();
        j["value"]["prefix"]        = ta->value().prefix();
        j["value"]["count"]         = ta->value().count();
        j["value"]["perfOffset"]    = ta->value().perfOffset();
        j["value"]["perfsPerFrame"] = ta->value().perfsPerFrame();
        j["value"]["perfsPerCount"] = ta->value().perfsPerCount();
        j["string"] =
            fmt::format("film manufacturer code {}, film type code {}, prefix {}, count {}, perf offset {}, perfs per "
                        "frame {}, perfs per count {}",
                        ta->value().filmMfcCode(), ta->value().filmType(), ta->value().prefix(), ta->value().count(),
                        ta->value().perfOffset(), ta->value().perfsPerFrame(), ta->value().perfsPerCount());
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::LineOrderAttribute *>(&a))
    {
        auto &lo   = ta->value();
        j["value"] = static_cast<int>(lo);
        string str;
        switch (lo)
        {
        case Imf::INCREASING_Y: str = "increasing y"; break;
        case Imf::DECREASING_Y: str = "decreasing y"; break;
        case Imf::RANDOM_Y: str = "random y"; break;
        default: str = fmt::format("unknown line order (={})", static_cast<int>(lo)); break;
        }
        j["string"] = str;
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::M33fAttribute *>(&a))
    {
        json mat = json::array();
        for (int r = 0; r < 3; ++r)
        {
            json row = json::array();
            for (int c = 0; c < 3; ++c) row.push_back(ta->value()[r][c]);
            mat.push_back(row);
        }
        j["value"]      = mat;
        std::string str = "[";
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
            {
                str += fmt::format("{}", ta->value()[r][c]);
                if (r != 2 || c != 2)
                    str += ", ";
            }
        }
        str += "]";
        j["string"] = str;
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::M44fAttribute *>(&a))
    {
        json mat = json::array();
        for (int r = 0; r < 4; ++r)
        {
            json row = json::array();
            for (int c = 0; c < 4; ++c) row.push_back(ta->value()[r][c]);
            mat.push_back(row);
        }
        j["value"]      = mat;
        std::string str = "[";
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                str += fmt::format("{}", ta->value()[r][c]);
                if (r != 3 || c != 3)
                    str += ", ";
            }
        }
        str += "]";
        j["string"] = str;
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::PreviewImageAttribute *>(&a))
    {
        j["value"]["width"]  = ta->value().width();
        j["value"]["height"] = ta->value().height();
        j["string"]          = fmt::format("{} by {} pixels", ta->value().width(), ta->value().height());
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::StringAttribute *>(&a))
    {
        j["value"] = j["string"] = ta->value();
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::StringVectorAttribute *>(&a))
    {
        j["value"] = ta->value();
        std::string str;
        for (const auto &s : ta->value()) str += s + ", ";
        j["string"] = str;
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::RationalAttribute *>(&a))
    {
        j["value"]["numerator"]   = ta->value().n / ta->value().d;
        j["value"]["numerator"]   = ta->value().n;
        j["value"]["denominator"] = ta->value().d;
        j["string"] = fmt::format("{}/{} ({})", ta->value().n, ta->value().d, static_cast<double>(ta->value()));
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::TileDescriptionAttribute *>(&a))
    {
        auto &t                    = ta->value();
        j["value"]["mode"]         = t.mode;
        j["value"]["xSize"]        = t.xSize;
        j["value"]["ySize"]        = t.ySize;
        j["value"]["roundingMode"] = t.roundingMode;
        std::string mode_str;
        switch (t.mode)
        {
        case Imf::ONE_LEVEL: mode_str = "single level"; break;
        case Imf::MIPMAP_LEVELS: mode_str = "mip-map"; break;
        case Imf::RIPMAP_LEVELS: mode_str = "rip-map"; break;
        default: mode_str = fmt::format("level mode {}", int(t.mode)); break;
        }
        std::string rounding_str;
        switch (t.roundingMode)
        {
        case Imf::ROUND_DOWN: rounding_str = "down"; break;
        case Imf::ROUND_UP: rounding_str = "up"; break;
        default: rounding_str = fmt::format("mode {}", int(t.roundingMode)); break;
        }
        j["string"] = fmt::format("mode {}, tile size {}x{}, rounding {}", mode_str, t.xSize, t.ySize, rounding_str);
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::TimeCodeAttribute *>(&a))
    {
        auto &t                  = ta->value();
        j["value"]["hours"]      = t.hours();
        j["value"]["minutes"]    = t.minutes();
        j["value"]["seconds"]    = t.seconds();
        j["value"]["frame"]      = t.frame();
        j["value"]["dropFrame"]  = t.dropFrame();
        j["value"]["colorFrame"] = t.colorFrame();
        j["value"]["fieldPhase"] = t.fieldPhase();
        j["value"]["bgf0"]       = t.bgf0();
        j["value"]["bgf1"]       = t.bgf1();
        j["value"]["bgf2"]       = t.bgf2();
        j["value"]["userData"]   = t.userData();
        j["string"] = fmt::format("time {:02}:{:02}:{:02}:{:02}", t.hours(), t.minutes(), t.seconds(), t.frame());
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::V2iAttribute *>(&a))
    {
        j["value"]  = {ta->value().x, ta->value().y};
        j["string"] = fmt::format("({}, {})", ta->value().x, ta->value().y);
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::V2fAttribute *>(&a))
    {
        j["value"]  = {ta->value().x, ta->value().y};
        j["string"] = fmt::format("({}, {})", ta->value().x, ta->value().y);
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::V3iAttribute *>(&a))
    {
        j["value"]  = {ta->value().x, ta->value().y, ta->value().z};
        j["string"] = fmt::format("({}, {}, {})", ta->value().x, ta->value().y, ta->value().z);
        return j;
    }
    if (const auto *ta = dynamic_cast<const Imf::V3fAttribute *>(&a))
    {
        j["value"]  = {ta->value().x, ta->value().y, ta->value().z};
        j["string"] = fmt::format("({}, {}, {})", ta->value().x, ta->value().y, ta->value().z);
        return j;
    }

    j["string"] = "unknown attribute type";
    return j;
}

json exr_header_to_json(const Imf::Header &header)
{
    json j;
    for (Imf::Header::ConstIterator i = header.begin(); i != header.end(); ++i)
        j[i.name()] = attribute_to_json(i.attribute());

    return j;
}

} // namespace

struct EXRSaveOptions
{
    std::vector<bool> group_enabled;                      // size = img.groups.size()
    int               pixel_type  = 1;                    // 0 = Imf::FLOAT, 1 = Imf::HALF
    Imf::Compression  compression = Imf::PIZ_COMPRESSION; // Default compression
    bool              tiled       = false;
    int               tile_width  = 64;
    int               tile_height = 64;
    float             dwa_quality = 45.0f; // Only for DWAA/DWAB
};

static EXRSaveOptions s_opts{};

bool is_exr_image(istream &is_, string_view filename) noexcept
{
    auto is = StdIStream{is_, string(filename).c_str()};
    return Imf::isOpenExrFile(is);
}

vector<ImagePtr> load_exr_image(istream &is_, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "EXR"};
    auto      is = StdIStream{is_, string(filename).c_str()};

    Imf::MultiPartInputFile infile{is};

    if (infile.parts() <= 0)
        throw invalid_argument{"EXR file contains no parts!"};

    ImGuiTextFilter filter{opts.channel_selector.c_str()};
    filter.Build();
    spdlog::info("Building filter for selector '{}'", opts.channel_selector);

    vector<ImagePtr> images;
    for (int p = 0; p < infile.parts(); ++p)
    {
        Imf::InputPart part{infile, p};

        auto channel_name = [&](Imf::ChannelList::ConstIterator c)
        {
            string name = c.name();
            if (part.header().hasName())
                name = part.header().name() + "."s + name;
            return name;
        };

        const auto &channels = part.header().channels();

        Imath::Box2i dataWindow    = part.header().dataWindow();
        Imath::Box2i displayWindow = part.header().displayWindow();
        int2         size          = {dataWindow.max.x - dataWindow.min.x + 1, dataWindow.max.y - dataWindow.min.y + 1};

        if (size.x <= 0 || size.y <= 0)
        {
            spdlog::warn("EXR part {}: '{}' has zero pixels, skipping...", p,
                         part.header().hasName() ? part.header().name() : "unnamed");
            continue;
        }

        auto img = make_shared<Image>();
        if (auto a = part.header().findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities"))
            img->chromaticities = {{a->value().red.x, a->value().red.y},
                                   {a->value().green.x, a->value().green.y},
                                   {a->value().blue.x, a->value().blue.y},
                                   {a->value().white.x, a->value().white.y}};
        img->metadata["loader"] = "OpenEXR";
        img->metadata["header"] = exr_header_to_json(part.header());

        img->metadata["header"]["version"] = {
            {"type", "version"},
            {"string", fmt::format("{}, flags 0x{:x}", Imf::getVersion(part.version()), Imf::getFlags(part.version()))},
            {"version", Imf::getVersion(part.version())},
            {"flags", fmt::format("0x{:x}", Imf::getFlags(part.version()))}};

        // spdlog::debug("exr header: {}", img->metadata["header"].dump(2));

        if (part.header().hasName())
            img->partname = part.header().name();

        // OpenEXR library's boxes include the max element, our boxes don't, so we increment by 1
        img->data_window    = {{dataWindow.min.x, dataWindow.min.y}, {dataWindow.max.x + 1, dataWindow.max.y + 1}};
        img->display_window = {{displayWindow.min.x, displayWindow.min.y},
                               {displayWindow.max.x + 1, displayWindow.max.y + 1}};

        if (img->data_window.is_empty())
            throw invalid_argument{fmt::format("Image has invalid data window: [{},{}] - [{},{}]",
                                               img->data_window.min.x, img->data_window.min.y, img->data_window.max.x,
                                               img->data_window.max.y)};

        if (img->display_window.is_empty())
            throw invalid_argument{fmt::format("Image has invalid display window: [{},{}] - [{},{}]",
                                               img->display_window.min.x, img->display_window.min.y,
                                               img->display_window.max.x, img->display_window.max.y)};

        Imf::FrameBuffer framebuffer;
        bool             has_channels = false;
        for (auto c = channels.begin(); c != channels.end(); ++c)
        {
            auto name = channel_name(c);
            if (!filter.PassFilter(&name[0], &name[0] + name.size()))
            {
                spdlog::debug("Skipping channel '{}' in part {}: '{}'", name, p, c.name());
                continue;
            }
            else
            {
                has_channels = true;
                spdlog::debug("Loading channel '{}' in part {}: '{}'", name, p, c.name());
            }

            name = c.name();

            img->channels.emplace_back(name, size);
            framebuffer.insert(c.name(), Imf::Slice::Make(Imf::FLOAT, img->channels.back().data(), dataWindow, 0, 0,
                                                          c.channel().xSampling, c.channel().ySampling));
        }

        if (!has_channels)
        {
            spdlog::debug("Part {}: '{}' has no channels matching the filter '{}', skipping...", p,
                          part.header().hasName() ? part.header().name() : "unnamed", opts.channel_selector);
            continue;
        }

        part.setFrameBuffer(framebuffer);
        part.readPixels(dataWindow.min.y, dataWindow.max.y);

        // now up-res any subsampled channels
        // NOTE: OpenEXR v3.3.0 broke this subsample channel loading
        // see https://github.com/AcademySoftwareFoundation/openexr/issues/1949
        // This was fixed in v3.4
        int i = 0;
        for (auto c = part.header().channels().begin(); c != part.header().channels().end(); ++c, ++i)
        {
            int xs = c.channel().xSampling;
            int ys = c.channel().ySampling;
            if (xs == 1 && ys == 1)
                continue;

            spdlog::warn("Channel '{}' is subsampled ({},{}). Only rudimentary subsampling is supported.", c.name(), xs,
                         ys);
            Array2Df tmp = img->channels[i];

            int subsampled_width = size.x / xs;
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) img->channels[i]({x, y}) = tmp(x / xs + (y / ys) * subsampled_width);
        }

        images.emplace_back(img);
    }
    return images;
}

void save_exr_image(const Image &img, ostream &os_, string_view filename, const EXRSaveOptions *params)
{
    try
    {
        if (!params)
            params = &s_opts;
        Timer timer;
        // OpenEXR expects the display window to be inclusive, while our images are exclusive
        auto displayWindow = Imath::Box2i(Imath::V2i(img.display_window.min.x, img.display_window.min.y),
                                          Imath::V2i(img.display_window.max.x - 1, img.display_window.max.y - 1));
        auto dataWindow    = Imath::Box2i(Imath::V2i(img.data_window.min.x, img.data_window.min.y),
                                          Imath::V2i(img.data_window.max.x - 1, img.data_window.max.y - 1));

        Imf::Header header;
        if (img.chromaticities)
            header.insert(
                "chromaticities",
                Imf::ChromaticitiesAttribute{{Imath::V2f(img.chromaticities->red.x, img.chromaticities->red.y),
                                              Imath::V2f(img.chromaticities->green.x, img.chromaticities->green.y),
                                              Imath::V2f(img.chromaticities->blue.x, img.chromaticities->blue.y),
                                              Imath::V2f(img.chromaticities->white.x, img.chromaticities->white.y)}});
        header.insert("channels", Imf::ChannelListAttribute());
        header.displayWindow() = displayWindow;
        header.dataWindow()    = dataWindow;

        // Compression
        header.compression() = params->compression;

        // Tiled
        if (params->tiled)
            header.setTileDescription(Imf::TileDescription(params->tile_width, params->tile_height, Imf::ONE_LEVEL));

        // DWA quality
        if (params->compression == Imf::DWAA_COMPRESSION || params->compression == Imf::DWAB_COMPRESSION)
            header.insert("dwaCompressionLevel", Imf::FloatAttribute(params->dwa_quality));

        Imf::FrameBuffer frameBuffer;

        std::map<std::string, std::vector<half>> halfBuffers; // Temporary storage for half buffers
        for (int g = 0; g < (int)img.groups.size(); ++g)
        {
            if (g >= (int)params->group_enabled.size() || !params->group_enabled[g])
                continue;
            auto &group = img.groups[g];
            if (!group.visible)
                continue;

            for (int c = 0; c < group.num_channels; ++c)
            {
                auto &channel    = img.channels[group.channels[c]];
                auto  pixel_type = (params->pixel_type == 1) ? Imf::HALF : Imf::FLOAT;

                // Specify desired file type in header
                header.channels().insert(channel.name, Imf::Channel(pixel_type));

                if (pixel_type == Imf::HALF)
                {
                    // Convert float buffer to half buffer
                    std::vector<half> &hbuf = halfBuffers[channel.name];
                    hbuf.resize(channel.num_elements());
                    const float *fbuf = channel.data();
                    for (int i = 0; i < channel.num_elements(); ++i) hbuf[i] = half(fbuf[i]);
                    frameBuffer.insert(channel.name, Imf::Slice::Make(Imf::HALF, hbuf.data(), dataWindow));
                }
                else
                {
                    // Use float buffer directly
                    frameBuffer.insert(channel.name, Imf::Slice::Make(Imf::FLOAT, channel.data(), dataWindow));
                }
            }
        }

        auto os = StdOStream{os_, string(filename).c_str()};
        if (params->tiled)
        {
            Imf::TiledOutputFile file{os, header};
            file.setFrameBuffer(frameBuffer);
            file.writeTiles(0, file.numXTiles() - 1, 0, file.numYTiles() - 1);
        }
        else
        {
            Imf::OutputFile file{os, header};
            file.setFrameBuffer(frameBuffer);
            file.writePixels(img.data_window.size().y);
        }
        spdlog::info("Saved EXR image to \"{}\" in {} seconds.", filename, (timer.elapsed() / 1000.f));
    }
    catch (const exception &e)
    {
        throw runtime_error{fmt::format("Failed to write EXR image \"{}\" failed: {}", filename, e.what())};
    }
}

EXRSaveOptions *exr_parameters_gui(const ImagePtr &img)
{
    static ImGuiSelectionBasicStorage group_selection;

    if (s_opts.group_enabled.size() != img->groups.size())
    {
        s_opts.group_enabled.assign(img->groups.size(), true);
        group_selection.Clear();
        for (int i = 0; i < (int)img->groups.size(); ++i) group_selection.SetItemSelected(i, true);
    }

    if (ImGui::PE::Begin("OpenEXR Save Options",
                         ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
    {
        ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthStretch);

        // Channels (custom multi-select widget)
        ImGui::PE::Entry(
            fmt::format("Channels ({}/{})", group_selection.Size, (int)img->groups.size()),
            [&]
            {
                if (ImGui::BeginChild("##Groups", ImVec2(-FLT_MIN, ImGui::GetFontSize() * 10),
                                      ImGuiChildFlags_FrameStyle | ImGuiChildFlags_ResizeY))
                {
                    ImGuiMultiSelectFlags flags =
                        ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d;
                    ImGuiMultiSelectIO *ms_io =
                        ImGui::BeginMultiSelect(flags, group_selection.Size, (int)img->groups.size());
                    group_selection.ApplyRequests(ms_io);

                    int width = (int)std::to_string(img->groups.size()).size();
                    for (int i = 0; i < (int)img->groups.size(); ++i)
                    {
                        auto &group            = img->groups[i];
                        bool  item_is_selected = group_selection.Contains((ImGuiID)i);
                        ImGui::SetNextItemSelectionUserData(i);

                        auto       &channel    = img->channels[group.channels[0]];
                        string      group_name = group.num_channels == 1 ? group.name : "(" + group.name + ")";
                        string      layer_path = Channel::head(channel.name) + group_name;
                        std::string label      = fmt::format("{:>{}d} {}", i + 1, width, layer_path);

                        ImGui::Selectable(label.c_str(), item_is_selected);
                    }

                    ms_io = ImGui::EndMultiSelect();
                    group_selection.ApplyRequests(ms_io);

                    // Update s_opts.group_enabled based on selection
                    if (s_opts.group_enabled.size() != img->groups.size())
                        s_opts.group_enabled.assign(img->groups.size(), true);
                    for (int i = 0; i < (int)img->groups.size(); ++i)
                        s_opts.group_enabled[i] = group_selection.Contains((ImGuiID)i);
                }
                ImGui::EndChild();
                return true;
            },
            "Select which channel groups to write to the EXR file.");

        // Pixel format
        ImGui::PE::Combo("Pixel format", &s_opts.pixel_type, "Float (32-bit)\0Half (16-bit)\0", -1,
                         "Choose whether to store channels as 32-bit float or 16-bit half in the EXR file.");

        // Compression (custom enumerated combo with tooltips)
        ImGui::PE::Entry(
            "Compression",
            [&]
            {
                static const Imf::Compression compression_values[] = {
                    Imf::NO_COMPRESSION,      Imf::RLE_COMPRESSION,     Imf::ZIPS_COMPRESSION, Imf::ZIP_COMPRESSION,
                    Imf::PIZ_COMPRESSION,     Imf::PXR24_COMPRESSION,   Imf::B44_COMPRESSION,  Imf::B44A_COMPRESSION,
                    Imf::DWAA_COMPRESSION,    Imf::DWAB_COMPRESSION,
#ifndef HDRVIEW_OPENEXR_PRE_3_3
                    Imf::HTJ2K32_COMPRESSION, Imf::HTJ2K256_COMPRESSION
#endif
                };
                static const int num_compressions = IM_ARRAYSIZE(compression_values);

                string name;
#ifdef HDRVIEW_OPENEXR_PRE_3_3
                getCompressionNameFromId(compression_values[s_opts.compression], name);
#else
                Imf::getCompressionNameFromId(compression_values[s_opts.compression], name);
#endif

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##Compression", name.c_str()))
                {
                    for (int i = 0; i < num_compressions; ++i)
                    {
                        bool is_selected = (s_opts.compression == compression_values[i]);
#ifdef HDRVIEW_OPENEXR_PRE_3_3
                        getCompressionNameFromId(compression_values[i], name);
#else
                        Imf::getCompressionNameFromId(compression_values[i], name);
#endif
                        if (ImGui::Selectable(name.c_str(), is_selected))
                            s_opts.compression = compression_values[i];

                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
#ifdef HDRVIEW_OPENEXR_PRE_3_3
                        getCompressionDescriptionFromId(compression_values[i], name);
#else
                        Imf::getCompressionDescriptionFromId(compression_values[i], name);
#endif
                        ImGui::Tooltip(name.c_str());
                    }
                    ImGui::EndCombo();
                }

                return true;
            },
            "Select the compression method for the EXR file.");

        // DWA compression quality
        if (s_opts.compression == Imf::DWAA_COMPRESSION || s_opts.compression == Imf::DWAB_COMPRESSION)
            ImGui::PE::SliderFloat("DWA compression quality", &s_opts.dwa_quality, 0.0f, 100.0f, "%.3f", 0,
                                   "Set the lossy quality for DWA compression (higher is better, 45 is default).");

        // Tiled vs scanline
        ImGui::PE::Entry(
            "Tiled",
            [&]
            {
                ImGui::Checkbox("##Tiled", &s_opts.tiled);
                if (s_opts.tiled)
                {
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2);
                    ImGui::SliderInt("##Tile width", &s_opts.tile_width, 16, 512, "Width: %d");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    ImGui::SliderInt("##Tile height", &s_opts.tile_height, 16, 512, "Height: %d");
                    ImGui::EndGroup();
                    ImGui::Tooltip("Set the tile size for tiled EXR output.");
                }
                return false;
            },
            "Enable to save as a tiled EXR file (recommended for large images).");

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = EXRSaveOptions{};

    return &s_opts;
}
