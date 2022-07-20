//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//


#include "common.h"
#include <CLI/CLI.hpp>
#include <filesystem/path.h>
#include "hdrviewscreen.h"

#ifdef __APPLE__
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#include <cstdlib>
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
    // vector<string>             arg_vector = {argv + 1, argv + argc};
    constexpr int              default_verbosity = spdlog::level::info;
    int                        verbosity         = default_verbosity;

    float gamma  = 2.2f, exposure = 0.0f;
    bool  dither = true, sRGB = true;

    vector<string> inFiles;

    try
    {

// #if defined(__APPLE__)
//         bool launched_from_finder = false;
//         // check whether -psn is set, and remove it from the arguments
//         for (vector<string>::iterator i = arg_vector.begin(); i != arg_vector.end(); ++i)
//         {
//             if (strncmp("-psn", i->c_str(), 4) == 0)
//             {
//                 launched_from_finder = true;
//                 arg_vector.erase(i);
//                 break;
//             }
//         }
// #endif

        string version_string = fmt::format("HDRView {}. (built on {} from git {}-{}-{} using {} backend)",
                                            HDRVIEW_VERSION, hdrview_timestamp(), hdrview_git_branch(),
                                            hdrview_git_version(), hdrview_git_revision(), HDRVIEW_BACKEND);

        CLI::App app{
"HDRView is a simple research-oriented tool for examining,"
"comparing, and converting high-dynamic range images. HDRView"
"is freely available under a 3-clause BSD license.",
"HDRView"};

        app.set_version_flag("--version", version_string, "Show the version");
        app.add_option("-e,--exposure", exposure,
                    fmt::format("Desired power of 2 EV or exposure value (gain = 2^exposure)."))
            ->check(CLI::Number);
        app.add_option("-g,--gamma", gamma,
                    fmt::format("Desired gamma value for exposure+gamma tonemapping. An sRGB curve is used if gamma is not specified."))
            ->check(CLI::Number);
        app.add_option("-v,--verbosity", verbosity,
                       R"(Set verbosity threshold T with lower values meaning more verbose
and higher values removing low-priority messages. All messages with
severity >= T are displayed, where the severities are:
    trace    = 0
    debug    = 1
    info     = 2
    warn     = 3
    err      = 4
    critical = 5
    off      = 6
The default is 2 (info).)")
            ->check(CLI::Range(0, 6));
        app.add_flag("--dither,--no-dither{false}", dither, "Enable/disable dithering.");
        app.add_option(
            "IMAGES", inFiles,
            "The images files to load.")
            ->check(CLI::ExistingPath);

        // Console logger with color
        spdlog::set_pattern("%^[%l]%$ %v");
        spdlog::set_level(spdlog::level::level_enum(default_verbosity));
     
        CLI11_PARSE(app, argc, argv);

        auto settings = read_settings();
        if (!app.count("--verbosity"))
            verbosity = settings.value("verbosity", default_verbosity);

        spdlog::set_level(spdlog::level::level_enum(verbosity));

        spdlog::info("Welcome to HDRView!");
        spdlog::info("Verbosity threshold set to level {:d}.", verbosity);
        spdlog::info("Setting intensity scale to {:f}", powf(2.0f, exposure));

        // gamma or sRGB
        if (app.count("--gamma"))
        {
            sRGB  = false;
            spdlog::info("Setting gamma correction to g={:f}.", gamma);
        }
        else
            spdlog::info("Using sRGB response curve.");

        // dithering
        spdlog::info("{}", (dither) ? "Dithering" : "Not dithering");

        settings["image view"]["exposure"]  = exposure;
        settings["image view"]["gamma"]     = gamma;
        settings["image view"]["sRGB"]      = sRGB;
        settings["image view"]["dithering"] = dither;
        write_settings();

        auto [capability10bit, capabilityEdr] = nanogui::test_10bit_edr_support();
        spdlog::info("Launching GUI with {} bit color and {} display support.", capability10bit ? 10 : 8, capabilityEdr ? "HDR" : "LDR");
        nanogui::init();

// #if defined(__APPLE__)
//         if (launched_from_finder)
//             nanogui::chdir_to_bundle_parent();
// #endif

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
        // fprintf(stderr, "%s", USAGE);
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
