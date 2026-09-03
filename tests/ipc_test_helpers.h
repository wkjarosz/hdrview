//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "endian-utils.h"
#include "ipc/ipc_packet.h"

#include <spdlog/fmt/fmt.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// The sending half of tev's IPC protocol, which HDRView itself has no use for: it only receives.
namespace ipc_test
{

/// Builds a packet's bytes, keeping the length prefix up to date as it grows.
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

/// Frame what \p w has written as a packet, checking it the way a received one is checked.
inline IpcPacket packet(PacketWriter &&w)
{
    auto bytes = std::move(w).bytes();
    return IpcPacket{bytes.data(), bytes.size()};
}

inline IpcPacket open_image(std::string_view path, std::string_view channel_selector, bool grab_focus)
{
    PacketWriter w{IpcPacketType::OpenImageV2};
    w.write_bool(grab_focus);
    w.write_string(path);
    w.write_string(channel_selector);
    return packet(std::move(w));
}

inline IpcPacket reload_image(std::string_view name, bool grab_focus)
{
    PacketWriter w{IpcPacketType::ReloadImage};
    w.write_bool(grab_focus);
    w.write_string(name);
    return packet(std::move(w));
}

inline IpcPacket close_image(std::string_view name)
{
    PacketWriter w{IpcPacketType::CloseImage};
    w.write_string(name);
    return packet(std::move(w));
}

inline IpcPacket create_image(std::string_view name, bool grab_focus, int2 size,
                              const std::vector<std::string> &channel_names)
{
    PacketWriter w{IpcPacketType::CreateImage};
    w.write_bool(grab_focus);
    w.write_string(name);
    w.write<int32_t>(size.x);
    w.write<int32_t>(size.y);
    w.write<int32_t>(int32_t(channel_names.size()));
    for (const auto &n : channel_names) w.write_string(n);
    return packet(std::move(w));
}

/// Builds a V3 update. `offsets`/`strides` address `data` as IpcUpdateImage documents.
inline IpcPacket update_image(std::string_view name, bool grab_focus, const std::vector<std::string> &channel_names,
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
    return packet(std::move(w));
}

inline IpcPacket vector_graphics(std::string_view name, bool grab_focus, bool append,
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
    return packet(std::move(w));
}

} // namespace ipc_test
