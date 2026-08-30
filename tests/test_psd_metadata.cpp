//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "imageio/psd.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

TEST_CASE("PSDMetadata::color_mode_name is defined for every 16-bit value")
{
    // color_mode is whatever uint16 the file's header holds -- including PSDMetadata's own NotSet default
    // of 0xFFFF -- while the names table has ten entries, so it is not an index to use unchecked.
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::RGB)) == "RGB");
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::Bitmap)) == "Bitmap");
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::Lab)) == "Lab");
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::NotSet)) == "Unknown");

    // Every value the header can hold has to name something, not walk off the table. Counted rather than
    // asserted per iteration, so one summary failure stands in for 65536 identical ones.
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

void be16(std::string &o, uint16_t v) { o += char(v >> 8), o += char(v & 0xff); }
void be32(std::string &o, uint32_t v)
{
    for (int i = 3; i >= 0; --i) o += char((v >> (8 * i)) & 0xff);
}

//! A PSD holding one image resource, whose declared data size need not match what follows it.
/*!
    `declared` is what the resource block claims; `payload` is what the section actually holds. Real files
    agree; a truncated or hand-edited one does not, and the parser reads from the same stream the pixel
    data lives in.
*/
std::string psd_with_resource(uint16_t resource_id, uint32_t declared, const std::string &payload,
                              const std::string &trailing)
{
    std::string res;
    res += "8BIM";
    be16(res, resource_id);
    res += '\0'; // empty Pascal name, already even with its length byte
    res += '\0';
    be32(res, declared);
    res += payload;
    if (payload.size() % 2 == 1)
        res += '\0'; // resource data is padded to an even length

    std::string o;
    o += "8BPS";
    be16(o, 1); // version
    o.append(6, '\0');
    be16(o, 3); // channels
    be32(o, 1); // height
    be32(o, 1); // width
    be16(o, 8); // depth
    be16(o, 3); // RGB
    be32(o, 0); // no colour-mode data
    be32(o, (uint32_t)res.size());
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
        // The section ends after `xmp`; everything after it is the layer and pixel data the resource has
        // no business seeing.
        const std::string  secret = std::string(4096, 'P');
        std::istringstream is{psd_with_resource(1060, (uint32_t)(xmp.size() + secret.size()), xmp, secret)};

        PSDMetadata psd;
        try
        {
            psd.read(is);
        }
        catch (const std::exception &)
        {
            // Refusing the file outright is a fine answer; swallowing the pixels is not.
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
