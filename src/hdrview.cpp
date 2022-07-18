//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//


#include "common.h"
#include "filesystem/path.h"
#include "hdrviewscreen.h"

#ifdef __APPLE__
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#include <cstdlib>
#include <docopt.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using namespace std;
using json = nlohmann::json;


static HDRViewScreen * g_screen = nullptr;

// Force usage of discrete GPU on laptops
NANOGUI_FORCE_DISCRETE_GPU();

static const char USAGE[] =
    R"(HDRView. Copyright (c) Wojciech Jarosz.

HDRView is a simple research-oriented tool for examining,
comparing, and converting high-dynamic range images. HDRView
is freely available under a 3-clause BSD license.

Usage:
  hdrview [options FILE...]
  hdrview -h | --help | --version

Options:
  -e E, --exposure=E       Desired power of 2 EV or exposure value
                           (gain = 2^exposure) [default: 0].
  -g G, --gamma=G          Desired gamma value for exposure+gamma tonemapping.
                           An sRGB curve is used if gamma is not specified.
  -d, --no-dither          Disable dithering.
  -v T, --verbose=T        Set verbosity threshold with lower values meaning
                           more verbose and higher values removing low-priority
                           messages.
                           T : (0 | 1 | 2 | 3 | 4 | 5 | 6).
                           All messages with severity >= T are displayed, where
                           the severities are:
                                trace    = 0
                                debug    = 1
                                info     = 2
                                warn     = 3
                                err      = 4
                                critical = 5
                                off      = 6
                           The default is 2 (info).
  -h, --help               Display this message.
  --version                Show the version.
)";

json read_settings()
{
    try
    {
        string directory = config_directory();
        ::filesystem::create_directories(directory);
        string filename = directory + "settings.json";
        spdlog::info("Reading configuration from file {}", filename);

        std::ifstream stream(filename);
        if (!stream.good())
            throw std::runtime_error(fmt::format("Cannot open settings file: \"{}\".", filename));

        json settings;
        stream >> settings;
        return settings;
    }
    catch (const exception &e)
    {
        return json::object();
    }
}

void write_settings()
{
    if (g_screen)
    {
        g_screen->write_settings();
    }
}

int main(int argc, char **argv)
{
    vector<string>             arg_vector = {argv + 1, argv + argc};
    map<string, docopt::value> docargs;
    constexpr int              default_verbosity = spdlog::level::info;
    int                        verbosity         = default_verbosity;

    float gamma  = 2.2f, exposure;
    bool  dither = true, sRGB = true;

    vector<string> inFiles;

    try
    {

#if defined(__APPLE__)
        bool launched_from_finder = false;
        // check whether -psn is set, and remove it from the arguments
        for (vector<string>::iterator i = arg_vector.begin(); i != arg_vector.end(); ++i)
        {
            if (strncmp("-psn", i->c_str(), 4) == 0)
            {
                launched_from_finder = true;
                arg_vector.erase(i);
                break;
            }
        }
#endif
        string version_string = fmt::format("HDRView {}. (built on {} from git {}-{}-{} using {} backend)",
                                            HDRVIEW_VERSION, hdrview_timestamp(), hdrview_git_branch(),
                                            hdrview_git_version(), hdrview_git_revision(), HDRVIEW_BACKEND);
        docargs               = docopt::docopt(USAGE, arg_vector,
                                 true,            // show help if requested
                                 version_string); // version string

        // Console logger with color
        spdlog::set_pattern("%^[%l]%$ %v");
        spdlog::set_level(spdlog::level::level_enum(default_verbosity));

        auto settings = read_settings();
        if (docargs["--verbose"])
            verbosity = docargs["--verbose"].asLong();
        else
            verbosity = settings.value("verbosity", default_verbosity);

        if (verbosity < spdlog::level::trace || verbosity > spdlog::level::off)
        {
            verbosity = default_verbosity;
            spdlog::error("Invalid verbosity threshold. Setting to default \"{}\"", verbosity);
        }

        spdlog::set_level(spdlog::level::level_enum(verbosity));

        spdlog::info("Welcome to HDRView!");
        spdlog::info("Verbosity threshold set to level {:d}.", verbosity);

        spdlog::debug("Running with the following commands/arguments/options:");
        for (auto const &arg : docargs) spdlog::debug("{:<13}: {}", arg.first, arg.second);

        // exposure
        exposure = strtof(docargs["--exposure"].asString().c_str(), (char **)NULL);
        spdlog::info("Setting intensity scale to {:f}", powf(2.0f, exposure));

        // gamma or sRGB
        if (docargs["--gamma"])
        {
            sRGB  = false;
            gamma = max(0.1f, strtof(docargs["--gamma"].asString().c_str(), (char **)NULL));
            spdlog::info("Setting gamma correction to g={:f}.", gamma);
        }
        else
            spdlog::info("Using sRGB response curve.");

        // dithering
        dither = !docargs["--no-dither"].asBool();

        // list of filenames
        inFiles = docargs["FILE"].asStringList();

        auto [capability10bit, capabilityEdr] = nanogui::test_10bit_edr_support();
        spdlog::info("Launching GUI with {} bit color and {} display support.", capability10bit ? 10 : 8, capabilityEdr ? "HDR" : "LDR");
        nanogui::init();

#if defined(__APPLE__)
        if (launched_from_finder)
            nanogui::chdir_to_bundle_parent();
#endif


        {
#ifdef __APPLE__
            // This code adapted from tev:
            // On macOS, the mechanism for opening an application passes filenames
            // through the NS api rather than CLI arguments, which means we need
            // special handling of these through GLFW.
            // There are two components to this special handling:

            // 1. The filenames that were passed to this application when it was opened.
            if (inFiles.empty()) {
                // If we didn't get any command line arguments for files to open,
                // then, on macOS, they might have been supplied through the NS api.
                const char* const* openedFiles = glfwGetOpenedFilenames();
                if (openedFiles) {
                    for (auto p = openedFiles; *p; ++p) {
                        inFiles.push_back(string(*p));
                    }
                }
            }

            // 2. a callback for when the same application is opened additional
            //    times with more files.
            glfwSetOpenedFilenamesCallback([](const char* imageFile) {
                g_screen->drop_event({string(imageFile)});
            });
#endif
            g_screen = new HDRViewScreen(exposure, gamma, sRGB, dither, inFiles);
            g_screen->draw_all();
            g_screen->set_visible(true);
            nanogui::mainloop(-1.f);
            write_settings();
        }
        nanogui::shutdown();
    }
    // Exceptions will only be thrown upon failed logger or sink construction (not during logging)
    catch (const spdlog::spdlog_ex &e)
    {
        fprintf(stderr, "Log init failed: %s\n", e.what());
        return 1;
    }
    catch (const std::exception &e)
    {
        spdlog::critical("Error: {}", e.what());
        fprintf(stderr, "%s", USAGE);
        return -1;
    }

    return EXIT_SUCCESS;
}
