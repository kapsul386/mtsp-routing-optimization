#!/usr/bin/env bash
# Build VROOM binary.
# Requires: g++ with C++20, libssl-dev, asio. Run inside WSL Ubuntu.
#   sudo apt install -y g++ make libssl-dev libasio-dev
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="$ROOT_DIR/external/vroom"

if [ ! -d "$SRC_DIR" ]; then
  echo "[build_vroom] error: $SRC_DIR not found. Run install_vroom.sh first." >&2
  exit 1
fi

cd "$SRC_DIR/src"
make -j

echo "[build_vroom] binary: $ROOT_DIR/external/vroom/bin/vroom"
ls -la "$ROOT_DIR/external/vroom/bin/vroom" 2>/dev/null || echo "[build_vroom] WARN: binary not at expected path"
