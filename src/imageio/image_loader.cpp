#include "image_loader.h"
#include "image.h"
#include <fstream>

using namespace std;
namespace fs = std::filesystem;

static constexpr size_t g_max_recent = 15;

struct BackgroundImageLoader::PendingImages
{
    string                 filename;
    Scheduler::TaskTracker computation;
    vector<ImagePtr>       images;
    bool                   add_to_recent;           ///< Whether to add the loaded images to the recent files list
    bool                   should_select = false;   ///< Whether to select the first loaded image
    ImagePtr               to_replace    = nullptr; ///< If not null, this image will be replaced with the loaded images
    PendingImages(const string &f, const string_view buffer, const fs::path &path, const string &channel_selector,
                  bool recent = true, bool should_select = false, ImagePtr to_replace = nullptr) :
        filename(f), add_to_recent(recent), should_select(should_select), to_replace(to_replace)
    {
        computation = do_async(
            // convert the buffer (if any) to a string so the async thread has its own copy,
            // then load from the string or filename depending on whether the buffer is empty
            [this, buffer_str = string(buffer), path, channel_selector]()
            {
                fs::file_time_type last_modified = fs::file_time_type::clock::now();
                if (buffer_str.empty())
                {
                    if (!fs::exists(path))
                    {
                        spdlog::error("File '{}' doesn't exist.", path.u8string());
                        return;
                    }

                    try
                    {
                        last_modified = fs::last_write_time(path);
                    }
                    catch (...)
                    {
                    }

                    if (std::ifstream is{path, std::ios_base::binary})
                        images = Image::load(is, path.u8string(), channel_selector);
                    else
                    {
                        spdlog::error("File '{}' doesn't exist.", path.u8string());
                        return;
                    }
                }
                else
                {
                    std::istringstream is{buffer_str};
                    images = Image::load(is, path.u8string(), channel_selector);
                }

                for (auto &img : images)
                {
                    img->last_modified = last_modified;
                    img->path          = path;
                }
            });
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
    auto it = std::find(m_recent_files.begin(), m_recent_files.end(), f);
    if (it != m_recent_files.end())
        m_recent_files.erase(it);

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
                                            ImagePtr to_replace, const string &channel_selector)
{
    if (should_select)
        spdlog::debug("will select image '{}'", filename);
    auto load_one = [this](const fs::path &path, const string_view buffer, bool add_to_recent, bool should_select,
                           ImagePtr to_replace, const string &channel_selector)
    {
        try
        {
            spdlog::info("Loading file '{}'...", path.u8string());
            pending_images.emplace_back(std::make_shared<PendingImages>(path.u8string(), buffer, path, channel_selector,
                                                                        add_to_recent, should_select, to_replace));
        }
        catch (const std::exception &e)
        {
            spdlog::error("Could not load image \"{}\": {}.", path.u8string(), e.what());
            return;
        }
    };

    auto path = fs::u8path(filename.c_str());
    if (!buffer.empty())
    {
        // if we have a buffer, we assume it is a file that has been downloaded
        // and we load it directly from the buffer
        spdlog::info("Loading image from buffer with size {} bytes", buffer.size());
        load_one(path, buffer, true, should_select, to_replace, channel_selector);
    }
    else if (!fs::exists(path))
        spdlog::error("File '{}' does not exist.", filename);
#if !defined(__EMSCRIPTEN__)
    else if (fs::is_directory(path))
    {
        spdlog::info("Loading images from folder '{}'", path.u8string());

        std::error_code ec;
        auto            canon_p = fs::canonical(path);
        m_directories.emplace(canon_p);

        vector<fs::directory_entry> entries;
        for (auto const &entry : fs::directory_iterator{canon_p, ec})
        {
            auto ext           = to_lower(get_extension(entry.path().filename().u8string()));
            bool supported_ext = Image::loadable_formats().find(ext) != Image::loadable_formats().end();
            if (!entry.is_directory() && supported_ext)
            {
                m_files_found_in_directories.emplace(entry);
                entries.emplace_back(entry);
            }
        }

        sort(begin(entries), end(entries),
             [](const auto &a, const auto &b) { return natural_less(a.path().string(), b.path().string()); });

        for (size_t i = 0; i < entries.size(); ++i)
            load_one(entries[i].path(), buffer, false, i == 0 ? should_select : false, to_replace, channel_selector);

        // this moves the file to the top of the recent files list
        add_recent_file(filename);
    }
#endif
    else if (fs::is_regular_file(path))
    {
        // remove any instances of filename from the recent files list until we know it has loaded successfully
        remove_recent_file(filename);
        load_one(filename, buffer, true, should_select, to_replace, channel_selector);
    }
}

void BackgroundImageLoader::remove_watched_directories(std::function<bool(const fs::path &)> criterion)
{
    // Remove directories that match the criterion
    for (auto it = m_directories.begin(); it != m_directories.end();)
    {
        if (criterion(*it))
            it = m_directories.erase(it);
        else
            ++it;
    }

    // Keep only files whose parent directory is still in m_directories
    for (auto it = m_files_found_in_directories.begin(); it != m_files_found_in_directories.end();)
    {
        const auto &file_path      = *it;
        bool        parent_in_dirs = std::any_of(m_directories.begin(), m_directories.end(),
                                                 [&file_path](const fs::path &dir) { return file_path.parent_path() == dir; });
        if (parent_in_dirs)
            ++it;
        else
            it = m_files_found_in_directories.erase(it);
    }
}

void BackgroundImageLoader::get_loaded_images(function<void(ImagePtr, ImagePtr, bool)> callback)
{
    // move elements matching the criterion to the end of the vector, and then erase all matching elements
    pending_images.erase(std::remove_if(pending_images.begin(), pending_images.end(),
                                        [this, &callback](shared_ptr<PendingImages> p)
                                        {
                                            // if the computation isn't ready, we return false to indicate that we can't
                                            // yet remove this entry
                                            if (!p->computation.ready())
                                                return false;

                                            // finalize the computation
                                            p->computation.wait();

                                            // once the async computation is ready, we can access the resulting
                                            // images and return true to report that we can remove this entry from
                                            // pending_images
                                            if (p->images.empty())
                                                return true;

                                            for (size_t i = 0; i < p->images.size(); ++i)
                                                callback(p->images[i], p->to_replace, p->should_select);

                                            // if loading was successful, add the filename to the recent list
                                            if (p->add_to_recent)
                                                add_recent_file(p->filename);

                                            return true;
                                        }),
                         pending_images.end());
}

void BackgroundImageLoader::load_new_files()
{
    std::error_code ec;
    for (const auto &dir : m_directories)
        for (auto const &entry : fs::directory_iterator{dir, ec})
        {
            if (entry.is_directory())
                continue;

            const auto p = entry.path();
            if (!m_files_found_in_directories.count(p))
            {
                m_files_found_in_directories.emplace(p);
                background_load(p.u8string());
            }
        }
}