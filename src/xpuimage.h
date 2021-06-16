//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "async.h"
#include "box.h"
#include "commandhistory.h"
#include "fwd.h"      // for HDRImage
#include "hdrimage.h" // for HDRImage
#include <cstdint>    // for uint32_t
#include <functional> // for function
#include <iosfwd>     // for string
#include <memory>
#include <nanogui/texture.h>
#include <type_traits> // for swap
#include <utility>
#include <vector> // for vector, allocator

struct ImageStatistics
{
    float minimum;
    float average;
    float maximum;
    float exposure;

    enum AxisScale : int
    {
        ELinear        = 0,
        ESRGB          = 1,
        ELog           = 2,
        ENumAxisScales = 3
    };

    struct Histogram
    {
        std::vector<float>       values[3];
        std::vector<float>       xTicks;
        std::vector<std::string> xTickLabels;
    };

    Histogram histogram[ENumAxisScales];

    static std::shared_ptr<ImageStatistics> compute_statistics(const HDRImage &img, float exposure,
                                                               AtomicProgress &prog);
};

/*!
    Stores an image both on the CPU (as an HDRImage) and as a corresponding texture on the GPU.

    An XPUImage also maintains a current histogram for the image data.

    Edit access to the HDRImage is provided only through the modify function, which accepts undo-able image editing
   commands
*/
class XPUImage
{
public:
    using TextureRef       = nanogui::ref<nanogui::Texture>;
    using HistogramTask    = AsyncTask<std::shared_ptr<ImageStatistics>>;
    using HistogramTaskPtr = std::shared_ptr<HistogramTask>;
    using ModifyingTask    = AsyncTask<ImageCommandResult>;
    using ModifyingTaskPtr = std::shared_ptr<ModifyingTask>;
    using VoidVoidFunc     = std::function<void(void)>;

    XPUImage(bool modified = false);
    ~XPUImage();

    bool  can_modify() const;
    float progress() const;
    void  async_modify(const ConstImageCommand &command);
    void  async_modify(const ConstImageCommandWithProgress &command);
    void  start_modify(const ConstImageCommand &command);
    void  direct_modify(const ImageCommand &command);
    bool  is_modified() const;
    bool  undo();
    bool  redo();
    bool  has_undo() const;
    bool  has_redo() const;

    TextureRef  texture() const;
    void        set_filename(const std::string &filename) { m_filename = filename; }
    std::string filename() const { return m_filename; }
    bool        is_null() const
    {
        check_async_result();
        return !m_image || m_image->is_null();
    }
    const HDRImage &image() const
    {
        check_async_result();
        return *m_image;
    }
    int width() const
    {
        check_async_result();
        return m_image->width();
    }
    int height() const
    {
        check_async_result();
        return m_image->height();
    }
    nanogui::Vector2i size() const
    {
        return is_null() ? nanogui::Vector2i(0, 0) : nanogui::Vector2i(m_image->width(), m_image->height());
    }
    Box2i  box() const { return is_null() ? Box2i() : Box2i(0, size()); }
    Box2i &roi() const { return m_roi; }

    bool save(const std::string &filename, float gain, float gamma, bool sRGB, bool dither) const;

    bool check_async_result() const;
    void upload_to_GPU() const;

    float histogram_exposure() const { return m_cached_histogram_exposure; }
    // bool histogram_dirty() const                { return m_histogram_dirty; }
    HistogramTaskPtr histograms() const { return m_histograms; }
    void             cancel_histograms() const;
    void             recompute_histograms(float exposure) const;

    /// Callback executed whenever an image finishes being modified, e.g. via @ref async_modify
    VoidVoidFunc async_modify_done_callback() const { return m_async_modify_done_callback; }
    void         set_async_modify_done_callback(const VoidVoidFunc &cb) { m_async_modify_done_callback = cb; }

protected:
    bool wait_for_async_result() const;
    void async_modify_done() const;

    mutable HDRImagePtr       m_image;
    mutable TextureRef        m_texture;
    mutable bool              m_texture_dirty = false;
    std::string               m_filename;
    mutable float             m_cached_histogram_exposure;
    mutable std::atomic<bool> m_histogram_dirty;
    mutable HistogramTaskPtr  m_histograms;
    mutable CommandHistory    m_history;

    mutable ModifyingTaskPtr m_async_command   = nullptr;
    mutable bool             m_async_retrieved = false;

    mutable Box2i m_roi = Box2i();

    // various callback functions
    VoidVoidFunc m_async_modify_done_callback;
};

#define hdrview_image_icon(ctx, name, imageFlags)                                                                      \
    hdrview_get_icon(ctx, #name, imageFlags, (uint8_t *)name##_png, name##_png_size)
int hdrview_get_icon(NVGcontext *ctx, const std::string &name, int imageFlags, uint8_t *data, uint32_t size);
