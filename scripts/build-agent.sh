#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${LSW_AGENT_BUILD:-${ROOT}/build-agent}"
TOOLCHAIN="${ROOT}/cmake/toolchain-mingw-w64.cmake"

echo ">>> Configuring agent cross-build (mingw-w64) -> ${BUILD}"
cmake -S "${ROOT}" -B "${BUILD}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLSW_BUILD_MANAGER=OFF \
    -DLSW_BUILD_AGENT=ON \
    -DLSW_BUILD_TESTS=OFF

echo ">>> Building agent + window mirror + Vulkan loader"
cmake --build "${BUILD}" --target lsw-guest-agent remotewin-guest vulkan

LOADER_TREES=("${BUILD}")
BUILD32="${LSW_AGENT32_BUILD:-${ROOT}/build-agent32}"
TOOLCHAIN32="${ROOT}/cmake/toolchain-mingw-w64-i686.cmake"
if command -v i686-w64-mingw32-g++ >/dev/null 2>&1; then
    echo ">>> Configuring 32-bit loader cross-build (mingw-w64 i686) -> ${BUILD32}"
    cmake -S "${ROOT}" -B "${BUILD32}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN32}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DLSW_BUILD_MANAGER=OFF \
        -DLSW_BUILD_AGENT=OFF \
        -DLSW_BUILD_REMOTEWIN=OFF \
        -DLSW_BUILD_TESTS=OFF
    echo ">>> Building 32-bit Vulkan loader"
    cmake --build "${BUILD32}" --target vulkan
    LOADER_TREES+=("${BUILD32}")
elif [[ "${LSW_REQUIRE_I686:-0}" == 1 ]]; then
    echo "!!! i686-w64-mingw32-g++ not found, and the 32-bit loader is required" >&2
    exit 1
else
    echo "!!! i686-w64-mingw32-g++ not found; skipping the 32-bit loader." >&2
    echo "    32-bit Vulkan applications in the guest will find no loader." >&2
fi

EXE="${BUILD}/src/agent/lsw-guest-agent.exe"
if [[ ! -f "${EXE}" ]]; then
    echo "!!! Build did not produce ${EXE}" >&2
    exit 1
fi
echo ">>> Built ${EXE} ($(stat -c%s "${EXE}") bytes)"

MIRROR="${BUILD}/remotewin/source/guest/remotewin-guest.exe"
if [[ ! -f "${MIRROR}" ]]; then
    echo "!!! Build did not produce ${MIRROR}" >&2
    exit 1
fi
echo ">>> Built ${MIRROR} ($(stat -c%s "${MIRROR}") bytes)"

LOADERS=()
for tree in "${LOADER_TREES[@]}"; do
    found="$(find "${tree}" -name vulkan-1.dll -print -quit)"
    if [[ ! -f "${found}" ]]; then
        echo "!!! Build did not produce vulkan-1.dll under ${tree}" >&2
        exit 1
    fi
    echo ">>> Built ${found} ($(stat -c%s "${found}") bytes)"
    LOADERS+=("${found}")
done

if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
    for f in "${LOADERS[@]}"; do
        if x86_64-w64-mingw32-objdump -p "${f}" | grep -qE "DLL Name: (libstdc\+\+|libgcc_s|libwinpthread)"; then
            echo "!!! ${f} depends on mingw runtime DLLs; it will not load on a stock Windows" >&2
            x86_64-w64-mingw32-objdump -p "${f}" | grep "DLL Name" >&2
            exit 1
        fi
    done
fi
