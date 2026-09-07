#!/usr/bin/env bash
# Build everything: the cross-compiled Windows guest agent + the
# host-side Linux binaries.
#
# Order matters — the manager bakes the agent's path into the payload
# ISO, so the agent has to exist first.
#
# Run: ./scripts/build.sh              # agent + manager (the usual case)
#      ./scripts/build.sh --all        # also the render server
#      ./scripts/build.sh --host-only  # skip the mingw cross-build
#
# Exits non-zero on the first build failure.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
cd "${ROOT}"

BUILD_AGENT=1
HOST_TARGETS=(lsw-manager)

for arg in "$@"; do
    case "$arg" in
        --all)       HOST_TARGETS=(lsw-manager rvk-render-server) ;;
        --host-only) BUILD_AGENT=0 ;;
        -h|--help)   sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)           echo "build: unknown argument '$arg'" >&2; exit 2 ;;
    esac
done

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

if [[ "${BUILD_AGENT}" -eq 1 ]]; then
    say "Building Windows guest agent (mingw-w64)"
    "${ROOT}/scripts/build-agent.sh"
fi

say "Building host binaries (Linux, Qt 6)"
if [[ ! -d "${BUILD}" ]]; then
    cmake -S "${ROOT}" -B "${BUILD}" -G Ninja -DCMAKE_BUILD_TYPE=Debug
fi
for target in "${HOST_TARGETS[@]}"; do
    cmake --build "${BUILD}" --target "${target}"
done

say "Done."
if [[ "${BUILD_AGENT}" -eq 1 ]]; then
    echo "  agent:         ${ROOT}/build-agent/src/agent/lsw-guest-agent.exe"
fi
for target in "${HOST_TARGETS[@]}"; do
    case "$target" in
        lsw-manager)       echo "  manager:       ${BUILD}/src/host/lsw-manager" ;;
        rvk-render-server) echo "  render-server: $(find "${BUILD}" -name rvk-render-server -print -quit)" ;;
    esac
done
