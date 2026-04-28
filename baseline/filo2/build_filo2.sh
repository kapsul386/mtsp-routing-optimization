#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR/external/filo2"

mkdir -p build
cd build

cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_VERBOSE=1 \
  -DENABLE_TIMELIMIT=1

cmake --build . --config Release --parallel

