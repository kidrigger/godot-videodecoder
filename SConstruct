
opts = Variables()

opts.Add(BoolVariable('test','build to test',True))
opts.Add(EnumVariable('platform','can be osx, linux or windows','osx',('osx','x11','win64'),map={'linux':'x11','windows':'win64'}))

env = Environment(variables=opts)

env.Append(CPPPATH=['#thirdparty/include'])
env.Append(CPPPATH=['#godot_include'])

env.Append(LIBPATH=['#thirdparty/lib'])

sources = ['#src/gdnative_videodecoder.c']



if env['test']:
    env.SharedLibrary('#test/addons/bin/'+env['platform']+'/'+'gdnative_videodecoder',sources)
else:
    env.SharedLibrary('#bin/gdnative_videodecoder',sources)