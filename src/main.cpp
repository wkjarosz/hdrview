/*
    main.cpp -- HDRView application entry point

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include <thread>
#include <cstdlib>
#include <iostream>
#include <docopt.h>
#include "hdrviewer.h"
#include "common.h"
#include "envmap.h"
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
  --remap=M,M,[S],[L]      Remap the input image from one environment map
                           format to another. M,M are the input and output
                           environment map formats respectively.
                           MAP : (latlong | angularmap | mirrorball | cubemap).
                           The optional S results in SxS super-sampling, where
                           the default is S=1: one centered sample per pixel.
                           The option I parameter specifies the sampling lookup
                           mode: I : (nearest | bilinear | bicubic).
                           Specifying the same M parameter twice results in no
                           change. Combine with --resize to specify output file
                           dimensions.
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
    string ext = "",
           avgFilename = "",
           basename = "",
           filterType = "",
           filterParams = "";
    int verbosity = 0, absoluteWidth, absoluteHeight, samples = 1;
    float gamma, exposure, relativeWidth = 100.f, relativeHeight = 100.f;
    bool average = false,
         dither = true,
         sRGB = true,
         dryRun = true,
         fixNaNs = false,
         resize = false,
         remap = false,
         relativeSize = true;
    HDRImage::BorderMode borderMode;
    Color3 nanColor(0.0f,0.0f,0.0f);
    // by default use a no-op passthrough warp function
    function<Vector2f(const Vector2f&)> warp =
        [](const Vector2f & uv) {return uv;};
    function<Color4(const HDRImage &, float, float, HDRImage::BorderMode)> sampler =
        [](const HDRImage & i, float x, float y, HDRImage::BorderMode m) {return i.bilinear(x,y,m);};
    function<HDRImage(const HDRImage &)> filter;

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

        if (verbosity)
            printf("Verbosity set to level %d.\n", verbosity);

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

        // border mode
        if (docargs["--border-mode"].asString() == "black")
            borderMode = HDRImage::BLACK;
        else if (docargs["--border-mode"].asString() == "mirror")
            borderMode = HDRImage::MIRROR;
        else if (docargs["--border-mode"].asString() == "repeat")
            borderMode = HDRImage::REPEAT;
        else if (docargs["--border-mode"].asString() == "edge")
            borderMode = HDRImage::EDGE;
        else
            throw invalid_argument(fmt::format("Invalid border mode \"{}\".", docargs["--border-mode"].asString()));

        console->info("Setting border mode to: {}.", docargs["--border-mode"].asString());

        // format
        if (docargs["--format"].isString())
        {
            ext = docargs["--format"].asString();
            console->info("Converting to \"{}\".", ext);
        }
        else
            console->info("Keeping original image file formats.");

        // base filename
        if (docargs["--out"].isString())
        {
            basename = docargs["--out"].asString();
            console->info("Setting base filename to \"{}\".", basename);
        }

        if (docargs["--average"].isString())
        {
            average = true;
            avgFilename = docargs["--average"].asString();
            console->info("Saving average image to \"{}\".", avgFilename);
        }

        if (docargs["--filter"].isString())
        {
            float filterArg1, filterArg2;
            char type[22], params[32];
            if (sscanf(docargs["--filter"].asString().c_str(), "%20[^','],%30s", type, params) != 2)
                throw invalid_argument(fmt::format("Cannot parse command-line parameter: --filter:\t{}", docargs["--filter"].asString()));

            filterParams = params;
            if (sscanf(filterParams.c_str(), "%f,%f", &filterArg1, &filterArg2) != 2)
                throw invalid_argument(fmt::format("Cannot parse command-line parameter: --filter:\t{}", docargs["--filter"].asString()));

            filterType = type;
            transform(filterType.begin(), filterType.end(), filterType.begin(), ::tolower);

            if (filterType == "gaussian")
                filter = [filterArg1, filterArg2, borderMode](const HDRImage & i) {return i.gaussianBlur(filterArg1,filterArg2,borderMode);};
            else if (filterType == "box")
                filter = [filterArg1, filterArg2, borderMode](const HDRImage & i) {return i.boxBlur(filterArg1,filterArg2,borderMode);};
            else if (filterType == "fast-gaussian")
                filter = [filterArg1, filterArg2, borderMode](const HDRImage & i) {return i.fastGaussianBlur(filterArg1,filterArg2,borderMode);};
            else if (filterType == "median")
                filter = [filterArg1, filterArg2, borderMode](const HDRImage & i) {return i.median(filterArg1,filterArg2,borderMode);};
            else if (filterType == "bilateral")
                filter = [filterArg1, filterArg2, borderMode](const HDRImage & i) {return i.bilateral(filterArg1,filterArg2,borderMode);};
            else if (filterType == "unsharp")
                filter = [filterArg1, filterArg2, borderMode](const HDRImage & i) {return i.unsharpMask(filterArg1,filterArg2,borderMode);};
            else
                throw invalid_argument(fmt::format("Unrecognized filter type: \"{}\".", filterType));

            console->info("Filtering using {}({:f},{:f}).", filterType, filterArg1, filterArg2);
        }

        if (docargs["--resize"].isString())
        {
            if (sscanf(docargs["--resize"].asString().c_str(), "%dx%d", &absoluteWidth, &absoluteHeight) == 2)
                relativeSize = false;
            else if (sscanf(docargs["--resize"].asString().c_str(), "%f%%x%f%%", &relativeWidth, &relativeHeight) == 2)
                relativeSize = true;
            else
                throw invalid_argument(fmt::format("Cannot parse --resize parameters:\t{}", docargs["--resize"].asString()));

            resize = true;
            if (relativeSize)
                console->info("Resizing images to a relative size of {:.1f}% x {:.1f}%.", relativeWidth, relativeHeight);
            else
                console->info("Resizing images to an absolute size of {:d} x {:d}.", absoluteWidth, absoluteHeight);
        }

        if (docargs["--remap"].isString())
        {
            char s1[32], s2[32], s3[32] = "bilinear";
            if (sscanf(docargs["--remap"].asString().c_str(), "%30[^','],%30[^','],%d,%30[^',']", s1, s2, &samples, s3) < 2)
                throw invalid_argument(fmt::format("Cannot parse --remap parameters:\t{}", docargs["--remap"].asString()));

            remap = true;

            UV2XYZFn dst2xyz;
            XYZ2UVFn xyz2src;

            string from = s1, to = s2;

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
                    throw invalid_argument(fmt::format("Cannot parse --remap parameters, unrecognized mapping type \"{}\"", from));

                if (to == "angularmap")
                    dst2xyz = angularMapToXYZ;
                else if (to == "mirrorball")
                    dst2xyz = mirrorBallToXYZ;
                else if (to == "latlong")
                    dst2xyz = latLongToXYZ;
                else if (to == "cubemap")
                    dst2xyz = cubeMapToXYZ;
                else
                    throw invalid_argument(fmt::format("Cannot parse --remap parameters, unrecognized mapping type \"{}\"", to));

                warp = [&](const Vector2f & uv) {return xyz2src(dst2xyz(Vector2f(uv(0), uv(1))));};
            }

            string interp = s3;
            if (interp == "nearest")
                sampler = [](const HDRImage & i, float x, float y, HDRImage::BorderMode m) {return i.nearest(x,y,m);};
            else if (interp == "bilinear")
                sampler = [](const HDRImage & i, float x, float y, HDRImage::BorderMode m) {return i.bilinear(x,y,m);};
            else if (interp == "bicubic")
                sampler = [](const HDRImage & i, float x, float y, HDRImage::BorderMode m) {return i.bicubic(x,y,m);};
            else
                throw invalid_argument(fmt::format("Cannot parse --remap parameters, unrecognized sampler type \"{}\"", interp));

            console->info("Remapping from {} to {} using {} interpolation with {:d} samples.", from, to, interp, samples);
        }

        if (docargs["--nan"].isString())
        {
            if (sscanf(docargs["--nan"].asString().c_str(), "%f,%f,%f", &nanColor[0], &nanColor[1], &nanColor[2]) != 3)
                throw invalid_argument("Cannot parse command-line parameter: --nan");

            console->info("Replacing NaNs and Infinities with ({}).", nanColor);
            fixNaNs = true;
        }

        dryRun = docargs["--dry-run"].asBool();
        if (dryRun)
            console->info("Only testing. Will not write files.");

        // list of filenames
        inFiles = docargs["<file>"].asStringList();


        // now actually do stuff
        if (!docargs["batch"].asBool())
        {
            console->info("Launching GUI. Start with -h for instructions on batch mode.");

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
                console->info("Reading image \"{}\"...", inFiles[i]);
                if (!image.load(inFiles[i]))
                {
                    console->error("Cannot read image \"{}\". Skipping...\n", inFiles[i]);
                    continue;
                }
                console->info("Image size: {:d}x{:d}", image.width(), image.height());

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
                    console->info("Filtering image with {}({})...", filterType, filterParams);

                    if (!dryRun)
                        image = filter(image);
                }

                if (resize || remap)
                {
                    int w = (int)round(relativeWidth/100.f*image.width());
                    int h = (int)round(relativeHeight/100.f*image.height());
                    if (!relativeSize)
                    {
                        w = absoluteWidth;
                        h = absoluteHeight;
                    }

                    if (!remap)
                    {
                        console->info("Resizing image to {:d}x{:d}...", w, h);
                        image = image.smoothScale(w, h);
                    }
                    else
                    {
                        console->info("Remapping image to {:d}x{:d}...", w, h);
                        image = image.resample(w, h, sampler, warp, samples,
                                               borderMode);
                    }
                }

                string thisExt = ext.size() ? ext : getExtension(inFiles[i]);
                string thisBasename = basename.size() ? basename : getBasename(inFiles[i]);
                string filename;
                if (inFiles.size() == 1 || !basename.size())
                    filename = thisBasename + "." + thisExt;
                else
                    filename = fmt::format("{}{:03d}.{}", thisBasename, i, thisExt);

                console->info("Writing image to \"{}\"...", filename);

                if (!dryRun)
                    image.save(filename, powf(2.0f, exposure), gamma, sRGB, dither);
            }

            if (average)
            {
                avgImg *= Color4(1.0f/inFiles.size());

                string filename = avgFilename;

                console->info("Writing average image to \"{}\"...", filename);

                if (!dryRun)
                    avgImg.save(filename, powf(2.0f, exposure), gamma, sRGB, dither);
            }
        }
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
