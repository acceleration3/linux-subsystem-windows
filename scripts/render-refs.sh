#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${LSW_RENDER_REF_BUILD:-${ROOT}/build-render-ref}"
OUT="${LSW_RENDER_REFS_OUT:-${BUILD}/refs}"
GUEST_DIR="${LSW_GUEST_SUITES_DIR:-av\\gputest}"
SMB_PORT="${LSW_GUEST_SMB_PORT:-5445}"
GPU="${LSW_RENDER_REF_GPU:-auto}"

build=1
args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) build=0 ;;
        --out) OUT="$2"; shift ;;
        --gpu) GPU="$2"; shift ;;
        *) args+=("$1") ;;
    esac
    shift
done

if [[ ${build} -eq 1 ]]; then
    "${ROOT}/scripts/build-render-ref.sh"
fi

rm -rf "${OUT}"
"${BUILD}/lsw-render-ref" --out "${OUT}" --gpu "${GPU}" ${args[@]+"${args[@]}"}
rm -rf "${OUT}/.logs"

smb() { PASSWD=lsw smbclient //127.0.0.1/C\$ -p "${SMB_PORT}" -U lsw -m SMB3 -c "$1"; }
smb "mkdir av; mkdir ${GUEST_DIR}; deltree ${GUEST_DIR}\\render-refs; mkdir ${GUEST_DIR}\\render-refs; recurse ON; prompt OFF; lcd ${OUT}; cd ${GUEST_DIR}\\render-refs; mput *" >/dev/null 2>&1
echo "render references pushed to C:\\${GUEST_DIR}\\render-refs"
