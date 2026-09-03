//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "imageio/psd.h"

#include "test_support.h"

using namespace hdrview_test;

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

TEST_CASE("PSDMetadata::color_mode_name is defined for every 16-bit value")
{
    // color_mode is any uint16 the header holds, including the NotSet default of 0xFFFF; the names table
    // has ten entries, so it is not an index to use unchecked
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::RGB)) == "RGB");
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::Bitmap)) == "Bitmap");
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::Lab)) == "Lab");
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::NotSet)) == "Unknown");

    // counted, so one summary failure stands in for 65536 identical ones
    int bad = 0;
    for (int m = 0; m <= 0xFFFF; ++m)
    {
        auto name = PSDMetadata::color_mode_name((PSDMetadata::ColorMode)m);
        if (!name || std::string(name).empty())
            ++bad;
    }
    CHECK(bad == 0);
}

namespace
{

/// A PSD holding one image resource.
/**
    `declared` is the size the resource block claims, `payload` what the section holds; a truncated or
    hand-edited file has them disagree.
*/
std::string psd_with_resource(uint16_t resource_id, uint32_t declared, const std::string &payload,
                              const std::string &trailing)
{
    std::string res;
    res += "8BIM";
    put<uint16_t>(res, resource_id, Endian::Big);
    res += '\0'; // empty Pascal name, already even with its length byte
    res += '\0';
    put<uint32_t>(res, declared, Endian::Big);
    res += payload;
    if (payload.size() % 2 == 1)
        res += '\0'; // resource data is padded to an even length

    std::string o;
    o += "8BPS";
    put<uint16_t>(o, 1, Endian::Big); // version
    o.append(6, '\0');
    put<uint16_t>(o, 3, Endian::Big); // channels
    put<uint32_t>(o, 1, Endian::Big); // height
    put<uint32_t>(o, 1, Endian::Big); // width
    put<uint16_t>(o, 8, Endian::Big); // depth
    put<uint16_t>(o, 3, Endian::Big); // RGB
    put<uint32_t>(o, 0, Endian::Big); // no color-mode data
    put<uint32_t>(o, (uint32_t)res.size(), Endian::Big);
    o += res;
    o += trailing;
    return o;
}

} // namespace

TEST_CASE("A PSD resource cannot read past the section that contains it")
{
    const std::string xmp = "<?xpacket begin=\"\"?><x:xmpmeta/><?xpacket end=\"w\"?>";

    SUBCASE("an honest resource is read whole")
    {
        std::istringstream is{psd_with_resource(1060, (uint32_t)xmp.size(), xmp, "")};
        PSDMetadata        psd;
        CHECK_NOTHROW(psd.read(is));
        CHECK(std::string(psd.xmp.begin(), psd.xmp.end()) == xmp);
    }

    SUBCASE("a resource claiming more than the section holds does not reach past it")
    {
        // the section ends after `xmp`; what follows is layer and pixel data
        const std::string  secret = std::string(4096, 'P');
        std::istringstream is{psd_with_resource(1060, (uint32_t)(xmp.size() + secret.size()), xmp, secret)};

        PSDMetadata psd;
        try
        {
            psd.read(is);
        }
        catch (const std::exception &)
        {
            // refusing the file outright is a fine answer; swallowing the pixels is not
        }
        CHECK(psd.xmp.size() <= xmp.size());
    }

    SUBCASE("a resource declaring far more than the file holds is not allocated for")
    {
        std::istringstream is{psd_with_resource(1060, 64u << 20, xmp, "")};
        PSDMetadata        psd;
        try
        {
            psd.read(is);
        }
        catch (const std::exception &)
        {
        }
        CHECK(psd.xmp.size() <= xmp.size());
    }
}
