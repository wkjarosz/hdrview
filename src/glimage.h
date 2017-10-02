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
#include "async.h"
#include <utility>
#include <memory>

/*!
    A class which encapsulates a single HDRImage, a corresponding OpenGL texture, and histogram.
    Access to the HDRImage is provided only through the modify function, which accepts undo-able image editing commands
*/
class GLImage
{
public:
	typedef std::pair<Eigen::MatrixX3f,Eigen::MatrixX3f> MatrixPair;
	typedef AsyncTask<MatrixPair> LazyHistograms;
	typedef std::shared_ptr<const AsyncTask<ImageCommandResult>> ConstModifyingTask;
	typedef std::shared_ptr<AsyncTask<ImageCommandResult>> ModifyingTask;


    GLImage();
    ~GLImage();

	bool canModify() const;
	ConstModifyingTask modifyingTask() const    {return m_asyncCommand;}
    void asyncModify(const ImageCommand & command);
	void asyncModify(const ImageCommandWithProgress & command);
    bool isModified() const                     {checkAsyncResult(); return m_history.isModified();}
    bool undo();
    bool redo();
    bool hasUndo() const                        {checkAsyncResult(); return m_history.hasUndo();}
    bool hasRedo() const                        {checkAsyncResult(); return m_history.hasRedo();}

	GLuint glTextureId() const;
    std::string filename() const                {return m_filename;}
    const HDRImage & image() const              {return *m_image;}
    int width() const                           {checkAsyncResult(); return m_image->width();}
    int height() const                          {checkAsyncResult(); return m_image->height();}
    Eigen::Vector2i size() const                {return Eigen::Vector2i(width(), height());}
    bool contains(const Eigen::Vector2i& p) const {return (p.array() >= 0).all() && (p.array() < size().array()).all();}

    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither) const;

	float histogramExposure() const {return m_cachedHistogramExposure;}
	bool histogramDirty() const {return m_histogramDirty;}
	std::shared_ptr<LazyHistograms> histograms() const {return m_histograms;}
	void recomputeHistograms(float exposure) const;


private:
	void init() const;
	bool checkAsyncResult() const;
	bool waitForAsyncResult() const;

	mutable GLuint m_texture = 0;

	mutable std::shared_ptr<HDRImage> m_image;

    std::string m_filename;
    mutable float m_cachedHistogramExposure;
    mutable std::atomic<bool> m_histogramDirty;
	mutable std::shared_ptr<LazyHistograms> m_histograms;
    mutable CommandHistory m_history;

	mutable ModifyingTask m_asyncCommand = nullptr;
};

typedef std::shared_ptr<const GLImage> ConstImagePtr;
typedef std::shared_ptr<GLImage> ImagePtr;
