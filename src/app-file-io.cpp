#include "app.h"

#include "colorspace.h"
#include "fonts.h"
#include "hello_imgui/dpi_aware.h"
#include "image.h"
#include <fstream>
#include <sstream>
#include <string>

#include "imageio/exr.h"
#include "imageio/heif.h"
#include "imageio/jpg.h"
#include "imageio/jxl.h"
#include "imageio/pfm.h"
#include "imageio/png.h"
#include "imageio/qoi.h"
#include "imageio/stb.h"
#include "imageio/uhdr.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "imgui_internal.h"

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
    // ImGui::SetNextWindowSize(ImVec2{HelloImGui::EmSize(29), 0}, ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Save as...", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        open = false;

        // Define enum for save formats
        enum Format_
        {
            Format_BMP_STB = 0,
            Format_HDR_STB,
            Format_HEIF_AVIF,
            Format_JPEG_LIBJPEG,
            Format_JPEG_STB,
            Format_JPEG_UHDR,
            Format_JPEG_XL,
            Format_EXR,
            Format_PFM,
            Format_PNG_LIBPNG,
            Format_PNG_STB,
            Format_QOI,
            Format_TGA_STB,
            Format_Last = Format_TGA_STB
        };
        static Format_ save_format = Format_EXR;

        static bool format_enabled[Format_Last + 1] = {true, true,
#ifdef HDRVIEW_ENABLE_HEIF
                                                       true,
#else
                                                       false,
#endif
#ifdef HDRVIEW_ENABLE_LIBJPEG
                                                       true,
#else
                                                       false,
#endif
                                                       true,
#ifdef HDRVIEW_ENABLE_UHDR
                                                       true,
#else
                                                       false,
#endif
#ifdef HDRVIEW_ENABLE_JPEGXL
                                                       true,
#else
                                                       false,
#endif
                                                       true, true,
#ifdef HDRVIEW_ENABLE_LIBPNG
                                                       true,
#else
                                                       false,
#endif
                                                       true, true, true};

        // Array of format names
        // clang-format off
        static const char *save_format_names[Format_Last + 1] = {
            "BMP (stb)",
            "HDR (stb)",
            "HEIF/AVIF",
            "JPEG (libjpeg)",
            "JPEG (stb)",
            "JPEG (UltraHDR)",
            "JPEG-XL",
            "OpenEXR",
            "PFM",
            "PNG (libpng)",
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
            ".heif",
            ".jpg",
            ".jpg",
            ".jpg",
            ".jxl",
            ".exr",
            ".pfm",
            ".png",
            ".png",
            ".qoi",
            ".tga"
        };
        // clang-format on

        // ImGui::PushItemWidth(-HelloImGui::EmSize(10));

        static int composite = 0;

        ImGui::BeginGroup();
        // ImGui::Combo("Image to export", &composite, "Current image\0Current/Reference composite image\0");
        // ImGui::WrappedTooltip("Save either the current image, or the composited/blended result between the current "
        //                       "image and reference image as shown in the viewport.");

        // ImGui Combo using BeginCombo/EndCombo
        ImGui::TextUnformatted("File format:");
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
        ImGui::TextUnformatted("Options:");

        static float gain   = 1.f;
        static bool  dither = true;
        static int   tf     = 1;

        // float                                                                      gain = powf(2.0f,
        // m_exposure_live);
        std::function<void(const Image &, std::ostream &, const std::string_view)> save_func;

        // if (ImGui::CollapsingHeader("Options"))
        {
            switch (save_format)
            {
            case Format_JPEG_LIBJPEG:
            {
                auto libjpeg_params = jpg_parameters_gui();
                save_func           = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_jpg_image(img, os, filename, libjpeg_params); };
            }
            break;

            case Format_HEIF_AVIF:
            {
                auto heif_params = heif_parameters_gui();
                save_func        = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_heif_image(img, os, filename, heif_params); };
            }
            break;

            case Format_JPEG_UHDR:
            {
                auto uhdr_params = uhdr_parameters_gui();
                save_func        = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_uhdr_image(img, os, filename, uhdr_params); };
            }
            break;

            case Format_JPEG_XL:
            {
                auto jxl_params = jxl_parameters_gui();
                save_func       = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_jxl_image(img, os, filename, jxl_params); };
            }
            break;

            case Format_EXR:
            {
                auto exr_params = exr_parameters_gui(current_image());
                save_func       = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_exr_image(img, os, filename, exr_params); };
            }
            break;

            case Format_PFM:
            {
                auto opts = pfm_parameters_gui();
                save_func = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_pfm_image(img, os, filename, opts); };
            }
            break;

            case Format_PNG_LIBPNG:
            {
                auto png_params = png_parameters_gui();
                save_func       = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_png_image(img, os, filename, png_params); };
            }
            break;

            case Format_QOI:
            {
                ImGui::BeginGroup();
                ImGui::SliderFloat("Gain", &gain, 0.1f, 10.0f);
                ImGui::SameLine();
                if (ImGui::Button("From viewport"))
                    gain = exp2f(exposure());
                ImGui::EndGroup();
                ImGui::WrappedTooltip("Multiply the pixels by this value before saving.");
                ImGui::Combo("Transfer function", &tf, "Linear\0sRGB\0");
                ImGui::Checkbox("Dither", &dither);
                save_func = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_qoi_image(img, os, filename, gain, tf == 1, dither); };
            }
            break;

            case Format_JPEG_STB:
            {
                ImGui::BeginGroup();
                ImGui::SliderFloat("Gain", &gain, 0.1f, 10.0f);
                ImGui::SameLine();
                if (ImGui::Button("From viewport"))
                    gain = exp2f(exposure());
                ImGui::EndGroup();
                ImGui::WrappedTooltip("Multiply the pixels by this value before saving.");
                ImGui::Combo("Transfer function", &tf, "Linear\0sRGB\0");
                ImGui::Checkbox("Dither", &dither);
                static float quality = 95.f;
                ImGui::SliderFloat("Quality", &quality, 1.f, 100.f);
                save_func = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_stb_jpg(img, os, filename, gain, tf == 1, dither, quality); };
            }
            break;

            case Format_BMP_STB:
            {
                ImGui::BeginGroup();
                ImGui::SliderFloat("Gain", &gain, 0.1f, 10.0f);
                ImGui::SameLine();
                if (ImGui::Button("From viewport"))
                    gain = exp2f(exposure());
                ImGui::EndGroup();
                ImGui::WrappedTooltip("Multiply the pixels by this value before saving.");
                ImGui::Combo("Transfer function", &tf, "Linear\0sRGB\0");
                ImGui::Checkbox("Dither", &dither);
                save_func = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_stb_bmp(img, os, filename, gain, tf == 1, dither); };
            }
            break;

            case Format_HDR_STB:
            {
                ImGui::BeginGroup();
                ImGui::SliderFloat("Gain", &gain, 0.1f, 10.0f);
                ImGui::SameLine();
                if (ImGui::Button("From viewport"))
                    gain = exp2f(exposure());
                ImGui::EndGroup();
                ImGui::WrappedTooltip("Multiply the pixels by this value before saving.");
                save_func = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_stb_hdr(img, os, filename, gain); };
            }
            break;

            case Format_PNG_STB:
            {
                ImGui::BeginGroup();
                ImGui::SliderFloat("Gain", &gain, 0.1f, 10.0f);
                ImGui::SameLine();
                if (ImGui::Button("From viewport"))
                    gain = exp2f(exposure());
                ImGui::EndGroup();
                ImGui::WrappedTooltip("Multiply the pixels by this value before saving.");
                ImGui::Combo("Transfer function", &tf, "Linear\0sRGB\0");
                ImGui::Checkbox("Dither", &dither);
                save_func = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_stb_png(img, os, filename, gain, tf == 1, dither); };
            }
            break;

            case Format_TGA_STB:
            {
                ImGui::BeginGroup();
                ImGui::SliderFloat("Gain", &gain, 0.1f, 10.0f);
                ImGui::SameLine();
                if (ImGui::Button("From viewport"))
                    gain = exp2f(exposure());
                ImGui::EndGroup();
                ImGui::WrappedTooltip("Multiply the pixels by this value before saving.");
                ImGui::Combo("Transfer function", &tf, "Linear\0sRGB\0");
                ImGui::Checkbox("Dither", &dither);
                save_func = [&](const Image &img, std::ostream &os, const std::string_view filename)
                { save_stb_tga(img, os, filename, gain, tf == 1, dither); };
            }
            break;
            }
        }

        // ImGui::Unindent();

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
            filename = current_image()->path.stem().u8string() + string(save_format_extensions[save_format]);
#if !defined(__EMSCRIPTEN__)
            filename = pfd::save_file(
                           save_as_name.c_str(), filename,
                           {string(save_format_names[save_format]) + " images", save_format_extensions[save_format]})
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

        load_image(filenames[i], {}, i == 0, ImageLoadOptions{channel_selector});
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

void HDRViewApp::draw_open_options_dialog(bool &open)
{
    if (open)
        ImGui::OpenPopup("Image loading options...");

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2, 5.f * HelloImGui::EmSize()),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));

    if (ImGui::BeginPopupModal("Image loading options...", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        open = false;

        load_image_options_gui();

        ImGui::EndPopup();
    }
}

// Note: the filename is passed by value in case its an element of m_recent_files, which we modify
void HDRViewApp::load_image(const string filename, const string_view buffer, bool should_select,
                            const ImageLoadOptions &opts)
{
    m_image_loader.background_load(filename, buffer, should_select, nullptr, opts);
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
    m_image_loader.background_load(image->filename, {}, should_select, image,
                                   ImageLoadOptions{image->channel_selector});
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
