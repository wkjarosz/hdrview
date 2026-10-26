#!/usr/bin/env bash
#
# Smoke-check a packaged macOS DMG: verify that everything in it runs on the macOS its Info.plist claims, then
# verify the app actually starts and loads its shaders.
#
# A build runs on its deployment target only if every binary it links was built for that target too. Homebrew's
# bottles are built for the macOS of the machine that built them, so one bundled into the app (by dylibbundler,
# from a runner's Homebrew) makes the whole app require that macOS, which dyld enforces with a refusal to load.
# The build-tree --help check on a newer runner sees none of this.
#
# Usage: check-macos-dmg.sh <path-to-.dmg-or-.app>
# Needs a GUI session for the launch check, which GitHub's macOS runners have, and no build tree at the path the
# app was built in: a binary looks for assets there first, which would hide any missing from the bundle.

set -euo pipefail

target=${1:?usage: check-macos-dmg.sh <path-to-.dmg-or-.app>}
[[ -e $target ]] || { echo "error: no such file: $target" >&2; exit 1; }

workdir=$(mktemp -d)
mnt=$workdir/mnt
trap 'hdiutil detach -quiet "$mnt" 2>/dev/null || true; rm -rf "$workdir"' EXIT

if [[ -d $target ]]; then
    app=$target
else
    # the DMG carries a license agreement, which attach waits on until something accepts it
    echo "Mounting $target"
    PAGER=cat hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$target" < <(yes) >/dev/null 2>&1
    app=$(find "$mnt" -maxdepth 1 -name '*.app' | head -1)
    [[ -n $app ]] || { echo "error: no .app in $target" >&2; exit 1; }
fi

floor=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$app/Contents/Info.plist" 2>/dev/null || true)
[[ -n $floor ]] || { echo "error: $app/Contents/Info.plist declares no LSMinimumSystemVersion" >&2; exit 1; }
echo "Info.plist claims macOS $floor"

# A binary's minimum is the minos of its LC_BUILD_VERSION, or the version of an older LC_VERSION_MIN_MACOSX,
# once per architecture of a universal file. Its dependencies must come from the OS or from inside the bundle.
bad=0
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' || continue
    rel=${f#"$app"/}
    while read -r minos; do
        if [[ $(printf '%s\n' "$floor" "$minos" | sort -V | tail -1) != "$floor" ]]; then
            echo "error: $rel requires macOS $minos" >&2
            bad=1
        fi
    done < <(otool -arch all -l "$f" | awk '/LC_BUILD_VERSION|LC_VERSION_MIN_MACOSX/ { want = 1 }
                                         want && ($1 == "minos" || $1 == "version") { print $2; want = 0 }' | sort -u)
    while read -r dep; do
        case $dep in
            /usr/lib/* | /System/* | @executable_path/* | @loader_path/* | @rpath/*) ;;
            *) echo "error: $rel links $dep, from outside the bundle" >&2; bad=1 ;;
        esac
    done < <(otool -arch all -L "$f" | awk 'NR > 1 && $1 !~ /:$/ { print $1 }' | sort -u)
done < <(find "$app" -type f -print0)
((bad == 0)) || exit 1
echo "Every binary in the bundle runs on macOS $floor and links only the OS and the bundle"

exe=$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Contents/Info.plist")
"$exe" --help >/dev/null

# Launch it for real. HDRView has no run-and-exit mode, so let it come up and then kill it; what matters is
# what it logged on the way, not its exit status. Poll rather than waiting out the timeout: a healthy start
# takes a couple of seconds, and the timeout only bounds a hung one.
log=$workdir/run.log
echo "Launching $exe"
"$exe" >"$log" 2>&1 &
pid=$!
for _ in $(seq 60); do
    grep -q 'Loading shader from' "$log" && break
    kill -0 "$pid" 2>/dev/null || break
    sleep 1
done
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true

if grep -qE 'Shader initialization failed|Could not find a shader' "$log"; then
    echo "error: the app failed to initialize its shaders" >&2
    cat "$log" >&2
    exit 1
fi
if ! grep -q 'Loading shader from' "$log"; then
    echo "error: the app never got as far as loading a shader" >&2
    cat "$log" >&2
    exit 1
fi
if grep 'Loading shader from' "$log" | grep -qv '\.app/Contents/'; then
    echo "error: the app loaded a shader from outside its bundle" >&2
    cat "$log" >&2
    exit 1
fi

echo "The app started and loaded its shaders:"
sed -n '/Loading shader from/p' "$log"
