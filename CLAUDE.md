# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

HDRView is a cross-platform (macOS/Linux/Windows/Emscripten web) research-oriented image viewer, with an
emphasis on high-dynamic-range (HDR) image formats. It's a single C++17 executable target built on
[Hello ImGui](https://github.com/pthom/hello_imgui) (Dear ImGui + ImPlot + a windowing/backend abstraction),
rendering through OpenGL/GLES everywhere except macOS, which uses GLFW+Metal.

## Build

Builds are driven entirely through CMake presets (`CMakePresets.json`) — there is no ad hoc build path.
List available presets with `cmake --list-presets=configure` / `--list-presets=build`.

Typical two-step configure+build, e.g. on macOS:
```bash
cmake --preset macos-arm64-cpm
cmake --build --preset macos-arm64-cpm-release
```
Linux:
```bash
cmake --preset linux-local        # or linux-cpm to let CPM build deps instead of using system libs
cmake --build --preset linux-local-release
```
Windows (Visual Studio generator):
```bash
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```
Emscripten (web build):
```bash
cmake --preset emscripten
cmake --build build/emscripten --parallel
```

Preset naming: `<platform>-<arch?>-<cpm|local>[-<buildtype>]`. `-cpm` presets fetch/build third-party
dependencies via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake); `-local` presets prefer
system-installed packages (`find_package` first, via `CPM_USE_LOCAL_PACKAGES`). Build type suffixes are
`-debug`, `-release`, `-minsizerel`, `-relwithdebinfo`. Even with `-cpm` presets, several system packages
must be pre-installed since CPM-fetched deps still `find_package()` some of their own dependencies — check
the relevant `.github/workflows/ci-*.yml` for the exact package list per platform.

After building, verify the binary runs, e.g.:
```bash
./build/<preset>/<buildtype>/HDRView.app/Contents/MacOS/HDRView --help   # macOS
./build/<preset>/HDRView --help                                          # Linux/Windows
```

There's also a "checking that HDRView runs" smoke check in CI that just invokes `HDRView --help` on every
built preset — keep that working even when the GUI itself can't be exercised headlessly.

Packaging (DMG on macOS, AppImage on Linux) is done via the `workflowPresets` (`dmg-*`, `appimage`) or
directly with `cpack -C Release -G DragNDrop` / `cpack -C Release -G External` after building.

### Key CMake options (top of `CMakeLists.txt`)
Per-format support is individually toggleable, e.g. `HDRVIEW_ENABLE_LIBJXL`, `HDRVIEW_ENABLE_LIBRAW`,
`HDRVIEW_ENABLE_AVIF`, `HDRVIEW_ENABLE_LIBWEBP`, `HDRVIEW_ENABLE_LIBTIFF`, `HDRVIEW_ENABLE_LIBUHDR`, etc.
(pass as `-D<OPTION>=OFF` at configure time). `HDRVIEW_ICONSET` selects which icon font/set the GUI uses
(`HDRVIEW_ICONSET_FA6|LC|MS|MD|MDI`). `HDRVIEW_BUILD_TESTS` (default `OFF`) builds the `hdrview_tests`
doctest binary described below.

### Tests
`HDRVIEW_BUILD_TESTS=ON` builds a `hdrview_tests` executable (doctest-based, see `tests/*.cpp`) that reuses
the main app's sources/dependencies (minus `src/hdrview.cpp`, since doctest supplies its own `main()`), and
registers with ctest via `doctest_discover_tests`:
```bash
cmake --preset macos-arm64-cpm -DHDRVIEW_BUILD_TESTS=ON
cmake --build --preset macos-arm64-cpm-release
ctest --test-dir build/macos-arm64-cpm -C Release --output-on-failure
```
Coverage: `test_colorspace.cpp` (gamut/transfer-function round-trips), `test_pixel_stats.cpp`, and
`test_exr_io.cpp`/`test_png_io.cpp` (loader correctness). The EXR/PNG tests conditionally compile in extra
cases against vendored real-world test images (OpenEXR's test suite, libpng's pngsuite/testpngs) that are
only present when CPM fetched those libraries from source — i.e. only on `-cpm`/`-universal` presets, not
`-local` ones; see the `HDRVIEW_TEST_OPENEXR_DIR`/`HDRVIEW_TEST_PNG_CONTRIB_DIR` guards in `CMakeLists.txt`
and those test files. CI builds+runs the full suite on exactly one `-cpm`/`-universal` preset per platform
(Release only), not across the whole preset matrix — see `run_tests`/`HDRVIEW_BUILD_TESTS` in
`.github/workflows/ci-{mac,linux,windows}.yml`. Treat a successful build plus `ctest` as the verification bar
for anything covered by these tests; for everything else (most GUI/interaction code), fall back to a
manual/CLI smoke check.

### Code formatting
`.clang-format` (Microsoft-based, 4-space indent, 120 col, Allman braces) governs C++ style; run
`clang-format` before committing. `.cmake-format`/`.cmake-format.json` similarly govern `CMakeLists.txt`
formatting.

## Architecture

### Application core: the `HDRViewApp` god-object
`src/app.h` declares a single `HDRViewApp` class that owns essentially all mutable state: the loaded image
list, current/reference image selection, viewport pan/zoom/exposure/tonemap settings, GUI dialog state, and
the render pass/shader. There's a global singleton accessor `hdrview()` (set up by `init_hdrview()` in
`hdrview.cpp`, the `main()` entry point). Its implementation is split by concern across files rather than by
class, since it's all one class:
- `app.cpp` — construction, lifecycle, core state
- `app-gui.cpp` — ImGui widget/window drawing (menus, panels, status bar, dialogs)
- `app-draw.cpp` — image/viewport rendering (background, pixel grid, overlays)
- `app-file-io.cpp` — open/save/load-folder/drag-drop plumbing (delegates actual decode to `imageio/`)
- `app-windows.cpp` — auxiliary windows (channel stats, pixel inspector, command palette, etc.)
- `app-zoom.cpp` — pan/zoom/viewport-fit math and coordinate transforms

`app.h` documents three coordinate systems used throughout: **app position** (whole native window, same
space as `ImGui::GetIO().MousePos`), **viewport position** (the central dockspace area showing the image),
and **pixel** (image-local, origin top-left). Conversion helpers between them live on `HDRViewApp`
(`pixel_at_vp_pos`, `vp_pos_at_pixel`, `app_pos_at_pixel`, etc.) — reuse these rather than hand-rolling
transforms.

### Image model
`image.h`/`image.cpp` define `Image` (referenced everywhere as `ImagePtr = shared_ptr<Image>` /
`ConstImagePtr`, see `src/fwd.h`), representing one loaded image with its channels, metadata, and GPU
texture. `image-gui.cpp` holds the ImGui-facing rendering/inspection code for a single image (histograms,
stats), separate from `image.cpp`'s data-model logic.

### Image I/O: pluggable loader registry
`src/imageio/image_loader.h` declares `BackgroundImageLoader`, which loads images asynchronously (off the
main thread, tile-uploaded to the GPU across frames to avoid stalling), watches directories for new/changed
files, and manages recent files. Actual format decoding lives in `src/imageio/<format>.cpp`, one file per
codec (`exr.cpp`, `png.cpp`, `jpg.cpp`, `jxl.cpp`, `heif.cpp` — also handles AVIF/HEIC/AVCI/J2K/HTJ2K, all
libheif plugin codecs gated by their own `HDRVIEW_ENABLE_*` options — `uhdr.cpp` (Ultra HDR), `webp.cpp`,
`tiff.cpp`, `raw.cpp`, `dds.cpp`, `qoi.cpp`, `pfm.cpp`, `stb.cpp` for the stb_image-covered formats, plus
`exif.cpp`/`icc.cpp`/`xmp.cpp` for metadata (`psd.cpp` similarly only parses PSD metadata — color mode, ICC
profile, etc. — consumed by `stb.cpp`, since stb_image itself decodes PSD pixel data). `image_loader.cpp`
holds a `default_loaders()` table of `{name, try_load}` entries, each guarded by an `is_<format>_image()`
magic-byte sniff (not file extension) and an `#if HDRVIEW_ENABLE_<FORMAT>` compile-time guard matching the
CMake options above. To add a new format: implement `is_X_image()` + `load_X_image()` in a new
`imageio/x.{h,cpp}`, add a CMake option/target wiring, and register it in `default_loaders()`.

### Rendering backend abstraction (GL vs Metal)
`renderpass.h`, `shader.h`, and `texture.h` declare platform-agnostic interfaces (adapted from NanoGUI),
each with two mutually-exclusive implementations selected at CMake configure time:
- `*_gl.cpp` (`renderpass_gl.cpp`, `shader_gl.cpp`, `texture_gl.cpp`) — used on Linux, Windows, and
  Emscripten (OpenGL/GLES).
- `*_metal.mm` (`renderpass_metal.mm`, `shader_metal.mm`, `texture_metal.mm`) — used on macOS (GLFW+Metal).

Don't assume GL is always active — code touching these classes must work through the shared header
interface, not backend-specific details.

### Forward declarations and shared aliases
`src/fwd.h` is included pervasively and centralizes: the `linalg`-based vector/matrix aliases (`la`
namespace, `float2`/`float3`/`float4`/`int2`/etc. via `la::aliases`), ImGui/linalg interop macros
(`IM_VEC2_CLASS_EXTRA`), and the core scoped enums used across the app (`Channels_`, `Tonemap_`,
`BlendMode_`, `BackgroundMode_`, `AxisScale_`, `MouseMode_`, `Target_`). Check here first before adding a
new cross-cutting enum or type alias.

### Color/dynamic-range handling
`colorspace.h`/`colorspace.cpp` implement gamut/transfer-function conversions (used both at load time via
`ImageLoadOptions::gamut_override`/`tf_override` and at display time). `colormap.h`/`colormap.cpp` provide
false-color palettes (Viridis, Turbo, etc., listed in `HDRViewApp::m_colormaps`) for the `Tonemap_FalseColor`
mode. `dithermatrix256.*` supplies blue-noise dithering used when downconverting HDR to an SDR display or
file format.
