#!/usr/bin/env bash
# Brings the local guest to the package the manager ships and waits until it has really arrived.
#
# The manager updates the guest by itself when it connects to an agent that runs another release. That update stops the
# agent, so the manager reports "Windows stopped during the update" while the guest is still applying it. A second
# (forced) update started in that window used to remove the first one's files. This script never starts an update
# while one is applying: it waits for the guest's update log to settle, checks that the last finished update is the
# release the manager shipped, and only otherwise asks for one more update, a bounded number of times.
#
# usage: guest-install.sh   (environment as scripts/verify.sh and the dev stack's common.sh: LSW_RENDER_DRIVER_DIR)

set -u
DEV_STACK="${LSW_RENDER_DRIVER_DIR:-${XDG_DATA_HOME:-${HOME}/.local/share}/lsw-dev/render-driver}"
# shellcheck disable=SC1091
. "${DEV_STACK}/common.sh"

settle_seconds=${LSW_GUEST_SETTLE_SECONDS:-45}
attempts=${LSW_GUEST_INSTALL_ATTEMPTS:-3}
since=${LSW_GUEST_INSTALL_SINCE:-$(date -d '-15 min' '+%Y-%m-%d %H:%M:%S')}
logfile=$(mktemp)
trap 'rm -f "${logfile}"' EXIT

fetch_update_log() {
    "${SMB[@]}" -c "get ProgramData\\LSW\\logs\\update.log ${logfile}" >/dev/null 2>&1
}

last_marker() {
    tr -d '\0' < "${logfile}" | grep -E '^[0-9T:-]+ === ' | tail -1
}

shipped_release() {
    journalctl --user -u lsw-manager-dev.service --since "${since}" --no-pager 2>/dev/null |
        sed -n 's/.*manager update: shipping release \([0-9a-f]\{16\}\).*/\1/p' | tail -1
}

wait_settled() {
    local deadline=$(( $(date +%s) + ${1:-900} )) last="" stable=0 now marker
    while [[ $(date +%s) -lt ${deadline} ]]; do
        if fetch_update_log; then
            marker=$(last_marker)
            if [[ ${marker} == "${last}" && ${marker} == *" === updated to release "* ]]; then
                stable=$(( stable + 10 ))
            elif [[ ${marker} == "${last}" ]]; then
                stable=$(( stable + 10 ))
                [[ ${stable} -ge 300 ]] && { log "the guest update has not moved for ${stable}s: ${marker}"; return 1; }
            else
                stable=0
            fi
            last=${marker}
            [[ ${stable} -ge ${settle_seconds} && ${marker} == *" === updated to release "* ]] && return 0
        fi
        sleep 10
    done
    log "the guest update did not settle: ${last}"
    return 1
}

arrived() {
    local want marker
    want=$(shipped_release)
    fetch_update_log || return 1
    marker=$(last_marker)
    [[ ${marker} == *" === updated to release "* ]] || return 1
    [[ -z ${want} || ${marker} == *" === updated to release ${want}"* ]]
}

log "waiting for the guest agent"
wait_agent 600 || { log "the guest agent did not answer"; exit 1; }
sleep 15

for n in $(seq 1 "${attempts}"); do
    wait_settled 900 || true
    wait_agent 600 || { log "the guest agent did not come back"; exit 1; }
    if arrived; then
        log "the guest runs the release the manager shipped: $(last_marker)"
        exit 0
    fi
    log "attempt ${n}: the guest has not finished the shipped release (shipped $(shipped_release), guest says: $(last_marker)); updating once more"
    for w in $(seq 1 60); do
        out=$(MCP_TIMEOUT=1500 mcp guest_update "{\"force\":$([[ ${n} -ge 2 ]] && echo true || echo false)}" 2>&1)
        rc=$?
        grep -q "already running" <<<"${out}" || break
        log "the manager is already running an update; waiting for it"
        sleep 20
    done
    printf '%s\n' "${out}" | tail -5
done

wait_settled 900 || true
wait_agent 600 || { log "the guest agent did not come back"; exit 1; }
if arrived; then
    log "the guest runs the release the manager shipped: $(last_marker)"
    exit 0
fi
log "the guest package did not install: shipped $(shipped_release), guest says: $(last_marker)"
exit 1
