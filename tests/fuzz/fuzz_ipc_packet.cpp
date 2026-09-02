//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// libFuzzer entry point for the IPC packet parser, whose input arrives unprompted over a socket from
// another process: every count, offset and stride in it is attacker-controlled.
//
// The bytes go through extract_ipc_packets() to cover the framing too: the length prefix decides how much
// is read, so it is as much part of the attack surface as the payload.
//
// Build:  cmake --preset linux-cpm -B build/fuzz -DHDRVIEW_BUILD_FUZZERS=ON \
//                -DUSE_SANITIZER="Address;Undefined" -DCMAKE_BUILD_TYPE=RelWithDebInfo
// Run:    ./build/fuzz/hdrview_fuzz_ipc_packet corpus/ -max_len=65536

#include "ipc/ipc_packet.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <vector>

namespace
{

struct QuietLogs
{
    QuietLogs() { spdlog::set_level(spdlog::level::off); }
};
QuietLogs quiet_logs;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    std::vector<char> buffer(reinterpret_cast<const char *>(data), reinterpret_cast<const char *>(data) + size);

    try
    {
        extract_ipc_packets(
            buffer,
            [](const IpcPacket &packet)
            {
                // read each packet as whatever it claims to be; reading it as the wrong type
                // throws, which is the parser working
                try
                {
                    switch (packet.type())
                    {
                    case IpcPacketType::OpenImage:
                    case IpcPacketType::OpenImageV2: (void)packet.as_open_image(); break;
                    case IpcPacketType::ReloadImage: (void)packet.as_reload_image(); break;
                    case IpcPacketType::CloseImage: (void)packet.as_close_image(); break;
                    case IpcPacketType::CreateImage: (void)packet.as_create_image(); break;
                    case IpcPacketType::UpdateImage:
                    case IpcPacketType::UpdateImageV2:
                    case IpcPacketType::UpdateImageV3:
                    {
                        const auto update = packet.as_update_image();
                        // touch every sample the parser said was addressable: an offset or
                        // stride that passed validation but does not land inside the payload
                        // shows up only when the addressing is used
                        const int64_t  n_pixels = int64_t(update.bounds.size().x) * update.bounds.size().y;
                        volatile float sink     = 0.f;
                        for (int c = 0; c < update.num_channels(); ++c)
                            for (int64_t px = 0; px < n_pixels; ++px)
                                sink = update.data[size_t(update.channel_offsets[c] + px * update.channel_strides[c])];
                        (void)sink;
                        break;
                    }
                    case IpcPacketType::VectorGraphics:
                    {
                        // the command stream carries no per-command length, each type implying its own, so
                        // a wrong entry in that table walks the parser into the next command's bytes;
                        // reading every command's arguments back makes such a slip observable
                        const auto     vg   = packet.as_vector_graphics();
                        volatile float sink = 0.f;
                        for (const auto &cmd : vg.commands)
                        {
                            for (float v : cmd.data) sink = v;
                            sink = float(cmd.text.size());
                        }
                        (void)sink;
                        break;
                    }
                    default: break; // an unknown or unsupported type is not a parser bug
                    }
                }
                catch (const std::exception &)
                {
                    // refusing a malformed packet is the correct behavior, not a finding
                }
            });
    }
    catch (const std::exception &)
    {
        // likewise for a stream whose framing is unusable
    }

    return 0;
}
