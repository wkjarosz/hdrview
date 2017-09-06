//
// Created by Wojciech Jarosz on 9/4/17.
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

    virtual void undo(HDRImage & img) = 0;
    virtual void redo(HDRImage & img) = 0;
};

//! Brute-force undo: Saves the entire image data so that we can copy it back
class FullImageUndo : public ImageCommandUndo
{
public:
    FullImageUndo(const HDRImage & img) : m_undoImage(img) {}
    virtual ~FullImageUndo() {}

    virtual void undo(HDRImage & img) { std::swap(img, m_undoImage);}
    virtual void redo(HDRImage & img) {undo(img);}

private:
    HDRImage m_undoImage;
};

//! Specify the undo and redo commands using lambda expressions
class LambdaUndo : public ImageCommandUndo
{
public:
    LambdaUndo(const std::function<void(HDRImage & img)> & undoCmd,
               const std::function<void(HDRImage & img)> & redoCmd = nullptr) :
        m_undo(undoCmd), m_redo(redoCmd ? redoCmd : undoCmd) {}
    virtual ~LambdaUndo() {}

    virtual void undo(HDRImage & img) {m_undo(img);}
    virtual void redo(HDRImage & img) {m_redo(img);}

private:
    std::function<void(HDRImage & img)> m_undo, m_redo;
};

//! Stores and manages an undo history list for image modifications
class CommandHistory
{
public:
    CommandHistory() :
        m_currentState(0), m_savedState(0)
    {

    }

    ~CommandHistory()
    {
        for (int i = 0; i < size(); ++i)
            delete m_history[i];
    }

    bool isModified() const     {return m_currentState != m_savedState;}
    void markSaved()            {m_savedState = m_currentState;}

    int currentState() const    {return m_currentState;}
    int savedState() const      {return m_savedState;}
    int size() const            {return m_history.size();}
    bool hasUndo() const        {return m_currentState > 0;}
    bool hasRedo() const        {return m_currentState < size();}

    void addCommand(ImageCommandUndo * cmd)
    {
        // delete all history newer than the current state
        for (int i = m_currentState; i < size(); ++i)
            delete m_history[i];
        m_history.resize(m_currentState);

        // add the new command and increment state
        m_history.push_back(cmd);
        m_currentState++;
    }
    bool undo(HDRImage & img)
    {
        // check if there is anything to undo
        if (!hasUndo() || m_currentState > size())
            return false;

        m_currentState--;
        m_history[m_currentState]->undo(img);
        return true;
    }
    bool redo(HDRImage & img)
    {
        // check if there is anything to redo
        if (!hasRedo() || m_currentState < 0)
            return false;

        m_history[m_currentState]->redo(img);
        m_currentState++;
        return true;
    }

private:
    std::vector<ImageCommandUndo *> m_history;

    // it is best to think of this state as pointing in between the entries in the m_history vector
    // it can range from [0,size()]
    // m_currentState == 0 indicates that there is nothing to undo
    // m_currentState == size() indicates that there is nothing to redo
    int m_currentState;
    int m_savedState;
};