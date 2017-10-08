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


struct ImageHistogram
{
	float minimum;
	float average;
	float maximum;

	Eigen::MatrixX3f linearHistogram;
	Eigen::MatrixX3f sRGBHistogram;

	float exposure;
};


/*!
 * A helper class that uploads a texture to the GPU incrementally in smaller chunks.
 * To avoid stalling the main rendering thread, chunks are uploaded until a
 * timeout has been reached.
 */
class LazyGLTextureLoader
{
public:
	~LazyGLTextureLoader();

	bool dirty() const {return m_dirty;}
	void setDirty() {m_dirty = true; m_nextScanline = 0; m_uploadTime = 0;}

	/*!
	 * Incrementally upload a portion of an image to the GPU, returning shortly after the
	 * specified timeout duration. Should be called repeatedly until it returns True.
	 *
	 * @param img 		The image to upload
	 * @param timeout 	Return after this many milliseconds
	 * @param mipLevel 	Which miplevel to upload
	 * @param chunkSize The target number of pixels to upload
	 * @return 			True iff uploading is done
	 */
	bool uploadToGPU(const std::shared_ptr<const HDRImage> & img,
	                 int timeout = 100,
	                 int mipLevel = 0,
	                 int chunkSize = 128 * 128);

	GLuint textureID() const {return m_texture;}

private:
	GLuint m_texture = 0;
	int m_nextScanline = -1;
	bool m_dirty = false;
	double m_uploadTime = 0.0;
};

/*!
    A class which encapsulates a single HDRImage, a corresponding OpenGL texture, and histogram.
    Access to the HDRImage is provided only through the modify function, which accepts undo-able image editing commands
*/
class GLImage
{
public:
	typedef AsyncTask<std::shared_ptr<ImageHistogram>> LazyHistograms;
	typedef std::shared_ptr<const AsyncTask<ImageCommandResult>> ConstModifyingTask;
	typedef std::shared_ptr<AsyncTask<ImageCommandResult>> ModifyingTask;


    GLImage();
    ~GLImage();

	bool canModify() const;
	float progress() const;
    void asyncModify(const ImageCommand & command);
	void asyncModify(const ImageCommandWithProgress & command);
    bool isModified() const;
    bool undo();
    bool redo();
    bool hasUndo() const;
    bool hasRedo() const;

	GLuint glTextureId() const;
	void setFilename(std::string filename)      { m_filename = filename; }
    std::string filename() const                { return m_filename; }
	bool isNull() const                         { checkAsyncResult(); return !m_image || m_image->isNull(); }
    const HDRImage & image() const              { checkAsyncResult(); return *m_image; }
    int width() const                           { checkAsyncResult(); return m_image->width(); }
    int height() const                          { checkAsyncResult(); return m_image->height(); }
    Eigen::Vector2i size() const                { return isNull() ? Eigen::Vector2i(0,0) : Eigen::Vector2i(m_image->width(), m_image->height()); }
    bool contains(const Eigen::Vector2i& p) const {return (p.array() >= 0).all() && (p.array() < size().array()).all();}

    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither) const;

	float histogramExposure() const             { return m_cachedHistogramExposure; }
	bool histogramDirty() const                 { return m_histogramDirty; }
	std::shared_ptr<LazyHistograms> histograms() const {return m_histograms;}
	void recomputeHistograms(float exposure) const;


	typedef std::function<void(void)> VoidVoidFunc;

	/// Callback executed whenever an image finishes being modified, e.g. via @ref asyncModify
	const VoidVoidFunc & imageModifyDoneCallback() const            { return m_imageModifyDoneCallback; }
	void setImageModifyDoneCallback(const VoidVoidFunc & callback)  { m_imageModifyDoneCallback = callback; }

private:
	bool checkAsyncResult() const;
	bool waitForAsyncResult() const;
	void uploadToGPU() const;
	void modifyFinished() const;

	mutable LazyGLTextureLoader m_texture;

	mutable std::shared_ptr<HDRImage> m_image;

    std::string m_filename;
    mutable float m_cachedHistogramExposure;
    mutable std::atomic<bool> m_histogramDirty;
	mutable std::shared_ptr<LazyHistograms> m_histograms;
    mutable CommandHistory m_history;

	mutable ModifyingTask m_asyncCommand = nullptr;
	mutable bool m_asyncRetrieved = false;

	// various callback functions
	VoidVoidFunc m_imageModifyDoneCallback;
};

typedef std::shared_ptr<const GLImage> ConstImagePtr;
typedef std::shared_ptr<GLImage> ImagePtr;
