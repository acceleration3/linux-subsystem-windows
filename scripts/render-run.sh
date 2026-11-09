#!/usr/bin/env bash

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HERE="${ROOT}/scripts"
MAIN_TREE="${LSW_MAIN_TREE:-/home/accel/Coding/linux-subsystem-windows}"
AV="${ROOT}/external/accel-virt"
[[ -f ${AV}/third_party/dxc/dxcompiler.dll ]] || AV="${MAIN_TREE}/external/accel-virt"
HOSTS_FILE="${LSW_GPU_TEST_HOSTS:-${ROOT}/scripts/gpu-test/hosts}"
LOCK="${LSW_RENDER_HEAVY_LOCK:-${XDG_RUNTIME_DIR:-/tmp}/lsw-render.lock}"
VM="${LSW_RENDER_VM:-${ROOT}/scripts/render-vm.sh}"
CPU_QUOTA="${LSW_RENDER_CPU_QUOTA:-400%}"
started_vm=0
GPUTEST="${ROOT}/scripts/gpu-test/gpu-test.sh"
STAMP="$(date +%Y%m%d-%H%M%S)"

hosts=local
gpus=all
adapters=all
bits=64
filter=""
jobs=2
dump=1
images=all
build=1
dry=0
list_only=0
baseline=""
out=""
remote_timeout=14400
parallel=0
stack=0
host_jobs=""
guest_timeout=3600
suite=render
hold_file=""
max_recoveries=5

usage() {
    cat <<'EOF'
usage: scripts/render-run.sh [OPTIONS]

Runs the lsw-suites render suite on this machine and/or the test machines of
scripts/gpu-test/hosts, per GPU and bitness, and merges every selected GPU into one
HTML report: build-render-report/<timestamp>/report.html

  --hosts local,test,accel    machines to run on (default local)
  --gpus all|HOST:SEL[,...]   SEL is uuid:HEX, name:SUBSTR or an index from --list-gpus
                              (default: every GPU of every selected host)
  --adapters all|first|list   local only: every VM-bound GPU, the first one, or list them
  --bits 64|32|both           guest executable bitness (default 64)
  --filter GLOB[,GLOB]        check id filter (also limits the host references)
  --jobs N                    parallel checks in the guest, per adapter (default 2)
  --host-jobs HOST=N[,...]    --jobs for single hosts, e.g. local=3,accel=2
  --parallel                  every selected host at the same time, and on each host every adapter at the same time
                              (one lsw-suites process per adapter in the guest)
  --stack                     remote hosts already run the stack (gpu-test.sh deploy-render and stack-install): no deploy
                              and no matrix run; references are made on the host and the guest is driven through ssh
                              forwards of its agent and share (implied by --parallel)
  --suite render|sanity       the suite to run (default render); sanity runs its main phase on every adapter at once and
                              its pacing phase one adapter at a time, writes under <out>/sanity/<host>/<gpu> and makes
                              no references, audit or report
  --hold-file PATH            check ids (one per line) that are never run, e.g. rows that hang the GPU on purpose
  --max-recoveries N          a dead VM is restarted and the run resumed at most N times per host (default 5)
  --guest-timeout SECONDS     limit of one guest run with --parallel (default 3600)
  --no-dump-captures          skip the capture dumps (the report then has no images and there is no audit)
  --images all|differing|failed|none  which rows get images in the report (default all)
  --no-build                  use the executables already built
  --baseline DIR             an earlier build-render-report/<timestamp> to compare against
  --out DIR                   output directory (default build-render-report/<timestamp>)
  --list-gpus                 print each selected host's GPUs and exit
  --dry-run                   print every step, run nothing (remote hosts are never touched)

A remote host needs an open ssh master (passwords, no keys); when there is none the
script prints the command that opens one and skips the host.
EOF
}

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
note() { printf '    %s\n' "$*"; }
die() { printf 'render-run: %s\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --hosts) hosts="$2"; shift ;;
        --gpus) gpus="$2"; shift ;;
        --adapters) adapters="$2"; shift ;;
        --bits) bits="$2"; shift ;;
        --filter) filter="$2"; shift ;;
        --jobs) jobs="$2"; shift ;;
        --host-jobs) host_jobs="$2"; shift ;;
        --parallel) parallel=1; stack=1 ;;
        --stack) stack=1 ;;
        --guest-timeout) guest_timeout="$2"; shift ;;
        --suite) suite="$2"; shift ;;
        --hold-file) hold_file="$2"; shift ;;
        --max-recoveries) max_recoveries="$2"; shift ;;
        --no-dump-captures) dump=0 ;;
        --images) images="$2"; shift ;;
        --no-build) build=0 ;;
        --baseline) baseline="$2"; shift ;;
        --out) out="$2"; shift ;;
        --list-gpus) list_only=1 ;;
        --dry-run) dry=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; die "unknown option $1" ;;
    esac
    shift
done

case "${bits}" in 64|32|both) ;; *) die "--bits needs 64, 32 or both" ;; esac
case "${adapters}" in all|first|list) ;; *) die "--adapters needs all, first or list" ;; esac
[[ ${adapters} == list ]] && list_only=1
[[ -n ${out} ]] || out="${ROOT}/build-render-report/${STAMP}"
bit_list=(64 32)
[[ ${bits} != both ]] && bit_list=("${bits}")

target_of() { awk -v name="$1" '!/^[[:space:]]*#/ && $1 == name { print $2 }' "${HOSTS_FILE}"; }

if [[ -f ${ROOT}/external/accel-virt/tools/host/list_gpus.py ]]; then
    export LSW_GPU_TEST_LOCAL_TREE="${ROOT}"
else
    export LSW_GPU_TEST_LOCAL_TREE="${MAIN_TREE}"
fi

step() {
    local text=$1
    shift
    if [[ ${dry} -eq 1 ]]; then
        printf '[dry-run] %s\n' "${text}"
        return 0
    fi
    say "${text}"
    "$@"
}

locked() {
    local seconds=$1
    shift
    flock -w 3600 "${LOCK}" timeout "${seconds}" "$@"
}

capped() {
    if command -v systemd-run >/dev/null 2>&1; then
        systemd-run --user --scope -q -p "CPUQuota=${CPU_QUOTA}" nice -n 10 ionice -c 3 "$@"
    else
        nice -n 10 ionice -c 3 "$@"
    fi
}

capped_step() {
    local text=$1 seconds=$2
    shift 2
    locked_step "${text}" "${seconds}" bash -c "$(declare -f capped); CPU_QUOTA='${CPU_QUOTA}'; "'capped "$@"' _ "$@"
}

locked_step() {
    local text=$1 seconds=$2
    shift 2
    if [[ ${dry} -eq 1 ]]; then
        printf '[dry-run] (heavy lock, %ss) %s\n' "${seconds}" "${text}"
        return 0
    fi
    say "${text}"
    locked "${seconds}" "$@"
}

gpu_table() {
    LSW_GPU_TEST_DRY=0 "${GPUTEST}" gpus "$1" 2>/dev/null | awk '/^ *[0-9]+  [0-9a-f]{8}-/ {
        name = ""; for (i = 6; i <= NF; i++) name = name (i > 6 ? " " : "") $i
        sub(/^[A-Za-z]+ /, "", name)
        printf "%s|%s|%s|%s|%s\n", $1, $2, $3, $4, name }'
}

domain_gpus() {
    virsh -c qemu:///session dumpxml lsw 2>/dev/null | grep -o 'host-gpu=[0-9a-f-]*' | cut -d= -f2 | awk '!seen[$0]++'
}

slug() { echo "$1" | sed -E 's/\([^)]*\)//g; s/[^A-Za-z0-9]+/-/g; s/^-+|-+$//g' | tr 'A-Z' 'a-z' | cut -c1-40; }
short_name() { echo "$1" | sed -E 's/ *\([^)]*\) *$//'; }

declare -A SEL_UUIDS
declare -A SEL_NAMES
host_list=()

selected_by() {
    local host=$1 selector=$2 line idx uuid
    local found=()
    while IFS='|' read -r idx uuid _ _ name; do
        case "${selector}" in
            all) found+=("${uuid}") ;;
            uuid:*) [[ ${uuid//-/} == "${selector#uuid:}" || ${uuid} == "${selector#uuid:}" ]] && found+=("${uuid}") ;;
            name:*) [[ ${name,,} == *"${selector#name:}"* ]] && found+=("${name:+${uuid}}") ;;
            *) [[ ${selector} == "${idx}" ]] && found+=("${uuid}") ;;
        esac
    done <<< "${TABLE[${host}]}"
    printf '%s\n' "${found[@]}"
}

declare -A TABLE

collect_hosts() {
    local wanted=() entry h
    IFS=',' read -ra wanted <<< "${hosts}"
    declare -A per_host
    if [[ ${gpus} != all ]]; then
        IFS=',' read -ra entries <<< "${gpus}"
        for entry in "${entries[@]}"; do
            h="${entry%%:*}"
            [[ -n $(target_of "${h}") ]] || die "--gpus names host '${h}', which is not in ${HOSTS_FILE}"
            per_host[${h}]+="${entry#*:} "
            [[ " ${wanted[*]} " == *" ${h} "* ]] || wanted+=("${h}")
        done
    fi
    for h in "${wanted[@]}"; do
        [[ -n $(target_of "${h}") ]] || die "no host '${h}' in ${HOSTS_FILE}"
        if [[ $(target_of "${h}") != local ]] && ! "${GPUTEST}" master "${h}" >/dev/null 2>&1; then
            "${GPUTEST}" master "${h}" | sed 's/^/render-run: /' >&2
            printf 'render-run: skipping %s\n' "${h}" >&2
            if [[ ${dry} -eq 1 ]]; then
                printf '[dry-run] %s is not reachable now; this is what a run on it would do (nothing is sent):\n' "${h}"
                LSW_GPU_TEST_DRY=1 "${GPUTEST}" deploy "${h}" 2>&1 | sed 's/^/    /'
                LSW_GPU_TEST_DRY=1 LSW_GPU_TEST_RUN="render-${STAMP}" "${GPUTEST}" run "${h}" -- --suites suites --gpus "<selected uuids of ${h}>" --bits "${bits}" \
                    --suite-args "--suite render --jobs ${jobs}${filter:+ --filter ${filter}}$([[ ${dump} -eq 1 ]] && echo ' --dump-captures')" 2>&1 | sed 's/^/    /'
                printf '[dry-run]     then: gpu-test.sh wait, collect, then render-report.py --import-matrix build/gpu-test/render-%s --into %s\n' "${STAMP}" "${out}"
            fi
            continue
        fi
        TABLE[${h}]="$(gpu_table "${h}")"
        if [[ -z ${TABLE[${h}]} ]]; then
            printf 'render-run: could not list the GPUs of %s, skipping it\n' "${h}" >&2
            continue
        fi
        local selectors=(all) chosen=()
        [[ ${gpus} != all ]] && read -ra selectors <<< "${per_host[${h}]:-}"
        if [[ ${h} == local || $(target_of "${h}") == local ]]; then
            local bound
            bound="$(domain_gpus)"
            local filtered=""
            local idx uuid vd ty name
            while IFS='|' read -r idx uuid vd ty name; do
                if grep -qx -- "${uuid}" <<< "${bound}" || [[ ${bound} == auto ]]; then
                    filtered+="${idx}|${uuid}|${vd}|${ty}|${name}"$'\n'
                fi
            done <<< "${TABLE[${h}]}"
            TABLE[${h}]="${filtered%$'\n'}"
            [[ ${adapters} == first && ${gpus} == all ]] && TABLE[${h}]="$(head -n 1 <<< "${TABLE[${h}]}")"
        fi
        local s u
        for s in "${selectors[@]}"; do
            while IFS= read -r u; do
                [[ -n ${u} ]] && chosen+=("${u}")
            done < <(selected_by "${h}" "${s}")
        done
        [[ ${#chosen[@]} -gt 0 ]] || { printf 'render-run: no GPU of %s matches the selection\n' "${h}" >&2; continue; }
        SEL_UUIDS[${h}]="$(printf '%s\n' "${chosen[@]}" | awk '!seen[$0]++' | tr '\n' ' ')"
        host_list+=("${h}")
    done
}

print_gpus() {
    local h idx uuid vd ty name n
    for h in "${host_list[@]}"; do
        n=0
        while IFS='|' read -r idx uuid vd ty name; do
            [[ -n ${uuid} ]] || continue
            local mark=" "
            [[ " ${SEL_UUIDS[${h}]} " == *" ${uuid} "* ]] && mark="*"
            if [[ $(target_of "${h}") == local ]]; then
                printf '%s %-6s #%s uuid:%s %s  %s  guest adapter %s (domain order; run --list-adapters in the guest for LUID)\n' "${mark}" "${h}" "${idx}" "${uuid//-/}" "${vd}" "${name}" "${n}"
            else
                printf '%s %-6s #%s uuid:%s %s  %s  guest adapter: the matrix binds this GPU alone, so it is the first\n' "${mark}" "${h}" "${idx}" "${uuid//-/}" "${vd}" "${name}"
            fi
            n=$((n + 1))
        done <<< "${TABLE[${h}]}"
    done
}

gpu_name_of() {
    local host=$1 uuid=$2 idx u vd ty name
    while IFS='|' read -r idx u vd ty name; do
        [[ ${u} == "${uuid}" ]] && { short_name "${name}"; return; }
    done <<< "${TABLE[${host}]}"
    echo "${uuid}"
}

vm_running() { [[ $(virsh -c qemu:///session domstate lsw 2>/dev/null | head -n 1) == running ]]; }

finish() {
    if [[ ${started_vm} -eq 1 ]]; then
        locked 400 "${VM}" down
    fi
}

guest_locked() {
    local seconds=$1
    shift
    locked "${seconds}" bash -c 'vm=$1; shift; "${vm}" up || exit 1; "$@"; rc=$?; "${vm}" done; exit ${rc}' _ "${VM}" "$@"
}

guest_step() {
    local text=$1 seconds=$2
    shift 2
    if [[ ${dry} -eq 1 ]]; then
        printf '[dry-run] (heavy lock, %ss, render-vm.sh up ... done) %s\n' "${seconds}" "${text}"
        return 0
    fi
    say "${text}"
    guest_locked "${seconds}" "$@"
}

run_local() {
    local h=$1 uuids=(${SEL_UUIDS[$1]}) refs="${out}/refs" uuid
    local all_selected=0
    [[ ${#uuids[@]} -eq $(domain_gpus | wc -l) && ${adapters} != first ]] && all_selected=1
    export RR_LOG="${out}/render-ref.log"
    local ref_filter=()
    [[ -n ${filter} ]] && ref_filter=(--filter "${filter}")
    if [[ ${all_selected} -eq 1 ]]; then
        capped_step "host references for every VM-bound GPU" 1800 bash -c 'exec "$@" >> "${RR_LOG}" 2>&1' _ "${ROOT}/build-render-ref/lsw-render-ref" --out "${refs}" --gpu all --jobs 4 --clean "${ref_filter[@]}"
        [[ ${dry} -eq 1 ]] || tail -n 4 "${out}/render-ref.log"
    else
        for uuid in "${uuids[@]}"; do
            capped_step "host references for ${uuid}" 1800 bash -c 'exec "$@" >> "${RR_LOG}" 2>&1' _ "${ROOT}/build-render-ref/lsw-render-ref" --out "${refs}/${uuid//-/}" --gpu "uuid:${uuid}" --jobs 4 --clean "${ref_filter[@]}"
            [[ ${dry} -eq 1 ]] || tail -n 3 "${out}/render-ref.log"
        done
    fi
    local exes=()
    for b in "${bit_list[@]}"; do
        local dir="build-suites"
        [[ ${b} == 32 ]] && dir="build-suites32"
        exes+=(--exe "${b}=${ROOT}/${dir}/lsw-suites${b}.exe")
    done
    guest_step "push executables, dxc and references to C:\\av\\render-run" 1500 python3 "${HERE}/render-run-guest.py" push "${exes[@]}" --dxc "${AV}/third_party/dxc" --refs "${refs}" || return 1
    local gpu_name base b adapter_arg run_args
    for uuid in "${uuids[@]}"; do
        gpu_name="$(gpu_name_of "${h}" "${uuid}")"
        for b in "${bit_list[@]}"; do
            base="${out}/${h}/$(slug "${gpu_name}")/x${b}"
            adapter_arg="uuid:${uuid//-/}"
            [[ ${adapters} == first ]] && adapter_arg="first"
            run_args=(--bits "${b}" --adapter "${adapter_arg}" --jobs "${jobs}" --tag "$(slug "${gpu_name}")-x${b}" --out "${base}")
            [[ -n ${filter} ]] && run_args+=(--filter "${filter}")
            [[ ${dump} -eq 1 ]] && run_args+=(--dump)
            guest_step "render suite x${b} on ${gpu_name} (${adapter_arg})" 1500 python3 "${HERE}/render-run-guest.py" run "${run_args[@]}"
            [[ ${dry} -eq 1 ]] && continue
            mkdir -p "${base}"
            local ref_dir="${refs}/${uuid//-/}"
            [[ -f ${ref_dir}/device.json ]] || ref_dir="${refs}"
            local label_name
            label_name="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("adapter",{}).get("name",""))' "${base}/result.json" 2>/dev/null)"
            printf '{"host": "%s", "gpu": "%s", "bits": "%s", "refs": "%s"}\n' "${h}" "${label_name:-${gpu_name}}" "${b}" "${ref_dir}" > "${base}/meta.json"
            if [[ ${dump} -eq 1 && -d ${base}/artifacts && -f ${base}/result.json ]]; then
                say "audit of ${gpu_name} x${b}"
                timeout 900 python3 -I "${HERE}/render-audit.py" --guest "${base}/artifacts" --json "${base}/result.json" \
                    --out "${base}/audit.md" --flags "${base}/audit.json" > "${base}/audit.txt" 2>&1 || note "render-audit exit $? (see ${base}/audit.txt)"
            fi
        done
    done
}

run_remote() {
    local h=$1 run="render-${STAMP}" uuids list
    uuids=(${SEL_UUIDS[$1]})
    list="$(IFS=,; echo "${uuids[*]}")"
    local suite_args="--suite render --jobs ${jobs}"
    [[ -n ${filter} ]] && suite_args+=" --filter ${filter}"
    [[ ${dump} -eq 1 ]] && suite_args+=" --dump-captures"
    export LSW_GPU_TEST_RUN="${run}"
    local dry_env=0
    [[ ${dry} -eq 1 ]] && dry_env=1
    say "${h}: deploy"
    LSW_GPU_TEST_DRY=${dry_env} "${GPUTEST}" deploy "${h}" || { printf 'render-run: deploy to %s failed, skipping it\n' "${h}" >&2; return 1; }
    say "${h}: matrix run on ${list}"
    LSW_GPU_TEST_DRY=${dry_env} "${GPUTEST}" run "${h}" -- --suites suites --suite-args "${suite_args}" --gpus "${list}" --bits "${bits}" || return 1
    if [[ ${dry} -eq 1 ]]; then
        printf '[dry-run] then: gpu-test.sh wait %s; collect %s; render-report.py --import-matrix build/gpu-test/%s --into %s\n' "${run}" "${run}" "${run}" "${out}"
        return 0
    fi
    timeout "${remote_timeout}" "${GPUTEST}" wait "${run}" || { printf 'render-run: %s did not finish in %ss\n' "${h}" "${remote_timeout}" >&2; }
    "${GPUTEST}" collect "${run}"
    python3 -B "${HERE}/render-report.py" --import-matrix "${ROOT}/build/gpu-test/${run}" --into "${out}"
}

jobs_of() {
    local entry
    IFS=',' read -ra entries <<< "${host_jobs}"
    for entry in "${entries[@]}"; do
        [[ ${entry%%=*} == "$1" ]] && { echo "${entry#*=}"; return; }
    done
    echo "${jobs}"
}

ssh_opts=(-o BatchMode=yes -o ConnectTimeout=10 -o "ControlPath=${LSW_GPU_TEST_CONTROL_PATH:-/tmp/lsw-gpu-test-%C}")
on_remote() { ssh "${ssh_opts[@]}" "$(target_of "$1")" bash -s; }

remote_references() {
    local h=$1 refs=$2 ref_filter=$3
    on_remote "${h}" <<RUN
set -e
cd ~/.local/share/lsw-gpu-test/tree
rm -rf ~/.local/share/lsw-gpu-test/render-refs
build-render-ref/lsw-render-ref --out ~/.local/share/lsw-gpu-test/render-refs --gpu all --jobs 4 --clean ${ref_filter} > ~/.local/share/lsw-gpu-test/render-ref.log 2>&1
tail -n 4 ~/.local/share/lsw-gpu-test/render-ref.log
RUN
    mkdir -p "${refs}"
    rsync -a -e "ssh ${ssh_opts[*]}" --exclude '*.png' --exclude .logs --exclude '*.bgra' \
        "$(target_of "${h}"):.local/share/lsw-gpu-test/render-refs/" "${refs}/"
}

effective_filter() {
    local text="${filter}" id
    if [[ -n ${hold_file} ]]; then
        while IFS= read -r id; do
            [[ -n ${id} ]] && text+="${text:+,}-${id}"
        done < "${hold_file}"
    fi
    printf '%s' "${text}"
}

finish_adapter() {
    local h=$1 uuid=$2 refs=$3 b=$4 base=$5 gpu_name=$6
    mkdir -p "${base}"
    [[ ${suite} == render ]] || return 0
    local ref_dir="${refs}/${uuid//-/}"
    [[ -f ${ref_dir}/device.json ]] || ref_dir="${refs}"
    local label_name
    label_name="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("adapter",{}).get("name",""))' "${base}/result.json" 2>/dev/null)"
    printf '{"host": "%s", "gpu": "%s", "bits": "%s", "refs": "%s"}\n' "${h}" "${label_name:-${gpu_name}}" "${b}" "${ref_dir}" > "${base}/meta.json"
    if [[ ${dump} -eq 1 && -d ${base}/artifacts && -f ${base}/result.json ]]; then
        timeout 900 python3 -I "${HERE}/render-audit.py" --guest "${base}/artifacts" --json "${base}/result.json" \
            --out "${base}/audit.md" --flags "${base}/audit.json" > "${base}/audit.txt" 2>&1 || note "${h}: render-audit exit $? (see ${base}/audit.txt)"
    fi
}

adapter_dir() {
    local h=$1 gpu_name=$2 b=$3
    if [[ ${suite} == sanity ]]; then
        echo "${out}/sanity/${h}/$(slug "${gpu_name}")"
    else
        echo "${out}/${h}/$(slug "${gpu_name}")/x${b}"
    fi
}

guest_runs() {
    local h=$1 jobs_here=$2 agent_port=$3 smb_port=$4 vm=$5 refs=$6
    shift 6
    local uuids=("$@") uuid b gpu_name base status=0
    local phases=all
    [[ ${suite} == sanity ]] && phases="main:parallel,pacing:serial"
    for b in "${bit_list[@]}"; do
        local runs=()
        for uuid in "${uuids[@]}"; do
            gpu_name="$(gpu_name_of "${h}" "${uuid}")"
            runs+=(--run "$(slug "${gpu_name}")-x${b}|uuid:${uuid//-/}|$(adapter_dir "${h}" "${gpu_name}" "${b}")")
        done
        local args=(--bits "${b}" --suite "${suite}" --jobs "${jobs_here}" --timeout "${guest_timeout}"
                    --agent-port "${agent_port}" --smb-port "${smb_port}" --vm "${vm}" --host "${h}" --root "${out}"
                    --evidence "${out}/evidence/${h}-x${b}" --max-recoveries "${max_recoveries}" --phases "${phases}")
        local effective
        effective="$(effective_filter)"
        [[ -n ${effective} ]] && args+=(--filter "${effective}")
        [[ ${dump} -eq 1 ]] && args+=(--dump)
        say "${h}: ${suite} suite x${b} on ${#uuids[@]} adapter(s) at once, jobs ${jobs_here} each"
        python3 "${HERE}/render-run-guest.py" run-set "${args[@]}" "${runs[@]}" || status=1
        for uuid in "${uuids[@]}"; do
            gpu_name="$(gpu_name_of "${h}" "${uuid}")"
            finish_adapter "${h}" "${uuid}" "${refs}" "${b}" "$(adapter_dir "${h}" "${gpu_name}" "${b}")" "${gpu_name}"
        done
    done
    return "${status}"
}

remote_lock() {
    coproc ACCEL_LOCK { ssh "${ssh_opts[@]}" "$(target_of "$1")" bash -s; }
    printf 'exec 9>/tmp/lsw-accel-vm.lock; flock -w 3600 9 || exit 1; echo locked; cat > /dev/null\n' >&"${ACCEL_LOCK[1]}"
    local line=""
    read -r -t 3700 line <&"${ACCEL_LOCK[0]}"
    [[ ${line} == locked ]] || { printf 'render-run: could not take /tmp/lsw-accel-vm.lock on %s\n' "$1" >&2; return 1; }
}

remote_unlock() {
    exec {ACCEL_LOCK[1]}>&-
    wait "${ACCEL_LOCK_PID}" 2>/dev/null
}

watch_memory() {
    local h=$1 log=$2
    while true; do
        { date +%T; on_remote "${h}" <<< "free -m | sed -n 2p; grep -E '^(pswpin|pswpout) ' /proc/vmstat | tr '\\n' ' '; echo"; } >> "${log}" 2>&1
        sleep 30
    done
}

run_host_parallel() {
    local h=$1 index=$2 uuids=(${SEL_UUIDS[$1]}) refs="${out}/refs-$1" jobs_here
    local agent_port=5102 smb_port=5445 ref_filter="" vm=local status=0
    jobs_here="$(jobs_of "${h}")"
    [[ -n ${filter} ]] && ref_filter="--filter ${filter}"
    local exes=() b
    for b in "${bit_list[@]}"; do
        local dir="build-suites"
        [[ ${b} == 32 ]] && dir="build-suites32"
        exes+=(--exe "${b}=${ROOT}/${dir}/lsw-suites${b}.exe")
    done
    local push_refs=(--refs "${refs}")
    if [[ $(target_of "${h}") == local ]]; then
        if [[ ${suite} == render ]]; then
            export RR_LOG="${out}/render-ref-${h}.log"
            capped_step "host references for every VM-bound GPU" 1800 bash -c 'exec "$@" >> "${RR_LOG}" 2>&1' _ "${ROOT}/build-render-ref/lsw-render-ref" --out "${refs}" --gpu all --jobs 4 --clean ${ref_filter} || return 1
        else
            push_refs=()
        fi
        local lockfd
        exec {lockfd}> "${LOCK}"
        flock -w 3600 "${lockfd}" || return 1
        "${VM}" up || return 1
    else
        vm="ssh:$(target_of "${h}")"
        agent_port=$((15102 + 10 * index))
        smb_port=$((15445 + 10 * index))
        if [[ ${suite} == render ]]; then
            say "${h}: host references"
            remote_references "${h}" "${refs}" "${ref_filter}" || return 1
        else
            push_refs=()
        fi
        "${GPUTEST}" tunnel "${h}" "${agent_port}" "${smb_port}" || note "${h}: the forward could not be added (already there?)"
        remote_lock "${h}" || return 1
        watch_memory "${h}" "${out}/memory-${h}-${suite}.log" &
        local watcher=$!
    fi
    say "${h}: push executables, dxc and references"
    python3 "${HERE}/render-run-guest.py" push "${exes[@]}" --dxc "${AV}/third_party/dxc" "${push_refs[@]}" \
        --agent-port "${agent_port}" --smb-port "${smb_port}" || status=1
    if [[ ${status} -eq 0 ]]; then
        guest_runs "${h}" "${jobs_here}" "${agent_port}" "${smb_port}" "${vm}" "${refs}" "${uuids[@]}" || status=1
    fi
    [[ -n ${watcher:-} ]] && kill "${watcher}" 2>/dev/null
    [[ -n ${lockfd:-} ]] && { "${VM}" done; eval "exec ${lockfd}>&-"; }
    [[ $(target_of "${h}") == local ]] || remote_unlock
    return "${status}"
}

run_parallel() {
    local logs="${out}/logs" h index=0 pids=() names=()
    mkdir -p "${logs}"
    for h in "${host_list[@]}"; do
        say "starting ${h} (log ${logs}/${h}.log)"
        run_host_parallel "${h}" "${index}" > "${logs}/${h}.log" 2>&1 &
        pids+=($!)
        names+=("${h}")
        index=$((index + 1))
    done
    local status=0 n
    for n in "${!pids[@]}"; do
        wait "${pids[n]}" || { printf 'render-run: %s failed, see %s/%s.log\n' "${names[n]}" "${logs}" "${names[n]}" >&2; status=1; }
    done
    return "${status}"
}

TABLE_DONE=1
collect_hosts
if [[ ${list_only} -eq 1 ]]; then
    [[ ${#host_list[@]} -gt 0 ]] || die "no host with GPUs"
    print_gpus
    exit 0
fi
[[ ${#host_list[@]} -gt 0 ]] || die "nothing to run: no host is available"
say "plan"
print_gpus
note "bits: ${bit_list[*]}, filter: ${filter:-none}, jobs: ${jobs}, output: ${out}"

mkdir -p "${out}"
if [[ ${dry} -eq 0 ]] && ! vm_running; then
    started_vm=1
    trap finish EXIT
fi
if [[ ${build} -eq 1 ]]; then
    export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
    export LSW_SUITES_KHRONOS_DIR="${LSW_SUITES_KHRONOS_DIR:-${AV}/third_party/khronos}"
    export LSW_SUITES_BITS="${bits}"
    capped_step "build lsw-suites (${bits})" 1200 "${ROOT}/scripts/build-suites.sh" || die "suites build failed"
    capped_step "build lsw-render-ref" 1200 "${ROOT}/scripts/build-render-ref.sh" || die "lsw-render-ref build failed"
fi

status=0
if [[ ${parallel} -eq 1 ]]; then
    if [[ ${dry} -eq 1 ]]; then
        for h in "${host_list[@]}"; do
            printf '[dry-run] %s: references, push, then one guest run per adapter at once (%s adapters, jobs %s each)\n' "${h}" "$(wc -w <<< "${SEL_UUIDS[${h}]}")" "$(jobs_of "${h}")"
        done
    else
        run_parallel || status=1
    fi
else
for h in "${host_list[@]}"; do
    if [[ $(target_of "${h}") == local ]]; then
        run_local "${h}" || status=1
    else
        run_remote "${h}" || status=1
    fi
done
fi

if [[ ${dry} -eq 1 ]]; then
    printf '[dry-run] render-report.py --tree %s%s --out %s/report.html --images %s\n' "${out}" "${baseline:+ --baseline-tree ${baseline}}" "${out}" "${images}"
    exit 0
fi
report_args=(--tree "${out}" --out "${out}/report.html" --images "${images}")
[[ -n ${baseline} ]] && report_args+=(--baseline-tree "${baseline}")
if [[ ${suite} == render ]]; then
    python3 -B "${HERE}/render-report.py" "${report_args[@]}" || status=1
else
    echo "sanity results: ${out}/sanity"
    exit "${status}"
fi
echo "report: ${out}/report.html"
exit "${status}"
