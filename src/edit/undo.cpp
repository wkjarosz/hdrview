//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/undo.h"

#include "image.h"

ChannelRectUndo::ChannelRectUndo(const Image &img, std::vector<int> channels, const Box2i &bounds, std::string name) :
    m_name(std::move(name)), m_bounds(bounds), m_channels(std::move(channels))
{
    // Boxes here are half-open, so clipping to nothing collapses a dimension to zero extent rather than
    // making max < min; the extent is what says whether anything is left.
    m_bounds.intersect(img.data_window);
    const int2 extent = m_bounds.size();
    if (extent.x <= 0 || extent.y <= 0)
        return;

    const int2 offset = m_bounds.min - img.data_window.min;

    m_pixels.reserve(m_channels.size());
    for (int c : m_channels)
    {
        const Channel &channel = img.channels[size_t(c)];

        Array2Df saved{extent};
        for (int y = 0; y < extent.y; ++y)
            for (int x = 0; x < extent.x; ++x) saved(x, y) = channel(offset.x + x, offset.y + y);

        m_pixels.push_back(std::move(saved));
    }
}

void ChannelRectUndo::swap(Image &img)
{
    const int2 extent = m_bounds.size();
    if (extent.x <= 0 || extent.y <= 0)
        return;

    // Channels are indexed from their own top-left corner, which for a non-zero data window is not where
    // the image's coordinates start.
    const int2  offset = m_bounds.min - img.data_window.min;
    const Box2i local{offset, offset + extent};

    for (size_t i = 0; i < m_channels.size(); ++i)
    {
        Channel &channel = img.channels[size_t(m_channels[i])];

        // Read what is there now before overwriting it, so this entry comes out holding the opposite
        // direction's pixels.
        Array2Df displaced{extent};
        for (int y = 0; y < extent.y; ++y)
            for (int x = 0; x < extent.x; ++x) displaced(x, y) = channel(offset.x + x, offset.y + y);

        channel.upload_tile(local, m_pixels[i].data());
        m_pixels[i] = std::move(displaced);
    }

    // Statistics and histograms are cached against this; see Image::content_version.
    ++img.content_version;
}

size_t ChannelRectUndo::memory_usage() const
{
    size_t total = 0;
    for (const auto &p : m_pixels) total += size_t(p.num_elements()) * sizeof(float);
    return total;
}

void CommandHistory::add(UndoPtr entry)
{
    // Anything that had been undone is unreachable once a new edit lands on top of it.
    const bool discarded_saved_state = m_saved_state > m_current_state;
    m_entries.resize(size_t(m_current_state));

    m_entries.push_back(std::move(entry));
    ++m_current_state;

    // The state the image last agreed with its file at is gone, so it can no longer be returned to.
    if (discarded_saved_state)
        m_saved_state = -1;

    trim();
}

bool CommandHistory::undo(Image &img)
{
    if (!has_undo())
        return false;

    m_entries[size_t(--m_current_state)]->undo(img);
    return true;
}

bool CommandHistory::redo(Image &img)
{
    if (!has_redo())
        return false;

    m_entries[size_t(m_current_state++)]->redo(img);
    return true;
}

size_t CommandHistory::memory_usage() const
{
    size_t total = 0;
    for (const auto &e : m_entries) total += e->memory_usage();
    return total;
}

void CommandHistory::trim()
{
    // Only entries behind the cursor are droppable: those ahead of it are what redo would apply, and an
    // entry the cursor sits on is the next undo. Dropping stops at the cursor even if that leaves the
    // history over budget, since the alternative is discarding an edit the user can still reach.
    size_t total   = memory_usage();
    int    dropped = 0;
    while (total > k_max_memory && dropped < m_current_state - 1)
    {
        total -= m_entries.front()->memory_usage();
        m_entries.erase(m_entries.begin());
        ++dropped;
    }

    if (dropped == 0)
        return;

    m_current_state -= dropped;
    // A save point among the dropped entries can never be reached again, so the image counts as modified
    // from here on rather than pretending some later state matches the file.
    m_saved_state = m_saved_state >= dropped ? m_saved_state - dropped : -1;
}
