#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
mkdir -p dist
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -shared \
  -static -static-libgcc -static-libstdc++ src/plugin.cpp -o dist/TauntKey.red \
  -Wl,--no-insert-timestamp -Wl,--dynamicbase -Wl,--nxcompat
