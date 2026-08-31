//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// Turning packets received from a renderer into changes to the loaded images. The socket and the wire
// format live in src/ipc/; everything here is HDRViewApp's side of them.

#include "app.h"

#include "common.h"
#include "image.h"
#include "imageio/image_loader.h"
#include "imgui_ext.h"

#include <hello_imgui/dpi_aware.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>

#if HDRVIEW_ENABLE_IPC

int HDRViewApp::image_index_by_name(std::string_view name) const
{
    for (int i = 0; i < num_images(); ++i)
        if (m_images[size_t(i)]->filename == name)
            return i;
    return -1;
}

bool HDRViewApp::start_ipc_listening(uint16_t port)
{
    // Decode on the receive thread and hand the main thread only the result: parsing a tile means copying
    // its samples, which is the bulk of the work and has no reason to happen on the frame.
    return m_ipc_server.start(
        port,
        [this](const IpcPacket &packet)
        {
            try
            {
                switch (packet.type())
                {
                case IpcPacketType::OpenImage:
                case IpcPacketType::OpenImageV2:
                    post_to_main_thread([this, info = packet.as_open_image()] { apply_ipc_open(info); });
                    break;

                case IpcPacketType::ReloadImage:
                    post_to_main_thread([this, info = packet.as_reload_image()] { apply_ipc_reload(info); });
                    break;

                case IpcPacketType::CloseImage:
                    post_to_main_thread([this, info = packet.as_close_image()] { apply_ipc_close(info); });
                    break;

                case IpcPacketType::CreateImage:
                    post_to_main_thread([this, info = packet.as_create_image()] { apply_ipc_create(info); });
                    break;

                case IpcPacketType::UpdateImage:
                case IpcPacketType::UpdateImageV2:
                case IpcPacketType::UpdateImageV3:
                    post_to_main_thread([this, info = packet.as_update_image()] { apply_ipc_update(info); });
                    break;

                case IpcPacketType::VectorGraphics:
                    // Overlay drawing, which HDRView has nothing to draw with. Ignoring it costs the sender
                    // nothing: the protocol expects no reply, so a client that draws debug overlays still
                    // streams its pixels normally.
                    spdlog::debug("Ignoring a VectorGraphics packet: HDRView does not draw overlays.");
                    break;

                default: spdlog::warn("Ignoring an IPC packet of unknown type {}.", int(packet.type())); break;
                }
            }
            catch (const std::exception &e)
            {
                // One unreadable packet is not grounds to drop the connection -- the framing was intact, so
                // the stream is still in sync and the next packet may well be fine.
                spdlog::warn("Could not read an IPC packet: {}", e.what());
            }

            // The frame loop idles by waiting on window events, so without a nudge a tile can sit undrawn
            // for as long as the idle timeout. See wake_event_loop().
            wake_event_loop();
        });
}

void HDRViewApp::stop_ipc_listening() { m_ipc_server.stop(); }

void HDRViewApp::set_ipc_listening(bool listen)
{
    // The one place the toggle's meaning lives, so the panel's checkbox and the command palette's entry
    // cannot drift apart. Binding can fail -- the port may be held by tev or another HDRView -- so the
    // request is answered by what the server actually did, not by what was asked for.
    if (listen)
        start_ipc_listening(m_ipc_port);
    else
        stop_ipc_listening();

    m_ipc_listen_requested = m_ipc_server.is_listening();
}

void HDRViewApp::set_ipc_port(uint16_t port)
{
    if (port == m_ipc_port)
        return;

    m_ipc_port = port;

    // Rebind straight away when already listening, so the port shown is the port in use. start() stops the
    // old listener first, so a client connected to it is dropped -- and if the new port turns out to be
    // taken, nothing is listening and the panel says why.
    if (m_ipc_server.is_listening())
        set_ipc_listening(true);
}

void HDRViewApp::IpcRates::update(const IpcActivity &now, double time)
{
    // Long enough that the numbers hold still and can be read, short enough that they follow a renderer
    // starting and stopping.
    static constexpr double k_window = 0.5;

    if (sampled_at == 0.0)
    {
        sampled_at   = time;
        last_packets = now.packets;
        last_bytes   = now.bytes;
        return;
    }

    const double elapsed = time - sampled_at;
    if (elapsed < k_window)
        return;

    packets_per_s = double(now.packets - last_packets) / elapsed;
    bytes_per_s   = double(now.bytes - last_bytes) / elapsed;
    sampled_at    = time;
    last_packets  = now.packets;
    last_bytes    = now.bytes;
}

void HDRViewApp::draw_ipc_gui()
{
    ImGui::SeparatorText("Live updates");

    const bool listening = m_ipc_server.is_listening();

    // Both this and the "Listen for image updates" command go through set_ipc_listening(); the label differs
    // because here the sentence continues into the port field beside it.
    bool toggle = listening;
    if (ImGui::Checkbox("Listen for image updates on port:", &toggle))
        set_ipc_listening(toggle);
    ImGui::Tooltip("Accept images pushed in by a renderer while it works, so a render appears here tile by "
                   "tile. Nothing outside this machine can connect. Off unless you turn it on.");

    // The port reads as the end of the checkbox's sentence, so it carries no label of its own.
    ImGui::SameLine();
    int port = int(m_ipc_port);
    ImGui::SetNextItemWidth(HelloImGui::EmSize(4.f));
    if (ImGui::InputInt("##Port", &port, 0, 0, ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue))
        set_ipc_port(uint16_t(std::clamp(port, 1, 65535)));
    ImGui::Tooltip("14158 is the port renderers connect to by default, so most need no configuring. Change it "
                   "if another viewer already holds that port. Changing it while listening rebinds, dropping "
                   "anything currently connected.");

    // Wrapped, not clipped: this panel is usually docked narrow, and both the address and the client count
    // are the point of the line.
    if (listening)
    {
        const size_t clients = m_ipc_server.num_connections();
        ImGui::TextWrapped("Listening on 127.0.0.1:%d \xe2\x80\x93 %s connected.", int(m_ipc_server.port()),
                           clients == 1 ? "1 client" : fmt::format("{} clients", clients).c_str());

        const auto activity = m_ipc_server.activity();
        m_ipc_rates.update(activity, ImGui::GetTime());

        // "Streaming" is anything still arriving; a renderer that pauses between passes should not make the
        // readout flicker, so the threshold is well above the quarter-second pbrt leaves between updates.
        static constexpr double k_idle_after = 1.5;
        const bool streaming = activity.seconds_since_last >= 0.0 && activity.seconds_since_last < k_idle_after;

        if (clients && streaming)
        {
            // Indeterminate on purpose. Nothing in the protocol says how much is left, and the two ways
            // clients stream -- painting each tile once, or resending the frame at rising sample counts --
            // are indistinguishable from the packets, so any percentage here would be invented.
            // No overlay text: the strip is deliberately thinner than a line of it, and the rates below say
            // what it is doing anyway.
            ImGui::ProgressBar(-1.f * float(ImGui::GetTime()), ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 0.35f), "");
            ImGui::TextWrapped("%s",
                               fmt::format("Receiving {:.1h}/s over {:.0f} updates/s.",
                                           human_readible{size_t(m_ipc_rates.bytes_per_s)}, m_ipc_rates.packets_per_s)
                                   .c_str());
        }
        else if (clients)
            // How long a connected client has been quiet is worth watching -- it separates a renderer
            // between passes from one that has stalled.
            ImGui::TextWrapped("Connected, but nothing received for %.0fs.", activity.seconds_since_last);
        else if (!activity.packets)
            ImGui::TextUnformatted("Waiting for a renderer to connect.");
        // With nobody connected there is nothing left to wait for, so the time since the last update stops
        // being information and just climbs. The totals below say what arrived; that is the whole story.

        if (activity.packets)
            ImGui::TextWrapped("%s", fmt::format("{:.1h} in {} total.", human_readible{size_t(activity.bytes)},
                                                 activity.packets == 1 ? std::string{"1 update"}
                                                                       : fmt::format("{} updates", activity.packets))
                                         .c_str());
    }
    else if (auto error = m_ipc_server.last_error(); !error.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, HelloImGui::EmSize(0.5f)));
    ImGui::SeparatorText("Watched folders");
}

void HDRViewApp::apply_ipc_open(const IpcOpenImage &info)
{
    auto opts             = load_image_options();
    opts.channel_selector = info.channel_selector;
    load_image(info.path, std::nullopt, info.grab_focus, opts);
}

void HDRViewApp::apply_ipc_reload(const IpcReloadImage &info)
{
    const int idx = image_index_by_name(info.name);
    if (!is_valid(idx))
    {
        spdlog::warn("Cannot reload '{}': no such image is open.", info.name);
        return;
    }
    reload_image(m_images[size_t(idx)], info.grab_focus);
}

void HDRViewApp::apply_ipc_close(const IpcCloseImage &info)
{
    const int idx = image_index_by_name(info.name);
    if (!is_valid(idx))
    {
        spdlog::warn("Cannot close '{}': no such image is open.", info.name);
        return;
    }
    close_image(idx);
}

void HDRViewApp::apply_ipc_create(const IpcCreateImage &info)
{
    ImagePtr image;
    try
    {
        image = std::make_shared<Image>(info.size, info.channel_names);

        image->filename   = info.name;
        image->short_name = info.name;
        image->is_live    = true;

        // The samples a renderer sends are its own; nothing here should reinterpret them. Leaving alpha_type
        // at AlphaType_None keeps finalize() from premultiplying, which would otherwise scale every tile by
        // an alpha channel that may not have been sent yet.
        image->finalize();
    }
    catch (const std::exception &e)
    {
        // A name and a channel list chosen by another process, so this is reachable: duplicate channel
        // names, or dimensions past what the GPU can hold, both land here.
        spdlog::error("Could not create '{}' over IPC: {}", info.name, e.what());
        return;
    }

    // Recreating an existing name replaces it in place, which is what a renderer restarting a render means
    // by it -- and matches tev, whose CreateImage is documented to overwrite.
    const int existing = image_index_by_name(info.name);
    if (is_valid(existing))
        m_images[size_t(existing)] = image;
    else
        m_images.push_back(image);

    if (info.grab_focus)
        set_current_image_index(is_valid(existing) ? existing : num_images() - 1, true);

    update_visibility(); // also sets the image textures
    m_request_sort = true;

    spdlog::info("Created live image '{}' ({}x{}, {} channels).", info.name, info.size.x, info.size.y,
                 info.channel_names.size());
}

void HDRViewApp::apply_ipc_update(const IpcUpdateImage &info)
{
    const int idx = image_index_by_name(info.name);
    if (!is_valid(idx))
    {
        spdlog::warn("Cannot update '{}': no such image is open. Send a CreateImage first.", info.name);
        return;
    }

    auto &image = *m_images[size_t(idx)];

    // The packet addresses its samples in the image's pixel coordinates; a channel is indexed from its own
    // top-left corner, which for an image with a non-zero data window is somewhere else.
    const Box2i bounds{info.bounds.min - image.data_window.min, info.bounds.max - image.data_window.min};

    bool any = false;
    for (int c = 0; c < info.num_channels(); ++c)
    {
        const auto &name = info.channel_names[size_t(c)];

        auto found = std::find_if(image.channels.begin(), image.channels.end(),
                                  [&name](const Channel &ch) { return ch.name == name; });
        if (found == image.channels.end())
        {
            spdlog::warn("Cannot update channel '{}' of '{}': it has no such channel.", name, info.name);
            continue;
        }

        found->upload_tile(bounds, info.data.data() + info.channel_offsets[size_t(c)], info.channel_strides[size_t(c)],
                           info.row_stride(c));
        any = true;
    }

    if (!any)
        return;

    // Statistics and histograms are cached against this; see Image::content_version.
    ++image.content_version;

    if (info.grab_focus)
        set_current_image_index(idx, true);
}

#endif // HDRVIEW_ENABLE_IPC
