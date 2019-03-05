#!/usr/bin/env python3
import os
import subprocess

def rename_files(prefix, changeto, filenames):
    renamer_buffer = 'RENAMER_BUFFER_314159265358979'

    for filename in filenames:
        with open(renamer_buffer,'w') as fle:
            subprocess.call(['otool','-L',filename],stdout=fle)

        data = None
        with open(renamer_buffer,'r') as flr:
            data = flr.read()

        val = map(lambda x: x[0], map(str.split,map(str.strip, data.strip().split('\n'))))
        val = list(val)[2:]

        to_change = {}
        for path in val:
            if path.startswith(prefix):
                to_change[path] = changeto+path[len(prefix):]

        for k,v in to_change.items():
            print(k, v, sep=' -> ')
            subprocess.call(['install_name_tool','-change',k,v,filename])
        subprocess.call(['rm',renamer_buffer])


if __name__ == '__main__':
    from sys import argv
    name, prefix, changeto = argv[0:3]
    filenames = argv[3:]
    rename_files(prefix, changeto, filenames)
