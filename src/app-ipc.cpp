//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// Turning packets received from a renderer into changes to the loaded images. The socket and the wire
// format live in src/ipc/; everything here is HDRViewApp's side of them.

#include "app.h"

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

void HDRViewApp::draw_ipc_gui()
{
    ImGui::SeparatorText("Live updates");

    const bool listening = m_ipc_server.is_listening();

    bool toggle = listening;
    if (ImGui::Checkbox("Listen for image updates on port:", &toggle))
    {
        if (toggle)
            start_ipc_listening(m_ipc_port);
        else
            stop_ipc_listening();
    }
    ImGui::Tooltip("Accept images pushed in by a renderer while it works, over the protocol tev uses. "
                   "Nothing outside this machine can connect. Off unless you turn it on.");

    // The port reads as the end of the checkbox's sentence, so it carries no label of its own.
    ImGui::SameLine();
    // It cannot change under a bound socket, and rebinding silently would drop whatever is connected.
    ImGui::BeginDisabled(listening);
    int port = int(m_ipc_port);
    ImGui::SetNextItemWidth(HelloImGui::EmSize(4.f));
    if (ImGui::InputInt("##Port", &port, 0, 0, ImGuiInputTextFlags_CharsDecimal))
        m_ipc_port = uint16_t(std::clamp(port, 1, 65535));
    ImGui::EndDisabled();
    ImGui::Tooltip("14158 is what tev listens on, so clients written for it need no changes. Change it to "
                   "run HDRView and tev side by side.");

    // Wrapped, not clipped: this panel is usually docked narrow, and both the address and the client count
    // are the point of the line.
    if (listening)
    {
        const size_t clients = m_ipc_server.num_connections();
        ImGui::TextWrapped("Listening on 127.0.0.1:%d \xe2\x80\x93 %s connected.", int(m_ipc_server.port()),
                           clients == 1 ? "1 client" : fmt::format("{} clients", clients).c_str());
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
