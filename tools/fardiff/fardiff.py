#!/usr/bin/env python3

"""
For usage help:
    ./fardiff.py --help

Examples:
    python3 tools/fardiff/fardiff.py libfilament-jni.so
"""

import argparse
import os
import tempfile
import zipfile

from pathlib import Path

tempdir = tempfile.gettempdir()
scriptdir = Path(os.path.realpath(__file__)).parent

# If the filename of a native binary inside a zip or dir contains one of these strings, then it is
# marked with an asterisk as a hint to the user.
def get_marker(filename):
    HINTS = ['renderer', 'libfilament-jni']
    hints = [hint in filename for hint in HINTS]
    return '*' if True in hints else ' '


def choose_from_dir(path: Path):
    paths = list(path.glob('**/*.so'))
    has_dso = False
    for index, dso in enumerate(paths):
        has_dso = True
        size = dso.stat().st_size / 1024
        filename = str(dso)
        marker = get_marker(filename)
        print(f"{index:3} {marker} {size:8.0f} KiB {filename}")
    has_dso or quit("No .so file found in the specified path")
    val = input("Which dso should be analyzed? Type a number. ")
    return str(paths[int(val)])


def choose_from_zip(path: Path):
    z = zipfile.ZipFile(path)
    paths = []
    for info in z.infolist():
        if info.filename.endswith('.so'):
            paths.append((info.filename, info.compress_size, info.file_size))
    has_dso = False
    for index, dso in enumerate(paths):
        has_dso = True
        filename, csize, usize = dso
        csize = csize / 1024
        usize = usize / 1024
        marker = get_marker(filename)
        print(f"{index:3} {marker} {usize:8.0f} KiB ({csize:8.0f} KiB) {filename}")
    has_dso or quit("No .so file found in the specified path")
    val = input("Which dso should be analyzed? Type a number. ")
    chosen_filename = paths[int(val)][0]
    return z.extract(chosen_filename, tempdir)


def main(args):
    path = Path(args.input)

    path.exists() or quit("No file or folder at the specified path.")

    if path.is_dir():
        path = choose_from_dir(path)
    elif path.suffix in ['.zip', '.aar', '.apk']:
        path = choose_from_zip(path)

    print('Running nm... (this might take a while)')
    os.system(f'nm -C -S -l {path} > {tempdir}/nm.out')

    print('Running objdump...')
    os.system(f'objdump -h {path} > {tempdir}/objdump.out')

    print('Generating treemap JSON...')
    os.system(f'cd {tempdir} ; python {scriptdir}/bloat.py syms > bloat.json')

    print(f'Generating {args.output}...')

    bloatjson = open(f'{tempdir}/bloat.json').read()
    treemapcss = open(f'{scriptdir}/webtreemap.css').read()
    treemapjs = open(f'{scriptdir}/webtreemap.js').read()

    template = open(f'{scriptdir}/template.html').read()
    template = template.replace('$BLOAT_JSON$', bloatjson)
    template = template.replace('$WEBTREEMAP_CSS$', treemapcss)
    template = template.replace('$WEBTREEMAP_JS$', treemapjs)

    open(args.output, 'w').write(template)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="index.html", help="generated HTML file name")
    parser.add_argument("input", help="path to folder, zip, aar, apk, or so")
    args = parser.parse_args()
    main(args)
