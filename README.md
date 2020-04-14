# Godot Video Decoder

GDNative Video Decoder library for [Godot Engine](https://godotengine.org),
using the [FFmpeg](https://ffmpeg.org) library for codecs.

**A GSoC 2018 Project**

## Instructions to use the test project

<!-- Ideally this would all be set up to build in a docker container, but I ran out of time -->
1. Add the repository as a submodule or clone the repository somewhere and initialize submodules.

```
git submodule add https://github.com/jamie-pate/godot-videodecoder.git contrib/godot-videodecoder
git submodule update --init --recursive
```

or

```
git clone https://github.com/jamie-pate/godot-videodecoder.git godot-videodecoder
cd contrib/godot-videodecoder
git submodule update --init --recursive
```

2. Add FFmpeg submodule in another folder.

```
git submodule add https://github.com/jamie-pate/ffmpeg-static.git contrib/ffmpeg-static
git submodule update --init --recursive
```

or

```
git clone https://github.com/jamie-pate/ffmpeg-static.git
```

3. Copy the `build_gdnative.sh.example` to your project and adjust the paths inside.
<!-- this step needs improvement -->

* `cp contrib/godot-videodecoder/build_gdnative.sh.example ./build_gdnative.sh`, vi `./build_gdnative.sh`
* `chmod +x ./build_gdnative.sh` if needed

4. Install dependencies

* `cd contrib/ffmpeg-static; ./install-ubuntu-deps.sh` -- This should install many required dependencies.
<!-- but might not be 100% complete -->
* make sure you are set up to cross compile godot and plugins:
* http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_x11.html
* http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_windows.html#cross-compiling-for-windows-from-other-operating-systems
* http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_osx.html#cross-compiling-for-macos-from-linux
   * NOTE: use XCode 7 for darwin15 support: https://developer.apple.com/download/more/?name=Xcode%207.3.1

5. then run `./build_gdnative.sh`

If you adjusted the paths correctly it should have created `addons/bin/(x11|win64|osx)` which should be full of libraries. You should copy `godot-videodecoder/test/addons/videodecoder.gdnlib` to your `addons` directory and make any changes you need.
