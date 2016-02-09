# HDRView
This repository contains a simple research-oriented high-dynamic range image viewer with minimalistic tonemapping capabilities.

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

Run ./HDRView --help to see command-line usage, or run ./HDRView and hit the ``h'' button to see a list of keyboard shortcuts in the application.