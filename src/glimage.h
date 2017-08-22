/*! \file glimage.h
    \author Wojciech Jarosz
*/
#pragma once

#include <string>
#include <nanogui/opengl.h>
#include <nanogui/glutil.h>
#include "hdrimage.h"

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

    virtual void undo(HDRImage & img) {std::swap(img, m_undoImage);}
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
class UndoHistory
{
public:
    UndoHistory() :
        m_currentState(0), m_savedState(0)
    {

    }

    ~UndoHistory()
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


/*!
    A class which encapulates a single image which is draw as a
    textured GL quad to the screen.
*/
class GLImage
{
public:
    GLImage();
    ~GLImage();
    void clear();

    void init();

    void modify(const std::function<ImageCommandUndo*(HDRImage & img)> & command)
    {
        m_history.addCommand(command(m_image));
        init();
    }
    bool isModified() const         {return m_history.isModified();}
    bool undo()                     {bool ret = m_history.undo(m_image); init(); return ret;}
    bool redo()                     {bool ret = m_history.redo(m_image); init(); return ret;}
    bool hasUndo()                  {return m_history.hasUndo();}
    bool hasRedo()                  {return m_history.hasRedo();}

    std::string filename() const    {return m_filename;}
    const HDRImage & image() const  {return m_image;}
    int width() const               {return m_image.width();}
    int height() const              {return m_image.height();}
    Eigen::Vector2i size() const    {return Eigen::Vector2i(width(), height());}

    void draw(const Eigen::Matrix4f & mvp,
              float gain, float gamma,
              bool sRGB, bool dither,
              const Eigen::Vector3f & channels) const;
    bool load(const std::string & filename)
    {
        m_history = UndoHistory();
        m_filename = filename;
        return m_image.load(filename);
    }
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither) const
    {
        m_history.markSaved();
        return m_image.save(filename, gain, gamma, sRGB, dither);
    }


private:
    nanogui::GLShader * m_shader = nullptr;
    uint32_t m_texture = 0;
    HDRImage m_image;
    std::string m_filename;
    mutable UndoHistory m_history;
};
