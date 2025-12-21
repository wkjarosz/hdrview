//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image_loader.h"
#include "app.h"
#include "colorspace.h"
#include "common.h"
#include "fonts.h"
#include "image.h"
#include "imgui_ext.h"
#include "timer.h"
#include <fstream>
#include <hello_imgui/dpi_aware.h>
#include <miniz.h>
#include <sstream>

#include "imageio/dds.h"
#include "imageio/exr.h"
#include "imageio/heif.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/pfm.h"
#include "imageio/png.h"
#include "imageio/qoi.h"
#include "imageio/stb.h"
#include "imageio/tiff.h"
#include "imageio/uhdr.h"
#include "imageio/webp.h"

using namespace std;
namespace fs = std::filesystem;

static ImageLoadOptions s_opts;
static constexpr size_t g_max_recent = 15;

struct LoaderEntry
{
    std::string                                                                                              name;
    std::function<bool(std::istream &, std::string_view, const ImageLoadOptions &, std::vector<ImagePtr> &)> try_load;
    bool enabled = true;
};

static std::vector<LoaderEntry> default_loaders()
{
    return {
        {"openexr",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_exr_image(is, filename))
             {
                 out = load_exr_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
#ifdef HDRVIEW_ENABLE_UHDR
        {"libultrahdr",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_uhdr_image(is))
             {
                 out = load_uhdr_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
#endif
#ifdef HDRVIEW_ENABLE_LIBJPEG
        {"libjpg",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_jpg_image(is))
             {
                 out = load_jpg_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
#endif
        {"qoi",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &, std::vector<ImagePtr> &out)
         {
             if (is_qoi_image(is))
             {
                 out = load_qoi_image(is, filename);
                 return true;
             }
             return false;
         }},
#ifdef HDRVIEW_ENABLE_JPEGXL
        {"libjxl",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_jxl_image(is))
             {
                 out = load_jxl_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
#endif
        {"dds",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_dds_image(is))
             {
                 out = load_dds_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
#ifdef HDRVIEW_ENABLE_LIBHEIF
        {"libheif",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_heif_image(is))
             {
                 out = load_heif_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
#endif
#ifdef HDRVIEW_ENABLE_LIBTIFF
        {"libtiff",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_tiff_image(is))
             {
                 out = load_tiff_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
#endif
#ifdef HDRVIEW_ENABLE_LIBPNG
        {"libpng",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_png_image(is))
             {
                 out = load_png_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
#endif
#ifdef HDRVIEW_ENABLE_LIBWEBP
        {"libwebp",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_webp_image(is))
             {
                 out = load_webp_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
#endif
        {"stb",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &opts, std::vector<ImagePtr> &out)
         {
             if (is_stb_image(is))
             {
                 out = load_stb_image(is, filename, opts);
                 return true;
             }
             return false;
         }},
        {"pfm",
         [](std::istream &is, std::string_view filename, const ImageLoadOptions &, std::vector<ImagePtr> &out)
         {
             if (is_pfm_image(is))
             {
                 out = load_pfm_image(is, filename);
                 return true;
             }
             return false;
         }},
    };
}

// Initialize g_loaders with the default order
static std::vector<LoaderEntry> g_loaders = default_loaders();

struct BackgroundImageLoader::PendingImages
{
    string                  filename;
    ThreadPool::TaskTracker computation;
    vector<ImagePtr>        images;
    bool                    add_to_recent;         ///< Whether to add the loaded images to the recent files list
    bool                    should_select = false; ///< Whether to select the first loaded image
    ImagePtr                to_replace = nullptr;  ///< If not null, this image will be replaced with the loaded images
    PendingImages(const string &f, const string_view buffer, const fs::path &path, ImageLoadOptions opts,
                  bool recent = true, bool should_select = false, ImagePtr to_replace = nullptr) :
        filename(f), add_to_recent(recent), should_select(should_select), to_replace(to_replace)
    {
        computation = do_async(
            // convert the buffer (if any) to a string so the async thread has its own copy,
            // then load from the string or filename depending on whether the buffer is empty
            [this, buffer_str = string(buffer), path, opts]()
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
                        images = load_image(is, path.u8string(), opts);
                    else
                    {
                        spdlog::error("File '{}' doesn't exist.", path.u8string());
                        return;
                    }
                }
                else
                {
                    std::istringstream is{buffer_str};
                    images = load_image(is, path.u8string(), opts);
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
        background_load(m_recent_files[idx], {}, true, nullptr, load_image_options());
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
                                            ImagePtr to_replace, const ImageLoadOptions &opts)
{
    if (should_select)
        spdlog::debug("will select image '{}'", filename);

    auto load_one = [this](const fs::path &path, const string_view buffer, bool add_to_recent, bool should_select,
                           ImagePtr to_replace, const ImageLoadOptions &opts)
    {
        try
        {
            pending_images.emplace_back(std::make_shared<PendingImages>(path.u8string(), buffer, path, opts,
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

        spdlog::debug("Zip '{}' contains {} files, loading...", zip_name, num);
        std::vector<char> buffer(1000000); // reuse a buffer to reduce memory reallocations
        Timer             timer;
        for (int i = 0; i < num; ++i)
        {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&zip, i, &stat))
                continue;
            if (stat.m_is_directory)
                continue;

            fs::path entry_path = fs::path(stat.m_filename);

            auto fn = entry_path.filename().u8string();
            // skip hidden files (starting with '.')
            if (!fn.empty() && fn.front() == '.')
                continue;

            // skip files we can't load based on the extension
            if (!Image::loadable(entry_path.extension().u8string()))
                continue;

            // If entry_pattern is set, skip entries that don't match
            if (!entry_pattern.empty() && entry_path.u8string() != entry_pattern)
                continue;

            buffer.resize(stat.m_uncomp_size);
            if (!mz_zip_reader_extract_to_mem(&zip, i, buffer.data(), buffer.size(), 0))
            {
                spdlog::warn("Failed to extract '{}' from '{}'", entry_path.u8string(), zip_name);
                continue;
            }

            string_view data{reinterpret_cast<char *>(buffer.data()), buffer.size()};
            // build a combined filename that prepends the zip path to the entry path
            string combined = zip_name + "/" + entry_path.u8string();
            // schedule async load; do not add each entry to recent files
            load_one(fs::u8path(combined), data, false, select_first && num_images == 0, to_replace, opts);
            ++num_images;

            // If entry_pattern is set, we only want one entry
            if (!entry_pattern.empty())
                break;
        }

        if (!num_images)
            spdlog::warn("No loadable images found in '{}'", zip_name);

        mz_zip_reader_end(&zip);

        spdlog::info("Loading files in the zip archive took {:f} seconds.", timer.elapsed() / 1000.f);

        return num_images;
    };

    auto path = fs::u8path(filename);

    if (!buffer.empty())
    {
        // if we have a buffer, we assume it is a file that has been downloaded
        // and we load it directly from the buffer
        spdlog::info("Loading image '{}' from {:.0h} buffer.", filename, human_readible{buffer.size()});

        if (to_lower(get_extension(filename)) == ".zip")
        {
            remove_recent_file(filename);
            if (extract_and_schedule(buffer, filename, should_select, to_replace))
                add_recent_file(filename);
        }
        else
            load_one(path, buffer, false, should_select, to_replace, opts);
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
            load_one(entries[i].path(), buffer, false, i == 0 ? should_select : false, to_replace, opts);
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

            spdlog::debug("Loading zip file into memory buffer...");
            Timer             timer;
            std::vector<char> buf((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
            if (buf.empty())
            {
                spdlog::warn("Zip file '{}' is empty", zip_path.u8string());
                return;
            }
            spdlog::info("Loading zip file data took {:f} seconds.", timer.elapsed() / 1000.f);

            if (extract_and_schedule(string_view(buf.data(), buf.size()), zip_fn, should_select, to_replace, entry_fn))
                add_recent_file(filename);
        }
        else
        {
            spdlog::info("Loading file '{}'...", filename);
            load_one(filename, buffer, true, should_select, to_replace, opts);
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
            ImGui::TextUnformatted(ICON_MY_ADD_WATCHED_FOLDER);
            ImGui::SameLine(0.f, 0.f);
            ImGui::TextAligned2(1.0f, -FLT_MIN, text.c_str());
            ImGui::EndDisabled();
        }

        if (!to_remove.empty())
            remove_watched_directories([to_remove](const fs::path &path) { return path == to_remove; });
        ImGui::PopStyleVar(2);
        ImGui::EndTable();
    }
}

const ImageLoadOptions &load_image_options() { return s_opts; }

const ImageLoadOptions &load_image_options_gui()
{
    ImGui::TextWrapped("These options control how images are loaded. They will be applied to all images opened "
                       "from now on, including those opened via the main \"Open image\" dialog.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    static char buf[256] = "";
    if (s_opts.channel_selector != buf)
        snprintf(buf, sizeof(buf), "%s", s_opts.channel_selector.c_str());
    if (ImGui::InputTextWithHint("Channel selector", ICON_MY_FILTER " Filter 'include,-exclude'", buf,
                                 IM_ARRAYSIZE(buf)))
        s_opts.channel_selector = string(buf);
    ImGui::Tooltip(
        "If the image file contains multiple images or channels (e.g. multi-part EXR files), you can specify "
        "which part(s) to load here. This is a comma-separated list of part,layer, or channel names to include or "
        "(prefixed with '-') exclude.\n\n"
        "For example, \"diffuse,specular\" will only load layers which contain either of these two words, and \"-.A\" "
        "would exclude channels named \"A\". Leave empty to load all parts.");

    ImGui::Checkbox("Override file's color profile", &s_opts.override_profile);
    ImGui::Tooltip(
        "By default, HDRView tries to detect the color profile of the image from metadata stored in the file. "
        "Enabling this option instructs HDRView to ignore any color profile information in the file and instead use "
        "the settings you select below.");

    if (s_opts.override_profile)
    {
        ImGui::Indent();
        // ImGui::BeginDisabled(!s_opts.override_profile);

        if (ImGui::BeginCombo("Color gamut", color_gamut_name(s_opts.gamut_override), ImGuiComboFlags_HeightLargest))
        {
            auto csn = color_gamut_names();
            for (ColorGamut n = ColorGamut_FirstNamed; n <= ColorGamut_LastNamed; ++n)
            {
                auto       cg          = (ColorGamut_)n;
                const bool is_selected = (s_opts.gamut_override == n);
                if (ImGui::Selectable(csn[n], is_selected))
                    s_opts.gamut_override = cg;

                // Set the initial focus when opening the combo (scrolling + keyboard
                // navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("Transfer function", transfer_function_name(s_opts.tf_override).c_str()))
        {
            for (TransferFunction::Type i = TransferFunction::Linear; i < TransferFunction::Count; ++i)
            {
                auto       t           = (TransferFunction::Type_)i;
                const bool is_selected = (s_opts.tf_override.type == t);
                if (ImGui::Selectable(transfer_function_name({t, s_opts.tf_override.gamma}).c_str(), is_selected))
                    s_opts.tf_override.type = t;
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::BeginDisabled(s_opts.tf_override.type != TransferFunction::Gamma);
        if (s_opts.tf_override.type == TransferFunction::Gamma)
            ImGui::SliderFloat("Gamma", &s_opts.tf_override.gamma, 0.1f, 5.f);
        ImGui::EndDisabled();

        // ImGui::EndDisabled();
        ImGui::Unindent();
    }

    ImGui::Checkbox("Keep file's primaries and only linearize on load", &s_opts.keep_primaries);
    ImGui::Tooltip(
        "HDRView can either 1) convert all pixel values to the working linear Rec709/sRGB color space upon loading, or "
        "2) only linearize the pixel values on load while retaining the file's original color gamut/primaries.\n\n"
        "With option 2, HDRView will still try to deduce the file's primaries during load, but it keeps the color "
        "values in the file's color space, only transforming colors to HDRView's working color space during display. "
        "This can be useful if you want to inspect the (linearized) pixel values in the image's native color space. It"
        "is exact when the file unambiguously defines the color primaries via CICP, but color shifts may occur if the "
        "color space is specified using a general ICC profile.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("FormatOrderTable", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable |
                              ImGuiTableFlags_SortTristate))
    {
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze the header row
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 0.0f, 0);
        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_None, 0.0f, 1); // Only this column is sortable
        ImGui::TableSetupColumn("Image loading format order (drag to reorder):", ImGuiTableColumnFlags_NoSort, 0.0f, 2);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs())
        {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0)
            {
                const ImGuiTableColumnSortSpecs &spec = sort_specs->Specs[0];
                if (spec.ColumnIndex == 1) // Only sort if the "Enabled" column is selected
                {
                    std::stable_sort(g_loaders.begin(), g_loaders.end(),
                                     [spec](const LoaderEntry &a, const LoaderEntry &b)
                                     {
                                         if (spec.SortDirection == ImGuiSortDirection_Ascending)
                                             return a.enabled < b.enabled;
                                         else if (spec.SortDirection == ImGuiSortDirection_Descending)
                                             return a.enabled > b.enabled;
                                         else
                                             return false;
                                     });
                }
                sort_specs->SpecsDirty = false;
            }
        }

        int drag_src = -1, drag_dst = -1;
        for (int n = 0; n < (int)g_loaders.size(); n++)
        {
            ImGui::TableNextRow();

            // Order number column
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", n + 1);

            // Enabled checkbox column
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(n);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
            ImGui::Checkbox("##enabled", &g_loaders[n].enabled);
            ImGui::PopStyleVar();
            ImGui::PopID();

            // Format name column
            ImGui::TableSetColumnIndex(2);
            if (!g_loaders[n].enabled)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            // Remove highlight for Selectable
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));        // No highlight when selected
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0)); // No highlight on hover

            if (ImGui::Selectable(g_loaders[n].name.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_NoAutoClosePopups))
            {
            }
            ImGui::PopStyleColor(2);
            if (!g_loaders[n].enabled)
                ImGui::PopStyleColor();

            // allow reordering by dragging. just record the source and destination indices
            if (ImGui::IsItemActive() && !ImGui::IsItemHovered())
            {
                drag_src = n;
                drag_dst = n + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);
            }
        }

        // now that we are outside the loop, perform the reordering if needed
        if (drag_src >= 0 && drag_dst >= 0 && drag_dst < (int)g_loaders.size() && drag_src != drag_dst)
        {
            std::swap(g_loaders[drag_src], g_loaders[drag_dst]);
            ImGui::ResetMouseDragDelta();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Reset options to defaults"))
    {
        s_opts    = ImageLoadOptions{};
        g_loaders = default_loaders();
    }

    ImGui::SameLine();

    string filename;

    if (ImGui::Button("OK", HelloImGui::EmToVec2(4.f, 0.f)))
        ImGui::CloseCurrentPopup();

    return s_opts;
}

vector<ImagePtr> load_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    spdlog::info("Loading from file: {}", filename);
    ScopedMDC mdc{"file", string(get_basename(filename))};
    Timer     timer;
    try
    {
        if (!is.good())
            throw invalid_argument("Invalid input stream");

        vector<ImagePtr> images;

        bool recognized = false;
        for (auto &loader : g_loaders)
        {
            if (!loader.enabled)
                continue;
            is.clear();
            is.seekg(0, std::ios::beg);
            if (loader.try_load(is, filename, opts, images) && !images.empty())
            {
                spdlog::info("Loaded using {} loader.", loader.name);
                recognized = true;
                break;
            }
        }
        if (!recognized)
            throw invalid_argument("This doesn't seem to be a supported image file.");

        // compute size of the input stream
        is.clear();
        std::streampos pos = is.tellg();
        is.seekg(0, std::ios::end);
        std::streampos size = is.tellg();
        is.seekg(pos);

        for (auto i : images)
        {
            try
            {
                i->finalize();
                i->filename   = filename;
                i->short_name = i->file_and_partname();
                i->size_bytes = static_cast<size_t>(size);

                // If multiple image "parts" were loaded and they have names, store these names in the image's
                // channel selector. This is useful if we later want to reload a specific image part from the
                // original file.
                if (i->partname.empty())
                    i->channel_selector = opts.channel_selector;
                else
                {
                    const auto selector_parts = split(opts.channel_selector, ",");
                    if (opts.channel_selector.empty())
                        i->channel_selector = i->partname;
                    else if (find(begin(selector_parts), end(selector_parts), i->partname) == end(selector_parts))
                        i->channel_selector = fmt::format("{},{}", i->partname, opts.channel_selector);
                    else
                        i->channel_selector = opts.channel_selector;
                }

                spdlog::info("Loaded image in {:f} seconds:\n{:s}", timer.elapsed() / 1000.f, i->to_string());
            }
            catch (const exception &e)
            {
                spdlog::error("Skipping image loaded from \"{}\" due to error:\n\t{}", filename, e.what());
                continue; // skip this image
            }
        }
        return images;
    }
    catch (const exception &e)
    {
        spdlog::error("Unable to load image file \"{}\":\n\t{}", filename, e.what());
    }
    return {};
}