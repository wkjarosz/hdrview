//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "box.h"
#include "fwd.h"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/*!
    tev's remote-control protocol: how a renderer hands a viewer its pixels while it is still producing them.

    One operation per packet, framed as

        [uint32 total_length_in_bytes][uint8 type][type-specific payload]

    with every integer little-endian and every string NUL-terminated. The length counts its own four bytes.
    The layout matches tev's byte for byte, so the clients that already exist for it (the C++, Python and
    Rust `tevclient` libraries, pbrt-v4's display server) work against HDRView unchanged.

    An independent reimplementation of the format tev documents, rather than a port of its code.

    Everything here treats its input as hostile: a packet arrives over a socket from a process that is not
    this one, so every count, offset and stride is checked against the bytes actually present rather than
    trusted to describe them.
*/

//! Which operation a packet carries. The numbering is the protocol's, not ours.
/*!
    The versioned duplicates are tev's backwards compatibility: V2 added multiple channels per packet and V3
    added per-channel offset/stride, so a client can hand over an interleaved tile without deinterleaving it
    first. A newer type is a superset of the older one it shadows, and all of them are still sent in
    practice, so all of them are read.
*/
enum class IpcPacketType : uint8_t
{
    OpenImage      = 0,
    ReloadImage    = 1,
    CloseImage     = 2,
    UpdateImage    = 3,
    CreateImage    = 4,
    UpdateImageV2  = 5, //!< adds multiple channels per packet
    UpdateImageV3  = 6, //!< adds per-channel offset/stride into one shared payload
    OpenImageV2    = 7, //!< separates the image name from the channel selector
    VectorGraphics = 8, //!< overlay drawing commands; recognized but not supported
};

//! Largest packet we are willing to hold, since the sender chooses the length.
/*!
    A tile is pixels times channels times four bytes, so a whole 8K RGBA frame in one packet is around
    530 MB and legitimate. The cap exists so that a bad or hostile length field cannot ask for an
    unbounded allocation, not because any real packet approaches it.
*/
inline constexpr uint32_t k_max_ipc_packet_size = 1u << 30; // 1 GiB

//! Most channels one packet may name, well past any real layer count and short of an allocation attack.
inline constexpr int32_t k_max_ipc_channels = 4096;

struct IpcOpenImage
{
    std::string path;             //!< Path on the machine HDRView is running on
    std::string channel_selector; //!< Empty for OpenImage v1, which had no separate selector
    bool        grab_focus = false;
};

struct IpcReloadImage
{
    std::string name;
    bool        grab_focus = false;
};

struct IpcCloseImage
{
    std::string name;
};

struct IpcCreateImage
{
    std::string              name;
    bool                     grab_focus = false;
    int2                     size{0};
    std::vector<std::string> channel_names;
};

//! A rectangle of pixels for channels that already exist, as a renderer finishes them.
/*!
    The samples stay in the one interleaved block the sender wrote, addressed per channel as
    `data[offset[c] + px * stride[c]]` for `px` running row-major over `bounds`. Keeping them that way
    rather than deinterleaving into a buffer per channel costs nothing to read -- Channel::upload_tile()
    takes a stride for exactly this -- and avoids a second copy of every tile.
*/
struct IpcUpdateImage
{
    std::string              name;
    bool                     grab_focus = false;
    std::vector<std::string> channel_names;
    std::vector<int64_t>     channel_offsets; //!< index into `data` of each channel's first sample
    std::vector<int64_t>     channel_strides; //!< distance in samples between consecutive pixels
    Box2i                    bounds;          //!< half-open, in the image's pixel coordinates
    std::vector<float>       data;

    int num_channels() const { return int(channel_names.size()); }

    //! Distance in samples between consecutive rows of channel `c`; pairs with channel_strides[c].
    int64_t row_stride(int c) const { return int64_t(bounds.size().x) * channel_strides[c]; }
};

//! A framed packet: the bytes as they travel, plus typed readers for the payload.
class IpcPacket
{
public:
    IpcPacket() = default;

    /*!
        Take ownership of one framed packet, starting at its length prefix.

        Checks only the framing -- that a length is present, that it is sane, and that it matches the bytes
        handed over. What the payload says is checked by whichever as_*() reads it, since only that knows
        which fields should be there.

        \throws std::runtime_error if the framing is not intact
    */
    IpcPacket(const char *data, size_t length);

    IpcPacketType type() const;

    const std::vector<char> &bytes() const { return m_bytes; }
    size_t                   size() const { return m_bytes.size(); }

    //@{ \name Payload readers. Each throws std::runtime_error if the packet is not that type, or if its
    //! contents do not describe the bytes present.
    IpcOpenImage   as_open_image() const;
    IpcReloadImage as_reload_image() const;
    IpcCloseImage  as_close_image() const;
    IpcCreateImage as_create_image() const;
    IpcUpdateImage as_update_image() const;
    //@}

    //@{ \name Builders, which produce exactly what tev's own client would send.
    static IpcPacket open_image(std::string_view path, std::string_view channel_selector, bool grab_focus);
    static IpcPacket reload_image(std::string_view name, bool grab_focus);
    static IpcPacket close_image(std::string_view name);
    static IpcPacket create_image(std::string_view name, bool grab_focus, int2 size,
                                  const std::vector<std::string> &channel_names);
    //! Builds a V3 update. `offsets`/`strides` address `data` as IpcUpdateImage documents.
    static IpcPacket update_image(std::string_view name, bool grab_focus, const std::vector<std::string> &channel_names,
                                  const std::vector<int64_t> &offsets, const std::vector<int64_t> &strides,
                                  const Box2i &bounds, const std::vector<float> &data);
    //@}

private:
    std::vector<char> m_bytes;
};

/*!
    Pull whole packets off a stream of received bytes.

    A TCP recv() returns whatever has arrived, which may be half a packet or several, so a connection has to
    accumulate until a length prefix and the bytes it promises are both present. Consumed bytes are dropped
    from the front and any partial remainder is kept for the next call.

    \param [in,out] buffer  Received bytes; whatever is left over on return is the start of the next packet
    \param [] on_packet     Invoked for each complete packet, in arrival order
    \throws std::runtime_error if the stream is unusable (a length no packet could have), which a caller
            should treat as grounds to drop the connection rather than resynchronize -- there is no framing
            marker to resynchronize to.
*/
void extract_ipc_packets(std::vector<char> &buffer, const std::function<void(const IpcPacket &)> &on_packet);
