#! /bin/bash

# For osx you must download XCode 7 and extract/generate ./darwin_sdk/MacOSX10.11.sdk.tar.gz
# by following these instructions: https://github.com/tpoechtrager/osxcross#packaging-the-sdk
# NOTE: for darwin15 support use: https://developer.apple.com/download/more/?name=Xcode%207.3.1
# To use a different MacOSX*.*.sdk.tar.gz sdk set XCODE_SDK
# e.g. XCODE_SDK=$PWD/darwin_sdk/MacOSX10.15.sdk.tar.gz ./build_gdnative.sh

# usage: ADDON_BIN_DIR=$PWD/godot/addons/bin ./contrib/godot-videodecoder/build_gdnative.sh
# (from within your project where this is a submodule installed at ./contrib/godot-videodecoder/build_gdnative.sh/)

# The Dockerfile will run a container to compile everything:
# http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_x11.html
# http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_windows.html#cross-compiling-for-windows-from-other-operating-systems
# http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_osx.html#cross-compiling-for-macos-from-linux

DIR="$(cd $(dirname "$0") && pwd)"
ADDON_BIN_DIR=${ADDON_BIN_DIR:-$DIR/target}
# COPY can't use variables, so pre-copy the file
XCODE_SDK_FOR_COPY=./darwin_sdk/MacOSX10.11.sdk.tar.xz
XCODE_SDK="${XCODE_SDK:-$XCODE_SDK_FOR_COPY}"
JOBS=${JOBS:-4}
if [ -f /proc/cpuinfo ]; then
    JOBS=$(echo "$(cat /proc/cpuinfo  | grep processor |wc -l) - 1" |  bc -l)
elif type sysctl > /dev/null; then
    # osx logical cores
    JOBS=$(sysctl -n hw.ncpu)
else
    echo "Unable to determine how many logical cores are available."
    echo "Using JOBS=$JOBS"
fi
echo "Using JOBS=$JOBS"

#img_version="$(git describe 2>/dev/null || git rev-parse HEAD)"
# TODO : pass in img_version like https://github.com/godotengine/build-containers/blob/master/Dockerfile.osx#L1
# trusty is for linux builds
echo "XCODE_SDK=$XCODE_SDK"
if [ ! -f "$XCODE_SDK" ]; then
    ls -l "$XCODE_SDK"
    echo "Unable to find $XCODE_SDK"
    exit 1
fi
if [ ! "$XCODE_SDK" = "$XCODE_SDK_FOR_COPY" ]; then
    mkdir -p $(dirname "$XCODE_SDK_FOR_COPY")
    cp "$XCODE_SDK" "$XCODE_SDK_FOR_COPY"
fi

set -e
# ideally we'd run these at the same time but ... https://github.com/moby/moby/issues/2776
docker build ./ -f Dockerfile.ubuntu-xenial -t "godot-videodecoder-ubuntu-xenial"
docker build ./ -f Dockerfile.ubuntu-bionic -t "godot-videodecoder-ubuntu-bionic" \
    --build-arg XCODE_SDK=$XCODE_SDK
docker build ./ -f Dockerfile.osx --build-arg JOBS=$JOBS -t "godot-videodecoder-osx"
docker build ./ -f Dockerfile.x11 --build-arg JOBS=$JOBS -t "godot-videodecoder-x11"
docker build ./ -f Dockerfile.x11_32 --build-arg JOBS=$JOBS -t "godot-videodecoder-x11_32"
docker build ./ -f Dockerfile.win64 --build-arg JOBS=$JOBS -t "godot-videodecoder-win64"
docker build ./ -f Dockerfile.win32 --build-arg JOBS=$JOBS -t "godot-videodecoder-win32"

set -x
# precreate the target directory because otherwise
# docker cp will copy x11/* -> $ADDON_BIN_DIR/* instead of x11/* -> $ADDON_BIN_DIR/x11/*
mkdir -p $ADDON_BIN_DIR/

echo "extracting $ADDON_BIN_DIR/x11"
id=$(docker create godot-videodecoder-x11)
docker cp $id:/opt/target/x11 $ADDON_BIN_DIR/
docker rm -v $id

echo "extracting $ADDON_BIN_DIR/x11_32"
id=$(docker create godot-videodecoder-x11_32)
docker cp $id:/opt/target/x11_32 $ADDON_BIN_DIR/
docker rm -v $id

echo "extracting $ADDON_BIN_DIR/osx"
id=$(docker create godot-videodecoder-osx)
docker cp $id:/opt/target/osx $ADDON_BIN_DIR/
docker rm -v $id

echo "extracting $ADDON_BIN_DIR/win64"
id=$(docker create godot-videodecoder-win64)
docker cp $id:/opt/target/win64 $ADDON_BIN_DIR/
docker rm -v $id

echo "extracting $ADDON_BIN_DIR/win32"
id=$(docker create godot-videodecoder-win32)
docker cp $id:/opt/target/win32 $ADDON_BIN_DIR/
docker rm -v $id
