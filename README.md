# HDRView

Master status:
[![Build Status](https://github.com/wkjarosz/hdrview/actions/workflows/main.yml/badge.svg?branch=master)](https://github.com/wkjarosz/hdrview/actions/workflows/main.yml)

Develop status:
[![Build Status](https://github.com/wkjarosz/hdrview/actions/workflows/main.yml/badge.svg?branch=develop)](https://github.com/wkjarosz/hdrview/actions/workflows/main.yml)

HDRView is a simple research-oriented high-dynamic range image viewer with an emphasis on examining and comparing images, and including minimalistic tonemapping capabilities. HDRView currently supports reading EXR, PNG, TGA, BMP, HDR, JPG, GIF, PNM, PFM, and PSD images and writing EXR, HDR, PNG, TGA, PPM, PFM, and BMP images.

## Example screenshots
HDRView supports loading several images and provides exposure and gamma/sRGB tone mapping control with high-quality dithering of HDR images.
![Screenshot](resources/screenshot1.png "Screenshot1")
When sufficiently zoomed in, HDRView can overlay the pixel grid and numeric color values on each pixel to facilitate inspection.
![Screenshot](resources/screenshot2.png "Screenshot2")
Displaying HDR images naively on a 24 bit display leads to visible banding in smooth gradients.
![Screenshot](resources/screenshot3.png "Screenshot3")
HDRView supports high-quality dithering (both when viewing and when saving to an LDR file) to reduce these artifacts.
![Screenshot](resources/screenshot4.png "Screenshot4")

## Obtaining HDRView

If you are running a recent version of macOS, you can download the pre-built binary installer DMG from the [releases page](https://bitbucket.org/wkjarosz/hdrview/downloads). For other platforms, you will need to build HDRView from source for now.

## Compiling

Compiling from scratch requires CMake and a recent version of the XCode build tools on macOS, Visual Studio 2015 on Windows, and GCC on Linux.

### Linux and macOS

On Linux and macOS, compiling should be as simple as

    git clone --recursive https://bitbucket.org/wkjarosz/hdrview.git
    cd hdrview
    mkdir build
    cd build
    cmake-gui ../
    make -j 4

### Windows

On Windows, a few extra steps are needed.

Since MSVC's regex implementation is buggy, you first need to have the Boost regex library installed. You can find binary installers for Windows on the [Boost website](http://www.boost.org/). You need at least Boost version 1.53. Once installed, you can run:

    git clone --recursive https://bitbucket.org/wkjarosz/hdrview.git
    cd hdrview
    mkdir build
    cd build
    cmake ../
        -G"Visual Studio 15 2017 Win64" 
        -DBOOST_ROOT="C:\where_you_installed_boost"
        -DUSE_BOOST_REGEX=true

You can also do this through ``cmake-gui`` if you prefer. Click ``Add Entry`` and define ``BOOST_ROOT`` to the directory where you installed Boost (by default something like ``C:\local\boost_1_65_0``). Run ``Configure`` and select your version of Visual C++ and 64bit. After configure finishes, search for ``USE_BOOST_REGEX`` and check it. Run ``Configure`` again, and then click ``Generate``.

Open the generated file ``HDRView.sln`` and proceed building as usual from within Visual Studio.


## Installing on macOS

This should be as easy as ``make install``. On macOS this will copy the application bundle into /Applications and create the symlink ``hdrview`` in ``/usr/local/bin`` so you can launch HDRView from the terminal.

## HDRView usage

Run ``./hdrview --help`` to see the command-line options, or run ``./hdrview `` and hit the ``h`` button to see a list of keyboard shortcuts in the application.

## hdrbatch usage

There is also a separate executable ``hdrbatch`` intended for batch processing/converting images. Run ``./hdrbatch --help`` to see the command-line options.

## License

Copyright (c) Wojciech Jarosz

3-clause BSD. For details, see the ``LICENSE.txt`` file.

HDRView depends on the following libraries (which are included explicitly or as git submodules):

* Wenzel Jakob's [NanoGUI](https://github.com/wjakob/nanogui) library, which is licensed under a BSD-style license.
* ILM's [OpenEXR](http://www.openexr.com) library, which is licensed under a modified BSD license.
* Some [stb](https://github.com/nothings/stb) libraries, developed by Sean Barrett and released into the public domain.
* The [tinydir](https://github.com/cxong/tinydir/) library, which is licensed under a simplified BSD.
* The [docopt.cpp](https://github.com/docopt/docopt.cpp) library, which is dual-licensed under MIT and Boost licenses.
* Gabi Melman's [spdlog](https://github.com/gabime/spdlog) library, which is licensed under the MIT license.
* syoyo's [tinydngloader](https://github.com/syoyo/tinydngloader) library, which is licensed under the MIT license.
