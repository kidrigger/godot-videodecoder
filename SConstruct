
opts = Variables()

opts.Add(BoolVariable('test','build to test',True))
opts.Add(EnumVariable('platform','can be osx, linux or windows','osx',('osx','x11','win64'),map={'linux':'x11','windows':'win64'}))

env = Environment(variables=opts)

env.Append(CPPPATH=['#thirdparty/include'])
env.Append(CPPPATH=['#godot_include'])

env.Append(LIBPATH=['#thirdparty/lib'])
env.Append(LIBS=['avformat','avutil'])

sources = ['#src/gdnative_videodecoder.c']

output_path = ""

if env['test']:
    output_path = '#test/addons/bin/'+env['platform']+'/'
else:
    output_path = '#bin/'

env.SharedLibrary(output_path+'gdnative_videodecoder',sources)