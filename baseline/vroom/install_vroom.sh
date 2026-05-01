#!/usr/bin/env bash
# Clone VROOM source into external/vroom.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TARGET="$ROOT_DIR/external/vroom"

if [ -d "$TARGET/.git" ]; then
  echo "[install_vroom] already cloned at $TARGET"
  exit 0
fi

mkdir -p "$ROOT_DIR/external"
git clone --depth 1 --recurse-submodules https://github.com/VROOM-Project/vroom.git "$TARGET"
echo "[install_vroom] cloned to $TARGET"
echo "[install_vroom] next: bash baseline/vroom/build_vroom.sh"
