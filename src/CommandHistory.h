//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <cstdint>             // for uint32_t
#include <Eigen/Core>          // for Vector2i, Matrix4f, Vector3f
#include <vector>              // for vector, allocator
#include "hdrimage.h"          // for HDRImage
#include "fwd.h"               // for HDRImage

//! Generic image manipulation undo class
class ImageCommandUndo
{
public:
    virtual ~ImageCommandUndo() = default;

    virtual void undo(std::shared_ptr<HDRImage> & img) = 0;
    virtual void redo(std::shared_ptr<HDRImage> & img) = 0;
};

using UndoPtr = std::shared_ptr<ImageCommandUndo>;
using ImageCommandResult = std::pair<std::shared_ptr<HDRImage>, UndoPtr>;

using ImageCommand = std::function<ImageCommandResult(const std::shared_ptr<const HDRImage> &)>;
using ImageCommandWithProgress = std::function<ImageCommandResult(const std::shared_ptr<const HDRImage> &, AtomicProgress &)>;


//! Brute-force undo: Saves the entire image data so that we can copy it back
class FullImageUndo : public ImageCommandUndo
{
public:
    explicit FullImageUndo(const HDRImage & img) : m_undoImage(std::make_shared<HDRImage>(img)) {}
    ~FullImageUndo() override = default;

    void undo(std::shared_ptr<HDRImage> & img) override {img.swap(m_undoImage);}
    void redo(std::shared_ptr<HDRImage> & img) override {undo(img);}

	const std::shared_ptr<HDRImage> image() const {return m_undoImage;}

private:
    std::shared_ptr<HDRImage> m_undoImage;
};

//! Specify the undo and redo commands using lambda expressions
class LambdaUndo : public ImageCommandUndo
{
public:
    explicit LambdaUndo(const std::function<void(std::shared_ptr<HDRImage> & img)> & undoCmd,
                        const std::function<void(std::shared_ptr<HDRImage> & img)> & redoCmd = nullptr) :
        m_undo(undoCmd), m_redo(redoCmd ? redoCmd : undoCmd) {}
    ~LambdaUndo() override = default;

    void undo(std::shared_ptr<HDRImage> & img) override {m_undo(img);}
    void redo(std::shared_ptr<HDRImage> & img) override {m_redo(img);}

private:
    std::function<void(std::shared_ptr<HDRImage> & img)> m_undo, m_redo;
};

//! Stores and manages an undo history list for image modifications
class CommandHistory
{
public:
    CommandHistory() :
        m_currentState(0), m_savedState(0)
    {
        // empty
    }

    ~CommandHistory()
    {
        // empty
    }

    bool is_modified() const     {return m_currentState != m_savedState;}
    void mark_saved()            {m_savedState = m_currentState;}

    int current_state() const    {return m_currentState;}
    int saved_state() const      {return m_savedState;}
    int size() const            {return m_history.size();}
    bool has_undo() const        {return m_currentState > 0;}
    bool has_redo() const        {return m_currentState < size();}

    void add_command(UndoPtr cmd)
    {
        // deletes all history newer than the current state
        m_history.resize(m_currentState);

        // add the new command and increment state
        m_history.push_back(std::move(cmd));
        m_currentState++;
    }

    bool undo(std::shared_ptr<HDRImage> & img)
    {
        // check if there is anything to undo
        if (!has_undo() || m_currentState > size())
            return false;

        m_history[--m_currentState]->undo(img);
        return true;
    }
    bool redo(std::shared_ptr<HDRImage> & img)
    {
        // check if there is anything to redo
        if (!has_redo() || m_currentState < 0)
            return false;

        m_history[m_currentState++]->redo(img);
        return true;
    }

private:
    std::vector<UndoPtr> m_history;

    // it is best to think of this state as pointing in between the entries in the m_history vector
    // it can range from [0,size()]
    // m_currentState == 0 indicates that there is nothing to undo
    // m_currentState == size() indicates that there is nothing to redo
    int m_currentState;
    int m_savedState;
};