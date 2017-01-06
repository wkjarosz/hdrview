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
  hdrview batch [options <file>...]
  hdrview [view] [options <file>...]
  hdrview -h | --help | --version

The available commands are:
    view       Launch the GUI image viewer [this is the default].
    batch      Batch process the files on the command-line.

Options: (global)
  -e E, --exposure=E       Desired power of 2 EV or exposure value
                           (gain = 2^exposure) [default: 0].
  -g G, --gamma=G          Desired gamma value for exposure+gamma tonemapping.
                           An sRGB curve is used if gamma is not specified.
  -d, --no-dither          Disable dithering.
  -v L, --verbose=L        Set verbosity (L can be 0, 1, 2) [default: 1].
  -h, --help               Display this message.
  --version                Show the version.

Options: (for batch processing)
  -c EXT, --convert=EXT    Convert all images to specified output format.
                           Available formats include:
                           bmp, exr, pfm, png, ppm, hdr, tga.
  -a FILE, --average=FILE  Average all loaded images (all images must have the
                           same dimensions).
  --blur="TYPE W,H"        Perform a blur of type ("Gaussian", "Box", "FastGaussian"),
                           with specified width W and height H.
  -n R,G,B, --nan=R,G,B    Replace all NaNs and INFs with (R,G,B)
  -t, --test               Don't convert files, just show what would be done.
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
    bool convert = false, average = false, blur = false;
    string convertFormat = "", avgFilename = "", blurType = "";
    float blurWidth = 0, blurHeight = 0;
    int verbosity = 0;
    bool dither = true;
    bool sRGB = true;
    bool onlyTesting = true;
    bool fixNaNs = false;
    Color3 nanColor(0.0f,0.0f,0.0f);
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
                cout << arg.first << ": " << arg.second << endl;
            cout << endl;
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

        if (docargs["--convert"].isString())
        {
            convert = true;
            convertFormat = docargs["--convert"].asString();
            if (verbosity)
                printf("Converting to \"%s\".\n", convertFormat.c_str());
        }

        if (docargs["--average"].isString())
        {
            average = true;
            avgFilename = docargs["--average"].asString();
            if (verbosity)
                printf("Saving average image to \"%s\".\n", avgFilename.c_str());
        }

        if (docargs["--blur"].isString())
        {
            blur = true;
            char type[32];
            if (sscanf(docargs["--blur"].asString().c_str(), "%s %f,%f", type, &blurWidth, &blurHeight) != 3)
            {
                fprintf(stderr, "Cannot parse command-line parameter: --blur:");
                fprintf(stderr, "\t%s\n", docargs["--blur"].asString().c_str());
                return -1;
            }
            blurType = type;
            if (verbosity)
                printf("Blurring using %s(%f,%f).\n", blurType.c_str(), blurWidth, blurHeight);
        }

        if (docargs["--nan"].isString())
        {
            if (sscanf(docargs["--nan"].asString().c_str(), "%f,%f,%f", &nanColor[0], &nanColor[1], &nanColor[2]) != 3)
            {
                fprintf(stderr, "Cannot parse command-line parameter: --nan\n");
                return -1;
            }
            if (verbosity)
                printf("Replacing NaNs and Infinities with (%f,%f,%f).\n", nanColor[0], nanColor[1], nanColor[2]);
            fixNaNs = true;
        }

        onlyTesting = docargs["--test"].asBool();
        if (onlyTesting && verbosity)
            printf("Only testing. Will not write files.\n");

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
        if (!docargs["batch"].asBool())
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
            if ((!average && !convert) || !inFiles.size())
            {
                fprintf(stderr, "Must specify at least one file and one of -c or -a in batch mode!\n");
                return -1;
            }

            FloatImage avgImg;
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

                if (fixNaNs)
                    for (int y = 0; y < image.height(); ++y)
                        for (int x = 0; x < image.width(); ++x)
                            if (!isfinite(image(x,y)[0]) ||
                                !isfinite(image(x,y)[1]) ||
                                !isfinite(image(x,y)[2]))
                                image(x,y) = Color4(nanColor, image(x,y)[3]);

                if (average)
                {
                    if (i == 0)
                        avgImg = image;
                    else
                    {
                        if (avgImg.width() != image.width() ||
                            avgImg.height() != image.height())
                        {
                            fprintf(stderr, "Images do not have the same size.");
                            return -1;
                        }
                        avgImg += image;
                    }
                }

                if (blur)
                {
                    if (blurType == "Gaussian")
                        image = image.gaussianBlur(blurWidth, blurHeight);
                    else if (blurType == "Box")
                        image = image.boxBlur(blurWidth, blurHeight);
                    else if (blurType == "FastGaussian")
                        image = image.fastGaussianBlur(blurWidth, blurHeight);
                }

                if (!convert)
                    continue;

                string filename = getBasename(inFiles[i]) + "." + convertFormat;

                if (verbosity)
                    printf("Writing image \"%s\"...", filename.c_str());

                if (!onlyTesting)
                    image.save(filename, powf(2.0f, exposure), gamma, sRGB, dither);

                if (verbosity)
                    printf(" done!\n");
            }

            if (average)
            {
                avgImg *= Color4(1.0f/inFiles.size());

                string filename = avgFilename;

                if (verbosity)
                    printf("Writing average image \"%s\"...\n", filename.c_str());

                if (!onlyTesting)
                    avgImg.save(filename, powf(2.0f, exposure), gamma, sRGB, dither);
            }
        }
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Error: %s\n%s\n", e.what(), USAGE);
        return -1;
    }

    return EXIT_SUCCESS;
}
