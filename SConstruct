#!/usr/bin/env python
import os
import platform
import sys

# Try to detect the host platform automatically.
# This is used if no `platform` argument is passed
if sys.platform.startswith("linux"):
    host_platform = "linux"
elif sys.platform == "darwin":
    host_platform = "macos"
elif sys.platform == "win32" or sys.platform == "msys":
    host_platform = "windows"
else:
    raise ValueError("Could not detect platform automatically, please specify with " "platform=<platform>")

env = Environment(ENV=os.environ)

opts = Variables([], ARGUMENTS)

# Define our options
opts.Add(EnumVariable("target", "Compilation target", "template_debug", ["template_debug", "template_release"]))
opts.Add(EnumVariable("platform", "Compilation platform", host_platform, ["", "windows", "macos", "linux"]))
opts.Add(EnumVariable("bits", "Target platform bits", "64", ("32", "64")))
opts.Add(BoolVariable("use_llvm", "Use the LLVM / Clang compiler", "no"))
opts.Add(PathVariable('toolchainbin', 'Path to the cross compiler toolchain bin directory. Only needed cross compiling and the toolchain isn\'t installed.', '', PathVariable.PathAccept))
opts.Add(PathVariable("target_path", "The path where the lib is installed.", "target/", PathVariable.PathAccept))
opts.Add(PathVariable("target_name", "The library name.", "libgdvideo", PathVariable.PathAccept))
opts.Add(PathVariable("godot_cpp_path", "The path to the godot-cpp directory.", "godot-cpp", PathVariable.PathAccept))
opts.Add(PathVariable("thirdparty_path", "The path to the thirdparty directory.", "thirdparty", PathVariable.PathAccept))
opts.Add(BoolVariable("vsproj", "Generate a project for Visual Studio", "no"))
opts.Add(PathVariable('darwinver', 'Darwin SDK version. (if cross compiling from linux to osx)', '15', PathVariable.PathAccept))

# Updates the environment with the option variables.
opts.Update(env)
# Generates help for the -h scons option.
Help(opts.GenerateHelpText(env))

# Local dependency paths, adapt them to your setup
godot_headers_path = "godot-cpp/gdextension/"
cpp_bindings_path = env["godot_cpp_path"] + "/"
godotcpp_library = "libgodot-cpp"

if env["platform"] == "macos" and env["bits"] == "32":
    print("32-bit builds are not supported on macOS.")
    Exit()

if env['toolchainbin']:
    env.PrependENVPath('PATH', env['toolchainbin'])

# This makes sure to keep the session environment variables on Windows.
# This way, you can run SCons in a Visual Studio 2017 prompt and it will find
# all the required tools
if host_platform == "windows" and env["platform"] != "android":
    if env["bits"] == "64":
        env = Environment(TARGET_ARCH="amd64")
        env['msvc_arch'] = 'X64'
    elif env["bits"] == "32":
        env = Environment(TARGET_ARCH="x86")
        env['msvc_arch'] = 'X86'

    opts.Update(env)

# Process some arguments
if env["use_llvm"]:
    env["CC"] = "clang"
    env["CXX"] = "clang++"

if env["platform"] == "":
    print("No valid target platform selected.")
    quit()

# For the reference:
# - CCFLAGS are compilation flags shared between C and C++
# - CFLAGS are for C-specific compilation flags
# - CXXFLAGS are for C++-specific compilation flags
# - CPPFLAGS are for pre-processor flags
# - CPPDEFINES are for pre-processor defines
# - LINKFLAGS are for linking flags

if env["target"] in ("template_debug"):
    env.Append(CPPDEFINES=["DEBUG_ENABLED", "DEBUG_METHODS_ENABLED"])

# Check our platform specifics
if env["platform"] == "macos":
    env["target_path"] += "/macos/"
    godotcpp_library += ".macos"
    env.Append(CCFLAGS=["-arch", "x86_64"])
    env.Append(CXXFLAGS=["-std=c++17"])
    env.Append(LINKFLAGS=["-arch", "x86_64", "-ldl"])
    if env["target"] in ("template_debug"):
        env.Append(CCFLAGS=["-g", "-O2"])
    else:
        env.Append(CCFLAGS=["-O3"])

elif env["platform"] in ("linux"):
    env["target_path"] += "/linux_%s/" % env["bits"]
    godotcpp_library += ".linux"
    env.Append(CCFLAGS=["-fPIC"])
    env.Append(CXXFLAGS=["-std=c++17"])
    if env["target"] in ("template_debug"):
        env.Append(CCFLAGS=["-g", "-O2"])
    else:
        env.Append(CCFLAGS=["-O3"])

elif env["platform"] == "windows":
    env["target_path"] += "/windows_%s/" % env["bits"]
    godotcpp_library += ".windows"
    # This makes sure to keep the session environment variables on windows,
    # that way you can run scons in a vs 2017 prompt and it will find all the required tools
    env.Append(ENV=os.environ)

    env.Append(CPPDEFINES=["WIN32", "_WIN32", "_WINDOWS", "_CRT_SECURE_NO_WARNINGS", "_ITERATOR_DEBUG_LEVEL=2"])
    env.Append(CXXFLAGS=["-std=c++17"])
    if env["target"] in ("template_debug"):
        env.Append(CCFLAGS=["-g", "-O2"])
    else:
        env.Append(CCFLAGS=["-O3"])

    if not(env["use_llvm"]):
        env.Append(CPPDEFINES=["TYPED_METHOD_BIND"])

godotcpp_library += ".%s" % env["target"]

if env["bits"] == "32":
    godotcpp_library += ".x86_32"
else:
    godotcpp_library += ".x86_64"

if env["platform"] == "macos":
    lib_prefix = "%s/%s" % (env['thirdparty_path'], env['platform'])
else:
    lib_prefix = "%s/%s_%s" % (env['thirdparty_path'], env['platform'], env['bits'])

msvc_build = os.name == 'nt'
if msvc_build:
    lib_path = lib_prefix + '/bin'
else:
    lib_path = lib_prefix + '/lib'
include_path = lib_prefix + '/include'
env.Prepend(CPPPATH=["#" + include_path + "/"])

# make sure our binding library is properly includes
env.Append(CPPPATH=[".", godot_headers_path, cpp_bindings_path + "include/", cpp_bindings_path + "gen/include/"])
env.Append(LIBPATH=[cpp_bindings_path + "bin/"])
env.Append(LIBPATH=[lib_path])
env.Append(LIBS=['avformat'])
env.Append(LIBS=['avcodec'])
env.Append(LIBS=['avutil'])
env.Append(LIBS=['swscale'])
env.Append(LIBS=['swresample'])
env.Append(LIBS=[godotcpp_library])

# tweak this if you want to use different folders, or more folders, to store your source code in.
env.Append(CPPPATH=["src/"])

Export('env')

env.__class__.sources = []
env.__class__.modules_sources = []
env.__class__.includes = []

sources = Glob("src/*.cpp")
includes = Glob("src/*.h")

env.sources += sources
env.includes = includes

def add_source_files(self, arr, regex):
    if type(regex) == list:
        arr += regex
        # print(arr)
    elif type(regex) == str:
        arr += Glob(regex)
        # print(arr)

env.__class__.add_source_files = add_source_files 

target_name = ""
lib_target = env["target_path"]
if env["target"] in ("template_debug"):
    target_name = "Debug"
else:
    target_name = "Release"

if env['platform'] == 'linux':
    env.Append(RPATH=env.Literal('\$$ORIGIN'))
    if env['bits'] == '32':
        #env.Append(LIBS=[File('/usr/lib32/libc_nonshared.a')])
        env.Append(CCFLAGS=['-m32'])
        env.Append(LINKFLAGS=['-m32'])
    #else:
    #    env.Append(LIBS=[File('/usr/lib/libc_nonshared.a')])
if env['platform'] == 'windows':
    env.Append(LIBS=['pthread'])
    env.Append(LINKFLAGS=['-static-libgcc'])
    env.Append(LINKFLAGS=['-static-libstdc++'])

tool_prefix = ''
if os.name == 'posix' and env['platform'] == 'windows' and env['bits'] == '64':
    tool_prefix = "x86_64-w64-mingw32-"
    env['SHLIBSUFFIX'] = '.dll'
    env.Append(CPPDEFINES='WIN32')
if os.name == 'posix' and env['platform'] == 'windows' and env['bits'] == '32':
    tool_prefix = "i686-w64-mingw32-"
    env['SHLIBSUFFIX'] = '.dll'
    env.Append(CPPDEFINES='WIN32')
if os.name == 'posix' and env['platform'] == 'macos':
    tool_prefix = 'x86_64-apple-darwin' + env['darwinver'] + '-'
    if (os.getenv("OSXCROSS_PREFIX")):
        tool_prefix = os.getenv('OSXCROSS_PREFIX')
    env['SHLIBSUFFIX'] = '.dylib'

if tool_prefix:
    env['CC'] = tool_prefix + env['CC']
    env['AS'] = tool_prefix + env['AS']
    env['CXX'] = tool_prefix + env['CXX']
    env['AR'] = tool_prefix + env['AR']
    env['RANLIB'] = tool_prefix + env['RANLIB']
    if env['toolchainbin']:
        # there's probably a better way to pass the PATH to the renamer script
        env['TOOL_PREFIX'] = env['toolchainbin'] + '/' + tool_prefix
    else:
        env['TOOL_PREFIX'] = tool_prefix

globs = {
    'windows': '../bin/*-[0-9]*.dll',
    'macos': '*.[0-9]*.dylib',
    'linux': '*.so.[0-9]*',
}

from glob import glob

ffmpeg_dylibs = glob(lib_path + '/' + globs[env['platform']])

if env['platform'].startswith('win') and tool_prefix:
    # mingw needs libwinpthread-1.dll which should be here. (remove trailing '-' from tool_prefix)
    winpthread = '/usr/%s/lib/libwinpthread-1.dll' % tool_prefix[:-1]
    ffmpeg_dylibs.append(winpthread)

installed_dylib = []
for dylib in ffmpeg_dylibs:
    installed_dylib.append(env.Install(lib_target,dylib))


library = env.SharedLibrary(target = lib_target + "%s-%s" % (env["target_name"], env["target"]), source=env.sources+env.modules_sources)


if env["vsproj"]:
    vsproj = env.MSVSProject(target = 'godot_video_reference' + env['MSVSPROJECTSUFFIX'],
                    srcs = env.sources + env.modules_sources,
                    incs = env.includes,
                    localincs = [],
                    resources = [],
                    misc = ['LICENSE','README.md','.clang-format','.gitignore','.gitmodules'],
                    buildtarget = library,
                    variant = [target_name + '|'+env['msvc_arch']] * len(library)
                    )

Default(env.Install(lib_target, ffmpeg_dylibs))
Default(library)