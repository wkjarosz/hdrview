//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "common.h"   // for get_basename, get_extension
#include "envmap.h"   // for XYZToAngularMap, XYZToCubeMap
#include "hdrimage.h" // for HDRImage
#include <ctype.h>    // for tolower
#include <iostream>   // for string
#include <nanogui/vector.h>
#include <random> // for normal_distribution, mt19937
#include <spdlog/spdlog.h>
#include "cliformatter.h"
#include <spdlog/fmt/ostr.h>

using namespace std;
using nanogui::Vector2f;

namespace
{
std::mt19937 g_rand(53);

} // namespace

int main(int argc, char **argv)
{
    set<string> env_format_names(
        {
            "angularmap",
            "mirrorball",
            "latlong",
            "cylindrical",
            "cubemap"
        });
    map<string, EEnvMappingUVMode> env_formats(
        {
            {"angularmap", ANGULAR_MAP},
            {"mirrorball", MIRROR_BALL},
            {"latlong", LAT_LONG},
            {"cylindrical", CYLINDRICAL},
            {"cubemap", CUBE_MAP}
        });
    set<string> lookup_mode_names(
        {
            "nearest",
            "bilinear",
            "bicubic"
        });
    map<string, HDRImage::Sampler> lookup_modes(
        {
            {"nearest", HDRImage::NEAREST},
            {"bilinear", HDRImage::BILINEAR},
            {"bicubic", HDRImage::BICUBIC}
        });
    set<string> border_mode_names(
        {
            "black", "mirror", "edge", "repeat"
        });
    map<string,HDRImage::BorderMode> border_modes(
        {
            {"black", HDRImage::BLACK},
            {"mirror", HDRImage::MIRROR},
            {"edge", HDRImage::EDGE},
            {"repeat", HDRImage::REPEAT}
        });

    string ext = "", avg_file = "", var_file = "", basename = "", error_type = "", reference_file = "", resize_text;
    tuple<string,float,float> filter_args;
    tuple<string,string,int,string> remap_args{"angularmap","angularmap",1,"bilinear"};
    tuple<string,string> border_mode_args{"black","black"};
    tuple<float,float> noise_args{0,0};
    tuple<float,float,float> nan_args{0,0,0};
    int   verbosity = 0, abs_width, abs_height;
    float gamma, exposure = 0.0f, rel_width = 1.f, rel_height = 1.f;
    bool dither = true, sRGB = true, dry_run = false, fix_NaNs = false, resize = false, remap = false, rel_size = true,
         save_files = false, make_noise = false, invert = false;
    Color3               nan_color(0.0f, 0.0f, 0.0f);
    // by default use a no-op passthrough warp function
    function<Vector2f(const Vector2f &)> warp = [](const Vector2f &uv) { return uv; };
    // no filter by default
    function<HDRImage(const HDRImage &)> filter;

    vector<string>             in_files;
    normal_distribution<float> gaussian(0, 0);

    try
    {
        CLI::App app{
R"(HDRBatch. Copyright (c) Wojciech Jarosz.

HDRBatch is a simple research-oriented tool for batch
processing high-dynamic range images. HDRBatch is freely
available under a 3-clause BSD license.
)",
"hdrbatch"};

        app.formatter(std::make_shared<ColorFormatter>());
        app.get_formatter()->column_width(20);
        app.get_formatter()->label("OPTIONS", fmt::format(fmt::emphasis::bold | fg(fmt::color::cornflower_blue), "OPTIONS"));

        app.add_option("-e,--exposure", exposure,
R"(Desired power of 2 EV or exposure value (gain = 2^exposure)
[default: 0].)")
            ->capture_default_str()
            ->group("Tone mapping and display")
            ;
        
        app.add_option("-g,--gamma", gamma,
R"(Desired gamma value for exposure+gamma tonemapping. An
sRGB curve is used if gamma is not specified.)")
            ->group("Tone mapping and display")
            ;

        app.add_option("-n, --nan", nan_args, "Replace all NaNs and INFs with (R,G,B).")
            ->type_size(3)
            ->option_text("FLOAT FLOAT FLOAT")
            ->group("Tone mapping and display")
            ;
        
        app.add_flag("--dither,--no-dither{false}", dither, "Enable/disable dithering when converting to LDR\n[default: on].")
            ->group("Tone mapping and display")
            ;
        
        app.add_flag("-s, --save", save_files, "Save the processed images. Specify output filename\nusing --out and/or --format.")
            ->group("Saving and converting")
            ;
        
        app.add_option("-o,--out", basename,
R"(Save image(s) using specified BASE output filename.
If multiple images are processed, an image sequence
is created by concatenating: BASE, image
number, and output format extension. For example:
    hdrbatch -o 'output-image-' -f png *.exr
would save all OpenEXR images in the working
directory as a PNG sequence 'output-image-%3d.png'.
If a single image is processed, the number is omitted.
If no basename is provided, the input files' basenames
are used instead and no numbers are appended (files
may be overwritten!). For example:
    hdrbatch -f png fileA.exr fileB.exr
would output 'fileA.png' and 'fileB.png'.)")
            ->option_text("BASE")
            ->group("Saving and converting")
            ;

        app.add_option("-f,--format", ext, fmt::format(
R"(Specify output file format EXTension, which is one of
 EXT : ({}).
If no format is given, each image is saved in its
original format (if supported).)",
            fmt::join(HDRImage::savable_formats(), " | ")))
            ->check(CLI::IsMember(HDRImage::savable_formats()))
            ->option_text("EXT")
            ->group("Saving and converting")
            ;
        
        app.add_flag("-i, --invert", invert, "Invert the image (compute 1-image).")
            ->group("Editing")
            ;
        
        app.add_option("--filter", filter_args,
R"(Process image(s) using filter TYPE, which must be
one of:
 TYPE : (gaussian | box | fast-gaussian | unsharp |
         bilateral | median),
with two filter-specific arguments following.

For example: '--filter fast-gaussian 10 10' would
filter using a 10x10 fast Gaussian approximation.)")
            ->check(CLI::Validator(CLI::IsMember({"gaussian", "box", "fast-gaussian", "median", "bilateral", "unsharp"})).application_index(0))
            ->option_text("TYPE FLOAT FLOAT")
            ->group("Editing")
            ;
        
        app.add_option("-r,--resize", resize_text,
R"(Resize the image to the size specified by SIZE_SPEC.
This currently uses a box filter for resampling, but
you can combine with a Gaussian blur to obtain
smoother downsampled results. The blur is applied
*before* downsampling.
SIZE_SPEC can be either absolute or relative.
Absolute: SIZE_SPEC integers, matching
the pattern '%dx%d', for instance: '640x480'.
Relative: SIZE is specified as floats, matching the
pattern '%fx%f' e.g. '.5x.25' would make the
image half its original width and a quarter its
original height.)")
            ->option_text("SIZE_SPEC")
            ->group("Editing")
            ;

        // FIXME: the two optional arguments don't default to the correct values if not specified
        // For now we just require all arguments
        app.add_option("--remap", remap_args,
R"(Remap the input image from one environment map format
to another.

The first two arguments are the input and output
environment map formats respectively and must be one of:
  M : (latlong | angularmap | mirrorball | cubemap).
Specifying the same format twice results in no change. 

The 3rd argument S specifies S^2 super-sampling,
where the default is S=1: one centered sample per pixel.

The 4th argument L specifies the sampling lookup mode
and must be one of:
  L : (nearest | bilinear | bicubic).
Combine with --resize to specify output file dimensions.)")
            ->check(CLI::Validator(CLI::IsMember(env_format_names)).application_index(0))
            ->check(CLI::Validator(CLI::IsMember(env_format_names)).application_index(1))
            ->check(CLI::Validator(CLI::PositiveNumber).application_index(2))
            ->check(CLI::Validator(CLI::IsMember(lookup_mode_names)).application_index(3))
            ->option_text("M M S L")
            ->group("Editing")
            ;

        app.add_option("--border-mode", border_mode_args,
R"(Specifies what x- and y-modes to use when accessing pixels
outside the bounds of the image. Each must be one of:
  MODE : (black | mirror | edge | repeat)
[default: edge edge])")
            ->check(CLI::Validator(CLI::IsMember(border_mode_names)))
            ->option_text("MODE MODE")
            ->group("Editing")
            ;

        app.add_option("--random-noise", noise_args,
R"(Replace pixel values with random Gaussian noise with mean
MEAN and variance VAR.)")
            ->option_text("MEAN VAR")
            ->group("Editing")
            ;

        app.add_option("--error", error_type,
R"(Compute the error or difference between the images
and a reference image (specified with --reference).
The type error type can be one of:
 TYPE : (squared | absolute | relative-squared).
The type name is appended to the saved filename (before
image sequence number).)")
            ->check(CLI::Validator(CLI::IsMember({"squared", "absolute", "relative-squared"})))
            ->option_text("TYPE")
            ->group("Calculating statistics")
            ;

        app.add_option("--reference", reference_file,
            "Specify the reference image for error computation.")
            ->check(CLI::ExistingFile)
            ->option_text("FILE")
            ->group("Calculating statistics")
            ;

        app.add_option("-a, --average", avg_file,
R"(Average all loaded images and save to FILE
(all images must have the same dimensions).)")
            ->option_text("FILE")
            ->group("Calculating statistics")
            ;

        app.add_option("--variance", var_file,
R"(Compute an unbiased reference-less sample variance
of FILEs and save to FILE. This uses the FILEs
themselves to compute the mean, and uses the (n-1)
Bessel correction factor.)")
            ->option_text("FILE")
            ->group("Calculating statistics")
            ;
        
        app.add_option("-v,--verbosity", verbosity,
R"(Set verbosity threshold T with lower values meaning more
verbose and higher values removing low-priority messages.
All messages with severity >= T are displayed, where the
severities are:
    trace    = 0
    debug    = 1
    info     = 2
    warn     = 3
    err      = 4
    critical = 5
    off      = 6
The default is 2 (info).)")
            ->check(CLI::Range(0, 6))
            ->option_text("INT in [0-6]")
            ->group("Misc")
            ;

        app.set_version_flag("--version", "hdrbatch " HDRVIEW_VERSION, "Show the version and exit.")
            ->group("Misc")
            ;
        app.set_help_flag("-h, --help", "Print this help message and exit.")
            ->group("Misc")
            ;

        app.add_flag("--dry-run", dry_run,
R"(Don't actually save any files, just report what would
be done.)")
            ->group("Misc")
            ;

        app.add_option(
            "FILES", in_files,
            "The images files to load.")
            ->check(CLI::ExistingPath)
            ->required()
            ->option_text("PATH(existing) ...");


        CLI11_PARSE(app, argc, argv);

        // Console logger with color
        spdlog::set_pattern("%^[%l]%$ %v");
        spdlog::set_level(spdlog::level::level_enum(verbosity));
        spdlog::flush_on(spdlog::level::level_enum(verbosity));

        spdlog::info("Welcome to hdrbatch!");
        spdlog::info("Verbosity threshold set to level {:d}.", verbosity);
        spdlog::info("Setting intensity scale to {:f}", powf(2.0f, exposure));

        if (app.count("--gamma"))
        {
            sRGB  = false;
            spdlog::info("Setting gamma correction to g={:f}.", gamma);
        }
        else
            spdlog::info("Using sRGB response curve.");

        spdlog::info("{}", (dither) ? "Dithering" : "Not dithering");

        spdlog::info("Border mode set to: {},{}.", get<0>(border_mode_args), get<1>(border_mode_args));

        if (app.count("-f"))
            spdlog::info("Converting to \"{}\".", ext);
        else
            spdlog::info("Keeping original image file formats.");

        if (app.count("--out"))
            spdlog::info("Setting base filename to \"{}\".", basename);
        if (app.count("--average"))
            spdlog::info("Saving average image to \"{}\".", avg_file);
        if (app.count("--variance"))
            spdlog::info("Saving variance image to \"{}\".", var_file);

        if (app.count("--filter"))
        {
            AtomicProgress progress;
            if (get<0>(filter_args) == "gaussian")
                filter = [&border_modes, filter_args, progress, border_mode_args](const HDRImage &i)
                { return i.gaussian_blurred(get<1>(filter_args), get<2>(filter_args), progress, border_modes[get<0>(border_mode_args)], border_modes[get<1>(border_mode_args)]); };
            else if (get<0>(filter_args) == "box")
                filter = [&border_modes, filter_args, progress, border_mode_args](const HDRImage &i)
                { return i.box_blurred(get<1>(filter_args), get<2>(filter_args), progress, border_modes[get<0>(border_mode_args)], border_modes[get<1>(border_mode_args)]); };
            else if (get<0>(filter_args) == "fast-gaussian")
                filter = [&border_modes, filter_args, progress, border_mode_args](const HDRImage &i)
                { return i.fast_gaussian_blurred(get<1>(filter_args), get<2>(filter_args), progress, border_modes[get<0>(border_mode_args)], border_modes[get<1>(border_mode_args)]); };
            else if (get<0>(filter_args) == "median")
                filter = [&border_modes, filter_args, progress, border_mode_args](const HDRImage &i)
                { return i.median_filtered(get<1>(filter_args), get<2>(filter_args), progress, border_modes[get<0>(border_mode_args)], border_modes[get<1>(border_mode_args)]); };
            else if (get<0>(filter_args) == "bilateral")
                filter = [&border_modes, filter_args, progress, border_mode_args](const HDRImage &i)
                { return i.bilateral_filtered(get<1>(filter_args), get<2>(filter_args), progress, border_modes[get<0>(border_mode_args)], border_modes[get<1>(border_mode_args)]); };
            else if (get<0>(filter_args) == "unsharp")
                filter = [&border_modes, filter_args, progress, border_mode_args](const HDRImage &i)
                { return i.unsharp_masked(get<1>(filter_args), get<2>(filter_args), progress, border_modes[get<0>(border_mode_args)], border_modes[get<1>(border_mode_args)]); };
            else
                throw invalid_argument(fmt::format("Unrecognized filter type: \"{}\".", get<0>(filter_args)));

            spdlog::info("Filtering using {}({:f},{:f}).", get<0>(filter_args), get<1>(filter_args), get<2>(filter_args));
        }

        if (app.count("--error"))
        {
            if (!app.count("--reference"))
                throw invalid_argument("Need to specify a reference file for error computation.");

            spdlog::info("Computing {} error using {} as reference.", error_type, reference_file);
        }

        if (app.count("--resize"))
        {
            if (sscanf(resize_text.c_str(), "%dx%d", &abs_width, &abs_height) == 2)
                rel_size = false;
            else if (sscanf(resize_text.c_str(), "%fx%f", &rel_width, &rel_height) == 2)
                rel_size = true;
            else
                throw invalid_argument(
                    fmt::format("Cannot parse --resize parameters:\t{}", resize_text));

            resize = true;
            if (rel_size)
                spdlog::info("Resizing images to a relative size of {:.1f}% x {:.1f}%.", rel_width*100, rel_height*100);
            else
                spdlog::info("Resizing images to an absolute size of {:d} x {:d}.", abs_width, abs_height);
        }

        if (app.count("--remap"))
        {
            remap = true;

            if (get<0>(remap_args) != get<1>(remap_args))
                warp = [&env_formats, remap_args](const Vector2f &uv) { return convertEnvMappingUV(env_formats[get<1>(remap_args)], env_formats[get<0>(remap_args)], uv); };

            spdlog::info("Remapping from {} to {} using {} interpolation with {:d}x{:d} samples.", get<0>(remap_args), get<1>(remap_args), get<3>(remap_args), get<2>(remap_args), get<2>(remap_args));
        }

        if (app.count("--random-noise"))
        {
            make_noise = true;
            gaussian = normal_distribution<float>(get<0>(noise_args), sqrt(get<1>(noise_args)));
            spdlog::info("Replacing images with random-noise({:f},{:f}).", get<0>(noise_args), get<1>(noise_args));
        }

        if (app.count("--nan"))
        {
            spdlog::info("Replacing NaNs and Infinities with ({}).", fmt::join(nan_args, ", "));
            fix_NaNs = true;
            nan_color = Color3(get<0>(nan_args), get<1>(nan_args), get<2>(nan_args));
        }

        if (dry_run)
            spdlog::info("Only testing. Will not write files.");

        // now actually do stuff
        HDRImage reference_image;
        if (!reference_file.empty())
        {
            spdlog::info("Reading reference image \"{}\"...", reference_file);
            if (!reference_image.load(reference_file))
                throw invalid_argument(fmt::format("Cannot read image \"{}\".", reference_file));
            spdlog::info("Reference image size: {:d}x{:d}", reference_image.width(), reference_image.height());
        }

        HDRImage avgImg;
        HDRImage varImg;
        int      varN = 0;

        for (size_t i = 0; i < in_files.size(); ++i)
        {
            HDRImage image;
            spdlog::info("Reading image \"{}\"...", in_files[i]);
            if (!image.load(in_files[i]))
            {
                spdlog::error("Cannot read image \"{}\". Skipping...\n", in_files[i]);
                continue;
            }
            spdlog::info("Image size: {:d}x{:d}", image.width(), image.height());

            varN += 1;
            // initialize variables for average and variance
            if (varN == 1)
            {
                // set images to zeros
                varImg = avgImg = image.apply_function([](const Color4 &c) { return Color4(0, 0, 0, 0); });
            }

            if (fix_NaNs || !dry_run)
                image = image.apply_function([nan_color](const Color4 &c)
                                             { return isfinite(c.sum()) ? c : Color4(nan_color, c[3]); });

            if (!avg_file.empty() || !var_file.empty())
            {
                if (avgImg.width() != image.width() || avgImg.height() != image.height())
                    throw invalid_argument("Images do not have the same size.");

                // incremental average and variance computation
                auto delta = image - avgImg;
                avgImg += delta / Color4(varN, varN, varN, varN);
                auto delta2 = image - avgImg;
                varImg += delta * delta2;
            }

            if (filter)
            {
                spdlog::info("Filtering image with {}({:f},{:f})...", get<0>(filter_args), get<1>(filter_args), get<2>(filter_args));

                if (!dry_run)
                    image = filter(image);
            }

            if (resize || remap)
            {
                int w = (int)round(rel_width * image.width());
                int h = (int)round(rel_height * image.height());
                if (!rel_size)
                {
                    w = abs_width;
                    h = abs_height;
                }

                if (!remap)
                {
                    spdlog::info("Resizing image to {:d}x{:d}...", w, h);
                    image = image.resized(w, h);
                }
                else
                {
                    spdlog::info("Remapping image to {:d}x{:d}...", w, h);
                    AtomicProgress progress;
                    image = image.resampled(w, h, progress, warp, get<2>(remap_args), lookup_modes[get<3>(remap_args)], border_modes[get<0>(border_mode_args)], border_modes[get<1>(border_mode_args)]);
                }
            }

            if (make_noise)
            {
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                    {
                        image(x, y) = Color4(gaussian(g_rand), gaussian(g_rand), gaussian(g_rand), 1.0f);
                    }
            }

            if (!error_type.empty())
            {
                if (image.width() != reference_image.width() || image.height() != reference_image.height())
                {
                    spdlog::error("Images must have same dimensions!");
                    continue;
                }

                if (error_type == "squared")
                    image = (image - reference_image).square();
                else if (error_type == "absolute")
                    image = (image - reference_image).abs();
                else // if (error_type == "relative-squared")
                    image = (image - reference_image).square() /
                            (reference_image.square() + Color4(1e-3f, 1e-3f, 1e-3f, 1e-3f));

                Color4 meanError = image.mean();
                Color4 maxError  = image.max();

                image.set_alpha(1.0f);

                spdlog::info(fmt::format("Mean {} error: {}.", error_type, meanError));
                spdlog::info(fmt::format("Max {} error: {}.", error_type, maxError));
            }

            if (invert)
            {
                image = Color4(1.0f, 1.0f, 1.0f, 2.0f) - image;
            }

            if (save_files)
            {
                string thisExt      = ext.size() ? ext : get_extension(in_files[i]);
                string thisBasename = basename.size() ? basename : get_basename(in_files[i]);
                string filename;
                string extra = (error_type.empty()) ? "" : fmt::format("-{}-error", error_type);
                if (in_files.size() == 1 || !basename.size())
                    filename = fmt::format("{}{}.{}", thisBasename, extra, thisExt);
                else
                    filename = fmt::format("{}{}{:03d}.{}", thisBasename, extra, i, thisExt);

                spdlog::info("Writing image to \"{}\"...", filename);

                if (!dry_run)
                    image.save(filename, powf(2.0f, exposure), gamma, sRGB, dither);
            }
        }

        if (!avg_file.empty())
        {
            spdlog::info("Writing average image to \"{}\"...", avg_file);

            if (!dry_run)
                avgImg.save(avg_file, powf(2.0f, exposure), gamma, sRGB, dither);
        }

        if (!var_file.empty())
        {
            varImg /= Color4(varN - 1, varN - 1, varN - 1, varN - 1);

            // set alpha channel to 1
            varImg = varImg.apply_function([](const Color4 &c) { return Color4(c.r, c.g, c.b, 1); });

            spdlog::info("Writing variance image to \"{}\"...", var_file);

            if (!dry_run)
                varImg.save(var_file, powf(2.0f, exposure), gamma, sRGB, dither);
        }
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
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
