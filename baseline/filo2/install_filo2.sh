#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

mkdir -p external

if [ ! -d "external/filo2/.git" ]; then
    git clone --depth 1 --filter=blob:none --sparse https://github.com/acco93/filo2.git external/filo2
    git -C external/filo2 sparse-checkout set --skip-checks \
        base \
        instance \
        localsearch \
        movegen \
        opt \
        solution \
        CMakeLists.txt \
        Parameters.hpp \
        Renderer.hpp \
        main.cpp \
        readme.md \
        LICENSE
else
    echo "FILO2 already installed in external/filo2"
fi
