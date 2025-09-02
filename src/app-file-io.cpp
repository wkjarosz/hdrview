#include "app.h"

#include "fonts.h"
#include "image.h"
#include <fstream>
#include <sstream>

#include "imageio/exr.h"
#include "imageio/jpg.h"
#include "imageio/pfm.h"
#include "imageio/png.h"
#include "imageio/qoi.h"
#include "imageio/stb.h"
#include "imageio/uhdr.h"
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

void HDRViewApp::draw_save_as_dialog(bool &open)
{
    if (open)
        ImGui::OpenPopup("Save as...");

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2, 5.f * HelloImGui::EmSize()),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2{HelloImGui::EmSize(29), 0}, ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Save as...", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        open = false;

        static int   composite         = 0;
        static bool  progressive       = false;
        static bool  dither            = true;
        static float quality           = 95.f;
        static float gainmap_quality   = 95.f;
        static bool  use_multi_channel = false;
        static int   gainmap_scale     = 1;
        static float gainmap_gamma     = 1.0f;
        static bool  interlaced        = false;
        static int   colorspace        = 1;

        // Define enum for save formats
        enum Format_
        {
            Format_BMP_STB,
            Format_HDR_STB,
#ifdef HDRVIEW_ENABLE_LIBJPEG
            Format_JPEG_LIBJPEG,
#endif
            Format_JPEG_STB,
#ifdef HDRVIEW_ENABLE_UHDR
            Format_JPEG_UHDR,
#endif
            // Format_JPEG_XL,
            Format_EXR,
            Format_PFM,
#ifdef HDRVIEW_ENABLE_LIBPNG
            Format_PNG_LIBPNG,
#endif
            Format_PNG_STB,
            Format_QOI,
            Format_TGA_STB,
            Format_Last = Format_TGA_STB
        };
        static Format_ save_format = Format_EXR;

        // Array of format names
        // clang-format off
        static const char *save_format_names[Format_Last + 1] = {
            "BMP (stb)",
            "HDR (stb)",
#ifdef HDRVIEW_ENABLE_LIBJPEG
            "JPEG (libjpeg)",
#endif
            "JPEG (stb)",
#ifdef HDRVIEW_ENABLE_UHDR
            "JPEG (UltraHDR)",
#endif
            // "JPEG-XL",
            "OpenEXR",
            "PFM",
#ifdef HDRVIEW_ENABLE_LIBPNG
            "PNG (libpng)",
#endif
            "PNG (stb)",
            "QOI",
            "TGA (stb)"
            };
        // clang-format on

        // filename extensions for each of the above
        // clang-format off
        static const char *save_format_extensions[Format_Last + 1] = {
            ".bmp",
            ".hdr",
#ifdef HDRVIEW_ENABLE_LIBJPEG
            ".jpg",
#endif
            ".jpg",
#ifdef HDRVIEW_ENABLE_UHDR
            ".jxl",
#endif
            // ".jpeg-xl",
            ".exr",
            ".pfm",
#ifdef HDRVIEW_ENABLE_LIBPNG
            ".png",
#endif
            ".png",
            ".qoi",
            ".tga"
        };
        // clang-format on

        // ImGui::PushItemWidth(-HelloImGui::EmSize(10));

        ImGui::Combo("Image to export", &composite, "Current image\0Current/Reference composite image\0");
        ImGui::WrappedTooltip("Save either the current image, or the composited/blended result between the current "
                              "image and reference image as shown in the viewport.");

        // ImGui Combo using BeginCombo/EndCombo
        if (ImGui::BeginCombo("File format", save_format_names[save_format]))
        {
            for (int i = 0; i <= Format_Last; ++i)
            {
                bool is_selected = (save_format == i);
                if (ImGui::Selectable(save_format_names[i], is_selected))
                    save_format = (Format_)i;
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Indent();

        if (ImGui::CollapsingHeader("Options"))
        {
            switch (save_format)
            {
            case Format_HDR_STB: break;
#ifdef HDRVIEW_ENABLE_LIBJPEG
            case Format_JPEG_LIBJPEG:
                ImGui::Combo("Colorspace", &colorspace, "Linear\0sRGB\0");
                ImGui::Checkbox("Dither", &dither);
                ImGui::SliderFloat("Quality", &quality, 1.f, 100.f);
                ImGui::Checkbox("Progressive", &progressive);
                break;
#endif
#ifdef HDRVIEW_ENABLE_UHDR
            case Format_JPEG_UHDR:
                ImGui::SliderFloat("Base image quality", &quality, 1.f, 100.f);
                ImGui::SliderFloat("Gain map quality", &gainmap_quality, 1.f, 100.f);
                ImGui::Checkbox("Use multi-channel gainmap", &use_multi_channel);
                ImGui::SliderInt("Gain map scale factor", &gainmap_scale, 1, 5);
                ImGui::WrappedTooltip("The factor by which to reduce the resolution of the gainmap.");
                ImGui::SliderFloat("Gain map gamma", &gainmap_gamma, 0.1f, 5.0f);
                break;
#endif
            // case Format_JPEG_XL: break;
            case Format_EXR: break;
            case Format_PFM: break;
#ifdef HDRVIEW_ENABLE_LIBPNG
            case Format_PNG_LIBPNG:
                ImGui::Combo("Colorspace", &colorspace, "Linear\0sRGB\0");
                ImGui::Checkbox("Dither", &dither);
                ImGui::Checkbox("Interlaced", &interlaced);
                break;
#endif
            case Format_JPEG_STB:
                ImGui::SliderFloat("Quality", &quality, 1.f, 100.f);
                ImGui::Combo("Colorspace", &colorspace, "Linear\0sRGB\0");
                ImGui::Checkbox("Dither", &dither);
                break;
            case Format_QOI: [[fallthrough]];
            case Format_PNG_STB: [[fallthrough]];
            case Format_TGA_STB: [[fallthrough]];
            case Format_BMP_STB:
                ImGui::Combo("Colorspace", &colorspace, "Linear\0sRGB\0");
                ImGui::Checkbox("Dither", &dither);
                break;
            }
        }

        ImGui::Unindent();

        ImGui::Spacing();

        if (ImGui::Button("Cancel") || ImGui::GlobalShortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteOverActive) ||
            ImGui::GlobalShortcut(ImGuiMod_Ctrl | ImGuiKey_Period, ImGuiInputFlags_RouteOverActive))
            ImGui::CloseCurrentPopup();

        ImGui::SameLine();

        string        filename;
        static string save_as_name;
        save_as_name = fmt::format("Save as {}...", save_format_names[save_format]).c_str();
        if (ImGui::Button(save_as_name.c_str()))
        {
#if !defined(__EMSCRIPTEN__)
            filename = pfd::save_file(
                           save_as_name.c_str(), "",
                           {string(save_format_names[save_format]) + " images", save_format_extensions[save_format]})
                           .result();
#else
            filename = "output" + string(save_format_extensions[save_format]);
#endif
        }

        ImGui::SetItemDefaultFocus();

        if (!filename.empty())
        {
            ImGui::CloseCurrentPopup();
            try
            {
                ostringstream os;
                float         gain = powf(2.0f, m_exposure_live);

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

                switch (save_format)
                {
                case Format_BMP_STB: save_stb_bmp(*img, os, filename, gain, colorspace == 1, dither); break;
                case Format_HDR_STB: save_stb_hdr(*img, os, filename); break;
#ifdef HDRVIEW_ENABLE_LIBJPEG
                case Format_JPEG_LIBJPEG:
                    save_jpg_image(*img, os, filename, gain, colorspace == 1, dither, quality, progressive);
                    break;
#endif
                case Format_JPEG_STB: save_stb_jpg(*img, os, filename, gain, colorspace == 1, dither, quality); break;
#ifdef HDRVIEW_ENABLE_UHDR
                case Format_JPEG_UHDR:
                    save_uhdr_image(*img, os, filename, gain, quality, gainmap_quality, use_multi_channel,
                                    gainmap_scale, gainmap_gamma);
                    break;
#endif
                case Format_EXR: save_exr_image(*img, os, filename); break;
                case Format_PFM: save_pfm_image(*img, os, filename, gain); break;
#ifdef HDRVIEW_ENABLE_LIBPNG
                case Format_PNG_LIBPNG:
                    save_png_image(*img, os, filename, gain, colorspace == 1, dither, interlaced);
                    break;
#endif
                case Format_PNG_STB: save_stb_png(*img, os, filename, gain, colorspace == 1, dither); break;
                case Format_QOI: save_qoi_image(*img, os, filename, gain, colorspace == 1, dither); break;
                case Format_TGA_STB: save_stb_tga(*img, os, filename, gain, colorspace == 1, dither); break;
                }

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
    string channel_selector = "";
    for (size_t i = 0; i < filenames.size(); ++i)
    {
        if (filenames[i].empty())
            continue;

        if (filenames[i][0] == ':')
        {
            channel_selector = filenames[i].substr(1);
            spdlog::debug("Channel selector set to: {}", channel_selector);
            continue;
        }

        load_image(filenames[i], {}, i == 0, channel_selector);
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
                auto [size, unit] = human_readable_size(buffer.size());
                spdlog::debug("User uploaded a {:.0f} {} file with filename '{}' of mime-type '{}'", size, unit,
                              filename, mime_type);
                hdrview()->load_image(filename, buffer, true);
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

// Note: the filename is passed by value in case its an element of m_recent_files, which we modify
void HDRViewApp::load_image(const string filename, const string_view buffer, bool should_select,
                            const string channel_selector)
{
    m_image_loader.background_load(filename, buffer, should_select, nullptr, channel_selector);
}

void HDRViewApp::load_url(const string_view url)
{
    if (url.empty())
        return;

#if !defined(__EMSCRIPTEN__)
    spdlog::error("load_url only supported via emscripten");
#else
    spdlog::info("Entered URL: {}", url);

    struct Payload
    {
        string      url;
        HDRViewApp *hdrview;
    };
    auto data = new Payload{string(url), this};

    m_remaining_download = 100;
    emscripten_async_wget2_data(
        data->url.c_str(), "GET", nullptr, data, true,
        (em_async_wget2_data_onload_func)[](unsigned, void *data, void *buffer, unsigned buffer_size) {
            auto   payload = reinterpret_cast<Payload *>(data);
            string url     = payload->url; // copy the url
            delete payload;

            auto filename    = get_filename(url);
            auto char_buffer = reinterpret_cast<const char *>(buffer);
            spdlog::info("Downloaded file '{}' with size {} from url '{}'", filename, buffer_size, url);
            hdrview()->load_image(url, {char_buffer, (size_t)buffer_size}, true);
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

            payload->hdrview->m_remaining_download = (total_bytes - bytes_loaded) / total_bytes;
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

void HDRViewApp::reload_image(ImagePtr image, bool should_select)
{
    if (!image)
    {
        spdlog::warn("Tried to reload a null image");
        return;
    }

    spdlog::info("Reloading file '{}' with channel selector '{}'...", image->filename, image->channel_selector);
    m_image_loader.background_load(image->filename, {}, should_select, image, image->channel_selector);
}

void HDRViewApp::close_image(int index)
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
    catch (const std::exception &e)
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
            });
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

void HDRViewApp::close_all_images()
{
    m_images.clear();
    m_current   = -1;
    m_reference = -1;
    m_active_directories.clear();
    m_image_loader.remove_watched_directories([](const fs::path &path) { return true; });
    update_visibility(); // this also calls set_image_textures();
}
