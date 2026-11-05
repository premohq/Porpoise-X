#!/usr/bin/env bash
# Checks the core's native routines (HLE_PorpoiseNative) against an interpreter
# of the game code they replace, on random data. Needs the patched Dolphin tree
# (tools/build-dolphin.sh) and a host clang.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../.." && pwd)
out=$(mktemp -d)
clang++ -std=c++20 -O2 -ffp-contract=off -I"$here/stub" -I"$root/.deps/dolphin-src/Source/Core" \
    "$here/check.cpp" -o "$out/check"
"$out/check"
