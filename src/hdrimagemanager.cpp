//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrimagemanager.h"
#include "timer.h"
#include <tinydir.h>
#include <spdlog/spdlog.h>
#include <set>

using namespace std;

HDRImageManager::HDRImageManager()
	: m_imageModifyDoneRequested(false),
	  m_imageModifyDoneCallback(function<void(int)>()),
	  m_numImagesCallback(function<void(void)>()),
	  m_currentImageCallback(function<void(void)>()),
	  m_referenceImageCallback(function<void(void)>())
{

}

void HDRImageManager::runRequestedCallbacks()
{
	if (m_imageModifyDoneRequested.exchange(false))
	{
		// remove any images that are not being modified and are null
		bool numImagesChanged = false;

		// iterate through the images, and remove the ones that didn't load properly
		auto it = m_images.begin();
		while (it != m_images.end())
		{
			int i = it - m_images.begin();
			auto img = m_images[i];
			if (img && img->canModify() && img->isNull())
			{
				it = m_images.erase(it);

				if (i < m_current)
					m_current--;
				else if (m_current >= int(m_images.size()))
					m_current = m_images.size() - 1;

				numImagesChanged = true;
			}
			else
				++it;
		}

		if (numImagesChanged)
		{
			m_currentImageCallback();
			m_numImagesCallback();
		}

		m_imageModifyDoneCallback(m_current);
	}
}

shared_ptr<const GLImage> HDRImageManager::image(int index) const
{
	return (index < 0 || index >= int(m_images.size())) ? nullptr : m_images[index];
}

shared_ptr<GLImage> HDRImageManager::image(int index)
{
	return (index < 0 || index >= int(m_images.size())) ? nullptr : m_images[index];
}

void HDRImageManager::setCurrentImageIndex(int index, bool forceCallback)
{
	if (index != m_current)
	{
		m_current = index;
		forceCallback = true;
	}

	if (forceCallback)
		m_currentImageCallback();
}

void HDRImageManager::setReferenceImageIndex(int index, bool forceCallback)
{
	if (forceCallback || index != m_reference)
	{
		m_reference = index;
		m_referenceImageCallback();
	}
}

void HDRImageManager::loadImages(const vector<string> & filenames)
{
	vector<string> allFilenames;

	const static set<string> extensions = {"exr", "png", "jpg", "jpeg", "hdr", "pic", "pfm", "ppm", "bmp", "tga", "psd"};

	// first just assemble all the images we will need to load by traversing any directories
	for (auto i : filenames)
	{
		tinydir_dir dir;
		if (tinydir_open(&dir, i.c_str()) != -1)
		{
			try
			{
				// filename is actually a directory, traverse it
				spdlog::get("console")->info("Loading images in \"{}\"...", dir.path);
				while (dir.has_next)
				{
					tinydir_file file;
					if (tinydir_readfile(&dir, &file) == -1)
						throw runtime_error("Error getting file");

					if (!file.is_reg)
					{
						if (tinydir_next(&dir) == -1)
							throw runtime_error("Error getting next file");
						continue;
					}

					// only consider image files we support
					string ext = file.extension;
					transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
					if (!extensions.count(ext))
					{
						if (tinydir_next(&dir) == -1)
							throw runtime_error("Error getting next file");
						continue;
					}

					allFilenames.push_back(file.path);

					if (tinydir_next(&dir) == -1)
						throw runtime_error("Error getting next file");
				}

				tinydir_close(&dir);
			}
			catch (const exception & e)
			{
				spdlog::get("console")->error("Error listing directory: ({}).", e.what());
			}
		}
		else
		{
			allFilenames.push_back(i);
		}
		tinydir_close(&dir);
	}

	// now start a bunch of asynchronous image loads
	for (auto filename : allFilenames)
	{
		shared_ptr<GLImage> image = make_shared<GLImage>();
		image->setImageModifyDoneCallback([this](){m_imageModifyDoneRequested = true;});
		image->setFilename(filename);
		image->asyncModify(
			[this,filename](const shared_ptr<const HDRImage> &) -> ImageCommandResult
			{
				Timer timer;
				spdlog::get("console")->info("Trying to load image \"{}\"", filename);
				shared_ptr<HDRImage> ret = loadImage(filename);
				if (ret)
					spdlog::get("console")->info("Loaded \"{}\" [{:d}x{:d}] in {} seconds", filename, ret->width(), ret->height(), timer.elapsed() / 1000.f);
				else
					spdlog::get("console")->info("Loading \"{}\" failed", filename);
				return {ret, nullptr};
			});
		m_images.emplace_back(image);
		m_imageModifyStartCallback(m_images.size()-1);
	}

	m_numImagesCallback();
	setCurrentImageIndex(int(m_images.size() - 1));
}

void HDRImageManager::saveImage(const string & filename, float exposure, float gamma, bool sRGB, bool dither)
{
	if (!currentImage())
		return;

	if (filename.size())
	{
		currentImage()->save(filename, powf(2.0f, exposure), gamma, sRGB, dither);
		m_imageModifyDoneCallback(m_current);
	}
}

void HDRImageManager::closeImage(int index)
{
	if (image(index))
	{
		m_images.erase(m_images.begin() + index);

		int newIndex = m_current;
		if (index < m_current)
			newIndex--;
		else if (m_current >= int(m_images.size()))
			newIndex = m_images.size() - 1;

		setCurrentImageIndex(newIndex, true);
		m_numImagesCallback();
	}
}

void HDRImageManager::closeAllImages()
{
	m_images.clear();

	m_current = -1;
	m_reference = -1;

	m_currentImageCallback();
	m_numImagesCallback();
}

void HDRImageManager::modifyImage(const ImageCommand & command)
{
	if (currentImage())
	{
		m_images[m_current]->asyncModify(
			[command, this](const shared_ptr<const HDRImage> & img)
			{
				auto ret = command(img);

				// if no undo was provided, just create a FullImageUndo
				if (!ret.second)
					ret.second = make_shared<FullImageUndo>(*img);

				return ret;
			});
		m_imageModifyStartCallback(m_current);
	}
}

void HDRImageManager::modifyImage(const ImageCommandWithProgress & command)
{
	if (currentImage())
	{
		m_images[m_current]->asyncModify(
			[command, this](const shared_ptr<const HDRImage> & img, AtomicProgress &progress)
			{
				auto ret = command(img, progress);

				// if no undo was provided, just create a FullImageUndo
				if (!ret.second)
					ret.second = make_shared<FullImageUndo>(*img);

				return ret;
			});
		m_imageModifyStartCallback(m_current);
	}
}

void HDRImageManager::undo()
{
	if (currentImage() && m_images[m_current]->undo())
		m_imageModifyDoneCallback(m_current);
}

void HDRImageManager::redo()
{
	if (currentImage() && m_images[m_current]->redo())
		m_imageModifyDoneCallback(m_current);
}

void HDRImageManager::bringImageForward()
{
	if (m_images.empty() || m_current == 0)
		// do nothing
		return;

	swap(m_images[m_current], m_images[m_current-1]);
	m_current--;

	m_imageModifyDoneCallback(m_current);
	m_currentImageCallback();
}

void HDRImageManager::sendImageBackward()
{
	if (m_images.empty() || m_current == int(m_images.size()-1))
		// do nothing
		return;

	swap(m_images[m_current], m_images[m_current+1]);
	m_current++;

	m_imageModifyDoneCallback(m_current);
	m_currentImageCallback();
}