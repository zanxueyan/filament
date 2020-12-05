#!/bin/bash
set -e

pushd "$(dirname "$0")" > /dev/null

docker build --tag fardiff .
docker run --rm -it -v `pwd`/../..:/filament fardiff python3 tools/fardiff/fardiff.py $1 $2 $3 $4
