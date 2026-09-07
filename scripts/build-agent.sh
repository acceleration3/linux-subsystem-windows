#!/usr/bin/env bash
# Cross-build the Windows guest side with mingw-w64.
#
# Outputs, under build-agent (and build-agent32 for the i686 set):
#   src/agent/lsw-guest-agent.exe                 the control-plane agent
#   _deps/vulkanloader-build/loader/vulkan-1.dll  the Khronos loader
#
# Both statically linked against the mingw runtime, so they drop onto a
# stock Win11 install with no extra DLLs.
#
# The wizard's payload-ISO step picks the agent path up automatically
# (see lsw::host::platform::AgentExePath), as does
# scripts/redeploy-agent.sh. Override either with $LSW_AGENT_EXE.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build-agent"
TOOLCHAIN="${ROOT}/cmake/toolchain-mingw-w64.cmake"

echo ">>> Configuring agent cross-build (mingw-w64) -> ${BUILD}"
cmake -S "${ROOT}" -B "${BUILD}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLSW_BUILD_MANAGER=OFF \
    -DLSW_BUILD_AGENT=ON \
    -DLSW_BUILD_TESTS=OFF

echo ">>> Building agent + Vulkan loader"
# ``vulkan`` is Vulkan-Loader's target: vulkan-1.dll. A VM with no GPU
# driver ships no loader, and without one nothing in the guest can find
# a Vulkan driver — the loader is what reads VK_DRIVER_FILES.
cmake --build "${BUILD}" --target lsw-guest-agent vulkan

# The i686 pair, in a second build tree.
#
# A 32-bit process can only load a 32-bit driver, and every D3D8 / D3D9
# title is 32-bit — so without this the guest can run modern Vulkan
# applications and none of the old games the whole exercise is for. Only
# the drivers and the loader are built here: the agent is our own
# program and nothing loads it into a 32-bit process.
BUILD32="${ROOT}/build-agent32"
TOOLCHAIN32="${ROOT}/cmake/toolchain-mingw-w64-i686.cmake"
if command -v i686-w64-mingw32-g++ >/dev/null 2>&1; then
    echo ">>> Configuring 32-bit ICD cross-build (mingw-w64 i686) -> ${BUILD32}"
    cmake -S "${ROOT}" -B "${BUILD32}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN32}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DLSW_BUILD_MANAGER=OFF \
        -DLSW_BUILD_AGENT=OFF \
        -DLSW_BUILD_TESTS=OFF
    echo ">>> Building the 32-bit Vulkan loader"
    cmake --build "${BUILD32}" --target vulkan
else
    echo "!!! i686-w64-mingw32-g++ not found; skipping the 32-bit pair." >&2
    echo "    32-bit applications in the guest will find no Vulkan driver." >&2
fi

EXE="${BUILD}/src/agent/lsw-guest-agent.exe"
if [[ ! -f "${EXE}" ]]; then
    echo "!!! Build did not produce ${EXE}" >&2
    exit 1
fi
echo ">>> Built ${EXE} ($(stat -c%s "${EXE}") bytes)"

LOADER="$(find "${BUILD}" -name vulkan-1.dll -print -quit)"
if [[ ! -f "${LOADER}" ]]; then
    echo "!!! Build did not produce vulkan-1.dll" >&2
    exit 1
fi
echo ">>> Built ${LOADER} ($(stat -c%s "${LOADER}") bytes)"

# A DLL that pulls in the mingw runtime loads on this machine and
# nowhere else, and a loader reports that as a driver it could not find.
# Cheap to check, so check.
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
    for f in "${EXE}" "${LOADER}"; do
        if x86_64-w64-mingw32-objdump -p "${f}" | grep -qE "DLL Name: (libstdc\+\+|libgcc_s|libwinpthread)"; then
            echo "!!! ${f} depends on mingw runtime DLLs; it will not load on a stock Windows" >&2
            x86_64-w64-mingw32-objdump -p "${f}" | grep "DLL Name" >&2
            exit 1
        fi
    done
fi
