#!/usr/bin/env bash
# Cross-build the Windows guest side with mingw-w64.
#
# Outputs, under build-agent (and build-agent32 for the i686 loader):
#   src/agent/lsw-guest-agent.exe                        the control-plane agent
#   remotewin/source/guest/remotewin-guest.exe           the window mirror's guest half
#   _deps/vulkanloader-build/loader/vulkan-1.dll         the Khronos Vulkan loader
#
# All statically linked against the mingw runtime, so they drop onto a
# stock Win11 install with no extra DLLs. For the loader that is not a
# nicety: it is loaded inside an arbitrary application, and a missing
# libstdc++-6.dll would surface as nothing more informative than
# "no Vulkan driver".
#
# The GPU drivers are not built here. accel-virt's avgpu.sys and its
# user-mode drivers come from the host build (the av-drivers tree under
# build/, made with accel-virt's own clang-cl toolchains), and so do the
# OpenCL loaders. Only vulkan-1.dll is ours to build, because Windows
# ships no Vulkan loader and accel-virt does not package one.
#
# The wizard's payload-ISO step picks the agent path up automatically
# (see lsw::host::platform::AgentExePath), as does
# scripts/redeploy-agent.sh. Override either with $LSW_AGENT_EXE.
#
# $LSW_AGENT_BUILD and $LSW_AGENT32_BUILD put the two trees somewhere
# else (scripts/package-release.sh builds its own), and
# $LSW_REQUIRE_I686=1 makes a missing i686 compiler an error rather than
# a skipped loader: a release without the 32-bit loader is incomplete.

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
# ``vulkan`` is Vulkan-Loader's target: vulkan-1.dll. Windows ships no
# loader, and without one nothing in the guest can find a Vulkan driver
# — the loader is what reads the adapter's VulkanDriverName, which is
# how avvk.dll is found.
cmake --build "${BUILD}" --target lsw-guest-agent remotewin-guest vulkan

# The i686 loader, in a second build tree.
#
# A 32-bit process can only load a 32-bit vulkan-1.dll, from SysWOW64,
# and Windows ships none — so without this a 32-bit Vulkan application,
# or a D3D8 / D3D9 title running on DXVK's d3d9.dll, finds no avvk32.dll
# even though the driver package installed one. Only the loader is
# built here: the agent is our own program and nothing loads it into a
# 32-bit process, and remotewin-guest.exe is configured out so that
# installing this tree (cmake --install --component lsw) lays out the
# loader and nothing it did not build.
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

# Vulkan-Loader builds inside FetchContent's _deps tree, so its binary
# dir is nested under a name this repository does not choose. Located
# rather than hard-coded: the alternative is a path that goes stale the
# next time the layout moves, and fails as "did not produce" long after
# the build itself succeeded.
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

# A DLL that pulls in the mingw runtime loads on this machine and
# nowhere else, and an application reports that as a loader it could
# not find. Cheap to check, so check. objdump reads either architecture.
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
    for f in "${LOADERS[@]}"; do
        if x86_64-w64-mingw32-objdump -p "${f}" | grep -qE "DLL Name: (libstdc\+\+|libgcc_s|libwinpthread)"; then
            echo "!!! ${f} depends on mingw runtime DLLs; it will not load on a stock Windows" >&2
            x86_64-w64-mingw32-objdump -p "${f}" | grep "DLL Name" >&2
            exit 1
        fi
    done
fi
