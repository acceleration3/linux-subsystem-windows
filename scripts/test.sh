#!/usr/bin/env bash
# Manual integration test: wipe any prior VM, rebuild agent + manager +
# accel-virt, launch the manager with LSW_AGENT_EXE pointing at the
# freshly-built guest agent so it gets bundled into the payload ISO. The
# GPU's pieces — build/av-drivers for the payload, and the patched QEMU
# under build/accel-virt — the manager finds beside its own executable.
#
# Click through the wizard yourself; the install is unattended after
# that and the wizard auto-advances to "Open manager" once the guest
# agent connects back over vsock.
#
# Run: ./scripts/test.sh

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
