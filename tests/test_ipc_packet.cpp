//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "endian-utils.h"
#include "ipc/ipc_packet.h"
#include "ipc_test_helpers.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace
{

/// Frame arbitrary payload bytes as a packet, filling in the length prefix the way a sender would.
std::vector<char> framed(IpcPacketType type, const std::vector<char> &payload)
{
    std::vector<char> bytes(sizeof(uint32_t));
    bytes.push_back(char(uint8_t(type)));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    write_as(reinterpret_cast<unsigned char *>(bytes.data()), uint32_t(bytes.size()), Endian::Little);
    return bytes;
}

template <typename T>
void append(std::vector<char> &v, T value)
{
    const size_t at = v.size();
    v.resize(at + sizeof(T));
    write_as(reinterpret_cast<unsigned char *>(v.data()) + at, value, Endian::Little);
}

void append_string(std::vector<char> &v, std::string_view s)
{
    v.insert(v.end(), s.begin(), s.end());
    v.push_back('\0');
}

IpcPacket parse(const std::vector<char> &bytes) { return IpcPacket{bytes.data(), bytes.size()}; }

/// An UpdateImage (v1) packet, which carried one channel and did not say so.
std::vector<char> update_v1(std::string_view channel, int2 size, const std::vector<float> &plane)
{
    std::vector<char> payload;
    payload.push_back(0); // grab_focus
    append_string(payload, "img");
    append_string(payload, channel);
    append<int32_t>(payload, 0);
    append<int32_t>(payload, 0);
    append<int32_t>(payload, size.x);
    append<int32_t>(payload, size.y);
    for (float v : plane) append<float>(payload, v);
    return framed(IpcPacketType::UpdateImage, payload);
}

/// An UpdateImageV2 packet, which added a channel count but kept one contiguous plane per channel.
std::vector<char> update_v2(const std::vector<std::string> &channels, int2 size, const std::vector<float> &planes)
{
    std::vector<char> payload;
    payload.push_back(0); // grab_focus
    append_string(payload, "img");
    append<int32_t>(payload, int32_t(channels.size()));
    for (const auto &c : channels) append_string(payload, c);
    append<int32_t>(payload, 0);
    append<int32_t>(payload, 0);
    append<int32_t>(payload, size.x);
    append<int32_t>(payload, size.y);
    for (float v : planes) append<float>(payload, v);
    return framed(IpcPacketType::UpdateImageV2, payload);
}

/// Every drawing command, with the number of floats tev's own VgCommand::bytes() says each one carries.
/**
    Transcribed from tev's include/tev/VectorGraphics.h rather than derived from VgCommand::num_floats(), so
    the reader and the builder below cannot agree with each other on a count tev does not use.
*/
const std::vector<std::pair<VgCommand::Type, int>> &tev_arity()
{
    using T = VgCommand::Type;
    static const std::vector<std::pair<T, int>> table{
        {T::Save, 0},
        {T::Restore, 0},
        {T::FillColor, 4},
        {T::Fill, 0},
        {T::StrokeColor, 4},
        {T::Stroke, 0},
        {T::BeginPath, 0},
        {T::ClosePath, 0},
        {T::PathWinding, 1},
        {T::DebugDumpPathCache, 0},
        {T::MoveTo, 2},
        {T::LineTo, 2},
        {T::ArcTo, 5},
        {T::Arc, 6},
        {T::BezierTo, 6},
        {T::Circle, 3},
        {T::Ellipse, 4},
        {T::QuadTo, 4},
        {T::Rect, 4},
        {T::RoundedRect, 5},
        {T::RoundedRectVarying, 8},
        {T::Text, 2},
        {T::TextAlign, 1},
        {T::FontFace, 0},
        {T::FontSize, 2},
        {T::StrokeWidth, 2},
    };
    return table;
}

/// One command of every type, each carrying data no other one does.
std::vector<VgCommand> one_command_of_each_type()
{
    std::vector<VgCommand> commands;
    for (const auto &[type, n] : tev_arity())
    {
        VgCommand c{type, {}, {}};
        for (int k = 0; k < n; ++k) c.data.push_back(100.f * float(int(type)) + float(k) + 0.5f);
        if (type == VgCommand::Type::Text || type == VgCommand::Type::FontFace)
            c.text = "text of " + std::to_string(int(type));
        commands.push_back(std::move(c));
    }
    return commands;
}

/// A well-formed packet of every type the builders can make, and of the UpdateImage versions they cannot.
std::vector<IpcPacket> packets_of_every_type()
{
    const Box2i              bounds{int2{4, 8}, int2{7, 10}};
    const int                n_pixels = bounds.size().x * bounds.size().y;
    std::vector<float>       data(size_t(n_pixels) * 3);
    std::vector<std::string> names{"R", "G", "B"};
    for (size_t i = 0; i < data.size(); ++i) data[i] = float(i);

    std::vector<char> open_v1;
    open_v1.push_back(1); // grab_focus
    append_string(open_v1, "/tmp/render.exr:diffuse");

    return {
        parse(framed(IpcPacketType::OpenImage, open_v1)),
        ipc_test::open_image("/tmp/render.exr", "diffuse", true),
        ipc_test::reload_image("beauty", false),
        ipc_test::close_image("beauty"),
        ipc_test::create_image("render", true, int2{64, 32}, {"R", "G", "B", "albedo.R"}),
        parse(update_v1("Y", bounds.size(), std::vector<float>(data.begin(), data.begin() + n_pixels))),
        parse(update_v2(names, bounds.size(), data)),
        ipc_test::update_image("render", false, names, {0, 1, 2}, {3, 3, 3}, bounds, data),
        ipc_test::vector_graphics("render", false, true, one_command_of_each_type()),
    };
}

/// Read \p p's payload with the reader its type calls for.
void read_payload(const IpcPacket &p)
{
    switch (p.type())
    {
    case IpcPacketType::OpenImage:
    case IpcPacketType::OpenImageV2: p.as_open_image(); break;
    case IpcPacketType::ReloadImage: p.as_reload_image(); break;
    case IpcPacketType::CloseImage: p.as_close_image(); break;
    case IpcPacketType::CreateImage: p.as_create_image(); break;
    case IpcPacketType::UpdateImage:
    case IpcPacketType::UpdateImageV2:
    case IpcPacketType::UpdateImageV3: p.as_update_image(); break;
    case IpcPacketType::VectorGraphics: p.as_vector_graphics(); break;
    default: FAIL("no reader for packet type ", int(p.type()));
    }
}

} // namespace

TEST_CASE("IPC packets round-trip through the wire format")
{
    SUBCASE("OpenImage keeps the path and selector apart")
    {
        auto p = ipc_test::open_image("/tmp/render.exr", "diffuse", true);
        CHECK(p.type() == IpcPacketType::OpenImageV2);

        auto info = parse(p.bytes()).as_open_image();
        CHECK(info.path == "/tmp/render.exr");
        CHECK(info.channel_selector == "diffuse");
        CHECK(info.grab_focus);
    }

    SUBCASE("CloseImage carries just a name")
    {
        auto info = parse(ipc_test::close_image("beauty").bytes()).as_close_image();
        CHECK(info.name == "beauty");
    }

    SUBCASE("ReloadImage")
    {
        auto info = parse(ipc_test::reload_image("beauty", false).bytes()).as_reload_image();
        CHECK(info.name == "beauty");
        CHECK_FALSE(info.grab_focus);
    }

    SUBCASE("CreateImage names every channel")
    {
        const std::vector<std::string> names{"R", "G", "B", "albedo.R"};
        auto info = parse(ipc_test::create_image("render", true, int2{64, 32}, names).bytes()).as_create_image();

        CHECK(info.name == "render");
        CHECK(info.size == int2{64, 32});
        CHECK(info.channel_names == names);
    }

    SUBCASE("UpdateImage preserves the interleaved payload and how to address it")
    {
        // three channels interleaved, as a renderer hands over an RGB bucket
        const Box2i                    bounds{int2{4, 8}, int2{8, 12}};
        const std::vector<std::string> names{"R", "G", "B"};
        const std::vector<int64_t>     offsets{0, 1, 2};
        const std::vector<int64_t>     strides{3, 3, 3};

        std::vector<float> data(size_t(bounds.size().x) * bounds.size().y * 3);
        for (size_t i = 0; i < data.size(); ++i) data[i] = float(i) * 0.5f;

        auto info = parse(ipc_test::update_image("render", false, names, offsets, strides, bounds, data).bytes())
                        .as_update_image();

        CHECK(info.name == "render");
        CHECK(info.bounds == bounds);
        CHECK(info.channel_names == names);
        CHECK(info.channel_offsets == offsets);
        CHECK(info.channel_strides == strides);
        CHECK(info.data == data);

        // row_stride() has to agree with the addressing, since upload_tile() is driven by that pair
        CHECK(info.row_stride(0) == int64_t(bounds.size().x) * 3);
    }
}

TEST_CASE("Every drawing command has tev's argument count, and round-trips through a packet")
{
    // The stream carries no per-command length, so a command's type is the only thing that says where the
    // next one starts, and the count has to be tev's own or neither side can read what the other wrote.
    for (int t = -128; t <= 127; ++t)
    {
        CAPTURE(t);
        const auto type     = VgCommand::Type(int8_t(t));
        const auto in_table = std::find_if(tev_arity().begin(), tev_arity().end(),
                                           [type](const auto &entry) { return entry.first == type; });
        // anything else, Invalid included, is a type whose length is unknown
        CHECK(VgCommand::num_floats(type) == (in_table == tev_arity().end() ? -1 : in_table->second));
        CHECK(VgCommand::has_text(type) == (type == VgCommand::Type::Text || type == VgCommand::Type::FontFace));
    }

    const auto commands = one_command_of_each_type();
    auto       info     = parse(ipc_test::vector_graphics("render", true, true, commands).bytes()).as_vector_graphics();

    CHECK(info.name == "render");
    CHECK(info.grab_focus);
    CHECK(info.append);
    REQUIRE(info.commands.size() == commands.size());
    for (size_t i = 0; i < commands.size(); ++i)
    {
        CAPTURE(int(commands[i].type));
        CHECK(info.commands[i].type == commands[i].type);
        CHECK(info.commands[i].data == commands[i].data);
        CHECK(info.commands[i].text == commands[i].text);
    }
}

TEST_CASE("A VectorGraphics packet with an unreadable command stream is refused")
{
    auto framed_vg = [](const std::vector<char> &tail)
    {
        std::vector<char> payload;
        payload.push_back(0); // grab_focus
        append_string(payload, "img");
        payload.push_back(1); // append
        payload.insert(payload.end(), tail.begin(), tail.end());
        return framed(IpcPacketType::VectorGraphics, payload);
    };

    SUBCASE("an unknown command type, whose length is therefore unknown")
    {
        // the whole rest of the packet becomes unparseable, so this cannot be skipped past
        std::vector<char> tail;
        append<int32_t>(tail, 1);
        tail.push_back(99); // not a command we know
        CHECK_THROWS_AS(parse(framed_vg(tail)).as_vector_graphics(), std::runtime_error);
    }

    SUBCASE("a command count that would be an allocation attack")
    {
        std::vector<char> tail;
        append<int32_t>(tail, 1 << 30);
        CHECK_THROWS_AS(parse(framed_vg(tail)).as_vector_graphics(), std::runtime_error);
    }

    SUBCASE("an empty command list is legitimate -- it clears the overlay")
    {
        std::vector<char> tail;
        append<int32_t>(tail, 0);
        auto info = parse(framed_vg(tail)).as_vector_graphics();
        CHECK(info.commands.empty());
        CHECK(info.append);
    }
}

TEST_CASE("Older UpdateImage versions are read as the newer one's equivalent")
{
    // V1 had no channel count and one channel; V2 added the count but kept the planes contiguous. Both have
    // to arrive as offsets/strides, which is all the rest of the code reads.
    const int2         size{2, 2};
    std::vector<float> plane_a{1.f, 2.f, 3.f, 4.f};
    std::vector<float> plane_b{5.f, 6.f, 7.f, 8.f};

    SUBCASE("v1 implies a single channel")
    {
        auto info = parse(update_v1("Y", size, plane_a)).as_update_image();
        CHECK(info.num_channels() == 1);
        CHECK(info.channel_offsets == std::vector<int64_t>{0});
        CHECK(info.channel_strides == std::vector<int64_t>{1});
        CHECK(info.data == plane_a);
    }

    SUBCASE("v2 concatenates one plane per channel")
    {
        std::vector<float> planes = plane_a;
        planes.insert(planes.end(), plane_b.begin(), plane_b.end());

        auto info = parse(update_v2({"Y", "A"}, size, planes)).as_update_image();
        REQUIRE(info.num_channels() == 2);
        CHECK(info.data == planes);

        // the addressing IpcUpdateImage documents, with the offsets and strides the parser derived, lands on
        // each channel's own plane
        for (int c = 0; c < 2; ++c)
            for (int px = 0; px < size.x * size.y; ++px)
            {
                CAPTURE(c);
                CAPTURE(px);
                CHECK(info.data[size_t(info.channel_offsets[c] + px * info.channel_strides[c])] ==
                      (c == 0 ? plane_a : plane_b)[size_t(px)]);
            }
    }
}

TEST_CASE("Malformed IPC packets are refused rather than believed")
{
    // everything here arrives over a socket from another process

    SUBCASE("a length that disagrees with the bytes present")
    {
        auto bytes = ipc_test::close_image("x").bytes();
        write_as(reinterpret_cast<unsigned char *>(bytes.data()), uint32_t(bytes.size() + 10), Endian::Little);
        CHECK_THROWS_AS(parse(bytes), std::runtime_error);
    }

    SUBCASE("too short to hold a type at all")
    {
        std::vector<char> bytes(3, 0);
        CHECK_THROWS_AS(parse(bytes), std::runtime_error);
    }

    SUBCASE("read as the wrong type")
    {
        auto p = parse(ipc_test::close_image("x").bytes());
        CHECK_THROWS_AS(p.as_update_image(), std::runtime_error);
        CHECK_THROWS_AS(p.as_create_image(), std::runtime_error);
    }

    SUBCASE("a packet cut short anywhere")
    {
        // Every field a packet carries is one its reader needs, so any prefix of a well-formed packet, framed
        // with a length that agrees with it, is missing something: a string's terminator, a count, an
        // argument, a sample.
        for (const auto &whole : packets_of_every_type())
        {
            CAPTURE(int(whole.type()));
            REQUIRE_NOTHROW(read_payload(whole));

            const auto &bytes = whole.bytes();
            for (size_t n = sizeof(uint32_t) + 1; n < bytes.size(); ++n)
            {
                CAPTURE(n);
                std::vector<char> cut(bytes.begin(), bytes.begin() + n);
                write_as(reinterpret_cast<unsigned char *>(cut.data()), uint32_t(n), Endian::Little);
                CHECK_THROWS_AS(read_payload(parse(cut)), std::runtime_error);
            }
        }
    }

    SUBCASE("a channel count that would be an allocation attack")
    {
        std::vector<char> payload;
        payload.push_back(0);
        append_string(payload, "img");
        append<int32_t>(payload, 1 << 30);
        CHECK_THROWS_AS(parse(framed(IpcPacketType::UpdateImageV2, payload)).as_update_image(), std::runtime_error);
    }

    SUBCASE("a negative channel count")
    {
        std::vector<char> payload;
        payload.push_back(0);
        append_string(payload, "img");
        append<int32_t>(payload, -1);
        CHECK_THROWS_AS(parse(framed(IpcPacketType::UpdateImageV2, payload)).as_update_image(), std::runtime_error);
    }

    SUBCASE("negative tile dimensions")
    {
        std::vector<char> payload;
        payload.push_back(0);
        append_string(payload, "img");
        append<int32_t>(payload, 1);
        append_string(payload, "Y");
        append<int32_t>(payload, 0);
        append<int32_t>(payload, 0);
        append<int32_t>(payload, -4);
        append<int32_t>(payload, -4);
        CHECK_THROWS_AS(parse(framed(IpcPacketType::UpdateImageV2, payload)).as_update_image(), std::runtime_error);
    }

    SUBCASE("dimensions that overflow into a plausible pixel count")
    {
        std::vector<char> payload;
        payload.push_back(0);
        append_string(payload, "img");
        append<int32_t>(payload, 1);
        append_string(payload, "Y");
        append<int32_t>(payload, 0);
        append<int32_t>(payload, 0);
        append<int32_t>(payload, 65536);
        append<int32_t>(payload, 65536); // 2^32 pixels: wraps to 0 in 32 bits
        CHECK_THROWS_AS(parse(framed(IpcPacketType::UpdateImageV2, payload)).as_update_image(), std::runtime_error);
    }

    SUBCASE("an offset or stride that would read outside the payload")
    {
        auto with = [](int64_t offset, int64_t stride)
        {
            std::vector<char> payload;
            payload.push_back(0);
            append_string(payload, "img");
            append<int32_t>(payload, 1);
            append_string(payload, "Y");
            append<int32_t>(payload, 0);
            append<int32_t>(payload, 0);
            append<int32_t>(payload, 2);
            append<int32_t>(payload, 2);
            append<int64_t>(payload, offset);
            append<int64_t>(payload, stride);
            for (int i = 0; i < 4; ++i) append<float>(payload, 1.f);
            return framed(IpcPacketType::UpdateImageV3, payload);
        };

        // reaches past the four samples that follow
        CHECK_THROWS_AS(parse(with(0, 1000)).as_update_image(), std::runtime_error);
        CHECK_THROWS_AS(parse(with(1000, 1)).as_update_image(), std::runtime_error);
        // negative addressing would index before the buffer
        CHECK_THROWS_AS(parse(with(-1, 1)).as_update_image(), std::runtime_error);
        CHECK_THROWS_AS(parse(with(0, -1)).as_update_image(), std::runtime_error);
        // multiplying these overflows int64 instead of exceeding the payload
        CHECK_THROWS_AS(parse(with(0, std::numeric_limits<int64_t>::max())).as_update_image(), std::runtime_error);
    }
}
