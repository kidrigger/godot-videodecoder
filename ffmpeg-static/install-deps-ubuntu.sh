#!/bin/bash

export DEBIAN_FRONTEND=noninteractive
BOOTSTRAP_PKGS="build-essential curl tar pkg-config"
# todo: trim this list
DEV_PKGS="autoconf \
  automake \
  build-essential \
  cmake \
  frei0r-plugins-dev \
  gawk \
  libass-dev \
  libfreetype6-dev \
  libopencore-amrnb-dev \
  libopencore-amrwb-dev \
  libsdl1.2-dev \
  libspeex-dev \
  libssl-dev \
  libtheora-dev \
  libtool \
  libva-dev \
  libvdpau-dev \
  libvo-amrwbenc-dev \
  libvorbis-dev \
  libwebp-dev \
  libxcb1-dev \
  libxcb-shm0-dev \
  libxcb-xfixes0-dev \
  libxvidcore-dev \
  libx264-dev \
  pkg-config \
  texi2html \
  zlib1g-dev \
  mingw-w64-tools"

OSXCROSS_PGKS="cmake git patch clang libbz2-dev fuse libfuse-dev libxml2-dev gcc g++ zlib1g-dev libmpc-dev libmpfr-dev libgmp-dev"
#Clang 3.4+, cmake, git, patch, Python, libssl-devel (openssl)
#lzma-devel, libxml2-devel and the bash shell
#llvm-devel uuid-devel
set -x
sudo apt -y install $BOOTSTRAP_PKGS
sudo apt -y install $DEV_PKGS
sudo apt -y install $OSXCROSS_PGKS
set +x
