#!/usr/bin/env python

opts = Variables()

opts.Add(BoolVariable('test','build to test',True))
opts.Add(BoolVariable('debug','debug build',True))
opts.Add(EnumVariable('platform','can be osx, linux (x11) or windows (win64)','',('osx','x11','win64'),
                                        map={'linux':'x11','windows':'win64'}))
opts.Add(BoolVariable('link_static','', False))

env = Environment(variables=opts)

if env['debug']:
    env.Append(CPPFLAGS=['-g'])

if env['platform'] == 'osx':
    # build everything, rename
    pass
elif env['platform'] == 'x11':
    env.Append(RPATH=env.Literal('\$$ORIGIN/lib'))

env.Append(CPPPATH=['#test/addons/bin/'+env['platform']+'/include'])
env.Append(CPPPATH=['#godot_include'])

# env.Append(LDFLAGS = ['-static'])

env.Append(LIBPATH=['#test/addons/bin/'+env['platform']+'/lib'])
env.Append(LIBS=['avformat'])
env.Append(LIBS=['avutil'])
env.Append(LIBS=['swscale'])
env.Append(LIBS=['swresample'])

sources = ['#src/gdnative_videodecoder.c']

output_path = ""

if env['test']:
    output_path = '#test/addons/bin/'+env['platform']+'/'
else:
    output_path = '#bin/'

env.SharedLibrary(output_path+'gdnative_videodecoder',sources)
