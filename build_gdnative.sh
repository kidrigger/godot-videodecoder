#! /bin/bash

# For osx you must download XCode 7 and extract/generate ./thirdparty/MacOSX10.11.sdk.tar.gz
# by following these instructions: https://github.com/tpoechtrager/osxcross#packaging-the-sdk
# NOTE: for darwin15 support use: https://developer.apple.com/download/more/?name=Xcode%207.3.1
# To use a different MacOSX*.*.sdk.tar.gz sdk set XCODE_SDK
# e.g. XCODE_SDK=$PWD/thirdparty/MacOSX10.15.sdk.tar.gz ./build_gdnative.sh

# usage: ADDON_BIN_DIR=$PWD/godot/addons/bin ./contrib/godot-videodecoder/build_gdnative.sh
# (from within your project where this is a submodule installed at ./contrib/godot-videodecoder/build_gdnative.sh/)

# The Dockerfile will run a container to compile everything:
# http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_x11.html
# http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_windows.html#cross-compiling-for-windows-from-other-operating-systems
# http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_osx.html#cross-compiling-for-macos-from-linux


# cd ~/src/osxcross; ./build.sh && ./build_gcc.sh
# https://docs.godotengine.org/en/3.2/tutorials/plugins/gdnative/gdnative-c-example.html

DIR="$(cd $(dirname "$0") && pwd)"
ADDON_BIN_DIR=${ADDON_BIN_DIR:-$DIR/target}
# COPY can't use variables, so pre-copy the file
XCODE_SDK_FOR_COPY=./darwin_sdk/MacOSX10.11.sdk.tar.xz
XCODE_SDK="${XCODE_SDK:-$XCODE_SDK_FOR_COPY}"
JOBS=$(echo "$(cat /proc/cpuinfo  | grep processor |wc -l) - 1" |  bc -l)
#img_version="$(git describe 2>/dev/null || git rev-parse HEAD)"
# TODO : pass in img_version like https://github.com/godotengine/build-containers/blob/master/Dockerfile.osx#L1
# trusty is for linux builds
echo "XCODE_SDK=$XCODE_SDK"
if [ ! -f "$XCODE_SDK" ]; then
    ls -l "$XCODE_SDK"
    echo "Unable to find $XCODE_SDK"
    exit 1
fi
if [ "$XCODE_SDK" -ne "$XCODE_SDK_FOR_COPY" ]; then
    mkdir -p $(dirname "$XCODE_SDK_FOR_COPY")
    cp "$XCODE_SDK" "$XCODE_SDK_FOR_COPY"
fi

set -e
docker build ./ -f Dockerfile.ubuntu-xenial -t "godot-videodecoder-ubuntu-xenial"
docker build ./ -f Dockerfile.ubuntu-bionic -t "godot-videodecoder-ubuntu-bionic" \
    --build-arg XCODE_SDK=$XCODE_SDK

# bionic is for cross compiles, use xenial for linux
# (for ubuntu 16 compatibility even though it's outdated already)
docker build ./ -f Dockerfile.osx --build-arg JOBS=$JOBS -t "godot-videodecoder-osx"
docker build ./ -f Dockerfile.x11 --build-arg JOBS=$JOBS -t "godot-videodecoder-x11"
docker build ./ -f Dockerfile.win64 --build-arg JOBS=$JOBS -t "godot-videodecoder-win64"

echo "extracting $ADDON_BIN_DIR/x11"
id=$(docker create godot-videodecoder-x11)
docker cp $id:/opt/target/x11 $ADDON_BIN_DIR/
docker rm -v $id

echo "extracting $ADDON_BIN_DIR/osx"
id=$(docker create godot-videodecoder-osx)
docker cp $id:/opt/target/osx $ADDON_BIN_DIR/
docker rm -v $id

id=$(docker create godot-videodecoder-win64)
docker cp $id:/opt/target/win64 $ADDON_BIN_DIR/
docker rm -v $id

exit 0
# TODO: remove below
# TODO: make this more portable?
OSXCROSS_BIN_DIR=$HOME/src/osxcross/target/bin

dist="$(lsb_release -is)"
JOBS="$(echo "$(cat /proc/cpuinfo  | grep processor |wc -l) - 1" |  bc -l)"
if [[ "$dist" == "Ubuntu" ]]; then
    echo "you may need to run contrib/ffmpeg-static/install-deps-ubuntu.sh"
fi

on_error() {
    caller
    echo $@
    exit $1
}

trap 'on_error' ERR
if [[ "$1" != "--no-libs" ]]; then
    if [ ! -z "$ADDON_BIN_DIR" ] && [ ! -z "$PLUGIN_BIN_DIR" ]; then
        rm -rf $ADDON_BIN_DIR/* $PLUGIN_BIN_DIR
    fi
    pushd contrib/ffmpeg-static
        set -x
        ./build.sh -B -T "$TARGET_DIR/x11" -j $JOBS
        ./build.sh -B -p windows -T "$TARGET_DIR/win64" -j $JOBS
        ./build.sh -B -p darwin -T "$TARGET_DIR/osx" -j $JOBS
        set +x
    popd
fi
pushd contrib/godot-videodecoder
    set -x
    scons platform=x11 prefix="$ADDON_BIN_DIR"
    scons platform=windows prefix="$ADDON_BIN_DIR"
    # NOTE: -j doesn't work with this platform
    scons platform=osx toolchainbin=$OSXCROSS_BIN_DIR prefix="$ADDON_BIN_DIR"
    set +x
popd
