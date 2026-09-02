//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// XMP decorates an image, it does not describe its pixels; but Image::finalize() parses it with no guard
// around the call, so whatever the parser does to a packet it dislikes happens to the whole load.

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/image_loader.h"
#include "imageio/xmp.h"

#include <sstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace
{

//! Wraps `body` in the packet framing a writer normally emits.
std::string packet(const std::string &body)
{
    return "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
           "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
           "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n" +
           body +
           "\n</rdf:RDF>\n"
           "</x:xmpmeta>\n"
           "<?xpacket end=\"w\"?>";
}

json parse(const std::string &xml)
{
    Xmp xmp(xml.data(), xml.size());
    return xmp.to_json();
}

} // namespace

TEST_CASE("An ordinary XMP packet parses into its schemas")
{
    // baseline, so a change that stops XMP parsing altogether cannot pass as one that hardens it
    const json j = parse(packet(R"(<rdf:Description rdf:about=""
        xmlns:dc="http://purl.org/dc/elements/1.1/"
        xmlns:xmp="http://ns.adobe.com/xap/1.0/"
        xmp:CreatorTool="HDRView">
        <dc:format>image/jpeg</dc:format>
      </rdf:Description>)"));

    REQUIRE(j.is_object());
    REQUIRE(j.contains("dc"));
    CHECK(j["dc"]["format"] == "image/jpeg");
    REQUIRE(j.contains("xmp"));
    CHECK(j["xmp"]["CreatorTool"] == "HDRView");
    // the namespace table the Info panel groups and orders by
    REQUIRE(j.contains("xmlns"));
    CHECK(j["xmlns"]["dc"]["name"] == "Dublin Core");
}

TEST_CASE("A default namespace declaration does not take the image down with it")
{
    // xmlns= has no colon, so it is stored as a plain string; xmlns:dc= then addresses it as an object
    const std::string with_default_ns = packet(R"(<rdf:Description rdf:about=""
        xmlns="http://purl.org/dc/elements/1.1/"
        xmlns:dc="http://purl.org/dc/elements/1.1/">
        <dc:title>ok</dc:title>
      </rdf:Description>)");

    json j;
    CHECK_NOTHROW(j = parse(with_default_ns));
    CHECK(j.is_object());

    // the same collision reached through an element's own attributes
    const std::string prefix_used_twice = packet(R"(<rdf:Description rdf:about=""
        xmlns:dc="http://purl.org/dc/elements/1.1/"
        dc="plain"
        dc:title="structured"/>)");
    CHECK_NOTHROW(parse(prefix_used_twice));

    // and through a child element whose prefix an attribute already claimed as a plain value
    const std::string child_after_attribute = packet(R"(<rdf:Description rdf:about=""
        xmlns:dc="http://purl.org/dc/elements/1.1/"
        dc="plain">
        <dc:title>structured</dc:title>
      </rdf:Description>)");
    CHECK_NOTHROW(parse(child_after_attribute));
}

TEST_CASE("A packet that cannot be parsed yields nothing rather than throwing")
{
    CHECK_NOTHROW(parse(packet("<rdf:Description"))); // truncated
    CHECK_NOTHROW(parse("not xml at all"));           // no packet framing
    CHECK_NOTHROW(parse(packet("")));                 // no descriptions
    CHECK_NOTHROW(parse(""));                         // nothing at all
    CHECK(parse("").is_object());
}

namespace
{

//! A 1x1 PNG carrying `xmp` in the iTXt chunk the XMP spec assigns it, which png.cpp reads.
std::string png_with_xmp(const std::string &xmp)
{
    auto be32 = [](uint32_t v)
    {
        std::string s(4, '\0');
        for (int i = 0; i < 4; ++i) s[i] = char((v >> (8 * (3 - i))) & 0xff);
        return s;
    };
    auto chunk = [&](const std::string &type, const std::string &data)
    {
        const std::string body = type + data;
        return be32((uint32_t)data.size()) + body +
               be32((uint32_t)crc32(0, (const Bytef *)body.data(), (uInt)body.size()));
    };

    // 1x1 8-bit RGB, one uncompressed zlib block holding a single filtered scanline
    std::string        ihdr = be32(1) + be32(1) + std::string("\x08\x02\x00\x00\x00", 5);
    std::string        raw  = std::string("\x00\xff\x00\x00", 4);
    uLongf             cap  = compressBound((uLong)raw.size());
    std::vector<Bytef> z(cap);
    compress(z.data(), &cap, (const Bytef *)raw.data(), (uLong)raw.size());

    // iTXt: keyword \0 compression-flag compression-method language \0 translated \0 text
    const std::string itxt = std::string("XML:com.adobe.xmp") + '\0' + '\0' + '\0' + '\0' + '\0' + xmp;

    return std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", ihdr) + chunk("iTXt", itxt) +
           chunk("IDAT", std::string((char *)z.data(), cap)) + chunk("IEND", "");
}

} // namespace

TEST_CASE("An image whose XMP the parser dislikes still loads its pixels")
{
    // Image::finalize() parses XMP with nothing catching around the call
    const std::string xmp = packet(R"(<rdf:Description rdf:about=""
        xmlns="http://purl.org/dc/elements/1.1/"
        xmlns:dc="http://purl.org/dc/elements/1.1/">
        <dc:title>ok</dc:title>
      </rdf:Description>)");

    // first the control, so a broken fixture cannot masquerade as the bug
    std::istringstream    plain{png_with_xmp("")};
    std::vector<ImagePtr> plain_images;
    CHECK_NOTHROW(plain_images = load_image(plain, "plain.png"));
    REQUIRE(plain_images.size() == 1);
    CHECK(plain_images[0]->size() == int2{1, 1});

    std::istringstream    is{png_with_xmp(xmp)};
    std::vector<ImagePtr> images;
    CHECK_NOTHROW(images = load_image(is, "xmp-test.png"));
    REQUIRE(images.size() == 1);
    CHECK(images[0]->size() == int2{1, 1});
}
