#!/usr/bin/env python

import os

opts = Variables()

opts.Add(BoolVariable('debug','debug build',True))
opts.Add(BoolVariable('test','copy output to test project',True))
opts.Add(EnumVariable('platform','can be osx, linux (x11) or windows (win64)','',('osx','x11','win64'),
                                        map={'linux':'x11','windows':'win64'}))

#probably a better way to do this instead of creating Enviroment() twice
prefix = 'thirdparty/' + Environment(variables=opts, BUILDERS={})['platform']
lib_path = prefix + '/lib'
include_path = prefix + '/include'
osx_renamer = Builder(action = './renamer.py ' + os.environ.get('PWD') + '/' + lib_path + '/ @loader_path/ $SOURCE')
env = Environment(variables=opts, BUILDERS={'OSXRename':osx_renamer})

output_path = '#bin/' + env['platform']+ '/'

if env['debug']:
    env.Append(CPPFLAGS=['-g'])

if env['platform'] == 'x11':
    env.Append(LIBPATH=[lib_path])
    env.Append(RPATH=env.Literal('\$$ORIGIN'))

env.Append(CPPPATH=['#' + include_path + '/'])
env.Append(CPPPATH=['#godot_include'])

from glob import glob
ffmpeg_dylibs = glob(lib_path + '/*.dylib')

installed_dylib = []
for dylib in ffmpeg_dylibs:
    installed_dylib.append(env.Install(output_path,dylib))
if env['platform'] == 'osx':
    for dylib in installed_dylib:
        env.OSXRename(None, dylib)

env.Append(LIBPATH=[output_path])
env.Append(LIBS=['avformat'])
env.Append(LIBS=['avcodec'])
env.Append(LIBS=['avutil'])
env.Append(LIBS=['swscale'])
env.Append(LIBS=['swresample'])

sources = list(map(lambda x: '#'+x, glob('src/*.c')))

output_dylib = env.SharedLibrary(output_path+'gdnative_videodecoder',sources)
if env['platform'] == 'osx':
    env.OSXRename(None, output_dylib)

if env['test']:
    for dylib in installed_dylib:
        env.Install('#test/addons/'+output_path[1:],dylib);
    env.Install('#test/addons/'+output_path[1:], output_dylib)

