#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="${ROOT}/scripts/gpu-test"
AV="${ROOT}/external/accel-virt"
HOSTS_FILE="${LSW_GPU_TEST_HOSTS:-${HERE}/hosts}"
BASE=".local/share/lsw-gpu-test"
RESULTS="${ROOT}/build/gpu-test"
TRACE_STORE="${LSW_GPU_TEST_TRACE_STORE:-${HOME}/${BASE}/traces}"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10 -o ControlMaster=auto
          -o "ControlPath=${LSW_GPU_TEST_CONTROL_PATH:-/tmp/lsw-gpu-test-%C}" -o ControlPersist=10m)

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf 'gpu-test: %s\n' "$*" >&2; exit 1; }

target_of() { awk -v name="$1" '!/^[[:space:]]*#/ && $1 == name { print $2 }' "${HOSTS_FILE}"; }
all_hosts() { awk '!/^[[:space:]]*#/ && NF >= 2 { print $1 }' "${HOSTS_FILE}"; }

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

dry() { [[ ${LSW_GPU_TEST_DRY:-0} == 1 ]]; }

on() {
    local target
    target="$(target_of "$1")"
    if dry; then
        printf '[dry-run] on %s (%s):\n' "$1" "${target}"
        sed 's/^/    /'
    elif [[ ${target} == local ]]; then
        (cd ~ && bash -s)
    else
        ssh "${SSH_OPTS[@]}" "${target}" bash -s
    fi
}

destination() {
    local target
    target="$(target_of "$1")"
    if [[ ${target} == local ]]; then echo "${HOME}/$2"; else echo "${target}:$2"; fi
}

sync_to() {
    if dry; then
        printf '[dry-run] rsync %s\n' "${*: -2:1} -> ${*: -1}"
    else
        rsync -e "ssh ${SSH_OPTS[*]}" "$@"
    fi
}

control_path() { echo "${LSW_GPU_TEST_CONTROL_PATH:-/tmp/lsw-gpu-test-%C}"; }

master_open() {
    local target
    target="$(target_of "$1")"
    [[ ${target} == local ]] && return 0
    ssh -O check -o "ControlPath=$(control_path)" "${target}" >/dev/null 2>&1
}

master_hint() {
    printf 'no ssh master is open for %s. Open one yourself (it asks for the password), then run again:\n' "$1"
    printf '  ! ssh -o ControlMaster=yes -o "ControlPath=%s" -o ControlPersist=10m -fN %s\n' "$(control_path)" "$(target_of "$1")"
}

tree_of() {
    if [[ $(target_of "$1") == local ]]; then echo "${LSW_GPU_TEST_LOCAL_TREE:-${ROOT}}"; else echo "${BASE}/tree"; fi
}

last_run() {
    [[ -f ${RESULTS}/last-run ]] || die "no run started yet"
    cat "${RESULTS}/last-run"
}

run_hosts() {
    [[ -f ${RESULTS}/$1/hosts ]] || die "no run $1 in ${RESULTS}"
    cat "${RESULTS}/$1/hosts"
}

cmd_build() {
    say "Building LSW: agent, manager, mirror, accel-virt"
    "${ROOT}/scripts/build.sh"
    cmake --build "${ROOT}/build-agent" --target remotewin-wgcbench
    say "Building lsw-suites"
    "${ROOT}/scripts/build-suites.sh"
    say "Building lsw-render-ref"
    "${ROOT}/scripts/build-render-ref.sh"
    say "Building accel-virt's test programs"
    if [[ ! -f ${AV}/build/CMakeCache.txt ]]; then
        cmake -S "${AV}" -B "${AV}/build" -G Ninja
    fi
    cmake --build "${AV}/build" --target avtest avd3d11samples_host avd3d12samples_host \
        avglsamples_host avclsamples_host av_device_loss_layer
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

bundle_paths() (
    local qemu=build/accel-virt/qemu
    cd "${ROOT}"
    echo scripts/gpu-test
    echo ${qemu}/qemu/qemu-system-x86_64
    echo ${qemu}/qemu/qemu-bundle/usr/local/share/qemu
    find ${qemu} -maxdepth 1 \( -name '*.so' -o -name av-renderer \)
    echo build/accel-virt/d3d/lib
    echo build/av-drivers
    echo build/src/host/lsw-manager
    echo build/remotewin/source/host/remotewin
    echo build-agent/src/agent/lsw-guest-agent.exe
    ls build-agent/remotewin/source/guest/remotewin-guest.exe 2>/dev/null || true
    echo build-agent/remotewin/source/guest/remotewin-wgcbench.exe
    echo build-suites/lsw-suites64.exe
    echo build-suites32/lsw-suites32.exe
    echo build-render-ref/lsw-render-ref
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
    bundle_paths > "${list}" 2>/dev/null || dry || die "the build outputs are incomplete: run gpu-test.sh build first"
    while read -r path; do
        if [[ ! -e ${ROOT}/${path} ]]; then
            dry && say "(dry-run) ${path} is missing: gpu-test.sh build would have to run first" && continue
            die "${path} is missing: run gpu-test.sh build first"
        fi
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

ship_missing_libraries() {
    local host=$1 round missing name path staged
    if dry; then
        say "(dry-run) would ship the shared libraries ${host} lacks for QEMU and the executor"
        return 0
    fi
    staged="$(mktemp -d)"
    for round in 1 2 3 4 5; do
        missing="$(on "${host}" <<EOF
LD_LIBRARY_PATH=${BASE}/tree/libs ldd ${BASE}/tree/build/accel-virt/qemu/qemu/qemu-system-x86_64 \
    ${BASE}/tree/build/accel-virt/qemu/libav_executor.so \$(ls ${BASE}/tree/build-render-ref/lsw-render-ref 2>/dev/null) | awk '/=> not found/ { print \$1 }' | sort -u
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

render_bundle_paths() (
    local qemu=build/accel-virt/qemu
    cd "${ROOT}"
    echo scripts/gpu-test
    echo ${qemu}/qemu/qemu-system-x86_64
    find ${qemu}/qemu -maxdepth 1 -name '*.so'
    echo ${qemu}/qemu/qemu-bundle/usr/local/share/qemu
    echo build-agent/src/agent/lsw-guest-agent.exe
    echo build-agent/_deps/vulkanloader-build/loader/vulkan-1.dll
    echo build-agent32/_deps/vulkanloader-build/loader/vulkan-1.dll
    echo build-suites/lsw-suites64.exe
    ls build-suites32/lsw-suites32.exe 2>/dev/null || true
    echo build-render-ref/lsw-render-ref
    echo external/accel-virt/tools
    echo external/accel-virt/third_party/dxc
)

cmd_deploy_render() {
    [[ -n ${LSW_RENDER_STACK:-} && -x ${LSW_RENDER_STACK}/libav_executor.so && -d ${LSW_RENDER_STACK}/d3dlib \
        && -d ${LSW_RENDER_STACK}/av-drivers/package \
        && -f ${LSW_RENDER_STACK}/qemu-modules/hw-display-virtio-gpu-av.so \
        && -f ${LSW_RENDER_STACK}/qemu-modules/hw-display-virtio-gpu-pci-av.so ]] ||
        die "LSW_RENDER_STACK must name a directory with libav_executor.so, d3dlib/, av-drivers/ and qemu-modules/"
    strings -a "${LSW_RENDER_STACK}/qemu-modules/hw-display-virtio-gpu-av.so" | grep -q offer_hybrid ||
        stack_without_hybrid=1
    split_arguments "$@"
    local list host
    list="$(mktemp)"
    trap 'rm -f "${list}"' RETURN
    render_bundle_paths > "${list}" 2>/dev/null || die "the build outputs are incomplete"
    while read -r path; do
        [[ -e ${ROOT}/${path} ]] || die "${path} is missing"
    done < "${list}"
    for host in "${HOSTS[@]}"; do
        say "Deploying the render stack to ${host}"
        if [[ ${stack_without_hybrid:-0} -eq 1 ]] &&
            [[ $(on "${host}" <<< "virsh -c qemu:///session dumpxml --inactive ${DOMAIN:-lsw} 2>/dev/null | grep -c 'hybrid='" | tail -n 1) -gt 0 ]]; then
            die "${host}'s domain uses hybrid= and the stack's virtio-gpu-av module has no offer_hybrid: it would not start"
        fi
        on "${host}" <<< "mkdir -p ${BASE}/tree/build/accel-virt/qemu ${BASE}/tree/build/accel-virt/d3d ${BASE}/runs"
        sync_to -a -r -L --exclude __pycache__ --files-from="${list}" "${ROOT}/" \
            "$(destination "${host}" "${BASE}/tree/")"
        sync_to -a -L --include='*.so' --exclude='*' "${LSW_RENDER_STACK}/qemu-modules/" \
            "$(destination "${host}" "${BASE}/tree/build/accel-virt/qemu/qemu/")"
        sync_to -a -L "${LSW_RENDER_STACK}/libav_executor.so" \
            "$(destination "${host}" "${BASE}/tree/build/accel-virt/qemu/libav_executor.so")"
        sync_to -a -L --delete "${LSW_RENDER_STACK}/d3dlib/" "$(destination "${host}" "${BASE}/tree/build/accel-virt/d3d/lib/")"
        sync_to -a -L --delete "${LSW_RENDER_STACK}/av-drivers/" "$(destination "${host}" "${BASE}/tree/build/av-drivers/")"
        portable_executables "${host}"
        ship_missing_libraries "${host}"
    done
}

cmd_stack() {
    local action=$1
    shift
    split_arguments "$@"
    local host
    for host in "${HOSTS[@]}"; do
        say "Render stack ${action} on ${host}"
        on "${host}" <<RUN
cd ${BASE}/tree/scripts/gpu-test
if systemd-run --user --quiet --collect true 2>/dev/null; then
    unit=lsw-render-stack-${action}
    systemctl --user reset-failed "\${unit}.service" 2>/dev/null || true
    systemd-run --user --quiet --collect --wait --pipe --unit="\${unit}" --working-directory="\${HOME}/${BASE}/tree/scripts/gpu-test" \
        python3 -B render_stack.py ${action}
else
    python3 -B render_stack.py ${action}
fi
RUN
    done
}

cmd_tunnel() {
    [[ $# -eq 3 ]] || die "tunnel needs a host and two local ports: the agent's and the share's"
    local target
    target="$(target_of "$1")"
    [[ -n ${target} && ${target} != local ]] || die "tunnel needs a remote host"
    ssh -O forward "${SSH_OPTS[@]}" -L "$2:127.0.0.1:5102" -L "$3:127.0.0.1:5445" "${target}"
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

cmd_run() {
    split_arguments "$@"
    local run
    run="${LSW_GPU_TEST_RUN:-$(date +%Y%m%d-%H%M%S)}"
    if ! dry; then
        mkdir -p "${RESULTS}/${run}"
        { cat "${RESULTS}/${run}/hosts" 2>/dev/null || true; printf '%s\n' "${HOSTS[@]}"; } | sort -u > "${RESULTS}/${run}/hosts.new"
        mv "${RESULTS}/${run}/hosts.new" "${RESULTS}/${run}/hosts"
        echo "${run}" > "${RESULTS}/last-run"
    fi
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

cmd_gpus() {
    split_arguments "$@"
    for host in "${HOSTS[@]}"; do
        on "${host}" <<< "python3 -B $(tree_of "${host}")/scripts/gpu-test/gpu_matrix.py gpus"
    done
}

cmd_master() {
    split_arguments "$@"
    local status=0
    for host in "${HOSTS[@]}"; do
        if master_open "${host}"; then
            echo "${host}: ssh master open"
        else
            master_hint "${host}"
            status=1
        fi
    done
    return "${status}"
}

cmd_render_report() {
    local run="${1:-$(last_run)}"
    local root="${RESULTS}/${run}"
    local imported="${root}/render"
    rm -rf "${imported}"
    python3 -B "${ROOT}/scripts/render-report.py" --import-matrix "${root}" --into "${imported}"
    local host
    for host in $(run_hosts "${run}"); do
        [[ -d ${imported}/${host} ]] || continue
        mkdir -p "${root}/render-${host}"
        cp -a "${imported}/${host}" "${root}/render-${host}/"
        python3 -B "${ROOT}/scripts/render-report.py" --tree "${root}/render-${host}" --out "${root}/render-report-${host}.html" \
            --title "Render suite report, ${host}"
    done
    python3 -B "${ROOT}/scripts/render-report.py" --tree "${imported}" --out "${root}/render-report.html" \
        --title "Render suite report, every host"
}

cmd_restore() {
    split_arguments "$@"
    for host in "${HOSTS[@]}"; do
        say "Restoring the VM definition on ${host}"
        on "${host}" <<< "python3 -B ${BASE}/tree/scripts/gpu-test/gpu_matrix.py restore"
    done
}

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

usage() {
    cat <<'EOF'
usage: scripts/gpu-test/gpu-test.sh COMMAND [HOST...] [-- gpu_matrix.py run options]

Builds here, deploys to the test machines listed in scripts/gpu-test/hosts and
runs every graphics API's tests on every GPU of each machine, in its LSW VM.

  build                     LSW and the test programs
  deploy   [HOST...]        rsync the tree, then preflight
  deploy-render [HOST...]   the render suite's stack only (LSW_RENDER_STACK names the executor, d3dlib, av-drivers and qemu-modules; nothing of them comes from this tree's build)
  stack-install [HOST...]   put that stack under the VM: saves the VM's definition, driver package and agent first
  stack-restore [HOST...]   put the saved ones back
  stack-status [HOST...]
  tunnel HOST AGENTPORT SMBPORT   forward the remote guest's agent and share to local ports (needs the ssh master)
  preflight [HOST...]       what each host lacks
  run      [HOST...] [-- OPTIONS]
  status   [RUN]
  wait     [RUN]            until every host finished
  collect  [RUN]            results into build/gpu-test/RUN
  report   [RUN]            the failures, every host and GPU
  render-report [RUN]       HTML reports of the render suite results in a run, per host and combined
  gpus     [HOST...]        the GPUs of each host a run would test
  master   [HOST...]        whether an ssh master is open for each host
  all      [HOST...] [-- OPTIONS]
  restore  [HOST...]        the VM definition a killed run changed
  provision HOST [-- lsw-manager options]
  setup-keys [HOST...]      this machine's ssh key on each host
  traces-sync [HOST...]     the trace store to each host
  traces-pull HOST APP/FUNC a trace recorded on HOST into this store

RUN defaults to the last one started. A run takes the VM over: lsw-manager
must be closed on every host.
EOF
}

[[ $# -ge 1 ]] || { usage; exit 2; }
command=$1
shift
case "${command}" in
    build)      cmd_build "$@" ;;
    deploy)     cmd_deploy "$@" ;;
    deploy-render) cmd_deploy_render "$@" ;;
    stack-install) cmd_stack install "$@" ;;
    stack-restore) cmd_stack restore "$@" ;;
    stack-status) cmd_stack status "$@" ;;
    tunnel)     cmd_tunnel "$@" ;;
    preflight)  cmd_preflight "$@" ;;
    run)        cmd_run "$@" ;;
    status)     cmd_status "$@" || true ;;
    wait)       cmd_wait "$@" ;;
    collect)    cmd_collect "$@" ;;
    report)     cmd_report "$@" ;;
    render-report) cmd_render_report "$@" ;;
    gpus)       cmd_gpus "$@" ;;
    master)     cmd_master "$@" ;;
    all)        cmd_all "$@" ;;
    restore)    cmd_restore "$@" ;;
    provision)  cmd_provision "$@" ;;
    setup-keys) cmd_setup_keys "$@" ;;
    traces-sync) cmd_traces_sync "$@" ;;
    traces-pull) cmd_traces_pull "$@" ;;
    -h|--help)  usage ;;
    *)          die "unknown command '${command}' (see --help)" ;;
esac
