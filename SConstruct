import os
import os.path
import sys

env = Environment()

if sys.platform == 'darwin':
    # Build flags to build Lion-compatible binaries from Mountain Lion.
    env.Append(CPPFLAGS = ['-isysroot', '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.7.sdk', '-mmacosx-version-min=10.7'])
    env.Append(LINKFLAGS = ['-isysroot', '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.7.sdk', '-mmacosx-version-min=10.7'])
    # Paths for homebrew.
    env.Append(CPPPATH = ['/usr/local/include'])
    env.Append(LIBPATH = ['/usr/local/lib'])

if sys.platform == 'linux2':
    env.Append(CPPFLAGS = ['-D__STDC_CONSTANT_MACROS'])

env.Append(CPPFLAGS = ['-O2'])

def objects(*names):
    return [env.Object(name) for name in names]

env.Program(target='player', source=objects('player.cc', 'util.cc'), LIBS=['avcodec', 'avformat', 'avutil', 'portaudio', 'gflags'])

