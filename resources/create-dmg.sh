#!/bin/bash

echo "Building hdrview..."

BUILD_DIR="build-dmg"

mkdir $BUILD_DIR
MACOSX_DEPLOYMENT_TARGET=10.15
cmake -B $BUILD_DIR -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 -DCMAKE_BUILD_TYPE=Release -DNANOGUI_BACKEND=Metal -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake -B $BUILD_DIR -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 -DCMAKE_BUILD_TYPE=Release -DNANOGUI_BACKEND=Metal -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build $BUILD_DIR --parallel --config Release

echo "Creating dmg..."
RESULT="HDRView.dmg"
test -f $RESULT && rm $RESULT
create-dmg --window-size 500 300 --icon-size 96 --volname "HDRView Installer" --app-drop-link 360 105 --icon HDRView.app 130 105 $RESULT $BUILD_DIR/HDRView.app

echo "Removing temporary build dir..."
rm -rf $BUILD_DIR
