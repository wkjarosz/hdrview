#include "app.h"

#include "image.h"
#include <fstream>

#ifdef __EMSCRIPTEN__
#include "platform_utils.h"
#include <emscripten/emscripten.h>
#include <emscripten_browser_file.h>
#else
#include "portable-file-dialogs.h"
#endif

using namespace std;

void HDRViewApp::save_as(const string &filename) const
{
    try
    {
#if !defined(__EMSCRIPTEN__)
        ofstream os{filename, ios_base::binary};
        current_image()->save(os, filename, powf(2.0f, m_exposure_live), true, m_dither);
#else
        ostringstream os;
        current_image()->save(os, filename, powf(2.0f, m_exposure_live), true, m_dither);
        string buffer = os.str();
        emscripten_browser_file::download(
            filename,                                    // the default filename for the browser to save.
            "application/octet-stream",                  // the MIME type of the data, treated as if it were a webserver
                                                         // serving a file
            string_view(buffer.c_str(), buffer.length()) // a buffer describing the data to download
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

void HDRViewApp::export_as(const string &filename) const
{
    try
    {
        Image img(current_image()->size(), 4);
        img.finalize();
        auto bounds     = current_image()->data_window;
        int  block_size = std::max(1, 1024 * 1024 / img.size().x);
        parallel_for(blocked_range<int>(0, img.size().y, block_size),
                     [this, &img, bounds](int begin_y, int end_y, int, int)
                     {
                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < img.size().x; ++x)
                             {
                                 float4 v = pixel_value(int2{x, y} + bounds.min, false, 2);

                                 img.channels[0](x, y) = v[0];
                                 img.channels[1](x, y) = v[1];
                                 img.channels[2](x, y) = v[2];
                                 img.channels[3](x, y) = v[3];
                             }
                     });

#if !defined(__EMSCRIPTEN__)
        ofstream os{filename, ios_base::binary};
        img.save(os, filename, 1.f, true, m_dither);
#else
        ostringstream os;
        img.save(os, filename, 1.f, true, m_dither);
        string buffer = os.str();
        emscripten_browser_file::download(
            filename,                                    // the default filename for the browser to save.
            "application/octet-stream",                  // the MIME type of the data, treated as if it were a webserver
                                                         // serving a file
            string_view(buffer.c_str(), buffer.length()) // a buffer describing the data to download
        );
#endif
    }
    catch (const exception &e)
    {
        spdlog::error("An error occurred while exporting to '{}':\n\t{}.", filename, e.what());
    }
    catch (...)
    {
        spdlog::error("An unknown error occurred while exporting to '{}'.", filename);
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
        int next = next_visible_image_index(index, Forward);
        if (next < index) // there is no visible image after this one, go to previous visible
            next = next_visible_image_index(index, Backward);
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
        int next_ref = next_visible_image_index(index, Forward);
        if (next_ref < index)
            next_ref = next_visible_image_index(index, Backward);
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
