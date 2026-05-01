#!/usr/bin/env bash
# Clone HGS-CVRP source into external/HGS-CVRP.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TARGET="$ROOT_DIR/external/HGS-CVRP"

if [ -d "$TARGET/.git" ]; then
  echo "[install_hgs] already cloned at $TARGET"
  exit 0
fi

mkdir -p "$ROOT_DIR/external"
git clone --depth 1 https://github.com/vidalt/HGS-CVRP.git "$TARGET"
echo "[install_hgs] cloned to $TARGET"
echo "[install_hgs] next: bash baseline/hgs_cvrp/build_hgs.sh"
