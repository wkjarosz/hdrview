/*
    hdrbatch.cpp -- HDRBatch application entry point

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/
#include <ctype.h>                       // for tolower
#include <docopt.h>                      // for docopt
#include <Eigen/Core>                    // for Vector2f
#include <iostream>                      // for string
#include <random>                        // for normal_distribution, mt19937
#include "common.h"                      // for getBasename, getExtension
#include "hdrimage.h"                    // for HDRImage
#include "envmap.h"                      // for XYZToAngularMap, XYZToCubeMap
#include "hdrviewer.h"                   // for spdlog
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

using namespace std;
namespace spd = spdlog;

namespace
{
std::mt19937 g_rand(53);

HDRImage::BorderMode parseBorderMode(const string &mode)
{
	if (mode == "black")
		return HDRImage::BLACK;
	if (mode == "mirror")
		return HDRImage::MIRROR;
	if (mode == "repeat")
		return HDRImage::REPEAT;
	if (mode == "edge")
		return HDRImage::EDGE;

	throw invalid_argument(fmt::format("Invalid border mode \"{}\".", mode));
}
}

static const char USAGE[] =
R"(HDRBatch. Copyright (c) Wojciech Jarosz.

HDRBatch is a simple research-oriented tool for batch
processing high-dynamic range images. HDRBatch is freely
available under a 3-clause BSD license.

Usage:
  hdrbatch [options FILE...]
  hdrbatch -h | --help | --version

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
  -s, --save               Save the processed images. Specify output filename
                           using --out and/or --format.
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
  --invert, -i             Invert the image (compute 1-image).
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
                           The optional L parameter specifies the sampling lookup
                           mode: L : (nearest | bilinear | bicubic).
                           Specifying the same M parameter twice results in no
                           change. Combine with --resize to specify output file
                           dimensions.
  --border-mode=MODE,MODE  Specifies what x- and y-modes to use when accessing pixels
                           outside the bounds of the image.
                           MODE : (black | mirror | edge | repeat)
                           [default: edge,edge]
  --error=TYPE             Compute the error or difference between the images
                           and a reference image, specified with --reference.
                           The error type can be:
                           TYPE : (squared | absolute | relative-squared).
                           The 'TYPE' is appended to the saved filename (before
                           image sequence number).
  --reference=FILE         Specify the reference image for error computation.
  -a FILE, --average=FILE  Average all loaded images and save to FILE
                           (all images must have the same dimensions).
  --variance=FILE          Compute an unbiased reference-less sample variance
                           of FILEs and save to FILE. This uses the FILEs
                           themselves to compute the mean, and uses the (n-1)
                           Bessel correction factor.
  --random-noise=M,V       Generate random Gaussian noise with mean M and
                           variance V.
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
           varFilename = "",
           basename = "",
           filterType = "",
           filterParams = "",
           errorType = "",
           referenceFile = "";
    int verbosity = 0, absoluteWidth, absoluteHeight, samples = 1;
    float gamma, exposure, relativeWidth = 100.f, relativeHeight = 100.f,
          noiseMean = 0, noiseVar = 0;
    bool dither = true,
         sRGB = true,
         dryRun = true,
         fixNaNs = false,
         resize = false,
         remap = false,
         relativeSize = true,
         saveFiles = false,
         makeNoise = false,
         invert = false;
    HDRImage::BorderMode borderModeX, borderModeY;
    Color3 nanColor(0.0f,0.0f,0.0f);
    // by default use a no-op passthrough warp function
    function<Vector2f(const Vector2f&)> warp = [](const Vector2f & uv) {return uv;};
    // use bilinear lookup by default
    HDRImage::Sampler sampler = HDRImage::BILINEAR;
    // no filter by default
    function<HDRImage(const HDRImage &)> filter;

    vector<string> inFiles;
    normal_distribution<float> normalDist(0,0);

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
                                 true,             // show help if requested
                                 "HDRBatch 0.1");  // version string

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

        console->flush_on(spd::level::level_enum(verbosity));

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

        // border mode
        if (docargs["--border-mode"])
        {
            if (docargs["--border-mode"].isString())
            {
                char first[22], second[32];
                if (sscanf(docargs["--border-mode"].asString().c_str(), "%20[^','],%20s", first, second) != 2)
                    throw invalid_argument(
                        fmt::format("Invalid border mode \"{}\".", docargs["--border-mode"].asString()));

                borderModeX = parseBorderMode(first);
                borderModeY = parseBorderMode(second);
            }
            else
                throw invalid_argument(fmt::format("Invalid border mode \"{}\".", docargs["--border-mode"].asString()));
        }

        console->info("Setting border mode to: {}.", docargs["--border-mode"].asString());

        saveFiles = docargs["--save"].asBool();
        invert = docargs["--invert"].asBool();

        if (docargs["--format"].isString())
        {
            ext = docargs["--format"].asString();
            console->info("Converting to \"{}\".", ext);
        }
        else
            console->info("Keeping original image file formats.");

        if (docargs["--out"].isString())
        {
            basename = docargs["--out"].asString();
            console->info("Setting base filename to \"{}\".", basename);
        }

        if (docargs["--average"].isString())
        {
            avgFilename = docargs["--average"].asString();
            console->info("Saving average image to \"{}\".", avgFilename);
            if (docargs["FILE"].asStringList().size() < 2)
                console->error("Computing an average from less than 2 images!");
        }

        if (docargs["--variance"].isString())
        {
            varFilename = docargs["--variance"].asString();
            if (docargs["FILE"].asStringList().size() < 2)
                throw invalid_argument("Computing reference-less variance requires at least 2 images.");
            console->info("Saving variance image to \"{}\".", varFilename);
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
                filter = [filterArg1, filterArg2, borderModeX, borderModeY](const HDRImage & i) {return i
                    .GaussianBlurred(filterArg1, filterArg2, borderModeX, borderModeY);};
            else if (filterType == "box")
                filter = [filterArg1, filterArg2, borderModeX, borderModeY](const HDRImage & i) {return i
                    .boxBlurred(filterArg1, filterArg2, borderModeX, borderModeY);};
            else if (filterType == "fast-gaussian")
                filter = [filterArg1, filterArg2, borderModeX, borderModeY](const HDRImage & i) {return i
                    .fastGaussianBlurred(filterArg1, filterArg2, borderModeX, borderModeY);};
            else if (filterType == "median")
                filter = [filterArg1, filterArg2, borderModeX, borderModeY](const HDRImage & i) {return i
                    .medianFiltered(filterArg1, filterArg2, borderModeX, borderModeY);};
            else if (filterType == "bilateral")
                filter = [filterArg1, filterArg2, borderModeX, borderModeY](const HDRImage & i) {return i
                    .bilateralFiltered(filterArg1, filterArg2, borderModeX, borderModeY);};
            else if (filterType == "unsharp")
                filter = [filterArg1, filterArg2, borderModeX, borderModeY](const HDRImage & i) {return i
                    .unsharpMasked(filterArg1, filterArg2, borderModeX, borderModeY);};
            else
                throw invalid_argument(fmt::format("Unrecognized filter type: \"{}\".", filterType));

            console->info("Filtering using {}({:f},{:f}).", filterType, filterArg1, filterArg2);
        }

        if (docargs["--error"].isString())
        {
            char type[22];
            if (sscanf(docargs["--error"].asString().c_str(), "%s", type) != 1)
                throw invalid_argument(fmt::format("Cannot parse command-line parameter: --error:\t{}", docargs["--error"].asString()));

            errorType = type;
            if (errorType != "squared" && errorType != "absolute" && errorType != "relative-squared")
                throw invalid_argument(fmt::format("Invalid error TYPE specified in --error:\t{}", docargs["--error"].asString()));

            if (docargs["--reference"].isString())
                referenceFile = docargs["--reference"].asString();
            else
                throw invalid_argument("Need to specify a reference file for error computation.");

            console->info("Computing {} error using {} as reference.", errorType, referenceFile);
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
                sampler = HDRImage::NEAREST;
            else if (interp == "bilinear")
                sampler = HDRImage::BILINEAR;
            else if (interp == "bicubic")
                sampler = HDRImage::BICUBIC;
            else
                throw invalid_argument(fmt::format("Cannot parse --remap parameters, unrecognized sampler type \"{}\"", interp));

            console->info("Remapping from {} to {} using {} interpolation with {:d} samples.", from, to, interp, samples);
        }

        if (docargs["--random-noise"].isString())
        {
            makeNoise = true;
            if (sscanf(docargs["--random-noise"].asString().c_str(), "%f,%f", &noiseMean, &noiseVar) != 2)
                throw invalid_argument("Cannot parse command-line parameter: --random-noise");
            normalDist = normal_distribution<float>(noiseMean, sqrt(noiseVar));
            console->info("Replacing images with random-noise({:f},{:f}).", noiseMean, noiseVar);
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
        inFiles = docargs["FILE"].asStringList();


        // now actually do stuff
        if (!inFiles.size())
            throw invalid_argument("No files specified!");

        HDRImage referenceImage;
        if (!referenceFile.empty())
        {
            console->info("Reading reference image \"{}\"...", referenceFile);
            if (!referenceImage.load(referenceFile))
                throw invalid_argument(fmt::format("Cannot read image \"{}\".", referenceFile));
            console->info("Reference image size: {:d}x{:d}", referenceImage.width(), referenceImage.height());
        }

        HDRImage avgImg;
        HDRImage varImg;
        int varN = 0;

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

            varN += 1;
            // initialize variables for average and variance
            if (varN == 1)
            {
                // set images to zeros
                varImg = avgImg = image.unaryExpr([](const Color4 & c)
                {
                    return Color4(0,0,0,0);
                });
            }

            if (fixNaNs || !dryRun)
                image = image.unaryExpr([nanColor](const Color4 & c)
                {
                    return isfinite(c.sum()) ? c : Color4(nanColor, c[3]);
                });

            if (!avgFilename.empty() || !varFilename.empty())
            {
                if (avgImg.width() != image.width() || avgImg.height() != image.height())
                    throw invalid_argument("Images do not have the same size.");

                // incremental average and variance computation
                auto delta = image - avgImg;
                avgImg += delta/Color4(varN,varN,varN,varN);
                auto delta2 = image - avgImg;
                varImg += delta * delta2;
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
                    image = image.resized(w, h);
                }
                else
                {
                    console->info("Remapping image to {:d}x{:d}...", w, h);
                    image = image.resampled(w, h, warp, samples,
                                            sampler, borderModeX, borderModeY);
                }
            }

            if (makeNoise)
            {
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                    {
                        image(x,y) = Color4(normalDist(g_rand), normalDist(g_rand),
                                      normalDist(g_rand), 1.0f);
                    }
            }

            if (!errorType.empty())
            {
                if (image.width() != referenceImage.width() ||
                    image.height() != referenceImage.height())
                {
                    console->error("Images must have same dimensions!");
                    continue;
                }

                if (errorType == "squared")
                    image = (image-referenceImage).square();
                else if (errorType == "absolute")
                    image = (image-referenceImage).abs();
                else //if (errorType == "relative-squared")
                    image = (image-referenceImage).square() / (referenceImage.square() + Color4(1e-3f, 1e-3f, 1e-3f, 1e-3f));

                Color4 meanError = image.mean();
                Color4 maxError = image.max();

                image.setAlpha(1.0f);

                console->info(fmt::format("Mean {} error: {}.", errorType, meanError));
                console->info(fmt::format("Max {} error: {}.", errorType, maxError));
            }

            if (invert)
            {
                image = Color4(1.0f, 1.0f, 1.0f, 2.0f) - image;
            }

            if (saveFiles)
            {
                string thisExt = ext.size() ? ext : getExtension(inFiles[i]);
                string thisBasename = basename.size() ? basename : getBasename(inFiles[i]);
                string filename;
                string extra = (errorType.empty()) ? "" : fmt::format("-{}-error", errorType);
                if (inFiles.size() == 1 || !basename.size())
                    filename = fmt::format("{}{}.{}", thisBasename, extra, thisExt);
                else
                    filename = fmt::format("{}{}{:03d}.{}", thisBasename, extra, i, thisExt);

                console->info("Writing image to \"{}\"...", filename);

                if (!dryRun)
                    image.save(filename, powf(2.0f, exposure), gamma, sRGB, dither);
            }
        }

        if (!avgFilename.empty())
        {
            // avgImg *= Color4(1.0f/inFiles.size());

            console->info("Writing average image to \"{}\"...", avgFilename);

            if (!dryRun)
                avgImg.save(avgFilename, powf(2.0f, exposure), gamma, sRGB, dither);
        }

        if (!varFilename.empty())
        {
            varImg /= Color4(varN - 1, varN - 1, varN - 1, varN - 1);

            // set alpha channel to 1
            varImg = varImg.unaryExpr([](const Color4 & c)
            {
                return Color4(c.r,c.g,c.b,1);
            });

            console->info("Writing variance image to \"{}\"...", varFilename);

            if (!dryRun)
                varImg.save(varFilename, powf(2.0f, exposure), gamma, sRGB, dither);
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
