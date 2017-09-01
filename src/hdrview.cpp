/*
    hdrview.cpp -- HDRView application entry point

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/
#include <cstdlib>
#include <iostream>
#include <docopt.h>
#include "hdrviewer.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

using namespace std;
namespace spd = spdlog;

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
                           T : (0 | 1 | 2 | 3 | 4 | 5 | 6) [default: 2].
                           All messages with severity > T are displayed, where
                           the severities are:
                                trace    = 0
                                debug    = 1
                                info     = 2
                                warn     = 3
                                err      = 4
                                critical = 5
                                off      = 6
  -h, --help               Display this message.
  --version                Show the version.
)";


int main(int argc, char **argv)
{
    vector<string> argVector = { argv + 1, argv + argc };
    map<string, docopt::value> docargs;
    int verbosity = 0;
    float gamma = 2.2f, exposure;
    bool dither = true, sRGB = true;

    vector<string> inFiles;

    try
    {

#if defined(__APPLE__)
        bool launched_from_finder = false;
        // check whether -psn is set, and remove it from the arguments
        for (vector<string>::iterator i = argVector.begin(); i != argVector.end(); ++i)
        {
            if (strncmp("-psn", i->c_str(), 4) == 0)
            {
                launched_from_finder = true;
                argVector.erase(i);
                break;
            }
        }
#endif
        docargs = docopt::docopt(USAGE, argVector,
                                 true,            // show help if requested
                                 "HDRView 0.1");  // version string

        verbosity = docargs["--verbose"].asLong();

        // Console logger with color
        auto console = spd::stdout_color_mt("console");
        spd::set_pattern("[%l] %v");
        spd::set_level(spd::level::level_enum(2));

        if (verbosity < spd::level::trace || verbosity > spd::level::off)
        {
            console->error("Invalid verbosity threshold. Setting to default \"2\"");
            verbosity = 2;
        }

        spd::set_level(spd::level::level_enum(verbosity));

        console->info("Welcome to HDRView!");
        console->info("Verbosity threshold set to level {:d}.", verbosity);

        console->debug("Running with the following commands/arguments/options:");
        for (auto const& arg : docargs)
            console->debug("{:<13}: {}", arg.first, arg.second);

        // exposure
        exposure = strtof(docargs["--exposure"].asString().c_str(), (char **)NULL);
        console->info("Setting intensity scale to {:f}", powf(2.0f, exposure));

        // gamma or sRGB
        if (docargs["--gamma"])
        {
            sRGB = false;
            gamma = max(0.1f, strtof(docargs["--gamma"].asString().c_str(), (char **)NULL));
            console->info("Setting gamma correction to g={:f}.", gamma);
        }
        else
            console->info("Using sRGB response curve.");

        // dithering
        dither = !docargs["--no-dither"].asBool();

	    // list of filenames
	    inFiles = docargs["FILE"].asStringList();

        console->info("Launching GUI.");
        nanogui::init();

#if defined(__APPLE__)
        if (launched_from_finder)
            nanogui::chdir_to_bundle_parent();
#endif

        {
            nanogui::ref<HDRViewScreen> viewer = new HDRViewScreen(exposure, gamma, sRGB, dither, inFiles);
            viewer->setVisible(true);
            nanogui::mainloop();
        }

        nanogui::shutdown();
    }
    // Exceptions will only be thrown upon failed logger or sink construction (not during logging)
    catch (const spd::spdlog_ex& e)
    {
        fprintf(stderr, "Log init failed: %s\n", e.what());
        return 1;
    }
    catch (const std::exception &e)
    {
        spd::get("console")->critical("Error: {}", e.what());
        fprintf(stderr, "%s", USAGE);
        return -1;
    }

    return EXIT_SUCCESS;
}
