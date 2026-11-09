#!/usr/bin/env bash

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STATE="${XDG_RUNTIME_DIR:-/tmp}/lsw-render"
DOMAIN="${LSW_RENDER_DOMAIN:-lsw}"
URI="${LSW_RENDER_URI:-qemu:///session}"
IDLE_SECONDS="${LSW_RENDER_VM_IDLE:-600}"
AGENT_WAIT="${LSW_RENDER_AGENT_WAIT:-300}"
SHUTDOWN_WAIT="${LSW_RENDER_SHUTDOWN_WAIT:-180}"
LOCK="${LSW_RENDER_HEAVY_LOCK:-${XDG_RUNTIME_DIR:-/tmp}/lsw-render.lock}"

mkdir -p "${STATE}"

virsh_() { virsh -c "${URI}" "$@"; }
state() { virsh_ domstate "${DOMAIN}" 2>/dev/null | head -n 1; }
running() { [[ $(state) == running ]]; }

usage() {
    cat <<'EOF'
usage: scripts/render-vm.sh up|done|down|status|idle-watch

  up          start the lsw domain if it is off and wait for the guest agent (bounded)
  done        mark the VM as just used
  down        shut the guest down gracefully, then destroy it if it does not stop
  status      print running or off, and whether this script started it
  idle-watch  stop the VM it started once it was idle for LSW_RENDER_VM_IDLE seconds (default 600)
EOF
}

case "${1:-}" in
    up)
        touch "${STATE}/idle"
        if ! running; then
            virsh_ start "${DOMAIN}" > /dev/null || exit 1
            touch "${STATE}/started"
        fi
        if python3 -c "
import sys, time
sys.path.insert(0, '${ROOT}/scripts/gpu-test')
import lsw_link
lsw_link.wait_for_agent(time.monotonic() + ${AGENT_WAIT}).close()
"; then
            echo "vm up"
            exit 0
        fi
        echo "the VM started but the guest agent did not answer within ${AGENT_WAIT} s" >&2
        exit 1
        ;;
    done)
        touch "${STATE}/idle"
        ;;
    down)
        running || { rm -f "${STATE}/started"; echo "vm already off"; exit 0; }
        virsh_ shutdown "${DOMAIN}" > /dev/null
        for _ in $(seq 1 $((SHUTDOWN_WAIT / 5))); do
            running || { rm -f "${STATE}/started"; echo "vm off"; exit 0; }
            sleep 5
        done
        virsh_ destroy "${DOMAIN}" > /dev/null
        rm -f "${STATE}/started"
        echo "vm destroyed after ${SHUTDOWN_WAIT} s without a clean shutdown"
        ;;
    status)
        running && printf 'running' || printf 'off'
        [[ -f ${STATE}/started ]] && printf ' (started by render-vm.sh)'
        printf '\n'
        ;;
    idle-watch)
        while true; do
            sleep 60
            running && [[ -f ${STATE}/started ]] || continue
            last="$(stat -c %Y "${STATE}/idle" 2>/dev/null || echo 0)"
            (( $(date +%s) - last > IDLE_SECONDS )) || continue
            flock -n "${LOCK}" "$0" down
        done
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
