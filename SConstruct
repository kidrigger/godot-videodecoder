
opts = Variables()

opts.Add(BoolVariable('test','build to test',True))
opts.Add(EnumVariable('platform','can be osx, linux (x11) or windows (win64)','osx',('osx','x11','win64'),
                                        map={'linux':'x11','windows':'win64'}))

env = Environment(variables=opts)

env.Append(LINKFLAG='-Wl,-rpath,./lib')

env.Append(CPPPATH=['#thirdparty/include'])
env.Append(CPPPATH=['#godot_include'])

env.Append(LIBPATH=['#test/addons/bin/osx/lib'])
env.Append(LIBS=['avformat'])
env.Append(LIBS=['avutil'])

sources = ['#src/gdnative_videodecoder.c']

output_path = ""

if env['test']:
    output_path = '#test/addons/bin/'+env['platform']+'/'
else:
    output_path = '#bin/'

env.SharedLibrary(output_path+'gdnative_videodecoder',sources)