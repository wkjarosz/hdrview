//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// libFuzzer entry point for the IPC packet parser. Unlike the image loaders, whose input is a file the user
// chose to open, this input arrives unprompted over a socket from another process -- so every count, offset
// and stride in it is attacker-controlled, and the parser is the only thing standing between them and the
// image model.
//
// Feeding the bytes through extract_ipc_packets() rather than straight to IpcPacket covers the framing too:
// the length prefix decides how much is read, so it is as much part of the attack surface as the payload.
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
                // Read each packet as whatever it claims to be. Reading it as the wrong type
                // throws, which is the parser working, so the type is dispatched on rather
                // than every reader being tried.
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
                        // Touch every sample the parser said was addressable: an offset or
                        // stride that passed validation but does not actually land inside
                        // the payload is exactly the bug worth finding, and it only shows
                        // up when the addressing is used.
                        const int64_t  n_pixels = int64_t(update.bounds.size().x) * update.bounds.size().y;
                        volatile float sink     = 0.f;
                        for (int c = 0; c < update.num_channels(); ++c)
                            for (int64_t px = 0; px < n_pixels; ++px)
                                sink = update.data[size_t(update.channel_offsets[c] + px * update.channel_strides[c])];
                        (void)sink;
                        break;
                    }
                    default: break; // an unknown or unsupported type is not a parser bug
                    }
                }
                catch (const std::exception &)
                {
                    // Refusing a malformed packet is the correct behavior, not a finding.
                }
            });
    }
    catch (const std::exception &)
    {
        // Likewise for a stream whose framing is unusable.
    }

    return 0;
}
