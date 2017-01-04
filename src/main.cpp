/*
    main.cpp -- HDRView application entry point

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include <thread>
#include <cstdlib>
#include <iostream>
#include <docopt.h>
#include "HDRViewer.h"

using namespace std;

// Force usage of discrete GPU on laptops
NANOGUI_FORCE_DISCRETE_GPU();

static const char USAGE[] =
R"(HDRView. Copyright (c) Wojciech Jarosz.

HDRView is a simple research-oriented tool for examining,
comparing, and converting high-dynamic range images. HDRView
is freely available under a 3-clause BSD license.

Usage:
  hdrview convert [options] <file>...
  hdrview [view] [options <file>...]
  hdrview -h | --help | --version

The available commands are:
    view       Launch the GUI image viewer [this is the default].
    convert    Batch convert the files on the command-line.

Options: (global)
  -e <e>, --exposure <e>    Desired power of 2 EV or exposure value
                            (gain = 2^exposure) [default: 0].
  -g <g>, --gamma <g>       Desired gamma value for exposure+gamma tonemapping.
                            An sRGB curve is used if gamma is not specified.
  -d, --no-dither           Disable dithering.
  -v <l>, --verbose <l>     Set verbosity (<l> can be 0, 1, 2) [default: 1].
  -h, --help                Display this message.
  --version                 Show the version.

Options: (for convert command)
  -f <ext>, --format <ext>  Output format for convert command.
                            Available formats include:
                                bmp, exr, pfm, png, ppm, hdr, tga
                                [default: png].
  -t, --test                Don't convert files, just show what would be done.
)";

string getBasename(const string& filename)
{
    auto lastSlash = filename.find_last_of("/\\");
    auto lastDot = filename.find_last_of(".");
    if (lastSlash == std::string::npos && lastDot == std::string::npos)
        return filename;

    auto start = (lastSlash != string::npos) ? lastSlash + 1 : 0;
    auto length = (lastDot != string::npos) ? lastDot-start : filename.size()-start;
    return filename.substr(start, length);
}

int main(int argc, char **argv)
{
    vector<string> argVector = { argv + 1, argv + argc };
    map<string, docopt::value> docargs;
    vector<string> inFiles;
    int verbosity = 0;
    bool dither = true;
    bool sRGB = true;
    float gamma, exposure;

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

    try
    {
        docargs = docopt::docopt(USAGE, argVector,
                                 true,            // show help if requested
                                 "HDRView 0.1");  // version string

        verbosity = docargs["--verbose"].asLong();

        if (verbosity)
            printf("Verbosity set to level %d.\n", verbosity);

        if (verbosity > 1)
        {
            printf("Running with the following commands/arguments/options:\n");
            for (auto const& arg : docargs)
                std::cout << arg.first << ": " << arg.second << std::endl;
        }

        // exposure
        exposure = strtof(docargs["--exposure"].asString().c_str(), (char **)NULL);
        if (verbosity)
            printf("Setting intensity scale to %f\n", powf(2.0f, exposure));

        // gamma or sRGB
        if (docargs["--gamma"])
        {
            sRGB = false;
            gamma = max(0.1f, strtof(docargs["--gamma"].asString().c_str(), (char **)NULL));
            if (verbosity)
                printf("Setting gamma correction to g=%f\n", gamma);
        }
        else if (verbosity)
            printf("Using sRGB response curve\n");

        // dithering
        dither = !docargs["--no-dither"].asBool();

        // list of filenames
        inFiles = docargs["<file>"].asStringList();
    }
    catch (const std::exception &e)
    {
        printf("Error: %s\n%s\n", e.what(), USAGE);
        return -1;
    }

    try
    {
        if (!docargs["convert"].asBool())
        {
            if (verbosity)
                printf("Launching GUI. Start with -h for instructions on batch mode.\n");

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
        else
        {
            string outFormat = docargs["--format"].asString();
            if (verbosity)
                printf("Converting to \"%s\".\n", outFormat.c_str());

            bool onlyTesting = docargs["--test"].asBool();
            if (onlyTesting && verbosity)
                printf("Only testing. Will not write files.\n");

            for (size_t i = 0; i < inFiles.size(); ++i)
            {
                FloatImage image;
                if (!image.load(inFiles[i]))
                {
                    printf("Cannot read image \"%s\". Skipping...\n", inFiles[i].c_str());
                    continue;
                }

                if (verbosity)
                    printf("Successfully read image \"%s\".\n", inFiles[i].c_str());

                string filename = getBasename(inFiles[i]) + "." + outFormat;
                if (verbosity)
                    printf("Writing image \"%s\"...", filename.c_str());

                if (!onlyTesting)
                    image.save(filename, powf(2.0f, exposure), gamma, sRGB, dither);

                if (verbosity)
                    printf(" done!\n");
            }
        }
    }
    catch (const std::exception &e)
    {
        printf("Error: %s\n%s\n", e.what(), USAGE);
        return -1;
    }

    return EXIT_SUCCESS;
}
