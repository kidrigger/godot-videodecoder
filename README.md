> [!WARNING]
> This addon is originally a GDNative/Godot 3.x addon. This was manually ported to GDExtension/Godot 4.1 out of necessity due to no good alternatives at the time of writing. More than before, this code comes with **no warranty** and **no support**. "Here be dragons", use at your own risk.

This is a fork that setups automatic CI to compile FFMPEG with libx264, enabling playback of MP4 media files.

Changes made:
  - Updated `build_containers` to 3.5 branch
  - MacOS SDK updated to 12.3 (`darwin21.4`)
  - Add support for arm64 on MacOS (M1 chips)
  - Updated Ubuntu distro from `xenial` to `bionic`
  - Enabled compilation of `libx264`
  - Detach `ffmpeg-static` from submodule, since changes are very small
  - Removed test project to save space

Run `build_gdextension_all.sh` to compile for all three platforms (details below).

This repository will not provide pre-built libraries due to x264 licensing troubles.

Original README.md below

-----

# Godot Video Decoder

GDNative Video Decoder library for [Godot Engine](https://godotengine.org),
using the [FFmpeg](https://ffmpeg.org) library for codecs.

**A GSoC 2018 Project**

This project is set up so that a game developer can build x11, windows and osx plugin libraries with a single script. The build enviroment has been dockerized for portability. Building has been tested on linux and windows 10 pro with WSL2.

The most difficult part of building the plugin libraries is extracting the macos sdk from the XCode download since it [can't be distributed directly](https://www.apple.com/legal/sla/docs/xcode.pdf).

**Releases**

Release builds should automatically be available [here](/releases/latest) on github.

**Media Support**

The current dockerized ffmpeg build supports VP9 decoding only. Support for other decoders could be added, PRs are welcome.
Patent encumbered codecs like [h264/h265](https://www.mpegla.com/wp-content/uploads/avcweb.pdf) will always be missing in the automatic builds due to copyright issues and the [cost of distributing pre-built binaries](https://jina-liu.medium.com/settle-your-questions-about-h-264-license-cost-once-and-for-all-hopefully-a058c2149256#5e65).

## Instructions to build with docker

1. Add the repository as a submodule or clone the repository somewhere and initialize submodules.

```
git submodule add https://github.com/jamie-pate/godot-videodecoder.git contrib/godot-videodecoder
git submodule update --init --recursive
```

or

```
git clone https://github.com/jamie-pate/godot-videodecoder.git godot-videodecoder
cd godot-videodecoder
git submodule update --init --recursive
```

2. Copy the `build_gdnative.sh.example` to your project and adjust the paths inside.

* `cp contrib/godot-videodecoder/build_gdnative.sh.example ./build_gdnative.sh`, vi `./build_gdnative.sh`
* `chmod +x ./build_gdnative.sh` if needed

4. [Install docker](https://docs.docker.com/get-docker/)

5. Extract MacOSX sdk from the XCode (if you are building for osx)

* For osx you must download XCode 7 and extract/generate MacOSX10.11.sdk.tar.gz and copy it to ./darwin_sdk/ by following these instructions: https://github.com/tpoechtrager/osxcross#packaging-the-sdk
  * NOTE: for darwin15 support use: https://developer.apple.com/download/more/?name=Xcode%207.3.1
* To use a different MacOSX*.*.sdk.tar.gz sdk set the XCODE_SDK environment variable. <!-- TODO: test this -->
* e.g. `XCODE_SDK=$PWD/darwin_sdk/MacOSX10.15.sdk.tar.gz ./build_gdnative.sh`

5. run `build_gdnative.sh`.. Prefix with e.g. `PLATFORM=win64` to build only for win64

Be sure to [add your user to the `docker` group](https://docs.docker.com/engine/install/linux-postinstall/) or you will have to run as `sudo` (which is bad)

TODO:

* instructions for running the test project
* Add a benchmark to the test project
* Input for additional ffmpeg flags/deps
* Re-enable additional media formats with the ability to build only a subset
* OSX build in releases via github actions
