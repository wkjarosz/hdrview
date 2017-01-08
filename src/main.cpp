/*
    main.cpp -- HDRView application entry point

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include <thread>
#include <cstdlib>
#include <iostream>
#include <docopt.h>
#include <tinyformat.h>
#include "hdrviewer.h"
#include "common.h"
#include "envmap.h"

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
  -r SIZE, --resize=SIZE   Resize the image to the specified SIZE.
                           This currently uses a box filter for resampling, but
                           you can combine with a Gaussian blur to obtain
                           smoother downsampled results. The blur is applied
                           *before* downsampling.
                           SIZE can be either absolute or relative.
                           Absolute: SIZE should be a string matching the
                           pattern '%dx%d', for instance: '640x480'.
                           Relative: SIZE should be a string matching the
                           pattern '%f%%x%f%%' e.g. '33.3%x25%' would make the
                           image a third its original width and a quarter its
                           original height.
  --remap=SIZE,M,M,[S]     Remap the input image from one environment map
                           format to another. SIZE is a size specification just
                           like in --resize. M,M are the input and output
                           environment map formats respectively.
                           MAP : (latlong | angularmap | mirrorball | cubemap).
                           The optional S results in SxS super-sampling, where
                           the default is one centered sample per pixel S=1.
                           Technically this method can be abused to resize an
                           image without any remapping by specifying the same
                           M parameter twice.
  --border-mode=MODE       Specifies what mode to use when accessing pixels
                           outside the bounds of the image.
                           MODE : (black | mirror | edge | repeat)
                           [default: edge]
  -a FILE, --average=FILE  Average all loaded images and save to FILE
                           (all images must have the same dimensions).
  -n R,G,B, --nan=R,G,B    Replace all NaNs and INFs with (R,G,B)
  --dry-run                Don't actually save any files, just report what would
                           be done.
)";


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
    HDRImage::BorderMode borderMode;
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

        if (docargs["--border-mode"].asString() == "black")
            borderMode = HDRImage::BLACK;
        else if (docargs["--border-mode"].asString() == "mirror")
            borderMode = HDRImage::MIRROR;
        else if (docargs["--border-mode"].asString() == "repeat")
            borderMode = HDRImage::REPEAT;
        else if (docargs["--border-mode"].asString() == "edge")
            borderMode = HDRImage::EDGE;
        else
            throw invalid_argument(tfm::format("Invalid border mode \"%s\".", docargs["--border-mode"].asString()));

        if (verbosity)
            printf("Using border mode: %s.\n", docargs["--border-mode"].asString().c_str());

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
                throw invalid_argument(tfm::format("Cannot parse command-line parameter: --filter:\t%s", docargs["--filter"].asString()));

            filterType = type;
            transform(filterType.begin(), filterType.end(), filterType.begin(), ::tolower);
            filterParams = params;
            if (verbosity)
                printf("Filtering using %s(%s).\n", filterType.c_str(), filterParams.c_str());
        }

        if (docargs["--nan"].isString())
        {
            if (sscanf(docargs["--nan"].asString().c_str(), "%f,%f,%f", &nanColor[0], &nanColor[1], &nanColor[2]) != 3)
                throw invalid_argument("Cannot parse command-line parameter: --nan");

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
                throw invalid_argument("No files specified for batch mode!");

            HDRImage avgImg;
            for (size_t i = 0; i < inFiles.size(); ++i)
            {
                HDRImage image;
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
                        if (avgImg.width() != image.width() || avgImg.height() != image.height())
                            throw invalid_argument("Images do not have the same size.");
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
                            throw invalid_argument(tfm::format("Cannot parse filter parameters (expecting \"%%fx%%f\"):\t%s\n", filterParams));

                        if (dryRun)
                            ;
                        else if (filterType == "gaussian")
                            image = image.gaussianBlur(width, height, borderMode);
                        else if (filterType == "box")
                            image = image.boxBlur(width, height, borderMode);
                        else if (filterType == "fast-gaussian")
                            image = image.fastGaussianBlur(width, height, borderMode);
                        else if (filterType == "median")
                            image = image.median(width, height, borderMode);
                        else if (filterType == "bilateral")
                            image = image.bilateral(width, height, borderMode);
                    }
                    else if (filterType == "unsharp")
                    {
                        float sigma, strength;
                        if (sscanf(filterParams.c_str(), "%f,%f", &sigma, &strength) != 2)
                            throw invalid_argument(tfm::format("Cannot parse 'unsharp' filter parameters (expecting \"%%f,%%f\"):\t%s\n", filterParams));

                        if (!dryRun)
                            image = image.unsharpMask(sigma, strength, borderMode);
                    }
                    else
                        throw invalid_argument(tfm::format("Unrecognized filter type: \"%s\".", filterType));
                }

                if (docargs["--resize"].isString())
                {
                    bool relative = false;
                    int newWidth, newHeight;
                    float percentX, percentY;
                    if (sscanf(docargs["--resize"].asString().c_str(), "%dx%d", &newWidth, &newHeight) == 2)
                        relative = false;
                    else if (sscanf(docargs["--resize"].asString().c_str(), "%f%%x%f%%", &percentX, &percentY) == 2)
                    {
                        relative = true;
                        newWidth = (int)round(percentX*image.width());
                        newHeight = (int)round(percentY*image.height());
                    }
                    else
                        throw invalid_argument(tfm::format("Cannot parse --resize parameters:\t%s\n", docargs["--resize"].asString()));

                    image = image.smoothScale(newWidth, newHeight);
                }

                if (docargs["--remap"].isString())
                {
                    bool relative = false;
                    int newWidth, newHeight;
                    float percentX, percentY;
                    int numSamples = 1;
                    char s1[32], s2[32], s3[32] = "bilinear";
                    if (sscanf(docargs["--remap"].asString().c_str(), "%dx%d,%30[^','],%30[^','],%d,%30[^',']", &newWidth, &newHeight, s1, s2, &numSamples, s3) >= 4)
                        relative = false;
                    else if (sscanf(docargs["--remap"].asString().c_str(), "%f%%x%f%%,%30[^','],%30s,%d,%30[^',']", &percentX, &percentY, s1, s2, &numSamples, s3) >= 4)
                    {
                        relative = true;
                        newWidth = (int)round(percentX*image.width());
                        newHeight = (int)round(percentY*image.height());
                    }
                    else
                        throw invalid_argument(tfm::format("Cannot parse --remap parameters:\t%s\n", docargs["--remap"].asString()));

                    UV2XYZFn dst2xyz;
                    XYZ2UVFn xyz2src;

                    string from = s1, to = s2;

                    // by default create a no-op passthrough warp function
                    function<Vector2f(const Vector2f&)> warp = [](const Vector2f & uv) {return uv;};
                    if (from != to)
                    {
                        if (from == "angularmap")
                            xyz2src = XYZToAngularMap;
                        else if (from == "mirrorball")
                            xyz2src = XYZToMirrorBall;
                        else if (from == "latlong")
                            xyz2src = XYZToLatLong;
                        else if (from == "cubemap")
                            xyz2src = XYZToCubeMap;
                        else
                            throw invalid_argument(tfm::format("Cannot parse --remap parameters, unrecognized mapping type \"%s\"", from));

                        if (to == "angularmap")
                            dst2xyz = angularMapToXYZ;
                        else if (to == "mirrorball")
                            dst2xyz = mirrorBallToXYZ;
                        else if (to == "latlong")
                            dst2xyz = latLongToXYZ;
                        else if (to == "cubemap")
                            dst2xyz = cubeMapToXYZ;
                        else
                            throw invalid_argument(tfm::format("Cannot parse --remap parameters, unrecognized mapping type \"%s\"", to));

                        warp = [&](const Vector2f & uv) {return xyz2src(dst2xyz(Vector2f(uv(0), uv(1))));};
                    }

                    function<Color4(const HDRImage &, float, float, HDRImage::BorderMode)> sampler;
                    string interp = s3;
                    if (interp == "nearest")
                        sampler = [](const HDRImage & i, float x, float y, HDRImage::BorderMode m) {return i.nearest(x,y,m);};
                    else if (interp == "bilinear")
                        sampler = [](const HDRImage & i, float x, float y, HDRImage::BorderMode m) {return i.bilinear(x,y,m);};
                    else if (interp == "bicubic")
                        sampler = [](const HDRImage & i, float x, float y, HDRImage::BorderMode m) {return i.bicubic(x,y,m);};
                    else
                        throw invalid_argument(tfm::format("Cannot parse --remap parameters, unrecognized sampler type \"%s\"", interp));

                    image = image.resample(newWidth, newHeight, sampler,
                                           warp, numSamples, borderMode);
                }

                string thisExt = ext.size() ? ext : getExtension(inFiles[i]);
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
