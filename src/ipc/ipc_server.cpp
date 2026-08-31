//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "ipc/ipc_server.h"

#include "common.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cerrno>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t                      = SOCKET;
static constexpr socket_t k_no_sock = INVALID_SOCKET;
#define poll_sockets WSAPoll
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t                      = int;
static constexpr socket_t k_no_sock = -1;
#define poll_sockets poll
#endif

namespace
{

//! How long the receive thread blocks in poll() before rechecking whether it has been asked to stop.
/*!
    Bounds how long stop() takes; nothing else waits on it, since poll() returns as soon as a packet or a
    connection arrives.
*/
constexpr int k_poll_timeout_ms = 100;

//! Largest amount read from one connection per poll, so one busy client cannot starve the others.
constexpr size_t k_recv_chunk = 1 << 16;

std::string socket_error_string()
{
#if defined(_WIN32)
    return fmt::format("winsock error {}", WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

void close_socket(socket_t s)
{
#if defined(_WIN32)
    closesocket(s);
#else
    ::close(s);
#endif
}

bool set_non_blocking(socket_t s)
{
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    return flags != -1 && fcntl(s, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

//! True when a non-blocking call failed only because there is nothing to do yet.
bool would_block()
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

//! Winsock needs initializing once per process; everywhere else this is nothing.
bool init_sockets()
{
#if defined(_WIN32)
    static const bool ok = []
    {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
#else
    return true;
#endif
}

//! One connected client, and the bytes of it that have arrived so far.
struct Connection
{
    socket_t          socket = k_no_sock;
    std::vector<char> buffer; //!< a recv() returns whatever has arrived, which is rarely a whole packet
};

} // namespace

IpcServer::~IpcServer() { stop(); }

bool IpcServer::start(uint16_t port, PacketHandler on_packet)
{
    stop();

    auto fail = [this](std::string message)
    {
        spdlog::warn("Could not listen for image updates: {}", message);
        std::lock_guard lock{m_mutex};
        m_last_error = std::move(message);
        return false;
    };

    if (!init_sockets())
        return fail("could not initialize networking");

    const socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == k_no_sock)
        return fail(fmt::format("could not create a socket ({})", socket_error_string()));

    // Closed on every way out of here until the receive thread takes ownership of it at the end.
    auto listener_guard = ScopeGuard{[listener] { close_socket(listener); }};

    // Without this, the port stays unbindable for a minute or two after a previous run's connections
    // finish closing, so restarting HDRView would fail for no reason the user could act on.
    const int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only; see the class comment

    if (bind(listener, (const sockaddr *)&address, sizeof(address)) != 0)
        return fail(fmt::format("port {} is unavailable ({}). Another viewer may already be listening on it.", port,
                                socket_error_string()));

    if (listen(listener, 8) != 0 || !set_non_blocking(listener))
        return fail(fmt::format("could not listen on port {} ({})", port, socket_error_string()));

    // Port 0 asks the OS to choose a free one, so the port actually bound is read back rather than assumed.
    // Only a test has reason to do that, but reporting the real port is right either way.
    sockaddr_in bound{};
    socklen_t   bound_len = sizeof(bound);
    if (getsockname(listener, (sockaddr *)&bound, &bound_len) == 0)
        port = ntohs(bound.sin_port);

    listener_guard.disarm(); // from here on the socket belongs to this object, and stop() closes it
    m_listen_socket = int64_t(listener);
    m_port          = port;
    m_on_packet     = std::move(on_packet);
    m_stopping      = false;
    m_listening     = true;

    // The readout counts one listening session, not the lifetime of the app.
    m_packets_received = 0;
    m_bytes_received   = 0;
    m_last_packet_ns   = 0;

    {
        std::lock_guard lock{m_mutex};
        m_last_error.clear();
    }

    m_thread = std::thread{[this] { run(); }};

    spdlog::info("Listening for image updates on 127.0.0.1:{}.", port);
    return true;
}

void IpcServer::stop()
{
    if (!m_listening && !m_thread.joinable())
        return;

    m_stopping = true;
    if (m_thread.joinable())
        m_thread.join();

    if (m_listen_socket != -1)
    {
        close_socket(socket_t(m_listen_socket));
        m_listen_socket = -1;
    }

    m_listening = false;
    m_on_packet = nullptr;

    {
        std::lock_guard lock{m_mutex};
        m_num_connections = 0;
    }

    if (m_port)
        spdlog::info("Stopped listening for image updates on port {}.", m_port);
    m_port = 0;
}

size_t IpcServer::num_connections() const
{
    std::lock_guard lock{m_mutex};
    return m_num_connections;
}

IpcActivity IpcServer::activity() const
{
    IpcActivity a;
    a.packets = m_packets_received.load();
    a.bytes   = m_bytes_received.load();

    if (const int64_t last = m_last_packet_ns.load(); last != 0)
    {
        const int64_t now    = std::chrono::steady_clock::now().time_since_epoch().count();
        a.seconds_since_last = double(now - last) * 1e-9;
    }
    return a;
}

std::string IpcServer::last_error() const
{
    std::lock_guard lock{m_mutex};
    return m_last_error;
}

void IpcServer::run()
{
    const socket_t          listener = socket_t(m_listen_socket);
    std::vector<Connection> connections;

    auto drop = [&](size_t i, const char *why)
    {
        spdlog::debug("Image update client disconnected: {}", why);
        close_socket(connections[i].socket);
        connections.erase(connections.begin() + i);

        std::lock_guard lock{m_mutex};
        m_num_connections = connections.size();
    };

    std::vector<char> chunk(k_recv_chunk);

    while (!m_stopping)
    {
        std::vector<pollfd> fds;
        fds.reserve(connections.size() + 1);
        fds.push_back(pollfd{listener, POLLIN, 0});
        for (const auto &c : connections) fds.push_back(pollfd{c.socket, POLLIN, 0});

        const int ready = poll_sockets(fds.data(), (unsigned)fds.size(), k_poll_timeout_ms);
        if (ready < 0)
        {
            if (would_block())
                continue;
            spdlog::warn("Gave up listening for image updates: {}", socket_error_string());
            break;
        }
        if (ready == 0)
            continue;

        // Existing connections first, then new ones, so that a connection accepted this round is not
        // indexed against the fds array it does not appear in.
        for (size_t i = connections.size(); i-- > 0;)
        {
            const short events = fds[i + 1].revents;
            if (!events)
                continue;

            if (events & (POLLHUP | POLLERR | POLLNVAL))
            {
                drop(i, "connection closed");
                continue;
            }

            const auto received = recv(connections[i].socket, chunk.data(), (int)chunk.size(), 0);
            if (received == 0)
            {
                drop(i, "end of stream");
                continue;
            }
            if (received < 0)
            {
                if (!would_block())
                    drop(i, "receive failed");
                continue;
            }

            auto &buffer = connections[i].buffer;
            buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + received);
            m_bytes_received += uint64_t(received);

            try
            {
                extract_ipc_packets(buffer,
                                    [this](const IpcPacket &packet)
                                    {
                                        ++m_packets_received;
                                        m_last_packet_ns = std::chrono::steady_clock::now().time_since_epoch().count();

                                        if (m_on_packet)
                                            m_on_packet(packet);
                                    });
            }
            catch (const std::exception &e)
            {
                // The stream carries no packet boundary to resynchronize to, so a client that sends
                // something unreadable has to go rather than have the rest of its bytes guessed at.
                spdlog::warn("Dropping image update client: {}", e.what());
                drop(i, "malformed packet");
            }
        }

        if (fds[0].revents & POLLIN)
        {
            while (true)
            {
                const socket_t client = accept(listener, nullptr, nullptr);
                if (client == k_no_sock)
                    break; // nothing more waiting, or the accept failed; either way, try again next poll

                if (!set_non_blocking(client))
                {
                    close_socket(client);
                    continue;
                }

                // Tiles arrive as small writes that matter immediately; waiting to coalesce them only adds
                // latency to what is meant to be a live view.
                const int no_delay = 1;
                setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char *)&no_delay, sizeof(no_delay));

                connections.push_back(Connection{client, {}});
                spdlog::debug("Image update client connected.");

                std::lock_guard lock{m_mutex};
                m_num_connections = connections.size();
            }
        }
    }

    for (auto &c : connections) close_socket(c.socket);
}
