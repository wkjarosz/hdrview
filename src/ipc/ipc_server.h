//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "ipc/ipc_packet.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//! The port tev listens on, and the one every client that speaks this protocol reaches for by default.
inline constexpr uint16_t k_default_ipc_port = 14158;

/*!
    Accepts connections from renderers and hands their packets to a callback.

    Listens on the loopback interface only. Extending that to other interfaces -- so a render node could
    push to a workstation -- means taking a bind address here and deciding what, if anything, authorizes a
    remote sender; the protocol itself has no notion of who is connecting, and a listener reachable off the
    machine would let anything on the network create and overwrite images. Nothing about the rest of this
    class assumes loopback, so that is a decision to make rather than a rewrite to do.

    Not started by default: a viewer that opens a listening socket on every launch is a standing exposure
    and, on Windows and macOS, a firewall prompt for users who will never stream a render.
*/
class IpcServer
{
public:
    /*!
        Invoked for each packet, on the server's own receive thread.

        Everything an image is made of belongs to the main thread, so a handler must not touch it directly;
        it should decode what it needs and hand the work to HDRViewApp::post_to_main_thread().
    */
    using PacketHandler = std::function<void(const IpcPacket &)>;

    IpcServer() = default;
    ~IpcServer();

    IpcServer(const IpcServer &)            = delete;
    IpcServer &operator=(const IpcServer &) = delete;

    /*!
        Begin listening on 127.0.0.1:\p port.

        \return true once the socket is bound and the receive thread is running. Binding fails when
                something else already holds the port -- most likely tev itself, or another HDRView -- which
                is reported and is not an error worth stopping the app for.
    */
    bool start(uint16_t port, PacketHandler on_packet);

    //! Stop listening and close every connection. Safe to call when not listening, and called by ~IpcServer.
    void stop();

    bool     is_listening() const { return m_listening; }
    uint16_t port() const { return m_port; }

    //! How many clients are connected right now. For the GUI to show what is streaming.
    size_t num_connections() const;

    //! The most recent reason start() failed, for showing in the GUI next to the toggle that failed.
    std::string last_error() const;

private:
    void run(); //!< the receive thread's loop

    std::atomic<bool> m_listening{false};
    std::atomic<bool> m_stopping{false};
    uint16_t          m_port = 0;

    // -1 when not listening; an OS socket handle otherwise. Kept as an int so this header needs no
    // platform socket headers -- Windows' SOCKET is a pointer-sized handle, but every value the API
    // actually returns fits, and the .cpp does the conversion in one place.
    int64_t m_listen_socket = -1;

    PacketHandler m_on_packet;
    std::thread   m_thread;

    mutable std::mutex m_mutex; //!< guards m_num_connections and m_last_error
    size_t             m_num_connections = 0;
    std::string        m_last_error;
};
