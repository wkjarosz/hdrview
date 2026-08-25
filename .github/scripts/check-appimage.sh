#!/usr/bin/env bash
#
# Smoke-check a packaged AppImage: verify it carries the compiled runtime assets, then verify it actually
# starts and loads its shaders.
#
# The "Checking that HDRView runs" step can't catch either problem: it runs the build-tree binary, which
# finds its assets through HDRVIEW_BUILD_TREE_ASSETS_DIR rather than the installed layout, and --help exits
# long before any shader is loaded. An AppImage that shipped the uncompiled .sglsl shader sources instead of
# the sokol-shdc output therefore passed CI and failed on every user's machine (issue #183).
#
# Usage: check-appimage.sh <path-to-.appimage>
# Needs a display for the launch check; run it under xvfb-run on a headless machine.

set -euo pipefail

appimage=$(readlink -f "${1:?usage: check-appimage.sh <path-to-.appimage>}")
[[ -f $appimage ]] || { echo "error: no such file: $appimage" >&2; exit 1; }

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

# --appimage-extract unpacks without needing FUSE, which GitHub runners don't provide.
echo "Extracting $appimage"
(cd "$workdir" && "$appimage" --appimage-extract >/dev/null)
appdir=$workdir/squashfs-root

# The four shaders the Linux/OpenGL build generates with sokol-shdc; see sokol_shdc_generate() in
# CMakeLists.txt. Their names are what Shader::from_asset() asks for at runtime, plus the .glsl extension.
shader_dir=$appdir/usr/bin/assets/shaders
missing=()
for shader in image-shader_vert.glsl image-shader_frag.glsl colorpass_vert.glsl colorpass_frag.glsl; do
    [[ -f $shader_dir/$shader ]] || missing+=("$shader")
done
if ((${#missing[@]})); then
    echo "error: AppImage is missing compiled shader(s): ${missing[*]}" >&2
    echo "$shader_dir contains:" >&2
    if [[ -d $shader_dir ]]; then ls -la "$shader_dir" >&2; else echo "    (no such directory)" >&2; fi
    exit 1
fi
echo "Compiled shaders present in $shader_dir"

# Launch it for real. HDRView has no run-and-exit mode, so let it come up and then kill it; what matters is
# what it logged on the way, not its exit status. Poll rather than waiting out the timeout: a healthy start
# takes a couple of seconds, and the timeout only bounds a hung one.
log=$workdir/run.log
echo "Launching the AppImage"
"$appdir/AppRun" >"$log" 2>&1 &
pid=$!
for _ in $(seq 60); do
    grep -q 'Loading shader from' "$log" && break
    kill -0 "$pid" 2>/dev/null || break
    sleep 1
done
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true

if grep -qE 'Shader initialization failed|Could not find a shader' "$log"; then
    echo "error: the AppImage failed to initialize its shaders" >&2
    cat "$log" >&2
    exit 1
fi
if ! grep -q 'Loading shader from' "$log"; then
    echo "error: the AppImage never got as far as loading a shader" >&2
    cat "$log" >&2
    exit 1
fi

echo "AppImage started and loaded its shaders:"
sed -n '/Loading shader from/p' "$log"
