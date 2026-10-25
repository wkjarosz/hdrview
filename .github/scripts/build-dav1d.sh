#!/usr/bin/env bash
#
# Build a static dav1d for a macOS release, and install it into a prefix that HDRView's configure is then
# pointed at (CMAKE_PREFIX_PATH and PKG_CONFIG_PATH), where both HDRView and libheif find it.
#
# dav1d builds only with meson, which CPM cannot drive, and a Homebrew dav1d requires the macOS its bottle was
# built on. Built static and for the release's deployment target, it links into HDRView and leaves nothing to
# bundle.
#
# Usage: build-dav1d.sh <install-prefix> [deployment-target, default 11.0]
# Needs meson and ninja, and on Intel NASM for dav1d's x86 assembly.

set -euo pipefail

prefix=${1:?usage: build-dav1d.sh <install-prefix> [deployment-target]}
target=${2:-11.0}
version=1.5.4

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

git clone --quiet --depth 1 --branch "$version" https://code.videolan.org/videolan/dav1d.git "$workdir/src"
MACOSX_DEPLOYMENT_TARGET=$target meson setup "$workdir/build" "$workdir/src" \
    --prefix="$prefix" --libdir=lib --buildtype=release --default-library=static \
    -Denable_tools=false -Denable_tests=false -Denable_examples=false \
    -Dc_args="-mmacosx-version-min=$target" -Dc_link_args="-mmacosx-version-min=$target"
meson install -C "$workdir/build" --quiet
echo "Installed dav1d $version for macOS $target into $prefix"
