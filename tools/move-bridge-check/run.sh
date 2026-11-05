#!/usr/bin/env bash
# REXCRAFT88's wire tests for his Move Studio input client (src/porpoise_move_bridge.hpp).
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out=$(mktemp -d)
clang++ -std=c++20 -O2 -Wall -Wextra "$here/wire_protocol.cpp" -o "$out/wire"
"$out/wire" && echo "move bridge wire tests passed"
