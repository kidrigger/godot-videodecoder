Build FFmpeg Static Libs for [Godot Videodecoder](https://github.com/jamie-pate/godot-videodecoder.git)
===================

Three scripts to compile and cross compile a static build of ffmpeg shared libraries with **vp9, opus and vorbis** decoders. This specific fork is for use with a specific fork of [godot-videodecoder](https://github.com/jamie-pate/godot-videodecoder.git)

Supported Platforms
-------------------

Currently this fork supports *compiling* `linux` shared libraries and *cross compiling* `win64` and `osx` shared libraries (from linux).

Build dependencies
------------------

```
# Debian & Ubuntu
# Try running `intall-deps-ubuntu.sh` to install most deps to compile and cross compile.
$ apt-get install build-essential curl tar libass-dev libtheora-dev libvorbis-dev libtool cmake automake autoconf
```

* make sure you are set up to cross compile godot and plugins:
* http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_x11.html
* http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_windows.html#cross-compiling-for-windows-from-other-operating-systems
* http://docs.godotengine.org/en/3.2/development/compiling/compiling_for_osx.html#cross-compiling-for-macos-from-linux
    * NOTE: use XCode 7 for darwin15 support: https://developer.apple.com/download/more/?name=Xcode%207.3.1
    * If you placed `osxcross` in a directory other than `$HOME/src/osxcross` then you should update `env.sources`

Build & "install"
-----------------

    $ build.sh: [-j concurrency_level] [-B] [-d] [-D] [-T /path/to/final/target] [-p platform]
    # -j: concurrency level (number of cores on your pc +- 20%)
    # -D: skip building dependencies
    # -d: download only
    # -B: force reconfigure and rebuild
    # -T: set final target for installing ffmpeg libs
    # -p: set cross compile platform (windows|darwin)
    # ... wait ...
    # binaries can be found in ./target/bin/ (or /path/to/final/target specified with -T)

If you have built ffmpeg before with `build.sh`, the default behaviour is to keep the previous configuration. If you would like to reconfigure and rebuild all packages, use the `-B` flag. `-d` flag will only download and unpack the dependencies but not build. `-D` flag will skip building dependency libs.

Debug
-----

On the top-level of the project, run:

    $ . env.source

You can then enter the source folders and make the compilation yourself

    $ cd build/ffmpeg-*
    $ ./configure --prefix=$TARGET_DIR #...
    # ...

Community, bugs and reports
---------------------------

This repository is community-supported. If you make a useful PR then you will
be added as a contributor to the repo. All changes are assumed to be licensed
under the same license as the project (ISC).

As a contributor you can do whatever you want. Help maintain the scripts,
upgrade dependencies and merge other people's PRs. Just be responsible and
make an issue if you want to introduce bigger changes so we can discuss them
beforehand.

Related projects
----------------

* FFmpeg Static Builds - http://johnvansickle.com/ffmpeg/
* Forked from - https://github.com/zimbatm/ffmpeg-static

License
-------

This project is licensed under the ISC. See the [LICENSE](LICENSE) file for
the legalities.

