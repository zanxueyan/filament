#!/usr/bin/env python3
#
# Copyright 2020 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
For usage help, see README.md
"""

import argparse
import os
import tempfile
import zipfile

from pathlib import Path

TEMPDIR = tempfile.gettempdir()
SCRIPTDIR = Path(os.path.realpath(__file__)).parent
DEBUG = False

def format_bytes(bytes):
    """Pretty-print a number of bytes."""
    if bytes > 1e6:
        bytes = bytes / 1.0e6
        return '%.1fm' % bytes
    if bytes > 1e3:
        bytes = bytes / 1.0e3
        return '%.1fk' % bytes
    return str(bytes)


def get_hint_marker(filename):
    HINTS = ['renderer', 'libfilament-jni']
    hints = [hint in filename for hint in HINTS]
    return '*' if True in hints else ' '


def choose_from_dir(path: Path):
    paths = list(path.glob('**/*.so'))
    has_dso = False
    for index, dso in enumerate(paths):
        has_dso = True
        size = dso.stat().st_size / 1000
        filename = str(dso)
        marker = get_hint_marker(filename)
        print(f"{index:3} {marker} {size:8.0f} kB {filename}")
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
        csize = csize / 1000
        usize = usize / 1000
        marker = get_hint_marker(filename)
        print(f"{index:3} {marker} {usize:8.0f} kB ({csize:8.0f} kB) {filename}")
    has_dso or quit("No .so file found in the specified path")
    val = input("Which dso should be analyzed? Type a number. ")
    chosen_filename = paths[int(val)][0]
    result_path = z.extract(chosen_filename, TEMPDIR)
    if DEBUG: os.system(f'cp {result_path} .')
    return result_path


def main(args):
    path = Path(args.input)
    path.exists() or quit("No file or folder at the specified path.")
    if path.is_dir():
        path = choose_from_dir(path)
    elif path.suffix in ['.zip', '.aar', '.apk']:
        path = choose_from_zip(path)

    dsopath = Path(path)
    size = format_bytes(dsopath.stat().st_size)
    info = f'Uncompressed size is {size}'

    print('Running nm... (this might take a while)')
    os.system(f'nm -C -S -l {path} > {TEMPDIR}/nm.out')
    if DEBUG: os.system(f'cp {TEMPDIR}/nm.out .')

    print('Running objdump...')
    os.system(f'objdump -h {path} > {TEMPDIR}/objdump.out')
    if DEBUG: os.system(f'cp {TEMPDIR}/objdump.out .')

    print('Generating treemap JSON...')
    os.system(f'cd {TEMPDIR} ; python {SCRIPTDIR}/bloat.py syms > syms.json')
    os.system(f'cd {TEMPDIR} ; python {SCRIPTDIR}/bloat.py sections > sections.json')
    if DEBUG: os.system(f'cp {TEMPDIR}/syms.json .')
    if DEBUG: os.system(f'cp {TEMPDIR}/sections.json .')

    print(f'Generating {args.output}...')
    sectionsjson = open(f'{TEMPDIR}/sections.json').read()
    symbolsjson = open(f'{TEMPDIR}/syms.json').read()
    template = open(f'{SCRIPTDIR}/template.html').read()
    template = template.replace('$TITLE$', dsopath.name)
    template = template.replace('$INFO$', info)
    template = template.replace('$SYMBOLS_JSON$', symbolsjson)
    template = template.replace('$SECTIONS_JSON$', sectionsjson)
    open(args.output, 'w').write(template)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="index.html", help="generated HTML file name")
    parser.add_argument("input", help="path to folder, zip, aar, apk, or so")
    args = parser.parse_args()
    main(args)
