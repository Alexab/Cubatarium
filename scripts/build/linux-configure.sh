#!/usr/bin/env bash
# CMake configure for Linux desktop (Ninja).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${ROOT}/build/desktop-linux"
CONFIG="${1:-Debug}"

cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE="${CONFIG}"
echo ">> configure OK -> ${BUILD_DIR} (runtime: ${ROOT}/bin)"
