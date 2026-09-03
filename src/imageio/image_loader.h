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
    /// Comma-separated list of channel names to include or exclude from the image. If empty, all channels are selected.
    string channel_selector;

    bool override_transparency = false;
    /// Override what the file says about its alpha and interpret it this way instead. See Image::transparency.
    TransparencyType_ transparency_override = TransparencyType_Straight;

    bool override_profile = false;
    /// Override any metadata in the file and decode pixel values using this color gamut.
    ColorGamut_ gamut_override = ColorGamut_sRGB_BT709;
    /// Override any metadata in the file and decode pixel values using this transfer function.
    TransferFunction tf_override = TransferFunction::Linear;

    /// If true, keep the file's primaries and only linearize the pixel values on load.
    /**
        If false, convert to Rec709/sRGB, or to Gray at D65 primaries, as appropriate.
    */
    bool keep_primaries = true;

    /// Ceiling, in stops, on how much of an HDR gain map to reconstruct. Infinity for all of it, zero for none.
    float gainmap_headroom = k_full_gainmap_headroom;

    /// Also keep the file's base rendition and gain map as their own `base.*` and `gainmap.*` channel groups.
    bool gainmap_renditions = true;
};

/// Throw if the header's dimensions are ones no real image has, before a loader sizes its buffers from them.
void check_image_dimensions(int64_t width, int64_t height, string_view format);

/// Whether a zip entry's declared uncompressed size is one the archive could be holding.
/**
    Entries are read whole into memory, sized from the directory's claim rather than from the bytes present.
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

/// Returns {entry_name, contents} for every root-level entry in a zip archive whose name ends with `suffix`.
/**
    Root-level means no '/' in the stored path, and `suffix` is matched case-insensitively. Empty if
    `zip_bytes` isn't a valid zip.
*/
vector<std::pair<string, string>> zip_root_entries_with_suffix(string_view zip_bytes, const string &suffix);

/// Extracts one entry from a zip archive by its exact stored path; nullopt if the zip or entry is unusable.
std::optional<string> zip_extract_entry(string_view zip_bytes, const string &entry_path);

struct BackgroundImageLoader
{
    /// Load `filename` from `buffer` if given (an empty buffer is an empty file, not "read from disk"), else from disk.
    void background_load(const string filename, std::optional<string_view> buffer = std::nullopt,
                         bool should_select = false, ImagePtr to_replace = nullptr, const ImageLoadOptions &opts = {});
    void get_loaded_images(function<void(ImagePtr, ImagePtr, bool)> callback);
    int  num_pending_images() const { return (int)pending_images.size(); }

    const set<fs::path> &watched_directories() const { return m_directories; }
    /// Watch `dir` in its own right, whether or not anything has been loaded from it.
    bool add_watched_directory(const fs::path &dir, bool ignore_existing);
    /// Stop watching every directory \p criterion returns true for.
    /**
        \p keep_explicit spares the ones add_watched_directory() was asked for. Those are watched for files
        that do not exist yet, so they hold no loaded images and would match a "nothing came from here" rule.
    */
    void remove_watched_directories(function<bool(const fs::path &)> criterion, bool keep_explicit = false);

    void load_new_and_modified_files();

    void                  set_recent_files(const vector<string> &recents) { m_recent_files = recents; }
    void                  clear_recent_files() { set_recent_files({}); }
    const vector<string> &recent_files() const { return m_recent_files; }
    vector<string>        recent_files_short(int head_length = 32, int tail_length = 25) const;
    /// The recent file at `index` in recent_files_short()'s order, or an empty string if it is out of range.
    string recent_file(int index) const;
    /// Adds (or moves to the front of) the recent-files list.
    void add_recent_file(const string &f);

    void draw_gui();

private:
    struct PendingImages;
    vector<shared_ptr<PendingImages>> pending_images;

    vector<string> m_recent_files;

    void remove_recent_file(const string &f);

    set<fs::path> m_directories;

    // the subset of m_directories that add_watched_directory() was asked for
    set<fs::path> m_explicit_directories;

    // don't treat these files as new (they are either currently loaded, or we've previously loaded them from a watched
    // directory and manually closed them, so don't want to automatically reload them)
    set<fs::path> m_existing_files;

    // loaded images whose backing file is currently missing on disk, so we only warn once per disappearance
    // instead of on every watch-loop poll
    set<fs::path> m_missing_files_warned;
};