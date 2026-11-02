#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

say "Reset VM state"
"${ROOT}/scripts/reset-vm.sh"

say "Build agent + manager + GPU"
"${ROOT}/scripts/build.sh"

AGENT_EXE="${ROOT}/build-agent/src/agent/lsw-guest-agent.exe"
if [[ ! -f "${AGENT_EXE}" ]]; then
    echo "ERROR: agent build did not produce ${AGENT_EXE}" >&2
    exit 1
fi

say "Launch manager (LSW_AGENT_EXE=${AGENT_EXE})"
LSW_LOG=info LSW_AGENT_EXE="${AGENT_EXE}" "${ROOT}/build/src/host/lsw-manager"
