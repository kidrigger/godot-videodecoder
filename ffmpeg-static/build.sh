#!/bin/bash

set -e
set -u

jflag=
jval=2
rebuild=0
download_only=0
no_build_deps=0
with_symbols=0
final_target_dir=
cross_platform=
platform=linux

FF_ENABLE="${FF_ENABLE:-"vp8 vp9 opus vorbis h264"}"

uname -mpi | grep -qE 'x86|i386|i686' && is_x86=1 || is_x86=0

rm_symver() {
  # hack to remove extra symver shared libs.
  # We don't need versioning since we're linking to local files anyways.
  case $cross_platform in
    'win32')
    ;&
    'windows')
    echo "Windows doesn't support SLIBNAME"
    ;;
    *)
    echo "Removing SLIBNAME"
    slibname_with_major="$(grep SLIBNAME_WITH_MAJOR= $1 | sed 's/SLIBNAME_WITH_MAJOR=//')"
    sed -i "s/SLIBNAME_WITH_VERSION=.*/SLIBNAME_WITH_VERSION=$slibname_with_major/" $1
    ;;
  esac
  sed -i 's/SLIB_INSTALL_NAME=.*/SLIB_INSTALL_NAME=$(SLIBNAME_WITH_MAJOR)/' $1
  sed -i 's/SLIB_INSTALL_LINKS=.*/SLIB_INSTALL_LINKS=$(SLIBNAME)/' $1
}

while getopts 'j:T:p:BdDs' OPTION
do
  case $OPTION in
  j)
      jflag=1
      jval="$OPTARG"
      ;;
  B)
      rebuild=1
      ;;
  d)
      download_only=1
      ;;
  D)
      no_build_deps=1
      ;;
  s)
      with_symbols=1
      ;;
  T)
      final_target_dir="$OPTARG"
      ;;
  p)
      cross_platform="$OPTARG"
      ;;
  ?)
      printf "Usage: %s: [-j concurrency_level] [-B] [-d] [-D] [-s] [-T /path/to/final/target] [-p platform]\n" $(basename $0) >&2
      echo " -j: concurrency level (number of cores on your pc +- 20%)"
      echo " -D: skip building dependencies" >&2
      echo " -d: download only" >&2
      echo " -s: Build with debug and symbols" >&2
      echo " -B: force reconfigure and rebuild" >&2 # not sure this makes a difference
      echo " -T: set final target for installing ffmpeg libs" >&2
      echo " -p: set cross compile platform (windows|win32|darwin|x11_32)" >&2
      exit 2
      ;;
  esac
done
shift $(($OPTIND - 1))

if [ "$jflag" ]
then
  if [ "$jval" ]
  then
    printf "Option -j specified (%d)\n" $jval
  fi
fi

[ "$rebuild" -eq 1 ] && echo "Reconfiguring existing packages..."
[ $is_x86 -ne 1 ] && echo "Not using yasm or nasm on non-x86 platform..."

debug_flags='--disable-debug'
if [ $with_symbols -eq 1 ]; then
  debug_flags='--enable-debug --disable-stripping'
fi

cd `dirname $0`
ENV_ROOT=`pwd`
. ./env.source
FINAL_TARGET_DIR=${final_target_dir:-$TARGET_DIR}

# check operating system
OS=`uname`
platform="unknown"

case $OS in
  'Darwin')
    platform='darwin'
    ;;
  'Linux')
    platform='linux'
    ;;
esac

# default flags for non-cross  platform build
cross_platform_flags="--disable-vaapi --disable-vdpau"
cc_flags=
cc_cflags=
cc_ldflags=
cc_triplet=
cc_extra_libs=
cc_lib_prefix=
cc_dep_lib_extra=
cc_cross_env=
cc_pkg_config_path=
if [ ! -z "$cross_platform" ]; then
  case $cross_platform in
    'win32')
      ;&
    'windows')
      platform=windows
      arch=x86_64
      if [ $cross_platform = "win32" ]; then
        arch=i686
      fi
      cc_triplet=$arch-w64-mingw32
      # for --enable-opencl we need to get defs from dlltool?
      # https://stackoverflow.com/questions/15185955/compile-opencl-on-mingw-nvidia-sdk
      accel_opts="--enable-d3d11va --enable-dxva2"
      cross_platform_flags="$accel_opts --arch=$arch --target-os=mingw32 --cross-prefix=$cc_triplet-"
      cc_lib_prefix="-static"
      cc_extra_libs="-lole32"
      ;;
    'x11_32')
      ;&
    'x11')
      arch=x86_64
      cc_triplet=$arch-pc-linux-gnu
      bits=64P
      if [ $cross_platform = "x11_32" ]; then
        arch=x86_32
        bits=32
        cc_triplet=i686-pc-linux-gnu
      fi
      cc_pkg_config_path=":/"
      cc_flags="CFLAGS=-m$bits LDFLAGS=-m$bits"
      cc_cflags="-m$bits"
      cc_ldflags="-m$bits"
      # vaapi and vdpau don't show a significant increase in performance
      # and cause portability issues, so only enable on linux
      # --enable-opencl does not show a significant peformance benefit, so skip it
      cross_platform_flags="--disable-vaapi --disable-vdpau --arch=$arch"
      ;;
    'darwin')
      platform=darwin
      d_sdk=darwin21.4
      cc_triplet=x86_64-apple-$d_sdk
      # 19 is catalina
      cc_cross_env=$cc_triplet-
      cc_platform=x86_64-$d_sdk-gcc #x86_64-apple-$d_sdk-clang
      cc_dep_lib_extra="LDFLAGS=-lm"
      accel_opts="--enable-opencl"
      cross_platform_flags="$accel_opts --arch=x86_64 --target-os=$platform --cross-prefix=$cc_triplet- --install-name-dir=@loader_path"
      if [ -z "$OSXCROSS_BIN_DIR" ] || [ ! -d "$OSXCROSS_BIN_DIR" ]; then
        echo "Unable to find osxcross bin directory [$OSXCROSS_BIN_DIR]: $(ls -l OSXCROSS_BIN_DIR)"
        exit 1
      fi
      PATH=$OSXCROSS_BIN_DIR:$PATH
      export OSXCROSS_PKG_CONFIG_USE_NATIVE_VARIABLES=1
      ;;
    '')
    ;;
    *)
    echo "Invalid platform $cross_platform"
    exit 1
    ;;
    esac
fi
disable_everything=--disable-everything
declare -a enable_features

# hwaccels
# h263_vaapi               h264_videotoolbox        mjpeg_nvdec              mpeg2_dxva2              mpeg4_vdpau              vc1_vdpau                vp9_vaapi
# h263_videotoolbox        hevc_d3d11va             mjpeg_vaapi              mpeg2_nvdec              mpeg4_videotoolbox       vp8_nvdec                wmv3_d3d11va
# h264_d3d11va             hevc_d3d11va2            mpeg1_nvdec              mpeg2_vaapi              vc1_d3d11va              vp8_vaapi                wmv3_d3d11va2
# h264_d3d11va2            hevc_dxva2               mpeg1_vdpau              mpeg2_vdpau              vc1_d3d11va2             vp9_d3d11va              wmv3_dxva2
# h264_dxva2               hevc_nvdec               mpeg1_videotoolbox       mpeg2_videotoolbox       vc1_dxva2                vp9_d3d11va2             wmv3_nvdec
# h264_nvdec               hevc_vaapi               mpeg1_xvmc               mpeg2_xvmc               vc1_nvdec                vp9_dxva2                wmv3_vaapi
# h264_vaapi               hevc_vdpau               mpeg2_d3d11va            mpeg4_nvdec              vc1_vaapi                vp9_nvdec                wmv3_vdpau
# h264_vdpau               hevc_videotoolbox        mpeg2_d3d11va2           mpeg4_vaapi

for decoder in $FF_ENABLE; do
  case $decoder in
    'everything')
      disable_everything=
      ;;
    'vp8')
      enable_features+=(--enable-decoder=vp8 --enable-parser=vp8)
      case $platform in
        'windows')
          enable_features+=(--enable-hwaccel=vp8_d3d11va2 --enable-hwaccel=vp8_d3d11va --enable-hwaccel=vp8_dxva2)
          ;;
        # add other platform's hwaccels here
      esac
    ;;
    'vp9')
      enable_features+=(--enable-decoder=vp9 --enable-parser=vp9)
      case $platform in
        'windows')
          enable_features+=(--enable-hwaccel=vp9_d3d11va2 --enable-hwaccel=vp9_d3d11va --enable-hwaccel=vp9_dxva2)
        ;;
      esac
    ;;
    'vorbis')
      enable_features+=(--enable-libvorbis --enable-demuxer=vorbis --enable-parser=vorbis --enable-decoder=vorbis --enable-decoder=libvorbis)
    ;;
    'opus')
      enable_features+=(--enable-libopus --enable-demuxer=opus --enable-parser=opus --enable-decoder=opus --enable-decoder=libopus)
    ;;
    'h264')
      enable_features+=(--enable-libx264 --enable-decoder=mpeg4 --enable-decoder=aac --enable-demuxer=mov --enable-demuxer=aac --enable-demuxer=m4v --enable-parser=aac --enable-parser=mpeg4video --enable-decoder=h264 --enable-decoder=libx264)
    ;;
    # add more decoders here
  esac
  if [ "$decoder" == "vp8" ] || [ "$decoder" == "vp9" ]; then
    enable_features+=(--enable-demuxer=matroska)
  fi
done

last_platform="$(cat $ENV_ROOT/.config-platform || true)"
if [ "$platform" != "$last_platform" ] && [ "$rebuild" -ne 1 ]; then
  rebuild=1
  echo "platform changed from $last_platform to $platform. Forcing a rebuild"
fi
echo "$platform" > $ENV_ROOT/.config-platform

#if you want a rebuild
#rm -rf "$BUILD_DIR" "$TARGET_DIR"
mkdir -p "$BUILD_DIR" "$TARGET_DIR" "$DOWNLOAD_DIR" "$BIN_DIR"

#download and extract package
download(){
  filename="$1"
  if [ ! -z "$2" ];then
    filename="$2"
  fi
  ../download.pl "$DOWNLOAD_DIR" "$1" "$filename" "$3" "$4"
  #disable uncompress
  REPLACE="$rebuild" CACHE_DIR="$DOWNLOAD_DIR" ../fetchurl "http://cache/$filename"
}

echo "#### FFmpeg static build ####"

#this is our working directory
cd $BUILD_DIR

[ $is_x86 -eq 1 ] && download \
  "yasm-1.3.0.tar.gz" \
  "" \
  "fc9e586751ff789b34b1f21d572d96af" \
  "http://www.tortall.net/projects/yasm/releases/"

[ $is_x86 -eq 1 ] && download \
  "nasm-2.13.01.tar.gz" \
  "" \
  "16050aa29bc0358989ef751d12b04ed2" \
  "http://www.nasm.us/pub/nasm/releasebuilds/2.13.01/"

#hack to fix https://bugzilla.nasm.us/show_bug.cgi?id=3392461
sed 's/void pure_func/void/g' -i ./nasm-2.13.01/include/nasm.h
sed 's/void pure_func/void/g' -i ./nasm-2.13.01/include/nasmlib.h
sed 's/void pure_func/void/g' -i ./yasm-1.3.0/modules/preprocs/nasm/nasmlib.h

download \
  "v1.2.11.tar.gz" \
  "zlib-1.2.11.tar.gz" \
  "0095d2d2d1f3442ce1318336637b695f" \
  "https://github.com/madler/zlib/archive/"

download \
  "opus-1.1.2.tar.gz" \
  "" \
  "1f08a661bc72930187893a07f3741a91" \
  "https://github.com/xiph/opus/releases/download/v1.1.2"

download \
  "rtmpdump-2.3.tgz" \
  "" \
  "eb961f31cd55f0acf5aad1a7b900ef59" \
  "https://rtmpdump.mplayerhq.hu/download/"

download \
  "release-0.98b.tar.gz" \
  "vid.stab-release-0.98b.tar.gz" \
  "299b2f4ccd1b94c274f6d94ed4f1c5b8" \
  "https://github.com/georgmartius/vid.stab/archive/"

download \
  "release-2.7.4.tar.gz" \
  "zimg-release-2.7.4.tar.gz" \
  "1757dcc11590ef3b5a56c701fd286345" \
  "https://github.com/sekrit-twc/zimg/archive/"

download \
  "v2.1.2.tar.gz" \
  "openjpeg-2.1.2.tar.gz" \
  "40a7bfdcc66280b3c1402a0eb1a27624" \
  "https://github.com/uclouvain/openjpeg/archive/"

download \
  "v1.3.3.tar.gz" \
  "ogg-1.3.3.tar.gz" \
  "b8da1fe5ed84964834d40855ba7b93c2" \
  "https://github.com/xiph/ogg/archive/"

download \
  "v1.3.6.tar.gz" \
  "vorbis-1.3.6.tar.gz" \
  "03e967efb961f65a313459c5d0f4cbfb" \
  "https://github.com/xiph/vorbis/archive/"

download \
  "x264-f7074e12d90de71f22aebd5040b8c6d31ca8f926.tar.bz2" \
  "" \
  "b7757d39c509423a4d569811f0c0e226" \
  "https://code.videolan.org/videolan/x264/-/archive/f7074e12d90de71f22aebd5040b8c6d31ca8f926/"

download \
  "n4.0.tar.gz" \
  "ffmpeg4.0.tar.gz" \
  "4749a5e56f31e7ccebd3f9924972220f" \
  "https://github.com/FFmpeg/FFmpeg/archive"

[ $download_only -eq 1 ] && exit 0

if [ ! -z "$cc_triplet" ]; then
  cc_flags="$cc_flags --host=$cc_triplet"
fi

TARGET_DIR_SED=$(echo $TARGET_DIR | awk '{gsub(/\//, "\\/"); print}')
if [ $no_build_deps -eq 1 ]; then
  echo "Skipping dependencies"
else

if [ $is_x86 -eq 1 ]; then
  echo "*** Building yasm ***"
  cd $BUILD_DIR/yasm*
  [ $rebuild -eq 1 -a -f Makefile ] && make distclean || true
  [ ! -f config.status ] && ./configure --prefix=$TARGET_DIR --bindir=$BIN_DIR $cc_flags
  make -j $jval
  make install

  echo "*** Building nasm ***"
  cd $BUILD_DIR/nasm*
  [ $rebuild -eq 1 -a -f Makefile ] && make distclean || true
  [ ! -f config.status ] && ./configure --prefix=$TARGET_DIR --bindir=$BIN_DIR $cc_flags
  make -j $jval
  make install
fi

echo "*** Building opus ***"
cd $BUILD_DIR/opus*
[ $rebuild -eq 1 -a -f Makefile ] && make distclean || true
[ ! -f config.status ] && ./configure --prefix=$TARGET_DIR --disable-shared --with-pic \
  --enable-intrinsics --disable-extra-programs \
  $cc_flags $cc_dep_lib_extra
make
make install

echo "*** Building libogg ***"
cd $BUILD_DIR/ogg*
[ $rebuild -eq 1 -a -f Makefile ] && make distclean || true
./autogen.sh
./configure --prefix=$TARGET_DIR --disable-shared --with-pic $cc_flags
make -j $jval
make install

echo "*** Building libvorbis ***"
cd $BUILD_DIR/vorbis*
[ $rebuild -eq 1 -a -f Makefile ] && make distclean || true
./autogen.sh
./configure --prefix=$TARGET_DIR --disable-shared --with-pic $cc_flags \
  --disable-oggtest --disable-examples --disable-docs $cc_dep_lib_extra
echo $PATH
make -j $jval
make install

echo "*** Building libx264 ***"
cd $BUILD_DIR/x264*
[ $rebuild -eq 1 -a -f Makefile ] && make distclean || true
if [ ! -z "$cc_triplet" ] && [ ! $cross_platform = "x11_32" ]; then
  ./configure --prefix=$TARGET_DIR --bindir="$BIN_DIR" --enable-pic --enable-static --extra-cflags="-I$TARGET_DIR/include $cc_cflags" --extra-ldflags="-L$TARGET_DIR/lib $cc_ldflags" --cross-prefix=$cc_triplet- --host=$cc_triplet
elif [ $is_x86 -eq 1 ]; then
  ./configure --prefix=$TARGET_DIR --bindir="$BIN_DIR" --enable-pic --enable-static --extra-cflags="-I$TARGET_DIR/include $cc_cflags" --extra-ldflags="-L$TARGET_DIR/lib $cc_ldflags" --host=$cc_triplet
fi
make -j $jval
make install-lib-static

fi

# FFMpeg
echo "*** Building FFmpeg ***"
cd $BUILD_DIR/FFmpeg*
[ $rebuild -eq 1 -a -f Makefile ] && make distclean || true

[ ! -f config.status ] && PATH="$BIN_DIR:$PATH" \

EXTRA_LIBS="$cc_lib_prefix -lpthread -lm $cc_extra_libs" # -lz
export PKG_CONFIG_PATH="$TARGET_DIR/lib/pkgconfig${cc_pkg_config_path}"


./configure \
  --prefix="$FINAL_TARGET_DIR" \
  --pkg-config-flags="--static" \
  --extra-cflags="-I$TARGET_DIR/include $cc_cflags" \
  --extra-ldflags="-L$TARGET_DIR/lib $cc_ldflags" \
  --extra-libs="$EXTRA_LIBS" \
  --bindir="$BIN_DIR" \
  \
  $disable_everything \
  $debug_flags \
  --enable-gpl --disable-programs \
  --enable-shared --disable-static \
  --enable-opengl \
  ${enable_features[@]} \
  $cross_platform_flags
#  --enable-libmfx \

PATH="$BIN_DIR:$PATH" make -j $jval
rm_symver $PWD/ffbuild/config.mak
make install
make distclean

file $FINAL_TARGET_DIR/lib/*
hash -r
echo "Installed to $FINAL_TARGET_DIR"
