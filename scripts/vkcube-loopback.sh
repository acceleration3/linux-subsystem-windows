#!/usr/bin/env bash
# Local baseline: run a Vulkan app through the LSW ICD into
# lsw-render-server, on this machine, with no VM involved.
#
# This is the smallest end-to-end exercise of the command-stream proxy —
# the same code path the Windows guest will use, minus the guest. When
# the guest side breaks, run this first: if the loopback is broken too,
# the bug is in the proxy, not in the VM plumbing.
#
#   app (vkcube)
#     -> liblsw_icd.so            serialise every Vulkan call
#       -> unix socket / shm      (guest: the same shared memory)
#         -> lsw-render-server    replay against the real driver
#
# The transport is a flag because it is meant to be one: since Phase B
# the only difference between the two is which URL is passed, so running
# both and diffing behaviour is how a shared-memory bug gets a
# socket-shaped control. ``--transport both`` does exactly that.
#
# Over shm the region is laid out by the same linkproto call the manager
# uses for a VM, at the same size, so this is not a simulation of the
# guest path — it is one end of it with the guest end swapped for a
# second local process.
#
# Run: ./scripts/vkcube-loopback.sh [frames]
#      ./scripts/vkcube-loopback.sh --transport shm [frames]
#      ./scripts/vkcube-loopback.sh --transport both
#      LSW_TRACE=1 ./scripts/vkcube-loopback.sh   (trace-level server log)
#
# Exits non-zero if the app fails to run, or if the server never saw the
# traffic it should have.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"

TRANSPORT="unix"
FRAMES=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --transport) TRANSPORT="${2:-}"; shift 2 ;;
        --transport=*) TRANSPORT="${1#*=}"; shift ;;
        -h|--help)
            sed -n '2,31p' "$0"
            exit 0 ;;
        *) FRAMES="$1"; shift ;;
    esac
done
FRAMES="${FRAMES:-300}"

case "${TRANSPORT}" in
    unix|shm|both) ;;
    *) printf 'unknown transport: %s (want unix, shm or both)\n' "${TRANSPORT}" >&2; exit 2 ;;
esac

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mFAIL:\033[0m %s\n' "$*" >&2; exit 1; }

command -v vkcube >/dev/null 2>&1 || fail "vkcube not installed (Arch: vulkan-tools)"
[[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] || fail "no display — this test needs a session to present into"

say "Building ICD + render server"
if [[ ! -d "${BUILD}" ]]; then
    cmake -S "${ROOT}" -B "${BUILD}" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
fi
cmake --build "${BUILD}" --target lsw_icd lsw-render-server >/dev/null

ICD_JSON="${BUILD}/lsw_icd.json"
SERVER="${BUILD}/src/render_server/lsw-render-server"
[[ -f "${ICD_JSON}" ]] || fail "missing ${ICD_JSON}"
[[ -x "${SERVER}" ]]   || fail "missing ${SERVER}"

RUNDIR="$(mktemp -d /tmp/lsw-loopback-XXXXXX)"

SERVER_PID=""
SHM_NAME=""
cleanup() {
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
    # The server unlinks its own region on exit; clean up after a kill.
    [[ -n "${SHM_NAME}" && -e "/dev/shm/${SHM_NAME}" ]] && rm -f "/dev/shm/${SHM_NAME}"
    return 0
}
trap cleanup EXIT

# Wait for the endpoint the server was told to create to actually exist.
# Both schemes put something on a filesystem, which beats sleeping a
# guessed interval.
wait_for_endpoint() {
    local path="$1" log="$2" test_flag="$3"
    for _ in $(seq 100); do
        # Single-bracket ``test`` on purpose: ``[[ ]]`` parses its
        # operators at parse time, so the flag cannot come from a
        # variable there.
        [ "${test_flag}" "${path}" ] && return 0
        kill -0 "${SERVER_PID}" 2>/dev/null || fail "render server exited early:\n$(cat "${log}")"
        sleep 0.1
    done
    return 1
}

run_one() {
    local scheme="$1"
    local server_log="${RUNDIR}/render-server-${scheme}.log"
    local app_log="${RUNDIR}/app-${scheme}.log"
    local url endpoint_path test_flag

    if [[ "${scheme}" == "unix" ]]; then
        endpoint_path="${RUNDIR}/render.sock"
        url="unix://${endpoint_path}"
        test_flag="-S"
    else
        SHM_NAME="lsw-vk-$$"
        endpoint_path="/dev/shm/${SHM_NAME}"
        url="shm:///${SHM_NAME}"
        test_flag="-f"
    fi

    say "Starting render server on ${url}"
    LSW_LOG="${LSW_TRACE:+trace}" "${SERVER}" --endpoint "${url}" >"${server_log}" 2>&1 &
    SERVER_PID=$!

    wait_for_endpoint "${endpoint_path}" "${server_log}" "${test_flag}" \
        || fail "render server never created ${endpoint_path}\n$(cat "${server_log}")"

    say "Running vkcube (${FRAMES} frames) over ${scheme}"
    local start end elapsed rc
    start=$(date +%s.%N)
    set +e
    LSW_SERVER_SOCKET="${url}" \
    VK_DRIVER_FILES="${ICD_JSON}" \
        vkcube --c "${FRAMES}" >"${app_log}" 2>&1
    rc=$?
    set -e
    end=$(date +%s.%N)
    elapsed=$(awk -v a="${start}" -v b="${end}" 'BEGIN { printf "%.2f", b - a }')

    [[ ${rc} -eq 0 ]] || fail "vkcube exited ${rc} over ${scheme}:\n$(cat "${app_log}")"

    # The app talking to the LSW ICD at all is the first thing to assert:
    # without it the loader silently fell back to the system driver and
    # the whole run proves nothing.
    grep -q "connected to ${scheme}://" "${app_log}" \
        || fail "vkcube did not connect through the LSW ICD over ${scheme} — did the loader pick another driver?\n$(cat "${app_log}")"

    # The server has to have created the window itself. Presenting into a
    # window owned by another process succeeds at every Vulkan call and
    # paints nothing, so "a surface exists" is not the assertion that
    # matters — "the host owns the window" is.
    grep -q 'host window' "${server_log}" \
        || fail "render server never created a host window over ${scheme}\n$(cat "${server_log}")"

    local fps
    fps=$(awk -v f="${FRAMES}" -v t="${elapsed}" 'BEGIN { printf "%.0f", f / t }')
    say "OK ${scheme} — ${FRAMES} frames in ${elapsed}s (~${fps} fps)"
    echo "    app log:    ${app_log}"
    echo "    server log: ${server_log}"

    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
    SERVER_PID=""
    [[ -n "${SHM_NAME}" && -e "/dev/shm/${SHM_NAME}" ]] && rm -f "/dev/shm/${SHM_NAME}"
    SHM_NAME=""
    return 0
}

if [[ "${TRANSPORT}" == "both" ]]; then
    run_one unix
    echo
    run_one shm
else
    run_one "${TRANSPORT}"
fi

echo
echo "The window titled \"vkcube\" is the host's, created by the render"
echo "server; the app's own window stays black and unused — in the guest"
echo "case it would not exist at all."
echo
echo "NOTE: exit code and frame count prove the stream was accepted and"
echo "      replayed. Geometry correctness still needs eyes: the cube must"
echo "      be spinning, not a flat clear colour. (A clear colour with no"
echo "      cube is the signature of mapped memory not reaching the"
echo "      server — see queue.cpp.)"
