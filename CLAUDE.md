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
Emscripten (web build) — always through `emcmake`, which points `CMAKE_TOOLCHAIN_FILE` at the active SDK.
The preset's own `toolchainFile` resolves only against a Homebrew-style `EMSDK`, not an `emsdk`-installed
one, so `cmake --preset emscripten` on its own is not portable:
```bash
emcmake cmake --preset emscripten
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
doctest binary described below; `HDRVIEW_BUILD_GUI_TESTS` (default `OFF`) builds the `hdrview_gui_tests`
Dear ImGui Test Engine binary, also described below.

### Tests
`HDRVIEW_BUILD_TESTS=ON` builds a `hdrview_tests` executable (doctest-based, see `tests/*.cpp`) that reuses
the main app's sources/dependencies (minus `src/hdrview.cpp`, since doctest supplies its own `main()`), and
registers with ctest via `doctest_discover_tests`:
```bash
cmake --preset macos-arm64-cpm -DHDRVIEW_BUILD_TESTS=ON
cmake --build --preset macos-arm64-cpm-release
ctest --test-dir build/macos-arm64-cpm -C Release --output-on-failure
```
Coverage is roughly two dozen files: color math (`test_colorspace.cpp`, `test_icc_cicp.cpp`,
`test_cicp_video_range.cpp`, `test_gainmap.cpp`), the loaders and their edge cases
(`test_exr_io.cpp`/`test_png_io.cpp`/`test_tiff_io.cpp`/`test_dds_io.cpp`/`test_heif_io.cpp`/`test_qoi_io.cpp`,
`test_loader_limits.cpp`, `test_numeric_edge_cases.cpp`), metadata (`test_exif.cpp`, `test_xmp.cpp`,
`test_psd_metadata.cpp`), export round-trips (`test_export_roundtrip.cpp`), and assorted helpers. There are
also libFuzzer targets for the loaders under `tests/fuzz/`, behind `HDRVIEW_BUILD_FUZZERS` (Clang only). The
EXR/PNG tests conditionally compile in extra
cases against vendored real-world test images (OpenEXR's test suite, libpng's pngsuite/testpngs) that are
only present when CPM fetched those libraries from source — i.e. only on `-cpm`/`-universal` presets, not
`-local` ones; see the `HDRVIEW_TEST_OPENEXR_DIR`/`HDRVIEW_TEST_PNG_CONTRIB_DIR` guards in `CMakeLists.txt`
and those test files. CI builds+runs the full suite on exactly one `-cpm`/`-universal` preset per platform
(Release only), not across the whole preset matrix — see `run_tests`/`HDRVIEW_BUILD_TESTS` in
`.github/workflows/ci-{mac,linux,windows}.yml`. Treat a successful build plus `ctest` as the verification bar
for anything covered by these tests; for everything else (most GUI/interaction code), fall back to a
manual/CLI smoke check — or, increasingly, the GUI regression tests below.

#### GUI regression tests (Dear ImGui Test Engine)
`HDRVIEW_BUILD_GUI_TESTS=ON` builds `hdrview_gui_tests`, which drives a real `HDRViewApp` instance (menus,
dialogs, dockable windows, viewport widgets) via [Dear ImGui Test
Engine](https://github.com/ocornut/imgui_test_engine), registered as its own `ctest` entry (kept separate
from `hdrview_tests`'s `doctest_discover_tests` registration so GUI-test failures/logs are never mixed in
with the logic-level doctest suite's). Turning the option on also asks hello_imgui's own CPM package for
`HELLOIMGUI_WITH_TEST_ENGINE ON`, which transparently fetches/builds the engine itself — no separate
vendoring:
```bash
cmake --preset macos-arm64-cpm -DHDRVIEW_BUILD_GUI_TESTS=ON
cmake --build --preset macos-arm64-cpm-release --target hdrview_gui_tests
./build/macos-arm64-cpm/Release/hdrview_gui_tests   # runs the queued tests, prints a pass/fail summary, exits with the result
```
The binary takes no arguments — it always opens a real HDRView window, runs every registered test, and
exits. There is no headless mode: capture and input both go through a live GL context, so CI runs it under
`xvfb-run` rather than without a display.
Test source lives under `tests/gui/`, one file per category (mirroring Dear ImGui's own `imgui_test_suite`
convention), each exposing a `RegisterTests_X(ImGuiTestEngine*)` aggregated by
`tests/gui/test_gui_registry.h`. `src/app.h`/`src/app-windows.cpp` add a small
`HDRViewApp::enable_gui_test_engine()` opt-in hook, entirely guarded behind `HDRVIEW_ENABLE_GUI_TEST_ENGINE`
(defined only for the `hdrview_gui_tests` target) so the production `HDRView` binary and `hdrview_tests` are
untouched by it. That hook also overrides `io.ConfigRunSpeed` to `ImGuiTestRunSpeed_Fast` — Hello ImGui's own
test-engine `Setup()` hardcodes `Normal` speed ("slowest mode... in this demo"), which animates every
simulated mouse move over many real frames instead of teleporting it; without the override the suite runs
correctly but painfully slowly, and it gets slower with every test added.

Addressing quirks worth knowing before writing new tests here:
- The menu bar lives in a top-level `ImGui::BeginMainMenuBar()` window named `"##MainMenuBar"`, and each
  `EdgeToolbar` (e.g. the exposure/offset toolbar) is its own floating window with a fixed internal name
  (`"##" + EdgeToolbarTypeName(...) + "_2123243"`, baked into hello_imgui) — neither is a child of the
  `"MainDockSpace"` host window that the regular dockable windows (Images, Pixel statistics, etc.) are
  displayed inside.
- `WindowInfo()`/`GatherItems()`/`MenuClick()` all resolve a bare (non-`"//"`-prefixed) ref *relative to
  whatever `ctx->SetRef(...)` is still active*, not relative to root. Always call `ctx->SetRef("")` (or use
  an explicit `"//Window"` absolute ref) before looking up something that isn't a child of the current ref —
  otherwise the lookup silently searches the wrong scope and just doesn't find it. This has bitten every
  test file in this suite at least once; it's the single most common mistake here.
- A menu/action name containing a literal `/` (e.g. `"Pixel/color inspector"`) needs it escaped as `\/` in a
  `MenuClick()` path, or the `/` gets parsed as a submenu separator — mirrors how Dear ImGui's own demo
  addresses `"Tools/Metrics\\/Debugger"`.
- `BeginTable(...)`-hosted content (e.g. the Images-window file list) lives in a child window whose name gets
  a runtime ID-hash suffix (`"Images/ImageList_<hex>"`), not the literal table name — don't hardcode that
  path. Gather broadly (`GatherItems(&list, "//Images", -1)`) and filter the result by `.Depth` and
  `.Window->Name` substring instead (see `tests/gui/test_gui_navigation.cpp`).
- `HDRViewApp::load_images({pathA, pathB})` does not guarantee the resulting `m_images` order matches the
  request order — background loads can complete in either order. Tests that care which loaded image is
  which should look it up by filename after loading, not assume index 0/1 (see
  `find_image_index_containing()` in `tests/gui/test_gui_filtering.cpp`).
- A test's body runs on the Test Engine's own coroutine thread, and the GL context is current on the main
  thread only, so **any GL call made directly from a test is a no-op**. Calling `modify_structure()` (or
  anything else that rebuilds textures) straight from a test therefore builds them with no context:
  `glGenTextures` quietly yields a handle of 0, the channel is marked clean anyway, and the *next* edit
  throws `Texture::upload_sub_region(): not implemented for render targets!` — or, more often, nothing
  visibly fails and the shader merely logs `unbound argument "primary_0_texture..."` for a few frames.
  Driving the same edit through `MenuClick()` is safe, because the action then runs inside the main
  thread's frame, which is also how the application reaches it. Prefer the menu for anything that changes
  an image's shape; calling the chokepoints directly is fine for edits that only write samples into
  textures that already exist.

Like `hdrview_tests`' EXR/PNG doctests, `tests/gui/test_gui_multipart.cpp` conditionally builds real
assertions against a vendored multi-part OpenEXR fixture (`HDRVIEW_TEST_OPENEXR_DIR`, only set on
`-cpm`/`-universal` presets) and registers zero tests otherwise — a useful real-world-scale complement to the
simple single-layer PNG fixtures (`HDRVIEW_GUI_TEST_IMAGE`/`_2`) the rest of the suite uses. Note a multi-part
EXR loads as multiple separate `Image`s (one per part), not as multiple `ChannelGroup`s within one `Image` —
that's only how a single-part *multi-layer* EXR behaves.

`tests/gui/test_gui_screenshots.cpp` rides the same harness for a different purpose: it regenerates the
README's screenshots. It registers zero tests unless `HDRVIEW_SCREENSHOT_DIR` is set, so an ordinary `ctest`
run never sees it; `resources/regenerate-screenshots.sh` is the entry point, and `HDRVIEW_SCREENSHOT_IMAGES`
points it at subject images other than the in-tree fixtures. Capture rides hello_imgui's test-engine
integration, which installs `ImGuiApp_ImplGL_CaptureFramebuffer` as the engine's `ScreenCaptureFunc` — but
only under `#ifdef HELLOIMGUI_HAS_OPENGL`, so this is an OpenGL-backend-only facility and cannot produce
macOS screenshots. `enable_gui_test_engine()` also honors `HDRVIEW_SCREENSHOT_SIZE`/`_SCALE` so the window
geometry and UI density are stated rather than inherited.

**A screenshot must be read back from the colorpass's offscreen target, not from the window.** Hello ImGui's
capture reads the window, which is display-referred: whenever the colorpass runs, that buffer holds whatever
transfer function the display negotiated — linear light under a Wayland compositor reporting a linear
transfer, PQ on an HDR display — and a PNG holding linear light is read back as sRGB and looks markedly too
dark. So `enable_gui_test_engine()` overrides `ScreenCaptureFunc` with `capture_colorpass_framebuffer()`
(`app-windows.cpp`), which binds `HDRViewApp::capture_source()`'s FBO for the read. That target still holds
HDRView's extended sRGB — already sRGB-encoded, 1.0 at SDR white — so reading it as fixed-point clamps to
[0, 1] and quantizes, which is exactly the SDR rendition a PNG wants, with no inverse of the display
conversion needed and the app still rendering in HDR. `capture_source()` returns nullptr when no color
management is in force, and then the window's framebuffer is already the sRGB one.

The imgui_test_engine license is non-standard (not MIT) but free for OSI-licensed open-source projects like
HDRView (see `LICENSE.txt` in the fetched package). CI runs it headlessly only on Linux (`ci-linux.yml`'s
`linux-cpm` Release job, under `xvfb-run`, since GitHub's Ubuntu runners have no display server by default)
— macOS/Windows headless GUI automation is buildable but not yet exercised in CI, matching a gap even Hello
ImGui's own CI has (its `Automate.yml` explicitly skips macOS).

### Code formatting
`.clang-format` (Microsoft-based, 4-space indent, 120 col, Allman braces) governs C++ style; run
`clang-format` before committing. `.cmake-format`/`.cmake-format.json` similarly govern `CMakeLists.txt`
formatting.

### Comments
Keep comments concise and describe what the code currently does and why. A comment should read the same
whether written with the file or years later — a reader should not be able to tell that a bug was just fixed
here. Avoid changelog notes ("previously X", "this used to race"), justifying a line by contrasting it with a
version that no longer exists, and restating an investigation that belongs in the commit message. Prefer a
short note on a non-obvious constraint, invariant, or the reason a surprising approach is necessary.

Comments accumulate cruft across multi-step edits, so at the end of a multi-iteration editing session re-read
every touched file as a whole — not just the diffs — and revise for clean design and concise, logically
ordered comments.

### Spelling
Use American English: **color** not colour, **behavior** not behaviour, **normalize**/**initialize**/
**recognize** not `-ise`, **honor**, **gray**, **center**, **analyze**. This applies to comments and commit
messages as much as to identifiers — the API bakes it in (`colorspace.h`, `color_conversion_matrix()`,
`ColorGamut_`, `colormap.cpp`), so a comment spelling it the other way reads as a different codebase and, more
practically, hides from a grep for `color`.

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

libheif is the one dependency that finds its own codecs, with `find_package()` calls that only ever turn up
an installed library — which Emscripten has none of. So the web build builds libaom (AVIF) and OpenJPEG
(J2K) itself, in `CMakeLists.txt` just above the libheif block, and reaches libheif through the
`<pkg>-extra.cmake` files it writes into `CMAKE_FIND_PACKAGE_REDIRECTS_DIR` — CPM already leaves a redirect
config there for every package it adds, and the `-extra.cmake` fills in the `AOM_*`/`OPENJPEG_*` variables
that libheif's own find modules would otherwise have set. Both of those packages also write their own
preferences into the CMake cache, which is global and read at generate time, so each block undoes that
afterwards; see the comments there. `HEIC`/`AVCI` stay off in released builds for patent reasons, and
`HTJ2K` encoding is off in the web build because libheif's `FindOPENJPH.cmake` insists on an install tree.

AVIF has two AV1 decoders in play. libaom is always there — it is also the AVIF *encoder*, which dav1d
cannot be — but dav1d decodes several times faster, so `HDRVIEW_ENABLE_DAV1D` asks libheif for its dav1d
plugin too and libheif then prefers it (plugin priority 150 vs libaom's 100). Output is bit-identical;
AV1 decoding is specified exactly. dav1d builds only under meson, so unlike libaom there is no CPM
fallback: the CMake block finds a system dav1d or quietly leaves AVIF on libaom, which is what currently
happens on Windows and Emscripten. `tests/bench_heif_decode.cpp` (behind `HDRVIEW_BUILD_BENCHMARKS`) is
what measures the two against each other and checks they agree; it uses
`heif_decoding_options::decoder_id` to address a specific plugin, so one build can exercise both.

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

**Working color space and the HDR display "colorpass".** Everything HDRView draws — image content *and*
Dear ImGui's own UI, which has no notion of display color space — is emitted in **extended sRGB**: sRGB
encoding over an unbounded signed range, with `1.0` meaning the display's SDR reference white (see the tail
of `assets/shaders/image-shader.sglsl`). How that reaches the display differs by platform:
- **macOS**: Metal consumes it directly. `requestFloatBuffer` makes Hello ImGui set up a `CAMetalLayer`
  with `RGBA16Float` + `kCGColorSpaceExtendedSRGB`, which *is* this convention. No extra pass.
- **Windows/Linux (GLFW+OpenGL)**: the display may instead want scRGB linear, a power curve, or PQ, possibly
  over a wider gamut. So the whole frame is redirected into an offscreen `RGBA16F` target and a final
  full-screen "colorpass" converts it — `update_colorpass()`/`begin_colorpass_frame()`/`end_colorpass_frame()`
  in `app-colorpass.cpp`, plus `assets/shaders/colorpass.sglsl`. The two halves straddle Dear ImGui's rendering
  (`CustomBackground` … `BeforeSwap`), which is why `update_colorpass()` makes the decision once per frame
  up front rather than letting each half decide.

`display_colorspace.{h,cpp}` is the boundary: it is the **only** place `wp_color_manager_v1` protocol
integers (what the GLFW fork reports) exist, translating them once into `colorspace.h`'s `TransferFunction`
and `Chromaticities`. Everything downstream — including the shader — speaks HDRView's own vocabulary, and
the gamut conversion is just `color_conversion_matrix()` uploaded as a uniform. When adding display-side
color handling, extend that translation rather than plumbing protocol code points further in.

HDR display output requires a patched GLFW (`Tom94/glfw`, pinned in `CMakeLists.txt`); every use of its API
is guarded by `#if defined(GLFW_FLOATBUFFER)` / `#if defined(GLFW_WAYLAND_COLOR_MANAGEMENT)` and degrades to
plain SDR against stock GLFW. Keep it that way — upstreaming to glfw/glfw has no timeline.
