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
#include <tinyformat.h>

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
  -o BASE, --out=BASE      Save image(s) using specified base output filename.
                           If multiple images are processed, an image sequence
                           is created by concetenating: the base filename, image
                           number, and output format extension. For example:
                                HDRView -o 'output-image-' -f png *.exr
                           would save all OpenEXR images in the working
                           directory as a PNG sequence 'output-image-%3d.png'.
                           If a single image is processed, the number is omitted.
                           If no basename is provided, the input files' basenames
                           are used instead and no numbers are appended (files
                           may be overwritten!). For example:
                                HDRview -f png fileA.exr fileB.exr
                           would output 'fileA.png' and 'fileB.png'.
  -f EXT, --format=EXT     Specify output file format and extension.
                           If no format is given, each image is saved in it's
                           original format (if supported).
                           EXT : (bmp | exr | pfm | png | ppm | hdr | tga).
  --filter=TYPE,PARAMS...  Process image(s) using filter TYPE with
                           filter-specific PARAMS specified after the comma.
                           TYPE : (gaussian | box | fast-gaussian | unsharp |
                                   bilateral | median).
                           For example: '--filter fast-gaussian,10x10' would
                           filter using a 10x10 fast Gaussian approximation.
  -a FILE, --average=FILE  Average all loaded images and save to FILE
                           (all images must have the same dimensions).
  -n R,G,B, --nan=R,G,B    Replace all NaNs and INFs with (R,G,B)
  --dry-run                Don't actually save any files, just report what would
                           be done.
)";

// TODO: don't duplicate this function, put it in some header
string getFileExtension(const string& filename)
{
    if (filename.find_last_of(".") != string::npos)
        return filename.substr(filename.find_last_of(".")+1);
    return "";
}

// TODO: also put this in the same header
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
    float gamma,
          exposure;
    string ext = "",
           avgFilename = "",
           basename = "",
           filterType = "",
           filterParams = "";
    int verbosity = 0;
    bool average = false,
         filter = false,
         dither = true,
         sRGB = true,
         dryRun = true,
         fixNaNs = false;
    Color3 nanColor(0.0f,0.0f,0.0f);

    vector<string> inFiles;

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

        // format
        if (docargs["--format"].isString())
        {
            ext = docargs["--format"].asString();
            if (verbosity)
                printf("Converting to \"%s\".\n", ext.c_str());
        }
        else
        {
            if (verbosity)
                printf("Keeping original image file formats.\n");
        }

        // base filename
        if (docargs["--out"].isString())
        {
            basename = docargs["--out"].asString();
            if (verbosity)
                printf("Setting base filename to \"%s\".\n", basename.c_str());
        }

        if (docargs["--average"].isString())
        {
            average = true;
            avgFilename = docargs["--average"].asString();
            if (verbosity)
                printf("Saving average image to \"%s\".\n", avgFilename.c_str());
        }

        if (docargs["--filter"].isString())
        {
            filter = true;
            char type[22];
            char params[32];
            if (sscanf(docargs["--filter"].asString().c_str(), "%20[^','],%30s", type, params) != 2)
            {
                fprintf(stderr, "Cannot parse command-line parameter: --filter:");
                fprintf(stderr, "\t%s\n", docargs["--filter"].asString().c_str());
                return -1;
            }
            filterType = type;
            transform(filterType.begin(), filterType.end(), filterType.begin(), ::tolower);
            filterParams = params;
            if (verbosity)
                printf("Filtering using %s(%s).\n", filterType.c_str(), filterParams.c_str());
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

        dryRun = docargs["--dry-run"].asBool();
        if (dryRun && verbosity)
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
            if (!inFiles.size())
            {
                fprintf(stderr, "No files specified for batch mode!\n");
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

                if (fixNaNs || !dryRun)
                    image = image.unaryExpr([&](const Color4 & c)
                    {
                        return isfinite(c.sum()) ? c : Color4(nanColor, c[3]);
                    });

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

                if (filter)
                {
                    if (filterType == "gaussian" ||
                        filterType == "box" ||
                        filterType == "fast-gaussian" ||
                        filterType == "median" ||
                        filterType == "bilateral")
                    {
                        float width, height;
                        if (sscanf(filterParams.c_str(), "%fx%f", &width, &height) != 2)
                        {
                            fprintf(stderr, "Cannot parse filter parameters (expecting \"%%fx%%f\"):");
                            fprintf(stderr, "\t%s\n", filterParams.c_str());
                            return -1;
                        }

                        if (dryRun)
                            ;
                        else if (filterType == "gaussian")
                            image = image.gaussianBlur(width, height);
                        else if (filterType == "box")
                            image = image.boxBlur(width, height);
                        else if (filterType == "fast-gaussian")
                            image = image.fastGaussianBlur(width, height);
                        else if (filterType == "median")
                            image = image.median(width, height);
                        else if (filterType == "bilateral")
                            image = image.bilateral(width, height);
                    }
                    else if (filterType == "unsharp")
                    {
                        float sigma, strength;
                        if (sscanf(filterParams.c_str(), "%f,%f", &sigma, &strength) != 2)
                        {
                            fprintf(stderr, "Cannot parse 'unsharp' filter parameters (expecting \"%%f,%%f\"):");
                            fprintf(stderr, "\t%s\n", filterParams.c_str());
                            return -1;
                        }

                        if (!dryRun)
                            image = image.unsharpMask(sigma, strength);
                    }
                    else
                    {
                        fprintf(stderr, "Unrecognized filter type: \"%s\".", filterType.c_str());
                        return -1;
                    }
                }

                string thisExt = ext.size() ? ext : getFileExtension(inFiles[i]);
                string thisBasename = basename.size() ? basename : getBasename(inFiles[i]);
                string filename;
                if (inFiles.size() == 1 || !basename.size())
                    filename = thisBasename + "." + thisExt;
                else
                    filename = tfm::format("%s%03d.%s", thisBasename, i, thisExt);

                if (verbosity)
                    printf("Writing image \"%s\"...", filename.c_str());

                if (!dryRun)
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

                if (!dryRun)
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
