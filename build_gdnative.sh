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
THIRDPARTY_DIR=${THIRDPARTY_DIR:-$DIR/thirdparty}
# COPY can't use variables, so pre-copy the file
XCODE_SDK_FOR_COPY=./darwin_sdk/MacOSX10.11.sdk.tar.xz
XCODE_SDK="${XCODE_SDK:-$XCODE_SDK_FOR_COPY}"

SUBMODULES_OK=1
if ! [ -f "$DIR/godot_include/gdnative_api_struct.gen.h" ]; then
    echo "godot_include headers are missing."
    echo "    Please run the following:"
    echo ""
    echo "    git submodule update --init godot_include"
    echo ""
    SUBMODULES_OK=
fi

if ! [ -f "$DIR/ffmpeg-static/build.sh" ]; then
    echo "godot_include headers are missing."
    echo "    Please run the following:"
    echo ""
    echo "    git submodule update --init ffmpeg-static"
    echo ""
    SUBMODULES_OK=
fi
if ! [ $SUBMODULES_OK ]; then
    echo "Aborting due to missing submodules"
    exit 1
fi

if [ -z "$PLATFORMS" ]; then
    PLATFORM_LIST=(win64 osx x11 win32 x11_32 android)
else
    IFS=',' read -r -a PLATFORM_LIST <<< "$PLATFORMS"
fi
declare -A PLATMAP
for p in "${PLATFORM_LIST[@]}"; do
    PLATMAP[$p]=1
done

echo "Building for ${PLATFORM_LIST[@]}"

plat_win64=${PLATMAP['win64']}
plat_win32=${PLATMAP['win32']}
plat_osx=${PLATMAP['osx']}
plat_x11=${PLATMAP['x11']}
plat_x11_32=${PLATMAP['x11_32']}
plat_win_any=${PLATMAP['win64']}${PLATMAP['win32']}
plat_x11_any=${PLATMAP['x11']}${PLATMAP['x11_32']}
plat_android=${PLATMAP['android']}

if ! [ "$JOBS" ]; then
    if [ -f /proc/cpuinfo ]; then
        JOBS=$(expr $(cat /proc/cpuinfo  | grep processor | wc -l) - 1)
    elif type sysctl > /dev/null; then
        # osx logical cores
        JOBS=$(sysctl -n hw.ncpu)
    else
        JOBS=4
        echo "Unable to determine how many logical cores are available."
    fi
fi
echo "Using JOBS=$JOBS"

#img_version="$(git describe 2>/dev/null || git rev-parse HEAD)"
# TODO : pass in img_version like https://github.com/godotengine/build-containers/blob/master/Dockerfile.osx#L1

if [ $plat_osx ]; then
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
fi

set -e
# ideally we'd run these at the same time but ... https://github.com/moby/moby/issues/2776
if [ $plat_x11_any ]; then
    docker build ./ -f Dockerfile.ubuntu-xenial -t "godot-videodecoder-ubuntu-xenial"
fi

if [ $plat_android ]; then
    docker build ./ -f Dockerfile.ubuntu-xenial -t "godot-videodecoder-ubuntu-xenial"
fi

# bionic is for cross compiles, use xenial for linux
# (for ubuntu 16 compatibility even though it's outdated already)
if [ $plat_osx ]; then
    echo "building with xcode sdk"
    docker build ./ -f Dockerfile.ubuntu-bionic -t "godot-videodecoder-ubuntu-bionic" \
    --build-arg XCODE_SDK=$XCODE_SDK
elif [ $plat_win_any ]; then
    echo "building without xcode sdk"
    docker build ./ -f Dockerfile.ubuntu-bionic -t "godot-videodecoder-ubuntu-bionic"
fi

if [ $plat_osx ]; then
    echo "Building for OSX"
    docker build ./ -f Dockerfile.osx --build-arg JOBS=$JOBS -t "godot-videodecoder-osx"
fi
if [ $plat_x11 ]; then
    echo "Building for X11"
    docker build ./ -f Dockerfile.x11 --build-arg JOBS=$JOBS -t "godot-videodecoder-x11"
fi
if [ $plat_x11_32 ]; then
    docker build ./ -f Dockerfile.x11_32 --build-arg JOBS=$JOBS -t "godot-videodecoder-x11_32"
fi
if [ $plat_win64 ]; then
    echo "Building for Win64"
    docker build ./ -f Dockerfile.win64 --build-arg JOBS=$JOBS -t "godot-videodecoder-win64"
fi
if [ $plat_win32 ]; then
    docker build ./ -f Dockerfile.win32 --build-arg JOBS=$JOBS -t "godot-videodecoder-win32"
fi
if [ $plat_android ]; then
    docker build ./ -f Dockerfile.android --build-arg JOBS=$JOBS -t "godot-videodecoder-android"
fi
set -x
# precreate the target directory because otherwise
# docker cp will copy x11/* -> $ADDON_BIN_DIR/* instead of x11/* -> $ADDON_BIN_DIR/x11/*
mkdir -p $ADDON_BIN_DIR/
# copy the thirdparty dir in case you want to try building the lib against the ffmpeg libs directly e.g. in MSVC
mkdir -p $THIRDPARTY_DIR

if [ "$(uname -o)" = "Msys" ]; then
    export MSYS=winsymlinks:native
fi

# TODO: this should be a loop over all the platforms
if [ $plat_android ]; then
    echo "extracting $ADDON_BIN_DIR/android"
    id=$(docker create godot-videodecoder-android)
    docker cp $id:/opt/target/android $ADDON_BIN_DIR/
    mkdir -p $THIRDPARTY_DIR/android

    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/android - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_x11 ]; then
    echo "extracting $ADDON_BIN_DIR/x11"
    id=$(docker create godot-videodecoder-x11)
    docker cp $id:/opt/target/x11 $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/x11
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/x11 - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_x11_32 ]; then
    echo "extracting $ADDON_BIN_DIR/x11_32"
    id=$(docker create godot-videodecoder-x11_32)
    docker cp $id:/opt/target/x11_32 $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/x11_32
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/x11_32 - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_osx ]; then
    echo "extracting $ADDON_BIN_DIR/osx"
    id=$(docker create godot-videodecoder-osx)
    docker cp $id:/opt/target/osx $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/osx
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/osx - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_win64 ]; then
    echo "extracting $ADDON_BIN_DIR/win64"
    id=$(docker create godot-videodecoder-win64)
    docker cp $id:/opt/target/win64 $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/win64
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/win64 - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_win32 ]; then
    echo "extracting $ADDON_BIN_DIR/win32"
    id=$(docker create godot-videodecoder-win32)
    docker cp $id:/opt/target/win32 $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/win32
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/win32 - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if type tree 2> /dev/null; then
    tree $THIRDPARTY_DIR -L 2 -hD
    tree $ADDON_BIN_DIR -hD
else
    find $THIRDPARTY_DIR -maxdepth 2 -print -exec ls -lh {} \;
    find $ADDON_BIN_DIR -maxdepth 1 -print -exec ls -lh {} \;
fi