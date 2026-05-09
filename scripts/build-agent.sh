#!/usr/bin/env bash
# Cross-build the Windows guest agent with mingw-w64.
#
# Output: build-agent/src/agent/lsw-guest-agent.exe (statically linked,
# drops onto a stock Win11 install with no extra DLLs needed).
#
# The manager's InstallPage will pick this up automatically when
# building the payload ISO if the path matches.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build-agent"
TOOLCHAIN="${ROOT}/cmake/toolchain-mingw-w64.cmake"

echo ">>> Configuring agent cross-build (mingw-w64) -> ${BUILD}"
cmake -S "${ROOT}" -B "${BUILD}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLSW_BUILD_SERVER=OFF \
    -DLSW_BUILD_CLIENT=OFF \
    -DLSW_BUILD_MANAGER=OFF \
    -DLSW_BUILD_AGENT=ON \
    -DLSW_BUILD_TESTS=OFF

echo ">>> Building agent"
cmake --build "${BUILD}" --target lsw-guest-agent

EXE="${BUILD}/src/agent/lsw-guest-agent.exe"
if [[ -f "${EXE}" ]]; then
    SIZE=$(stat -c%s "${EXE}")
    echo ">>> Built ${EXE} (${SIZE} bytes)"
else
    echo "!!! Build did not produce ${EXE}"
    exit 1
fi
