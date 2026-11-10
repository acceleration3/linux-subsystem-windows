#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${LSW_SUITES_BUILD:-${ROOT}/build-suites}"
BUILD32="${LSW_SUITES32_BUILD:-${ROOT}/build-suites32}"

launcher=()
if command -v ccache >/dev/null 2>&1; then
    launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache)
fi

revision="$(git -C "${ROOT}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [[ -n $(git -C "${ROOT}" status --porcelain --untracked-files=no -- suites cmake 2>/dev/null) ]]; then
    revision+="+dirty"
fi

build_one() {
    local label=$1 dir=$2 toolchain=$3
    echo ">>> Configuring lsw-suites ${label} -> ${dir}"
    cmake -S "${ROOT}/suites" -B "${dir}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${ROOT}/cmake/${toolchain}" \
        -DCMAKE_BUILD_TYPE=Release \
        ${LSW_SUITES_KHRONOS_DIR:+-DLSW_SUITES_KHRONOS_DIR="${LSW_SUITES_KHRONOS_DIR}"} \
        "${launcher[@]}"
    echo ">>> Building lsw-suites ${label}"
    cmake --build "${dir}" ${CMAKE_BUILD_PARALLEL_LEVEL:+-j "${CMAKE_BUILD_PARALLEL_LEVEL}"}
    printf '%s\n' "${revision}" > "${dir}/REVISION"
}

BITS="${LSW_SUITES_BITS:-both}"
exes=()
if [[ "${BITS}" != 32 ]]; then
    build_one 64-bit "${BUILD}" toolchain-mingw-w64.cmake
    exes+=("${BUILD}/lsw-suites64.exe")
fi
if [[ "${BITS}" != 64 ]]; then
    build_one 32-bit "${BUILD32}" toolchain-mingw-w64-i686.cmake
    exes+=("${BUILD32}/lsw-suites32.exe")
fi

for exe in "${exes[@]}"; do
    if [[ ! -f "${exe}" ]]; then
        echo "!!! Build did not produce ${exe}" >&2
        exit 1
    fi
    echo ">>> Built ${exe} ($(stat -c%s "${exe}") bytes)"
done
