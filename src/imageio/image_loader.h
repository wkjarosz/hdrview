#pragma once

#include "colorspace.h"
#include "fwd.h"
#include "imageio/gainmap.h"
#include <cstdint>
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

    //! Whether to keep what a gain-mapped file actually stores, alongside the rendition built from it.
    /*!
        A gain-mapped file holds a base rendition and a map, and the image HDRView shows is the two
        combined. With this on, both are kept as their own `base.*` and `gainmap.*` channel groups,
        so everything in the file is loaded rather than only the result. Costs roughly 75% more
        memory for a three-channel image with a single-channel map.
    */
    bool gainmap_renditions = true;
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

/**
    Whether a zip entry's declared uncompressed size is one the archive could actually be holding.

    Every entry is read whole into memory before anything looks at it, sized from what the archive's
    directory claims rather than from the bytes present -- so a declared size costs that much memory, and
    the time to decompress into it, whether or not the data is there. The same reasoning as
    check_image_dimensions(), one layer further out.

    \param [] uncompressed_size  Size the archive's directory declares for the entry
    \param [] compressed_size    Size the archive's directory says it stores it in
    \param [] entry_name         Entry name, for the warning
*/
bool zip_entry_size_is_plausible(uint64_t uncompressed_size, uint64_t compressed_size, string_view entry_name);

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
    void get_loaded_images(function<void(ImagePtr, ImagePtr, bool)> callback);
    int  num_pending_images() const { return (int)pending_images.size(); }

    const set<fs::path> &watched_directories() const { return m_directories; }
    //! Watch `dir` in its own right, whether or not anything has been loaded from it.
    bool add_watched_directory(const fs::path &dir, bool ignore_existing);
    //! Remove all watched directories that match the criterion, however they came to be watched.
    void remove_watched_directories(function<bool(const fs::path &)> remove_criterion);
    //! Same, but keeps the ones add_watched_directory() was asked for.
    /*!
        Callers prune by "no loaded image came from here", which is the right rule for a directory that is
        only watched because its images were opened. A directory the user asked for holds no loaded images
        of its own -- that is the point of it -- so that rule would always throw it away.
    */
    void remove_implicitly_watched_directories(function<bool(const fs::path &)> remove_criterion);

    void load_new_and_modified_files();

    void                  set_recent_files(const vector<string> &recents) { m_recent_files = recents; }
    void                  clear_recent_files() { set_recent_files({}); }
    const vector<string> &recent_files() const { return m_recent_files; }
    vector<string>        recent_files_short(int head_length = 32, int tail_length = 25) const;
    //! The recent file at `index` in the most-recently-used-first order that recent_files_short() returns,
    //! or an empty string if `index` is out of range. Opening it is left to the caller, since what a path
    //! means (image, session, session bundle) is an app-level decision.
    string recent_file(int index) const;
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

    // The subset of m_directories that add_watched_directory() was asked for, rather than ones picked up
    // from the folder an image was opened from.
    set<fs::path> m_explicit_directories;

    void remove_watched_directories_if(const function<bool(const fs::path &)> &criterion, bool keep_explicit);

    // don't treat these files as new (they are either currently loaded, or we've previously loaded them from a watched
    // directory and manually closed them, so don't want to automatically reload them)
    set<fs::path> m_existing_files;

    // loaded images whose backing file is currently missing on disk, so load_new_and_modified_files()
    // only warns once per disappearance instead of on every watch-loop poll
    set<fs::path> m_missing_files_warned;
};