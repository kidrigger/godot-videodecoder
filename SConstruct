#!/usr/bin/env python

import os
from glob import glob

opts = Variables()

opts.Add(BoolVariable('debug','debug build',True))
opts.Add(BoolVariable('test','copy output to test project',True))
opts.Add(EnumVariable('platform','can be osx, linux (x11) or windows (win64)','',('osx','x11','win64','win32','x11_32'),
                                        map={'linux':'x11','windows':'win64'}))
opts.Add(PathVariable('toolchainbin', 'Path to the cross compiler toolchain bin directory. Only needed cross compiling and the toolchain isn\'t installed.', '', PathVariable.PathAccept))
opts.Add(PathVariable('thirdparty', 'Path containing the ffmpeg libs', 'thirdparty', PathVariable.PathAccept))
opts.Add(PathVariable('prefix', 'Path where the output lib will be installed', '', PathVariable.PathAccept))
opts.Add(EnumVariable('darwinver', 'Darwin SDK version. (if cross compiling from linux to osx)', '15', [str(v) for v in range(11, 19 + 1)]))

#probably a better way to do this instead of creating Enviroment() twice
early_env=Environment(variables=opts, BUILDERS={})
lib_prefix = early_env['thirdparty'] + '/' + early_env['platform']

msvc_build = os.name == 'nt'
if msvc_build:
    lib_path = lib_prefix + '/bin'
else:
    lib_path = lib_prefix + '/lib'
include_path = lib_prefix + '/include'
# probably a better way to do this too (pass $TOOL_PREFIX)
# PWD is not present in windows
pwd = os.environ.get('PWD') or os.getcwd()

osx_renamer = Builder(action = './renamer.py ' + pwd + '/' + lib_path + '/ @loader_path/ "$TOOL_PREFIX" $SOURCE', )

env = Environment(variables=opts, BUILDERS={'OSXRename':osx_renamer}, CFLAGS='/WX' if msvc_build else '-std=gnu11', TARGET_ARCH='x86_64')

if env['toolchainbin']:
    env.PrependENVPath('PATH', env['toolchainbin'])
output_path = '#bin/' + env['platform'] + '/'

if env['debug'] and not msvc_build:
    env.Append(CPPFLAGS=['-g'])

env.Append(LIBPATH=[lib_path])
if env['platform'] == 'x11':
    env.Append(RPATH=env.Literal('\$$ORIGIN'))
    # statically link glibc
    env.Append(LIBS=[File('/usr/lib/x86_64-linux-gnu/libc_nonshared.a')])
if env['platform'] == 'x11_32':
    env.Append(RPATH=env.Literal('\$$ORIGIN'))
    # statically link glibc
    env.Append(LIBS=[File('/usr/lib/i386-linux-gnu/libc_nonshared.a')])

env.Append(CPPPATH=['#' + include_path + '/'])
env.Append(CPPPATH=['#godot_include'])

tool_prefix = ''
if os.name == 'posix' and env['platform'] == 'win64':
    tool_prefix = "x86_64-w64-mingw32-"
    env['SHLIBSUFFIX'] = '.dll'
    env.Append(CPPDEFINES='WIN32')
if os.name == 'posix' and env['platform'] == 'win32':
    tool_prefix = "i686-w64-mingw32-"
    env['SHLIBSUFFIX'] = '.dll'
    env.Append(CPPDEFINES='WIN32')
if os.name == 'posix' and env['platform'] == 'osx':
    tool_prefix = 'x86_64-apple-darwin' + env['darwinver'] + '-'
    if (os.getenv("OSXCROSS_PREFIX")):
        tool_prefix = os.getenv('OSXCROSS_PREFIX')
    env['SHLIBSUFFIX'] = '.dylib'

globs = {
    'x11': '*.so.[0-9]*',
    'win64': '../bin/*-[0-9]*.dll',
    'osx': '*.[0-9]*.dylib',
    'x11_32': '*.so.[0-9]*',
    'win32': '../bin/*-[0-9]*.dll',
}

ffmpeg_dylibs = glob(lib_path + '/' + globs[env['platform']])

if env['platform'].startswith('win') and tool_prefix:
    # mingw needs libwinpthread-1.dll which should be here. (remove trailing '-' from tool_prefix)
    winpthread = '/usr/%s/lib/libwinpthread-1.dll' % tool_prefix[:-1]
    ffmpeg_dylibs.append(winpthread)

installed_dylib = []
for dylib in ffmpeg_dylibs:
    installed_dylib.append(env.Install(output_path,dylib))

if tool_prefix:
    env['CC'] = tool_prefix + 'gcc'
    env['AS'] = tool_prefix + 'as'
    env['CXX'] = tool_prefix + 'g++'
    env['AR'] = tool_prefix + 'gcc-ar'
    env['RANLIB'] = tool_prefix + 'gcc-ranlib'
    if env['toolchainbin']:
        # there's probably a better way to pass the PATH to the renamer script
        env['TOOL_PREFIX'] = env['toolchainbin'] + '/' + tool_prefix
    else:
        env['TOOL_PREFIX'] = tool_prefix

if env['platform'] == 'osx':
    for dylib in installed_dylib:
        env.OSXRename(None, dylib)

env.Append(LIBPATH=[output_path])
env.Append(LIBS=['avformat'])
env.Append(LIBS=['avcodec'])
env.Append(LIBS=['avutil'])
env.Append(LIBS=['swscale'])
env.Append(LIBS=['swresample'])
if msvc_build:
    env.Append(LIBS=['WinMM.lib'])


sources = list(map(lambda x: '#'+x, glob('src/*.c')))

# msvc doesn't prefix dll files with 'lib'
libprefix = 'lib' if msvc_build else ''
if env['debug']:
    env['PDB'] = output_path+libprefix+'gdnative_videodecoder.pdb'
output_dylib = env.SharedLibrary(output_path+libprefix+'gdnative_videodecoder',sources)

if env['platform'] == 'osx':
    env.OSXRename(None, output_dylib)

if env['prefix']:
    path = env['prefix'] + '/' + env['platform'] + '/'
    Default(env.Install(path, ffmpeg_dylibs + output_dylib))
elif env['test']:
    env.Install('#test/addons/' + output_path[1:], output_dylib + ffmpeg_dylibs)
