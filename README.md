# HDRView

Master branch:
[![macOS build](https://github.com/wkjarosz/hdrview/actions/workflows/ci-mac.yml/badge.svg?branch=master)](https://github.com/wkjarosz/hdrview/actions/workflows/ci-mac.yml)
[![Linux build](https://github.com/wkjarosz/hdrview/actions/workflows/ci-linux.yml/badge.svg?branch=master)](https://github.com/wkjarosz/hdrview/actions/workflows/ci-linux.yml)
[![Windows build](https://github.com/wkjarosz/hdrview/actions/workflows/ci-windows.yml/badge.svg?branch=master)](https://github.com/wkjarosz/hdrview/actions/workflows/ci-windows.yml)

HDRView is a simple research-oriented high-dynamic range image viewer with an emphasis on examining and comparing images. HDRView currently supports reading (EXR, HDR, PFM, and Ultra HDR JPEG) and writing (EXR, HDR, PFM) several HDR image formats, as well as reading (PNG, TGA, BMP, JPG, GIF, PNM, and PSD) and writing (PNG, TGA, PPM, PFM, and BMP) standard LDR images.

HDRView can display images in true HDR on Apple extended dynamic range (EDR) and 10-bit displays.

HDRView runs on macOS, Linux, Windows, and directly in your browser -- just go to [wkjarosz.github.io/hdrview/](https://wkjarosz.github.io/hdrview/) for the latest release version and [wkjarosz.github.io/hdrview/dev](https://wkjarosz.github.io/hdrview/dev) for the development version. This even works on an iPhone or iPad! Try it out.

## Example screenshots
Here's a screenshot of HDRView viewing a JPEG on macOS:
![Screenshot](resources/screenshot-mac.png "Screenshot macOS")

Or, running on an iPad as a webapp, viewing a luminance-chroma EXR image stored using XYZ primaries with chroma subsampling:
![Screenshot](resources/screenshot-ipad.jpg "Screenshot iPad")

When sufficiently zoomed in, HDRView can overlay the pixel grid and numeric color values on each pixel to facilitate inspection:
![Screenshot](resources/screenshot-zoomed.png "Screenshot Zoomed-in")

HDRView features extensive keyboard shortcuts, and pressing `Cmd+Shift+P` brings up a VS Code/Atom/Sublime Text-style command palette allowing you to find any command with keyboard-based fuzzy searching:
![Screenshot](resources/screenshot-command-palette.png "Screenshot of command palette")

HDRView supports the extended dynamic range (XDR, 30 bit) capabilities of recent Macs, allowing it to use finer precision (reducing banding) and brighter whites (reducing clipping) when displaying HDR images.

When displaying images on a standard dynamic range (SDR, 24 bit) display (or saving to an LDR file format), HDRView uses blue-noise dithering:
![Screenshot](resources/screenshot-dithered.png "Screenshot dithering on")

This reduces apparent banding artifacts in smooth gradients compared to naively displaying HDR images on such displays:
![Screenshot](resources/screenshot-no-dither.png "Screenshot dithering off")


## Obtaining/running HDRView

You can download pre-built binaries for macOS, Linux, and Windows from the [releases page](https://github.com/wkjarosz/hdrview/releases). You can also just run the [web app version](https://wkjarosz.github.io/hdrview/) directly in your browser from any platform.

## Building from source

> [!IMPORTANT]  
> If you want to build from source you should check out a [published release tag](https://github.com/wkjarosz/hdrview/tags) -- the master branch is used for development and may be broken between releases.

Compiling from source requires:
- CMake >= 3.13
- A C++ toolchain for your platform (Xcode on macOS, Visual Studio on Windows, GCC/Clang on Linux)
- Optional: `ninja` for faster builds; `emscripten` if building the web version

Compiling should be as simple as:
```bash
git clone https://github.com/wkjarosz/hdrview.git
cd hdrview
cmake --preset default
cmake --build --preset default-release
```
You can see all available presets (defined in [`CMakePresets.json`](https://github.com/wkjarosz/hdrview/blob/master/CMakePresets.json)) by running:
```bash
cmake --list-presets=configure
cmake --list-presets=build
```

For instance:
- macOS:
    ```bash
	cmake --preset macos-arm64-cpm
	cmake --build --preset macos-arm64-cpm-release
    ```

- Linux (use `linux-local` to prefer system-installed deps or `linux-cpm` to let CPM fetch/build them):
    ```bash
    cmake --preset linux-local
    cmake --build --preset linux-local-release
    ```

- Windows (Visual Studio generator via presets):
    ```bash
	cmake --preset windows
	cmake --build --preset windows-release
    ```

- Emscripten (web build):
    ```bash
	cmake --preset emscripten
	cmake --build build/emscripten --parallel
    ```

By default HDRView uses [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) to download and build third-party dependencies automatically; however, you will still need to have several packages installed since those dependencies often try to find their own dependencies locally via `find_package`. You can check the GitHub Actions workflows under [`.github/workflows/`](https://github.com/wkjarosz/hdrview/tree/master/.github/workflows) to see which packages should be installed.

If you want to rely entirely on system-installed libraries instead (often desirably on linux), pick one of the `-local` presets (for example `linux-local` or `macos-x86_64-local`). These presets enable CPM.cmake's `CPM_USE_LOCAL_PACKAGES` option, which instructs it to try to use local packages via `find_package` first.

A number of options are available to control supported features (see the top of the [`CMakeLists.txt`](https://github.com/wkjarosz/hdrview/blob/master/CMakeLists.txt) file).

Alternatively, you should be able to do all this via VS Code if you have the CMake extension set up properly.

### Installation
On macOS you can just copy the `HDRView.app` bundle to your `/Applications` folder. Alternatively you can create a DMG installer using:
```bash
cpack -C Release -G DragNDrop
```
Recent version of macOS will complain that the app is unsigned and from an unknown developer. You will need to go to the Security and Privacy part of system Settings to allow HDRView to run.

On Windows you'll need to copy `HDRView.exe` together with the accompanying `assets` folder to your desired installation location.

HDRView provides an appimage installer. After configuring and building using the `linux-appimage` preset, you can create the appimage using:
```bash
cpack -C Release -G External
```

## Running from the terminal

You can also run HDRView from the terminal. Run ``HDRView --help`` to see the command-line options. On macOS the executable is stored within the app bundle in `HDRView.app/Contents/MacOS/HDRView`. You might want to add it, or a symlink, to your path.

## License
- Copyright (c) Wojciech Jarosz
- 3-clause BSD — see [LICENSE.txt](https://github.com/wkjarosz/hdrview/blob/master/LICENSE.txt) for details.

## Credits
- HDRView builds on a number of open-source libraries. See the About dialog (Credits) in the app for the full list.
