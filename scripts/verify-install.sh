#!/usr/bin/env bash
# Check whether the LSW install succeeded by probing what's externally
# observable from the host:
#   * libvirt domain ``lsw`` exists and is running
#   * the qcow2 disk has been written to (>= 4 GiB used = Windows
#     unpacked, not just an empty-disk first boot)
#   * vsock(cid=100, port=5102) accepts a connection (= guest agent
#     is up and listening)
#
# Each check prints PASS / FAIL with a short note. Exit 0 if every
# check passes, 1 otherwise.

set -uo pipefail

VM_NAME="lsw"
URI="qemu:///session"
VSOCK_CID=100
VSOCK_PORT=5102
DISK="${XDG_DATA_HOME:-$HOME/.local/share}/lsw/disks/${VM_NAME}.qcow2"

passes=0
fails=0
pass() { printf '\033[1;32m  PASS\033[0m %s\n' "$*"; passes=$((passes+1)); }
fail() { printf '\033[1;31m  FAIL\033[0m %s\n' "$*"; fails=$((fails+1)); }

echo "=== libvirt domain ==="
state=$(virsh -c "${URI}" domstate "${VM_NAME}" 2>/dev/null || true)
if [[ -z "${state}" ]]; then
    fail "domain '${VM_NAME}' not defined (run the wizard first)"
elif [[ "${state}" == "running" ]]; then
    pass "domain '${VM_NAME}' is running"
else
    fail "domain '${VM_NAME}' state: ${state} (should be running)"
fi

echo ""
echo "=== qcow2 disk ==="
if [[ ! -f "${DISK}" ]]; then
    fail "disk image missing: ${DISK}"
else
    used_bytes=$(stat -c%s "${DISK}")  # apparent size; sparse so this is upper bound
    actual_blocks=$(stat -c%b "${DISK}")
    actual_bytes=$((actual_blocks * 512))
    actual_gib=$((actual_bytes / 1024 / 1024 / 1024))
    if (( actual_bytes < 4 * 1024 * 1024 * 1024 )); then
        fail "disk only ${actual_gib} GiB used — Windows hasn't unpacked yet"
    else
        pass "disk ${actual_gib} GiB used — install proceeded past unpacking"
    fi
fi

echo ""
echo "=== guest agent (vsock cid=${VSOCK_CID} port=${VSOCK_PORT}) ==="
agent_msg=$(python3 -c "
import socket, sys
s = socket.socket(socket.AF_VSOCK, socket.SOCK_STREAM)
s.settimeout(3)
try:
    s.connect((${VSOCK_CID}, ${VSOCK_PORT}))
    print('connected')
except OSError as e:
    print(f'{e.errno}:{e.strerror}')
" 2>&1)
if [[ "${agent_msg}" == "connected" ]]; then
    pass "agent reachable — install pipeline complete"
else
    fail "vsock connect failed: ${agent_msg}"
    fail "  next step: VNC into the VM and check the in-guest log files"
    fail "  (see in-guest checklist printed below)"
fi

echo ""
echo "=== summary ==="
echo "${passes} passed, ${fails} failed"

if (( fails > 0 )); then
    cat <<'EOF'

=== in-VM verification (open a VNC viewer to the running VM and run
    these in cmd.exe — Win+R "cmd") ===

  1. Did the post-OOBE promote script run?
     type C:\ProgramData\LSW\promote.log

     It logs every step of SetupComplete: creating the wizard account,
     promoting it to Administrators, and registering the agent's
     scheduled task. An empty or missing file means SetupComplete.cmd
     never ran.

  2. Was the agent .exe copied off the payload CD?
     dir C:\ProgramData\LSW\lsw-agent.exe

     Copied during the specialize pass. Missing means the payload ISO
     was not mounted, or did not contain lsw-agent.exe — rebuild it
     with scripts/build-agent.sh before re-running the wizard.

  3. Did the specialize pass and the finalize scripts finish?
     type C:\ProgramData\LSW\installed.txt
     type C:\ProgramData\LSW\lsw-ready.flag

  3b. Is the desktop actually up?
     tasklist /fi "imagename eq explorer.exe"

     The agent deliberately holds :5102 closed until Explorer and the
     taskbar exist, so a running task with no listener usually means
     the session is still on "Preparing Windows" rather than that
     anything is broken. Check the agent log for the "waiting for the
     interactive shell" line.

  4. Is the agent's scheduled task registered and running?
     schtasks /query /tn lsw-agent /v /fo list

     The task runs the agent AS THE WIZARD USER in their interactive
     session (InteractiveToken + HighestAvailable), which is what makes
     window capture and app launch native.

       Last Result 0, task Running  -> agent is up but vsock is not
                                       reaching the host
       Registered but never run     -> no interactive logon yet; check
                                       that AutoLogon is configured
       "cannot find the file"       -> LswPromote.ps1 did not get that
                                       far; see promote.log above

  5. Connect the VNC viewer:
     virt-viewer -c qemu:///session lsw
EOF
    exit 1
fi
exit 0
