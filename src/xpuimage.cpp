//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "xpuimage.h"
#include "colorspace.h"
#include "common.h"
#include "multigraph.h"
#include "parallelfor.h"
#include "timer.h"
#include <cmath>
#include <map>
#include <nanogui/common.h>
#include <nanovg.h>
#include <random>
#include <spdlog/spdlog.h>

using namespace nanogui;
using namespace std;

shared_ptr<ImageStatistics> ImageStatistics::compute_statistics(const HDRImage &img, float exposure,
                                                                AtomicProgress &prog)
{
    static const int numBins  = 256;
    static const int numTicks = 8;

    try
    {
        auto  ret        = make_shared<ImageStatistics>();
        float displayMax = pow(2.f, -exposure);

        for (int i = 0; i < ENumAxisScales; ++i)
        {
            ret->histogram[i].values[0] = vector<float>(numBins, 0);
            ret->histogram[i].values[1] = vector<float>(numBins, 0);
            ret->histogram[i].values[2] = vector<float>(numBins, 0);
        }

        ret->exposure = exposure;
        ret->average  = 0;

        ret->maximum = img.max().Color3::max();
        ret->minimum = img.min().Color3::min();

        Color4 gain(pow(2.f, exposure), 1.f);
        float  d = 1.f / (img.width() * img.height());

        for (int i = 0; i < img.size(); ++i)
        {
            if (prog.canceled())
                throw std::exception();

            ret->average += img(i)[0] + img(i)[1] + img(i)[2];
            Color4 val = gain * img(i);

            for (int c = 0; c < 3; ++c)
            {
                ret->histogram[ELinear].values[c][::clamp(int(floor(val[c] * numBins)), 0, numBins - 1)] += d;
                ret->histogram[ESRGB].values[c][::clamp(int(floor(LinearToSRGB(val[c]) * numBins)), 0, numBins - 1)] +=
                    d;
                ret->histogram[ELog]
                    .values[c][::clamp(int(floor(normalizedLogScale(val[c]) * numBins)), 0, numBins - 1)] += d;
            }
        }

        if (prog.canceled())
            throw std::exception();

        ret->average /= 3 * img.width() * img.height();

        // Normalize each histogram according to its 10th-largest bin
        for (int i = 0; i < ENumAxisScales; ++i)
        {
            vector<float> temp;
            for (int c = 0; c < 3; ++c)
            {
                auto h = ret->histogram[i].values[c];
                temp.insert(temp.end(), h.begin(), h.end());
            }

            auto idx = temp.size() - 10;
            nth_element(temp.begin(), temp.begin() + idx, temp.end());
            float s = temp[idx];

            for (int c = 0; c < 3; ++c)
            {
                if (prog.canceled())
                    throw std::exception();
                for_each(ret->histogram[i].values[c].begin(), ret->histogram[i].values[c].end(),
                         [s](float &v) { v /= s; });
            }
        }

        if (prog.canceled())
            throw std::exception();

        // create the tick marks
        auto ticks                     = linspaced(numTicks + 1, 0.0f, 1.0f);
        ret->histogram[ELinear].xTicks = ticks;
        ret->histogram[ESRGB].xTicks   = ticks;
        ret->histogram[ELog].xTicks    = ticks;

        for_each(ret->histogram[ESRGB].xTicks.begin(), ret->histogram[ESRGB].xTicks.end(),
                 [](float &v) { v = LinearToSRGB(v); });
        for_each(ret->histogram[ELog].xTicks.begin(), ret->histogram[ELog].xTicks.end(),
                 [](float &v) { v = normalizedLogScale(v); });

        // create the tick labels
        auto &hist = ret->histogram[ELinear];
        hist.xTickLabels.resize(numTicks + 1);
        for (int i = 0; i <= numTicks; ++i) hist.xTickLabels[i] = fmt::format("{:.3f}", displayMax * hist.xTicks[i]);
        ret->histogram[ESRGB].xTickLabels = ret->histogram[ELog].xTickLabels = hist.xTickLabels;

        return ret;
    }
    catch (const std::exception &e)
    {
        spdlog::trace("Interrupting histogram computation");
        return nullptr;
    }
}

XPUImage::XPUImage(bool modified) :
    m_image(make_shared<HDRImage>()), m_filename(), m_cached_histogram_exposure(NAN), m_histogram_dirty(true),
    m_history(modified), m_async_modify_done_callback(XPUImage::VoidVoidFunc())
{
    m_texture = new Texture(Texture::PixelFormat::RGBA, Texture::ComponentFormat::Float32, nanogui::Vector2i(1, 1),
                            Texture::InterpolationMode::Trilinear, Texture::InterpolationMode::Nearest,
                            Texture::WrapMode::Repeat);
}

XPUImage::~XPUImage() { cancel_histograms(); }

float XPUImage::progress() const
{
    check_async_result();
    return m_async_command ? m_async_command->progress() : 1.0f;
}
bool XPUImage::is_modified() const
{
    check_async_result();
    return m_history.is_modified();
}
bool XPUImage::has_undo() const
{
    check_async_result();
    return m_history.has_undo();
}
bool XPUImage::has_redo() const
{
    check_async_result();
    return m_history.has_redo();
}

bool XPUImage::can_modify() const { return !m_async_command; }

void XPUImage::async_modify(const ConstImageCommandWithProgress &command)
{
    // make sure any pending edits are done
    wait_for_async_result();

    m_async_command   = make_shared<ModifyingTask>([this, command](AtomicProgress &p)
                                                 { return command(m_image, shared_from_this(), p); });
    m_async_retrieved = false;
    m_async_command->compute();
}

void XPUImage::async_modify(const ConstImageCommand &command)
{
    // make sure any pending edits are done
    wait_for_async_result();

    m_async_command =
        make_shared<ModifyingTask>([this, command](void) { return command(m_image, shared_from_this()); });
    m_async_retrieved = false;
    m_async_command->compute();
}

void XPUImage::direct_modify(const ImageCommand &command)
{
    // make sure any pending edits are done
    wait_for_async_result();

    command(m_image);

    m_texture_dirty = true;

    upload_to_GPU();
}

void XPUImage::start_modify(const ConstImageCommand &command)
{
    // make sure any pending edits are done
    wait_for_async_result();

    auto result = command(m_image, shared_from_this());

    if (!result.second)
    {
        // if there is no undo, treat this as a continuation of the previous edit
        // so don't create a new undo record
        if (result.first)
            m_image = result.first;
    }
    else
    {
        m_history.add_command(result.second);
        m_image = result.first;
    }

    m_texture_dirty = true;

    upload_to_GPU();
}

bool XPUImage::undo()
{
    // make sure any pending edits are done
    wait_for_async_result();

    if (m_history.undo(m_image))
    {
        m_histogram_dirty = true;
        m_texture_dirty   = true;
        upload_to_GPU();
        return true;
    }
    return false;
}

bool XPUImage::redo()
{
    // make sure any pending edits are done
    wait_for_async_result();

    if (m_history.redo(m_image))
    {
        m_histogram_dirty = true;
        m_texture_dirty   = true;
        upload_to_GPU();
        return true;
    }
    return false;
}

bool XPUImage::check_async_result() const
{
    if (!m_async_command || !m_async_command->ready())
        return false;

    return wait_for_async_result();
}

void XPUImage::async_modify_done() const
{
    m_async_command = nullptr;
    if (m_async_modify_done_callback)
        m_async_modify_done_callback();
}

bool XPUImage::wait_for_async_result() const
{
    // nothing to wait for
    if (!m_async_command)
        return false;

    if (!m_async_retrieved)
    {
        // first cancel and wait for any histogram task to finish if present
        cancel_histograms();

        // now retrieve the result and copy it out of the async task
        auto result = m_async_command->get();

        // if there is no undo, treat this as an image load
        if (!result.second)
        {
            if (result.first)
            {
                m_history = CommandHistory(m_history.is_modified());
                m_image   = result.first;
            }
        }
        else
        {
            m_history.add_command(result.second);
            m_image = result.first;
        }

        m_async_retrieved = true;
        m_histogram_dirty = true;
        m_texture_dirty   = true;

        if (!result.first)
        {
            // image loading failed
            async_modify_done();
            return false;
        }
    }

    // now set the progress bar to busy as we upload to GPU
    m_async_command->set_progress(-1.f);

    upload_to_GPU();

    return true;
}

void XPUImage::upload_to_GPU() const
{
    if (m_image->is_null())
    {
        m_texture_dirty = false;
        return;
    }

    // check if we need to upload the image to the GPU
    if (!m_texture_dirty && m_texture)
        return;

    Timer timer;

    auto s = nanogui::Vector2i(m_image->width(), m_image->height());
    m_texture->resize(s);
    m_texture->upload((const uint8_t *)m_image->data());
    m_texture_dirty = false;
    spdlog::trace("Uploading texture to GPU took {} ms", timer.lap());

    // now that we grabbed the results and uploaded to GPU, destroy the task
    async_modify_done();
}

XPUImage::TextureRef XPUImage::texture() const
{
    check_async_result();
    upload_to_GPU();
    return m_texture;
}

bool XPUImage::save(const std::string &filename, float gain, float gamma, bool sRGB, bool dither) const
{
    // make sure any pending edits are done
    wait_for_async_result();

    if (!m_image->save(filename, gain, gamma, sRGB, dither))
        return false;

    m_history.mark_saved();

    return true;
}

void XPUImage::cancel_histograms() const
{
    // try to cancel the histogram task if any first
    if (m_histograms)
    {
        m_histograms->cancel();
        m_histograms->get();
    }
}

void XPUImage::recompute_histograms(float exposure) const
{
    check_async_result();

    if ((!m_histograms || m_histogram_dirty || exposure != m_cached_histogram_exposure) && !m_image->is_null())
    {
        m_histograms =
            make_shared<HistogramTask>([this, exposure](AtomicProgress &prog)
                                       { return ImageStatistics::compute_statistics(*m_image, exposure, prog); });
        m_histograms->compute();
        m_histogram_dirty           = false;
        m_cached_histogram_exposure = exposure;
    }
}

int hdrview_get_icon(NVGcontext *ctx, const std::string &name, int imageFlags, uint8_t *data, uint32_t size)
{
    static std::map<std::string, int> icon_cache;
    auto                              it = icon_cache.find(name);
    if (it != icon_cache.end())
        return it->second;
    int icon_id = nvgCreateImageMem(ctx, imageFlags, data, size);
    if (icon_id == 0)
        throw std::runtime_error("Unable to load resource data.");
    icon_cache[name] = icon_id;
    return icon_id;
}