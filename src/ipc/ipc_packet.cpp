//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "ipc/ipc_packet.h"

#include "endian-utils.h"

#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <spdlog/fmt/fmt.h>

namespace
{

//! Reads fields out of a packet's bytes, refusing to run off the end of them.
/*!
    Every read is checked rather than the length being validated once up front, because the payload's own
    contents decide how much follows: a channel count says how many strings come next, and a string ends
    wherever its NUL is.
*/
class PacketReader
{
public:
    PacketReader(const std::vector<char> &data) : m_data(data)
    {
        // The length prefix is part of the framing and was checked when the packet was framed; step over it.
        m_index = sizeof(uint32_t);
    }

    template <typename T>
    T read()
    {
        static_assert(std::is_trivially_copyable_v<T>, "read() needs a trivially copyable type");
        require(sizeof(T), "a value");
        T value = read_as<T>(reinterpret_cast<const unsigned char *>(m_data.data()) + m_index, Endian::Little);
        m_index += sizeof(T);
        return value;
    }

    bool read_bool() { return read<uint8_t>() != 0; }

    std::string read_string()
    {
        const size_t start = m_index;
        while (m_index < m_data.size() && m_data[m_index] != '\0') ++m_index;

        if (m_index >= m_data.size())
            throw std::runtime_error{"IPC packet ended inside a string."};

        std::string value{m_data.data() + start, m_index - start};
        ++m_index; // the NUL
        return value;
    }

    //! Number of bytes not yet read.
    size_t remaining() const { return m_data.size() - m_index; }

    //! Read `count` floats, which must be the whole of what is left.
    std::vector<float> read_floats(size_t count)
    {
        require(count * sizeof(float), "sample data");

        std::vector<float> values(count);
        // An empty vector's data() may be null, and memcpy is not allowed one even for zero bytes -- which
        // every drawing command that takes no arguments would otherwise do.
        if (count == 0)
            return values;

        read_array(values.data(), reinterpret_cast<const unsigned char *>(m_data.data()) + m_index, count,
                   Endian::Little);
        m_index += count * sizeof(float);
        return values;
    }

private:
    void require(size_t n, const char *what) const
    {
        if (n > m_data.size() - m_index)
            throw std::runtime_error{fmt::format("IPC packet ended before {} it declared ({} bytes needed, {} left).",
                                                 what, n, m_data.size() - m_index)};
    }

    const std::vector<char> &m_data;
    size_t                   m_index = 0;
};

//! Builds a packet's bytes, keeping the length prefix up to date as it grows.
class PacketWriter
{
public:
    PacketWriter(IpcPacketType type)
    {
        m_bytes.resize(sizeof(uint32_t)); // reserved for the length, filled in by bytes()
        write<uint8_t>(uint8_t(type));
    }

    template <typename T>
    void write(T value)
    {
        const size_t at = m_bytes.size();
        m_bytes.resize(at + sizeof(T));
        write_as(reinterpret_cast<unsigned char *>(m_bytes.data()) + at, value, Endian::Little);
    }

    void write_bool(bool value) { write<uint8_t>(value ? 1 : 0); }

    void write_string(std::string_view value)
    {
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
        m_bytes.push_back('\0');
    }

    void write_floats(const std::vector<float> &values)
    {
        const size_t at = m_bytes.size();
        m_bytes.resize(at + values.size() * sizeof(float));
        write_array(reinterpret_cast<unsigned char *>(m_bytes.data()) + at, values.data(), values.size(),
                    Endian::Little);
    }

    std::vector<char> bytes() &&
    {
        if (m_bytes.size() > k_max_ipc_packet_size)
            throw std::runtime_error{fmt::format("IPC packet of {} bytes exceeds the {} byte limit.", m_bytes.size(),
                                                 k_max_ipc_packet_size)};

        write_as(reinterpret_cast<unsigned char *>(m_bytes.data()), uint32_t(m_bytes.size()), Endian::Little);
        return std::move(m_bytes);
    }

private:
    std::vector<char> m_bytes;
};

//! Reads a channel-name list preceded by its count, bounded so a count cannot become an allocation.
std::vector<std::string> read_channel_names(PacketReader &r, int32_t count)
{
    if (count <= 0 || count > k_max_ipc_channels)
        throw std::runtime_error{
            fmt::format("IPC packet declares {} channels; expected 1 to {}.", count, k_max_ipc_channels)};

    std::vector<std::string> names;
    names.reserve(size_t(count));
    for (int32_t i = 0; i < count; ++i) names.push_back(r.read_string());
    return names;
}

//! The half-open rectangle an update packet covers, rejecting dimensions no tile has.
Box2i read_bounds(PacketReader &r)
{
    const int32_t x = r.read<int32_t>();
    const int32_t y = r.read<int32_t>();
    const int32_t w = r.read<int32_t>();
    const int32_t h = r.read<int32_t>();

    // Width and height multiply into a pixel count and then index the payload, so a negative or absurd one
    // has to be refused here rather than wrapping into a plausible-looking size later.
    if (w <= 0 || h <= 0 || int64_t(w) * h > int64_t(k_max_ipc_packet_size / sizeof(float)))
        throw std::runtime_error{fmt::format("IPC update packet has implausible dimensions {}x{}.", w, h)};

    return Box2i{int2{x, y}, int2{x + w, y + h}};
}

/*!
    Highest index into the sample payload that `offsets`/`strides` can reach, or none if they overflow.

    Every access this addressing performs is `offset[c] + px * stride[c]` for px in [0, n_pixels), so
    bounding the largest of them bounds all of them -- but only once negative offsets and strides are ruled
    out, since with those the largest index is no longer the last pixel's.
*/
std::optional<int64_t> max_sample_index(const std::vector<int64_t> &offsets, const std::vector<int64_t> &strides,
                                        int64_t n_pixels)
{
    int64_t highest = 0;
    for (size_t c = 0; c < offsets.size(); ++c)
    {
        if (offsets[c] < 0 || strides[c] < 0)
            return std::nullopt;

        // (n_pixels - 1) * stride + offset, refusing to wrap rather than computing a wrapped answer.
        constexpr int64_t k_max = std::numeric_limits<int64_t>::max();
        if (strides[c] != 0 && (n_pixels - 1) > k_max / strides[c])
            return std::nullopt;
        const int64_t span = (n_pixels - 1) * strides[c];
        if (offsets[c] > k_max - span)
            return std::nullopt;

        highest = std::max(highest, offsets[c] + span);
    }
    return highest;
}

} // namespace

IpcPacket::IpcPacket(const char *data, size_t length)
{
    if (length < sizeof(uint32_t) + 1)
        throw std::runtime_error{fmt::format("IPC packet of {} bytes is too short to hold a type.", length)};

    const uint32_t declared = read_as<uint32_t>(reinterpret_cast<const unsigned char *>(data), Endian::Little);
    if (declared != length)
        throw std::runtime_error{fmt::format("IPC packet declares {} bytes but {} were given.", declared, length)};

    m_bytes.assign(data, data + length);
}

IpcPacketType IpcPacket::type() const
{
    if (m_bytes.size() < sizeof(uint32_t) + 1)
        throw std::runtime_error{"IPC packet has no type byte."};
    return IpcPacketType(uint8_t(m_bytes[sizeof(uint32_t)]));
}

IpcOpenImage IpcPacket::as_open_image() const
{
    PacketReader r{m_bytes};
    const auto   t = IpcPacketType(r.read<uint8_t>());
    if (t != IpcPacketType::OpenImage && t != IpcPacketType::OpenImageV2)
        throw std::runtime_error{"IPC packet is not an OpenImage."};

    IpcOpenImage result;
    result.grab_focus = r.read_bool();
    result.path       = r.read_string();
    // v1 packed the channel selector into the path, and left it to the receiver to split; v2 sends it
    // separately. Nothing here splits a v1 path -- HDRView's own loader takes a selector alongside a path.
    if (t == IpcPacketType::OpenImageV2)
        result.channel_selector = r.read_string();
    return result;
}

IpcReloadImage IpcPacket::as_reload_image() const
{
    PacketReader r{m_bytes};
    if (IpcPacketType(r.read<uint8_t>()) != IpcPacketType::ReloadImage)
        throw std::runtime_error{"IPC packet is not a ReloadImage."};

    IpcReloadImage result;
    result.grab_focus = r.read_bool();
    result.name       = r.read_string();
    return result;
}

IpcCloseImage IpcPacket::as_close_image() const
{
    PacketReader r{m_bytes};
    if (IpcPacketType(r.read<uint8_t>()) != IpcPacketType::CloseImage)
        throw std::runtime_error{"IPC packet is not a CloseImage."};

    IpcCloseImage result;
    result.name = r.read_string();
    return result;
}

IpcCreateImage IpcPacket::as_create_image() const
{
    PacketReader r{m_bytes};
    if (IpcPacketType(r.read<uint8_t>()) != IpcPacketType::CreateImage)
        throw std::runtime_error{"IPC packet is not a CreateImage."};

    IpcCreateImage result;
    result.grab_focus = r.read_bool();
    result.name       = r.read_string();

    const int32_t w = r.read<int32_t>();
    const int32_t h = r.read<int32_t>();
    if (w <= 0 || h <= 0)
        throw std::runtime_error{fmt::format("IPC CreateImage has implausible dimensions {}x{}.", w, h)};
    result.size = int2{w, h};

    result.channel_names = read_channel_names(r, r.read<int32_t>());
    return result;
}

IpcUpdateImage IpcPacket::as_update_image() const
{
    PacketReader r{m_bytes};
    const auto   t = IpcPacketType(r.read<uint8_t>());
    if (t != IpcPacketType::UpdateImage && t != IpcPacketType::UpdateImageV2 && t != IpcPacketType::UpdateImageV3)
        throw std::runtime_error{"IPC packet is not an UpdateImage."};

    IpcUpdateImage result;
    result.grab_focus = r.read_bool();
    result.name       = r.read_string();

    // v1 carried exactly one channel and did not say so.
    const int32_t n_channels = t >= IpcPacketType::UpdateImageV2 ? r.read<int32_t>() : 1;
    result.channel_names     = read_channel_names(r, n_channels);

    result.bounds          = read_bounds(r);
    const int64_t n_pixels = int64_t(result.bounds.size().x) * result.bounds.size().y;

    result.channel_offsets.resize(size_t(n_channels));
    result.channel_strides.resize(size_t(n_channels));
    if (t >= IpcPacketType::UpdateImageV3)
    {
        for (auto &o : result.channel_offsets) o = r.read<int64_t>();
        for (auto &s : result.channel_strides) s = r.read<int64_t>();
    }
    else
    {
        // Before v3 the channels were simply concatenated, one contiguous plane after another.
        for (int32_t c = 0; c < n_channels; ++c)
        {
            result.channel_offsets[size_t(c)] = n_pixels * c;
            result.channel_strides[size_t(c)] = 1;
        }
    }

    const auto highest = max_sample_index(result.channel_offsets, result.channel_strides, n_pixels);
    if (!highest)
        throw std::runtime_error{"IPC UpdateImage has channel offsets/strides that do not address a buffer."};

    const int64_t needed = *highest + 1;
    if (uint64_t(needed) * sizeof(float) > r.remaining())
        throw std::runtime_error{
            fmt::format("IPC UpdateImage needs {} samples but only {} bytes of data follow.", needed, r.remaining())};

    result.data = r.read_floats(size_t(needed));
    return result;
}

IpcVectorGraphics IpcPacket::as_vector_graphics() const
{
    PacketReader r{m_bytes};
    if (IpcPacketType(r.read<uint8_t>()) != IpcPacketType::VectorGraphics)
        throw std::runtime_error{"IPC packet is not a VectorGraphics."};

    IpcVectorGraphics result;
    result.grab_focus = r.read_bool();
    result.name       = r.read_string();
    result.append     = r.read_bool();

    const int32_t count = r.read<int32_t>();
    if (count < 0 || count > k_max_ipc_vg_commands)
        throw std::runtime_error{
            fmt::format("IPC VectorGraphics declares {} commands; expected 0 to {}.", count, k_max_ipc_vg_commands)};

    result.commands.reserve(size_t(count));
    for (int32_t i = 0; i < count; ++i)
    {
        VgCommand cmd;
        cmd.type = VgCommand::Type(r.read<int8_t>());

        // Nothing in the stream says how long a command is; its type does, via this table. So an
        // unrecognized type is not a command to skip -- it leaves us with no idea where the next one
        // starts, and the rest of the packet is unreadable.
        const int n = VgCommand::num_floats(cmd.type);
        if (n < 0)
            throw std::runtime_error{fmt::format("IPC VectorGraphics has an unknown command type {}.", int(cmd.type))};

        cmd.data = r.read_floats(size_t(n));
        if (VgCommand::has_text(cmd.type))
            cmd.text = r.read_string();

        result.commands.push_back(std::move(cmd));
    }

    return result;
}

IpcPacket IpcPacket::open_image(std::string_view path, std::string_view channel_selector, bool grab_focus)
{
    PacketWriter w{IpcPacketType::OpenImageV2};
    w.write_bool(grab_focus);
    w.write_string(path);
    w.write_string(channel_selector);

    IpcPacket p;
    p.m_bytes = std::move(w).bytes();
    return p;
}

IpcPacket IpcPacket::reload_image(std::string_view name, bool grab_focus)
{
    PacketWriter w{IpcPacketType::ReloadImage};
    w.write_bool(grab_focus);
    w.write_string(name);

    IpcPacket p;
    p.m_bytes = std::move(w).bytes();
    return p;
}

IpcPacket IpcPacket::close_image(std::string_view name)
{
    PacketWriter w{IpcPacketType::CloseImage};
    w.write_string(name);

    IpcPacket p;
    p.m_bytes = std::move(w).bytes();
    return p;
}

IpcPacket IpcPacket::create_image(std::string_view name, bool grab_focus, int2 size,
                                  const std::vector<std::string> &channel_names)
{
    PacketWriter w{IpcPacketType::CreateImage};
    w.write_bool(grab_focus);
    w.write_string(name);
    w.write<int32_t>(size.x);
    w.write<int32_t>(size.y);
    w.write<int32_t>(int32_t(channel_names.size()));
    for (const auto &n : channel_names) w.write_string(n);

    IpcPacket p;
    p.m_bytes = std::move(w).bytes();
    return p;
}

IpcPacket IpcPacket::update_image(std::string_view name, bool grab_focus, const std::vector<std::string> &channel_names,
                                  const std::vector<int64_t> &offsets, const std::vector<int64_t> &strides,
                                  const Box2i &bounds, const std::vector<float> &data)
{
    if (channel_names.size() != offsets.size() || channel_names.size() != strides.size())
        throw std::runtime_error{"UpdateImage needs one offset and one stride per channel."};

    PacketWriter w{IpcPacketType::UpdateImageV3};
    w.write_bool(grab_focus);
    w.write_string(name);
    w.write<int32_t>(int32_t(channel_names.size()));
    for (const auto &n : channel_names) w.write_string(n);
    w.write<int32_t>(bounds.min.x);
    w.write<int32_t>(bounds.min.y);
    w.write<int32_t>(bounds.size().x);
    w.write<int32_t>(bounds.size().y);
    for (auto o : offsets) w.write<int64_t>(o);
    for (auto s : strides) w.write<int64_t>(s);
    w.write_floats(data);

    IpcPacket p;
    p.m_bytes = std::move(w).bytes();
    return p;
}

IpcPacket IpcPacket::vector_graphics(std::string_view name, bool grab_focus, bool append,
                                     const std::vector<VgCommand> &commands)
{
    PacketWriter w{IpcPacketType::VectorGraphics};
    w.write_bool(grab_focus);
    w.write_string(name);
    w.write_bool(append);
    w.write<int32_t>(int32_t(commands.size()));
    for (const auto &c : commands)
    {
        const int n = VgCommand::num_floats(c.type);
        if (n < 0 || int(c.data.size()) != n)
            throw std::runtime_error{
                fmt::format("VgCommand of type {} needs {} floats but has {}.", int(c.type), n, c.data.size())};

        w.write<int8_t>(int8_t(c.type));
        for (float v : c.data) w.write<float>(v);
        if (VgCommand::has_text(c.type))
            w.write_string(c.text);
    }

    IpcPacket p;
    p.m_bytes = std::move(w).bytes();
    return p;
}

void extract_ipc_packets(std::vector<char> &buffer, const std::function<void(const IpcPacket &)> &on_packet)
{
    size_t consumed = 0;
    while (buffer.size() - consumed >= sizeof(uint32_t))
    {
        const uint32_t declared =
            read_as<uint32_t>(reinterpret_cast<const unsigned char *>(buffer.data()) + consumed, Endian::Little);

        // A length that could never frame a packet means the stream is not carrying this protocol, or has
        // lost sync. Nothing marks a packet boundary, so there is nothing to resynchronize to.
        if (declared < sizeof(uint32_t) + 1 || declared > k_max_ipc_packet_size)
            throw std::runtime_error{fmt::format("IPC stream declares a {} byte packet.", declared)};

        if (buffer.size() - consumed < declared)
            break; // the rest is still in flight

        on_packet(IpcPacket{buffer.data() + consumed, declared});
        consumed += declared;
    }

    buffer.erase(buffer.begin(), buffer.begin() + consumed);
}
