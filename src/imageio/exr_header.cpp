#include "exr_header.h"
#include <ImfBoxAttribute.h>
#include <ImfChannelListAttribute.h>
#include <ImfChromaticitiesAttribute.h>
#include <ImfCompressionAttribute.h>
#include <ImfDoubleAttribute.h>
#include <ImfEnvmapAttribute.h>
#include <ImfFloatAttribute.h>
#include <ImfIntAttribute.h>
#include <ImfKeyCodeAttribute.h>
#include <ImfLineOrderAttribute.h>
#include <ImfMatrixAttribute.h>
#include <ImfPreviewImageAttribute.h>
#include <ImfRationalAttribute.h>
#include <ImfStringAttribute.h>
#include <ImfStringVectorAttribute.h>
#include <ImfTileDescriptionAttribute.h>
#include <ImfTimeCodeAttribute.h>
#include <ImfVecAttribute.h>
#include <iomanip>
#include <sstream>

using namespace Imf;
using namespace std;

namespace
{

json attribute_to_json(const Attribute &a)
{
    json        j;
    std::string type = a.typeName();
    j["type"]        = type;
    if (const auto *ta = dynamic_cast<const Box2iAttribute *>(&a))
    {
        auto &b     = ta->value();
        j["min"]    = {b.min.x, b.min.y};
        j["max"]    = {b.max.x, b.max.y};
        j["string"] = fmt::format("({}, {}) - ({}, {})", b.min.x, b.min.y, b.max.x, b.max.y);
    }
    else if (const auto *ta = dynamic_cast<const Box2fAttribute *>(&a))
    {
        auto &b     = ta->value();
        j["min"]    = {b.min.x, b.min.y};
        j["max"]    = {b.max.x, b.max.y};
        j["string"] = fmt::format("({}, {}) - ({}, {})", b.min.x, b.min.y, b.max.x, b.max.y);
    }
    else if (const auto *ta = dynamic_cast<const ChannelListAttribute *>(&a))
    {
        json               channels = json::array();
        std::ostringstream oss;
        for (ChannelList::ConstIterator i = ta->value().begin(); i != ta->value().end(); ++i)
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
            case HALF: oss << "half"; break;
            case FLOAT: oss << "float"; break;
            case UINT: oss << "uint"; break;
            default: oss << "unknown type (" << int(i.channel().type) << ")"; break;
            }
            oss << ", sampling " << i.channel().xSampling << " " << i.channel().ySampling;
            if (i.channel().pLinear)
                oss << ", plinear";
        }
        j["channels"] = channels;
        j["string"]   = oss.str();
    }
    else if (const auto *ta = dynamic_cast<const ChromaticitiesAttribute *>(&a))
    {
        auto &c     = ta->value();
        j["red"]    = {c.red.x, c.red.y};
        j["green"]  = {c.green.x, c.green.y};
        j["blue"]   = {c.blue.x, c.blue.y};
        j["white"]  = {c.white.x, c.white.y};
        j["string"] = fmt::format("red ({}, {})\ngreen ({}, {})\nblue ({}, {})\nwhite ({}, {})", c.red.x, c.red.y,
                                  c.green.x, c.green.y, c.blue.x, c.blue.y, c.white.x, c.white.y);
    }
    else if (const auto *ta = dynamic_cast<const CompressionAttribute *>(&a))
    {
        j["value"] = ta->value();
        std::string comp_str;
        switch (ta->value())
        {
        case NO_COMPRESSION: comp_str = "none"; break;
        case RLE_COMPRESSION: comp_str = "run-length encoding"; break;
        case ZIPS_COMPRESSION: comp_str = "zip, individual scanlines"; break;
        case ZIP_COMPRESSION: comp_str = "zip, multi-scanline blocks"; break;
        case PIZ_COMPRESSION: comp_str = "piz"; break;
        case PXR24_COMPRESSION: comp_str = "pxr24"; break;
        case B44_COMPRESSION: comp_str = "b44"; break;
        case B44A_COMPRESSION: comp_str = "b44a"; break;
        case DWAA_COMPRESSION: comp_str = "dwa, small scanline blocks"; break;
        case DWAB_COMPRESSION: comp_str = "dwa, medium scanline blocks"; break;
        default: comp_str = fmt::format("{}", static_cast<int>(ta->value())); break;
        }
        j["string"] = comp_str;
    }
    else if (const auto *ta = dynamic_cast<const DoubleAttribute *>(&a))
    {
        j["value"]  = ta->value();
        j["string"] = fmt::format("{}", ta->value());
    }
    else if (const auto *ta = dynamic_cast<const EnvmapAttribute *>(&a))
    {
        j["value"] = ta->value();
        string str;
        switch (ta->value())
        {
        case ENVMAP_LATLONG: str = "latitude-longitude map"; break;
        case ENVMAP_CUBE: str = "cube-face map"; break;
        default: str = fmt::format("map type {}", int(ta->value())); break;
        }
        j["string"] = str;
    }
    else if (const auto *ta = dynamic_cast<const FloatAttribute *>(&a))
    {
        j["value"]  = ta->value();
        j["string"] = fmt::format("{}", ta->value());
    }
    else if (const auto *ta = dynamic_cast<const IntAttribute *>(&a))
    {
        j["value"]  = ta->value();
        j["string"] = fmt::format("{}", ta->value());
    }
    else if (const auto *ta = dynamic_cast<const KeyCodeAttribute *>(&a))
    {
        j["filmMfcCode"]   = ta->value().filmMfcCode();
        j["filmType"]      = ta->value().filmType();
        j["prefix"]        = ta->value().prefix();
        j["count"]         = ta->value().count();
        j["perfOffset"]    = ta->value().perfOffset();
        j["perfsPerFrame"] = ta->value().perfsPerFrame();
        j["perfsPerCount"] = ta->value().perfsPerCount();
        j["string"] =
            fmt::format("film manufacturer code {}, film type code {}, prefix {}, count {}, perf offset {}, perfs per "
                        "frame {}, perfs per count {}",
                        ta->value().filmMfcCode(), ta->value().filmType(), ta->value().prefix(), ta->value().count(),
                        ta->value().perfOffset(), ta->value().perfsPerFrame(), ta->value().perfsPerCount());
    }
    else if (const auto *ta = dynamic_cast<const LineOrderAttribute *>(&a))
    {
        auto &lo   = ta->value();
        j["value"] = static_cast<int>(lo);
        string str;
        switch (lo)
        {
        case INCREASING_Y: str = "increasing y"; break;
        case DECREASING_Y: str = "decreasing y"; break;
        case RANDOM_Y: str = "random y"; break;
        default: str = fmt::format("unknown line order (={})", static_cast<int>(lo)); break;
        }
        j["string"] = str;
    }
    else if (const auto *ta = dynamic_cast<const M33fAttribute *>(&a))
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
    }
    else if (const auto *ta = dynamic_cast<const M44fAttribute *>(&a))
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
    }
    else if (const auto *ta = dynamic_cast<const PreviewImageAttribute *>(&a))
    {
        j["width"]  = ta->value().width();
        j["height"] = ta->value().height();
        j["string"] = fmt::format("{} by {} pixels", ta->value().width(), ta->value().height());
    }
    else if (const auto *ta = dynamic_cast<const StringAttribute *>(&a))
    {
        j["value"] = j["string"] = ta->value();
    }
    else if (const auto *ta = dynamic_cast<const StringVectorAttribute *>(&a))
    {
        j["value"] = ta->value();
        std::string str;
        for (const auto &s : ta->value()) str += s + ", ";
        j["string"] = str;
    }
    else if (const auto *ta = dynamic_cast<const RationalAttribute *>(&a))
    {
        j["numerator"]   = ta->value().n;
        j["denominator"] = ta->value().d;
        j["string"]      = fmt::format("{}/{} ({})", ta->value().n, ta->value().d, static_cast<double>(ta->value()));
    }
    else if (const auto *ta = dynamic_cast<const TileDescriptionAttribute *>(&a))
    {
        auto &t           = ta->value();
        j["mode"]         = t.mode;
        j["xSize"]        = t.xSize;
        j["ySize"]        = t.ySize;
        j["roundingMode"] = t.roundingMode;
        std::string mode_str;
        switch (t.mode)
        {
        case ONE_LEVEL: mode_str = "single level"; break;
        case MIPMAP_LEVELS: mode_str = "mip-map"; break;
        case RIPMAP_LEVELS: mode_str = "rip-map"; break;
        default: mode_str = fmt::format("level mode {}", int(t.mode)); break;
        }
        std::string rounding_str;
        switch (t.roundingMode)
        {
        case ROUND_DOWN: rounding_str = "down"; break;
        case ROUND_UP: rounding_str = "up"; break;
        default: rounding_str = fmt::format("mode {}", int(t.roundingMode)); break;
        }
        j["string"] = fmt::format("mode {}, tile size {}x{}, rounding {}", mode_str, t.xSize, t.ySize, rounding_str);
    }
    else if (const auto *ta = dynamic_cast<const TimeCodeAttribute *>(&a))
    {
        auto &t         = ta->value();
        j["hours"]      = t.hours();
        j["minutes"]    = t.minutes();
        j["seconds"]    = t.seconds();
        j["frame"]      = t.frame();
        j["dropFrame"]  = t.dropFrame();
        j["colorFrame"] = t.colorFrame();
        j["fieldPhase"] = t.fieldPhase();
        j["bgf0"]       = t.bgf0();
        j["bgf1"]       = t.bgf1();
        j["bgf2"]       = t.bgf2();
        j["userData"]   = t.userData();
        j["string"]     = fmt::format("time {:02}:{:02}:{:02}:{:02}", t.hours(), t.minutes(), t.seconds(), t.frame());
    }
    else if (const auto *ta = dynamic_cast<const V2iAttribute *>(&a))
    {
        j["value"]  = {ta->value().x, ta->value().y};
        j["string"] = fmt::format("({}, {})", ta->value().x, ta->value().y);
    }
    else if (const auto *ta = dynamic_cast<const V2fAttribute *>(&a))
    {
        j["value"]  = {ta->value().x, ta->value().y};
        j["string"] = fmt::format("({}, {})", ta->value().x, ta->value().y);
    }
    else if (const auto *ta = dynamic_cast<const V3iAttribute *>(&a))
    {
        j["value"]  = {ta->value().x, ta->value().y, ta->value().z};
        j["string"] = fmt::format("({}, {}, {})", ta->value().x, ta->value().y, ta->value().z);
    }
    else if (const auto *ta = dynamic_cast<const V3fAttribute *>(&a))
    {
        j["value"]  = {ta->value().x, ta->value().y, ta->value().z};
        j["string"] = fmt::format("({}, {}, {})", ta->value().x, ta->value().y, ta->value().z);
    }
    else
    {
        j["string"] = "unknown attribute type";
    }
    return j;
}

} // namespace

json exr_header_to_json(const Header &header)
{
    json j;
    for (Header::ConstIterator i = header.begin(); i != header.end(); ++i)
        j[i.name()] = attribute_to_json(i.attribute());

    return j;
}
