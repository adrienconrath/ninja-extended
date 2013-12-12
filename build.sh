#!/bin/bash
CMAKE="cmake"

if ! [ -d _build ]; then
  mkdir _build
fi

cd _build
if ! [ -e CMakeCache.txt ]; then
  echo "Running cmake..."
  export CXX=g++
  $CMAKE ..
fi
echo "Building..."
make
cd ..
