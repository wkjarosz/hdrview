#pragma once

#include "colorspace.h"
#include "fwd.h"
#include "imageio/gainmap.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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

    //! When false, an alpha channel is loaded as ordinary data instead of transparency: it is grouped on its own
    //! and the color channels are not premultiplied by it. See Image::alpha_is_transparency.
    bool alpha_is_transparency = true;

    bool override_profile = false;
    //! Override any metadata in the file and decode pixel values using this color gamut.
    ColorGamut_ gamut_override = ColorGamut_sRGB_BT709;
    //! Override any metadata in the file and decode pixel values using this transfer function.
    TransferFunction tf_override = TransferFunction::Linear;

    //! If true, keep the file's primaries and only linearize the pixel values on load. If false, convert to Rec709/sRGB
    //! or Gray at D65 primaries as appropriate.
    bool keep_primaries = true;

    //! Ceiling, in stops, on how much of an HDR gain map to reconstruct when a file carries one.
    /*!
        A gain map says how much brighter an image's HDR rendition is than the SDR pixels stored
        alongside it. Infinity reconstructs all of it, which is the default since HDRView's working
        space is unbounded; zero leaves the SDR rendition alone. See imageio/gainmap.h.
    */
    float gainmap_headroom = k_full_gainmap_headroom;
};

/**
    Reject dimensions no real image has, before a loader allocates or decodes for them.

    Loaders size their buffers from the header, so a file declaring billions of pixels costs the memory or
    the decode time whether or not the pixels are there. Image::finalize() rejects oversized images too, but
    only once that cost has already been paid.

    \param [] width    Width the file declares
    \param [] height   Height the file declares
    \param [] format   Format name, for the error message
*/
void check_image_dimensions(int64_t width, int64_t height, string_view format);

const ImageLoadOptions &load_image_options();
void                    draw_load_image_options_dialog(bool &open);

/**
    Load the an image from the input stream.

    \param [] is       The input stream to read from
    \param [] filename The corresponding filename if `is` was opened from a file
    \param [] opts     Options for loading the image
    \return            A vector of possibly multiple images (e.g. from multi-part EXR files)
*/
vector<ImagePtr> load_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts = {});

/// Returns {entry_name, contents} for every root-level (no '/' in the stored path) entry in a zip archive
/// whose name ends with `suffix` (case-insensitive). Used to look for a manifest at a zip's root without
/// assuming a fixed filename. Returns an empty vector if `zip_bytes` isn't a valid zip.
vector<std::pair<string, string>> zip_root_entries_with_suffix(string_view zip_bytes, const string &suffix);

/// Extracts one specific entry from a zip archive by its exact stored path. Returns std::nullopt if the
/// zip can't be opened or doesn't contain that entry.
std::optional<string> zip_extract_entry(string_view zip_bytes, const string &entry_path);

struct BackgroundImageLoader
{
    void background_load(const string filename, const string_view = string_view{}, bool should_select = false,
                         ImagePtr to_replace = nullptr, const ImageLoadOptions &opts = {});
    void load_recent_file(int index);
    void get_loaded_images(function<void(ImagePtr, ImagePtr, bool)> callback);
    int  num_pending_images() const { return (int)pending_images.size(); }

    const set<fs::path> &watched_directories() const { return m_directories; }
    bool                 add_watched_directory(const fs::path &dir, bool ignore_existing);
    //! Remove all watched directories that match the criterion.
    void remove_watched_directories(function<bool(const fs::path &)> remove_criterion);

    void load_new_and_modified_files();

    void                  set_recent_files(const vector<string> &recents) { m_recent_files = recents; }
    void                  clear_recent_files() { set_recent_files({}); }
    const vector<string> &recent_files() const { return m_recent_files; }
    vector<string>        recent_files_short(int head_length = 32, int tail_length = 25) const;
    //! Adds (or moves to the front of) the recent-files list. Public so callers that load something outside
    //! background_load()'s own paths (e.g. HDRViewApp's session loading) can still register it as recent.
    void add_recent_file(const string &f);

    void draw_gui();

    // Called with the raw bytes of a top-level zip archive before it's extracted as a folder of images (not
    // called for a single-entry re-extraction from within a zip). Returning true means "handled, don't also
    // extract this zip's images normally". A plain byte-buffer hook with no session/JSON knowledge, so this
    // loader stays app-agnostic.
    function<bool(string_view zip_bytes, const string &zip_name)> zip_bundle_hook;

private:
    struct PendingImages;
    vector<shared_ptr<PendingImages>> pending_images;

    vector<string> m_recent_files;

    void remove_recent_file(const string &f);

    set<fs::path> m_directories;

    // don't treat these files as new (they are either currently loaded, or we've previously loaded them from a watched
    // directory and manually closed them, so don't want to automatically reload them)
    set<fs::path> m_existing_files;

    // loaded images whose backing file is currently missing on disk, so load_new_and_modified_files()
    // only warns once per disappearance instead of on every watch-loop poll
    set<fs::path> m_missing_files_warned;
};