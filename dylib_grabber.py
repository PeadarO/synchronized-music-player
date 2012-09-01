#!/usr/bin/env python
'''
A short utility script that collects all .dylib files associated with the given
binaries, copies them and modifies their library load path so that a copied
binary will run on a different OS X installation.  By default, it only copies
binaries located in either "/opt/" or "/usr/local", as those are (on my system)
associated with homebrew and friends. Other .dylib files are supplied by the
system and copying them is rather silly.
'''

import os
import os.path
import shutil
import subprocess
import sys

def get_dependencies(path):
    output = subprocess.check_output(["otool", "-L", path])
    lines = output.split("\n")
    lines = [line.strip() for line in lines]
    lines = [line for line in lines if line]

    dependencies = []
    for line in lines[1:]:
        dependency = line.split(' ')[0]
        assert os.path.exists(dependency)
        dependencies.append(dependency)
    return dependencies

binary = sys.argv[1]
to_process = [binary]
all_dependencies = set()

for path in to_process:
    for dependency in set(map(os.path.realpath, get_dependencies(path))) - all_dependencies:
        to_process.append(dependency)
        all_dependencies.add(dependency)

to_pack = sys.argv[1:]

for path in all_dependencies:
    if path.startswith("/usr/local/") or path.startswith("/opt"):
        to_pack.append(path)

os.mkdir("packed")

def packed_path(path):
    return os.path.join('packed', os.path.basename(path))

def loader_path(path):
    return os.path.join('@loader_path', os.path.basename(path))

for path in to_pack:
    shutil.copyfile(path, packed_path(path))

for path in to_pack:
    path = packed_path(path)
    for dependency in get_dependencies(path):
        if os.path.realpath(dependency) in to_pack:
            subprocess.check_output(["install_name_tool", "-change", dependency, loader_path(os.path.realpath(dependency)), path])

