//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// Exercises the listener over a real loopback socket, covering what the codec tests cannot: bytes split
// arbitrarily by the network reassembling into the packets that were sent, and crossing threads intact.

#include <doctest/doctest.h>

#include "ipc/ipc_server.h"
#include "ipc_test_helpers.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#if defined(_WIN32)
// winsock2.h drags in windows.h, whose min/max macros would eat the std::min below
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using test_socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using test_socket_t = int;
#endif

namespace
{

/// Connects to a listener on loopback, as a renderer would.
class TestClient
{
public:
    explicit TestClient(uint16_t port)
    {
        m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(m_socket >= 0);

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(connect(m_socket, (const sockaddr *)&address, sizeof(address)) == 0);
    }

    ~TestClient()
    {
#if defined(_WIN32)
        closesocket(m_socket);
#else
        ::close(m_socket);
#endif
    }

    void send_all(const std::vector<char> &bytes, size_t chunk = SIZE_MAX)
    {
        size_t sent = 0;
        while (sent < bytes.size())
        {
            const size_t n       = std::min(chunk, bytes.size() - sent);
            const auto   written = send(m_socket, bytes.data() + sent, (int)n, 0);
            REQUIRE(written > 0);
            sent += size_t(written);
        }
    }

private:
    test_socket_t m_socket;
};

/// Collects packets as they arrive on the server's thread, and lets a test wait for a count of them.
class Collector
{
public:
    void add(std::string name)
    {
        {
            std::lock_guard lock{m_mutex};
            m_names.push_back(std::move(name));
        }
        m_arrived.notify_all();
    }

    /// Wait for at least `count` packets, or give up. Returns what arrived either way.
    std::vector<std::string> wait_for(size_t count, std::chrono::milliseconds timeout = std::chrono::seconds{5})
    {
        std::unique_lock lock{m_mutex};
        m_arrived.wait_for(lock, timeout, [&] { return m_names.size() >= count; });
        return m_names;
    }

private:
    std::mutex               m_mutex;
    std::condition_variable  m_arrived;
    std::vector<std::string> m_names;
};

/// Concatenated bytes of several CloseImage packets, whose only payload is a name we can check.
std::vector<char> stream_of(const std::vector<std::string> &names)
{
    std::vector<char> bytes;
    for (const auto &n : names)
    {
        auto packet = ipc_test::close_image(n);
        bytes.insert(bytes.end(), packet.bytes().begin(), packet.bytes().end());
    }
    return bytes;
}

} // namespace

TEST_CASE("The listener receives what a client sends over loopback")
{
    Collector collector;
    IpcServer server;

    // port 0 lets the OS pick a free one, so this never fights a real tev on 14158
    REQUIRE(server.start(0, [&](const IpcPacket &p) { collector.add(p.as_close_image().name); }));
    REQUIRE(server.is_listening());
    REQUIRE(server.port() != 0);

    const std::vector<std::string> names{"alpha", "beta", "gamma"};

    SUBCASE("sent in one write")
    {
        TestClient client{server.port()};
        client.send_all(stream_of(names));
        CHECK(collector.wait_for(names.size()) == names);
    }

    SUBCASE("dribbled a byte at a time, so no read lands on a packet boundary")
    {
        TestClient client{server.port()};
        client.send_all(stream_of(names), 1);
        CHECK(collector.wait_for(names.size()) == names);
    }

    SUBCASE("from two clients at once")
    {
        TestClient a{server.port()};
        TestClient b{server.port()};
        a.send_all(stream_of({"alpha"}));
        b.send_all(stream_of({"beta"}));

        auto got = collector.wait_for(2);
        REQUIRE(got.size() == 2);
        // two connections are serviced in whichever order they become readable, so only the set is defined
        std::sort(got.begin(), got.end());
        CHECK(got == std::vector<std::string>{"alpha", "beta"});
    }

    server.stop();
    CHECK_FALSE(server.is_listening());
}

TEST_CASE("The listener counts what it has received, for the activity readout")
{
    Collector collector;
    IpcServer server;
    REQUIRE(server.start(0, [&](const IpcPacket &p) { collector.add(p.as_close_image().name); }));

    // nothing has arrived yet, so there is no "time since the last packet" to report
    CHECK(server.activity().packets == 0);
    CHECK(server.activity().bytes == 0);
    CHECK(server.activity().seconds_since_last < 0.0);

    const std::vector<std::string> names{"a", "bb", "ccc"};
    const auto                     stream = stream_of(names);

    {
        TestClient client{server.port()};
        client.send_all(stream);
        REQUIRE(collector.wait_for(names.size()).size() == names.size());
    }

    const auto activity = server.activity();
    CHECK(activity.packets == names.size());
    // bytes are counted off the wire, so they include each packet's length prefix and type byte
    CHECK(activity.bytes == stream.size());
    CHECK(activity.seconds_since_last >= 0.0);

    // counting covers one listening session: restarting begins again from nothing
    server.stop();
    REQUIRE(server.start(0, [](const IpcPacket &) {}));
    CHECK(server.activity().packets == 0);
    CHECK(server.activity().bytes == 0);
    CHECK(server.activity().seconds_since_last < 0.0);
    server.stop();
}

TEST_CASE("A tile survives the trip over a socket")
{
    // the same payload the codec tests round-trip in memory, now through a real connection, where a framing
    // mistake shows up as silently wrong pixels instead of a parse error
    const Box2i                    bounds{int2{2, 3}, int2{6, 7}};
    const std::vector<std::string> channels{"R", "G", "B"};
    std::vector<float>             data(size_t(bounds.size().x) * bounds.size().y * 3);
    for (size_t i = 0; i < data.size(); ++i) data[i] = float(i) * 0.25f;

    std::mutex                    mutex;
    std::condition_variable       arrived;
    std::optional<IpcUpdateImage> received;

    IpcServer server;
    REQUIRE(server.start(0,
                         [&](const IpcPacket &p)
                         {
                             std::lock_guard lock{mutex};
                             received = p.as_update_image();
                             arrived.notify_all();
                         }));

    {
        TestClient client{server.port()};
        auto       packet = ipc_test::update_image("render", false, channels, {0, 1, 2}, {3, 3, 3}, bounds, data);
        // in two writes, so the receiver has to hold a partial packet across reads
        std::vector<char> bytes = packet.bytes();
        client.send_all({bytes.begin(), bytes.begin() + bytes.size() / 2});
        client.send_all({bytes.begin() + bytes.size() / 2, bytes.end()});

        std::unique_lock lock{mutex};
        arrived.wait_for(lock, std::chrono::seconds{5}, [&] { return received.has_value(); });
    }

    REQUIRE(received.has_value());
    CHECK(received->name == "render");
    CHECK(received->bounds == bounds);
    CHECK(received->channel_names == channels);
    CHECK(received->data == data);

    server.stop();
}
