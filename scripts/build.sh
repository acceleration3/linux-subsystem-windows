#!/usr/bin/env bash
# Build everything: the cross-compiled Windows guest agent, the
# host-side Linux binaries, and accel-virt — the GPU's patched QEMU and
# host libraries, and the guest driver package (build/av-drivers).
#
# Order matters — the manager bakes the agent's path into the payload
# ISO, so the agent has to exist first.
#
# Run: ./scripts/build.sh              # agent + manager + GPU (the usual case)
#      ./scripts/build.sh --no-gpu     # skip accel-virt (manager iteration)
#      ./scripts/build.sh --host-only  # skip the mingw cross-build
#
# The first GPU build fetches what accel-virt pins and does not commit
# (the Windows kit subset, the Khronos registries, QEMU's tarball) and
# compiles QEMU, DXVK and vkd3d-proton, so it takes a while; later ones
# are incremental.
#
# Exits non-zero on the first build failure.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
cd "${ROOT}"

BUILD_AGENT=1
HOST_TARGETS=(lsw-manager remotewin accel_virt)

for arg in "$@"; do
    case "$arg" in
        --no-gpu)    HOST_TARGETS=(lsw-manager remotewin) ;;
        --host-only) BUILD_AGENT=0 ;;
        -h|--help)   sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)           echo "build: unknown argument '$arg'" >&2; exit 2 ;;
    esac
done

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

# Submodules: remotewin and accel-virt here, and accel-virt's own dxvk
# and vkd3d-proton, which both of its halves compile against. Only the
# ones that are missing — an unconditional ``submodule update`` would
# move a submodule someone is working in back to the recorded commit.
missing=()
for sub in external/remotewin external/accel-virt; do
    [[ -f "${sub}/CMakeLists.txt" ]] || missing+=("${sub}")
done
if (( ${#missing[@]} )); then
    say "Initialising submodules: ${missing[*]}"
    git submodule update --init --recursive -- "${missing[@]}"
fi
AV="${ROOT}/external/accel-virt"
if [[ ! -f "${AV}/third_party/dxvk/meson.build" || ! -f "${AV}/third_party/vkd3d-proton/meson.build" ]]; then
    say "Initialising accel-virt's dxvk and vkd3d-proton"
    git -C "${AV}" submodule update --init --recursive third_party/dxvk third_party/vkd3d-proton
fi

if [[ "${BUILD_AGENT}" -eq 1 ]]; then
    say "Building Windows guest agent (mingw-w64)"
    "${ROOT}/scripts/build-agent.sh"
fi

say "Building host binaries (Linux, Qt 6)"
if [[ ! -d "${BUILD}" ]]; then
    cmake -S "${ROOT}" -B "${BUILD}" -G Ninja -DCMAKE_BUILD_TYPE=Debug
fi
# A tree configured with -DLSW_BUILD_ACCEL_VIRT=OFF has no accel_virt
# target; asking for it would fail the build over a choice made on
# purpose.
if grep -q '^LSW_BUILD_ACCEL_VIRT:BOOL=OFF' "${BUILD}/CMakeCache.txt" 2>/dev/null; then
    HOST_TARGETS=("${HOST_TARGETS[@]/accel_virt}")
fi
# The same for the window mirror's host half, -DLSW_BUILD_REMOTEWIN=OFF.
# Nothing the manager links needs it, so building only lsw-manager left
# every launch without a mirror.
if grep -q '^LSW_BUILD_REMOTEWIN:BOOL=OFF' "${BUILD}/CMakeCache.txt" 2>/dev/null; then
    HOST_TARGETS=("${HOST_TARGETS[@]/remotewin}")
fi
for target in "${HOST_TARGETS[@]}"; do
    [[ -n "${target}" ]] || continue
    [[ "${target}" == accel_virt ]] && say "Building accel-virt: driver package, QEMU, host Direct3D"
    cmake --build "${BUILD}" --target "${target}"
done

say "Done."
if [[ "${BUILD_AGENT}" -eq 1 ]]; then
    echo "  agent:         ${ROOT}/build-agent/src/agent/lsw-guest-agent.exe"
fi
for target in "${HOST_TARGETS[@]}"; do
    case "$target" in
        lsw-manager)
            echo "  manager:       ${BUILD}/src/host/lsw-manager" ;;
        remotewin)
            echo "  mirror:        ${BUILD}/remotewin/source/host/remotewin" ;;
        accel_virt)
            echo "  av-drivers:    ${BUILD}/av-drivers"
            echo "  qemu:          ${BUILD}/accel-virt/qemu/qemu/qemu-system-x86_64"
            echo "  executor:      ${BUILD}/accel-virt/qemu/libav_executor.so"
            echo "  host d3d:      ${BUILD}/accel-virt/d3d/lib" ;;
    esac
done
