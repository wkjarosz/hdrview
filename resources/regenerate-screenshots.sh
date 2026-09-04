#!/usr/bin/env bash
#
# Regenerates the screenshots the README shows, by running the capture harness in
# tests/gui/test_gui_screenshots.cpp. That file registers no tests unless HDRVIEW_SCREENSHOT_DIR is set, so
# this script is the only thing that shoots anything -- an ordinary ctest run is untouched.
#
# Capture reads back the app's own OpenGL framebuffer, which needs a real GL context: run this under a
# desktop session, or under `xvfb-run -a` on a machine without one. It does not work on macOS, whose Metal
# backend hello_imgui does not install a capture function for.
#
#   ./resources/regenerate-screenshots.sh        # whatever sample images are in the tree
#
# HDRVIEW_SCREENSHOT_IMAGES is a ":"-separated list of files and/or directories, in order: the first entry
# is the image the shots are actually of, and the rest are loaded so the Images panel has something to show.
# HDRVIEW_SCREENSHOT_DIFF_IMAGE names the multi-view file whose two views the comparison shot differences.
# HDRVIEW_SCREENSHOT_PALETTE_IMAGE names the file the command-palette and editing shots sit over: those two
# want recognizable content, one being marked up with an arrow that points at something and the other
# blurring part of the frame. The annotation and blur geometry is placed as fractions of the image, tuned
# for the still life below, so pointing this elsewhere still draws them, just not around anything.
# HDRVIEW_SCREENSHOT_ZOOM_PIXEL is the "x,y" the zoomed-in shot centers on; without it the harness picks the
# strongest edge it can find, which is serviceable but rarely the most telling spot in a photograph.
# The screenshots committed to this repository were taken with:
#
#   base="$HOME/Dartmouth College Dropbox/Wojciech Jarosz/Temporary Shares/hdrview-test-images"
#   HDRVIEW_SCREENSHOT_IMAGES="$base/HDR Lightroom export/_28A7394-HDR-sRGB.jpg:$base/openexr/MultiView/Adjuster.exr:$base/openexr/MultiView/LosPadres.exr:$base/openexr/Beachball/singlepart.0001.exr" \
#   HDRVIEW_SCREENSHOT_DIFF_IMAGE="$base/openexr/MultiView/Fog.exr" \
#   HDRVIEW_SCREENSHOT_PALETTE_IMAGE="$base/self-generated/upbp large teaser.exr" \
#   HDRVIEW_SCREENSHOT_ZOOM_PIXEL="1511,2344" \
#       ./resources/regenerate-screenshots.sh
#
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${HDRVIEW_SCREENSHOT_PRESET:-linux-cpm}"
build="$root/build/$preset"
binary="$build/Release/hdrview_gui_tests"

export HDRVIEW_SCREENSHOT_DIR="${HDRVIEW_SCREENSHOT_DIR:-$root/resources}"
# A small window at 1x rather than a large one at 2x. What makes text legible in a screenshot is its size
# relative to the frame, not how many pixels it is drawn with, so this way the picture is close to the size it
# is displayed at -- neither downscaled into mush nor several megabytes.
# The scale is stated rather than left to hello_imgui's DPI detection, which would otherwise vary by monitor.
export HDRVIEW_SCREENSHOT_SIZE="${HDRVIEW_SCREENSHOT_SIZE:-1400x880}"
export HDRVIEW_SCREENSHOT_SCALE="${HDRVIEW_SCREENSHOT_SCALE:-1}"

magick="$(command -v magick || command -v convert || true)"
if [[ -z "$magick" ]]; then
    echo "error: ImageMagick ('magick' or 'convert') is needed to convert the captures to JPEG." >&2
    exit 1
fi

if [[ ! -x "$binary" ]]; then
    cmake --preset "$preset" -DHDRVIEW_BUILD_GUI_TESTS=ON
    cmake --build --preset "$preset-release" --target hdrview_gui_tests --parallel
fi

mkdir -p "$HDRVIEW_SCREENSHOT_DIR"

# The harness reports its own pass/fail; a failure here means a shot did not get taken.
"$binary"

# The capture tool only writes PNG, and a PNG of a photograph is several megabytes apiece -- these would
# become the largest thing in the repository by some margin. They are screenshots meant to be looked at in a
# browser, so they are converted. 4:4:4 chroma rather than JPEG's usual 4:2:0: the interface is full of
# single-pixel-wide glyph edges and one-pixel borders, and subsampling visibly softens them.
for png in "$HDRVIEW_SCREENSHOT_DIR"/screenshot-*.png; do
    [[ -e "$png" ]] || continue
    "$magick" "$png" -sampling-factor 1x1 -quality 92 "${png%.png}.jpg"
    rm "$png"
done

echo
echo "Screenshots written to $HDRVIEW_SCREENSHOT_DIR:"
ls -lh "$HDRVIEW_SCREENSHOT_DIR"/screenshot-*.jpg
