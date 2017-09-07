//
// Created by Wojciech Jarosz on 9/4/17.
//

#include "hdrimagemanager.h"
#include <tinydir.h>
#include <spdlog/spdlog.h>

using namespace std;
using namespace nanogui;

HDRImageManager::HDRImageManager()
	: m_imageChangedCallback(std::function<void(int)>()),
	  m_numImagesCallback(std::function<void(void)>()),
	  m_imageSelectedCallback(std::function<void(int)>())
{

}

const GLImage * HDRImageManager::currentImage() const
{
	return image(m_current);
}

GLImage * HDRImageManager::currentImage()
{
	return image(m_current);
}

const GLImage * HDRImageManager::image(int index) const
{
	return (index < 0 || index >= int(m_images.size())) ? nullptr : m_images[index];
}

GLImage * HDRImageManager::image(int index)
{
	return (index < 0 || index >= int(m_images.size())) ? nullptr : m_images[index];
}

void HDRImageManager::selectImage(int index, bool forceCallback)
{
	if (forceCallback || index != m_current)
	{
		m_current = index;
		m_imageSelectedCallback(m_current);
	}
}

void HDRImageManager::loadImages(const vector<string> & filenames)
{
	size_t numErrors = 0;
	vector<pair<string, bool> > loadedOK;
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
					if (ext != "exr" && ext != "png" && ext != "jpg" &&
						ext != "jpeg" && ext != "hdr" && ext != "pic" &&
						ext != "pfm" && ext != "ppm" && ext != "bmp" &&
						ext != "tga" && ext != "psd")
					{
						if (tinydir_next(&dir) == -1)
							throw std::runtime_error("Error getting next file");
						continue;
					}

					GLImage* image = new GLImage();
					if (image->load(file.path))
					{
						loadedOK.push_back({file.path, true});
						image->init();
						m_images.push_back(image);
						spdlog::get("console")->info("Loaded \"{}\" [{:d}x{:d}]", file.name,
						                             image->width(), image->height());
					}
					else
					{
						loadedOK.push_back({file.name, false});
						numErrors++;
						delete image;
					}

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
			GLImage* image = new GLImage();
			if (image->load(i))
			{
				loadedOK.push_back({i, true});
				image->init();
				m_images.push_back(image);
				spdlog::get("console")->info("Loaded \"{}\" [{:d}x{:d}]", i, image->width(), image->height());
			}
			else
			{
				loadedOK.push_back({i, false});
				numErrors++;
				delete image;
			}
		}
		tinydir_close(&dir);
	}

	m_numImagesCallback();
	selectImage(int(m_images.size() - 1));

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
		m_imageChangedCallback(m_current);
	}
}

void HDRImageManager::closeImage(int index)
{
	if (image(index))
	{
		delete m_images[index];
		m_images.erase(m_images.begin() + index);

		int newIndex = m_current;
		if (index < m_current)
			newIndex--;
		else if (m_current >= int(m_images.size()))
			newIndex = m_images.size() - 1;

		selectImage(newIndex, true);
		m_numImagesCallback();
	}
}

void HDRImageManager::modifyImage(const std::function<ImageCommandUndo*(HDRImage & img)> & command)
{
	if (currentImage())
	{
		m_images[m_current]->modify(command);
		m_imageChangedCallback(m_current);
	}
}

void HDRImageManager::undo()
{
	if (currentImage() && m_images[m_current]->undo())
		m_imageChangedCallback(m_current);
}

void HDRImageManager::redo()
{
	if (currentImage() && m_images[m_current]->redo())
		m_imageChangedCallback(m_current);
}

void HDRImageManager::bringImageForward()
{
	if (m_images.empty() || m_current == 0)
		// do nothing
		return;

	std::swap(m_images[m_current], m_images[m_current-1]);
	m_current--;

	m_imageChangedCallback(m_current);
	m_imageSelectedCallback(m_current);
}

void HDRImageManager::sendImageBackward()
{
	if (m_images.empty() || m_current == int(m_images.size()-1))
		// do nothing
		return;

	std::swap(m_images[m_current], m_images[m_current+1]);
	m_current++;

	m_imageChangedCallback(m_current);
	m_imageSelectedCallback(m_current);
}