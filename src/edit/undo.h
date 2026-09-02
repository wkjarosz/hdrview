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

struct Channel;

//! One reversible change to an Image.
/*!
    An entry restores principal data only: the samples and the windows they sit in. Derived data
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

//! Specify the undo and redo commands using lambda expressions, storing no pixels at all. For operations
//! that re-derive their inverse: a flip, or a quarter turn the other way.
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

//! A rectangle of some channels, saved before an edit overwrote it. Construct from the pre-edit image;
//! undo and redo both swap the stored pixels with the image's, so only that rectangle reaches the GPU.
class ChannelRectUndo : public UndoEntry
{
public:
    /*!
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
    //! Exchange the stored pixels with the image's, leaving this entry holding what it just replaced.
    void swap(Image &img);

    std::string           m_name;
    Box2i                 m_bounds; //!< In image coordinates
    std::vector<int>      m_channels;
    std::vector<Array2Df> m_pixels; //!< One per entry of m_channels, sized to m_bounds
};

//! An image's whole channel list and windows, for cropping, resizing, and anything that adds or removes
//! a channel. Putting one back swaps the vectors, so the sample buffers move rather than copy.
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

//! Every field compute_color_transform() reads or writes, plus the profile name, saved before a color
//! conversion changed them. Rides alongside the pixels in a CompositeUndo, since the two must undo together.
class ColorMetadataUndo : public UndoEntry
{
public:
    ColorMetadataUndo(const Image &img, std::string name);
    //! Out of line because State is incomplete here and the pointer has to destroy it.
    ~ColorMetadataUndo() override;

    void        undo(Image &img) override { swap(img); }
    void        redo(Image &img) override { swap(img); }
    std::string name() const override { return m_name; }

private:
    void swap(Image &img);

    struct State;
    std::unique_ptr<State> m_state; //!< Out of line, since its members need image.h
    std::string            m_name;
};

//! Several entries applied as one, undone in the opposite order to how they were built.
class CompositeUndo : public UndoEntry
{
public:
    CompositeUndo(std::string name, std::vector<UndoPtr> entries) :
        m_name(std::move(name)), m_entries(std::move(entries))
    {
    }

    void undo(Image &img) override
    {
        for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) (*it)->undo(img);
    }
    void redo(Image &img) override
    {
        for (auto &e : m_entries) e->redo(img);
    }
    std::string name() const override { return m_name; }
    size_t      memory_usage() const override
    {
        size_t total = 0;
        for (const auto &e : m_entries) total += e->memory_usage();
        return total;
    }

private:
    std::string          m_name;
    std::vector<UndoPtr> m_entries;
};

//! An image's undo history: the entries applied so far, and a cursor into them. Bounded by total bytes,
//! since what threatens memory is one large structural edit and not many small ones.
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

    //! Largest total the entries may occupy before the oldest are dropped. Generous, since dropping one
    //! silently shortens how far back the user can go.
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
