//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"
#include "fwd.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

//! Declared here as image.h does, rather than in fwd.h, which does not carry it.
struct Channel;

/*!
    One reversible change to an Image.

    Every entry restores *principal* data only -- the samples themselves and the windows they sit in.
    Derived data (textures, mip chains, statistics, the layer/group tree) is not stored; it is flagged for
    recomputation by the code that applies the entry, since rebuilding it costs less than keeping a second
    copy of it for every step of the history.

    undo() and redo() are separate methods, but an entry that stores the pixels it displaced implements
    both as the same swap: after undo() the entry holds what it just replaced, which is exactly what
    redo() needs.
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

/*!
    Specify the undo and redo commands using lambda expressions, storing no pixels at all.

    For operations that are their own inverse (a flip) or whose inverse is another operation of the same
    cost (rotate 90 degrees one way, then the other). Both directions re-derive the pixels rather than
    remembering them, so a full-image geometric change costs a few bytes of history.
*/
class LambdaUndo : public UndoEntry
{
public:
    LambdaUndo(std::string name, std::function<void(Image &)> undo_fn, std::function<void(Image &)> redo_fn) :
        m_name(std::move(name)), m_undo(std::move(undo_fn)), m_redo(std::move(redo_fn))
    {
    }

    /// An operation that is its own inverse; \p fn is used in both directions.
    LambdaUndo(std::string name, std::function<void(Image &)> fn) : LambdaUndo(std::move(name), fn, fn) {}

    void        undo(Image &img) override { m_undo(img); }
    void        redo(Image &img) override { m_redo(img); }
    std::string name() const override { return m_name; }

private:
    std::string                  m_name;
    std::function<void(Image &)> m_undo, m_redo;
};

/*!
    A rectangle of one or more channels, saved before it was overwritten.

    The workhorse: every point operation and every filter changes a known set of channels within a known
    box, so this is all that has to be remembered. Constructed from the image *before* the edit is
    applied, then swapped back and forth -- so undoing costs what the edit did, and the entry never holds
    more than the one rectangle it displaced.

    The stored pixels are pushed back through Channel::upload_tile(), the same path a renderer streams
    through, so only the changed rectangle reaches the GPU.
*/
class ChannelRectUndo : public UndoEntry
{
public:
    /*!
        Snapshot \p bounds of the given channels of \p img.

        \param [] img      Image to read from; must be in its pre-edit state
        \param [] channels Indices into Image::channels
        \param [] bounds   Rectangle in image (not channel-local) coordinates; clipped to the data window
        \param [] name     Shown beside "Undo"/"Redo"
    */
    ChannelRectUndo(const Image &img, std::vector<int> channels, const Box2i &bounds, std::string name);

    void        undo(Image &img) override { swap(img); }
    void        redo(Image &img) override { swap(img); }
    std::string name() const override { return m_name; }
    size_t      memory_usage() const override;

private:
    //! Exchange the stored pixels with the ones currently in the image, leaving this entry holding
    //! whatever it just replaced -- which is what the opposite direction needs.
    void swap(Image &img);

    std::string           m_name;
    Box2i                 m_bounds; //!< In image coordinates
    std::vector<int>      m_channels;
    std::vector<Array2Df> m_pixels; //!< One per entry of m_channels, sized to m_bounds
};

/*!
    An image's whole channel list and windows, for edits that change the shape of the image.

    Cropping, resizing, and anything that adds or removes a channel cannot be described as a rectangle of
    the samples that were already there, so these keep the entire set. Rare enough that the cost is
    acceptable, and cheaper than it looks: putting one back swaps the vectors, which moves the sample
    buffers rather than copying them.
*/
class StructureUndo : public UndoEntry
{
public:
    /// Snapshot the channels and windows of \p img, which must be in its pre-edit state.
    StructureUndo(const Image &img, std::string name);
    //! Out of line because Channel is incomplete here and the vector has to destroy them.
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

/*!
    An image's undo history: the entries applied so far, and a cursor into them.

    The cursor points *between* entries and ranges over [0, size()]: 0 means there is nothing left to
    undo, size() nothing to redo. A second cursor records where the image last agreed with what is on
    disk, which is what lets is_modified() answer correctly after undoing back past a save as well as
    after editing forward past one.

    Bounded by total bytes rather than entry count, since what threatens memory is one large structural
    edit rather than many small ones.
*/
class CommandHistory
{
public:
    /// \param [] already_modified True for an image that differs from its file before any edit
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

    /// Record \p entry as the newest change, discarding anything that had been undone.
    void add(UndoPtr entry);

    /// Step back one entry, applying it to \p img. False if there was nothing to undo.
    bool undo(Image &img);
    /// Step forward one entry, applying it to \p img. False if there was nothing to redo.
    bool redo(Image &img);

    /// Total bytes held by the entries.
    size_t memory_usage() const;

    /*!
        Largest total the entries may occupy before the oldest are dropped.

        Generous, because dropping an entry silently shortens how far back the user can go: the limit is
        here to stop a long session on a large image growing without bound, not to keep the history small.
    */
    static constexpr size_t k_max_memory = size_t(1) << 30; // 1 GiB

private:
    //! Drop the oldest entries until the total fits in k_max_memory.
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
