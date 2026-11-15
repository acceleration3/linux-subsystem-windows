#!/usr/bin/env bash

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
URI="qemu:///session"
USER_DOMAIN="lsw"
USER_DISK="${XDG_DATA_HOME:-$HOME/.local/share}/lsw/disks/${USER_DOMAIN}.qcow2"
USER_NVRAM="${XDG_DATA_HOME:-$HOME/.local/share}/lsw/nvram/${USER_DOMAIN}.fd"
USER_INI="${XDG_CONFIG_HOME:-$HOME/.config}/lsw/manager.ini"

WORK="${LSW_UPGRADE_WORK:-$HOME/.cache/lsw-upgrade-test}"
BASE_REF="master"
BRANCH_REF="HEAD"
DOMAIN="lsw-upgrade-test"
PORT=5195
JOBS=4
CPUS=""
PAUSE_VM=0
KEEP=0
X86=0
BUILD_ONLY=0
MAKE_BASELINE=0
BASELINE_SRC=""
RESTORE_USER=0
BOOT_TIMEOUT=900
UPDATE_TIMEOUT=1200
PROBE_TIMEOUT=600

usage() {
    cat <<'EOF'
usage: scripts/test-upgrade.sh [options]
       scripts/test-upgrade.sh --make-baseline [SOURCE.qcow2] [--pause-vm]
       scripts/test-upgrade.sh --restore-user

Proves that this branch updates an install made by master: host binaries and
the guest. It never performs an update step itself. It starts from what
master's own manager leaves behind, starts the BRANCH's manager on the same
install, calls the branch's "Update LSW in Windows" (the manager's MCP tool
guest_update), and then checks outcomes only. It runs on this machine with its
GPU; there is no CI for it.

First time (once, with the real VM shut down):
  scripts/test-upgrade.sh --make-baseline --pause-vm
    Copies the real VM's disk (~/.local/share/lsw/disks/lsw.qcow2, or SOURCE),
    its NVRAM, its libvirt definition and manager.ini into WORK/baseline.
    Re-make the baseline whenever the real guest should change; remove
    WORK/baseline first.

Every time:
  scripts/test-upgrade.sh --pause-vm
    1. Builds master (--base) and the branch (--branch) with scripts/build.sh in
       worktrees under WORK/trees/<commit>. A tree that was built is reused, so
       master is rebuilt only when it moves.
    2. Puts a fresh qcow2 overlay on the baseline and defines a test domain
       (--domain) with its own XDG config/data/cache dirs and MCP port (--port).
    3. Starts master's manager headless on the overlay (QT_QPA_PLATFORM=offscreen),
       which starts the VM, runs master's guest_update with force, and records
       the before state (versions, gpu_status).
    4. Shuts the VM and master's manager down, starts the branch's manager on the
       same dirs (the host upgrade), which starts the VM from the branch's QEMU,
       executor and drivers.
    5. Runs the branch's guest_update (not forced first; forced only if it has
       nothing to ship) and waits for it.
    6. Checks, printing PASS or FAIL for each:
         the domain runs the branch's emitter and executor
         the guest reports the branch's agent, release and driver versions
         every adapter has problem_code 0 and the agent is online
         the driver log shows the capset accepted
         avd3d11probe avd3d12probe avd3d9probe avglprobe avclprobe pass in
           session 1 (--x86 adds the 32-bit ones)
       then reboots the guest and checks everything again, then runs the
       branch's guest_update without force and checks that nothing changed.
    Exit status: 0 when every check passed, 1 when a check failed, 2 when the
    test itself could not run. The log of every step is WORK/runs/<time>/log.

The host cannot run two Windows VMs, so the script refuses to run while the
real domain 'lsw' runs or any lsw-manager is up. --pause-vm stops the real
manager and shuts the real VM down through virsh first, and always restarts
both as they were (same command line, environment, directory and log file),
also when the test fails or is interrupted. If the script is killed hard,
--restore-user does that restore from the state it saved.

Options:
  --make-baseline [SRC]  create WORK/baseline from the real VM, then exit
  --pause-vm             pause the real VM and manager for the run, then restore
  --restore-user         restore the real VM and manager saved by an earlier run
  --base REF             the install to upgrade from (default master)
  --branch REF           the code under test (default HEAD of this checkout)
  --work DIR             scratch directory (default ~/.cache/lsw-upgrade-test,
                         or $LSW_UPGRADE_WORK)
  --domain NAME          test libvirt domain (default lsw-upgrade-test)
  --port N               test manager's MCP port (default 5195)
  --jobs N               build parallelism, also the CPU count the build is
                         limited to (default 4)
  --cpus LIST            CPUs the builds may use (default: the last JOBS of ours)
  --x86                  also run the 32-bit probes
  --keep                 keep the overlay, test data dirs and shared folder
  --build-only           build both trees and exit
  -h, --help             this text

A ref that lacks LSW_DOMAIN_NAME and LSW_MCP_PORT (the test's isolation from the
real VM) gets those two small edits in its worktree before it is built.
EOF
}

START=$SECONDS
LOG=""
RUN=""
passes=0
fails=0
USER_PAUSED=0
MGR_PID=""
RESULT_CODE=0

stamp() { date +%T; }

log() {
    local line
    line="$(stamp) $*"
    if [[ -n "$LOG" ]]; then
        printf '%s\n' "$line" >>"$LOG"
    fi
}

say() {
    printf '\033[1;36m==>\033[0m %s\n' "$*"
    log "==> $*"
}

note() {
    printf '    %s\n' "$*"
    log "    $*"
}

pass() {
    printf '\033[1;32m  PASS\033[0m %s\n' "$*"
    log "  PASS $*"
    passes=$((passes + 1))
}

fail() {
    printf '\033[1;31m  FAIL\033[0m %s\n' "$*"
    log "  FAIL $*"
    fails=$((fails + 1))
}

die() {
    printf '\033[1;31mtest-upgrade: %s\033[0m\n' "$*" >&2
    log "ERROR $*"
    RESULT_CODE=2
    exit 2
}

need() {
    local tool
    for tool in "$@"; do
        command -v "$tool" >/dev/null 2>&1 || die "$tool is not installed"
    done
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --make-baseline)
            MAKE_BASELINE=1
            if [[ $# -gt 1 && "$2" != --* ]]; then
                BASELINE_SRC="$2"
                shift
            fi
            shift ;;
        --pause-vm) PAUSE_VM=1; shift ;;
        --restore-user) RESTORE_USER=1; shift ;;
        --base) BASE_REF="${2:?--base needs a ref}"; shift 2 ;;
        --branch) BRANCH_REF="${2:?--branch needs a ref}"; shift 2 ;;
        --work) WORK="${2:?--work needs a directory}"; shift 2 ;;
        --domain) DOMAIN="${2:?--domain needs a name}"; shift 2 ;;
        --port) PORT="${2:?--port needs a number}"; shift 2 ;;
        --jobs) JOBS="${2:?--jobs needs a number}"; shift 2 ;;
        --cpus) CPUS="${2:?--cpus needs a list}"; shift 2 ;;
        --x86) X86=1; shift ;;
        --keep) KEEP=1; shift ;;
        --build-only) BUILD_ONLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "test-upgrade: unknown argument '$1' (see --help)" >&2; exit 2 ;;
    esac
done

[[ "$DOMAIN" != "$USER_DOMAIN" ]] || die "the test domain must not be the real one ('$USER_DOMAIN')"
[[ "$PORT" =~ ^[0-9]+$ && "$PORT" != 5105 ]] || die "--port must be a number other than the real manager's 5105"
[[ "$JOBS" =~ ^[0-9]+$ && "$JOBS" -ge 1 ]] || die "--jobs must be a positive number"
WORK="$(mkdir -p "$WORK" && cd "$WORK" && pwd)"
MCP_URL="http://127.0.0.1:${PORT}/"
STATE="${WORK}/user-state"
BASELINE="${WORK}/baseline"
TREES="${WORK}/trees"

virshq() { virsh -c "$URI" "$@"; }

domstate() { virshq domstate "$1" 2>/dev/null | head -n1 | tr -d '[:space:]'; }

wait_until() {
    local limit="$1" what="$2" end
    shift 2
    end=$((SECONDS + limit))
    while ((SECONDS < end)); do
        if "$@" >/dev/null 2>&1; then
            return 0
        fi
        sleep 5
    done
    log "timed out after ${limit}s waiting for ${what}"
    return 1
}

mcp() {
    local empty='{}'
    python3 -I - "$MCP_URL" "$1" "${2:-$empty}" "${3:-60}" <<'PY'
import json, sys, urllib.request
url, name, args, limit = sys.argv[1:5]
body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
        "params": {"name": name, "arguments": json.loads(args)}}
request = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
try:
    reply = json.load(urllib.request.urlopen(request, timeout=float(limit)))
except Exception as exc:
    print(exc, file=sys.stderr)
    sys.exit(2)
if "error" in reply:
    print(reply["error"], file=sys.stderr)
    sys.exit(1)
result = reply.get("result", {})
print("\n".join(c.get("text", "") for c in result.get("content", []) if c.get("type") == "text"))
sys.exit(1 if result.get("isError") else 0)
PY
}

rpc() {
    local empty='{}'
    mcp rpc "$(jq -cn --arg cmd "$1" --argjson params "${2:-$empty}" '{cmd:$cmd,params:$params}')" "${3:-60}"
}

sysinfo() { rpc system_info; }

agent_online() { sysinfo >/dev/null 2>&1; }

mcp_up() {
    curl -s -m 5 -X POST "$MCP_URL" -H 'Content-Type: application/json' \
        --data '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | grep -q '"tools"'
}

user_domain_running() { [[ "$(domstate "$USER_DOMAIN")" == running ]]; }

test_domain_running() { [[ "$(domstate "$DOMAIN")" == running ]]; }

agent_restarted() {
    local now
    now="$(info_field agent_started 2>/dev/null)"
    [[ -n "$now" && "$now" != "$1" ]]
}

gpu_status() { rpc gpu_status; }

info_field() { sysinfo 2>/dev/null | jq -r ".$1 // empty"; }

last_cpus() {
    local list
    list="$(taskset -pc $$ | sed 's/.*: //')"
    python3 -I - "$list" "$1" <<'PY'
import sys
cpus = []
for part in sys.argv[1].split(","):
    lo, _, hi = part.partition("-")
    cpus.extend(range(int(lo), int(hi or lo) + 1))
print(",".join(str(c) for c in cpus[-int(sys.argv[2]):]))
PY
}

save_user_manager() {
    local pid="$1" dir="${STATE}/$1"
    mkdir -p "$dir"
    tr '\0' '\n' <"/proc/${pid}/cmdline" >"${dir}/argv"
    cp "/proc/${pid}/environ" "${dir}/environ"
    readlink "/proc/${pid}/cwd" >"${dir}/cwd"
    readlink "/proc/${pid}/exe" >"${dir}/exe"
    readlink "/proc/${pid}/fd/1" >"${dir}/stdout"
    readlink "/proc/${pid}/fd/2" >"${dir}/stderr"
}

stop_pid() {
    local pid="$1" i
    kill -TERM "$pid" 2>/dev/null || return 0
    for ((i = 0; i < 40; i++)); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.5
    done
    kill -KILL "$pid" 2>/dev/null || true
    sleep 1
}

shutdown_domain() {
    local name="$1" i
    [[ "$(domstate "$name")" == running ]] || return 0
    virshq shutdown "$name" >>"${LOG:-/dev/null}" 2>&1 || true
    for ((i = 0; i < 60; i++)); do
        [[ "$(domstate "$name")" == running ]] || return 0
        sleep 4
    done
    log "domain ${name} did not shut down in 240 s; destroying it"
    virshq destroy "$name" >>"${LOG:-/dev/null}" 2>&1 || true
    sleep 2
}

pause_user() {
    local pid
    mkdir -p "$STATE"
    if [[ -n "$(ls -A "$STATE" 2>/dev/null)" ]]; then
        die "${STATE} holds a saved real manager already; run --restore-user first"
    fi
    echo "$(domstate "$USER_DOMAIN")" >"${STATE}/domain-was"
    for pid in $(pgrep -u "$(id -u)" -x lsw-manager); do
        save_user_manager "$pid"
    done
    USER_PAUSED=1
    say "Pausing the real VM and manager"
    for pid in $(pgrep -u "$(id -u)" -x lsw-manager); do
        note "stopping lsw-manager ${pid}"
        stop_pid "$pid"
    done
    note "shutting the domain '${USER_DOMAIN}' down ($(domstate "$USER_DOMAIN"))"
    shutdown_domain "$USER_DOMAIN"
    [[ "$(domstate "$USER_DOMAIN")" != running ]] || die "the real VM would not stop"
    pgrep -u "$(id -u)" -x lsw-manager >/dev/null && die "an lsw-manager is still running"
    return 0
}

restore_user() {
    local dir was started=0
    [[ -d "$STATE" ]] || return 0
    was="$(cat "${STATE}/domain-was" 2>/dev/null || true)"
    for dir in "$STATE"/*/; do
        [[ -f "${dir}argv" ]] || continue
        log "restoring lsw-manager from ${dir}"
        python3 -I - "$dir" <<'PY' >>"${LOG:-/dev/null}" 2>&1
import os, subprocess, sys
d = sys.argv[1]
def text(name):
    return open(os.path.join(d, name)).read().rstrip("\n")
argv = text("argv").split("\n")
env = dict(item.split("=", 1) for item in open(os.path.join(d, "environ"), "rb").read().decode(errors="replace").split("\0") if "=" in item)
def sink(name):
    target = text(name)
    return open(target, "ab") if os.path.isfile(target) else subprocess.DEVNULL
subprocess.Popen(argv, executable=text("exe"), cwd=text("cwd"), env=env, stdin=subprocess.DEVNULL,
                 stdout=sink("stdout"), stderr=sink("stderr"), start_new_session=True)
print("started", argv)
PY
        started=1
    done
    if [[ "$started" -eq 1 ]]; then
        wait_until 180 "the real VM" user_domain_running || true
    elif [[ "$was" == running ]]; then
        virshq start "$USER_DOMAIN" >>"${LOG:-/dev/null}" 2>&1 || true
    fi
    note "the real domain '${USER_DOMAIN}' is $(domstate "$USER_DOMAIN") (it was ${was:-unknown} before)"
    rm -rf "$STATE"
    USER_PAUSED=0
}

check_free() {
    if [[ "$PAUSE_VM" -eq 0 ]]; then
        [[ "$(domstate "$USER_DOMAIN")" != running ]] \
            || die "the real VM '${USER_DOMAIN}' is running and the host cannot run two Windows VMs; use --pause-vm"
        pgrep -u "$(id -u)" -x lsw-manager >/dev/null \
            && die "an lsw-manager is running (it would take the test manager's place); use --pause-vm"
    else
        pause_user
    fi
    return 0
}

tracked_in() {
    [[ -n "$(git -C "$1" ls-tree HEAD "$2" 2>/dev/null)" ]]
}

isolate_tree() {
    local dir="$1"
    local vm_name="${dir}/src/host/include/lsw/host/config/vm_name.hpp"
    local app="${dir}/src/host/src/app/app.cpp"
    if ! grep -q LSW_DOMAIN_NAME "$vm_name"; then
        cat >"$vm_name" <<'EOF'
#pragma once

#include <cstdlib>

namespace lsw::host::config {

inline const char* const kVmName =
    std::getenv("LSW_DOMAIN_NAME") != nullptr && *std::getenv("LSW_DOMAIN_NAME") != '\0'
        ? std::getenv("LSW_DOMAIN_NAME")
        : "lsw";

}
EOF
        note "added LSW_DOMAIN_NAME to ${dir##*/}"
    fi
    if ! grep -q LSW_MCP_PORT "$app"; then
        sed -i 's|^    (void)mcp_server_->listen();$|    const int port = qEnvironmentVariableIntValue("LSW_MCP_PORT");\n    (void)mcp_server_->listen(port > 0 ? static_cast<std::uint16_t>(port) : std::uint16_t{5105});|' "$app"
        grep -q LSW_MCP_PORT "$app" || die "${app} has no mcp_server_->listen() to give LSW_MCP_PORT to"
        grep -q '#include <cstdint>' "$app" || sed -i '0,/^#include <string>$/s||#include <cstdint>\n#include <string>|' "$app"
        note "added LSW_MCP_PORT to ${dir##*/}"
    fi
}

clone_submodule() {
    local dir="$1" name="$2" source="$3" pin
    pin="$(git -C "$dir" ls-tree HEAD "external/${name}" | awk '{print $3}')"
    [[ -n "$pin" ]] || die "${dir} pins no external/${name}"
    if [[ ! -e "${dir}/external/${name}/.git" ]]; then
        git clone -q --shared --no-checkout "${SUBGIT}/${name}" "${dir}/external/${name}" >>"$LOG" 2>&1 \
            || die "could not clone ${SUBGIT}/${name}"
    fi
    git -C "${dir}/external/${name}" checkout -q --detach "$pin" >>"$LOG" 2>&1 \
        || die "external/${name} ${pin:0:12} is not in ${SUBGIT}/${name}"
}

fill_accel_virt() {
    local dir="$1" av="$1/external/accel-virt" sub pin
    for sub in dxvk vkd3d-proton; do
        if [[ ! -f "${av}/third_party/${sub}/meson.build" ]]; then
            pin="$(git -C "$av" ls-tree HEAD "third_party/${sub}" | awk '{print $3}')"
            git clone -q --shared --no-checkout "${SUBGIT}/accel-virt/modules/third_party/${sub}" "${av}/third_party/${sub}" >>"$LOG" 2>&1 \
                || die "could not clone ${sub}"
            git -C "${av}/third_party/${sub}" checkout -q --detach "$pin" >>"$LOG" 2>&1 \
                || die "${sub} ${pin:0:12} is not in ${SUBGIT}"
        fi
    done
    local have="${MAIN}/external/accel-virt/third_party" entry name
    for entry in "$have"/* "$have"/.cache; do
        [[ -e "$entry" ]] || continue
        name="${entry##*/}"
        tracked_in "$av" "third_party/${name}" && continue
        [[ -e "${av}/third_party/${name}" ]] && continue
        cp -a --reflink=auto "$entry" "${av}/third_party/${name}"
    done
}

prepare_tree() {
    local ref="$1" label="$2" sha dir rc
    sha="$(git -C "$ROOT" rev-parse --verify "${ref}^{commit}" 2>/dev/null)" || die "${label} ref '${ref}' is not a commit"
    dir="${TREES}/${sha:0:12}"
    TREE_SHA="$sha"
    TREE="$dir"
    if [[ -f "${dir}/.built" ]]; then
        say "${label}: ${ref} (${sha:0:12}) is built already"
        return 0
    fi
    say "${label}: preparing ${ref} (${sha:0:12})"
    mkdir -p "$TREES"
    if [[ ! -e "${dir}/.git" ]]; then
        git -C "$ROOT" worktree add -q --detach "$dir" "$sha" >>"$LOG" 2>&1 || die "git worktree add failed for ${sha}"
    fi
    clone_submodule "$dir" accel-virt "$SUBGIT"
    clone_submodule "$dir" remotewin "$SUBGIT"
    fill_accel_virt "$dir"
    isolate_tree "$dir"
    say "${label}: building ${sha:0:12} with scripts/build.sh (-j${JOBS}, CPUs ${CPUS}); this takes a long time the first time"
    rc="$(env JOBS="$JOBS" CMAKE_BUILD_PARALLEL_LEVEL="$JOBS" MAKEFLAGS="-j${JOBS}" \
        CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache \
        bash -c 'cd "$1" && taskset -c "$2" ./scripts/build.sh' _ "$dir" "$CPUS" >>"${WORK}/build-${sha:0:12}.log" 2>&1; echo $?)"
    [[ "$rc" == 0 ]] || die "scripts/build.sh failed for ${sha:0:12}; see ${WORK}/build-${sha:0:12}.log"
    local needed
    for needed in build/src/host/lsw-manager build-agent/src/agent/lsw-guest-agent.exe \
        build/accel-virt/qemu/qemu/qemu-system-x86_64 build/accel-virt/qemu/libav_executor.so \
        build/av-drivers/package/avgpu.inf build/accel-virt/d3d/lib build/accel-virt/drivers/guest/avd3d11probe.exe; do
        [[ -e "${dir}/${needed}" ]] || die "${label} build left no ${needed}"
    done
    touch "${dir}/.built"
}

manager_env() {
    local tree="$1" var
    local unset_args=()
    for var in $(compgen -e | grep '^LSW_'); do
        unset_args+=(-u "$var")
    done
    MGR_ENV=(env "${unset_args[@]}"
        QT_QPA_PLATFORM=offscreen
        XDG_CONFIG_HOME="${RUN}/xdg/config" XDG_DATA_HOME="${RUN}/xdg/data" XDG_CACHE_HOME="${RUN}/xdg/cache"
        LSW_DOMAIN_NAME="$DOMAIN" LSW_MCP_PORT="$PORT" LSW_LOG=info
        LSW_AV_QEMU_BUILD="${tree}/build/accel-virt/qemu"
        LSW_AV_D3D_PATH="${tree}/build/accel-virt/d3d/lib"
        LSW_AV_DRIVERS="${tree}/build/av-drivers"
        LSW_AGENT_EXE="${tree}/build-agent/src/agent/lsw-guest-agent.exe"
        LSW_REMOTEWIN_GUEST="${tree}/build-agent/remotewin/source/guest/remotewin-guest.exe"
        LSW_REMOTEWIN="${tree}/build/remotewin/source/host/remotewin"
        LSW_GUEST_UPDATE_SCRIPT="${tree}/build/guest-update/lsw-guest-update.ps1"
        LSW_GUEST_VIRTIOFS="${tree}/build/guest-virtiofs"
        LSW_VULKAN_LOADER="${tree}/build-agent/_deps/vulkanloader-build/loader/vulkan-1.dll"
        LSW_VULKAN_LOADER_X86="${tree}/build-agent32/_deps/vulkanloader-build/loader/vulkan-1.dll"
        LSW_USB_ACCESS="${tree}/packaging/lsw-usb-access")
}

start_manager() {
    local label="$1" tree="$2" mlog="${RUN}/manager-$1.log"
    manager_env "$tree"
    say "Starting the ${label} manager (${tree##*/}) on port ${PORT}"
    "${MGR_ENV[@]}" "${tree}/build/src/host/lsw-manager" >>"$mlog" 2>&1 &
    MGR_PID=$!
    note "pid ${MGR_PID}, log ${mlog}"
    wait_until 120 "the ${label} manager's MCP server" mcp_up \
        || die "the ${label} manager did not open its MCP server on port ${PORT}; see ${mlog}"
    grep -E 'artifact: ' "$mlog" | sed 's/^/    /' | tee -a "$LOG" >/dev/null
    if grep -E 'artifact: +(qemu|executor|host d3d|driver tree|guest agent) +\(not found\)' "$mlog" >/dev/null; then
        die "the ${label} manager did not find all of its artifacts; see ${mlog}"
    fi
}

stop_manager() {
    if [[ -n "$MGR_PID" ]]; then
        stop_pid "$MGR_PID"
        wait "$MGR_PID" 2>/dev/null || true
        MGR_PID=""
    fi
}

seed_domain_xml() {
    local tree="$1" out="$2"
    sed -e "0,/<name>[^<]*<\/name>/s||<name>${DOMAIN}</name>|" \
        -e '/<uuid>/d' \
        -e "s|<emulator>[^<]*</emulator>|<emulator>${tree}/build/accel-virt/qemu/qemu/qemu-system-x86_64</emulator>|" \
        -e "s|<source file='[^']*qcow2'|<source file='${RUN}/xdg/data/lsw/disks/${DOMAIN}.qcow2'|" \
        -e "s|\(<nvram [^>]*>\)[^<]*</nvram>|\1${RUN}/xdg/data/lsw/nvram/${DOMAIN}.fd</nvram>|" \
        "${BASELINE}/domain.xml" >"$out"
}

seed_manager_ini() {
    python3 -I - "${BASELINE}/manager.ini" "${RUN}/xdg/config/lsw/manager.ini" "${RUN}/share/Games" <<'PY'
import configparser, sys
src, dst, share = sys.argv[1:4]
ini = configparser.RawConfigParser(interpolation=None)
ini.optionxform = str
ini.read(src)
def put(section, key, value):
    if not ini.has_section(section):
        ini.add_section(section)
    ini.set(section, key, value)
put("files", "host_shares", share + "\\x1eZ:\\x1erw")
put("files", "bookmark_dolphin", "false")
put("files", "bookmark_gtk", "false")
put("desktop", "publish", "false")
put("desktop", "mime", "false")
put("desktop", "share_clipboard", "false")
put("host", "close_to_tray", "false")
put("guest", "autostart", "true")
put("setup", "wizard_complete", "true")
with open(dst, "w") as out:
    ini.write(out, space_around_delimiters=False)
PY
}

prepare_run() {
    RUN="${WORK}/runs/$(date +%Y%m%d-%H%M%S)"
    mkdir -p "${RUN}/xdg/config/lsw" "${RUN}/xdg/data/lsw/disks" "${RUN}/xdg/data/lsw/nvram" "${RUN}/xdg/cache" \
        "${RUN}/share/Games/_upgrade"
    LOG="${RUN}/log"
    : >"$LOG"
    ln -sfn "$RUN" "${WORK}/runs/latest"
    seed_manager_ini
    cp --reflink=auto "${BASELINE}/nvram.fd" "${RUN}/xdg/data/lsw/nvram/${DOMAIN}.fd"
    qemu-img create -q -f qcow2 -b "${BASELINE}/disk.qcow2" -F qcow2 "${RUN}/xdg/data/lsw/disks/${DOMAIN}.qcow2" \
        >>"$LOG" 2>&1 || die "qemu-img could not make the overlay"
}

define_domain() {
    virshq destroy "$DOMAIN" >/dev/null 2>&1 || true
    virshq undefine --nvram "$DOMAIN" >/dev/null 2>&1 || true
    seed_domain_xml "$1" "${RUN}/domain.xml"
    virshq define "${RUN}/domain.xml" >>"$LOG" 2>&1 || die "virsh could not define the test domain from ${RUN}/domain.xml"
}

cleanup() {
    local code=$?
    trap - EXIT INT TERM
    [[ "$RESULT_CODE" -ne 0 ]] && code="$RESULT_CODE"
    if [[ "$MAKE_BASELINE" -eq 0 && "$RESTORE_USER" -eq 0 ]]; then
        stop_manager
        if [[ -n "$RUN" ]]; then
            shutdown_domain "$DOMAIN"
            virshq destroy "$DOMAIN" >/dev/null 2>&1 || true
            virshq undefine --nvram "$DOMAIN" >/dev/null 2>&1 || true
            if [[ "$KEEP" -eq 0 ]]; then
                rm -rf "${RUN}/xdg/data/lsw/disks" "${RUN}/xdg/data/lsw/nvram" "${RUN}/share"
            fi
        fi
    fi
    if [[ "$USER_PAUSED" -eq 1 ]]; then
        say "Restoring the real VM and manager"
        restore_user
    fi
    if [[ -n "$LOG" ]]; then
        echo "log: ${LOG}"
    fi
    exit "$code"
}

make_baseline() {
    local src="${BASELINE_SRC:-$USER_DISK}"
    [[ -f "$src" ]] || die "no disk at ${src}"
    [[ ! -e "$BASELINE" ]] || die "${BASELINE} exists; remove it to make a new baseline"
    check_free
    mkdir -p "${BASELINE}.partial"
    say "Copying ${src} to ${BASELINE}"
    cp --reflink=auto "$src" "${BASELINE}.partial/disk.qcow2" || die "copying the disk failed"
    cp --reflink=auto "$USER_NVRAM" "${BASELINE}.partial/nvram.fd" || die "copying ${USER_NVRAM} failed"
    cp "$USER_INI" "${BASELINE}.partial/manager.ini" || die "copying ${USER_INI} failed"
    virshq dumpxml --inactive "$USER_DOMAIN" >"${BASELINE}.partial/domain.xml" || die "virsh could not dump the domain '${USER_DOMAIN}'"
    qemu-img info "${BASELINE}.partial/disk.qcow2" >/dev/null || die "the copied disk is not a qcow2 image"
    mv "${BASELINE}.partial" "$BASELINE"
    say "Baseline ready in ${BASELINE}"
    du -sh "${BASELINE}/disk.qcow2"
}

booted_checkpoint() {
    AVGPU_LOG="${RUN}/xdg/data/lsw/logs/vm/avgpu.log"
    AVGPU_MARK="$(stat -c%s "$AVGPU_LOG" 2>/dev/null || echo 0)"
}

capset_accepted() {
    local mark="$1" lines
    [[ -f "$AVGPU_LOG" ]] || return 1
    [[ "$(stat -c%s "$AVGPU_LOG")" -ge "$mark" ]] || mark=0
    lines="$(tail -c +$((mark + 1)) "$AVGPU_LOG" | grep 'device capset status' | tail -n1 || true)"
    [[ "$lines" == *'status 0x00000000' ]]
}

domain_runs_branch() {
    local xml cmdline
    xml="$(virshq dumpxml "$DOMAIN" 2>/dev/null)" || return 1
    cmdline="$(pgrep -af "guest=${DOMAIN}," | head -n1)"
    grep -qF "<emulator>${BR_QEMU}</emulator>" <<<"$xml" \
        && grep -qF "executor=${BR_EXEC}" <<<"$xml" \
        && grep -qF "${BR_QEMU}" <<<"$cmdline" \
        && grep -qF "executor=${BR_EXEC}" <<<"$cmdline"
}

adapters_healthy() {
    local status
    status="$(gpu_status 2>/dev/null)" || return 1
    jq -e '(.adapters | length) > 0 and all(.adapters[]; .problem_code == 0 and .started == true)' <<<"$status" >/dev/null
}

adapters_at_driver() {
    gpu_status 2>/dev/null | jq -e --arg old "$MA_DRIVER" --arg line "${BR_DRIVER%.*.*}." \
        '(.adapters | length) > 0 and (([.adapters[].driver_version] | unique | length) == 1) and all(.adapters[]; .driver_version != $old and (.driver_version | startswith($line)))' >/dev/null
}

guest_at_branch() {
    local info
    info="$(sysinfo 2>/dev/null)" || return 1
    jq -e --arg stamp "$BR_STAMP" --arg ver "$BR_VERSION" --arg rel "$BR_RELEASE" \
        '.agent_version == $stamp and .release_version == $ver and (if $rel == "" then true else .release == $rel end)' \
        <<<"$info" >/dev/null
}

stage_probes() {
    local arch="$1" tree="$2" probe
    mkdir -p "${RUN}/share/Games/_upgrade/${arch}" "${RUN}/share/Games/_upgrade/out"
    for probe in $PROBES; do
        [[ -f "${tree}/${probe}.exe" ]] || return 1
        cp -f "${tree}/${probe}.exe" "${RUN}/share/Games/_upgrade/${arch}/"
    done
}

probe_script() {
    local arch="$1" round="$2" probe
    printf '@echo off\r\ncd /d Z:\\_upgrade\\%s\r\n' "$arch"
    for probe in $PROBES; do
        printf '%s.exe > Z:\\_upgrade\\out\\%s-%s-%s.txt 2>&1\r\n' "$probe" "$round" "$arch" "$probe"
        printf 'echo exit=%%errorlevel%% >> Z:\\_upgrade\\out\\%s-%s-%s.txt\r\n' "$round" "$arch" "$probe"
    done
    printf 'echo done > Z:\\_upgrade\\out\\%s-%s-done.txt\r\n' "$round" "$arch"
}

session_one_up() {
    rpc list_sessions | jq -e '[.. | objects | select(.id? == 1 or .session? == 1)] | length > 0' >/dev/null
}

run_probes() {
    local label="$1" round="$2" arch tree out probe code
    local archs="x64"
    [[ "$X86" -eq 1 ]] && archs="x64 x86"
    wait_until 300 "a user session in the guest" session_one_up
    for arch in $archs; do
        tree="${BR_TREE}/build/accel-virt/drivers/guest"
        [[ "$arch" == x86 ]] && tree="${BR_TREE}/build/accel-virt/drivers/guest32"
        if ! stage_probes "$arch" "$tree"; then
            fail "${label}: the branch build has no ${PROBES} in ${tree}"
            continue
        fi
        out="${RUN}/share/Games/_upgrade/out"
        rm -f "${out}/${round}-${arch}-"*
        if ! rpc run_script "$(jq -cn --arg s "$(probe_script "$arch" "$round")" --arg n "upgrade probes ${arch}" '{name:$n,script:$s,session:1}')" 60 >>"$LOG" 2>&1; then
            fail "${label}: the guest would not run the ${arch} probes in session 1"
            continue
        fi
        if ! wait_until "$PROBE_TIMEOUT" "the ${arch} probes" test -f "${out}/${round}-${arch}-done.txt"; then
            fail "${label}: the ${arch} probes did not finish in ${PROBE_TIMEOUT} s"
            continue
        fi
        for probe in $PROBES; do
            code="$(tail -n1 "${out}/${round}-${arch}-${probe}.txt" 2>/dev/null | tr -d '\r ')"
            cp "${out}/${round}-${arch}-${probe}.txt" "${RUN}/${round}-${arch}-${probe}.txt" 2>/dev/null
            if [[ "$code" == "exit=0" ]]; then
                pass "${label}: ${arch} ${probe} passes"
            else
                fail "${label}: ${arch} ${probe}: ${code:-no output} (see ${RUN}/${round}-${arch}-${probe}.txt)"
            fi
        done
    done
}

assert_all() {
    local label="$1" round="$2" mark="$3"
    say "Checking ${label}"
    if wait_until 120 "the branch's emitter" domain_runs_branch; then
        pass "${label}: the domain runs the branch's emitter and executor"
    else
        fail "${label}: the domain does not run ${BR_QEMU} with ${BR_EXEC}"
    fi
    if wait_until 300 "the agent" agent_online; then
        pass "${label}: the agent is online"
    else
        fail "${label}: the agent is not online"
    fi
    if wait_until 120 "the branch's versions" guest_at_branch; then
        pass "${label}: the guest reports agent ${BR_STAMP}, release version ${BR_VERSION}${BR_RELEASE:+, release ${BR_RELEASE:0:12}}"
    else
        fail "${label}: the guest reports $(sysinfo 2>/dev/null | jq -c '{agent_version,release_version,release}' 2>/dev/null) and the branch has agent ${BR_STAMP}, version ${BR_VERSION}, release ${BR_RELEASE:-?}"
    fi
    if wait_until 120 "the branch's driver" adapters_at_driver; then
        pass "${label}: every adapter has one driver version, newer than master's ${MA_DRIVER}, in the branch's ${BR_DRIVER%.*.*} series"
    else
        fail "${label}: adapter driver versions: $(gpu_status 2>/dev/null | jq -c '[.adapters[].driver_version]' 2>/dev/null); master's was ${MA_DRIVER}, the branch's series is ${BR_DRIVER%.*.*}"
    fi
    if wait_until 180 "healthy adapters" adapters_healthy; then
        pass "${label}: every adapter has problem_code 0 ($(gpu_status | jq -r '.adapters | length') adapter(s))"
    else
        fail "${label}: adapters: $(gpu_status 2>/dev/null | jq -c '[.adapters[] | {name,problem_code,started}]' 2>/dev/null)"
    fi
    if wait_until 60 "the capset" capset_accepted "$mark"; then
        pass "${label}: the driver log shows the capset accepted"
    else
        fail "${label}: the driver log shows no accepted capset after the checkpoint ($(grep 'device capset status' "$AVGPU_LOG" 2>/dev/null | tail -n1))"
    fi
    run_probes "$label" "$round"
}

guest_update() {
    local force="$1" out status end
    end=$((SECONDS + UPDATE_TIMEOUT))
    while ((SECONDS < end)); do
        out="$(mcp guest_update "{\"force\":${force}}" 1100 2>&1)"
        status=$?
        if grep -q 'already running' <<<"$out"; then
            log "guest_update: an update is running already; waiting"
            sleep 15
            continue
        fi
        printf '%s\n' "$out" >>"$LOG"
        UPDATE_OUT="$out"
        return "$status"
    done
    UPDATE_OUT="no guest_update could start in ${UPDATE_TIMEOUT} s"
    return 2
}

guest_reboot() {
    local before
    before="$(info_field agent_started)"
    rpc run_script '{"name":"reboot","script":"shutdown /r /t 0"}' 60 >>"$LOG" 2>&1 || die "the guest would not take the reboot command"
    wait_until 600 "the guest to restart" agent_restarted "$before" \
        || die "the guest did not come back with a new agent after the reboot"
}

summary() {
    local took=$((SECONDS - START))
    echo
    say "${passes} passed, ${fails} failed in $((took / 60)) min $((took % 60)) s"
    log "summary: ${passes} passed, ${fails} failed, ${took} s"
}

main() {
    need git qemu-img virsh jq python3 taskset ccache cmake
    if [[ "$RESTORE_USER" -eq 1 ]]; then
        mkdir -p "${WORK}/runs"
        LOG="${WORK}/restore.log"
        restore_user
        exit 0
    fi
    mkdir -p "${WORK}/runs"
    LOG="${WORK}/last-command.log"
    : >"$LOG"
    trap cleanup EXIT INT TERM
    MAIN="$(git -C "$ROOT" worktree list --porcelain | sed -n '1s/^worktree //p')"
    SUBGIT="$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)/modules/external"
    [[ -d "${SUBGIT}/accel-virt" && -d "${SUBGIT}/remotewin" ]] || die "no submodule repositories in ${SUBGIT}"
    [[ -n "$CPUS" ]] || CPUS="$(last_cpus "$JOBS")"

    if [[ "$MAKE_BASELINE" -eq 1 ]]; then
        make_baseline
        exit 0
    fi

    [[ -d "$BASELINE" || "$BUILD_ONLY" -eq 1 ]] || die "no baseline; run scripts/test-upgrade.sh --make-baseline --pause-vm first"

    prepare_tree "$BASE_REF" "master"
    MA_TREE="$TREE"
    MA_SHA="$TREE_SHA"
    prepare_tree "$BRANCH_REF" "branch"
    BR_TREE="$TREE"
    BR_SHA="$TREE_SHA"
    [[ "$BUILD_ONLY" -eq 0 ]] || { say "Built ${MA_SHA:0:12} and ${BR_SHA:0:12}"; exit 0; }

    BR_QEMU="${BR_TREE}/build/accel-virt/qemu/qemu/qemu-system-x86_64"
    BR_EXEC="${BR_TREE}/build/accel-virt/qemu/libav_executor.so"
    BR_STAMP="$(sed -n 's/^#define LSW_BUILD_STAMP "\(.*\)"/\1/p' "$(find "${BR_TREE}/build-agent" -name lsw_build_stamp.h | head -n1)")"
    BR_VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "${BR_TREE}/CMakeLists.txt" | head -n1)"
    BR_DRIVER="$(sed -n 's/^DriverVer *= *[^,]*,\(.*\)/\1/p' "${BR_TREE}/build/av-drivers/package/avgpu.inf" | tr -d '\r ')"
    BR_RELEASE=""
    PROBES="avd3d11probe avd3d12probe avd3d9probe avglprobe avclprobe"
    [[ -n "$BR_STAMP" && -n "$BR_VERSION" && -n "$BR_DRIVER" ]] || die "could not read the branch's agent stamp (${BR_STAMP}), version (${BR_VERSION}) or driver version (${BR_DRIVER})"

    check_free
    prepare_run
    say "Run directory ${RUN}"
    log "base ${BASE_REF} ${MA_SHA}, branch ${BRANCH_REF} ${BR_SHA}"
    note "branch expects agent '${BR_STAMP}', version ${BR_VERSION}, driver ${BR_DRIVER}"
    define_domain "$MA_TREE"

    start_manager master "$MA_TREE"
    say "Waiting for master's VM and agent (up to $((BOOT_TIMEOUT / 60)) min)"
    wait_until "$BOOT_TIMEOUT" "the agent under master" agent_online || die "master's guest did not bring its agent up; see ${RUN}/manager-master.log"
    say "Updating the guest to master (forced)"
    guest_update true || die "master's guest_update failed: ${UPDATE_OUT}"
    note "$(jq -c '{outcome,shipped_release}' <<<"$UPDATE_OUT" 2>/dev/null)"
    wait_until 600 "the agent after master's update" agent_online || die "the agent did not come back after master's update"
    MA_RELEASE="$(info_field release)"
    MA_DRIVER="$(gpu_status | jq -r '.adapters[0].driver_version')"
    say "Before state (master)"
    sysinfo | jq -c '{agent_version,release,release_version,os}' | tee -a "$LOG"
    gpu_status | jq -c '.adapters | map({name,driver_version,problem_code,started})' | tee -a "$LOG"
    [[ -n "$MA_RELEASE" ]] || die "master's guest reports no release"

    say "Stopping master's VM and manager"
    stop_manager
    shutdown_domain "$DOMAIN"
    [[ "$(domstate "$DOMAIN")" != running ]] || die "the test VM would not stop"

    booted_checkpoint
    start_manager branch "$BR_TREE"
    say "Waiting for the branch's VM and agent (up to $((BOOT_TIMEOUT / 60)) min)"
    wait_until 300 "the VM" test_domain_running || die "the branch's manager did not start the VM; see ${RUN}/manager-branch.log"
    wait_until "$BOOT_TIMEOUT" "the agent under the branch" agent_online || die "the guest did not bring its agent up under the branch's manager; see ${RUN}/manager-branch.log"

    say "Updating the guest through the branch (guest_update, not forced)"
    guest_update false || die "the branch's guest_update failed: ${UPDATE_OUT}"
    BR_RELEASE="$(jq -r '.shipped_release // empty' <<<"$UPDATE_OUT" 2>/dev/null)"
    note "$(jq -c '{outcome,shipped_release}' <<<"$UPDATE_OUT" 2>/dev/null)"
    if [[ -z "$BR_RELEASE" || "$BR_RELEASE" == "$MA_RELEASE" ]]; then
        say "The branch ships the release the guest has; forcing"
        guest_update true || die "the branch's forced guest_update failed: ${UPDATE_OUT}"
        BR_RELEASE="$(jq -r '.shipped_release // empty' <<<"$UPDATE_OUT" 2>/dev/null)"
        note "$(jq -c '{outcome,shipped_release}' <<<"$UPDATE_OUT" 2>/dev/null)"
    fi
    wait_until 600 "the agent after the branch's update" agent_online || die "the agent did not come back after the branch's update"

    assert_all "after the update" r1 "$AVGPU_MARK"

    say "Rebooting the guest"
    booted_checkpoint
    guest_reboot
    assert_all "after a guest reboot" r2 "$AVGPU_MARK"

    say "Updating again without force"
    local before after
    before="$(sysinfo | jq -c '{release,agent_started,finished:.last_update.finished}')"
    guest_update false
    local update_status=$?
    after="$(sysinfo | jq -c '{release,agent_started,finished:.last_update.finished}')"
    if [[ "$update_status" -eq 0 && "$before" == "$after" ]]; then
        pass "a second guest_update without force changes nothing"
    else
        fail "a second guest_update without force: status ${update_status}, before ${before}, after ${after}"
    fi

    summary
    [[ "$fails" -eq 0 && "$passes" -gt 0 ]] && RESULT_CODE=0 || RESULT_CODE=1
    exit "$RESULT_CODE"
}

main
