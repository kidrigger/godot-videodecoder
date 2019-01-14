# Godot Video Decoder

GDNative Video Decoder libraries for Godot Game Engine, using FFmpeg library for codecs.

#### A GSoC 2018 Project

## Instructions to use the test project:

1. Clone the repository
`git clone https://github.com/KidRigger/godot-videodecoder.git godot-videodecoder`

2. Clone FFmpeg in another folder.
`git clone git://source.ffmpeg.org/ffmpeg.git ffmpeg`

3. Switch ffmpeg to branch `release/4.0`
    (Currently only tested with 4.0, feel free to test with 4.1)

4. Configure FFmpeg using the `configure` tool in the repository. Use the following flags. (Or as per requirement) [Needs OpenCL] _Do not forget to replace the path to godot-videodecoder folder_

    - OSX
`--enable-shared --enable-version3 --disable-programs \
--enable-libmp3lame --enable-libtheora \
--enable-libvorbis --enable-libvpx --enable-libwebp \
--enable-opencl --enable-opengl --disable-debug \
--prefix=<path-to-videodecoder-folder>/godot-videodecoder/test/addons/bin/osx`
    - Linux
`--enable-shared --enable-version3 --disable-programs \
--enable-libmp3lame --enable-libtheora \
--enable-libvorbis --enable-libvpx --enable-libwebp \
--enable-opencl --enable-opengl --disable-debug \
--prefix=<path-to-videodecoder-folder>/godot-videodecoder/test/addons/bin/x11`
    - Windows
`--enable-shared --enable-version3 --disable-programs \
--enable-libmp3lame --enable-libtheora \
--enable-libvorbis --enable-libvpx --enable-libwebp \
--enable-opencl --enable-opengl --disable-debug \
--prefix=<path-to-videodecoder-folder>/godot-videodecoder/test/addons/bin/win64`

5. Go to the Godot videodecoder folder, and clone the samples
`git submodule init`
`git submodule update`

6. Now you can simply use the test project.