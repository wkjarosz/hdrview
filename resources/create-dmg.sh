#!/bin/bash

cd "$(dirname ${BASH_SOURCE[0]})"

echo "Building hdrview..."

BUILD_DIR="build-dmg"

mkdir $BUILD_DIR
MACOSX_DEPLOYMENT_TARGET=10.14
cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 -DHDRVIEW_DEPLOY=1 ../
cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 -DHDRVIEW_DEPLOY=1 ../
cmake --build $BUILD_DIR --parallel --config Release

echo "Creating dmg..."
RESULT="../HDRView.dmg"
test -f $RESULT && rm $RESULT
create-dmg --window-size 500 300 --icon-size 96 --volname "HDRView Mojave Installer" --app-drop-link 360 105 --icon HDRView.app 130 105 $RESULT $BUILD_DIR/HDRView.app

echo "Removing temporary build dir..."
rm -rf $BUILD_DIR
