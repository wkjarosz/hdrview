//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "endian-utils.h"
#include "ipc/ipc_packet.h"

#include <cstring>

namespace
{

//! Frame arbitrary payload bytes as a packet, filling in the length prefix the way a sender would.
//! Lets a test state a malformed payload the builders cannot produce.
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

} // namespace

TEST_CASE("IPC packets round-trip through the wire format")
{
    SUBCASE("OpenImage keeps the path and selector apart")
    {
        auto p = IpcPacket::open_image("/tmp/render.exr", "diffuse", true);
        CHECK(p.type() == IpcPacketType::OpenImageV2);

        auto info = parse(p.bytes()).as_open_image();
        CHECK(info.path == "/tmp/render.exr");
        CHECK(info.channel_selector == "diffuse");
        CHECK(info.grab_focus);
    }

    SUBCASE("CloseImage carries just a name")
    {
        auto info = parse(IpcPacket::close_image("beauty").bytes()).as_close_image();
        CHECK(info.name == "beauty");
    }

    SUBCASE("ReloadImage")
    {
        auto info = parse(IpcPacket::reload_image("beauty", false).bytes()).as_reload_image();
        CHECK(info.name == "beauty");
        CHECK_FALSE(info.grab_focus);
    }

    SUBCASE("CreateImage names every channel")
    {
        const std::vector<std::string> names{"R", "G", "B", "albedo.R"};
        auto info = parse(IpcPacket::create_image("render", true, int2{64, 32}, names).bytes()).as_create_image();

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

        auto info = parse(IpcPacket::update_image("render", false, names, offsets, strides, bounds, data).bytes())
                        .as_update_image();

        CHECK(info.name == "render");
        CHECK(info.bounds == bounds);
        CHECK(info.channel_names == names);
        CHECK(info.channel_offsets == offsets);
        CHECK(info.channel_strides == strides);
        REQUIRE(info.data.size() == data.size());
        CHECK(info.data == data);

        // the addressing the struct documents has to land on the right samples
        const int n_pixels = bounds.size().x * bounds.size().y;
        for (int c = 0; c < info.num_channels(); ++c)
            for (int px = 0; px < n_pixels; ++px)
                CHECK(info.data[size_t(info.channel_offsets[c] + px * info.channel_strides[c])] ==
                      data[size_t(px * 3 + c)]);

        // ...and row_stride() has to agree with it, since upload_tile() is driven by that pair
        CHECK(info.row_stride(0) == int64_t(bounds.size().x) * 3);
    }
}

TEST_CASE("VectorGraphics packets round-trip, including the per-type argument counts")
{
    // the stream carries no per-command length, so a command's type is the only thing that says where the
    // next one starts
    std::vector<VgCommand> commands{
        {VgCommand::Type::BeginPath, {}},
        {VgCommand::Type::StrokeColor, {1.f, 0.5f, 0.f, 1.f}},
        {VgCommand::Type::StrokeWidth, {2.f, float(VgCommand::Relative)}},
        {VgCommand::Type::Rect, {4.f, 8.f, 16.f, 32.f}},
        {VgCommand::Type::MoveTo, {1.f, 2.f}},
        {VgCommand::Type::BezierTo, {3.f, 4.f, 5.f, 6.f, 7.f, 8.f}},
        {VgCommand::Type::Stroke, {}},
        {VgCommand::Type::FontSize, {20.f, float(VgCommand::Relative)}},
        {VgCommand::Type::TextAlign, {float(VgCommand::AlignLeft | VgCommand::AlignBaseline)}},
        {VgCommand::Type::FontFace, {}, "sans-bold"},
        {VgCommand::Type::Text, {5.f, 9.f}, "Tile 7"},
    };

    auto info = parse(IpcPacket::vector_graphics("render", false, true, commands).bytes()).as_vector_graphics();

    CHECK(info.name == "render");
    CHECK(info.append);
    REQUIRE(info.commands.size() == commands.size());
    for (size_t i = 0; i < commands.size(); ++i)
    {
        CAPTURE(i);
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

    SUBCASE("a command whose arguments run off the end")
    {
        std::vector<char> tail;
        append<int32_t>(tail, 1);
        tail.push_back(char(VgCommand::Type::BezierTo)); // wants six floats
        append<float>(tail, 1.f);                        // only one follows
        CHECK_THROWS_AS(parse(framed_vg(tail)).as_vector_graphics(), std::runtime_error);
    }

    SUBCASE("a text command with no terminator on its string")
    {
        std::vector<char> tail;
        append<int32_t>(tail, 1);
        tail.push_back(char(VgCommand::Type::Text));
        append<float>(tail, 0.f);
        append<float>(tail, 0.f);
        tail.insert(tail.end(), {'h', 'i'}); // no NUL
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
    const int32_t      w = 2, h = 2;
    std::vector<float> plane_a{1.f, 2.f, 3.f, 4.f};
    std::vector<float> plane_b{5.f, 6.f, 7.f, 8.f};

    SUBCASE("v1 implies a single channel")
    {
        std::vector<char> payload;
        payload.push_back(0); // grab_focus
        append_string(payload, "img");
        append_string(payload, "Y");
        append<int32_t>(payload, 0);
        append<int32_t>(payload, 0);
        append<int32_t>(payload, w);
        append<int32_t>(payload, h);
        for (float v : plane_a) append<float>(payload, v);

        auto info = parse(framed(IpcPacketType::UpdateImage, payload)).as_update_image();
        CHECK(info.num_channels() == 1);
        CHECK(info.channel_offsets == std::vector<int64_t>{0});
        CHECK(info.channel_strides == std::vector<int64_t>{1});
        CHECK(info.data == plane_a);
    }

    SUBCASE("v2 concatenates one plane per channel")
    {
        std::vector<char> payload;
        payload.push_back(0);
        append_string(payload, "img");
        append<int32_t>(payload, 2);
        append_string(payload, "Y");
        append_string(payload, "A");
        append<int32_t>(payload, 0);
        append<int32_t>(payload, 0);
        append<int32_t>(payload, w);
        append<int32_t>(payload, h);
        for (float v : plane_a) append<float>(payload, v);
        for (float v : plane_b) append<float>(payload, v);

        auto info = parse(framed(IpcPacketType::UpdateImageV2, payload)).as_update_image();
        CHECK(info.num_channels() == 2);
        // a plane per channel, so the second starts a whole tile in and both step by one
        CHECK(info.channel_offsets == std::vector<int64_t>{0, w * h});
        CHECK(info.channel_strides == std::vector<int64_t>{1, 1});
        CHECK(info.data.size() == plane_a.size() + plane_b.size());
        CHECK(info.data[size_t(info.channel_offsets[1])] == plane_b[0]);
    }
}

TEST_CASE("Malformed IPC packets are refused rather than believed")
{
    // everything here arrives over a socket from another process

    SUBCASE("a length that disagrees with the bytes present")
    {
        auto bytes = IpcPacket::close_image("x").bytes();
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
        auto p = parse(IpcPacket::close_image("x").bytes());
        CHECK_THROWS_AS(p.as_update_image(), std::runtime_error);
        CHECK_THROWS_AS(p.as_create_image(), std::runtime_error);
    }

    SUBCASE("a string with no terminator")
    {
        std::vector<char> payload{'n', 'a', 'm', 'e'}; // runs off the end
        CHECK_THROWS_AS(parse(framed(IpcPacketType::CloseImage, payload)).as_close_image(), std::runtime_error);
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

    SUBCASE("update data that stops short")
    {
        std::vector<char> payload;
        payload.push_back(0);
        append_string(payload, "img");
        append<int32_t>(payload, 1);
        append_string(payload, "Y");
        append<int32_t>(payload, 0);
        append<int32_t>(payload, 0);
        append<int32_t>(payload, 4);
        append<int32_t>(payload, 4);
        append<float>(payload, 1.f); // one sample where sixteen were promised
        CHECK_THROWS_AS(parse(framed(IpcPacketType::UpdateImageV2, payload)).as_update_image(), std::runtime_error);
    }
}

TEST_CASE("A byte stream is split back into the packets that were written into it")
{
    // what a socket delivers: whatever has arrived, split wherever the network chose
    std::vector<char> stream;
    for (const auto &p : {IpcPacket::close_image("a"), IpcPacket::close_image("bb"), IpcPacket::close_image("ccc")})
        stream.insert(stream.end(), p.bytes().begin(), p.bytes().end());

    SUBCASE("all at once")
    {
        std::vector<std::string> names;
        auto                     buffer = stream;
        extract_ipc_packets(buffer, [&](const IpcPacket &p) { names.push_back(p.as_close_image().name); });

        CHECK(names == std::vector<std::string>{"a", "bb", "ccc"});
        CHECK(buffer.empty());
    }

    SUBCASE("one byte at a time, which is the case that exercises the buffering")
    {
        std::vector<std::string> names;
        std::vector<char>        buffer;
        for (char c : stream)
        {
            buffer.push_back(c);
            extract_ipc_packets(buffer, [&](const IpcPacket &p) { names.push_back(p.as_close_image().name); });
        }

        CHECK(names == std::vector<std::string>{"a", "bb", "ccc"});
        CHECK(buffer.empty());
    }

    SUBCASE("a trailing partial packet is kept for the next read")
    {
        std::vector<std::string> names;
        auto                     buffer = stream;
        buffer.resize(buffer.size() - 1); // cut the last packet short

        extract_ipc_packets(buffer, [&](const IpcPacket &p) { names.push_back(p.as_close_image().name); });

        CHECK(names == std::vector<std::string>{"a", "bb"});
        CHECK_FALSE(buffer.empty()); // the remainder of "ccc" is still waiting
    }

    SUBCASE("an impossible length is unrecoverable, since nothing marks a packet boundary")
    {
        std::vector<char> buffer(sizeof(uint32_t));
        write_as(reinterpret_cast<unsigned char *>(buffer.data()), uint32_t(2), Endian::Little);
        CHECK_THROWS_AS(extract_ipc_packets(buffer, [](const IpcPacket &) {}), std::runtime_error);
    }
}
