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
// #include <nanogui/opengl.h>
#include <nanogui/texture.h>
#include "hdrimage.h"          // for HDRImage
#include "fwd.h"               // for HDRImage
#include "commandhistory.h"
#include "async.h"
#include <utility>
#include <memory>


struct ImageStatistics
{
	float minimum;
	float average;
	float maximum;
	float exposure;

	enum AxisScale : int
	{
		ELinear = 0,
		ESRGB = 1,
		ELog = 2,
		ENumAxisScales = 3
	};

	struct Histogram
	{
		std::vector<float> values[3];
		std::vector<float> xTicks;
		std::vector<std::string> xTickLabels;
	};

	Histogram histogram[ENumAxisScales];


	static std::shared_ptr<ImageStatistics> computeStatistics(const HDRImage &img, float exposure);
};


// /*!
//  * A helper class that uploads a texture to the GPU incrementally in smaller chunks.
//  * To avoid stalling the main rendering thread, chunks are uploaded until a
//  * timeout has been reached.
//  */
// class LazyGLTextureLoader
// {
// public:
// 	~LazyGLTextureLoader();

// 	bool dirty() const {return m_dirty;}
// 	void setDirty() {m_dirty = true; m_nextScanline = 0; m_uploadTime = 0;}

// 	/*!
// 	 * Incrementally upload a portion of an image to the GPU, returning shortly after the
// 	 * specified timeout duration. Should be called repeatedly until it returns True.
// 	 *
// 	 * @param img 		The image to upload
// 	 * @param timeout 	Return after this many milliseconds
// 	 * @param mipLevel 	Which miplevel to upload
// 	 * @param chunkSize The target number of pixels to upload
// 	 * @return 			True iff uploading is done
// 	 */
// 	bool upload_to_GPU(const std::shared_ptr<const HDRImage> & img,
// 	                 int timeout = 100,
// 	                 int chunkSize = 128 * 128);

// 	GLuint textureID() const {return m_texture;}

// private:
// 	GLuint m_texture = 0;
// 	int m_nextScanline = -1;
// 	bool m_dirty = false;
// 	double m_uploadTime = 0.0;
// };

/*!
    A class which encapsulates a single HDRImage, a corresponding GPU texture, and histogram.
    Edit access to the HDRImage is provided only through the modify function, which accepts undo-able image editing commands
*/
class GLImage
{
public:
	using TextureRef = nanogui::ref<nanogui::Texture>;
	using LazyHistogram = AsyncTask<std::shared_ptr<ImageStatistics>>;
	using LazyHistogramPtr = std::shared_ptr<LazyHistogram>;
	using ConstModifyingTask = std::shared_ptr<const AsyncTask<ImageCommandResult>>;
	using ModifyingTask = std::shared_ptr<AsyncTask<ImageCommandResult>>;
	using VoidVoidFunc = std::function<void(void)>;


    GLImage();
    ~GLImage();

	bool can_modify() const;
	float progress() const;
    void asyncModify(const ImageCommand & command);
	void asyncModify(const ImageCommandWithProgress & command);
    bool is_modified() const;
    bool undo();
    bool redo();
    bool has_undo() const;
    bool has_redo() const;

	TextureRef texture();
	void set_filename(const std::string & filename) { m_filename = filename; }
    std::string filename() const                    { return m_filename; }
	bool is_null() const                            { check_async_result(); return !m_image || m_image->is_null(); }
    const HDRImage & image() const                  { check_async_result(); return *m_image; }
    int width() const                               { check_async_result(); return m_image->width(); }
    int height() const                              { check_async_result(); return m_image->height(); }
    nanogui::Vector2i size() const                  { return is_null() ? nanogui::Vector2i(0,0) : nanogui::Vector2i(m_image->width(), m_image->height()); }
    bool contains(const nanogui::Vector2i& p) const
	{
		return p[0] >= 0 && p[1] >= 0 && p[0] < size()[0] && p[1] < size()[1];
	}

    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither) const;

	bool check_async_result() const;
	void upload_to_GPU() const;

	float histogram_exposure() const            { return m_cachedHistogramExposure; }
	// bool histogram_dirty() const                { return m_histogramDirty; }
	LazyHistogramPtr histograms() const         { return m_histograms; }
	void recompute_histograms(float exposure) const;

	/// Callback executed whenever an image finishes being modified, e.g. via @ref asyncModify
	const VoidVoidFunc & modify_done_callback() const            { return m_modify_done_callback; }
	void set_modify_done_callback(const VoidVoidFunc & callback)  { m_modify_done_callback = callback; }

private:

	bool wait_for_async_result() const;
	void modify_done() const;

	mutable std::shared_ptr<HDRImage> m_image;
	mutable TextureRef m_texture;
	mutable bool m_texture_dirty = false;
    std::string m_filename;
    mutable float m_cachedHistogramExposure;
    mutable std::atomic<bool> m_histogramDirty;
	mutable LazyHistogramPtr m_histograms;
    mutable CommandHistory m_history;

	mutable ModifyingTask m_asyncCommand = nullptr;
	mutable bool m_asyncRetrieved = false;

	// various callback functions
	VoidVoidFunc m_modify_done_callback;
};

using ConstImagePtr = std::shared_ptr<const GLImage>;
using ImagePtr = std::shared_ptr<GLImage>;
