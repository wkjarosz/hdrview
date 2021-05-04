//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "glimage.h"
#include "common.h"
#include "timer.h"
#include "colorspace.h"
#include "parallelfor.h"
#include <random>
#include <nanogui/common.h>
#include <cmath>
#include <spdlog/spdlog.h>
#include "multigraph.h"

using namespace nanogui;
// using namespace Eigen;
using namespace std;

shared_ptr<ImageStatistics> ImageStatistics::computeStatistics(const HDRImage &img, float exposure)
{
	static const int numBins = 256;
	static const int numTicks = 8;
	float displayMax = pow(2.f, -exposure);

	auto ret = make_shared<ImageStatistics>();
	for (int i = 0; i < ENumAxisScales; ++i)
	{
		ret->histogram[i].values[0] = vector<float>(numBins, 0);
		ret->histogram[i].values[1] = vector<float>(numBins, 0);
		ret->histogram[i].values[2] = vector<float>(numBins, 0);
	}

	ret->exposure = exposure;
	ret->average = 0;

	ret->maximum = img.max().Color3::max();
	ret->minimum = img.min().Color3::min();

	Color4 gain(pow(2.f, exposure), 1.f);
	float d = 1.f / (img.width() * img.height());

	for (Eigen::DenseIndex i = 0; i < img.size(); ++i)
    {
        Color4 val = gain * img(i);
        ret->average += val[0] + val[1] + val[2];

        for (int c = 0; c < 3; ++c)
        {
	        ret->histogram[ELinear].values[c][::clamp(int(floor(val[c] * numBins)), 0, numBins - 1)] += d;
	        ret->histogram[ESRGB].values[c][::clamp(int(floor(LinearToSRGB(val[c]) * numBins)), 0, numBins - 1)] += d;
	        ret->histogram[ELog].values[c][::clamp(int(floor(normalizedLogScale(val[c]) * numBins)), 0, numBins - 1)] += d;
        }
    }

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
			for_each(ret->histogram[i].values[c].begin(), ret->histogram[i].values[c].end(), [s](float & v){v /= s;});
	}

	// create the tick marks
	auto ticks = linspaced(numTicks+1, 0.0f, 1.0f);
	ret->histogram[ELinear].xTicks = ticks;
	ret->histogram[ESRGB].xTicks = ticks;
	ret->histogram[ELog].xTicks = ticks;

	for_each(ret->histogram[ESRGB].xTicks.begin(), ret->histogram[ESRGB].xTicks.end(), [](float & v){v = LinearToSRGB(v);});
	for_each(ret->histogram[ELog].xTicks.begin(), ret->histogram[ELog].xTicks.end(), [](float & v){v = normalizedLogScale(v);});

	// create the tick labels
	auto & hist = ret->histogram[ELinear];
	hist.xTickLabels.resize(numTicks + 1);
	for (int i = 0; i <= numTicks; ++i)
		hist.xTickLabels[i] = fmt::format("{:.3f}", displayMax * hist.xTicks[i]);
	ret->histogram[ESRGB].xTickLabels = ret->histogram[ELog].xTickLabels = hist.xTickLabels;

	return ret;
}

GLImage::GLImage() :
    m_image(make_shared<HDRImage>()),
    m_filename(),
    m_cached_histogram_exposure(NAN),
    m_histogram_dirty(true),
    m_modify_done_callback(GLImage::VoidVoidFunc())
{
    m_texture = new Texture(Texture::PixelFormat::RGBA,
		Texture::ComponentFormat::Float32,
		nanogui::Vector2i(1, 1),
		Texture::InterpolationMode::Trilinear,
		Texture::InterpolationMode::Nearest,
		Texture::WrapMode::Repeat);
}


GLImage::~GLImage()
{

}

float GLImage::progress() const
{
	check_async_result();
	return m_async_command ? m_async_command->progress() : 1.0f;
}
bool GLImage::is_modified() const    { check_async_result(); return m_history.is_modified(); }
bool GLImage::has_undo() const       { check_async_result(); return m_history.has_undo(); }
bool GLImage::has_redo() const       { check_async_result(); return m_history.has_redo(); }

bool GLImage::can_modify() const
{
	return !m_async_command;
}

void GLImage::async_modify(const ImageCommandWithProgress & command)
{
	// make sure any pending edits are done
	wait_for_async_result();

	m_async_command = make_shared<AsyncTask<ImageCommandResult>>([this,command](AtomicProgress & prog){return command(m_image, prog);});
	m_async_retrieved = false;
	m_async_command->compute();
}

void GLImage::async_modify(const ImageCommand &command)
{
	// make sure any pending edits are done
	wait_for_async_result();

	m_async_command = make_shared<AsyncTask<ImageCommandResult>>([this,command](void){return command(m_image);});
	m_async_retrieved = false;
	m_async_command->compute();
}

bool GLImage::undo()
{
	// make sure any pending edits are done
	wait_for_async_result();

	if (m_history.undo(m_image))
	{
		m_histogram_dirty = true;
		m_texture_dirty = true;
		upload_to_GPU();
		return true;
	}
	return false;
}

bool GLImage::redo()
{
	// make sure any pending edits are done
	wait_for_async_result();

	if (m_history.redo(m_image))
	{
		m_histogram_dirty = true;
		m_texture_dirty = true;
		upload_to_GPU();
		return true;
	}
	return false;
}

bool GLImage::check_async_result() const
{
	if (!m_async_command || !m_async_command->ready())
		return false;

	return wait_for_async_result();
}

void GLImage::modify_done() const
{
	m_async_command = nullptr;
	if (m_modify_done_callback)
		m_modify_done_callback();
}


bool GLImage::wait_for_async_result() const
{
	// nothing to wait for
	if (!m_async_command)
		return false;

	if (!m_async_retrieved)
	{
		// now retrieve the result and copy it out of the async task
		auto result = m_async_command->get();

		// if there is no undo, treat this as an image load
		if (!result.second)
		{
			if (result.first)
			{
				m_history = CommandHistory();
				m_image = result.first;
			}
		}
		else
		{
			m_history.add_command(result.second);
			m_image = result.first;
		}

		m_async_retrieved = true;
		m_histogram_dirty = true;
		m_texture_dirty = true;

		if (!result.first)
		{
			// image loading failed
			modify_done();
			return false;
		}
	}

	// now set the progress bar to busy as we upload to GPU
	m_async_command->set_progress(-1.f);

	upload_to_GPU();

	return true;
}


void GLImage::upload_to_GPU() const
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
	spdlog::get("console")->trace("Uploading texture to GPU took {} ms", timer.lap());

	// now that we grabbed the results and uploaded to GPU, destroy the task
	modify_done();
}


GLImage::TextureRef GLImage::texture()
{
	check_async_result();
	upload_to_GPU();
    return m_texture;
}


bool GLImage::save(const std::string & filename,
                   float gain, float gamma,
                   bool sRGB, bool dither) const
{
	// make sure any pending edits are done
	wait_for_async_result();

    if (!m_image->save(filename, gain, gamma, sRGB, dither))
    	return false;

	m_history.mark_saved();

    return true;
}

void GLImage::recompute_histograms(float exposure) const
{
	check_async_result();

    if ((!m_histograms || m_histogram_dirty || exposure != m_cached_histogram_exposure) && !m_image->is_null())
    {
        m_histograms = make_shared<LazyHistogram>(
	        [this,exposure](void)
	        {
		        return ImageStatistics::computeStatistics(*m_image, exposure);
	        });
        m_histograms->compute();
        m_histogram_dirty = false;
        m_cached_histogram_exposure = exposure;
    }
}