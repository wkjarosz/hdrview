#pragma once

#include "colorspace.h"
#include "fwd.h"
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

using std::function;
using std::set;
using std::shared_ptr;
using std::string;
using std::string_view;
using std::vector;

struct ImageLoadOptions
{
    //! Comma-separated list of channel names to include or exclude from the image. If empty, all channels are selected.
    string channel_selector;

    //! Override any metadata in the file and decode pixel values using this transfer function
    TransferFunction tf    = TransferFunction_Unknown;
    float            gamma = 1.f;
};

const ImageLoadOptions &load_image_options();
const ImageLoadOptions &load_image_options_gui();

/**
    Load the an image from the input stream.

    \param [] is       The input stream to read from
    \param [] filename The corresponding filename if `is` was opened from a file
    \param [] opts     Options for loading the image
    \return            A vector of possibly multiple images (e.g. from multi-part EXR files)
*/
vector<ImagePtr> load_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});

struct BackgroundImageLoader
{
    void background_load(const string filename, const string_view = string_view{}, bool should_select = false,
                         ImagePtr to_replace = nullptr, const ImageLoadOptions &opts = {});
    void load_recent_file(int index);
    void get_loaded_images(function<void(ImagePtr, ImagePtr, bool)> callback);
    int  num_pending_images() const { return pending_images.size(); }

    const set<fs::path> &watched_directories() const { return m_directories; }
    bool                 add_watched_directory(const fs::path &dir, bool ignore_existing);
    //! Remove all watched directories that match the criterion.
    void remove_watched_directories(function<bool(const fs::path &)> remove_criterion);

    void load_new_and_modified_files();

    void                  set_recent_files(const vector<string> &recents) { m_recent_files = recents; }
    void                  clear_recent_files() { set_recent_files({}); }
    const vector<string> &recent_files() const { return m_recent_files; }
    vector<string>        recent_files_short(int head_length = 32, int tail_length = 25) const;

    void draw_gui();

private:
    struct PendingImages;
    vector<shared_ptr<PendingImages>> pending_images;

    vector<string> m_recent_files;

    void add_recent_file(const string &f);
    void remove_recent_file(const string &f);

    set<fs::path> m_directories;

    // don't treat these files as new (they are either currently loaded, or we've previously loaded them from a watched
    // directory and manually closed them, so don't want to automatically reload them)
    set<fs::path> m_existing_files;
};