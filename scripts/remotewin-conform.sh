#!/usr/bin/env bash
# Run remotewin's conformance harness against the LSW VM: build both
# halves, put the probe (and, with --guest, the mirror's guest half) into
# the guest over SMB, and run the harness, which launches the probe through
# the guest agent, mirrors it in-process and writes a report.
#
# Usage:
#   scripts/remotewin-conform.sh [--no-build] [--guest] [-- harness options]
#
#   --no-build   use what build/ and build-agent/ already have
#   --guest      also redeploy remotewin-guest.exe (after changing the
#                guest half); the one in use is renamed aside first
#
# Harness options, after --: --filter <regex>, --out <dir>, --tolerance <n>,
# --scenarios <json>. See build/remotewin/source/conform/remotewin-conform --help.
#
# Windows appear on the desktop while it runs; they are taken down at the end.
#
# Requires: the VM running with the agent on 127.0.0.1:5102 and the guest's
# C$ share on 127.0.0.1:5445 (user lsw, password lsw).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SMB_SHARE="${LSW_SMB_SHARE:-//127.0.0.1/C\$}"
SMB_PORT="${LSW_SMB_PORT:-5445}"
SMB_USER="${LSW_SMB_USER:-lsw%lsw}"
DO_BUILD=1
DO_GUEST=0
HARNESS_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) DO_BUILD=0 ;;
        --guest) DO_GUEST=1 ;;
        --) shift; HARNESS_ARGS=("$@"); break ;;
        -h|--help) sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ $DO_BUILD -eq 1 ]]; then
    # Named targets only: a bare build of build/ rebuilds accel-virt.
    ninja -C "$ROOT/build" remotewin remotewin-conform
    ninja -C "$ROOT/build-agent" remotewin-guest remotewin-probe
fi

# A file the guest is running cannot be overwritten, but it can be renamed.
put() {
    local local_file="$1" name="$2"
    local stamp; stamp="$(date +%s)"
    smbclient "$SMB_SHARE" -p "$SMB_PORT" -U "$SMB_USER" \
        -c "cd ProgramData/LSW; del $name.*.old; rename $name $name.$stamp.old; put $local_file $name" \
        2>&1 | grep -v -e "smb.conf" -e "NT_STATUS_OBJECT_NAME_NOT_FOUND" || true
}

put "$ROOT/build-agent/remotewin/source/probe/remotewin-probe.exe" remotewin-probe.exe
if [[ $DO_GUEST -eq 1 ]]; then
    put "$ROOT/build-agent/remotewin/source/guest/remotewin-guest.exe" remotewin-guest.exe
fi

exec "$ROOT/build/remotewin/source/conform/remotewin-conform" "${HARNESS_ARGS[@]}"
