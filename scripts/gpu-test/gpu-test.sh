#!/usr/bin/env bash
# Build here, deploy to every test machine, and run every graphics API's
# tests on every GPU each machine has, in its LSW VM.
#
# This machine is the builder: it builds LSW and accel-virt once and rsyncs
# what a run needs - the patched QEMU and executor, the guest driver package,
# accel-virt's guest and host test programs and their runtimes - to
# ~/.local/share/lsw-gpu-test/tree on each host, laid out as in this
# checkout. Each host then runs scripts/gpu-test/gpu_matrix.py on itself:
# its VM redefined with one accel-virt adapter per pass, bound to one GPU,
# the probes and the D3D11, D3D12, Vulkan, OpenGL and OpenCL sample suites
# run in it, and the VM's own definition put back at the end.
#
# Usage:
#   scripts/gpu-test/gpu-test.sh build                  # LSW + the test programs
#   scripts/gpu-test/gpu-test.sh deploy   [HOST...]     # rsync the tree, then preflight
#   scripts/gpu-test/gpu-test.sh preflight [HOST...]    # what each host lacks
#   scripts/gpu-test/gpu-test.sh run      [HOST...] [-- gpu_matrix.py run options]
#   scripts/gpu-test/gpu-test.sh status   [RUN]
#   scripts/gpu-test/gpu-test.sh wait     [RUN]         # until every host finished
#   scripts/gpu-test/gpu-test.sh collect  [RUN]         # results into build/gpu-test/RUN
#   scripts/gpu-test/gpu-test.sh report   [RUN]         # the failures, every host and GPU
#   scripts/gpu-test/gpu-test.sh all      [HOST...] [-- run options]
#   scripts/gpu-test/gpu-test.sh restore  [HOST...]     # the VM definition a killed run changed
#   scripts/gpu-test/gpu-test.sh provision HOST [-- lsw-manager options]   # an LSW VM where none is
#   scripts/gpu-test/gpu-test.sh setup-keys [HOST...]   # this machine's ssh key on each host
#   scripts/gpu-test/gpu-test.sh traces-sync [HOST...]  # the trace store (TRACES.md) to each host
#   scripts/gpu-test/gpu-test.sh traces-pull HOST APP/FUNC   # a trace recorded on HOST into this store
#
# HOST is a name from scripts/gpu-test/hosts (all of them when none is
# given). RUN defaults to the last one started. Run options are
# gpu_matrix.py's: --gpus 0,2, --suites probes,vulkan, --frames N, ...
#
# A run takes the VM over: lsw-manager must be closed on every host, and the
# VM is shut down and restarted several times. Runs on different hosts go in
# parallel; on one host, roughly 20-40 minutes per GPU.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="${ROOT}/scripts/gpu-test"
AV="${ROOT}/external/accel-virt"
HOSTS_FILE="${LSW_GPU_TEST_HOSTS:-${HERE}/hosts}"
BASE=".local/share/lsw-gpu-test"   # under each host's home
RESULTS="${ROOT}/build/gpu-test"
# The trace store (scripts/gpu-test/TRACES.md): this machine's is the master
# copy; each host's is ~/${BASE}/traces, where gpu_matrix.py's traces suite reads it.
TRACE_STORE="${LSW_GPU_TEST_TRACE_STORE:-${HOME}/${BASE}/traces}"
# LSW_GPU_TEST_CONTROL_PATH reuses master connections opened some other way
# (with a password, say), since every connection here is non-interactive.
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10 -o ControlMaster=auto
          -o "ControlPath=${LSW_GPU_TEST_CONTROL_PATH:-/tmp/lsw-gpu-test-%C}" -o ControlPersist=10m)

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf 'gpu-test: %s\n' "$*" >&2; exit 1; }

target_of() { awk -v name="$1" '!/^[[:space:]]*#/ && $1 == name { print $2 }' "${HOSTS_FILE}"; }
all_hosts() { awk '!/^[[:space:]]*#/ && NF >= 2 { print $1 }' "${HOSTS_FILE}"; }

# The hosts named before a "--", or every host; what follows "--" is left in PASS.
HOSTS=()
PASS=()
split_arguments() {
    HOSTS=()
    PASS=()
    while [[ $# -gt 0 ]]; do
        if [[ $1 == -- ]]; then shift; PASS=("$@"); break; fi
        [[ -n "$(target_of "$1")" ]] || die "no host '$1' in ${HOSTS_FILE}"
        HOSTS+=("$1")
        shift
    done
    if [[ ${#HOSTS[@]} -eq 0 ]]; then mapfile -t HOSTS < <(all_hosts); fi
}

# A bash script on stdin, run in HOST's home. The hosts' login shells need
# not be bash, so nothing is passed on the command line.
on() {
    local target
    target="$(target_of "$1")"
    if [[ ${target} == local ]]; then
        (cd ~ && bash -s)
    else
        ssh "${SSH_OPTS[@]}" "${target}" bash -s
    fi
}

# rsync's destination for PATH under HOST's home.
destination() {
    local target
    target="$(target_of "$1")"
    if [[ ${target} == local ]]; then echo "${HOME}/$2"; else echo "${target}:$2"; fi
}

sync_to() { rsync -e "ssh ${SSH_OPTS[*]}" "$@"; }

last_run() {
    [[ -f ${RESULTS}/last-run ]] || die "no run started yet"
    cat "${RESULTS}/last-run"
}

run_hosts() {
    [[ -f ${RESULTS}/$1/hosts ]] || die "no run $1 in ${RESULTS}"
    cat "${RESULTS}/$1/hosts"
}

# ---------------------------------------------------------------- building

cmd_build() {
    say "Building LSW: agent, manager, mirror, accel-virt"
    "${ROOT}/scripts/build.sh"
    # The app suites' capture benchmark, which the agent build leaves out.
    cmake --build "${ROOT}/build-agent" --target remotewin-wgcbench
    say "Building accel-virt's test programs"
    if [[ ! -f ${AV}/build/CMakeCache.txt ]]; then
        cmake -S "${AV}" -B "${AV}/build" -G Ninja
    fi
    cmake --build "${AV}/build" --target avtest avd3d11samples_host avd3d12samples_host \
        avglsamples_host avclsamples_host av_device_loss_layer
    # Always: it applies DXVK's and vkd3d-proton's patch series and rebuilds
    # what changed, and a runtime built before a new patch would ship without it.
    (cd "${AV}" && tools/build_d3d_runtime.sh)
    (cd "${AV}" && tools/fetch_dxc.sh)
    (cd "${AV}" && tools/build_vulkan_samples.sh all)
    local redist="${AV}/third_party/vcredist/vc_redist.x64.exe"
    if [[ ! -f ${redist} ]]; then
        mkdir -p "$(dirname "${redist}")"
        curl -sSL -o "${redist}" https://aka.ms/vs/17/release/vc_redist.x64.exe
    fi
    say "Built."
}

# --------------------------------------------------------------- deploying

# What a host needs, relative to the checkout: the files and directories
# rsync copies, symlinks followed (QEMU's qemu-bundle links into this
# machine's build tree).
bundle_paths() (
    local qemu=build/accel-virt/qemu
    cd "${ROOT}"
    echo scripts/gpu-test
    echo ${qemu}/qemu/qemu-system-x86_64
    find ${qemu}/qemu -maxdepth 1 -name '*.so'
    echo ${qemu}/qemu/qemu-bundle/usr/local/share/qemu
    find ${qemu} -maxdepth 1 \( -name '*.so' -o -name av-renderer \)
    echo build/accel-virt/d3d/lib
    echo build/av-drivers
    echo build/src/host/lsw-manager
    echo build/remotewin/source/host/remotewin
    echo build-agent/src/agent/lsw-guest-agent.exe
    ls build-agent/remotewin/source/guest/remotewin-guest.exe 2>/dev/null || true
    echo build-agent/remotewin/source/guest/remotewin-wgcbench.exe
    echo build-agent/_deps/vulkanloader-build/loader/vulkan-1.dll
    echo build-agent32/_deps/vulkanloader-build/loader/vulkan-1.dll
    echo external/accel-virt/tools
    echo external/accel-virt/build/guest
    echo external/accel-virt/build/gpu_fault/libav_device_loss_layer.so
    echo external/accel-virt/tests/gpu_fault/VkLayer_av_device_loss.json.in
    for program in avd3d11samples_host avd3d12samples_host avglsamples_host avclsamples_host; do
        echo external/accel-virt/build/${program}
    done
    echo external/accel-virt/build-d3d/lib
    echo external/accel-virt/build-vulkan-samples/host/avvksamples
    echo external/accel-virt/build-vulkan-samples/guest/avvksamples.exe
    echo external/accel-virt/third_party/vulkan-runtime
    echo external/accel-virt/third_party/dxc
    echo external/accel-virt/third_party/vcredist
    echo external/accel-virt/third_party/vulkan-samples/Vulkan-Samples/data
)

cmd_deploy() {
    split_arguments "$@"
    local list
    list="$(mktemp)"
    trap 'rm -f "${list}"' RETURN
    bundle_paths > "${list}"
    while read -r path; do
        [[ -e ${ROOT}/${path} ]] || die "${path} is missing: run gpu-test.sh build first"
    done < "${list}"
    for host in "${HOSTS[@]}"; do
        say "Deploying to ${host}"
        on "${host}" <<< "mkdir -p ${BASE}/tree ${BASE}/runs"
        sync_to -a -r -L --delete --exclude __pycache__ --files-from="${list}" "${ROOT}/" \
            "$(destination "${host}" "${BASE}/tree/")"
        portable_executables "${host}"
        ship_missing_libraries "${host}"
    done
    cmd_preflight "${HOSTS[@]}"
}

# A CachyOS builder's glibc is built for x86-64-v4, and its crt1.o carries
# that as an ISA-level note into every executable linked here, so a host
# without AVX-512 refuses to start them ("CPU ISA level is lower than
# required") although their code is baseline x86-64. The note is removed
# from the deployed copies; the shared libraries never carry it.
portable_executables() {
    on "$1" <<EOF
cd ${BASE}/tree
find . -type f -perm -u+x ! -name '*.so' ! -name '*.so.*' ! -name '*.exe' ! -name '*.dll' ! -name '*.py' \
        ! -name '*.sh' -print0 |
    while IFS= read -r -d '' file; do
        if readelf -n "\${file}" 2>/dev/null | grep -q 'ISA needed:.*x86-64-v[234]'; then
            objcopy --remove-section .note.gnu.property "\${file}"
        fi
    done
EOF
}

# The shared libraries QEMU links that HOST lacks (optional features this
# machine's build picked up: RDMA, VDE, XDP, ...), copied from here into
# tree/libs, which gpu_matrix.py puts on QEMU's LD_LIBRARY_PATH. Repeated
# until nothing is missing, since a copied library may need others.
ship_missing_libraries() {
    local host=$1 round missing name path staged
    staged="$(mktemp -d)"
    for round in 1 2 3 4 5; do
        missing="$(on "${host}" <<EOF
LD_LIBRARY_PATH=${BASE}/tree/libs ldd ${BASE}/tree/build/accel-virt/qemu/qemu/qemu-system-x86_64 \
    ${BASE}/tree/build/accel-virt/qemu/libav_executor.so | awk '/=> not found/ { print \$1 }' | sort -u
EOF
)"
        [[ -z ${missing} ]] && break
        rm -f "${staged}"/*
        for name in ${missing}; do
            path="$(ldconfig -p | awk -v n="${name}" '$1 == n && /x86-64/ && !found { found = $NF } END { print found }')"
            [[ -n ${path} ]] || die "${host} lacks ${name}, which this machine does not have either"
            cp -L "${path}" "${staged}/${name}"
        done
        say "Shipping to ${host}: ${missing//$'\n'/ }"
        on "${host}" <<< "mkdir -p ${BASE}/tree/libs"
        sync_to -a "${staged}/" "$(destination "${host}" "${BASE}/tree/libs/")"
    done
    rm -rf "${staged}"
}

cmd_preflight() {
    split_arguments "$@"
    local failed=0
    for host in "${HOSTS[@]}"; do
        say "Preflight on ${host}"
        on "${host}" <<< "python3 -B ${BASE}/tree/scripts/gpu-test/gpu_matrix.py preflight; \
python3 -B ${BASE}/tree/scripts/gpu-test/gpu_matrix.py gpus" || failed=1
    done
    return "${failed}"
}

# ----------------------------------------------------------------- running

cmd_run() {
    split_arguments "$@"
    local run
    run="${LSW_GPU_TEST_RUN:-$(date +%Y%m%d-%H%M%S)}"
    mkdir -p "${RESULTS}/${run}"
    # A run restarted on some hosts keeps the others it already had.
    { cat "${RESULTS}/${run}/hosts" 2>/dev/null || true; printf '%s\n' "${HOSTS[@]}"; } | sort -u > "${RESULTS}/${run}/hosts.new"
    mv "${RESULTS}/${run}/hosts.new" "${RESULTS}/${run}/hosts"
    echo "${run}" > "${RESULTS}/last-run"
    local options=""
    [[ ${#PASS[@]} -gt 0 ]] && options="$(printf ' %q' "${PASS[@]}")"
    for host in "${HOSTS[@]}"; do
        say "Starting run ${run} on ${host}"
        on "${host}" <<EOF
set -e
out=${BASE}/runs/${run}
mkdir -p "\${out}"
command="exec python3 -B ${BASE}/tree/scripts/gpu-test/gpu_matrix.py run --out \"\${out}\"${options//\"/\\\"} > \"\${out}/run.log\" 2>&1 < /dev/null"
# A transient user service of its own where systemd has one: a process merely
# detached with setsid still dies with the scope it was started in, which is a
# terminal tab's or a session's.
if systemd-run --user --quiet --collect true 2>/dev/null; then
    # libvirt's session daemon is spawned by the first virsh and stays in that
    # caller's scope, with every VM it starts: one a terminal or an ssh session
    # started dies with it, and takes the run's VM along. One that is idle is
    # stopped here, so the run's own first virsh starts it inside the run's
    # unit; one with a VM running is left alone.
    for daemon in \$(pgrep -u "\$(id -u)" -x virtqemud); do
        grep -q '\.service\$' "/proc/\${daemon}/cgroup" 2>/dev/null && continue
        [ -z "\$(virsh -c qemu:///session list --name 2>/dev/null | tr -d '[:space:]')" ] && kill "\${daemon}"
    done
    unit="lsw-gpu-test-${run}"
    systemctl --user reset-failed "\${unit}.service" 2>/dev/null || true
    systemd-run --user --quiet --collect --unit="\${unit}" --working-directory="\${HOME}" /bin/sh -c "\${command}"
    systemctl --user show -p MainPID --value "\${unit}.service" > "\${out}/pid"
else
    setsid nohup /bin/sh -c "\${command}" > /dev/null 2>&1 < /dev/null &
    echo \$! > "\${out}/pid"
fi
EOF
    done
    say "Run ${run} started; follow it with: $0 status, or $0 wait"
}

cmd_status() {
    local run="${1:-$(last_run)}"
    local unfinished=0
    for host in $(run_hosts "${run}"); do
        local line
        line="$(on "${host}" <<EOF
out=${BASE}/runs/${run}
state=\$(python3 -c 'import json,sys; s=json.load(open(sys.argv[1])); print(s.get("state","?"), s.get("current") or s.get("reason") or "")' "\${out}/status.json" 2>/dev/null || echo starting)
alive=dead
[ -f "\${out}/pid" ] && kill -0 "\$(cat "\${out}/pid")" 2>/dev/null && alive=alive
echo "\${state} (\${alive}) | \$(tail -n 1 "\${out}/run.log" 2>/dev/null)"
EOF
)"
        printf '%-8s %s\n' "${host}" "${line}"
        [[ ${line} == running*'(alive)'* || ${line} == starting*'(alive)'* ]] && unfinished=1
    done
    return "${unfinished}"
}

cmd_wait() {
    local run="${1:-$(last_run)}"
    until cmd_status "${run}"; do
        sleep 60
        echo
    done
}

cmd_collect() {
    local run="${1:-$(last_run)}"
    for host in $(run_hosts "${run}"); do
        say "Collecting ${run} from ${host}"
        mkdir -p "${RESULTS}/${run}/${host}"
        sync_to -a --exclude agent.sock --exclude '*.bgra' --exclude 'guest-frames.zip' --exclude 'data.zip' \
            "$(destination "${host}" "${BASE}/runs/${run}/")" "${RESULTS}/${run}/${host}/"
    done
}

cmd_report() {
    local run="${1:-$(last_run)}"
    python3 -B "${HERE}/aggregate.py" "${RESULTS}/${run}"
}

cmd_all() {
    split_arguments "$@"
    local hosts=("${HOSTS[@]}") pass=("${PASS[@]}")
    cmd_build
    cmd_deploy "${hosts[@]}"
    cmd_run "${hosts[@]}" -- "${pass[@]}"
    local run
    run="$(last_run)"
    cmd_wait "${run}"
    cmd_collect "${run}"
    cmd_report "${run}"
}

cmd_restore() {
    split_arguments "$@"
    for host in "${HOSTS[@]}"; do
        say "Restoring the VM definition on ${host}"
        on "${host}" <<< "python3 -B ${BASE}/tree/scripts/gpu-test/gpu_matrix.py restore"
    done
}

# An unattended LSW install with the deployed manager, run headless and
# detached: it downloads Windows, so it takes the better part of an hour.
cmd_provision() {
    [[ $# -ge 1 ]] || die "provision needs a host"
    local host=$1
    shift
    [[ ${1:-} == -- ]] && shift
    local options=""
    [[ $# -gt 0 ]] && options="$(printf ' %q' "$@")"
    say "Installing an LSW VM on ${host} (log: ~/${BASE}/provision.log there)"
    on "${host}" <<EOF
cd ${BASE}/tree
QT_QPA_PLATFORM=offscreen setsid nohup build/src/host/lsw-manager --accept-license${options} \
    > ~/${BASE}/provision.log 2>&1 < /dev/null &
echo "started: pid \$!"
EOF
}

# The trace store mirrored to each host: rsync sends only what changed, and
# --delete keeps a host's copy no bigger than this one. Run it after
# traces.py fetch-tools or pull, before a run with --suites traces.
cmd_traces_sync() {
    split_arguments "$@"
    [[ -d ${TRACE_STORE} ]] || die "no trace store at ${TRACE_STORE}: run scripts/gpu-test/traces.py fetch-tools"
    for host in "${HOSTS[@]}"; do
        if [[ $(target_of "${host}") == local && $(realpath "${TRACE_STORE}") == $(realpath -m "${HOME}/${BASE}/traces") ]]; then
            say "${host}: the store is this machine's own"
            continue
        fi
        say "Syncing the trace store to ${host}"
        on "${host}" <<< "mkdir -p ${BASE}/traces"
        sync_to -a --delete --partial --info=stats1 --exclude '*.incoming' --exclude '.*.staging' \
            "${TRACE_STORE}/" "$(destination "${host}" "${BASE}/traces/")"
        on "${host}" <<< "du -sh ${BASE}/traces; df -h ~ | tail -n 1"
    done
}

# A functionality traces.py pulled into HOST's store, into this machine's.
cmd_traces_pull() {
    [[ $# -eq 2 ]] || die "traces-pull needs a host and an APP/FUNC"
    [[ -n "$(target_of "$1")" ]] || die "no host '$1' in ${HOSTS_FILE}"
    [[ $2 =~ ^[a-z0-9][a-z0-9_.-]*/[a-z0-9][a-z0-9_.-]*$ ]] || die "$2 is not APP/FUNC"
    mkdir -p "${TRACE_STORE}/$2"
    sync_to -a --delete --partial "$(destination "$1" "${BASE}/traces/$2/")" "${TRACE_STORE}/$2/"
    say "$2 is in ${TRACE_STORE}/$2"
}

cmd_setup_keys() {
    split_arguments "$@"
    [[ -f ${HOME}/.ssh/id_ed25519.pub ]] || ssh-keygen -t ed25519 -N '' -f "${HOME}/.ssh/id_ed25519"
    for host in "${HOSTS[@]}"; do
        local target
        target="$(target_of "${host}")"
        [[ ${target} == local ]] && continue
        say "Installing this machine's key on ${host}"
        ssh-copy-id -i "${HOME}/.ssh/id_ed25519.pub" "${target}"
    done
}

[[ $# -ge 1 ]] || { sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
command=$1
shift
case "${command}" in
    build)      cmd_build "$@" ;;
    deploy)     cmd_deploy "$@" ;;
    preflight)  cmd_preflight "$@" ;;
    run)        cmd_run "$@" ;;
    status)     cmd_status "$@" || true ;;
    wait)       cmd_wait "$@" ;;
    collect)    cmd_collect "$@" ;;
    report)     cmd_report "$@" ;;
    all)        cmd_all "$@" ;;
    restore)    cmd_restore "$@" ;;
    provision)  cmd_provision "$@" ;;
    setup-keys) cmd_setup_keys "$@" ;;
    traces-sync) cmd_traces_sync "$@" ;;
    traces-pull) cmd_traces_pull "$@" ;;
    -h|--help)  sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//' ;;
    *)          die "unknown command '${command}' (see --help)" ;;
esac
