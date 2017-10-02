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
    virtual ~ImageCommandUndo() {}

    virtual void undo(std::shared_ptr<HDRImage> & img) = 0;
    virtual void redo(std::shared_ptr<HDRImage> & img) = 0;
};

typedef std::shared_ptr<ImageCommandUndo> UndoPtr;
typedef std::pair<std::shared_ptr<HDRImage>, UndoPtr> ImageCommandResult;

typedef std::function<ImageCommandResult(const std::shared_ptr<HDRImage> &)> ImageCommand;
typedef std::function<ImageCommandResult(const std::shared_ptr<HDRImage> &, AtomicProgress &)> ImageCommandWithProgress;


//! Brute-force undo: Saves the entire image data so that we can copy it back
class FullImageUndo : public ImageCommandUndo
{
public:
    FullImageUndo(const HDRImage & img) : m_undoImage(std::make_shared<HDRImage>(img)) {}
    virtual ~FullImageUndo() {}

    virtual void undo(std::shared_ptr<HDRImage> & img) {img.swap(m_undoImage);}
    virtual void redo(std::shared_ptr<HDRImage> & img) {undo(img);}

	const std::shared_ptr<HDRImage> image() const {return m_undoImage;}

private:
    std::shared_ptr<HDRImage> m_undoImage;
};

//! Specify the undo and redo commands using lambda expressions
class LambdaUndo : public ImageCommandUndo
{
public:
    LambdaUndo(const std::function<void(std::shared_ptr<HDRImage> & img)> & undoCmd,
               const std::function<void(std::shared_ptr<HDRImage> & img)> & redoCmd = nullptr) :
        m_undo(undoCmd), m_redo(redoCmd ? redoCmd : undoCmd) {}
    virtual ~LambdaUndo() {}

    virtual void undo(std::shared_ptr<HDRImage> & img) {m_undo(img);}
    virtual void redo(std::shared_ptr<HDRImage> & img) {m_redo(img);}

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

    bool isModified() const     {return m_currentState != m_savedState;}
    void markSaved()            {m_savedState = m_currentState;}

    int currentState() const    {return m_currentState;}
    int savedState() const      {return m_savedState;}
    int size() const            {return m_history.size();}
    bool hasUndo() const        {return m_currentState > 0;}
    bool hasRedo() const        {return m_currentState < size();}

    void addCommand(UndoPtr cmd)
    {
        // deletes all history newer than the current state
        m_history.resize(m_currentState);

        // add the new command and increment state
        m_history.push_back(std::move(cmd));
        m_currentState++;
    }

//    UndoPtr grabUndo(std::shared_ptr<HDRImage> & img)
//    {
//        // check if there is anything to undo
//        if (!hasUndo() || m_currentState > size())
//            return nullptr;
//
//        return m_history[--m_currentState];
//    }
//    UndoPtr grabRedo(std::shared_ptr<HDRImage> & img)
//    {
//        // check if there is anything to redo
//        if (!hasRedo() || m_currentState < 0)
//            return nullptr;
//
//        return m_history[m_currentState++];
//    }

    bool undo(std::shared_ptr<HDRImage> & img)
    {
        // check if there is anything to undo
        if (!hasUndo() || m_currentState > size())
            return false;

        m_history[--m_currentState]->undo(img);
        return true;
    }
    bool redo(std::shared_ptr<HDRImage> & img)
    {
        // check if there is anything to redo
        if (!hasRedo() || m_currentState < 0)
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