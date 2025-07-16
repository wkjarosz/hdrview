#include "image_loader.h"
#include "image.h"
#include <fstream>

using namespace std;
namespace fs = std::filesystem;

using ImageLoadTask = AsyncTask<std::vector<ImagePtr>>;

static constexpr size_t g_max_recent = 15;

struct BackgroundImageLoader::PendingImages
{
    std::string   filename;
    ImageLoadTask images;
    bool          add_to_recent;           ///< Whether to add the loaded images to the recent files list
    bool          should_select = false;   ///< Whether to select the first loaded image
    ImagePtr      to_replace    = nullptr; ///< If not null, this image will be replaced with the loaded images
    PendingImages(const std::string &f, ImageLoadTask::NoProgressTaskFunc func, bool recent = true,
                  bool should_select = false, ImagePtr to_replace = nullptr) :
        filename(f), images(func), add_to_recent(recent), should_select(should_select), to_replace(to_replace)
    {
        images.compute();
    }
};

void BackgroundImageLoader::load_recent_file(int index)
{
    int idx = int(m_recent_files.size()) - 1 - index;
    if (idx >= 0 && idx < int(m_recent_files.size()))
        background_load(m_recent_files[idx], {}, true);
}

void BackgroundImageLoader::add_recent_file(const string &f)
{
    m_recent_files.push_back(f);
    if (m_recent_files.size() > g_max_recent)
        m_recent_files.erase(m_recent_files.begin(), m_recent_files.end() - g_max_recent);
}

void BackgroundImageLoader::remove_recent_file(const string &f)
{
    m_recent_files.erase(std::remove(m_recent_files.begin(), m_recent_files.end(), f), m_recent_files.end());
}

vector<string> BackgroundImageLoader::recent_files_short(int head_length, int tail_length) const
{
    auto           total_length = head_length + tail_length + 3;
    vector<string> short_names;
    short_names.reserve(m_recent_files.size());
    for (auto f = m_recent_files.rbegin(); f != m_recent_files.rend(); ++f)
        short_names.push_back(((int)f->length() < total_length)
                                  ? *f
                                  : f->substr(0, head_length) + "..." + f->substr(f->length() - tail_length));
    return short_names;
}

void BackgroundImageLoader::background_load(const string filename, const string_view buffer, bool should_select,
                                            ImagePtr to_replace)
{
    if (should_select)
        spdlog::debug("will select image '{}'", filename);
    auto load_one = [this](const fs::path &path, const string_view buffer, bool add_to_recent, bool should_select,
                           ImagePtr to_replace)
    {
        try
        {
            if (should_select)
                spdlog::debug("will select image file'{}'", path.u8string());
            spdlog::info("Loading file '{}'...", path.u8string());
            // convert the buffer (if any) to a string so the async thread has its own copy,
            // then load from the string or filename depending on whether the buffer is empty
            pending_images.emplace_back(std::make_shared<PendingImages>(
                path.u8string(),
                [buffer_str = string(buffer), path]() -> vector<ImagePtr>
                {
                    vector<ImagePtr>   images{};
                    fs::file_time_type last_modified = fs::file_time_type::clock::now();
                    if (buffer_str.empty())
                    {
                        if (fs::exists(path))
                        {
                            try
                            {
                                last_modified = fs::last_write_time(path);
                            }
                            catch (...)
                            {
                            }
                        }

                        if (std::ifstream is{path, std::ios_base::binary})
                            images = Image::load(is, path.u8string());
                        else
                        {
                            spdlog::error("File '{}' doesn't exist.", path.u8string());
                            return vector<ImagePtr>{};
                        }
                    }
                    else
                    {
                        std::istringstream is{buffer_str};
                        images = Image::load(is, path.u8string());
                    }

                    for (auto &img : images)
                    {
                        img->last_modified = last_modified;
                        img->path          = path;
                    }
                    return images;
                },
                add_to_recent, should_select, to_replace));
        }
        catch (const std::exception &e)
        {
            spdlog::error("Could not load image \"{}\": {}.", path.u8string(), e.what());
            return;
        }
    };

    auto path = fs::u8path(filename.c_str());
    if (!fs::exists(path))
        spdlog::error("File '{}' does not exist.", filename);
    else if (fs::is_directory(path))
    {
        spdlog::info("Loading images from folder '{}'", path.u8string());

        std::error_code ec;
        auto            canon_p = fs::canonical(path);
        mDirectories.emplace(canon_p);

        vector<fs::directory_entry> entries;
        for (auto const &entry : fs::directory_iterator{canon_p, ec})
        {
            auto ext           = to_lower(get_extension(entry.path().filename().u8string()));
            bool supported_ext = Image::loadable_formats().find(ext) != Image::loadable_formats().end();
            if (!entry.is_directory() && supported_ext)
            {
                mFilesFoundInDirectories.emplace(entry);
                entries.emplace_back(entry);
            }
        }

        sort(begin(entries), end(entries),
             [](const auto &a, const auto &b) { return natural_less(a.path().string(), b.path().string()); });

        for (size_t i = 0; i < entries.size(); ++i)
            load_one(entries[i].path(), buffer, false, i == 0 ? should_select : false, to_replace);

        remove_recent_file(filename);
        add_recent_file(filename);
    }
    else if (!buffer.empty())
    {
        // if we have a buffer, we assume it is a file that has been downloaded
        // and we load it directly from the buffer
        spdlog::info("Loading image from buffer with size {} bytes", buffer.size());
        load_one(path, buffer, true, should_select, to_replace);
    }
    else if (fs::exists(path) && fs::is_regular_file(path))
    {
        // remove any instances of filename from the recent files list until we know it has loaded successfully
        remove_recent_file(filename);
        load_one(filename, buffer, true, should_select, to_replace);
    }
}

void BackgroundImageLoader::get_loaded_images(function<void(ImagePtr, ImagePtr, bool)> callback)
{
    // move elements matching the criterion to the end of the vector, and then erase all matching elements
    pending_images.erase(std::remove_if(pending_images.begin(), pending_images.end(),
                                        [this, &callback](shared_ptr<PendingImages> p)
                                        {
                                            if (p->images.ready())
                                            {
                                                // get the result, add any loaded images, and report that we can
                                                // remove this task
                                                auto new_images = p->images.get();
                                                if (new_images.empty())
                                                    return true;

                                                for (size_t i = 0; i < new_images.size(); ++i)
                                                    callback(new_images[i], p->to_replace, p->should_select);

                                                // if loading was successful, add the filename to the recent list and
                                                // limit to g_max_recent files
                                                if (p->add_to_recent)
                                                    add_recent_file(p->filename);

                                                return true;
                                            }
                                            else
                                                return false;
                                        }),
                         pending_images.end());
}

void BackgroundImageLoader::load_new_files()
{
    std::error_code ec;
    for (const auto &dir : mDirectories)
        for (auto const &entry : fs::directory_iterator{dir, ec})
        {
            if (entry.is_directory())
                continue;

            const auto p = entry.path();
            if (!mFilesFoundInDirectories.count(p))
            {
                mFilesFoundInDirectories.emplace(p);
                background_load(p.u8string());
            }
        }
}