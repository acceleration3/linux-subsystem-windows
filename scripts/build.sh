#!/usr/bin/env bash
# Build everything: the cross-compiled Windows guest agent + the
# host-side Linux manager. Order matters — the manager bakes the
# agent's path into the payload ISO, so the agent has to exist first.
#
# Run: ./scripts/build.sh
# Exits non-zero on the first build failure.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

say "Building Windows guest agent (mingw-w64)"
"${ROOT}/scripts/build-agent.sh"

say "Building host manager (Linux, Qt 6)"
if [[ ! -d "${ROOT}/build" ]]; then
    cmake -S "${ROOT}" -B "${ROOT}/build" -G Ninja -DCMAKE_BUILD_TYPE=Debug
fi
cmake --build "${ROOT}/build" --target lsw-manager

say "Done."
echo "  agent:   ${ROOT}/build-agent/src/agent/lsw-guest-agent.exe"
echo "  manager: ${ROOT}/build/src/manager/lsw-manager"
