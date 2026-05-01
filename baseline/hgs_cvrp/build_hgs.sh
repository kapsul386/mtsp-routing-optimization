#!/usr/bin/env bash
# Build HGS-CVRP binary into external/HGS-CVRP/build/hgs.
# Requires: cmake >= 3.10, g++ with C++17. Run inside WSL Ubuntu (Linux build) or
# native Windows with MSVC if you prefer.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="$ROOT_DIR/external/HGS-CVRP"

if [ ! -d "$SRC_DIR" ]; then
  echo "[build_hgs] error: $SRC_DIR not found. Run install_hgs.sh first." >&2
  exit 1
fi

cd "$SRC_DIR"
mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release --parallel

echo "[build_hgs] binary: $SRC_DIR/build/hgs"
ls -la "$SRC_DIR/build/hgs" 2>/dev/null || echo "[build_hgs] WARN: hgs binary not found at expected path"
