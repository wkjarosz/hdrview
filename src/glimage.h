/*! \file glimage.h
    \author Wojciech Jarosz
*/
#pragma once

#include <cstdint>             // for uint32_t
#include <Eigen/Core>          // for Vector2i, Matrix4f, Vector3f
#include <functional>          // for function
#include <iosfwd>              // for string
#include <type_traits>         // for swap
#include <vector>              // for vector, allocator
#include "hdrimage.h"          // for HDRImage
#include "fwd.h"               // for HDRImage
#include "commandhistory.h"

/*!
    A class which encapsulates a single image which is draw as a
    textured GL quad to the screen.

    Also stores a linear and sRGB histogram.
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
        m_histogramDirty = true;
        init();
    }
    bool isModified() const         {return m_history.isModified();}
    bool undo();
    bool redo();
    bool hasUndo() const            {return m_history.hasUndo();}
    bool hasRedo() const            {return m_history.hasRedo();}

    std::string filename() const    {return m_filename;}
    const HDRImage & image() const  {return m_image;}
    int width() const               {return m_image.width();}
    int height() const              {return m_image.height();}
    Eigen::Vector2i size() const    {return Eigen::Vector2i(width(), height());}
    bool contains(const Eigen::Vector2i& p) const {return (p.array() >= 0).all() && (p.array() < size().array()).all();}

    void draw(const Eigen::Vector2f & scale,
              const Eigen::Vector2f & position,
              float gain, float gamma,
              bool sRGB, bool dither,
              const Eigen::Vector3f & channels) const;
    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither) const;

    const Eigen::MatrixX3f & linearHistogram(float exposure) const;
    const Eigen::MatrixX3f & sRGBHistogram(float exposure) const;
    const Eigen::MatrixX3f & histogram(bool linear, float exposure) const
    {
        return linear ? linearHistogram(exposure) : sRGBHistogram(exposure);
    }


private:
    nanogui::GLShader * m_shader = nullptr;
    uint32_t m_texture = 0;
    HDRImage m_image;
    std::string m_filename;
    mutable float m_histogramExposure;
    mutable bool m_histogramDirty = true;
    mutable Eigen::MatrixX3f m_linearHistogram, m_sRGBHistogram;
    mutable CommandHistory m_history;
};
