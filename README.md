# HDRView
This repository contains a simple research-oriented high-dynamic range image viewer with an emphasis on examining and comparing images, and including minimalistic tonemapping capabilities. HDRView currently supports reading EXR, PNG, TGA, BMP, HDR, JPG, GIF, PNM, and PSD images and writing EXR, HDR, PNG, TGA, and BMP images.

## Compiling

Compiling from scratch requires CMake and a recent version of XCode on Mac, Visual Studio 2015 on Windows, and GCC on Linux.

On Linux and MacOS, compiling should be as simple as

    git clone --recursive https://bitbucket.org/wkjarosz/hdrview.git
    cd hdrview
    mkdir build
    cmake-gui ../
    make -j 4

On Windows, open the generated file ``HDRView.sln`` after step 4 and proceed building as usual from within Visual Studio.


## Usage

Run ``./HDRView --help`` to see command-line usage, or run ``./HDRView`` and hit the ``h`` button to see a list of keyboard shortcuts in the application.

## License

3-clause BSD

``hdrview`` uses ``OpenEXR`` (http://www.openexr.com) which is licensed under a modified BSD license.

``hdrview`` uses stb (https://github.com/nothings/stb), developed by Sean Barrett which is licensed under public domain.