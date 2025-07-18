#pragma once

#include "async.h"
#include "fwd.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

struct BackgroundImageLoader
{
    void background_load(const std::string filename, const std::string_view = std::string_view{},
                         bool should_select = false, ImagePtr to_replace = nullptr,
                         const std::string &channel_selector = "");
    void load_recent_file(int index);
    void get_loaded_images(std::function<void(ImagePtr, ImagePtr, bool)> callback);
    int  num_pending_images() const { return pending_images.size(); }

    //! Remove all watched directories that match the criterion.
    void remove_watched_directories(std::function<bool(const std::filesystem::path &)> remove_criterion);

    void load_new_files();

    void set_recent_files(const std::vector<std::string> &recents) { m_recent_files = recents; }
    void clear_recent_files() { set_recent_files({}); }
    const std::vector<std::string> &recent_files() const { return m_recent_files; }
    std::vector<std::string>        recent_files_short(int head_length = 32, int tail_length = 25) const;

private:
    struct PendingImages;
    std::vector<std::shared_ptr<PendingImages>> pending_images;

    std::vector<std::string> m_recent_files;

    void add_recent_file(const std::string &f);
    void remove_recent_file(const std::string &f);

    std::set<std::filesystem::path> m_directories;

    // don't treat these files as new (they are either currently loaded, or we've previously loaded them from a watched
    // directory and manually closed them, so don't want to automatically reload them)
    std::set<std::filesystem::path> m_files_found_in_directories;
};