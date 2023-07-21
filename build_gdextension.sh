#! /bin/bash

# For osx you must download XCode 7 and extract/generate ./darwin_sdk/MacOSX10.11.sdk.tar.gz
# by following these instructions: https://github.com/tpoechtrager/osxcross#packaging-the-sdk
# NOTE: for darwin15 support use: https://developer.apple.com/download/more/?name=Xcode%207.3.1
# To use a different MacOSX*.*.sdk.tar.gz sdk set XCODE_SDK
# e.g. XCODE_SDK=$PWD/darwin_sdk/MacOSX10.15.sdk.tar.gz ./build_gdextension.sh

# usage: ADDON_BIN_DIR=$PWD/godot/addons/bin ./contrib/godot-videodecoder/build_gdextension.sh
# (from within your project where this is a submodule installed at ./contrib/godot-videodecoder/build_gdextension.sh/)

# The Dockerfile will run a container to compile everything:
# http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_x11.html
# http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_windows.html#cross-compiling-for-windows-from-other-operating-systems
# http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_osx.html#cross-compiling-for-macos-from-linux

DIR="$(cd $(dirname "$0") && pwd)"
ADDON_BIN_DIR=${ADDON_BIN_DIR:-$DIR/target}
THIRDPARTY_DIR=${THIRDPARTY_DIR:-$DIR/thirdparty}
# COPY can't use variables, so pre-copy the file
XCODE_SDK_FOR_COPY=./darwin_sdk/MacOSX12.3.sdk.tar.xz
XCODE_SDK="${XCODE_SDK:-$XCODE_SDK_FOR_COPY}"
# set FF_ENABLE=everything to make a 14MiB build with all the LGPL features
FF_ENABLE="vp8 vp9 opus vorbis"

SUBMODULES_OK=1
if ! [ -f "$DIR/godot-cpp/gdextension/gdextension_interface.h" ]; then
    echo "godot-cpp headers are missing."
    echo "    Please run the following:"
    echo ""
    echo "    git submodule update --init godot-cpp"
    echo ""
    SUBMODULES_OK=
fi

if ! [ -f "$DIR/ffmpeg-static/build.sh" ]; then
    echo "ffmpeg-static headers are missing."
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
    PLATFORM_LIST=(windows_32 windows_64 macos linux_64 linux_32)
else
    IFS=',' read -r -a PLATFORM_LIST <<< "$PLATFORMS"
fi
declare -A PLATMAP
for p in "${PLATFORM_LIST[@]}"; do
    PLATMAP[$p]=1
done

echo "Building for ${PLATFORM_LIST[@]}"

plat_windows_64=${PLATMAP['windows_64']}
plat_windows_32=${PLATMAP['windows_32']}
plat_macos=${PLATMAP['macos']}
plat_linux_64=${PLATMAP['linux_64']}
plat_linux_32=${PLATMAP['linux_32']}
plat_windows_any=${PLATMAP['windows_64']}${PLATMAP['windows_32']}
plat_linux_any=${PLATMAP['linux_64']}${PLATMAP['linux_32']}

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

if [ $plat_macos ]; then
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
if [ $plat_linux_any ]; then
    docker build ./ -f Dockerfile.ubuntu-linux -t "godot-videodecoder-ubuntu-linux"
fi

# bionic is for cross compiles, use xenial for linux
# (for ubuntu 16 compatibility even though it's outdated already)
if [ $plat_macos ]; then
    echo "building with xcode sdk"
    docker build ./ -f Dockerfile.ubuntu-macos -t "godot-videodecoder-ubuntu-macos" \
    --build-arg XCODE_SDK=$XCODE_SDK
elif [ $plat_windows_any ]; then
    echo "building without xcode sdk"
    docker build ./ -f Dockerfile.ubuntu-windows -t "godot-videodecoder-ubuntu-windows"
fi

if [ $plat_macos ]; then
    echo "Building for macOS"
    docker build ./ -f Dockerfile.macos --build-arg JOBS=$JOBS -t "godot-videodecoder-macos"
fi
if [ $plat_linux_64 ]; then
    echo "Building for Linux - 64 bits"
    docker build ./ -f Dockerfile.linux_64 --build-arg JOBS=$JOBS -t "godot-videodecoder-linux_64"
fi
if [ $plat_linux_32 ]; then
    echo "Building for Linux - 32 bits"
    docker build ./ -f Dockerfile.linux_32 --build-arg JOBS=$JOBS -t "godot-videodecoder-linux_32"
fi
if [ $plat_windows_64 ]; then
    echo "Building for Windows - 64 bits"
    docker build ./ -f Dockerfile.windows_64 --build-arg JOBS=$JOBS -t "godot-videodecoder-windows_64"
fi
if [ $plat_windows_32 ]; then
    echo "Building for Windows - 32 bits"
    docker build ./ -f Dockerfile.windows_32 --build-arg JOBS=$JOBS -t "godot-videodecoder-windows_32"
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
if [ $plat_linux_64 ]; then
    echo "extracting $ADDON_BIN_DIR/linux_64"
    id=$(docker create godot-videodecoder-linux_64)
    docker cp $id:/opt/target/linux_64 $ADDON_BIN_DIR/
    mkdir -p $THIRDPARTY_DIR/linux_64

    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/linux_64 - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_linux_64 ]; then
    echo "extracting $ADDON_BIN_DIR/linux_64"
    id=$(docker create godot-videodecoder-linux_64)
    docker cp $id:/opt/target/linux_64 $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/linux_64
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/linux_64 - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_linux_32 ]; then
    echo "extracting $ADDON_BIN_DIR/linux_32"
    id=$(docker create godot-videodecoder-linux_32)
    docker cp $id:/opt/target/linux_32 $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/linux_32
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/linux_32 - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_macos ]; then
    echo "extracting $ADDON_BIN_DIR/macos"
    id=$(docker create godot-videodecoder-macos)
    docker cp $id:/opt/target/macos $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/macos
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/macos - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_windows_64 ]; then
    echo "extracting $ADDON_BIN_DIR/windows_64"
    id=$(docker create godot-videodecoder-windows_64)
    docker cp $id:/opt/target/windows_64 $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/windows_64
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/windows_64 - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if [ $plat_windows_32 ]; then
    echo "extracting $ADDON_BIN_DIR/windows_32"
    id=$(docker create godot-videodecoder-windows_32)
    docker cp $id:/opt/target/windows_32 $ADDON_BIN_DIR/

    mkdir -p $THIRDPARTY_DIR/windows_32
    # tar because copying a symlink on windows will fail if you don't run as administrator
    docker cp -L $id:/opt/godot-videodecoder/thirdparty/windows_32 - | tar -xhC $THIRDPARTY_DIR/
    docker rm -v $id
fi

if type tree 2> /dev/null; then
    tree $THIRDPARTY_DIR -L 2 -hD
    tree $ADDON_BIN_DIR -hD
else
    find $THIRDPARTY_DIR -maxdepth 2 -print -exec ls -lh {} \;
    find $ADDON_BIN_DIR -maxdepth 1 -print -exec ls -lh {} \;
fi
