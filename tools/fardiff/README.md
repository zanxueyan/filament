# fardiff : Filament Archive Diffing Utility

`fardiff` helps you analyze the size of an Android dso or apk that uses Filament. It wraps Evan
Martin's `bloat` and `webtreemap` tools, and includes the following enhancements:

- Filament materials are shown if `resgen --json` was used in the build.
- The input path can be a so file, folder, or zip archive (apk or aar).
- If the path is a zip or folder, interactively finds the file to analyze.
- The generated web report is a self-contained HTML file.
- Two binaries can be provided to create a side-by-side report.
- Reports the gzipped size of all Filament materials.

For example, from a mac, you can generate a self-contained HTML file
by typing this from the Filament root:

    ./tools/fardiff/fardiff.py ./android/filament-android
    open index.html

You can also compare two files:

    ./tools/fardiff/fardiff.py -o comparison.html old.apk new.apk
    open comparison.html

This tool uses Python, `nm`, and `objdump`. To make it easy to use on any machine, there is a
`Dockerfile` provided that installs dependencies inside a Linux container, plus a bash
script that invokes docker for you. Simply use `fardiff.sh` instead of `fardiff.py`.
