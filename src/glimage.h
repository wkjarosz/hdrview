//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <cstdint>             // for uint32_t
#include <Eigen/Core>          // for Vector2i, Matrix4f, Vector3f
#include <functional>          // for function
#include <iosfwd>              // for string
#include <type_traits>         // for swap
#include <vector>              // for vector, allocator
#include <nanogui/opengl.h>
#include "hdrimage.h"          // for HDRImage
#include "fwd.h"               // for HDRImage
#include "commandhistory.h"

/*!
    A class which encapsulates a single HDRImage, a corresponding OpenGL texture, and histogram.
    Access to the HDRImage is provided only through the modify function, which accepts undo-able image editing commands
*/
class GLImage
{
public:
    GLImage();
    ~GLImage();
    void clear();

    void init() const;

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

	GLuint glTextureId() const;
    std::string filename() const    {return m_filename;}
    const HDRImage & image() const  {return m_image;}
    int width() const               {return m_image.width();}
    int height() const              {return m_image.height();}
    Eigen::Vector2i size() const    {return Eigen::Vector2i(width(), height());}
    bool contains(const Eigen::Vector2i& p) const {return (p.array() >= 0).all() && (p.array() < size().array()).all();}

    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither) const;

	float histogramExposure() const {return m_cachedHistogramExposure;}

    const Eigen::MatrixX3f & linearHistogram(float exposure) const;
    const Eigen::MatrixX3f & sRGBHistogram(float exposure) const;
    const Eigen::MatrixX3f & histogram(bool linear, float exposure) const
    {
        return linear ? linearHistogram(exposure) : sRGBHistogram(exposure);
    }


private:
	mutable GLuint m_texture = 0;
    HDRImage m_image;
    std::string m_filename;
    mutable float m_cachedHistogramExposure;
    mutable bool m_histogramDirty = true;
    mutable Eigen::MatrixX3f m_linearHistogram, m_sRGBHistogram;
    mutable CommandHistory m_history;
};
