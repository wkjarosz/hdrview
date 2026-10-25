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
#include <functional>
#include <string>
#include <utility>

namespace
{

/// Frame arbitrary payload bytes as a packet, filling in the length prefix the way a sender would.
std::vector<char> framed(IpcPacketType type, const std::vector<char> &payload)
{
    // the length, the type and the payload, sized at once
    std::vector<char> bytes(sizeof(uint32_t) + 1 + payload.size());
    bytes[sizeof(uint32_t)] = char(uint8_t(type));
    std::copy(payload.begin(), payload.end(), bytes.begin() + sizeof(uint32_t) + 1);
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

/// Checks that an UpdateImage's addressing reaches `sample(c, px)` for each pixel `px` of channel `c`.
template <typename F>
void check_update(const IpcUpdateImage &info, const std::vector<std::string> &names, const Box2i &bounds, F &&sample)
{
    CHECK(info.name == "img");
    CHECK(info.bounds == bounds);
    CHECK(info.channel_names == names);
    REQUIRE(info.num_channels() == int(names.size()));
    REQUIRE(info.channel_offsets.size() == names.size());
    REQUIRE(info.channel_strides.size() == names.size());

    // the addressing IpcUpdateImage documents, which is all the rest of the code reads, lands on each channel's
    // own samples; row_stride() has to agree with it, since upload_tile() is driven by that pair
    const int n_pixels = bounds.size().x * bounds.size().y;
    REQUIRE(info.data.size() == size_t(n_pixels) * names.size());
    for (int c = 0; c < info.num_channels(); ++c)
    {
        CAPTURE(c);
        CHECK(info.row_stride(c) == int64_t(bounds.size().x) * info.channel_strides[c]);
        for (int px = 0; px < n_pixels; ++px)
        {
            CAPTURE(px);
            CHECK(info.data[size_t(info.channel_offsets[c] + px * info.channel_strides[c])] == sample(c, px));
        }
    }
}

/// A well-formed packet of one type, and what reading it back has to yield.
struct PacketCase
{
    IpcPacket                              packet;
    std::function<void(const IpcPacket &)> check;
};

/// A packet of every type the builders can make, and of the UpdateImage and OpenImage versions they cannot.
/**
    The UpdateImage versions carry the same samples three ways: one channel (v1, which did not say how many),
    one contiguous plane per channel (v2), and interleaved (v3, as a renderer hands over an RGB bucket). Each
    has to arrive as offsets and strides that address them.
*/
std::vector<PacketCase> packets_of_every_type()
{
    const Box2i              bounds{int2{4, 8}, int2{7, 10}};
    const int                n_pixels = bounds.size().x * bounds.size().y;
    std::vector<float>       data(size_t(n_pixels) * 3);
    std::vector<std::string> names{"R", "G", "B"};
    for (size_t i = 0; i < data.size(); ++i) data[i] = float(i);
    const std::vector<float> plane(data.begin(), data.begin() + n_pixels);
    const Box2i              tile{int2{0}, bounds.size()}; // where v1 and v2 put theirs

    std::vector<char> open_v1;
    open_v1.push_back(1); // grab_focus
    append_string(open_v1, "/tmp/render.exr:diffuse");

    const std::vector<std::string> create_names{"R", "G", "B", "albedo.R"};
    const auto                     commands = one_command_of_each_type();

    return {
        {parse(framed(IpcPacketType::OpenImage, open_v1)),
         [](const IpcPacket &p)
         {
             // v1 packed the selector into the path, which is left unsplit
             const auto info = p.as_open_image();
             CHECK(info.path == "/tmp/render.exr:diffuse");
             CHECK(info.channel_selector.empty());
             CHECK(info.grab_focus);
         }},
        {ipc_test::open_image("/tmp/render.exr", "diffuse", true),
         [](const IpcPacket &p)
         {
             CHECK(p.type() == IpcPacketType::OpenImageV2);
             const auto info = p.as_open_image();
             CHECK(info.path == "/tmp/render.exr");
             CHECK(info.channel_selector == "diffuse");
             CHECK(info.grab_focus);
         }},
        {ipc_test::reload_image("beauty", false),
         [](const IpcPacket &p)
         {
             const auto info = p.as_reload_image();
             CHECK(info.name == "beauty");
             CHECK_FALSE(info.grab_focus);
         }},
        {ipc_test::close_image("beauty"), [](const IpcPacket &p) { CHECK(p.as_close_image().name == "beauty"); }},
        {ipc_test::create_image("render", true, int2{64, 32}, create_names),
         [create_names](const IpcPacket &p)
         {
             const auto info = p.as_create_image();
             CHECK(info.name == "render");
             CHECK(info.grab_focus);
             CHECK(info.size == int2{64, 32});
             CHECK(info.channel_names == create_names);
         }},
        {parse(update_v1("Y", bounds.size(), plane)), [tile](const IpcPacket &p)
         { check_update(p.as_update_image(), {"Y"}, tile, [](int, int px) { return float(px); }); }},
        {parse(update_v2(names, bounds.size(), data)),
         [tile, names, n_pixels](const IpcPacket &p)
         {
             check_update(p.as_update_image(), names, tile,
                          [n_pixels](int c, int px) { return float(c * n_pixels + px); });
         }},
        {ipc_test::update_image("img", true, names, {0, 1, 2}, {3, 3, 3}, bounds, data),
         [bounds, names](const IpcPacket &p)
         {
             const auto info = p.as_update_image();
             CHECK(info.grab_focus);
             check_update(info, names, bounds, [](int c, int px) { return float(px * 3 + c); });
         }},
        {ipc_test::vector_graphics("render", true, true, commands),
         [commands](const IpcPacket &p)
         {
             const auto info = p.as_vector_graphics();
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
         }},
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

TEST_CASE("IPC packets of every type round-trip through the wire format")
{
    for (const PacketCase &c : packets_of_every_type())
    {
        const IpcPacket &packet = c.packet; //< named rather than bound: CAPTURE() captures in a lambda, which a
                                            //structured binding cannot be before C++20
        CAPTURE(int(packet.type()));
        c.check(parse(packet.bytes()));
    }
}

TEST_CASE("Every drawing command has tev's argument count")
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
        for (const PacketCase &c : packets_of_every_type())
        {
            const IpcPacket &whole = c.packet;
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

    SUBCASE("a channel count or tile size no update can have")
    {
        // A count that would be an allocation attack, a negative one, tiles negative along both axes and along
        // one, and one whose 2^32 pixels wrap to 0 in 32 bits. Each is followed by the samples believing it
        // would read, where those are few enough to send, so nothing but the check on the header refuses it.
        const struct
        {
            int32_t channels;
            int2    size;
            int     samples;
        } headers[] = {{1 << 30, {}, 0}, {-1, {}, 0}, {1, {-4, -4}, 16}, {1, {-4, 4}, 16}, {1, {65536, 65536}, 0}};

        for (const auto &h : headers)
        {
            CAPTURE(h.channels);
            CAPTURE(h.size);
            std::vector<char> payload;
            payload.push_back(0);
            append_string(payload, "img");
            append<int32_t>(payload, h.channels);
            if (h.channels == 1)
            {
                append_string(payload, "Y");
                for (int32_t v : {0, 0, h.size.x, h.size.y}) append<int32_t>(payload, v);
                for (int i = 0; i < h.samples; ++i) append<float>(payload, 1.f);
            }
            CHECK_THROWS_AS(parse(framed(IpcPacketType::UpdateImageV2, payload)).as_update_image(), std::runtime_error);
        }
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
