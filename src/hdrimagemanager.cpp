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
	  m_imageModifyDoneCallback(std::function<void(int)>()),
	  m_numImagesCallback(std::function<void(void)>()),
	  m_currentImageCallback(std::function<void(void)>()),
	  m_referenceImageCallback(std::function<void(void)>())
{

}

void HDRImageManager::runRequestedCallbacks()
{
	if (m_imageModifyDoneRequested)
	{
		m_imageModifyDoneRequested = false;
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
	if (forceCallback || index != m_current)
	{
		m_current = index;
		m_currentImageCallback();
	}
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
	size_t numErrors = 0;
	vector<pair<string, bool> > loadedOK;
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
						throw std::runtime_error("Error getting file");

					if (!file.is_reg)
					{
						if (tinydir_next(&dir) == -1)
							throw std::runtime_error("Error getting next file");
						continue;
					}

					// only consider image files we support
					string ext = file.extension;
					transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
					if (!extensions.count(ext))
					{
						if (tinydir_next(&dir) == -1)
							throw std::runtime_error("Error getting next file");
						continue;
					}

					allFilenames.push_back(file.path);

					if (tinydir_next(&dir) == -1)
						throw std::runtime_error("Error getting next file");
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

	// now actually load the images
	Timer allTimer;
	for (auto i : allFilenames)
	{
		shared_ptr<GLImage> image = make_shared<GLImage>();
		Timer timer;
		if (image->load(i))
		{
			loadedOK.push_back({i, true});
			m_images.push_back(image);
			spdlog::get("console")->info("Loaded \"{}\" [{:d}x{:d}] in {} seconds", i, image->width(), image->height(), timer.elapsed() / 1000.f);
		}
		else
		{
			loadedOK.push_back({i, false});
			numErrors++;
		}
	}

	spdlog::get("console")->info("Loading all {:d} images took {} seconds", allFilenames.size(), allTimer.elapsed() / 1000.f);

	m_numImagesCallback();
	setCurrentImageIndex(int(m_images.size() - 1));

	if (numErrors)
	{
		string badFiles;
		for (size_t i = 0; i < loadedOK.size(); ++i)
		{
			if (!loadedOK[i].second)
				badFiles += loadedOK[i].first + "\n";
		}
		throw runtime_error(badFiles);
	}
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

void HDRImageManager::modifyImage(const ImageCommand & command)
{
	if (currentImage())
	{
		m_images[m_current]->asyncModify(
			[command, this](std::shared_ptr<HDRImage> img)
			{
				auto ret = command(img);
				m_imageModifyDoneRequested = true;
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
			[command, this](std::shared_ptr<HDRImage> img, AtomicProgress &progress)
			{
				auto ret = command(img, progress);
				m_imageModifyDoneRequested = true;
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

	std::swap(m_images[m_current], m_images[m_current-1]);
	m_current--;

	m_imageModifyDoneCallback(m_current);
	m_currentImageCallback();
}

void HDRImageManager::sendImageBackward()
{
	if (m_images.empty() || m_current == int(m_images.size()-1))
		// do nothing
		return;

	std::swap(m_images[m_current], m_images[m_current+1]);
	m_current++;

	m_imageModifyDoneCallback(m_current);
	m_currentImageCallback();
}