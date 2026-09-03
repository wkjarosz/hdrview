#include "app.h"
#include "common.h"

#include "image.h"
#include "version.h"
#include <algorithm>
#include <fstream>
#include <hello_imgui/dpi_aware.h>
#include <miniz.h>
#include <sstream>
#include <string>

#include "imageio/exr.h"
#include "imageio/heif.h"
#include "imageio/j2k.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/pfm.h"
#include "imageio/png.h"
#include "imageio/qoi.h"
#include "imageio/stb.h"
#include "imageio/tiff.h"
#include "imageio/uhdr.h"
#include "imageio/webp.h"
#include "imgui.h"
#include "imgui_ext.h"

#ifdef __EMSCRIPTEN__
#include "platform_utils.h"
#include <emscripten/emscripten.h>
#include <emscripten_browser_file.h>
#else
#include "portable-file-dialogs.h"
#endif

using namespace std;

namespace
{

// Stable, lowercase snake_case identifiers for enum values as stored in .hsess session files. Independent
// of the GUI display-string tables (blend_mode_names()/channel_names() in common.cpp), so relabeling a
// dropdown cannot change what an existing session file means.
const char *const g_blend_mode_ids[BlendMode_COUNT] = {
    "normal",     "multiply",           "divide", "add", "average", "subtract", "relative_subtract",
    "difference", "relative_difference"};
const char *const g_tonemap_ids[Tonemap_COUNT]  = {"gamma", "false_color", "positive_negative"};
const char *const g_channel_ids[Channels_COUNT] = {"rgba", "rgb", "red", "green", "blue", "alpha", "y"};
const char *const g_bg_mode_ids[BGMode_COUNT]   = {"black", "white", "dark_checker", "light_checker", "custom_color"};
// A session records the alpha override the user chose, not the interpretation it produced: with no override
// the file is read afresh, so a corrected loader or an edited file is picked up. The trailing entry is that
// "no override" case, which is why this table is one longer than the enum.
const char *const g_transparency_type_ids[TransparencyType_Count + 1] = {"none", "premultiplied-linear",
                                                                         "premultiplied-nonlinear", "straight", ""};

template <typename Enum, size_t N>
string enum_to_id(Enum value, const char *const (&ids)[N])
{
    size_t i = (size_t)value;
    return ids[i < N ? i : 0];
}

template <typename Enum, size_t N>
Enum id_to_enum(const json &j, const char *key, const char *const (&ids)[N], Enum default_value)
{
    if (!j.contains(key))
        return default_value;
    string id = j.value<string>(key, ids[(size_t)default_value]);
    for (size_t i = 0; i < N; ++i)
        if (id == ids[i])
            return (Enum)i;
    spdlog::warn("Unrecognized '{}' value '{}' in session file; using default.", key, id);
    return default_value;
}

// The alpha override an image entry asks for, empty when it uses the file's own interpretation.
optional<TransparencyType_> transparency_override_from_id(const json &j)
{
    auto at =
        id_to_enum(j, "transparency_override", g_transparency_type_ids, (TransparencyType_)TransparencyType_Count);
    return at == TransparencyType_Count ? optional<TransparencyType_>{} : at;
}

// Checks "type"/"version" on a parsed session manifest (`source_name` is a filename or zip archive name,
// for the log message only). False means `j` does not look like a session at all, which callers should
// treat as "not a session" rather than a failure.
bool validate_session_manifest(const json &j, const string &source_name)
{
    if (j.value<string>("type", "") != "HDRView session")
    {
        spdlog::error("'{}' does not look like an HDRView session.", source_name);
        return false;
    }

    if (auto v = parse_version(j.value<string>("version", "")))
    {
        if (v->combined() > version_combined())
            spdlog::warn("'{}' was saved by a newer version of HDRView ({}.{}.{}) than this one ({}.{}.{}); some "
                         "things may not load correctly.",
                         source_name, v->major, v->minor, v->patch, version_major(), version_minor(), version_patch());
    }
    else
        spdlog::warn("'{}' has an unrecognized version; attempting to load anyway.", source_name);

    return true;
}

} // namespace

void HDRViewApp::draw_save_as_dialog(bool &open)
{
    if (ImGui::BeginModalDialog("Save as...", open, ImGui::DialogPosition::Center))
    {
        // Define enum for save formats
        enum Format_
        {
            Format_BMP_STB = 0,
            Format_HDR_STB,
            Format_HEIF,
            Format_AVIF,
            Format_JPEG_LIBJPEG,
            Format_JPEG_STB,
            Format_JPEG_UHDR,
            Format_JPEG_XL,
            Format_JPEG2000,
            Format_WEBP,
            Format_EXR,
            Format_PFM,
            Format_PNG_LIBPNG,
            Format_PNG_STB,
            Format_QOI,
            Format_TGA_STB,
            Format_TIFF,
            Format_Last = Format_TIFF
        };
        static Format_ save_format = Format_EXR;

        static bool format_enabled[Format_Last + 1] = {true, true,
#if HDRVIEW_ENABLE_LIBHEIF
                                                       true, true,
#else
                                                       false, false,
#endif
#if HDRVIEW_ENABLE_LIBJPEG
                                                       true,
#else
                                                       false,
#endif
                                                       true,
#if HDRVIEW_ENABLE_LIBUHDR
                                                       true,
#else
                                                       false,
#endif
#if HDRVIEW_ENABLE_LIBJXL
                                                       true,
#else
                                                       false,
#endif
#if HDRVIEW_ENABLE_J2K
                                                       true,
#else
                                                       false,
#endif
#if HDRVIEW_ENABLE_LIBWEBP
                                                       true,
#else
                                                       false,
#endif
                                                       true, true,
#if HDRVIEW_ENABLE_LIBPNG
                                                       true,
#else
                                                       false,
#endif
                                                       true, true, true,
#if HDRVIEW_ENABLE_LIBTIFF
                                                       true
#else
                                                       false
#endif
        };

        // Array of format names
        // clang-format off
        static const char *save_format_names[Format_Last + 1] = {
            "BMP (stb)",
            "HDR (stb)",
            "HEIF",
            "AVIF",
            "JPEG (libjpeg)",
            "JPEG (stb)",
            "JPEG (UltraHDR)",
            "JPEG-XL",
            "JPEG 2000",
            "WebP",
            "OpenEXR",
            "PFM",
            "PNG (libpng)",
            "PNG (stb)",
            "QOI",
            "TGA (stb)",
            "TIFF"
            };
        // clang-format on

        // filename extensions for each of the above
        // clang-format off
        static const char *save_format_extensions[Format_Last + 1] = {
            ".bmp",
            ".hdr",
            ".heif",
            ".avif",
            ".jpg",
            ".jpg",
            ".jpg",
            ".jxl",
            ".jp2",
            ".webp",
            ".exr",
            ".pfm",
            ".png",
            ".png",
            ".qoi",
            ".tga",
            ".tiff"
        };
        // clang-format on

        // ImGui::PushItemWidth(-HelloImGui::EmSize(10));

        static int composite = 0;
        ImGui::Combo("Image to export", &composite, "Current image\0Current/Reference composite image\0");
        ImGui::Tooltip("Save either the current image, or the composited/blended result between the current "
                       "image and reference image as shown in the viewport.");
        // ImGui::NewLine();

        ImGui::BeginGroup();

        // ImGui Combo using BeginCombo/EndCombo
        ImGui::PushFont(font("sans bold"), 0.f);
        ImGui::TextUnformatted("File format:");
        ImGui::PopFont();
        // ImGui::SetNextItemWidth(HelloImGui::EmSize(10.f));
        if (ImGui::BeginListBox("##File format", HelloImGui::EmToVec2(8.f, 17.f)))
        {
            for (int i = 0; i <= Format_Last; ++i)
            {
                if (!format_enabled[i])
                    continue;
                bool is_selected = (save_format == i);
                if (ImGui::Selectable(save_format_names[i], is_selected))
                    save_format = (Format_)i;
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }
        ImGui::EndGroup();

        // ImGui::Spacing();
        // ImGui::Indent();
        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::PushFont(font("sans bold"), 0.f);
        ImGui::TextUnformatted("Options:");
        ImGui::PopFont();

        std::function<void(const Image &, std::ostream &, const std::string_view)> save_func;
        // what the format writes unless one of its options says otherwise
        std::string save_extension = save_format_extensions[save_format];

        switch (save_format)
        {
        case Format_JPEG_LIBJPEG:
        {
            auto opts = jpg_parameters_gui();
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_jpg_image(img, os, filename, opts); };
        }
        break;

        // Two entries, so the file's extension and the codec inside it cannot disagree: AVIF is AV1 by
        // definition, while a .heif may hold HEVC, JPEG or JPEG 2000.
        case Format_HEIF:
        case Format_AVIF:
        {
            auto opts = heif_parameters_gui(save_format == Format_AVIF ? HEIFCodec::AV1 : HEIFCodec::HEIF);
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_heif_image(img, os, filename, opts); };
        }
        break;

        case Format_JPEG_UHDR:
        {
            auto opts = uhdr_parameters_gui();
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_uhdr_image(img, os, filename, opts); };
        }
        break;

        case Format_JPEG_XL:
        {
            auto opts = jxl_parameters_gui();
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_jxl_image(img, os, filename, opts); };
        }
        break;

        case Format_JPEG2000:
        {
            auto opts = j2k_parameters_gui();
            // the container is one of its options, so the extension follows what was chosen there
            save_extension = j2k_extension(opts);
            save_func      = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_j2k_image(img, os, filename, opts); };
        }
        break;

        case Format_WEBP:
        {
            auto opts = webp_parameters_gui();
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_webp_image(img, os, filename, opts); };
        }
        break;

        case Format_EXR:
        {
            auto opts = exr_parameters_gui(current_image());
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_exr_image(img, os, filename, opts); };
        }
        break;

        case Format_PFM:
        {
            auto opts = pfm_parameters_gui();
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_pfm_image(img, os, filename, opts); };
        }
        break;

        case Format_PNG_LIBPNG:
        {
            auto opts = png_parameters_gui();
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_png_image(img, os, filename, opts); };
        }
        break;

        case Format_QOI:
        {
            auto opts = qoi_parameters_gui();
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_qoi_image(img, os, filename, opts); };
        }
        break;

        case Format_JPEG_STB:
        {
            auto opts = stb_parameters_gui(false, true);
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_stb_jpg(img, os, filename, opts); };
        }
        break;

        case Format_BMP_STB:
        {
            auto opts = stb_parameters_gui(false, false);
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_stb_bmp(img, os, filename, opts); };
        }
        break;

        case Format_HDR_STB:
        {
            auto opts = stb_parameters_gui(true, false);
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_stb_hdr(img, os, filename, opts); };
        }
        break;

        case Format_PNG_STB:
        {
            auto opts = stb_parameters_gui(false, false);
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_stb_png(img, os, filename, opts); };
        }
        break;

        case Format_TGA_STB:
        {
            auto opts = stb_parameters_gui(false, false);
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_stb_tga(img, os, filename, opts); };
        }
        break;

        case Format_TIFF:
        {
            auto opts = tiff_parameters_gui();
            save_func = [opts](const Image &img, std::ostream &os, const std::string_view filename)
            { save_tiff_image(img, os, filename, opts); };
        }
        break;
        }

        ImGui::Dummy(HelloImGui::EmToVec2(25.f, 0.f)); // ensure minimum size even for no options
        ImGui::EndGroup();

        ImGui::Spacing();

        if (ImGui::Button("Cancel") ||
            (!ImGui::GetIO().NavVisible &&
             (ImGui::Shortcut(ImGuiKey_Escape) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Period))))
            ImGui::CloseCurrentPopup();

        ImGui::SameLine();

        string        filename;
        static string save_as_name;
        save_as_name = fmt::format("Save as {}...", save_format_names[save_format]).c_str();
        if (ImGui::Button(save_as_name.c_str()))
        {
            filename = current_image()->path.stem().u8string() + save_extension;
#if !defined(__EMSCRIPTEN__)
            filename = pfd::save_file(save_as_name.c_str(), filename,
                                      {string(save_format_names[save_format]) + " images", save_extension})
                           .result();
#endif
        }

        ImGui::SetItemDefaultFocus();

        if (!filename.empty())
        {
            ImGui::CloseCurrentPopup();
            try
            {
                ostringstream os;

                ImagePtr img = current_image();

                if (composite)
                {
                    img = make_shared<Image>(current_image()->size(), 4);
                    img->finalize();
                    auto bounds     = current_image()->data_window;
                    int  block_size = std::max(1, 1024 * 1024 / img->size().x);
                    parallel_for(blocked_range<int>(0, img->size().y, block_size),
                                 [this, &img, bounds](int begin_y, int end_y, int, int)
                                 {
                                     for (int y = begin_y; y < end_y; ++y)
                                         for (int x = 0; x < img->size().x; ++x)
                                         {
                                             float4 v = pixel_value(int2{x, y} + bounds.min, false, 2);

                                             img->channels[0](x, y) = v[0];
                                             img->channels[1](x, y) = v[1];
                                             img->channels[2](x, y) = v[2];
                                             img->channels[3](x, y) = v[3];
                                         }
                                 });
                }

                if (save_func)
                    save_func(*img, os, filename);
                else
                    throw runtime_error("No save function defined for this format.");

                string buffer = os.str();

#if !defined(__EMSCRIPTEN__)
                ofstream ofs{filename, ios_base::binary};
                ofs.write(buffer.data(), buffer.size());
#else
                emscripten_browser_file::download(
                    filename,                   // the default filename for the browser to save.
                    "application/octet-stream", // the MIME type of the data, treated as if it were a webserver
                                                // serving a file
                    string_view(buffer.data(), buffer.length()) // a buffer describing the data to download
                );
#endif

                // The file now holds what the image holds, so its edits count as saved. Not for a
                // composite, which bakes in exposure, tonemapping and the blend: what was written is a
                // rendition of the view, and reopening it would not give the image back.
                if (!composite)
                    current_image()->history.mark_saved();
            }
            catch (const exception &e)
            {
                spdlog::error("An error occurred while saving to '{}':\n\t{}.", filename, e.what());
            }
            catch (...)
            {
                spdlog::error("An unknown error occurred while saving to '{}'.", filename);
            }
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::load_images(const vector<string> &filenames)
{
    ImageLoadOptions opts = load_image_options();
    for (size_t i = 0; i < filenames.size(); ++i)
    {
        if (filenames[i].empty())
            continue;

        if (filenames[i][0] == ':')
        {
            opts.channel_selector = filenames[i].substr(1);
            spdlog::debug("Channel selector set to: {}", opts.channel_selector);
            continue;
        }

        if (to_lower(fs::path(filenames[i]).extension().string()) == ".hsess")
        {
            // a session file arrives by the same paths as an image (drag-and-drop, CLI args, Finder
            // "Open With", the "Open image..." dialog), so route it to session loading
            load_session(filenames[i]);
            continue;
        }

        // A .zip might be a session bundle, which background_load() checks for once it has the bytes in hand.
        load_image(filenames[i], std::nullopt, i == 0, opts);
    }
}

void HDRViewApp::open_image()
{
#if defined(__EMSCRIPTEN__)

    // due to this bug, we just allow all file types on safari:
    // https://stackoverflow.com/questions/72013027/safari-cannot-upload-file-w-unknown-mime-type-shows-tempimage,
    string extensions = host_is_safari() ? "*"
                                         : fmt::format(".{},.zip", fmt::join(Image::loadable_formats(), ",.")) +
                                               ",image/*" + ",application/zip";

    // open the browser's file selector, and pass the file to the upload handler
    spdlog::debug("Requesting file from user...");
    emscripten_browser_file::upload(
        extensions,
        [](const string &filename, const string &mime_type, string_view buffer, void *my_data = nullptr)
        {
            if (buffer.empty())
                spdlog::debug("User canceled upload.");
            else
            {
                spdlog::debug("User uploaded a {:.0h} file with filename '{}' of mime-type '{}'",
                              human_readible{buffer.size()}, filename, mime_type);

                // A zip might be a session bundle; background_load() checks for that.
                hdrview()->load_image(filename, buffer, true, load_image_options());
            }
        });
#else
    string extensions = fmt::format("*.{} *.zip", fmt::join(Image::loadable_formats(), " *."));

    load_images(pfd::open_file("Open image(s)", "", {"Image files", extensions}, pfd::opt::multiselect).result());
#endif
}

void HDRViewApp::open_folder()
{
#if !defined(__EMSCRIPTEN__)
    load_images({pfd::select_folder("Open images in folder", "").result()});
#endif
}

void HDRViewApp::open_session_bundle()
{
#if defined(__EMSCRIPTEN__)
    spdlog::debug("Requesting a session bundle from user...");
    emscripten_browser_file::upload(
        ".zip,application/zip",
        [](const string &filename, const string &mime_type, string_view buffer, void *my_data = nullptr)
        {
            if (buffer.empty())
            {
                spdlog::debug("User canceled upload.");
                return;
            }
            spdlog::debug("User uploaded a {:.0h} file with filename '{}' of mime-type '{}'",
                          human_readible{buffer.size()}, filename, mime_type);

            // Explicit "load a session" entry point: an error, not a silent fallback to plain image
            // loading, if the uploaded zip is not a session bundle.
            if (!hdrview()->try_load_zip_as_session(buffer, filename))
                spdlog::error("'{}' does not contain a session manifest at its root.", filename);
        });
#endif
}

// Note: the filename is passed by value in case its an element of m_recent_files, which we modify
void HDRViewApp::load_image(const string filename, std::optional<string_view> buffer, bool should_select,
                            const ImageLoadOptions &opts, ImagePtr to_replace)
{
    m_image_loader.background_load(filename, buffer, should_select, to_replace, opts);
}

void HDRViewApp::load_url(string_view url, bool should_select, ImagePtr to_replace, const ImageLoadOptions &opts)
{
    if (url.empty())
        return;

#if !defined(__EMSCRIPTEN__)
    spdlog::error("load_url only supported via emscripten");
#else
    spdlog::info("Entered URL: {}", url);

    // Everything the callbacks need travels through the payload: emscripten takes them as plain function
    // pointers, so they are captureless.
    struct Payload
    {
        string           url;
        HDRViewApp      *hdrview;
        bool             should_select;
        ImagePtr         to_replace;
        ImageLoadOptions opts;
    };
    auto data = new Payload{string(url), this, should_select, to_replace, opts};

    m_remaining_download = 100;
    emscripten_async_wget2_data(
        data->url.c_str(), "GET", nullptr, data, true,
        (em_async_wget2_data_onload_func)[](unsigned, void *data, void *buffer, unsigned buffer_size) {
            auto payload = reinterpret_cast<Payload *>(data);
            // copy out everything needed before the payload goes away
            string           url           = payload->url;
            bool             should_select = payload->should_select;
            ImagePtr         to_replace    = payload->to_replace;
            ImageLoadOptions opts          = payload->opts;
            delete payload;

            auto filename    = get_filename(url);
            auto char_buffer = reinterpret_cast<const char *>(buffer);
            spdlog::info("Downloaded file '{}' with size {} from url '{}'", filename, buffer_size, url);
            hdrview()->m_remaining_download = 0; // the last progress callback need not have reported it
            hdrview()->load_image(url, string_view{char_buffer, (size_t)buffer_size}, should_select, opts, to_replace);
        },
        (em_async_wget2_data_onerror_func)[](unsigned, void *data, int err, const char *desc) {
            auto   payload                         = reinterpret_cast<Payload *>(data);
            string url                             = payload->url; // copy the url
            payload->hdrview->m_remaining_download = 0;
            delete payload;

            spdlog::error("Downloading the file '{}' failed; {}: '{}'.", url, err, desc);
        },
        (em_async_wget2_data_onprogress_func)[](unsigned, void *data, int bytes_loaded, int total_bytes) {
            auto payload = reinterpret_cast<Payload *>(data);

            payload->hdrview->m_remaining_download = download_percent_remaining(bytes_loaded, total_bytes);
        });

    // emscripten_async_wget_data(
    //     data->url.c_str(), data,
    //     (em_async_wget_onload_func)[](void *data, void *buffer, int buffer_size) {
    //         auto   payload = reinterpret_cast<Payload *>(data);
    //         string url     = payload->url; // copy the url
    //         delete payload;

    //         auto filename    = get_filename(url);
    //         auto char_buffer = reinterpret_cast<const char *>(buffer);
    //         spdlog::info("Downloaded file '{}' with size {} from url '{}'", filename, buffer_size, url);
    //         hdrview()->load_image(url, {char_buffer, (size_t)buffer_size}, true);
    //     },
    //     (em_arg_callback_func)[](void *data) {
    //         auto   payload = reinterpret_cast<Payload *>(data);
    //         string url     = payload->url; // copy the url
    //         delete payload;

    //         spdlog::error("Downloading the file '{}' failed.", url);
    //     });
#endif
}

bool HDRViewApp::can_reload(const ConstImagePtr &image) const
{
    if (!image)
        return false;

    // A live image's pixels are pushed in by another process; there is no file to read again.
    if (image->is_live)
        return false;

#if defined(__EMSCRIPTEN__)
    // A URL can be fetched again; bytes the browser handed over once carry only a display name.
    return is_url(image->filename);
#else
    // Every way in reads from disk, so there is always something to read again. Whether it is still there
    // is reload_image()'s problem: this gates a shortcut, whose enabled() runs every frame per image.
    return true;
#endif
}

void HDRViewApp::reload_image(ImagePtr image, bool should_select)
{
    if (!image)
    {
        spdlog::warn("Tried to reload a null image");
        return;
    }

    if (!can_reload(image))
    {
        spdlog::warn("Cannot reload '{}': it was loaded from data with no source to read again.", image->filename);
        return;
    }

    spdlog::info("Reloading file '{}' with channel selector '{}'...", image->filename, image->channel_selector);
    auto opts                  = load_image_options();
    opts.channel_selector      = image->channel_selector;
    opts.override_transparency = image->transparency_override.has_value();
    if (image->transparency_override)
        opts.transparency_override = *image->transparency_override;

#if defined(__EMSCRIPTEN__)
    // A URL was never on a filesystem to be re-read from; fetch it again instead.
    if (is_url(image->filename))
    {
        load_url(image->filename, should_select, image, opts);
        return;
    }
#endif

    m_image_loader.background_load(image->filename, std::nullopt, should_select, image, opts);
}

void HDRViewApp::duplicate_image()
{
    auto img = current_image();
    if (!img)
        return;

    // With a selection, what is duplicated is the selection; the menu says which of the two it will do.
    auto copy = img->duplicate(m_roi);
    if (!copy)
        return;

    // A duplicate is not the file it came from; the part name keeps the Images panel able to tell them
    // apart while the file name still says where it came from.
    copy->partname = m_roi.has_volume() ? "selection" : "copy";

    // Annotations are in image coordinates, so they only land correctly on a copy of the whole image; a
    // duplicated selection is a different region and starts unmarked.
    if (!m_roi.has_volume())
        copy->annotations = img->annotations;

    // Nothing on disk holds this, so it counts as unsaved from the start and closing it will say so.
    copy->history = CommandHistory{true};

    // Not finalize(): duplicate() has already built the layers, and these samples came from an image that
    // was finalized once already, so premultiplying again would darken the copy.

    add_image_beside_current(copy, copy->partname);
}

void HDRViewApp::add_image_beside_current(ImagePtr img, const string &partname)
{
    if (!img)
        return;

    img->partname = partname;

    // Nothing on disk holds this, so it counts as unsaved from the start and closing it will say so.
    img->history = CommandHistory{true};

    const int index = current_image_index();
    if (is_valid(index))
        m_images.insert(m_images.begin() + index + 1, img);
    else
        m_images.push_back(img);

    set_current_image_index(is_valid(index) ? index + 1 : int(m_images.size()) - 1);

    update_visibility();
}

void HDRViewApp::close_image_immediately(int index)
{
    if (!is_valid(index))
        index = current_image_index();

    // If index is not valid, do nothing
    if (!is_valid(index) || m_images.empty())
        return;

    // Determine if the image being closed is current or reference
    bool closing_current   = (index == m_current);
    bool closing_reference = (index == m_reference);

    fs::path parent_path = fs::path(m_images[index]->filename).parent_path();
    auto     filename    = m_images[index]->filename;
    m_images.erase(m_images.begin() + index);

    try
    {
        parent_path = fs::weakly_canonical(parent_path);
    }
    catch (const std::exception &)
    {
        // path probably doesn't exist anymore
        parent_path = fs::path();
    }

    if (!parent_path.empty())
    {
#if !defined(__EMSCRIPTEN__)
        if (!m_active_directories.empty())
        {
            spdlog::debug("Active directories before closing image in '{}'.", parent_path.u8string());
            for (const auto &dir : m_active_directories) spdlog::debug("Active directory: {}", dir.u8string());
        }

        // Remove the parent directory from m_active_directories if no other images are from the same directory
        bool others_in_same_directory = false;
        for (const auto &img : m_images)
        {
            std::error_code ec;
            if (fs::equivalent(fs::path(img->filename).parent_path(), parent_path, ec))
            {
                others_in_same_directory = true;
                break;
            }
        }

        if (!others_in_same_directory)
            m_active_directories.erase(parent_path);

        if (!m_active_directories.empty())
        {
            spdlog::debug("Active directories after closing image in '{}'.", parent_path.u8string());
            for (const auto &dir : m_active_directories) spdlog::debug("Active directory: {}", dir.u8string());
        }

        spdlog::debug("Watched directories after closing image:");
        m_image_loader.remove_watched_directories(
            [this](const fs::path &path)
            {
                spdlog::debug("{} watched directory: {}",
                              m_active_directories.count(path) == 0 ? "Removing" : "Keeping", path.u8string());
                return m_active_directories.count(path) == 0;
            },
            /* keep_explicit */ true);
#endif
    }

    // Adjust indices after erasing the image
    if (closing_current)
    {
        // select the next image down the list
        int next = next_visible_image_index(index, Direction_Forward);
        if (next < index) // there is no visible image after this one, go to previous visible
            next = next_visible_image_index(index, Direction_Backward);
        set_current_image_index(next < index ? next : next - 1);
    }
    else if (m_current > index && m_current > 0)
    {
        // If current image index was after the erased image, decrement it
        set_current_image_index(m_current - 1);
    }
    // else: current image index remains unchanged

    if (closing_reference)
    {
        int next_ref = next_visible_image_index(index, Direction_Forward);
        if (next_ref < index)
            next_ref = next_visible_image_index(index, Direction_Backward);
        set_reference_image_index(next_ref < index ? next_ref : next_ref - 1);
    }
    else if (m_reference > index && m_reference > 0)
    {
        // If reference image index was after the erased image, decrement it
        set_reference_image_index(m_reference - 1);
    }
    // else: reference image index remains unchanged

    update_visibility(); // this also calls set_image_textures();
}

void HDRViewApp::close_all_images_immediately()
{
    // Nothing left for a queued filter to be applied to.
    m_filter_queue.clear();

    m_images.clear();
    m_current   = -1;
    m_reference = -1;
    m_active_directories.clear();
    // Only the folders opened alongside these images; one the user asked to watch stays watched, since it
    // is there for files that do not exist yet.
    m_image_loader.remove_watched_directories([](const fs::path &) { return true; }, /* keep_explicit */ true);
    update_visibility(); // this also calls set_image_textures();
}

json HDRViewApp::build_session_manifest(const std::function<string(ConstImagePtr)> &path_of) const
{
    json j;
    j["type"]    = "HDRView session";
    j["version"] = fmt::format("{}.{}.{}", version_major(), version_minor(), version_patch());

    // A live image's pixels come from a running process, so a session, which records where to find its
    // images again, has nothing to write for it. Leaving it out renumbers the remaining entries, hence the
    // map from image index to manifest entry for the current/reference indices below.
    json             images = json::array();
    std::vector<int> entry_of_image(m_images.size(), -1);
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        const auto &img = m_images[i];
        if (img->is_live)
            continue;

        entry_of_image[i] = int(images.size());

        json entry;
        entry["path"]             = path_of(img);
        entry["channel_selector"] = img->channel_selector;
        if (img->transparency_override)
            entry["transparency_override"] = enum_to_id(*img->transparency_override, g_transparency_type_ids);
        entry["selected_group"]  = img->selected_group;
        entry["reference_group"] = img->reference_group;

        // The multi-selection, by channel name: a group index means whatever the rebuild after loading
        // makes it mean, while the names are what the grouping is derived from.
        json selected_channels = json::array();
        for (const auto &c : img->channels)
            if (c.selected)
                selected_channels.push_back(c.name);
        entry["selected_channels"] = selected_channels;

        // The user's own markup, in the order it draws in. Written even when empty is not worth it, so a
        // session from an image with none says nothing about them.
        if (!img->annotations.empty())
            entry["annotations"] = img->annotations;

        images.push_back(entry);
    }
    j["images"] = images;
    // Indices into "images", not paths: the same file can be listed more than once, so a path alone can't
    // identify which occurrence is current/reference. -1 when there is no such image.
    auto entry_index = [&](int idx) { return idx >= 0 && idx < (int)entry_of_image.size() ? entry_of_image[idx] : -1; };
    j["current"]     = entry_index(image_index(current_image()));
    j["reference"]   = entry_index(image_index(reference_image()));

    j["blend_mode"] = enum_to_id(m_blend_mode, g_blend_mode_ids);

    json view;
    view["exposure"]           = m_exposure;
    view["gamma"]              = m_gamma;
    view["offset"]             = m_offset;
    view["tonemap"]            = enum_to_id(m_tonemap, g_tonemap_ids);
    view["channel"]            = enum_to_id(m_channel, g_channel_ids);
    view["colormap_index"]     = m_colormap_index;
    view["reverse_colormap"]   = m_reverse_colormap;
    view["clamp_to_LDR"]       = m_clamp_to_LDR;
    view["dither"]             = m_dither;
    view["bg_mode"]            = enum_to_id(m_bg_mode, g_bg_mode_ids);
    view["bg_color"]           = m_bg_color.xyz();
    view["zoom"]               = m_zoom;
    view["translate"]          = m_translate;
    view["flip"]               = m_flip;
    view["auto_fit_display"]   = m_auto_fit_display;
    view["auto_fit_data"]      = m_auto_fit_data;
    view["auto_fit_selection"] = m_auto_fit_selection;
    view["draw_grid"]          = m_draw_grid;
    view["draw_pixel_info"]    = m_draw_pixel_info;
    view["clip_warnings"]      = m_clip_warnings;
    view["clip_range"]         = m_clip_range;
    view["roi"]                = json::array();
    view["roi"].push_back(m_roi.min);
    view["roi"].push_back(m_roi.max);
    j["view"] = view;

    return j;
}

void HDRViewApp::save_session()
{
    if (m_images.empty())
        return;

#if !defined(__EMSCRIPTEN__)
    string filename = pfd::save_file("Save session...", "session.hsess", {"HDRView session", "*.hsess"}).result();
    if (filename.empty())
        return;

    fs::path dir = fs::path(filename).parent_path();

    json j = build_session_manifest(
        [&dir](ConstImagePtr img) -> string
        {
            if (!img)
                return "";
            std::error_code ec;
            fs::path        rel = fs::relative(img->path, dir, ec);
            return (ec ? img->path : rel).generic_u8string();
        });

    try
    {
        ofstream ofs{filename};
        ofs << j.dump(4);
        spdlog::info("Saved session to '{}'.", filename);
    }
    catch (const exception &e)
    {
        spdlog::error("Failed to save session to '{}': {}.", filename, e.what());
    }
#endif
}

void HDRViewApp::export_session_bundle()
{
    if (m_images.empty())
        return;

#if !defined(__EMSCRIPTEN__)
    string filename =
        pfd::save_file("Export session bundle...", "session.hsess.zip", {"HDRView session bundle", "*.zip"}).result();
    if (filename.empty())
        return;

    // Flatten every image into "images/<index>_<original filename>" inside the bundle; the index prefix
    // keeps two source images that share a filename from colliding.
    vector<string> archive_paths(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i)
        archive_paths[i] = fmt::format("images/{:03d}_{}", i, m_images[i]->path.filename().u8string());

    json j = build_session_manifest([&](ConstImagePtr img) -> string { return archive_paths[image_index(img)]; });

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, filename.c_str(), 0))
    {
        spdlog::error("Failed to create '{}'.", filename);
        return;
    }

    string manifest_text = j.dump(4);
    bool   ok =
        mz_zip_writer_add_mem(&zip, "session.hsess", manifest_text.data(), manifest_text.size(), MZ_DEFAULT_LEVEL);
    for (size_t i = 0; ok && i < m_images.size(); ++i)
    {
        if (m_images[i]->is_live)
            continue; // no file behind it, and build_session_manifest() left it out too

        string source = m_images[i]->path.u8string();
        string source_zip, entry_path;
        if (split_zip_entry(source, source_zip, entry_path) && !entry_path.empty())
        {
            // This image's source is an entry inside another zip, not a standalone file on disk, so
            // re-extract its bytes from that zip and embed them directly.
            ifstream ifs{fs::u8path(source_zip), std::ios::binary};
            string   zip_bytes{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
            auto     bytes = ifs ? zip_extract_entry(zip_bytes, entry_path) : std::nullopt;
            if (!bytes)
            {
                spdlog::warn("Could not re-extract '{}' from '{}'; omitting it from the exported bundle.", entry_path,
                             source_zip);
                continue;
            }
            ok = mz_zip_writer_add_mem(&zip, archive_paths[i].c_str(), bytes->data(), bytes->size(), MZ_DEFAULT_LEVEL);
            continue;
        }

        if (!fs::exists(m_images[i]->path))
        {
            spdlog::warn("'{}' no longer exists; omitting it from the exported bundle.", source);
            continue;
        }
        ok = mz_zip_writer_add_file(&zip, archive_paths[i].c_str(), m_images[i]->path.string().c_str(), nullptr, 0,
                                    MZ_DEFAULT_LEVEL);
    }
    if (ok)
        ok = mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);

    if (ok)
        spdlog::info("Exported session bundle to '{}'.", filename);
    else
    {
        spdlog::error("Failed to write '{}'.", filename);
        std::error_code ec;
        fs::remove(filename, ec); // don't leave a corrupt/partial zip behind
    }
#endif
}

/// A parsed session file waiting on the user to confirm closing the currently-open images.
struct HDRViewApp::UnconfirmedSession
{
    json     j;
    fs::path dir;       ///< Where a plain .hsess's entries are relative to
    string   zip_bytes; ///< Empty for a plain .hsess; begin_session_load() needs these again after the confirm
    string   zip_name;
};

/// A session whose images have been issued to the loader and are loading asynchronously.
struct HDRViewApp::LoadingSession
{
    struct Entry
    {
        fs::path                    path;
        string                      channel_selector;
        optional<TransparencyType_> transparency_override; ///< Empty when the file's own interpretation was used
        int                         selected_group = 0, reference_group = 0;
        vector<string>              selected_channels; ///< The multi-selection, by channel name; see Channel::selected
        vector<Annotation>          annotations;       ///< The user's markup over this image, in draw order
        ImagePtr loaded; ///< Set once this entry's image arrives; still null => not yet arrived, or failed
    };
    vector<Entry> entries;                                  ///< One per saved "images" entry, in file order
    int           current_index = -1, reference_index = -1; ///< Index into entries, or -1 if unset
    BlendMode_    blend_mode;
    json          view; ///< The session file's "view" sub-object, applied verbatim once loading completes

    // An arrival fills the earliest entry sharing its load options. Entries sharing a key ask for identical
    // content, so any one-to-one assignment is right whatever order the loads finish in.
    using Key =
        std::tuple<fs::path, string, optional<TransparencyType_>>; ///< path, channel_selector, transparency_override
    map<Key, deque<int>> unresolved;
};

void HDRViewApp::load_session()
{
#if !defined(__EMSCRIPTEN__)
    auto selected = pfd::open_file("Load session...", "", {"HDRView session", "*.hsess *.zip"}).result();
    if (selected.empty())
        return;
    load_session(selected.front());
#endif
}

void HDRViewApp::load_session(const string &filename)
{
#if !defined(__EMSCRIPTEN__)
    if (to_lower(fs::path(filename).extension().string()) == ".zip")
    {
        // An explicit "load a session" action: an error if the zip is not a bundle, unlike a zip opened
        // generically via drag-and-drop or "Open image...", which falls back to plain image loading.
        ifstream ifs{filename, ios::binary};
        string   bytes;
        if (ifs)
            bytes.assign((istreambuf_iterator<char>(ifs)), istreambuf_iterator<char>());
        if (bytes.empty() || !try_load_zip_as_session(bytes, filename))
            spdlog::error("'{}' does not contain a session manifest at its root.", filename);
        return;
    }

    spdlog::info("Loading session from '{}'...", filename);

    json j;
    try
    {
        ifstream ifs{filename};
        if (!ifs)
            throw runtime_error("could not open file");
        ifs >> j;
    }
    catch (const exception &e)
    {
        spdlog::error("Failed to parse session file '{}': {}.", filename, e.what());
        return;
    }

    if (!validate_session_manifest(j, filename))
        return;

    m_image_loader.add_recent_file(filename);

    fs::path dir = fs::path(filename).parent_path();

    UnconfirmedSession load{j, dir, {}, {}};

    if (!m_images.empty())
    {
        m_unconfirmed_session           = std::make_shared<UnconfirmedSession>(std::move(load));
        dialog("Replace session?").open = true;
        return;
    }

    begin_session_load(load);
#endif
}

bool HDRViewApp::try_load_zip_as_session(string_view zip_bytes, const string &zip_name)
{
    auto candidates = zip_root_entries_with_suffix(zip_bytes, ".hsess");
    if (candidates.empty())
        return false;

    if (candidates.size() > 1)
    {
        string names;
        for (auto &c : candidates) names += (names.empty() ? "" : ", ") + c.first;
        spdlog::warn("'{}' contains multiple session manifests at its root ({}); using '{}'.", zip_name, names,
                     candidates.front().first);
    }

    json j;
    try
    {
        j = json::parse(candidates.front().second);
    }
    catch (const exception &e)
    {
        spdlog::error("Found '{}' at the root of '{}', but failed to parse it as a session: {}.",
                      candidates.front().first, zip_name, e.what());
        return false;
    }

    if (!validate_session_manifest(j, zip_name))
        return false;

    spdlog::info("'{}' is a session bundle (manifest '{}'); loading it as a session, not as a folder of images.",
                 zip_name, candidates.front().first);
    m_image_loader.add_recent_file(zip_name);

    UnconfirmedSession load{j, {}, string(zip_bytes), zip_name};

    if (!m_images.empty())
    {
        m_unconfirmed_session           = std::make_shared<UnconfirmedSession>(std::move(load));
        dialog("Replace session?").open = true;
        return true;
    }

    begin_session_load(load);
    return true;
}

void HDRViewApp::begin_session_load(const UnconfirmedSession &load)
{
    // Loading a session was already confirmed, by the prompt that warned it would replace what is
    // open; asking a second time here would stall a load that is already underway.
    close_all_images_immediately();

    const json &j        = load.j;
    const bool  from_zip = !load.zip_bytes.empty();

    auto resolve = [&load](const string &rel) -> fs::path
    {
        if (rel.empty())
            return {};
        std::error_code ec;
        fs::path        abs = fs::weakly_canonical(load.dir / fs::u8path(rel), ec);
        return ec ? (load.dir / fs::u8path(rel)) : abs;
    };

    LoadingSession pending;
    pending.blend_mode = id_to_enum(j, "blend_mode", g_blend_mode_ids, BlendMode_Normal);
    pending.view       = j.value("view", json::object());

    for (auto &entry : j.value("images", json::array()))
    {
        string rel = entry.value<string>("path", "");
        if (rel.empty())
            continue;

        // A bundled entry takes the same "zip_name/entry_path" synthetic identity regular zip-loaded images
        // get (see extract_and_schedule() in image_loader.cpp), so "reveal in file manager" and
        // reload_image() need no session-specific handling.
        LoadingSession::Entry e;
        e.path                  = from_zip ? fs::path(load.zip_name) / fs::u8path(rel) : resolve(rel);
        e.channel_selector      = entry.value<string>("channel_selector", "");
        e.transparency_override = transparency_override_from_id(entry);
        e.selected_group        = entry.value<int>("selected_group", 0);
        e.reference_group       = entry.value<int>("reference_group", 0);
        e.selected_channels     = entry.value<vector<string>>("selected_channels", {});
        e.annotations           = entry.value<vector<Annotation>>("annotations", {});

        int idx = (int)pending.entries.size();
        pending.entries.push_back(e);

        optional<string> bytes;
        if (from_zip)
        {
            bytes = zip_extract_entry(load.zip_bytes, rel);
            if (!bytes)
            {
                // Never issued to the loader, so it stays unresolved and finish_loading_session() reports it
                // as a load failure, as it does a missing file on disk.
                spdlog::warn("Session bundle '{}' references '{}', but it isn't present in the zip.", load.zip_name,
                             rel);
                continue;
            }
        }

        pending.unresolved[{e.path, e.channel_selector, e.transparency_override}].push_back(idx);

        ImageLoadOptions opts;
        opts.channel_selector      = e.channel_selector;
        opts.override_transparency = e.transparency_override.has_value();
        if (e.transparency_override)
            opts.transparency_override = *e.transparency_override;
        if (from_zip)
            m_image_loader.background_load(e.path.string(), *bytes, false, nullptr, opts);
        else
            load_image(e.path.string(), {}, false, opts);
    }

    int n                   = (int)pending.entries.size();
    pending.current_index   = clamp(j.value<int>("current", -1), -1, n - 1);
    pending.reference_index = clamp(j.value<int>("reference", -1), -1, n - 1);

    m_loading_session                 = std::make_shared<LoadingSession>(std::move(pending));
    dialog("Loading session...").open = true;
}

void HDRViewApp::resolve_loading_session_image(const ImagePtr &new_image)
{
    if (!m_loading_session)
        return;

    // Resolve this arrival to the earliest not-yet-filled entry sharing its load options; see LoadingSession
    // for why that key is the right one to match on.
    auto key = LoadingSession::Key{new_image->path, new_image->channel_selector, new_image->transparency_override};
    auto it  = m_loading_session->unresolved.find(key);
    if (it == m_loading_session->unresolved.end())
        return;

    if (!it->second.empty())
    {
        int entry_idx = it->second.front();
        it->second.pop_front();
        m_loading_session->entries[entry_idx].loaded = new_image;
    }
    if (it->second.empty())
        m_loading_session->unresolved.erase(it);
}

void HDRViewApp::finish_loading_session()
{
    if (!m_loading_session)
        return;

    auto &entries = m_loading_session->entries;

    for (auto &e : entries)
        if (!e.loaded)
            spdlog::warn("Session referenced '{}', but it failed to load.", e.path.u8string());
        else
        {
            // Group indices are whatever the file holds, and the image they name is only known now that it
            // has loaded. reference_group is left unchecked: every reader guards it with is_valid_group(),
            // and -1 is the "no reference group" state update_visibility() assigns.
            e.loaded->selected_group  = clamp(e.selected_group, 0, std::max(0, (int)e.loaded->groups.size() - 1));
            e.loaded->reference_group = e.reference_group;

            // A name the image no longer has selects nothing, and a session with no "selected_channels"
            // leaves every channel unselected; update_visibility() below then collapses the selection onto
            // the group each image is showing.
            for (auto &c : e.loaded->channels)
                c.selected = std::find(e.selected_channels.begin(), e.selected_channels.end(), c.name) !=
                             e.selected_channels.end();

            // In image coordinates, so they need nothing from the image to be placed correctly.
            e.loaded->annotations = e.annotations;
        }

    // Rebuild m_images in the saved order: images arrive in whatever order their background loads finish,
    // and the same path can appear more than once, so entries -> loaded is what carries order and identity.
    m_images.clear();
    for (auto &e : entries)
        if (e.loaded)
            m_images.push_back(e.loaded);

    // current_index/reference_index index into `entries`, not m_images, so bounds-check against that
    // rather than with is_valid().
    auto entry_loaded = [&entries](int idx) -> ImagePtr
    { return (idx >= 0 && idx < (int)entries.size()) ? entries[idx].loaded : nullptr; };

    m_current = m_reference = -1;
    if (auto img = entry_loaded(m_loading_session->current_index))
        m_current = image_index(img);
    if (auto img = entry_loaded(m_loading_session->reference_index))
        m_reference = image_index(img);

    m_blend_mode = m_loading_session->blend_mode;

    const json &view = m_loading_session->view;
    m_exposure_live = m_exposure = view.value<float>("exposure", m_exposure);
    // Only the floor; see MIN_GAMMA. Exposure and offset have no unsafe values, and a session has to carry
    // back whatever Ctrl+click entry and the keyboard shortcuts can set.
    m_gamma_live = m_gamma = std::max(MIN_GAMMA, view.value<float>("gamma", m_gamma));
    m_offset_live = m_offset = view.value<float>("offset", m_offset);
    m_tonemap                = id_to_enum(view, "tonemap", g_tonemap_ids, m_tonemap);
    m_channel                = id_to_enum(view, "channel", g_channel_ids, m_channel);
    m_colormap_index =
        clamp<int>(view.value<int>("colormap_index", m_colormap_index), 0, (int)std::size(m_colormaps) - 1);
    m_reverse_colormap = view.value<bool>("reverse_colormap", m_reverse_colormap);
    m_clamp_to_LDR     = view.value<bool>("clamp_to_LDR", m_clamp_to_LDR);
    m_dither           = view.value<bool>("dither", m_dither);
    m_bg_mode          = id_to_enum(view, "bg_mode", g_bg_mode_ids, m_bg_mode);
    m_bg_color.xyz()   = view.value<float3>("bg_color", m_bg_color.xyz());
    set_zoom(view.value<float>("zoom", m_zoom));
    m_translate          = view.value<float2>("translate", m_translate);
    m_flip               = view.value<bool2>("flip", m_flip);
    m_auto_fit_display   = view.value<bool>("auto_fit_display", m_auto_fit_display);
    m_auto_fit_data      = view.value<bool>("auto_fit_data", m_auto_fit_data);
    m_auto_fit_selection = view.value<bool>("auto_fit_selection", m_auto_fit_selection);
    m_draw_grid          = view.value<bool>("draw_grid", m_draw_grid);
    m_draw_pixel_info    = view.value<bool>("draw_pixel_info", m_draw_pixel_info);
    // "draw_clip_warnings" is the older key, a single toggle covering both ends; fall back to it so sessions
    // written by an earlier version still restore their clip warnings
    bool both       = view.value<bool>("draw_clip_warnings", false);
    m_clip_warnings = view.value<bool2>("clip_warnings", bool2{both, both});
    m_clip_range    = view.value<float2>("clip_range", m_clip_range);
    if (view.contains("roi") && view["roi"].is_array() && view["roi"].size() == 2)
    {
        view["roi"][0].get_to(m_roi.min);
        view["roi"][1].get_to(m_roi.max);
        m_roi.make_valid(); // the file's two corners need not arrive in that order
    }
    m_roi_live = m_roi;

    m_request_sort = true;
    m_loading_session.reset();

    // m_images was rebuilt above and m_visible_images indexes into it, so leaving the old indices in place
    // would walk off the end of the new vector.
    update_visibility();
}

void HDRViewApp::draw_confirm_load_session_dialog(bool &open)
{
    auto result = ImGui::ConfirmDialog("Replace session?", open,
                                       "Loading this session will close all currently open images.", "Replace");
    if (result == ImGui::DialogResult::Cancel)
        m_unconfirmed_session.reset();
    else if (result == ImGui::DialogResult::Confirm)
    {
        if (m_unconfirmed_session)
            begin_session_load(*m_unconfirmed_session);
        m_unconfirmed_session.reset();
    }
}

void HDRViewApp::draw_loading_session_dialog(bool &open)
{
    if (ImGui::BeginModalDialog("Loading session...", open, ImGui::DialogPosition::Center,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        if (m_loading_session)
        {
            int total = (int)m_loading_session->entries.size();
            int done  = 0;
            for (auto &e : m_loading_session->entries)
                if (e.loaded)
                    ++done;
            ImGui::Text("Loading session... (%d/%d)", done, total);
        }
        else
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}
