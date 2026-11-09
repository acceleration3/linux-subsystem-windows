#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${LSW_RENDER_REF_BUILD:-${ROOT}/build-render-ref}"

launcher=()
if command -v ccache >/dev/null 2>&1; then
    launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

revision="$(git -C "${ROOT}" rev-parse --short HEAD 2>/dev/null || echo unknown)"

echo ">>> Configuring lsw-render-ref -> ${BUILD}"
cmake -S "${ROOT}/suites/render-ref" -B "${BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS=-march=x86-64-v2 \
    -DLSW_RENDER_REF_REV="${revision}" \
    ${LSW_SUITES_KHRONOS_DIR:+-DLSW_SUITES_KHRONOS_DIR="${LSW_SUITES_KHRONOS_DIR}"} \
    "${launcher[@]}"
echo ">>> Building lsw-render-ref"
cmake --build "${BUILD}" ${CMAKE_BUILD_PARALLEL_LEVEL:+-j "${CMAKE_BUILD_PARALLEL_LEVEL}"}

if [[ ! -x "${BUILD}/lsw-render-ref" ]]; then
    echo "!!! Build did not produce ${BUILD}/lsw-render-ref" >&2
    exit 1
fi
echo ">>> Built ${BUILD}/lsw-render-ref"
