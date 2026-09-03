//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"
#include "colorspace.h"
#include "fwd.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct Channel;

/// One reversible change to an Image.
/**
    An entry restores principal data only: the pixels and the windows they sit in. Derived data
    (textures, mip chains, statistics, the layer tree) is flagged for recomputation by whoever applies it.
*/
class UndoEntry
{
public:
    virtual ~UndoEntry() = default;

    virtual void undo(Image &img) = 0;
    virtual void redo(Image &img) = 0;

    /// What the Edit menu shows beside "Undo"/"Redo", e.g. "Invert".
    virtual std::string name() const = 0;

    /// Bytes of pixel data this entry holds, which CommandHistory uses to bound itself.
    virtual size_t memory_usage() const { return 0; }
};

using UndoPtr = std::unique_ptr<UndoEntry>;

/// Undo and redo given as lambdas, storing no pixels at all.
/**
    For the edits that re-derive their inverse: a flip, or a quarter turn the other way.
*/
class LambdaUndo : public UndoEntry
{
public:
    LambdaUndo(std::string name, std::function<void(Image &)> undo_fn, std::function<void(Image &)> redo_fn) :
        m_name(std::move(name)), m_undo(std::move(undo_fn)), m_redo(std::move(redo_fn))
    {
    }

    void        undo(Image &img) override { m_undo(img); }
    void        redo(Image &img) override { m_redo(img); }
    std::string name() const override { return m_name; }

private:
    std::string                  m_name;
    std::function<void(Image &)> m_undo, m_redo;
};

/// A rectangle of some channels, saved before an edit overwrote it.
/**
    Construct from the pre-edit image; undo and redo both swap the stored pixels with the image's, so only
    that rectangle reaches the GPU.
*/
class ChannelRectUndo : public UndoEntry
{
public:
    /**
        Snapshot \p bounds of the given channels of \p img.

        \param img      Image to read from; must be in its pre-edit state
        \param channels Indices into Image::channels
        \param bounds   Rectangle in image (not channel-local) coordinates; clipped to the data window
        \param name     Shown beside "Undo"/"Redo"
    */
    ChannelRectUndo(const Image &img, std::vector<int> channels, const Box2i &bounds, std::string name);

    void        undo(Image &img) override { swap(img); }
    void        redo(Image &img) override { swap(img); }
    std::string name() const override { return m_name; }
    size_t      memory_usage() const override;

private:
    /// Exchange the stored pixels with the image's, leaving this entry holding what it just replaced.
    void swap(Image &img);

    std::string           m_name;
    Box2i                 m_bounds; ///< In image coordinates
    std::vector<int>      m_channels;
    std::vector<Array2Df> m_pixels; ///< One per entry of m_channels, sized to m_bounds
};

/// An image's whole channel list and windows, for cropping, resizing, and anything that adds or removes one.
class StructureUndo : public UndoEntry
{
public:
    /// Snapshot the channels and windows of \p img, which must be in its pre-edit state.
    StructureUndo(const Image &img, std::string name);
    /// Out of line because Channel is incomplete here and the vector has to destroy them.
    ~StructureUndo() override;

    void        undo(Image &img) override { swap(img); }
    void        redo(Image &img) override { swap(img); }
    std::string name() const override { return m_name; }
    size_t      memory_usage() const override;

private:
    void swap(Image &img);

    std::string          m_name;
    std::vector<Channel> m_channels;
    Box2i                m_data_window, m_display_window;
};

/// The pixels a color conversion rewrote and the color metadata describing them, taken back together.
class ColorMetadataUndo : public UndoEntry
{
public:
    /// As ChannelRectUndo, and \p img's color metadata alongside.
    ColorMetadataUndo(const Image &img, std::vector<int> channels, const Box2i &bounds, std::string name);

    void        undo(Image &img) override { swap(img); }
    void        redo(Image &img) override { swap(img); }
    std::string name() const override { return m_pixels.name(); }
    size_t      memory_usage() const override { return m_pixels.memory_usage(); }

private:
    void swap(Image &img);

    ChannelRectUndo               m_pixels;
    std::optional<Chromaticities> m_chromaticities;
    std::optional<float2>         m_adopted_neutral;
    float3x3                      m_RGB_to_XYZ, m_XYZ_to_RGB, m_to_sRGB;
    float3                        m_luminance_weights;
    AdaptationMethod              m_adaptation_method;
    ColorGamut_                   m_color_space;
    WhitePoint_                   m_white_point;
    std::string                   m_color_profile; ///< metadata["color profile"], the panel's "Profile name"
};

/// Builds the entry that reverses an edit, from the image as it was before it; see modify_image().
using UndoFactory = std::function<UndoPtr(const Image &, const std::string &)>;

/// Saves the whole channel list and the windows, for the edits that reshape the image.
UndoPtr structure_undo(const Image &img, const std::string &name);

/// An edit that re-derives its inverse: \p backward must undo \p forward, as a flip or a quarter turn does.
UndoFactory reversible(std::function<void(Image &)> forward, std::function<void(Image &)> backward);

/// An image's undo history: the entries applied so far, a cursor into them, and a bound on total bytes.
class CommandHistory
{
public:
    /// \param already_modified True for an image that differs from its file before any edit
    explicit CommandHistory(bool already_modified = false) : m_saved_state(already_modified ? -1 : 0) {}

    bool is_modified() const { return m_current_state != m_saved_state; }
    void mark_saved() { m_saved_state = m_current_state; }

    int  size() const { return int(m_entries.size()); }
    bool has_undo() const { return m_current_state > 0; }
    bool has_redo() const { return m_current_state < size(); }

    /// Name of the entry "Undo" would apply, or "" if there is none.
    std::string undo_name() const { return has_undo() ? m_entries[size_t(m_current_state - 1)]->name() : ""; }
    /// Name of the entry "Redo" would apply, or "" if there is none.
    std::string redo_name() const { return has_redo() ? m_entries[size_t(m_current_state)]->name() : ""; }

    //
    // What the History panel reads. The cursor sits between entries: a state is an index into [0, size()],
    // an entry an index into [0, size()), and state k is the image after entries 0 through k-1.
    //
    int         current_state() const { return m_current_state; }
    int         saved_state() const { return m_saved_state; }
    std::string entry_name(int i) const { return m_entries[size_t(i)]->name(); }
    size_t      entry_memory_usage(int i) const { return m_entries[size_t(i)]->memory_usage(); }

    /// Record \p entry as the newest change, discarding anything that had been undone.
    void add(UndoPtr entry);

    /// Step back one entry, applying it to \p img. False if there was nothing to undo.
    bool undo(Image &img);
    /// Step forward one entry, applying it to \p img. False if there was nothing to redo.
    bool redo(Image &img);

    /// Total bytes held by the entries.
    size_t memory_usage() const;

    /// Largest total the entries may occupy before the oldest are dropped; generous, since dropping one is silent.
    static constexpr size_t k_max_memory = size_t(1) << 30; // 1 GiB

private:
    /// Drop the oldest entries until the total fits in k_max_memory.
    void trim();

    std::vector<UndoPtr> m_entries;

    // it is best to think of this state as pointing in between the entries in the m_entries vector
    // it can range from [0,size()]
    // m_current_state == 0 indicates that there is nothing to undo
    // m_current_state == size() indicates that there is nothing to redo
    int m_current_state = 0;
    // where the image last agreed with what is on disk; -1 once that point can no longer be reached
    int m_saved_state = 0;
};
