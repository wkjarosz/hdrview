//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "ipc/ipc_packet.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//! The port tev listens on, and the one every client that speaks this protocol reaches for by default.
inline constexpr uint16_t k_default_ipc_port = 14158;

//! Running totals since start(). The protocol has no notion of progress, so this is all there is to show.
struct IpcActivity
{
    uint64_t packets = 0; //!< Complete packets read since the listener started
    uint64_t bytes   = 0; //!< Bytes read off the wire, which includes framing
    //! Seconds since the last packet, or negative when none has arrived yet.
    double seconds_since_last = -1.0;
};

//! Accepts connections from renderers and hands their packets to a callback.
/*!
    Listens on the loopback interface only, and not until start() is called: the protocol has no notion of
    who is connecting, so a listener reachable off the machine would let anything on the network create and
    overwrite images.
*/
class IpcServer
{
public:
    //! Invoked for each packet, on the server's own receive thread. Images belong to the main thread, so a
    //! handler must decode what it needs and hand the work to HDRViewApp::post_to_main_thread().
    using PacketHandler = std::function<void(const IpcPacket &)>;

    IpcServer() = default;
    ~IpcServer();

    IpcServer(const IpcServer &)            = delete;
    IpcServer &operator=(const IpcServer &) = delete;

    /*!
        Begin listening on 127.0.0.1:\p port.

        \return true once the socket is bound and the receive thread is running. Binding fails when something
                else already holds the port (tev itself, or another HDRView); see last_error().
    */
    bool start(uint16_t port, PacketHandler on_packet);

    //! Stop listening and close every connection. Safe to call when not listening, and called by ~IpcServer.
    void stop();

    bool     is_listening() const { return m_listening; }
    uint16_t port() const { return m_port; }

    //! How many clients are connected right now.
    size_t num_connections() const;

    //! Running totals of what has been received. Safe to call from any thread.
    IpcActivity activity() const;

    //! The most recent reason start() failed.
    std::string last_error() const;

private:
    void run(); //!< the receive thread's loop

    std::atomic<bool> m_listening{false};
    std::atomic<bool> m_stopping{false};
    uint16_t          m_port = 0;

    // Written by the receive thread and read by the GUI every frame. The timestamp is a steady_clock count
    // because time_point is not trivially copyable enough to be atomic everywhere.
    std::atomic<uint64_t> m_packets_received{0};
    std::atomic<uint64_t> m_bytes_received{0};
    std::atomic<int64_t>  m_last_packet_ns{0}; //!< 0 until the first packet arrives

    // -1 when not listening; an OS socket handle otherwise. An int64_t so this header needs no platform
    // socket headers: Windows' SOCKET is a pointer-sized handle, but every value the API returns fits.
    int64_t m_listen_socket = -1;

    PacketHandler m_on_packet;
    std::thread   m_thread;

    mutable std::mutex m_mutex; //!< guards m_num_connections and m_last_error
    size_t             m_num_connections = 0;
    std::string        m_last_error;
};
