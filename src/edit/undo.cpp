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
    // boxes here are half-open, so clipping to nothing collapses a dimension to zero extent
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

    // channels are indexed from their own top-left corner, not from the image's origin
    const int2  offset = m_bounds.min - img.data_window.min;
    const Box2i local{offset, offset + extent};

    for (size_t i = 0; i < m_channels.size(); ++i)
    {
        Channel &channel = img.channels[size_t(m_channels[i])];

        // read what is there now before overwriting it, so this entry ends up holding the other direction
        Array2Df displaced{extent};
        for (int y = 0; y < extent.y; ++y)
            for (int x = 0; x < extent.x; ++x) displaced(x, y) = channel(offset.x + x, offset.y + y);

        channel.upload_tile(local, m_pixels[i].data());
        m_pixels[i] = std::move(displaced);
    }

    // statistics and histograms are cached against this; see Image::content_version
    ++img.content_version;
}

size_t ChannelRectUndo::memory_usage() const
{
    size_t total = 0;
    for (const auto &p : m_pixels) total += size_t(p.num_elements()) * sizeof(float);
    return total;
}

ColorMetadataUndo::ColorMetadataUndo(const Image &img, std::vector<int> channels, const Box2i &bounds,
                                     std::string name) :
    m_pixels(img, std::move(channels), bounds, std::move(name)), m_chromaticities(img.chromaticities),
    m_adopted_neutral(img.adopted_neutral), m_RGB_to_XYZ(img.M_RGB_to_XYZ), m_XYZ_to_RGB(img.M_XYZ_to_RGB),
    m_to_sRGB(img.M_to_sRGB), m_luminance_weights(img.luminance_weights), m_adaptation_method(img.adaptation_method),
    m_color_space(img.color_space), m_white_point(img.white_point),
    m_color_profile(img.metadata.value<std::string>("color profile", ""))
{
}

void ColorMetadataUndo::swap(Image &img)
{
    m_pixels.undo(img); // swaps, whichever direction this is

    std::swap(m_chromaticities, img.chromaticities);
    std::swap(m_adopted_neutral, img.adopted_neutral);
    std::swap(m_RGB_to_XYZ, img.M_RGB_to_XYZ);
    std::swap(m_XYZ_to_RGB, img.M_XYZ_to_RGB);
    std::swap(m_to_sRGB, img.M_to_sRGB);
    std::swap(m_luminance_weights, img.luminance_weights);
    std::swap(m_adaptation_method, img.adaptation_method);
    std::swap(m_color_space, img.color_space);
    std::swap(m_white_point, img.white_point);

    // an image whose profile was never named has no entry, not an empty one, so the panel falls back
    std::string current = img.metadata.value<std::string>("color profile", "");
    if (m_color_profile.empty())
        img.metadata.erase("color profile");
    else
        img.metadata["color profile"] = m_color_profile;
    m_color_profile = std::move(current);
}

void CommandHistory::add(UndoPtr entry)
{
    // anything that had been undone is unreachable once a new edit lands on top of it
    const bool discarded_saved_state = m_saved_state > m_current_state;
    m_entries.resize(size_t(m_current_state));

    m_entries.push_back(std::move(entry));
    ++m_current_state;

    // the state the image last agreed with its file at can no longer be returned to
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
    // only entries behind the cursor are droppable: those ahead of it are what redo would apply.
    // Dropping stops at the cursor even if that leaves the history over budget.
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
    // a save point among the dropped entries can never be reached again, so the image counts as modified
    // from here on
    m_saved_state = m_saved_state >= dropped ? m_saved_state - dropped : -1;
}

StructureUndo::StructureUndo(const Image &img, std::string name) :
    m_name(std::move(name)), m_data_window(img.data_window), m_display_window(img.display_window)
{
    // Channel is not copyable, its deleted copy being what stops a texture and a statistics task from
    // being duplicated, so the samples are copied into fresh channels
    m_channels.reserve(img.channels.size());
    for (const auto &c : img.channels)
    {
        Channel copy{c.name, c.size()};
        copy.bits_per_sample = c.bits_per_sample;
        copy.ungrouped       = c.ungrouped;
        std::copy(c.data(), c.data() + c.num_elements(), copy.data());
        m_channels.push_back(std::move(copy));
    }
}

StructureUndo::~StructureUndo() = default;

void StructureUndo::swap(Image &img)
{
    std::swap(m_channels, img.channels);
    std::swap(m_data_window, img.data_window);
    std::swap(m_display_window, img.display_window);

    // the channels that just arrived may be a different size, or carry no texture, so each is rebuilt
    // from its samples on the next draw
    for (auto &c : img.channels)
    {
        c.texture_is_dirty = true;
        c.mipmap_is_dirty  = true;
    }

    // the layers, groups and tree are built from the channel names, which just changed wholesale
    img.rebuild_layers();

    ++img.content_version;
}

size_t StructureUndo::memory_usage() const
{
    size_t total = 0;
    for (const auto &c : m_channels) total += size_t(c.num_elements()) * sizeof(float);
    return total;
}

UndoPtr structure_undo(const Image &img, const std::string &name) { return std::make_unique<StructureUndo>(img, name); }

UndoFactory reversible(std::function<void(Image &)> forward, std::function<void(Image &)> backward)
{
    return [forward = std::move(forward), backward = std::move(backward)](const Image &,
                                                                          const std::string &name) -> UndoPtr
    { return std::make_unique<LambdaUndo>(name, backward, forward); };
}
