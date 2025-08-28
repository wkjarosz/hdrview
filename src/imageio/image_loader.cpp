#include "image_loader.h"
#include "app.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"
#include "miniz.h"
#include <fstream>

using namespace std;
namespace fs = std::filesystem;

static constexpr size_t g_max_recent = 15;

struct BackgroundImageLoader::PendingImages
{
    string                  filename;
    ThreadPool::TaskTracker computation;
    vector<ImagePtr>        images;
    bool                    add_to_recent;         ///< Whether to add the loaded images to the recent files list
    bool                    should_select = false; ///< Whether to select the first loaded image
    ImagePtr                to_replace = nullptr;  ///< If not null, this image will be replaced with the loaded images
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

// helper: case-insensitive extension check for .zip
static bool has_zip_extension(const string &fn)
{
    try
    {
        return to_lower(fs::path(fn).extension().u8string()) == ".zip";
    }
    catch (...)
    {
        return false;
    }
}

// Helper to split "archive.zip/entry.png" into zip and entry
static bool split_zip_entry(const string &filename, string &zip_path, string &entry_path)
{
    if (has_zip_extension(filename))
    {
        zip_path = filename;
        entry_path.clear();
        return true;
    }
    else
    {
        auto pos = filename.find(".zip/");
        if (pos == string::npos)
        {
            zip_path = filename;
            entry_path.clear();
            return false;
        }
        pos += 4; // include ".zip"
        zip_path   = filename.substr(0, pos);
        entry_path = filename.substr(pos + 1);
        return true;
    }
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
            pending_images.emplace_back(std::make_shared<PendingImages>(path.u8string(), buffer, path, channel_selector,
                                                                        add_to_recent, should_select, to_replace));
        }
        catch (const std::exception &e)
        {
            spdlog::error("Could not load image \"{}\": {}.", path.u8string(), e.what());
            return;
        }
    };

    // helper to extract zip buffer and schedule each contained image via load_one
    auto extract_and_schedule = [&](string_view zip_buffer, const string &zip_name, bool select_first,
                                    ImagePtr to_replace, const string &entry_pattern = "")
    {
        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));
        if (!mz_zip_reader_init_mem(&zip, zip_buffer.data(), zip_buffer.size(), 0))
        {
            spdlog::error("Failed to open zip archive '{}'", zip_name);
            return 0;
        }

        int num        = (int)mz_zip_reader_get_num_files(&zip);
        int num_images = 0;
        spdlog::info("Zip '{}' contains {} files.", zip_name, num);

        for (int i = 0; i < num; ++i)
        {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&zip, i, &stat))
                continue;
            if (stat.m_is_directory)
                continue;

            fs::path entry_path = fs::path(stat.m_filename);
            if (!Image::loadable(entry_path.extension().u8string()))
                continue;

            // If entry_pattern is set, skip entries that don't match
            if (!entry_pattern.empty() && entry_path.u8string() != entry_pattern)
                continue;

            size_t uncompressed_size = 0;
            void  *p                 = mz_zip_reader_extract_to_heap(&zip, i, &uncompressed_size, 0);
            if (!p)
                continue;

            string_view data{reinterpret_cast<char *>(p), uncompressed_size};
            // build a combined filename that prepends the zip path to the entry path
            string combined = zip_name + "/" + entry_path.u8string();
            // schedule async load; do not add each entry to recent files
            load_one(fs::u8path(combined), data, false, select_first && num_images == 0, to_replace, channel_selector);
            ++num_images;
            mz_free(p);

            // If entry_pattern is set, we only want one entry
            if (!entry_pattern.empty())
                break;
        }

        if (!num_images)
            spdlog::warn("No loadable images found in '{}'", zip_name);

        mz_zip_reader_end(&zip);

        return num_images;
    };

    auto path = fs::u8path(filename);

    if (!buffer.empty())
    {
        // if we have a buffer, we assume it is a file that has been downloaded
        // and we load it directly from the buffer
        auto [sz, unit] = human_readable_size(buffer.size());
        spdlog::info("Loading image '{}' from {:.0f} {} buffer.", filename, sz, unit);

        if (has_zip_extension(filename))
        {
            remove_recent_file(filename);
            if (extract_and_schedule(buffer, filename, should_select, to_replace))
                add_recent_file(filename);
        }
        else
            load_one(path, buffer, false, should_select, to_replace, channel_selector);
        return;
    }
#if !defined(__EMSCRIPTEN__)
    else if (fs::is_directory(path))
    {
        spdlog::info("Loading images from folder '{}'", filename);

        std::error_code ec;
        auto            canon_p = fs::weakly_canonical(path, ec);
        if (ec)
        {
            spdlog::error("Could not access directory '{}': {}.", filename, ec.message());
            return;
        }
        m_directories.emplace(canon_p);

        vector<fs::directory_entry> entries;
        for (auto const &entry : fs::directory_iterator{canon_p, ec})
        {
            if (entry.is_directory())
                continue;

            if (Image::loadable(entry.path().extension().u8string()))
            {
                m_existing_files.emplace(entry);
                entries.emplace_back(entry);
            }
        }

        sort(begin(entries), end(entries),
             [](const auto &a, const auto &b) { return natural_less(a.path().string(), b.path().string()); });

        for (size_t i = 0; i < entries.size(); ++i)
        {
            spdlog::info("Loading file '{}'...", entries[i].path().u8string());
            load_one(entries[i].path(), buffer, false, i == 0 ? should_select : false, to_replace, channel_selector);
        }

        // this moves the file to the top of the recent files list
        add_recent_file(filename);
    }
#endif
    else // a regular file
    {
        // remove any instances of filename from the recent files list until we know it has loaded successfully
        remove_recent_file(filename);
        string zip_fn, entry_fn;
        bool   is_zip_entry = split_zip_entry(filename, zip_fn, entry_fn);

        auto zip_path = fs::u8path(zip_fn);

        if (!fs::exists(zip_path) || !fs::is_regular_file(zip_path))
        {
            spdlog::error("File '{}' does not exist or is not a regular file.", zip_path.u8string());
            return;
        }

        // If the file is a zip on disk, read into memory and extract
        if (is_zip_entry)
        {
            std::ifstream is(zip_path, std::ios::binary);
            if (!is)
            {
                spdlog::error("Failed to open zip file '{}'", zip_path.u8string());
                return;
            }
            std::vector<char> buf((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
            if (buf.empty())
            {
                spdlog::warn("Zip file '{}' is empty", zip_path.u8string());
                return;
            }

            if (extract_and_schedule(string_view(buf.data(), buf.size()), zip_fn, should_select, to_replace, entry_fn))
                add_recent_file(filename);
        }
        else
        {
            spdlog::info("Loading file '{}'...", filename);
            load_one(filename, buffer, true, should_select, to_replace, channel_selector);
        }
    }
}

bool BackgroundImageLoader::add_watched_directory(const std::filesystem::path &dir, bool ignore_existing)
{
    if (dir.empty())
        return false;

    spdlog::trace("adding watched folder '{}'", dir.string());
    std::error_code ec;
    auto            canon_p = fs::weakly_canonical(dir, ec);
    if (ec)
    {
        spdlog::error("Could not access directory '{}': {}.", dir.u8string(), ec.message());
        return false;
    }
    m_directories.emplace(canon_p);

    if (!ignore_existing)
        return true;

    // if we are ignoring existing files, add all files in the directory to m_existing_files
    for (auto const &entry : fs::directory_iterator{canon_p, ec})
    {
        if (entry.is_directory() || !entry.is_regular_file())
            continue;

        if (Image::loadable(entry.path().extension().u8string()))
            m_existing_files.emplace(entry);
    }

    return true;
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
    for (auto it = m_existing_files.begin(); it != m_existing_files.end();)
    {
        if (std::any_of(m_directories.begin(), m_directories.end(),
                        [&file_path = *it](const fs::path &dir) { return file_path.parent_path() == dir; }))
            ++it;
        else
            it = m_existing_files.erase(it);
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

void BackgroundImageLoader::load_new_and_modified_files()
{
    // reload any modified files
    bool any_reloaded = false;
    for (int i = 0; i < hdrview()->num_images(); ++i)
    {
        auto img = hdrview()->image(i);
        if (!fs::exists(img->path))
        {
            spdlog::warn("File[{}] '{}' no longer exists, skipping reload.", i, img->path.u8string());
            if (auto it = m_existing_files.find(img->path); it != m_existing_files.end())
                m_existing_files.erase(it);
            continue;
        }

        fs::file_time_type last_modified;
        try
        {
            last_modified = fs::last_write_time(img->path);
        }
        catch (...)
        {
            continue;
        }

        if (last_modified != img->last_modified)
        {
            // Updating the last-modified date prevents double-scheduled reloads if the load take a lot of time or
            // fails.
            img->last_modified = last_modified;
            hdrview()->reload_image(img);
            any_reloaded = true;
        }
    }

    if (!any_reloaded)
        spdlog::debug("No modified files found to reload.");

    // load new files
    std::error_code ec;
    for (const auto &dir : m_directories)
        for (auto const &entry : fs::directory_iterator{dir, ec})
        {
            if (entry.is_directory())
                continue;

            const auto p = entry.path();
            if (m_existing_files.count(p))
                continue;

            if (Image::loadable(p.extension().u8string()))
            {
                m_existing_files.emplace(p);
                background_load(p.u8string());
            }
        }
}

void BackgroundImageLoader::draw_gui()
{
    ImGui::IconButton(hdrview()->action("Watch for changes"), true);
    ImGui::SameLine();
    ImGui::IconButton(hdrview()->action("Add watched folder..."), true);

    if (ImGui::BeginTable("Watched folders", 1,
                          ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg,
                          ImVec2(0.f, ImGui::GetContentRegionAvail().y)))
    {
        const float icon_width = ImGui::IconSize().x;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, icon_width);

        ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
        ImGui::TableSetupColumn("Watched folders", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        fs::path to_remove;
        for (const auto &path : m_directories)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::SmallButton(fmt::format("{}##{}", ICON_MY_CLOSE_SMALL, path.string()).c_str()))
                to_remove = path;

            ImGui::SameLine();

            ImGui::BeginDisabled(!(*hdrview()->action("Watch for changes").p_selected));
            string text = ImGui::TruncatedText(path.string(), ICON_MY_ADD_WATCHED_FOLDER);
            // if (ImGui::Selectable(text.c_str(), false))
            //     ;
            ImGui::TextUnformatted(text.c_str());
            ImGui::EndDisabled();
        }

        if (!to_remove.empty())
            remove_watched_directories([to_remove](const fs::path &path) { return path == to_remove; });
        ImGui::PopStyleVar(2);
        ImGui::EndTable();
    }
}